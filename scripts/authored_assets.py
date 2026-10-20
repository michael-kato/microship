"""Read and stage JSON asset files for application runs."""
from copy import deepcopy
import json
import os
from pathlib import Path


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
        content[name]=read(definition)[name]
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
                (path.parent/target).write_text(json.dumps({name:items},indent=2)+'\n')
            document=manifest
        (path.parent/filename).write_text(json.dumps(document,indent=2)+'\n')
        world[section]=filename
    path.write_text(json.dumps(world,indent=2)+'\n')


