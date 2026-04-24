// http://www.frank-zhao.com/cache/hid_tutorial_1.php

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "hidparser.h"
#include "debug.h"

#if 1
#define hidp_extreme_debugf(...) hidp_debugf(__VA_ARGS__)
#else
#define hidp_extreme_debugf(...)
#endif

typedef struct {
  uint8_t bSize: 2;
  uint8_t bType: 2;
  uint8_t bTag: 4;
} __attribute__((packed)) item_t;

// flags for joystick components required
#define JOY_MOUSE_REQ_AXIS_X  0x01
#define JOY_MOUSE_REQ_AXIS_Y  0x02
#define JOY_MOUSE_REQ_BTN_0   0x04
#define JOY_MOUSE_REQ_BTN_1   0x08
#define JOYSTICK_COMPLETE     (JOY_MOUSE_REQ_AXIS_X | JOY_MOUSE_REQ_AXIS_Y | JOY_MOUSE_REQ_BTN_0)
#define MOUSE_COMPLETE        (JOY_MOUSE_REQ_AXIS_X | JOY_MOUSE_REQ_AXIS_Y | JOY_MOUSE_REQ_BTN_0 | JOY_MOUSE_REQ_BTN_1)

#define USAGE_PAGE_GENERIC_DESKTOP  1
#define USAGE_PAGE_SIMULATION       2
#define USAGE_PAGE_VR               3
#define USAGE_PAGE_SPORT            4
#define USAGE_PAGE_GAMING           5
#define USAGE_PAGE_GENERIC_DEVICE   6
#define USAGE_PAGE_KEYBOARD         7
#define USAGE_PAGE_LEDS             8
#define USAGE_PAGE_BUTTON           9
#define USAGE_PAGE_ORDINAL         10
#define USAGE_PAGE_TELEPHONY       11
#define USAGE_PAGE_CONSUMER        12

#define USAGE_POINTER   1
#define USAGE_MOUSE     2
#define USAGE_JOYSTICK  4
#define USAGE_GAMEPAD   5
#define USAGE_KEYBOARD  6
#define USAGE_KEYPAD    7
#define USAGE_MULTIAXIS 8

#define USAGE_X       48
#define USAGE_Y       49
#define USAGE_Z       50
#define USAGE_RX      51
#define USAGE_RY      52
#define USAGE_RZ      53
#define USAGE_WHEEL   56
#define USAGE_HAT     57

// check if the current report
static bool report_is_usable(uint16_t bit_count, uint8_t report_complete, hid_report_t *conf) {
	hidp_debugf("  - total bit count: %d (%d bytes, %d bits)",
		bit_count, bit_count / 8, bit_count % 8);

	conf->report_size = (bit_count + 7) / 8;

	// check if something useful was detected
	if(((conf->type == REPORT_TYPE_JOYSTICK)  && ((report_complete & JOYSTICK_COMPLETE) == JOYSTICK_COMPLETE))
		|| ((conf->type == REPORT_TYPE_MOUSE) && ((report_complete & MOUSE_COMPLETE) == MOUSE_COMPLETE))
		|| ((conf->type == REPORT_TYPE_KEYBOARD))) {
		hidp_debugf("  - report 0x%02x is usable", conf->report_id);
		return true;
	}

	hidp_debugf("  - unusable report 0x%02x", conf->report_id);
	return false;
}

bool parse_report_descriptor(uint8_t *rep, uint16_t rep_size, hid_report_t *conf, int target_type) {
	int8_t app_collection = 0;
	int8_t phys_log_collection = 0;
	uint8_t skip_collection = 0;
	int8_t generic_desktop = -1;   // depth at which first gen_desk was found
	uint8_t collection_depth = 0;

	uint8_t report_size = 0, report_count = 0;
	uint16_t bit_count = 0, usage_count = 0;
	int16_t logical_minimum=0, physical_minimum=0;
	uint16_t logical_maximum=0, physical_maximum=0;

	memset(conf, 0, sizeof(hid_report_t));
	conf->type = REPORT_TYPE_NONE;

	// mask used to check of all required components have been found, so
	// that e.g. both axes and the button of a joystick are ready to be used
	uint8_t report_complete = 0;

	// joystick/mouse components
	int8_t axis[MAX_AXES];
	uint8_t btns = 0;
	int8_t hat = -1;

	for (int i=0; i<MAX_AXES; i++) axis[i] = -1;

	while(rep_size) {
		uint32_t value = 0;

		// extract short item
		uint8_t tag = ((item_t*)rep)->bTag;
		uint8_t type = ((item_t*)rep)->bType;
		uint8_t size = ((item_t*)rep)->bSize;

		rep++;
		rep_size--;   // one byte consumed

		if (size == 3)
			size = 4;

		for (uint8_t j = 0; j < size; j++) {
			value |= ((uint32_t)(*rep++) << (8 * j));
			rep_size--;
		}

		// we are currently skipping an unknown/unsupported collection)
		if(skip_collection) {
			if(!type) {  // main item
				// any new collection increases the depth of collections to skip
				if(tag == 10) {
					skip_collection++;
					collection_depth++;
				}

				// any end collection decreases it
				if(tag == 12) {
					skip_collection--;
					collection_depth--;

					// leaving the depth the generic desktop was valid for
					if(generic_desktop > collection_depth)
						generic_desktop = -1;
				}
			}
			usage_count = 0;
			btns = 0;

		} else {
			// hidp_extreme_debugf("-> Item tag=%d type=%d size=%d", tag, type, size);
			uint16_t usage_id = (value & 0xffff);

			switch(type) {
			case 0:
				// main item
				switch(tag) {
				case 8:
					// handle found buttons
					hidp_extreme_debugf("INPUT(%u)", value);
					if(btns) {
						if((conf->type == REPORT_TYPE_JOYSTICK) ||
						   (conf->type == REPORT_TYPE_MOUSE)) {
							// scan for buttons
							for(int b=0; b<report_count && b<MAX_BUTTONS; b++) {
								if(conf->joystick_mouse.button_count < MAX_BUTTONS) {
									uint16_t this_bit = bit_count + b * report_size;
									uint8_t idx = conf->joystick_mouse.button_count;

									hidp_debugf("BUTTON%d @ %d (byte %d, mask %d)", idx,
										this_bit, this_bit / 8, 1 << (this_bit % 8));

									conf->joystick_mouse.button[idx].byte_offset = this_bit / 8;
									conf->joystick_mouse.button[idx].bitmask = 1 << (this_bit % 8);
									conf->joystick_mouse.button_count++;
								}
							}

							// we found at least one button which is all
							// we want to accept this as a valid joystick
							if(conf->joystick_mouse.button_count > 0) report_complete |= JOY_MOUSE_REQ_BTN_0;
							if(conf->joystick_mouse.button_count > 1) report_complete |= JOY_MOUSE_REQ_BTN_1;
						}
					}

					// handle found axes
					for(int c=0; c<MAX_AXES; c++) {
						if(axis[c] >= 0) {
							const char axis_names[] = "XYZRST";
							uint16_t cnt = bit_count + (axis[c] * report_size);
        					hidp_debugf("  (%c-AXIS @ %d (byte %d, bit %d))",
								axis_names[c], cnt, cnt / 8, cnt & 7);

							if((conf->type == REPORT_TYPE_JOYSTICK) || (conf->type == REPORT_TYPE_MOUSE)) {
								// save in joystick report
								conf->joystick_mouse.axis[c].offset = cnt;
								conf->joystick_mouse.axis[c].size = report_size;
								conf->joystick_mouse.axis[c].logical.min = logical_minimum;
								conf->joystick_mouse.axis[c].logical.max = logical_maximum;
								if(c==0) report_complete |= JOY_MOUSE_REQ_AXIS_X;
								if(c==1) report_complete |= JOY_MOUSE_REQ_AXIS_Y;
							}
						}
					}

					// handle found hat
					if(hat >= 0) {
						uint16_t cnt = bit_count + report_size * hat;
						hidp_debugf("  (HAT @ %d (byte %d, bit %d), size %d)",
						  cnt, cnt / 8, cnt & 7, report_size);

						if(conf->type == REPORT_TYPE_JOYSTICK) {
							conf->joystick_mouse.hat.offset = cnt;
							conf->joystick_mouse.hat.size = report_size;
							conf->joystick_mouse.hat.logical.min = logical_minimum;
							conf->joystick_mouse.hat.logical.max = logical_maximum;
							conf->joystick_mouse.hat.physical.min = physical_minimum;
							conf->joystick_mouse.hat.physical.max = physical_maximum;
						}
					}

					bit_count += report_count * report_size;
					for (int i=0; i<MAX_AXES; i++) axis[i] = -1;
					usage_count = 0;
					btns = 0;
					hat = -1;
					break;

				case 9:
					hidp_extreme_debugf("OUTPUT(%u)", value);
					usage_count = 0;
					btns = 0;
					break;

				case 11:
					hidp_extreme_debugf("FEATURE(%u)", value);
					usage_count = 0;
					btns = 0;
					break;

				case 10:
					hidp_extreme_debugf("COLLECTION(%u)", value);
					collection_depth++;
					usage_count = 0;

					if(value == 1) {   // app collection
						hidp_extreme_debugf("  -> application");
						app_collection++;
					} else if(value == 0) {  // physical collection
						hidp_extreme_debugf("  -> physical");
						phys_log_collection++;
					} else if(value == 2) {  // logical collection
						hidp_extreme_debugf("  -> logical");
						phys_log_collection++;
					} else {
						phys_log_collection++;
					}

					usage_count = 0;
					break;

				case 12:
					hidp_extreme_debugf("END_COLLECTION(%u)", value);
					collection_depth--;
					if(phys_log_collection) {
						hidp_extreme_debugf("  -> phys/log end");
						phys_log_collection--;
					} else if(app_collection > 0) {
						app_collection--;
					} else {
						hidp_debugf(" -> unexpected");
					}
					break;

				default:
					hidp_debugf("unexpected main item %d", tag);
					return false;
				}
				break;

			case 1:
				// global item
				switch(tag) {
				case 0:
					hidp_extreme_debugf("USAGE_PAGE(0x%x)", value);
					generic_desktop = -1;

					if(usage_id == USAGE_PAGE_KEYBOARD) {
						hidp_extreme_debugf(" -> Keyboard");
					} else if(usage_id == USAGE_PAGE_GAMING) {
						hidp_extreme_debugf(" -> Game device");
					} else if(usage_id == USAGE_PAGE_LEDS) {
						hidp_extreme_debugf(" -> LEDs");
					} else if(usage_id == USAGE_PAGE_CONSUMER) {
						hidp_extreme_debugf(" -> Consumer");
					} else if(usage_id == USAGE_PAGE_BUTTON) {
						hidp_extreme_debugf(" -> Buttons");
						btns = 1;
					} else if(usage_id == USAGE_PAGE_GENERIC_DESKTOP) {
						hidp_extreme_debugf(" -> Generic Desktop");
						generic_desktop = 1;
					} else {
						hidp_extreme_debugf(" -> UNSUPPORTED USAGE_PAGE");
					}
					break;

				case 1:
					if (size == 1) logical_minimum = (int8_t)(value & 0xff);
					else if (size == 2) logical_minimum = (int16_t)(value & 0xffff);
					else logical_minimum = (int32_t)value;
					hidp_extreme_debugf("LOGICAL_MINIMUM(%d)", logical_minimum);
					break;

				case 2:
					hidp_extreme_debugf("LOGICAL_MAXIMUM(%u)", value);
					logical_maximum = value;
					break;

				case 3:
					if (size == 1) physical_minimum = (int8_t)(value & 0xff);
					else if (size == 2) physical_minimum = (int16_t)(value & 0xffff);
					else physical_minimum = (int32_t)value;
					hidp_extreme_debugf("PHYSICAL_MINIMUM(%d)", physical_minimum);
					break;

				case 4:
					hidp_extreme_debugf("PHYSICAL_MAXIMUM(%u)", value);
					physical_maximum = value;
					break;

				case 5:
					hidp_extreme_debugf("UNIT_EXPONENT(%u)", value);
					break;

				case 6:
					hidp_extreme_debugf("UNIT(%u)", value);
					break;

				case 7:
					hidp_extreme_debugf("REPORT_SIZE(%u)", value);
					report_size = value;
					break;

				case 8:
					// check if report is usable and stop parsing if it is
					if (bit_count > 0) {
						if (report_is_usable(bit_count, report_complete, conf)) {
							if (conf->type == target_type) {
								return true;
							}
						}
					}

					// Next report is beginning from this point
					hidp_extreme_debugf("REPORT_ID(%u)", value);

					conf->report_size = 0;
					conf->report_id = value;
					conf->joystick_mouse.button_count = 0;
					for(int a=0; a<MAX_AXES; a++) {
						conf->joystick_mouse.axis[a].offset = 0;
						conf->joystick_mouse.axis[a].size = 0;
					}
					report_complete = 0;
					usage_count = 0;
					bit_count = 8;
					break;

				case 9:
					hidp_extreme_debugf("REPORT_COUNT(%u)", value);
					report_count = value;
					break;

				default:
					hidp_debugf("unexpected global item %d", tag);
					return false;
				}
				break;

			case 2:
				// local item
				value &= 0xffff;

				switch(tag) {
				case 0:
					// we only support mice, keyboards and joysticks
					hidp_extreme_debugf("USAGE(0x%x)", value);

					if(usage_id == USAGE_KEYBOARD && generic_desktop == 1) {
						// usage(keyboard) is always allowed
						hidp_debugf(" -> Keyboard");
						conf->type = REPORT_TYPE_KEYBOARD;
					} else if(usage_id == USAGE_MOUSE && generic_desktop == 1) {
						// usage(mouse) is always allowed
						conf->type = REPORT_TYPE_MOUSE;
						hidp_debugf(" -> Mouse");
					} else if(((usage_id == USAGE_GAMEPAD) || (usage_id == USAGE_JOYSTICK)) && generic_desktop == 1) {
						hidp_extreme_debugf(" -> Gamepad/Joystick");
						hidp_debugf("Gamepad/Joystick usage found");
						conf->type = REPORT_TYPE_JOYSTICK;
					} else if(usage_id == USAGE_POINTER && app_collection) {
						// usage(pointer) is allowed within the application collection
						hidp_debugf(" -> Pointer");
					} else if ((usage_id == USAGE_HAT || (value == 0 && generic_desktop == 1))
						&& conf->type == REPORT_TYPE_JOYSTICK) {
						// usage(hat) is allowed within the app collection
						hidp_extreme_debugf(" -> HAT usage");
						if (hat == -1) {
							hat = usage_count;
							hidp_debugf(" -> Assigned HAT switch to usage index %d", hat);
						}
					} else if((conf->type != REPORT_TYPE_NONE) && app_collection) {
						hidp_extreme_debugf(" -> axis usage");

						// usage(x) and usage(y) are allowed within the app collection
						int target_slot = -1;

						if (conf->type == REPORT_TYPE_MOUSE) {
							if (usage_id == USAGE_X) {
								hidp_extreme_debugf("MOUSE: found X axis @ %d", usage_count);
								target_slot = 0;
							} else if (usage_id == USAGE_Y) {
								hidp_extreme_debugf("MOUSE: found Y axis @ %d", usage_count);
								target_slot = 1;
							} else if (usage_id == USAGE_WHEEL) {
								hidp_extreme_debugf("MOUSE: found Wheel @ %d", usage_count);
								target_slot = 2;
							}
						}
						else if (conf->type == REPORT_TYPE_JOYSTICK) {
							if (usage_id == USAGE_X) {
								hidp_extreme_debugf("JOYSTICK: found X axis @ %d", usage_count);
								target_slot = 0;
							} else if (usage_id == USAGE_Y) {
								hidp_extreme_debugf("JOYSTICK: found Y axis @ %d", usage_count);
								target_slot = 1;
							} else if (usage_id == USAGE_Z) {
								hidp_extreme_debugf("JOYSTICK: found Z axis @ %d", usage_count);
								target_slot = 2;
							} else if (usage_id == USAGE_RZ) {
								hidp_extreme_debugf("JOYSTICK: found RZ axis @ %d", usage_count);
								target_slot = 3;
							}
						}

						if (target_slot != -1 && target_slot < MAX_AXES) {
							hidp_debugf(" -> Assigned axis to slot %d", target_slot);
							axis[target_slot] = usage_count;
						}
					} else {
						hidp_extreme_debugf(" -> UNSUPPORTED USAGE");
					}

					usage_count++;
					break;

				case 1:
					hidp_extreme_debugf("USAGE_MINIMUM(%u)", value);
					break;

				case 2:
					hidp_extreme_debugf("USAGE_MAXIMUM(%u)", value);
					break;

				default:
					hidp_extreme_debugf("unexpected local item %d", tag);
					break;
				}
				break;

			default:
				// reserved
				hidp_extreme_debugf("unexpected reserved item %d", tag);
				break;
			}
		}
	}

	return report_is_usable(bit_count, report_complete, conf)
		&& (conf->type == target_type);
}
