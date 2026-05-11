#include <stdio.h>
#include "hardware.h"
#include "idxfile.h"
#include "debug.h"

ALIGNED(4) IDXFile sd_image[SD_IMAGES];

void IDXIndex(IDXFile *idx, int entry) {
  // builds index to speed up hard file seek
  unsigned long time = GetRTTC();

  idx->clmt[0] = SZ_TBL;
  idx->file.cltbl = idx->clmt;

  FRESULT res = f_lseek(&(idx->file), CREATE_LINKMAP);

  if (res == FR_OK) {
    infof("Index #%d: created in %lu ms", entry, GetRTTC() - time);
    idx->valid = 1;
    return;
  }

  errorf("indexing error: %d, continuing without indices", res);
  idx->file.cltbl = 0;
}

FRESULT IDXOpen(IDXFile *idx, const char *name, char mode) {
  FRESULT res = f_open(&(idx->file), name, mode);
  if (res == FR_OK)
    idx->valid = 1;
  return res;
}

FRESULT IDXRead(IDXFile *file, unsigned char *pBuffer, uint8_t blksz) {
  UINT br;
  return f_read(&(file->file), pBuffer, 512<<blksz, &br);
}

FRESULT IDXWrite(IDXFile *file, unsigned char *pBuffer, uint8_t blksz) {
  UINT bw;
  return f_write(&(file->file), pBuffer, 512<<blksz, &bw);
}

FRESULT IDXSeek(IDXFile *idx, unsigned long lba) {
  return f_lseek(&(idx->file), (FSIZE_t) lba << 9);
}

void IDXClose(IDXFile *idx) {
  f_close(&(idx->file));
  idx->valid = 0;
}
