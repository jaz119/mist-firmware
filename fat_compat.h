#ifndef _FAT16_H_INCLUDED
#define _FAT16_H_INCLUDED

#include "spi.h"
#include "FatFs/ff.h"

struct PartitionEntry
{
	uint8_t  is_boot;
	uint8_t  start_chs[3];
	uint8_t  partition_type;
	uint8_t  end_chs[3];
	uint32_t start_lba;
	uint32_t size;
} __attribute__ ((packed));

struct MasterBootRecord
{
	uint8_t boot_code[446];
	struct PartitionEntry partitions[4];
	uint16_t signature;
} __attribute__ ((packed));

extern FATFS fs;
extern char fat_device;
extern struct PartitionEntry partitions[4];	// FirstBlock and LastBlock will be byteswapped as necessary
extern int partitioncount;

#define MAXDIRENTRIES 16
#define iCurrentDirectory fs.cdir

extern unsigned char nDirEntries;
extern unsigned char maxDirEntries;
extern unsigned char iSelectedEntry;
extern char cwd[FF_LFN_BUF + 1];

extern FILINFO  DirEntries[MAXDIRENTRIES];
extern unsigned char sort_table[MAXDIRENTRIES];

// global sector buffer, data for read/write actions is stored here.
// BEWARE, this buffer is also used and thus trashed by all other functions
extern unsigned char sector_buffer[SECTOR_BUFFER_SIZE];

// scanning flags
#define SCAN_INIT  0       // start search from beginning of directory
#define SCAN_NEXT  1       // find next file in directory
#define SCAN_PREV -1       // find previous file in directory
#define SCAN_NEXT_PAGE   2 // find next 8 files in directory
#define SCAN_PREV_PAGE  -2 // find previous 8 files in directory
#define SCAN_INIT_FIRST  3 // search for an entry with given cluster number
#define SCAN_INIT_NEXT   4 // search for entries higher than the first one

// options flags
#define SCAN_DIR     1 // include subdirectories
#define SCAN_LFN     2 // include long file names
#define FIND_DIR     4 // find first directory beginning with given character
#define FIND_FILE    8 // find first file entry beginning with given character
#define SCAN_SYSDIR 16 // include subdirectories with system attribute

// functions
bool FindDrive(void);
void ChangeDirectoryName(const char *name);
char ScanDirectory(unsigned long mode, char *extension, unsigned char options);

FAST const char *GetExtension(const char *fileName);
RAMFUNC FRESULT FileReadNextBlock(FIL *, void *pBuffer);

void fat_switch_to_usb(void);
char *fs_type_to_string(void);
int8_t fat_medium_present(void);
const char *get_short_name(const char *);
int8_t fat_uses_mmc(void);
void purge_dir_cache();

#endif
