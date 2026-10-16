#ifndef MICROSHIP_AUDIO_H
#define MICROSHIP_AUDIO_H
#include "sound_events.h"
#include <stdint.h>
#include <vector>
#include <stdio.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
struct Simulation;
enum { AUDIO_VOICE_COUNT=16, AUDIO_OUTPUT_BUFFER_COUNT=4, AUDIO_OUTPUT_BUFFER_SAMPLES=1024, AUDIO_SAMPLE_RATE=48000, AUDIO_SAMPLE_BYTES=2, AUDIO_SAMPLE_BITS=16 };
struct AudioClip { std::vector<int16_t> samples;float gain,cooldown; };
struct AudioVoice { uint32_t clip,cursor;float gain;bool active; };
struct Audio
{
    AudioClip clips[SOUND_COUNT];AudioVoice voices[AUDIO_VOICE_COUNT];float cooldown[SOUND_COUNT];
    HWAVEOUT device;WAVEHDR headers[AUDIO_OUTPUT_BUFFER_COUNT];int16_t buffers[AUDIO_OUTPUT_BUFFER_COUNT][AUDIO_OUTPUT_BUFFER_SAMPLES];
    FILE* capture;uint32_t captured;float distance;bool valid;
    Audio();~Audio();
};
bool audio_load(Audio*,const char* manifest,const char* capture);
bool audio_open(Audio*);
void audio_events(Audio*,Simulation*,Vec3 listener);
bool audio_tick(Audio*);
bool audio_pump(Audio*);
bool audio_close(Audio*);
#endif
