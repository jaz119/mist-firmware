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

#include <joystick.h>
#include <usb.h>
#include <utils.h>
#include <debug.h>

typedef struct {
    uint8_t jindex; // current index
    uint8_t round;  // 0 for pure devices, 1 for composite
} visitor_ctx_t;

static bool jindex_shift(usb_device_t *dev, void *arg) {
    visitor_ctx_t *ctx = (visitor_ctx_t *)arg;
    usb_hid_info_t *info = &(dev->hid_info);
    bool is_composite = false;

    if (!dev->vid && !dev->pid)
        return true;
    if (!(info->device_types & HID_DEVICE_JOYSTICK))
        return true;

    if (info->device_types & HID_DEVICE_KEYBOARD)
        is_composite = true;
    if (info->device_types & HID_DEVICE_MOUSE)
        is_composite = true;

    if (ctx->round == 0 && is_composite)
        return true;
    if (ctx->round == 1 && !is_composite)
        return true;

    // joystick reindex
    for (uint8_t i = 0; i < info->num_ifaces; i++)
    {
        usb_hid_iface_info_t *iface = &info->iface[i];

        if (iface->device_type != HID_DEVICE_JOYSTICK)
            continue;

        iface->jindex = ctx->jindex++;

        // make it visible in menu
        StateUsbIdSet(
            dev->vid, dev->pid,
            iface->conf.joystick_mouse.button_count,
            iface->jindex);
    }

    return true;
}

uint8_t joysticks_renumber() {
    visitor_ctx_t ctx = { 0, 0 };

    // native usb joysticks must be first in list
    visit_devices(USB_HID, jindex_shift, &ctx);

    // composite devices after they
    ctx.round = 1;
    visit_devices(USB_HID, jindex_shift, &ctx);

    return ctx.jindex;
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

uint8_t joystick_add() {
    uint8_t index = joystick_count();
    StateNumJoysticksSet(index + 1);
    return index;
}

uint8_t on_joystick_release() {
    uint8_t count = joysticks_renumber();

    // one less joystick in the system ...
    StateNumJoysticksSet(count);

    if (count < MAX_NUM_JOYS) {
        memset(&mist_joysticks[count], 0, sizeof(mist_joystick_t));
    }

    return 0;
}
