#ifndef I2C_TINY_H
#define I2C_TINY_H

/*
 * RTC driver
 * for i2c-tiny-usb by Till Harbaum
 */

#include <stdbool.h>
#include <inttypes.h>
#include "rtc.h"

// interface to usb core
extern const usb_rtc_class_config_t i2c_tiny_rtc_class;

#endif // I2C_TINY_H
