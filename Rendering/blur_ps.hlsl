#include "fullscreen_quad.hlsli"

#define LINEAR 1
#define ANISOTROPIC 2

SamplerState sampler_states[3] : register(s0);
Texture2D texture_maps[4] : register(t0);

float4 main(VS_OUT input) : SV_TARGET
{
	uint width;
	uint height;
	uint number_of_levels;
	texture_maps[1].GetDimensions(0, width, height, number_of_levels);

	float4 color = texture_maps[0].Sample(sampler_states[ANISOTROPIC], input.texcoord);
	float3 blur_color = 0;
	float gaussian_kernel_total = 0;

	// 半解像度の高輝度画像へ7x7のガウシアンブラーをかける。
	static const int gaussian_half_kernel_size = 3;
	const float safe_sigma = max(gaussian_sigma, 0.01);
	[unroll]
	for (int x = -gaussian_half_kernel_size; x <= gaussian_half_kernel_size; ++x)
	{
		[unroll]
		for (int y = -gaussian_half_kernel_size; y <= gaussian_half_kernel_size; ++y)
		{
			const float gaussian_kernel =
				exp(-(x * x + y * y) / (2.0 * safe_sigma * safe_sigma)) /
				(2.0 * 3.14159265358979 * safe_sigma * safe_sigma);
			const float2 offset = float2(
				x / (float)width,
				y / (float)height);
			blur_color += texture_maps[1].Sample(
				sampler_states[LINEAR],
				input.texcoord + offset).rgb * gaussian_kernel;
			gaussian_kernel_total += gaussian_kernel;
		}
	}

	// 元のHDRシーンへぼかした高輝度成分を加算してブルームを作る。
	color.rgb += (blur_color / gaussian_kernel_total) * bloom_intensity;

	// HDRをSDRへ収めた後、モニター表示用のガンマ補正を行う。
	color.rgb = 1.0 - exp(-color.rgb * exposure);
	static const float gamma = 2.2;
	color.rgb = pow(saturate(color.rgb), 1.0 / gamma);
	return float4(color.rgb, color.a);
}
