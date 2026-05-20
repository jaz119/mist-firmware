[![Build firmware](https://github.com/jaz119/mist-firmware/actions/workflows/test-build.yaml/badge.svg)](https://github.com/jaz119/mist-firmware/actions/workflows/test-build.yaml)

**MiST** and **SiDi128** firmware source code fork
=========================

The purpose of this fork is to get rid of outdated code and further improve it.

*Changelog*:
- Improved USB support:
    - improved Hub events handling for better peripheral compatibility;
    - MAX3421e INT pin is used to wait for interrupts instead of polling the SPI bus;
    - HID: better compatibility for modern devices in *Report mode*;
    - implemented robust timeouts for retries and NAKs;

- Smaller size, but speed optimized:
    - faster loading of FPGA cores;

- Other:
    - reduced RAM consumption, firmware size:

        | text   | data | bss   | dec    | hex   | filename     |
        | :--    | :--  | :--   | :--    | :--   | :--          |
        | 152071 | 0    | 42988 | 195059 | 2f9f3 | firmware.elf |

    - FatFs updated to 0.16 version;
    - 8BIT: used indexing for faster ROMs loading;
    - disks images and ROMs can be loaded from any directories for *Archie*;
    - *memory-consuming ASIX driver is disabled by default*;
    - *incomplete PL2303 driver is disabled by default*;
    - LFN now limited to 80 chars;
    - SD card hot-swap;

- *Minimig core*:
    - disk images and ROMs can be loaded from any directories;
    - speed rate of SD card has been increased (slightly);
    - *removed legacy V1 core support*;

- *MiSTery core*:
    - redesigned menu structure (Minimig-style);
    - disk images and ROMs can be loaded from any directories;
    - *ACSI: full ICD/SCSI-2 support with hot-swappable images and Read-Only mode*;
    - *removed legacy ST core support*;
    - *removed direct SD card mode*;

Binaries can be found in [Latest Build](https://github.com/jaz119/mist-firmware/releases/tag/latest-build).
