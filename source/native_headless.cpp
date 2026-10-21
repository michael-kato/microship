// Native diagnosis uses the real scene and Vulkan renderer, without a window.
#include "window.h"
#include "start_menu.h"
#include "pause_menu.h"
#include <stdio.h>
bool window_open(Window*,uint32_t,uint32_t,bool,bool)
{ fprintf(stderr,"Native diagnostic requires --headless or --smoke.\n");return false; }
bool window_poll(Window*,PointerInput*) { return false; }
void window_close(Window*) {}
void window_suspend_input(Window*,bool) {}
int start_menu(const char*,char*,size_t,uint32_t)
{ fprintf(stderr,"Native diagnostic has no menu; use --headless or --smoke.\n");return -1; }

PauseAction pause_menu(Window*,PlayerSettings*,const PlayerSettings*,int*,const char*)
{ return PAUSE_ERROR; }
