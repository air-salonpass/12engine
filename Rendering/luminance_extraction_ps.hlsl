#include "fullscreen_quad.hlsli"

#define ANISOTROPIC 2

SamplerState sampler_states[3] : register(s0);
Texture2D texture_maps[4] : register(t0);

float4 main(VS_OUT input) : SV_TARGET
{
	float4 color = texture_maps[0].Sample(sampler_states[ANISOTROPIC], input.texcoord);
	const float luminance = dot(color.rgb, float3(0.299, 0.587, 0.114));

	// しきい値以上の高輝度成分だけを半解像度バッファへ残す。
	color.rgb *= step(luminance_threshold, luminance);
	return color;
}
