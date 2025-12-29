//
// ds3231.c
//
// driver for Maxim DS3231 RTC chip
//

#include "usb/rtc.h"
#include "rtc/ds3231.h"

#define DS3231_ADDR     0x68

#define REG_SECS        0x00
#define REG_MINUTES     0x01
#define REG_HOURS       0x02
    #define HOURS_BIT_12HR      0x40    /* in REG_HOUR */
#define REG_WEEKDAY     0x03
#define REG_DAYS        0x04
#define REG_MONTHS      0x05
#define REG_YEARS       0x06

#define REG_CR          0x0E    /* Control register */
#define REG_SR          0x0F    /* Control/Status register */

static uint8_t ds3231_probe(usb_device_t *dev, const i2c_bus_t *i2c)
{
    uint8_t crsr[2] = { 0, 0 };

    if (i2c->bulk_read(dev, DS3231_ADDR, REG_CR, crsr, 2))
        return ((crsr[0] & 0x1c) == 0x1c && (crsr[1] & 0x88) == 0x88);

    return 0;
}

static uint8_t ds3231_get_time(usb_device_t *dev, const i2c_bus_t *i2c, timestamp_t date)
{
    uint8_t regs[7];

    uint8_t ret = i2c->bulk_read(dev, DS3231_ADDR, REG_SECS, regs, 7);
    if (!ret)
        return ret;

    if (regs[REG_MONTHS] & 0x80) {
        date[0] = bcd2bin(regs[REG_YEARS]) + 100;
    } else {
        date[0] = bcd2bin(regs[REG_YEARS]);
    }

    date[1] = bcd2bin(regs[REG_MONTHS] & 0x7f);
    date[2] = bcd2bin(regs[REG_DAYS]);
    date[3] = bcd2bin(regs[REG_HOURS] & 0x3f);
    date[4] = bcd2bin(regs[REG_MINUTES]);
    date[5] = bcd2bin(regs[REG_SECS]);
    date[6] = regs[REG_WEEKDAY];

    return 1;
}

static uint8_t ds3231_set_time(usb_device_t *dev, const i2c_bus_t *i2c, timestamp_t date)
{
    uint8_t regs[7];

    regs[REG_MONTHS] = bin2bcd(date[1]);

    if (date[0] >= 100) {
        regs[REG_MONTHS] |= 0x80;
        regs[REG_YEARS] = bin2bcd(date[0] - 100);
    } else {
        regs[REG_YEARS] = bin2bcd(date[0]);
    }

    regs[REG_DAYS] = bin2bcd(date[2]);
    regs[REG_HOURS] = bin2bcd(date[3]) & ~HOURS_BIT_12HR;
    regs[REG_MINUTES] = bin2bcd(date[4]);
    regs[REG_SECS] = bin2bcd(date[5]);
    regs[REG_WEEKDAY] = date[6];

    return i2c->bulk_write(dev, DS3231_ADDR, REG_SECS, regs, 7);
}

const rtc_chip_t rtc_ds3231_chip = {
    .name = "ds3231",
    .probe = ds3231_probe,
    .get_time = ds3231_get_time,
    .set_time = ds3231_set_time,
};
