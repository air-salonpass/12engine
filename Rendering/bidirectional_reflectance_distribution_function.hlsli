// UNIT39: BRDFの分割和近似から環境光を求めるため、IBLテクスチャのサンプリング関数を取り込む。
#include "image_based_lighting.hlsli"

// UNIT38: Schlick近似で、視線角度に応じたフレネル反射率を求める。
float3 f_schlick(float3 f0, float3 f90, float VoH)
{
	return f0 + (f90 - f0) * pow(saturate(1.0 - VoH), 5.0);
}

// UNIT38: GGXの幾何減衰項。視線と光が面へ斜めになるほど遮蔽を強くする。
float v_ggx(float NoL, float NoV, float alpha_roughness)
{
	const float alpha_squared = alpha_roughness * alpha_roughness;
	const float attenuation_light = NoV * sqrt(NoL * NoL * (1.0 - alpha_squared) + alpha_squared);
	const float attenuation_view = NoL * sqrt(NoV * NoV * (1.0 - alpha_squared) + alpha_squared);
	return 0.5 / max(attenuation_light + attenuation_view, 0.00001);
}

// UNIT38: GGXの法線分布項。粗さに応じて鏡面反射の広がりを決める。
float d_ggx(float NoH, float alpha_roughness)
{
	const float alpha_squared = alpha_roughness * alpha_roughness;
	const float denominator = NoH * NoH * (alpha_squared - 1.0) + 1.0;
	return alpha_squared / max(PI * denominator * denominator, 0.00001);
}

// UNIT38: Lambert反射として拡散反射色を半球上へ均等に分配する。
float3 brdf_lambertian(float3 diffuse_color)
{
	return diffuse_color / PI;
}

// UNIT38: Fresnel・可視性・法線分布を合成し、GGX鏡面反射を返す。
float3 brdf_specular_ggx(
	float3 f0,
	float3 f90,
	float alpha_roughness,
	float VoH,
	float NoL,
	float NoV,
	float NoH)
{
	return f_schlick(f0, f90, VoH) *
		v_ggx(NoL, NoV, alpha_roughness) *
		d_ggx(NoH, alpha_roughness);
}

// UNIT39: 拡散IEMとGGX LUTを使い、多重散乱を含むLambert環境光を計算する。
float3 ibl_radiance_lambertian(
	float3 N,
	float3 V,
	float roughness,
	float3 diffuse_color,
	float3 f0)
{
	const float NoV = saturate(dot(N, V));
	const float2 f_ab = sample_lut_ggx(float2(NoV, roughness)).rg;
	const float3 irradiance = sample_diffuse_iem(N).rgb;
	const float3 fr = max(1.0 - roughness, f0) - f0;
	const float3 k_s = f0 + fr * pow(1.0 - NoV, 5.0);
	const float3 fss_ess = k_s * f_ab.x + f_ab.y;
	const float ems = 1.0 - (f_ab.x + f_ab.y);
	const float3 f_avg = f0 + (1.0 - f0) / 21.0;
	const float3 fms_ems =
		ems * fss_ess * f_avg / max(1.0 - f_avg * ems, 0.00001);
	const float3 k_d = diffuse_color * (1.0 - fss_ess + fms_ems);
	return (fms_ems + k_d) * irradiance;
}

// UNIT39: 反射方向のPMREMとGGX LUTから、粗さに応じた鏡面環境光を計算する。
float3 ibl_radiance_ggx(
	float3 N,
	float3 V,
	float roughness,
	float3 f0)
{
	const float NoV = saturate(dot(N, V));
	const float2 f_ab = sample_lut_ggx(float2(NoV, roughness)).rg;
	const float3 reflection = normalize(reflect(-V, N));
	const float3 specular_light = sample_specular_pmrem(reflection, roughness).rgb;
	const float3 fr = max(1.0 - roughness, f0) - f0;
	const float3 k_s = f0 + fr * pow(1.0 - NoV, 5.0);
	const float3 fss_ess = k_s * f_ab.x + f_ab.y;
	return specular_light * fss_ess;
}
