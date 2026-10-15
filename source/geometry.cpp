#include "geometry.h"

#include "render_config.h"
#include "world.h"
#include "vector_math.h"
#include <stdio.h>
#include <cmath>
#include <new>
#include <string.h>

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

static void add_triangle(Geometry* geometry, uint32_t a, uint32_t b, uint32_t c,
    uint32_t emissive)
{
    GpuTriangle triangle = { { a, b, c, emissive } };
    geometry->triangles.push_back(triangle);
}

static bool add_sphere(Geometry* geometry, Vec3 center, float radius, Color color,
    const RenderConfig* config, uint32_t emissive)
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
        add_triangle(geometry, first, first + 1 + next, first + 1 + segment, emissive);
    }
    for (uint32_t ring = 0; ring + 2 < rings; ++ring)
    {
        uint32_t upper = first + 1 + ring * segments;
        uint32_t lower = upper + segments;
        for (uint32_t segment = 0; segment < segments; ++segment)
        {
            uint32_t next = (segment + 1) % segments;
            add_triangle(geometry, upper + segment, upper + next, lower + segment, emissive);
            add_triangle(geometry, upper + next, lower + next, lower + segment, emissive);
        }
    }
    uint32_t last_ring = first + 1 + (rings - 2) * segments;
    for (uint32_t segment = 0; segment < segments; ++segment)
    {
        uint32_t next = (segment + 1) % segments;
        add_triangle(geometry, bottom, last_ring + segment, last_ring + next, emissive);
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
    double light_area = 0.0;
    for (uint32_t i = 0; i < geometry->triangles.size(); ++i)
    {
        double area = triangle_area(geometry, &geometry->triangles[i]);
        float gpu_area = (float)area;
        if (!std::isfinite(gpu_area) || gpu_area <= 0.0f)
        {
            fprintf(stderr, "geometry: triangle %u has an unrepresentable area; check world positions/radii\n", i);
            return false;
        }
        if (i >= geometry->light_first_triangle)
            light_area += area;
    }
    geometry->light_area = (float)light_area;
    if (!std::isfinite(geometry->light_area) || (geometry->light_triangle_count && geometry->light_area <= 0.0f))
    {
        fprintf(stderr, "geometry: star area is not representable\n");
        return false;
    }
    return true;
}

static bool mesh_header(FILE* file, uint32_t* vertex_count, uint32_t* triangle_count)
{
    char magic[8];
    return fread(magic, 1, 8, file) == 8 && memcmp(magic, "FLMESH01", 8) == 0
        && fread(vertex_count, 4, 1, file) == 1 && fread(triangle_count, 4, 1, file) == 1
        && *vertex_count >= 3 && *vertex_count <= 1000000
        && *triangle_count > 0 && *triangle_count <= 2000000;
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
    Color color, float yaw_degrees)
{
    float angle = yaw_degrees * (float)(PI / 180.0);
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
    for (uint32_t i = first_vertex; valid && i < geometry->vertices.size(); ++i)
    {
        GpuVertex* v = &geometry->vertices[i];
        for (uint32_t j = 0; j < 4; ++j)
            valid = valid && std::isfinite(v->position[j]) && std::isfinite(v->normal[j]) && std::isfinite(v->color[j]);
        float normal_squared = v->normal[0]*v->normal[0]+v->normal[1]*v->normal[1]+v->normal[2]*v->normal[2];
        valid = valid && v->position[3] >= 1 && v->position[3] <= 3
            && v->normal[3] >= 0 && v->normal[3] <= 10000
            && normal_squared > .98f && normal_squared < 1.02f
            && v->color[3] >= .02f && v->color[3] <= 1;
        valid = valid && std::fabs(v->position[0]) <= 1.5f && std::fabs(v->position[2]) <= 1.5f;
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
            valid = valid && std::fabs(v->position[j]) <= 1.5f && v->color[j] >= 0 && v->color[j] <= 1;
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

bool geometry_build(Geometry* geometry, const World* world, const RenderConfig* config)
{
    if ((world->body_count == 0 && world->ship_count == 0) || world->body_count > MAX_BODIES
        || world->ship_count > MAX_SHIPS
        || config->sphere_segments < 3 || config->sphere_segments > 512
        || config->sphere_rings < 2 || config->sphere_rings > 256)
    {
        fprintf(stderr, "geometry: invalid world or sphere subdivision count\n");
        return false;
    }
    uint32_t vertices_per_sphere = 2 + (config->sphere_rings - 1) * config->sphere_segments;
    uint32_t triangles_per_sphere = 2 * config->sphere_segments * (config->sphere_rings - 1);
    uint32_t vertex_capacity = world->sun.radius>0 ? vertices_per_sphere : 0;
    uint32_t triangle_capacity = world->sun.radius>0 ? triangles_per_sphere : 0;
    for (uint32_t i = 0; i < world->body_count; ++i)
    {
        uint32_t vertices = vertices_per_sphere, triangles = triangles_per_sphere;
        if (world->bodies[i].model_path[0] && !mesh_counts(world->bodies[i].model_path, &vertices, &triangles)) return false;
        vertex_capacity += vertices;
        triangle_capacity += triangles;
    }
    for (uint32_t i = 0; i < world->ship_count; ++i)
    {
        uint32_t vertices = 0, triangles = 0;
        if (!mesh_counts(world->ships[i].model_path, &vertices, &triangles)) return false;
        vertex_capacity += vertices;
        triangle_capacity += triangles;
    }
    if (world->gameplay.enabled)
    {
        uint32_t vertices=0,triangles=0;
        if (!mesh_counts(world->gameplay.stalk_model,&vertices,&triangles)) return false;
        vertex_capacity+=vertices;triangle_capacity+=triangles;
    }
    if (world->spikes.enabled)
    {
        uint32_t vertices=0,triangles=0;if (!mesh_counts(world->spikes.model,&vertices,&triangles)) return false;
        vertex_capacity+=vertices;triangle_capacity+=triangles;
    }
    if (world->backdrop.model_path[0])
    {
        uint32_t vertices=0,triangles=0;
        if (!mesh_counts(world->backdrop.model_path,&vertices,&triangles)) return false;
        vertex_capacity+=vertices;triangle_capacity+=triangles;
    }
    if (vertex_capacity > 1000000 || triangle_capacity > 2000000)
    {
        fprintf(stderr, "geometry: scene exceeds mesh capacity (one million vertices/two million triangles)\n");
        return false;
    }
    Geometry built = {};
    // These exact bounds prevent push_back allocation during mesh element loops.
    try
    {
        built.vertices.reserve(vertex_capacity);
        built.triangles.reserve(triangle_capacity);
    }
    catch (const std::bad_alloc&)
    {
        fprintf(stderr, "geometry: cannot allocate startup mesh storage\n");
        return false;
    }
    for (uint32_t i = 0; i < world->body_count; ++i)
    {
        const Cell* planet = &world->bodies[i];
        bool added = planet->model_path[0] ? add_model(&built, planet->model_path, planet->position, planet->radius, planet->color, 0)
            : add_sphere(&built, planet->position, planet->radius, planet->color, config, 0);
        if (!added)
        {
            fprintf(stderr, "geometry: nonfinite generated vertex for body %u\n", i);
            return false;
        }
    }
    if (!world->gameplay.enabled)
        for (uint32_t i=0;i<world->ship_count;++i)
        {
            const Ship* ship=&world->ships[i];uint32_t first=(uint32_t)built.triangles.size();
            if (!add_model(&built,ship->model_path,ship->position,ship->scale,ship->color,ship->yaw_degrees)) return false;
            built.ships[i]=MeshRange{first,(uint32_t)built.triangles.size()-first};
        }
    built.backdrop.first=(uint32_t)built.triangles.size();
    if (world->backdrop.model_path[0] && !add_model(&built,world->backdrop.model_path,
        world->backdrop.position,world->backdrop.scale,world->backdrop.color,0)) return false;
    built.backdrop.count=(uint32_t)built.triangles.size()-built.backdrop.first;
    built.static_triangle_count=world->backdrop_soft ? built.backdrop.first : (uint32_t)built.triangles.size();
    for (uint32_t i = 0; world->gameplay.enabled && i < world->ship_count; ++i)
    {
        const Ship* ship = &world->ships[i];
        bool shared=false;
        if (world->gameplay.enabled)
            for (uint32_t j=0;j<i;++j)
                if (!strcmp(ship->model_path,world->ships[j].model_path)
                    && !memcmp(&ship->color,&world->ships[j].color,sizeof(Color)))
                { built.ships[i]=built.ships[j];shared=true;break; }
        if (shared) continue;
        uint32_t first=(uint32_t)built.triangles.size();
        Vec3 position=world->gameplay.enabled ? Vec3{0,0,0} : ship->position;
        if (!add_model(&built, ship->model_path, position, world->gameplay.enabled ? 1.0f : ship->scale,
                ship->color,world->gameplay.enabled ? 0.0f : ship->yaw_degrees))
            return false;
        built.ships[i]=MeshRange{first,(uint32_t)built.triangles.size()-first};
    }
    if (world->gameplay.enabled)
    {
        built.stalk.first=(uint32_t)built.triangles.size();
        if (!add_model(&built,world->gameplay.stalk_model,Vec3{0,0,0},1,Color{1,1,1},0)) return false;
        built.stalk.count=(uint32_t)built.triangles.size()-built.stalk.first;
    }
    if (world->spikes.enabled)
    {
        built.spike.first=(uint32_t)built.triangles.size();
        if (!add_model(&built,world->spikes.model,Vec3{0,0,0},1,Color{1,1,1},0)) return false;
        built.spike.count=(uint32_t)built.triangles.size()-built.spike.first;
    }
    built.light_first_triangle = (uint32_t)built.triangles.size();
    built.light_triangle_count = world->sun.radius>0 ? triangles_per_sphere : 0;
    if (world->sun.radius>0 && !add_sphere(&built, world->sun.position, world->sun.radius, world->sun.color, config, 1))
    {
        fprintf(stderr, "geometry: nonfinite generated star vertex\n");
        return false;
    }
    if (!validate_areas(&built))
        return false;
    geometry->vertices.swap(built.vertices);
    geometry->triangles.swap(built.triangles);
    geometry->light_first_triangle = built.light_first_triangle;
    geometry->light_triangle_count = built.light_triangle_count;
    geometry->light_area = built.light_area;
    geometry->static_triangle_count=built.static_triangle_count;
    memcpy(geometry->ships,built.ships,sizeof(built.ships));geometry->stalk=built.stalk;
    geometry->backdrop=built.backdrop;geometry->spike=built.spike;
    return true;
}

bool geometry_cell_surface_point(const Cell* planet, Vec3* point)
{
    if (!planet->model_path[0])
    { *point=add(planet->position,scale(planet->colony_normal,planet->radius));return true; }
    Geometry geometry={};uint32_t vertices=0,triangles=0;
    if (!mesh_counts(planet->model_path,&vertices,&triangles)) return false;
    try { geometry.vertices.reserve(vertices);geometry.triangles.reserve(triangles); }
    catch (const std::bad_alloc&) { fprintf(stderr,"Cannot allocate colony terrain query.\n");return false; }
    if (!add_model(&geometry,planet->model_path,Vec3{0,0,0},planet->radius,Color{1,1,1},0)) return false;
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
        Vec3 h=cross(planet->colony_normal,edge2);float determinant=dot(edge1,h);
        if (std::fabs(determinant)<1e-8f) continue;
        Vec3 offset=scale(v[0],-1),q=cross(offset,edge1);
        float u=dot(offset,h)/determinant,vv=dot(planet->colony_normal,q)/determinant;
        if (u<0 || vv<0 || u+vv>1) continue;
        surface=std::fmax(surface,dot(edge2,q)/determinant);
    }
    if (surface<=0) { fprintf(stderr,"Colony site ray missed planet mesh.\n");return false; }
    *point=add(planet->position,scale(planet->colony_normal,surface-.015f));return true;
}
