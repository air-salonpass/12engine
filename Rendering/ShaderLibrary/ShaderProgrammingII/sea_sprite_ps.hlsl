#include "sprite.hlsli"

#include "scene_constant_buffer.hlsli"

Texture2D color_map : register(t0);
Texture2D flow_map : register(t1);
SamplerState linear_sampler_state : register(s0);

// Unit01: フローマップの速度と変位量。
cbuffer SCROLL_CONSTANT_BUFFER : register(b2)
{
    float flow_speed;
    float flow_power;
    float2 scroll_padding;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    float2 scroll_direction = flow_map.Sample(linear_sampler_state, pin.texcoord).xy * 2.0f - 1.0f;
    scroll_direction *= flow_power;

    // Unit01 応用: 半周期ずらした2サンプルをクロスフェードしてループ境界を隠す。
    float progress1 = frac(flow_speed * options.z);
    float progress2 = frac(progress1 + 0.5f);
    float4 color1 = color_map.Sample(linear_sampler_state, pin.texcoord + scroll_direction * progress1);
    float4 color2 = color_map.Sample(linear_sampler_state, pin.texcoord + scroll_direction * progress2);
    float blend_rate = abs((progress1 - 0.5f) * 2.0f);
    float4 color = lerp(color1, color2, blend_rate);
    return color * pin.color;
}

