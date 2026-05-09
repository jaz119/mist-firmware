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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "joystick.h"
#include "usb.h"
#include "debug.h"
#include "utils.h"

uint8_t joystick_add() {
	uint8_t index = joystick_count();
	StateNumJoysticksSet(index + 1);
	return index;
}

uint8_t joystick_release(uint8_t raw_jindex) {
	usb_device_t *dev = usb_get_devices();
	uint8_t count = joystick_count();
	if (!count) return 0;

	// walk through all devices and search for sticks with a higher id
	for (uint8_t j=0; j < USB_NUMDEVICES; j++) {
		// search for all joystick interfaces on all hid devices
		if (dev[j].bAddress && (dev[j].class == &usb_hid_class)) {
			for (uint8_t k=0; k < MAX_IFACES; k++) {
				usb_hid_iface_info_t *iface = &dev[j].hid_info.iface[k];
				// search for joystick interfaces
				if (iface->device_type == HID_DEVICE_JOYSTICK) {
					if (iface->jindex > raw_jindex) {
						hid_debugf("decreasing joystick index of dev #%d from %d to %d",
							j, jindex, jindex - 1);
						iface->jindex--;
						StateUsbIdSet(dev[j].vid, dev[j].pid,
							iface->conf.joystick_mouse.button_count,
							iface->jindex);
					}
				}
			}
		}
	}

	// one less joystick in the system ...
	StateNumJoysticksSet(--count);

	raw_jindex = joystick_index(count);
	if (raw_jindex < 6)
		memset(&mist_joysticks[raw_jindex], 0, sizeof(mist_joystick_t));

	return 0;
}
