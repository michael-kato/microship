#include "player_settings.h"
#include "asset_json.h"
#include "render_features.h"
#include <cmath>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

static const double OFF_ON[] = {0, 1};
static const double WIDTHS[] = {1280, 1600, 1920, 2560, 3840};
static const double HEIGHTS[] = {720, 900, 1080, 1440, 2160};
static const double SAMPLES[] = {1, 2, 3, 4, 6, 8, 16, 32, 64};
static const double EXPOSURE[] = {.25, .5, .75, 1, 1.25, 1.6, 2, 2.5, 3, 4};
static const double FOV[] = {30, 35, 40, 45, 50, 60, 75, 90};
static const double UNIT[] = {0, .1, .2, .25, .35, .5, .65, .75, .85, 1};
static const double GRID[] = {0, 8, 16, 24, 32, 48, 64};
static const double STEPS[] = {2, 3, 4, 8, 12, 16, 24, 32, 48, 64};
static const double STRIDE[] = {2, 4};
static const double RADIUS[] = {0, .5, 1, 1.5, 2, 4, 6, 8, 12, 16, 24, 32, 48, 64};
static const double BLOOM_RADIUS[] = {1, 2, 4, 6, 8, 12, 16, 24, 32, 48, 64};
static const double THRESHOLD[] = {0, .1, .25, .5, .55, .75, 1, 1.5, 2, 4};
static const double THICKNESS[] = {.01, .04, .08, .12, .16, .24, .32, .5, 1};
static const double FOCUS_RANGE[] = {0, .01, .02, .03, .05, .1, .2};
static const double BLUR_SCALE[] = {0, .25, .5, .75, 1, 2, 4, 8, 15, 30, 45, 60, 90, 120, 200};
static const double SPEED[] = {0, 6, 12, 18, 24, 36, 48, 72, 96, 128};
static const double SENSITIVITY[] = {.25, .5, .75, 1, 1.25, 1.5, 2, 3, 4};

#define FIELD(page, key, label, type, member, lo, hi, values)                  \
	{SETTINGS_##page,                                                          \
	 key,                                                                      \
	 label,                                                                    \
	 SETTING_##type,                                                           \
	 offsetof(PlayerSettings, member),                                         \
	 lo,                                                                       \
	 hi,                                                                       \
	 values,                                                                   \
	 sizeof(values) / sizeof(values[0])}
#define GRAPHICS(page, key, label, type, member, lo, hi, values)               \
	{SETTINGS_##page,                                                          \
	 key,                                                                      \
	 label,                                                                    \
	 SETTING_##type,                                                           \
	 offsetof(PlayerSettings, graphics) + offsetof(RenderConfig, member),      \
	 lo,                                                                       \
	 hi,                                                                       \
	 values,                                                                   \
	 sizeof(values) / sizeof(values[0])}
static const SettingField FIELDS[] = {
    FIELD(DISPLAY, "borderless", "Borderless fullscreen", BOOL, borderless, 0,
	      1, OFF_ON),
    GRAPHICS(DISPLAY, "width", "Window width", UINT, width, 1, 4096, WIDTHS),
    GRAPHICS(DISPLAY, "height", "Window height", UINT, height, 1, 4096,
	         HEIGHTS),
    GRAPHICS(DISPLAY, "vsync", "VSync", BOOL, vsync, 0, 1, OFF_ON),
    GRAPHICS(DISPLAY, "samples", "Film samples per pixel", UINT,
	         samples_per_pixel, 1, 4096, SAMPLES),
    GRAPHICS(DISPLAY, "antialiasing", "Spatial anti-aliasing", BOOL,
	         spatial_antialiasing, 0, 1, OFF_ON),
    GRAPHICS(DISPLAY, "exposure", "Exposure", FLOAT, exposure, .01, 100,
	         EXPOSURE),
    GRAPHICS(DISPLAY, "field_of_view", "Vertical field of view", FLOAT,
	         vertical_fov_degrees, 2, 178, FOV),
    FIELD(DISPLAY, "show_stats", "Frame statistics", BOOL, show_stats, 0, 1,
	      OFF_ON),
    FIELD(LIGHTING, "subsurface_enabled", "Subsurface scattering", BOOL,
	      subsurface, 0, 1, OFF_ON),
    GRAPHICS(LIGHTING, "subsurface_strength", "Subsurface strength", FLOAT,
	         subsurface_strength, 0, 1, UNIT),
    GRAPHICS(LIGHTING, "subsurface_samples", "Subsurface film samples", UINT,
	         subsurface_samples, 0, 4096, SAMPLES),
    GRAPHICS(LIGHTING, "subsurface_lod_minimum", "SSS minimum radius in pixels",
	         FLOAT, detail_subsurface_minimum, 0, 4096, RADIUS),
    GRAPHICS(LIGHTING, "subsurface_lod_full", "SSS full radius in pixels",
	         FLOAT, detail_subsurface_full, 0, 4096, RADIUS),
    GRAPHICS(LIGHTING, "subsurface_wrap", "Subsurface light wrap", FLOAT,
	         subsurface_wrap, 0, 1, UNIT),
    GRAPHICS(LIGHTING, "subsurface_transmission", "Subsurface transmission",
	         FLOAT, subsurface_transmission, 0, 1, UNIT),
    GRAPHICS(LIGHTING, "membrane_thickness", "Membrane optical thickness",
	         FLOAT, subsurface_thickness, 0, 1, THICKNESS),
    GRAPHICS(LIGHTING, "shadow_grid", "Lighting grid resolution", UINT,
	         shadow_grid_resolution, 0, 64, GRID),
    GRAPHICS(LIGHTING, "static_cache", "Static lighting cache", BOOL,
	         static_lighting_cache, 0, 1, OFF_ON),
    GRAPHICS(LIGHTING, "texture_grid", "Texture lighting grid", BOOL,
	         texture_lighting_grid, 0, 1, OFF_ON),
    GRAPHICS(LIGHTING, "thickness_cache", "Static thickness cache", BOOL,
	         organelle_lighting_cache, 0, 1, OFF_ON),
    FIELD(VOLUME, "enabled", "Volume scattering", BOOL, volume, 0, 1, OFF_ON),
    GRAPHICS(VOLUME, "rays", "Volume rays per pixel", UINT,
	         volume_rays_per_pixel, 0, 4096, SAMPLES),
    GRAPHICS(VOLUME, "steps", "Integration steps", UINT, volume_steps, 2, 64,
	         STEPS),
    GRAPHICS(VOLUME, "gauss", "Gauss-Legendre integration", BOOL, volume_gauss,
	         0, 1, OFF_ON),
    GRAPHICS(VOLUME, "spatial_filter", "Spatial volume filter", BOOL,
	         volume_spatial_filter, 0, 1, OFF_ON),
    GRAPHICS(VOLUME, "spatial_stride", "Volume grid pixel stride", UINT,
	         volume_spatial_stride, 2, 4, STRIDE),
    GRAPHICS(VOLUME, "optical_lookup", "Optical depth lookup", BOOL,
	         optical_depth_lut, 0, 1, OFF_ON),
    GRAPHICS(VOLUME, "lighting_grid", "Volume lighting grid", BOOL,
	         volume_lighting_grid, 0, 1, OFF_ON),
    FIELD(EFFECTS, "depth_of_field", "Depth of field", BOOL, depth_of_field, 0,
	      1, OFF_ON),
    FIELD(EFFECTS, "fog_blur", "Fog of war blur", BOOL, fog_blur, 0, 1, OFF_ON),
    FIELD(EFFECTS, "glow", "Glow and bloom", BOOL, glow, 0, 1, OFF_ON),
    GRAPHICS(EFFECTS, "bloom_radius", "Bloom radius", UINT, bloom_radius, 1, 64,
	         BLOOM_RADIUS),
    GRAPHICS(EFFECTS, "bloom_strength", "Bloom strength", FLOAT, bloom_strength,
	         0, 1, UNIT),
    GRAPHICS(EFFECTS, "bloom_threshold", "Bloom threshold", FLOAT,
	         bloom_threshold, 0, 1000000, THRESHOLD),
    GRAPHICS(EFFECTS, "focus_range", "Sharp depth range", FLOAT, focus_range,
	         0, 1, FOCUS_RANGE),
    GRAPHICS(EFFECTS, "focus_blur_scale", "Depth blur strength", FLOAT,
	         focus_blur_scale, 0, 1000, BLUR_SCALE),
    GRAPHICS(EFFECTS, "focus_radius", "Maximum depth blur radius", FLOAT,
	         focus_radius, 0, 64, RADIUS),
    FIELD(AUDIO, "master_volume", "Master volume", FLOAT, master_volume, 0, 1,
	      UNIT),
    FIELD(AUDIO, "effects_volume", "Sound effects volume", FLOAT,
	      effects_volume, 0, 1, UNIT),
    GRAPHICS(CONTROLS, "keyboard_speed", "Keyboard camera speed", FLOAT,
	         camera_keyboard_speed, 0, 1000, SPEED),
    FIELD(CONTROLS, "drag_sensitivity", "Mouse pan sensitivity", FLOAT,
	      drag_sensitivity, .1, 10, SENSITIVITY),
    FIELD(CONTROLS, "zoom_sensitivity", "Mouse zoom sensitivity", FLOAT,
	      zoom_sensitivity, .1, 10, SENSITIVITY),
    FIELD(CONTROLS, "invert_zoom", "Invert mouse wheel zoom", BOOL, invert_zoom,
	      0, 1, OFF_ON)};
#undef FIELD
#undef GRAPHICS

const char* player_settings_page_name(SettingPage page)
{
	static const char* names[] = {"display", "lighting", "volume",
	                              "effects", "audio",    "controls"};
	return names[page];
}
const SettingField* player_setting_fields(size_t* count)
{
	*count = sizeof(FIELDS) / sizeof(FIELDS[0]);
	return FIELDS;
}
double player_setting_value(const PlayerSettings* s, const SettingField& f)
{
	const char* value = (const char*)s + f.offset;
	if (f.type == SETTING_BOOL)
		return *(const bool*)value ? 1 : 0;
	if (f.type == SETTING_UINT)
		return *(const uint32_t*)value;
	return *(const float*)value;
}
void player_setting_set(PlayerSettings* s, const SettingField& f, double value)
{
	char* output = (char*)s + f.offset;
	if (f.type == SETTING_BOOL)
		*(bool*)output = value != 0;
	else if (f.type == SETTING_UINT)
		*(uint32_t*)output = (uint32_t)value;
	else
		*(float*)output = (float)value;
}
const char* player_settings_error(const PlayerSettings* s)
{
	for (size_t i = 0; i < sizeof(FIELDS) / sizeof(FIELDS[0]); ++i)
	{
		double value = player_setting_value(s, FIELDS[i]);
		if (!std::isfinite(value) || value < FIELDS[i].minimum ||
		    value > FIELDS[i].maximum)
			return "A setting is outside its allowed range.";
	}
	if (s->graphics.subsurface_samples > s->graphics.samples_per_pixel)
		return "Subsurface film samples must not exceed film samples per "
		       "pixel.";
	if (s->graphics.volume_gauss && s->graphics.volume_steps != 2 &&
	    s->graphics.volume_steps != 3)
		return "Gauss-Legendre integration needs 2 or 3 steps. For more steps, "
		       "turn it off.";
	if (s->graphics.volume_spatial_filter &&
	    s->graphics.volume_rays_per_pixel != 1)
		return "Spatial volume filtering needs 1 volume ray. For more rays, "
		       "turn it off.";
	if (s->graphics.volume_spatial_stride != 2 &&
	    s->graphics.volume_spatial_stride != 4)
		return "Volume grid stride must be 2 or 4 pixels.";
	if (s->graphics.shadow_grid_resolution &&
	    s->graphics.shadow_grid_resolution < 8)
		return "Lighting grid resolution must be 0, or from 8 to 64.";
	return render_config_error(&s->graphics);
}
bool player_settings_load(PlayerSettings* settings, const char* path,
                          bool optional)
{
	if (!path || !*path)
		return optional;
	FILE* file = fopen(path, "rb");
	if (!file)
	{
		if (optional && errno == ENOENT)
			return true;
		fprintf(stderr, "Cannot read player settings '%s': %s.\n", path,
		        strerror(errno));
		return false;
	}
	fclose(file);
	JsonDocument document;
	if (!json_load(&document, path))
		return false;
	Json root = json_root(&document);
	json_object(
	    root, "$schema|version|display|lighting|volume|effects|audio|controls");
	json_uint(json_get(root, "version"), 1, 1);
	PlayerSettings loaded = *settings;
	for (uint32_t page = 0; page < SETTINGS_PAGE_COUNT; ++page)
	{
		Json group =
		    json_find(root, player_settings_page_name((SettingPage)page));
		if (!json_present(group))
			continue;
		char keys[1024] = {};
		size_t used = 0;
		for (size_t i = 0; i < sizeof(FIELDS) / sizeof(FIELDS[0]); ++i)
			if (FIELDS[i].page == page)
				used +=
				    (size_t)snprintf(keys + used, sizeof(keys) - used, "%s%s",
					                 used ? "|" : "", FIELDS[i].key);
		json_object(group, keys);
		for (size_t i = 0; i < sizeof(FIELDS) / sizeof(FIELDS[0]); ++i)
		{
			const SettingField& f = FIELDS[i];
			if (f.page != page)
				continue;
			Json value = json_find(group, f.key);
			if (!json_present(value))
				continue;
			double number =
			    f.type == SETTING_BOOL ? (json_bool(value) ? 1 : 0)
				: f.type == SETTING_UINT
			        ? json_uint(value, (uint32_t)f.minimum, (uint32_t)f.maximum)
			        : json_float(value, f.minimum, f.maximum);
			player_setting_set(&loaded, f, number);
		}
	}
	if (!document.valid)
		return false;
	const char* error = player_settings_error(&loaded);
	if (error)
		return json_error(root, error);
	*settings = loaded;
	return true;
}
bool player_settings_defaults(PlayerSettings* settings,
                              const RenderConfig* config, const char* path)
{
	*settings = PlayerSettings();
	settings->graphics = *config;
	return player_settings_load(settings, path, false);
}
bool player_settings_save(const PlayerSettings* settings, const char* path)
{
	if (!path || !*path || player_settings_error(settings))
		return false;
	char temporary[ASSET_PATH_CAPACITY];
#ifdef _WIN32
	unsigned long process = GetCurrentProcessId();
#else
	unsigned long process = 0;
#endif
	int length =
	    snprintf(temporary, sizeof(temporary), "%s.%lu.tmp", path, process);
	if (length <= 0 || (size_t)length >= sizeof(temporary))
		return false;
	FILE* file = fopen(temporary, "wb");
	if (!file)
		return false;
	fprintf(file, "{\n  \"version\": 1");
	for (uint32_t page = 0; page < SETTINGS_PAGE_COUNT; ++page)
	{
		fprintf(file, ",\n  \"%s\": {",
		        player_settings_page_name((SettingPage)page));
		bool first = true;
		for (size_t i = 0; i < sizeof(FIELDS) / sizeof(FIELDS[0]); ++i)
		{
			const SettingField& f = FIELDS[i];
			if (f.page != page)
				continue;
			fprintf(file, "%s\n    \"%s\": ", first ? "" : ",", f.key);
			first = false;
			double value = player_setting_value(settings, f);
			if (f.type == SETTING_BOOL)
				fprintf(file, "%s", value ? "true" : "false");
			else
				fprintf(file, "%.9g", value);
		}
		fprintf(file, "\n  }");
	}
	fprintf(file, "\n}\n");
	bool valid = !ferror(file) && fflush(file) == 0;
#ifdef _WIN32
	if (valid)
		valid = FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(file))) != 0;
#endif
	if (fclose(file))
		valid = false;
	if (valid)
	{
#ifdef _WIN32
		valid = MoveFileExA(temporary, path,
		                    MOVEFILE_REPLACE_EXISTING |
		                        MOVEFILE_WRITE_THROUGH) != 0;
#else
		valid = rename(temporary, path) == 0;
#endif
	}
	if (!valid)
	{
		remove(temporary);
		fprintf(stderr, "Cannot save player settings '%s'.\n", path);
	}
	return valid;
}
bool player_settings_user_path(char* path, size_t capacity)
{
#ifdef _WIN32
	char local[ASSET_PATH_CAPACITY];
	DWORD count = GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof(local));
	if (!count || count >= sizeof(local))
		return false;
	int length = snprintf(path, capacity, "%s\\Nanovore", local);
	if (length <= 0 || (size_t)length >= capacity)
		return false;
	if (!CreateDirectoryA(path, 0) && GetLastError() != ERROR_ALREADY_EXISTS)
		return false;
	length =
	    snprintf(path, capacity, "%s\\Nanovore\\player_settings.json", local);
	return length > 0 && (size_t)length < capacity;
#else
	(void)path;
	(void)capacity;
	return false;
#endif
}
uint32_t player_settings_disabled_features(const PlayerSettings* s)
{
	return (s->subsurface ? 0u : (uint32_t)RENDER_SSS) |
	       (s->volume ? 0u : (uint32_t)RENDER_VOLUME) |
	       (s->depth_of_field ? 0u : (uint32_t)RENDER_DEPTH_OF_FIELD) |
	       (s->fog_blur ? 0u : (uint32_t)RENDER_FOG_BLUR) |
	       (s->glow ? 0u : (uint32_t)RENDER_GLOW);
}
