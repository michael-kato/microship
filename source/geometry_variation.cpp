#include "geometry.h"
#include "asset_json.h"
#include "vector_math.h"
#include <stdio.h>
#include <vector>

namespace {
struct Part { uint32_t first,count;Vec3 pivot,attachment;bool anchor,movable; };
struct Parts { Part values[64];uint32_t count; };
bool load(const char* path,Parts* parts)
{
    JsonDocument doc;if (!json_load(&doc,path)) return false;Json root=json_root(&doc);
    json_object(root,"$schema|vertices|parts");uint32_t vertices=json_uint(json_get(root,"vertices"),3,1000000),next=0;
    JsonArray a=json_array(json_get(root,"parts"),64);Json j={};
    while (json_next(&a,&j))
    {
        json_object(j,"first|count|pivot|movable|attachment");Part p={};
        p.first=json_uint(json_get(j,"first"),0,vertices-1);p.count=json_uint(json_get(j,"count"),1,vertices);
        p.pivot=json_vector(json_get(j,"pivot"),-1.5,1.5);p.movable=json_bool(json_get(j,"movable"));
        Json anchor=json_find(j,"attachment");p.anchor=json_present(anchor);
        if (p.anchor) p.attachment=json_vector(anchor,-1.5,1.5);
        if (p.first!=next || p.count>vertices-next) return json_error(j,"mesh parts must cover vertices in order without gaps or overlap");
        next+=p.count;parts->values[parts->count++]=p;
    }
    if (next!=vertices) json_error(root,"mesh parts do not cover the mesh");
    return doc.valid;
}
float random(uint32_t* seed) { *seed=*seed*1664525u+1013904223u;return (*seed>>8)*(1.0f/8388608)-1; }
Vec3 transform(Vec3 v,const Part& p,const MeshSource& source,uint32_t index)
{
    uint32_t seed=source.seed^(index*747796405u+2891336453u);
    float phase=random(&seed)*3.14159265f;
    if (p.movable)
    {
        Vec3 delta={random(&seed),random(&seed),random(&seed)};
        float angle=random(&seed)*source.part_variation*4,c=std::cos(angle),s=std::sin(angle);
        float size=1+random(&seed)*source.part_variation;
        Vec3 local=scale(subtract(v,p.pivot),size);
        v=add(add(p.pivot,scale(delta,source.part_variation)),Vec3{local.x*c-local.y*s,local.x*s+local.y*c,local.z});
    }
    // A smooth bounded displacement preserves connected surfaces and shared seams.
    float a=source.shape_variation;
    return Vec3{v.x+a*std::sin(3*v.y+phase)*std::sin(4*v.z+phase),
        v.y+a*std::sin(4*v.z-phase)*std::sin(3*v.x+phase),
        v.z+a*std::sin(3*v.x-phase)*std::sin(4*v.y-phase)};
}
}
bool geometry_attachment(const MeshSource& source,Vec3 direction,Vec3* point)
{
    if (!source.parts_path || !source.parts_path[0])
        return geometry_surface_point(source.path,source.position,source.scale,direction,point);
    Parts parts={};if (!load(source.parts_path,&parts)) return false;
    float best=-2;bool found=false;Vec3 chosen={};
    for (uint32_t i=0;i<parts.count;++i) if (parts.values[i].anchor)
    {
        Vec3 v=transform(parts.values[i].attachment,parts.values[i],source,i);float d=dot(normalize(v),direction);
        if (d>best) { best=d;chosen=v;found=true; }
    }
    if (!found) { fprintf(stderr,"%s: no attachment points in mesh parts\n",source.parts_path);return false; }
    *point=add(source.position,scale(chosen,source.scale));return true;
}
bool geometry_vary(GpuVertex* vertices,uint32_t count,const GpuTriangle* triangles,uint32_t triangle_count,const MeshSource& source)
{
    Parts parts={};if (!load(source.parts_path,&parts)) return false;
    const Part& last=parts.values[parts.count-1];if (last.first+last.count!=count)
    { fprintf(stderr,"%s: mesh vertex count does not match parts\n",source.parts_path);return false; }
    for (uint32_t i=0;i<count;++i)
    {
        const GpuVertex& v=vertices[i];float n=0;
        for (uint32_t j=0;j<3;++j)
        { if (!std::isfinite(v.position[j]) || std::fabs(v.position[j])>1.5f || !std::isfinite(v.normal[j])) return false;n+=v.normal[j]*v.normal[j]; }
        if (n<.98f || n>1.02f) return false;
    }
    for (uint32_t p=0;p<parts.count;++p)
    {
        const Part& part=parts.values[p];
        for (uint32_t i=part.first;i<part.first+part.count;++i)
        {
            GpuVertex& v=vertices[i];Vec3 t=transform(Vec3{v.position[0],v.position[1],v.position[2]},part,source,p);
            v.position[0]=t.x;v.position[1]=t.y;v.position[2]=t.z;
            v.normal[0]=v.normal[1]=v.normal[2]=0;
        }
    }
    for (uint32_t i=0;i<triangle_count;++i)
    {
        const uint32_t* t=triangles[i].index;
        if (t[0]>=count || t[1]>=count || t[2]>=count) return false;
        Vec3 p[3];for (uint32_t j=0;j<3;++j) { const float* v=vertices[t[j]].position;p[j]=Vec3{v[0],v[1],v[2]}; }
        Vec3 n=cross(subtract(p[1],p[0]),subtract(p[2],p[0]));
        for (uint32_t j=0;j<3;++j) { float* v=vertices[t[j]].normal;v[0]+=n.x;v[1]+=n.y;v[2]+=n.z; }
    }
    for (uint32_t i=0;i<count;++i)
    {
        float* n=vertices[i].normal;Vec3 v={n[0],n[1],n[2]};if (dot(v,v)<1e-20f) return false;
        v=normalize(v);n[0]=v.x;n[1]=v.y;n[2]=v.z;
    }
    return true;
}
