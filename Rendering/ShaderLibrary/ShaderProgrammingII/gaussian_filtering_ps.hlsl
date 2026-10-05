#include "gaussian_filtering.hlsli"

Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

// Unit02-01: 周辺画素の重み付き和からぼかし色を求める。
float4 main(VS_OUT pin) : SV_TARGET
{
    float4 color = float4(0.0f, 0.0f, 0.0f, 1.0f);
    int sample_count = (int)(kernel_size * kernel_size);
    for (int index = 0; index < sample_count; ++index)
    {
        float2 offset = texel_size * weights[index].xy;
        color.rgb += source_texture.Sample(source_sampler, pin.texcoord + offset).rgb * weights[index].z;
    }
    return color * pin.color;
}
