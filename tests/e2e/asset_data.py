"""Change named asset fields for complete application scenarios."""
from copy import deepcopy
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from authored_assets import stage_world, write_world


def established_world(source,directory):
    """Stage the prior axial balance fixture. Current-content checks use stage_world."""
    data=stage_world(source,directory)
    data['rules']['economy'].pop('fighter_percent',None)
    for start in data['match'].pop('starts',[]):data['bodies'][start['cell']]['owner']=start['faction']
    # Keep exact timing and damage fixtures independent of current faction tuning.
    data['rules']['spikes']['regrow_seconds']=5
    prior={
        'virus_fighter':dict(speed=2.8,health=36,hits=3,range=3.2,cost=12,shot_interval=1),
        'virus_bomber':dict(speed=2,health=60,hits=3,range=3.2,cost=30,shot_interval=3),
        'immune_fighter':dict(speed=2,health=432,hits=24,cost=28,shot_interval=.8),
        'immune_bomber':dict(speed=1.52,health=720,hits=24,cost=52,shot_interval=1.3),
        'swarm_fighter':dict(speed=3.84,health=24,hits=2,range=3.2,cost=7,shot_interval=.8),
        'swarm_bomber':dict(speed=2.8,health=40,hits=2,range=3.2,cost=18,shot_interval=2.4),
    }
    for unit in data['content']['units']:
        if unit['name'] in prior:
            unit.update(prior[unit['name']]);unit.pop('manufacture_seconds',None)
            if 'payload' in unit:unit['payload']['reload_seconds']=12
            if unit['name']=='immune_fighter':
                unit['attack']['blob'].update(range=3,interval=6)
                unit['attack']['aura'].update(radius=3,heal_interval=1.5)
        if unit['name']=='fungal_spore':
            unit.pop('manufacture_seconds',None);unit.update(speed=4.2,cost=1.5,brood=16)
            unit['attack']['damage']=6;unit['attack']['cloud']['damage']=2
    for building in data['content']['buildings']:
        immune=building['faction']=='immune'
        building.update(health=480 if immune else 240,hits=24 if immune else 12)
        building['branches'].update(health=300 if immune else 150,hits=16 if immune else 8)
        building.pop('repair',None);building.pop('haze',None)
        for key in ('root_height','spread_all_roots','surface_clearance','all_leaf_nodes','additional_root_scale','spread_scale','chain_length','production_leaf_limits','production_reference_count'):building['branches'].pop(key,None)
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
