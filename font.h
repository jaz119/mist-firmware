/*
 * font.h
 */

#ifndef FONT_H
#define FONT_H

extern unsigned char charfont[128][8];

void font_load();
char char_row(char c, char row);

#endif // FONT_H
