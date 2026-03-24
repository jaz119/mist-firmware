[![Build firmware](https://github.com/jaz119/mist-firmware/actions/workflows/test-build.yaml/badge.svg)](https://github.com/jaz119/mist-firmware/actions/workflows/test-build.yaml)

**MiST** and **SiDi128** firmware source code fork
=========================

The purpose of this fork is to get rid of outdated code and further improve it.

*Changelog*:
- Improved USB support:
    - improved Hub events handling for better peripheral compatibility;
    - MAX3421e INT pin is used to wait for interrupts instead of polling the SPI bus;
    - implemented robust timeouts for retries and NAKs;

- Smaller size, but speed optimized:
    - speed rate of SD card has been increased (slightly);
    - faster loading of FPGA cores;

- Other:
    - reduced RAM consumption, firmware size:

        | text   | data | bss   | dec    | hex   | filename     |
        | :--    | :--  | :--   | :--    | :--   | :--          |
        | 157285 | 0    | 44536 | 201821 | 3145d | firmware.elf |

    - firmware upgrading: flash pages full unlock is fixed;
    - SD card hot swap (*it may require hardware modification*);
    - disks images and ROMs can be loaded from any directories for *Archie*;
    - *memory-consuming ASIX driver is disabled by default*;
    - *incomplete PL2303 driver is disabled by default*;
    - 8BIT: used indexing for faster ROMs loading;
    - FatFs updated to 0.16 version;
    - LFN now limited to 80 chars;

- *Minimig core*:
    - disk images and ROMs can be loaded from any directories;
    - *removed legacy V1 core support*;

- *MiSTery core*:
    - redesigned menu structure (Minimig-style);
    - disk images and ROMs can be loaded from any directories;
    - *ACSI: full ICD/SCSI-2 support with hot-swappable images and Read-Only mode*;
    - *removed legacy ST core support*;
    - *removed direct SD card mode*;

Binaries can be found in [Latest Build](https://github.com/jaz119/mist-firmware/releases/tag/latest-clean).
