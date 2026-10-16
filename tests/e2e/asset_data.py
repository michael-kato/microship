"""Change named asset fields for complete application scenarios."""
from copy import deepcopy
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from authored_assets import stage_world, write_world


def change(data,path,value):
    result=deepcopy(data);parent=result;parts=path.split('.')
    for key in parts[:-1]:parent=parent[int(key) if isinstance(parent,list) else key]
    key=int(parts[-1]) if isinstance(parent,list) else parts[-1]
    parent[key]=deepcopy(value)
    return result


def remove(data,path):
    result=deepcopy(data);parent=result;parts=path.split('.')
    for key in parts[:-1]:parent=parent[int(key) if isinstance(parent,list) else key]
    del parent[int(parts[-1]) if isinstance(parent,list) else parts[-1]]
    return result


def units(data,group=None,**settings):
    result=deepcopy(data)
    aliases={'shot':'shot_interval','spacing':'separation','colonize':'colonizes','detach':'detaches','pick':'pick_radius','cue':'cue_radius','yaw':'yaw_degrees'}
    for unit in result['content']['units']:
        if group is not None and unit['group']!=group:continue
        for key,value in settings.items():unit[aliases.get(key,key)]=value
    return result


def producers(data,**settings):
    result=deepcopy(data)
    for building in result['content']['buildings']:
        for key,value in settings.items():building['growth_seconds' if key=='growth' else key]=value
    return result


def branches(data,**settings):
    result=deepcopy(data)
    for building in result['content']['buildings']:building['branches'].update(settings)
    return result
