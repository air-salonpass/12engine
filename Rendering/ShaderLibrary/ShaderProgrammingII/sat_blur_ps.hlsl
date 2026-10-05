#include "sprite.hlsli"

Texture2D<float4> color_map : register(t0);
SamplerState samplerstate : register(s0);

cbuffer SAT_BLUR_CONSTANT_BUFFER : register(b0)
{
    float2 blur_size;
    float2 texture_size;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    //  ブラー範囲を計算
    float2 blur_texcoord = blur_size / texture_size * 0.5f;
    float2 texcoord11 = saturate(pin.texcoord + float2(+blur_texcoord.x, +blur_texcoord.y));
    float2 texcoord01 = saturate(pin.texcoord + float2(-blur_texcoord.x, +blur_texcoord.y));
    float2 texcoord10 = saturate(pin.texcoord + float2(+blur_texcoord.x, -blur_texcoord.y));
    float2 texcoord00 = saturate(pin.texcoord + float2(-blur_texcoord.x, -blur_texcoord.y));

    //  色情報取得
    //  SAT化したことで4点のサンプリングだけでその範囲の総和が求まります。
    float3 color11 = color_map.SampleLevel(samplerstate, texcoord11, 0).rgb;
    float3 color01 = color_map.SampleLevel(samplerstate, texcoord01, 0).rgb;
    float3 color10 = color_map.SampleLevel(samplerstate, texcoord10, 0).rgb;
    float3 color00 = color_map.SampleLevel(samplerstate, texcoord00, 0).rgb;
    float2 texcoord_delta = (texcoord11 - texcoord00) * texture_size;
    float4 color = float4(0, 0, 0, 1);
    color.rgb = (color11 - color01 - color10 + color00) / (texcoord_delta.x * texcoord_delta.y);
    return color * pin.color;
}

