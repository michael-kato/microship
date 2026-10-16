#ifndef MICROSHIP_PRESENTATION_H
#define MICROSHIP_PRESENTATION_H
#include "render_scene.h"
#include "world.h"
struct Simulation;
struct Camera;
struct RenderConfig;

// Game code owns these mappings. The renderer receives RenderScene only.
struct MembraneVertex { uint32_t index,group;GpuVertex neutral;Vec3 displacement[MAX_FACTIONS]; };
struct CellMembrane { uint32_t body,first,count,first_group,group_count;float weights[MAX_FACTIONS]; };
struct Presentation
{
    RenderScene scene;
    std::vector<CellMembrane> membranes;
    std::vector<MembraneVertex> membrane_vertices;
    std::vector<uint32_t> membrane_groups;
    std::vector<Vec3> membrane_normals;
    uint32_t body_meshes[MAX_BODIES],unit_meshes[MAX_UNIT_TYPES],factory_meshes[MAX_PRODUCER_TYPES],spike_mesh,background_mesh;
    uint32_t segment_meshes[MAX_PRODUCER_TYPES],tip_meshes[MAX_PRODUCER_TYPES],stem_base,bud_base,debris_base;
    uint32_t unit_base,body_base,factory_base,attached_base,flying_base,embedded_base,background_slot;
    int32_t medium_bodies[MAX_BODIES+1];
    uint32_t attached_spikes,flying_spikes,embedded_spikes;
};
bool presentation_initialize(Presentation*,const Simulation*,const RenderConfig*);
bool presentation_build(Presentation*,const Simulation*,const Camera*,const RenderConfig*);
bool presentation_prepare_membranes(Presentation*,const Simulation*);
bool presentation_update_membranes(Presentation*,const Simulation*);
#endif
