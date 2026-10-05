#include "sprite.hlsli"
#include "shading_functions.hlsli"

// Unit10: 輝度差からエッジ方向を推定し、その方向の色を混ぜてジャギーを抑える。
Texture2D<float4> scene_color_map : register(t0);
SamplerState sampler_states[5] : register(s0);

#define FXAA_REDUCE_MIN (1.0f / 128.0f)
#define FXAA_REDUCE_MUL (1.0f / 8.0f)
#define FXAA_SPAN_MAX 8.0f

float luminance(float3 color) { return dot(color, float3(0.299f, 0.587f, 0.114f)); }

float3 fxaa(float2 uv, float2 texel)
{
    float3 nw = scene_color_map.Sample(sampler_states[1], uv + texel * float2(-1, -1)).rgb;
    float3 ne = scene_color_map.Sample(sampler_states[1], uv + texel * float2(+1, -1)).rgb;
    float3 sw = scene_color_map.Sample(sampler_states[1], uv + texel * float2(-1, +1)).rgb;
    float3 se = scene_color_map.Sample(sampler_states[1], uv + texel * float2(+1, +1)).rgb;
    float3 m  = scene_color_map.Sample(sampler_states[1], uv).rgb;
    float l_nw = luminance(nw), l_ne = luminance(ne), l_sw = luminance(sw), l_se = luminance(se), l_m = luminance(m);
    float l_min = min(l_m, min(min(l_nw, l_ne), min(l_sw, l_se)));
    float l_max = max(l_m, max(max(l_nw, l_ne), max(l_sw, l_se)));
    float2 dir = float2(-((l_nw + l_ne) - (l_sw + l_se)), (l_nw + l_sw) - (l_ne + l_se));
    float reduce = max((l_nw + l_ne + l_sw + l_se) * (0.25f * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), -FXAA_SPAN_MAX, FXAA_SPAN_MAX) * texel;
    float3 a = 0.5f * (scene_color_map.Sample(sampler_states[1], uv + dir * (-1.0f / 6.0f)).rgb +
                       scene_color_map.Sample(sampler_states[1], uv + dir * (+1.0f / 6.0f)).rgb);
    float3 b = a * 0.5f + 0.25f * (scene_color_map.Sample(sampler_states[1], uv - dir * 0.5f).rgb +
                                   scene_color_map.Sample(sampler_states[1], uv + dir * 0.5f).rgb);
    float l_b = luminance(b);
    return (l_b < l_min || l_b > l_max) ? a : b;
}

float4 main(VS_OUT pin) : SV_TARGET
{
    uint width, height;
    scene_color_map.GetDimensions(width, height);
    float4 source = scene_color_map.Sample(sampler_states[1], pin.texcoord);
    // Unit10: ディファード結果はリニア色なので、表示時にガンマ空間へ戻す。
    return float4(pow(saturate(fxaa(pin.texcoord, rcp(float2(width, height)))), 1.0f / GammaFactor), source.a);
}
