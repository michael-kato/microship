#include "geometry.h"
#include "asset_json.h"
#include "vector_math.h"
#include <stdio.h>
#include <float.h>
#include <vector>

static const uint32_t THIN_WALL_MATERIAL = 3;
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
static const float LAYOUT_TILT_FRACTION = .25f;
static const int DISPLACEMENT_LOW_FREQUENCY = 3;
static const int DISPLACEMENT_HIGH_FREQUENCY = 4;

namespace {
struct Part { uint32_t first,count;Vec3 pivot,attachment;float minimum_scale,radius;bool anchor,movable; };
struct Parts { Part values[MAX_MESH_PARTS];uint32_t count; };
bool load(const char* path,Parts* parts)
{
    JsonDocument doc;if (!json_load(&doc,path)) return false;Json root=json_root(&doc);
    json_object(root,"$schema|vertices|parts");uint32_t vertices=json_uint(json_get(root,"vertices"),3,MAX_PART_VERTEX_COUNT),next=0;
    JsonArray a=json_array(json_get(root,"parts"),MAX_MESH_PARTS);Json j={};
    while (json_next(&a,&j))
    {
        json_object(j,"first|count|pivot|movable|attachment|minimum_scale|attachment_radius");Part p={};
        p.first=json_uint(json_get(j,"first"),0,vertices-1);p.count=json_uint(json_get(j,"count"),1,vertices);
        p.pivot=json_vector(json_get(j,"pivot"),-MAX_LOCAL_COORDINATE,MAX_LOCAL_COORDINATE);p.movable=json_bool(json_get(j,"movable"));
        Json anchor=json_find(j,"attachment");p.anchor=json_present(anchor);
        if (p.anchor) p.attachment=json_vector(anchor,-MAX_LOCAL_COORDINATE,MAX_LOCAL_COORDINATE);
        Json radius=json_find(j,"attachment_radius");if (json_present(radius)) p.radius=json_positive(radius,MAX_LOCAL_COORDINATE);
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
        // Rotate the organ arrangement as one body. This preserves the authored spacing
        // and the distance from the cell center. Meshes and roots use this same transform.
        uint32_t layout_seed=source.seed^PART_SEED_OFFSET;
        float yaw=random(&layout_seed)*source.layout_rotation;
        float tilt=random(&layout_seed)*source.layout_rotation*LAYOUT_TILT_FRACTION;
        Vec3 rotated={v.x*std::cos(yaw)-v.y*std::sin(yaw),v.x*std::sin(yaw)+v.y*std::cos(yaw),v.z};
        v=Vec3{rotated.x,rotated.y*std::cos(tilt)-rotated.z*std::sin(tilt),rotated.y*std::sin(tilt)+rotated.z*std::cos(tilt)};
    }
    // A smooth bounded displacement preserves connected surfaces and shared seams.
    float a=source.shape_variation;
    return Vec3{v.x+a*std::sin(DISPLACEMENT_LOW_FREQUENCY*v.y+phase)*std::sin(DISPLACEMENT_HIGH_FREQUENCY*v.z+phase),
        v.y+a*std::sin(DISPLACEMENT_HIGH_FREQUENCY*v.z-phase)*std::sin(DISPLACEMENT_LOW_FREQUENCY*v.x+phase),
        v.z+a*std::sin(DISPLACEMENT_LOW_FREQUENCY*v.x-phase)*std::sin(DISPLACEMENT_HIGH_FREQUENCY*v.y-phase)};
}
}
bool geometry_attachments(const MeshSource& source,Vec3 direction,Vec3* points,float* radii,uint32_t capacity,uint32_t* count)
{
    *count=0;
    if (!source.parts_path || !source.parts_path[0])
    {
        if (!capacity || !geometry_surface_point(source.path,source.position,source.scale,direction,points)) return false;
        radii[0]=1;*count=1;return true;
    }
    Parts parts={};if (!load(source.parts_path,&parts)) return false;
    for (uint32_t i=0;i<parts.count;++i) if (parts.values[i].anchor && source.scale>=parts.values[i].minimum_scale)
    {
        if (*count==capacity) { fprintf(stderr,"%s: attachment capacity exceeded\n",source.parts_path);return false; }
        const Part& part=parts.values[i];float size=1;
        if (part.movable)
        {
            uint32_t seed=source.seed^(i*PART_SEED_MULTIPLIER+PART_SEED_OFFSET);
            // Match phase, translation and rotation draws before the startup scale.
            for (uint32_t draw=0;draw<5;++draw) random(&seed);
            size+=random(&seed)*source.part_variation;
        }
        radii[*count]=part.radius*size;
        points[(*count)++]=transform(part.attachment,part,source,i);
    }
    if (!*count) { fprintf(stderr,"%s: no active attachment points in mesh parts\n",source.parts_path);return false; }
    // Keep the most forward attachment first. The remaining order is authored.
    uint32_t best=0;
    for (uint32_t i=1;i<*count;++i) if (dot(normalize(points[i]),direction)>dot(normalize(points[best]),direction)) best=i;
    Vec3 first=points[best];points[best]=points[0];points[0]=first;
    float radius=radii[best];radii[best]=radii[0];radii[0]=radius;
    for (uint32_t i=0;i<*count;++i) points[i]=add(source.position,scale(points[i],source.scale));
    return true;
}
bool geometry_self_glow(Geometry* geometry,uint32_t mesh_index,const char* path,float interior,float organelles)
{
    if (interior==0 && organelles==0) return true;
    MeshRange& mesh=geometry->meshes[mesh_index];
    std::vector<uint8_t> wall(mesh.vertex_count,0);
    for (uint32_t t=mesh.first;t<mesh.first+mesh.count;++t)
        if (geometry->triangles[t].index[3]==THIN_WALL_MATERIAL)
            for (uint32_t j=0;j<3;++j) wall[geometry->triangles[t].index[j]-mesh.first_vertex]=1;
    for (uint32_t v=0;v<mesh.vertex_count;++v)
        if (!wall[v]) geometry->vertices[mesh.first_vertex+v].normal[3]+=interior;
    if (!path || !path[0]) return true;
    Parts parts={};if (!load(path,&parts)) return false;
    const Part& last=parts.values[parts.count-1];
    if (last.first+last.count!=mesh.vertex_count) return false;
    for (uint32_t p=0;p<parts.count;++p) if (parts.values[p].movable)
        for (uint32_t v=parts.values[p].first;v<parts.values[p].first+parts.values[p].count;++v)
            if (!wall[v]) geometry->vertices[mesh.first_vertex+v].normal[3]+=organelles-interior;
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


bool geometry_partition_parts(Geometry* g,uint32_t mesh_index,const MeshSource& source,std::vector<MeshPartPose>* output)
{
    if (!source.parts_path || !source.parts_path[0]) return true;
    Parts parts={};if (!load(source.parts_path,&parts)) return false;
    const MeshRange original=g->meshes[mesh_index];
    if (parts.values[parts.count-1].first+parts.values[parts.count-1].count!=original.vertex_count) return false;
    std::vector<uint32_t> groups(original.vertex_count,0);
    for (uint32_t p=0;p<parts.count;++p) if (parts.values[p].movable)
        for (uint32_t v=parts.values[p].first;v<parts.values[p].first+parts.values[p].count;++v) groups[v]=p+1;
    std::vector<GpuTriangle> triangles(g->triangles.begin()+original.first,g->triangles.begin()+original.first+original.count);
    uint32_t counts[MAX_MESH_PARTS+1]={},next[MAX_MESH_PARTS+1]={};
    for (uint32_t t=0;t<triangles.size();++t)
    {
        const GpuTriangle& triangle=triangles[t];uint32_t group=groups[triangle.index[0]-original.first_vertex];
        for (uint32_t j=1;j<3;++j) if (groups[triangle.index[j]-original.first_vertex]!=group)
        { fprintf(stderr,"%s: triangle crosses mesh parts.\n",source.parts_path);return false; }
        if (triangle.index[3]==THIN_WALL_MATERIAL && group)
        { fprintf(stderr,"%s: a movable part contains the cell wall.\n",source.parts_path);return false; }
        ++counts[group];
    }
    if (!counts[0]) { fprintf(stderr,"%s: cell has no stationary surface.\n",source.parts_path);return false; }
    uint32_t first=original.first;
    for (uint32_t group=0;group<=parts.count;++group)
    {
        next[group]=first;
        if (counts[group])
        {
            MeshRange mesh=original;mesh.first=first;mesh.count=counts[group];
            if (!group) g->meshes[mesh_index]=mesh;
            else
            {
                const Part& part=parts.values[group-1];mesh.first_vertex=original.first_vertex+part.first;mesh.vertex_count=part.count;
                for (uint32_t axis=0;axis<3;++axis) { mesh.low[axis]=FLT_MAX;mesh.high[axis]=-FLT_MAX; }
                for (uint32_t v=mesh.first_vertex;v<mesh.first_vertex+mesh.vertex_count;++v)
                    for (uint32_t axis=0;axis<3;++axis)
                    { mesh.low[axis]=std::fmin(mesh.low[axis],g->vertices[v].position[axis]);mesh.high[axis]=std::fmax(mesh.high[axis],g->vertices[v].position[axis]); }
                Vec3 pivot=transform(part.anchor ? part.attachment : part.pivot,part,source,group-1);
                pivot=add(source.position,scale(pivot,source.scale));
                output->push_back(MeshPartPose{(uint32_t)g->meshes.size(),pivot});g->meshes.push_back(mesh);
            }
        }
        first+=counts[group];
    }
    for (uint32_t t=0;t<triangles.size();++t)
    { uint32_t group=groups[triangles[t].index[0]-original.first_vertex];g->triangles[next[group]++]=triangles[t]; }
    return true;
}
