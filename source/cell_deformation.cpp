#include "presentation.h"
#include "simulation.h"
#include "vector_math.h"
#include <array>
#include <map>
#include <float.h>
#include <stdio.h>

static const uint32_t MEMBRANE_MATERIAL = 3;
static const uint32_t NO_MEMBRANE_GROUP = UINT32_MAX;
static const float GOLDEN_ANGLE = 2.39996323f;
static const float PI_RADIANS = 3.14159265f;
static const float MIN_TRIANGLE_NORMAL_SQUARED = 1e-20f;

// Each peak direction is shared by all wall vertices of one cell.
static void displacement_centers(const Body& body,const FactionDefinition& profile,Vec3* centers)
{
    float phase=((body.seed^profile.displacement_seed)%65536)*(2*PI_RADIANS/65536);
    for (uint32_t k=0;k<profile.displacement_features;++k)
    {
        float z=1-2*(k+.5f)/profile.displacement_features,ring=std::sqrt(1-z*z),angle=k*GOLDEN_ANGLE+phase;
        centers[k]=Vec3{ring*std::cos(angle),ring*std::sin(angle),z};
    }
}

// Evaluate the existing height field at startup. The support test retains a
// rounding margin at the angular boundary; accepted peaks use the same formula.
static Vec3 target_displacement(Vec3 point,const Body& body,const FactionDefinition& profile,const Vec3* centers,float support)
{
    if (!profile.displacement_features && profile.cell_roundness<=0) return Vec3{};
    Vec3 offset=subtract(point,body.position),radial=normalize(offset);
    float radial_change=profile.cell_roundness*(body.radius-std::sqrt(dot(offset,offset))),height=0;
    for (uint32_t k=0;k<profile.displacement_features;++k)
    {
        float alignment=dot(radial,centers[k]);if (alignment<support) continue;
        float distance=std::acos(std::fmax(-1.0f,std::fmin(1.0f,alignment)));
        float t=std::fmax(0.0f,1-distance/profile.displacement_width);
        float shape=profile.displacement_pointed ? t : .5f-.5f*std::cos(t*PI_RADIANS);
        height=std::fmax(height,shape);
    }
    return scale(radial,radial_change+height*body.radius*profile.displacement_height);
}

static void wall_pigment(GpuVertex* v,const Body& body,const BodyDefinition& d)
{
    if (d.wall_noise_strength<=0) return;
    Vec3 q=scale(subtract(Vec3{v->position[0],v->position[1],v->position[2]},body.position),d.wall_noise_frequency/body.radius);
    float phase=(body.seed%65536)*(2*PI_RADIANS/65536);
    float noise=.5f+.25f*std::sin(q.x+1.7f*q.y+.7f*q.z+phase)*std::sin(1.3f*q.z-.6f*q.y+phase)
        +.15f*std::sin(2.1f*q.x-.8f*q.z+phase)*std::cos(1.6f*q.y+.9f*q.z);
    float brightness=1+.12f*std::sin(1.9f*q.x+1.3f*q.y-q.z+phase);
    const float warm[3]={d.wall_noise_warm.r,d.wall_noise_warm.g,d.wall_noise_warm.b};
    const float cool[3]={d.wall_noise_cool.r,d.wall_noise_cool.g,d.wall_noise_cool.b};
    for (uint32_t c=0;c<3;++c)
    {
        float pigment=warm[c]+(cool[c]-warm[c])*noise;
        v->color[c]=std::fmax(0.0f,std::fmin(1.0f,v->color[c]*brightness*(1-d.wall_noise_strength)+pigment*d.wall_noise_strength));
    }
}

bool presentation_prepare_membranes(Presentation* p,const Simulation* s)
{
    bool enabled=false;
    for (uint32_t f=1;f<MAX_FACTIONS;++f) enabled=enabled || s->world.content.factions[f].displacement_features>0 || s->world.content.factions[f].cell_roundness>0;
    for (uint32_t b=0;b<s->world.content.body_count;++b) enabled=enabled || s->world.content.bodies[b].wall_noise_strength>0;
    if (!enabled) return true;
    Geometry& g=p->scene.geometry;p->membrane_groups.resize(g.vertices.size(),NO_MEMBRANE_GROUP);
    for (uint32_t body=0;body<s->world.body_count;++body)
    {
        if (!s->world.content.bodies[s->world.bodies[body].type].hosts_site) continue;
        Vec3 centers[MAX_FACTIONS][MAX_CELL_DISPLACEMENT_FEATURES];float support[MAX_FACTIONS]={};
        for (uint32_t f=1;f<MAX_FACTIONS;++f)
        {
            const FactionDefinition& profile=s->world.content.factions[f];
            displacement_centers(s->world.bodies[body],profile,centers[f]);
            support[f]=std::cos(profile.displacement_width)-FLT_EPSILON;
        }
        MeshRange& mesh=g.meshes[p->body_meshes[body]];
        std::vector<uint8_t> wall(mesh.vertex_count,0);
        for (uint32_t t=mesh.first;t<mesh.first+mesh.count;++t)
            if (g.triangles[t].index[3]==MEMBRANE_MATERIAL)
                for (uint32_t j=0;j<3;++j) wall[g.triangles[t].index[j]-mesh.first_vertex]=1;
        CellMembrane membrane={};membrane.body=body;membrane.first=(uint32_t)p->membrane_vertices.size();
        membrane.first_group=(uint32_t)p->membrane_normals.size();
        std::map<std::array<float,3>,uint32_t> groups;
        for (uint32_t v=0;v<mesh.vertex_count;++v)
        {
            if (!wall[v]) continue;
            MembraneVertex record={};record.index=mesh.first_vertex+v;record.neutral=g.vertices[record.index];
            wall_pigment(&record.neutral,s->world.bodies[body],s->world.content.bodies[s->world.bodies[body].type]);
            g.vertices[record.index]=record.neutral;
            Vec3 point={record.neutral.position[0],record.neutral.position[1],record.neutral.position[2]};
            std::array<float,3> key={{point.x,point.y,point.z}};
            auto inserted=groups.insert(std::make_pair(key,(uint32_t)p->membrane_normals.size()));
            record.group=inserted.first->second;
            if (inserted.second) p->membrane_normals.push_back(Vec3{});
            p->membrane_groups[record.index]=record.group;
            for (uint32_t f=1;f<MAX_FACTIONS;++f)
                record.displacement[f]=target_displacement(point,s->world.bodies[body],s->world.content.factions[f],centers[f],support[f]);
            p->membrane_vertices.push_back(record);
        }
        membrane.count=(uint32_t)p->membrane_vertices.size()-membrane.first;
        membrane.group_count=(uint32_t)p->membrane_normals.size()-membrane.first_group;
        if (!membrane.count) { fprintf(stderr,"Cell displacement requires an authored thin wall.\n");return false; }
        // Reject a wall vertex shared with an organelle surface.
        for (uint32_t t=mesh.first;t<mesh.first+mesh.count;++t)
            if (g.triangles[t].index[3]!=MEMBRANE_MATERIAL)
                for (uint32_t j=0;j<3;++j)
                    if (p->membrane_groups[g.triangles[t].index[j]]!=NO_MEMBRANE_GROUP)
                    { fprintf(stderr,"Cell wall shares vertices with an interior surface.\n");return false; }
        mesh.deformable=true;p->membranes.push_back(membrane);
    }
    printf("Cell wall blend shapes: %u cells, %u existing vertices; fixed topology.\n",
        (uint32_t)p->membranes.size(),(uint32_t)p->membrane_vertices.size());
    return true;
}

static bool update_normals(Presentation* p,const CellMembrane& membrane,const MeshRange& mesh)
{
    Geometry& g=p->scene.geometry;
    for (uint32_t i=membrane.first_group;i<membrane.first_group+membrane.group_count;++i) p->membrane_normals[i]=Vec3{};
    for (uint32_t t=mesh.first;t<mesh.first+mesh.count;++t)
    {
        const GpuTriangle& triangle=g.triangles[t];if (triangle.index[3]!=MEMBRANE_MATERIAL) continue;
        Vec3 point[3];
        for (uint32_t j=0;j<3;++j) { const float* v=g.vertices[triangle.index[j]].position;point[j]=Vec3{v[0],v[1],v[2]}; }
        Vec3 normal=cross(subtract(point[1],point[0]),subtract(point[2],point[0]));
        if (dot(normal,normal)<MIN_TRIANGLE_NORMAL_SQUARED) { fprintf(stderr,"Cell wall has a degenerate triangle.\n");return false; }
        for (uint32_t j=0;j<3;++j)
        { uint32_t group=p->membrane_groups[triangle.index[j]];p->membrane_normals[group]=add(p->membrane_normals[group],normal); }
    }
    for (uint32_t i=membrane.first;i<membrane.first+membrane.count;++i)
    {
        const MembraneVertex& record=p->membrane_vertices[i];Vec3 normal=p->membrane_normals[record.group];
        if (dot(normal,normal)<MIN_TRIANGLE_NORMAL_SQUARED) { fprintf(stderr,"Cell wall has an invalid normal.\n");return false; }
        normal=normalize(normal);float* n=g.vertices[record.index].normal;n[0]=normal.x;n[1]=normal.y;n[2]=normal.z;
    }
    return true;
}

bool presentation_update_membranes(Presentation* p,const Simulation* s)
{
    Geometry& g=p->scene.geometry;
    for (uint32_t m=0;m<p->membranes.size();++m)
    {
        CellMembrane& membrane=p->membranes[m];bool changed=false,neutral=true;
        for (uint32_t f=1;f<MAX_FACTIONS;++f)
        {
            const FactionDefinition& profile=s->world.content.factions[f];
            float weight=s->visible_bodies[membrane.body] && (profile.displacement_features || profile.cell_roundness>0)
                ? s->cell_appearance[membrane.body].factions[f] : 0;
            changed=changed || weight!=membrane.weights[f];neutral=neutral && weight==0;membrane.weights[f]=weight;
        }
        if (!changed) continue;
        for (uint32_t i=membrane.first;i<membrane.first+membrane.count;++i)
        {
            const MembraneVertex& record=p->membrane_vertices[i];GpuVertex& vertex=g.vertices[record.index];vertex=record.neutral;
            if (neutral) continue;
            Vec3 delta={};for (uint32_t f=1;f<MAX_FACTIONS;++f) delta=add(delta,scale(record.displacement[f],membrane.weights[f]));
            vertex.position[0]+=delta.x;vertex.position[1]+=delta.y;vertex.position[2]+=delta.z;
        }
        MeshRange& mesh=g.meshes[p->body_meshes[membrane.body]];
        if (!neutral && !update_normals(p,membrane,mesh)) return false;
        for (uint32_t axis=0;axis<3;++axis) { mesh.low[axis]=FLT_MAX;mesh.high[axis]=-FLT_MAX; }
        for (uint32_t t=mesh.first;t<mesh.first+mesh.count;++t)
            for (uint32_t j=0;j<3;++j) for (uint32_t axis=0;axis<3;++axis)
            {
                float value=g.vertices[g.triangles[t].index[j]].position[axis];
                mesh.low[axis]=std::fmin(mesh.low[axis],value);mesh.high[axis]=std::fmax(mesh.high[axis],value);
            }
        ++mesh.revision;
    }
    return true;
}
