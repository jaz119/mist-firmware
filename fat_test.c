#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "fat_compat.h"

FILE *fp = NULL;

void dump(unsigned char *buf) {
	for (int i = 0; i < 512; i++) {
		if (i%16 == 0) {
			printf(" ");
			for (int j = 0; j < 16; j++) {
				if (buf[i-16+j] > 32 && buf[i-16+j] < 128)
					printf("%c",buf[i-16+j]);
				else
					printf(" ");
			}
			printf("\n");
		}
		printf("%02x ", buf[i]);
	}
	printf("\n");
}

void FatalError(unsigned long error) {
	iprintf("Fatal: %lu\r", error);
	exit(error);
}

char OsdLines() {
    return 16;
}

bool usb_rtc_get_time(ctime_t) {
	return false;
}

unsigned char MMC_CheckCard() {
	// iprintf("> %s()\n", __FUNCTION__);
	return 1;
}

unsigned long MMC_GetCapacity() {
	// iprintf("! %s()\n", __FUNCTION__);
	return 0; // FIXME
}

unsigned char MMC_Read(unsigned long lba, unsigned char *pReadBuffer) {
	// iprintf("> %s(%lu)\n", __FUNCTION__, lba);
	fseek(fp, lba << 9, SEEK_SET);
	fread(pReadBuffer, 512, 1, fp);
	return(1);
}

unsigned char MMC_Write(unsigned long lba, unsigned char *pWriteBuffer) {
	// iprintf("> %s(%lu)\n", __FUNCTION__, lba);
	fseek(fp, lba << 9, SEEK_SET);
	fwrite(pWriteBuffer, 512, 1, fp);
	return(1);
}

unsigned char MMC_ReadMultiple(unsigned long lba, unsigned char *pReadBuffer, unsigned long nBlockCount) {
	// iprintf("> %s(%lu, %lu)\n", __FUNCTION__, lba, nBlockCount);
	fseek(fp, lba << 9, SEEK_SET);
	fread(pReadBuffer, 512, nBlockCount, fp);
	return(1);
}

unsigned char MMC_WriteMultiple(unsigned long lba, const unsigned char *pWriteBuffer, unsigned long nBlockCount) {
	//iprintf("! %s(%lu, %lu)\n", __FUNCTION__, lba, nBlockCount);
	return 0;
}

void ErrorMessage(const char *message, unsigned char code) {
	printf("Error: %s\n", message);
}

void BootPrint(const char *message) {
	printf("Boot: %s\n", message);
}

static char* GetDiskInfo(char* lfn, long len) {
// extracts disk number substring from file name
// if file name contains "X of Y" substring where X and Y are one or two digit number
// then the number substrings are extracted and put into the temporary buffer for further processing
// comparison is case sensitive

	short i, k;
	static char info[] = "XX/XX"; // temporary buffer
	static char template[4] = " of "; // template substring to search for
	char *ptr1, *ptr2, c;
	unsigned char cmp;

	if (len > 20) // scan only names which can't be fully displayed
	{
		for (i = (unsigned short)len - 1 - sizeof(template); i > 0; i--) // scan through the file name starting from its end
		{
			ptr1 = &lfn[i]; // current start position
			ptr2 = template;
			cmp = 0;
			for (k = 0; k < sizeof(template); k++) // scan through template
			{
				cmp |= *ptr1++ ^ *ptr2++; // compare substrings' characters one by one
				if (cmp)
					break; // stop further comparing if difference already found
			}

			if (!cmp) // match found
			{
				k = i - 1; // no need to check if k is valid since i is greater than zero

				c = lfn[k]; // get the first character to the left of the matched template substring
				if (c >= '0' && c <= '9') // check if a digit
				{
					info[1] = c; // copy to buffer
					info[0] = ' '; // clear previous character
					k--; // go to the preceding character
					if (k >= 0) // check if index is valid
					{
						c = lfn[k];
						if (c >= '0' && c <= '9') // check if a digit
							info[0] = c; // copy to buffer
					}

					k = i + sizeof(template); // get first character to the right of the mached template substring
					c = lfn[k]; // no need to check if index is valid
					if (c >= '0' && c <= '9') // check if a digit
					{
						info[3] = c; // copy to buffer
						info[4] = ' '; // clear next char
						k++; // go to the followwing character
						if (k < len) // check if index is valid
						{
							c = lfn[k];
							if (c >= '0' && c <= '9') // check if a digit
								info[4] = c; // copy to buffer
						}
						return info;
					}
				}
			}
		}
	}
	return NULL;
}

void OsdWrite(unsigned char n, char *text, unsigned char invert, unsigned char stipple)
{
	printf("%.*s\n", n, text);
}

////////////////////////////////////////////////////////////////////

void FileReadTest() {
	char *fname = "/ZAXXON.ARC";
	FIL file;
	unsigned char buf[512];
	UINT br;

	if (f_open(&file, fname, FA_READ) == FR_OK) {
		f_read(&file, buf, 512, &br);
		dump(buf);
		f_close(&file);
	} else {
		printf("Error opening %s\n", fname);
	}

}

void FileNextBlockTest() {
	char *fname = "/POOYAN.ROM";
	FIL file;
	FILE *fp;
	char buf[512];
	FSIZE_t fsize;
	DWORD clmt[99];

	clmt[0] = 99;

	if (f_open(&file, fname, FA_READ) == FR_OK) {
		file.cltbl = clmt;
		if (f_lseek(&file, CREATE_LINKMAP) != FR_OK) {
			f_close(&file);
			return;
		}
		fp = fopen("dump.bin", "w");
		fsize = f_size(&file);
		printf("File: %s, size: %lu\n", fname, fsize);
		while (fsize) {
			FileReadNextBlock(&file, buf);
			//dump(buf);
			fwrite(buf, 512, 1, fp);
			if (fsize > 512) fsize -= 512; else fsize = 0;
		}
		f_close(&file);
		fclose(fp);
	} else {
		printf("Error opening %s\n", fname);
	}
}

#define OSD_BUF_SIZE 128
char s[OSD_BUF_SIZE];

char DirEntryInfo[MAXDIRENTRIES][5]; // disk number info of dir entries
char fs_pFileExt[13] = "*";

unsigned char fs_ShowExt = 0;
unsigned char fs_Options = (SCAN_DIR | SCAN_LFN);

static void PrintDirectory(void)
{
	unsigned char i;
	unsigned long len;
	char *lfn;
	char *info;
	char *p;
	unsigned char j;

	s[32] = 0; // set temporary string length to OSD line length

	// ScrollReset();

	for (i = 0; i < OsdLines(); i++)
	{
		memset(s, ' ', 32); // clear line buffer
		if (i < nDirEntries)
		{
			lfn = DirEntries[i].fname; // long file name pointer
			DirEntryInfo[i][0] = 0; // clear disk number info buffer

			len = strlen(lfn); // get name length
			info = NULL; // no disk info

			if (!(DirEntries[i].fattrib & AM_DIR)) // if a file
			{
				if((len > 4) && !fs_ShowExt)
					if (lfn[len-4] == '.')
						len -= 4; // remove extension

				info = GetDiskInfo(lfn, len); // extract disk number info

				if (info != NULL)
					memcpy(DirEntryInfo[i], info, 5); // copy disk number info if present
			}

			if (len > 30)
				len = 30; // trim display length if longer than 30 characters

			if (i != iSelectedEntry && info != NULL)
			{ // display disk number info for not selected items
				strncpy(s + 1, lfn, 30-6); // trimmed name
				strncpy(s + 1+30-5, info, 5); // disk number
			}
			else
				strncpy(s + 1, lfn, len); // display only name

			if (DirEntries[i].fattrib & AM_DIR) // mark directory with suffix
				strcpy(&s[22], " <DIR>");
		}
		else
		{
			if (i == 0 && nDirEntries == 0) { // selected directory is empty
				if (fat_medium_present())
					strcpy(s, "          No files");
				else
					strcpy(s, "     No media detected");
			}
		}

		printf("%c", (i == iSelectedEntry) ? '*' : ' ');
		OsdWrite(len + 1, s, i == iSelectedEntry,0); // display formatted line text
	}
}

int main (int argc, char *argv[])
{
	if (argc < 2) {
		printf("Usage: %s file.img | /dev/sdX\n", argv[0]);
		return 0;
	}

	fp = fopen(argv[1], "r");
	if (!fp) {
		perror(0);
		return(-1);
	}

	if (!FindDrive())
		return 1;

	// FileReadTest();
	// FileNextBlockTest();

	// repeat scenario of MENU
	{
		ChangeDirectoryName("/");
		ScanDirectory(SCAN_INIT, fs_pFileExt, fs_Options);
		// 17 positions down
		for (int n = 0; n < 15; n++)
 			ScanDirectory(SCAN_NEXT, fs_pFileExt, fs_Options); // down
		ScanDirectory(SCAN_NEXT, fs_pFileExt, fs_Options); // down
		ScanDirectory(SCAN_NEXT, fs_pFileExt, fs_Options); // down
 		// show result
		PrintDirectory();
	}

	{
		// enter directory
		const char *selected = DirEntries[iSelectedEntry].fname;
		ChangeDirectoryName(selected);
		ScanDirectory(SCAN_INIT, fs_pFileExt, fs_Options);
		ScanDirectory(SCAN_NEXT, fs_pFileExt, fs_Options); // down
		// ScanDirectory(SCAN_PREV, fs_pFileExt, fs_Options); // up
		PrintDirectory();
	}

	{
		// return back (KEY_BACK)
		ChangeDirectoryName("..");
		if (ScanDirectory(SCAN_INIT_FIRST, fs_pFileExt, fs_Options))
			ScanDirectory(SCAN_INIT_NEXT, fs_pFileExt, fs_Options);
		else
			ScanDirectory(SCAN_INIT, fs_pFileExt, fs_Options);
		PrintDirectory();
	}

	fclose(fp);
	return(0);
}
