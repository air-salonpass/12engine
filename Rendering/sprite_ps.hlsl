#include "sprite.hlsli"

Texture2D color_map : register(t0);

SamplerState point_sampler_state : register(s0);
SamplerState linear_sampler_state : register(s1);
SamplerState anisotropic_sampler_state : register(s2);

float4 main(VS_OUT input) : SV_TARGET
{
	float4 color = color_map.Sample(anisotropic_sampler_state, input.texcoord);
	const float alpha = color.a;

	// JPEGなどのガンマ補正済み色を、照明・ブルーム処理用のリニア色空間へ戻す。
	static const float gamma = 2.2;
	color.rgb = pow(color.rgb, gamma);
	return float4(color.rgb, alpha) * input.color;
}
