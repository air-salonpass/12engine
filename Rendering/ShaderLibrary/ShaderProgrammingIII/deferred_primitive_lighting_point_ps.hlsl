#include "deferred_primitive_lighting.hlsli"

Texture2D<float4> gbuffer_base_color : register(t0);
Texture2D<float4> gbuffer_emissive_color : register(t1);
Texture2D<float4> gbuffer_normal : register(t2);
Texture2D<float4> gbuffer_parameter : register(t3);
Texture2D<float> gbuffer_depth : register(t4);
Texture2D<float4> gbuffer_velocity : register(t5);

#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2
SamplerState sampler_states[3] : register(s0);

//  シャドウマップ用テクスチャ
Texture2D shadow_map : register(t10);
SamplerState shadow_sampler_state : register(s10);

//  点光源情報
cbuffer LIGHT_CONSTANT_BUFFER : register(b2)
{
    point_lights point_light;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    //  テクスチャ座標を算出
    float2 texcoord = pin.position.xy / float2(1280, 720);

    //  GBufferテクスチャから情報をデコードする
    PSGBufferTextures gbuffer_textures;
    gbuffer_textures.base_color = gbuffer_base_color;
    gbuffer_textures.emissive_color = gbuffer_emissive_color;
    gbuffer_textures.normal = gbuffer_normal;
    gbuffer_textures.parameter = gbuffer_parameter;
    gbuffer_textures.depth = gbuffer_depth;
    gbuffer_textures.velocity = gbuffer_velocity;
    gbuffer_textures.state = sampler_states[POINT];
    GBufferData data;
    data = DecodeGBuffer(gbuffer_textures, texcoord, inverse_view_projection_transform, z_buffer_parameteres);

    float3 total_diffuse = (float3) 0, total_specular = (float3) 0;
    if (data.shading_model != shading_model_unlit)
    {
        float3 L = data.w_position.xyz - point_light.position.xyz;
        float len = length(L);
        if (len < point_light.range)
        {
            L /= len;

            float attenuateLength = saturate(1.0f - len / point_light.range);
            float attenuation = attenuateLength * attenuateLength;

            LightingData lighting_data;
            lighting_data.attenuation = attenuation;
            lighting_data.direction = L;
            lighting_data.color = point_light.color.rgb * point_light.color.a;
            lighting_data.camera_position = camera_position.xyz;
            DirectLighting result = integrate_bxdf(data, lighting_data);
            total_diffuse += result.diffuse;
            total_specular += result.specular;
        }
    }
    return float4(total_diffuse + total_specular, 1);
}
