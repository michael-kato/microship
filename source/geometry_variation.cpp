#include "geometry.h"
#include "asset_json.h"
#include "vector_math.h"
#include <stdio.h>
#include <vector>

static const int MAX_MESH_PARTS = 64;
static const int MAX_PART_VERTEX_COUNT = 1000000;
static const double MAX_LOCAL_COORDINATE = 1.5;
static const int MAX_ORGAN_MINIMUM_SCALE = 10000;
static const unsigned int RANDOM_MULTIPLIER = 1664525u;
static const unsigned int RANDOM_INCREMENT = 1013904223u;
static const int RANDOM_DISCARDED_BITS = 8;
static const int RANDOM_SIGNED_HALF_RANGE = 8388608;
static const unsigned int PART_SEED_MULTIPLIER = 747796405u;
static const unsigned int PART_SEED_OFFSET = 2891336453u;
static const float HALF_TURN_RADIANS = 3.14159265f;
static const float MAX_VERTEX_LOCAL_COORDINATE = 1.5f;
static const float MIN_AUTHORED_NORMAL_SQUARED_LENGTH = .98f;
static const float MAX_AUTHORED_NORMAL_SQUARED_LENGTH = 1.02f;
static const float MIN_GENERATED_NORMAL_SQUARED_LENGTH = 1e-20f;
static const int PART_ROTATION_RADIANS_PER_VARIATION = 4;
static const int DISPLACEMENT_LOW_FREQUENCY = 3;
static const int DISPLACEMENT_HIGH_FREQUENCY = 4;

namespace {
struct Part { uint32_t first,count;Vec3 pivot,attachment;float minimum_scale;bool anchor,movable; };
struct Parts { Part values[MAX_MESH_PARTS];uint32_t count; };
bool load(const char* path,Parts* parts)
{
    JsonDocument doc;if (!json_load(&doc,path)) return false;Json root=json_root(&doc);
    json_object(root,"$schema|vertices|parts");uint32_t vertices=json_uint(json_get(root,"vertices"),3,MAX_PART_VERTEX_COUNT),next=0;
    JsonArray a=json_array(json_get(root,"parts"),MAX_MESH_PARTS);Json j={};
    while (json_next(&a,&j))
    {
        json_object(j,"first|count|pivot|movable|attachment|minimum_scale");Part p={};
        p.first=json_uint(json_get(j,"first"),0,vertices-1);p.count=json_uint(json_get(j,"count"),1,vertices);
        p.pivot=json_vector(json_get(j,"pivot"),-MAX_LOCAL_COORDINATE,MAX_LOCAL_COORDINATE);p.movable=json_bool(json_get(j,"movable"));
        Json anchor=json_find(j,"attachment");p.anchor=json_present(anchor);
        if (p.anchor) p.attachment=json_vector(anchor,-MAX_LOCAL_COORDINATE,MAX_LOCAL_COORDINATE);
        Json minimum=json_find(j,"minimum_scale");
        if (json_present(minimum)) p.minimum_scale=json_float(minimum,0,MAX_ORGAN_MINIMUM_SCALE);
        if (p.first!=next || p.count>vertices-next) return json_error(j,"mesh parts must cover vertices in order without gaps or overlap");
        next+=p.count;parts->values[parts->count++]=p;
    }
    if (next!=vertices) json_error(root,"mesh parts do not cover the mesh");
    return doc.valid;
}
float random(uint32_t* seed) { *seed=*seed*RANDOM_MULTIPLIER+RANDOM_INCREMENT;return (*seed>>RANDOM_DISCARDED_BITS)*(1.0f/RANDOM_SIGNED_HALF_RANGE)-1; }
Vec3 transform(Vec3 v,const Part& p,const MeshSource& source,uint32_t index)
{
    uint32_t seed=source.seed^(index*PART_SEED_MULTIPLIER+PART_SEED_OFFSET);
    float phase=random(&seed)*HALF_TURN_RADIANS;
    if (p.movable)
    {
        Vec3 delta={random(&seed),random(&seed),random(&seed)};
        float angle=random(&seed)*source.part_variation*PART_ROTATION_RADIANS_PER_VARIATION,c=std::cos(angle),s=std::sin(angle);
        float size=1+random(&seed)*source.part_variation;
        Vec3 local=scale(subtract(v,p.pivot),size);
        v=add(add(p.pivot,scale(delta,source.part_variation)),Vec3{local.x*c-local.y*s,local.x*s+local.y*c,local.z});
    }
    // A smooth bounded displacement preserves connected surfaces and shared seams.
    float a=source.shape_variation;
    return Vec3{v.x+a*std::sin(DISPLACEMENT_LOW_FREQUENCY*v.y+phase)*std::sin(DISPLACEMENT_HIGH_FREQUENCY*v.z+phase),
        v.y+a*std::sin(DISPLACEMENT_HIGH_FREQUENCY*v.z-phase)*std::sin(DISPLACEMENT_LOW_FREQUENCY*v.x+phase),
        v.z+a*std::sin(DISPLACEMENT_LOW_FREQUENCY*v.x-phase)*std::sin(DISPLACEMENT_HIGH_FREQUENCY*v.y-phase)};
}
}
bool geometry_attachments(const MeshSource& source,Vec3 direction,Vec3* points,uint32_t capacity,uint32_t* count)
{
    *count=0;
    if (!source.parts_path || !source.parts_path[0])
    {
        if (!capacity || !geometry_surface_point(source.path,source.position,source.scale,direction,points)) return false;
        *count=1;return true;
    }
    Parts parts={};if (!load(source.parts_path,&parts)) return false;
    for (uint32_t i=0;i<parts.count;++i) if (parts.values[i].anchor && source.scale>=parts.values[i].minimum_scale)
    {
        if (*count==capacity) { fprintf(stderr,"%s: attachment capacity exceeded\n",source.parts_path);return false; }
        points[(*count)++]=transform(parts.values[i].attachment,parts.values[i],source,i);
    }
    if (!*count) { fprintf(stderr,"%s: no active attachment points in mesh parts\n",source.parts_path);return false; }
    // Keep the most forward attachment first. The remaining order is authored.
    uint32_t best=0;
    for (uint32_t i=1;i<*count;++i) if (dot(normalize(points[i]),direction)>dot(normalize(points[best]),direction)) best=i;
    Vec3 first=points[best];points[best]=points[0];points[0]=first;
    for (uint32_t i=0;i<*count;++i) points[i]=add(source.position,scale(points[i],source.scale));
    return true;
}
bool geometry_vary(GpuVertex* vertices,uint32_t count,GpuTriangle* triangles,uint32_t* triangle_count,const MeshSource& source)
{
    Parts parts={};if (!load(source.parts_path,&parts)) return false;
    const Part& last=parts.values[parts.count-1];if (last.first+last.count!=count)
    { fprintf(stderr,"%s: mesh vertex count does not match parts\n",source.parts_path);return false; }
    for (uint32_t i=0;i<count;++i)
    {
        const GpuVertex& v=vertices[i];float n=0;
        for (uint32_t j=0;j<3;++j)
        { if (!std::isfinite(v.position[j]) || std::fabs(v.position[j])>MAX_VERTEX_LOCAL_COORDINATE || !std::isfinite(v.normal[j])) return false;n+=v.normal[j]*v.normal[j]; }
        if (n<MIN_AUTHORED_NORMAL_SQUARED_LENGTH || n>MAX_AUTHORED_NORMAL_SQUARED_LENGTH) return false;
    }
    std::vector<uint8_t> enabled(count,0);
    for (uint32_t p=0;p<parts.count;++p)
    {
        const Part& part=parts.values[p];
        for (uint32_t i=part.first;i<part.first+part.count;++i)
        {
            GpuVertex& v=vertices[i];Vec3 t=transform(Vec3{v.position[0],v.position[1],v.position[2]},part,source,p);
            v.position[0]=t.x;v.position[1]=t.y;v.position[2]=t.z;
            enabled[i]=source.scale>=part.minimum_scale;
            if (enabled[i]) v.normal[0]=v.normal[1]=v.normal[2]=0;
        }
    }
    uint32_t kept=0;
    for (uint32_t i=0;i<*triangle_count;++i)
    {
        const uint32_t* t=triangles[i].index;
        if (t[0]>=count || t[1]>=count || t[2]>=count) return false;
        if (!enabled[t[0]] || !enabled[t[1]] || !enabled[t[2]]) continue;
        Vec3 p[3];for (uint32_t j=0;j<3;++j) { const float* v=vertices[t[j]].position;p[j]=Vec3{v[0],v[1],v[2]}; }
        Vec3 n=cross(subtract(p[1],p[0]),subtract(p[2],p[0]));
        for (uint32_t j=0;j<3;++j) { float* v=vertices[t[j]].normal;v[0]+=n.x;v[1]+=n.y;v[2]+=n.z; }
        triangles[kept++]=triangles[i];
    }
    *triangle_count=kept;
    for (uint32_t i=0;i<count;++i)
    {
        if (!enabled[i]) continue;
        float* n=vertices[i].normal;Vec3 v={n[0],n[1],n[2]};if (dot(v,v)<MIN_GENERATED_NORMAL_SQUARED_LENGTH) return false;
        v=normalize(v);n[0]=v.x;n[1]=v.y;n[2]=v.z;
    }
    return true;
}
