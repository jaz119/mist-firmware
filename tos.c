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
#include "ikbd.h"
#include "idxfile.h"
#include "font.h"
#include "mmc.h"
#include "utils.h"
#include "FatFs/diskio.h"

extern bool eth_present;
extern char s[OSD_BUF_SIZE];

typedef struct {
  unsigned long system_ctrl;  // system control word
  char tos_img[64];
  char cart_img[64];
  char acsi_img[2][64];
  char video_adjust[2];
  char cdc_control_redirect;
  char sd_direct;
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
  unsigned char sides;
  unsigned char spt;
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

static void tos_insert_disk(char, const char *);
static void tos_select_hdd_image(char, const char *);

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

static void mist_memory_set_address(unsigned long a, unsigned char s, char rw) {
  //  iprintf("set addr = %x, %d, %d\n", a, s, rw);

  a |= rw?0x1000000:0;
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
  while(words--) {
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

static void mist_memory_write(const char *data, unsigned long words) {
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
static void tos_set_direct_hdd(char on) {
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

static void handle_acsi(unsigned char *buffer) {

  static unsigned char asc[2] = { 0,0 };
  unsigned char target = buffer[10] >> 5;
  unsigned char device = buffer[1] >> 5;
  unsigned char cmd = buffer[0];
  unsigned long lba = 256 * 256 * (buffer[1] & 0x1f) +
    256 * buffer[2] + buffer[3];
  unsigned short length = buffer[4];

  unsigned short blocklen;
  unsigned char *buf;
  unsigned short blocks;

  if(length == 0) length = 256;

  if(is_dip_switch1_on()) {
    tos_debugf("ACSI: target %d.%d, \"%s\" (%02x)", target, device, acsi_cmd_name(cmd), cmd);
    tos_debugf("ACSI: lba %lu (%lx), length %u", lba, lba, length);
  }

  // only a harddisk on ACSI 0/1 is supported
  // ACSI 0/1 is only supported if a image is loaded
  // ACSI 0 is only supported for direct IO
  if( ((target < 2) && disk_inserted[target+2]) ||
      ((target == 0) && hdd_direct)) {
    unsigned long blocks = f_size(&sd_image[target+2].file) / 512;

    // if in hdd direct mode then hdd_direct contains device sizee
    if(hdd_direct && target==0) blocks = hdd_direct;

    // only lun0 is fully supported
    switch(cmd) {
    case 0x25:
      if(device == 0) {
        bzero(sector_buffer, 512);
        sector_buffer[0] = (blocks-1) >> 24;
        sector_buffer[1] = (blocks-1) >> 16;
        sector_buffer[2] = (blocks-1) >> 8;
        sector_buffer[3] = (blocks-1) >> 0;
        sector_buffer[6] = 2;  // 512 bytes per block

        mist_memory_write(sector_buffer, 4);

        dma_ack(0x00);
        asc[target] = 0x00;
      } else {
        dma_ack(0x02);
        asc[target] = 0x25;
      }
      break;

    case 0x00: // test drive ready
    case 0x04: // format
      if(device == 0) {
        asc[target] = 0x00;
        dma_ack(0x00);
      } else {
        asc[target] = 0x25;
        dma_ack(0x02);
      }
      break;

    case 0x03: // request sense
      if(device != 0)
        asc[target] = 0x25;

      bzero(sector_buffer, 512);
      sector_buffer[7] = 0x0b;
      if(asc[target] != 0) {
        sector_buffer[2] = 0x05;
        sector_buffer[12] = asc[target];
      }
      mist_memory_write(sector_buffer, 9); // 18 bytes
      dma_ack(0x00);
      asc[target] = 0x00;
      break;

    case 0x08: // read sector
    case 0x28: // read (10)
      if(device == 0) {
        if(cmd == 0x28) {
          lba =
            256 * 256 * 256 * buffer[2] +
            256 * 256 * buffer[3] +
            256 * buffer[4] +
            buffer[5];

          length = 256 * buffer[7] + buffer[8];
          // iprintf("READ(10) %d, %d\n", lba, length);
        }

        if(lba+length <= blocks) {
          DISKLED_ON;
#ifndef SD_NO_DIRECT_MODE
          if (user_io_core_type() == CORE_TYPE_MISTERY && fat_uses_mmc()) {
            // SD-Card -> FPGA direct SPI transfer on MISTERY
            spi_speed = spi_get_speed();
            mist_spi_set_speed(spi_newspeed);
            if(hdd_direct && target == 0) {
              if(is_dip_switch1_on())
                tos_debugf("ACSI: direct read %lu", lba);
              disk_read(fs.pdrv, 0, lba, length);
            } else {
              IDXSeek(&sd_image[target+2], lba);
              f_read(&sd_image[target+2].file, 0, 512*length, &br);
            }
            mist_spi_set_speed(spi_speed);
          } else {
#endif
            while(length) {
              int blocksize = MIN(length, SECTOR_BUFFER_SIZE/512);
              if(hdd_direct && target == 0) {
                if(is_dip_switch1_on())
                  tos_debugf("ACSI: direct read %lu", lba);
                disk_read(fs.pdrv, sector_buffer, lba, blocksize);
              } else {
                IDXSeek(&sd_image[target+2], lba);
                f_read(&sd_image[target+2].file, sector_buffer, 512*blocksize, &br);
              }
              // hexdump(sector_buffer, 32, 0);
              mist_memory_write_blocks(sector_buffer, blocksize);
              length-=blocksize;
              lba+=blocksize;
            }
#ifndef SD_NO_DIRECT_MODE
          }
#endif
          DISKLED_OFF;
          dma_ack(0x00);
          asc[target] = 0x00;
        } else {
          tos_debugf("ACSI: read (%lu+%u) exceeds device limits (%lu)",
            lba, length, blocks);
          dma_ack(0x02);
          asc[target] = 0x21;
        }
      } else {
        dma_ack(0x02);
        asc[target] = 0x25;
      }
      break;

    case 0x0a: // write sector
    case 0x2a: // write (10)
      if(device == 0) {
        if(cmd == 0x2a) {
          lba =
            256 * 256 * 256 * buffer[2] +
            256 * 256 * buffer[3] +
            256 * buffer[4] +
            buffer[5];

          length = 256 * buffer[7] + buffer[8];

          // iprintf("WRITE(10) %d, %d\n", lba, length);
        }

        if(lba+length <= blocks) {
          DISKLED_ON;
          while(length) {
            UINT bw;

            blocklen = (length > SECTOR_BUFFER_SIZE/512) ? SECTOR_BUFFER_SIZE/512 : length;
            buf = sector_buffer;
            blocks = blocklen;
            while (blocks--) {
              mist_memory_read_block(buf);
              buf+=512;
            }
            if(hdd_direct && target == 0) {
              if(is_dip_switch1_on())
                tos_debugf("ACSI: direct write %lu", lba);
              disk_write(fs.pdrv, sector_buffer, lba, blocklen);
            } else {
              IDXSeek(&sd_image[target+2], lba);
              f_write(&sd_image[target+2].file, sector_buffer, blocklen*512, &bw);
            }
            lba+=blocklen;
            length-=blocklen;
          }
          DISKLED_OFF;
          dma_ack(0x00);
          asc[target] = 0x00;
        } else {
          tos_debugf("ACSI: write (%lu+%u) exceeds device limits (%lu)",
            lba, length, blocks);
          dma_ack(0x02);
          asc[target] = 0x21;
        }
      } else {
        dma_ack(0x02);
        asc[target] = 0x25;
      }
      break;

    case 0x12: // inquiry
      if(hdd_direct && target == 0) tos_debugf("ACSI: Inquiry DIRECT");
      else                          tos_debugf("ACSI: Inquiry target %d", target);
      bzero(sector_buffer, 512);
      sector_buffer[2] = 2;                                   // SCSI-2
      sector_buffer[4] = length-5;                            // len
      memcpy(sector_buffer+8,  "MIST    ", 8);                // Vendor
      memcpy(sector_buffer+16, "                ", 16);       // Clear device entry
      if(hdd_direct && target == 0) memcpy(sector_buffer+16, "SD DIRECT", 9);// Device
      else                        { memcpy(sector_buffer+16, config.acsi_img[target], strlen(config.acsi_img[target]) > 16 ? 16 : strlen(config.acsi_img[target])); }
      memcpy(sector_buffer+32, "ATA ", 4);                    // Product revision
      memcpy(sector_buffer+36, VDATE "  ", 8);                // Serial number
      if(device != 0) sector_buffer[0] = 0x7f;
      mist_memory_write(sector_buffer, length/2);
      dma_ack(0x00);
      asc[target] = 0x00;
      break;

    case 0x1a: // mode sense
      if(device == 0) {
        tos_debugf("ACSI: mode sense, blocks = %lu", blocks);
        bzero(sector_buffer, 512);
        sector_buffer[3] = 8;            // size of extent descriptor list
        sector_buffer[5] = blocks >> 16;
        sector_buffer[6] = blocks >> 8;
        sector_buffer[7] = blocks;
        sector_buffer[10] = 2;           // byte 1 of block size in bytes (512)
        mist_memory_write(sector_buffer, length/2);
        dma_ack(0x00);
        asc[target] = 0x00;
      } else {
        asc[target] = 0x25;
        dma_ack(0x02);
      }
      break;

#if 0
    case 0x1f: // ICD command?
      tos_debugf("ACSI: ICD command %s ($%02x)",
        acsi_cmd_name(buffer[1] & 0x1f), buffer[1] & 0x1f);
      asc[target] = 0x05;
      dma_ack(0x02);
      break;
#endif

    default:
      tos_debugf("ACSI: >>>>>>>>>>>> Unsupported command <<<<<<<<<<<<<<<<");
      asc[target] = 0x20;
      dma_ack(0x02);
      break;
    }
  } else {
    tos_debugf("ACSI: Request for unsupported target");

    // tell acsi state machine that io controller is done
    // but don't generate a acsi irq
    dma_nak();
  }
}

static void handle_fdc(unsigned char *buffer) {
  // extract contents
  unsigned int dma_address = 256 * 256 * buffer[0] +
    256 * buffer[1] + (buffer[2]&0xfe);
  unsigned char scnt = buffer[3];
  unsigned char fdc_cmd = buffer[4];
  unsigned char fdc_track = buffer[5];
  unsigned char fdc_sector = buffer[6];
  unsigned char fdc_data = buffer[7];
  unsigned char drv_sel = 3-((buffer[8]>>2)&3);
  unsigned char drv_side = 1-((buffer[8]>>1)&1);

  //  tos_debugf("FDC: sel %d, cmd %x", drv_sel, fdc_cmd);

  // check if a matching disk image has been inserted
  if(drv_sel && disk_inserted[drv_sel-1]) {
    // if the fdc has been asked to write protect the disks, then
    // write sector commands should never reach the oi controller

    // read/write sector command
    if((fdc_cmd & 0xc0) == 0x80) {
      // convert track/sector/side into disk offset
      unsigned int offset = drv_side;
      offset += fdc_track * fdd_image[drv_sel-1].sides;
      offset *= fdd_image[drv_sel-1].spt;
      offset += fdc_sector-1;

      if(is_dip_switch1_on()) {
        tos_debugf("FDC %s req %d sec (%c, SD:%d, T:%d, S:%d = %d) -> %p",
          (fdc_cmd & 0x10) ? "multi" : "single",
          scnt, 'A'+drv_sel-1, drv_side, fdc_track,
          fdc_sector, offset, (void *) dma_address);
      }

      while(scnt) {
        // check if requested sector is in range
        if((fdc_sector > 0) && (fdc_sector <= fdd_image[drv_sel-1].spt)) {

          DISKLED_ON;

          f_lseek(&fdd_image[drv_sel-1].file, offset * 512);

          if((fdc_cmd & 0xe0) == 0x80) {
            // read from disk ...
            f_read(&fdd_image[drv_sel-1].file, sector_buffer, 512, &br);
            // ... and copy to ram
            mist_memory_write_block(sector_buffer);
          } else {
            // read from ram ...
            mist_memory_read_block(sector_buffer);
            // ... and write to disk
            f_write(&(fdd_image[drv_sel-1].file), sector_buffer, 512, &br);
          }

          DISKLED_OFF;
        } else
          tos_debugf("sector out of range");

        scnt--;
        dma_address += 512;
        offset += 1;
        if(!(fdc_cmd & 0x10)) break; // single sector
      }
      dma_ack(0x00);
    } else if((fdc_cmd & 0xc0) == 0xc0) {
      char msg[32];

      if((fdc_cmd & 0xe0) == 0xc0) iprintf("READ ADDRESS\n");

      if((fdc_cmd & 0xf0) == 0xe0) {
        iprintf("READ TRACK %d SIDE %d\n", fdc_track, drv_side);
        siprintf(msg, "RD TRK %d S %d", fdc_track, drv_side);
        InfoMessage(msg);
      }

      if((fdc_cmd & 0xf0) == 0xf0) {
        iprintf("WRITE TRACK %d SIDE %d\n", fdc_track, drv_side);
        siprintf(msg, "WR TRK %d S %d", fdc_track, drv_side);
        InfoMessage(msg);
      }

      iprintf("scnt = %d\n", scnt);

      dma_ack(0x00);
    }
  }
}

static void mist_get_dmastate() {
  ALIGNED(4) unsigned char buffer[32];
  unsigned int dma_address;
  unsigned char scnt;

  EnableFpga();
  SPI(MIST_GET_DMASTATE);
  if (user_io_core_type() == CORE_TYPE_MIST) {
    spi_read(buffer, 32);
  } else {
    spi_read(buffer, 16);
  }
  DisableFpga();

  if (user_io_core_type() == CORE_TYPE_MIST) {
    if(is_dip_switch1_on()) {
      if(buffer[19] & 0x01) {
        dma_address = 256 * 256 * buffer[0] + 256 * buffer[1] + (buffer[2]&0xfe);
        scnt = buffer[3];
        tos_debugf("DMA: scnt %u, addr %p", scnt, (void *) dma_address);
        if(buffer[20] == 0xa5) {
          tos_debugf("DMA: fifo %d/%d %x %s",
            (buffer[21]>>4)&0x0f, buffer[21]&0x0f,
             buffer[22], (buffer[2]&1)?"OUT":"IN");
          tos_debugf("DMA stat=%x, mode=%x, fdc_irq=%d, acsi_irq=%d",
             buffer[23], buffer[24], buffer[25], buffer[26]);
        }
      }
    }
    //  check if acsi is busy
    if(buffer[19] & 0x01) {
      handle_acsi(&buffer[9]);
    }
    // check if fdc is busy
    if(buffer[8] & 0x01) {
      handle_fdc(buffer);
    }
  } else { // CORE_TYPE_MISTERY
    if(buffer[10] & 0x01) {
      spi_newspeed = SPI_MMC_CLK_VALUE;
      handle_acsi(buffer);
    }
  }
}

// color test, used to test the shifter without CPU/TOS
#define COLORS   20
#define PLANES   4

static void tos_write(char *str);

static void tos_color_test() {
  ALIGNED(4) unsigned short buffer[COLORS][PLANES];

  int y;
  for(y=0;y<13;y++) {
    int i, j;
    for(i=0;i<COLORS;i++)
      for(j=0;j<PLANES;j++)
        buffer[i][j] = ((y+i) & (1<<j))?0xffff:0x0000;

    for(i=0;i<16;i++) {
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
  int l;

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
    for(l=0;l<16;l++) {
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

static void tos_load_cartridge_mist() {
  FIL file;

  // upload cartridge
  if(config.cart_img[0] && (f_open(&file, config.cart_img, FA_READ) == FR_OK)) {
    int i;
    tos_debugf("%s:\n  size = %lu", config.cart_img, (uint32_t) f_size(&file));

    int blocks = f_size(&file) / 512;
    tos_debugf("  blocks = %d", blocks);

    DISKLED_ON;
    for(i=0;i<blocks;i++) {
      f_read(&file, sector_buffer, 512, &br);

      if(!(i&0x7f))
       mist_memory_set_address(CART_BASE_ADDRESS+512*i, 128, 0);

      mist_memory_write_block(sector_buffer);

    }
    DISKLED_OFF;

    iprintf("Cartridge %s uploaded\n", config.cart_img);
    f_close(&file);
    return;
  }

  // erase that ram area to remove any previously uploaded
  // image
  tos_debugf("Erasing cart memory");
  mist_memory_set_address(CART_BASE_ADDRESS, 128, 0);
  mist_memory_set(0xff, 64*1024/2);
  mist_memory_set_address(CART_BASE_ADDRESS+128*512, 128, 0);
  mist_memory_set(0xff, 64*1024/2);
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

  // erase that ram area to remove any previously uploaded
  // image
  tos_debugf("Erasing cart memory");
  data_io_fill_tx(0xff, 128*1024, 0x02);
}

static void tos_load_cartridge(const char *name) {
  assign_full_path(
    config.cart_img, sizeof(config.cart_img) - 1, name);

  if(user_io_core_type() == CORE_TYPE_MIST) {
    tos_load_cartridge_mist();
  } else {
    tos_load_cartridge_mistery();
  }
}

static inline char tos_cartridge_is_inserted() {
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
  ikbd_reset();
}

static void tos_upload_mist(const char *name) {
  FIL file;
  int i;

  // set video offset in fpga
  tos_set_video_adjust(0, 0);

  tos_clr();

  // do the MiST core handling
  tos_write("\x0e\x0f MIST core \x0e\x0f ");
  tos_write("Uploading TOS ... ");

  DISKLED_ON;

  // upload and verify tos image
  if(f_open(&file, config.tos_img, FA_READ) == FR_OK) {
    int i;
    ALIGNED(4) char buffer[512];
    unsigned long time;
    unsigned long tos_base = TOS_BASE_ADDRESS_192k;

    iprintf("TOS: %s\n", config.tos_img);

    if(f_size(&file) >= 256*1024)
      tos_base = TOS_BASE_ADDRESS_256k;
    else if(f_size(&file) != 192*1024)
      tos_debugf("WARNING: Unexpected TOS size!");

    int blocks = f_size(&file) / 512;
    tos_debugf("  blocks = %d", blocks);
    tos_debugf("  address = $%08lx", tos_base);

    // clear first 16k
    mist_memory_set_address(0, 16384/512, 0);
    mist_memory_set(0x00, 8192);

#if 0  // spi transfer tests
    iprintf("SPI transfer test\n");

    // draw some max power pattern on screen, round to next sector
    // size
    mist_memory_set_address(VIDEO_BASE_ADDRESS, (32000+511)/512, 0);
    mist_memory_set(0x55, 16000);

    f_read(&file, buffer, 512, &br);
    int run_ok = 0, run_fail = 0;

    while(1) {
      int j;
      ALIGNED(4) char b2[512];

      for(j=0;j<512;j++) {
        buffer[j] ^= 0x55;
        b2[j] = 0xa5;
      }

      mist_memory_set_address(0, 1, 0);
      mist_memory_set(0xaa, 256);

      mist_memory_set_address(0, 1, 0);
      //      mist_memory_write_block(buffer);
      mist_memory_write(buffer, 256);

      mist_memory_set_address(0, 1, 1);
      //      mist_memory_read_block(b2);
      mist_memory_read(b2, 256);

      char ok = 1;
      for(j=0;j<512;j++)
       if(buffer[j] != b2[j])
         ok = 0;

      if(ok) run_ok++;
      else   run_fail++;

      if(!ok) {
        hexdump(buffer, 512, 0);
        hexdump(b2, 512, 0);

        // re-read to check whether reading fails
        mist_memory_set_address(0, 1, 1);
        mist_memory_read_block(b2);
        hexdump(b2, 512, 0);

        for(;;);
      }

      if(!((run_ok + run_fail)%10))
       iprintf("ok %d, failed %d\r", run_ok, run_fail);
    }
#endif

    time = GetRTTC();
    tos_debugf("Uploading ...");

    for(i=0;i<blocks;i++) {
      f_read(&file, buffer, 512, &br);

      // copy first 8 bytes to address 0 as well
      if(i == 0) {
        mist_memory_set_address(0, 1, 0);

        // write first 4 words
        // (actually 8 words/16 bytes as the dma cannot transfer less)
        mist_memory_write(buffer, 8);
      }

      // send address every 64k (128 sectors) as dma can max transfer
      // 255 sectors at once

      // set real tos base address
      if((i & 0x7f) == 0)
        mist_memory_set_address(tos_base+i*512, 128, 0);

      mist_memory_write_block(buffer);
    }

#if 0
    // verify
    if(is_dip_switch1_on()) {
      ALIGNED(4) char b2[512];
      int j, ok;

      f_lseek(&file, 0);
      for(i=0;i<blocks;i++) {

        if(!(i&0x7f))
          mist_memory_set_address(tos_base+i*512, 128, 1);

        f_read(&file, b2, 512, &br);
        mist_memory_read_block(buffer);

        ok = -1;
        for(j=0;j<512;j++)
          if(buffer[j] != b2[j])
            if(ok < 0)
              ok = j;

        if(ok >= 0) {
          iprintf("Failed in block %d/%x (%x != %x)\n", i, ok, 0xff & buffer[ok], 0xff & b2[ok]);

          hexdump(buffer, 512, 0);
          puts("");
          hexdump(b2, 512, 0);

          // re-read to check whether read or write failed
          bzero(buffer, 512);
          mist_memory_set_address(tos_base+i*512, 1, 1);
          mist_memory_read_block(buffer);

          ok = -1;
          for(j=0;j<512;j++)
            if(buffer[j] != b2[j])
              if(ok < 0)
                ok = j;

          if(ok >= 0) {
            iprintf("Re-read failed in block %d/%x (%x != %x)\n", i, ok, 0xff & buffer[ok], 0xff & b2[ok]);
            hexdump(buffer, 512, 0);
          } else
            iprintf("Re-read ok!\n");

            for(;;);
        }
      }
      iprintf("Verify: %s\n", ok?"ok":"failed");
    }
#endif

    time = GetRTTC() - time;
    tos_debugf("TOS.IMG uploaded in %lu ms (%lu kB/s / %lu kBit/s)",
      time, (uint32_t) (f_size(&file)/(time >> 20)), (uint32_t) (8*f_size(&file)/time));
    f_close(&file);
  } else {
    tos_debugf("Unable to find tos.img");
    tos_write("Unable to find tos.img");

    DISKLED_OFF;
    return;
  }

  DISKLED_OFF;

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
      tos_write("Enabling direct SD card access via ACSI0");
    } else {
      // try to open harddisk image
      for(i=0;i<2;i++) {
        char msg[] = "Found hard disk image for ACSIX";
        msg[30] = '0'+i;
        tos_write(msg);
        tos_select_hdd_image(i, config.acsi_img[i]);
      }
    }
  }

  tos_write("Booting ... ");

  // clear sector count register -> stop DMA
  mist_memory_set_address(0,0,0);

  ikbd_reset();
}

void tos_upload(const char *name) {

  tos_debugf("Uploading TOS");

  ResetMenu();

  // put cpu into reset
  config.system_ctrl |= TOS_CONTROL_CPU_RESET;
  mist_set_control(config.system_ctrl);

  assign_full_path(
    config.tos_img, sizeof(config.tos_img) - 1, name);

  if(user_io_core_type() == CORE_TYPE_MIST) {
    tos_upload_mist(name);
  } else {
    tos_upload_mistery(name);
  }

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

  for(int i=0;i<4;i++)
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

static inline char tos_disk_is_inserted(char index) {
  return disk_inserted[index];
}

static void tos_select_hdd_image(char i, const char *name) {
  int slot = i+2;
  IDXFile *idx = &sd_image[slot & 3];

  // try to re/open harddisk image
  if (disk_inserted[slot]) {
    IDXClose(idx);
    disk_inserted[slot] = 0;
  }

  config.system_ctrl &= ~(TOS_ACSI0_ENABLE<<i);

  if(name && name[0]) {
    FRESULT res = IDXOpen(idx, name, FA_READ | FA_WRITE);
    if (res == FR_OK) {
      assign_full_path(config.acsi_img[i], sizeof(config.acsi_img[i]) - 1, name);
      iprintf("ACSI%d: %s\n", i, config.acsi_img[i]);
      IDXIndex(idx, slot);
      disk_inserted[slot] = 1;
      config.system_ctrl |= (TOS_ACSI0_ENABLE<<i);
    } else {
      iprintf("Cannot open %s file, error %d\n", name, res);
    }
  } else {
    config.acsi_img[i][0] = 0;
  }

  // update system control
  mist_set_control(config.system_ctrl);
}

static void tos_insert_disk(char i, const char *name) {
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

  // no new disk given?
  if(!name || !name[0]) return;

  if (f_open(&fdd_image[i].file, name, FA_READ | FA_WRITE) != FR_OK) {
    if (f_open(&fdd_image[i].file, name, FA_READ) == FR_OK)
      mist_set_control(config.system_ctrl | wp_bit);
    else
      return;
  }

  assign_full_path(
    fdd_image[i].name, sizeof(fdd_image[i].name) - 1, name);

  // open floppy
  tos_debugf("%c: insert", i+'A');

  // check image size and parameters

  // check if image size suggests it's a two sided disk
  if(f_size(&fdd_image[i].file) > 85*11*512)
    fdd_image[i].sides = 2;

  // try common sector/track values
  int m, s, t;
  for(m=0;m<=2;m++)  // multiplier for hd/ed disks
    for(s=9;s<=12;s++)
      for(t=78;t<=85;t++)
        if(512*(1<<m)*s*t*fdd_image[i].sides == f_size(&fdd_image[i].file))
          fdd_image[i].spt = s*(1<<m);


  if(!fdd_image[i].spt) {
    // read first sector from disk
    if(MMC_Read(0, sector_buffer)) {
      fdd_image[i].spt = sector_buffer[24] + 256 * sector_buffer[25];
      fdd_image[i].sides = sector_buffer[26] + 256 * sector_buffer[27];
    }
  }

  if(f_size(&fdd_image[i].file)) {
    disk_inserted[i] = 1;
    // restore state of write protect bit
    tos_update_sysctrl(config.system_ctrl);
    tos_debugf("%c: detected %d sides with %d sectors per track",
      i+'A', fdd_image[i].sides, fdd_image[i].spt);
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
  ikbd_reset();

  tos_update_sysctrl(config.system_ctrl |  TOS_CONTROL_CPU_RESET);  // set reset

  if(cold) {
#if 0 // clearing mem should be sifficient. But currently we upload TOS as it may be damaged
    // clear first 16k
    mist_memory_set_address(8);
    mist_memory_set(0x00, 8192-4);
#else
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
					if(user_io_core_type() == CORE_TYPE_MIST) {
						item->item = " Video adjust";
						item->newpage = 6;
					} else {
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
					if(user_io_core_type() == CORE_TYPE_MIST) {
						item->item = "";
						item->active = 0;
					} else {
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
						SelectFileNG("IMGHD ", SCAN_DIR | SCAN_LFN, tos_file_selected, 0);
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
					SelectFileNG("IMG", SCAN_DIR | SCAN_LFN, tos_file_selected, 0);
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
					if(user_io_core_type() == CORE_TYPE_MIST) {
						item->newpage = 6;
					} else {
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
