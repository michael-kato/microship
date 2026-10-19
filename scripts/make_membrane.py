"""Create coarse vertex-colored flesh membranes with branching veins; no image maps."""
import bpy,json,math,os,struct,sys
from pathlib import Path
from mathutils import Vector

FOLD_MODULATION_FREQUENCY = 3
FINE_FOLD_WEIGHT = .2
FINE_FOLD_X_FREQUENCY = 13
FINE_FOLD_Y_FREQUENCY = 7
GRID_HALF_HEIGHT = 1.46
GRID_HALF_WIDTH = 1.48
VEIN_CROSS_SECTION_SIDES = 8
VEIN_TAPER_AMOUNT = .92
VEIN_TAPER_EXPONENT = 1.2
TRUNK_SAMPLES = 35
TRUNK_SPAN = 2.92
TRUNK_INTERVALS = 34
TRUNK_WAVE_AMPLITUDE = .1
TRUNK_WAVE_FREQUENCY = 2.8
TRUNK_FINE_WAVE_AMPLITUDE = .035
TRUNK_FINE_WAVE_FREQUENCY = 7
BRANCH_X_EXTENT = .4
BRANCH_X_WAVE_AMPLITUDE = .025
BRANCH_Y_EXTENT = .36
BRANCH_SURFACE_OFFSET_SCALE = .45
BRANCH_SAMPLES = 20
BRANCH_INTERVALS = 19
BRANCH_RADIUS_SCALE = .58
CAPILLARY_SAMPLES = 12
CAPILLARY_INTERVALS = 11
CAPILLARY_X_EXTENT = .12
CAPILLARY_Y_EXTENT = .13
CAPILLARY_WAVE_AMPLITUDE = .015
CAPILLARY_RADIUS_SCALE = .24
PIGMENT_BASE_SHADE = .78
PIGMENT_X_FREQUENCY = 5
MOTTLE_X_FREQUENCY = 17
MOTTLE_Y_MODULATION_FREQUENCY = 9
MOTTLE_Y_FREQUENCY = 14
MEMBRANE_IOR = 1.35
BRANCH_CURVE_FREQUENCY = 4
CAPILLARY_START_FRACTION = .3
CAPILLARY_WAVE_FREQUENCY = 4
CAPILLARY_SURFACE_OFFSET_SCALE = .2
PIGMENT_WAVE_AMPLITUDE = .12
PIGMENT_Y_FREQUENCY = 3
MOTTLE_Y_MODULATION_SCALE = 2
MOTTLE_X_COUPLING = 4
SOURCE_PREVIEW_SPACING = 4
TRUNK_HALF_WIDTH = 1.46
CAPILLARY_SPACING_FRACTION = .3
BRANCH_Y_WAVE_AMPLITUDE = .035

out=Path(sys.argv[sys.argv.index('--')+1]).resolve()
p=json.loads((out/'source/background-shape.json').read_text())['membrane']
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
def height(x,y):return p['fold_amplitude']*(math.sin(p['frequency'][0]*x+math.sin(FOLD_MODULATION_FREQUENCY*y))+.5*math.cos(p['frequency'][1]*y-x)+FINE_FOLD_WEIGHT*math.sin(FINE_FOLD_X_FREQUENCY*x+FINE_FOLD_Y_FREQUENCY*y))
for mode,key in enumerate(('tissue','bloodstream')):
    parts=[];nx,ny=p['grid'];verts=[];faces=[]
    for j in range(ny+1):
        y=(j/ny*2-1)*GRID_HALF_HEIGHT
        for i in range(nx+1):
            x=(i/nx*2-1)*GRID_HALF_WIDTH;verts.append((x,y,height(x,y)))
    for j in range(ny):
        for i in range(nx):
            a=j*(nx+1)+i;faces.extend([(a,a+1,a+nx+2),(a,a+nx+2,a+nx+1)])
    mesh=bpy.data.meshes.new(key);mesh.from_pydata(verts,[],faces);mesh.update()
    obj=bpy.data.objects.new(key+'-membrane',mesh);bpy.context.collection.objects.link(obj);parts.append((obj,False))
    def vein(points,radius,taper=False):
        verts=[];faces=[];sides=VEIN_CROSS_SECTION_SIDES
        for i,point in enumerate(points):
            tangent=(Vector(points[min(i+1,len(points)-1)])-Vector(points[max(0,i-1)])).normalized()
            axis=tangent.cross(Vector((0,0,1))).normalized();other=tangent.cross(axis).normalized()
            for j in range(sides):
                angle=j*math.tau/sides;local_radius=radius*(1-VEIN_TAPER_AMOUNT*(i/(len(points)-1))**VEIN_TAPER_EXPONENT) if taper else radius
                verts.append(Vector(point)+local_radius*(math.cos(angle)*axis+math.sin(angle)*other))
        for i in range(len(points)-1):
            for j in range(sides):
                a=i*sides+j;b=i*sides+(j+1)%sides;c=b+sides;d=a+sides
                faces.extend([(a,b,c),(a,c,d)])
        mesh=bpy.data.meshes.new('vein');mesh.from_pydata(verts,[],faces);mesh.update()
        obj=bpy.data.objects.new('vein',mesh);bpy.context.collection.objects.link(obj);parts.append((obj,True))
    for index,y0 in enumerate(p['trunks']):
        points=[]
        for i in range(TRUNK_SAMPLES):
            x=-TRUNK_HALF_WIDTH+i*TRUNK_SPAN/TRUNK_INTERVALS;y=y0+TRUNK_WAVE_AMPLITUDE*math.sin(TRUNK_WAVE_FREQUENCY*x+index)+TRUNK_FINE_WAVE_AMPLITUDE*math.sin(TRUNK_FINE_WAVE_FREQUENCY*x)
            points.append((x,y,height(x,y)+p['vein_radius']))
        vein(points,p['vein_radius'])
        for j,x0 in enumerate(p['branches']):
            points=[];sign=1 if (j+index)%2 else -1
            def branch(t):
                x=x0+t*BRANCH_X_EXTENT+BRANCH_X_WAVE_AMPLITUDE*math.sin(t*math.pi*2)
                y=y0+TRUNK_WAVE_AMPLITUDE*math.sin(TRUNK_WAVE_FREQUENCY*x0+index)+TRUNK_FINE_WAVE_AMPLITUDE*math.sin(TRUNK_FINE_WAVE_FREQUENCY*x0)+sign*(t*BRANCH_Y_EXTENT+BRANCH_Y_WAVE_AMPLITUDE*math.sin(t*BRANCH_CURVE_FREQUENCY))
                return (x,y,height(x,y)+p['vein_radius']*BRANCH_SURFACE_OFFSET_SCALE)
            for i in range(BRANCH_SAMPLES):points.append(branch(i/BRANCH_INTERVALS))
            vein(points,p['vein_radius']*BRANCH_RADIUS_SCALE,True)
            for k in range(p['capillaries']):
                origin=branch(CAPILLARY_START_FRACTION+k*CAPILLARY_SPACING_FRACTION);twig=[]
                for i in range(CAPILLARY_SAMPLES):
                    t=i/CAPILLARY_INTERVALS;x=origin[0]+t*CAPILLARY_X_EXTENT*sign;y=origin[1]+t*CAPILLARY_Y_EXTENT+CAPILLARY_WAVE_AMPLITUDE*math.sin(t*CAPILLARY_WAVE_FREQUENCY+k)
                    twig.append((x,y,height(x,y)+p['vein_radius']*CAPILLARY_SURFACE_OFFSET_SCALE))
                vein(twig,p['vein_radius']*CAPILLARY_RADIUS_SCALE,True)
    records=[];triangles=[]
    for obj,is_vein in parts:
        mesh=obj.data;mesh.update();mesh.calc_loop_triangles()
        color=mesh.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='POINT')
        for v in mesh.vertices:
            x,y=v.co.x,v.co.y
            wave=PIGMENT_BASE_SHADE+PIGMENT_WAVE_AMPLITUDE*math.sin(PIGMENT_X_FREQUENCY*x+PIGMENT_Y_FREQUENCY*y)+p['mottle_strength']*.5*math.sin(MOTTLE_X_FREQUENCY*x+MOTTLE_Y_MODULATION_SCALE*math.sin(MOTTLE_Y_MODULATION_FREQUENCY*y))*math.sin(MOTTLE_Y_FREQUENCY*y-MOTTLE_X_COUPLING*x)
            rgb=p['vein_colors' if is_vein else 'colors'][key]
            color.data[v.index].color=(*(c*wave for c in rgb),1)
        base=len(records)
        for v in mesh.vertices:
            records.append((*v.co,MEMBRANE_IOR,*v.normal,1 if is_vein else 0,*color.data[v.index].color[:3],1))
        triangles.extend((*[base+i for i in t.vertices],0) for t in mesh.loop_triangles)
        obj.location.x=mode*SOURCE_PREVIEW_SPACING
    name='soft_'+key+'_background'
    with (out/(name+'.fmesh')).open('wb') as f:
        f.write(struct.pack('<8sII',b'FLMESH01',len(records),len(triangles)))
        for v in records:f.write(struct.pack('<12f',*v))
        for t in triangles:f.write(struct.pack('<4I',*t))
    print(name,len(records),'vertices',len(triangles),'triangles',flush=True)
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(out/'source/membrane.blend'),compress=True)
sys.stdout.flush();os._exit(0)
