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
struct AudioClip { std::vector<int16_t> samples;float gain,cooldown; };
struct AudioVoice { uint32_t clip,cursor;float gain;bool active; };
struct Audio
{
    AudioClip clips[SOUND_COUNT];AudioVoice voices[16];float cooldown[SOUND_COUNT];
    HWAVEOUT device;WAVEHDR headers[4];int16_t buffers[4][1024];
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
