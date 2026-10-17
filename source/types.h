#ifndef NANOVA_TYPES_H
#define NANOVA_TYPES_H

// Plain records keep XYZ coordinates distinct from RGB channels without a library.

struct Vec3
{
    float x;
    float y;
    float z;
};

struct Color
{
    float r;
    float g;
    float b;
};

#endif
