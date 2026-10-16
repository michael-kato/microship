#!/usr/bin/env python3
"""Measure current-sight image preservation and fog detail in real application captures."""
import json
import math
from pathlib import Path
import re
import sys

out=Path(sys.argv[1]).resolve()
def ppm(path):
    data=path.read_bytes();header=re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s',data)
    assert header,'Invalid PPM header'
    width,height=map(int,header.groups());pixels=data[header.end():]
    assert len(pixels)==width*height*3
    return width,height,pixels
w,h,blur=ppm(out/'fog-wide.ppm');a,b,detail=ppm(out/'fog-minimal.ppm');assert (w,h)==(a,b)==(1920,1080)
scene=json.loads((out/'fog-wide-scene.json').read_text());spheres=scene['fog']['sight'];feather=scene['fog']['sight_feather']
log=(out/'fog-wide.log').read_text();match=re.search(r'Initial camera: ([^;]+); target: ([^\n]+)\.\n',log)
eye=list(map(float,match[1].split()));target=list(map(float,match[2].split()))
def norm(v):
    d=math.sqrt(sum(x*x for x in v));return [x/d for x in v]
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
forward=norm([a-b for a,b in zip(target,eye)]);right=norm(cross(forward,[0,1,0]));up=cross(right,forward)
fov=math.tan(math.radians(45/2))
def color(data,x,y):
    k=3*(y*w+x);return data[k:k+3]
def gradient(data,x,y):
    center=color(data,x,y);other=color(data,x+1,y)
    return sum(abs(a-b)*weight for a,b,weight in zip(center,other,(.2126,.7152,.0722)))
inside=outside=changed_inside=0;strong=weak=0.0
for y in range(0,h-164,4):
    for x in range(0,w-1,4):
        nx=(2*(x+.5)/w-1)*fov*w/h;ny=(1-2*(y+.5)/h)*fov
        direction=[f+nx*r+ny*u for f,r,u in zip(forward,right,up)]
        if abs(direction[2])<1e-6:continue
        distance=(scene['fog']['plane_z']-eye[2])/direction[2]
        if distance<=0:continue
        point=[o+distance*d for o,d in zip(eye,direction)]
        nearest=min((math.dist(point,s[:3])-s[3] for s in spheres),default=1e30)
        if nearest < -feather-.2:
            inside+=1;changed_inside+=color(blur,x,y)!=color(detail,x,y)
        elif nearest>1:
            outside+=1;strong+=gradient(blur,x,y);weak+=gradient(detail,x,y)
assert inside>100 and outside>100,(inside,outside)
assert changed_inside==0,('Fog changed pixels inside current sight',changed_inside,inside)
ratio=strong/max(weak,1e-9)
report=dict(sample_stride_pixels=4,inside_samples=inside,outside_samples=outside,changed_inside_samples=changed_inside,
            outside_mean_horizontal_gradient_blurred=strong/outside,outside_mean_horizontal_gradient_minimal=weak/outside,
            outside_gradient_ratio=ratio)
(out/'fog-measurement.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
assert ratio<.6,'Fog did not remove at least forty percent of sampled outside-sight horizontal detail'
