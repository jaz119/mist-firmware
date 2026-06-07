/*
 * user_io_hid.h
 */

#ifndef USER_IO_HID_H
#define USER_IO_HID_H

#include <stdint.h>
#include <stdbool.h>
#include "utils.h"

#define MOUSE_FREQ      20  // 20 ms -> 50hz
#define KBD_LED_FREQ    100 // 100 ms

// virtual d-pad
#define JOY_RIGHT       BIT(0)
#define JOY_LEFT        BIT(1)
#define JOY_DOWN        BIT(2)
#define JOY_UP          BIT(3)

#define JOY_BTN_SHIFT   4

// virtual gamepad buttons
#define JOY_A           BIT(4)
#define JOY_B           BIT(5)
#define JOY_SELECT      BIT(6)
#define JOY_START       BIT(7)
#define JOY_X           BIT(8)
#define JOY_Y           BIT(9)
#define JOY_L           BIT(10)
#define JOY_R           BIT(11)
#define JOY_L2          BIT(12)
#define JOY_R2          BIT(13)
#define JOY_L3          BIT(14)
#define JOY_R3          BIT(15)

// right stick
#define JOY_RIGHT2      BIT(16)
#define JOY_LEFT2       BIT(17)
#define JOY_DOWN2       BIT(18)
#define JOY_UP2         BIT(19)

#define JOY_BTN1        JOY_A
#define JOY_BTN2        JOY_B
#define JOY_BTN3        JOY_SELECT
#define JOY_BTN4        JOY_START

#define UIO_PRIORITY_KEYBOARD   0
#define UIO_PRIORITY_GAMEPAD    1

extern int kbd_reset;
extern int ps2_kbd_scan_set;
extern unsigned char ps2_typematic_rate;

extern uint32_t mouse_flags[2];
extern int32_t mouse_pos[2][3];
extern uint32_t mouse_timer;

// for user_io
void user_io_hid_init();
void user_io_hid_reset();
void user_io_hid_poll();
void user_io_check_reset(uint16_t modifiers, char useKeys);

// for HID polling
void user_io_kbd(uint8_t m, uint8_t *k, uint8_t priority);
void user_io_mouse(uint8_t idx, uint8_t b, int8_t x, int8_t y, int8_t z);
void user_io_analog_joystick(uint8_t, int, int, int, int);
void user_io_digital_joystick(uint8_t, uint32_t);
uint8_t user_io_swap_joystick(uint8_t);

// generic core support
void send_keycode_ps2(uint16_t code);
void send_mouse_ps2(uint8_t idx, uint8_t b, char x, char y, char z);
void send_analog_joystick(uint8_t joy, int LX, int LY, int RX, int RY);
void send_digital_joystick(uint8_t joy, uint32_t map);

uint16_t keycode_ps2(uint8_t key);
uint16_t modify_keycode_ps2(uint8_t key);

void handle_kbd_commands_ps2();
void handle_typematic_repeat_ps2();

void handle_mouse_commands_ps2();
void handle_mouse_events_ps2();

// for config read
char user_io_key_remap(const char *, char, int);
void add_modifiers(uint8_t mod, uint16_t *keys);

#endif // USER_IO_HID_H
