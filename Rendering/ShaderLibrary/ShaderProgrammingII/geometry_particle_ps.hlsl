// Unit06-02: ポイントをカメラ正対クアッドへ展開するGPUパーティクルシェーダー。
#include "geometry_particle.hlsli"

Texture2D color_map : register(t0);

#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2
SamplerState sampler_states[3] : register(s0);

float4 main(PS_IN pin) : SV_TARGET0
{
    return color_map.Sample(sampler_states[ANISOTROPIC], pin.texcoord) * pin.color;
}


