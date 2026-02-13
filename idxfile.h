#ifndef IDXFILE_H
#define IDXFILE_H

/*	Indexed file for fast random access on big files for e.g. hdd images
	Copyright (c) 2015 by Till Harbaum

	Contributed to the Minimig project, which is free software;
	you can redistribute it and/or modify it under the terms of
	the GNU General Public License as published by the Free Software Foundation;
	either version 3 of the License, or (at your option) any later version.

	Minimig is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "fat_compat.h"

#ifndef SZ_TBL
#define SZ_TBL    1024
#endif

#define SD_IMAGES 4

typedef struct
{
  FIL file;
  volatile bool valid;
  DWORD clmt[SZ_TBL];
} IDXFile;

extern IDXFile sd_image[SD_IMAGES];

// sd_image slots:
// Minimig:  0-3 - IDE
// Atari ST: 0-1 - FDD
//           2-3 - ACSI
// Archie:   0-1 - IDE
//           2-3 - FDD
// 8 bit:    0-3 - Block access

void IDXIndex(IDXFile *, int);
FRESULT IDXOpen(IDXFile *, const char *name, char mode);
FRESULT IDXRead(IDXFile *, unsigned char *, uint8_t);
FRESULT IDXWrite(IDXFile *, unsigned char *, uint8_t);
FRESULT IDXSeek(IDXFile *, unsigned long lba);
void IDXClose(IDXFile *);

#endif
