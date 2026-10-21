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

MAX_LOCAL_COORDINATE = 1.49
GOLDEN_ANGLE_RADIANS = 2.39996323
DEFAULT_ROUGHNESS = .82
DEFAULT_IOR = 1.35
MIN_MATERIAL_ROUGHNESS = .02
MIN_MATERIAL_FREQUENCY = .1
MAX_MATERIAL_FREQUENCY = 100
ROUGHNESS_PRIMARY_WEIGHT = .65
ROUGHNESS_Y_COUPLING = .71
ROUGHNESS_Z_FREQUENCY_SCALE = .83
ROUGHNESS_Y_PHASE_SCALE = .41
ROUGHNESS_SECONDARY_WEIGHT = .35
ROUGHNESS_SECONDARY_FREQUENCY_SCALE = .55
ROUGHNESS_SECONDARY_Y_COUPLING = .43
ROUGHNESS_SECONDARY_Z_COUPLING = .67
PIGMENT_Y_COUPLING = .51
PIGMENT_Z_COUPLING = .73
PIGMENT_PHASE = .9
PIGMENT_X_COUPLING = .67
ROD_SIDES = 8
ROD_TIP_RADIUS_SCALE = .55
FRACTAL_REFERENCE_AXIS = (.37,.81,.45)
BUD_ROUGHNESS = .65
MIN_BRANCH_AXIS_LENGTH = .01
SOURCE_PREVIEW_COLUMNS = 4
SOURCE_PREVIEW_SPACING = 3.5
CRISTAE_SIZE = (.018,.063,.018)
VESICLE_SIZE = (.065,.055,.06)
CELL_SHELL_SUBDIVISIONS = 5
CELL_PRIMARY_DISPLACEMENT = .035
CELL_FINE_DISPLACEMENT = .018
CELL_VERTICAL_SCALE = .93
CELL_RIM_PIGMENT_WEIGHT = .23
RETICULUM_SAMPLES = 25
RETICULUM_INTERVALS = 24
RETICULUM_RING_SPACING = .06
RETICULUM_Y_SCALE = .7
RETICULUM_Z_BASE = .15
RETICULUM_Z_RING_SPACING = .095
RETICULUM_Z_WAVE_AMPLITUDE = .04
SUBSTRATE_START_RADIUS = .12
CRISTAE_SPACING = .07
CRISTAE_DEPTH_OFFSET = .085
VESICLE_ATTACHMENT_RADIUS = .065
CELL_NOISE_X_FREQUENCY = 8
CELL_NOISE_Y_FREQUENCY = 3
CELL_NOISE_Z_FREQUENCY = 7
CELL_NOISE_Y_FINE_COUPLING = 2
CELL_FINE_NOISE_X_FREQUENCY = 17
CELL_PIGMENT_Y_FREQUENCY = 3
CELL_PIGMENT_Y_FINE_COUPLING = 2
RETICULUM_Z_WAVE_FREQUENCY = 3
BOMBER_CAPSID_SIZE = (.42,.3,.78)
BOMBER_PAYLOAD_SIZE = (.19,.18,.2)
VIRION_RADIUS = .58
VIRUS_FIBRE_COUNT = 6
VIRION_FIBRE_UP_COMPONENT = .2
VIRION_FIBRE_ROOT_RADIUS = .45
VIRION_FIBRE_LENGTH = .10
VIRION_FIBRE_RADIUS = .012
BOMBER_PAYLOAD_SPACING = .4
BOMBER_PAYLOAD_X_OFFSET = .34
BOMBER_PAYLOAD_Y_OFFSET = .1
BOMBER_RIB_ROOT_X_OFFSET = .25
BOMBER_RIB_ROOT_Y_OFFSET = .05
BOMBER_RIB_Z_DIRECTION = .12
BOMBER_RIB_LENGTH = .22
BOMBER_RIB_RADIUS = .035
IMMUNE_NUCLEUS_SIZE = (.19,.22,.12)
IMMUNE_LOBE_SIZE = (.20,.17,.22)
IMMUNE_GRANULE_SIZE = (.04,.04,.04)
IMMUNE_BOMBER_RADIUS = .70
IMMUNE_FIGHTER_RADIUS = .59
IMMUNE_SURFACE_DISPLACEMENT = .09
IMMUNE_SURFACE_X_FREQUENCY = 7
IMMUNE_SURFACE_Z_FREQUENCY = 6
IMMUNE_BOMBER_X_SCALE = 1.12
IMMUNE_NUCLEUS_SPACING = .18
IMMUNE_NUCLEUS_Y_OFFSET = .10
IMMUNE_NUCLEUS_Z_OFFSET = .51
IMMUNE_LOBE_Y_OFFSET = .2
IMMUNE_GRANULE_COUNT = 10
IMMUNE_GRANULE_X_RADIUS = .38
IMMUNE_GRANULE_Y_RADIUS = .32
IMMUNE_GRANULE_Z_OFFSET = .49
IMMUNE_BRANCH_UP_COMPONENT = .1
IMMUNE_BOMBER_BRANCH_LENGTH = .13
IMMUNE_BRANCH_RADIUS = .022
IMMUNE_FIGHTER_BRANCH_LENGTH = .09
NODE_BASE_SIZE = (.24,.16,.24)
NODE_BUD_SIZE = (.14,.14,.14)
NODE_CROWN_SIZE = (.20,.17,.20)
NODE_BASE_Y = .12
NODE_FIRST_RING_Y = .25
NODE_RING_SPACING = .16
NODE_RING_BASE_RADIUS = .18
NODE_RING_RADIUS_WAVE = .05
NODE_RING_X_WAVE = .04
NODE_RING_HALF_HEIGHT = .17
NODE_BUD_COUNT = 5
NODE_BUD_Y = .82
NODE_CROWN_Y = .87
SPORE_RADIUS = .48
SWARM_SPORE_RADIUS = .5
IMMUNE_BLOB_SIZE = (.5,.42,.46)
SPORE_RAY_COUNT = 9
SPORE_RAY_ROOT_RADIUS = .4
SPORE_RAY_LENGTH = .18
SPORE_RAY_RADIUS = .045
IMMUNE_STEM_LOBE_SIZE = (.18,.23,.14)
IMMUNE_CROWN_SIZE = (.22,.25,.20)
IMMUNE_FACTORY_GRANULE_SIZE = (.14,.17,.12)
FUNGAL_CAP_STALK_SIZE = (.14,.28,.14)
VIRUS_FACTORY_FIBRE_COUNT = 6
VIRUS_FIBRE_BEND_Y = .32
VIRUS_FIBRE_END_SPREAD_SCALE = 1.35
VIRUS_FIBRE_END_Y = .65
VIRUS_FIBRE_ROOT_Y = .12
FACTORY_BRANCH_RADIUS = .025
VIRUS_FIBRE_TIP_RADIUS = .018
IMMUNE_STEM_LOBE_X = .09
IMMUNE_FACTORY_BRANCH_UP_COMPONENT = .35
IMMUNE_FACTORY_BRANCH_DEPTH_COMPONENT = .15
IMMUNE_CROWN_RADIUS = .18
IMMUNE_CROWN_Y = .08
FACTORY_GRANULE_Y = .10
FUNGAL_BRANCH_UP_COMPONENT = .7
FUNGAL_CAP_Y = .14
FUNGAL_CAP_HALF_HEIGHT = .16
FUNGAL_SPORE_RING_RADIUS = .28
FUNGAL_SPORE_RING_Y = .25
BACKGROUND_GRID_X = 64
BACKGROUND_GRID_Y = 40
BACKGROUND_HALF_HEIGHT = 1.46
BACKGROUND_HALF_WIDTH = 1.48
BACKGROUND_PRIMARY_FOLD = .018
BACKGROUND_PRIMARY_X_FREQUENCY = 11
BACKGROUND_Y_MODULATION_FREQUENCY = 5
BACKGROUND_SECONDARY_FOLD = .013
BACKGROUND_SECONDARY_Y_FREQUENCY = 13
BACKGROUND_FINE_FOLD = .006
BACKGROUND_FINE_X_FREQUENCY = 29
BACKGROUND_FINE_Y_FREQUENCY = 17
BACKGROUND_RIDGE_BLEND = .55
BACKGROUND_FIBRE_ROWS = 7
BACKGROUND_FIBRE_START_Y = .72
BACKGROUND_FIBRE_ROW_SPACING = .23
BACKGROUND_FIBRE_SAMPLES = 20
BACKGROUND_FIBRE_HALF_WIDTH = 1.38
BACKGROUND_FIBRE_SPAN = 2.76
BACKGROUND_FIBRE_INTERVALS = 19
BACKGROUND_FIBRE_Y_WAVE = .025
BACKGROUND_FIBRE_Z_WAVE = .015
BACKGROUND_FIBRE_RADIUS = .007
BACKGROUND_BLOOD_COUNT = 18
BACKGROUND_BLOOD_HALF_HEIGHT = .7
BACKGROUND_BLOOD_NEAR_Z = .08
BACKGROUND_BLOOD_FAR_Z = .38
BACKGROUND_CLEAR_HALF_WIDTH = .46
BACKGROUND_CLEAR_HALF_HEIGHT = .22
BACKGROUND_BLOOD_MAX_RADIUS = .05
BACKGROUND_BLOOD_DEPTH_SCALE = .32
BACKGROUND_VESICLE_COUNT = 26
BACKGROUND_VESICLE_HALF_HEIGHT = .73
BACKGROUND_VESICLE_NEAR_Z = .02
BACKGROUND_VESICLE_FAR_Z = .28
BACKGROUND_FIBRE_Y_FREQUENCY = 8
BACKGROUND_FIBRE_Z_FREQUENCY = 7
SPIKE_HEAD_SIZE = (.14,.14,.09)
GROWTH_SEGMENT_RADIUS = .065
GROWTH_TIP_RADIUS = .12
GROWTH_TIP_ROUGHNESS = .55
SPIKE_SHAFT_LENGTH = .87
SPIKE_SHAFT_RADIUS = .075
SPIKE_HEAD_Z = .91
CELL_FINE_NOISE_Z_FREQUENCY = 5
CELL_PIGMENT_XZ_FREQUENCY = 5
IMMUNE_BOMBER_LOBE_COUNT = 7
IMMUNE_BOMBER_BRANCH_COUNT = 6
BOMBER_RIB_Y_DIRECTION = .25
NODE_BUD_RING_RADIUS = .25
IMMUNE_FACTORY_GRANULE_Z = .12
FUNGAL_FACTORY_SPORE_RADIUS = .065
FUNGAL_STALK_ROOT_Y = .10
BACKGROUND_FIBRE_BASE_Z = .025
BACKGROUND_BLOOD_MIN_RADIUS = .025
BACKGROUND_BLOOD_HEIGHT_SCALE = .72
BACKGROUND_VESICLE_RADIUS = .006
IMMUNE_SURFACE_ATTACHMENT_RADIUS_SCALE = .8
BACKGROUND_PARTICLE_HALF_WIDTH = 1.2

out=Path(sys.argv[sys.argv.index('--')+1]).resolve()
profiles=json.loads((out/'source/cell-unit-factory-shape.json').read_text())
def unique_fields(pairs):
    result={}
    for key,value in pairs:
        if key in result:raise ValueError('Duplicate material field: '+key)
        result[key]=value
    return result

materials=json.loads((out/'source/surface-materials.json').read_text(),object_pairs_hook=unique_fields)
if set(materials)!={'$schema','surfaces'}:raise ValueError('Invalid material document fields')
materials=materials['surfaces']
surface_names={'membrane','nucleus','nucleolus','mitochondrion','cristae','vesicle','reticulum','substrate',
               'virus_capsid','virus_sheath','virus_fibre','immune_surface','immune_granule',
               'fungal_stalk','fungal_cap','fungal_spore','spike'}
if set(materials)!=surface_names:raise ValueError('Incomplete or unknown material surfaces')
for name,m in materials.items():
    if set(m)!={'roughness_min','roughness_max','ior','frequency','pigment_low','pigment_high','subsurface'}:
        raise ValueError('Invalid material fields: '+name)
    def number(value,low,high):
        return type(value) in (int,float) and math.isfinite(value) and low<=value<=high
    if not (number(m['roughness_min'],MIN_MATERIAL_ROUGHNESS,1) and number(m['roughness_max'],m['roughness_min'],1)
            and number(m['ior'],1,3) and number(m['frequency'],MIN_MATERIAL_FREQUENCY,MAX_MATERIAL_FREQUENCY)):
        raise ValueError('Invalid material range: '+name)
    for key in ('pigment_low','pigment_high'):
        if not isinstance(m[key],list) or len(m[key])!=3 or not all(number(v,0,1) for v in m[key]):
            raise ValueError('Invalid material pigment: '+name)
    s=m['subsurface']
    if set(s)!={'weight','radius'} or not number(s['weight'],0,1) or len(s['radius'])!=3 or not all(number(v,.0001,10) for v in s['radius']):
        raise ValueError('Invalid subsurface profile: '+name)
rng=random.Random(profiles['seed'])
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
parts=[];models=[]
component=0;components=[]

def cell_part(pivot,movable=True,attachment=None,minimum_scale=0,anchor_origin=False):
    global component
    component=len(components)
    components.append(dict(pivot=list(pivot),movable=movable,attachment=attachment,minimum_scale=minimum_scale,anchor_origin=anchor_origin))

def finish(obj,color,roughness=DEFAULT_ROUGHNESS,ior=DEFAULT_IOR,smooth=True,material=0,surface=None,subsurface=None,emission=0):
    obj.data.transform(Matrix.LocRotScale(obj.location,obj.rotation_euler.to_quaternion(),obj.scale))
    obj.matrix_world=Matrix.Identity(4);mesh=obj.data
    colors=mesh.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='POINT')
    m=materials[surface] if surface else None
    scattering=materials[subsurface]['subsurface'] if subsurface else m['subsurface'] if m else None
    rough=mesh.attributes.new(name='roughness',type='FLOAT',domain='POINT')
    optical=mesh.attributes.new(name='ior',type='FLOAT',domain='POINT')
    glow=mesh.attributes.new(name='emission',type='FLOAT',domain='POINT')
    sss=mesh.color_attributes.new(name='Subsurface',type='FLOAT_COLOR',domain='POINT')
    for v in mesh.vertices:
        rgb=color(v.co) if callable(color) else color
        r=roughness
        if m:
            x,y,z=v.co;f=m['frequency']
            # Broad object-space fields are baked once. No render-time noise or image maps.
            wave=.5+.5*(ROUGHNESS_PRIMARY_WEIGHT*math.sin(f*x+ROUGHNESS_Y_COUPLING*f*y)*math.cos(ROUGHNESS_Z_FREQUENCY_SCALE*f*z-ROUGHNESS_Y_PHASE_SCALE*f*y)
                         +ROUGHNESS_SECONDARY_WEIGHT*math.sin(ROUGHNESS_SECONDARY_FREQUENCY_SCALE*f*(x-ROUGHNESS_SECONDARY_Y_COUPLING*y+ROUGHNESS_SECONDARY_Z_COUPLING*z)))
            pigment=.5+.5*math.sin(PIGMENT_X_COUPLING*f*x-PIGMENT_Y_COUPLING*f*y+PIGMENT_Z_COUPLING*f*z+PIGMENT_PHASE)
            rgb=tuple(rgb[i]*(m['pigment_low'][i]*(1-pigment)+m['pigment_high'][i]*pigment) for i in range(3))
            r=m['roughness_min']+(m['roughness_max']-m['roughness_min'])*wave
        colors.data[v.index].color=(*rgb,1)
        glow.data[v.index].value=emission
        rough.data[v.index].value=r;optical.data[v.index].value=m['ior'] if m else ior
        sss.data[v.index].color=(*scattering['radius'],scattering['weight']) if scattering else (0,0,0,0)
    tag=mesh.attributes.new(name="surface",type="INT",domain="FACE")
    for entry in tag.data:entry.value=material
    tag=mesh.attributes.new(name="component",type="INT",domain="POINT")
    for entry in tag.data:entry.value=component
    for poly in mesh.polygons:poly.use_smooth=smooth
    mesh.update();parts.append(obj);return obj

def sphere(center,size,color,sub=2,roughness=DEFAULT_ROUGHNESS,ior=DEFAULT_IOR,surface=None,subsurface=None):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=sub,radius=1,location=center)
    obj=bpy.context.object;obj.scale=size
    return finish(obj,color,roughness,ior,surface=surface,subsurface=subsurface)

def rod(a,b,radius,color,surface=None,subsurface=None,emission=0,sides=ROD_SIDES,smooth=True,tip_scale=ROD_TIP_RADIUS_SCALE):
    a,b=Vector(a),Vector(b);delta=b-a
    bpy.ops.mesh.primitive_cone_add(vertices=sides,radius1=radius,radius2=radius*tip_scale,depth=delta.length,location=(a+b)/2)
    obj=bpy.context.object;obj.rotation_euler=delta.to_track_quat('Z','Y').to_euler()
    return finish(obj,color,surface=surface,subsurface=subsurface,emission=emission,smooth=smooth)

def branches(start,direction,length,radius,depth,color,planar=False,buds=None,surface=None,bud_surface=None):
    p=profiles['fractal'];start=Vector(start);direction=Vector(direction).normalized();end=start+direction*length
    rod(start,end,radius,color,surface)
    if depth==0:
        if buds:sphere(end,(radius*3,)*3,buds,1,BUD_ROUGHNESS,surface=bud_surface)
        return
    axis=Vector((0,0,1)) if planar else Vector(FRACTAL_REFERENCE_AXIS)
    side=direction.cross(axis)
    if side.length<MIN_BRANCH_AXIS_LENGTH:side=direction.cross(Vector((1,0,0)))
    side.normalize()
    for sign in (-1,1):
        jitter=rng.uniform(-p['jitter'],p['jitter'])
        child=direction+side*(sign*p['spread']+jitter)
        if not planar:child+=axis*rng.uniform(-p['jitter'],p['jitter'])
        branches(end,child,length*p['scale'],radius*p['radius_scale'],depth-1,color,planar,buds,surface,bud_surface)

def export(name,anatomy=False,subsurface_weight=None):
    global parts
    bpy.ops.object.select_all(action='DESELECT')
    for obj in parts:obj.select_set(True)
    bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join()
    obj=bpy.context.object;obj.name=name;mesh=obj.data;mesh.calc_loop_triangles()
    sss=mesh.color_attributes['Subsurface'].data;colors=mesh.color_attributes['Color'].data;rough=mesh.attributes['roughness'].data;ior=mesh.attributes['ior'].data
    glow=mesh.attributes['emission'].data
    if subsurface_weight is not None:
        for value in sss:value.color=(*value.color[:3],subsurface_weight)
    records=[];indices=[];lookup={};groups=[]
    for triangle in mesh.loop_triangles:
        row=[]
        for vi,li in zip(triangle.vertices,triangle.loops):
            normal=mesh.corner_normals[li].vector;key=(vi,*normal)
            if key not in lookup:
                lookup[key]=len(records)
                records.append((*mesh.vertices[vi].co,ior[vi].value,*normal,glow[vi].value,*colors[vi].color[:3],rough[vi].value,*sss[vi].color))
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
            if part['attachment'] is not None:
                d['attachment']=list(part['attachment'])
                points=records[first:first+count]
                d['attachment_radius']=max(max(v[k] for v in points)-min(v[k] for v in points) for k in range(3))*.5
            if part['minimum_scale']:d['minimum_scale']=part['minimum_scale']
            if part['anchor_origin']:d['anchor_origin']=True
            definitions.append(d);first+=count
        (out/(name+'.parts.json')).write_text(json.dumps({'$schema':'../schemas/mesh-parts.schema.json','vertices':len(records),'parts':definitions},indent=2)+'\n')
    if any(abs(x)>MAX_LOCAL_COORDINATE for row in records for x in row[:3]):raise RuntimeError(name+' exceeds local mesh bounds')
    with (out/(name+'.fmesh')).open('wb') as f:
        has_subsurface=any(row[15]>0 for row in records)
        f.write(struct.pack('<8sII',b'FLMESH02' if has_subsurface else b'FLMESH01',len(records),len(indices)))
        for row in records:f.write(struct.pack('<16f',*row) if has_subsurface else struct.pack('<12f',*row[:12]))
        for row in indices:f.write(struct.pack('<4I',*row))
    obj.location=((len(models)%SOURCE_PREVIEW_COLUMNS)*SOURCE_PREVIEW_SPACING,0,(len(models)//SOURCE_PREVIEW_COLUMNS)*SOURCE_PREVIEW_SPACING)
    models.append(obj);parts=[];print(name,len(records),'vertices',len(indices),'triangles',flush=True)

def cell(name,key):
    global components
    components=[];cell_part((0,0,0),False)
    p=profiles[key]
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=CELL_SHELL_SUBDIVISIONS,radius=1)
    obj=bpy.context.object
    for v in obj.data.vertices:
        n=v.co.normalized();x,y,z=n
        noise=CELL_PRIMARY_DISPLACEMENT*math.sin(CELL_NOISE_X_FREQUENCY*x+CELL_NOISE_Y_FREQUENCY*y)*math.sin(CELL_NOISE_Z_FREQUENCY*z-CELL_NOISE_Y_FINE_COUPLING*y)+CELL_FINE_DISPLACEMENT*math.sin(CELL_FINE_NOISE_X_FREQUENCY*x-CELL_FINE_NOISE_Z_FREQUENCY*z)
        v.co=n*(1+noise);v.co.y*=CELL_VERTICAL_SCALE
    def pigment(v):
        n=v.normalized();wave=.5+.5*math.sin(CELL_PIGMENT_XZ_FREQUENCY*n.x+CELL_PIGMENT_Y_FREQUENCY*n.y)*math.sin(CELL_PIGMENT_XZ_FREQUENCY*n.z-CELL_PIGMENT_Y_FINE_COUPLING*n.y)
        return tuple(p['membrane'][i]*(1-CELL_RIM_PIGMENT_WEIGHT*wave)+p['rim'][i]*(CELL_RIM_PIGMENT_WEIGHT*wave) for i in range(CELL_PIGMENT_Y_FREQUENCY))
    finish(obj,pigment,material=3,surface='membrane')
    inner=profiles['interior']
    center=Vector(inner['nucleus_center'])
    cell_part(center,attachment=center+center.normalized()*inner['nucleus_size'][0],anchor_origin=True)
    sphere(inner['nucleus_center'],inner['nucleus_size'],p['nucleus'],3,surface='nucleus')
    sphere(inner['nucleolus_center'],inner['nucleolus_size'],p['vesicle'],2,surface='nucleolus')
    for index,center in enumerate(inner['mitochondria']):
        anchor=Vector(center)+Vector(center).normalized()*inner['mitochondrion_size'][1]
        cell_part(center,attachment=anchor,minimum_scale=inner['mitochondrion_minimum_radius'][index])
        sphere(center,inner['mitochondrion_size'],inner['mitochondrion_color'],2,surface='mitochondrion')
        # Mitochondrial cristae are geometry rather than painted lines.
        for j in range(3):
            c=Vector(center)+Vector(((j-1)*CRISTAE_SPACING,0,CRISTAE_DEPTH_OFFSET))
            sphere(c,CRISTAE_SIZE,p['nucleus'],1,surface='cristae')
    for index,center in enumerate(inner['vesicle_centers']):
        center=Vector(center)
        cell_part(center,attachment=center+center.normalized()*VESICLE_ATTACHMENT_RADIUS,minimum_scale=inner['vesicle_minimum_radius'][index])
        sphere(center,VESICLE_SIZE,inner['vesicle_colors'][index],2,surface='vesicle')
    # Curved endoplasmic reticulum surrounding the nucleus, inside the shell.
    cell_part((0,0,0))
    for ring in range(3):
        previous=None
        for j in range(RETICULUM_SAMPLES):
            angle=j*math.tau/RETICULUM_INTERVALS;radius=inner['reticulum_radius']+ring*RETICULUM_RING_SPACING
            point=(radius*math.cos(angle),radius*RETICULUM_Y_SCALE*math.sin(angle),-RETICULUM_Z_BASE+ring*RETICULUM_Z_RING_SPACING+RETICULUM_Z_WAVE_AMPLITUDE*math.sin(angle*RETICULUM_Z_WAVE_FREQUENCY))
            if previous:rod(previous,point,inner['reticulum_tube'],inner['reticulum_color'],'reticulum')
            previous=point
    substrate=profiles['substrate']
    cell_part((0,0,0))
    for i in range(substrate['roots']):
        angle=i*GOLDEN_ANGLE_RADIANS;y=1-2*(i+.5)/substrate['roots'];r=math.sqrt(1-y*y)
        direction=Vector((r*math.cos(angle),y,r*math.sin(angle)))
        branches(direction*SUBSTRATE_START_RADIUS,direction,substrate['length'],substrate['radius'],substrate['depth'],substrate['color'],surface='substrate')
    export(name,True)

def virus(large):
    p=profiles['virus']
    if not large:
        sphere((0,0,0),(VIRION_RADIUS,VIRION_RADIUS,VIRION_RADIUS),p['body'],2,surface='virus_capsid')
        for i in range(VIRUS_FIBRE_COUNT):
            a=i*math.tau/VIRUS_FIBRE_COUNT;d=Vector((math.cos(a),math.sin(a),VIRION_FIBRE_UP_COMPONENT))
            branches(d*VIRION_FIBRE_ROOT_RADIUS,d,VIRION_FIBRE_LENGTH,VIRION_FIBRE_RADIUS,2,p['spikes'],surface='virus_fibre')
        # Detachable spikes are instances of virus_spike.fmesh at runtime.
    else:
        # A broad capsid with paired payload sacs and repeated forked ribs.
        sphere((0,0,0),BOMBER_CAPSID_SIZE,p['body'],2,surface='virus_capsid')
        for side in (-1,1):
            for j in range(3):
                z=(j-1)*BOMBER_PAYLOAD_SPACING
                branches((side*BOMBER_RIB_ROOT_X_OFFSET,BOMBER_RIB_ROOT_Y_OFFSET,z),(side,BOMBER_RIB_Y_DIRECTION,BOMBER_RIB_Z_DIRECTION),BOMBER_RIB_LENGTH,BOMBER_RIB_RADIUS,3,p['spikes'],buds=p['body'],surface='virus_fibre',bud_surface='virus_capsid')
    export('virus_'+('bomber' if large else 'virion'))

def immune(large):
    p=profiles['immune'];radius=IMMUNE_BOMBER_RADIUS if large else IMMUNE_FIGHTER_RADIUS
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3,radius=1)
    obj=bpy.context.object
    for v in obj.data.vertices:
        n=v.co.normalized();x,y,z=n;v.co=n*radius*(1+IMMUNE_SURFACE_DISPLACEMENT*math.sin(IMMUNE_SURFACE_X_FREQUENCY*x+2*y)*math.sin(IMMUNE_SURFACE_Z_FREQUENCY*z-y))
        if large:v.co.x*=IMMUNE_BOMBER_X_SCALE
    finish(obj,p['body'],surface='immune_surface')
    for i in range(3 if not large else 2):
        sphere(((i-1)*IMMUNE_NUCLEUS_SPACING,IMMUNE_NUCLEUS_Y_OFFSET,IMMUNE_NUCLEUS_Z_OFFSET),IMMUNE_NUCLEUS_SIZE,p['nucleus'],2,surface='nucleus')
    for i in range(IMMUNE_BOMBER_LOBE_COUNT if large else 4):
        a=i*math.tau/(IMMUNE_BOMBER_LOBE_COUNT if large else 4)
        sphere((math.cos(a)*radius*IMMUNE_SURFACE_ATTACHMENT_RADIUS_SCALE,-IMMUNE_LOBE_Y_OFFSET,math.sin(a)*radius*IMMUNE_SURFACE_ATTACHMENT_RADIUS_SCALE),IMMUNE_LOBE_SIZE,p['body'],2,surface='immune_surface')
    for i in range(IMMUNE_GRANULE_COUNT):
        a=i*GOLDEN_ANGLE_RADIANS;sphere((IMMUNE_GRANULE_X_RADIUS*math.cos(a),IMMUNE_GRANULE_Y_RADIUS*math.sin(a),IMMUNE_GRANULE_Z_OFFSET),IMMUNE_GRANULE_SIZE,p['granules'],1,surface='immune_granule')
    for i in range(IMMUNE_BOMBER_BRANCH_COUNT if large else 4):
        a=i*math.tau/(IMMUNE_BOMBER_BRANCH_COUNT if large else 4);d=Vector((math.cos(a),math.sin(a),IMMUNE_BRANCH_UP_COMPONENT))
        branches(d*radius*IMMUNE_SURFACE_ATTACHMENT_RADIUS_SCALE,d,IMMUNE_BOMBER_BRANCH_LENGTH if large else IMMUNE_FIGHTER_BRANCH_LENGTH,IMMUNE_BRANCH_RADIUS,2,p['body'],buds=p['granules'],surface='immune_surface',bud_surface='immune_granule')
    export('immune_'+('bomber' if large else 'leukocyte'))

def node():
    p=profiles['node'];sphere((0,NODE_BASE_Y,0),NODE_BASE_SIZE,p['stem'],2,subsurface='substrate')
    for i in range(4):
        y=NODE_FIRST_RING_Y+i*NODE_RING_SPACING;r=NODE_RING_BASE_RADIUS+NODE_RING_RADIUS_WAVE*math.sin(i)
        sphere((NODE_RING_X_WAVE*math.sin(i*2),y,0),(r,NODE_RING_HALF_HEIGHT,r),p['membrane'],2,subsurface='membrane')
    for i in range(NODE_BUD_COUNT):
        a=i*math.tau/NODE_BUD_COUNT;sphere((NODE_BUD_RING_RADIUS*math.cos(a),NODE_BUD_Y,NODE_BUD_RING_RADIUS*math.sin(a)),NODE_BUD_SIZE,p['buds'],2,subsurface='vesicle')
    sphere((0,NODE_CROWN_Y,0),NODE_CROWN_SIZE,p['membrane'],2,subsurface='membrane')
    export('cell_replication_node')

def spore():
    p=profiles['fungal_spore'];sphere((0,0,0),(SPORE_RADIUS,SPORE_RADIUS,SPORE_RADIUS),p['body'],3,surface='fungal_spore')
    for i in range(SPORE_RAY_COUNT):
        a=i*GOLDEN_ANGLE_RADIANS;y=1-2*(i+.5)/SPORE_RAY_COUNT;r=math.sqrt(1-y*y);d=Vector((r*math.cos(a),y,r*math.sin(a)))
        branches(d*SPORE_RAY_ROOT_RADIUS,d,SPORE_RAY_LENGTH,SPORE_RAY_RADIUS,2,p['rays'],buds=p['core'],surface='fungal_stalk',bud_surface='fungal_spore')
    export('fungal_spore')

def factory(side):
    global parts
    saved=rng.getstate()
    p=profiles[side];f=profiles['factories'][side]
    if side=='virus':
        # A continuous irregular shaft replaces the repeated collars.
        shaft_rng=random.Random(profiles['seed']);sides=f['shaft_sides'];sections=f['stem_sections']
        vertices=[];faces=[]
        for j in range(sections+1):
            t=j/sections;envelope=math.sin(math.pi*t)
            x=shaft_rng.uniform(-1,1)*f['shaft_radius']*f['surface_jitter']*envelope
            z=shaft_rng.uniform(-1,1)*f['shaft_radius']*f['surface_jitter']*envelope
            for k in range(sides):
                a=k*math.tau/sides
                radius=f['shaft_radius']*(1-(1-ROD_TIP_RADIUS_SCALE)*t)*(1+shaft_rng.uniform(-1,1)*f['surface_jitter']*envelope)
                vertices.append((x+radius*math.cos(a),t,z+radius*math.sin(a)))
        for j in range(sections):
            for k in range(sides):
                a=j*sides+k;b=j*sides+(k+1)%sides
                faces.append((b,a,a+sides,b+sides))
        faces.extend([tuple(range(sides)),tuple(sections*sides+k for k in reversed(range(sides)))])
        mesh=bpy.data.meshes.new('shaft');mesh.from_pydata(vertices,[],faces);mesh.update()
        obj=bpy.data.objects.new('shaft',mesh);bpy.context.collection.objects.link(obj)
        finish(obj,p['body'],smooth=True,surface='virus_sheath',emission=1)
        export('virus_stem',subsurface_weight=0)
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1,radius=1)
        obj=bpy.context.object;obj.scale=(f['head_radius'],f['head_radius']*f['head_height_scale'],f['head_radius'])
        finish(obj,p['body'],smooth=False,surface='virus_capsid')
        for i in range(VIRUS_FACTORY_FIBRE_COUNT):
            a=i*math.tau/VIRUS_FACTORY_FIBRE_COUNT;d=Vector((math.cos(a),0,math.sin(a)))
            bend=d*f['fibre_spread']+Vector((0,-VIRUS_FIBRE_BEND_Y,0));end=d*f['fibre_spread']*VIRUS_FIBRE_END_SPREAD_SCALE+Vector((0,-VIRUS_FIBRE_END_Y,0))
            rod((0,-VIRUS_FIBRE_ROOT_Y,0),bend,f['fibre_radius'],p['spikes'],'virus_fibre',sides=3,smooth=False)
            rod(bend,end,f['fibre_radius'],p['core'],'virus_fibre',sides=3,smooth=False,tip_scale=0)
        export('virus_head',subsurface_weight=0)
    elif side=='immune':
        rod((0,0,0),(0,1,0),f['shaft_radius'],p['body'],'virus_sheath' if side=='virus' else 'immune_surface')
        for i in range(f['lobes']):
            y=(i+.5)/f['lobes'];sign=-1 if i%2 else 1
            sphere((sign*IMMUNE_STEM_LOBE_X,y,0),IMMUNE_STEM_LOBE_SIZE,p['body'],2,surface='immune_surface')
            branches((0,y,0),(sign,IMMUNE_FACTORY_BRANCH_UP_COMPONENT,IMMUNE_FACTORY_BRANCH_DEPTH_COMPONENT),f['spread'],FACTORY_BRANCH_RADIUS,2,p['body'],buds=p['granules'],surface='immune_surface',bud_surface='immune_granule')
        for obj in parts:obj.data.transform(Matrix.Diagonal((f['width_scale'],1,f['width_scale'],1)))
        export('immune_stem',subsurface_weight=0)
        for i in range(f['crown_lobes']):
            a=i*math.tau/f['crown_lobes']
            sphere((IMMUNE_CROWN_RADIUS*math.cos(a),IMMUNE_CROWN_Y,IMMUNE_CROWN_RADIUS*math.sin(a)),IMMUNE_CROWN_SIZE,p['body'],2,surface='immune_surface')
        sphere((0,FACTORY_GRANULE_Y,IMMUNE_FACTORY_GRANULE_Z),IMMUNE_FACTORY_GRANULE_SIZE,p['granules'],2,surface='immune_granule')
        for obj in parts:obj.data.transform(Matrix.Diagonal((f['width_scale'],1,f['width_scale'],1)))
        export('immune_head',subsurface_weight=0)
    else:
        rod((0,0,0),(0,1,0),f['shaft_radius'],p['rays'],'fungal_stalk')
        for i in range(f['forks']):
            a=i*GOLDEN_ANGLE_RADIANS;y=(i+.5)/f['forks']
            branches((0,y,0),(math.cos(a),FUNGAL_BRANCH_UP_COMPONENT,math.sin(a)),f['spread'],FACTORY_BRANCH_RADIUS,2,p['body'],buds=p['core'],surface='fungal_stalk',bud_surface='fungal_spore')
        export('fungal_stem',subsurface_weight=0)
        sphere((0,-FUNGAL_STALK_ROOT_Y,0),FUNGAL_CAP_STALK_SIZE,p['rays'],2,surface='fungal_stalk')
        sphere((0,FUNGAL_CAP_Y,0),(f['cap_radius'],FUNGAL_CAP_HALF_HEIGHT,f['cap_radius']),p['body'],2,surface='fungal_cap')
        for i in range(f['spores']):
            a=i*GOLDEN_ANGLE_RADIANS
            sphere((FUNGAL_SPORE_RING_RADIUS*math.cos(a),FUNGAL_SPORE_RING_Y,FUNGAL_SPORE_RING_RADIUS*math.sin(a)),(FUNGAL_FACTORY_SPORE_RADIUS,)*3,p['core'],1,surface='fungal_spore')
        for obj in parts:obj.data.transform(Matrix.Diagonal((f['head_scale'],f['head_scale'],f['head_scale'],1)))
        export('fungal_head',subsurface_weight=0)
    # The complete model is used by the non-staged presentation path.
    for i,source in enumerate(models[-2:]):
        obj=bpy.data.objects.new('complete',source.data.copy());bpy.context.collection.objects.link(obj)
        if i:obj.data.transform(Matrix.Translation((0,1,0)))
        parts.append(obj)
    export(('fungal' if side=='fungal_spore' else side)+'_factory',subsurface_weight=0)
    rng.setstate(saved)

def background(name,key):
    p=profiles[key];vertices=[];faces=[];nx,ny=BACKGROUND_GRID_X,BACKGROUND_GRID_Y
    for j in range(ny+1):
        y=(j/ny*2-1)*BACKGROUND_HALF_HEIGHT
        for i in range(nx+1):
            x=(i/nx*2-1)*BACKGROUND_HALF_WIDTH
            z=BACKGROUND_PRIMARY_FOLD*math.sin(BACKGROUND_PRIMARY_X_FREQUENCY*x+2*math.sin(BACKGROUND_Y_MODULATION_FREQUENCY*y))+BACKGROUND_SECONDARY_FOLD*math.cos(BACKGROUND_SECONDARY_Y_FREQUENCY*y-3*x)+BACKGROUND_FINE_FOLD*math.sin(BACKGROUND_FINE_X_FREQUENCY*x+BACKGROUND_FINE_Y_FREQUENCY*y)
            vertices.append((x,y,z))
    for j in range(ny):
        for i in range(nx):
            a=j*(nx+1)+i;faces.extend([(a,a+1,a+nx+2),(a,a+nx+2,a+nx+1)])
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(vertices,[],faces);mesh.update()
    obj=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(obj)
    def pigment(v):
        # Broad cytoplasm gradients and membrane/fiber folds, all baked to vertices.
        x,y,z=v;f=.5+.5*math.sin(BACKGROUND_PRIMARY_X_FREQUENCY*x+2*math.sin(BACKGROUND_Y_MODULATION_FREQUENCY*y))*math.cos(BACKGROUND_SECONDARY_Y_FREQUENCY*y-3*x)
        return tuple(p['base'][i]*(1-BACKGROUND_RIDGE_BLEND*f)+p['ridge'][i]*BACKGROUND_RIDGE_BLEND*f for i in range(3))
    finish(obj,pigment,1,1)
    for j in range(BACKGROUND_FIBRE_ROWS):
        y=-BACKGROUND_FIBRE_START_Y+j*BACKGROUND_FIBRE_ROW_SPACING
        last=None
        for i in range(BACKGROUND_FIBRE_SAMPLES):
            x=-BACKGROUND_FIBRE_HALF_WIDTH+i*BACKGROUND_FIBRE_SPAN/BACKGROUND_FIBRE_INTERVALS;point=(x,y+BACKGROUND_FIBRE_Y_WAVE*math.sin(x*BACKGROUND_FIBRE_Y_FREQUENCY+j),BACKGROUND_FIBRE_BASE_Z+BACKGROUND_FIBRE_Z_WAVE*math.cos(x*BACKGROUND_FIBRE_Z_FREQUENCY+j))
            if last:rod(last,point,BACKGROUND_FIBRE_RADIUS,p['fiber'])
            last=point
    # Blood cells and small extracellular vesicles occupy depth behind the play field.
    for i in range(BACKGROUND_BLOOD_COUNT):
        x=rng.uniform(-BACKGROUND_PARTICLE_HALF_WIDTH,BACKGROUND_PARTICLE_HALF_WIDTH);y=rng.uniform(-BACKGROUND_BLOOD_HALF_HEIGHT,BACKGROUND_BLOOD_HALF_HEIGHT);z=rng.uniform(BACKGROUND_BLOOD_NEAR_Z,BACKGROUND_BLOOD_FAR_Z)
        if abs(x)<BACKGROUND_CLEAR_HALF_WIDTH and abs(y)<BACKGROUND_CLEAR_HALF_HEIGHT:continue
        radius=rng.uniform(BACKGROUND_BLOOD_MIN_RADIUS,BACKGROUND_BLOOD_MAX_RADIUS)
        sphere((x,y,z),(radius,radius*BACKGROUND_BLOOD_HEIGHT_SCALE,radius*BACKGROUND_BLOOD_DEPTH_SCALE),p['blood'],2,1,1)
    for i in range(BACKGROUND_VESICLE_COUNT):
        x=rng.uniform(-BACKGROUND_PARTICLE_HALF_WIDTH,BACKGROUND_PARTICLE_HALF_WIDTH);y=rng.uniform(-BACKGROUND_VESICLE_HALF_HEIGHT,BACKGROUND_VESICLE_HALF_HEIGHT);z=rng.uniform(BACKGROUND_VESICLE_NEAR_Z,BACKGROUND_VESICLE_FAR_Z)
        sphere((x,y,z),(BACKGROUND_VESICLE_RADIUS,BACKGROUND_VESICLE_RADIUS,BACKGROUND_VESICLE_RADIUS),p['ridge'],1,1,1)
    export(name)

cell('cell_infected','infected_cell');cell('cell_healthy','healthy_cell');cell('cell_neutral','neutral_cell')
virus(False);virus(True)
sphere((0,0,0),BOMBER_PAYLOAD_SIZE,profiles['virus']['core'],1,surface='vesicle');export('virus_bomb')
immune(False);immune(True);node();factory('virus');factory('immune');factory('fungal_spore')
spore()
# A single tapered local +Y segment. Runtime poses extend segments in sequence.
rod((0,0,0),(0,1,0),GROWTH_SEGMENT_RADIUS,(1,1,1),subsurface='substrate');export('growth_segment')
sphere((0,0,0),(GROWTH_TIP_RADIUS,GROWTH_TIP_RADIUS,GROWTH_TIP_RADIUS),(1,1,1),2,GROWTH_TIP_ROUGHNESS,subsurface='vesicle');export('growth_tip')
# One reusable shaft and head, local +Z; attached, flying and embedded use this mesh.
rod((0,0,0),(0,0,SPIKE_SHAFT_LENGTH),SPIKE_SHAFT_RADIUS,(1,1,1),'spike');sphere((0,0,SPIKE_HEAD_Z),SPIKE_HEAD_SIZE,(1,1,1),1,surface='spike')
export('virus_spike')
# One low-poly mote per swarm unit: thousands can be drawn at once.
sphere((0,0,0),(SWARM_SPORE_RADIUS,)*3,profiles['fungal_spore']['core'],1,surface='fungal_spore');export('fungal_swarm_spore')
# A white blood cell's sticky projectile.
sphere((0,0,0),IMMUNE_BLOB_SIZE,profiles['immune']['body'],2,surface='vesicle',subsurface='vesicle');export('immune_blob')
rng=random.Random(profiles['seed'])
background('tissue_background','tissue');background('bloodstream_background','bloodstream')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(out/'source/biology.blend'),compress=True)
print('Saved source/biology.blend',flush=True)
# All exports are closed. Avoid the sandboxed Blender audio teardown hang.
os._exit(0)
