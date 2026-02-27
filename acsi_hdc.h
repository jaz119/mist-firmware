#ifndef ACSI_HDC_H
#define ACSI_HDC_H

#include <stdint.h>
#include <stdbool.h>
#include "attrs.h"

/* ACSI device
 * Based on ACSI code of the Hatari emulator v.2.6.1
 */

/* Opcodes */
/* The following are multi-sector transfers with seek implied */
#define HD_VERIFY_TRACK    0x05               /* Verify track */
#define HD_FORMAT_TRACK    0x06               /* Format track */
#define HD_READ_SECTOR     0x08               /* Read sector */
#define HD_READ_SECTOR1    0x28               /* Read sector (class 1) */
#define HD_WRITE_SECTOR    0x0A               /* Write sector */
#define HD_WRITE_SECTOR1   0x2A               /* Write sector (class 1) */

/* Other codes */
#define HD_TEST_UNIT_RDY   0x00               /* Test unit ready */
#define HD_FORMAT_DRIVE    0x04               /* Format the whole drive */
#define HD_SEEK            0x0B               /* Seek */
#define HD_CORRECTION      0x0D               /* Correction */
#define HD_INQUIRY         0x12               /* Inquiry */
#define HD_MODESELECT      0x15               /* Mode select */
#define HD_MODESENSE       0x1A               /* Mode sense */
#define HD_REQ_SENSE       0x03               /* Request sense */
#define HD_SHIP            0x1B               /* Ship drive */
#define HD_READ_CAPACITY1  0x25               /* Read capacity (class 1) */
#define HD_REPORT_LUNS     0xa0               /* Report Luns */

/* Status codes */
#define HD_STATUS_OK       0x00
#define HD_STATUS_ERROR    0x02
#define HD_STATUS_BUSY     0x08

/* Error codes for REQUEST SENSE: */
#define HD_REQSENS_OK       0x00              /* OK return status */
#define HD_REQSENS_NOSECTOR 0x01              /* No index or sector */
#define HD_REQSENS_WRITEERR 0x03              /* Write fault */
#define HD_REQSENS_OPCODE   0x20              /* Opcode not supported */
#define HD_REQSENS_INVADDR  0x21              /* Invalid block address */
#define HD_REQSENS_INVARG   0x24              /* Invalid argument */
#define HD_REQSENS_INVLUN   0x25              /* Invalid LUN */

/**
 * Information about a ACSI/SCSI drive
 */
typedef struct scsi_data {
    int (*disk_read)(int, uint32_t, size_t);
    int (*disk_write)(int, uint32_t, size_t);
    void (*dma_write)(const char *, size_t);
    uint32_t nLastBlockAddr;    /* The specified sector number */
    bool bSetLastBlockAddr;     /* Sector number is valid */
    uint8_t nLastError;
    unsigned long hdSize;       /* Size of the hard disk in sectors */
    unsigned long blockSize;    /* Size of a sector in bytes */
    int scsi_version;
} SCSI_DEV;

/**
 * Status of the ACSI/SCSI bus/controller including the current command block.
 */
typedef struct ALIGNED(4) {
    int target;
    uint8_t command[32];    /* Core DMA state buffer */
    uint8_t opcode;
    short int status;       /* Return code from the HDC operation */
    uint8_t *buffer;        /* Response buffer */
    int buffer_size;
    int data_len;
    SCSI_DEV devs[2];       /* Only a harddisk on ACSI 0/1 is supported */
} SCSI_CTRLR;

extern SCSI_CTRLR AcsiBus;

const char *HDC_CmdInfoStr(SCSI_CTRLR *);
FAST void HDC_HandleCommandPacket(SCSI_CTRLR *);

#endif // ACSI_HDC_H
