#include "fullscreen_quad.hlsli"

#define LINEAR 1

SamplerState sampler_states[3] : register(s0);
Texture2D texture_map : register(t0);

float4 main(VS_OUT input) : SV_TARGET
{
	// 加工しない場合は、オフスクリーンの色をそのまま画面へ転送する。
	return texture_map.Sample(sampler_states[LINEAR], input.texcoord);
}
