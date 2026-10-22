#!/usr/bin/env python3
"""Synthesize the authored PCM sound banks without external samples."""
import json
import math
from pathlib import Path
import random
import struct
import wave

SAMPLE_RATE_HZ=48000
NOISE_MEMORY_WEIGHT=.82
NOISE_INPUT_WEIGHT=.18
ATTACK_SECONDS=.008
RELEASE_SECONDS=.025
OVERTONE_GAIN=.24
OVERTONE_FREQUENCY_RATIO=2.01
NOISE_GAIN=3
MAX_SIGNAL_AMPLITUDE=.9
OUTPUT_GAIN=.52
PCM16_MAX_SAMPLE=32767
ROOT=Path(__file__).resolve().parents[1]/'assets/audio'

def unique(pairs):
    result={}
    for key,value in pairs:
        if key in result:raise ValueError('Duplicate synthesis field: '+key)
        result[key]=value
    return result

profiles=json.loads((ROOT/'sound-synthesis.json').read_text(),object_pairs_hook=unique)
bank=json.loads((ROOT/'audio.json').read_text(),object_pairs_hook=unique)
if set(profiles)-{'$schema','sounds','banks'}:raise ValueError('Unknown synthesis fields')
ranges={'seconds':(.005,2),'start_hz':(20,20000),'end_hz':(20,20000),'noise':(0,1),'decay':(0,12),'seed':(0,16777215)}
optional={'attack_seconds':(.0001,.1),'release_seconds':(.0001,.1)}

def generate(name,p,path):
    if set(p)-set(ranges)-set(optional)-{'texture'} or set(ranges)-set(p):raise ValueError(name+': incomplete or unknown synthesis fields')
    for key,(low,high) in dict(ranges,**optional).items():
        if key not in p:continue
        value=p[key]
        if type(value) not in (int,float) or not math.isfinite(value) or not low<=value<=high:raise ValueError(name+': invalid '+key)
    if type(p['seed']) is not int:raise ValueError(name+': seed must be an integer')
    texture=p.get('texture','sine')
    if texture not in ('sine','fracture','command','spores'):raise ValueError(name+': invalid texture')
    rng=random.Random(p['seed']);samples=[];phase=0;filtered=0;count=round(p['seconds']*SAMPLE_RATE_HZ)
    for i in range(count):
        t=i/SAMPLE_RATE_HZ;u=i/max(1,count-1)
        frequency=p['start_hz']*(p['end_hz']/p['start_hz'])**u
        if texture=='spores':frequency*=1+.22*math.sin(math.tau*7*t+.6*math.sin(math.tau*2*t))
        phase+=math.tau*frequency/SAMPLE_RATE_HZ
        raw=rng.uniform(-1,1);filtered=filtered*NOISE_MEMORY_WEIGHT+raw*NOISE_INPUT_WEIGHT
        attack=min(1,t/p.get('attack_seconds',ATTACK_SECONDS));release=min(1,(count-1-i)/(SAMPLE_RATE_HZ*p.get('release_seconds',RELEASE_SECONDS)))
        envelope=attack*release*math.exp(-p['decay']*u)
        if texture=='fracture':
            gate=(.5+.5*math.sin(math.tau*83*t))**3
            signal=(math.sin(phase)+.23*math.sin(phase*2.87))*gate*(1-p['noise'])+(raw-filtered)*p['noise']*.65
        elif texture=='command':
            gate=max(0,min(1,(.34-u)*80,u*80))+max(0,min(1,(u-.48)*80,(.89-u)*80))
            signal=(math.sin(phase)+.16*math.sin(phase*3))*gate*(1-p['noise'])+filtered*p['noise']
        elif texture=='spores':
            puff=(.5+.5*math.sin(math.tau*19*t+.7*math.sin(math.tau*3*t)))**4
            signal=(math.sin(phase+filtered*3)*(1-p['noise'])+filtered*p['noise']*4)*(.2+.8*puff)
        else:signal=(math.sin(phase)+OVERTONE_GAIN*math.sin(phase*OVERTONE_FREQUENCY_RATIO))*(1-p['noise'])+filtered*p['noise']*NOISE_GAIN
        samples.append(round(max(-MAX_SIGNAL_AMPLITUDE,min(MAX_SIGNAL_AMPLITUDE,signal*envelope*OUTPUT_GAIN))*PCM16_MAX_SAMPLE))
    with wave.open(str(path),'wb') as f:
        f.setnchannels(1);f.setsampwidth(2);f.setframerate(SAMPLE_RATE_HZ);f.writeframes(struct.pack('<'+'h'*len(samples),*samples))
    print(name,count,'samples')

def generate_bank(sounds,events,prefix):
    if set(sounds)!=set(events):raise ValueError(prefix+': synthesis events must match the sound bank')
    for name,p in sounds.items():
        path=(ROOT/events[name]['file']).resolve()
        if path.parent!=ROOT.resolve():raise ValueError('Generated sound must remain in assets/audio')
        generate(prefix+name,p,path)

generate_bank(profiles['sounds'],bank['events'],'')
authored={f['name']:f for f in profiles.get('banks',[])}
if len(authored)!=len(profiles.get('banks',[])):raise ValueError('Duplicate faction synthesis bank')
if set(authored)!={f['name'] for f in bank.get('banks',[])}:raise ValueError('Faction synthesis banks do not match playback')
for faction in bank.get('banks',[]):
    source=authored[faction['name']]
    if set(source)!={'name','sounds'}:raise ValueError('Unknown faction synthesis fields')
    generate_bank(source['sounds'],faction['events'],source['name']+'_')
