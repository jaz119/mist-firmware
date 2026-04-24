#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "user_io.h"
#include "usb.h"
#include "max3421e.h"
#include "timer.h"
#include "hidparser.h"
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

static const char hid_device_name[4][10] = {
	"NONE", "MOUSE", "KEYBOARD", "JOYSTICK"
};

// up to 8 buttons can be remapped
#define MAX_JOYSTICK_BUTTON_REMAP 8

/*****************************************************************************/
//NOTE: the below mapping is hardware buttons to USB,
//      not to be confused with USB HID -> Virtual Keyboard
//      The purpose of the below is to overcome hardware problems e.g. :
//      - some controllers have buttons that are always on, so this allows to ignore them
//      - the layout of physical buttons might be random
//      In general it's easier to use virtual joystick mapping, but this gives a lower-level of control if needed.
//

ALIGNED(4) static struct {
  uint16_t vid;   // vendor id
  uint16_t pid;   // product id
  uint8_t offset; // bit index within report
  uint8_t button; // joystick button to be reported
} joystick_button_remap[MAX_JOYSTICK_BUTTON_REMAP];

void hid_joystick_button_remap_init(void) {
	memset(joystick_button_remap, 0, sizeof(joystick_button_remap));
}

char hid_joystick_button_remap(char *s, char action, int tag) {
	uint32_t i;

	hid_debugf("%s(%s)", __FUNCTION__, s);

	if (action == INI_SAVE) return 0;

	if(strlen(s) < 13) {
		hid_debugf("malformed entry");
		return 0;
	}

	// parse remap request
	for(i=0; i<MAX_JOYSTICK_BUTTON_REMAP; i++) {
		if(!joystick_button_remap[i].vid) {
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

static bool hid_get_report_descr(usb_device_t *dev, usb_hid_iface_info_t *iface, uint16_t size) {
	if (size > USB_MAX_CONFIG_DESC_SIZE)
		return false;

	ALIGNED(4) uint8_t buf[size];
	uint8_t rcode = get_report_desc(dev, iface->iface_idx, buf, size);

	if (rcode) {
		iprintf("usb: get report descriptor, error 0x%x\n", rcode);
		return false;
	}

#ifdef DEBUG_HID
	hexdump(buf, size, 0);
#endif

	// we got a report descriptor, try to parse it
	if (!parse_report_descriptor(buf, size, &(iface->conf),
		iface->device_type ? iface->device_type : HID_DEVICE_JOYSTICK))
		return false;

	if (iface->conf.type == HID_DEVICE_MOUSE) {
		iface->jindex = mice++;
	}
	else if (iface->conf.type == HID_DEVICE_KEYBOARD) {
		keyboards++;
	}
	else if (iface->conf.type == REPORT_TYPE_JOYSTICK) {
		iface->device_type = HID_DEVICE_JOYSTICK;
		iface->jindex = joystick_add();
	}

	return true;
}

static uint8_t hid_get_idle(usb_device_t *dev, uint8_t iface, uint8_t reportID, uint8_t *duration ) {
	return usb_ctrl_req( dev, HID_REQ_HIDIN, HID_REQUEST_GET_IDLE,
		reportID, 0, iface, 0x0001, duration);
}

static uint8_t hid_set_idle(usb_device_t *dev, uint8_t iface, uint8_t reportID, uint8_t duration ) {
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

static uint8_t hid_set_report(usb_device_t *dev, uint8_t iface,
	uint8_t report_type, uint8_t report_id, uint16_t nbytes, uint8_t* dataptr ) {

	return usb_ctrl_req(dev, HID_REQ_HIDOUT, HID_REQUEST_SET_REPORT,
		report_id, report_type, iface, nbytes, dataptr);
}

static uint8_t usb_hid_parse_conf(usb_device_t *dev, uint8_t conf, uint16_t len) {
	usb_hid_info_t *info = &(dev->hid_info);
	usb_hid_iface_info_t *cur_iface = NULL;
	uint8_t rcode;

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

	/* scan through all descriptors */
	p = &buf;

	while (len > 0) {
		switch (p->conf_desc.bDescriptorType) {
		case USB_DESCRIPTOR_CONFIGURATION:
			break;

		case USB_DESCRIPTOR_INTERFACE:
			usb_dump_interface_descriptor(&p->iface_desc);
			cur_iface = NULL;

			// only HID interfaces are supported
			if (p->iface_desc.bInterfaceClass != USB_CLASS_HID
				|| info->bNumIfaces >= MAX_IFACES)
				break;

			iprintf("HID interface %d:\n", p->iface_desc.bInterfaceNumber);

			// ok, let's use this interface
			cur_iface = &info->iface[info->bNumIfaces];
			memset(cur_iface, 0, sizeof(usb_hid_iface_info_t));

			cur_iface->iface_idx = p->iface_desc.bInterfaceNumber;
			cur_iface->has_boot_mode = (p->iface_desc.bInterfaceSubClass == HID_BOOT_INTF_SUBCLASS);
			cur_iface->device_type = HID_DEVICE_UNKNOWN;
			cur_iface->conf.type = REPORT_TYPE_NONE;

			if (p->iface_desc.bInterfaceProtocol == HID_PROTOCOL_KEYBOARD) {
				cur_iface->device_type = HID_DEVICE_KEYBOARD;
			}
			else if (p->iface_desc.bInterfaceProtocol == HID_PROTOCOL_MOUSE) {
				cur_iface->device_type = HID_DEVICE_MOUSE;
			}

			info->bNumIfaces++;
			break;

		case USB_DESCRIPTOR_ENDPOINT:
			usb_dump_endpoint_descriptor(&p->ep_desc);

			// only interrupt in endpoints are supported
			if (!cur_iface
				|| (p->ep_desc.bmAttributes & 0x03) != 3
				|| (p->ep_desc.bEndpointAddress & 0x80) != 0x80)
				break;

			iprintf(" -> endpoint %d, %s, interval: %d ms\n", p->ep_desc.bEndpointAddress & 0x0F,
				hid_device_name[cur_iface->conf.type], p->ep_desc.bInterval);

			// fill in the endpoint info structure
			cur_iface->interval      = p->ep_desc.bInterval;
			cur_iface->ep.epAddr     = (p->ep_desc.bEndpointAddress & 0x0F);
			cur_iface->ep.epType     = (p->ep_desc.bmAttributes & EP_TYPE_MSK);
			cur_iface->ep.maxPktSize = p->ep_desc.wMaxPacketSize[0] | (p->ep_desc.wMaxPacketSize[1] << 8);
			cur_iface->ep.bmNakPower = USB_NAK_NOWAIT;
			break;

		case HID_DESCRIPTOR_HID:
			usb_dump_hid_descriptor(&p->hid_desc);

			// we need a report descriptor
			if (!cur_iface || p->hid_desc.bDescrType != HID_DESCRIPTOR_REPORT)
				break;

			uint16_t desc_size = p->hid_desc.wDescriptorLength[0] | (p->hid_desc.wDescriptorLength[1] << 8);
			iprintf(" -> report descriptor, size = %d\n", desc_size);

			cur_iface->report_desc_size = desc_size;
			cur_iface->ignore_boot_mode = false;

			// verify report descriptor
			if (hid_get_report_descr(dev, cur_iface, desc_size)) {
				cur_iface->ignore_boot_mode = true;
				break;
			}

			info->bNumIfaces--;
			break;

		case USB_DESCRIPTOR_IAD:
		case USB_DESCRIPTOR_CS_IFACE:
			return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;

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
		hid_debugf("Config underrun: %d", len);
		return USB_ERROR_CONFIGURATION_SIZE_MISMATCH;
	}

	hid_debugf("found %d interface(s)", info->bNumIfaces);
	return 0;
}

static uint8_t usb_hid_init(usb_device_t *dev, usb_device_descriptor_t *dev_desc) {
	hid_debugf("%s(%u)", __FUNCTION__, dev->bAddress);

	uint8_t rcode;
	uint16_t vid, pid;

	usb_hid_info_t *info = &(dev->hid_info);
	static usb_configuration_descriptor_t conf_desc;

	// reset status
	info->bPollEnable = false;
	info->bNumIfaces = 0;

	for(uint32_t i=0; i<MAX_IFACES; i++) {
		info->iface[i].qLastPollTime = 0;
		info->iface[i].ep.epAddr     = i;
		info->iface[i].ep.epType     = 0;
		info->iface[i].ep.maxPktSize = 8;
		info->iface[i].ep.epAttribs  = 0;
		info->iface[i].ep.bmNakPower = USB_NAK_DEFAULT;
	}

	// save vid/pid for automatic hack later
	vid = dev_desc->idVendor;
	pid = dev_desc->idProduct;

	uint32_t num_of_conf = dev_desc->bNumConfigurations;

	for(uint32_t i=0; i<num_of_conf; i++) {
		if((rcode = usb_get_conf_descr(dev, sizeof(usb_configuration_descriptor_t), i, &conf_desc)))
			return rcode;

		usb_dump_conf_descriptor(&conf_desc);

		// parse directly if it already fitted completely into the buffer
		usb_hid_parse_conf(dev, i, conf_desc.wTotalLength);
	}

	// check if we found valid hid interfaces
	if(!info->bNumIfaces) {
		hid_debugf("no interface(s) found");
		return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
	}

	// Set Configuration Value
	rcode = usb_set_conf(dev, conf_desc.bConfigurationValue);
	if (rcode) hid_debugf("hid_set_conf error: %d", rcode);

	// process all supported interfaces
	for(uint32_t i=0; i<info->bNumIfaces; i++) {

		if(!info->iface[i].has_boot_mode || info->iface[i].ignore_boot_mode) {

			iprintf("%s: report ID = 0x%02x, size = %d\n",
				hid_device_name[info->iface[i].conf.type],
				info->iface[i].conf.report_id,
				info->iface[i].conf.report_size);

			if(info->iface[i].device_type == HID_DEVICE_JOYSTICK) {

				for(int k=0; k<MAX_AXES; k++)
					iprintf("Axis%d: %d@%d %d->%d\n", k,
						info->iface[i].conf.joystick_mouse.axis[k].size,
						info->iface[i].conf.joystick_mouse.axis[k].offset/8,
						info->iface[i].conf.joystick_mouse.axis[k].logical.min,
						info->iface[i].conf.joystick_mouse.axis[k].logical.max);

				for(int k=0; k<info->iface[i].conf.joystick_mouse.button_count; k++)
					iprintf("Button%d: @%d/%d\n", k,
						info->iface[i].conf.joystick_mouse.button[k].byte_offset,
						info->iface[i].conf.joystick_mouse.button[k].bitmask);
			}

			if((vid == 0x04d8) && (pid == 0xf6ec) && (i==0)) {
				iprintf("hacking 5200daptor\n");

				info->iface[0].conf.joystick_mouse.button[2].byte_offset = 4;
				info->iface[0].conf.joystick_mouse.button[2].bitmask = 0x40;    // "Reset"
				info->iface[0].conf.joystick_mouse.button[3].byte_offset = 4;
				info->iface[0].conf.joystick_mouse.button[3].bitmask = 0x10;    // "Start"

				info->iface[0].is_5200daptor = true;
			}

			// apply remap information from mist.ini if present
			for(int j=0; j<MAX_JOYSTICK_BUTTON_REMAP; j++) {
				if((joystick_button_remap[j].vid == vid) && (joystick_button_remap[j].pid == pid)) {
					uint8_t but = joystick_button_remap[j].button;
					info->iface[0].conf.joystick_mouse.button[but].byte_offset = joystick_button_remap[j].offset >> 3;
					info->iface[0].conf.joystick_mouse.button[but].bitmask = 0x80 >> (joystick_button_remap[j].offset & 7);
					iprintf("hacking from ini file %d %d -> %d\n",
						info->iface[0].conf.joystick_mouse.button[but].byte_offset,
						info->iface[0].conf.joystick_mouse.button[but].bitmask, but);
				}
			}
		}

		rcode = hid_set_idle(dev, info->iface[i].iface_idx, 0, 0);
		if (rcode && rcode != hrSTALL) {
			hid_debugf("%s: set IDLE error 0x%x",
				hid_device_name[info->iface[i].device_type], rcode);
			if(info->iface[i].device_type == HID_DEVICE_JOYSTICK) {
				uint8_t c_jindex = joystick_index(info->iface[i].jindex);
				hid_debugf("releasing joystick #%d, renumbering", c_jindex);
				joystick_release(c_jindex);
			}
			return rcode;
		}

		// enable boot mode if its not diabled
		if(info->iface[i].has_boot_mode && !info->iface[i].ignore_boot_mode) {
			iprintf("%s: enabling BOOT mode\n",
				hid_device_name[info->iface[i].device_type]);
			hid_set_protocol(dev, info->iface[i].iface_idx, HID_BOOT_PROTOCOL);
		} else
			hid_set_protocol(dev, info->iface[i].iface_idx, HID_RPT_PROTOCOL);
	}

	// update leds
	for(int i=0; i<info->bNumIfaces; i++)
		if(dev->hid_info.iface[i].device_type == HID_DEVICE_KEYBOARD)
			hid_set_report(dev, dev->hid_info.iface[i].iface_idx, 2, 0, 1, &kbd_led_state);

	// Logitech K400r : set F1-F12 as primary functions
	if((vid == 0x046d) && (pid == 0xc52b)) {
		hid_set_report(dev, 2, 2, 16, 7, "\x10\x01\x03\x15\x00\x00\x00"); timer_delay_msec(100);
		hid_set_report(dev, 2, 2, 16, 7, "\x10\x01\x0F\x15\x01\x00\x00"); timer_delay_msec(100);
		hid_set_report(dev, 2, 2, 16, 7, "\x10\x01\x10\x15\x00\x00\x00"); timer_delay_msec(100);
	}

	iprintf("HID configured\n");
	info->bPollEnable = true;

	return 0;
}

static uint8_t usb_hid_release(usb_device_t *dev) {
	usb_hid_info_t *info = &(dev->hid_info);

	hid_debugf("%s()", __FUNCTION__);

	for(uint32_t i=0; i<info->bNumIfaces; i++) {
		// check if a joystick is released
		if(info->iface[i].device_type == HID_DEVICE_JOYSTICK) {
			uint8_t c_jindex = joystick_index(info->iface[i].jindex);
			hid_debugf("releasing joystick #%d, renumbering", c_jindex);
			joystick_release(c_jindex);
		}

		// check if a keyboard is released
		if(info->iface[i].device_type == HID_DEVICE_KEYBOARD) {
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
								hid_debugf("decreasing jindex of mouse #%d from %d to %d", j,
									jindex, jindex-1);
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

// special 5200daptor button processing
static void handle_5200daptor(usb_device_t *dev, usb_hid_iface_info_t *iface, uint8_t *buf) {

	// list of buttons that are reported as keys
	static const struct {
	    uint8_t byte_offset;   // offset of the byte within the report which the button bit is in
	    uint8_t mask;          // bitmask of the button bit
	    uint8_t key_code[2];   // usb keycodes to be sent for joystick 0 and joystick 1
	} button_map[] ALIGNED(4) = {
	    { 4, 0x10, { 0x3a, 0x3d }}, /* START -> f1/f4 */
	    { 4, 0x20, { 0x3b, 0x3e }}, /* PAUSE -> f2/f5 */
	    { 4, 0x40, { 0x3c, 0x3f }}, /* RESET -> f3/f6 */
	    { 5, 0x01, { 0x1e, 0x21 }}, /*     1 ->  1/4  */
	    { 5, 0x02, { 0x1f, 0x22 }}, /*     2 ->  2/5  */
	    { 5, 0x04, { 0x20, 0x23 }}, /*     3 ->  3/6  */
	    { 5, 0x08, { 0x14, 0x15 }}, /*     4 ->  q/r  */
	    { 5, 0x10, { 0x1a, 0x17 }}, /*     5 ->  w/t  */
	    { 5, 0x20, { 0x08, 0x1c }}, /*     6 ->  e/y  */
	    { 5, 0x40, { 0x04, 0x09 }}, /*     7 ->  a/f  */
	    { 5, 0x80, { 0x16, 0x0a }}, /*     8 ->  s/g  */
	    { 6, 0x01, { 0x07, 0x0b }}, /*     9 ->  d/h  */
	    { 6, 0x02, { 0x1d, 0x19 }}, /*     * ->  z/v  */
	    { 6, 0x04, { 0x1b, 0x05 }}, /*     0 ->  x/b  */
	    { 6, 0x08, { 0x06, 0x11 }}, /*     # ->  c/n  */
	    { 0, 0x00, { 0x00, 0x00 }}  /* ----  end ---- */
	};

	// keyboard events are only generated for the first
	// two joysticks in the system
	uint8_t jindex = joystick_index(iface->jindex);
	if(jindex > 1) return;

	// build map of pressed keys
	uint16_t keys = 0;
	for(uint32_t i=0; button_map[i].mask; i++)
		if(buf[button_map[i].byte_offset] & button_map[i].mask)
			keys |= (1<<i);

	// check if keys have changed
	if(iface->key_state != keys) {
		ALIGNED(4) uint8_t buf[6] = { 0,0,0,0,0,0 };
		uint8_t p = 0;

		// report up to 6 pressed keys
		for(uint32_t i=0; (i<16)&&(p<6); i++)
			if(keys & (1<<i))
				buf[p++] = button_map[i].key_code[jindex];

		//    iprintf("5200: %d %d %d %d %d %d\n", buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);

		// generate key events
		user_io_kbd(0x00, buf, UIO_PRIORITY_GAMEPAD, dev->vid, dev->pid);

		// save current state of keys
		iface->key_state = keys;
	}
}

// collect bits from byte stream and assemble them into a signed word
FORCE_ARM static int32_t collect_bits(
	uint8_t *p, uint16_t offset, uint8_t size, bool is_signed) {
	if (!size || size > 32) return 0;

	uint32_t idx = offset >> 3;
	uint32_t shift = offset & 7;

	uint32_t val = p[idx];
	if (idx + 1 < REPORT_BUF_SZ) val |= (uint32_t)p[idx + 1] << 8;
	if (idx + 2 < REPORT_BUF_SZ) val |= (uint32_t)p[idx + 2] << 16;
	if (idx + 3 < REPORT_BUF_SZ) val |= (uint32_t)p[idx + 3] << 24;

	val >>= shift;
	uint32_t s_shift = 32 - size;

	if (is_signed) {
		return (int32_t)(val << s_shift) >> s_shift;
	}

	return val & (0xFFFFFFFFU >> s_shift);
}

static usb_hid_iface_info_t *virt_joy_kbd_iface = NULL;

/* processes a single USB interface */
FORCE_ARM static void usb_process_iface(
	usb_device_t *dev, usb_hid_iface_info_t *iface, uint16_t read, uint8_t *buf) {

	// successfully received some bytes
	hid_report_t *conf = &iface->conf;
	const uint8_t id_offset = (conf->report_id ? 1 : 0);

	// checking ID of received report
	if (conf->report_id && (buf[0] != conf->report_id))
		return;

	// ---------- process keyboard -------------
	if (iface->device_type == HID_DEVICE_KEYBOARD) {
		uint8_t *data = (buf + id_offset);
   		user_io_kbd(data[0], data + 2, UIO_PRIORITY_KEYBOARD, dev->vid, dev->pid);
		return;
	}

	ALIGNED(4) int16_t a[MAX_AXES];
	ALIGNED(4) static int16_t rem[MAX_AXES];

	// several axes ...
	for (uint32_t i=0; i<MAX_AXES; i++) {
		if (conf->joystick_mouse.axis[i].size) {
			bool is_signed = (int16_t)conf->joystick_mouse.axis[i].logical.min < 0;
			a[i] = collect_bits(buf, conf->joystick_mouse.axis[i].offset,
				conf->joystick_mouse.axis[i].size, is_signed);
		} else {
			a[i] = (iface->device_type == HID_DEVICE_JOYSTICK)
				? JOYSTICK_AXIS_MID : 0;
		}
	}

	// ... and buttons
	uint8_t btn = 0, btn_extra = 0, jmap = 0;

	for (uint32_t i=0; i<MAX_BUTTONS; i++) {
		uint8_t idx = conf->joystick_mouse.button[i].byte_offset;
		if(buf[idx] & conf->joystick_mouse.button[i].bitmask) {
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
		user_io_mouse(iface->jindex > 1 ? 1 : iface->jindex, btn, a[0], a[1], a[2]);
		return;
	}

	if (iface->device_type != HID_DEVICE_JOYSTICK)
		return;

	// ---------- process joystick -------------
	for (uint32_t i=0; i<MAX_AXES; i++) {
		if (conf->joystick_mouse.axis[i].size == 0) {
			a[i] = JOYSTICK_AXIS_MID;
		} else {
			int32_t val = a[i];
			int32_t min = conf->joystick_mouse.axis[i].logical.min;
			int32_t max = conf->joystick_mouse.axis[i].logical.max;
			int32_t range = max - min;
			if (range <= 0) {
				a[i] = JOYSTICK_AXIS_MID;
			} else {
				if (val < min) val = min;
				if (val > max) val = max;
				// scale
				a[i] = ((val - min) * 255) / range;
				// apply dead range
				if (a[i] > (JOYSTICK_AXIS_MID - mist_cfg.joystick_dead_range) &&
					a[i] < (JOYSTICK_AXIS_MID + mist_cfg.joystick_dead_range)) {
					a[i] = JOYSTICK_AXIS_MID;
				}
			}
		}
	}

	// handle HAT if present and overwrite any axis value
	if (conf->joystick_mouse.hat.size && !mist_cfg.joystick_ignore_hat) {
		uint8_t hat = collect_bits(buf, conf->joystick_mouse.hat.offset,
			conf->joystick_mouse.hat.size, false);

		ALIGNED(4) static const uint8_t hat2x[] = { 128,255,255,255,128,  0,  0,  0 };
		ALIGNED(4) static const uint8_t hat2y[] = {   0,  0,128,255,255,255,128,  0 };

		if (hat <= conf->joystick_mouse.hat.logical.max) {
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
	jmap |= btn << JOY_BTN_SHIFT; // add buttons

	// report joystick 1 to OSD
	uint8_t idx = joystick_index(iface->jindex);
	StateUsbIdSet(dev->vid, dev->pid, conf->joystick_mouse.button_count, idx);
	StateUsbJoySet(jmap, btn_extra, idx);

	// map virtual joypad
	uint32_t vjoy = jmap;
	vjoy |= btn_extra << 8;
	vjoy = virtual_joystick_mapping(dev->vid, dev->pid, vjoy );

	// now go back to original variables for downstream processing
	btn_extra = ((vjoy & 0xFF00) >> 8);
	jmap = (vjoy & 0x00FF);

	// report joysticks to OSD
	StateJoySet(jmap, idx);
	StateJoySetExtra(btn_extra, idx);

	// send right joy to OSD
	jmap = 0;
	if (a[3] < JOYSTICK_AXIS_TRIGGER_MIN) jmap |= JOY_LEFT;
	if (a[3] > JOYSTICK_AXIS_TRIGGER_MAX) jmap |= JOY_RIGHT;
	if (a[2] < JOYSTICK_AXIS_TRIGGER_MIN) jmap |= JOY_UP;
	if (a[2] > JOYSTICK_AXIS_TRIGGER_MAX) jmap |= JOY_DOWN;
	StateJoySetRight(jmap, idx);
	StateJoySetAnalogue(a[0], a[1], a[3], a[2], idx);

	// add it to vjoy (no remapping)
	vjoy |= jmap<<16;

	// swap joystick 0 and 1 since 1 is the one
	// used primarily on most systems
	if (!mist_cfg.joystick_disable_swap || user_io_core_type() != CORE_TYPE_8BIT) {
		if (idx == 0)      idx = 1;
		else if (idx == 1) idx = 0;
		// StateJoySetExtra( btn_extra, idx);
	}

	// if real DB9 mouse is preffered, switch the id back to 1
	idx = (idx == 0) && mist_cfg.joystick0_prefer_db9 ? 1 : idx;

	// don't run if not changed
	if (vjoy != iface->jmap) {
		user_io_digital_joystick(idx, vjoy & 0xFF);
		// new API with all extra buttons
		user_io_digital_joystick_ext(idx, vjoy);
	}

	iface->jmap = vjoy;

	// also send analog values
	user_io_analog_joystick(idx, a[0]-128, a[1]-128, a[3]-128, a[2]-128);

	// do special 5200daptor treatment
	if (iface->is_5200daptor)
		handle_5200daptor(dev, iface, buf);

	// apply keyboard mappings
	if ((!virt_joy_kbd_iface) || (virt_joy_kbd_iface == iface)) {
		bool ret = virtual_joystick_keyboard( vjoy );
		virt_joy_kbd_iface = NULL;
		if (ret)
			virt_joy_kbd_iface = iface;
	}
}

FORCE_ARM static uint8_t usb_hid_poll(usb_device_t *dev) {
	usb_hid_info_t *info = &(dev->hid_info);

	if (!info->bPollEnable)
		return 0;

	ALIGNED(4) uint8_t buf[REPORT_BUF_SZ];

	for (int i=0; i<info->bNumIfaces; i++)
	{
		usb_hid_iface_info_t *iface = &info->iface[i];

		if (iface->device_type == HID_DEVICE_UNKNOWN)
			continue;

		// poll at requested rate
		if (!timer_check(iface->qLastPollTime, iface->interval))
			continue;

		memset(buf, 0, REPORT_BUF_SZ);

		uint16_t read = MIN(iface->conf.report_size, sizeof(buf));
		uint8_t rcode = usb_in_transfer(dev, &(iface->ep), &read, buf);

		if (rcode) {
			if (rcode != hrNAK)
				hid_debugf("%s(%d): error 0x%02X",
					__FUNCTION__, dev->bAddress, rcode);
		} else {
			usb_process_iface(dev, iface, read, buf);
		}

		iface->qLastPollTime = timer_get_msec();
	}

	return 0;
}

void hid_set_kbd_led(unsigned char led, bool on) {
	// check if led state has changed
	if( (on && !(kbd_led_state&led)) || (!on && (kbd_led_state&led))) {
		if(on) kbd_led_state |=  led;
		else   kbd_led_state &= ~led;

		// search for all keyboard interfaces on all hid devices
		usb_device_t *dev = usb_get_devices();
		for(int i=0; i<USB_NUMDEVICES; i++) {
			if(dev[i].bAddress && (dev[i].class == &usb_hid_class)) {
				// search for keyboard interfaces
				for(int j=0; j<MAX_IFACES; j++)
					if(dev[i].hid_info.iface[j].device_type == HID_DEVICE_KEYBOARD)
				hid_set_report(dev+i, dev[i].hid_info.iface[j].iface_idx, 2, 0, 1, &kbd_led_state);
			}
		}
	}
}

int8_t hid_keyboard_present(void) {
	// check all USB devices for keyboards
	usb_device_t *dev = usb_get_devices();
	for(int i=0; i<USB_NUMDEVICES; i++) {
		if(dev[i].bAddress && (dev[i].class == &usb_hid_class)) {
			// search for keyboard interfaces
			for(int j=0; j<MAX_IFACES; j++)
				if(dev[i].hid_info.iface[j].device_type == HID_DEVICE_KEYBOARD)
			return 1;
		}
	}
	return 0;
}

const usb_device_class_config_t usb_hid_class = {
	USB_HID,
	usb_hid_init,
	usb_hid_release,
	usb_hid_poll
};
