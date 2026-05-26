#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "user_io.h"
#include "usb.h"
#include "max3421e.h"
#include "timer.h"
#include "hidparser.h"
#include "hidquirks.h"
#include "joymapping.h"
#include "joystick.h"
#include "hardware.h"
#include "utils.h"
#include "mist_cfg.h"
#include "osd.h"
#include "state.h"
#include "debug.h"

static unsigned char kbd_led_state = 0;  // default: all leds off
static unsigned char keyboards = 0;      // number of detected usb keyboards
static unsigned char mice      = 0;      // number of detected usb mice

unsigned char get_keyboards(void) {
	return keyboards;
}

unsigned char get_mice(void) {
	return mice;
}

static const char *hid_device_name[4] = {
	"NONE", "MOUSE", "KEYBOARD", "JOYSTICK"
};

// up to 8 buttons can be remapped
#define MAX_JOYSTICK_BUTTON_REMAP 8

/*****************************************************************************/
// NOTE: the below mapping is hardware buttons to USB,
//      not to be confused with USB HID -> Virtual Keyboard
//      The purpose of the below is to overcome hardware problems e.g. :
//      - some controllers have buttons that are always on, so this allows to ignore them
//      - the layout of physical buttons might be random
//      In general it's easier to use virtual joystick mapping, but this gives a lower-level of control if needed.
//

static struct {
	uint16_t vid;   // vendor id
	uint16_t pid;   // product id
	uint8_t offset; // bit index within report
	uint8_t button; // joystick button to be reported
} joystick_button_remap[MAX_JOYSTICK_BUTTON_REMAP];

void hid_joystick_button_remap_init(void) {
	memset(joystick_button_remap, 0, sizeof(joystick_button_remap));
}

char hid_joystick_button_remap(char *s, char action, int tag) {
	hid_debugf("%s(%s)", __FUNCTION__, s);

	if (action == INI_SAVE) return 0;

	if (strlen(s) < 13) {
		hid_debugf("malformed entry");
		return 0;
	}

	// parse remap request
	for (uint32_t i=0; i<MAX_JOYSTICK_BUTTON_REMAP; i++) {
		if (!joystick_button_remap[i].vid) {
			// first two entries are comma seperated
			joystick_button_remap[i].vid = strtol(s, NULL, 16);
			joystick_button_remap[i].pid = strtol(s+5, NULL, 16);
			joystick_button_remap[i].offset = strtol(s+10, NULL, 10);
			// search for next comma
			s+=10; while(*s && (*s != ',')) s++; s++;
			joystick_button_remap[i].button = strtol(s, NULL, 10);

			hid_debugf("parsed: 0x%x/0x%x %d -> %d",
			joystick_button_remap[i].vid, joystick_button_remap[i].pid,
			joystick_button_remap[i].offset, joystick_button_remap[i].button);

			return 0;
		}
	}

	return 0;
}

/*****************************************************************************/

//get HID report descriptor
static inline uint8_t get_report_desc(usb_device_t *dev, uint8_t iface_idx, void *buf, uint16_t size) {
	return usb_ctrl_req(dev, HID_REQ_HIDREPORT, USB_REQUEST_GET_DESCRIPTOR,
		0x00, HID_DESCRIPTOR_REPORT, iface_idx, size, buf);
}

static uint8_t hid_get_idle(usb_device_t *dev,
	uint8_t iface, uint8_t reportID, uint8_t *duration ) {

	return usb_ctrl_req( dev, HID_REQ_HIDIN, HID_REQUEST_GET_IDLE,
		reportID, 0, iface, 0x0001, duration);
}

static uint8_t hid_set_idle(usb_device_t *dev,
	uint8_t iface, uint8_t reportID, uint8_t duration ) {

	return usb_ctrl_req( dev, HID_REQ_HIDOUT, HID_REQUEST_SET_IDLE,
		reportID, duration, iface, 0x0000, NULL);
}

static uint8_t hid_get_protocol(usb_device_t *dev, uint8_t iface, uint8_t *protocol) {
	return usb_ctrl_req( dev, HID_REQ_HIDIN, HID_REQUEST_GET_PROTOCOL,
		0, 0x00, iface, 0x0001, protocol);
}

static uint8_t hid_set_protocol(usb_device_t *dev, uint8_t iface, uint8_t protocol) {
	return usb_ctrl_req(dev, HID_REQ_HIDOUT, HID_REQUEST_SET_PROTOCOL,
		protocol, 0x00, iface, 0x0000, NULL);
}

uint8_t hid_set_report(usb_device_t *dev, uint8_t iface,
	uint8_t report_type, uint8_t report_id, uint16_t nbytes, uint8_t* dataptr ) {

	return usb_ctrl_req(dev, HID_REQ_HIDOUT, HID_REQUEST_SET_REPORT,
		report_id, report_type, iface, nbytes, dataptr);
}

static bool hid_get_report_descr(usb_device_t *dev, usb_hid_iface_info_t *iface, uint16_t size) {
	if (size > USB_MAX_CONFIG_DESC_SIZE)
		return false;

	ALIGNED(4) uint8_t buf[size];
	uint8_t rcode = get_report_desc(dev, iface->iface_idx, buf, size);

	if (rcode) {
		errorf("usb: get report descriptor, error 0x%02x", rcode);
		return false;
	}

#ifdef DEBUG_HID
	hexdump(buf, size, 0);
#endif

	// we got a report descriptor, try to parse it
	if (!parse_report_descriptor(buf, size, &(iface->conf),
		iface->device_type ? iface->device_type : HID_DEVICE_JOYSTICK))
		return false;

	return true;
}

void hid_axis_precalc(hid_axis_t *axis, uint8_t type) {
	if (type == HID_DEVICE_MOUSE) {
		axis->mul = (1 << 16);
	} else {
		int32_t l_min = axis->logical.min;
		int32_t l_max = axis->logical.max;

		// range is always positive to keep multiplier stable
		int32_t range = (l_max > l_min) ? (l_max - l_min) : (l_min - l_max);
		if (range == 0) range = 1;

		// span = JOYSTICK_AXIS_MID * mult / 64
		int64_t span = (int64_t)JOYSTICK_AXIS_MID * mist_cfg.joystick_analog_mult >> 6;

		// multiplier in fixed-point 16.16 format
		axis->mul = (int32_t)((span << 16) / range);
	}
}

static uint8_t usb_hid_parse_conf(usb_device_t *dev, uint8_t conf, uint16_t len) {
	usb_hid_info_t *info = &(dev->hid_info);
	usb_hid_iface_info_t *cur_iface = NULL;
	uint8_t rcode, ep_count;

	if (len > USB_MAX_CONFIG_DESC_SIZE)
		return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;

	union buf_u {
	    usb_configuration_descriptor_t conf_desc;
	    usb_interface_descriptor_t iface_desc;
	    usb_endpoint_descriptor_t ep_desc;
	    usb_hid_descriptor_t hid_desc;
	    uint8_t raw[len];
	} buf, *p;

	// usb_interface_descriptor
	if((rcode = usb_get_conf_descr(dev, len, conf, &buf.conf_desc)))
		return rcode;

	// search for device in quirks table
	info->quirks = get_hid_dev(dev->vid, dev->pid);

	/* scan through all descriptors */
	p = &buf;

	while (len > 0) {
		switch (p->conf_desc.bDescriptorType) {
		case USB_DESCRIPTOR_CONFIGURATION:
			break;

		case USB_DESCRIPTOR_INTERFACE:
			usb_dump_interface_descriptor(&p->iface_desc);
			cur_iface = NULL;
			ep_count = 0;

			if (info->numIfaces >= MAX_IFACES
				|| p->iface_desc.bInterfaceClass != USB_CLASS_HID) {
				if (!info->quirks || !(info->quirks->has && info->quirks->has(&p->iface_desc)))
					break;
			}

			if (info->quirks && info->quirks->name)
				iprintf("[ %s ]\n", info->quirks->name);

			iprintf("HID interface %d:\n", p->iface_desc.bInterfaceNumber);

			// ok, let's use this interface
			cur_iface = &info->iface[info->numIfaces];
			memset(cur_iface, 0, sizeof(usb_hid_iface_info_t));

			cur_iface->ignore_boot_mode = false;
			cur_iface->iface_idx = p->iface_desc.bInterfaceNumber;
			cur_iface->has_boot_mode = (p->iface_desc.bInterfaceSubClass == HID_BOOT_INTF_SUBCLASS);

			if (p->iface_desc.bInterfaceProtocol == HID_PROTOCOL_KEYBOARD) {
				cur_iface->device_type = HID_DEVICE_KEYBOARD;
			}
			else if (p->iface_desc.bInterfaceProtocol == HID_PROTOCOL_MOUSE) {
				cur_iface->device_type = HID_DEVICE_MOUSE;
			}

			info->numIfaces++;
			break;

		case USB_DESCRIPTOR_ENDPOINT:
			usb_dump_endpoint_descriptor(&p->ep_desc);
			ep_count++;

			if (ep_count > 2) {
				// skip vendor specific iface
				if (cur_iface) {
					info->numIfaces--;
					cur_iface = NULL;
				}
				break;
			}

			bool is_in = (p->ep_desc.bEndpointAddress & 0x80);

			if (!cur_iface
				|| (is_in && (p->ep_desc.bmAttributes & EP_TYPE_MSK) != EP_TYPE_INTR))
				break;

			ep_t *ep = (is_in) ? &cur_iface->ep_in : &cur_iface->ep_out;

			// fill the endpoint info structure
			ep->nakPower   = USB_NAK_NOWAIT;
			ep->addr       = (p->ep_desc.bEndpointAddress & 0x0F);
			ep->type       = (p->ep_desc.bmAttributes & EP_TYPE_MSK);
			ep->maxPktSize = p->ep_desc.wMaxPacketSize[0] | (p->ep_desc.wMaxPacketSize[1] << 8);
			ep->interval   = MAX(4, p->ep_desc.bInterval); // for battery saving

			iprintf(" -> %s endpoint %d, %s, interval: %d ms\n", (is_in) ? "IN" : "OUT",
				ep->addr, hid_device_name[cur_iface->conf.type], ep->interval);
			break;

		case HID_DESCRIPTOR_HID:
			usb_dump_hid_descriptor(&p->hid_desc);

			// we need a report descriptor
			if (!cur_iface || p->hid_desc.bDescrType != HID_DESCRIPTOR_REPORT)
				break;

			uint16_t desc_size = p->hid_desc.wDescriptorLength[0] | (p->hid_desc.wDescriptorLength[1] << 8);
			hid_debugf(" -> report descriptor, size = %d", desc_size);

			// verify report descriptor
			if (hid_get_report_descr(dev, cur_iface, desc_size)) {
				cur_iface->ignore_boot_mode = true;
				break;
			}

			// invalid HID descriptor
			info->numIfaces--;
			break;

		case USB_DESCRIPTOR_INTERFACE_AD:
		case USB_DESCRIPTOR_INTERFACE_AUDIO:
		case USB_DESCRIPTOR_ENDPOINT_AUDIO:
			break;

		default:
			hid_debugf("unknown descriptor 0x%02x, length %d",
				p->raw[1], p->raw[0]);
			break;
		}

		if (!p->conf_desc.bLength || p->conf_desc.bLength > len)
			break;

		// advance to next descriptor
		len -= p->conf_desc.bLength;
		p = (union buf_u*)(p->raw + p->conf_desc.bLength);
	}

	if (len != 0) {
		hid_debugf("config underrun: %d", len);
		return USB_ERROR_CONFIGURATION_SIZE_MISMATCH;
	}

	hid_debugf("found %d interface(s)", info->numIfaces);
	return 0;
}

static uint8_t usb_hid_init(usb_device_t *dev, usb_device_descriptor_t *dev_desc) {
	hid_debugf("%s(%u)", __FUNCTION__, dev->bAddress);

	uint8_t rcode;
	uint16_t vid, pid;
	usb_hid_info_t *info = &(dev->hid_info);

	// reset status
	memset(info, 0, sizeof(usb_hid_info_t));

	// save vid/pid for automatic hack later
	vid = dev_desc->idVendor;
	pid = dev_desc->idProduct;

	usb_configuration_descriptor_t conf_desc;
	uint32_t num_of_conf = dev_desc->bNumConfigurations;

	for (uint32_t i=0; i<num_of_conf; i++) {
		if ((rcode = usb_get_conf_descr(dev, sizeof(usb_configuration_descriptor_t), i, &conf_desc)))
			return rcode;

		usb_dump_conf_descriptor(&conf_desc);

		// parse directly if it already fitted completely into the buffer
		usb_hid_parse_conf(dev, i, conf_desc.wTotalLength);
	}

	// check if we found valid hid interfaces
	if (!info->numIfaces) {
		hid_debugf("no interface(s) found");
		return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
	}

	// set configuration value
	rcode = usb_set_conf(dev, conf_desc.bConfigurationValue);
	if (rcode) {
		hid_debugf("hid: set device config, error 0x%02x", rcode);
		return rcode;
	}

	// apply device init quirks
	if (info->quirks && info->quirks->init_quirk) {
		if (!info->quirks->init_quirk(dev))
			return USB_ERROR_NO_SUCH_DEVICE;
	}

	// process all supported interfaces
	for (uint32_t i=0; i<info->numIfaces; i++) {
		usb_hid_iface_info_t *iface = &info->iface[i];

		if (iface->conf.type == HID_DEVICE_MOUSE) {
			iface->jindex = mice++;
		}
		else if (iface->conf.type == HID_DEVICE_KEYBOARD) {
			hid_set_report(dev, iface->iface_idx, 2, 0, 1, &kbd_led_state);
			keyboards++;
		}
		else if (iface->conf.type == REPORT_TYPE_JOYSTICK) {
			iface->device_type = HID_DEVICE_JOYSTICK;
			iface->jindex = joystick_add();
		}

		infof("%s: report 0x%02x, size %d",
			hid_device_name[iface->conf.type], iface->conf.report_id,
			iface->conf.report_size);

		for (int k=0; k<MAX_AXES; k++) {
			hid_axis_precalc(
				&iface->conf.joystick_mouse.axis[k],
				iface->device_type);
			if (iface->device_type != HID_DEVICE_JOYSTICK)
				continue;
			iprintf("Axis%d: %d@%d %d->%d\n", k,
				iface->conf.joystick_mouse.axis[k].size,
				iface->conf.joystick_mouse.axis[k].offset / 8,
				iface->conf.joystick_mouse.axis[k].logical.min,
				iface->conf.joystick_mouse.axis[k].logical.max);
		}

		if (iface->device_type == HID_DEVICE_JOYSTICK) {
			for (int k=0; k<iface->conf.joystick_mouse.button_count; k++) {
				iprintf("Button%d: @%d/%d\n", k,
					iface->conf.joystick_mouse.button[k].byte_offset,
					iface->conf.joystick_mouse.button[k].bitmask);
			}
		}

		// apply remap information from mist.ini if present
		for (int j=0; j<MAX_JOYSTICK_BUTTON_REMAP; j++) {
			if ((joystick_button_remap[j].vid == vid) && (joystick_button_remap[j].pid == pid)) {
				uint8_t but = joystick_button_remap[j].button;
				info->iface[0].conf.joystick_mouse.button[but].byte_offset = joystick_button_remap[j].offset >> 3;
				info->iface[0].conf.joystick_mouse.button[but].bitmask = 0x80 >> (joystick_button_remap[j].offset & 7);
				infof("Hacking from INI file: @%d/%d -> %d",
					info->iface[0].conf.joystick_mouse.button[but].byte_offset,
					info->iface[0].conf.joystick_mouse.button[but].bitmask, but);
			}
		}

		// set zero Idle time for all reports
		rcode = hid_set_idle(dev, iface->iface_idx, 0, 0);
		if (rcode && rcode != hrSTALL) {
			hid_debugf("%s: set Idle error 0x%02x",
				hid_device_name[iface->device_type], rcode);
			if (iface->device_type == HID_DEVICE_JOYSTICK) {
				uint8_t c_jindex = joystick_index(iface->jindex);
				hid_debugf("releasing joystick #%d, renumbering", c_jindex);
				joystick_release(c_jindex);
			}
			return rcode;
		}

		// enable Boot mode if its not diabled
		if (iface->has_boot_mode && !iface->ignore_boot_mode) {
			infof("%s: enabling BOOT mode", hid_device_name[iface->device_type]);
			hid_set_protocol(dev, iface->iface_idx, HID_BOOT_PROTOCOL);
		} else {
			hid_set_protocol(dev, iface->iface_idx, HID_RPT_PROTOCOL);
		}
	}

	hid_debugf("all configured");
	info->pollEnable = true;
	return 0;
}

static uint8_t usb_hid_release(usb_device_t *dev) {
	usb_hid_info_t *info = &(dev->hid_info);

	hid_debugf("%s()", __FUNCTION__);

	for(uint32_t i=0; i<info->numIfaces; i++) {
		// check if a joystick is released
		if(info->iface[i].device_type == HID_DEVICE_JOYSTICK) {
			uint8_t c_jindex = joystick_index(info->iface[i].jindex);
			hid_debugf("releasing joystick #%d, renumbering", c_jindex);
			joystick_release(info->iface[i].jindex);
			virtual_joystick_keyboard(0);
		}

		// check if a keyboard is released
		if(info->iface[i].device_type == HID_DEVICE_KEYBOARD) {
			hid_debugf("releasing keyboard #%d", keyboards);
			keyboards--;
		}

		// check if a mouse is released
		if(info->iface[i].device_type == HID_DEVICE_MOUSE) {
			uint8_t c_jindex = info->iface[i].jindex;
			hid_debugf("releasing mouse #%d, renumbering", info->iface[i].jindex);
			// search for all mouse interfaces on all hid devices
			usb_device_t *dev = usb_get_devices();
			for(uint32_t j=0; j<USB_NUMDEVICES; j++) {
				if(dev[j].bAddress && (dev[j].class == &usb_hid_class)) {
					// search for mouse interfaces, decrease the index with a higher id
					for(uint32_t k=0; k<MAX_IFACES; k++) {
						if(dev[j].hid_info.iface[k].device_type == HID_DEVICE_MOUSE) {
							uint8_t jindex = dev[j].hid_info.iface[k].jindex;
							if(jindex > c_jindex) {
								hid_debugf("decreasing jindex of mouse #%lu from %d to %d",
									j, jindex, jindex - 1);
								dev[j].hid_info.iface[k].jindex--;
							}
						}
					}
				}
			}
			mice--;
		}
	}

	return 0;
}

// collect bits from byte stream and assemble them into a signed word
FORCE_ARM static int32_t collect_bits(
	uint8_t *p, uint16_t limit, uint16_t offset, uint8_t size, bool is_signed) {
	if (!size || size > 32) return 0;

	uint32_t idx = offset >> 3;
	uint32_t shift = offset & 7;

	uint32_t val = p[idx];
	if (idx + 1 < limit) val |= (uint32_t)p[idx + 1] << 8;
	if (idx + 2 < limit) val |= (uint32_t)p[idx + 2] << 16;
	if (idx + 3 < limit) val |= (uint32_t)p[idx + 3] << 24;

	val >>= shift;
	uint32_t s_shift = 32 - size;

	if (is_signed) {
		return (int32_t)(val << s_shift) >> s_shift;
	}

	return val & (0xFFFFFFFFU >> s_shift);
}

/* processes a single USB interface */
FORCE_ARM static void usb_process_iface(
	usb_device_t *dev, usb_hid_iface_info_t *iface, uint16_t read, uint8_t *buf) {

	usb_hid_info_t *info = &(dev->hid_info);

	// successfully received some bytes
	hid_report_t *conf = &iface->conf;
	const uint8_t id_offset = (conf->report_id ? 1 : 0);

	// checking ID of received report
	if (conf->report_id && (buf[0] != conf->report_id))
		return;

	// apply device poll quirks
	if (info->quirks && info->quirks->poll_quirk)
		info->quirks->poll_quirk(dev, iface, buf);

	// ---------- process keyboard -------------
	if (iface->device_type == HID_DEVICE_KEYBOARD) {
		uint8_t *data = (buf + id_offset);
		user_io_kbd(data[0], data + 2, UIO_PRIORITY_KEYBOARD);
		return;
	}

	int16_t a[MAX_AXES];
	static int16_t rem[MAX_AXES];

	// several axes ...
	for (uint32_t i=0; i<MAX_AXES; i++) {
		hid_axis_t *axis = &conf->joystick_mouse.axis[i];
		bool is_joystick = (iface->device_type == HID_DEVICE_JOYSTICK);

		if (axis->size == 0) {
			a[i] = is_joystick ? JOYSTICK_AXIS_MID : 0;
			continue;
		}

		bool is_signed = (axis->logical.min < 0) || (axis->logical.max < 0);
		int32_t val = collect_bits(buf, read, axis->offset, axis->size, is_signed);

		if (is_signed) {
			if (axis->size == 16) val = (int32_t)(int16_t)val;
			else if (axis->size == 8) val = (int32_t)(int8_t)val;
		}

		int32_t pos = val;

		if (is_joystick) {
			int32_t l_min = axis->logical.min;
			int32_t l_max = axis->logical.max;

			int32_t l_mid = l_min + ((l_max - l_min) / 2);
			int32_t dist = val - l_mid;

			// handle axis inversion
			if (l_max < l_min) {
				dist = -dist;
			}

			// fast scaling
			int32_t scaled_dist = (dist * axis->mul) >> 16;
			pos = JOYSTICK_AXIS_MID + scaled_dist + mist_cfg.joystick_analog_offset;

			if (pos < JOYSTICK_AXIS_MIN) pos = JOYSTICK_AXIS_MIN;
			if (pos > JOYSTICK_AXIS_MAX) pos = JOYSTICK_AXIS_MAX;

			if (ABS(pos - JOYSTICK_AXIS_MID) < mist_cfg.joystick_dead_range) {
				pos = JOYSTICK_AXIS_MID;
			}
		}

		a[i] = pos;
	}

	// ... and buttons
	uint8_t btn = 0, btn_extra = 0, jmap = 0;

	for (uint32_t i=0; i<conf->joystick_mouse.button_count; i++) {
		hid_button_t *b = &conf->joystick_mouse.button[i];
		if(buf[b->byte_offset] & b->bitmask) {
			if (i < 4) btn |= (1 << i);
			else btn_extra |= (1 << (i - 4));
		}
	}

	// ---------- process mouse -------------
	if (iface->device_type == HID_DEVICE_MOUSE) {
		// limit mouse movement to +/- 127
		const uint8_t mouse_speed = mist_cfg.mouse_speed;
		for (uint32_t i=0; i<3; i++) {
			if (i < 2) {
				int32_t val = (int32_t)a[i] * mouse_speed + rem[i];
				a[i] = val / 100;
				rem[i] = val - (a[i] * 100);
			}
			if (a[i] > 127) a[i] = 127;
			else if (a[i] < -128) a[i] = -128;
		}
		user_io_mouse(0, btn, a[0], a[1], a[2]);
		return;
	}

	if (iface->device_type != HID_DEVICE_JOYSTICK)
		return;

	// ---------- process joystick -------------
	// handle HAT if present and overwrite any axis value
	if (conf->joystick_mouse.hat.size && !mist_cfg.joystick_ignore_hat) {
		uint32_t hat = collect_bits(buf, read,
			conf->joystick_mouse.hat.offset,
			conf->joystick_mouse.hat.size, false);

		static const uint8_t hat2x[] = { 128, 255, 255, 255, 128,   0,   0,   0 };
		static const uint8_t hat2y[] = {   0,   0, 128, 255, 255, 255, 128,   0 };

		if (hat <= (uint32_t)conf->joystick_mouse.hat.logical.max) {
			uint8_t idx = (hat - conf->joystick_mouse.hat.logical.min) & 0x07;
			uint8_t x_val = hat2x[idx], y_val = hat2y[idx];

			if (x_val != JOYSTICK_AXIS_MID) a[0] = x_val;
			if (y_val != JOYSTICK_AXIS_MID) a[1] = y_val;
		}
	}

	if (a[0] < JOYSTICK_AXIS_TRIGGER_MIN) jmap |= JOY_LEFT;
	if (a[0] > JOYSTICK_AXIS_TRIGGER_MAX) jmap |= JOY_RIGHT;
	if (a[1] < JOYSTICK_AXIS_TRIGGER_MIN) jmap |= JOY_UP;
	if (a[1] > JOYSTICK_AXIS_TRIGGER_MAX) jmap |= JOY_DOWN;
	jmap |= (btn << JOY_BTN_SHIFT); // add buttons

	// report joystick to OSD
	uint8_t idx = joystick_index(iface->jindex);
	StateUsbIdSet(dev->vid, dev->pid, conf->joystick_mouse.button_count, idx);
	StateUsbJoySet(jmap, btn_extra, idx);

	// map virtual joypad
	uint32_t vjoy = jmap;
	const joy_remap_t *remap_quirk = (info->quirks) ? &info->quirks->remap : NULL;

	vjoy |= (btn_extra << 8);
	vjoy = virtual_joystick_mapping(dev->vid, dev->pid, vjoy, remap_quirk);

	// now go back to original variables for downstream processing
	btn_extra = ((vjoy & 0xFF00) >> 8);
	jmap = (vjoy & 0x00FF);

	// report joysticks to OSD
	StateJoySet(jmap, idx);
	StateJoySetExtra(btn_extra, idx);

	// send right joy to OSD
	jmap = 0;
	if (a[2] < JOYSTICK_AXIS_TRIGGER_MIN) jmap |= JOY_LEFT;
	if (a[2] > JOYSTICK_AXIS_TRIGGER_MAX) jmap |= JOY_RIGHT;
	if (a[3] < JOYSTICK_AXIS_TRIGGER_MIN) jmap |= JOY_UP;
	if (a[3] > JOYSTICK_AXIS_TRIGGER_MAX) jmap |= JOY_DOWN;
	StateJoySetRight(jmap, idx);
	StateJoySetAnalogue(a[0], a[1], a[2], a[3], idx);

	// add it to vjoy (no remapping)
	vjoy |= (jmap << 16);

	// don't run if not changed
	if (vjoy == iface->jmap)
		return;

	iface->jmap = vjoy;

	// swap joystick if need
	idx = user_io_swap_joystick(idx);

	// send digital values with extra buttons
	user_io_digital_joystick(idx, vjoy);

	// send analog values
	user_io_analog_joystick(idx,
		a[0] - 128, a[1] - 128,  // left
		a[2] - 128, a[3] - 128); // right

	// apply keyboard mappings
	virtual_joystick_keyboard(vjoy);
}

FORCE_ARM static uint8_t usb_hid_poll(usb_device_t *dev) {
	usb_hid_info_t *info = &(dev->hid_info);

	if (!info->pollEnable)
		return 0;

	uint8_t rcode = 0;
	ALIGNED(4) uint8_t buf[REPORT_BUF_SZ];

	for (int i=0; i<info->numIfaces; i++)
	{
		usb_hid_iface_info_t *iface = &info->iface[i];

		if (iface->device_type == HID_DEVICE_UNKNOWN)
			continue;

		// poll at requested rate
		if (!timer_check(iface->lastPollTime, iface->ep_in.interval))
			continue;

		uint16_t read = MIN(iface->conf.report_size, sizeof(buf));
		rcode = usb_in_transfer(dev, &(iface->ep_in), &read, buf);

		iface->lastPollTime = timer_get_msec();

		if (rcode == 0) {
			usb_process_iface(dev, iface, read, buf);
		} else if (rcode != hrNAK) {
			iface->lastPollTime += 100;
			break;
		}
	}

	return rcode;
}

void hid_set_kbd_led(unsigned char led, bool on) {
	// check if led state has changed
	if ((on && !(kbd_led_state & led)) || (!on && (kbd_led_state & led))) {

		if (on) kbd_led_state |=  led;
		else    kbd_led_state &= ~led;

		// search for all keyboard interfaces on all hid devices
		usb_device_t *dev = usb_get_devices();

		for (int i=0; i<USB_NUMDEVICES; i++) {
			if (!dev[i].bAddress || (dev[i].class != &usb_hid_class))
				continue;
			// search for keyboard interfaces
			for (int j=0; j<MAX_IFACES; j++) {
				usb_hid_iface_info_t *iface = &dev[i].hid_info.iface[j];
				if (iface->device_type != HID_DEVICE_KEYBOARD)
					continue;
				uint8_t size = 1;
				uint8_t rid = iface->conf.report_id;
				ALIGNED(4) uint8_t report[2];
				if (rid != 0) {
					report[0] = rid;
					report[1] = kbd_led_state;
					size = 2;
				} else {
					report[0] = kbd_led_state;
				}
				hid_set_report(&dev[i], iface->iface_idx, 2, rid, size, report);
			}
		}
	}
}

const usb_device_class_config_t usb_hid_class = {
	USB_HID,
	usb_hid_init,
	usb_hid_release,
	usb_hid_poll
};
