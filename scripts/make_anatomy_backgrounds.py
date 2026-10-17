"""Build eight editable, vertex-colored anatomical backdrops. No image maps."""
import bpy
import json
import math
import os
from pathlib import Path
import struct
import sys
from mathutils import Vector

out=Path(sys.argv[sys.argv.index('--')+1]).resolve()
p=json.loads((out/'source/anatomy_backgrounds.json').read_text())
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
parts=[]

def surface(x,y):
    fx,fy=p['fold_frequency']
    return p['base_z']+p['fold_amplitude']*(math.sin(fx*x+math.sin(3*y))+.6*math.cos(fy*y-x))

def mesh_part(name,verts,faces,color,roughness):
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(verts,[],faces);mesh.update()
    obj=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(obj)
    colors=mesh.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='POINT')
    for v in mesh.vertices:
        x,y,z=v.co
        shade=.88+.08*math.sin(13*x+4*y)+.04*math.sin(39*y-11*x)
        colors.data[v.index].color=(*(max(0,min(1,c*shade)) for c in color),roughness)
    for polygon in mesh.polygons:polygon.use_smooth=True
    parts.append(obj)
    return obj

def tube(name,points,radius,color,roughness,sides=10,flatten=1,taper=.15):
    verts=[];faces=[]
    for i,point in enumerate(points):
        tangent=(Vector(points[min(i+1,len(points)-1)])-Vector(points[max(0,i-1)])).normalized()
        axis=tangent.cross(Vector((0,0,1)))
        if axis.length<.01:axis=tangent.cross(Vector((0,1,0)))
        axis.normalize();other=tangent.cross(axis).normalized()
        r=radius*(1-taper*math.sin(math.pi*i/(len(points)-1))**2)
        for j in range(sides):
            a=math.tau*j/sides;offset=r*(axis*math.cos(a)+other*math.sin(a))
            offset.z*=flatten;verts.append(Vector(point)+offset)
    for i in range(len(points)-1):
        for j in range(sides):
            a=i*sides+j;b=i*sides+(j+1)%sides
            faces.append((a,b,b+sides,a+sides))
    faces.extend([tuple(reversed(range(sides))),tuple((len(points)-1)*sides+j for j in range(sides))])
    return mesh_part(name,verts,faces,color,roughness)

def lobe(name,center,size,color,roughness,phase=0,segments=20,rings=12):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segments,ring_count=rings,radius=1)
    obj=bpy.context.object;obj.name=name;mesh=obj.data
    for v in mesh.vertices:
        n=v.co.copy();bulge=1+.09*math.sin(5*n.y+phase)*math.cos(4*n.x-2*n.z)
        v.co=Vector([center[k]+n[k]*size[k]*bulge for k in range(3)])
    verts=[tuple(v.co) for v in mesh.vertices];faces=[tuple(f.vertices) for f in mesh.polygons]
    bpy.data.objects.remove(obj,do_unlink=True)
    return mesh_part(name,verts,faces,color,roughness)

for mode,profile in enumerate(p['profiles']):
    parts=[];key=profile['name'];base=profile['color'];accent=profile['accent'];rough=profile['roughness'];count=profile['count'];radius=profile['radius'];height=profile['height']
    nx,ny=p['grid'];ex,ey=p['extent'];verts=[];faces=[]
    for j in range(ny+1):
        y=ey*(2*j/ny-1)
        for i in range(nx+1):
            x=ex*(2*i/nx-1);verts.append((x,y,surface(x,y)))
    for j in range(ny):
        for i in range(nx):
            a=j*(nx+1)+i;faces.append((a,a+1,a+nx+2,a+nx+1))
    mesh_part(key+'-folded-bed',verts,faces,base,rough)
    if key=='flesh':
        for k in range(count):
            points=[]
            for j in range(49):
                x=-1.44+2.88*j/48;y=-1.23+2.46*k/(count-1)+.065*math.sin(3*x+k)
                points.append((x,y,surface(x,y)+height))
            tube('flesh-fold',points,radius,accent,rough,flatten=.38,taper=.25)
    elif key=='bone':
        for k in range(count):
            points=[];y0=-1.15+2.3*k/(count-1)
            for j in range(49):
                x=-1.35+2.7*j/48;y=y0+.11*math.sin(2.3*x+k*.5)
                points.append((x,y,surface(x,y)+height))
            tube('ivory-rib',points,radius,accent,rough,flatten=.9,taper=-.12)
            for x in (-.9,.85):
                y=y0+.11*math.sin(2.3*x+k*.5);z=surface(x,y)+height
                lobe('bone-joint',(x,y,z),(.15,.115,.095),accent,rough,k)
                for n in range(3):
                    px=x+.045*(n-1);py=y+.023*math.sin(n*2+k)
                    lobe('marrow-pore',(px,py,z+.087),(.012,.02,.003),base,.36,n,8,6)
    elif key in ('red_organs','blue_organs'):
        for k in range(count):
            column=k%3;row=k//3;x=-.92+column*.88+.09*math.sin(k*3);y=-1.1+row*.53+.04*math.cos(k*5)
            lobe('organ-lobe',(x,y,surface(x,y)+height),(.39,.29,.14),accent,rough,k,32,18)
            points=[]
            for j in range(25):
                t=j/24;x1=x-.30+.6*t;y1=y+.08*math.sin(t*math.pi*2+k)
                points.append((x1,y1,surface(x,y)+height+.135))
            tube('organ-seam',points,.009,base,rough,sides=6,taper=.7)
    elif key=='veins':
        for k in range(count):
            y0=-1.02+2.04*k/(count-1)
            def branch(x):return y0+.09*math.sin(3*x+k)
            points=[(x,branch(x),surface(x,branch(x))+height) for x in [-1.43+2.86*j/48 for j in range(49)]]
            tube('blue-vein',points,radius,accent,rough,taper=.4)
            for x0 in (-.95,-.32,.32,.93):
                sign=1 if (k+int(x0*10))%2 else -1;points=[]
                for j in range(17):
                    t=j/16;x=x0+.2*t;y=branch(x0)+sign*(.25*t+.035*math.sin(4*t))
                    points.append((x,y,surface(x,y)+height*.8))
                tube('vein-branch',points,radius*.45,accent,rough,sides=8,taper=.6)
    elif key=='ligaments':
        for k in range(count):
            points=[];y0=-1.1+2.2*k/(count-1)
            for j in range(65):
                x=-1.43+2.86*j/64;y=y0+.13*math.sin(3.5*x+k*.28)
                points.append((x,y,surface(x,y)+height+.015*math.cos(8*x+k*.7)))
            tube('ligament-fibre',points,radius,accent,rough,sides=8,taper=.15)
    elif key=='intestines':
        # One continuous meander; each turn joins the next long bowel segment.
        points=[]
        for k in range(count):
            y0=-1.13+2.26*k/(count-1)
            for j in range(49):
                t=j/48;x=(-1.21+2.42*t)*(1 if k%2==0 else -1);y=y0+.035*math.sin(8*t+k)
                points.append((x,y,surface(x,y)+height))
            if k+1<count:
                sign=1 if k%2==0 else -1
                for j in range(1,17):
                    a=math.pi*j/16;x=sign*(1.21+.12*math.sin(a));y=y0+1.13/(count-1)*(1-math.cos(a))
                    points.append((x,y,surface(x,y)+height))
        tube('coiled-intestine',points,radius,accent,rough,sides=12,taper=.07)
    elif key=='villi':
        for j in range(12):
            for i in range(count):
                x=-1.32+2.64*i/(count-1)+.025*math.sin(j*4+i);y=-1.31+2.62*j/11
                z=surface(x,y);h=height*(.8+.2*math.sin(i*2+j*3))
                # Mucosal fingers lean along the lining, so the normal camera
                # sees their length instead of only their tips.
                verts=[];faces=[];sides=8;rings=7
                for ring in range(rings):
                    t=ring/(rings-1);center=Vector((x+.02*t*t,y+h*1.25*t,z+h*t))
                    tangent=Vector((.04*t,h*1.25,h)).normalized()
                    axis=tangent.cross(Vector((0,0,1))).normalized();other=tangent.cross(axis).normalized()
                    r=radius*(1-.2*t)*math.sqrt(max(.0025,1-t**8))
                    for side in range(sides):
                        a=math.tau*side/sides;verts.append(center+r*(axis*math.cos(a)+other*math.sin(a)))
                for ring in range(rings-1):
                    for side in range(sides):
                        a=ring*sides+side;b=ring*sides+(side+1)%sides;faces.append((a,b,b+sides,a+sides))
                faces.extend([tuple(reversed(range(sides))),tuple((rings-1)*sides+i for i in range(sides))])
                mesh_part('intestinal-villus',verts,faces,accent,rough)
    records=[];triangles=[]
    for obj in parts:
        mesh=obj.data;mesh.update();mesh.calc_loop_triangles();offset=len(records);colors=mesh.color_attributes['Color']
        for v in mesh.vertices:
            if any(abs(c)>1.5 for c in v.co):raise ValueError((key,obj.name,tuple(v.co)))
            color=colors.data[v.index].color
            records.append((*v.co,p['ior'],*v.normal,0,*color[:3],color[3]))
        triangles.extend((*[offset+i for i in t.vertices],0) for t in mesh.loop_triangles)
        obj.location.x=mode*4
    with (out/('anatomy_'+key+'.fmesh')).open('wb') as f:
        f.write(struct.pack('<8sII',b'FLMESH01',len(records),len(triangles)))
        for record in records:f.write(struct.pack('<12f',*record))
        for triangle in triangles:f.write(struct.pack('<4I',*triangle))
    print(key,len(records),'vertices',len(triangles),'triangles',flush=True)
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(out/'source/anatomy_backgrounds.blend'),compress=True)
sys.stdout.flush();os._exit(0)
