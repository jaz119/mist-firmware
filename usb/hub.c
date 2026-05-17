#include <stdio.h>

#include "timer.h"
#include "usb.h"
#include "max3421e.h"
#include "debug.h"

// Clear Hub Feature
static uint8_t usb_hub_clear_hub_feature(usb_device_t *dev, uint8_t fid) {
  return usb_ctrl_req(dev, USB_HUB_REQ_CLEAR_HUB_FEATURE,
    USB_REQUEST_CLEAR_FEATURE, fid, 0, 0, 0, NULL);
}

// Clear Port Feature
static uint8_t usb_hub_clear_port_feature(usb_device_t *dev, uint8_t fid, uint8_t port, uint8_t sel) {
  return usb_ctrl_req(dev, USB_HUB_REQ_CLEAR_PORT_FEATURE,
    USB_REQUEST_CLEAR_FEATURE, fid, 0, ((uint16_t)port|((uint16_t)sel<<8)), 0, NULL);
}

// Get Hub Descriptor
static uint8_t usb_hub_get_hub_descriptor(usb_device_t *dev, uint8_t index,
    uint16_t nbytes, usb_hub_descriptor_t *dataptr ) {
  return usb_ctrl_req(dev, USB_HUB_REQ_GET_HUB_DESCRIPTOR,
    USB_REQUEST_GET_DESCRIPTOR, index, 0x29, 0, nbytes, (uint8_t*)dataptr);
}

// Set Port Feature
static uint8_t usb_hub_set_port_feature(usb_device_t *dev, uint8_t fid, uint8_t port, uint8_t sel) {
  return usb_ctrl_req(dev, USB_HUB_REQ_SET_PORT_FEATURE,
    USB_REQUEST_SET_FEATURE, fid, 0, ((uint16_t)port|((uint16_t)sel<<8)), 0, NULL);
}

// Get Port Status
static uint8_t usb_hub_get_port_status(usb_device_t *dev, uint8_t port, uint16_t nbytes, uint8_t* dataptr) {
  return usb_ctrl_req(dev, USB_HUB_REQ_GET_PORT_STATUS,
    USB_REQUEST_GET_STATUS, 0, 0, port, nbytes, dataptr);
}

static uint8_t usb_hub_parse_conf(
  usb_device_t *dev, uint8_t conf, uint16_t len, ep_t *pep) {

  uint8_t rcode;

  if (len > USB_MAX_CONFIG_DESC_SIZE)
    return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;

  union buf_u {
    usb_configuration_descriptor_t conf_desc;
    usb_interface_descriptor_t iface_desc;
    usb_endpoint_descriptor_t ep_desc;
    uint8_t raw[len];
  } buf, *p;

  if ((rcode = usb_get_conf_descr(dev, len, conf, &buf.conf_desc)))
    return rcode;

  /* scan through all descriptors */
  p = &buf;
  while (len > 0) {
    switch(p->conf_desc.bDescriptorType) {

    case USB_DESCRIPTOR_CONFIGURATION:
      break;

    case USB_DESCRIPTOR_INTERFACE:
      usb_dump_interface_descriptor(&p->iface_desc);
      break;

    case USB_DESCRIPTOR_ENDPOINT:
      usb_dump_endpoint_descriptor(&p->ep_desc);
      if ((p->ep_desc.bmAttributes & 0x03) == 0x03 && (p->ep_desc.bEndpointAddress & 0x80)) {
        pep->epAddr     = p->ep_desc.bEndpointAddress & 0x0f;
        pep->maxPktSize = p->ep_desc.wMaxPacketSize[0];
        return 0;
      }
      break;

    default:
      usb_debugf("hub: unsupported descriptor type %d size %d",
        p->raw[1], p->raw[0]);
      break;
    }

    if (!p->conf_desc.bLength || p->conf_desc.bLength > len)
      break;

    // advance to next descriptor
    len -= p->conf_desc.bLength;
    p = (union buf_u *)(p->raw + p->conf_desc.bLength);
  }

  if (len != 0) {
    usb_debugf("config underrun: %d", len);
    return USB_ERROR_CONFIGURATION_SIZE_MISMATCH;
  }

  return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
}

static uint8_t usb_hub_init(
  usb_device_t *dev, usb_device_descriptor_t *dev_desc) {

  usb_debugf("%s(%d)", __FUNCTION__, dev->bAddress);

  uint8_t rcode;
  usb_hub_info_t *info = &(dev->hub_info);

  union {
    usb_configuration_descriptor_t conf_desc;
    usb_hub_descriptor_t hub_desc;
  } buf;

  // Reset status
  info->nrPorts = 0;
  info->lastPollTime = 0;
  info->pollEnable = false;
  info->resetMask = 0;

  info->ep.epAddr     = 1;
  info->ep.maxPktSize = 8; // kludge
  info->ep.epType     = EP_TYPE_INTR;
  info->ep.epAttribs  = 0;
  info->ep.bmNakPower = USB_NAK_NOWAIT;

  // Extract device class from device descriptor
  // If device class is not a hub return
  if (dev_desc->bDeviceClass != USB_CLASS_HUB)
    return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;

  // Get hub descriptor
  rcode = usb_hub_get_hub_descriptor(dev, 0, 9, &buf.hub_desc);

  if (rcode) {
    usb_debugf("hub: failed to get descriptor, error 0x%02x", rcode);
    return rcode;
  }

  // Save number of ports for future use
  info->nrPorts = buf.hub_desc.bNbrPorts;

  // Read configuration Descriptor in Order To Obtain Proper Configuration Value
  rcode = usb_get_conf_descr(dev, sizeof(usb_configuration_descriptor_t), 0, &buf.conf_desc);
  if (rcode) {
    usb_debugf("hub: failed to read configuration descriptor, error 0x%02x", rcode);
    return rcode;
  }

  usb_dump_conf_descriptor(&buf.conf_desc);

  rcode = usb_hub_parse_conf(dev, 0, buf.conf_desc.wTotalLength, &info->ep);
  if (rcode) {
    usb_debugf("hub: failed to get endpoint data, error 0x%02x", rcode);
    return rcode;
  }

  // Set Configuration Value
  rcode = usb_set_conf(dev, buf.conf_desc.bConfigurationValue);
  if (rcode) {
    usb_debugf("hub: failed to set configuration to %d, error 0x%02x",
      buf.conf_desc.bConfigurationValue, rcode);
    return rcode;
  }

  // Power on all ports
  for (uint32_t i=1; i<=info->nrPorts; i++)
    usb_hub_set_port_feature(dev, HUB_FEATURE_PORT_POWER, i, 0); // HubPortPowerOn(i);

  if (!dev->parent)
    usb_SetHubPreMask();

  iprintf("HUB initialized\n");
  info->pollEnable = true;

  return 0;
}

static uint8_t usb_hub_release(usb_device_t *dev) {
  usb_debugf("%s()", __FUNCTION__);

  // Root hub unplugged
  if (!dev->parent)
    usb_ResetHubPreMask();

  return 0;
}

static void usb_hub_show_port_status(
  uint8_t port, uint16_t status, uint16_t changed) {

  usb_debugf("%s(%d)", __FUNCTION__, port);
  if(status & USB_HUB_PORT_STATUS_PORT_CONNECTION)    usb_debugf(" connected");
  if(status & USB_HUB_PORT_STATUS_PORT_ENABLE)        usb_debugf(" enabled");
  if(status & USB_HUB_PORT_STATUS_PORT_SUSPEND)       usb_debugf(" suspended");
  if(status & USB_HUB_PORT_STATUS_PORT_OVER_CURRENT)  usb_debugf(" over current");
  if(status & USB_HUB_PORT_STATUS_PORT_RESET)         usb_debugf(" reset");
  if(status & USB_HUB_PORT_STATUS_PORT_POWER)         usb_debugf(" powered");
  if(status & USB_HUB_PORT_STATUS_PORT_LOW_SPEED)     usb_debugf(" low speed");
  if(status & USB_HUB_PORT_STATUS_PORT_HIGH_SPEED)    usb_debugf(" high speed");
  if(status & USB_HUB_PORT_STATUS_PORT_TEST)          usb_debugf(" test");
  if(status & USB_HUB_PORT_STATUS_PORT_INDICATOR)     usb_debugf(" indicator");

  usb_debugf("changes on port %d:", port);
  if(changed & USB_HUB_PORT_STATUS_PORT_CONNECTION)   usb_debugf(" connected");
  if(changed & USB_HUB_PORT_STATUS_PORT_ENABLE)       usb_debugf(" error");
  if(changed & USB_HUB_PORT_STATUS_PORT_SUSPEND)      usb_debugf(" suspended");
  if(changed & USB_HUB_PORT_STATUS_PORT_OVER_CURRENT) usb_debugf(" over current");
  if(changed & USB_HUB_PORT_STATUS_PORT_RESET)        usb_debugf(" reset");
}

static uint8_t usb_hub_port_status_change(
  usb_device_t *dev, uint8_t port, const hub_event_t *evt) {

  const uint16_t mask = (1 << port);
  usb_hub_info_t *info = &(dev->hub_info);
  uint8_t rcode = 0;

  iprintf("hub: port %u: status 0x%x, change 0x%x\n",
    port, evt->bmStatus, evt->bmChange);

  if (!(evt->bmStatus & USB_HUB_PORT_STATUS_PORT_POWER))
    usb_hub_set_port_feature(dev, HUB_FEATURE_PORT_POWER, port, 0);

  if (evt->bmStatus & USB_HUB_PORT_STATUS_PORT_OVER_CURRENT)
    usb_hub_clear_port_feature(dev, HUB_FEATURE_C_PORT_OVER_CURRENT, port, 0);

  if (evt->bmChange & USB_HUB_PORT_STATUS_PORT_ENABLE)
    usb_hub_clear_port_feature(dev, HUB_FEATURE_C_PORT_ENABLE, port, 0);

  if (evt->bmChange & USB_HUB_PORT_STATUS_PORT_SUSPEND)
    usb_hub_clear_port_feature(dev, HUB_FEATURE_C_PORT_SUSPEND, port, 0);
  if (evt->bmStatus & USB_HUB_PORT_STATUS_PORT_SUSPEND)
    usb_hub_clear_port_feature(dev, HUB_FEATURE_PORT_SUSPEND, port, 0);

  if (evt->bmChange & USB_HUB_PORT_STATUS_PORT_CONNECTION) {
    usb_hub_clear_port_feature(dev, HUB_FEATURE_C_PORT_CONNECTION, port, 0);
    usb_release_device(dev->bAddress, port);

    if (evt->bmStatus & USB_HUB_PORT_STATUS_PORT_CONNECTION) {
      if (!(info->resetMask & mask)) {
        info->resetMask |= mask;
        warningf("hub: port %d: CONNECT", port);
        usb_hub_clear_port_feature(dev, HUB_FEATURE_C_PORT_RESET, port, 0);
        timer_delay_msec(50);
        usb_hub_set_port_feature(dev, HUB_FEATURE_PORT_RESET, port, 0);
        return HUB_ERROR_PORT_HAS_BEEN_RESET;
      }
    } else {
      warningf("hub: port %d: DISCONNECT", port);
    }
  }

  if (evt->bmChange & USB_HUB_PORT_STATUS_PORT_RESET) {
    usb_hub_clear_port_feature(dev, HUB_FEATURE_C_PORT_RESET, port, 0);
    warningf("hub: port %d: RESET", port);

    if (evt->bmStatus & USB_HUB_PORT_STATUS_PORT_CONNECTION) {
      timer_delay_msec(20);
      bool isLS = !!(evt->bmStatus & USB_HUB_PORT_STATUS_PORT_LOW_SPEED);
      rcode = usb_configure(dev->bAddress, port, isLS);
      if (rcode) {
        errorf("hub: port %d: configure error 0x%02x", port, rcode);
        usb_release_device(dev->bAddress, port);
      }
    }
  }

  info->resetMask &= ~mask;
  return rcode;
}

static uint8_t usb_hub_check_hub_status(usb_device_t *dev, uint8_t ports) {
  usb_hub_info_t *info = &(dev->hub_info);

  uint8_t ALIGNED(4) buf[8];
  ALIGNED(4) hub_event_t evt;
  uint16_t read = 1;

  uint8_t rcode = usb_in_transfer(dev, &(info->ep), &read, buf);
  if (rcode)
    return rcode;

  for (uint8_t port = 0; port <= ports; port++) {
    if (buf[0] & (1 << port)) {
      if (port == 0) {
        usb_hub_clear_hub_feature(dev, HUB_FEATURE_C_HUB_LOCAL_POWER);
        usb_hub_clear_hub_feature(dev, HUB_FEATURE_C_HUB_OVER_CURRENT);
        continue;
      }

      if (usb_hub_get_port_status(dev, port, sizeof(evt.evtBuff), evt.evtBuff) == 0) {
        rcode = usb_hub_port_status_change(dev, port, &evt);
        if (rcode == HUB_ERROR_PORT_HAS_BEEN_RESET)
          break;
      }
    }
  }

  return 0;
}

static uint8_t usb_hub_poll(usb_device_t *dev) {
  usb_hub_info_t *info = &(dev->hub_info);
  uint8_t rcode = 0;

  if (!info->pollEnable)
    return 0;

  if (timer_check(info->lastPollTime, 50)) {
    // poll 20 times a second
    rcode = usb_hub_check_hub_status(dev, info->nrPorts);
    info->lastPollTime = timer_get_msec();
  }

  return rcode;
}

const usb_device_class_config_t usb_hub_class = {
  USB_HUB,
  usb_hub_init,
  usb_hub_release,
  usb_hub_poll
};
