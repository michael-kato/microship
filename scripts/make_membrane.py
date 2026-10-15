"""Create coarse vertex-colored flesh membranes with branching veins; no image maps."""
import bpy,json,math,os,struct,sys
from pathlib import Path
from mathutils import Vector
out=Path(sys.argv[sys.argv.index('--')+1]).resolve()
p=json.loads((out/'source/background_profiles.json').read_text())['membrane']
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
def height(x,y):return p['fold_amplitude']*(math.sin(p['frequency'][0]*x+math.sin(3*y))+.5*math.cos(p['frequency'][1]*y-x)+.2*math.sin(13*x+7*y))
for mode,key in enumerate(('tissue','bloodstream')):
    parts=[];nx,ny=p['grid'];verts=[];faces=[]
    for j in range(ny+1):
        y=(j/ny*2-1)*1.46
        for i in range(nx+1):
            x=(i/nx*2-1)*1.48;verts.append((x,y,height(x,y)))
    for j in range(ny):
        for i in range(nx):
            a=j*(nx+1)+i;faces.extend([(a,a+1,a+nx+2),(a,a+nx+2,a+nx+1)])
    mesh=bpy.data.meshes.new(key);mesh.from_pydata(verts,[],faces);mesh.update()
    obj=bpy.data.objects.new(key+'-membrane',mesh);bpy.context.collection.objects.link(obj);parts.append((obj,False))
    def vein(points,radius,taper=False):
        verts=[];faces=[];sides=8
        for i,point in enumerate(points):
            tangent=(Vector(points[min(i+1,len(points)-1)])-Vector(points[max(0,i-1)])).normalized()
            axis=tangent.cross(Vector((0,0,1))).normalized();other=tangent.cross(axis).normalized()
            for j in range(sides):
                angle=j*math.tau/sides;local_radius=radius*(1-.92*(i/(len(points)-1))**1.2) if taper else radius
                verts.append(Vector(point)+local_radius*(math.cos(angle)*axis+math.sin(angle)*other))
        for i in range(len(points)-1):
            for j in range(sides):
                a=i*sides+j;b=i*sides+(j+1)%sides;c=b+sides;d=a+sides
                faces.extend([(a,b,c),(a,c,d)])
        mesh=bpy.data.meshes.new('vein');mesh.from_pydata(verts,[],faces);mesh.update()
        obj=bpy.data.objects.new('vein',mesh);bpy.context.collection.objects.link(obj);parts.append((obj,True))
    for index,y0 in enumerate(p['trunks']):
        points=[]
        for i in range(35):
            x=-1.46+i*2.92/34;y=y0+.1*math.sin(2.8*x+index)+.035*math.sin(7*x)
            points.append((x,y,height(x,y)+p['vein_radius']))
        vein(points,p['vein_radius'])
        for j,x0 in enumerate(p['branches']):
            points=[];sign=1 if (j+index)%2 else -1
            def branch(t):
                x=x0+t*.4+.025*math.sin(t*math.pi*2)
                y=y0+.1*math.sin(2.8*x0+index)+.035*math.sin(7*x0)+sign*(t*.36+.035*math.sin(t*4))
                return (x,y,height(x,y)+p['vein_radius']*.45)
            for i in range(20):points.append(branch(i/19))
            vein(points,p['vein_radius']*.58,True)
            for k in range(p['capillaries']):
                origin=branch(.3+k*.3);twig=[]
                for i in range(12):
                    t=i/11;x=origin[0]+t*.12*sign;y=origin[1]+t*.13+.015*math.sin(t*4+k)
                    twig.append((x,y,height(x,y)+p['vein_radius']*.2))
                vein(twig,p['vein_radius']*.24,True)
    records=[];triangles=[]
    for obj,is_vein in parts:
        mesh=obj.data;mesh.update();mesh.calc_loop_triangles()
        color=mesh.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='POINT')
        for v in mesh.vertices:
            x,y=v.co.x,v.co.y
            wave=.78+.12*math.sin(5*x+3*y)+p['mottle_strength']*.5*math.sin(17*x+2*math.sin(9*y))*math.sin(14*y-4*x)
            rgb=p['vein_colors' if is_vein else 'colors'][key]
            color.data[v.index].color=(*(c*wave for c in rgb),1)
        base=len(records)
        for v in mesh.vertices:
            records.append((*v.co,1.35,*v.normal,1 if is_vein else 0,*color.data[v.index].color[:3],1))
        triangles.extend((*[base+i for i in t.vertices],0) for t in mesh.loop_triangles)
        obj.location.x=mode*4
    name='soft_'+key+'_background'
    with (out/(name+'.fmesh')).open('wb') as f:
        f.write(struct.pack('<8sII',b'FLMESH01',len(records),len(triangles)))
        for v in records:f.write(struct.pack('<12f',*v))
        for t in triangles:f.write(struct.pack('<4I',*t))
    print(name,len(records),'vertices',len(triangles),'triangles',flush=True)
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(out/'source/membrane.blend'),compress=True)
sys.stdout.flush();os._exit(0)
