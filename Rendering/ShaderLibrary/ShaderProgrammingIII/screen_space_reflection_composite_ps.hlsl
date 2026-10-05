#include "sprite.hlsli"
#include "scene_constant_buffer.hlsli"
#include "gbuffer.hlsli"
#include "screen_space_reflection.hlsli"
#include "shading_functions.hlsli"

Texture2D<float4> gbuffer_base_color : register(t0);
Texture2D<float4> gbuffer_emissive : register(t1);
Texture2D<float4> gbuffer_normal : register(t2);
Texture2D<float4> gbuffer_parameter : register(t3);
Texture2D<float> gbuffer_depth : register(t4);
Texture2D<float4> gbuffer_velocity : register(t5);
Texture2D<float4> reflection_map : register(t10);
SamplerState sampler_states[5] : register(s0);

float4 reflection_blur(float2 uv, int radius, float separation)
{
    float2 size;
    reflection_map.GetDimensions(size.x, size.y);
    float4 total = 0;
    float count = 0;
    [loop] for (int y = -radius; y <= radius; ++y)
    [loop] for (int x = -radius; x <= radius; ++x)
    {
        float4 sample_color = reflection_map.Sample(sampler_states[1], uv + float2(x, y) * separation / size);
        if (sample_color.a > 0.01f) { total += sample_color; count += 1; }
    }
    return total / max(count, 0.001f);
}

float4 main(VS_OUT pin) : SV_TARGET
{
    PSGBufferTextures textures = { gbuffer_base_color, gbuffer_emissive, gbuffer_normal,
        gbuffer_parameter, gbuffer_depth, gbuffer_velocity, sampler_states[0] };
    GBufferData data = DecodeGBuffer(textures, pin.texcoord, inverse_view_projection_transform, z_buffer_parameteres);
    float4 reflection = reflection_map.Sample(sampler_states[1], pin.texcoord);
    if (reflection.a >= 0.01f && data.roughness >= 0.01f && (ssr_flags & 2))
        reflection = lerp(reflection, reflection_blur(pin.texcoord, 6, 4), data.roughness);
    float3 color = lerp(data.base_color, reflection.rgb, reflection.a);
    // Unit08: 反射合成後に表示用ガンマへ変換する。
    return float4(pow(saturate(color), 1.0f / GammaFactor), 1);
}
