#include <stdio.h>
#include <string.h>

#include "timer.h"
#include "max3421e.h"
#include "debug.h"
#include "usb.h"

static uint8_t hubPre, hrsl;
static uint8_t usb_task_state;

void usb_reset_state() {
	usb_debugf("%s()", __FUNCTION__);

	hubPre = 0;
}

void usb_hw_init() {
	usb_debugf("%s()", __FUNCTION__);

	max3421e_init(); // init underlaying hardware layer

	usb_task_state = USB_DETACHED_SUBSTATE_INITIALIZE;

	usb_reset_state();
}

static inline uint8_t usb_wait_irq() {
	uint32_t start = timer_get_msec();

	// wait for transfer completion
	while( !usb_irq_active() );

	// get transfer result
	hrsl = max3421e_read_u08( MAX3421E_HRSL );

	// clear the interrupt
	max3421e_write_u08( MAX3421E_HIRQ, MAX3421E_HXFRDNIRQ );

	return ( hrsl & 0x0f );
}

static uint8_t usb_set_address(
	usb_device_t *dev, ep_t *ep, uint16_t *nak_limit) {

	*nak_limit = ( 1U << ( ( ep->nakPower > USB_NAK_MAX_POWER )
		? USB_NAK_MAX_POWER : ep->nakPower ) ) - 1;

	max3421e_write_u08( MAX3421E_PERADDR, dev->bAddress );

	uint8_t mode = max3421e_read_u08( MAX3421E_MODE ),
			new_mode = mode;

	if( dev->lowspeed ) {
		new_mode &= ~MAX3421E_HUBPRE;
		new_mode |= ( MAX3421E_LOWSPEED | hubPre );
	} else {
		new_mode &= ~( MAX3421E_LOWSPEED | MAX3421E_HUBPRE );
	}

	if( mode != new_mode ) {
		max3421e_write_u08( MAX3421E_MODE, new_mode );
	}

	return 0;
}

/* Dispatch usb packet. */
/* Assumes peripheral address is set and relevant buffer is loaded/empty */
/* If NAK, tries to re-send up to nak_limit times  */
/* If nak_limit == 0, do not count NAKs, exit after timeout */
/* return codes 0x00-0x0f are HRSLT (0x00 being success) */

static uint8_t usb_dispatchPkt(
	uint8_t token, ep_t *ep, uint16_t nak_limit ) {

	uint16_t nak_count = 0;
	uint8_t retry_count = 0;
	uint32_t timeout = timer_get_msec();

	while( 1 ) {

		// set toggle value
		if( token == tokIN ) {
			max3421e_write_u08( MAX3421E_HCTL,
				ep->rcvToggle ? MAX3421E_RCVTOG1 : MAX3421E_RCVTOG0 );
		} else if( token == tokOUT ) {
			max3421e_write_u08( MAX3421E_HCTL,
				ep->sndToggle ? MAX3421E_SNDTOG1 : MAX3421E_SNDTOG0 );
		}

		// clear previous interrupt(s)
		max3421e_write_u08( MAX3421E_HIRQ,
			( MAX3421E_HXFRDNIRQ | MAX3421E_FRAMEIRQ ) );

		// launch the transfer
		max3421e_write_u08( MAX3421E_HXFR, token | ep->addr );

		// wait for transfer completion
		uint8_t rcode = usb_wait_irq();

		switch( rcode ) {
		case hrNAK:
			nak_count++;
			if( nak_limit > 0 && nak_count == nak_limit )
				return rcode;
			delay_usec( USB_NACK_DELAY );
			break;

		case hrCRCERR:
		case hrPKTERR:
		case hrTIMEOUT:
			retry_count++;
			if( !USB_RETRY_LIMIT || retry_count == USB_RETRY_LIMIT )
				return rcode;
			delay_usec( USB_RETRY_DELAY );
			break;

		case hrTOGERR:
		case hrSUCCESS:
			if( token == tokIN )
				ep->rcvToggle = !!( hrsl & MAX3421E_RCVTOGRD );
			else if( token == tokOUT )
				ep->sndToggle = !!( hrsl & MAX3421E_SNDTOGRD );
			if( ep->addr == 0 )
				delay_usec( 100 );
			return rcode;

		default:
			return rcode;
		}
	}
}

static uint8_t usb_InTransfer(
	ep_t *ep, uint16_t nak_limit, uint16_t *nbytesptr, uint8_t* data ) {

	const uint16_t nbytes = *nbytesptr;
	const uint16_t maxpktsize = ep->maxPktSize;

	*nbytesptr = 0;

	// use a 'return' to exit this loop
	while( 1 ) {

		// dispatch packet
		uint8_t rcode = usb_dispatchPkt( tokIN, ep, nak_limit );

		// should be 0, indicating ACK
		if( rcode )
			return rcode;

		// number of received bytes
		uint16_t pktsize = max3421e_read_u08( MAX3421E_RCVBC );

		uint16_t bytes_left = (nbytes > *nbytesptr) ? (nbytes - *nbytesptr) : 0;
		uint8_t bytes_toread = MIN( pktsize, bytes_left );

		if( bytes_toread > 0 )
			data = max3421e_read( MAX3421E_RCVFIFO, bytes_toread, data );

		// clear the IRQ & free the buffer
		max3421e_write_u08( MAX3421E_HIRQ, MAX3421E_RCVDAVIRQ );

		// add this packet's byte count to total transfer length
		/* The transfer is complete under two conditions:           */
		/* 1. The device sent a short packet (L.T. maxPacketSize)   */
		/* 2. 'nbytes' have been transferred.                       */
		*nbytesptr += bytes_toread;

		// have we transferred 'nbytes' bytes?
		if( (pktsize < maxpktsize) || (*nbytesptr >= nbytes) )
			return 0;
	}
}

/* IN transfer to arbitrary endpoint. Assumes PERADDR is set. */
/* Handles multiple packets if necessary. Transfers 'nbytes' bytes. */
/* Keep sending INs and writes data to memory area pointed by 'data' */
/* rcode 0 if no errors. rcode 01-0f is relayed from dispatchPkt(). */
/* Rcode f0 means RCVDAVIRQ error, 0xef USB xfer timeout */

uint8_t usb_in_transfer(
	usb_device_t *dev, ep_t *ep, uint16_t *nbytesptr, uint8_t* data ) {

	uint16_t nak_limit = USB_NAK_DEFAULT;
	uint8_t rcode = usb_set_address( dev, ep, &nak_limit );

	if( rcode )
		return rcode;

	return usb_InTransfer( ep, nak_limit, nbytesptr, data );
}

static uint8_t usb_OutTransfer(
	ep_t *ep, uint16_t nak_limit, uint16_t nbytes, const uint8_t *data ) {

	uint16_t bytes_left = nbytes;
	const uint8_t maxpktsize = ep->maxPktSize;
	uint8_t zlp = 0;

	if( maxpktsize < 1 || maxpktsize > 64 )
		return USB_ERROR_INVALID_MAX_PKT_SIZE;

	uint32_t timeout = timer_get_msec();

	do {
		uint16_t bytes_tosend = MIN( maxpktsize, bytes_left );

		// filling OUT fifo
		if( bytes_tosend > 0 )
			max3421e_write( MAX3421E_SNDFIFO, bytes_tosend, data );

		// set number of bytes
		max3421e_write_u08( MAX3421E_SNDBC, bytes_tosend );

		// dispatch packet
		uint8_t rcode = usb_dispatchPkt( tokOUT, ep, nak_limit );

		if( rcode )
			return rcode;

		bytes_left -= bytes_tosend;
		data += bytes_tosend;

		// bulk only: zero length packet at end
		zlp = ( ep->type == EP_TYPE_BULK )
			&& ( bytes_left == 0 )
			&& ( bytes_tosend == maxpktsize );

	} while( bytes_left > 0 || zlp );

	return 0;
}

/* OUT transfer to arbitrary endpoint. */
/* Handles multiple packets if necessary. Transfers 'nbytes' bytes. */
/* rcode 0 if no errors. rcode 01-0f is relayed from HRSL */

uint8_t usb_out_transfer(
	usb_device_t *dev, ep_t *ep, uint16_t nbytes, const uint8_t* data ) {

	uint16_t nak_limit = USB_NAK_DEFAULT;
	uint8_t rcode = usb_set_address(dev, ep, &nak_limit);

	if( rcode )
		return rcode;

	return usb_OutTransfer( ep, nak_limit, nbytes, data );
}

/* Control transfer. Sets address, endpoint, fills control packet */
/* with necessary data, dispatches control packet, and initiates */
/* bulk IN transfer, depending on request. Actual requests are defined */
/* as inlines                   */
/* return codes:                */
/* 00       =   success         */
/* 01-0f    =   non-zero HRSLT  */

uint8_t usb_ctrl_req(
	usb_device_t *dev, uint8_t bmReqType, uint8_t bRequest,
	uint8_t wValLo, uint8_t wValHi, uint16_t wInd,
	uint16_t nbytes, uint8_t* dataptr) {

	uint16_t nak_limit = USB_NAK_DEFAULT;

	uint8_t rcode = usb_set_address( dev, &(dev->ep0), &nak_limit );
	if( rcode )
		return rcode;

	// request direction, IN or OUT
	bool direction = (( bmReqType & 0x80 ) > 0);

	setup_pkt_t setup_pkt;
	memset(&setup_pkt, 0, sizeof(setup_pkt_t));

	/* fill in setup packet */
	setup_pkt.ReqType_u.bmRequestType = bmReqType;
	setup_pkt.bRequest                = bRequest;
	setup_pkt.wVal_u.wValueLo         = wValLo;
	setup_pkt.wVal_u.wValueHi         = wValHi;
	setup_pkt.wIndex                  = wInd;
	setup_pkt.wLength                 = nbytes;

	// transfer to setup packet FIFO
	max3421e_write( MAX3421E_SUDFIFO, sizeof(setup_pkt_t), (uint8_t*)&setup_pkt );

	// dispatch packet
	rcode = usb_dispatchPkt( tokSETUP, &(dev->ep0), nak_limit );
	if( rcode ) // return HRSL if not zero
		return rcode;

	// data stage, if present
	if( dataptr != NULL && nbytes > 0 ) {
		if( direction ) { // IN transfer
			dev->ep0.rcvToggle = 1;
			rcode = usb_InTransfer( &(dev->ep0), nak_limit, &nbytes, dataptr );
		} else { // OUT transfer
			dev->ep0.sndToggle = 1;
			rcode = usb_OutTransfer( &(dev->ep0), nak_limit, nbytes, dataptr );
		}

		if( rcode )
			return rcode;
	}

	// Status stage
	return usb_dispatchPkt(
		direction ? tokOUTHS : tokINHS, &(dev->ep0), nak_limit );
}

uint8_t usb_poll() {
	uint8_t rcode = 0;
	static msec_t delay = 0;
	usb_device_t *dev = usb_get_devices();
	static bool lowspeed = false;

	// poll underlaying hardware layer,
	// modify USB task state if Vbus changed

	switch( max3421e_poll() ) {
	// illegal state
	case MAX3421E_STATE_SE1:
		if (usb_task_state != USB_DETACHED_SUBSTATE_ILLEGAL) {
			usb_task_state = USB_DETACHED_SUBSTATE_ILLEGAL;
			usb_debugf("=> SE1");
		}
		lowspeed = false;
		break;

	// disconnected
	case MAX3421E_STATE_SE0:
		if(( usb_task_state & USB_STATE_MASK ) != USB_STATE_DETACHED ) {
			usb_task_state = USB_DETACHED_SUBSTATE_INITIALIZE;
			usb_debugf("=> SE0");
		}
		lowspeed = false;
		break;

	// attached
	case MAX3421E_STATE_LSHOST:
		lowspeed = true;
		// intentional fall-through ...
	case MAX3421E_STATE_FSHOST:
		if( ( usb_task_state & USB_STATE_MASK ) != USB_STATE_DETACHED )
			break;
		usb_debugf("=> %s SPEED", lowspeed ? "LOW" : "FULL");
		usb_task_state = USB_ATTACHED_SUBSTATE_SETTLE;
		delay = timer_get_msec();
		break;
	}

	switch( usb_task_state ) {
	// settle time for just attached device
	case USB_ATTACHED_SUBSTATE_SETTLE:
		if( !timer_check(delay, USB_SETTLE_DELAY) )
			break;
		usb_debugf("=> SETTLE");
		usb_task_state = USB_ATTACHED_SUBSTATE_RESET_DEVICE;
 		break;

	// issue bus reset
	case USB_ATTACHED_SUBSTATE_RESET_DEVICE:
		usb_debugf("=> RESET");
		usb_task_state = USB_ATTACHED_SUBSTATE_WAIT_RESET_COMPLETE;
		max3421e_write_u08( MAX3421E_HCTL, MAX3421E_BUSRST );
		break;

	// waiting for bus reset is done
	case USB_ATTACHED_SUBSTATE_WAIT_RESET_COMPLETE:
		delay_usec(100);
		if( max3421e_read_u08( MAX3421E_HCTL ) & MAX3421E_BUSRST )
			break;
		usb_debugf("=> READY");
		uint8_t mode = max3421e_read_u08( MAX3421E_MODE );
		// start SOF generation
		max3421e_write_u08( MAX3421E_MODE, mode | MAX3421E_SOFKAENAB );
		usb_task_state = USB_ATTACHED_SUBSTATE_WAIT_SOF;
		// 20ms wait after reset per USB spec
		delay = timer_get_msec();
		break;

	// when first SOF received we can continue
	case USB_ATTACHED_SUBSTATE_WAIT_SOF:
		if( !timer_check( delay, 20 ) )
			break;
		// 20ms passed
		if( !( max3421e_read_u08( MAX3421E_HIRQ ) & MAX3421E_FRAMEIRQ ) )
			break;
		// when first SOF received we can continue
		max3421e_write_u08( MAX3421E_HIRQ, MAX3421E_FRAMEIRQ );
		usb_task_state = USB_STATE_CONFIGURING;
		break;

	// configure root device
	case USB_STATE_CONFIGURING:
		usb_debugf("=> CONFIGURING");
		usb_configure(0, 0, lowspeed);
		usb_task_state = USB_STATE_RUNNING;
		break;

	// polling next one device
	case USB_STATE_RUNNING: {
		usb_device_t *it = usb_get_next_device( true );
		if( !it )
			break;
		rcode = it->class->poll( it );
		if( rcode == 0 || rcode == hrNAK )
			break;
		if( rcode == hrJERR && (hrsl & (MAX3421E_JSTATUS | MAX3421E_KSTATUS)) ) {
			// device is disconnected
			it->class->release( it );
			it->bAddress = 0;
		} else {
			errorf("%s(%d): error 0x%02x",
				__FUNCTION__, it->bAddress, rcode);
		}
		break;
	}

	// bus failure
	case USB_DETACHED_SUBSTATE_ILLEGAL:
		usb_debugf("=> ILLEGAL");
		usb_task_state = USB_DETACHED_SUBSTATE_INITIALIZE;
		break;

	// just remove everything ...
	case USB_DETACHED_SUBSTATE_INITIALIZE:
		usb_debugf("=> RESET ALL");
		usb_reset_state();
		for( uint32_t i=0; i<USB_NUMDEVICES; i++ ) {
			if( dev[i].bAddress && dev[i].class ) {
				rcode = dev[i].class->release( &dev[i] );
				dev[i].bAddress = 0;
			}
		}
		usb_task_state = USB_DETACHED_SUBSTATE_WAIT_FOR_DEVICE;
		break;

	case USB_DETACHED_SUBSTATE_WAIT_FOR_DEVICE:
		break;
	}

	return rcode;
}

void usb_SetHubPreMask() {
	hubPre |= MAX3421E_HUBPRE;
};

void usb_ResetHubPreMask() {
	hubPre &= ~MAX3421E_HUBPRE;
};
