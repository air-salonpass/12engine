#include "geometric_primitive_object_data.hlsli"
#include "scene_constant_buffer.hlsli"

struct HS_IN { float4 position : SV_POSITION; float4 color : COLOR; float3 normal : NORMAL; float2 texcoord : TEXCOORD; };
struct HS_CONSTANT_OUT { float edge[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
struct DS_IN { float4 position : SV_POSITION; float4 color : COLOR; float3 normal : NORMAL; float2 texcoord : TEXCOORD; };
struct DS_OUT { float4 position : SV_POSITION; float4 color : COLOR; float3 normal : NORMAL; float2 texcoord : TEXCOORD; };

cbuffer TESSELLATION_CONSTANT_BUFFER : register(b2)
{
    float edge_factor;
    float inside_factor;
    float height_scale;
    float lod_distance_max;
};
