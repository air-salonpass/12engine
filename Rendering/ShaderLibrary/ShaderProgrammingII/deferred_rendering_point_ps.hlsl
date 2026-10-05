#include "deferred_rendering.hlsli"

Texture2D<float4> gbuffer_base_color : register(t0);
Texture2D<float4> gbuffer_emissive_color : register(t1);
Texture2D<float4> gbuffer_normal_depth : register(t2);

#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2
SamplerState sampler_states[3] : register(s0);

//  シャドウマップ用テクスチャ
Texture2D shadow_map : register(t10);
SamplerState shadow_sampler_state : register(s10);

float4 main(VS_OUT pin) : SV_TARGET
{
    point_lights point_light = convert_point_lights();

    //  GBufferテクスチャから情報をデコードする
    PSGBufferTextures gbuffer_textures;
    gbuffer_textures.base_color = gbuffer_base_color;
    gbuffer_textures.emissive_color = gbuffer_emissive_color;
    gbuffer_textures.normal_depth = gbuffer_normal_depth;
    gbuffer_textures.state = sampler_states[POINT];
    GBufferData data;
    data = DecodeGBuffer(gbuffer_textures, pin.texcoord, inverse_view_projection_transform);

    float3 diffuse = (float3) 0, specular = (float3) 0;
    {
        float3 L = data.w_position.xyz - point_light.position.xyz;
        float len = length(L);
        if (len < point_light.range)
        {
            L /= len;

            float attenuateLength = saturate(1.0f - len / point_light.range);
            float attenuation = attenuateLength * attenuateLength;

            LightingData lighting_data;
            lighting_data.direction = L;
            lighting_data.camera_position = camera_position;
            lighting_data.color = point_light.color.rgb * point_light.color.a;
            lighting_data.attenuation = attenuation;

            DirectLighting result = integrate_bxdf(data, lighting_data);
            diffuse = result.diffuse;
            specular = result.specular;
        }
    }

    return float4(diffuse + specular, 1);
}
