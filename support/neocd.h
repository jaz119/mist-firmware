#ifndef NEOCD_H
#define NEOCD_H

typedef struct
{
    uint32_t latency;
    uint8_t status;
    uint8_t isData;
    int has_status;
    int has_command;
    char can_read_next;
    char cdda_fifo_halffull;
    int index;
    int lba;
    uint16_t sectorSize;
    int scanOffset;
    int audioLength;
    int audioOffset;
    int speed;
    uint8_t stat[10];
} neocd_t;

void neocd_poll();

#endif // NEOCD_H
