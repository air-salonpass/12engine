#ifndef IMAGE_BASED_LIGHTING_HLSLI
#define IMAGE_BASED_LIGHTING_HLSLI

// UNIT39: IBLで使う背景画像・拡散環境光・鏡面環境光・GGX積分LUTをt32～t35で受け取る。
TextureCube<float4> skybox : register(t32);
TextureCube<float4> diffuse_iem : register(t33);
TextureCube<float4> specular_pmrem : register(t34);
Texture2D<float4> lut_ggx : register(t35);

// UNIT39: LUTの端をREPEATサンプラーで反対側へ巻き戻さないよう、半テクセル内側を参照する。
float4 sample_lut_ggx(float2 brdf_sample_point)
{
	uint width;
	uint height;
	lut_ggx.GetDimensions(width, height);
	const float2 half_texel = 0.5 / float2(width, height);
	return lut_ggx.Sample(
		sampler_states[LINEAR],
		clamp(brdf_sample_point, half_texel, 1.0 - half_texel));
}

// UNIT40: HDRから生成したキューブ型skyboxを、方向ベクトルと粗さに対応するLODで読む。
float4 sample_skybox(float3 direction, float roughness)
{
	uint width;
	uint height;
	uint number_of_levels;
	skybox.GetDimensions(0, width, height, number_of_levels);
	const float lod = saturate(roughness) * float(number_of_levels - 1);
	return skybox.SampleLevel(sampler_states[LINEAR], normalize(direction), lod);
}

// UNIT39: 法線方向に畳み込んだ低解像度キューブマップから拡散環境光を得る。
float4 sample_diffuse_iem(float3 direction)
{
	return diffuse_iem.SampleLevel(sampler_states[LINEAR], normalize(direction), 0.0);
}

// UNIT39: 粗さをミップLODへ変換し、事前フィルター済み環境マップから鏡面環境光を得る。
float4 sample_specular_pmrem(float3 direction, float roughness)
{
	uint width;
	uint height;
	uint number_of_levels;
	specular_pmrem.GetDimensions(0, width, height, number_of_levels);
	const float lod = saturate(roughness) * float(number_of_levels - 1);
	return specular_pmrem.SampleLevel(sampler_states[LINEAR], normalize(direction), lod);
}

#endif
