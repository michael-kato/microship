#ifndef NANOVA_AUDIO_H
#define NANOVA_AUDIO_H
#include "sound_events.h"
#include <stdint.h>
#include <vector>
#include <stdio.h>
#include <atomic>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif
struct Simulation;
// Four 10.7 ms device buffers: about 43 ms queued output.
enum { AUDIO_VOICE_COUNT=16, AUDIO_OUTPUT_BUFFER_COUNT=4, AUDIO_OUTPUT_BUFFER_SAMPLES=512, AUDIO_SAMPLE_RATE=48000, AUDIO_SAMPLE_BYTES=2, AUDIO_SAMPLE_BITS=16 };
enum { AUDIO_TRIGGER_CAPACITY=64 };
struct AudioClip { std::vector<int16_t> samples;float gain,cooldown; };
struct AudioVoice { uint32_t clip,cursor;float gain;bool active; };
struct AudioTrigger { uint32_t clip;float gain; };
// Live output mixes on its own thread, woken by the device when a buffer
// completes, so slow frames do not starve it. The game thread only queues
// triggers through a single-producer, single-consumer ring. Captures mix
// synchronously per simulation tick instead.
struct Audio
{
    AudioClip clips[SOUND_COUNT];AudioVoice voices[AUDIO_VOICE_COUNT];float cooldown[SOUND_COUNT];
    AudioTrigger triggers[AUDIO_TRIGGER_CAPACITY];
    std::atomic<uint32_t> trigger_write,trigger_read;
    std::atomic<bool> stopping,failed;
    std::atomic<float> output_gain;
#ifdef _WIN32
    HWAVEOUT device;WAVEHDR headers[AUDIO_OUTPUT_BUFFER_COUNT];HANDLE wake,thread;
#else
    void* device;
#endif
    int16_t buffers[AUDIO_OUTPUT_BUFFER_COUNT][AUDIO_OUTPUT_BUFFER_SAMPLES];
    FILE* capture;uint32_t captured;float distance;bool valid;
    Audio();~Audio();
};
void audio_set_volume(Audio*,float master,float effects);
bool audio_load(Audio*,const char* manifest,const char* capture);
bool audio_open(Audio*);
void audio_events(Audio*,Simulation*,Vec3 listener);
bool audio_tick(Audio*);
bool audio_running(const Audio*); // False once the live output thread has failed.
bool audio_close(Audio*);
#endif
