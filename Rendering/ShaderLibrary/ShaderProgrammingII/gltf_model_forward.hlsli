#include "gltf_model.hlsli"

cbuffer LIGHT_CONSTANT_BUFFER : register(b2)
{
    float4 ambient_color;
    uint4 light_count; //	x : 空き, y : ポイントライト数, z : スポットライト数, w : 空き。
    directional_lights directional_light;
    point_lights point_light[8];
    spot_lights spot_light[8];
};

cbuffer SHADOWMAP_CONSTANT_BUFFER : register(b3)
{
    row_major float4x4 light_view_projection;
    float shadow_attenuation;
    float shadow_bias;
    float2 shadow_dummy;
};


