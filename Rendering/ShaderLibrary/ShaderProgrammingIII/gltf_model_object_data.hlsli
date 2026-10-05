#ifndef __GLTF_MODEL_OBJECT_DATA_HLSLI__
#define __GLTF_MODEL_OBJECT_DATA_HLSLI__

struct VS_IN
{
    float4 position : POSITION;
    float4 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texcoord : TEXCOORD;
    uint4 joints : JOINTS;
    float4 weights : WEIGHTS;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 w_position : POSITION;
    float4 w_normal : NORMAL;
    float4 w_tangent : TANGENT;
    float2 texcoord : TEXCOORD;
    float3 shadow_texcoord : TEXCOORD1;
};

cbuffer PRIMITIVE_CONSTANT_BUFFER : register(b0)
{
    row_major float4x4 world;
    int material;
    int has_tangent;
    int skin;
    int pad;
};

#endif // __GLTF_MODEL_OBJECT_DATA_HLSLI__
