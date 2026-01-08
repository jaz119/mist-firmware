//
// pcf85x63.c
//
// driver for PCF85263/PCF85363 RTC chips
//

#include "usb.h"
#include "rtc/pcf85x63.h"

#define PCF85x63_ADDR   0x51

// date/time registers
#define REG_100THS      0x00
#define REG_SECS        0x01
#define REG_MINUTES     0x02
#define REG_HOURS       0x03
#define REG_DAYS        0x04
#define REG_WEEKDAY     0x05
#define REG_MONTHS      0x06
#define REG_YEARS       0x07

// control registers
#define CTRL_OSCILLATOR 0x25
    #define OSC_BIT_12HR        0x20
#define CTRL_FUNCTION   0x28
    #define CTRL_BIT_COF_OFF    0x07
#define CTRL_STOP_EN    0x2e
    #define STOP_BIT_EN_STOP    0x01
#define CTRL_RESET      0x2f
    #define RESET_CPR           0xa4

static bool pcf85x63_probe(usb_device_t *dev, const i2c_bus_t *i2c)
{
    uint8_t cof = CTRL_BIT_COF_OFF;

    // disable clock output for check and battery save
    return i2c->bulk_write(dev, PCF85x63_ADDR, CTRL_FUNCTION, &cof, 1);
}

static bool pcf85x63_get_time(usb_device_t *dev, const i2c_bus_t *i2c, ctime_t date)
{
    uint8_t regs[REG_YEARS + 1];

    if (!i2c->bulk_read(dev, PCF85x63_ADDR, REG_100THS, regs, sizeof(regs)))
        return false;

    date[0] = bcd2bin(regs[REG_YEARS]) + 100;
    date[1] = bcd2bin(regs[REG_MONTHS]);
    date[2] = bcd2bin(regs[REG_DAYS]);
    date[3] = bcd2bin(regs[REG_HOURS] & 0x3f);
    date[4] = bcd2bin(regs[REG_MINUTES] & 0x7F);
    date[5] = bcd2bin(regs[REG_SECS] & 0x7F);
    date[6] = regs[REG_WEEKDAY];

    return true;
}

static bool pcf85x63_set_time(usb_device_t *dev, const i2c_bus_t *i2c, const ctime_t date)
{
    uint8_t buf[12];
    uint8_t *regs = &buf[2], zero = 0;

    buf[0] = STOP_BIT_EN_STOP;
    buf[1] = RESET_CPR;

    regs[REG_100THS] = 0;
    regs[REG_YEARS]  = bin2bcd(date[0] % 100);
    regs[REG_MONTHS] = bin2bcd(date[1]);
    regs[REG_DAYS]   = bin2bcd(date[2]);
    regs[REG_HOURS]  = bin2bcd(date[3]);
    regs[REG_MINUTES] = bin2bcd(date[4]);
    regs[REG_SECS]    = bin2bcd(date[5]);
    regs[REG_WEEKDAY] = date[6];

    // stop and clear prescaler
    if (!i2c->bulk_write(dev, PCF85x63_ADDR, CTRL_STOP_EN, buf, 2))
        return false;

    // set new time
    if (!i2c->bulk_write(dev, PCF85x63_ADDR, REG_100THS, regs, 8))
        return false;

    // start
    return i2c->bulk_write(dev, PCF85x63_ADDR, CTRL_STOP_EN, &zero, 1);
}

const rtc_chip_t rtc_pcf85x63_chip = {
    .name = "pcf85x63", 400,
    .probe = pcf85x63_probe,
    .get_time = pcf85x63_get_time,
    .set_time = pcf85x63_set_time,
};
