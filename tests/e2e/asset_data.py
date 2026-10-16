"""Change named asset fields for complete application scenarios."""
from copy import deepcopy
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from authored_assets import stage_world, write_world


def established_world(source,directory):
    """Stage the prior axial factory fixture. Current startup and tree checks use stage_world."""
    data=stage_world(source,directory)
    for start in data['match'].pop('starts',[]):data['bodies'][start['cell']]['owner']=start['faction']
    for building in data['content']['buildings']:
        if building['branches']['grammar']=='tree':
            building['branches'].update(grammar='axial',count=24,production=.05)
            building['branches'].pop('tree',None)
    return data


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
    emitters={f['name'] for f in result['content']['factions'] if f.get('production',{}).get('mode')=='spores'}
    for building in result['content']['buildings']:
        if building['faction'] not in emitters:
            building['branches'].update(settings)
            if 'count' in settings:
                building['branches']['roots']='single';building['branches']['grammar']='axial';building['branches'].pop('tree',None)
    return result
