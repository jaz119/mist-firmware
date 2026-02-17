[![Build firmware](https://github.com/mist-devel/mist-firmware/actions/workflows/test-build.yaml/badge.svg)](https://github.com/mist-devel/mist-firmware/actions/workflows/test-build.yaml)

MIST Firmware source code
=========================

This is the source code of the MIST firmware.

Branch **clean** has the following non-merged differences from the original (master) branch:

- Improved USB stack:
    - INT pin is used to wait for max3421e interrupts, instead of SPI polling;
    - optimizations for HID devices, lower input latency;
    - added timeouts for reties/NAKs;

- Smaller firmware size, selective speed optimization:
    - speed rate of SD card has been increased, up to 1.9 Mb/sec;
    - faster loading of FPGA cores;

- Other:
    - minimig: kickstart ROM can be loaded from anywhere;
    - minimig: HDF files can be loaded from anywhere;
    - firmware upgrading: flash pages full unlock is fixed;
    - reduced RAM consumption, LFN now limited to 80 chars;
    - SD card hot swap (*it may require hardware modification*);
    - *disabled useless Asix and PL2303 drivers by default*;
    - 8BIT: using indexing for ROMs files;
    - FatFs updated to 0.16 version;

Binaries can be found in the [Latest Build](https://github.com/jaz119/mist-firmware/releases/tag/latest-clean).
