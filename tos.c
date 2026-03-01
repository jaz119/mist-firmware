#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "hardware.h"

#include "menu.h"
#include "osd.h"
#include "misc_cfg.h"
#include "tos.h"
#include "fat_compat.h"
#include "fpga.h"
#include "cdc_control.h"
#include "debug.h"
#include "user_io.h"
#include "data_io.h"
#include "idxfile.h"
#include "font.h"
#include "mmc.h"
#include "utils.h"
#include "FatFs/diskio.h"
#include "acsi_hdc.h"

extern bool eth_present;
extern char s[OSD_BUF_SIZE];

typedef struct {
  unsigned long system_ctrl;  // system control word
  char tos_img[64];
  char cart_img[64];
  char acsi_img[2][64];
  char video_adjust[2];
  char cdc_control_redirect;
  bool sd_direct;
} tos_config_t;

static tos_config_t config;
static UINT br;

#define TOS_BASE_ADDRESS_192k    0xfc0000
#define TOS_BASE_ADDRESS_256k    0xe00000
#define CART_BASE_ADDRESS        0xfa0000
#define VIDEO_BASE_ADDRESS       0x010000

// two floppies
ALIGNED(4) static struct {
  FIL file;
  char name[64];
  unsigned int sides;
  unsigned int spt;
} fdd_image[2];

unsigned long hdd_direct = 0;
// 0-1 floppy, 2-3 hdd
char disk_inserted[HARDFILES];

unsigned char spi_speed;
unsigned char spi_newspeed;

static const char *acsi_cmd_name(int cmd) {
  static const char *cmdname[] = {
    "Test Drive Ready", "Restore to Zero", "Cmd $2", "Request Sense",
    "Format Drive", "Read Block limits", "Reassign Blocks", "Cmd $7",
    "Read Sector", "Cmd $9", "Write Sector", "Seek Block",
    "Cmd $C", "Cmd $D", "Cmd $E", "Cmd $F",
    "Cmd $10", "Cmd $11", "Inquiry", "Verify",
    "Cmd $14", "Mode Select", "Cmd $16", "Cmd $17",
    "Cmd $18", "Cmd $19", "Mode Sense", "Start/Stop Unit",
    "Cmd $1C", "Cmd $1D", "Cmd $1E", "Cmd $1F",
    // extended commands supported by ICD feature:
    "Cmd $20", "Cmd $21", "Cmd $22",
    "Read Format Capacities", "Cmd $24", "Read Capacity (10)",
    "Cmd $26", "Cmd $27", "Read (10)", "Read Generation",
    "Write (10)", "Seek (10)"
  };

  if(cmd > 0x2b) return NULL;

  return cmdname[cmd];
}

static void tos_insert_disk(int, const char *);
static void tos_select_hdd_image(int, const char *);

void assign_full_path(char *buf, int buf_size, const char *fname) {
  if (!buf || !buf_size || !fname || buf == fname) return;
  if (*fname == '/') {
    sniprintf(buf, buf_size, "%s", fname);
  } else {
    sniprintf(buf, buf_size, "%s/%s", cwd, fname);
  }
}

char tos_get_cdc_control_redirect(void) {
  return config.cdc_control_redirect;
}

void tos_set_cdc_control_redirect(char mode) {
  if((mode >= CDC_REDIRECT_NONE) && (mode <= CDC_REDIRECT_MIDI)) {
    config.cdc_control_redirect = mode;

    // core is only informed about redirections of rs232/par/midi
    if(mode < CDC_REDIRECT_RS232)
      mode = 0;
    else
      mode -= CDC_REDIRECT_RS232 - 1;

    tos_update_sysctrl((tos_system_ctrl() & ~0x0c000000) |
       (((unsigned long)mode) << 26) );
  }
}

void tos_set_video_adjust(char axis, char value) {
  config.video_adjust[axis] += value;

  EnableFpga();
  SPI(MIST_SET_VADJ);
  SPI(config.video_adjust[0]);
  SPI(config.video_adjust[1]);
  DisableFpga();
}

char tos_get_video_adjust(char axis) {
  return config.video_adjust[axis];
}

static void mist_memory_set_address(unsigned long a, unsigned char s, bool rw) {
  //  iprintf("set addr = %x, %d, %d\n", a, s, rw);

  a |= rw ? 0x1000000 : 0;
  a >>= 1;

  EnableFpga();
  SPI(MIST_SET_ADDRESS);
  SPI(s);
  SPI((a >> 16) & 0xff);
  SPI((a >>  8) & 0xff);
  SPI((a >>  0) & 0xff);
  DisableFpga();
}

static void mist_set_control(unsigned long ctrl) {
  EnableFpga();
  SPI(MIST_SET_CONTROL);
  SPI((ctrl >> 24) & 0xff);
  SPI((ctrl >> 16) & 0xff);
  SPI((ctrl >>  8) & 0xff);
  SPI((ctrl >>  0) & 0xff);
  DisableFpga();
}

static void mist_memory_read(char *data, unsigned long words) {
  EnableFpga();
  SPI(MIST_READ_MEMORY);

  // transmitted bytes must be multiple of 2 (-> words)
  while (words--) {
    *data++ = SPI(0);
    *data++ = SPI(0);
  }

  DisableFpga();
}

static inline void mist_spi_set_speed(unsigned char speed)
{
  if (user_io_core_type() == CORE_TYPE_MISTERY)
    spi_set_speed(speed);
}

static void mist_memory_write(const char *data, size_t words) {
  spi_speed = spi_get_speed();
  mist_spi_set_speed(spi_newspeed);

  EnableFpga();
  SPI(MIST_WRITE_MEMORY);
  spi_write(data, words*2);
  DisableFpga();

  mist_spi_set_speed(spi_speed);
}

static void mist_memory_read_block(char *data) {
  spi_speed = spi_get_speed();
  mist_spi_set_speed(spi_newspeed);

  EnableFpga();
  SPI(MIST_READ_MEMORY);
  spi_block_read(data);
  DisableFpga();

  mist_spi_set_speed(spi_speed);
}

static void mist_memory_write_block(const char *data) {
  EnableFpga();
  SPI(MIST_WRITE_MEMORY);
  spi_block_write(data);
  DisableFpga();
}

static void mist_memory_write_blocks(const char *data, int count) {
  spi_speed = spi_get_speed();
  mist_spi_set_speed(spi_newspeed);

  EnableFpga();
  SPI(MIST_WRITE_MEMORY);
  spi_write(data, 512*count);
  DisableFpga();

  mist_spi_set_speed(spi_speed);
}

void mist_memory_set(char data, unsigned long words) {
  EnableFpga();
  SPI(MIST_WRITE_MEMORY);

  while(words--) {
    SPI(data);
    SPI(data);
  }

  DisableFpga();
}

// enable direct sd card access on acsi0
static void tos_set_direct_hdd(bool on) {
  config.sd_direct = on;

  if(on) {
    tos_debugf("ACSI: enable direct SD access");
    disk_ioctl(fs.pdrv, GET_SECTOR_COUNT, &hdd_direct);

    tos_debugf("ACSI: Direct capacity = %ld (%ld Bytes)", hdd_direct, hdd_direct*512);
    config.system_ctrl |= TOS_ACSI0_ENABLE;
  } else {
    tos_debugf("ACSI: disable direct SD access");
    config.system_ctrl &= ~TOS_ACSI0_ENABLE;
    hdd_direct = 0;

    // check if image access should be enabled instead
    if(disk_inserted[2]) {
      tos_debugf("ACSI: re-enabling image on ACSI0");
      config.system_ctrl |= TOS_ACSI0_ENABLE;
    }
  }

  mist_set_control(config.system_ctrl);
}

static inline char tos_get_direct_hdd() {
  return config.sd_direct;
}

static inline bool tos_disk_is_inserted(int index) {
  return disk_inserted[index & 3];
}

static void dma_ack(unsigned char status) {
  EnableFpga();
  SPI(MIST_ACK_DMA);
  SPI(status);
  DisableFpga();
}

static void dma_nak(void) {
  EnableFpga();
  SPI(MIST_NAK_DMA);
  DisableFpga();
}

FAST static int acsi_disk_read(int target, uint32_t lba, size_t length) {
  DISKLED_ON;
  int read = 0;
#ifndef SD_NO_DIRECT_MODE
  if (user_io_core_type() == CORE_TYPE_MISTERY && fat_uses_mmc()) {
    // SD-Card -> FPGA direct SPI transfer on MISTERY
    spi_speed = spi_get_speed();
    mist_spi_set_speed(spi_newspeed);
    if (hdd_direct && target == 0) {
      if (is_dip_switch1_on())
        tos_debugf("ACSI: direct read, LBA: %lu", lba);
      if (disk_read(fs.pdrv, 0, lba, length) == RES_OK)
        read = length;
    } else {
      IDXSeek(&sd_image[target + 2], lba);
      if (f_read(&sd_image[target + 2].file, 0, 512 * length, &br) == FR_OK)
        read = length;
    }
    mist_spi_set_speed(spi_speed);
  } else {
#endif
    while (length) {
      int blocksize = MIN(length, SECTOR_BUFFER_SIZE / 512);
      if (hdd_direct && target == 0) {
        if (is_dip_switch1_on())
          tos_debugf("ACSI: direct read, LBA=%lu", lba);
        if (disk_read(fs.pdrv, sector_buffer, lba, blocksize) == RES_OK)
          read += blocksize;
      } else {
        IDXSeek(&sd_image[target + 2], lba);
        if (f_read(&sd_image[target + 2].file, sector_buffer, 512*blocksize, &br) == FR_OK)
          read += blocksize;
      }
      // hexdump(sector_buffer, 32, 0);
      mist_memory_write_blocks(sector_buffer, blocksize);
      length -= blocksize;
      lba += blocksize;
    }
#ifndef SD_NO_DIRECT_MODE
  }
#endif
  DISKLED_OFF;
  return read;
}

FAST static int acsi_disk_write(int target, uint32_t lba, size_t length) {
  int written = 0;
  unsigned short blocklen;
  unsigned char *buf;

  DISKLED_ON;
  while (length) {
    UINT bw;

    blocklen = (length > SECTOR_BUFFER_SIZE / 512) ? SECTOR_BUFFER_SIZE / 512 : length;
    buf = sector_buffer;
    int n = blocklen;
    while (n--) {
      mist_memory_read_block(buf);
      buf += 512;
    }
    if (hdd_direct && target == 0) {
      if (is_dip_switch1_on())
        tos_debugf("ACSI: direct write, LBA=%lu", lba);
      if (disk_write(fs.pdrv, sector_buffer, lba, blocklen) == RES_OK)
        written += blocklen;
    } else {
      IDXSeek(&sd_image[target + 2], lba);
      if (f_write(&sd_image[target + 2].file, sector_buffer, blocklen * 512, &bw) == FR_OK)
        written += blocklen;
    }
    lba += blocklen;
    length -= blocklen;
  }
  DISKLED_OFF;
  return written;
}

static void acsi_init() {
  memset(&AcsiBus, 0, sizeof(AcsiBus));

  AcsiBus.buffer = sector_buffer;
  AcsiBus.buffer_size = sizeof(sector_buffer);

  for (int i = 0; i < ARRAY_SIZE(AcsiBus.devs); i++) {
    SCSI_DEV *dev = &AcsiBus.devs[i];

    dev->blockSize = 512;
    dev->disk_read = acsi_disk_read;
    dev->disk_write = acsi_disk_write;
    dev->dma_write = mist_memory_write;
    dev->scsi_version = 2;
  }
}

static void mist_get_dmastate() {
  unsigned char *buffer = AcsiBus.command;
  unsigned int dma_address;
  unsigned char scnt;

  EnableFpga();
  SPI(MIST_GET_DMASTATE);
  spi_read(buffer, 16);
  DisableFpga();

  // CORE_TYPE_MISTERY
  if(buffer[10] & 0x01 /* BUSY */) {
    spi_newspeed = SPI_MMC_CLK_VALUE;

    AcsiBus.opcode = AcsiBus.command[0];
    AcsiBus.target = (AcsiBus.command[10] & 0xE0) >> 5;

    // only a harddisk on ACSI 0/1 is supported
    // ACSI 0/1 is only supported if a image is loaded
    // ACSI 0 is only supported for direct IO
    if (AcsiBus.target < 2) {
      HDC_HandleCommandPacket(&AcsiBus);
      if (AcsiBus.status != HD_STATUS_OK) {
        SCSI_DEV *dev = &AcsiBus.devs[AcsiBus.target];
        iprintf("ACSI: opcode=0x%x, status=0x%x, error=0x%x\n",
          AcsiBus.opcode, AcsiBus.status, dev->nLastError);
        if (!(hdd_direct && AcsiBus.target == 0) && dev->hdSize == 0) {
          dev->nLastError = HD_REQSENS_NOTREADY;
        }
      }
      dma_ack(AcsiBus.status);
    } else {
      if (is_dip_switch1_on())
        tos_debugf("ACSI: Unsupported target: %s", HDC_CmdInfoStr(&AcsiBus));
      // tell acsi state machine that io controller is done
      // but don't generate a acsi irq
      dma_nak();
    }
  }
}

// color test, used to test the shifter without CPU/TOS
#define COLORS   20
#define PLANES   4

static void tos_write(char *str);

static void tos_color_test() {
  ALIGNED(4) unsigned short buffer[COLORS][PLANES];

  for(int y=0; y<13; y++) {
    for(int i=0; i<COLORS; i++)
      for(int j=0; j<PLANES; j++)
        buffer[i][j] = ((y+i) & (1<<j))?0xffff:0x0000;

    for(int i=0; i<16; i++) {
      mist_memory_set_address(VIDEO_BASE_ADDRESS + (16*y+i)*160, 1, 0);
      mist_memory_write((char*)buffer, COLORS*PLANES);
    }
  }

#if 1
  mist_memory_set_address(VIDEO_BASE_ADDRESS, 1, 0);
  mist_memory_set(0xf0, 40);

  mist_memory_set_address(VIDEO_BASE_ADDRESS+80, 1, 0);
  mist_memory_set(0x55, 40);

  mist_memory_set_address(VIDEO_BASE_ADDRESS+160, 1, 0);
  mist_memory_set(0x0f, 40);

#if 1
  tos_write("");
  tos_write("AAAAAAAABBBBBBBBCCCCCCCCDDDDDDDDEEEEEEEEFFFFFFFFGGGGGGGGHHHHHHHHIIIIIIIIJJJJJJJJ");
  tos_write("ABCDEFGHIJHKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJHKLMNOPQRSTUVWXYZ0123456789");
#endif

  //  for(;;);
#endif
}

static void tos_write(char *str) {
  static int y = 0;

  // empty string is "cursor home"
  if(!str) {
    y = 0;
    return;
  }

  // get next higher multiple of 16 for string length
  // as dma works in 16 bytes chunks only
  int c = (strlen(str)+15) & ~15;
  {
    char buffer[c];

    // 16 pixel lines
    for(int l=0; l<16; l++) {
      char *p = str, *f=buffer;
      while(*p)	*f++ = char_row(*p++, l>>1);
      while(f < buffer+c) *f++ = char_row(' ', l>>1);

      mist_memory_set_address(VIDEO_BASE_ADDRESS + 80*(y+l), 1, 0);
      mist_memory_write(buffer, c/2);
    }
  }
  y+=16;
}

static void tos_clr() {
  mist_memory_set_address(VIDEO_BASE_ADDRESS, (32000+511)/512, 0);
  mist_memory_set(0, 16000);

  tos_write(NULL);
}

static void tos_load_cartridge_mistery() {
  FIL file;

  // upload cartridge
  if(config.cart_img[0] && (f_open(&file, config.cart_img, FA_READ) == FR_OK)) {
    data_io_file_tx(&file, 0x02, 0);
    iprintf("Cartridge %s uploaded\n", config.cart_img);
    f_close(&file);
    return;
  }

  // erase that ram area to remove any previously uploaded image
  tos_debugf("Erasing cart memory");
  data_io_fill_tx(0xff, 128*1024, 0x02);
}

static void tos_load_cartridge(const char *name) {
  assign_full_path(
    config.cart_img, sizeof(config.cart_img) - 1, name);

  tos_load_cartridge_mistery();
}

static inline bool tos_cartridge_is_inserted() {
  return config.cart_img[0];
}

static void tos_upload_mistery(const char *name) {
  FIL file;

  // clear first 16k
  tos_debugf("Clear first 16k");
  data_io_fill_tx(0, 16*1024, 0x03);

  // upload and verify tos image
  if(f_open(&file, config.tos_img, FA_READ) == FR_OK) {
    iprintf("TOS: %s\n", config.tos_img);

    if(f_size(&file) >= 256*1024)
      data_io_file_tx(&file, 0x00, 0);
    else if(f_size(&file) == 192*1024)
      data_io_file_tx(&file, 0x01, 0);
    else
      tos_debugf("WARNING: Unexpected TOS size!");
    f_close(&file);
  } else {
    tos_debugf("Unable to find %s", config.tos_img);
    return;
  }

  // This is the initial boot if no name was given. Otherwise the
  // user reloaded a new os
  if(!name) {
    // load
    tos_load_cartridge(NULL);

    // try to open both floppies
    tos_insert_disk(0, "DISK_A.ST");
    tos_insert_disk(1, "DISK_B.ST");

    if(config.sd_direct) {
      tos_set_direct_hdd(1);
    } else {
      // try to open harddisk image
      for(int i=0;i<2;i++) {
        if (*config.acsi_img[i]) {
          tos_debugf("trying to open %s image #%d", config.acsi_img[i], i);
          tos_select_hdd_image(i, config.acsi_img[i]);
        }
      }
    }
  }
}

void tos_upload(const char *name) {

  tos_debugf("Uploading TOS");

  ResetMenu();

  // put cpu into reset
  config.system_ctrl |= TOS_CONTROL_CPU_RESET;
  mist_set_control(config.system_ctrl);

  assign_full_path(
    config.tos_img, sizeof(config.tos_img) - 1, name);

  tos_upload_mistery(name);

  // let cpu run (release reset)
  config.system_ctrl &= ~TOS_CONTROL_CPU_RESET;
  unsigned long system_ctrl = config.system_ctrl;

  // adjust for detected ethernet adapter
#ifdef USB_ASIX_NET
  if (!eth_present)
#endif
    system_ctrl &= ~TOS_CONTROL_ETHERNET;

  mist_set_control(system_ctrl);
}

static unsigned long get_long(char *buffer, int offset) {
  unsigned long retval = 0;

  for(int i=0; i<4; i++)
    retval = (retval << 8) + *(unsigned char*)(buffer+offset+i);

  return retval;
}

void tos_poll() {
  // 1 == button not pressed, 2 = 1 sec exceeded, else timer running
  static unsigned long timer = 1;

  mist_get_dmastate();

  // check the user button
  if(!MenuButton() && UserButton()) {
    if(timer == 1)
      timer = GetTimer(1000);
    else if(timer != 2)
      if(CheckTimer(timer)) {
        tos_reset(1);
        timer = 2;
      }
  } else {
    // released while still running (< 1 sec)
    if(!(timer & 3))
      tos_reset(0);

    timer = 1;
  }
}

void tos_update_sysctrl(unsigned long n) {
  //  iprintf(">>>>>>>>>>>> set sys %x, eth is %s\n", n, (n&TOS_CONTROL_ETHERNET)?"on":"off");

  // some of the usb drivers also call this without knowing which
  // core is running. So make sure this only happens if the Atari ST (MIST)
  // core is running
  if((user_io_core_type() == CORE_TYPE_MIST) ||
     (user_io_core_type() == CORE_TYPE_MISTERY))
  {
    config.system_ctrl = n;
    mist_set_control(config.system_ctrl);
  }
}

static const char *tos_get_disk_name(char index) {
  if(!disk_inserted[index]) {
    return "* no disk *";
  }

  if (index <= 1) {
    return get_short_name(fdd_image[index].name);
  } else {
    return get_short_name(config.acsi_img[index-2]);
  }
}

static inline const char *tos_get_image_name() {
  return get_short_name(config.tos_img);
}

static const char *tos_get_cartridge_name() {
  if(!config.cart_img[0]) {  // no cart name set
    return "* no cartridge *";
  } else
    return get_short_name(config.cart_img);
}

static void tos_select_hdd_image(int i, const char *name) {
  int slot = i+2;
  IDXFile *idx = &sd_image[slot & 3];
  SCSI_DEV *acsi_dev = NULL;

  // try to re/open harddisk image
  if (disk_inserted[slot]) {
    IDXClose(idx);
    disk_inserted[slot] = 0;
  }

  config.system_ctrl &= ~(TOS_ACSI0_ENABLE<<i);

  if (i < 2) {
    // Link file with ACSI driver
    acsi_dev = &AcsiBus.devs[i];
    acsi_dev->blockSize = 512;
    acsi_dev->nLastError = HD_REQSENS_OK;
    acsi_dev->hdSize = 0;
  }

  if(name && name[0]) {
    FRESULT res = IDXOpen(idx, name, FA_READ | FA_WRITE);
    if (res == FR_OK) {
      assign_full_path(config.acsi_img[i], sizeof(config.acsi_img[i]) - 1, name);
      iprintf("ACSI%d: %s\n", i, config.acsi_img[i]);
      IDXIndex(idx, slot);
      disk_inserted[slot] = 1;
      config.system_ctrl |= (TOS_ACSI0_ENABLE<<i);
      if (acsi_dev)
        acsi_dev->hdSize = f_size(&(idx->file)) / acsi_dev->blockSize;
    } else {
      iprintf("Cannot open %s file, error %d\n", name, res);
    }
  } else {
    config.acsi_img[i][0] = 0;
  }

  // update system control
  mist_set_control(config.system_ctrl);
}

static void tos_insert_disk(int i, const char *name) {
  if(i > 1) {
    tos_select_hdd_image(i-2, name);
    return;
  }

  fdd_image[i].name[0] = 0;
  tos_debugf("%c: eject", i+'A');

  // toggle write protect bit to help tos detect a media change
  int wp_bit = (!i)?TOS_CONTROL_FDC_WR_PROT_A:TOS_CONTROL_FDC_WR_PROT_B;

  // any disk ejected is "write protected" (as nothing covers the write protect mechanism)
  mist_set_control(config.system_ctrl | wp_bit);

  // first "eject" disk
  fdd_image[i].sides = 1;
  fdd_image[i].spt = 0;
  disk_inserted[i] = 0;

  if (user_io_core_type() == CORE_TYPE_MISTERY) {
    user_io_file_mount(NULL, i);
    if (name && name[0]) {
        user_io_file_mount(name, i);
        if (user_io_is_mounted(i)) {
          assign_full_path(fdd_image[i].name, sizeof(fdd_image[i].name) - 1, name);
          iprintf("%c: %s\n", i+'A', fdd_image[i].name);
          disk_inserted[i] = 1;
        }
        tos_update_sysctrl(config.system_ctrl);
    }
    return;
  }
}

// force ejection of all disks (SD card has been removed)
void tos_eject_all() {
  int i;
  for(i=0;i<2;i++) {
    tos_insert_disk(i, NULL);
    disk_inserted[i] = 0;
  }

  // ejecting an SD card while a hdd image is mounted may be a bad idea
  for(i=0;i<2;i++) {
    if(hdd_direct)
      hdd_direct = 0;

    if(disk_inserted[i+2]) {
      InfoMessage("Card removed:\nDisabling Harddisk!");
      disk_inserted[i+2] = 0;
    }
  }
}

void tos_reset(char cold) {
  tos_update_sysctrl(config.system_ctrl |  TOS_CONTROL_CPU_RESET);  // set reset

  if(cold) {
#if 0 // clearing mem should be sifficient. But currently we upload TOS as it may be damaged
    // clear first 16k
    mist_memory_set_address(8);
    mist_memory_set(0x00, 8192-4);
#else
    acsi_init();
    tos_upload(NULL);
#endif
  }

  tos_update_sysctrl(config.system_ctrl & ~TOS_CONTROL_CPU_RESET);  // release reset
}

unsigned long tos_system_ctrl(void) {
  return config.system_ctrl;
}

static const char *get_config_fname(int slot) {
  static char fname[16];
  if(slot) {
    siprintf(fname,"/MIST%d.CFG", slot);
  } else {
    strcpy(fname,"/MIST.CFG");
  }
  return fname;
}

// load/init configuration
void tos_config_load(char slot) {
  FIL file;
  static char last_slot = 0;
  char new_slot = (slot == -1) ? last_slot : slot;

  tos_eject_all();

  // set default values
  config.system_ctrl = TOS_MEMCONFIG_4M | TOS_CONTROL_BLITTER;
  strcpy(config.tos_img, "TOS.IMG");
  memset(config.cart_img, 0, sizeof(config.cart_img));
  strcpy(config.acsi_img[0], "/HARDDISK.HD");
  memset(config.acsi_img[1], 0, sizeof(config.acsi_img[1]));
  strcpy(fdd_image[0].name, "DISK_A.ST");
  memset(fdd_image[1].name, 0, sizeof(fdd_image[1].name));
  config.video_adjust[0] = config.video_adjust[1] = 0;
  config.cdc_control_redirect = CDC_REDIRECT_NONE;

  // try to load config
  const char *cfname = get_config_fname(new_slot);

  if (f_open(&file, cfname, FA_READ) == FR_OK) {
    if(f_size(&file) == sizeof(tos_config_t)) {
      f_read(&file, (unsigned char*) &config, sizeof(tos_config_t), &br);
      iprintf("Config file '%s' loaded\n", cfname);
    }
    f_close(&file);
  }

  acsi_init();
}

// save configuration
static bool tos_config_save(char slot) {
  FIL file;
  UINT bw;

  // save configuration data
  if (f_open(&file, get_config_fname(slot), FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
    tos_debugf("Config file opening/creating failed.");
    return false;
  }

  // finally write the config
  f_write(&file, (unsigned char *) &config, sizeof(tos_config_t), &bw);
  f_close(&file);
  return (bw == sizeof(tos_config_t));
}

// configuration file check
static bool tos_config_exists(char slot) {
  FIL file;
  if (f_open(&file, get_config_fname(slot), FA_READ) == FR_OK) {
    f_close(&file);
    return true;
  }
  return false;
}

///////////////////////////
////// Atari ST menu //////
///////////////////////////

static const char* scanlines[]={"Off","25%","50%","75%"};
static const char* stereo[]={"Mono","Stereo"};
static const char* blend[]={"Off","On"};
static const char* atari_chipset[]={"ST","STE","MegaSTE","STEroids"};
static const char *config_tos_mem[] =  {"512 kB", "1 MB", "2 MB", "4 MB", "8 MB", "14 MB", "--", "--" };
static const char *config_tos_wrprot[] =  {"none", "A:", "B:", "A: and B:"};
static const char *config_tos_usb[] =  {"none", "control", "debug", "serial", "parallel", "midi"};
static const char *config_tos_cart[] = {"File", "Ethernec", "Cubase"};

static char tos_file_selected(uint8_t idx, const char *SelectedName) {

	switch(idx) {
		case 0:
		case 7:
		case 8:	// Floppy
			menu_debugf("Insert image %s for disk %d", SelectedName, (idx>=7) ? idx-7 : idx);
			tos_insert_disk((idx>=7) ? idx-7 : idx, SelectedName);
			break;
		case 12:
		case 13: // ACSI
			menu_debugf("Insert image %s for ACSI disk %d", SelectedName, idx-10);
			tos_insert_disk(idx-10, SelectedName);
			break;
		case 16:  // TOS
			tos_upload(SelectedName);
			break;
		case 18:  // Cart
			tos_load_cartridge(SelectedName);
			break;
	}
	return 0;
}

static char tos_getmenupage(uint8_t idx, char action, menu_page_t *page) {
	if (action==MENU_PAGE_EXIT) return 0;
	if (user_io_core_type() == CORE_TYPE_MISTERY)
		page->title = "MiSTery";
	else
		page->title = "MiST";
	if (!idx)
		page->flags = OSD_ARROW_RIGHT;
	else
		page->flags = 0;
	page->timer = 0;
	page->stdexit = MENU_STD_EXIT;
	return 0;
}

static char tos_getmenuitem(uint8_t idx, char action, menu_item_t *item) {
	char page_idx = item->page; // save current page number
	char enable;

	item->stipple = 0;
	item->active = 1;
	item->page = 0;
	item->newpage = 0;
	item->newsub = 0;
	item->item = "";
	if(idx<=6) item->page = 0;
	else if(idx<=13) item->page = 1;
	else if(idx<=21) item->page = 2;
	else if(idx<=28) item->page = 3;
	else if(idx<=33) item->page = 6;
	else if(idx<=39) item->page = 4;
	else if(idx<=45) item->page = 5;
	else return 0;

	if (page_idx == 0 && action == MENU_ACT_RIGHT) {
		SetupSystemMenu();
		return 0;
	}

	switch (action) {
		case MENU_ACT_GET:
			if (item->page != page_idx) return 1; // shortcut
			switch(idx) {
				case 0:
					// most important: main page has setup for floppy A:
					strcpy(s, " A: ");
					strcat(s, tos_get_disk_name(0));
					if(tos_system_ctrl() & TOS_CONTROL_FDC_WR_PROT_A) strcat(s, " \x17");
					item->item = s;
					break;
				//case 1 same as page 3/screen
				case 2:
					item->item = " Storage";
					item->newpage = 1;
					break;
				case 3:
					item->item = " System";
					item->newpage = 2;
					break;
				case 4:
					item->item = " Audio / Video";
					item->newpage = 3;
					break;
				case 5:
					item->item = " Load config";
					item->newpage = 4;
					break;
				case 6:
					item->item = " Save config";
					item->newpage = 5;
					break;

				// Page 1 - Storage
				case 7:
				case 8:
					// entries for both floppies
					strcpy(s, " A: ");
					strcat(s, tos_get_disk_name(idx-7));
					s[1] = 'A'+idx-7;
					if(tos_system_ctrl() & (TOS_CONTROL_FDC_WR_PROT_A << (idx-7)))
						strcat(s, " \x17");
					item->item = s;
					break;
				case 9:
					strcpy(s, " Write protect: ");
					strcat(s, config_tos_wrprot[(tos_system_ctrl() >> 6)&3]);
					item->item = s;
					break;
				case 11:
					strcpy(s, " ACSI0 direct SD: ");
					strcat(s, tos_get_direct_hdd() ? "on" : "off");
					item->item = s;
					break;
				case 12:
				case 13:
					strcpy(s, " ACSI0: ");
					s[5] = '0'+idx-12;
					strcat(s, tos_get_disk_name(2+idx-12));
					item->item = s;
					item->active = ((idx == 13) || !tos_get_direct_hdd());
					item->stipple = !item->active;
					break;

				// Page 2 - System
				case 14:
					strcpy(s, " Memory:    ");
					strcat(s, config_tos_mem[(tos_system_ctrl() >> 1)&7]);
					item->item = s;
					break;
				case 15:
					strcpy(s, " CPU:       ");
					strcat(s, config_cpu_msg[(tos_system_ctrl() >> 4)&3]);
					item->item = s;
					break;
				case 16:
					strcpy(s, " TOS:       ");
					strcat(s, tos_get_image_name());
					item->item = s;
					break;
				case 17: {
					uint8_t cartport = ((tos_system_ctrl() & TOS_CONTROL_ETHERNET) ? 1 : 0) |
					                   ((tos_system_ctrl() & TOS_CONTROL_CUBASE) ? 2 : 0);
					strcpy(s, " Cart.port: ");
					strcat(s, config_tos_cart[cartport]);
					item->item = s;
					}
					break;
				case 18:
					strcpy(s, " Cartridge: ");
					strcat(s, tos_get_cartridge_name());
					item->item = s;
					if (tos_system_ctrl() & (TOS_CONTROL_ETHERNET | TOS_CONTROL_CUBASE)) {
						item->active = 0;
						item->stipple = 1;
					}
					break;
				case 19:
					strcpy(s, " USB I/O:   ");
					strcat(s, config_tos_usb[tos_get_cdc_control_redirect()]);
					item->item = s;
					break;
				case 20:
					item->item = " Reset";
					break;
				case 21:
					item->item = " Cold boot";
					break;

				// Page 3 - A/V
				case 1:
				case 22:
					strcpy(s, " Screen:        ");
					if (idx==1) strcat(s, "     ");
					if(tos_system_ctrl() & TOS_CONTROL_VIDEO_COLOR) strcat(s, "Color");
					else                                            strcat(s, "Mono");
					item->item = s;
					break;
				case 23: // Viking card can only be enabled with max 8MB RAM
					enable = (tos_system_ctrl()&0xe) <= TOS_MEMCONFIG_8M;
					strcpy(s, " Viking/SM194:  ");
					strcat(s, ((tos_system_ctrl() & TOS_CONTROL_VIKING) && enable)?"on":"off");
					item->item = s;
					item->active = enable;
					item->stipple = !enable;
					break;
				case 24:
					// Blitter is always present in >= STE
					enable = (tos_system_ctrl() & (TOS_CONTROL_STE | TOS_CONTROL_MSTE))?1:0;
					strcpy(s, " Blitter:       ");
					strcat(s, ((tos_system_ctrl() & TOS_CONTROL_BLITTER) || enable)?"on":"off");
					item->item = s;
					item->active = !enable;
					item->stipple = enable;
					break;
				case 25:
					strcpy(s, " Chipset:       ");
					// extract  TOS_CONTROL_STE and  TOS_CONTROL_MSTE bits
					strcat(s, atari_chipset[(tos_system_ctrl()>>23)&3]);
					item->item = s;
					break;
				case 26:
					{
						strcpy(s, " Scanlines:     ");
						strcat(s,scanlines[(tos_system_ctrl()>>20)&3]);
						item->item = s;
					}
					break;
				case 27:
					strcpy(s, " YM-Audio:      ");
					strcat(s, stereo[(tos_system_ctrl() & TOS_CONTROL_STEREO)?1:0]);
					item->item = s;
					break;
				case 28:
					{
						strcpy(s, " Comp. blend:   ");
						strcat(s, blend[(tos_system_ctrl() & TOS_CONTROL_BLEND)?1:0]);
						item->item = s;
					}
					break;

				// Page 6 - V-adj
				case 29:
					strcpy(s, " PAL mode:    ");
					if(tos_system_ctrl() & TOS_CONTROL_PAL50HZ) strcat(s, "50Hz");
					else                                        strcat(s, "56Hz");
					item->item = s;
					break;
				case 30:
					strcpy(s, " Scanlines:   ");
					strcat(s,scanlines[(tos_system_ctrl()>>20)&3]);
					item->item = s;
					break;
				case 32:
					siprintf(s, " Horizontal:  %d", tos_get_video_adjust(0));
					item->item = s;
					break;
				case 33:
					siprintf(s, " Vertical:    %d", tos_get_video_adjust(1));
					item->item = s;
					break;

				// Page 4 - Load config
				case 35:
				case 36:
				case 37:
				case 38:
				case 39:
					if(!tos_config_exists(idx-35)) {
						item->active = 0;
						item->stipple = 1;
					}
					strcpy(s,"          ");
					strcat(s, atarist_cfg.conf_name[idx-35]);
					item->item = s;
					break;

				// page 5 - Save config
				case 41:
				case 42:
				case 43:
				case 44:
				case 45:
					strcpy(s,"          ");
					strcat(s, atarist_cfg.conf_name[idx-41]);
					item->item = s;
					break;
				default:
					item->active = 0;
			}
			break;

		case MENU_ACT_SEL:
			switch(idx) {
				case 0:
				case 7:
				case 8:
					if(tos_disk_is_inserted(idx>=7 ? idx-7 : idx))
						tos_insert_disk(idx>=7 ? idx-7 : idx, NULL);
					else
						SelectFileNG("ST ", SCAN_DIR | SCAN_LFN, tos_file_selected, 0);
					break;
				case 2:
				case 3:
				case 4:
				case 5:
				case 6:
					item->newpage = idx-1;
					break;

				case 9:
					// remove current write protect bits and increase by one
					tos_update_sysctrl((tos_system_ctrl() & ~(TOS_CONTROL_FDC_WR_PROT_A | TOS_CONTROL_FDC_WR_PROT_B))
					     | (((((tos_system_ctrl() >> 6)&3) + 1)&3)<<6) );
					break;
				case 11:
					iprintf("toggle direct hdd\n");
					tos_set_direct_hdd(!tos_get_direct_hdd());
					break;
				case 12:
				case 13:
					iprintf("Select image for disk %d\n", idx-10);
					if(tos_disk_is_inserted(idx-10))
						tos_insert_disk(idx-10, NULL);
					else
						SelectFileNG("IMGVHDHD ", SCAN_DIR | SCAN_LFN, tos_file_selected, 0);
					break;

				case 14: { // RAM
					int mem = (tos_system_ctrl() >> 1)&7;   // current memory config
					mem++;
					if(mem > 5) mem = 0;
					tos_update_sysctrl((tos_system_ctrl() & ~0x0e) | (mem<<1) );
					tos_reset(1);
					} break;
				case 15: { // CPU
					int cpu = (tos_system_ctrl() >> 4)&3;   // current cpu config
					cpu = (cpu+1)&3;
					if(cpu == 2 || (user_io_core_type() == CORE_TYPE_MISTERY && cpu == 1)) cpu = 3; // skip unused config
					tos_update_sysctrl((tos_system_ctrl() & ~0x30) | (cpu<<4) );
					tos_reset(0);
					} break;
				case 16:  // TOS
					SelectFileNG("IMGROM", SCAN_DIR | SCAN_LFN, tos_file_selected, 0);
					break;
				case 17: {
					unsigned long system_ctrl = tos_system_ctrl();
					uint8_t cartport = ((system_ctrl & TOS_CONTROL_ETHERNET) ? 1 : 0) |
					                   ((system_ctrl & TOS_CONTROL_CUBASE) ? 2 : 0);
					cartport += 1;
					if (cartport>2) cartport = 0;
					system_ctrl &= ~(TOS_CONTROL_ETHERNET | TOS_CONTROL_CUBASE);
					if (cartport & 1) system_ctrl |= TOS_CONTROL_ETHERNET;
					if (cartport & 2) system_ctrl |= TOS_CONTROL_CUBASE;
					tos_update_sysctrl(system_ctrl);
					}
					break;
				case 18:  // Cart
					// if a cart name is set, then remove it
					if(tos_cartridge_is_inserted()) {
						tos_load_cartridge("");
					} else
						SelectFileNG("IMG", SCAN_DIR | SCAN_LFN, tos_file_selected, 0);
					break;
				case 19:
					if(tos_get_cdc_control_redirect() == CDC_REDIRECT_MIDI)
						tos_set_cdc_control_redirect(CDC_REDIRECT_NONE);
					else
						tos_set_cdc_control_redirect(tos_get_cdc_control_redirect()+1);
					break;
				case 20:  // Reset
					tos_reset(0);
					CloseMenu();
					break;
				case 21:  // Cold Boot
					tos_reset(1);
					CloseMenu();
					break;

				case 1:
				case 22:
					tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_VIDEO_COLOR);
					break;
				case 23:
					// viking/sm194
					tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_VIKING);
					break;
				case 24:
					if(!(tos_system_ctrl() & TOS_CONTROL_STE)) {
						tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_BLITTER );
					}
					break;
				case 25: {
					unsigned long chipset = (tos_system_ctrl() >> 23)+1;
					if(chipset == 4) chipset = 0;
					tos_update_sysctrl((tos_system_ctrl() & ~(TOS_CONTROL_STE | TOS_CONTROL_MSTE)) |
						(chipset << 23));
					}
					break;
				case 26:
					{
						// next scanline state
						int scan = ((tos_system_ctrl() >> 20)+1)&3;
						tos_update_sysctrl((tos_system_ctrl() & ~TOS_CONTROL_SCANLINES) | (scan << 20));
					}
					break;
				case 27:
					tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_STEREO);
					break;
				case 28:
					tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_BLEND);
					break;
				case 29:
					tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_PAL50HZ);
					break;
				case 30: {
					// next scanline state
					int scan = ((tos_system_ctrl() >> 20)+1)&3;
					tos_update_sysctrl((tos_system_ctrl() & ~TOS_CONTROL_SCANLINES) | (scan << 20));
					} break;

				// page 4 - Load config
				case 35:
				case 36:
				case 37:
				case 38:
				case 39:
					tos_insert_disk(2, NULL);
					tos_insert_disk(3, NULL);
					tos_config_load(idx-35);
					tos_upload(NULL);
					CloseMenu();
					break;

				// page 5 - Save config
				case 41:
				case 42:
				case 43:
				case 44:
				case 45:
					tos_config_save(idx-41);
					CloseMenu();
					break;

				default:
					return 0;
			}
			break;

		case MENU_ACT_PLUS:
		case MENU_ACT_MINUS:
			switch(idx) {
				case 32:
				case 33:
					if(action == MENU_ACT_MINUS && (tos_get_video_adjust(idx - 32) > -100))
						tos_set_video_adjust(idx - 32, -1);

					if(action == MENU_ACT_PLUS && (tos_get_video_adjust(idx - 32) < 100))
						tos_set_video_adjust(idx - 32, +1);
					break;
				default:
					return 0;
			}
			break;

		default:
			return 0;
	}
	return 1;
}

void tos_setup_menu() {
	SetupMenu(tos_getmenupage, tos_getmenuitem, NULL);
}
