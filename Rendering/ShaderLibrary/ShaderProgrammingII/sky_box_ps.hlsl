#include "sky_box.hlsli"

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
    //	視線ベクトル
    float3 E = normalize(pin.world_position.xyz - camera_position.xyz);

    //  環境光の色を適応しておく
    float3 ambient = ambient_color.rgb * ambient_color.a;
    
    //  スカイボックスから色を取得する
    return float4(ambient, 1) * SampleSkybox(texture0, sampler0, E) * pin.color;
}
