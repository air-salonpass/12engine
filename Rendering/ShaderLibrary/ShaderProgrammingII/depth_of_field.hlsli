#ifndef DEPTH_OF_FIELD_HLSLI
#define DEPTH_OF_FIELD_HLSLI
#include "sprite.hlsli"
#include "scene_constant_buffer.hlsli"

// Unit02-03: Focus plane and transition range in view-space units.
cbuffer DEPTH_OF_FIELD_CONSTANT_BUFFER : register(b2)
{
    float focus_distance;
    float dof_range;
    float2 dof_padding;
};
#endif
