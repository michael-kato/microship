#include "map_layout.h"
#include "math_constants.h"
#include "vector_math.h"
#include <cmath>

static const uint32_t FIELD_POLYGON_SIDES=64;
static const uint32_t POLYGON_CAPACITY=FIELD_POLYGON_SIDES+MAX_LAYOUT_CELLS;
static const uint32_t LAYOUT_ATTEMPTS=256;
static const uint32_t SITE_TRIES=64;
// Random sites keep at least this fraction of the mean spacing before relaxation.
static const float SITE_SPACING_FRACTION=.5f;
static const float MIN_REGION_AREA=1e-6f;
static const float PI_FLOAT=3.14159265359f,TWO_PI=6.28318530718f;

struct Polygon { Vec3 points[POLYGON_CAPACITY];uint32_t count; };

static uint32_t next_random(uint32_t* state)
{ uint32_t x=*state;x^=x<<13;x^=x>>17;x^=x<<5;*state=x;return x; }
static float unit_random(uint32_t* state)
{ return (float)(next_random(state)>>8)*(1.0f/16777216.0f); }

// Keep the side of the bisector nearer to the site.
static void clip(const Polygon& input,Vec3 site,Vec3 other,Polygon* output)
{
    Vec3 normal=subtract(other,site),middle=scale(add(site,other),.5f);output->count=0;
    for (uint32_t i=0;i<input.count;++i)
    {
        Vec3 a=input.points[i],b=input.points[(i+1)%input.count];
        float da=dot(subtract(a,middle),normal),db=dot(subtract(b,middle),normal);
        if (da<=0) output->points[output->count++]=a;
        if ((da<0)!=(db<0) && da!=db) output->points[output->count++]=add(a,scale(subtract(b,a),da/(da-db)));
    }
}
static void region(const VoronoiLayout& layout,const Vec3* sites,uint32_t i,Polygon* result)
{
    Polygon scratch[2];Polygon* current=&scratch[0];current->count=FIELD_POLYGON_SIDES;
    for (uint32_t k=0;k<FIELD_POLYGON_SIDES;++k)
    {
        float angle=TWO_PI*k/FIELD_POLYGON_SIDES;
        current->points[k]=Vec3{layout.field_radius*std::cos(angle),layout.field_radius*std::sin(angle),0};
    }
    for (uint32_t j=0;j<layout.cells;++j) if (j!=i)
    {
        Polygon* next=current==&scratch[0] ? &scratch[1] : &scratch[0];
        clip(*current,sites[i],sites[j],next);current=next;
    }
    *result=*current;
}
static float centroid(const Polygon& p,Vec3* center)
{
    float area=0,x=0,y=0;
    for (uint32_t i=0;i<p.count;++i)
    {
        Vec3 a=p.points[i],b=p.points[(i+1)%p.count];float w=a.x*b.y-b.x*a.y;
        area+=w;x+=(a.x+b.x)*w;y+=(a.y+b.y)*w;
    }
    area*=.5f;
    if (std::fabs(area)>MIN_REGION_AREA) *center=Vec3{x/(6*area),y/(6*area),0};
    return std::fabs(area);
}

static Vec3 disk_point(float radius,uint32_t* random)
{
    float r=radius*std::sqrt(unit_random(random)),angle=TWO_PI*unit_random(random);
    return Vec3{r*std::cos(angle),r*std::sin(angle),0};
}
static uint32_t fixed_sites(const VoronoiLayout& layout)
{ return layout.homes+(layout.center_radius>0 ? 1u : 0u); }
static void place_sites(const VoronoiLayout& layout,uint32_t* random,Vec3* sites)
{
    for (uint32_t k=0;k<layout.homes;++k)
    {
        float angle=(layout.first_home_degrees+360.0f*k/layout.homes)*DEGREES_TO_RADIANS;
        sites[k]=Vec3{layout.field_radius*std::cos(angle),layout.field_radius*std::sin(angle),0};
    }
    if (layout.center_radius>0) sites[layout.homes]=disk_point(layout.center_jitter,random);
    float spacing=SITE_SPACING_FRACTION*layout.field_radius*std::sqrt(PI_FLOAT/layout.cells);
    for (uint32_t i=fixed_sites(layout);i<layout.cells;++i)
        for (uint32_t t=0;t<SITE_TRIES;++t)
        {
            sites[i]=disk_point(layout.field_radius,random);bool clear=true;
            for (uint32_t j=0;j<i && clear;++j) clear=dot(subtract(sites[i],sites[j]),subtract(sites[i],sites[j]))>=spacing*spacing;
            if (clear) break;
        }
}

bool voronoi_layout(const VoronoiLayout& layout,uint32_t seed,LayoutCell* cells)
{
    Vec3 sites[MAX_LAYOUT_CELLS];float areas[MAX_LAYOUT_CELLS];Polygon polygon;
    uint32_t first=fixed_sites(layout),neutral=layout.cells-first;
    for (uint32_t attempt=0;attempt<LAYOUT_ATTEMPTS;++attempt)
    {
        uint32_t random=(seed^(attempt*0x9e3779b9u))|1u;
        place_sites(layout,&random,sites);
        // Homes and the center cell stay; the other sites move to their region centroids.
        for (uint32_t step=0;step<layout.centroid_steps;++step)
        {
            Vec3 moved[MAX_LAYOUT_CELLS];
            for (uint32_t i=0;i<layout.cells;++i)
            { moved[i]=sites[i];if (i>=first) { region(layout,sites,i,&polygon);centroid(polygon,&moved[i]); } }
            for (uint32_t i=0;i<layout.cells;++i) sites[i]=moved[i];
        }
        for (uint32_t i=0;i<layout.cells;++i) { region(layout,sites,i,&polygon);Vec3 unused;areas[i]=centroid(polygon,&unused); }
        // Neutral radii follow region area rank evenly from the largest to the smallest.
        for (uint32_t i=0;i<layout.cells;++i)
        {
            cells[i].position=sites[i];
            cells[i].radius=i<layout.homes ? layout.home_radius : layout.center_radius;
            if (i<first) continue;
            uint32_t rank=0;
            for (uint32_t j=first;j<layout.cells;++j) rank+=areas[j]>areas[i] || (areas[j]==areas[i] && j<i);
            float t=neutral>1 ? (float)rank/(neutral-1) : 0;
            cells[i].radius=layout.neutral_radius[1]+(layout.neutral_radius[0]-layout.neutral_radius[1])*t;
        }
        bool clear=true;
        for (uint32_t i=0;i<layout.cells && clear;++i) for (uint32_t j=i+1;j<layout.cells && clear;++j)
        {
            Vec3 d=subtract(sites[i],sites[j]);float limit=cells[i].radius+cells[j].radius+layout.minimum_gap;
            clear=dot(d,d)>=limit*limit;
        }
        if (clear) return true;
    }
    return false;
}
