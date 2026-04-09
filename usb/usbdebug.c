#include <stdio.h>
#include "usb.h"
#include "debug.h"

void usb_dump_device_descriptor(usb_device_descriptor_t *desc) {
	usb_debugf("USB device descriptor:");
	usb_debugf("  bLength:              %d", desc->bLength);
	usb_debugf("  bDescriptorType:      %d", desc->bDescriptorType);
	usb_debugf("  bcdUSB:               0x%x", desc->bcdUSB);
	usb_debugf("  bDeviceClass:         0x%x", desc->bDeviceClass);
	usb_debugf("  bDeviceSubClass:      0x%x", desc->bDeviceSubClass);
	usb_debugf("  bDeviceProtocol:      0x%x", desc->bDeviceProtocol);
	usb_debugf("  bMaxPacketSize0:      %d", desc->bMaxPacketSize0);
	usb_debugf("  idVendor:             0x%04x", desc->idVendor);
	usb_debugf("  idProduct:            0x%04x", desc->idProduct);
	usb_debugf("  bcdDevice:            0x%x", desc->bcdDevice);
	usb_debugf("  iManufacturer:        %d", desc->iManufacturer);
	usb_debugf("  iProduct:             %d", desc->iProduct);
	usb_debugf("  iSerialNumber:        %d", desc->iSerialNumber);
	usb_debugf("  bNumConfigurations:   %d", desc->bNumConfigurations);
}

void usb_dump_device_qualifier_descriptor(usb_device_qualifier_descriptor_t *desc) {
	usb_debugf("USB device qualifier descriptor:");
	usb_debugf("  bLength:              %d", desc->bLength);
	usb_debugf("  bDescriptorType:      %d", desc->bDescriptorType);
	usb_debugf("  bcdUSB:               0x%x", desc->bcdUSB);
	usb_debugf("  bDeviceClass:         0x%x", desc->bDeviceClass);
	usb_debugf("  bDeviceSubClass:      0x%x", desc->bDeviceSubClass);
	usb_debugf("  bDeviceProtocol:      0x%x", desc->bDeviceProtocol);
	usb_debugf("  bMaxPacketSize0:      0x%d", desc->bMaxPacketSize0);
	usb_debugf("  bNumConfigurations:   0x%d", desc->bNumConfigurations);
}

void usb_dump_conf_descriptor(usb_configuration_descriptor_t *desc) {
	usb_debugf("USB configuration descriptor:");
	usb_debugf("  bLength:              %d", desc->bLength);
	usb_debugf("  bDescriptorType:      %d", desc->bDescriptorType);
	usb_debugf("  wTotalLength:         0x%x", desc->wTotalLength);
	usb_debugf("  bNumInterfaces:       %d", desc->bNumInterfaces);
	usb_debugf("  bConfigurationValue:  %d", desc->bConfigurationValue);
	usb_debugf("  iConfiguration:       %d", desc->iConfiguration);
	usb_debugf("  bmAttributes:         0x%x", desc->bmAttributes);
	usb_debugf("  bMaxPower:            %d", desc->bMaxPower);
}

void usb_dump_interface_descriptor(usb_interface_descriptor_t *desc) {
	usb_debugf("  Interface descriptor:");
	usb_debugf("    bLength:            %d", desc->bLength);
	usb_debugf("    bDescriptorType:    %d", desc->bDescriptorType);
	usb_debugf("    bInterfaceNumber:   %d", desc->bInterfaceNumber);
	usb_debugf("    bAlternateSetting:  %d", desc->bAlternateSetting);
	usb_debugf("    bNumEndpoints:      %d", desc->bNumEndpoints);
	usb_debugf("    bInterfaceClass:    %d", desc->bInterfaceClass);
	usb_debugf("    bInterfaceSubClass: %d", desc->bInterfaceSubClass);
	usb_debugf("    bInterfaceProtocol: %d", desc->bInterfaceProtocol);
	usb_debugf("    iInterface:         %d", desc->iInterface);
}

void usb_dump_endpoint_descriptor(usb_endpoint_descriptor_t *desc) {
	usb_debugf("    Endpoint descriptor:");
	usb_debugf("      bLength:          %d", desc->bLength);
	usb_debugf("      bDescriptorType:  %d", desc->bDescriptorType);
	usb_debugf("      bEndpointAddress: 0x%x", desc->bEndpointAddress);
	usb_debugf("      bmAttributes:     %d", desc->bmAttributes);
	usb_debugf("      wMaxPacketSize:   %d", desc->wMaxPacketSize[0] | desc->wMaxPacketSize[1]<<8);
	usb_debugf("      bInterval:        %d", desc->bInterval);
}

void usb_dump_hid_descriptor(usb_hid_descriptor_t *desc) {
	usb_debugf("    HID descriptor:");
	usb_debugf("      bLength:          %d", desc->bLength);
	usb_debugf("      bDescriptorType:  %d", desc->bDescriptorType);
	usb_debugf("      bcdHID:           0x%x", desc->bcdHID);
	usb_debugf("      bCountryCode:     %d", desc->bCountryCode);
	usb_debugf("      bNumDescriptors:  %d", desc->bNumDescriptors);
	usb_debugf("      bDescrType:       %d", desc->bDescrType);
	usb_debugf("      wDescriptorLength:%d", desc->wDescriptorLength[0] | desc->wDescriptorLength[1]<<8);
}
