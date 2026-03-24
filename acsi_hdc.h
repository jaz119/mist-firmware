#ifndef ACSI_HDC_H
#define ACSI_HDC_H

#include <stdint.h>
#include <stdbool.h>
#include "attrs.h"

/* ACSI device
 * Based on ACSI code of the Hatari emulator
 */

/* Opcodes */
#define HD_TEST_UNIT_RDY    0x00            /* Test unit ready */
#define HD_REQ_SENSE        0x03            /* Request sense */
#define HD_FORMAT_UNIT      0x04            /* Format the whole drive */
#define HD_READ_6           0x08            /* Read sector */
#define HD_WRITE_6          0x0A            /* Write sector */
#define HD_SEEK_6           0x0B            /* Seek */
#define HD_INQUIRY          0x12            /* Inquiry */
#define HD_MODE_SELECT      0x15            /* Mode select */
#define HD_MODE_SENSE       0x1A            /* Mode sense */
#define HD_MODE_SENSE_10    0x5A            /* Mode sense */
#define HD_START_STOP       0x1B            /* Eject drive */
#define HD_RECV_DIAG        0x1C            /* Receive Diagnostic Results */
#define HD_SEND_DIAG        0x1D            /* Send Diagnostic */
#define HD_ALLOW_REMOVAL    0x1E            /* Prevent/Allow Medium Removal */
#define HD_READ_CAPACITY_10 0x25            /* Read capacity */
#define HD_READ_10          0x28            /* Read sector */
#define HD_WRITE_10         0x2A            /* Write sector */
#define HD_SEEK_10          0x2B            /* Seek */
#define HD_REPORT_LUNS      0xA0            /* Report Luns */
#define HD_READ_12          0xA8            /* Read sector */
#define HD_WRITE_12         0xAA            /* Write sector */

/* Status codes */
#define HD_STATUS_OK        0x00
#define HD_STATUS_ERROR     0x02
#define HD_STATUS_BUSY      0x08

/* Error codes for REQUEST SENSE: */
#define HD_REQSENS_OK       0x00            /* OK return status */
#define HD_REQSENS_NOSECTOR 0x01            /* No index or sector */
#define HD_REQSENS_NOTREADY 0x02            /* Drive not ready */
#define HD_REQSENS_WRITEERR 0x03            /* Write fault */
#define HD_REQSENS_OPCODE   0x20            /* Opcode not supported */
#define HD_REQSENS_INVADDR  0x21            /* Invalid block address */
#define HD_REQSENS_INVARG   0x24            /* Invalid argument */
#define HD_REQSENS_INVLUN   0x25            /* Invalid LUN */
#define HD_REQSENS_WRPROT   0x27            /* Write Protected */
#define HD_REQSENS_CHANGED  0x28            /* Medium may have changed */

/**
 * Information about a ACSI/SCSI drive
 */
typedef struct scsi_data {
    int (*disk_read)(int, uint32_t, size_t);
    int (*disk_write)(int, uint32_t, size_t);
    void (*dma_write)(const char *, size_t);
    bool is_readonly;           /* Is it read only mode? */
    bool is_changed;            /* Has image been changed? */
    bool is_locked;             /* Medium change/removal is prohibited */
    uint32_t nLastBlockAddr;    /* The specified sector number */
    bool bSetLastBlockAddr;     /* Sector number is valid */
    uint8_t nLastError;
    unsigned long hdSize;       /* Size of the hard disk in sectors */
    unsigned long blockSize;    /* Size of a sector in bytes */
} SCSI_DEV;

/**
 * Status of the ACSI/SCSI bus/controller including the current command block.
 */
typedef struct ALIGNED(4) {
    uint8_t target;
    uint8_t command[16];    /* Core DMA state buffer */
    uint8_t opcode;
    short int status;       /* Return code from the HDC operation */
    uint8_t *buffer;        /* Response buffer */
    int buffer_size;
    int data_len;
    SCSI_DEV devs[2];       /* Only a harddisk on ACSI 0/1 is supported */
} SCSI_CTRLR;

extern SCSI_CTRLR AcsiBus;

const char *HDC_CmdInfoStr(SCSI_CTRLR *);
void HDC_HandleCommandPacket(SCSI_CTRLR *);

#endif // ACSI_HDC_H
