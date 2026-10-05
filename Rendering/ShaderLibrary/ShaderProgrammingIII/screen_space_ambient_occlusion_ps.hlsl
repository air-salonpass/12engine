#include "sprite.hlsli"
#include "scene_constant_buffer.hlsli"
#include "gbuffer.hlsli"

Texture2D<float4> scene_color : register(t0);
Texture2D<float4> gbuffer_normal : register(t2);
Texture2D<float> gbuffer_depth : register(t4);
SamplerState sampler_states[5] : register(s0);

cbuffer SSAO_CONSTANT_BUFFER : register(b2)
{
    float ssao_radius;
    float ssao_intensity;
    float ssao_bias;
    float ssao_power;
};

static const float2 kernel[16] = {
    float2(1,0),float2(-1,0),float2(0,1),float2(0,-1),float2(.707,.707),float2(-.707,.707),
    float2(.707,-.707),float2(-.707,-.707),float2(.38,.92),float2(-.38,.92),float2(.92,.38),float2(-.92,.38),
    float2(.38,-.92),float2(-.38,-.92),float2(.92,-.38),float2(-.92,-.38) };

float4 main(VS_OUT pin) : SV_TARGET
{
    // Unit07: 周辺深度が手前にある割合を数え、遮蔽率として返す。
    float center_depth = gbuffer_depth.Sample(sampler_states[0], pin.texcoord);
    float3 normal = normalize(gbuffer_normal.Sample(sampler_states[0], pin.texcoord).xyz);
    float occlusion = 0;
    [unroll] for (int i=0;i<16;++i)
    {
        float scale = ssao_radius * (0.25f + 0.75f * (i+1)/16.0f);
        float d = gbuffer_depth.Sample(sampler_states[0], pin.texcoord + kernel[i] * viewport_size.zw * scale);
        occlusion += (d + ssao_bias < center_depth) ? 1.0f : 0.0f;
    }
    float ao = pow(saturate(1.0f - occlusion / 16.0f * ssao_intensity), max(ssao_power, 0.0001f));
    float4 scene = scene_color.Sample(sampler_states[1], pin.texcoord);
    return float4(scene.rgb * ao, scene.a);
}
