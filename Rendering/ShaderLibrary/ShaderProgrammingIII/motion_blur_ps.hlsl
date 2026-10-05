#include "motion_blur.hlsli"
#include "shading_functions.hlsli"
Texture2D<float4> scene_color : register(t0);
Texture2D<float> gbuffer_depth : register(t4);
Texture2D<float4> gbuffer_velocity : register(t5);
Texture2D<float4> neighbor_max_velocity : register(t6);
SamplerState sampler_states[5] : register(s0);

cbuffer MOTION_BLUR_CONSTANT_BUFFER : register(b2)
{
    float motion_blur_intensity;
    uint motion_blur_samples;
    float motion_blur_depth_scale;
    float motion_blur_dummy;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    // Unit09: 速度方向へ前後サンプルし、深度差の大きい別物体は混ぜすぎない。
    float2 velocity = gbuffer_velocity.Sample(sampler_states[0], pin.texcoord).xy;
    // Unit09: 縮小テクスチャも正規化UVで参照するため、元画面と同じUVを使う。
    float2 neighbor = neighbor_max_velocity.Sample(sampler_states[0], pin.texcoord).xy;
    if(dot(neighbor,neighbor)>dot(velocity,velocity)) velocity=neighbor;
    velocity *= motion_blur_intensity;
    float center_depth=gbuffer_depth.Sample(sampler_states[0], pin.texcoord);
    float4 sum=0; float weight_sum=0; uint count=max(motion_blur_samples,1);
    [loop] for(uint i=0;i<count;++i)
    {
        float t=((i+0.5f)/count)-0.5f; float2 uv=pin.texcoord+velocity*t;
        float depth=gbuffer_depth.Sample(sampler_states[0],uv);
        float weight=saturate(1.0f-abs(depth-center_depth)*motion_blur_depth_scale);
        sum += scene_color.Sample(sampler_states[1],uv)*weight; weight_sum+=weight;
    }
    float4 color = sum/max(weight_sum,0.0001f);
    // Unit09: 最終表示前にリニア色からガンマ空間へ戻す。
    color.rgb = pow(saturate(color.rgb), 1.0f / GammaFactor);
    return color;
}
