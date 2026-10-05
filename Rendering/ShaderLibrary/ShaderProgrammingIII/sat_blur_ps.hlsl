#include "sprite.hlsli"
Texture2D<float4> color_map : register(t0);
SamplerState samplerstate : register(s0);
cbuffer SAT_BLUR_CONSTANT_BUFFER : register(b0) { float2 blur_size; float2 texture_size; };

float4 main(VS_OUT pin) : SV_TARGET
{
    // Unit11: SATの四隅だけで矩形領域の総和を復元するため、ぼかし幅に依存せず4回参照で済む。
    float2 half_uv=blur_size/texture_size*0.5f;
    float2 p11=saturate(pin.texcoord+half_uv), p01=saturate(pin.texcoord+float2(-half_uv.x,half_uv.y));
    float2 p10=saturate(pin.texcoord+float2(half_uv.x,-half_uv.y)), p00=saturate(pin.texcoord-half_uv);
    float3 sum=color_map.SampleLevel(samplerstate,p11,0).rgb-color_map.SampleLevel(samplerstate,p01,0).rgb-
               color_map.SampleLevel(samplerstate,p10,0).rgb+color_map.SampleLevel(samplerstate,p00,0).rgb;
    float2 extent=max((p11-p00)*texture_size,1.0f.xx);
    return float4(sum/(extent.x*extent.y),1)*pin.color;
}
