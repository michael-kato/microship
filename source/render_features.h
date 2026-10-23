#ifndef NANOVORE_RENDER_FEATURES_H
#define NANOVORE_RENDER_FEATURES_H

// Bits match function key offsets from F5; F7 is unassigned.
enum RenderFeature
{
	RENDER_SSS = 1u << 0,
	RENDER_VOLUME = 1u << 1,
	RENDER_DEPTH_OF_FIELD = 1u << 3,
	RENDER_FOG_BLUR = 1u << 4,
	RENDER_GLOW = 1u << 5,
	RENDER_FEATURE_MASK = RENDER_SSS | RENDER_VOLUME |
	    RENDER_DEPTH_OF_FIELD | RENDER_FOG_BLUR | RENDER_GLOW
};

#endif
