#include "sprite.hlsli"
#include "lights.hlsli"
#include "shading_models.hlsli"

#include "scene_constant_buffer.hlsli"

struct PS_OUT
{
    float4 diffuse : SV_TARGET0;
    float4 specular : SV_TARGET1;
};
