#!/usr/bin/env python3
"""Synthesize the authored PCM sound bank. No network or external samples."""
import json
import math
from pathlib import Path
import random
import struct
import wave

SAMPLE_RATE_HZ = 48000
NOISE_MEMORY_WEIGHT = .82
NOISE_INPUT_WEIGHT = .18
ATTACK_SECONDS = .008
RELEASE_SECONDS = .025
OVERTONE_GAIN = .24
OVERTONE_FREQUENCY_RATIO = 2.01
NOISE_GAIN = 3
MAX_SIGNAL_AMPLITUDE = .9
OUTPUT_GAIN = .52
PCM16_MAX_SAMPLE = 32767

root=Path(__file__).resolve().parents[1]/'assets/audio'
def unique(pairs):
    result={}
    for key,value in pairs:
        if key in result:raise ValueError('Duplicate synthesis field: '+key)
        result[key]=value
    return result
profiles=json.loads((root/'sound-synthesis.json').read_text(),object_pairs_hook=unique)
if set(profiles)-{'$schema','sounds'}:raise ValueError('Unknown synthesis fields')
bank=json.loads((root/'sound-playback.json').read_text(),object_pairs_hook=unique)
if set(profiles['sounds'])!=set(bank['events']):raise ValueError('Synthesis events must match the sound bank')
ranges={'seconds':(.03,2),'start_hz':(20,20000),'end_hz':(20,20000),'noise':(0,1),'decay':(0,12),'seed':(0,16777215)}
for name,p in profiles['sounds'].items():
    if set(p)!=set(ranges):raise ValueError(name+': incomplete or unknown synthesis fields')
    for key,(low,high) in ranges.items():
        value=p[key]
        if type(value) not in (int,float) or not math.isfinite(value) or not low<=value<=high:raise ValueError(name+': invalid '+key)
    if type(p['seed']) is not int:raise ValueError(name+': seed must be an integer')
for name,p in profiles['sounds'].items():
    rng=random.Random(p['seed']);samples=[];phase=0;filtered=0
    count=round(p['seconds']*SAMPLE_RATE_HZ)
    for i in range(count):
        t=i/SAMPLE_RATE_HZ;u=i/max(1,count-1)
        frequency=p['start_hz']*(p['end_hz']/p['start_hz'])**u
        phase+=math.tau*frequency/SAMPLE_RATE_HZ
        filtered=filtered*NOISE_MEMORY_WEIGHT+rng.uniform(-1,1)*NOISE_INPUT_WEIGHT
        attack=min(1,t/ATTACK_SECONDS);release=min(1,(count-1-i)/(SAMPLE_RATE_HZ*RELEASE_SECONDS))
        envelope=attack*release*math.exp(-p['decay']*u)
        signal=(math.sin(phase)+OVERTONE_GAIN*math.sin(phase*OVERTONE_FREQUENCY_RATIO))*(1-p['noise'])+filtered*p['noise']*NOISE_GAIN
        samples.append(round(max(-MAX_SIGNAL_AMPLITUDE,min(MAX_SIGNAL_AMPLITUDE,signal*envelope*OUTPUT_GAIN))*PCM16_MAX_SAMPLE))
    with wave.open(str(root/(name+'.wav')),'wb') as f:
        f.setnchannels(1);f.setsampwidth(2);f.setframerate(SAMPLE_RATE_HZ)
        f.writeframes(struct.pack('<'+'h'*len(samples),*samples))
    print(name,count,'samples')
