#ifndef NANOVA_GEOMETRY_H
#define NANOVA_GEOMETRY_H

#include <stdint.h>
#include <vector>
#include "types.h"

struct RenderConfig;

// Matches std430. Surface tags: 0 dielectric, 2 conductor, 3 thin transmission.
struct GpuVertex { float position[4],normal[4],color[4],subsurface[4]; };
struct GpuTriangle { uint32_t index[4]; };
struct MeshRange
{
    uint32_t first,count,first_vertex,vertex_count;
    float low[3],high[3];
    bool deformable;
    uint64_t revision;
};
struct MeshSource
{
    const char* path; // An empty path requests an authored sphere primitive.
    Vec3 position;
    float scale,yaw;
    Color color;
    const char* parts_path;
    uint32_t seed;
    float shape_variation,part_variation;
};
struct Geometry
{
    std::vector<GpuVertex> vertices;
    std::vector<GpuTriangle> triangles;
    std::vector<MeshRange> meshes;
};

bool geometry_build(Geometry*,const MeshSource*,uint32_t count,const RenderConfig*);
bool geometry_validate_model(const char* path); // Startup validation of unselected authored alternatives.
bool geometry_surface_point(const char* path,Vec3 position,float scale,Vec3 direction,Vec3* point);
// The same startup transform is used for mesh parts and their attachment points.
bool geometry_attachments(const MeshSource&,Vec3 direction,Vec3* points,uint32_t capacity,uint32_t* count);
// Assign current-scene surface emission at startup; keep thin walls clear.
struct MeshPartPose { uint32_t mesh;Vec3 pivot; };
// Partition existing triangles into immutable part meshes. No new vertices or triangles.
bool geometry_partition_parts(Geometry*,uint32_t mesh,const MeshSource&,std::vector<MeshPartPose>*);
bool geometry_self_glow(Geometry*,uint32_t mesh,const char* parts,float interior,float organelles);
bool geometry_vary(GpuVertex*,uint32_t,GpuTriangle*,uint32_t*,const MeshSource&);

#endif
