#ifndef FDD_H
#define FDD_H

#include "FatFs/ff.h"

// floppy disk interface defs
#define CMD_RDTRK 0x01
#define CMD_WRTRK 0x02

// floppy status
#define DSK_INSERTED 0x01 /*disk is inserted*/
#define DSK_WRITABLE 0x10 /*disk is writable*/

#define MAX_TRACKS (83*2)

typedef struct
{
    FIL           file;
    uint32_t status; /*status of floppy*/
    unsigned int tracks; /*number of tracks*/
    unsigned char sector_offset; /*sector offset to handle tricky loaders*/
    unsigned char track; /*current track*/
    unsigned char track_prev; /*previous track*/
    char          name[22]; /*floppy name*/
} adfTYPE;

extern adfTYPE df[4];
extern unsigned char drives;

void UpdateFDDStatus(void);
void HandleFDD(unsigned int c1, unsigned int c2);

#endif // FDD_H
