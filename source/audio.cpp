#include "audio.h"
#include "simulation.h"
#include "asset_json.h"
#include "vector_math.h"
#include <string.h>
#include <algorithm>
#include <new>

Audio::Audio():device(0),capture(0),captured(0),distance(1),valid(true)
{ memset(voices,0,sizeof(voices));memset(cooldown,0,sizeof(cooldown));memset(headers,0,sizeof(headers)); }
Audio::~Audio() { audio_close(this); }
static bool wav_header(FILE* f,uint32_t bytes)
{
    uint32_t h[11]={0x46464952u,bytes+36,0x45564157u,0x20746d66u,16,0x00010001u,48000,96000,0x00100002u,0x61746164u,bytes};
    return fwrite(h,sizeof(h),1,f)==1;
}
static bool load_clip(AudioClip* clip,const char* path)
{
    FILE* f=fopen(path,"rb");uint32_t h[11];bool ok=f && fread(h,sizeof(h),1,f)==1;
    ok=ok && h[0]==0x46464952u && h[2]==0x45564157u && h[3]==0x20746d66u && h[4]==16 && h[5]==0x00010001u
        && h[6]==48000 && h[7]==96000 && h[8]==0x00100002u && h[9]==0x61746164u && h[10]>0 && h[10]<=192000 && !(h[10]%2) && h[1]==h[10]+36;
    if (ok)
    {
        try { clip->samples.resize(h[10]/2); }
        catch (const std::bad_alloc&) { ok=false; }
        if (ok) ok=fread(clip->samples.data(),1,h[10],f)==h[10] && fgetc(f)==EOF && !ferror(f);
    }
    if (f && fclose(f)) ok=false;
    if (!ok) fprintf(stderr,"%s: expected a canonical mono 48 kHz 16-bit PCM WAV, at most two seconds\n",path);
    return ok;
}
bool audio_load(Audio* a,const char* manifest,const char* capture)
{
    JsonDocument doc;if (!json_load(&doc,manifest)) return false;Json root=json_root(&doc);
    json_object(root,"$schema|distance|events");a->distance=json_positive(json_get(root,"distance"),10000);
    Json events=json_get(root,"events");json_object(events,"order|growth|mature|birth|shot|hit|death|colonize|heal|win|lose|break");
    for (uint32_t i=0;i<SOUND_COUNT;++i)
    {
        Json j=json_get(events,sound_name(i));json_object(j,"file|gain|cooldown");char path[256]={};
        json_path(json_get(j,"file"),manifest,path);a->clips[i].gain=json_float(json_get(j,"gain"),0,1);
        a->clips[i].cooldown=json_float(json_get(j,"cooldown"),0,10);
        if (!doc.valid || !load_clip(&a->clips[i],path)) return false;
    }
    if (capture)
    {
        a->capture=fopen(capture,"wb");
        if (!a->capture || !wav_header(a->capture,0)) { fprintf(stderr,"Cannot create audio capture '%s'.\n",capture);return false; }
    }
    return doc.valid;
}
bool audio_open(Audio* a)
{
    WAVEFORMATEX format={WAVE_FORMAT_PCM,1,48000,96000,2,16,0};
    if (waveOutOpen(&a->device,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR)
    { a->device=0;fprintf(stderr,"Audio output unavailable; sound is muted.\n");return true; }
    for (uint32_t i=0;i<4;++i)
    {
        WAVEHDR* h=&a->headers[i];h->lpData=(LPSTR)a->buffers[i];h->dwBufferLength=sizeof(a->buffers[i]);
        if (waveOutPrepareHeader(a->device,h,sizeof(*h))!=MMSYSERR_NOERROR) return false;
    }
    printf("Audio output: mono 48 kHz PCM; 16 voices.\n");return true;
}
void audio_events(Audio* a,Simulation* s,Vec3 listener)
{
    for (uint32_t i=0;i<s->sounds.count;++i)
    {
        const SoundEvent& event=s->sounds.pending[i];uint32_t k=event.kind;
        if (a->cooldown[k]>0) continue;
        float d=std::sqrt(dot(subtract(event.position,listener),subtract(event.position,listener)));
        bool global=k==SOUND_ORDER || k==SOUND_WIN || k==SOUND_LOSE;
        float gain=a->clips[k].gain*(global ? 1 : std::fmax(0.0f,1-d/a->distance));if (gain<=0) continue;
        uint32_t slot=0;while (slot<16 && a->voices[slot].active) ++slot;
        if (slot==16) continue;
        a->voices[slot]=AudioVoice{k,0,gain,true};a->cooldown[k]=a->clips[k].cooldown;
    }
    s->sounds.count=0;
}
static void mix(Audio* a,int16_t* output,uint32_t count)
{
    for (uint32_t i=0;i<count;++i)
    {
        float sample=0;
        for (uint32_t j=0;j<16;++j) if (a->voices[j].active)
        {
            AudioVoice& v=a->voices[j];const AudioClip& c=a->clips[v.clip];sample+=c.samples[v.cursor++]*v.gain;
            if (v.cursor>=c.samples.size()) v.active=false;
        }
        output[i]=(int16_t)std::fmax(-30000.0f,std::fmin(30000.0f,sample));
    }
    for (uint32_t j=0;j<SOUND_COUNT;++j) a->cooldown[j]=std::fmax(0.0f,a->cooldown[j]-count/48000.0f);
}
bool audio_tick(Audio* a)
{
    if (!a->capture) return true;
    int16_t samples[800];mix(a,samples,800);
    if (a->captured>UINT32_MAX-36-sizeof(samples)) return false;
    a->captured+=sizeof(samples);a->valid=a->valid && fwrite(samples,sizeof(samples),1,a->capture)==1;return a->valid;
}
bool audio_pump(Audio* a)
{
    if (!a->device) return true;
    for (uint32_t i=0;i<4;++i) if (!(a->headers[i].dwFlags&WHDR_INQUEUE))
    {
        mix(a,a->buffers[i],1024);
        if (waveOutWrite(a->device,&a->headers[i],sizeof(WAVEHDR))!=MMSYSERR_NOERROR) return false;
    }
    return true;
}
bool audio_close(Audio* a)
{
    if (a->device)
    {
        if (waveOutReset(a->device)!=MMSYSERR_NOERROR) a->valid=false;
        for (uint32_t i=0;i<4;++i) if (a->headers[i].dwFlags&WHDR_PREPARED)
            if (waveOutUnprepareHeader(a->device,&a->headers[i],sizeof(WAVEHDR))!=MMSYSERR_NOERROR) a->valid=false;
        if (waveOutClose(a->device)!=MMSYSERR_NOERROR) a->valid=false;
        a->device=0;
    }
    if (a->capture)
    {
        if (fseek(a->capture,0,SEEK_SET)!=0 || !wav_header(a->capture,a->captured)) a->valid=false;
        if (fclose(a->capture)) a->valid=false;
        a->capture=0;
    }
    return a->valid;
}
