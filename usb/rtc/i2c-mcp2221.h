#ifndef USB_RTC_MCP2221_H
#define USB_RTC_MCP2221_H

/*
 * RTC driver
 * for MCP2221(A) USB/I2S bridge chip
 */

#include "usb/rtc.h"

// runtime device info
typedef struct {
    ep_t ep_in;
    ep_t ep_out;
    uint8_t chip_type;
    uint16_t i2c_clock;
    uint32_t last_poll_time;
} mcp_rtc_info_t;

// i2c bus interface
typedef struct {
    bool (*bulk_read)(usb_device_t *, uint8_t addr, uint8_t reg, uint8_t *, uint8_t);
    bool (*bulk_write)(usb_device_t *, uint8_t addr, uint8_t reg, uint8_t *, uint8_t);
} i2c_bus_t;

// rtc chip interface
typedef struct {
    char name[8];
    uint16_t max_i2c_clock;  // clock rate, in kHz
    bool (*probe)(usb_device_t *, const i2c_bus_t *);
    bool (*get_time)(usb_device_t *, const i2c_bus_t *, ctime_t);
    bool (*set_time)(usb_device_t *, const i2c_bus_t *, const ctime_t);
} rtc_chip_t;

// interface for usb core
extern const usb_rtc_class_config_t usb_rtc_i2c_mcp2221_class;

#endif // USB_RTC_MCP2221_H
