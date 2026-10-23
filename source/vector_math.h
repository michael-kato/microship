#ifndef NANOVORE_VECTOR_MATH_H
#define NANOVORE_VECTOR_MATH_H

#include "types.h"
#include <cmath>

inline Vec3 add(Vec3 a, Vec3 b)
{
	return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 subtract(Vec3 a, Vec3 b)
{
	return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 scale(Vec3 v, float s) { return Vec3{v.x * s, v.y * s, v.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b)
{
	return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
	            a.x * b.y - a.y * b.x};
}
inline Vec3 normalize(Vec3 v) { return scale(v, 1.0f / std::sqrt(dot(v, v))); }
// std::fmax/fmin results, NaN handling included, without the MinGW library
// call.
inline float max_of(float a, float b) { return a > b || b != b ? a : b; }
inline float min_of(float a, float b) { return a < b || b != b ? a : b; }
inline double max_of(double a, double b) { return a > b || b != b ? a : b; }
inline double min_of(double a, double b) { return a < b || b != b ? a : b; }

#endif
