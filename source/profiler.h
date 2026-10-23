#ifndef NANOVORE_PROFILER_H
#define NANOVORE_PROFILER_H
#include <stdint.h>

enum ProfileNode
{
	PROFILE_CPU_FRAME,
	PROFILE_INPUT,
	PROFILE_SIMULATION,
	PROFILE_SCENE,
	PROFILE_RENDER,
	PROFILE_ACQUIRE,
	PROFILE_UPLOAD,
	PROFILE_RECORD,
	PROFILE_SUBMIT_WAIT,
	PROFILE_READ_TIMES,
	PROFILE_SLOT_FENCE,
	PROFILE_QUERY_READ,
	PROFILE_QUERY_REPORT,
	PROFILE_PRESENT,
	PROFILE_CAPTURE,
	PROFILE_GPU_FRAME,
	PROFILE_GPU_SCENE,
	PROFILE_ACCELERATION,
	PROFILE_BACKDROP,
	PROFILE_SHADOW_GRID,
	PROFILE_SURFACE,
	PROFILE_SURFACE_TRANSPARENT,
	PROFILE_SURFACE_OPAQUE,
	PROFILE_VOLUME,
	PROFILE_GPU_POST,
	PROFILE_EFFECTS,
	PROFILE_CELL_BLUR,
	PROFILE_FOG,
	PROFILE_DEPTH_OF_FIELD,
	PROFILE_BLOOM,
	PROFILE_GPU_OUTPUT,
	PROFILE_OVERLAY,
	PROFILE_COPY,
	PROFILE_NODE_COUNT
};
enum
{
	PROFILE_HISTORY = 120
};
struct ProfileDefinition
{
	const char* name;
	int parent, depth;
};
extern const ProfileDefinition profile_definitions[PROFILE_NODE_COUNT];
struct ProfileSample
{
	double start_ms, duration_ms;
};
struct Profiler
{
	double origin_ms;
	ProfileSample current[PROFILE_NODE_COUNT], latest[PROFILE_NODE_COUNT],
	    mean[PROFILE_NODE_COUNT], sum[PROFILE_NODE_COUNT];
	ProfileSample history[PROFILE_HISTORY][PROFILE_NODE_COUNT];
	uint32_t count, cursor;
	int32_t pointer_x, pointer_y;
};
double profiler_clock_ms();
void profiler_begin(Profiler*, double origin_ms);
void profiler_cpu(Profiler*, ProfileNode, double start_ms, double end_ms);
void profiler_span(Profiler*, ProfileNode, double start_ms, double duration_ms);
void profiler_finish(Profiler*, bool print);
void profiler_clear_history(Profiler*);
#endif
