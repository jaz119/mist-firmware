// this file allows to enabled and disable rs232 debugging on a detailed basis
#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

// ------------ generic debugging -----------
#if DEBUG
#define debugf(a, ...) iprintf("\033[0;37m" a "\033[0m\n", ##__VA_ARGS__)
#else
#define debugf(...)
#endif

// ------------ menu debugging -----------
#if DEBUG_MENU
#define menu_debugf(a, ...) iprintf("\033[0;37m" a "\033[0m\n", ##__VA_ARGS__)
#else
#define menu_debugf(...)
#endif

// ----------- minimig debugging -------------
#if DEBUG_HDD
#define hdd_debugf(a, ...) iprintf("\033[1;32mHDD: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define hdd_debugf(...)
#endif

#if DEBUG_FDD
#define fdd_debugf(...) iprintf(__VA_ARGS__)
#else
#define fdd_debugf(...)
#endif

// -------------- TOS debugging --------------
#ifdef DEBUG_TOS
#define tos_debugf(a, ...) iprintf("\033[1;32mTOS: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define tos_debugf(...)
#endif

// --------- 8bit debug output in blue --------
#if DEBUG_8BIT
#define bit8_debugf(a, ...) iprintf("\033[1;34m8BIT: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define bit8_debugf(...)
#endif

// ------------ Archie debugging ------------
#if DEBUG_ARCHIE
#define archie_debugf(a, ...) iprintf("\033[1;31mARCHIE: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define archie_debugf(...)
#endif

// ------------ Ethernet debugging -----------
#if DEBUG_ETH
#define eth_debug(a, ...) iprintf("\033[1;32mETH: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define eth_debug(...)
#endif

#if ERRORS_ETH
#define eth_error(a, ...) iprintf("\033[1;32mETH: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define eth_error(...)
#endif

#if INFO_ETH
#define eth_info(a, ...) iprintf("\033[1;32mETH: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define eth_info(...)
#endif

#define eth_info_wp eth_info

// ------------ usb debugging -----------
#if DEBUG_USB
#define usb_debugf(a, ...) iprintf("\033[1;33mUSB: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define usb_debugf(...)
#endif

#if DEBUG_HIDP
#define hidp_debugf(a, ...)  iprintf("\033[1;94mHIDP: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define hidp_debugf(...)
#endif

// ---- usb asix debug output in blue ---
#if DEBUG_ASIX
#define asix_debugf(a, ...) iprintf("\033[1;34mASIX: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define asix_debugf(...)
#endif

// ---- usb hid debug output in green ---
#if DEBUG_HID
#define hid_debugf(a, ...) iprintf("\033[1;32mHID: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define hid_debugf(...)
#endif

// -- usb mass storage debug output in purple --
#if DEBUG_STORAGE
#define storage_debugf(a, ...) iprintf("\033[1;35mSTORAGE: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define storage_debugf(...)
#endif

// --- usb rtc debug output in blue -----
#if DEBUG_RTC
#define usbrtc_debugf(a, ...) iprintf("\033[1;35mUSBRTC: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define usbrtc_debugf(...)
#endif

// -- usb pl2303 debug output in blue ---
#if DEBUG_UART
#define pl2303_debugf(a, ...) iprintf("\033[1;34mPL2303: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define pl2303_debugf(...)
#endif

// ------ ini_parser debug output -------
#if DEBUG_INIP
#define ini_parser_debugf(a, ...) iprintf("\033[1;34mINI_PARSER : " a "\033[0m\n",## __VA_ARGS__)
#else
#define ini_parser_debugf(...)
#endif

// ------- cue_parser debug output ------
#if DEBUG_CUEP
#define cue_parser_debugf(a, ...) iprintf("\033[1;34mCUE_PARSER : " a "\033[0m\n",## __VA_ARGS__)
#else
#define cue_parser_debugf(...)
#endif

// --------- pcecd debug output ---------
#if DEBUG_PCECD
#define pcecd_debugf(a, ...) iprintf("\033[1;34mPCECD : " a "\033[0m\n",## __VA_ARGS__)
#else
#define pcecd_debugf(...)
#endif

// --------- neocd debug output ---------
#if DEBUG_NEOCD
#define neocd_debugf(a, ...) iprintf("\033[1;34mNEOCD : " a "\033[0m\n",## __VA_ARGS__)
#else
#define neocd_debugf(...)
#endif

// ---------- PSX debug output ----------
#if DEBUG_PSX
#define psx_debugf(a, ...) iprintf("\033[1;34mPSX : " a "\033[0m\n",## __VA_ARGS__)
#else
#define psx_debugf(...)
#endif

// --------- SNES debug output ----------
#if DEBUG_SNES
#define snes_debugf(a, ...) iprintf("\033[1;34mSNES : " a "\033[0m\n",## __VA_ARGS__)
#else
#define snes_debugf(...)
#endif

// ---------- HDMI debug output ---------
#if DEBUG_HDMI
#define hdmi_debugf(a, ...) iprintf("\033[1;34mHDMI : " a "\033[0m",## __VA_ARGS__)
#else
#define hdmi_debugf(...)
#endif

#endif // DEBUG_H
