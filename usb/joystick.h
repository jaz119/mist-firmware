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

#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <stdlib.h>
#include "state.h"
#include "mist_cfg.h"

uint8_t joystick_add();
uint8_t joystick_renumber(uint8_t joy);
uint8_t joystick_release(uint8_t joy);

static inline uint8_t joystick_count() {
  return StateNumJoysticks();
}

static inline uint8_t joystick_index(uint8_t jindex) {
  // If DB9 joystick are preferred: USB joysticks are shifted to 2,3...
  return (mist_cfg.joystick_db9_fixed_index)
    ? (jindex + 2) : jindex;
}

#endif
