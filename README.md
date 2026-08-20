[![Build firmware](https://github.com/jaz119/mist-firmware/actions/workflows/test-build.yaml/badge.svg)](https://github.com/jaz119/mist-firmware/actions/workflows/test-build.yaml)

**MiST** and **SiDi128** firmware source code fork
=========================

The purpose of this fork is to get rid of outdated code and further improve it.

*Changelog*:
- Improved USB support:
    - Improved Hub events handling for better peripheral compatibility;
    - MAX3421e INT pin is used to wait for interrupts instead of polling the SPI bus;
    - HID: better compatibility for modern devices in *Report mode*;
    - Implemented robust timeouts for retries and NAKs;
    - Network: New *USB CDC-ECM* driver;

- Smaller size, but speed optimized:
    - faster loading of FPGA cores;

- Other:
    - Reduced RAM consumption, firmware size:

        | text   | data | bss   | dec    | hex   | filename     |
        | :--    | :--  | :--   | :--    | :--   | :--          |
        | 154864 | 0    | 44848 | 199712 | 30c20 | firmware.elf |

    - FatFs updated to 0.16 version;
    - 8BIT: used indexing for faster ROMs loading;
    - Disks images and ROMs can be loaded from any directories for *Archie*;
    - Memory-consuming ASIX driver is *disabled by default*;
    - Incomplete PL2303 driver is *disabled by default*;
    - LFN now limited to 80 chars;
    - SD card hot-swap;

- *Minimig core*:
    - Disk images and ROMs can be loaded from any directories;
    - Speed rate of SD card has been increased (slightly);
    - *Removed legacy V1 core support*;

- *MiSTery core*:
    - Redesigned menu structure (Minimig-style);
    - Disk images and ROMs can be loaded from any directories;
    - *ACSI: full ICD/SCSI-2 support with hot-swappable images and Read-Only mode*;
    - *Removed legacy ST core support*;
    - *Removed direct SD card mode*;

Binaries can be found in [Latest Build](https://github.com/jaz119/mist-firmware/releases/tag/latest-build).
