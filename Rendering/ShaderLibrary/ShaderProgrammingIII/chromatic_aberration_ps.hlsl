#include "sprite.hlsli"

// Unit06: RGBごとに異なる位置を読むことで、レンズの倍率色収差を再現する。
cbuffer CHROMATIC_ABERRATION_CONSTANT_BUFFER : register(b2)
{
    float chromatic_aberration_amount;
    int chromatic_aberration_max_samples;
    float2 chromatic_aberration_dummy;
    float4 chromatic_aberration_shift[3];
};

Texture2D scene_map : register(t0);
SamplerState linear_sampler_state : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
    float2 scene_map_size;
    scene_map.GetDimensions(scene_map_size.x, scene_map_size.y);
    float2 coords = 2.0f * pin.texcoord - 1.0f;
    float2 end = pin.texcoord - coords * dot(coords, coords) * chromatic_aberration_amount;
    float2 diff = end - pin.texcoord;
    int samples = clamp(int(length(scene_map_size * diff / 2.0f)), 3, min(chromatic_aberration_max_samples, 16));
    float2 delta = diff / samples;
    float2 pos = pin.texcoord;
    float4 sum = 0.0f.xxxx;
    float4 filter_sum = 0.0f.xxxx;
    // Unit06: R・G・Bのフィルターを順に重ね、色ごとのずれを滑らかに見せる。
    [unroll] for (int index = 0; index < 16; ++index)
    {
        int t = int(index + 0.5f) / samples;
        float4 sampled = scene_map.Sample(linear_sampler_state, pos);
        float4 filter = float4(chromatic_aberration_shift[t % 3].rgb, 1.0f);
        sum += sampled * filter;
        filter_sum += filter;
        pos += delta;
    }
    return sum / max(filter_sum, 0.000001f.xxxx);
}
