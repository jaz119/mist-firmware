#include <stdio.h>
#include <string.h>

#include "timer.h"
#include "usb.h"
#include "debug.h"

ALIGNED(4) static usb_device_t usb_devices[USB_NUMDEVICES];

usb_device_t *usb_get_devices() {
	return usb_devices;
}

// get (last connected) device by type
usb_device_t *usb_get_device(usb_dev_type_t type) {
	usb_device_t *devs = usb_get_devices();

	for (int i=USB_NUMDEVICES-1; i>=0; i--)
		if (devs[i].bAddress && devs[i].class && devs[i].class->type == type)
			return &devs[i];

	return NULL;
}

// iterate usb device over list
usb_device_t *usb_get_next_device(bool with_poll) {
	usb_device_t *devs = usb_get_devices();
	static int cur_index = -1;

	for (int n = 0; n < USB_NUMDEVICES; n++) {
		cur_index++;

		if (cur_index >= USB_NUMDEVICES) {
			cur_index = 0;
		}

		if (!devs[cur_index].bAddress || !devs[cur_index].class)
			continue;

		if (with_poll && !devs[cur_index].class->poll)
			continue;

		return &devs[cur_index];
	}

	return NULL;
}

void usb_init() {
	usb_debugf("%s()", __FUNCTION__);

	for (int i=0; i<USB_NUMDEVICES; i++)
		usb_devices[i].bAddress = 0;

	usb_hw_init();
}

// list of supported device classes
static const usb_device_class_config_t *class_list[] = {
  &usb_hub_class,
#ifndef CONFIG_CHIP_SAMV71
  &usb_rtc_tiny_class.base,
  &usb_rtc_mcp2221_class.base,
#endif
#ifdef USB_STORAGE
  &usb_storage_class,
#endif
#ifdef USB_PL2303_CDC
  &usb_pl2303_class,
#endif
#ifdef USB_ASIX_NET
  &usb_asix_class,
#endif
  &usb_hid_class,
  NULL
};

uint8_t usb_configure(uint8_t parent, uint8_t port, bool lowspeed) {

	usb_debugf("%s(parent=0x%x, port=%d, lowspeed=%d)",
		__FUNCTION__, parent, port, lowspeed);

	usb_device_descriptor_t dev_desc;
	ALIGNED(4) union {
		usb_string0_descriptor_t str0_desc;
		usb_string_descriptor_t str_desc;
		uint8_t buf[255];
	} str;

	uint8_t rcode = 0, i;

	// find an empty device entry
	for(i=0; i<USB_NUMDEVICES && usb_devices[i].bAddress; i++);

	if(i < USB_NUMDEVICES) {
		usb_debugf("using free entry at %d", i);

		usb_device_t *dev = &usb_devices[i];
		memset(dev, 0, sizeof(usb_device_t));

		// setup generic info
		dev->parent = parent;
		dev->lowspeed = lowspeed;
		dev->port = port;

		// setup endpoint 0
		dev->ep0.maxPktSize = 8;
		dev->ep0.bmNakPower = USB_NAK_DEFAULT;

		if((rcode = usb_get_dev_descr( dev, 8, &dev_desc )))
			return rcode;

		dev->ep0.maxPktSize = dev_desc.bMaxPacketSize0;
		usb_debugf("EP0 max packet size: %d", dev->ep0.maxPktSize);

		// Assign new address to the device
		// (address is simply the number of the free slot + 1)
		rcode = usb_set_addr(dev, i + 1);
		if(rcode) {
			iprintf("usb: failed to assign address (rcode=%d)\n", rcode);
			dev->bAddress = 0;
			return rcode;
		}

		uint32_t timer = timer_get_msec();
		do {
			rcode = usb_get_dev_descr( dev, sizeof(usb_device_descriptor_t), &dev_desc );
		} while (rcode && !timer_check(timer, 20)); // Some recovery interval (2 ms as USB 2.0 9.2.6.3)

		if(rcode) {
			dev->bAddress = 0;
			return rcode;
		}

		// --- enumerate device ---
		usb_dump_device_descriptor(&dev_desc);
		iprintf("USB device %04x:%04x detected\n",
			dev_desc.idVendor, dev_desc.idProduct);

		// save vid/pid
		dev->vid = dev_desc.idVendor;
		dev->pid = dev_desc.idProduct;

		// The Retroflag Classic USB Gamepad doesn't report movement until the string descriptors are read,
		// so read all of them here (and show them on the console)
		if (!usb_get_string_descr(dev, sizeof(str), 0, 0, &str.str_desc)) { // supported languages descriptor
			usb_debugf("wLangId: 0x%04X", str.str0_desc.wLANGID[0]);
		}

		// try to connect device to one of the supported classes
		for(int c=0; class_list[c]; c++) {
			usb_debugf("trying to init class %d", c);

			unsigned long time = GetRTTC();
			rcode = class_list[c]->init(dev, &dev_desc);

			if (!rcode) {
				dev->class = class_list[c];
				iprintf("USB device %d accepted, %lu ms\n", i, GetRTTC() - time);
				return 0;
			}
		}

		usb_debugf("device NOT accepted");
		dev->bAddress = 0;

	} else
		iprintf("no more free device entries\n");

	iprintf("usb: unknown device\n");
	return 0;
}

uint8_t usb_release_device(uint8_t parent, uint8_t port) {
	usb_debugf("%s(parent=0x%x, port=%d)", __FUNCTION__, parent, port);

	for(uint8_t i=0; i<USB_NUMDEVICES; i++) {
		if(usb_devices[i].bAddress && usb_devices[i].parent == parent && usb_devices[i].port == port) {
			usb_debugf("  -> device with address %u", usb_devices[i].bAddress);

			// check if this is a hub (parent of some other device)
			// and release its kids first
			for(uint8_t j=0; j<USB_NUMDEVICES; j++) {
				if(usb_devices[j].parent == usb_devices[i].bAddress)
					usb_release_device(usb_devices[i].bAddress, usb_devices[j].port);
			}

			uint8_t rcode = 0;
			if(usb_devices[i].class)
				rcode = usb_devices[i].class->release(&usb_devices[i]);

			usb_devices[i].bAddress = 0;
			return rcode;
		}
	}

	// this should never happen ...
	return 0;
}

uint8_t usb_get_dev_descr( usb_device_t *dev, uint16_t nbytes, usb_device_descriptor_t* p ) {
  return usb_ctrl_req( dev, USB_REQ_GET_DESCR, USB_REQUEST_GET_DESCRIPTOR,
	0x00, USB_DESCRIPTOR_DEVICE, 0x0000, nbytes, (uint8_t*)p );
}

uint8_t usb_get_dev_qualifier_descr( usb_device_t *dev,
	uint16_t nbytes, usb_device_qualifier_descriptor_t* p ) {

	return usb_ctrl_req( dev, USB_REQ_GET_DESCR, USB_REQUEST_GET_DESCRIPTOR,
		0x00, USB_DESCRIPTOR_DEVICE_QUALIFIER, 0x0000, nbytes, (uint8_t*)p );
}

// get configuration descriptor
uint8_t usb_get_conf_descr( usb_device_t *dev,
	uint16_t nbytes, uint8_t conf, usb_configuration_descriptor_t* p ) {

	return usb_ctrl_req( dev, USB_REQ_GET_DESCR, USB_REQUEST_GET_DESCRIPTOR,
		conf, USB_DESCRIPTOR_CONFIGURATION, 0x0000, nbytes, (uint8_t*)p );
}

uint8_t usb_get_other_speed_descr( usb_device_t *dev,
	uint16_t nbytes, uint8_t conf, usb_configuration_descriptor_t* p ) {

	return usb_ctrl_req( dev, USB_REQ_GET_DESCR, USB_REQUEST_GET_DESCRIPTOR,
		conf, USB_DESCRIPTOR_OTHER_SPEED, 0x0000, nbytes, (uint8_t*)p );
}

uint8_t usb_set_addr( usb_device_t *dev, uint8_t newaddr ) {
	usb_debugf("%s(%u)", __FUNCTION__, newaddr);

	uint8_t rcode = usb_ctrl_req( dev, USB_REQ_SET, USB_REQUEST_SET_ADDRESS,
		newaddr, 0x00, 0x0000, 0x0000, NULL );

	dev->bAddress = (rcode) ? 0 : newaddr;
	return rcode;
}

// get configuration
uint8_t usb_get_conf( usb_device_t *dev, uint8_t *conf_value ) {
	return usb_ctrl_req( dev, USB_REQ_GET, USB_REQUEST_GET_CONFIGURATION,
		0x00, 0x00, 0x0000, 1, conf_value );
}

// set configuration
uint8_t usb_set_conf( usb_device_t *dev, uint8_t conf_value ) {
	return usb_ctrl_req( dev, USB_REQ_SET, USB_REQUEST_SET_CONFIGURATION,
		conf_value, 0x00, 0x0000, 0x0000, NULL );
}

uint8_t usb_get_string_descr( usb_device_t *dev, uint16_t nbytes,
	uint8_t index, uint16_t lang_id, usb_string_descriptor_t* dataptr ) {

	return usb_ctrl_req( dev, USB_REQ_GET_DESCR, USB_REQUEST_GET_DESCRIPTOR,
		index, USB_DESCRIPTOR_STRING, lang_id, nbytes, (uint8_t*)dataptr );
}
