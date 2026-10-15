#ifndef MICROSHIP_GEOMETRY_H
#define MICROSHIP_GEOMETRY_H

#include <stdint.h>
#include <vector>

struct World;
struct RenderConfig;
struct Cell;
struct Vec3;

// Matches std430 records. Triangle tag: 0 dielectric, 1 sun, 2 conductor, 3 thin cell membrane.
struct GpuVertex
{
    float position[4];
    float normal[4];
    float color[4];
};

struct GpuTriangle
{
    uint32_t index[4];
};
struct MeshRange { uint32_t first, count; };

struct Geometry
{
    std::vector<GpuVertex> vertices;
    std::vector<GpuTriangle> triangles;
    uint32_t light_first_triangle;
    uint32_t light_triangle_count;
    float light_area;
    uint32_t static_triangle_count;
    MeshRange ships[64], stalk, backdrop,spike;
};

// Generates one startup mesh. Geometry contains no rendering API resources.
bool geometry_build(Geometry* geometry, const World* world, const RenderConfig* config);
bool geometry_cell_surface_point(const Cell* planet, Vec3* point);

#endif
