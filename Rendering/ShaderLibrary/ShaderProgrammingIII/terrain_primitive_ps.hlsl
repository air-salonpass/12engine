#include "terrain_primitive.hlsli"
Texture2D<float4> textures[2] : register(t0);
Texture2D<float4> terrain_textures[3] : register(t20);
SamplerState sampler_states[3] : register(s0);
float4 main(PS_IN pin) : SV_TARGET
{
    // Unit02: 編集テクスチャのG成分に応じて、岩→土→草の順に地表材質を混ぜる。
    float2 tiled_uv = pin.texcoord * tiling_scale;
    float4 rock = terrain_textures[0].Sample(sampler_states[2], tiled_uv);
    float4 dirt = terrain_textures[1].Sample(sampler_states[2], tiled_uv);
    float4 grass = terrain_textures[2].Sample(sampler_states[2], tiled_uv);
    float blend_rate = saturate(textures[1].Sample(sampler_states[2], pin.texcoord).g);
    float4 color = lerp(rock, dirt, smoothstep(0.0f, 0.5f, blend_rate));
    return lerp(color, grass, smoothstep(0.5f, 1.0f, blend_rate)) * pin.color;
}
