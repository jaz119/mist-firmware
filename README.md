[![Build firmware](https://github.com/mist-devel/mist-firmware/actions/workflows/test-build.yaml/badge.svg)](https://github.com/mist-devel/mist-firmware/actions/workflows/test-build.yaml)

MIST Firmware source code
=========================

This is the source code of the MIST firmware.

Branch *clean* has the following non-merged differences from the original (master) branch:

- Improved USB stack:
    - added timeouts for reties/NAKs;
    - INT pin is used to wait for max3421e interrupts, instead of SPI polling;
    - optimizations for HID devices, lower input latency;

- Smaller firmware size, selective speed optimization:
    - faster loading of FPGA cores;
    - speed rate of SD card has been increased, up to 1.9 Mb/sec;

- Other improvements:
    - minimig: kickstart ROM can be loaded from anywhere;
    - minimig: HDF files can be loaded from anywhere;
    - firmware updating: flash pages unlock is fixed;
    - reduced RAM consumption, LFN now limited to 80 chars;
    - SD card hot swap (it may require hardware modification);

Binaries can be found in the [Latest Build](https://github.com/jaz119/mist-firmware/releases/tag/latest-clean).
