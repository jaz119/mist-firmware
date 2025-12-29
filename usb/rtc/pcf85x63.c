//
// pcf85x63.c
//
// driver for PCF85263/PCF85363 RTC chip(s)
//

#include "usb/rtc.h"
#include "rtc/pcf85x63.h"

#define PCF85x63_ADDR   0xa2

/*
 * Date/Time registers
 */
#define REG_100THS      0x00
#define REG_SECS        0x01
#define REG_MINUTES     0x02
#define REG_HOURS       0x03
#define REG_DAYS        0x04
#define REG_WEEKDAY     0x05
#define REG_MONTHS      0x06
#define REG_YEARS       0x07
#define MAX_REG_NUM     0x2f

/*
 * Control registers
 */
#define CTRL_OSCILLATOR 0x25
    #define OSC_BIT_12_24    0x20
#define CTRL_STOP_EN    0x2e
    #define STOP_BIT_EN_STOP 0x01
#define CTRL_RESET      0x2f
    #define RESET_CPR        0xa4

static uint8_t pcf85x63_probe(usb_device_t *dev, const i2c_bus_t *i2c)
{
    uint8_t rst = -1;

    /* no any strong way to check chip present */
    return i2c->bulk_read(dev, PCF85x63_ADDR, MAX_REG_NUM, &rst, 1)
        && rst == 0;
}

static uint8_t pcf85x63_get_time(usb_device_t *dev, const i2c_bus_t *i2c, timestamp_t date)
{
    uint8_t regs[REG_YEARS + 1];

    uint8_t ret = i2c->bulk_read(dev, PCF85x63_ADDR, REG_100THS, regs, sizeof(regs));
    if (!ret)
        return ret;

    date[0] = bcd2bin(regs[REG_YEARS]) + 100;
    date[1] = bcd2bin(regs[REG_MONTHS]);
    date[2] = bcd2bin(regs[REG_DAYS]);
    date[3] = bcd2bin(regs[REG_HOURS] & 0x3f);
    date[4] = bcd2bin(regs[REG_MINUTES] & 0x7F);
    date[5] = bcd2bin(regs[REG_SECS] & 0x7F);
    date[6] = regs[REG_WEEKDAY];

    return 1;
}

static uint8_t pcf85x63_set_time(usb_device_t *dev, const i2c_bus_t *i2c, timestamp_t date)
{
    uint8_t buf[10];
    uint8_t *regs = &buf[2];

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

    // STOP and Clear prescaler
    if (i2c->bulk_write(dev, PCF85x63_ADDR, CTRL_STOP_EN, buf, 2))
    {
        // Set TIME
        if (i2c->bulk_write(dev, PCF85x63_ADDR, REG_100THS, regs, 8))
        {
            // START
            return i2c->bulk_write(dev, PCF85x63_ADDR, CTRL_STOP_EN, 0, 1);
        }
    }

    return 0;
}

const rtc_chip_t rtc_pcf85x63_chip = {
    .name = "pcf85x63",
    .probe = pcf85x63_probe,
    .get_time = pcf85x63_get_time,
    .set_time = pcf85x63_set_time,
};
