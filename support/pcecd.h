#ifndef PCECD_H
#define PCECD_H

typedef struct
{
    uint32_t latency;
    uint8_t state;
    uint8_t isData;
    int loaded;
    int has_status;
    char data_req;
    char can_read_next;
    char cdda_fifo_halffull;
    int index;
    int lba;
    int cnt;
    int scanOffset;
    int audioLength;
    int audioOffset;
    int CDDAStart;
    int CDDAEnd;
    int CDDAFirst;
    char CDDAMode;
    uint16_t stat;
} pcecd_t;

void pcecd_poll();

#endif // PCECD_H
