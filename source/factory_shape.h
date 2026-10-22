#ifndef NANOVORE_FACTORY_SHAPE_H
#define NANOVORE_FACTORY_SHAPE_H

static const float FACTORY_ROUNDED_MIN_WIDTH=.55f;

// Keep visible stems, contact bounds, and detached fragments at the same size.
static const float FACTORY_STEM_TAPER_PER_SEGMENT=.12f;
static const float MIN_FACTORY_OUTWARD_COMPONENT=.4f;
static const uint32_t FACTORY_CURVE_DETAIL_OCTAVES=3;
static const float FACTORY_CURVE_DETAIL_DECAY=.25f;
static const float FACTORY_CURVE_SKEW_SCALE=.6f;
static const float FACTORY_CURVE_AMPLITUDE_JITTER=.35f;
static const float FACTORY_CURVE_S_BEND_SCALE=1.5f;
static const float FACTORY_CHILD_STEM_WIDTH_SCALE=.65f;
static const float FACTORY_STEM_START_WIDTH_SCALE=.55f;
static const float FACTORY_STEM_GROWTH_WIDTH_SCALE=.45f;
static const float FACTORY_CHILD_HEAD_WIDTH_SCALE=.7f;
static const float FACTORY_HEAD_START_WIDTH_SCALE=.4f;
static const float FACTORY_HEAD_GROWTH_WIDTH_SCALE=.6f;

#endif
