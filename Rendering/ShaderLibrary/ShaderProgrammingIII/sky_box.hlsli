#include "gbuffer.hlsli"

#include "common.hlsli"

#include "scene_constant_buffer.hlsli"

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float4 world_position : WORLD_POSITION;
    float4 current_clip_position : CLIP_POSITION0;
    float4 previous_clip_position : CLIP_POSITION1;
};
