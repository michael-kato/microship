#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <chrono>
#endif
#include "profiler.h"
#include <stdio.h>
#include <string.h>
const ProfileDefinition profile_definitions[PROFILE_NODE_COUNT] = {
    {"CPU FRAME", -1, 0},
    {"INPUT", PROFILE_CPU_FRAME, 1},
    {"SIMULATION", PROFILE_CPU_FRAME, 1},
    {"SCENE + AUDIO", PROFILE_CPU_FRAME, 1},
    {"RENDER", PROFILE_CPU_FRAME, 1},
    {"ACQUIRE", PROFILE_RENDER, 2},
    {"UPLOAD + AS RECORD", PROFILE_RENDER, 2},
    {"PASS RECORD", PROFILE_RENDER, 2},
    {"SUBMIT", PROFILE_RENDER, 2},
    {"SLOT WAIT + QUERY", PROFILE_RENDER, 2},
    {"SLOT FENCE WAIT", PROFILE_READ_TIMES, 3},
    {"TIMESTAMP READ", PROFILE_READ_TIMES, 3},
    {"TIMING REPORT", PROFILE_READ_TIMES, 3},
    {"PRESENT WAIT", PROFILE_RENDER, 2},
    {"CAPTURE", PROFILE_RENDER, 2},
    {"GPU FRAME", -1, 0},
    {"SCENE", PROFILE_GPU_FRAME, 1},
    {"AS UPDATE", PROFILE_GPU_SCENE, 2},
    {"BACKDROP", PROFILE_GPU_SCENE, 2},
    {"LIGHTING FIELDS", PROFILE_GPU_SCENE, 2},
    {"SURFACE + SSS", PROFILE_GPU_SCENE, 2},
    {"MEMBRANES", PROFILE_SURFACE, 3},
    {"OPAQUE HITS", PROFILE_SURFACE, 3},
    {"VOLUME", PROFILE_GPU_SCENE, 2},
    {"POST", PROFILE_GPU_FRAME, 1},
    {"EFFECTS", PROFILE_GPU_POST, 2},
    {"CELL BLUR", PROFILE_GPU_POST, 2},
    {"FOG", PROFILE_GPU_POST, 2},
    {"DEPTH OF FIELD", PROFILE_GPU_POST, 2},
    {"BLOOM + DISPLAY", PROFILE_GPU_POST, 2},
    {"OUTPUT", PROFILE_GPU_FRAME, 1},
    {"HUD + PROFILER", PROFILE_GPU_OUTPUT, 2},
    {"OUTPUT COPY", PROFILE_GPU_OUTPUT, 2}};
double profiler_clock_ms()
{
#ifdef _WIN32
	LARGE_INTEGER count, frequency;
	QueryPerformanceCounter(&count);
	QueryPerformanceFrequency(&frequency);
	return (double)count.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
	return std::chrono::duration<double, std::milli>(
	           std::chrono::steady_clock::now().time_since_epoch())
	    .count();
#endif
}
void profiler_begin(Profiler* p, double origin_ms)
{
	p->origin_ms = origin_ms;
	memset(p->current, 0, sizeof(p->current));
}
void profiler_span(Profiler* p, ProfileNode node, double start_ms,
                   double duration_ms)
{
	p->current[node] = ProfileSample{start_ms, duration_ms};
}
void profiler_cpu(Profiler* p, ProfileNode node, double start_ms, double end_ms)
{
	profiler_span(p, node, start_ms - p->origin_ms, end_ms - start_ms);
}
void profiler_clear_history(Profiler* p)
{
	memset(p->latest, 0, sizeof(p->latest));
	memset(p->mean, 0, sizeof(p->mean));
	memset(p->sum, 0, sizeof(p->sum));
	memset(p->history, 0, sizeof(p->history));
	p->count = 0;
	p->cursor = 0;
}
void profiler_finish(Profiler* p, bool print)
{
	if (p->count < PROFILE_HISTORY)
		++p->count;
	for (uint32_t i = 0; i < PROFILE_NODE_COUNT; ++i)
	{
		ProfileSample& old = p->history[p->cursor][i];
		ProfileSample now = p->current[i];
		p->sum[i].start_ms += now.start_ms - old.start_ms;
		p->sum[i].duration_ms += now.duration_ms - old.duration_ms;
		old = p->latest[i] = now;
		p->mean[i] = ProfileSample{p->sum[i].start_ms / p->count,
		                           p->sum[i].duration_ms / p->count};
		if (print)
			printf("Profile %s: start %.6f ms; last %.6f ms; mean %.6f ms; "
			       "samples %u.\n",
			       profile_definitions[i].name, now.start_ms, now.duration_ms,
			       p->mean[i].duration_ms, p->count);
	}
	p->cursor = (p->cursor + 1) % PROFILE_HISTORY;
}
