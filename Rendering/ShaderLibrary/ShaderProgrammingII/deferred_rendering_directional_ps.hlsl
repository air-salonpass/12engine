#include "deferred_rendering.hlsli"

Texture2D<float4> gbuffer_base_color : register(t0);
Texture2D<float4> gbuffer_emissive_color : register(t1);
Texture2D<float4> gbuffer_normal_depth : register(t2);
SamplerState sampler_states[3] : register(s0);
Texture2D shadow_map : register(t10);
SamplerState shadow_sampler_state : register(s10);

// Unit04: Full-screen directional-light accumulation with shadow lookup.
float4 main(VS_OUT pin) : SV_TARGET
{
    PSGBufferTextures textures;
    textures.base_color = gbuffer_base_color;
    textures.emissive_color = gbuffer_emissive_color;
    textures.normal_depth = gbuffer_normal_depth;
    textures.state = sampler_states[0];
    GBufferData data = DecodeGBuffer(textures, pin.texcoord, inverse_view_projection_transform);
    directional_lights light = convert_directional_lights();
    float attenuation = 1.0f;
    if (use_shadow != 0)
    {
        float4 shadow_position = mul(float4(data.w_position, 1.0f), light_view_projection);
        shadow_position /= shadow_position.w;
        shadow_position.y = -shadow_position.y;
        shadow_position.xy = shadow_position.xy * 0.5f + 0.5f;
        float depth = shadow_map.Sample(shadow_sampler_state, shadow_position.xy).r;
        attenuation = lerp(1.0f, shadow_attenuation, shadow_position.z - depth > shadow_bias);
    }
    LightingData lighting;
    lighting.direction = normalize(light.direction.xyz);
    lighting.attenuation = attenuation;
    lighting.color = light.color.rgb * light.color.a;
    lighting.camera_position = camera_position.xyz;
    lighting.dummy = 0.0f;
    DirectLighting result = integrate_bxdf(data, lighting);
    return float4(result.diffuse + result.specular, 1.0f);
}
