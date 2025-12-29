//
// ds1307.c
//
// driver for DS1307/37/38/39/40/41, ST M41T00, EPSON RX-8025, ISL12057 RTC chip(s)
//

#include "usb/rtc.h"
#include "rtc/ds1307.h"

#define DS1307_ADDR     0x68

#define REG_SECS        0x00    /* 00-59 */
#define REG_MINUTES     0x01    /* 00-59 */
#define REG_HOURS       0x02    /* 00-23, or 1-12{am,pm} */
    #define HOURS_BIT_PM    0x20    /* in REG_HOUR */
    #define HOURS_BIT_12HR  0x40    /* in REG_HOUR */
#define REG_WEEKDAY     0x03    /* 01-07 */
#define REG_DAYS        0x04    /* 01-31 */
#define REG_MONTHS      0x05    /* 01-12 */
#define REG_YEARS       0x06    /* 00-99 */
#define REG_CONTROL     0x07

static uint8_t ds1307_probe(usb_device_t *dev, const i2c_bus_t *i2c)
{
    uint8_t ctrl = 0;

    return i2c->bulk_read(dev, DS1307_ADDR, REG_CONTROL, &ctrl, 1)
        && (ctrl & 3) == 3;
}

static uint8_t ds1307_get_time(usb_device_t *dev, const i2c_bus_t *i2c, timestamp_t date)
{
    uint8_t regs[7];

    uint8_t ret = i2c->bulk_read(dev, DS1307_ADDR, REG_SECS, regs, 7);
    if (!ret)
        return ret;

    date[0] = bcd2bin(regs[REG_YEARS]) + 100;
    date[1] = bcd2bin(regs[REG_MONTHS]);
    date[2] = bcd2bin(regs[REG_DAYS]);
    date[3] = bcd2bin(regs[REG_HOURS] & 0x3f);
    date[4] = bcd2bin(regs[REG_MINUTES]);
    date[5] = bcd2bin(regs[REG_SECS] & 0x7f);
    date[6] = regs[REG_WEEKDAY];

    return 1;
}

static uint8_t ds1307_set_time(usb_device_t *dev, const i2c_bus_t *i2c, timestamp_t date)
{
    uint8_t regs[7];

    regs[REG_YEARS] = bin2bcd(date[0] % 100);
    regs[REG_MONTHS] = bin2bcd(date[1]);
    regs[REG_DAYS] = bin2bcd(date[2]);
    regs[REG_HOURS] = bin2bcd(date[3]) & ~HOURS_BIT_12HR;
    regs[REG_MINUTES] = bin2bcd(date[4]);
    regs[REG_SECS] = bin2bcd(date[5]);
    regs[REG_WEEKDAY] = date[6];

    return i2c->bulk_write(dev, DS1307_ADDR, REG_SECS, regs, 7);
}

const rtc_chip_t rtc_ds1307_chip = {
    .name = "ds1307",
    .probe = ds1307_probe,
    .get_time = ds1307_get_time,
    .set_time = ds1307_set_time,
};
