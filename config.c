#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "hardware.h"
#include "boot.h"
#include "fat_compat.h"
#include "ini_parser.h"
#include "osd.h"
#include "fpga.h"
#include "fdd.h"
#include "hdd.h"
#include "firmware.h"
#include "menu.h"
#include "config.h"
#include "user_io.h"
#include "usb/usb.h"
#include "misc_cfg.h"
#include "menu-minimig.h"

configTYPE config;
static configTYPE tmpconf;
static char configfilename[13];

extern char s[OSD_BUF_SIZE];
static unsigned char *romkey = (sector_buffer + 512);

static const ini_section_t config_ini_sections[] = {
  {1, "MINIMIG"}
};

static const ini_var_t config_ini_vars[] = {
  {"KICKSTART",        (void*)tmpconf.kickstart, STRING, 1, 79, 1},
  {"FILTER_LO",        (void*)&tmpconf.filter.lores, UINT8, 0, 3, 1},
  {"FILTER_HI",        (void*)&tmpconf.filter.hires, UINT8, 0, 3, 1},
  {"MEMORY",           (void*)&tmpconf.memory, UINT8, 0, 127, 1},
  {"CHIPSET",          (void*)&tmpconf.chipset, UINT8, 0, 127, 1},
  {"FLOPPY_SPD",       (void*)&tmpconf.floppy.speed, UINT8, 0, 1, 1},
  {"FLOPPY_CNT",       (void*)&tmpconf.floppy.drives, UINT8, 0, 4, 1},
  {"AR3_DISABLE",      (void*)&tmpconf.disable_ar3, UINT8, 0, 1, 1},
  {"IDE0_ENABLE",      (void*)&tmpconf.enable_ide[0], UINT8, 0, 1, 1},
  {"IDE1_ENABLE",      (void*)&tmpconf.enable_ide[1], UINT8, 0, 1, 1},
  {"SCANLINES",        (void*)&tmpconf.scanlines, UINT8, 0, 15, 1},
  {"HDD0_ENABLE",      (void*)&tmpconf.hardfile[0].enabled, UINT32, 0, 255, 1},
  {"HDD0",             (void*)tmpconf.hardfile[0].path, STRING, 1, 63, 1},
  {"HDD1_ENABLE",      (void*)&tmpconf.hardfile[1].enabled, UINT32, 0, 255, 1},
  {"HDD1",             (void*)tmpconf.hardfile[1].path, STRING, 1, 63, 1},
  {"HDD2_ENABLE",      (void*)&tmpconf.hardfile[2].enabled, UINT32, 0, 255, 1},
  {"HDD2",             (void*)tmpconf.hardfile[2].path, STRING, 1, 63, 1},
  {"HDD3_ENABLE",      (void*)&tmpconf.hardfile[3].enabled, UINT32, 0, 255, 1},
  {"HDD3",             (void*)tmpconf.hardfile[3].path, STRING, 1, 63, 1},
  {"CPU",              (void*)&tmpconf.cpu, UINT8, 0, 15, 1},
  {"AUTOFIRE",         (void*)&tmpconf.autofire, UINT8, 0, 7, 1},
  {"AUDIOFILTERMODE",  (void*)&tmpconf.features.audiofiltermode, UINT8, 0, 2, 1},
  {"POWERLEDOFFSTATE", (void*)&tmpconf.features.powerledoffstate, UINT8, 0, 1, 1}
};

static void ClearKickstartMirrorE0(void)
{
  spi_osd_cmd32le_cont(OSD_CMD_WR, 0x00e00000);
  for (int i = 0; i < (0x80000 / 4); i++) {
    SPI(0x00);
    SPI(0x00);
    delay_usec(1);
    SPI(0x00);
    SPI(0x00);
    delay_usec(1);
  }
  DisableOsd();
  delay_usec(1);
}

static void ClearVectorTable(void)
{
  spi_osd_cmd32le_cont(OSD_CMD_WR, 0x00000000);
  for (int i = 0; i < 256; i++) {
    SPI(0x00);
    SPI(0x00);
    delay_usec(1);
    SPI(0x00);
    SPI(0x00);
    delay_usec(1);
  }
  DisableOsd();
  delay_usec(1);
}

//// UploadKickstart() ////
bool UploadKickstart(const char *name)
{
  UINT br;
  FIL romfile, keyfile;
  unsigned long keysize = 0;

  ResetMenu();
  BootPrint("Checking for Amiga Forever key file:");

  if(f_open(&keyfile,"/ROM.KEY", FA_READ) == FR_OK) {
    keysize=f_size(&keyfile);
    if(keysize<(SECTOR_BUFFER_SIZE-512)) {
      f_read(&keyfile, romkey, keysize, &br);
      BootPrint("Loaded Amiga Forever key file");
    } else {
      BootPrint("Amiga Forever keyfile is too large!");
    }
    f_close(&keyfile);
  }

  BootPrint("Loading file: ");
  BootPrint(name);

  if (f_open(&romfile, name, FA_READ) == FR_OK) {
    if (f_size(&romfile) == 0x100000) {
      // 1MB Kickstart ROM
      BootPrint("Uploading 1MB Kickstart ...");
      SendFileV2(&romfile, NULL, 0, 0xe00000, f_size(&romfile)>>10);
      SendFileV2(&romfile, NULL, 0, 0xf80000, f_size(&romfile)>>10);
      ClearVectorTable();
      f_close(&romfile);
      return true;
    } else if(f_size(&romfile) == 0x80000) {
      // 512KB Kickstart ROM
      BootPrint("Uploading 512KB Kickstart ...");
      SendFileV2(&romfile, NULL, 0, 0xf80000, f_size(&romfile)>>9);
      f_rewind(&romfile);
      SendFileV2(&romfile, NULL, 0, 0xe00000, f_size(&romfile)>>9);
      ClearVectorTable();
      f_close(&romfile);
      return true;
    } else if ((f_size(&romfile) == 0x8000b) && keysize) {
      // 512KB Kickstart ROM
      BootPrint("Uploading 512 KB Kickstart (Probably Amiga Forever encrypted...)");
      SendFileV2(&romfile, romkey, keysize, 0xf80000, f_size(&romfile)>>9);
      f_rewind(&romfile);
      SendFileV2(&romfile, romkey, keysize, 0xe00000, f_size(&romfile)>>9);
      ClearVectorTable();
      f_close(&romfile);
      return true;
    } else if (f_size(&romfile) == 0x40000) {
      // 256KB Kickstart ROM
      BootPrint("Uploading 256 KB Kickstart...");
      SendFileV2(&romfile, NULL, 0, 0xf80000, f_size(&romfile)>>9);
      f_rewind(&romfile);
      SendFileV2(&romfile, NULL, 0, 0xfc0000, f_size(&romfile)>>9);
      ClearVectorTable();
      ClearKickstartMirrorE0();
      f_close(&romfile);
      return true;
    } else if ((f_size(&romfile) == 0x4000b) && keysize) {
      // 256KB Kickstart ROM
      BootPrint("Uploading 256 KB Kickstart (Probably Amiga Forever encrypted...");
      SendFileV2(&romfile, romkey, keysize, 0xf80000, f_size(&romfile)>>9);
      f_rewind(&romfile);
      SendFileV2(&romfile, romkey, keysize, 0xfc0000, f_size(&romfile)>>9);
      ClearVectorTable();
      ClearKickstartMirrorE0();
      f_close(&romfile);
      return true;
    } else {
      f_close(&romfile);
      BootPrint("Unsupported ROM file size!");
    }
  } else {
    siprintf(s, "No \"%s\" file!", name);
    BootPrint(s);
  }

  return false;
}

//// UploadActionReplay() ////
static bool UploadActionReplay()
{
  FIL romfile;
  if (f_open(&romfile, "HRTMON.ROM", FA_READ) != FR_OK) {
    return false;
  }

  puts("Uploading HRTmon ROM... ");
  SendFileV2(&romfile, NULL, 0, 0xa10000, (f_size(&romfile)+511)>>9);

  // HRTmon config
  uint32_t addr, data;
  addr = 0xa10000 + 20;
  spi_osd_cmd32le_cont(OSD_CMD_WR, addr);
  data = 0x00800000; // mon_size, 4 bytes
  SPI((data>>24)&0xff); SPI((data>>16)&0xff); delay_usec(1); SPI((data>>8)&0xff); SPI((data>>0)&0xff);
  data = 0x00; // col0h, 1 byte
  SPI((data>>0)&0xff);
  data = 0x5a; // col0l, 1 byte
  SPI((data>>0)&0xff);
  delay_usec(1);
  data = 0x0f; // col1h, 1 byte
  SPI((data>>0)&0xff);
  data = 0xff; // col1l, 1 byte
  SPI((data>>0)&0xff);
  delay_usec(1);
  data = 0xff; // right, 1 byte
  SPI((data>>0)&0xff);
  delay_usec(1);
  data = 0x00; // keyboard, 1 byte
  SPI((data>>0)&0xff);
  delay_usec(1);
  data = 0xff; // key, 1 byte
  SPI((data>>0)&0xff);
  delay_usec(1);
  data = config.enable_ide[0] ? 0xff : 0; // ide, 1 byte
  SPI((data>>0)&0xff);
  delay_usec(1);
  data = 0xff; // a1200, 1 byte
  SPI((data>>0)&0xff);
  delay_usec(1);
  data = config.chipset&CONFIG_AGA ? 0xff : 0; // aga, 1 byte
  SPI((data>>0)&0xff);
  delay_usec(1);
  data = 0xff; // insert, 1 byte
  SPI((data>>0)&0xff);
  delay_usec(1);
  data = 0x0f; // delay, 1 byte
  SPI((data>>0)&0xff);
  delay_usec(1);
  data = 0xff; // lview, 1 byte
  SPI((data>>0)&0xff);
  delay_usec(1);
  data = 0x00; // cd32, 1 byte
  SPI((data>>0)&0xff);
  delay_usec(1);
  data = !!(config.chipset & CONFIG_NTSC); // screenmode, 1 byte
  SPI((data>>0)&0xff);
  delay_usec(1);
  data = 0xff; // novbr, 1 byte
  SPI((data>>0)&0xff);
  delay_usec(1);
  data = 0; // entered, 1 byte
  SPI((data>>0)&0xff);
  delay_usec(1);
  data = 1; // hexmode, 1 byte
  SPI((data>>0)&0xff);
  delay_usec(1);
  DisableOsd();
  delay_usec(1);
  addr = 0xa10000 + 68;
  spi_osd_cmd32le_cont(OSD_CMD_WR, addr);
  data = ((config.memory&0x3) + 1) * 512 * 1024; // maxchip, 4 bytes TODO is this correct?
  SPI((data>>24)&0xff); SPI((data>>16)&0xff); delay_usec(1); SPI((data>>8)&0xff); SPI((data>>0)&0xff);
  delay_usec(1);
  DisableOsd();
  delay_usec(1);
  f_close(&romfile);
  return true;
}

//// SetConfigurationFilename() ////
void SetConfigurationFilename(int slot)
{
  if (slot) {
    siprintf(configfilename, "/MINIMIG%d.CFG", slot);
  } else {
    strcpy(configfilename, "/MINIMIG.CFG");
  }
}

//// ConfigurationExists() ////
bool ConfigurationExists(const char *filename)
{
  FIL file;
  if (!filename) {
    // use slot-based filename if none provided
    filename = configfilename;
  }
  if (f_open(&file, filename, FA_READ) == FR_OK) {
    f_close(&file);
    return true;
  }
  return false;
}

static void ApplyConfiguration(bool);

//// LoadConfiguration() ////
bool LoadConfiguration(const char *filename, bool verbose)
{
  uint32_t key;
  bool result = false;

  if(!filename) {
    // use slot-based filename if none provided
    filename = configfilename;
  }

  memset((void*)&tmpconf, 0, sizeof(config));

  const ini_cfg_t config_ini = {
    .filename = filename,
    .nsections = ARRAY_SIZE(config_ini_sections),
    .sections = config_ini_sections,
    .nvars = ARRAY_SIZE(config_ini_vars),
    .vars = config_ini_vars,
  };

  ini_parse(&config_ini, 0, 0);

  if(tmpconf.floppy.drives<=4 && tmpconf.kickstart[0]) {
    // successfully loaded the config
    memcpy((void*)&config, (void*)&tmpconf, sizeof(config));
    result = true;
  } else {
    // using default configuration
    memset((void*)&config, 0, sizeof(config));
    strncpy(config.kickstart, "KICK.ROM", sizeof(config.kickstart));
    config.memory = 0x11;
    config.floppy.drives = 1;
    config.floppy.speed = CONFIG_FLOPPY2X;
    BootPrintEx("*** No config found. Using defaults.");
    BootPrintEx(" ");
  }

  // print config to boot screen
  if (verbose) {
    char cfg_str[81];
    siprintf(cfg_str, "CPU:     %s", config_cpu_msg[config.cpu & 0x03]); BootPrintEx(cfg_str);
    siprintf(cfg_str, "Chipset: %s", config_chipset_msg[(config.chipset >> 2) & 7]); BootPrintEx(cfg_str);
    siprintf(cfg_str, "Memory:  CHIP: %s  FAST: %s  SLOW: %s%s",
        config_memory_chip_msg[(config.memory >> 0) & 0x03],
        config_memory_fast_txt(),
        config_memory_slow_msg[(config.memory >> 2) & 0x03],
        minimig_cfg.kick1x_memory_detection_patch ? "  [Kick 1.x patch enabled]" : "");
    BootPrintEx(cfg_str);
  }

  key = OsdGetCtrl();
  if (key == KEY_F1) {
    // force NTSC mode if F1 pressed
    config.chipset |= CONFIG_NTSC;
  }

  if (key == KEY_F2) {
    // force PAL mode if F2 pressed
    config.chipset &= ~CONFIG_NTSC;
  }

  ApplyConfiguration(true);
  return result;
}

//// ApplyConfiguration() ////
static void ApplyConfiguration(bool reloadkickstart)
{
  ConfigCPU(config.cpu);

  if (!reloadkickstart) {
    ConfigChipset(config.chipset);
    ConfigFloppy(config.floppy.drives, config.floppy.speed);
  }

  bool idxfail = false;

  for (int i = 0; i < ARRAY_SIZE(config.hardfile); i++)
    hardfile[i] = &config.hardfile[i];

  ResetMenu();

  // Whether or not we uploaded a kickstart image we now need to set various parameters from the config.
  for (int i = 0; i < ARRAY_SIZE(hdf); i++) {
    if (OpenHardfile(i, true)) {
      switch(hdf[i].type) {
        // Customise message for SD card acces
        case (HDF_FILE | HDF_SYNTHRDB):
          siprintf(s, "\nHardfile %d (with fake RDB): %s", i, get_fname(hardfile[i]->path));
          break;
        case HDF_FILE:
          siprintf(s, "\nHardfile %d: %s", i, get_fname(hardfile[i]->path));
          break;
        case HDF_CARD:
          siprintf(s, "\nHardfile %d: using entire SD card", i);
          break;
        case HDF_CDROM:
          siprintf(s, "\nHardfile %d: CDROM", i);
          break;
        default:
          siprintf(s, "\nHardfile %d: using SD card partition %d", i, hdf[i].type-HDF_CARD);  // Number from 1
          break;
      }
      BootPrint(s);
      siprintf(s, "CHS: %u.%u.%u", hdf[i].cylinders, hdf[i].heads, hdf[i].sectors);
      BootPrint(s);
      siprintf(s, "Size: %lu MB", ((((unsigned long) hdf[i].cylinders) * hdf[i].heads * hdf[i].sectors) >> 11));
      BootPrint(s);
      siprintf(s, "Offset: %ld", hdf[i].offset);
      BootPrint(s);
      if (((hdf[i].type & HDF_TYPEMASK) == HDF_FILE) && !hdf[i].idxfile->file.cltbl)
        idxfail = true;
    }
  }

  if (idxfail) {
    BootPrintEx("*** Indexing failed for a hardfile, continuing without indices.");
  }

  ConfigIDE(config.enable_ide[0],        config.hardfile[0].present && config.hardfile[0].enabled, config.hardfile[1].present && config.hardfile[1].enabled);
  ConfigIDE(config.enable_ide[1] | 0x02, config.hardfile[2].present && config.hardfile[2].enabled, config.hardfile[3].present && config.hardfile[3].enabled);

  siprintf(s, "CPU clock     : %s", config.chipset & 0x01 ? "turbo" : "normal");
  BootPrint(s);
  siprintf(s, "Chip RAM size : %s", config_memory_chip_msg[config.memory & 0x03]);
  BootPrint(s);
  siprintf(s, "Slow RAM size : %s", config_memory_slow_msg[config.memory >> 2 & 0x03]);
  BootPrint(s);
  siprintf(s, "Fast RAM size : %s", config_memory_fast_txt());
  BootPrint(s);
  siprintf(s, "Floppy drives : %u", config.floppy.drives + 1);
  BootPrint(s);
  siprintf(s, "Floppy speed  : %s", config.floppy.speed ? "fast": "normal");
  BootPrint(s);
  BootPrint("");
  siprintf(s, "\nA600 IDE HDC is %s/%s.", config.enable_ide[0] ? "enabled" : "disabled", config.enable_ide[1] ? "enabled" : "disabled");
  BootPrint(s);

  for (int i = 0; i < ARRAY_SIZE(config.hardfile); i++) {
    siprintf(s, "%s %s HDD is %s.",
      (i & 0x02) ? "Secondary" : "Primary", (i & 0x01) ? "Slave" : "Master",
      config.hardfile[i].present ? config.hardfile[i].enabled ? "enabled" : "disabled" : "not present");
    BootPrint(s);
  }

  BootPrint("\nExiting bootloader...");

  ConfigMemory(config.memory);
  ConfigCPU(config.cpu);
  ConfigAutofire(config.autofire);

  {
    ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
    ConfigChipset(config.chipset);
    ConfigFloppy(config.floppy.drives, config.floppy.speed);
    ConfigFeatures(config.features.audiofiltermode, config.features.powerledoffstate);

    if (reloadkickstart) {
      debugf("Reloading Kickstart ...");
      WaitTimer(250);
      EnableOsd();
      SPI(OSD_CMD_RST);
      rstval |= (SPI_RST_CPU | SPI_CPU_HLT); // reset #3
      SPI(rstval);
      DisableOsd();
      delay_usec(50);
      UploadActionReplay();
      if (!UploadKickstart(config.kickstart)) {
        strcpy(config.kickstart, "KICK.ROM");
        if (!UploadKickstart(config.kickstart)) {
          FatalError(ERROR_KICKSTART_UPLOAD);
        }
      }
    }

    debugf("Resetting ...");
    EnableOsd();
    SPI(OSD_CMD_RST);
    rstval |= (SPI_RST_USR | SPI_RST_CPU); // reset #4
    SPI(rstval);
    DisableOsd();
    delay_usec(50);
    EnableOsd();
    SPI(OSD_CMD_RST);
    rstval = 0; // 68K CPU ready to go
    SPI(rstval);
    DisableOsd();
  }
}

//// SaveConfiguration() ////
bool SaveConfiguration(const char *filename)
{
  if (!filename) {
    // use slot-based filename if none provided
    filename = configfilename;
  }

  const ini_cfg_t config_ini = {
    .filename = filename,
    .nsections = ARRAY_SIZE(config_ini_sections),
    .sections = config_ini_sections,
    .nvars = ARRAY_SIZE(config_ini_vars),
    .vars = config_ini_vars,
  };

  memcpy((void*)&tmpconf, (void*)&config, sizeof(config));
  return ini_save(&config_ini, 0);
}

void minimig_eject_all() {
  for (int i=0; i<drives; i++) {
    df[i].status = 0;
  }
  config.hardfile[0].present = 0;
  config.hardfile[1].present = 0;
}
