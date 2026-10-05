#ifndef __STATIC_MESH_OBJECT_DATA_HLSLI__
#define __STATIC_MESH_OBJECT_DATA_HLSLI__

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 world_position : POSITION;
    float4 world_normal : NORMAL;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD;
};

cbuffer OBJECT_CONSTANT_BUFFER : register(b0)
{
    row_major float4x4 world;
    float4 Ka;
    float4 Kd;
    float4 Ks;
};

#endif // __STATIC_MESH_OBJECT_DATA_HLSLI__
