"""Create a few coarse vertex-colored organs/membranes, with no image textures."""
import bpy
import json
import math
import os
from pathlib import Path
import struct
import sys

DEFORMATION_X_FREQUENCY = 4
DEFORMATION_Y_FREQUENCY = 5
SHADE_MIN = .68
SHADE_RANGE = .32
SHADE_X_FREQUENCY = 2
SHADE_Y_FREQUENCY = 3
MAX_LOCAL_COORDINATE = 1.49
MEMBRANE_IOR = 1.35
SOURCE_PREVIEW_SPACING = 4

out=Path(sys.argv[sys.argv.index('--')+1]).resolve()
profile=json.loads((out/'source/background-shape.json').read_text())
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
for scene_index,key in enumerate(('tissue','bloodstream')):
    records=[];triangles=[];parts=[]
    for index,form in enumerate(profile['forms']):
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=profile['subdivisions'],radius=1)
        obj=bpy.context.object;obj.name=key+'-membrane-'+str(index);parts.append(obj)
        mesh=obj.data;colors=mesh.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='POINT')
        for v in mesh.vertices:
            n=v.co.normalized();x,y,z=n
            bulge=1+profile['deformation']*math.sin(DEFORMATION_X_FREQUENCY*x+index)*math.cos(DEFORMATION_Y_FREQUENCY*y-2*z)
            for axis in range(3):v.co[axis]=form['center'][axis]+n[axis]*form['size'][axis]*bulge
            shade=SHADE_MIN+SHADE_RANGE*(.5+.5*math.sin(SHADE_X_FREQUENCY*x+SHADE_Y_FREQUENCY*y+index))
            rgb=profile[key][form['color']]
            colors.data[v.index].color=(*(c*shade for c in rgb),1)
        mesh.update();mesh.calc_loop_triangles()
        for poly in mesh.polygons:poly.use_smooth=True
        offset=len(records)
        for v in mesh.vertices:
            if any(abs(c)>MAX_LOCAL_COORDINATE for c in v.co):raise RuntimeError('Background exceeds authoring bounds')
            records.append((*v.co,MEMBRANE_IOR,*v.normal,0,*colors.data[v.index].color[:3],1))
        triangles.extend((*[offset+i for i in t.vertices],0) for t in mesh.loop_triangles)
        # Editable source presents the two settings next to one another.
        obj.location.x=scene_index*SOURCE_PREVIEW_SPACING
    path=out/('soft_'+key+'_background.fmesh')
    with path.open('wb') as f:
        f.write(struct.pack('<8sII',b'FLMESH01',len(records),len(triangles)))
        for vertex in records:f.write(struct.pack('<12f',*vertex))
        for triangle in triangles:f.write(struct.pack('<4I',*triangle))
    print(path.name,len(records),'vertices',len(triangles),'triangles',flush=True)
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(out/'source/soft_backgrounds.blend'),compress=True)
sys.stdout.flush();os._exit(0)
