/*
  This file is part of MiST-firmware

  MiST-firmware is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  MiST-firmware is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
This file defines how to handle mapping in the MiST controllers in various ways:

	1) USB input to internal "virtual joystick" (standardizes inputs)
	2) Virtual joystick to keyboard
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "user_io.h"
#include <user_io_hid.h>
#include <joymapping.h>
#include "hidquirks.h"
#include "mist_cfg.h"
#include "debug.h"

// Up to 3 device remap profiles
#define MAX_VIRTUAL_JOYSTICK_REMAP	3
#define MAX_JOYSTICK_KEYBOARD_MAP	16

/*****************************************************************************\
   Virtual joystick remap - custom parsing
   The mapping translates directions plus generic HID buttons (1-12) into a sandard MiST "virtual joystick"
\****************************************************************************/

static joymapping_t joystick_mappers[MAX_VIRTUAL_JOYSTICK_REMAP];

static const uint16_t default_joystick_mapping[16] = {
	JOY_RIGHT,
	JOY_LEFT,
	JOY_DOWN,
	JOY_UP,
	JOY_A,
	JOY_B,
	JOY_SELECT,
	JOY_START,
	JOY_X,
	JOY_Y,
	JOY_L,
	JOY_R,
	JOY_L2,
	JOY_R2,
	JOY_L3,
	JOY_R3
};

static int idx = 0;

void virtual_joystick_remap_init(char save) {
  if(save)
    idx = 0;
  else
    memset(joystick_mappers, 0, sizeof(joystick_mappers));
}

/* Parses an input comma-separated string into a mapping strucutre
   The string is expected to have the following format:  [VID],[PID],[comma separated list of buttons]
   and requires at least 13  characters in length
*/

char virtual_joystick_remap(char *s, char action, int tag) {

  uint32_t count;
  uint8_t len = strlen(s);
  uint16_t value = 0;
  uint16_t pid, vid;
  char *token;

  // save entry to string
  if(action == INI_SAVE) {
    hid_debugf("%s(tag: %d)", __FUNCTION__, tag);

    while(1) {
      if (idx == MAX_VIRTUAL_JOYSTICK_REMAP || joystick_mappers[idx].vid == 0)
        return 0;
      if(joystick_mappers[idx].tag == tag) {
        siprintf(s, "%04X,%04X", joystick_mappers[idx].vid, joystick_mappers[idx].pid);
        for (count=0; count<16; count++) {
          char hex[16];
          siprintf(hex, ",%X", joystick_mappers[idx].mapping[count]);
          strcat(s, hex);
        }
        idx++;
        return 1;
      }
      idx++;
    }
  }

  hid_debugf("%s(%s)", __FUNCTION__, s);

  // load entry from string
  if(len < 13) {
    hid_debugf("malformed entry");
    return 0;
  }

  token  = strtok (s, ",");
  if (!token) {
    hid_debugf("no vid");
    return 0;
  }
  vid = strtol(token, NULL, 16);
  if (vid==0) {
    hid_debugf("invalid vid");
    return 0; // invalid vid
  }

  token  = strtok (NULL, ",");
  if (!token) {
    hid_debugf("no pid");
    return 0;
  }
  pid = strtol(token, NULL, 16);
  if (pid==0) {
    hid_debugf("invalid pid");
    return 0; // invalid vid
  }

  // parse remap request
  for(uint32_t i=0; i<MAX_VIRTUAL_JOYSTICK_REMAP; i++) {
    // update if the same vid/pid/tag found, or use the first empty slot
    if((joystick_mappers[i].vid == vid &&
        joystick_mappers[i].pid == pid &&
        joystick_mappers[i].tag == tag) ||
       !joystick_mappers[i].vid) {
      // init mapping data
      for (count=0; count<16; count++)
        joystick_mappers[i].mapping[count]=0;

      joystick_mappers[i].vid = vid;
      joystick_mappers[i].pid = pid;
      joystick_mappers[i].tag = tag;
      // default assignment for directions
      joystick_mappers[i].mapping[0] = JOY_RIGHT;
      joystick_mappers[i].mapping[1] = JOY_LEFT;
      joystick_mappers[i].mapping[2] = JOY_DOWN;
      joystick_mappers[i].mapping[3] = JOY_UP;
      count  = 0;
      token  = strtok (NULL, ",");
      while(token!=NULL) {
        value = strtol(token, NULL, 16);
        if (count < 16) {
            //parse sub-tokens sequentially and assign 16-bit value to them
            joystick_mappers[i].mapping[count] = value;
            hid_debugf("parsed: 0x%x/0x%x %lu -> %d",
                      joystick_mappers[i].vid, joystick_mappers[i].pid,
                      count, joystick_mappers[i].mapping[count]);
        }
        token = strtok (NULL, ",");
        count++;
      }
      return 0; // finished processing input string so exit
    }
  }
  return 0;
}

void virtual_joystick_remap_update(joymapping_t *map) {
	for(uint32_t i=0; i<MAX_VIRTUAL_JOYSTICK_REMAP; i++) {
		if((joystick_mappers[i].vid == map->vid
		  && joystick_mappers[i].pid == map->pid
		  && joystick_mappers[i].tag == map->tag)
		  || !joystick_mappers[i].vid) {
			memcpy(&joystick_mappers[i], map, sizeof(joymapping_t));
			return;
		}
	}
}

void virtual_joystick_tag_update(uint16_t vid, uint16_t pid, int newtag)
{
	// first search for the entry to update with the largest tag
	int old = -1, new = -1, oldtag = 0;
	for(uint32_t i=0; i<MAX_VIRTUAL_JOYSTICK_REMAP; i++) {
		if(joystick_mappers[i].vid == vid
		  && joystick_mappers[i].pid == pid
		  && joystick_mappers[i].tag >= oldtag) {
			old = i;
		}
	}
	if (old == -1) return; // old entry not found

	// now search if the entry with the same newtag already there
	for(uint32_t i=0; i<MAX_VIRTUAL_JOYSTICK_REMAP; i++) {
		if(joystick_mappers[i].vid == vid
		  && joystick_mappers[i].pid == pid
		  && joystick_mappers[i].tag == newtag) {
			new = i;
			break;
		}
	}

	if (new == -1) {
		// no entry with the same tag, simply update
		joystick_mappers[old].tag = newtag;
	} else if (new != old) {
		memcpy(&joystick_mappers[new].mapping, &joystick_mappers[old].mapping, 16*sizeof(uint16_t));
		// delete the old entry
		for(uint32_t i = old; i<MAX_VIRTUAL_JOYSTICK_REMAP; i++) {
			if (i == (MAX_VIRTUAL_JOYSTICK_REMAP-1)) {
				memset(&joystick_mappers[i], 0, sizeof(joymapping_t));
			} else {
				memcpy(&joystick_mappers[i], &joystick_mappers[i+1].mapping, sizeof(joymapping_t));
			}
		}
	}
}

/*****************************************************************************/

/*
 * Translates USB input into internal virtual joystick
 */

FORCE_ARM uint16_t virtual_joystick_mapping(
	uint16_t vid, uint16_t pid, uint16_t joy_input, const joy_remap_t *remap ) {

	// no events - no work
	if (!joy_input) return 0;

	// defines translations between physical buttons and virtual joysticks
	uint16_t mapping[16];

	// Init all by defaults
	for (int i = 0; i < 16; i++) {
		mapping[i] = default_joystick_mapping[i];
	}

	// Apply remap quirks for common/known gampads
	if (remap && remap->count) {
		for (int n=0; n<remap->count; n++) {
			uint8_t i = remap->btn[n].idx;
			if (i < 16) {
				mapping[i] = remap->btn[n].value;
			}
		}
	}

	// Apply remap information from various config sources if present
	// Priority (low to high):
	// 0 - mist.ini
	// 1 - mistcfg.ini
	// 2 - [corename].cfg
	// 3 - Menu
	int tag = 0;
	for (int j=0; j<MAX_VIRTUAL_JOYSTICK_REMAP; j++) {
		if (joystick_mappers[j].vid == vid
		  && joystick_mappers[j].pid == pid
		  && joystick_mappers[j].tag >= tag) {
			for (int i=0; i<16; i++)
				mapping[i] = joystick_mappers[j].mapping[i];
			tag = joystick_mappers[j].tag + 1;
		}
	}

	// Get map of pressed buttons
	uint16_t vjoy = 0;
	for (int i=0; i<16; i++)
		if (joy_input & BIT(i))
			vjoy |= mapping[i];

	hid_debugf("%s: 0x%04x => 0x%04x", __FUNCTION__, joy_input, vjoy);
	return vjoy;
}

/*****************************************************************************\
   Virtual joystick to Keyboard mapping
   binds different button states of internal joypad to verious key combinations
\****************************************************************************/

/*****************************************************************************/

/*  Custom parsing for joystick->keyboard map
    We bind a bitmask of the virtual joypad with a keyboard USB code
*/

static struct {
    uint16_t mask;
    uint8_t modifier;
    uint8_t keys[6];  // support up to 6 key codes
} joy_key_map[MAX_JOYSTICK_KEYBOARD_MAP];

void joy_key_map_init(void) {
  memset(joy_key_map, 0, sizeof(joy_key_map));
}

char joystick_key_map(char *s, char action, int tag) {
  uint32_t count;
  uint32_t assign = 0;
  uint32_t len = strlen(s);
  uint32_t scancode=0;
  char *token;

  hid_debugf("%s(%s)", __FUNCTION__, s);

  if(action == INI_SAVE) return 0;

  if(len < 3) {
    hid_debugf("malformed entry");
    return 0;
  }

  // parse remap request
  for(uint32_t i=0; i<MAX_JOYSTICK_KEYBOARD_MAP; i++) {
    // fill sequentially the available mapping slots, stopping at first empty one
    if(!joy_key_map[i].mask) {
      joy_key_map[i].modifier = 0;
      for(uint32_t j=0; j<6; j++)
        joy_key_map[i].keys[j] = 0;
      count = 0;
      assign = 0;
      token = strtok (s, ",");
      while(token != NULL) {
        if (count==0) {
            joy_key_map[i].mask = strtol(token, NULL, 16);
          } else {
          scancode = strtol(token, NULL, 16);
          // set as modifier if scancode is on the relevant range (224 to 231)
          if(scancode >= 224 && scancode <= 231) {
            //  bit  0     1      2    3    4     5      6    7
            //  key  LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
            //
            joy_key_map[i].modifier |= BIT(scancode - 224);
          } else {
            // max 6 keys
            if (assign < 6)
              joy_key_map[i].keys[assign++] = scancode;
          }
        }
        token = strtok(NULL, ",");
        count+=1;
      }
      return 0; // finished processing input string so exit
    }
  }
  return 0;
}

/*****************************************************************************/

bool virtual_joystick_keyboard( uint16_t vjoy ) {
	// ignore if globally switched off
	if (mist_cfg.joystick_disable_shortcuts)
		return false;

	// use button combinations as shortcut for certain keys
	uint8_t buf[6] = { 0,0,0,0,0,0 };

	// if OSD is open control it via USB joystick
	if (user_io_osd_is_visible() && !mist_cfg.joystick_ignore_osd) {
		int idx = 0;
		if(vjoy & JOY_A)     buf[idx++] = 0x28; // ENTER
		if(vjoy & JOY_B)     buf[idx++] = 0x29; // ESC
		if(vjoy & JOY_START) buf[idx++] = 0x45; // F12
		if(vjoy & JOY_LEFT)  buf[idx++] = 0x50; // left arrow
		if(vjoy & JOY_RIGHT) buf[idx++] = 0x4F; // right arrow

		// up and down uses SELECT or L for faster scrolling

		if (vjoy & JOY_UP) {
			if (vjoy & JOY_SELECT || vjoy & JOY_L) buf[idx] = 0x4B; // page up
			else buf[idx] = 0x52; // up arrow
			if (idx < 6) idx++; //avoid overflow if we assigned 6 already
		}

		if (vjoy & JOY_DOWN) {
			if (vjoy & JOY_SELECT || vjoy & JOY_L) buf[idx] = 0x4E; // page down
			else buf[idx] = 0x51; // down arrow
			if (idx < 6) idx++; //avoid overflow if we assigned 6 already
		}

		if (!(vjoy & JOY_UP) && !(vjoy & JOY_DOWN)) {
			if (vjoy & JOY_L) buf[idx++] = 0x56;// KP-
			else
			if (vjoy & JOY_R) buf[idx++] = 0x57;// KP+
		}

	} else {

		// shortcuts mapped if start is pressed (take priority)
		if (vjoy & JOY_START) {
			//iprintf("joy2key START is pressed\n");
			int idx = 0;
			if (vjoy & JOY_A)       buf[idx++] = 0x28; // ENTER
			if (vjoy & JOY_B)       buf[idx++] = 0x2C; // SPACE
			if (vjoy & JOY_L)       buf[idx++] = 0x29; // ESC
			if (vjoy & JOY_R)       buf[idx++] = 0x3A; // F1
			if (vjoy & JOY_SELECT)  buf[idx++] = 0x45;  //F12 // i.e. open OSD in most cores
		} else {

			// shortcuts with SELECT - mouse emulation
			if (vjoy & JOY_SELECT) {
			//iprintf("joy2key SELECT is pressed\n");
				unsigned char but = 0;
				char a0 = 0;
				char a1 = 0;
				if (vjoy & JOY_L)   but |= 1;
				if (vjoy & JOY_R)   but |= 2;
				if (vjoy & JOY_LEFT) a0 = -4;
				if (vjoy & JOY_RIGHT) a0 = 4;
				if (vjoy & JOY_UP) a1 = -2;
				if (vjoy & JOY_DOWN) a1 = 2;
				user_io_mouse(0, but, a0, a1, 0);
			}

		}
	}

	// process mapped keyboard commands from mist.ini
	uint8_t mapped_hit = 0;
	uint8_t modifier = 0;
	//uint8_t joy_buf[6] = { 0,0,0,0,0,0 };
	for (uint32_t i=0; i<MAX_JOYSTICK_KEYBOARD_MAP; i++) {
		if (vjoy & joy_key_map[i].mask) {
			if (joy_key_map[i].modifier) {
				modifier |= joy_key_map[i].modifier;
			}
			// only override up to 6 keys,
			// and preserve overrides from further up this function
			for (uint32_t j=0; j<6; j++) {
				if (idx >= 6) break; // max keys reached
				if (joy_key_map[i].keys[j]) {
					buf[idx++] = joy_key_map[i].keys[j];
				}
			}
		}
	}

	// generate key events
	user_io_kbd(modifier, buf, UIO_PRIORITY_GAMEPAD);

	return (buf[0] ? true : false);
}
