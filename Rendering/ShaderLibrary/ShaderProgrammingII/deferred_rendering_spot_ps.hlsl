#include "deferred_rendering.hlsli"

Texture2D<float4> gbuffer_base_color : register(t0);
Texture2D<float4> gbuffer_emissive_color : register(t1);
Texture2D<float4> gbuffer_normal_depth : register(t2);

#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2
SamplerState sampler_states[3] : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
    spot_lights spot_light = convert_spot_lights();

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
        float3 L = data.w_position.xyz - spot_light.position.xyz;
        float len = length(L);
        if (len < spot_light.range)
        {
            float attenuateLength = saturate(1.0f - len / spot_light.range);
            float attenuation = attenuateLength * attenuateLength;
            L /= len;
            float3 spotDirection = normalize(spot_light.direction.xyz);
            float angle = dot(spotDirection, L);
            float area = spot_light.inner_corn - spot_light.outer_corn;
            attenuation *= saturate(1.0f - (spot_light.inner_corn - angle) / area);

            LightingData lighting_data;
            lighting_data.direction = L;
            lighting_data.camera_position = camera_position;
            lighting_data.color = spot_light.color.rgb * spot_light.color.a;
            lighting_data.attenuation = attenuation;

            DirectLighting result = integrate_bxdf(data, lighting_data);
            diffuse = result.diffuse;
            specular = result.specular;
        }
    }
    return float4(diffuse + specular, 1);
}
