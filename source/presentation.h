#ifndef NANOVORE_PRESENTATION_H
#define NANOVORE_PRESENTATION_H
#include "render_scene.h"
#include "world.h"
struct Simulation;
struct Camera;
struct RenderConfig;

// Game code owns these mappings. The renderer receives RenderScene only.
struct MembraneVertex { uint32_t index,group;GpuVertex neutral;Vec3 displacement[MAX_FACTIONS]; };
struct CellMembrane { uint32_t body,first,count,first_group,group_count;float weights[MAX_FACTIONS]; };
struct OrganellePose { uint32_t body,mesh;Vec3 pivot; };
// The current view for animation and level of detail. Disabled: everything animates at full detail.
struct ViewDetail
{
    Vec3 position,forward,right,up;
    float tan_x,tan_y,pixels_per_tan,margin;
    float surface_minimum,surface_full,cell_minimum; // Level-of-detail radii in pixels.
    bool enabled;
};
struct Presentation
{
    RenderScene scene;
    std::vector<OrganellePose> organelles;
    uint32_t organelle_base;
    std::vector<CellMembrane> membranes;
    std::vector<MembraneVertex> membrane_vertices;
    std::vector<uint32_t> membrane_groups;
    std::vector<Vec3> membrane_normals;
    uint32_t body_meshes[MAX_BODIES],unit_meshes[MAX_UNIT_TYPES],factory_meshes[MAX_PRODUCER_TYPES],spike_mesh,background_mesh;
    uint32_t payload_meshes[MAX_UNIT_TYPES],blob_meshes[MAX_UNIT_TYPES],seed_meshes[MAX_UNIT_TYPES];
    bool stem_glow_mask[MAX_PRODUCER_TYPES];
    uint32_t segment_meshes[MAX_PRODUCER_TYPES],tip_meshes[MAX_PRODUCER_TYPES],stem_base,bud_base,debris_base;
    uint32_t unit_base,body_base,factory_base,attached_base,flying_base,embedded_base,structure_embedded_base,background_slot;
    int32_t medium_bodies[MAX_BODIES+1];
    uint32_t structure_first[MAX_BODIES+1]; // Each cell's first structure slot; the last entry is the total.
    uint32_t attached_spikes,flying_spikes,embedded_spikes;
    uint32_t attached_used,embedded_used; // Slots written from each spike cursor's base this frame.
    std::vector<RenderRange> previous_ranges; // Cleared before the next build.
    ViewDetail view;
    bool colony_animated[MAX_BODIES]; // Factories near the view sway; the others hold their rest pose.
    uint32_t span_step[MAX_BODIES]; // Factory curve spans per drawn stem: 1 near, several when the cell is small.
};
bool presentation_initialize(Presentation*,const Simulation*,const RenderConfig*);
bool presentation_build(Presentation*,const Simulation*,const Camera*,const RenderConfig*);
bool presentation_prepare_membranes(Presentation*,const Simulation*);
bool presentation_update_membranes(Presentation*,const Simulation*);
#endif
