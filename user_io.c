#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hardware.h>
#include <user_io.h>
#include <user_io_hid.h>
#include <usb/usb.h>
#include <data_io.h>
#include <8bit/core.h>
#include <minimig/core.h>
#include <config_union.h>
#include <psx.h>
#include <tos.h>
#include "cdc_control.h"
#include "osd.h"
#include "mist_cfg.h"
#include "mmc.h"
#include "arc_file.h"
#include <FatFs/diskio.h>
#include "serial_sink.h"
#include <utils.h>
#include <debug.h>

#ifdef HAVE_HDMI
#include "it6613/HDMI_TX.h"
#define HDMI_FREQ 1000
static uint8_t i2c_flags;
static uint32_t hdmi_timer;
static bool hdmi_detected = 0;
static uint8_t hdmi_hiclk = 0;
#endif // HAVE_HDMI

extern char s[OSD_BUF_SIZE];

// all of supported cores
static const struct {
	uint32_t type;
	const user_io_core_t *core;
} cores_list[] = {
	{ CORE_TYPE_MINIMIG_V2, &minimig_v2_core },
	{ CORE_TYPE_MISTERY,    &mistery_core },
	{ CORE_TYPE_ARCHIE,     &archie_core  },
	{ CORE_TYPE_8BIT,       &generic_core },
};

const user_io_core_t *core = NULL;
uint32_t core_type = CORE_TYPE_UNKNOWN;

#define RTC_FREQ 500
static uint32_t rtc_timer;

#define NIC_FREQ 1
static uint32_t nic_timer;

// set by OSD code to suppress forwarding of those keys
// to the core which may be in use by an active OSD
bool osd_is_visible = false;

static char umounted; // 1st image is file or direct SD?
ALIGNED(4) static char cache_buffer[1024];
static uint8_t buffer_drive_index = 0;
static uint32_t buffer_lba = 0xffffffff;

static void user_io_send_buttons(bool);

void user_io_reset()
{
	// no sd card image selected, SD card accesses will go directly
	// to the card (first slot, and only until the first unmount)
	umounted = 0;
	toc.valid = 0;
	for (int n = 0; n < ARRAY_SIZE(sd_image); n++) {
		IDXClose(&sd_image[n]);
	}
	if (core && core->eject_all) {
		core->eject_all();
	}
	core_features = 0;
	user_io_set_core_mod(0);
	user_io_hid_reset();
	memset(&config, 0, sizeof(united_config_t));
	conf_items = 0;
	core = NULL;
}

void user_io_init()
{
	user_io_reset();
	user_io_hid_init();

	if (VIDEO_KEEP_VAR != VIDEO_KEEP_VALUE) {
		VIDEO_ALTERED_VAR = 0;
	}

	VIDEO_KEEP_VAR = 0;

	if (MenuButton()) {
		DEBUG_MODE_VAR = DEBUG_MODE ? 0 : DEBUG_MODE_VALUE;
	}

	iprintf("Debug mode: %s\n",
		DEBUG_MODE ? "on" : "off");

	warningf("DIP switches (1/2): %s/%s",
		is_dip_switch1_on() ? "on" : "off",
		is_dip_switch2_on() ? "on" : "off");
}

void user_io_detect_core_type()
{
	EnableIO();
	core_type = SPI(0xff);
	DisableIO();

#ifdef SD_NO_DIRECT_MODE
	rom_direct_upload = 0;
#else
	rom_direct_upload = (core_type & 0x10) >> 4; // bit 4 - direct upload support
#endif

	core_name[0] = 0;
	core_type &= 0xef;

	warningf("Core Id: 0x%02lx", core_type);

	for (int i = 0; i < ARRAY_SIZE(cores_list); i++)
	{
		if (cores_list[i].type != core_type)
			continue;

		core = cores_list[i].core;

		if (!core || !core->name)
			break;

		strncpy(core_name, core->name, sizeof(core_name));
		iprintf("Identified %s core\n", core_name);
		return;
	}

	errorf("Unable to identify core");
	core_type = CORE_TYPE_UNKNOWN;
}

void user_io_init_core()
{
	usb_device_t *dev = usb_get_device(USB_NIC);

	if (core && core->init) {
		core->init();
	}

	user_io_send_buttons(true);

  	if (dev) {
		usb_nic_class_config_t *nic = (usb_nic_class_config_t *) dev->class;
		user_io_eth_send_mac(nic->get_mac(dev));
	}

#ifdef HAVE_HDMI
	hdmi_detected = false;
	hdmi_hiclk = 0;

	if ((core_type == CORE_TYPE_8BIT && core_features & FEAT_HDMI)
		|| core_type == CORE_TYPE_MISTERY
		|| core_type == CORE_TYPE_MINIMIG_V2
		|| core_type == CORE_TYPE_ARCHIE)
	{
		hdmi_detected = HDMITX_Init();
		if (hdmi_detected) HDMITX_ChangeVideoTiming(1);
	}
#endif
}

static void user_io_send_rtc()
{
	ctime_t date;

	if (!GetRTC((uint8_t *)&date))
		return;

	spi_uio_cmd_cont(UIO_SET_RTC);
	spi8(bin2bcd(date[T_SEC]));
	spi8(bin2bcd(date[T_MIN]));
	spi8(bin2bcd(date[T_HOUR]));
	spi8(bin2bcd(date[T_DAY]));
	spi8(bin2bcd(date[T_MONTH]));
	spi8(bin2bcd(date[T_YEAR]  - 100));
	spi8(bin2bcd(date[T_WDAY]) - 1); // day 1-7 -> 0-6
	spi8(0x40); // flag
	DisableIO();
}

// transmit serial/rs232 data into core
void user_io_serial_tx(char *chr, uint16_t cnt)
{
	spi_uio_cmd_cont(UIO_SIO_OUT);
	while (cnt--) spi8(*chr++);
	DisableIO();
}

bool user_io_serial_status(
	serial_status_t *status_in, uint8_t status_out)
{
	uint8_t *p = (uint8_t *)status_in;
	spi_uio_cmd_cont(UIO_SERIAL_STAT);

	// first byte returned by core must be "magic"
	// otherwise the core doesn't support this request
	if (SPI(status_out) != 0xa5)
	{
		DisableIO();
		return false;
	}

	// read the whole structure
	for (uint32_t i = 0; i < sizeof(serial_status_t); i++)
		*p++ = spi_in();

	DisableIO();
	return true;
}

// transmit midi data into core
static inline void user_io_midi_tx(uint8_t c)
{
	spi_uio_cmd8(UIO_MIDI_OUT, c);
}

// set SD card info in FPGA (CSD, CID)
void user_io_sd_set_config()
{
	uint8_t data[33];

	// get CSD and CID from SD card
	if (fat_uses_mmc())
	{
		MMC_GetCID(data);
		MMC_GetCSD(data + 16);
		// byte 32 is a generic config byte
		data[32] = !!MMC_IsSDHC();
	} else {
		// synthetic CSD for non-MMC storage
		uint32_t capacity;
		disk_ioctl(fs.pdrv, GET_SECTOR_COUNT, &capacity);
		memset(data, 0, sizeof(data));
		data[16 + 0] = 0x40;
		data[16 + 1] = 0x0e;
		data[16 + 3] = 0x32;
		data[16 + 4] = 0x5b;
		data[16 + 5] = 0x59;
		data[16 + 6] = 0x90;
		data[16 + 7] = (capacity >> 26) & 0xff;
		data[16 + 8] = (capacity >> 18) & 0xff;
		data[16 + 9] = (capacity >> 10) & 0xff;
		data[16 + 10] = 0x5f;
		data[16 + 11] = 0xc0;
		data[32] = 1; // SDHC
	}

	// and forward it to the FPGA
	spi_uio_cmd_cont(UIO_SET_SDCONF);
	spi_write(data, sizeof(data));
	DisableIO();
}

void user_io_sd_ack(uint8_t drive_index)
{
	spi_uio_cmd_cont(UIO_SD_ACK);
	spi8(drive_index);
	DisableIO();
}

// read 8+32 bit sd card status word from FPGA
static uint8_t user_io_sd_get_status(
	uint32_t *lba, uint8_t *drive_index, uint8_t *blksz)
{
	uint32_t s;
	uint8_t c;

	*drive_index = 0;
	*blksz = 0;

	spi_uio_cmd_cont(UIO_GET_SDSTAT);
	c = spi_in();

	if ((c & 0xf0) == 0x60)
	{
		uint8_t tmp = spi_in();
		*drive_index = tmp & 0x03;
		*blksz = (tmp >> 4) & 0x01;
	}

	s = spi_in();
	s = (s << 8) | spi_in();
	s = (s << 8) | spi_in();
	s = (s << 8) | spi_in();
	DisableIO();

	if (lba) *lba = s;
	return c;
}

// read 32 bit ethernet status word from FPGA
uint32_t user_io_eth_get_status()
{
	spi_uio_cmd_cont(UIO_ETH_STATUS);
	uint32_t s = spi_in();
	s = (s << 8) | spi_in();
	s = (s << 8) | spi_in();
	s = (s << 8) | spi_in();
	DisableIO();
	return s;
}

// send ethernet mac address into FPGA
void user_io_eth_send_mac(const uint8_t *mac)
{
	spi_uio_cmd_cont(UIO_ETH_MAC);
	for (int i = 0; i < 6; i++)
		spi8(*mac++);
	DisableIO();
}

// write ethernet frame to FPGAs rx buffer
void user_io_eth_send_rx_frame(uint8_t *s, uint16_t len)
{
	spi_uio_cmd_cont(UIO_ETH_FRM_OUT);
	while (len--) SPI(*s++);
	// spi_write(s, len);
	spi8(0); // one additional byte to allow fpga to store the previous one
	DisableIO();
}

// read ethernet frame from FPGAs tx buffer
void user_io_eth_receive_tx_frame(uint8_t *d, uint16_t len)
{
	spi_uio_cmd_cont(UIO_ETH_FRM_IN);
	while (len--) *d++ = spi_in();
	DisableIO();
}

// usb networking
static void user_io_nic_poll()
{
	usb_device_t *dev = usb_get_device(USB_NIC);

	if (!dev)
		return;

	usb_nic_class_config_t *nic = (usb_nic_class_config_t *) dev->class;

	if (!nic->link_is_up(dev))
		return;

	const uint32_t status = user_io_eth_get_status();
	const uint8_t code = NIC_STAT_CODE(status);

	if (code == NIC_STATUS_TX_PENDING)
	{
		// packet is ready to transmit
		nic->send_pkt(
			dev, user_io_eth_receive_tx_frame,
			NIC_STAT_TBCR(status));
	}

	if (status & NIC_STAT_ISR_PRX)
		return;

	if (code == NIC_STATUS_IDLE || code == NIC_STATUS_TX_DONE)
	{
		// core is ready to receive packet (64 bytes minimum)
		nic->recv_pkt(
			dev, user_io_eth_send_rx_frame);
	}
}

char user_io_cue_mount(const unsigned char *name, int index)
{
	char res = CUE_RES_OK;
	toc.valid = 0;

	if (name) {
		res = cue_parse(name, &sd_image[index]);
	}

#ifdef HAVE_PSX
	if (core_features & FEAT_PSX)
		psx_mount_cd(name);
#endif

	// send mounted image size first then notify about mounting
	EnableIO();
	SPI(UIO_SET_SDINFO);
	// use LE version, so following BYTE(s) may be used for size extension in the future
	spi32le(toc.valid ? f_size(&toc.file->file) : 0);
	spi32le(toc.valid ? f_size(&toc.file->file) >> 32 : 0);
	spi32le(0); // reserved for future expansion
	spi32le(0); // reserved for future expansion
	DisableIO();

	// notify core of possible sd image change
	spi_uio_cmd8(UIO_SET_SDSTAT, 1);
	return res;
}

static inline uint8_t sd_index(int index)
{
	if (core_type == CORE_TYPE_ARCHIE) {
		return (index + 2) & 3;
	} else {
		return index & 3;
	}
}

bool user_io_is_mounted(int index)
{
	return sd_image[sd_index(index)].valid;
}

bool user_io_file_mount(const unsigned char *name, int index)
{
	uint8_t slot = sd_index(index);
	IDXFile *idxfile = &sd_image[slot];

	buffer_lba = 0xffffffff; // invalidate cache

	if (idxfile->valid)
	{
		debugf("unmounting slot %d", slot);
		IDXClose(idxfile);
	}

	if (name)
	{
		FRESULT res = IDXOpen(idxfile, name, FA_READ | FA_WRITE);

		if (res != FR_OK)
			res = IDXOpen(idxfile, name, FA_READ);

		if (res == FR_OK)
		{
			iprintf("%s: %lu byte(s) into slot: %d\n",
				__FUNCTION__, (uint32_t) f_size(&idxfile->file), slot);

			// build index for fast random access
			IDXIndex(idxfile, slot);
		}
		else
		{
			errorf("user_io: mount %s file, error %d", name, res);
			return false;
		}
	}
	else
	{
		if (!index)
			umounted = 1;
	}

	// send mounted image size first then notify about mounting
	EnableIO();
	SPI(UIO_SET_SDINFO);
	// use LE version, so following BYTE(s) may be used for size extension in the future
	spi32le(idxfile->valid ? f_size(&idxfile->file) : 0);
	spi32le(idxfile->valid ? f_size(&idxfile->file) >> 32 : 0);
	spi32le(0); // reserved for future expansion
	spi32le(0); // reserved for future expansion
	DisableIO();

	// notify core of possible sd image change
	spi_uio_cmd8(UIO_SET_SDSTAT, index);
	return idxfile->valid;
}

static void user_io_send_buttons(bool force)
{
	static uint8_t key_map = 0;

	// frequently poll the adc the switches
	// and buttons are connected to
	PollADC();

	uint8_t map = 0;

	if (is_dip_switch1_on())
		map |= SWITCH_DEBUG;

	if (is_dip_switch2_on())
		map |= SWITCH_CORE;

	if (MenuButton())
		map |= BUTTON_MENU;
	else if (UserButton())
		map |= BUTTON_USER;

	if (kbd_reset)
		map |= BUTTON_USER;

	if (!mist_cfg.keep_video_mode)
		VIDEO_ALTERED_VAR = 0;

	if (VIDEO_ALTERED_VAR & 1)
	{
		if (VIDEO_SD_DISABLE_VAR)
			map |= CONF_SCANDBL_DIS;
	}
	else
	{
		if (mist_cfg.scandoubler_disable)
			map |= CONF_SCANDBL_DIS;
	}

	if (VIDEO_ALTERED_VAR & 2)
	{
		if (VIDEO_YPBPR_VAR)
			map |= CONF_YPBPR;
	}
	else
	{
		if (mist_cfg.ypbpr)
			map |= CONF_YPBPR;
	}

	if (mist_cfg.csync_disable)
		map |= CONF_CSYNC_DISABLE;

	if (mist_cfg.sdram64)
		map |= CONF_SDRAM64;

	if ((map != key_map) || force)
	{
		key_map = map;
		spi_uio_cmd8(UIO_BUT_SW, map);

		debugf("Sending keymap");
	}
}

void user_io_poll()
{
	// check of core has changed from a good one
	// to a not supported on as this likely means
	// that the user is reloading the core via jtag
	uint8_t ct;
	static uint8_t ct_cnt = 0;

	EnableIO();
	ct = SPI(0xff);
	DisableIO();

	if ((ct & 0xef) == core_type)
	{
		ct_cnt = 0; // same core type, everything is fine
	}
	else
	{
		// core type has changed
		if (++ct_cnt == 255)
		{
			USB_LOAD_VAR = USB_LOAD_VALUE;

			// wait for a new valid core id to appear
			while ((ct & 0xe0) != 0xa0)
			{
				EnableIO();
				ct = SPI(0xff);
				DisableIO();
			}

			// reset io controller to cope with new core
			MCUReset();
		}
	}

	if (core_type == CORE_TYPE_MISTERY)
	{
		uint32_t redirect = tos_get_cdc_control_redirect();

		// check for input data on usart
		USART_Poll();

		unsigned char c = 0;

		// check for incoming serial data
		// this is directly forwarded to
		// the arm rs232 and mixes with debug output
		// Useful for debugging only of e.g. the diagnostic cartridge
#ifdef USB_PL2303_CDC
		if (!pl2303_is_blocked())
		{
			spi_uio_cmd_cont(UIO_SIO_IN);

			while (spi_in() && !pl2303_is_blocked())
			{
				c = spi_in();

				// if a serial/usb adapter is connected
				// it has precesence over any other sink
				if (pl2303_present())
					pl2303_tx_byte(c);
				else
				{
					if (c != 0xff)
						putchar(c);

					// forward to USB if redirection via USB/CDC enabled
					if (redirect == CDC_REDIRECT_RS232)
						cdc_control_tx(c);
				}
			}
			DisableIO();
		}
#endif
		// check for incoming parallel/midi data
		if ((redirect == CDC_REDIRECT_PARALLEL) || (redirect == CDC_REDIRECT_MIDI))
		{
			spi_uio_cmd_cont((redirect == CDC_REDIRECT_PARALLEL) ? UIO_PARALLEL_IN : UIO_MIDI_IN);

			// character 0xff is returned if FPGA isn't configured
			c = 0;
			while (spi_in() && (c!= 0xff))
			{
				c = spi_in();
				cdc_control_tx(c);
			}

			DisableIO();

			// always flush when doing midi to reduce latencies
			if (redirect == CDC_REDIRECT_MIDI)
				cdc_control_flush();
		}
	}

	user_io_hid_poll();
	user_io_send_buttons(false);

	if (CheckTimer(nic_timer))
	{
		user_io_nic_poll();
		nic_timer = GetTimer(NIC_FREQ);
	}

	if (CheckTimer(rtc_timer))
	{
		rtc_timer = GetTimer(RTC_FREQ);
		user_io_send_rtc();
	}

	// serial IO - TODO: merge with MiSTery
	if (core_type == CORE_TYPE_8BIT)
	{
		uint32_t c = 1, f, p = 0;

		// check for input data on usart
		USART_Poll(); // TODO: currently doesn't send anything for 8BIT

		// check for serial data to be sent

		// check for incoming serial data.
		spi_uio_cmd_cont(UIO_SIO_IN);

		// status byte is 1000xxxA with A=1
		// if data is available xxx is the channel
		if (((f = spi_in()) & 0x81) == 0x81)
		{
			uint8_t channel = (f >> 1) & 0x07;
			uint8_t lastf = f;
			serial_sink_t *sink = serial_sink_get(channel);

			if (sink)
			{
				if (sink->begin)
					sink->begin();

				while (f == lastf && p < sink->burst)
				{
					c = spi_in();
					sink->process_data(c);
					f = spi_in();
					p++;
				}

				if (sink->end)
					sink->end();
			}
		}

		DisableIO();
	}

	// sd card emulation
	if ((core_type == CORE_TYPE_8BIT) ||
		(core_type == CORE_TYPE_MISTERY) ||
		(core_type == CORE_TYPE_ARCHIE))
	{
		uint32_t lba;
		uint8_t drive_index;
		uint8_t blksz;
		uint8_t c = user_io_sd_get_status(&lba, &drive_index, &blksz);

		// valid sd commands start with "5x" (old API), or "6x" (new API)
		// to avoid problems with cores that don't implement this command
		if ((c & 0xf0) == 0x50 || (c & 0xf0) == 0x60)
		{
#if 0
			// debug: If the io controller reports and non-sdhc card, then
			// the core should never set the sdhc flag
			if ((c & 3) && !MMC_IsSDHC() && (c & 0x04))
				warningf("SDHC access to non-SDHC card");
#endif
			// check if core requests configuration
			if (c & 0x08)
			{
				debugf("Core requests SD config");
				user_io_sd_set_config();
			}

			// check if system is trying to access
			// a sdhc card from a sd/mmc setup

			// check if an SDHC card is inserted
			if (MMC_IsSDHC())
			{
				static bool using_sdhc = true;

				// SD request and
				if (c & 0x03)
				{
					if (!(c & 0x04))
					{
						if (using_sdhc)
						{
							// we have not been using sdhc so far?
							// -> complain!
							ErrorMessage(" This core does not support\n"
								" SDHC cards. Using them may\n"
								" lead to data corruption.\n\n"
								" Please use an SD card <2GB!", 0);
							using_sdhc = false;
						}
					} else
						// SDHC request from core is always ok
						using_sdhc = true;
				}
			}

			// Write to file/SD Card
			if ((c & 0x03) == 0x02)
			{
				// only write if the inserted card is not sdhc or
				// if the core uses sdhc
				if ((!MMC_IsSDHC()) || (c & 0x04))
				{
					if (is_dip_switch1_on())
						debugf("SD WR (%d) %lu/%d", drive_index, lba, 512<<blksz);

					// if we write the sector stored in the read buffer, then
					// invalidate the cache
					if (buffer_lba == lba && buffer_drive_index == drive_index)
					{
						buffer_lba = 0xffffffff;
					}

					user_io_sd_ack(drive_index);

					// Fetch sector data from FPGA ...
					spi_uio_cmd_cont(UIO_SECTOR_WR);
					spi_read(sector_buffer, 512 << blksz);
					DisableIO();

					// ... and write it to disk
#if 1
					if (sd_image[sd_index(drive_index)].valid)
					{
						if (((f_size(&sd_image[sd_index(drive_index)].file) - 1) >> (9 + blksz)) >= lba)
						{
							IDXSeek(&sd_image[sd_index(drive_index)], (lba << blksz));
							IDXWrite(&sd_image[sd_index(drive_index)], sector_buffer, blksz);
						}
					} else if (!drive_index && !umounted)
						disk_write(fs.pdrv, sector_buffer, lba, 1 << blksz);
#else
					hexdump(sector_buffer, 32, 0);
#endif
				}
			}

			// Read from file/SD Card
			if ((c & 0x03) == 0x01)
			{
				if (is_dip_switch1_on())
					debugf("SD RD (%d) %lu/%d", drive_index, lba, 512 << blksz);

				// invalidate cache if it stores data from another drive
				if (drive_index != buffer_drive_index)
					buffer_lba = 0xffffffff;

#ifdef HAVE_PSX
				if ((core_features & FEAT_PSX) && drive_index == 1) {
					psx_read_cd(drive_index, lba);
				} else {
#endif
				// are we using a file as the sd card image?
				// (C64 floppy does that ...)
				if (buffer_lba != lba)
				{
					if (sd_image[sd_index(drive_index)].valid)
					{
						if (((f_size(&sd_image[sd_index(drive_index)].file) - 1) >> (9 + blksz)) >= lba)
						{
							IDXSeek(&sd_image[sd_index(drive_index)], lba << blksz);
							IDXRead(&sd_image[sd_index(drive_index)], cache_buffer, blksz);
						}
					}
					else if (!drive_index && !umounted)
					{
						// sector read
						// read sector from sd card if it is not already present in
						// the buffer
						disk_read(fs.pdrv, cache_buffer, lba, 1 << blksz);
					}

					buffer_lba = lba;
				}

				if (buffer_lba == lba)
				{
					// hexdump(cache_buffer, 512 << blksz, 0);
					user_io_sd_ack(drive_index);

					// data is now stored in buffer. send it to fpga
					spi_uio_cmd_cont(UIO_SECTOR_RD);
					spi_write(cache_buffer, 512 << blksz);
					DisableIO();

					// the end of this transfer acknowledges the FPGA internal
					// sd card emulation
				}

				// just load the next sector now, so it may be prefetched
				// for the next request already
				if (sd_image[sd_index(drive_index)].valid)
				{
					// but check if it would overrun on the file
					if (((f_size(&sd_image[sd_index(drive_index)].file) - 1) >> (9 + blksz)) > lba)
					{
						IDXSeek(&sd_image[sd_index(drive_index)], (lba + 1) << blksz);
						IDXRead(&sd_image[sd_index(drive_index)], cache_buffer, blksz);
						buffer_lba = lba + 1;
					}
				} else {
					// sector read
					// read sector from sd card if it is not already present in
					// the buffer
					disk_read(fs.pdrv, cache_buffer, lba + 1, 1 << blksz);
					buffer_lba = lba + 1;
				}

				buffer_drive_index = drive_index;
#ifdef HAVE_PSX
				}
#endif
			}
		}
	}

	if (core_features & FEAT_IDE_MASK)
	{
		EnableFpga();
		uint8_t c1 = SPI(0); // cmd request
		SPI(0);
		SPI(0);
		SPI(0);
		SPI(0);
		SPI(0);
		DisableFpga();

		HandleHDD(c1, 0, 1);
	}

	// check for long press > 1 sec on menu button
	// and toggle scandoubler on/off then
	static uint32_t timer = 1;
	static bool ypbpr_toggle = 0;

	if (MenuButton())
	{
		if (timer == 1)
		{
			timer = GetTimer(1000);
		}
		else if (timer != 2)
		{
			if (CheckTimer(timer))
			{
				// toggle scandoubler mode
				mist_cfg.scandoubler_disable ^= 1;
				timer = 2;

				user_io_send_buttons(true);
				OsdDisableMenuButton(true);

				VIDEO_ALTERED_VAR |= 1;
				VIDEO_SD_DISABLE_VAR = mist_cfg.scandoubler_disable;
			}
		}

		if (UserButton())
		{
			if (!ypbpr_toggle)
			{
				// toggle video mode
				mist_cfg.ypbpr ^= 1;
				timer = 2;
				ypbpr_toggle = 1;

				user_io_send_buttons(true);
				OsdDisableMenuButton(true);

				VIDEO_ALTERED_VAR |= 2;
				VIDEO_YPBPR_VAR = mist_cfg.ypbpr;
			}
		}
		else
		{
			ypbpr_toggle = 0;
		}
	}
	else
	{
		timer = 1;
		OsdDisableMenuButton(false);
		ypbpr_toggle = 0;
	}

#ifdef HAVE_HDMI

	if (hdmi_detected)
	{
		if (CheckTimer(hdmi_timer))
		{
			hdmi_timer = GetTimer(HDMI_FREQ);
			HDMITX_DevLoopProc();

			if ((i2c_flags & 1) != hdmi_hiclk)
			{
				hdmi_hiclk = i2c_flags & 1;

				if (hdmi_detected) {
					HDMITX_ChangeVideoTiming(hdmi_hiclk ? 16 : 1);
				}
			}
		}
	}

#endif // HAVE_HDMI
}

void user_io_osd_key_enable(bool on)
{
	iprintf("OSD is now %s\n", on ? "visible" : "invisible");
	osd_is_visible = on;
}

void user_io_change_into_core_dir()
{
	if (arc_get_dirname()[0]) {
		strcpy(s, "/");
		strcat(s, arc_get_dirname());
	} else {
		user_io_create_config_name(s, 0, CONFIG_ROOT);
	}

	// try to change into subdir named after the core
	ChangeDirectoryName(s);
}

#ifdef HAVE_HDMI

static char user_io_i2c_stat(uint8_t *data)
{
	unsigned char c, d;

	while (true)
	{
		spi_uio_cmd_cont(UIO_I2C_GET);
		c = SPI(0xff);
		d = SPI(0xff);
		DisableIO();

		if (c & 1)
		{
			// end flag
			if (data)
				*data = d;

			i2c_flags = c >> 2;
			return (c & 2); // ack flag
		}
	}
}

char user_io_i2c_write(uint8_t addr, uint8_t subaddr, uint8_t data)
{
	spi_uio_cmd_cont(UIO_I2C_SEND);
	spi8(addr << 1);
	spi8(subaddr);
	spi8(data);
	DisableIO();

	return user_io_i2c_stat(0);
}

char user_io_i2c_read(uint8_t addr, uint8_t subaddr, uint8_t *data)
{
	spi_uio_cmd_cont(UIO_I2C_SEND);
	spi8(addr << 1 | 1); // read request
	spi8(subaddr);
	spi8(0xff);
	DisableIO();

	return user_io_i2c_stat(data);
}

bool user_io_hdmi_detected()
{
	return hdmi_detected;
}

#endif // HAVE_HDMI
