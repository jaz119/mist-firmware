[![Build firmware](https://github.com/jaz119/mist-firmware/actions/workflows/test-build.yaml/badge.svg)](https://github.com/jaz119/mist-firmware/actions/workflows/test-build.yaml)

**MiST** and **SiDi128** firmware source code fork
=========================

The purpose of this fork is to get rid of outdated code and further improve it.

*Changelog*:
- Improved USB support:
    - INT pin of MAX3421e is used to wait for interrupts, instead of SPI polling;
    - added timeouts for reties/NAKs;

- Smaller size, but speed optimized:
    - speed rate of SD card has been increased (slightly);
    - faster loading of FPGA cores;

- Other:
    - MiST: reduced RAM consumption, LFN now limited to 80 chars:

        | text   | data | bss   | dec    | hex   | filename     |
        | :--    | :--  | :--   | :--    | :--   | :--          |
        | 168168 | 0    | 44624 | 212792 | 33f38 | firmware.elf |

    - firmware upgrading: flash pages full unlock is fixed;
    - SD card hot swap (*it may require hardware modification*);
    - disks images and ROMs can be loaded from any directories for *Archie*;
    - *memory-consuming ASIX driver is disabled by default*;
    - *incomplete PL2303 driver is disabled by default*;
    - 8BIT: using indexing for ROMs files;
    - FatFs updated to 0.16 version;

- *Minimig core*:
    - disk images and ROMs can be loaded from any directories;
    - *removed legacy V1 core support*;

- *MiSTery core*:
    - updated in Minimig-style menu structure;
    - disk images and ROMs can be loaded from any directories;
    - *new ACSI driver with full ICD/SCSI-2 support*;
        - supports disk images hot swap;
        - supports read-only mode;
    - *removed direct SD card mode*;
    - *removed legacy ST core support*;

Binaries can be found in [Latest Build](https://github.com/jaz119/mist-firmware/releases/tag/latest-clean).
