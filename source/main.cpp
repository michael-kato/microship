#include "asset_json.h"
#include "game.h"
#include "start_menu.h"

#include <cmath>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

static const int MAX_CAMERA_PAN_SECONDS = 60;
static const int MAX_FRAME_DELAY_MS = 250;
static const int MAX_COMMAND_SAMPLES = 4096;
static const int MAX_COMMAND_TICKS = 1000000;
static const int MAX_COMMAND_MATCHES = 1000;
static const int EXECUTABLE_PATH_CAPACITY = 4096;
static const int MAX_COMMAND_REAL_MAGNITUDE = 1000000;
static const int DECIMAL_RADIX = 10;
static const int MAX_CLIENT_PIXEL_COORDINATE = 65535;
static const double MAX_CURSOR_ANCHOR_COORDINATE = 100000.0;
static const int MAX_AUDIO_CAPTURE_TICKS = 3600;

static bool asset_paths(char* world, char* config, char* shader, char* audio,
                        char* menu, char* settings, size_t capacity)
{
	char executable[EXECUTABLE_PATH_CAPACITY];
#ifdef _WIN32
	size_t count = GetModuleFileNameA(0, executable, sizeof(executable));
#else
	ssize_t size = readlink("/proc/self/exe", executable, sizeof(executable));
	size_t count = size > 0 ? (size_t)size : 0;
#endif
	if (!count || count >= sizeof(executable))
		return false;
	executable[count] = 0;
	char* slash = strrchr(executable, '/');
	char* backslash = strrchr(executable, '\\');
	if (backslash && (!slash || backslash > slash))
		slash = backslash;
	if (!slash)
		return false;
	*slash = 0;
	char manifest[EXECUTABLE_PATH_CAPACITY];
	int length = snprintf(manifest, sizeof(manifest), "%s/assets/startup.json",
	                      executable);
	if (length <= 0 || (size_t)length >= sizeof(manifest))
		return false;
	JsonDocument document;
	if (!json_load(&document, manifest))
		return false;
	Json root = json_root(&document);
	json_object(root, "$schema|world|render|shader|audio|menu|settings");
	return json_path(json_get(root, "world"), manifest, world, capacity) &&
	       json_path(json_get(root, "render"), manifest, config, capacity) &&
	       json_path(json_get(root, "shader"), manifest, shader, capacity) &&
	       json_path(json_get(root, "audio"), manifest, audio, capacity) &&
	       json_path(json_get(root, "menu"), manifest, menu, capacity) &&
	       json_path(json_get(root, "settings"), manifest, settings,
	                 capacity) &&
	       document.valid;
}

static void print_usage(FILE* output)
{
	fprintf(
	    output,
	    "Usage: nanovore [--world PATH] [--config PATH] [--shader PATH] "
	    "[--headless]\n"
	    "              [--frames N] [--frame-ticks 1..4] [--frame-delay-ms "
	    "0..250] [--capture PATH.ppm|PATH.pfm] [--samples N] [--seed N]\n"
	    "              [--vsync] [--time SECONDS] [--step-seconds N] "
	    "[--camera-drag X Y] [--camera-zoom N]\n"
	    "              [--camera-pointer X Y] [--click X Y] [--command X Y] "
	    "[--select-group 1..3] [--select-screen-group 1..2] [--focus] "
	    "[--additive] [--restart]\n"
	    "              [--pointer-drag X1 Y1 X2 Y2] (client pixels)\n"
	    "              [--smoke] [--stats|--no-stats] [--debug-views] "
	    "[--wireframe] [--no-activate]\n"
	    "              [--replay PATH.orders] [--ticks N] [--state PATH.json] "
	    "[--scene-state PATH.json]\n"
	    "              [--matches N] [--cpu-benchmark] (with --smoke and "
	    "--ticks: seeded matches, optional tick timings)\n"
	    "              [--audio-capture PATH.wav] (with --smoke and --ticks, "
	    "at most 3600 ticks)\n"
	    "              [--camera-pan X Y SECONDS] [--world-seed N] [--audio "
	    "PATH.json] [--menu|--no-menu] [--menu-select 1..3] [--faction NAME]\n"
	    "              [--settings PATH.json] [--pause-on-start] (production "
	    "menu diagnostic)\n"
	    "Default: faction menu, then direct ray-traced Win32 window; Esc opens "
	    "the pause menu; close exits.\n"
	    "Headless/capture runs default to one frame. PFM saves linear HDR "
	    "before bloom; PPM saves display RGB.\n"
	    "--smoke runs asset loading and simulation lifecycle without a GPU.\n");
#ifdef NANOVORE_RENDERDOC_CAPTURE
	fprintf(output, "Native RenderDoc capture support: enabled.\n");
#endif
}

static bool parse_real(const char* text, double* value)
{
	char* end = 0;
	errno = 0;
	double parsed = strtod(text, &end);
	if (!text[0] || *end || errno || !std::isfinite(parsed) ||
	    std::fabs(parsed) > MAX_COMMAND_REAL_MAGNITUDE)
		return false;
	*value = parsed;
	return true;
}

static bool parse_integer(const char* text, uint32_t minimum, uint32_t maximum,
                          uint32_t* value)
{
	if (!text[0])
		return false;
	for (const char* p = text; *p; ++p)
		if (*p < '0' || *p > '9')
			return false;
	errno = 0;
	char* end = 0;
	unsigned long long parsed = strtoull(text, &end, DECIMAL_RADIX);
	if (errno || *end || parsed < minimum || parsed > maximum)
		return false;
	*value = (uint32_t)parsed;
	return true;
}

int main(int argc, char** argv)
{
	static char diagnostic_output[64 * 1024];
	if (setvbuf(stdout, diagnostic_output, _IOFBF, sizeof(diagnostic_output)))
		return 1;
	GameOptions options = {};
	char world_path[EXECUTABLE_PATH_CAPACITY],
	    config_path[EXECUTABLE_PATH_CAPACITY],
	    shader_path[EXECUTABLE_PATH_CAPACITY],
	    audio_path[EXECUTABLE_PATH_CAPACITY],
	    menu_path[EXECUTABLE_PATH_CAPACITY],
	    settings_defaults_path[EXECUTABLE_PATH_CAPACITY];
	bool no_menu = false, force_menu = false;
	uint32_t menu_selection = 0;
	char menu_faction[ASSET_NAME_CAPACITY] = {};
	if (!asset_paths(world_path, config_path, shader_path, audio_path,
	                 menu_path, settings_defaults_path, sizeof(world_path)))
	{
		fprintf(stderr,
		        "Cannot locate assets beside the nanovore executable.\n");
		return 1;
	}
	options.world_path = world_path;
	options.config_path = config_path;
	options.shader_path = shader_path;
	options.builtin_shader_path = shader_path;
	options.audio_path = audio_path;
	options.settings_defaults_path = settings_defaults_path;
	for (int i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "--help") == 0)
		{
			print_usage(stdout);
			return 0;
		}
		if (!strcmp(argv[i], "--pause-on-start"))
			options.pause_on_start = true;
		else if (!strcmp(argv[i], "--settings"))
		{
			if (++i == argc)
			{
				fprintf(stderr, "--settings requires a JSON file path.\n");
				return 2;
			}
			options.settings_path = argv[i];
		}
		else if (!strcmp(argv[i], "--faction"))
		{
			if (++i == argc)
			{
				fprintf(stderr, "--faction requires a faction name.\n");
				return 2;
			}
			options.player_faction = argv[i];
		}
		else if (!strcmp(argv[i], "--no-menu"))
			no_menu = true;
		else if (!strcmp(argv[i], "--menu"))
			force_menu = true;
		else if (!strcmp(argv[i], "--menu-select"))
		{
			if (++i == argc || !parse_integer(argv[i], 1, 8, &menu_selection))
			{
				fprintf(stderr, "--menu-select requires a choice in [1,8].\n");
				return 2;
			}
		}
		else if (strcmp(argv[i], "--frame-ticks") == 0)
		{
			if (++i == argc ||
			    !parse_integer(argv[i], 1, 4, &options.frame_ticks))
			{
				fprintf(stderr,
				        "--frame-ticks requires an integer in [1,4].\n");
				return 2;
			}
		}
		else if (strcmp(argv[i], "--frame-delay-ms") == 0)
		{
			if (++i == argc || !parse_integer(argv[i], 0, MAX_FRAME_DELAY_MS,
			                                  &options.frame_delay_ms))
			{
				fprintf(stderr,
				        "--frame-delay-ms requires an integer in [0,250].\n");
				return 2;
			}
		}
		else if (strcmp(argv[i], "--smoke") == 0)
			options.smoke = true;
		else if (strcmp(argv[i], "--cpu-benchmark") == 0)
			options.cpu_benchmark = true;
		else if (strcmp(argv[i], "--vsync") == 0)
			options.vsync = true;
		else if (strcmp(argv[i], "--headless") == 0)
			options.headless = true;
		else if (strcmp(argv[i], "--stats") == 0 ||
		         strcmp(argv[i], "--no-stats") == 0)
		{
			options.stats_set = true;
			options.show_stats = strcmp(argv[i], "--stats") == 0;
		}
#ifdef DEVELOPMENT
		else if (strcmp(argv[i], "--debug-views") == 0)
			options.debug_views = true;
		else if (strcmp(argv[i], "--wireframe") == 0)
			options.wireframe = true;
#endif
		else if (strcmp(argv[i], "--no-activate") == 0)
			options.no_activate = true;
		else if (strcmp(argv[i], "--restart") == 0)
			options.camera_input.restart = true;
		else if (strcmp(argv[i], "--focus") == 0)
			options.camera_input.focus = true;
		else if (strcmp(argv[i], "--additive") == 0)
			options.camera_input.additive = true;
		else if (strcmp(argv[i], "--select-group") == 0)
		{
			if (++i == argc || strlen(argv[i]) != 1 || argv[i][0] < '1' ||
			    argv[i][0] > '3')
			{
				fprintf(stderr, "--select-group requires 1, 2 or 3.\n");
				return 2;
			}
			options.camera_input.group = (uint32_t)(argv[i][0] - '0');
		}
		else if (strcmp(argv[i], "--select-screen-group") == 0)
		{
			if (++i == argc || strlen(argv[i]) != 1 || argv[i][0] < '1' ||
			    argv[i][0] > '2')
			{
				fprintf(stderr, "--select-screen-group requires 1 or 2.\n");
				return 2;
			}
			options.camera_input.screen_group = (uint32_t)(argv[i][0] - '0');
		}
		else if (strcmp(argv[i], "--camera-pan") == 0)
		{
			double values[3];
			for (uint32_t j = 0; j < 3; ++j)
				if (++i == argc || !parse_real(argv[i], &values[j]) ||
				    values[j] < (j == 2 ? 0 : -1) ||
				    values[j] > (j == 2 ? MAX_CAMERA_PAN_SECONDS : 1))
				{
					fprintf(stderr, "--camera-pan requires X/Y in [-1,1] and "
					                "seconds in [0,60].\n");
					return 2;
				}
			options.camera_input.pan_x = (float)values[0];
			options.camera_input.pan_y = (float)values[1];
			options.camera_input.pan_seconds = (float)values[2];
		}
		else if (strcmp(argv[i], "--pointer-drag") == 0)
		{
			int32_t pixels[4];
			for (int j = 0; j < 4; ++j)
			{
				double value;
				if (++i == argc || !parse_real(argv[i], &value) || value < 0 ||
				    value > MAX_CLIENT_PIXEL_COORDINATE ||
				    value != std::floor(value))
				{
					fprintf(stderr, "--pointer-drag requires four integer "
					                "client coordinates in [0,65535].\n");
					return 2;
				}
				pixels[j] = (int32_t)value;
			}
			PointerInput* input = &options.camera_input;
			input->pointer_pressed = true;
			input->pointer_released = true;
			input->pointer_valid = true;
			input->pointer_moved = true;
			input->press_x = pixels[0];
			input->press_y = pixels[1];
			input->pointer_x = pixels[2];
			input->pointer_y = pixels[3];
		}
		else if (strcmp(argv[i], "--time") == 0 ||
		         strcmp(argv[i], "--step-seconds") == 0 ||
		         strcmp(argv[i], "--camera-zoom") == 0 ||
		         strcmp(argv[i], "--camera-drag") == 0 ||
		         strcmp(argv[i], "--camera-pointer") == 0 ||
		         strcmp(argv[i], "--click") == 0 ||
		         strcmp(argv[i], "--command") == 0)
		{
			const char* flag = argv[i];
			double value = 0, second = 0;
			bool drag = strcmp(flag, "--camera-drag") == 0;
			bool pointer = strcmp(flag, "--camera-pointer") == 0;
			bool click = strcmp(flag, "--click") == 0,
			     command = strcmp(flag, "--command") == 0;
			if (++i == argc || !parse_real(argv[i], &value) ||
			    ((drag || pointer || click || command) &&
			     (++i == argc || !parse_real(argv[i], &second))))
			{
				fprintf(stderr, "%s requires finite numeric values.\n", flag);
				return 2;
			}
			if (strcmp(flag, "--time") == 0)
			{
				options.time_seconds = value;
				options.time_set = true;
			}
			else if (strcmp(flag, "--step-seconds") == 0)
				options.step_seconds = value;
			else if (click || command)
			{
				if (value < 0 || value > MAX_CLIENT_PIXEL_COORDINATE ||
				    second < 0 || second > MAX_CLIENT_PIXEL_COORDINATE ||
				    value != std::floor(value) || second != std::floor(second))
				{
					fprintf(stderr, "Pointer controls require integer client "
					                "coordinates in [0,65535].\n");
					return 2;
				}
				if (click)
				{
					options.camera_input.click = true;
					options.camera_input.click_x = (int32_t)value;
					options.camera_input.click_y = (int32_t)second;
				}
				else
				{
					options.camera_input.command = true;
					options.camera_input.command_x = (int32_t)value;
					options.camera_input.command_y = (int32_t)second;
				}
			}
			else if (pointer)
			{
				options.camera_input.pointer_x = (int32_t)std::fmax(
				    -MAX_CURSOR_ANCHOR_COORDINATE,
				    std::fmin(MAX_CURSOR_ANCHOR_COORDINATE, value));
				options.camera_input.pointer_y = (int32_t)std::fmax(
				    -MAX_CURSOR_ANCHOR_COORDINATE,
				    std::fmin(MAX_CURSOR_ANCHOR_COORDINATE, second));
				options.camera_input.pointer_valid = true;
			}
			else if (drag)
			{
				options.camera_input.drag_x = (float)value;
				options.camera_input.drag_y = (float)second;
			}
			else
				options.camera_input.wheel = (float)value;
		}
		else if (strcmp(argv[i], "--world") == 0 ||
		         strcmp(argv[i], "--config") == 0 ||
		         strcmp(argv[i], "--capture") == 0 ||
		         strcmp(argv[i], "--shader") == 0 ||
		         strcmp(argv[i], "--replay") == 0 ||
		         strcmp(argv[i], "--state") == 0 ||
		         strcmp(argv[i], "--scene-state") == 0 ||
		         strcmp(argv[i], "--audio-capture") == 0 ||
		         strcmp(argv[i], "--audio") == 0)
		{
			const char* flag = argv[i];
			if (++i == argc || !argv[i][0] || strncmp(argv[i], "--", 2) == 0)
			{
				fprintf(stderr, "%s requires a file path.\n", flag);
				return 2;
			}
			if (strcmp(flag, "--world") == 0)
				options.world_path = argv[i];
			else if (strcmp(flag, "--config") == 0)
				options.config_path = argv[i];
			else if (strcmp(flag, "--shader") == 0)
				options.shader_path = argv[i];
			else if (strcmp(flag, "--replay") == 0)
				options.replay_path = argv[i];
			else if (strcmp(flag, "--scene-state") == 0)
				options.scene_state_path = argv[i];
			else if (strcmp(flag, "--state") == 0)
				options.state_path = argv[i];
			else if (strcmp(flag, "--audio-capture") == 0)
				options.audio_capture_path = argv[i];
			else if (strcmp(flag, "--audio") == 0)
				options.audio_path = argv[i];
			else
				options.capture_path = argv[i];
		}
		else if (strcmp(argv[i], "--frames") == 0 ||
		         strcmp(argv[i], "--samples") == 0 ||
		         strcmp(argv[i], "--seed") == 0 ||
		         strcmp(argv[i], "--world-seed") == 0 ||
		         strcmp(argv[i], "--ticks") == 0 ||
		         strcmp(argv[i], "--matches") == 0)
		{
			const char* flag = argv[i];
			uint32_t value = 0;
			bool seed = strcmp(flag, "--seed") == 0 ||
			            strcmp(flag, "--world-seed") == 0;
			uint32_t maximum =
			    strcmp(flag, "--samples") == 0   ? MAX_COMMAND_SAMPLES
				: strcmp(flag, "--ticks") == 0   ? MAX_COMMAND_TICKS
				: strcmp(flag, "--matches") == 0 ? MAX_COMMAND_MATCHES
				                                 : UINT32_MAX;
			if (++i == argc ||
			    !parse_integer(argv[i], seed ? 0 : 1, maximum, &value))
			{
				fprintf(stderr, "%s requires an integer in [%u, %u].\n", flag,
				        seed ? 0 : 1, maximum);
				return 2;
			}
			if (!strcmp(flag, "--world-seed"))
			{
				options.world_seed = value;
				options.world_seed_set = true;
			}
			else if (seed)
			{
				options.seed = value;
				options.seed_set = true;
			}
			else if (strcmp(flag, "--samples") == 0)
				options.samples = value;
			else if (strcmp(flag, "--ticks") == 0)
				options.ticks = value;
			else if (strcmp(flag, "--matches") == 0)
				options.matches = value;
			else
				options.frames = value;
		}
		else
		{
			print_usage(stderr);
			return 2;
		}
	}
	if (options.audio_capture_path && (!options.smoke || !options.ticks ||
	                                   options.ticks > MAX_AUDIO_CAPTURE_TICKS))
	{
		fprintf(stderr,
		        "--audio-capture requires --smoke and --ticks in [1,3600].\n");
		return 2;
	}
	if (options.matches &&
	    (!options.smoke || !options.ticks || options.replay_path ||
	     options.state_path || options.scene_state_path ||
	     options.audio_capture_path))
	{
		fprintf(stderr, "--matches requires --smoke and --ticks, without "
		                "replay, state or audio capture.\n");
		return 2;
	}
	if (options.cpu_benchmark && !options.matches)
	{
		fprintf(stderr, "--cpu-benchmark requires --matches.\n");
		return 2;
	}
	if (options.smoke &&
	    (options.headless || options.capture_path || options.frames))
	{
		fprintf(stderr,
		        "--smoke cannot be combined with GPU frame controls.\n");
		return 2;
	}
	if (!options.frames && (options.headless || options.capture_path))
		options.frames = 1;
	if (options.frame_ticks && (!options.frames || options.time_set))
	{
		fprintf(stderr, "--frame-ticks requires bounded --frames and no --time "
		                "override.\n");
		return 2;
	}
	if (options.frame_delay_ms && !options.frames)
	{
		fprintf(stderr, "--frame-delay-ms requires a bounded --frames run.\n");
		return 2;
	}
	bool diagnostic = options.smoke || options.headless || options.frames ||
	                  options.capture_path || options.world_path != world_path;
	if (options.pause_on_start && options.headless)
	{
		fprintf(stderr, "--pause-on-start needs a window-capable build.\n");
		return 2;
	}
	if (options.pause_on_start && diagnostic && !options.settings_path)
	{
		fprintf(stderr,
		        "A pause-menu diagnostic requires --settings PATH.json.\n");
		return 2;
	}
#ifdef _WIN32
	if (options.pause_on_start || options.frames)
	{
		printf("Application process: %lu.\n", GetCurrentProcessId());
		fflush(stdout);
	}
#endif
	bool show_menu = force_menu || menu_selection || (!no_menu && !diagnostic);
	for (;;)
	{
		if (show_menu)
		{
			int result = start_menu(menu_path, menu_faction,
			                        sizeof(menu_faction), menu_selection);
			if (result <= 0)
				return result < 0 ? 1 : 0;
			options.player_faction = menu_faction;
		}
		int result = game_run(&options);
		if (result != GAME_RETURN_TO_MENU)
			return result;
		show_menu = true;
		menu_selection = 0;
		options.pause_on_start = false;
	}
}
