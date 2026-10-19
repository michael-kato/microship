#include "audio.h"
#include "simulation.h"
#include "asset_json.h"
#include "vector_math.h"
#include <string.h>
#include <algorithm>
#include <new>
#include "asset_limits.h"

static const int WAV_HEADER_WORDS = 11;
static const unsigned int WAV_RIFF_TAG = 0x46464952u;
static const int WAV_RIFF_OVERHEAD_BYTES = 36;
static const unsigned int WAV_WAVE_TAG = 0x45564157u;
static const unsigned int WAV_FORMAT_TAG = 0x20746d66u;
static const unsigned int WAV_PCM_MONO_FORMAT = 0x00010001u;
static const int WAV_BYTES_PER_SECOND = 96000;
static const unsigned int WAV_PCM16_BLOCK_FORMAT = 0x00100002u;
static const unsigned int WAV_DATA_TAG = 0x61746164u;
static const int MAX_CLIP_BYTES = 192000;
static const int MAX_AUDIBLE_DISTANCE = 10000;
static const float MAX_MIX_SAMPLE = 30000.0f;
static const float AUDIO_SAMPLE_RATE_FLOAT = (float)AUDIO_SAMPLE_RATE;
static const int AUDIO_CAPTURE_SAMPLES_PER_TICK = AUDIO_SAMPLE_RATE/SIMULATION_TICKS_PER_SECOND;
static const int WAV_PCM_FORMAT_BYTES = 16;
static const int MAX_SOUND_COOLDOWN_SECONDS = 10;

enum { WAV_RIFF_WORD=0, WAV_RIFF_BYTES_WORD=1, WAV_WAVE_WORD=2, WAV_FORMAT_WORD=3, WAV_FORMAT_BYTES_WORD=4, WAV_ENCODING_WORD=5, WAV_SAMPLE_RATE_WORD=6, WAV_BYTE_RATE_WORD=7, WAV_BLOCK_FORMAT_WORD=8, WAV_DATA_WORD=9, WAV_DATA_BYTES_WORD=10 };

Audio::Audio():trigger_write(0),trigger_read(0),stopping(false),failed(false),device(0),capture(0),captured(0),distance(1),valid(true)
{
    memset(voices,0,sizeof(voices));memset(cooldown,0,sizeof(cooldown));memset(triggers,0,sizeof(triggers));
#ifdef _WIN32
    memset(headers,0,sizeof(headers));wake=0;thread=0;
#endif
}
Audio::~Audio() { audio_close(this); }
static bool wav_header(FILE* f,uint32_t bytes)
{
    uint32_t h[WAV_HEADER_WORDS]={WAV_RIFF_TAG,bytes+WAV_RIFF_OVERHEAD_BYTES,WAV_WAVE_TAG,WAV_FORMAT_TAG,WAV_PCM_FORMAT_BYTES,WAV_PCM_MONO_FORMAT,AUDIO_SAMPLE_RATE,WAV_BYTES_PER_SECOND,WAV_PCM16_BLOCK_FORMAT,WAV_DATA_TAG,bytes};
    return fwrite(h,sizeof(h),1,f)==1;
}
static bool load_clip(AudioClip* clip,const char* path)
{
    FILE* f=fopen(path,"rb");uint32_t h[WAV_HEADER_WORDS];bool ok=f && fread(h,sizeof(h),1,f)==1;
    ok=ok && h[WAV_RIFF_WORD]==WAV_RIFF_TAG && h[WAV_WAVE_WORD]==WAV_WAVE_TAG && h[WAV_FORMAT_WORD]==WAV_FORMAT_TAG && h[WAV_FORMAT_BYTES_WORD]==WAV_PCM_FORMAT_BYTES && h[WAV_ENCODING_WORD]==WAV_PCM_MONO_FORMAT
        && h[WAV_SAMPLE_RATE_WORD]==AUDIO_SAMPLE_RATE && h[WAV_BYTE_RATE_WORD]==WAV_BYTES_PER_SECOND && h[WAV_BLOCK_FORMAT_WORD]==WAV_PCM16_BLOCK_FORMAT && h[WAV_DATA_WORD]==WAV_DATA_TAG && h[WAV_DATA_BYTES_WORD]>0 && h[WAV_DATA_BYTES_WORD]<=MAX_CLIP_BYTES && !(h[WAV_DATA_BYTES_WORD]%AUDIO_SAMPLE_BYTES) && h[WAV_RIFF_BYTES_WORD]==h[WAV_DATA_BYTES_WORD]+WAV_RIFF_OVERHEAD_BYTES;
    if (ok)
    {
        try { clip->samples.resize(h[WAV_DATA_BYTES_WORD]/AUDIO_SAMPLE_BYTES); }
        catch (const std::bad_alloc&) { ok=false; }
        if (ok) ok=fread(clip->samples.data(),1,h[WAV_DATA_BYTES_WORD],f)==h[WAV_DATA_BYTES_WORD] && fgetc(f)==EOF && !ferror(f);
    }
    if (f && fclose(f)) ok=false;
    if (!ok) fprintf(stderr,"%s: expected a canonical mono 48 kHz 16-bit PCM WAV, at most two seconds\n",path);
    return ok;
}
bool audio_load(Audio* a,const char* manifest,const char* capture)
{
    JsonDocument doc;if (!json_load(&doc,manifest)) return false;Json root=json_root(&doc);
    json_object(root,"$schema|distance|events");a->distance=json_positive(json_get(root,"distance"),MAX_AUDIBLE_DISTANCE);
    Json events=json_get(root,"events");json_object(events,"order|growth|mature|birth|shot|hit|death|colonize|heal|win|lose|break");
    for (uint32_t i=0;i<SOUND_COUNT;++i)
    {
        Json j=json_get(events,sound_name(i));json_object(j,"file|gain|cooldown");char path[ASSET_PATH_CAPACITY]={};
        json_path(json_get(j,"file"),manifest,path);a->clips[i].gain=json_float(json_get(j,"gain"),0,1);
        a->clips[i].cooldown=json_float(json_get(j,"cooldown"),0,MAX_SOUND_COOLDOWN_SECONDS);
        if (!doc.valid || !load_clip(&a->clips[i],path)) return false;
    }
    if (capture)
    {
        a->capture=fopen(capture,"wb");
        if (!a->capture || !wav_header(a->capture,0)) { fprintf(stderr,"Cannot create audio capture '%s'.\n",capture);return false; }
    }
    return doc.valid;
}
static void mix(Audio* a,int16_t* output,uint32_t count);
static void start_voice(Audio* a,uint32_t clip,float gain)
{
    if (a->cooldown[clip]>0) return;
    uint32_t slot=0;while (slot<AUDIO_VOICE_COUNT && a->voices[slot].active) ++slot;
    if (slot==AUDIO_VOICE_COUNT) return;
    a->voices[slot]=AudioVoice{clip,0,gain,true};a->cooldown[clip]=a->clips[clip].cooldown;
}
#ifdef _WIN32
// Owns voices and cooldowns while the device is open. Refills every buffer the
// device has finished, after starting the voices queued since the last wake.
static DWORD WINAPI audio_thread(LPVOID context)
{
    Audio* a=(Audio*)context;
    while (!a->stopping.load(std::memory_order_acquire))
    {
        WaitForSingleObject(a->wake,INFINITE);
        uint32_t read=a->trigger_read.load(std::memory_order_relaxed),write=a->trigger_write.load(std::memory_order_acquire);
        for (;read!=write;++read) start_voice(a,a->triggers[read%AUDIO_TRIGGER_CAPACITY].clip,a->triggers[read%AUDIO_TRIGGER_CAPACITY].gain);
        a->trigger_read.store(read,std::memory_order_release);
        for (uint32_t i=0;i<AUDIO_OUTPUT_BUFFER_COUNT && !a->stopping.load(std::memory_order_acquire);++i)
            if (!(a->headers[i].dwFlags&WHDR_INQUEUE))
            {
                mix(a,a->buffers[i],AUDIO_OUTPUT_BUFFER_SAMPLES);
                if (waveOutWrite(a->device,&a->headers[i],sizeof(WAVEHDR))!=MMSYSERR_NOERROR) { a->failed.store(true);return 1; }
            }
    }
    return 0;
}
#endif
bool audio_open(Audio* a)
{
#ifdef _WIN32
    WAVEFORMATEX format={WAVE_FORMAT_PCM,1,AUDIO_SAMPLE_RATE,WAV_BYTES_PER_SECOND,AUDIO_SAMPLE_BYTES,AUDIO_SAMPLE_BITS,0};
    a->wake=CreateEventA(0,FALSE,FALSE,0);
    if (!a->wake) return false;
    if (waveOutOpen(&a->device,WAVE_MAPPER,&format,(DWORD_PTR)a->wake,0,CALLBACK_EVENT)!=MMSYSERR_NOERROR)
    { a->device=0;fprintf(stderr,"Audio output unavailable; sound is muted.\n");return true; }
    for (uint32_t i=0;i<AUDIO_OUTPUT_BUFFER_COUNT;++i)
    {
        WAVEHDR* h=&a->headers[i];h->lpData=(LPSTR)a->buffers[i];h->dwBufferLength=sizeof(a->buffers[i]);
        if (waveOutPrepareHeader(a->device,h,sizeof(*h))!=MMSYSERR_NOERROR) return false;
    }
    a->thread=CreateThread(0,0,audio_thread,a,0,0);
    if (!a->thread) { fprintf(stderr,"Cannot start the audio thread.\n");return false; }
    SetThreadPriority(a->thread,THREAD_PRIORITY_TIME_CRITICAL);SetEvent(a->wake);
    printf("Audio output: mono 48 kHz PCM; 16 voices; mixing thread; %u x %u-sample buffers.\n",
        (unsigned)AUDIO_OUTPUT_BUFFER_COUNT,(unsigned)AUDIO_OUTPUT_BUFFER_SAMPLES);return true;
#else
    (void)a;fprintf(stderr,"Native diagnostic has no live audio output.\n");return true;
#endif
}
void audio_events(Audio* a,Simulation* s,Vec3 listener)
{
#ifdef _WIN32
    bool live=a->thread!=0;
#else
    bool live=false;
#endif
    for (uint32_t i=0;i<s->sounds.count;++i)
    {
        const SoundEvent& event=s->sounds.pending[i];uint32_t k=event.kind;
        float d=std::sqrt(dot(subtract(event.position,listener),subtract(event.position,listener)));
        bool global=k==SOUND_ORDER || k==SOUND_WIN || k==SOUND_LOSE;
        float gain=a->clips[k].gain*(global ? 1 : std::fmax(0.0f,1-d/a->distance));if (gain<=0) continue;
        if (!live) { start_voice(a,k,gain);continue; }
        // A full ring drops the trigger, as a busy voice set already does.
        uint32_t write=a->trigger_write.load(std::memory_order_relaxed);
        if (write-a->trigger_read.load(std::memory_order_acquire)>=AUDIO_TRIGGER_CAPACITY) continue;
        a->triggers[write%AUDIO_TRIGGER_CAPACITY]=AudioTrigger{k,gain};
        a->trigger_write.store(write+1,std::memory_order_release);
    }
    s->sounds.count=0;
}
static void mix(Audio* a,int16_t* output,uint32_t count)
{
    for (uint32_t i=0;i<count;++i)
    {
        float sample=0;
        for (uint32_t j=0;j<AUDIO_VOICE_COUNT;++j) if (a->voices[j].active)
        {
            AudioVoice& v=a->voices[j];const AudioClip& c=a->clips[v.clip];sample+=c.samples[v.cursor++]*v.gain;
            if (v.cursor>=c.samples.size()) v.active=false;
        }
        output[i]=(int16_t)std::fmax(-MAX_MIX_SAMPLE,std::fmin(MAX_MIX_SAMPLE,sample));
    }
    for (uint32_t j=0;j<SOUND_COUNT;++j) a->cooldown[j]=std::fmax(0.0f,a->cooldown[j]-count/AUDIO_SAMPLE_RATE_FLOAT);
}
bool audio_tick(Audio* a)
{
    if (!a->capture) return true;
    int16_t samples[AUDIO_CAPTURE_SAMPLES_PER_TICK];mix(a,samples,AUDIO_CAPTURE_SAMPLES_PER_TICK);
    if (a->captured>UINT32_MAX-WAV_RIFF_OVERHEAD_BYTES-sizeof(samples)) return false;
    a->captured+=sizeof(samples);a->valid=a->valid && fwrite(samples,sizeof(samples),1,a->capture)==1;return a->valid;
}
bool audio_running(const Audio* a)
{
    return !a->failed.load(std::memory_order_acquire);
}
bool audio_close(Audio* a)
{
#ifdef _WIN32
    if (a->thread)
    {
        a->stopping.store(true,std::memory_order_release);SetEvent(a->wake);
        WaitForSingleObject(a->thread,INFINITE);CloseHandle(a->thread);a->thread=0;
        if (a->failed.load()) a->valid=false;
    }
    if (a->device)
    {
        if (waveOutReset(a->device)!=MMSYSERR_NOERROR) a->valid=false;
        for (uint32_t i=0;i<AUDIO_OUTPUT_BUFFER_COUNT;++i) if (a->headers[i].dwFlags&WHDR_PREPARED)
            if (waveOutUnprepareHeader(a->device,&a->headers[i],sizeof(WAVEHDR))!=MMSYSERR_NOERROR) a->valid=false;
        if (waveOutClose(a->device)!=MMSYSERR_NOERROR) a->valid=false;
        a->device=0;
    }
    if (a->wake) { CloseHandle(a->wake);a->wake=0; }
    #endif
    if (a->capture)
    {
        if (fseek(a->capture,0,SEEK_SET)!=0 || !wav_header(a->capture,a->captured)) a->valid=false;
        if (fclose(a->capture)) a->valid=false;
        a->capture=0;
    }
    return a->valid;
}
