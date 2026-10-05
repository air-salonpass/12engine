#include "deferred_rendering.hlsli"
Texture2D<float4> gbuffer_base_color : register(t0);
Texture2D<float4> gbuffer_parameter : register(t3);
Texture2D<float4> ssao_map : register(t10);
SamplerState linear_sampler : register(s1);
// Unit07: 間接光へSSAOを乗算し、接触部だけを暗くする。
float4 main(VS_OUT pin) : SV_TARGET
{
    float3 base_color = gbuffer_base_color.Sample(linear_sampler, pin.texcoord).rgb;
    float occlusion_factor = gbuffer_parameter.Sample(linear_sampler, pin.texcoord).r;
    float ao = ssao_map.Sample(linear_sampler, pin.texcoord).r;
    return float4(base_color * ao * occlusion_factor, 1);
}
