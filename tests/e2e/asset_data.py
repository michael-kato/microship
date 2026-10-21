"""Change named asset fields for complete application scenarios."""
from copy import deepcopy
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from authored_assets import stage_world, write_world


def established_world(source,directory):
    """Stage the prior axial factory fixture. Current startup and tree checks use stage_world."""
    data=stage_world(source,directory)
    data['rules']['economy'].pop('fighter_percent',None)
    for start in data['match'].pop('starts',[]):data['bodies'][start['cell']]['owner']=start['faction']
    for unit in data['content']['units']:
        if unit['name']=='fungal_spore':
            unit.pop('manufacture_seconds',None);unit.update(speed=4.2,cost=1.5,brood=16)
            unit['attack']['damage']=6;unit['attack']['cloud']['damage']=2
    for building in data['content']['buildings']:
        building.pop('repair',None);building.pop('haze',None)
        building['branches'].pop('root_interval_seconds',None)
        building['growth_plane'].pop('spread_roots',None)
        building['growth_plane']['depth_ratio']=.15
        building.update(establishment_seconds=180,sprout_seconds=3)
        building['branches']['root_offset_seconds']=2
        building['branches'].pop('root_limit',None);building['branches'].pop('root_minimum_radius',None)
        building['branches']['count']=max(16,building['branches']['count'])
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
                building['branches'].pop('root_limit',None);building['branches'].pop('root_minimum_radius',None)
                building['branches']['roots']='single';building['branches']['grammar']='axial';building['branches'].pop('tree',None)
    return result
