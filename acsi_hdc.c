#include <string.h>
#include <stdlib.h>

#include "acsi_hdc.h"
#include "utils.h"
#include "debug.h"

/* ACSI device
 * Based on ACSI code of the Hatari emulator
 */
ALIGNED(4) SCSI_CTRLR AcsiBus;

#define HDC_ReadInt16(a, i) (((unsigned) a[i] << 8)  | a[i + 1])
#define HDC_ReadInt24(a, i) (((unsigned) a[i] << 16) | ((unsigned) a[i + 1] << 8) | a[i + 2])
#define HDC_ReadInt32(a, i) (((unsigned) a[i] << 24) | ((unsigned) a[i + 1] << 16) | ((unsigned) a[i + 2] << 8) | a[i + 3])

/* Our dummy INQUIRY response data */
ALIGNED(4) static unsigned char inquiry_bytes[] =
{
    0,                /* Direct Access Device */
    0,                /* Removable: No */
    2,                /* SCSI-2 compatible */
    2,                /* Response data format */
    31,               /* length of the following data */
    0, 0, 0,          /* Vendor specific data */
    'M','I','S','T',' ',' ',' ',' ',  /* Vendor ID */
    'A','C','S','I',' ','D','i','s','k',' ',' ',' ',' ',' ',' ',' ', /* Product ID */
    '0','1','0','0',  /* Revision */
};

/**
 * Return the LUN (logical unit number) specified in the current
 * ACSI/SCSI command block.
 */
static inline unsigned char HDC_GetLUN(SCSI_CTRLR *ctr)
{
    if ((ctr->opcode >> 5) == 0)
    {
        return (ctr->command[1] & 0xE0) >> 5;
    }

    return 0;
}

/**
 * Return the start sector (logical block address)
 * specified in the current ACSI/SCSI command block.
 */
FORCE_ARM static inline unsigned long HDC_GetLBA(SCSI_CTRLR *ctr)
{
    uint8_t group = (ctr->opcode >> 5);

    if (group == 1 || group == 2 || group == 5)
    {
        // 10/12-bytes: LBA 32-bit
        return (unsigned long) HDC_ReadInt32(ctr->command, 2);
    }
    else if (group == 4)
    {
        // 16-bytes (0x80-0x9F)
        return (unsigned long) HDC_ReadInt32(ctr->command, 6);
    }

    // 6-bytes: LBA 21-bit
    return HDC_ReadInt24(ctr->command, 1) & 0x1FFFFF;
}

/**
 * Return the count specified in the current ACSI command block.
 */
FORCE_ARM static inline int HDC_GetCount(SCSI_CTRLR *ctr)
{
    uint8_t group = (ctr->opcode >> 5);

    if (group == 0)
    {
        // 6-bytes
        int count = ctr->command[4];
        if (count == 0 && (ctr->opcode == HD_READ_SECTOR || ctr->opcode == HD_WRITE_SECTOR))
            return 256;
        return count;
    }
    else if (group == 1 || group == 2)
    {
        // 10-bytes
        return HDC_ReadInt16(ctr->command, 7);
    }
    else if (group == 5)
    {
        // 12-bytes
        return (int) HDC_ReadInt32(ctr->command, 6);
    }
    else if (group == 4)
    {
        // 16-bytes
        return (int) HDC_ReadInt32(ctr->command, 10);
    }

    return ctr->command[4];
}

/**
 * Get pointer to response buffer
 */
static inline uint8_t *HDC_PrepRespBuf(SCSI_CTRLR *ctr, int size)
{
    ctr->data_len = size;
    ctr->buffer[size] = 0;

    return ctr->buffer;
}

/**
 * Return number of bytes for a command block.
 */
FORCE_ARM static int HDC_GetCommandByteCount(SCSI_CTRLR *ctr)
{
    switch (ctr->opcode >> 5)
    {
        case 0: // 0x00-0x1F
            return 6;

        case 1: // 0x20-0x3F
        case 2: // 0x40-0x5F
            return 10;

        case 4: // 0x80-0x9F
            return 16;

        default:
            return 12;
    }
}

/**
 * Get info string for ACSI command packets.
 */
#ifdef TOS_DEBUG
const char *HDC_CmdInfoStr(SCSI_CTRLR *ctr)
{
    static char buf[64];
    SCSI_DEV *dev = &ctr->devs[ctr->target];

    sniprintf(buf, sizeof(buf), "opcode=0x%x, target=%i, lun=%i, count=%d, max_lba=%lu",
        ctr->opcode, ctr->target, HDC_GetLUN(ctr), HDC_GetCount(ctr), dev->hdSize - 1);

    return buf;
}
#endif

/**
 * Inquiry - return some disk information.
 */
static void HDC_Cmd_Inquiry(SCSI_CTRLR *ctr)
{
    SCSI_DEV *dev = &ctr->devs[ctr->target];
    int count = HDC_GetCount(ctr);
    uint8_t *buf;

    dev->bSetLastBlockAddr = false;

    tos_debugf("ACSI: Inquiry: %s", HDC_CmdInfoStr(ctr));

    if (count > ctr->buffer_size)
    {
        ctr->status = HD_STATUS_ERROR;
        dev->nLastError = HD_REQSENS_INVARG;
        return;
    }

    buf = HDC_PrepRespBuf(ctr, count);

    if (count > (int)sizeof(inquiry_bytes))
        memset(&buf[sizeof(inquiry_bytes)], 0, count - sizeof(inquiry_bytes));

    memcpy(buf, inquiry_bytes, sizeof(inquiry_bytes));
    count = MIN(sizeof(inquiry_bytes), count);

    /* For unsupported LUNs set the Peripheral Qualifier and the
     * Peripheral Device Type according to the SCSI standard */
    buf[0] = HDC_GetLUN(ctr) == 0 ? 0 : 0x7F;

    buf[2] = 2; /* SCSI-2 */
    buf[4] = sizeof(inquiry_bytes) - 5;

    if (dev->dma_write)
    {
        ctr->status = HD_STATUS_OK;
        dev->nLastError = HD_REQSENS_OK;
        dev->dma_write(buf, count);
    }
    else
    {
        ctr->status = HD_STATUS_ERROR;
        dev->nLastError = HD_REQSENS_NOTREADY;
    }
}

/**
 * Format drive.
 */
static void HDC_Cmd_FormatDrive(SCSI_CTRLR *ctr)
{
    SCSI_DEV *dev = &ctr->devs[ctr->target];

    tos_debugf("ACSI: Format Drive: %s", HDC_CmdInfoStr(ctr));

    /* Should erase the whole image file here... */

    ctr->status = HD_STATUS_OK;
    dev->nLastError = HD_REQSENS_OK;

    dev->bSetLastBlockAddr = false;
}

/**
 * Report LUNs (SCSI-3).
 */
static void HDC_Cmd_ReportLuns(SCSI_CTRLR *ctr)
{
    SCSI_DEV *dev = &ctr->devs[ctr->target];
    int count = HDC_GetCount(ctr);
    uint8_t *buf;

    tos_debugf("ACSI: Report LUNs: %s", HDC_CmdInfoStr(ctr));

    buf = HDC_PrepRespBuf(ctr, count);

    /* LUN list length, 8 bytes per LUN */
    buf[0] = 0;
    buf[1] = 0;
    buf[2] = 0;
    buf[3] = 8;
    memset(&buf[4], 0, 12);

    if (dev->dma_write)
    {
        ctr->status = HD_STATUS_OK;
        dev->nLastError = HD_REQSENS_OK;
        dev->dma_write(buf, ctr->data_len);
    }
    else
    {
        ctr->status = HD_STATUS_ERROR;
        dev->nLastError = HD_REQSENS_NOTREADY;
    }

    dev->bSetLastBlockAddr = false;
}

/**
 * Read capacity of our disk.
 */
static void HDC_Cmd_ReadCapacity(SCSI_CTRLR *ctr)
{
    SCSI_DEV *dev = &ctr->devs[ctr->target];
    unsigned long nSectors = dev->hdSize > 0 ? dev->hdSize - 1 : 0;
    uint8_t *buf;

    tos_debugf("ACSI: Read Capacity: %s", HDC_CmdInfoStr(ctr));

    buf = HDC_PrepRespBuf(ctr, 8);

    buf[0] = (nSectors >> 24) & 0xFF;
    buf[1] = (nSectors >> 16) & 0xFF;
    buf[2] = (nSectors >> 8) & 0xFF;
    buf[3] = nSectors & 0xFF;
    buf[4] = (dev->blockSize >> 24) & 0xFF;
    buf[5] = (dev->blockSize >> 16) & 0xFF;
    buf[6] = (dev->blockSize >> 8) & 0xFF;
    buf[7] = dev->blockSize & 0xFF;

    if (dev->dma_write && dev->hdSize > 0)
    {
        ctr->status = HD_STATUS_OK;
        dev->nLastError = HD_REQSENS_OK;
        dev->dma_write(buf, 8);
    }
    else
    {
        ctr->status = HD_STATUS_ERROR;
        dev->nLastError = HD_REQSENS_NOTREADY;
    }

    dev->bSetLastBlockAddr = false;
}

/**
 * Test unit ready
 */
static inline void HDC_Cmd_TestUnitReady(SCSI_CTRLR *ctr)
{
    SCSI_DEV *dev = &ctr->devs[ctr->target];

    tos_debugf("ACSI: Test Unit Ready: %s", HDC_CmdInfoStr(ctr));

    if (dev->hdSize > 0)
    {
        ctr->status = HD_STATUS_OK;
        dev->nLastError = HD_REQSENS_OK;
    }
    else
    {
        ctr->status = HD_STATUS_ERROR;
        dev->nLastError = HD_REQSENS_NOTREADY;
    }

    dev->bSetLastBlockAddr = false;
}

/**
 * Request sense - return some disk information
 */
static void HDC_Cmd_RequestSense(SCSI_CTRLR *ctr)
{
    SCSI_DEV *dev = &ctr->devs[ctr->target];
    int nRetLen = HDC_GetCount(ctr);

    tos_debugf("ACSI: Request Sense: %s", HDC_CmdInfoStr(ctr));

    if (nRetLen == 0) nRetLen = 18; // 4 for SCSI-1
    if (nRetLen > 22) nRetLen = 22;

    uint8_t *retbuf = HDC_PrepRespBuf(ctr, nRetLen);
    memset(retbuf, 0, nRetLen);

    if (nRetLen <= 4)
    {
        retbuf[0] = dev->nLastError;
        if (dev->bSetLastBlockAddr)
        {
            retbuf[0] |= 0x80;
            retbuf[1] = (dev->nLastBlockAddr >> 16) & 0x1F;
            retbuf[2] = dev->nLastBlockAddr >> 8;
            retbuf[3] = dev->nLastBlockAddr;
        }
    }
    else
    {
        retbuf[0] = 0x70;

        if (dev->bSetLastBlockAddr)
        {
            retbuf[0] |= 0x80;
            retbuf[3] = dev->nLastBlockAddr >> 24;
            retbuf[4] = dev->nLastBlockAddr >> 16;
            retbuf[5] = dev->nLastBlockAddr >> 8;
            retbuf[6] = dev->nLastBlockAddr;
        }

        switch (dev->nLastError)
        {
            case HD_REQSENS_OK:         retbuf[2] = 0; break;
            case HD_REQSENS_NOTREADY:   retbuf[2] = 2; break;
            case HD_REQSENS_NOSECTOR:
            case HD_REQSENS_WRITEERR:   retbuf[2] = 3; break;
            default:                    retbuf[2] = 5; break;
        }

        if (nRetLen > 7)
            retbuf[7] = nRetLen - 8;

        if (nRetLen > 12)
            retbuf[12] = dev->nLastError;

        if (nRetLen > 21) {
            retbuf[19] = dev->nLastBlockAddr >> 16;
            retbuf[20] = dev->nLastBlockAddr >> 8;
            retbuf[21] = dev->nLastBlockAddr;
        }
    }

    if (dev->dma_write && nRetLen > 0)
    {
        ctr->status = HD_STATUS_OK;
        dev->dma_write(retbuf, nRetLen);
        dev->nLastError = HD_REQSENS_OK;
        dev->bSetLastBlockAddr = false;
    }
    else
    {
        ctr->status = HD_STATUS_ERROR;
    }
}

/**
 * Mode sense - Vendor specific page 00h.
 * (Just enough to make the HDX tool from AHDI 5.0 happy)
 */
static void HDC_CmdModeSense0x00(SCSI_DEV *dev, SCSI_CTRLR *ctr, uint8_t *buf)
{
    buf[0] = 0;
    buf[1] = 14;
    buf[2] = 0;
    buf[3] = 8;
    buf[4] = 0;

    buf[5] = dev->hdSize >> 16;  // Number of blocks, high
    buf[6] = dev->hdSize >> 8;   // Number of blocks, med
    buf[7] = dev->hdSize;        // Number of blocks, low

    buf[8] = 0;

    buf[9] = 0;      // Block size in bytes, high
    buf[10] = 2;     // Block size in bytes, med
    buf[11] = 0;     // Block size in bytes, low

    buf[12] = 0;
    buf[13] = 0;
    buf[14] = 0;
    buf[15] = 0;
}

/**
 * Mode sense - Rigid disk geometry page (requested by ASV).
 */
static void HDC_CmdModeSense0x04(SCSI_DEV *dev, SCSI_CTRLR *ctr, uint8_t *buf)
{
    buf[0] = 4;
    buf[1] = 22;

    buf[2] = dev->hdSize >> 23;  // Number of cylinders, high
    buf[3] = dev->hdSize >> 15;  // Number of cylinders, med
    buf[4] = dev->hdSize >> 7;   // Number of cylinders, low

    buf[5] = 128;    // Number of heads

    buf[6] = 0;
    buf[7] = 0;
    buf[8] = 0;

    buf[9] = 0;
    buf[10] = 0;
    buf[11] = 0;

    buf[12] = 0;
    buf[13] = 0;

    buf[14] = 0;
    buf[15] = 0;
    buf[16] = 0;

    buf[17] = 0;

    buf[18] = 0;

    buf[19] = 0;

    buf[20] = 0x1c; // Medium rotation rate 7200
    buf[21] = 0x20;

    buf[22] = 0;
    buf[23] = 0;
}

/**
 * Mode sense - Get parameters from disk.
 */
static void HDC_Cmd_ModeSense(SCSI_CTRLR *ctr)
{
    uint8_t *buf;
    SCSI_DEV *dev = &ctr->devs[ctr->target];
    int nRetLen = HDC_GetCount(ctr);

    tos_debugf("ACSI: Mode Sense: %s", HDC_CmdInfoStr(ctr));

    dev->bSetLastBlockAddr = false;

    // Subpages are not supported
    if (ctr->command[3]) {
        ctr->status = HD_STATUS_ERROR;
        dev->nLastError = HD_REQSENS_INVARG;
        return;
    }

    switch (ctr->command[2])
    {
        case 0x00:
            buf = HDC_PrepRespBuf(ctr, 16);
            HDC_CmdModeSense0x00(dev, ctr, buf);
            break;

        case 0x04:
            buf = HDC_PrepRespBuf(ctr, 28);
            HDC_CmdModeSense0x04(dev, ctr, buf + 4);
            buf[0] = 27;
            buf[1] = 0;
            buf[2] = 0;
            buf[3] = 0;
            break;

        case 0x3f:
            buf = HDC_PrepRespBuf(ctr, 44);
            HDC_CmdModeSense0x04(dev, ctr, buf + 4);
            HDC_CmdModeSense0x00(dev, ctr, buf + 28);
            buf[0] = 43;
            buf[1] = 0;
            buf[2] = 0;
            buf[3] = 0;
            break;

        default:
            ctr->status = HD_STATUS_ERROR;
            dev->nLastError = HD_REQSENS_INVARG;
            return;
    }

    if (dev->dma_write)
    {
        ctr->status = HD_STATUS_OK;
        dev->nLastError = HD_REQSENS_OK;
        dev->dma_write(buf, MIN(nRetLen, ctr->data_len));
    }
    else
    {
        ctr->status = HD_STATUS_ERROR;
        dev->nLastError = HD_REQSENS_NOTREADY;
    }
}

/**
 * Seek - move to a sector
 */
FORCE_ARM static void HDC_Cmd_Seek(SCSI_CTRLR *ctr)
{
    SCSI_DEV *dev = &ctr->devs[ctr->target];

    dev->nLastBlockAddr = HDC_GetLBA(ctr);
    dev->bSetLastBlockAddr = false;

    tos_debugf("ACSI: Seek: %s, LBA=%lu",
        HDC_CmdInfoStr(ctr), dev->nLastBlockAddr);

    if (dev->hdSize == 0)
    {
        ctr->status = HD_STATUS_ERROR;
        dev->nLastError = HD_REQSENS_NOTREADY;
    }
    else if (dev->nLastBlockAddr < dev->hdSize)
    {
        ctr->status = HD_STATUS_OK;
        dev->nLastError = HD_REQSENS_OK;
    }
    else
    {
        ctr->status = HD_STATUS_ERROR;
        dev->nLastError = HD_REQSENS_INVADDR;
        dev->bSetLastBlockAddr = true;
    }
}

/**
 * Read a sector off our disk - (implied seek)
 */
FORCE_ARM static void HDC_Cmd_ReadSector(SCSI_CTRLR *ctr)
{
    SCSI_DEV *dev = &ctr->devs[ctr->target];
    int count = HDC_GetCount(ctr);

    dev->nLastBlockAddr = HDC_GetLBA(ctr);
    dev->bSetLastBlockAddr = false;

    tos_debugf("ACSI: Read Sector: %s, LBA=%lu",
        HDC_CmdInfoStr(ctr), dev->nLastBlockAddr);

    if (dev->hdSize == 0)
    {
        ctr->status = HD_STATUS_ERROR;
        dev->nLastError = HD_REQSENS_NOTREADY;
    }
    else if (dev->disk_read && (dev->nLastBlockAddr + count) <= dev->hdSize)
    {
        int n = dev->disk_read(ctr->target, dev->nLastBlockAddr, count);
        if (n == count)
        {
            ctr->status = HD_STATUS_OK;
            dev->nLastError = HD_REQSENS_OK;
        }
        else
        {
            ctr->status = HD_STATUS_ERROR;
            dev->nLastError = HD_REQSENS_NOSECTOR;
            dev->bSetLastBlockAddr = true;
        }
    }
    else
    {
        ctr->status = HD_STATUS_ERROR;
        dev->nLastError = HD_REQSENS_INVADDR;
        dev->bSetLastBlockAddr = true;
    }
}

/**
 * Write a sector off our disk - (seek implied)
 */
FORCE_ARM static void HDC_Cmd_WriteSector(SCSI_CTRLR *ctr)
{
    SCSI_DEV *dev = &ctr->devs[ctr->target];
    int count = HDC_GetCount(ctr);

    dev->nLastBlockAddr = HDC_GetLBA(ctr);
    dev->bSetLastBlockAddr = false;

    tos_debugf("ACSI: Write Sector: %s, LBA=%lu",
        HDC_CmdInfoStr(ctr), dev->nLastBlockAddr);

    if (dev->hdSize == 0)
    {
        ctr->status = HD_STATUS_ERROR;
        dev->nLastError = HD_REQSENS_NOTREADY;
    }
    else if (dev->disk_write && (dev->nLastBlockAddr + count) <= dev->hdSize)
    {
        int n = dev->disk_write(ctr->target, dev->nLastBlockAddr, count);
        if (n == count)
        {
            ctr->status = HD_STATUS_OK;
            dev->nLastError = HD_REQSENS_OK;
        }
        else
        {
            ctr->status = HD_STATUS_ERROR;
            dev->nLastError = HD_REQSENS_WRITEERR;
            dev->bSetLastBlockAddr = true;
        }
    }
    else
    {
        ctr->status = HD_STATUS_ERROR;
        dev->nLastError = HD_REQSENS_INVADDR;
        dev->bSetLastBlockAddr = true;
    }
}

/**
 * Handling routine for HDC command packets.
 */
FORCE_ARM void HDC_HandleCommandPacket(SCSI_CTRLR *ctr)
{
    SCSI_DEV *dev = &ctr->devs[ctr->target];

    /* We currently only support LUN 0,
     * however INQUIRY must always be handled, see SCSI standard */
    if (HDC_GetLUN(ctr) != 0 && ctr->opcode != HD_INQUIRY)
    {
        dev->nLastError = HD_REQSENS_INVLUN;

        /* REQUEST SENSE is still handled for invalid LUNs */
        if (ctr->opcode != HD_REQ_SENSE)
        {
            ctr->status = HD_STATUS_ERROR;
            return;
        }
    }

    switch (ctr->opcode)
    {
        case HD_TEST_UNIT_RDY:
            HDC_Cmd_TestUnitReady(ctr);
            break;

        case HD_READ_CAPACITY1:
            HDC_Cmd_ReadCapacity(ctr);
            break;

        case HD_READ_SECTOR:
        case HD_READ_SECTOR1:
            HDC_Cmd_ReadSector(ctr);
            break;

        case HD_WRITE_SECTOR:
        case HD_WRITE_SECTOR1:
            HDC_Cmd_WriteSector(ctr);
            break;

        case HD_INQUIRY:
            HDC_Cmd_Inquiry(ctr);
            break;

        case HD_SEEK:
            HDC_Cmd_Seek(ctr);
            break;

        case HD_SHIP:
            tos_debugf("ACSI: Ship: %s", HDC_CmdInfoStr(ctr));
            ctr->status = HD_STATUS_OK;
            break;

        case HD_REQ_SENSE:
            HDC_Cmd_RequestSense(ctr);
            break;

        case HD_MODESELECT:
            tos_debugf("ACSI: Mode Select: %s", HDC_CmdInfoStr(ctr));
            ctr->status = HD_STATUS_OK;
            dev->nLastError = HD_REQSENS_OK;
            dev->bSetLastBlockAddr = false;
            break;

        case HD_MODESENSE:
            HDC_Cmd_ModeSense(ctr);
            break;

        case HD_FORMAT_DRIVE:
            HDC_Cmd_FormatDrive(ctr);
            break;

        case HD_REPORT_LUNS:
            HDC_Cmd_ReportLuns(ctr);
            break;

        /* as of yet unsupported commands */
        case HD_VERIFY_TRACK:
        case HD_FORMAT_TRACK:
        case HD_CORRECTION:

        default:
            tos_debugf("ACSI: Unsupported command: %s", HDC_CmdInfoStr(ctr));
            ctr->status = HD_STATUS_ERROR;
            dev->nLastError = HD_REQSENS_OPCODE;
            dev->bSetLastBlockAddr = false;
            break;
    };
}
