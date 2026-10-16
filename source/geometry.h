#ifndef MICROSHIP_GEOMETRY_H
#define MICROSHIP_GEOMETRY_H

#include <stdint.h>
#include <vector>
#include "types.h"

struct RenderConfig;

// Matches std430. Surface tags: 0 dielectric, 2 conductor, 3 thin transmission.
struct GpuVertex { float position[4],normal[4],color[4]; };
struct GpuTriangle { uint32_t index[4]; };
struct MeshRange { uint32_t first,count; float low[3],high[3]; };
struct MeshSource
{
    const char* path; // An empty path requests an authored sphere primitive.
    Vec3 position;
    float scale,yaw;
    Color color;
};
struct Geometry
{
    std::vector<GpuVertex> vertices;
    std::vector<GpuTriangle> triangles;
    std::vector<MeshRange> meshes;
};

bool geometry_build(Geometry*,const MeshSource*,uint32_t count,const RenderConfig*);
bool geometry_surface_point(const char* path,Vec3 position,float scale,Vec3 direction,Vec3* point);

#endif
