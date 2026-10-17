#include "render_scene.h"
#include <cmath>
#include <float.h>
#include <stdio.h>

static const float MIN_BOUNDS_MARGIN = .0001f;
static const float RELATIVE_BOUNDS_MARGIN = .00001f;
static const unsigned int VALID_INSTANCE_MASK_BITS = 5u;
static const int INSTANCE_STYLE_VALUE_COUNT = 8;

void render_scene_bounds(RenderScene* scene)
{
    for (uint32_t axis=0;axis<3;++axis)
    {
        scene->bounds_min[axis]=scene->foreground_min[axis]=FLT_MAX;
        scene->bounds_max[axis]=scene->foreground_max[axis]=-FLT_MAX;
    }
    for (uint32_t i=0;i<scene->instances.size();++i)
    {
        const RenderInstance* instance=&scene->instances[i];
        if (!(instance->mask&1)) continue;
        const MeshRange* mesh=&scene->geometry.meshes[instance->mesh];
        for (uint32_t row=0;row<3;++row)
        {
            float midpoint=instance->transform[row][3],radius=0;
            for (uint32_t column=0;column<3;++column)
            {
                float value=instance->transform[row][column];
                midpoint+=value*.5f*(mesh->low[column]+mesh->high[column]);
                radius+=std::fabs(value)*.5f*(mesh->high[column]-mesh->low[column]);
            }
            radius+=std::fmax(MIN_BOUNDS_MARGIN,RELATIVE_BOUNDS_MARGIN*(std::fabs(midpoint)+radius));
            scene->bounds_min[row]=std::fmin(scene->bounds_min[row],midpoint-radius);
            scene->bounds_max[row]=std::fmax(scene->bounds_max[row],midpoint+radius);
            if (instance->background) continue;
            scene->foreground_min[row]=std::fmin(scene->foreground_min[row],midpoint-radius);
            scene->foreground_max[row]=std::fmax(scene->foreground_max[row],midpoint+radius);
        }
    }
}

bool render_scene_validate(const RenderScene* scene)
{
    bool valid=!scene->geometry.meshes.empty() && !scene->instances.empty()
        && scene->effect_count<=scene->effects.size() && scene->effects.size()<=MAX_EFFECT_SEGMENTS
        && scene->effect_tiles.size()==2*scene->tile_count+1+MAX_EFFECT_REFERENCES
        && std::isfinite(scene->time_seconds) && scene->sight_count<=MAX_RENDER_SIGHT
        && std::isfinite(scene->fog_blur) && scene->fog_blur>=0 && scene->fog_blur<=256
        && std::isfinite(scene->fog_feather) && (scene->fog_blur==0 || scene->fog_feather>0) && std::isfinite(scene->fog_plane_z);
    valid=valid && std::isfinite(scene->thin_blur_radius) && scene->thin_blur_radius>=0 && scene->thin_blur_radius<=32
        && std::isfinite(scene->thin_edge_strength) && scene->thin_edge_strength>=0 && scene->thin_edge_strength<=1
        && std::isfinite(scene->thin_edge_power) && scene->thin_edge_power>=0 && scene->thin_edge_power<=16;
    valid=valid && std::isfinite(scene->fog_tint.r) && scene->fog_tint.r>=0 && scene->fog_tint.r<=1
        && std::isfinite(scene->fog_tint.g) && scene->fog_tint.g>=0 && scene->fog_tint.g<=1
        && std::isfinite(scene->fog_tint.b) && scene->fog_tint.b>=0 && scene->fog_tint.b<=1;
    if (scene->fog_blur>0)
    {
        valid=valid && std::isfinite(scene->fog_field.low_x) && std::isfinite(scene->fog_field.low_y)
            && std::isfinite(scene->fog_field.spacing) && scene->fog_field.spacing>0;
        for (int i=0;valid && i<SIGHT_FIELD_CELLS;++i)
            valid=std::isfinite(scene->fog_field.reveal[i]) && scene->fog_field.reveal[i]>=0 && scene->fog_field.reveal[i]<=1;
    }
    for (uint32_t i=0;valid && i<scene->sight_count;++i)
        for (uint32_t j=0;j<4;++j) valid=valid && std::isfinite(scene->sight[i].center_radius[j]) && (j<3 || scene->sight[i].center_radius[j]>0);
    for (uint32_t i=0;valid && i<scene->instances.size();++i)
    {
        const RenderInstance* instance=&scene->instances[i];
        valid=instance->mesh<scene->geometry.meshes.size() && !(instance->mask&~VALID_INSTANCE_MASK_BITS);
        for (uint32_t row=0;row<3;++row)
            for (uint32_t column=0;column<4;++column) valid=valid && std::isfinite(instance->transform[row][column]);
        for (uint32_t j=0;j<INSTANCE_STYLE_VALUE_COUNT;++j) valid=valid && std::isfinite(instance->style[j]);
    }
    for (uint32_t i=0;valid && i<scene->media.size();++i)
    {
        const RenderMedium* m=&scene->media[i];
        for (uint32_t j=0;j<4;++j)
            valid=valid && std::isfinite(m->center_radius[j]) && std::isfinite(m->scattering[j])
                && std::isfinite(m->absorption_phase[j]) && std::isfinite(m->lighting[j]) && std::isfinite(m->blur[j]);
        valid=valid && m->blur[0]>=0 && m->blur[0]<=32 && m->blur[1]>=0 && m->blur[1]<=100 && m->blur[2]>=0 && m->blur[2]<=32;
        valid=valid && (m->lighting[0]==0 || m->lighting[0]==1) && m->center_radius[3]>=0 && m->scattering[3]>=0 && m->scattering[3]<=1;
    }
    for (uint32_t axis=0;axis<3;++axis)
        valid=valid && std::isfinite(scene->bounds_min[axis]) && std::isfinite(scene->bounds_max[axis])
            && std::isfinite(scene->foreground_min[axis]) && std::isfinite(scene->foreground_max[axis]);
    if (!valid) fprintf(stderr,"Invalid render scene input.\n");
    return valid;
}

bool render_scene_write(const RenderScene* scene,const char* path)
{
    FILE* file=fopen(path,"wb");
    if (!file) { fprintf(stderr,"Cannot write render scene '%s'.\n",path);return false; }
    fprintf(file,"{\"meshes\":[");
    for (uint32_t i=0;i<scene->geometry.meshes.size();++i)
    {
        const MeshRange* m=&scene->geometry.meshes[i];
        float rough_low=1,rough_high=0,ior_low=3,ior_high=1,sss_low=1,sss_high=0,radius_low=FLT_MAX,radius_high=0;
        for (uint32_t t=m->first;t<m->first+m->count;++t)
            for (uint32_t j=0;j<3;++j)
            {
                const GpuVertex& v=scene->geometry.vertices[scene->geometry.triangles[t].index[j]];
                rough_low=std::fmin(rough_low,v.color[3]);rough_high=std::fmax(rough_high,v.color[3]);
                ior_low=std::fmin(ior_low,v.position[3]);ior_high=std::fmax(ior_high,v.position[3]);
                sss_low=std::fmin(sss_low,v.subsurface[3]);sss_high=std::fmax(sss_high,v.subsurface[3]);
                for (uint32_t c=0;c<3;++c) { radius_low=std::fmin(radius_low,v.subsurface[c]);radius_high=std::fmax(radius_high,v.subsurface[c]); }
            }
        fprintf(file,"%s{\"triangles\":%u,\"low\":[%.6f,%.6f,%.6f],\"high\":[%.6f,%.6f,%.6f],\"roughness\":[%.6f,%.6f],\"ior\":[%.6f,%.6f]",
            i ? "," : "",m->count,m->low[0],m->low[1],m->low[2],m->high[0],m->high[1],m->high[2],rough_low,rough_high,ior_low,ior_high);
        fprintf(file,",\"subsurface_weight\":[%.6f,%.6f],\"subsurface_radius\":[%.6f,%.6f]",sss_low,sss_high,radius_low,radius_high);
        if (m->deformable)
        {
            fprintf(file,",\"deformation\":{\"revision\":%llu,\"vertices\":[",(unsigned long long)m->revision);
            for (uint32_t v=0;v<m->vertex_count;++v)
            {
                const GpuVertex& vertex=scene->geometry.vertices[m->first_vertex+v];
                fprintf(file,"%s[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]",v ? "," : "",vertex.position[0],vertex.position[1],vertex.position[2],vertex.normal[0],vertex.normal[1],vertex.normal[2]);
            }
            fprintf(file,"],\"indices\":[");
            for (uint32_t t=m->first;t<m->first+m->count;++t)
            {
                const GpuTriangle& triangle=scene->geometry.triangles[t];
                fprintf(file,"%s[%u,%u,%u,%u]",t>m->first ? "," : "",triangle.index[0]-m->first_vertex,triangle.index[1]-m->first_vertex,triangle.index[2]-m->first_vertex,triangle.index[3]);
            }
            fprintf(file,"]}");
        }
        fprintf(file,"}");
    }
    fprintf(file,"],\"instances\":[");bool first=true;
    for (uint32_t i=0;i<scene->instances.size();++i)
    {
        const RenderInstance* instance=&scene->instances[i];if (!instance->mask) continue;
        fprintf(file,"%s{\"slot\":%u,\"mesh\":%u,\"mask\":%u,\"transform\":[",first ? "" : ",",i,instance->mesh,instance->mask);first=false;
        for (uint32_t row=0;row<3;++row) for (uint32_t column=0;column<4;++column)
            fprintf(file,"%s%.6f",row || column ? "," : "",instance->transform[row][column]);
        fprintf(file,"],\"style\":[");
        for (uint32_t j=0;j<INSTANCE_STYLE_VALUE_COUNT;++j) fprintf(file,"%s%.6f",j ? "," : "",instance->style[j]);
        fprintf(file,"]}");
    }
    fprintf(file,"],\"media\":[");
    for (uint32_t i=0;i<scene->media.size();++i)
    {
        const RenderMedium* m=&scene->media[i];
        fprintf(file,"%s[%.6f,%.6f,%.6f,%.6f]",i ? "," : "",m->center_radius[0],m->center_radius[1],m->center_radius[2],m->center_radius[3]);
    }
    fprintf(file,"],\"membrane_fade\":[%.6f,%.6f],\"wall_blur_radius\":%.6f,\"medium_blur\":[",scene->thin_edge_strength,scene->thin_edge_power,scene->thin_blur_radius);
    for (uint32_t i=0;i<scene->media.size();++i) fprintf(file,"%s[%.6f,%.6f,%.6f]",i ? "," : "",scene->media[i].blur[0],scene->media[i].blur[1],scene->media[i].blur[2]);
    fprintf(file,"],\"medium_shadows\":[");
    for (uint32_t i=0;i<scene->media.size();++i) fprintf(file,"%s%s",i ? "," : "",scene->media[i].lighting[0]>0 ? "true" : "false");
    fprintf(file,"],\"fog\":{\"blur_radius\":%.6f,\"sight_feather\":%.6f,\"plane_z\":%.6f,\"tint\":[%.6f,%.6f,%.6f],\"sight\":[",scene->fog_blur,scene->fog_feather,scene->fog_plane_z,scene->fog_tint.r,scene->fog_tint.g,scene->fog_tint.b);
    for (uint32_t i=0;i<scene->sight_count;++i)
    { const float* v=scene->sight[i].center_radius;fprintf(file,"%s[%.6f,%.6f,%.6f,%.6f]",i ? "," : "",v[0],v[1],v[2],v[3]); }
    fprintf(file,"],\"grid\":{\"side\":%d,\"origin\":[%.9g,%.9g],\"spacing\":%.9g,\"reveal\":[",SIGHT_FIELD_SIDE,scene->fog_field.low_x,scene->fog_field.low_y,scene->fog_field.spacing);
    if (scene->fog_blur>0) for (int i=0;i<SIGHT_FIELD_CELLS;++i) fprintf(file,"%s%.9g",i ? "," : "",scene->fog_field.reveal[i]);
    fprintf(file,"]}},\"bounds_min\":[%.6f,%.6f,%.6f],\"bounds_max\":[%.6f,%.6f,%.6f],\"effects\":%u,\"effect_segments\":[",
        scene->bounds_min[0],scene->bounds_min[1],scene->bounds_min[2],scene->bounds_max[0],scene->bounds_max[1],scene->bounds_max[2],scene->effect_count);
    for (uint32_t i=0;i<scene->effect_count;++i)
    {
        const EffectSegment& e=scene->effects[i];
        fprintf(file,"%s[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]",i ? "," : "",e.start[0],e.start[1],e.start[2],e.start[3],e.end[0],e.end[1],e.end[2],e.end[3]);
    }
    fprintf(file,"]}\n");
    bool valid=!ferror(file);if (fclose(file)) valid=false;return valid;
}
