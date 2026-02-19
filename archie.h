#ifndef ARCHIE_H
#define ARCHIE_H

void archie_init(void);
void archie_poll(void);
void archie_kbd(unsigned short code);
void archie_mouse(unsigned char b, char x, char y);
void archie_setup_menu(void);
void archie_eject_all();

#endif // ARCHIE_H
