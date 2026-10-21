#ifndef NANOVA_PLAYER_SETTINGS_H
#define NANOVA_PLAYER_SETTINGS_H

#include "render_config.h"
#include <stddef.h>

struct PlayerSettings
{
    RenderConfig graphics;
    bool borderless,show_stats;
    bool subsurface,volume,cell_blur,tilt_shift,fog_blur,glow;
    float master_volume,effects_volume,drag_sensitivity,zoom_sensitivity;
    bool invert_zoom;
};

enum SettingPage { SETTINGS_DISPLAY,SETTINGS_LIGHTING,SETTINGS_VOLUME,
    SETTINGS_EFFECTS,SETTINGS_AUDIO,SETTINGS_CONTROLS,SETTINGS_PAGE_COUNT };
enum SettingType { SETTING_BOOL,SETTING_UINT,SETTING_FLOAT };
struct SettingField
{
    SettingPage page;
    const char* key;
    const char* label;
    SettingType type;
    size_t offset;
    double minimum,maximum;
    const double* choices;
    size_t choice_count;
};

const SettingField* player_setting_fields(size_t* count);
double player_setting_value(const PlayerSettings*,const SettingField&);
void player_setting_set(PlayerSettings*,const SettingField&,double value);
const char* player_settings_error(const PlayerSettings*);
bool player_settings_defaults(PlayerSettings*,const RenderConfig*,const char* path);
bool player_settings_load(PlayerSettings*,const char* path,bool optional);
bool player_settings_save(const PlayerSettings*,const char* path);
bool player_settings_user_path(char* path,size_t capacity);
uint32_t player_settings_disabled_features(const PlayerSettings*);
const char* player_settings_page_name(SettingPage);

#endif
