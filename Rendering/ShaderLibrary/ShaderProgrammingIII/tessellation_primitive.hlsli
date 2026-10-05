#include "geometric_primitive_object_data.hlsli"
#include "scene_constant_buffer.hlsli"

// Unit01: VS→HS→DS間で受け渡す制御点とテッセレーション係数。
struct HS_IN { float4 position : POSITION; float4 color : COLOR; float2 texcoord : TEXCOORD; };
struct HS_CONSTANT_OUT { float edge[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
struct DS_IN { float4 position : POSITION; float4 color : COLOR; float2 texcoord : TEXCOORD; };
struct DS_OUT { float4 position : SV_POSITION; float4 color : COLOR; float2 texcoord : TEXCOORD; };

cbuffer TESSELLATION_CONSTANT_BUFFER : register(b2)
{
    float edge_factor;
    float inside_factor;
    float height_scale;
    float tiling_scale;
};
