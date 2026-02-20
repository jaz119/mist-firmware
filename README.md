[![Build firmware](https://github.com/mist-devel/mist-firmware/actions/workflows/test-build.yaml/badge.svg)](https://github.com/mist-devel/mist-firmware/actions/workflows/test-build.yaml)

MIST Firmware source code
=========================

This is the source code of the MIST firmware.

Branch **clean** has the following non-merged differences from the original (master) branch:

- Improved USB support:
    - INT pin of MAX3421e is used to wait for interrupts, instead of SPI polling;
    - added timeouts for reties/NAKs;

- Smaller size, but speed optimized:
    - speed rate of SD card has been increased slightly;
    - faster loading of FPGA cores;

- Other:
    - reduced RAM consumption, LFN now limited to 80 chars;
    - firmware upgrading: flash pages full unlock is fixed;
    - SD card hot swap (*it may require hardware modification*);
    - disks images and ROMs can be loaded from any directories for *Minimig*, *ST*, *Archie*;
    - *disabled useless Asix and PL2303 drivers by default*;
    - 8BIT: using indexing for ROMs files;
    - FatFs updated to 0.16 version;

Binaries can be found in the [Latest Build](https://github.com/jaz119/mist-firmware/releases/tag/latest-clean).
