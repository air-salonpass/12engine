#include "sprite.hlsli"

// Unit05: 中心から離れた画素を指定色へ寄せ、視線を中央へ誘導する。
cbuffer VIGNETTE_CONSTANT_BUFFER : register(b2)
{
    float4 vignette_color;
    float2 vignette_center;
    float vignette_intensity;
    float vignette_smoothness;
    float vignette_rounded;
    float vignette_roundness;
    float2 vignette_dummy;
};

Texture2D scene_map : register(t0);
SamplerState linear_sampler_state : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
    float2 size;
    scene_map.GetDimensions(size.x, size.y);
    float4 color = scene_map.Sample(linear_sampler_state, pin.texcoord);
    float2 d = abs(pin.texcoord - vignette_center) * vignette_intensity;
    d.x *= lerp(1.0f, size.x / size.y, vignette_rounded);
    d = pow(saturate(d), max(vignette_roundness, 0.000001f));
    float factor = pow(saturate(1.0f - dot(d, d)), max(vignette_smoothness, 0.000001f));
    color.rgb *= lerp(vignette_color.rgb, 1.0f.xxx, factor);
    return color;
}
