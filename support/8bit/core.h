#ifndef GENERIC_8BIT_CORE_H
#define GENERIC_8BIT_CORE_H

#include <user_io_core.h>

#define CONF_TBL_MAX 64

extern char core_name[16 + 1];
extern uint32_t core_features;
extern int conf_items;

void user_io_send_core_mod();
void user_io_set_core_mod(int64_t);
uint32_t user_io_get_core_features();
char user_io_is_8bit_with_config_string();
uint64_t user_io_8bit_set_status(uint64_t value, uint64_t mask);
bool user_io_create_config_name(char *s, const char *ext, uint8_t flags);
uint8_t user_io_ext_idx(const char *, const char *);
char *user_io_8bit_get_string(uint8_t);
const char *user_io_get_core_name();

// core iface
extern const user_io_core_t generic_core;

#endif // GENERIC_8BIT_CORE_H
