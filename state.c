/*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

This code keeps status of MiST state
*/

#include "state.h"

#define joy_init { \
	.vid = 0, \
	.pid = 0, \
	.num_buttons=1, \
	.state=0 , \
	.state_extra=0, \
	.right=0, \
	.usb_state=0, \
	.usb_state_extra=0, \
	.analogue={0,0,0,0}, \
	.menu_button=0, \
	}

/* latest joystick state */
ALIGNED(4) mist_joystick_t mist_joysticks[7] = {
	joy_init,
	joy_init,
	joy_init,
	joy_init,
	joy_init,
	joy_init,
	// 7th one is dummy, used to store defaults
	joy_init
};

// De-init all joysticks, useful when changing core
void StateReset() {
	for(int idx=0; idx<6; idx++) {
		StateJoySet(0, idx);
		StateJoySetExtra(0, idx);
		StateJoySetRight(0, idx);
		StateJoySetAnalogue(0, 0, 0, 0, idx);
		StateJoySetMenu(0, idx);
		StateUsbIdSet(0, 0, 0, idx);
		StateUsbJoySet(0, 0, idx);
	}
}

/* latest joystick state */
static uint8_t mist_joystick_menu;

uint8_t StateJoyGetMenuAny() {
	for(int joy_num=0; joy_num<6; joy_num++) {
		if (mist_joysticks[joy_num].menu_button)
			return mist_joysticks[joy_num].menu_button;
	}
	return 0;
}

// Keep track of connected sticks
uint8_t num_joysticks=0;

/* keyboard data */
uint8_t key_modifier = 0;
ALIGNED(4) static uint8_t key_pressed[6] = { 0,0,0,0,0,0 };
ALIGNED(4) static uint16_t keys_ps2[6] = { 0,0,0,0,0,0 };

void StateKeyboardPressedPS2(uint16_t *keycodes) {
	for(int i=0; i<6; i++) {
		keycodes[i]=keys_ps2[i];
	}
}

FORCE_ARM void StateKeyboardSet(
	uint8_t modifier, uint8_t* keycodes, uint16_t* keycodes_ps2) {
	unsigned i=0,j=0;
	key_modifier = modifier;
	for(i=0; i<6; i++) {
		// iprintf("Key N=%d, USB=%x, PS2=%x\n", i, keycodes[i], keycodes_ps2[i]);
		if(((keycodes[i]&0xFF) != 0xFF) && (keycodes[i]&0xFF)) {
			key_pressed[j]=keycodes[i];
			if((keycodes_ps2[i]&0xFF) != 0xFF ) {
				// translate EXT into 0E
				if(0x1000 & keycodes_ps2[i]) {
					keys_ps2[j++] = (keycodes_ps2[i]&0xFF) | 0xE000;
				} else {
					keys_ps2[j++] = keycodes_ps2[i]&0xFF;
				}
			} else {
				keys_ps2[j++] = 0;
			}
		}
	}

	while(j<6) {
		key_pressed[j]=0;
		keys_ps2[j++]=0;
	}
}

void StateKeyboardPressed(uint8_t *keycodes) {
	for(int i=0; i<6; i++)
		keycodes[i]=key_pressed[i];
}
