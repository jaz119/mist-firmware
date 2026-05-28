#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

#include "hardware.h"
#include "usb/timer.h"
#include "user_io.h"
#include "tos.h"
#include "menu.h"
#include "osd.h"
#include "hdd.h"
#include "misc_cfg.h"
#include "cdc_control.h"
#include "ini_parser.h"
#include "data_io.h"
#include "acsi_hdc.h"
#include "idxfile.h"
#include "utils.h"
#include "debug.h"

extern bool eth_present;
extern char s[OSD_BUF_SIZE];

typedef struct {
  char path[FF_LFN_BUF];
} floppyTYPE;

typedef struct {
  uint32_t system_ctrl;
  char cdc_control_redirect;
  char tos_img[FF_LFN_BUF];
  char cart_img[FF_LFN_BUF];
  hardfileTYPE acsi[2];
  floppyTYPE fdd[2];
} configTYPE;

static configTYPE config;
static uint32_t runtime_ctrl;

static const ini_section_t config_sections[] = {
  {1, "MISTERY"},
  {1, "ATARI_ST"},
};

static const ini_var_t config_vars[] = {
  {"SYS_CTRL",      (void*) &config.system_ctrl, UINT32, 0, UINT_MAX, 1},
  {"CDC_MODE",      (void*) &config.cdc_control_redirect, UINT8, 0, 5, 1},
  {"TOS",           (void*) config.tos_img, STRING, 0, FF_LFN_BUF, 1},
  {"CARTRIDGE",     (void*) config.cart_img, STRING, 0, FF_LFN_BUF, 1},
  {"ACSI0",         (void*) config.acsi[0].path, STRING, 1, FF_LFN_BUF, 1},
  {"ACSI1",         (void*) config.acsi[1].path, STRING, 1, FF_LFN_BUF, 1},
  {"FDD0",          (void*) config.fdd[0].path, STRING, 1, FF_LFN_BUF, 1},
  {"FDD1",          (void*) config.fdd[1].path, STRING, 1, FF_LFN_BUF, 1},
};

#define TOS_BASE_ADDRESS_192k    0xfc0000
#define TOS_BASE_ADDRESS_256k    0xe00000
#define CART_BASE_ADDRESS        0xfa0000
#define VIDEO_BASE_ADDRESS       0x010000

unsigned char spi_speed;
unsigned char spi_mmc_speed;

static void tos_insert_disk(int, const char *);
static void tos_select_hdd_image(int, const char *);

static void fpga_set_control(unsigned long);

void assign_full_path(char *buf, int buf_size, const char *fname) {
  if (!buf || !buf_size || !fname || buf == fname) return;
  if (*fname == '/') {
    sniprintf(buf, buf_size, "%s", fname);
  } else if (*fname == 0) {
    buf[0] = 0;
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
    if(mode < CDC_REDIRECT_RS232) {
      mode = 0;
    } else {
      mode -= (CDC_REDIRECT_RS232 - 1);
    }

    config.system_ctrl &= ~0x0c000000;
    config.system_ctrl |= (((uint32_t) mode) << 26);

    runtime_ctrl &= ~0x0c000000;
    runtime_ctrl |= (((uint32_t) mode) << 26);

    fpga_set_control(runtime_ctrl);
  }
}

static void fpga_set_control(unsigned long ctrl) {
  EnableFpga();
  SPI(MIST_SET_CONTROL);
  SPI((ctrl >> 24) & 0xff);
  SPI((ctrl >> 16) & 0xff);
  SPI((ctrl >>  8) & 0xff);
  SPI((ctrl >>  0) & 0xff);
  DisableFpga();
}

static void fpga_memory_write(const char *data, size_t lenght) {
  spi_speed = spi_get_speed();
  spi_set_speed(spi_mmc_speed);

  EnableFpga();
  SPI(MIST_WRITE_MEMORY);
  // length must be a multiple of 16-bit words
  spi_write(data, (lenght + 1) & ~1);
  DisableFpga();

  spi_set_speed(spi_speed);
}

static void fpga_memory_read_block(char *data) {
  spi_speed = spi_get_speed();
  spi_set_speed(spi_mmc_speed);

  EnableFpga();
  SPI(MIST_READ_MEMORY);
  spi_block_read(data);
  DisableFpga();

  spi_set_speed(spi_speed);
}

static void fpga_memory_write_blocks(const char *data, int count) {
  spi_speed = spi_get_speed();
  spi_set_speed(spi_mmc_speed);

  EnableFpga();
  SPI(MIST_WRITE_MEMORY);
  spi_write(data, 512 * count);
  DisableFpga();

  spi_set_speed(spi_speed);
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

static int acsi_disk_read(int target, uint32_t lba, size_t length) {
  int read = 0;
  UINT br = 0;

#ifndef SD_NO_DIRECT_MODE
  if (fat_uses_mmc()) {
    // SD-Card -> FPGA direct SPI transfer
    spi_speed = spi_get_speed();
    spi_set_speed(spi_mmc_speed);
    if (IDXSeek(&sd_image[target + 2], lba) == FR_OK
        && f_read(&sd_image[target + 2].file, 0, 512 * length, &br) == FR_OK
        && br == (512 * length))
      read = length;
    spi_set_speed(spi_speed);
  } else {
#endif
    while (length) {
      int blocksize = MIN(length, SECTOR_BUFFER_SIZE / 512);
      if (IDXSeek(&sd_image[target + 2], lba) != FR_OK)
        break;
      if (f_read(&sd_image[target + 2].file, sector_buffer, 512 * blocksize, &br) != FR_OK)
        break;
      if (br != (512 * blocksize))
        break;
      read += blocksize;
      fpga_memory_write_blocks(sector_buffer, blocksize);
      length -= blocksize;
      lba += blocksize;
    }
#ifndef SD_NO_DIRECT_MODE
  }
#endif

  return read;
}

static int acsi_disk_write(int target, uint32_t lba, size_t length) {
  unsigned short blocklen;
  unsigned char *buf;
  int written = 0;
  UINT bw = 0;

  while (length) {
    blocklen = (length > SECTOR_BUFFER_SIZE / 512) ? SECTOR_BUFFER_SIZE / 512 : length;
    buf = sector_buffer;
    int n = blocklen;
    while (n--) {
      fpga_memory_read_block(buf);
      buf += 512;
    }
    if (IDXSeek(&sd_image[target + 2], lba) != FR_OK)
      break;
    if (f_write(&sd_image[target + 2].file, sector_buffer, 512 * blocklen, &bw) != FR_OK)
      break;
    if (bw != (512 * blocklen))
      break;
    written += blocklen;
    lba += blocklen;
    length -= blocklen;
  }

  return written;
}

static void acsi_disk_init(SCSI_DEV *dev, unsigned long hdSize, bool changed) {
  dev->disk_read = acsi_disk_read;
  dev->disk_write = acsi_disk_write;
  dev->dma_write = fpga_memory_write;

  dev->is_locked = false;
  dev->is_changed = changed;
  dev->is_readonly = false;

  if (!dev->is_changed) {
    dev->nLastError = HD_REQSENS_OK;
  }

  dev->bSetLastBlockAddr = false;
  dev->nLastBlockAddr = 0;

  dev->blockSize = 512;
  dev->hdSize = hdSize;
}

static void acsi_init(bool cold_boot) {
  tos_debugf("ACSI: Init(%d)", cold_boot);

  if (cold_boot) {
    memset(&AcsiBus, 0, sizeof(AcsiBus));
  }

  AcsiBus.buffer = sector_buffer;
  AcsiBus.buffer_size = sizeof(sector_buffer);

  for (int i=0; i<ARRAY_SIZE(AcsiBus.devs); i++) {
    SCSI_DEV *dev = &AcsiBus.devs[i];

    acsi_disk_init(dev, cold_boot ? 0 : dev->hdSize, false);
    dev->is_readonly = mmc_write_protected();

    tos_debugf("ACSI: Init: storage[%d] size: %lu", i, dev->hdSize);
  }
}

static void acsi_poll() {
  EnableFpga();
  SPI(MIST_GET_DMASTATE);
  spi_read(AcsiBus.command, 16);
  DisableFpga();

  // DMA busy check
  if (!(AcsiBus.command[10] & 0x01))
    return;

  spi_mmc_speed = SPI_MMC_CLK_VALUE;

  SCSI_DEV *dev = NULL;
  AcsiBus.opcode = AcsiBus.command[0];
  AcsiBus.target = (AcsiBus.command[10] & 0xE0) >> 5;

  // only a harddisk on ACSI 0/1 is supported
  if (AcsiBus.target < 2)
    dev = &AcsiBus.devs[AcsiBus.target];

  if (dev) {
    HDC_HandleCommandPacket(&AcsiBus);
    if (AcsiBus.status != HD_STATUS_OK) {
      errorf("ACSI: opcode=0x%x, target=%i, status=0x%x, error=0x%x",
        AcsiBus.opcode, AcsiBus.target, AcsiBus.status, dev->nLastError);
    }
    dma_ack(AcsiBus.status);
  } else {
    // tell acsi state machine that io controller is done
    // but don't generate a acsi irq
    dma_nak();
  }
}

static inline bool tos_cartridge_is_inserted() {
  return config.cart_img[0];
}

static void tos_load_cartridge(const char *name) {
  FIL file;

  // erase that ram area to remove any previously uploaded image
  tos_debugf("Erasing cartridge memory");
  data_io_fill_tx(0xff, 128*1024, 0x2);

  if(!tos_cartridge_is_inserted()
    || f_open(&file, name, FA_READ) != FR_OK)
    return;

  if(f_size(&file) > 128*1024) {
    tos_debugf("Cartridge file too big: %lu", (uint32_t) f_size(&file));
    f_close(&file);
    return;
  }

  // upload cartridge
  data_io_file_tx(&file, 0x2, 0);
  iprintf("Cartridge %s uploaded\n", config.cart_img);

  f_close(&file);
}

static bool tos_upload_mistery(const char *name) {
  FIL file;
  bool res = true;

  // clear first 16K
  tos_debugf("Clear first 16K");
  data_io_fill_tx(0, 16*1024, 0x3);

  // upload and verify TOS image
  if(f_open(&file, config.tos_img, FA_READ) == FR_OK) {
    iprintf("TOS: %s\n", config.tos_img);
    if(f_size(&file) == 192*1024)
      data_io_file_tx(&file, 0x1, 0);
    else if(f_size(&file) == 256*1024 || f_size(&file) == 512*1024)
      data_io_file_tx(&file, 0x0, 0);
    else {
      tos_debugf("WARNING: Unexpected TOS size");
      res = false;
    }
    f_close(&file);
  } else {
    tos_debugf("Unable to find %s", config.tos_img);
    return false;
  }

  // This is the initial boot if no name was given.
  // Otherwise the user reloaded a new os
  if(!name) {
    // load cartridge
    tos_load_cartridge(config.cart_img);

    for(int i=0; i<2; i++) {
      // try to open floppy image
      if (*config.fdd[i].path) {
        tos_insert_disk(i, config.fdd[i].path);
      }

      // try to open harddisk image
      if (*config.acsi[i].path) {
        tos_debugf("trying to open %s image #%d", config.acsi[i].path, i);
        tos_select_hdd_image(i, config.acsi[i].path);
        AcsiBus.devs[i].is_changed = false; // boot time mount
      }
    }
  }

  return res;
}

void tos_upload(const char *name) {
  tos_debugf("Uploading TOS");

  ResetMenu();

  // put cpu into reset
  config.system_ctrl |= TOS_CONTROL_CPU_RESET;
  fpga_set_control(config.system_ctrl);

  assign_full_path(
    config.tos_img, sizeof(config.tos_img), name);

  tos_upload_mistery(name);

  // let cpu run (release reset)
  config.system_ctrl &= ~TOS_CONTROL_CPU_RESET;
  runtime_ctrl = config.system_ctrl;

  // adjust for detected ethernet adapter
#ifdef USB_ASIX_NET
  if (!eth_present)
#endif
    runtime_ctrl &= ~TOS_CONTROL_ETHERNET;

  fpga_set_control(runtime_ctrl);
}

unsigned long tos_system_ctrl(void) {
  return runtime_ctrl;
}

void tos_update_sysctrl(unsigned long n) {
  runtime_ctrl = n;
  fpga_set_control(runtime_ctrl);
}

static inline const char *tos_get_image_name() {
  return get_fname(config.tos_img);
}

static const char *tos_get_disk_name(int index) {
  if(!user_io_is_mounted(index)) {
    return "* no disk *";
  }

  // 0-1 floppy, 2-3 hdd
  if (index < 2) {
    return get_fname(config.fdd[index].path);
  }

  return get_fname(config.acsi[index-2].path);
}

static const char *tos_get_cartridge_name() {
  if(!config.cart_img[0]) {  // no cart name set
    return "* no cartridge *";
  }

  return get_fname(config.cart_img);
}

static void tos_select_hdd_image(int i, const char *name) {
  if(i < 0 || i > 1) return;

  int slot = i + 2;
  IDXFile *idxfile = &sd_image[slot];
  SCSI_DEV *acsi_dev = NULL;

  // try to re/open harddisk image
  if (idxfile->valid) {
    IDXClose(idxfile);
  }

  runtime_ctrl &= ~(TOS_ACSI0_ENABLE << i);
  config.system_ctrl &= ~(TOS_ACSI0_ENABLE << i);

  if (i < 2) {
    acsi_dev = &AcsiBus.devs[i];
    acsi_disk_init(acsi_dev, 0, true);
  }

  if(name && name[0]) {
    FRESULT res = IDXOpen(idxfile, name, FA_READ | FA_WRITE);
    if (res != FR_OK) res = IDXOpen(idxfile, name, FA_READ);
    if (res == FR_OK) {
      assign_full_path(config.acsi[i].path, sizeof(config.acsi[0].path), name);
      iprintf("ACSI%d: %s\n", i, config.acsi[i].path);
      IDXIndex(idxfile, slot);
      runtime_ctrl |= (TOS_ACSI0_ENABLE << i);
      config.system_ctrl |= (TOS_ACSI0_ENABLE << i);
      if (acsi_dev) {
        acsi_dev->hdSize = f_size(&(idxfile->file)) / acsi_dev->blockSize;
        tos_debugf("ACSI: new image[%d] size: %lu", slot, acsi_dev->hdSize);
        acsi_dev->is_readonly |= !(idxfile->file.flag & FA_WRITE);
      }
    } else {
      errorf("Cannot open %s file, error %d", name, res);
    }
  } else {
    config.acsi[i].path[0] = 0;
  }

  // update system control
  fpga_set_control(runtime_ctrl);
}

static void tos_insert_disk(int i, const char *name) {
  if(i < 0 || i > 1) return;

  tos_debugf("%c: eject", i+'A');

  // toggle write protect bit to help tos detect a media change
  uint32_t wp_bit = (!i) ? TOS_CONTROL_FDC_WR_PROT_A : TOS_CONTROL_FDC_WR_PROT_B;

  // any disk ejected is "write protected" (as nothing covers the write protect mechanism)
  fpga_set_control(runtime_ctrl | wp_bit);

  // first "eject" disk
  user_io_file_mount(NULL, i);

  if(name && name[0]) {
    if(user_io_file_mount(name, i)) {
      assign_full_path(config.fdd[i].path, sizeof(config.fdd[0].path), name);
      iprintf("%c: %s\n", i+'A', config.fdd[i].path);
    }
  } else {
    config.fdd[i].path[0] = 0;
  }

  // update system control
  fpga_set_control(runtime_ctrl);
}

static const char *get_config_fname(int slot) {
  static char fname[16];
  if(slot) {
    sniprintf(fname, sizeof(fname), "/ATARIST%d.CFG", slot);
  } else {
    strcpy(fname, "/ATARIST.CFG");
  }
  return fname;
}

static void tos_config_load(int slot) {
  // load/init configuration
  static int last_slot = 0;
  int new_slot = (slot == -1) ? last_slot : slot;

  tos_eject_all();

  // try to load config
  ini_cfg_t config_ini = {
    .filename = get_config_fname(new_slot),
    .sections = config_sections,
    .nsections = ARRAY_SIZE(config_sections),
    .vars = config_vars,
    .nvars =  ARRAY_SIZE(config_vars),
  };

  if (ini_parse(&config_ini, 0, 0)) {
    runtime_ctrl = config.system_ctrl;
    last_slot = slot;
    return;
  }

  // set default values
  config.system_ctrl = TOS_CONTROL_STE | TOS_MEMCONFIG_1M | TOS_CONTROL_VIDEO_COLOR;
  config.cdc_control_redirect = CDC_REDIRECT_NONE;

  strcpy(config.tos_img, "/TOS.IMG");
  strcpy(config.acsi[0].path, "/HARDDISK.HD");
  strcpy(config.fdd[0].path, "DISK_A.ST");

  config.acsi[1].path[0] = 0;
  config.fdd[1].path[0] = 0;
  config.cart_img[0] = 0;

  runtime_ctrl = config.system_ctrl;
}

static bool tos_config_save(int slot) {
  // save configuration
  const ini_cfg_t config_ini = {
    .filename = get_config_fname(slot),
    .sections = config_sections,
    .nsections = ARRAY_SIZE(config_sections),
    .vars = config_vars,
    .nvars =  ARRAY_SIZE(config_vars),
  };

  return ini_save(&config_ini, 0);
}

static bool tos_config_exists(int slot) {
  FIL file;

  // configuration file check
  if (f_open(&file, get_config_fname(slot), FA_READ) != FR_OK)
    return false;

  f_close(&file);
  return true;
}

void tos_eject_all() {
  // ejecting floppies
  for (int i=0; i<2; i++) {
    tos_insert_disk(i, NULL);
  }

  // unmounting ACSI (removable) mediums
  for (int i=0; i<ARRAY_SIZE(AcsiBus.devs); i++) {
    SCSI_DEV *dev = &AcsiBus.devs[i];
    acsi_disk_init(dev, 0, true);
    tos_debugf("ACSI: Init: storage[%d] size: %lu", i, dev->hdSize);
  }
}

void tos_reset(bool cold_boot) {
  fpga_set_control(config.system_ctrl | TOS_CONTROL_CPU_RESET);  // set reset

  timer_delay_msec(10);
  acsi_init(cold_boot);

  if (cold_boot) {
    tos_upload(NULL);
  }

  fpga_set_control(config.system_ctrl & ~TOS_CONTROL_CPU_RESET);  // release reset
}

void tos_poll() {
  acsi_poll();

  // check the user button
  if (UserButton()) {
    tos_reset(true);
  }
}

void tos_init() {
  acsi_init(true);
  tos_config_load(-1);
}

///////////////////////////
////// Atari ST menu //////
///////////////////////////

static const char* offon[] = {"Off", "On"};
static const char* stereo[] = {"Mono", "Stereo"};
static const char* scanlines[] = {"Off", "25%", "50%", "75%"};
static const char *config_tos_wrprot[] = {"None", "A:", "B:", "A: and B:"};
static const char* atari_chipset[] = {"ST", "STE", "MegaSTE", "STEroids"};
static const char *config_tos_mem[] = {"512 Kb", "1 Mb", "2 Mb", "4 Mb", "8 Mb", "14 Mb", "--", "--"};
static const char *config_tos_usb[] = {"None", "RS232", "Parallel", "MIDI"};
static const char *config_tos_cart[] = {"File", "Ethernec", "Cubase"};

static char tos_file_selected(uint8_t idx, const char *SelectedName) {
  switch(idx) {
    case 0:
    case 1: // Floppy
      menu_debugf("Insert image %s for disk %d", SelectedName, idx);
      tos_insert_disk(idx, SelectedName);
      break;
    case 7:
    case 8: // ACSI
      menu_debugf("Insert image %s for ACSI disk %d", SelectedName, idx-7);
      tos_select_hdd_image(idx-7, SelectedName);
      break;
    case 28: // TOS
      assign_full_path(
        config.tos_img, sizeof(config.tos_img), SelectedName);
      break;
    case 36: // Cartridge
      assign_full_path(
        config.cart_img, sizeof(config.cart_img), SelectedName);
      break;
  }

  return 0;
}

static char tos_get_menu_page(uint8_t idx, char action, menu_page_t *page) {
  if (action==MENU_PAGE_EXIT)
    return 0;

  page->flags = OSD_ARROW_RIGHT;
  page->stdexit = MENU_STD_EXIT;
  page->timer = 500;

  switch (idx) {
    case 0:
      page->title = "\x0e\x0f Atari ST";
      page->flags = OSD_ARROW_RIGHT;
      break;
    case 1:
      page->title = "Storage";
      page->flags = OSD_ARROW_LEFT;
      break;
    case 2:
      page->title = "Settings";
      page->flags = OSD_ARROW_LEFT | OSD_ARROW_RIGHT;
      break;
    case 3:
      page->title = "Load";
      page->flags = OSD_ARROW_LEFT;
      break;
    case 4:
      page->title = "Save";
      page->flags = OSD_ARROW_LEFT;
      break;
    case 5:
      page->title = "System";
      page->flags = OSD_ARROW_LEFT;
      break;
    case 6:
      page->title = "Video";
      page->flags = OSD_ARROW_LEFT;
      break;
    case 7:
      page->title = "Features";
      page->flags = OSD_ARROW_LEFT;
      break;
  };

  return 0;
}

static char tos_get_menu_item(uint8_t idx, char action, menu_item_t *item) {
  int page_idx = item->page; // save current page number
  bool is_medium_present = fat_medium_present();
  bool enable;

  item->stipple = 0;
  item->active = 1;
  item->page = 0;
  item->newpage = 0;
  item->newsub = 0;
  item->item = "";
  if(idx<=6) item->page = 0;
  else if(idx<=8)  item->page = 1;
  else if(idx<=14) item->page = 2;
  else if(idx<=19) item->page = 3;
  else if(idx<=24) item->page = 4;
  else if(idx<=28) item->page = 5;
  else if(idx<=32) item->page = 6;
  else if(idx<=37) item->page = 7;
  else return 0;

  if (item->page != page_idx)
    return 1; // shortcut

  switch (action) {
    case MENU_ACT_GET:
      switch(idx) {
        case 0:
        case 1:
          // entries for both floppies
          strcpy(s, " A: ");
          strcat(s, tos_get_disk_name(idx));
          s[1] = 'A'+idx;
          if(config.system_ctrl & (TOS_CONTROL_FDC_WR_PROT_A << idx))
            strcat(s, " \x17");
          item->active = is_medium_present;
          item->stipple = !item->active;
          item->item = s;
          break;
        case 2:
          strcpy(s, " Write Protect: ");
          strcat(s, config_tos_wrprot[(config.system_ctrl >> 6) & 3]);
          item->item = s;
          break;
        case 4:
          item->item = " Hard Disks";
          item->newpage = 1;
          break;
        case 5:
          item->item = " Reset";
          break;

        // Page 1 - Storage
        case 7:
        case 8:
          {
            SCSI_DEV *acsi_dev = &AcsiBus.devs[idx-7];
            strcpy(s, " ACSI0: ");
            s[5] = '0'+idx-7;
            if (acsi_dev->is_locked) {
              strcat(s, "Locked by TOS");
              item->active = false;
            } else {
              strcat(s, tos_get_disk_name(2+idx-7));
              item->active = is_medium_present;
            }
            if(acsi_dev->is_readonly) strcat(s, " \x17");
            item->stipple = !item->active;
            item->item = s;
            break;
          }

        // Page 2 - Settings
        case 9:
          item->item = " Load config";
          item->active = is_medium_present;
          item->stipple = !item->active;
          item->newpage = 3;
          break;
        case 10:
          item->item = " Save config";
          item->active = is_medium_present;
          item->stipple = !item->active;
          item->newpage = 4;
          break;
        case 12:
          item->item = " System";
          item->newpage = 5;
          break;
        case 13:
          item->item = " Video";
          item->newpage = 6;
          break;
        case 14:
          item->item = " Features";
          item->newpage = 7;
          break;

        // Page 3 - Load config
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
          if(!tos_config_exists(idx-15)) {
            item->stipple = 1;
            item->active = 0;
          }
          strcpy(s,"          ");
          strcat(s, atarist_cfg.conf_name[idx-15]);
          item->item = s;
          break;

        // Page 4 - Save config
        case 20:
        case 21:
        case 22:
        case 23:
        case 24:
          strcpy(s,"          ");
          strcat(s, atarist_cfg.conf_name[idx-20]);
          item->item = s;
          break;

        // Page 5 - System
        case 25:
          strcpy(s, " Memory:    ");
          strcat(s, config_tos_mem[(config.system_ctrl >> 1) & 7]);
          item->item = s;
          break;
        case 26:
          strcpy(s, " CPU:       ");
          strcat(s, config_cpu_msg[(config.system_ctrl >> 4) & 3]);
          item->item = s;
          break;
        case 27:
          strcpy(s, " Chipset:   ");
          strcat(s, atari_chipset[(config.system_ctrl >> 23) & 3]);
          item->item = s;
          break;
        case 28:
          strcpy(s, " TOS:       ");
          strcat(s, tos_get_image_name());
          item->item = s;
          break;

        // Page 6 - Video
        case 29:
          strcpy(s, " Screen:        ");
          strcat(s, (config.system_ctrl & TOS_CONTROL_VIDEO_COLOR)
            ? "Color" : "Mono");
          item->item = s;
          break;
        case 30: // Viking card can only be enabled with max 8MB RAM
          enable = (config.system_ctrl & 0xe) <= TOS_MEMCONFIG_8M;
          strcpy(s, " Viking/SM194:  ");
          strcat(s, offon[!!((config.system_ctrl & TOS_CONTROL_VIKING) && enable)]);
          item->item = s;
          item->active = enable;
          item->stipple = !enable;
          break;
        case 31:
          strcpy(s, " Scanlines:     ");
          strcat(s, scanlines[(config.system_ctrl >> 20) & 3]);
          item->item = s;
          break;
        case 32:
          strcpy(s, " Comp. blend:   ");
          strcat(s, offon[!!(config.system_ctrl & TOS_CONTROL_BLEND)]);
          item->item = s;
          break;

        // Page 7 - Features
        case 33:
          strcpy(s, " YM-Audio:  ");
          strcat(s, stereo[!!(config.system_ctrl & TOS_CONTROL_STEREO)]);
          item->item = s;
          break;
        case 34:
          // Blitter is always present in >= STE
          enable = !!(config.system_ctrl & (TOS_CONTROL_STE | TOS_CONTROL_MSTE));
          strcpy(s, " Blitter:   ");
          strcat(s, offon[!!((config.system_ctrl & TOS_CONTROL_BLITTER) || enable)]);
          item->item = s;
          item->active = !enable;
          item->stipple = enable;
          break;
        case 35: {
          uint8_t cartport = ((config.system_ctrl & TOS_CONTROL_ETHERNET) ? 1 : 0)
                             | ((config.system_ctrl & TOS_CONTROL_CUBASE) ? 2 : 0);
          strcpy(s, " Cart.port: ");
          strcat(s, config_tos_cart[cartport]);
          item->item = s;
          }
          break;
        case 36:
          strcpy(s, " Cartridge: ");
          strcat(s, tos_get_cartridge_name());
          item->item = s;
          if (config.system_ctrl & (TOS_CONTROL_ETHERNET | TOS_CONTROL_CUBASE)) {
            item->stipple = 1;
            item->active = 0;
          }
          break;
        case 37:
          strcpy(s, " USB I/O:   ");
          strcat(s, config_tos_usb[(config.system_ctrl >> 26) & 3]);
          item->item = s;
          break;

        default:
          item->active = 0;
      }
      break;

    case MENU_ACT_SEL:
      switch(idx) {
        case 0:
        case 1:
          if(user_io_is_mounted(idx))
            tos_insert_disk(idx, NULL);
          else
            SelectFileNG("ST ", SCAN_DIR | SCAN_LFN, tos_file_selected, 0);
          break;
        case 2: // Write protect
          {
            uint32_t wr_prot = (config.system_ctrl >> 6) & 3;
            config.system_ctrl &= ~(TOS_CONTROL_FDC_WR_PROT_A | TOS_CONTROL_FDC_WR_PROT_B);
            runtime_ctrl &= ~(TOS_CONTROL_FDC_WR_PROT_A | TOS_CONTROL_FDC_WR_PROT_B);
            config.system_ctrl |= (((wr_prot + 1) & 3) << 6);
            runtime_ctrl |= (((wr_prot + 1) & 3) << 6);
            fpga_set_control(runtime_ctrl);
          }
          break;
        case 4:
          item->newpage = 1;
          break;
        case 5: // Reset (Cold Boot)
          tos_reset(1);
          CloseMenu();
          break;

        // Page 1 - Storage
        case 7:
        case 8:
          debugf("Select image for disk %d", 2+idx-7);
          if(user_io_is_mounted(2+idx-7))
            tos_select_hdd_image(idx-7, NULL);
          else
            SelectFileNG("IMGVHDHD ", SCAN_DIR | SCAN_LFN, tos_file_selected, 0);
          break;

        // Page 2 - Settings
        case 9:
          item->newpage = 3;
          break;
        case 10:
          item->newpage = 4;
          break;
        case 12:
          item->newpage = 5;
          break;
        case 13:
          item->newpage = 6;
          break;
        case 14:
          item->newpage = 7;
          break;

        // Page 3 - Load config
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
          tos_insert_disk(2, NULL);
          tos_insert_disk(3, NULL);
          tos_config_load(idx-15);
          tos_upload(NULL);
          CloseMenu();
          break;

        // Page 4 - Save config
        case 20:
        case 21:
        case 22:
        case 23:
        case 24:
          tos_config_save(idx-20);
          CloseMenu();
          break;

        // Page 5 - System
        case 25: // Memory
          {
            uint32_t mem = (config.system_ctrl >> 1) & 7; // current RAM config
            mem++;
            if(mem > 5) mem = 0;
            config.system_ctrl &= ~0x0e;
            config.system_ctrl |= (mem << 1);
          }
          break;
        case 26: // CPU
          {
            uint32_t cpu = (config.system_ctrl >> 4) & 3; // current CPU config
            cpu = (cpu + 1) & 3;
            if(cpu == 2 || cpu == 1) cpu = 3; // skip unused config
            config.system_ctrl &= ~0x30;
            config.system_ctrl |= (cpu << 4);
          }
          break;
        case 27: // Chipset
          {
            uint32_t chipset = ((config.system_ctrl >> 23) + 1) & 3;
            config.system_ctrl &= ~(TOS_CONTROL_STE | TOS_CONTROL_MSTE);
            config.system_ctrl |= (chipset << 23);
          }
          break;
        case 28: // TOS
          SelectFileNG("IMGROM", SCAN_DIR | SCAN_LFN, tos_file_selected, 0);
          break;

        // Page 6 - Video
        case 29: // Screen
          config.system_ctrl ^= TOS_CONTROL_VIDEO_COLOR;
          break;
        case 30: // Viking
          config.system_ctrl ^= TOS_CONTROL_VIKING;
          break;
        case 31: // Scanlines
          {
            uint32_t scan = ((config.system_ctrl >> 20) + 1) & 3;
            config.system_ctrl &= ~TOS_CONTROL_SCANLINES;
            runtime_ctrl &= ~TOS_CONTROL_SCANLINES;
            config.system_ctrl |= (scan << 20);
            runtime_ctrl |= (scan << 20);
            fpga_set_control(runtime_ctrl);
          }
          break;
        case 32: // Comp.blend
          config.system_ctrl ^= TOS_CONTROL_BLEND;
          runtime_ctrl ^= TOS_CONTROL_BLEND;
          fpga_set_control(runtime_ctrl);
          break;

        // Page 7 - Features
        case 33: // YM-Audio
          config.system_ctrl ^= TOS_CONTROL_STEREO;
          runtime_ctrl ^= TOS_CONTROL_STEREO;
          fpga_set_control(runtime_ctrl);
          break;
        case 34: // Blitter
          config.system_ctrl ^= TOS_CONTROL_BLITTER;
          runtime_ctrl ^= TOS_CONTROL_BLITTER;
          fpga_set_control(runtime_ctrl);
          break;
        case 35: // Cart.port
          {
            uint8_t cartport = ((config.system_ctrl & TOS_CONTROL_ETHERNET) ? 1 : 0)
                               | ((config.system_ctrl & TOS_CONTROL_CUBASE) ? 2 : 0);
            cartport++;
            if (cartport > 2) cartport = 0;
            config.system_ctrl &= ~(TOS_CONTROL_ETHERNET | TOS_CONTROL_CUBASE);
            runtime_ctrl &= ~(TOS_CONTROL_ETHERNET | TOS_CONTROL_CUBASE);
            if (cartport & 1) {
              config.system_ctrl |= TOS_CONTROL_ETHERNET;
              runtime_ctrl |= TOS_CONTROL_ETHERNET;
            }
            if (cartport & 2) {
              config.system_ctrl |= TOS_CONTROL_CUBASE;
              runtime_ctrl |= TOS_CONTROL_CUBASE;
            }
            fpga_set_control(runtime_ctrl);
          }
          break;
        case 36: // Cartridge
          if(tos_cartridge_is_inserted())
            assign_full_path(config.cart_img, sizeof(config.cart_img), "");
          else
            SelectFileNG("IMGROM", SCAN_DIR | SCAN_LFN, tos_file_selected, 0);
          break;
        case 37: // USB I/O
          switch(((config.system_ctrl >> 26) + 1) & 3)
          {
            case 0:
              tos_set_cdc_control_redirect(CDC_REDIRECT_NONE);
              break;
            case 1:
              tos_set_cdc_control_redirect(CDC_REDIRECT_RS232);
              break;
            case 2:
              tos_set_cdc_control_redirect(CDC_REDIRECT_PARALLEL);
              break;
            case 3:
              tos_set_cdc_control_redirect(CDC_REDIRECT_MIDI);
              break;
          }
          break;

        default:
          return 0;
      }
      break;

    case MENU_ACT_RIGHT:
      switch(page_idx) {
        case 0: // main
          item->newpage = 2;
          break;
        case 2: // Settings
          SetupSystemMenu();
          break;
        default:
          return 0;
      }
      break;

    case MENU_ACT_LEFT:
      switch(page_idx) {
        case 1: // Storage
        case 2: // return to main
          ClosePage();
          break;
        case 3: // Load
        case 4: // Save
        case 5: // System
        case 6: // Video
        case 7: // Features
          item->newpage = 2;
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
  SetupMenu(tos_get_menu_page, tos_get_menu_item, NULL);
}
