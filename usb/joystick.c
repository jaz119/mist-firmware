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

// the physical joysticks (db9 ports at the right device side)
// as well as the joystick emulation are renumbered if usb joysticks
// are present in the system. The USB joystick(s) replace joystick 1
// and 0 and the physical joysticks are "shifted up".
//
// Since the primary joystick is in port 1 the first usb joystick
// becomes joystick 1 and only the second one becomes joystick 0
// (mouse port)

uint8_t joystick_renumber(uint8_t j)
{
    uint8_t usb_sticks = joystick_count();

    // no usb sticks present: no changes are being made
    if (!usb_sticks)
        return j;

    // Keep DB9 joysticks as joystick 0 and joystick 1
    // USB joysticks will be 2,3,...
    if (mist_cfg.joystick_db9_fixed_index)
        return j;

    if (j == 0)
    {
        // if usb joysticks are present, then
        // physical joystick 0 (mouse port) becomes becomes 2,3,...
        j = mist_cfg.joystick0_prefer_db9 ? 0 : usb_sticks + 1;
    }
    else
    {
        // if one usb joystick is present, then
        // physical joystick 1 (joystick port) becomes physical joystick 0 (mouse) port.
        // If more than 1 usb joystick is present it becomes 2,3,...
        if (usb_sticks == 1)
        {
            j = mist_cfg.joystick_disable_swap ? 1 : 0;
        } else {
            j = usb_sticks;
        }
    }

    return j;
}

uint8_t joystick_release(uint8_t raw_jindex) {
    usb_device_t *dev = usb_get_devices();
    uint8_t count = joystick_count();
    if (!count) return 0;

    // walk through all devices and search for sticks with a higher id
    for (uint8_t j=0; j < USB_NUMDEVICES; j++)
    {
        // search for all joystick interfaces on all hid devices
        if (dev[j].bAddress && (dev[j].class == &usb_hid_class))
        {
            for (uint8_t k=0; k < MAX_IFACES; k++)
            {
                usb_hid_iface_info_t *iface = &dev[j].hid_info.iface[k];

                // search for joystick interfaces
                if (iface->device_type == HID_DEVICE_JOYSTICK)
                {
                    if (iface->jindex > raw_jindex)
                    {
                        hid_debugf("decreasing joystick index of dev #%d from %d to %d",
                            j, iface->jindex, iface->jindex - 1);

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
