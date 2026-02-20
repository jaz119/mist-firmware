#ifndef STATE_H
#define STATE_H

#include <inttypes.h>
#include "attrs.h"

//// type definitions ////

typedef struct {
	uint16_t vid;				// USB vendor ID
	uint16_t pid;				// USB product ID
	uint8_t  num_buttons; 		// number of physical buttons reported by HID parsing
	uint8_t  state;   			// virtual joystick: current state of 4 direction + 4 first buttons
	uint8_t  state_extra;  		// current state of 8 more buttons
	uint8_t  right;			 	// right stick state
	uint8_t  usb_state;			// raw USB state of direction and buttons
	uint8_t  usb_state_extra;	// raw USB state of 8 more buttons
	uint8_t  analogue[4];
	uint8_t  menu_button;
} mist_joystick_t;

extern mist_joystick_t mist_joysticks[7];

/*****
 * Various functions to retrieve hardware state from the State
 */

void StateReset();

 // USB raw data for joystick
static inline void StateUsbJoySet(uint8_t usbjoy, uint8_t usbextra, uint8_t joy_num) {
	if (joy_num > 5) return;
	mist_joysticks[joy_num].usb_state = usbjoy;
	mist_joysticks[joy_num].usb_state_extra = usbextra;
}

static inline void StateUsbIdSet(uint16_t vid, uint16_t pid, uint8_t num, uint8_t joy_num) {
	if (joy_num > 5) return;
	mist_joysticks[joy_num].vid = vid;
	mist_joysticks[joy_num].pid = pid;
	mist_joysticks[joy_num].num_buttons = num;
}

static inline uint8_t StateUsbJoyGet(uint8_t joy_num) {
	return (joy_num < 6) ? mist_joysticks[joy_num].usb_state : 0;
}

static inline uint8_t StateUsbJoyGetExtra(uint8_t joy_num) {
	return (joy_num < 6) ? mist_joysticks[joy_num].usb_state_extra : 0;
}

static inline uint8_t StateUsbGetNumButtons(uint8_t joy_num) {
	return (joy_num < 6) ? mist_joysticks[joy_num].num_buttons : 0;
}

static inline uint16_t StateUsbVidGet(uint8_t joy_num) {
	return (joy_num < 6) ? mist_joysticks[joy_num].vid : 0;
}

static inline uint16_t StateUsbPidGet(uint8_t joy_num) {
	return (joy_num < 6) ? mist_joysticks[joy_num].pid : 0;
}

// State of first (virtual) internal joystick i.e. after mapping
static inline void StateJoySet(uint8_t c, uint8_t joy_num) {
	if (joy_num > 5) return;
	mist_joysticks[joy_num].state = c;
}

static inline void StateJoySetExtra(uint8_t c, uint8_t joy_num) {
	if (joy_num > 5) return;
	mist_joysticks[joy_num].state_extra = c;
}

static inline void StateJoySetRight(uint8_t c, uint8_t joy_num) {
	if (joy_num > 5) return;
	mist_joysticks[joy_num].right = c;
}

static inline uint8_t StateJoyGet(uint8_t joy_num) {
	return (joy_num < 6) ? mist_joysticks[joy_num].state : 0;
}

static inline uint8_t StateJoyGetExtra(uint8_t joy_num) {
	return (joy_num < 6) ? mist_joysticks[joy_num].state_extra : 0;
}

static inline  uint8_t StateJoyGetRight(uint8_t joy_num) {
	return (joy_num < 6) ? mist_joysticks[joy_num].right : 0;
}

static inline void StateJoySetAnalogue(uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry, uint8_t joy_num) {
	if (joy_num > 5) return;
	mist_joysticks[joy_num].analogue[0] = lx;
	mist_joysticks[joy_num].analogue[1] = ly;
	mist_joysticks[joy_num].analogue[2] = rx;
	mist_joysticks[joy_num].analogue[3] = ry;
}

static inline uint8_t StateJoyGetAnalogue(uint8_t idx, uint8_t joy_num) {
	return (joy_num < 6 && idx < 4) ? mist_joysticks[joy_num].analogue[idx] : 0;
}

// Keep track of connected sticks
extern uint8_t num_joysticks;

static inline uint8_t StateNumJoysticks() {
	return num_joysticks;
}

static inline void StateNumJoysticksSet(uint8_t num) {
	num_joysticks = num;
}

static inline void StateJoySetMenu(uint8_t c, uint8_t joy_num) {
	if (joy_num > 5) return;
	mist_joysticks[joy_num].menu_button = c;
}

static inline uint8_t StateJoyGetMenu(uint8_t joy_num) {
	return (joy_num < 6) ? mist_joysticks[joy_num].menu_button : 0;
}

uint8_t StateJoyGetMenuAny();

// keyboard status
extern uint8_t key_modifier;

// get usb and ps2 codes
FORCE_ARM void StateKeyboardSet(
	uint8_t modifier, uint8_t* pressed, uint16_t* pressed_ps2);

static inline uint8_t StateKeyboardModifiers() {
	return key_modifier;
}

void StateKeyboardPressed(uint8_t *pressed);
void StateKeyboardPressedPS2(uint16_t *keycodes);

#endif // STATE_H
