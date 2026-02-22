/*
 * font.h
 */

#ifndef FONT_H
#define FONT_H

extern unsigned char charfont[128][8];

void font_load();
uint8_t char_row(int c, int row);

#endif // FONT_H
