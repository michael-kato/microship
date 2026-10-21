#ifndef NANOVA_PAUSE_MENU_H
#define NANOVA_PAUSE_MENU_H

#include "player_settings.h"
struct Window;
enum PauseAction { PAUSE_ERROR,PAUSE_RESUME,PAUSE_APPLY,PAUSE_MAIN_MENU,PAUSE_QUIT };
// The modal menu stops application work and consumes input until it closes.
PauseAction pause_menu(Window*,PlayerSettings* draft,const PlayerSettings* defaults,
    int* page,const char* status);

#endif
