#include "common.hlsli"

#include "scene_constant_buffer.hlsli"

#include "shading_functions.hlsli"

#include "lights.hlsli"

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float4 world_position : WORLD_POSITION;
    float4 current_clip_position : CLIP_POSITION0;
    float4 previous_clip_position : CLIP_POSITION1;
};

cbuffer LIGHT_CONSTANT_BUFFER : register(b2)
{
    float4 ambient_color;
    uint4 light_count; //	x : 空き, y : ポイントライト数, z : スポットライト数, w : 空き。
    directional_lights directional_light;
    point_lights point_light[8];
    spot_lights spot_light[8];
};

