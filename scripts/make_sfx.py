#!/usr/bin/env python3
"""Synthesize the authored PCM sound bank. No network or external samples."""
import json
import math
from pathlib import Path
import random
import struct
import wave

root=Path(__file__).resolve().parents[1]/'assets/audio'
def unique(pairs):
    result={}
    for key,value in pairs:
        if key in result:raise ValueError('Duplicate synthesis field: '+key)
        result[key]=value
    return result
profiles=json.loads((root/'synthesis.json').read_text(),object_pairs_hook=unique)
if set(profiles)-{'$schema','sounds'}:raise ValueError('Unknown synthesis fields')
bank=json.loads((root/'biology.json').read_text(),object_pairs_hook=unique)
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
    count=round(p['seconds']*48000)
    for i in range(count):
        t=i/48000;u=i/max(1,count-1)
        frequency=p['start_hz']*(p['end_hz']/p['start_hz'])**u
        phase+=math.tau*frequency/48000
        filtered=filtered*.82+rng.uniform(-1,1)*.18
        attack=min(1,t/.008);release=min(1,(count-1-i)/(48000*.025))
        envelope=attack*release*math.exp(-p['decay']*u)
        signal=(math.sin(phase)+.24*math.sin(phase*2.01))*(1-p['noise'])+filtered*p['noise']*3
        samples.append(round(max(-.9,min(.9,signal*envelope*.52))*32767))
    with wave.open(str(root/(name+'.wav')),'wb') as f:
        f.setnchannels(1);f.setsampwidth(2);f.setframerate(48000)
        f.writeframes(struct.pack('<'+'h'*len(samples),*samples))
    print(name,count,'samples')
