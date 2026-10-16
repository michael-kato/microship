"""Author vertex-colored cellular meshes and tissue backgrounds with local Blender.

blender --background --factory-startup -noaudio --python scripts/make_biology.py -- assets/models
Editable profiles/.blend stay in models/source; only .fmesh exports are bundled.
"""
import bpy
import json
import math
import os
from mathutils import Matrix,Vector
from pathlib import Path
import random
import struct
import sys

out=Path(sys.argv[sys.argv.index('--')+1]).resolve()
profiles=json.loads((out/'source/biology_profiles.json').read_text())
rng=random.Random(profiles['seed'])
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
parts=[];models=[]
component=0;components=[]

def cell_part(pivot,movable=True,attachment=None,minimum_scale=0):
    global component
    component=len(components)
    components.append(dict(pivot=list(pivot),movable=movable,attachment=attachment,minimum_scale=minimum_scale))

def finish(obj,color,roughness=.82,ior=1.35,smooth=True,material=0):
    obj.data.transform(Matrix.LocRotScale(obj.location,obj.rotation_euler.to_quaternion(),obj.scale))
    obj.matrix_world=Matrix.Identity(4);mesh=obj.data
    colors=mesh.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='POINT')
    for v in mesh.vertices:
        rgb=color(v.co) if callable(color) else color
        colors.data[v.index].color=(*rgb,1)
    for name,value in [('roughness',roughness),('ior',ior)]:
        a=mesh.attributes.new(name=name,type='FLOAT',domain='POINT')
        for entry in a.data:entry.value=value
    tag=mesh.attributes.new(name="surface",type="INT",domain="FACE")
    for entry in tag.data:entry.value=material
    tag=mesh.attributes.new(name="component",type="INT",domain="POINT")
    for entry in tag.data:entry.value=component
    for poly in mesh.polygons:poly.use_smooth=smooth
    mesh.update();parts.append(obj);return obj

def sphere(center,size,color,sub=2,roughness=.82,ior=1.35):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=sub,radius=1,location=center)
    obj=bpy.context.object;obj.scale=size
    return finish(obj,color,roughness,ior)

def rod(a,b,radius,color):
    a,b=Vector(a),Vector(b);delta=b-a
    bpy.ops.mesh.primitive_cone_add(vertices=8,radius1=radius,radius2=radius*.55,depth=delta.length,location=(a+b)/2)
    obj=bpy.context.object;obj.rotation_euler=delta.to_track_quat('Z','Y').to_euler()
    return finish(obj,color)

def branches(start,direction,length,radius,depth,color,planar=False,buds=None):
    p=profiles['fractal'];start=Vector(start);direction=Vector(direction).normalized();end=start+direction*length
    rod(start,end,radius,color)
    if depth==0:
        if buds:sphere(end,(radius*3,)*3,buds,1,.65)
        return
    axis=Vector((0,0,1)) if planar else Vector((.37,.81,.45))
    side=direction.cross(axis)
    if side.length<.01:side=direction.cross(Vector((1,0,0)))
    side.normalize()
    for sign in (-1,1):
        jitter=rng.uniform(-p['jitter'],p['jitter'])
        child=direction+side*(sign*p['spread']+jitter)
        if not planar:child+=axis*rng.uniform(-p['jitter'],p['jitter'])
        branches(end,child,length*p['scale'],radius*p['radius_scale'],depth-1,color,planar,buds)

def export(name,anatomy=False):
    global parts
    bpy.ops.object.select_all(action='DESELECT')
    for obj in parts:obj.select_set(True)
    bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join()
    obj=bpy.context.object;obj.name=name;mesh=obj.data;mesh.calc_loop_triangles()
    colors=mesh.color_attributes['Color'].data;rough=mesh.attributes['roughness'].data;ior=mesh.attributes['ior'].data
    records=[];indices=[];lookup={};groups=[]
    for triangle in mesh.loop_triangles:
        row=[]
        for vi,li in zip(triangle.vertices,triangle.loops):
            normal=mesh.corner_normals[li].vector;key=(vi,*normal)
            if key not in lookup:
                lookup[key]=len(records)
                records.append((*mesh.vertices[vi].co,ior[vi].value,*normal,0,*colors[vi].color[:3],rough[vi].value))
                groups.append(mesh.attributes['component'].data[vi].value)
            row.append(lookup[key])
        indices.append((*row,mesh.attributes["surface"].data[triangle.polygon_index].value))
    if anatomy:
        order=sorted(range(len(records)),key=lambda i:groups[i]);remap={old:new for new,old in enumerate(order)}
        records=[records[i] for i in order]
        indices=[(*(remap[i] for i in row[:3]),row[3]) for row in indices]
        definitions=[];first=0
        for index,part in enumerate(components):
            count=groups.count(index)
            if not count:raise RuntimeError('empty mesh part')
            d=dict(first=first,count=count,pivot=part['pivot'],movable=part['movable'])
            if part['attachment'] is not None:d['attachment']=list(part['attachment'])
            if part['minimum_scale']:d['minimum_scale']=part['minimum_scale']
            definitions.append(d);first+=count
        (out/(name+'.parts.json')).write_text(json.dumps({'$schema':'../schemas/mesh-parts.schema.json','vertices':len(records),'parts':definitions},indent=2)+'\n')
    if any(abs(x)>1.49 for row in records for x in row[:3]):raise RuntimeError(name+' exceeds local mesh bounds')
    with (out/(name+'.fmesh')).open('wb') as f:
        f.write(struct.pack('<8sII',b'FLMESH01',len(records),len(indices)))
        for row in records:f.write(struct.pack('<12f',*row))
        for row in indices:f.write(struct.pack('<4I',*row))
    obj.location=((len(models)%4)*3.5,0,(len(models)//4)*3.5)
    models.append(obj);parts=[];print(name,len(records),'vertices',len(indices),'triangles',flush=True)

def cell(name,key):
    global components
    components=[];cell_part((0,0,0),False)
    p=profiles[key]
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=5,radius=1)
    obj=bpy.context.object
    for v in obj.data.vertices:
        n=v.co.normalized();x,y,z=n
        noise=.035*math.sin(8*x+3*y)*math.sin(7*z-2*y)+.018*math.sin(17*x-5*z)
        v.co=n*(1+noise);v.co.y*=.93
    def pigment(v):
        n=v.normalized();wave=.5+.5*math.sin(5*n.x+3*n.y)*math.sin(5*n.z-2*n.y)
        return tuple(p['membrane'][i]*(1-.23*wave)+p['rim'][i]*(.23*wave) for i in range(3))
    finish(obj,pigment,.38,ior=1.38,material=3)
    inner=profiles['interior']
    center=Vector(inner['nucleus_center'])
    cell_part(center,attachment=center+center.normalized()*inner['nucleus_size'][0])
    sphere(inner['nucleus_center'],inner['nucleus_size'],p['nucleus'],3,.42)
    sphere(inner['nucleolus_center'],inner['nucleolus_size'],p['vesicle'],2,.4)
    for index,center in enumerate(inner['mitochondria']):
        anchor=Vector(center)+Vector(center).normalized()*inner['mitochondrion_size'][1]
        cell_part(center,attachment=anchor,minimum_scale=inner['mitochondrion_minimum_radius'][index])
        sphere(center,inner['mitochondrion_size'],inner['mitochondrion_color'],2,.52)
        # Mitochondrial cristae are geometry rather than painted lines.
        for j in range(3):
            c=Vector(center)+Vector(((j-1)*.07,0,.085))
            sphere(c,(.018,.063,.018),p['nucleus'],1,.6)
    for index,center in enumerate(inner['vesicle_centers']):
        center=Vector(center)
        cell_part(center,attachment=center+center.normalized()*.065,minimum_scale=inner['vesicle_minimum_radius'][index])
        sphere(center,(.065,.055,.06),p['vesicle'],2,.44)
    # Curved endoplasmic reticulum surrounding the nucleus, inside the shell.
    cell_part((0,0,0))
    for ring in range(3):
        previous=None
        for j in range(25):
            angle=j*math.tau/24;radius=inner['reticulum_radius']+ring*.06
            point=(radius*math.cos(angle),radius*.7*math.sin(angle),-.15+ring*.095+.04*math.sin(angle*3))
            if previous:rod(previous,point,inner['reticulum_tube'],inner['reticulum_color'])
            previous=point
    substrate=profiles['substrate']
    cell_part((0,0,0))
    for i in range(substrate['roots']):
        angle=i*2.39996323;y=1-2*(i+.5)/substrate['roots'];r=math.sqrt(1-y*y)
        direction=Vector((r*math.cos(angle),y,r*math.sin(angle)))
        branches(direction*.12,direction,substrate['length'],substrate['radius'],substrate['depth'],substrate['color'])
    export(name,True)

def virus(large):
    p=profiles['virus']
    if not large:
        sphere((0,0,0),(.58,.58,.58),p['body'],2,.8)
        for i in range(6):
            a=i*math.tau/6;d=Vector((math.cos(a),math.sin(a),.2))
            branches(d*.45,d,.10,.012,2,p['spikes'])
        # Detachable spikes are instances of virus_spike.fmesh at runtime.
    else:
        # A broad capsid with paired payload sacs and repeated forked ribs.
        sphere((0,0,0),(.42,.3,.78),p['body'],2)
        for side in (-1,1):
            for j in range(3):
                z=(j-1)*.4;sphere((side*.34,-.1,z),(.19,.18,.2),p['core'],1,.48)
                branches((side*.25,.05,z),(side,.25,.12),.22,.035,3,p['spikes'],buds=p['body'])
    export('virus_'+('bomber' if large else 'virion'))

def immune(large):
    p=profiles['immune'];radius=.70 if large else .59
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3,radius=1)
    obj=bpy.context.object
    for v in obj.data.vertices:
        n=v.co.normalized();x,y,z=n;v.co=n*radius*(1+.09*math.sin(7*x+2*y)*math.sin(6*z-y))
        if large:v.co.x*=1.12
    finish(obj,p['body'],.86)
    for i in range(3 if not large else 2):
        sphere(((i-1)*.18,.10,.51),(.19,.22,.12),p['nucleus'],2)
    for i in range(7 if large else 4):
        a=i*math.tau/(7 if large else 4)
        sphere((math.cos(a)*radius*.8,-.2,math.sin(a)*radius*.8),(.20,.17,.22),p['body'],2)
    for i in range(10):
        a=i*2.39996323;sphere((.38*math.cos(a),.32*math.sin(a),.49),(.04,.04,.04),p['granules'],1)
    for i in range(6 if large else 4):
        a=i*math.tau/(6 if large else 4);d=Vector((math.cos(a),math.sin(a),.1))
        branches(d*radius*.8,d,.13 if large else .09,.022,2,p['body'],buds=p['granules'])
    export('immune_'+('bomber' if large else 'leukocyte'))

def node():
    p=profiles['node'];sphere((0,.12,0),(.24,.16,.24),p['stem'],2)
    for i in range(4):
        y=.25+i*.16;r=.18+.05*math.sin(i)
        sphere((.04*math.sin(i*2),y,0),(r,.17,r),p['membrane'],2)
    for i in range(5):
        a=i*math.tau/5;sphere((.25*math.cos(a),.82,.25*math.sin(a)),(.14,.14,.14),p['buds'],2)
    sphere((0,.87,0),(.20,.17,.20),p['membrane'],2)
    export('cell_replication_node')

def spore():
    p=profiles['fungal_spore'];sphere((0,0,0),(.48,.48,.48),p['body'],3,.45)
    for i in range(9):
        a=i*2.39996323;y=1-2*(i+.5)/9;r=math.sqrt(1-y*y);d=Vector((r*math.cos(a),y,r*math.sin(a)))
        branches(d*.4,d,.18,.045,2,p['rays'],buds=p['core'])
    export('fungal_spore')

def factory(side):
    global parts
    saved=rng.getstate()
    p=profiles[side];f=profiles['factories'][side]
    if side=='virus':
        # Repeated sheath rings, an angular capsid, and six bent tail fibres.
        for i in range(f['rings']):
            y=(i+.5)/f['rings']
            sphere((0,y,0),(f['sheath_radius'],.065,f['sheath_radius']),p['spikes'],1,.5)
        rod((0,0,0),(0,1,0),f['shaft_radius'],p['body'])
        export('virus_stem')
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1,radius=1)
        obj=bpy.context.object;obj.scale=(f['head_radius'],f['head_radius']*1.25,f['head_radius'])
        finish(obj,p['body'],.48,smooth=False)
        for i in range(6):
            a=i*math.tau/6;d=Vector((math.cos(a),0,math.sin(a)))
            bend=d*f['fibre_spread']+Vector((0,-.32,0));end=d*f['fibre_spread']*1.35+Vector((0,-.65,0))
            rod((0,-.12,0),bend,.025,p['spikes']);rod(bend,end,.018,p['core'])
        export('virus_head')
    elif side=='immune':
        rod((0,0,0),(0,1,0),f['shaft_radius'],p['body'])
        for i in range(f['lobes']):
            y=(i+.5)/f['lobes'];sign=-1 if i%2 else 1
            sphere((sign*.09,y,0),(.18,.23,.14),p['body'],2,.8)
            branches((0,y,0),(sign,.35,.15),f['spread'],.025,2,p['body'],buds=p['granules'])
        export('immune_stem')
        for i in range(f['crown_lobes']):
            a=i*math.tau/f['crown_lobes']
            sphere((.18*math.cos(a),.08,.18*math.sin(a)),(.22,.25,.20),p['body'],2,.75)
        sphere((0,.10,.12),(.14,.17,.12),p['granules'],2,.55)
        export('immune_head')
    else:
        rod((0,0,0),(0,1,0),f['shaft_radius'],p['rays'])
        for i in range(f['forks']):
            a=i*2.39996323;y=(i+.5)/f['forks']
            branches((0,y,0),(math.cos(a),.7,math.sin(a)),f['spread'],.025,2,p['body'],buds=p['core'])
        export('fungal_stem')
        sphere((0,-.10,0),(.14,.28,.14),p['rays'],2)
        sphere((0,.14,0),(f['cap_radius'],.16,f['cap_radius']),p['body'],2,.6)
        for i in range(f['spores']):
            a=i*2.39996323
            sphere((.28*math.cos(a),.25,.28*math.sin(a)),(.065,)*3,p['core'],1,.5)
        export('fungal_head')
    # The complete model is used by the non-staged presentation path.
    for i,source in enumerate(models[-2:]):
        obj=bpy.data.objects.new('complete',source.data.copy());bpy.context.collection.objects.link(obj)
        if i:obj.data.transform(Matrix.Translation((0,1,0)))
        parts.append(obj)
    export(('fungal' if side=='fungal_spore' else side)+'_factory')
    rng.setstate(saved)

def background(name,key):
    p=profiles[key];vertices=[];faces=[];nx,ny=64,40
    for j in range(ny+1):
        y=(j/ny*2-1)*1.46
        for i in range(nx+1):
            x=(i/nx*2-1)*1.48
            z=.018*math.sin(11*x+2*math.sin(5*y))+.013*math.cos(13*y-3*x)+.006*math.sin(29*x+17*y)
            vertices.append((x,y,z))
    for j in range(ny):
        for i in range(nx):
            a=j*(nx+1)+i;faces.extend([(a,a+1,a+nx+2),(a,a+nx+2,a+nx+1)])
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(vertices,[],faces);mesh.update()
    obj=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(obj)
    def pigment(v):
        # Broad cytoplasm gradients and membrane/fiber folds, all baked to vertices.
        x,y,z=v;f=.5+.5*math.sin(11*x+2*math.sin(5*y))*math.cos(13*y-3*x)
        return tuple(p['base'][i]*(1-.55*f)+p['ridge'][i]*.55*f for i in range(3))
    finish(obj,pigment,1,1)
    for j in range(7):
        y=-.72+j*.23
        last=None
        for i in range(20):
            x=-1.38+i*2.76/19;point=(x,y+.025*math.sin(x*8+j),.025+.015*math.cos(x*7+j))
            if last:rod(last,point,.007,p['fiber'])
            last=point
    # Blood cells and small extracellular vesicles occupy depth behind the play field.
    for i in range(18):
        x=rng.uniform(-1.2,1.2);y=rng.uniform(-.7,.7);z=rng.uniform(.08,.38)
        if abs(x)<.46 and abs(y)<.22:continue
        radius=rng.uniform(.025,.05)
        sphere((x,y,z),(radius,radius*.72,radius*.32),p['blood'],2,1,1)
    for i in range(26):
        x=rng.uniform(-1.2,1.2);y=rng.uniform(-.73,.73);z=rng.uniform(.02,.28)
        sphere((x,y,z),(.006,.006,.006),p['ridge'],1,1,1)
    export(name)

cell('cell_infected','infected_cell');cell('cell_healthy','healthy_cell');cell('cell_neutral','neutral_cell')
virus(False);virus(True);immune(False);immune(True);node();factory('virus');factory('immune');factory('fungal_spore')
spore()
# A single tapered local +Y segment. Runtime poses extend segments in sequence.
rod((0,0,0),(0,1,0),.065,(1,1,1));export('growth_segment')
sphere((0,0,0),(.12,.12,.12),(1,1,1),2,.55);export('growth_tip')
# One reusable shaft and head, local +Z; attached, flying and embedded use this mesh.
rod((0,0,0),(0,0,.87),.075,(1,1,1));sphere((0,0,.91),(.14,.14,.09),(1,1,1),1,.45)
export('virus_spike')
rng=random.Random(profiles['seed'])
background('tissue_background','tissue');background('bloodstream_background','bloodstream')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(out/'source/biology.blend'),compress=True)
print('Saved source/biology.blend',flush=True)
# All exports are closed. Avoid the sandboxed Blender audio teardown hang.
os._exit(0)
