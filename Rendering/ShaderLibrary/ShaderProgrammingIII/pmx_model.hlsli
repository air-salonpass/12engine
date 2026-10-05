#include	"lights.hlsli"
#include    "common.hlsli"

#include	"scene_constant_buffer.hlsli"

#include    "gbuffer.hlsli"

struct VS_IN
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 additional_uv[4] : ADDITIONAL_TEXCOORD;
    uint4 joints : JOINTS;
    float4 weights : WEIGHTS;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float3 w_position : POSITION;
    float3 w_normal : NORMAL;
    float2 texcoord : TEXCOORD;
};

#define BONE_MAX 1000
cbuffer PRIMITIVE_CONSTANT_BUFFER : register(b0)
{
    row_major float4x4 world;
    row_major float4x4 previous_world;
    int4 texture_index;
    float4 diffuse_color;
    float4 specular_color;
    float4 edge_color;
    float edge_size;
    float3 dummy;
    row_major float4x4 bone_transforms[BONE_MAX];
};

cbuffer LIGHT_CONSTANT_BUFFER : register(b3)
{
    directional_lights directional_light;
    point_lights point_light[8];
    spot_lights spot_light[8];
};

cbuffer SHADOWMAP_CONSTANT_BUFFER : register(b4)
{
    row_major float4x4 light_view_projection;
    float shadow_attenuation;
    float shadow_bias;
    float2 shadow_dummy;
};


#include "shading_functions.hlsli"
