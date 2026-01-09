//
// ds3231.c
//
// RTC driver for Maxim DS3231
//

#include "usb.h"
#include "rtc/ds3231.h"

#define DS3231_ADDR     0x68

// date/time registers
#define REG_SECS        0x00
#define REG_MINUTES     0x01
#define REG_HOURS       0x02
    #define HOURS_BIT_12HR      0x40
#define REG_WEEKDAY     0x03
#define REG_DAYS        0x04
#define REG_MONTHS      0x05
    #define MONTHS_BIT_CENTURY  0x80
#define REG_YEARS       0x06

// control registers
#define REG_CR          0x0E
#define REG_SR          0x0F

static bool ds3231_probe(usb_device_t *dev, const i2c_bus_t *i2c)
{
    uint8_t crsr[2] = { 0, 0 };

    if (i2c->bulk_read(dev, DS3231_ADDR, REG_CR, crsr, 2))
        return ((crsr[0] & 0x1c) == 0x1c && (crsr[1] & 0x80) == 0x80);

    return false;
}

static bool ds3231_get_time(usb_device_t *dev, const i2c_bus_t *i2c, ctime_t date)
{
    uint8_t regs[7];

    if (!i2c->bulk_read(dev, DS3231_ADDR, REG_SECS, regs, 7))
        return false;

    date[0] = bcd2bin(regs[REG_YEARS]);

    if (regs[REG_MONTHS] & MONTHS_BIT_CENTURY)
        date[0] += 100;

    date[1] = bcd2bin(regs[REG_MONTHS] & 0x7f);
    date[2] = bcd2bin(regs[REG_DAYS]);
    date[3] = bcd2bin(regs[REG_HOURS] & 0x3f);
    date[4] = bcd2bin(regs[REG_MINUTES]);
    date[5] = bcd2bin(regs[REG_SECS]);
    date[6] = regs[REG_WEEKDAY];

    return true;
}

static bool ds3231_set_time(usb_device_t *dev, const i2c_bus_t *i2c, const ctime_t date)
{
    uint8_t regs[7], zero = 0;

    regs[REG_MONTHS] = bin2bcd(date[1]);

    if (date[0] >= 100)
        regs[REG_MONTHS] |= MONTHS_BIT_CENTURY;

    regs[REG_YEARS] = bin2bcd(date[0] % 100);
    regs[REG_DAYS] = bin2bcd(date[2]);
    regs[REG_HOURS] = bin2bcd(date[3]);
    regs[REG_MINUTES] = bin2bcd(date[4]);
    regs[REG_SECS] = bin2bcd(date[5]);
    regs[REG_WEEKDAY] = date[6];

    if (i2c->bulk_write(dev, DS3231_ADDR, REG_SECS, regs, 7))
    {
        // clear the OSF, and time is valid now
        return i2c->bulk_write(dev, DS3231_ADDR, REG_SR, &zero, 1);
    }

    return false;
}

const rtc_chip_t rtc_ds3231_chip = {
    .name = "DS3231", 400,
    .probe = ds3231_probe,
    .get_time = ds3231_get_time,
    .set_time = ds3231_set_time,
};
