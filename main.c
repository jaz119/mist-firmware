/*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski
Copyright 2012 Till Harbaum

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// 2008-10-04   - porting to ARM
// 2008-10-06   - support for 4 floppy drives
// 2008-10-30   - hdd write support
// 2009-05-01   - subdirectory support
// 2009-06-26   - SDHC and FAT32 support
// 2009-08-10   - hardfile selection
// 2009-09-11   - minor changes to hardware initialization routine
// 2009-10-10   - any length fpga core file support
// 2009-11-14   - adapted floppy gap size
//              - changes to OSD labels
// 2009-12-24   - updated version number
// 2010-01-09   - changes to floppy handling
// 2010-07-28   - improved menu button handling
//              - improved FPGA configuration routines
//              - added support for OSD vsync
// 2010-08-15   - support for joystick emulation
// 2010-08-18   - clean-up

#include <stdio.h>
#include <string.h>
#include "errors.h"
#include "hardware.h"
#include "mmc.h"
#include "fat_compat.h"
#include "osd.h"
#include "fpga.h"
#include "fdd.h"
#include "hdd.h"
#include "config.h"
#include "menu.h"
#include "user_io.h"
#include "data_io.h"
#include "c64files.h"
#include "archie.h"
#include "snes.h"
#include "zx_col.h"
#include "arc_file.h"
#include "serial_sink.h"
#include "ini_parser.h"
#include "font.h"
#include "tos.h"
#include "hdd.h"
#include "usb.h"
#include "debug.h"
#include "mist_cfg.h"
#include "usbdev.h"
#include "cdc_control.h"
#include "storage_control.h"
#include "FatFs/diskio.h"
#ifdef HAVE_QSPI
#include "qspi.h"
#endif
#include "eth.h"

#ifndef _WANT_IO_LONG_LONG
#error "newlib lacks support of long long type in IO functions. Please use a toolchain that was compiled with option --enable-newlib-io-long-long."
#endif

const char version[] = {"$VER:ATA" VDATE};

unsigned char Error;

ALIGNED(4) char s[OSD_BUF_SIZE];
ALIGNED(4) DWORD clmt[99]; // fast seek cache

unsigned long storage_size = 0;

void FatalError(unsigned long error)
{
    unsigned long i;
    iprintf("Fatal error: %lu\r", error);

    while (true)
    {
        for (i = 0; i < error; i++)
        {
          DISKLED_ON;
          WaitTimer(250);

          DISKLED_OFF;
          WaitTimer(250);
        }

        WaitTimer(2000);
        MCUReset();
    }
}

void HandleFpga(void)
{
    unsigned char  c1, c2;

    EnableFpga();
    c1 = SPI(0); // cmd request and drive number
    c2 = SPI(0); // track number
    SPI(0);
    SPI(0);
    SPI(0);
    SPI(0);
    DisableFpga();

    HandleFDD(c1, c2);
    HandleHDD(c1, c2, 1);

    UpdateDriveStatus();
}

static void eject_all_media()
{
    // Floppies
    for (int i=0; i<ARRAY_SIZE(df); i++)
    {
        df[i].status = 0;
        df[i].file.obj.fs = 0;
    }

    // Indexes
    for (int i=0; i<ARRAY_SIZE(sd_image); i++)
    {
        IDXClose(&sd_image[i]);
        sd_image[i].file.obj.fs = 0;
    }

    // Hard disks
    for (int i=0; i<ARRAY_SIZE(config.hardfile); i++)
    {
        config.hardfile[i].present = 0;
        config.hardfile[i].enabled = HDF_DISABLED;
        hdf[i].type = HDF_DISABLED;
    }

    ini_file.obj.fs = 0;
    purge_dir_cache();
    f_unmount("");
}

extern void inserttestfloppy();

#ifdef USB_STORAGE
int GetUSBStorageDevices()
{
    uint32_t to = GetTimer(2000);

    // poll usb 2 seconds or until a mass storage device becomes ready
    while (!storage_devices && !CheckTimer(to))
        usb_poll();

    return storage_devices;
}
#endif

FAST int main(void)
{
    uint8_t mmc_ok = 0;
    uint32_t last_try = 0;

#ifdef __GNUC__
    __init_hardware();

    // make sure printf works over rs232
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
#endif

    DISKLED_ON;

    data_io_init();
    c64files_init();
    snes_init();
    zx_init();
    serial_sink_init();
    Timer_Init();

    USART_Init(115200);

    iprintf("\rMinimig by Dennis van Weeren");
    iprintf("\rARM Controller by Jakub Bednarski\r\r");
    iprintf("Version %s\r\r", version+5);

    spi_init();
#ifdef HAVE_QSPI
    qspi_init();
#endif

    mmc_ok = MMC_Init();

    iprintf("spi_clock: %u MHz\r", GetSPICLK());

    usb_init();

    InitADC();

#ifdef USB_STORAGE
    if (UserButton())
        USB_BOOT_VAR = (USB_BOOT_VAR == USB_BOOT_VALUE) ? 0 : USB_BOOT_VALUE;

    if (USB_BOOT_VAR == USB_BOOT_VALUE)
    {
      if (!GetUSBStorageDevices())
      {
          if (!mmc_ok)
              FatalError(ERROR_FILE_NOT_FOUND);
      } else
          fat_switch_to_usb();  // redirect file io to usb
    }
    else
    {
#endif
      if (!mmc_ok || !FindDrive())
      {
#ifdef USB_STORAGE
          if (!GetUSBStorageDevices())
              FatalError(ERROR_FILE_NOT_FOUND);

          fat_switch_to_usb();  // redirect file io to usb
#else
          FatalError(ERROR_INVALID_DATA);
#endif
      }
#ifdef USB_STORAGE
    }
#endif

    disk_ioctl(fs.pdrv, GET_SECTOR_COUNT, &storage_size);
    storage_size >>= 11;

    ChangeDirectoryName("/");

    arc_reset();

    font_load();

    eth_init();

    user_io_init();

    // tos config also contains cdc redirect settings used by minimig
    tos_config_load(-1);

    int64_t mod = -1;

    if ((USB_LOAD_VAR != USB_LOAD_VALUE) && !user_io_dip_switch1())
    {
        mod = arc_open("/CORE.ARC");
    }
    else
    {
        user_io_detect_core_type();

        if(user_io_core_type() != CORE_TYPE_UNKNOWN && !user_io_create_config_name(s, "ARC", CONFIG_ROOT))
        {
            // when loaded from USB, try to load the development ARC file
            iprintf("Load development ARC: %s\n", s);
            mod = arc_open(s);
        }
    }

    unsigned char err;

    if (mod < 0 || !strlen(arc_get_rbfname()))
    {
        err = fpga_init(NULL); // error opening default ARC, try with default RBF
    }
    else
    {
        user_io_set_core_mod(mod);
        strncpy(s, arc_get_rbfname(), sizeof(s)-5);
        strcat(s,".RBF");
        err = fpga_init(s);
    }

    if (err != ERROR_NONE)
        FatalError(err);

    usb_dev_open();

    // main loop
    while (true)
    {
        uint8_t key = 0;

        if (mmc_ok)
        {
            if (storage_size && !mmc_inserted())
            {
                mmc_ok = false;
                eject_all_media();
                storage_size = 0;

                // force menu update
                key = KEY_HOME;
            }
        }
        else if (timer_check(last_try, 1000))
        {
            DISKLED_TOGGLE;

            // trying to remount card
            if (MMC_Init()
                && disk_ioctl(fs.pdrv, GET_SECTOR_COUNT, &storage_size) == 0
                && FindDrive())
            {
                storage_size >>= 11;
                mmc_ok = fat_medium_present();

                if (storage_size && mmc_ok)
                {
                    DISKLED_OFF;

                    // apply config changes
                    mist_ini_parse();

                    // force menu update
                    key = KEY_HOME;
                }
            }

            last_try = timer_get_msec();
        }

        cdc_control_poll();
        storage_control_poll();

        user_io_poll();

        usb_poll();

        eth_poll();

        switch (user_io_core_type())
        {
            // MIST (atari) core supports the same UI as Minimig
            case CORE_TYPE_MIST:
            case CORE_TYPE_MIST2:
                if (!mmc_ok)
                    tos_eject_all();
                break;

            // call original minimig handlers if minimig core is found
            case CORE_TYPE_MINIMIG:
            case CORE_TYPE_MINIMIG2:
                if (!mmc_ok)
                    minimig_eject_all();
                HandleFpga();
                break;

            case CORE_TYPE_ARCHIE:
                if (!mmc_ok)
                    archie_eject_all();
                break;

            // 8 bit cores can also have a ui if a valid config string can be read from it
            case CORE_TYPE_8BIT:
                if (!user_io_is_8bit_with_config_string())
                    continue;
                break;

            default:
                continue;
        };

        HandleUI(key ? key : OsdGetCtrl());
    }

    return 0;
}
