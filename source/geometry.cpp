#include "geometry.h"

#include "render_config.h"
#include "vector_math.h"
#include <stdio.h>
#include <cmath>
#include <new>
#include <float.h>
#include <string.h>

static const int MESH_MAGIC_BYTES = 8;
static const int MAX_MESH_VERTICES = 1000000;
static const int MAX_MESH_TRIANGLES = 2000000;
static const double HALF_TURN_DEGREES = 180.0;
static const int MAX_VERTEX_EMISSION = 10000;
static const float MIN_NORMAL_SQUARED_LENGTH = .98f;
static const float MAX_NORMAL_SQUARED_LENGTH = 1.02f;
static const float MIN_MATERIAL_ROUGHNESS = .02f;
static const float MAX_LOCAL_COORDINATE = 1.5f;
static const int MAX_SCENE_MESHES = 128;
static const int MAX_SPHERE_SEGMENTS = 512;
static const int MAX_SPHERE_RINGS = 256;
static const float SURFACE_DETERMINANT_EPSILON = 1e-8f;
static const float FACTORY_SURFACE_INSET = .015f;

static const double PI = 3.14159265358979323846;

static bool add_vertex(Geometry* geometry, Vec3 center, float radius, Color color,
    double x, double y, double z)
{
    GpuVertex vertex = {
        { (float)(center.x + radius * x), (float)(center.y + radius * y),
          (float)(center.z + radius * z), 1.0f },
        { (float)x, (float)y, (float)z, 0.0f },
        { color.r, color.g, color.b, 1.0f }
    };
    if (!std::isfinite(vertex.position[0]) || !std::isfinite(vertex.position[1])
        || !std::isfinite(vertex.position[2]))
        return false;
    geometry->vertices.push_back(vertex);
    return true;
}

static void add_triangle(Geometry* geometry, uint32_t a, uint32_t b, uint32_t c)
{
    GpuTriangle triangle = { { a, b, c, 0 } };
    geometry->triangles.push_back(triangle);
}

static bool add_sphere(Geometry* geometry, Vec3 center, float radius, Color color,
    const RenderConfig* config)
{
    uint32_t first = (uint32_t)geometry->vertices.size();
    uint32_t segments = config->sphere_segments;
    uint32_t rings = config->sphere_rings;
    if (!add_vertex(geometry, center, radius, color, 0.0, 1.0, 0.0))
        return false;
    for (uint32_t ring = 1; ring < rings; ++ring)
    {
        double phi = PI * ring / rings;
        double radial = std::sin(phi);
        double y = std::cos(phi);
        for (uint32_t segment = 0; segment < segments; ++segment)
        {
            double theta = 2.0 * PI * segment / segments;
            if (!add_vertex(geometry, center, radius, color,
                radial * std::cos(theta), y, radial * std::sin(theta)))
                return false;
        }
    }
    uint32_t bottom = (uint32_t)geometry->vertices.size();
    if (!add_vertex(geometry, center, radius, color, 0.0, -1.0, 0.0))
        return false;

    // Each pole has a single vertex: no zero-area triangles at the UV seam or poles.
    for (uint32_t segment = 0; segment < segments; ++segment)
    {
        uint32_t next = (segment + 1) % segments;
        add_triangle(geometry, first, first + 1 + next, first + 1 + segment);
    }
    for (uint32_t ring = 0; ring + 2 < rings; ++ring)
    {
        uint32_t upper = first + 1 + ring * segments;
        uint32_t lower = upper + segments;
        for (uint32_t segment = 0; segment < segments; ++segment)
        {
            uint32_t next = (segment + 1) % segments;
            add_triangle(geometry, upper + segment, upper + next, lower + segment);
            add_triangle(geometry, upper + next, lower + next, lower + segment);
        }
    }
    uint32_t last_ring = first + 1 + (rings - 2) * segments;
    for (uint32_t segment = 0; segment < segments; ++segment)
    {
        uint32_t next = (segment + 1) % segments;
        add_triangle(geometry, bottom, last_ring + segment, last_ring + next);
    }
    return true;
}

static double triangle_area(const Geometry* geometry, const GpuTriangle* triangle)
{
    const float* a = geometry->vertices[triangle->index[0]].position;
    const float* b = geometry->vertices[triangle->index[1]].position;
    const float* c = geometry->vertices[triangle->index[2]].position;
    double ux = (double)b[0] - a[0];
    double uy = (double)b[1] - a[1];
    double uz = (double)b[2] - a[2];
    double vx = (double)c[0] - a[0];
    double vy = (double)c[1] - a[1];
    double vz = (double)c[2] - a[2];
    double x = uy * vz - uz * vy;
    double y = uz * vx - ux * vz;
    double z = ux * vy - uy * vx;
    return 0.5 * std::sqrt(x * x + y * y + z * z);
}

static bool validate_areas(Geometry* geometry)
{
    for (uint32_t i = 0; i < geometry->triangles.size(); ++i)
    {
        double area = triangle_area(geometry, &geometry->triangles[i]);
        float gpu_area = (float)area;
        if (!std::isfinite(gpu_area) || gpu_area <= 0.0f)
        {
            fprintf(stderr, "geometry: triangle %u has an unrepresentable area; check source transforms\n", i);
            return false;
        }
    }
    return true;
}

static bool mesh_header(FILE* file, uint32_t* vertex_count, uint32_t* triangle_count)
{
    char magic[MESH_MAGIC_BYTES];
    return fread(magic, 1, MESH_MAGIC_BYTES, file) == MESH_MAGIC_BYTES && memcmp(magic, "FLMESH01", MESH_MAGIC_BYTES) == 0
        && fread(vertex_count, 4, 1, file) == 1 && fread(triangle_count, 4, 1, file) == 1
        && *vertex_count >= 3 && *vertex_count <= MAX_MESH_VERTICES
        && *triangle_count > 0 && *triangle_count <= MAX_MESH_TRIANGLES;
}

static bool mesh_counts(const char* path, uint32_t* vertices, uint32_t* triangles)
{
    FILE* file = fopen(path, "rb");
    bool valid = file && mesh_header(file, vertices, triangles);
    if (file) fclose(file);
    if (!valid) fprintf(stderr, "%s: cannot read a valid FLMESH01 mesh header\n", path);
    return valid;
}

static bool add_model(Geometry* geometry, const char* path, Vec3 position, float scale,
    Color color, float yaw_degrees,const MeshSource* source=0)
{
    float angle = yaw_degrees * (float)(PI / HALF_TURN_DEGREES);
    float cosine = std::cos(angle), sine = std::sin(angle);
    FILE* file = fopen(path, "rb");
    uint32_t vertex_count = 0, triangle_count = 0;
    bool valid = file && mesh_header(file, &vertex_count, &triangle_count);
    uint32_t first_vertex = (uint32_t)geometry->vertices.size();
    uint32_t first_triangle = (uint32_t)geometry->triangles.size();
    valid = valid && first_vertex + vertex_count <= geometry->vertices.capacity()
        && first_triangle + triangle_count <= geometry->triangles.capacity();
    if (valid)
    {
        geometry->vertices.resize(first_vertex + vertex_count);
        geometry->triangles.resize(first_triangle + triangle_count);
        valid = fread(&geometry->vertices[first_vertex], sizeof(GpuVertex), vertex_count, file) == vertex_count
            && fread(&geometry->triangles[first_triangle], sizeof(GpuTriangle), triangle_count, file) == triangle_count
            && fgetc(file) == EOF && !ferror(file);
    }
    if (file && fclose(file) != 0) valid = false;
    if (valid && source && source->parts_path && source->parts_path[0])
    {
        valid=geometry_vary(&geometry->vertices[first_vertex],vertex_count,&geometry->triangles[first_triangle],&triangle_count,*source);
        geometry->triangles.resize(first_triangle+triangle_count);
    }
    for (uint32_t i = first_vertex; valid && i < geometry->vertices.size(); ++i)
    {
        GpuVertex* v = &geometry->vertices[i];
        for (uint32_t j = 0; j < 4; ++j)
            valid = valid && std::isfinite(v->position[j]) && std::isfinite(v->normal[j]) && std::isfinite(v->color[j]);
        float normal_squared = v->normal[0]*v->normal[0]+v->normal[1]*v->normal[1]+v->normal[2]*v->normal[2];
        valid = valid && v->position[3] >= 1 && v->position[3] <= 3
            && v->normal[3] >= 0 && v->normal[3] <= MAX_VERTEX_EMISSION
            && normal_squared > MIN_NORMAL_SQUARED_LENGTH && normal_squared < MAX_NORMAL_SQUARED_LENGTH
            && v->color[3] >= MIN_MATERIAL_ROUGHNESS && v->color[3] <= 1;
        valid = valid && std::fabs(v->position[0]) <= MAX_LOCAL_COORDINATE && std::fabs(v->position[2]) <= MAX_LOCAL_COORDINATE;
        float x = v->position[0], z = v->position[2];
        v->position[0] = cosine * x + sine * z;
        v->position[2] = -sine * x + cosine * z;
        x = v->normal[0]; z = v->normal[2];
        v->normal[0] = cosine * x + sine * z;
        v->normal[2] = -sine * x + cosine * z;
        const float center[] = {position.x, position.y, position.z};
        const float tint[] = {color.r, color.g, color.b};
        for (uint32_t j = 0; j < 3; ++j)
        {
            valid = valid && std::fabs(v->position[j]) <= MAX_LOCAL_COORDINATE && v->color[j] >= 0 && v->color[j] <= 1;
            v->position[j] = center[j] + scale * v->position[j];
            v->color[j] *= tint[j];
            valid = valid && std::isfinite(v->position[j]);
        }
    }
    for (uint32_t i = first_triangle; valid && i < geometry->triangles.size(); ++i)
    {
        GpuTriangle* t = &geometry->triangles[i];
        valid = t->index[3] == 0 || t->index[3] == 2 || t->index[3] == 3;
        for (uint32_t j = 0; j < 3; ++j)
        {
            valid = valid && t->index[j] < vertex_count;
            t->index[j] += first_vertex;
        }
    }
    if (!valid) fprintf(stderr, "%s: invalid vertex-colored mesh data\n", path);
    return valid;
}

bool geometry_build(Geometry* geometry,const MeshSource* sources,uint32_t count,const RenderConfig* config)
{
    if (!count || count>MAX_SCENE_MESHES || config->sphere_segments<3 || config->sphere_segments>MAX_SPHERE_SEGMENTS
        || config->sphere_rings<2 || config->sphere_rings>MAX_SPHERE_RINGS)
    { fprintf(stderr,"geometry: invalid mesh count or sphere subdivisions\n");return false; }
    uint32_t vertex_capacity=0,triangle_capacity=0;
    for (uint32_t i=0;i<count;++i)
    {
        uint32_t vertices=2+(config->sphere_rings-1)*config->sphere_segments;
        uint32_t triangles=2*config->sphere_segments*(config->sphere_rings-1);
        if (sources[i].path[0] && !mesh_counts(sources[i].path,&vertices,&triangles)) return false;
        if (vertices>MAX_MESH_VERTICES-vertex_capacity || triangles>MAX_MESH_TRIANGLES-triangle_capacity)
        { fprintf(stderr,"geometry: scene exceeds mesh capacity\n");return false; }
        vertex_capacity+=vertices;triangle_capacity+=triangles;
    }
    Geometry built={};
    try
    {
        built.vertices.reserve(vertex_capacity);built.triangles.reserve(triangle_capacity);
        built.meshes.resize(count);
    }
    catch (const std::bad_alloc&)
    { fprintf(stderr,"geometry: cannot allocate startup mesh storage\n");return false; }
    for (uint32_t i=0;i<count;++i)
    {
        const MeshSource* source=&sources[i];MeshRange* range=&built.meshes[i];
        range->first=(uint32_t)built.triangles.size();
        bool valid=source->path[0] ? add_model(&built,source->path,source->position,source->scale,source->color,source->yaw,source)
            : add_sphere(&built,source->position,source->scale,source->color,config);
        if (!valid) return false;
        range->count=(uint32_t)built.triangles.size()-range->first;
        for (uint32_t axis=0;axis<3;++axis) { range->low[axis]=FLT_MAX;range->high[axis]=-FLT_MAX; }
        for (uint32_t t=range->first;t<range->first+range->count;++t)
            for (uint32_t v=0;v<3;++v)
                for (uint32_t axis=0;axis<3;++axis)
                {
                    float coordinate=built.vertices[built.triangles[t].index[v]].position[axis];
                    range->low[axis]=std::fmin(range->low[axis],coordinate);
                    range->high[axis]=std::fmax(range->high[axis],coordinate);
                }
    }
    if (!validate_areas(&built)) return false;
    geometry->vertices.swap(built.vertices);geometry->triangles.swap(built.triangles);geometry->meshes.swap(built.meshes);
    return true;
}

bool geometry_surface_point(const char* path,Vec3 position,float radius,Vec3 normal,Vec3* point)
{
    if (!path[0])
    { *point=add(position,scale(normal,radius));return true; }
    Geometry geometry={};uint32_t vertices=0,triangles=0;
    if (!mesh_counts(path,&vertices,&triangles)) return false;
    try { geometry.vertices.reserve(vertices);geometry.triangles.reserve(triangles); }
    catch (const std::bad_alloc&) { fprintf(stderr,"Cannot allocate surface query.\n");return false; }
    if (!add_model(&geometry,path,Vec3{0,0,0},radius,Color{1,1,1},0)) return false;
    float surface=0;
    for (uint32_t i=0;i<triangles;++i)
    {
        Vec3 v[3];
        for (uint32_t j=0;j<3;++j)
        {
            const float* p=geometry.vertices[geometry.triangles[i].index[j]].position;
            v[j]=Vec3{p[0],p[1],p[2]};
        }
        Vec3 edge1=subtract(v[1],v[0]),edge2=subtract(v[2],v[0]);
        Vec3 h=cross(normal,edge2);float determinant=dot(edge1,h);
        if (std::fabs(determinant)<SURFACE_DETERMINANT_EPSILON) continue;
        Vec3 offset=scale(v[0],-1),q=cross(offset,edge1);
        float u=dot(offset,h)/determinant,vv=dot(normal,q)/determinant;
        if (u<0 || vv<0 || u+vv>1) continue;
        surface=std::fmax(surface,dot(edge2,q)/determinant);
    }
    if (surface<=0) { fprintf(stderr,"Surface ray missed mesh.\n");return false; }
    *point=add(position,scale(normal,surface-FACTORY_SURFACE_INSET));return true;
}
