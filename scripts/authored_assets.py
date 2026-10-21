"""Read and stage JSON asset files for application runs."""
from copy import deepcopy
import json
import os
from pathlib import Path


FACTORY_SHARED_FIELDS=('growth_seconds', 'sprout_seconds', 'establishment_seconds', 'collection', 'height', 'width', 'repair', 'growth_plane')
FACTORY_SHARED_BRANCH_FIELDS=('seconds', 'seed', 'regrow_seconds', 'roots', 'root_limit', 'root_minimum_radius', 'additional_root_scale', 'surface_clearance', 'all_leaf_nodes', 'root_offset_seconds')


def stage_world(source, directory):
    def read(path):
        data=json.loads(path.read_text());data.pop('$schema',None)
        return data
    def model(data, key, origin):
        if key in data:
            data[key]=Path(os.path.relpath((origin/data[key]).resolve(),directory)).as_posix()
    world=read(source)
    manifest=(source.parent/world['content']).resolve()
    content={}
    for name,path in read(manifest).items():
        definition=(manifest.parent/path).resolve()
        document=read(definition);content[name]=document[name]
        if name=='buildings' and 'shared' in document:
            shared=document['shared']
            for i,item in enumerate(content[name]):
                merged=deepcopy(shared);merged.update(item)
                merged['branches']=dict(shared['branches'],**item['branches'])
                content[name][i]=merged
        if name=='units' and 'shared' in document:
            for item in content[name]:
                if 'secondary_attack' in item:item['secondary_attack']['interval_factor']=document['shared']['secondary_interval_factor']
        if name=='bodies' and 'shared' in document:
            for item in content[name]:
                if 'variation' in item:item['variation']=dict(document['shared'],**item['variation'])
        for item in content[name]:
            model(item,'model',definition.parent)
            if 'payload' in item:model(item['payload'],'model',definition.parent)
            if 'blob' in item.get('attack',{}):model(item['attack']['blob'],'model',definition.parent)
            if 'variation' in item:model(item['variation'],'parts',definition.parent)
            if 'growth_meshes' in item:
                for key in ('segment','tip'):model(item['growth_meshes'],key,definition.parent)
    world['content']=content
    for name in ('rules','appearance','effects'):
        if name not in world:continue
        path=(source.parent/world[name]).resolve();world[name]=read(path)
        if name=='appearance' and 'backdrop' in world[name]:
            backdrop=world[name]['backdrop'];model(backdrop,'model',path.parent)
            if 'variants' in backdrop:backdrop['variants']=[Path(os.path.relpath((path.parent/v).resolve(),directory)).as_posix() for v in backdrop['variants']]
        if name=='effects' and 'spikes' in world[name]:model(world[name]['spikes'],'model',path.parent)
    return world


def write_world(path, data):
    if isinstance(data,str):
        path.write_text(data);return
    world=deepcopy(data)
    for section in ('content','rules','appearance','effects'):
        if not isinstance(world.get(section),dict):continue
        document=world[section];filename=path.stem+'-'+section+'.json'
        if section=='content':
            manifest={}
            for name,items in document.items():
                target=path.stem+'-'+name+'.json';manifest[name]=target
                payload={name:items}
                if name=='buildings' and items:
                    first=items[0];shared={key:deepcopy(first[key]) for key in FACTORY_SHARED_FIELDS if key in first}
                    shared['branches']={key:deepcopy(first['branches'][key]) for key in FACTORY_SHARED_BRANCH_FIELDS if key in first['branches']}
                    for item in items:
                        for key in FACTORY_SHARED_FIELDS:item.pop(key,None)
                        for key in FACTORY_SHARED_BRANCH_FIELDS:item['branches'].pop(key,None)
                    payload['shared']=shared
                if name=='units':
                    factor=next((item['secondary_attack'].get('interval_factor',1) for item in items if 'secondary_attack' in item),1)
                    payload['shared']={'secondary_interval_factor':factor}
                    for item in items:
                        if 'secondary_attack' in item:item['secondary_attack'].pop('interval_factor',None)
                if name=='bodies':
                    variation=next((item['variation'] for item in items if 'variation' in item),{})
                    payload['shared']={key:deepcopy(value) for key,value in variation.items() if key!='parts'}
                    for item in items:
                        if 'variation' in item:item['variation']={key:value for key,value in item['variation'].items() if key=='parts'}
                (path.parent/target).write_text(json.dumps(payload,indent=2)+'\n')
            document=manifest
        (path.parent/filename).write_text(json.dumps(document,indent=2)+'\n')
        world[section]=filename
    path.write_text(json.dumps(world,indent=2)+'\n')


