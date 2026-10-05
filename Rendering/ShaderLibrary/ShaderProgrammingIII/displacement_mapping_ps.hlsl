#include "displacement_mapping.hlsli"
Texture2D<float4> color_map : register(t0);
SamplerState anisotropic_sampler : register(s2);

// Unit01: 変位後の面へ同じUVで世界地図のカラー画像を貼る。
float4 main(DS_OUT pin) : SV_TARGET
{
    return color_map.Sample(anisotropic_sampler, pin.texcoord) * pin.color;
}
