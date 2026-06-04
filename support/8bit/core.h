#ifndef GENERIC_CORE_H
#define GENERIC_CORE_H

#include <user_io_core.h>

#define CONF_TBL_MAX 64

extern int64_t core_mod;
extern char core_name[16 + 1];
extern uint32_t core_features;
extern uint16_t conf_idx[CONF_TBL_MAX];
extern int conf_items;

void user_io_send_core_mod();
void user_io_set_core_mod(int64_t);
uint32_t user_io_get_core_features();
char *user_io_8bit_get_string(uint8_t);
char user_io_is_8bit_with_config_string();
uint64_t user_io_8bit_set_status(uint64_t value, uint64_t mask);
char user_io_create_config_name(char *s, const char *ext, uint8_t flags);
uint8_t user_io_ext_idx(const char *, const char *);
const char *user_io_get_core_name();

// core iface
extern const user_io_core_t generic_core;

#endif // GENERIC_CORE_H
