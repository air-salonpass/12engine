#include "sprite.hlsli"

// Unit02-02: Bloom bright-pass parameters.
cbuffer LUMINANCE_EXTRACT : register(b2)
{
    float threshold;
    float intensity;
    float2 luminance_padding;
};

Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

float rgb_to_luminance(float3 rgb)
{
    return dot(rgb, float3(0.299f, 0.587f, 0.114f));
}

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 color = source_texture.Sample(source_sampler, pin.texcoord) * pin.color;
    float luminance = max(rgb_to_luminance(color.rgb), 1.0e-5f);
    float contribution = max(0.0f, luminance - threshold) / luminance;
    color.rgb *= contribution * intensity;
    color.a = 1.0f;
    return color;
}
