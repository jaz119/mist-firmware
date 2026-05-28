/*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski

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

// 2009-10-10   - any length (any multiple of 8 bytes) fpga core file support
// 2009-12-10   - changed command header id
// 2010-04-14   - changed command header id

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "errors.h"
#include "user_io.h"
#include "hardware.h"
#include "fdd.h"
#include "config.h"
#include "boot.h"
#include "osd.h"
#include "fpga.h"
#include "tos.h"
#include "arc_file.h"
#include "mist_cfg.h"
#include "settings.h"
#include "usb/joymapping.h"

#ifndef DEFAULT_CORE_NAME
#define DEFAULT_CORE_NAME "CORE.RBF"
#endif

uint8_t rstval = 0;

extern DWORD clmt[99];

char minimig_ver_beta;
char minimig_ver_major;
char minimig_ver_minor;
char minimig_ver_minion;

#ifdef XILINX_CCLK

// single byte serialization of FPGA configuration datastream
static inline void ShiftFpga(unsigned char data)
{
    AT91_REG *ppioa_codr = AT91C_PIOA_CODR;
    AT91_REG *ppioa_sodr = AT91C_PIOA_SODR;

    // bit 0
    *ppioa_codr = XILINX_DIN | XILINX_CCLK;
    if (data & 0x80)
        *ppioa_sodr = XILINX_DIN;
    *ppioa_sodr = XILINX_CCLK;

    // bit 1
    *ppioa_codr = XILINX_DIN | XILINX_CCLK;
    if (data & 0x40)
        *ppioa_sodr = XILINX_DIN;
    *ppioa_sodr = XILINX_CCLK;

    // bit 2
    *ppioa_codr = XILINX_DIN | XILINX_CCLK;
    if (data & 0x20)
        *ppioa_sodr = XILINX_DIN;
    *ppioa_sodr = XILINX_CCLK;

    // bit 3
    *ppioa_codr = XILINX_DIN | XILINX_CCLK;
    if (data & 0x10)
        *ppioa_sodr = XILINX_DIN;
    *ppioa_sodr = XILINX_CCLK;

    // bit 4
    *ppioa_codr = XILINX_DIN | XILINX_CCLK;
    if (data & 0x08)
        *ppioa_sodr = XILINX_DIN;
    *ppioa_sodr = XILINX_CCLK;

    // bit 5
    *ppioa_codr = XILINX_DIN | XILINX_CCLK;
    if (data & 0x04)
        *ppioa_sodr = XILINX_DIN;
    *ppioa_sodr = XILINX_CCLK;

    // bit 6
    *ppioa_codr = XILINX_DIN | XILINX_CCLK;
    if (data & 0x02)
        *ppioa_sodr = XILINX_DIN;
    *ppioa_sodr = XILINX_CCLK;

    // bit 7
    *ppioa_codr = XILINX_DIN | XILINX_CCLK;
    if (data & 0x01)
        *ppioa_sodr = XILINX_DIN;
    *ppioa_sodr = XILINX_CCLK;

}

// Xilinx FPGA configuration
// was before unsigned char ConfigureFpga(void)
unsigned char ConfigureFpga(const char *name)
{
    unsigned long  t;
    unsigned long  n;
    unsigned char *ptr;
    FIL file;
    UINT br;

    // set outputs
    *AT91C_PIOA_SODR = XILINX_CCLK | XILINX_DIN | XILINX_PROG_B;
    // enable outputs
    *AT91C_PIOA_OER = XILINX_CCLK | XILINX_DIN | XILINX_PROG_B;

    // reset FGPA configuration sequence
    // specs: PROG_B pulse min 0.3 us
    t = 15;
    while (--t)
        *AT91C_PIOA_CODR = XILINX_PROG_B;

    *AT91C_PIOA_SODR = XILINX_PROG_B;

    // now wait for INIT to go high
    // specs: max 2ms
    t = 100000;
    while (!(*AT91C_PIOA_PDSR & XILINX_INIT_B))
    {
        if (--t == 0)
        {
            errorf("FPGA init is NOT high!");
            FatalError(3);
        }
    }

    iprintf("FPGA init is high\n");

    if (*AT91C_PIOA_PDSR & XILINX_DONE)
    {
        iprintf("FPGA done is high before configuration!\n");
        FatalError(3);
    }

    if(!name)
    //  name = "CORE.BIN";
        name = "X7A102T.BIN";

    // open bitstream file
    if (f_open(&file, name, FA_READ) != FR_OK)
    {
        errorf("No FPGA configuration file found!");
        FatalError(4);
    }

    iprintf("FPGA bitstream %s opened, size = %lu\n", name, (uint32_t) f_size(&file));

    // using fast seek
    clmt[0] = ARRAY_SIZE(clmt);
    file.cltbl = clmt;
    if (f_lseek(&file, CREATE_LINKMAP) != FR_OK)
        file.cltbl = 0;

    // send all bytes to FPGA in loop
    t = 0;
    n = f_size(&file) >> 3;
    ptr = sector_buffer;
    do
    {
        // read sector if 512 (64*8) bytes done
        if ((t & 0x3F) == 0)
        {
            if (f_read(&file, sector_buffer, 512, &br) != FR_OK) {
                f_close(&file);
                return(0);
            }

            ptr = sector_buffer;
        }

        // send data in packets of 8 bytes
        ShiftFpga(*ptr++);
        ShiftFpga(*ptr++);
        ShiftFpga(*ptr++);
        ShiftFpga(*ptr++);
        ShiftFpga(*ptr++);
        ShiftFpga(*ptr++);
        ShiftFpga(*ptr++);
        ShiftFpga(*ptr++);

        t++;

    }

    while (t < n);
    f_close(&file);

    // return outputs to a state suitable for user_io.c
    *AT91C_PIOA_SODR = XILINX_CCLK | XILINX_DIN | XILINX_PROG_B;

    // iprintf("FPGA bitstream loaded\n");

    // check if DONE is high
    if (*AT91C_PIOA_PDSR & XILINX_DONE)
        return(1);

    iprintf("FPGA done is NOT high!\n");
    FatalError(5);
    return 0;
}

#endif // XILINX_CCLK

#ifdef ALTERA_DCLK

static inline void ShiftFpga(unsigned int data)
{
#pragma GCC unroll 8
    for (uint32_t i = 0; i < 8; i++)
    {
        /* Dump to DATA0 and insert a positive edge pulse at the same time */
        ALTERA_DATA0_RESET;
        ALTERA_DCLK_RESET;
        if(data & 1) ALTERA_DATA0_SET;
        ALTERA_DCLK_SET;
        data >>= 1;
    }
}

// Altera FPGA configuration
unsigned char ConfigureFpga(const char *name)
{
    unsigned long i;
    unsigned char *ptr;
    FIL file;
    UINT br;

    if (!name) {
        name = DEFAULT_CORE_NAME;
    }

    // open bitstream file
    if (f_open(&file, name, FA_READ) != FR_OK) {
        errorf("No FPGA configuration file found!");
        return ERROR_BITSTREAM_OPEN;
    }

    iprintf("FPGA bitstream %s opened, size = %lu\n",
        name, (uint32_t) f_size(&file));

    // set outputs
    ALTERA_DCLK_SET;
    ALTERA_NCONFIG_SET;
    ALTERA_DATA0_SET;

    // using fast seek
    clmt[0] = ARRAY_SIZE(clmt);
    file.cltbl = clmt;
    if (f_lseek(&file, CREATE_LINKMAP) != FR_OK)
        file.cltbl = 0;

    // send all bytes to FPGA in loop
    ptr = sector_buffer;

    ALTERA_START_CONFIG

    /* Drive a transition of 0 to 1 to NCONFIG to indicate start of configuration */
    for (i = 0; i < 10; i++)
        ALTERA_NCONFIG_RESET;  // must be low for at least 500ns
    ALTERA_NCONFIG_SET;

    // now wait for NSTATUS to go high
    // specs: max 800us
    for (i = 1000000; !ALTERA_NSTATUS_STATE; )
    {
        if (--i == 0) {
            ALTERA_STOP_CONFIG
            errorf("FPGA NSTATUS is NOT high!");
            f_close(&file);
            return ERROR_UPDATE_INIT_FAILED;
        }
    }

    int fsize = f_size(&file), n = fsize >> 3;

    /* Loop through every single byte */
    for (i = 0; i < fsize; )
    {
        // read sector if SECTOR_BUFFER_SIZE bytes done
        if ((i & (SECTOR_BUFFER_SIZE-1)) == 0)
        {
            if (f_read(&file, sector_buffer, SECTOR_BUFFER_SIZE, &br) != FR_OK) {
                f_close(&file);
                return ERROR_READ_BITSTREAM_FAILED;
            }

            ptr = sector_buffer;
        }

        int bytes2copy = (i < fsize - 8) ? 8 : fsize - i;
        i += bytes2copy;

        while (bytes2copy) {
            ShiftFpga(*ptr++);
            bytes2copy--;
        }

        /* Check for error through NSTATUS for every 8KB programmed and the last byte */
        if (!(i & 8191) || (i == fsize - 1)) {
            if (!ALTERA_NSTATUS_STATE) {
                ALTERA_STOP_CONFIG

                iprintf("FPGA NSTATUS is NOT high!\n");
                f_close(&file);

                return ERROR_UPDATE_PROGRESS_FAILED;
            }
        }
    }

    ALTERA_STOP_CONFIG

    // iprintf("FPGA bitstream loaded\n");
    f_close(&file);

    // check if DONE is high
    if (!ALTERA_DONE_STATE) {
        errorf("FPGA Configuration done but contains error... CONF_DONE is LOW");
        return ERROR_UPDATE_FAILED;
    }

    /* Start initialization */
    /* Clock another extra DCLK cycles while initialization is in progress
       through internal oscillator or driving clock cycles into CLKUSR pin */
    /* These extra DCLK cycles do not initialize the device into USER MODE */
    /* It is not required to drive extra DCLK cycles at the end of configuration */
    /* The purpose of driving extra DCLK cycles here is to insert some delay
       while waiting for the initialization of the device to complete before
       checking the CONFDONE and NSTATUS signals at the end of whole
       configuration cycle */
    for (int n = 0; n < 50; n++) {
        ALTERA_DCLK_RESET;
        ALTERA_DCLK_SET;
    }

    /* Initialization end */
    if (!ALTERA_NSTATUS_STATE || !ALTERA_DONE_STATE) {
        errorf("FPGA Initialization finish but contains error: NSTATUS is %s and CONF_DONE is %s",
            ALTERA_NSTATUS_STATE ? "HIGH" : "LOW", ALTERA_DONE_STATE ? "HIGH" : "LOW");
        return ERROR_UPDATE_FAILED;
    }

    return ERROR_NONE;
}

#endif // ALTERA_DCLK

char kick1xfoundstr[] = "Kickstart v1.x found\n";
const char applymemdetectionpatchstr[] = "Applying Kickstart 1.x memory detection patch\n";

const char *kickfoundstr = NULL, *applypatchstr = NULL;

void PatchKick1xMemoryDetection()
{
    if (!strncmp(sector_buffer + 0x18, "exec 33.192 (8 Oct 1986)", 24)) {
        kick1xfoundstr[13] = '2';
        kickfoundstr = kick1xfoundstr;
        goto applypatch;
    }
    if (!strncmp(sector_buffer + 0x18, "exec 34.2 (28 Oct 1987)", 23)) {
        kick1xfoundstr[13] = '3';
        kickfoundstr = kick1xfoundstr;
        goto applypatch;
    }
    return;

applypatch:
    if ((sector_buffer[0x154] == 0x66) && (sector_buffer[0x155] == 0x78)) {
        applypatchstr = applymemdetectionpatchstr;
        sector_buffer[0x154] = 0x60;
    }
}

// SendFileV2 (for minimig_v2)
void SendFileV2(FIL* file, unsigned char* key, int keysize, int address, int size)
{
    UINT br;
    unsigned int keyidx = 0;

    debugf("File size: %dkB", size>>1);

    if (keysize) {
        // read header
        f_read(file, sector_buffer, 0xb, &br);
    }

    for (int i=0; i<size; i++) {
        f_read(file, sector_buffer, 512, &br);
        if (keysize) {
            // decrypt ROM
            for (int j=0; j<512; j++) {
                sector_buffer[j] ^= key[keyidx++];
                if(keyidx >= keysize) keyidx -= keysize;
            }
        }

        // patch kickstart 1.x to force memory detection every time the AMIGA is reset
        if (minimig_cfg.kick1x_memory_detection_patch && (i == 0 || i == 512)) {
            kickfoundstr = NULL;
            applypatchstr = NULL;
            PatchKick1xMemoryDetection();
        }

        EnableOsd();
        uint32_t addr = address + i*512;
        SPI(OSD_CMD_WR);
        delay_usec(1);
        SPI(addr&0xff); addr = addr>>8;
        SPI(addr&0xff); addr = addr>>8;
        delay_usec(1);
        SPI(addr&0xff); addr = addr>>8;
        SPI(addr&0xff); addr = addr>>8;
        for (int j=0; j<512; j=j+4) {
            delay_usec(1);
            SPI(sector_buffer[j+0]);
            SPI(sector_buffer[j+1]);
            delay_usec(1);
            SPI(sector_buffer[j+2]);
            SPI(sector_buffer[j+3]);
        }
        DisableOsd();
    }

    if (kickfoundstr) {
        debugf("%s", kickfoundstr);
    }
    if (applypatchstr) {
        debugf("%s", applypatchstr);
    }
}

unsigned char GetFPGAStatus(void)
{
    unsigned char status;

    EnableFpga();
    status = SPI(0);
    SPI(0);
    SPI(0);
    SPI(0);
    SPI(0);
    SPI(0);
    DisableFpga();

    return status;
}

unsigned char fpga_init(const char *name) {
    int loaded_from_usb = USB_LOAD_VAR;
    unsigned char ct;

    // load the global MISTCFG.INI here
    // FIXME: loading between the FPGA init and detect_core_type
    // breaks with some SD-Cards. Reason unknown.
    virtual_joystick_remap_init(false);
    settings_load(true);

    debugf("loaded_from_usb = %d", USB_LOAD_VAR == USB_LOAD_VALUE);
    uint32_t time = GetRTTC();
    USB_LOAD_VAR = 0;

    if((loaded_from_usb != USB_LOAD_VALUE) && !is_dip_switch2_on()) {
        unsigned char err = ConfigureFpga(name);
        if (err != ERROR_NONE) return err;

        time = GetRTTC() - time;
        iprintf("FPGA configured in %lu ms\n", time);
    }

    // wait max 100 msec for a valid core type
    time = GetTimer(100);
    do {
        EnableIO();
        ct = SPI(0xff);
        DisableIO();
    } while( ((ct == 0) || (ct == 0xff)) && !CheckTimer(time));

    warningf("Core Id: 0x%02x", ct);

    user_io_detect_core_type();
    user_io_init_core();
    mist_ini_parse();
    user_io_send_buttons(true);
    InitDB9();

    if (user_io_core_type() == CORE_TYPE_MINIMIG_AGA) {
        puts("Running Minimig setup");
        user_io_8bit_set_status(minimig_cfg.clock_freq << 1, 0xffffffff);
        WaitTimer(100); // delay for PLL
        EnableOsd();
        SPI(OSD_CMD_VERSION);
        minimig_ver_beta   = SPI(0xff);
        minimig_ver_major  = SPI(0xff);
        minimig_ver_minor  = SPI(0xff);
        minimig_ver_minion = SPI(0xff);
        DisableOsd();
        delay_usec(1);
        EnableOsd();
        SPI(OSD_CMD_RST);
        rstval = (SPI_RST_USR | SPI_RST_CPU | SPI_CPU_HLT); // reset #1
        SPI(rstval);
        DisableOsd();
        delay_usec(50);
        EnableOsd();
        SPI(OSD_CMD_RST);
        rstval = (SPI_RST_CPU | SPI_CPU_HLT); // reset #2
        SPI(rstval);
        DisableOsd();
        WaitTimer(100); // video sync delay
        BootInit();
        WaitTimer(250);
        char rtl_ver[45];
        siprintf(rtl_ver, "*** MINIMIG-AGA%s v%d.%d.%d for MiST ***",
            minimig_ver_beta ? " BETA" : "",
            minimig_ver_major, minimig_ver_minor, minimig_ver_minion);
        BootPrintEx(rtl_ver);
        BootPrintEx(" ");
        BootPrintEx("MINIMIG-AGA for MiST by Rok Krajnc (rok.krajnc@gmail.com)");
        BootPrintEx("Original Minimig by Dennis van Weeren");
        BootPrintEx("Updates by Jakub Bednarski, Tobias Gubener, Sascha Boing, A.M. Robinson & others");
        BootPrintEx("MiST by Till Harbaum (till@harbaum.org)");
        BootPrintEx(" ");
        BootPrintEx(" ");

        // eject all disk
        for (int n = 0; n < ARRAY_SIZE(df); n++) {
            df[n].status = 0;
        }

        config.kickstart[0] = 0;
        SetConfigurationFilename(arc_get_cfg_file_n());

        // use slot-based config filename
        LoadConfiguration(NULL, true);
    }

    if (user_io_core_type() == CORE_TYPE_MISTERY) {
        puts("Running MiSTery setup");
        tos_upload(NULL);
    }

    if (user_io_core_type() == CORE_TYPE_ARCHIE) {
        puts("Running Archimedes setup");
    }

    return ERROR_NONE;
}
