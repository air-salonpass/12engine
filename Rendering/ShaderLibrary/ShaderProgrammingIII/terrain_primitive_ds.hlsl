#include "terrain_primitive.hlsli"
Texture2D<float4> height_map : register(t0);
SamplerState sampler_states[3] : register(s0);

float sample_height(float2 uv) { return height_map.SampleLevel(sampler_states[1], uv, 0).r * height_scale; }

[domain("tri")]
DS_OUT main(HS_CONSTANT_OUT c, float3 b : SV_DomainLocation, const OutputPatch<DS_IN, 3> p)
{
    DS_OUT o;
    float4 local = p[0].position*b.x + p[1].position*b.y + p[2].position*b.z;
    float2 uv = p[0].texcoord*b.x + p[1].texcoord*b.y + p[2].texcoord*b.z;
    // Unit02: 高さテクスチャで頂点を押し上げ、近傍差分から地形法線を再構築する。
    uint w,h; height_map.GetDimensions(w,h); float2 texel=rcp(float2(w,h));
    local.y += sample_height(uv);
    float dx = sample_height(uv + float2(texel.x,0)) - sample_height(uv - float2(texel.x,0));
    float dz = sample_height(uv + float2(0,texel.y)) - sample_height(uv - float2(0,texel.y));
    float4 wp = mul(local, world);
    o.world_position = wp;
    o.normal = normalize(mul(float4(-dx, 2.0f*texel.x, -dz, 0), world).xyz);
    o.position = mul(wp, view_projection_transform);
    o.color = p[0].color*b.x + p[1].color*b.y + p[2].color*b.z;
    o.texcoord = uv * tiling_scale;
    return o;
}
