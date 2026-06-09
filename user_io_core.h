/*
 * user_io_core.h
 */

#ifndef USER_IO_CORE_H
#define USER_IO_CORE_H

#include <stdint.h>
#include <stdbool.h>

// fpga core support interface
typedef struct user_io_core_t {
    void (*init)();
    void (*poll)();
    void (*reset)(bool cold_boot);
    uint16_t (*keycode)(uint8_t key);
    uint16_t (*modify_keycode)(uint8_t key);
    void (*send_keycode)(uint16_t);
    void (*send_mouse)(uint8_t idx, uint8_t b, int8_t X, int8_t Y, int8_t Z);
    void (*send_analog_joy)(uint8_t joy, int LX, int LY, int RX, int RY);
    void (*send_digital_joy)(uint8_t joy, uint32_t map);
    void (*setup_menu)();
    void (*eject_all)();
    const char *name;
} user_io_core_t;

#endif // USER_IO_CORE_H
