#ifndef NANOVA_START_MENU_H
#define NANOVA_START_MENU_H
#include <stddef.h>
#include <stdint.h>
// Returns 1 after selection, 0 after close, and -1 on an asset or window error.
int start_menu(const char* path,char* world,size_t capacity,uint32_t selection);
#endif
