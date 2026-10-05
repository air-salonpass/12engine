#include "sprite.hlsli"

// Unit03: 画面中心へ向かって複数回サンプリングし、放射状の残像を作る。
cbuffer RADIAL_BLUR_CONSTANT_BUFFER : register(b2)
{
    float blur_radius;
    int blur_sampling_count;
    float2 blur_center;
    float blur_mask_radius;
    float3 blur_dummy;
};

Texture2D scene_map : register(t0);
SamplerState linear_sampler_state : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
    float2 size;
    scene_map.GetDimensions(size.x, size.y);
    float4 original = scene_map.Sample(linear_sampler_state, pin.texcoord);
    float4 blurred = original;
    int sample_count = max(blur_sampling_count, 1);
    float2 step_uv = (blur_center - pin.texcoord) * (blur_radius / size) / sample_count;
    [loop] for (int i = 1; i < sample_count; ++i)
        blurred += scene_map.Sample(linear_sampler_state, pin.texcoord + step_uv * i);

    blurred /= sample_count;
    float mask_size = max(blur_mask_radius / min(size.x, size.y), 0.000001f);
    float mask = saturate(length(pin.texcoord - blur_center) / mask_size);
    return lerp(original, blurred, mask);
}
