#include <stdio.h>

#include "max3421e.h"
#include "timer.h"
#include "debug.h"
#include "spi.h"
#include "mist_cfg.h"

void max3421e_write_u08(uint8_t reg, uint8_t data) {
  spi_max_start();
  spi8(reg | MAX3421E_WRITE);
  spi8(data);
  spi_max_end();
}

uint8_t max3421e_read_u08(uint8_t reg) {
  spi_max_start();
  spi8(reg);
  uint8_t ret = spi_in();
  spi_max_end();
  return ret;
}

const uint8_t *max3421e_write(uint8_t reg, uint8_t n, const uint8_t* data) {
  spi_max_start();
  spi8(reg | MAX3421E_WRITE);
  spi_write(data, n);
  spi_max_end();
  return data+n;
}

// discard data if NULL ptr was provided
uint8_t *max3421e_read(uint8_t reg, uint8_t n, uint8_t* data) {
  spi_max_start();
  spi8(reg);

  if(data)
    spi_read(data, n);
  else
    //    spi_write(0, n);  // spi write sends something, but we don't care
    while(n--) spi8(0);

  spi_max_end();
  return data+n;
}

static uint8_t vbusState = MAX3421E_STATE_SE0;

uint16_t max3421e_reset() {
  /* reset chip */
  max3421e_write_u08( MAX3421E_USBCTL, MAX3421E_CHIPRES );
  max3421e_write_u08( MAX3421E_USBCTL, 0 );

  /* wait for pll to synchronize */
  for( uint32_t timeout = 0; timeout < 1000; timeout++ ) {
    if(( max3421e_read_u08( MAX3421E_USBIRQ ) & MAX3421E_OSCOKIRQ )) {
      // reset all interrupts
      max3421e_write_u08( MAX3421E_HIRQ, 0xff );
      max3421e_write_u08( MAX3421E_USBIRQ, 0xff );
      return 1;
    }
    delay_usec(25);
  }
  return 0;
}

void max3421e_busprobe() {
  usb_debugf("%s()", __FUNCTION__);

  uint8_t bus_sample = max3421e_read_u08( MAX3421E_HRSL );  // Get J,K status
  bus_sample &= ( MAX3421E_JSTATUS | MAX3421E_KSTATUS );    // zero the rest of the byte

  switch( bus_sample ) {
  case MAX3421E_JSTATUS:  // idle state
    if( max3421e_read_u08( MAX3421E_MODE ) & MAX3421E_LOWSPEED ) {
      max3421e_write_u08( MAX3421E_MODE, MAX3421E_MODE_HOST | MAX3421E_LOWSPEED | MAX3421E_HUBPRE );
      vbusState = MAX3421E_STATE_LSHOST;
    } else {
      max3421e_write_u08( MAX3421E_MODE, MAX3421E_MODE_HOST );
      vbusState = MAX3421E_STATE_FSHOST;
    }
    break;

  case MAX3421E_KSTATUS:  // opposite state
    if( max3421e_read_u08( MAX3421E_MODE ) & MAX3421E_LOWSPEED ) {
      max3421e_write_u08( MAX3421E_MODE, MAX3421E_MODE_HOST );
      vbusState = MAX3421E_STATE_FSHOST;
    } else {
      max3421e_write_u08( MAX3421E_MODE, MAX3421E_MODE_HOST | MAX3421E_LOWSPEED | MAX3421E_HUBPRE );
      vbusState = MAX3421E_STATE_LSHOST;
    }
    break;

  case MAX3421E_SE1:      // illegal state
    vbusState = MAX3421E_STATE_SE1;
    break;

  case MAX3421E_SE0:      // disconnected or reset state
    max3421e_write_u08( MAX3421E_MODE, MAX3421E_MODE_HOST );
    vbusState = MAX3421E_STATE_SE0;
    break;
  }
}

void max3421e_init() {
  usb_debugf("%s()", __FUNCTION__);

  timer_init();

  // switch to full duplex mode
  max3421e_write_u08(MAX3421E_PINCTL, MAX3421E_FDUPSPI);

  if( max3421e_reset() == 0 ) {
    iprintf("max3421e: pll init failed\n");
    return;
  }

  max3421e_write_u08(MAX3421E_PINCTL, MAX3421E_FDUPSPI);

  // read and output version
  iprintf("max3421e: chip rev: 0x%X\n", max3421e_read_u08(MAX3421E_REVISION));

  // enable pulldowns, set host mode
  max3421e_write_u08( MAX3421E_MODE, MAX3421E_MODE_HOST );
  delay_usec(50);

  // enable interrupts
  max3421e_write_u08( MAX3421E_HIEN, MAX3421E_HXFRDNIE );

  /* check if device is connected */

  // sample USB bus
  max3421e_write_u08( MAX3421E_HCTL, MAX3421E_SAMPLEBUS );

  // wait for sample operation to finish
  uint16_t sample_timeout = 1000;

  do {
    delay_usec(250);
    sample_timeout--;
  } while( sample_timeout && (max3421e_read_u08( MAX3421E_HCTL ) & MAX3421E_SAMPLEBUS) );

  // check if anything is connected
  max3421e_busprobe();

  // clear connection detect interrupt
  max3421e_write_u08( MAX3421E_HIRQ, MAX3421E_CONDETIRQ | MAX3421E_HXFRDNIRQ );

  // enable interrupts
  max3421e_write_u08( MAX3421E_CPUCTL, MAX3421E_IE );

  // switch off leds
  max3421e_write_u08(MAX3421E_IOPINS2, 0xff);

  return;
}

#include "timer.h"

FAST uint8_t max3421e_poll() {
  uint8_t hirq = max3421e_read_u08( MAX3421E_HIRQ );

  if( hirq & MAX3421E_CONDETIRQ ) {
    max3421e_write_u08( MAX3421E_HIRQ, MAX3421E_CONDETIRQ );
    usb_debugf("=> CONDETIRQ");
    max3421e_busprobe();
  }

  if( hirq & MAX3421E_BUSEVENTIRQ ) {
    max3421e_write_u08( MAX3421E_HIRQ, MAX3421E_BUSEVENTIRQ );
    usb_debugf("=> BUSEVENTIRQ");
  }

  if( hirq & MAX3421E_SNDBAVIRQ ) {
    max3421e_write_u08( MAX3421E_HIRQ, MAX3421E_SNDBAVIRQ );
  }

  // do LED animation on V1.3+ boards if enabled via cfg file
  if( mist_cfg.led_animation ) {
    static msec_t last = 0;

    if( timer_check(last, 100) ) {
      static uint8_t led_pattern = 0x01;

      // iprintf("irq src=%x, bus state %x\n", hirq, vbusState);
      // iprintf("host result %x\n", max3421e_read_u08( MAX3421E_HRSL));

      max3421e_write_u08(MAX3421E_IOPINS2, ~(led_pattern & 0x0f));

      if(!(led_pattern & 0x10)) {
        // knight rider left
        led_pattern <<= 1;
        if(!(led_pattern & 0x0f)) led_pattern = 0x18;
      } else {
        // knight rider right
        led_pattern = ((led_pattern & 0x0f) >> 1) | 0x10;
        if(!(led_pattern & 0x0f)) led_pattern = 0x01;
      }

      last = timer_get_msec();
    }
  }

  return vbusState;
}
