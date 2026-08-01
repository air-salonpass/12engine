struct VS_OUT
{
	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD;
};

// UNIT32の高輝度抽出とブルーム合成をCPUから調整する定数。
cbuffer POST_EFFECT_CONSTANTS : register(b0)
{
	float luminance_threshold;
	float gaussian_sigma;
	float bloom_intensity;
	float exposure;
};
