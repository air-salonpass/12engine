#include "skinned_mesh.hlsli"

#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2

SamplerState sampler_states[3] : register(s0);
Texture2D texture_maps[4] : register(t0);

float4 main(VS_OUT pin) : SV_TARGET
{
    // UV座標で拡散テクスチャをサンプリングする。
	float4 color =
		texture_maps[0].Sample(sampler_states[ANISOTROPIC], pin.texcoord);
	float alpha = color.a;

	// テクスチャ色をリニア色空間へ戻してから、拡散・鏡面反射を計算する。
	static const float gamma = 2.2;
	color.rgb = pow(color.rgb, gamma);

	// 法線N・接線Tを直交化し、接線空間からワールド空間へ移す基底を作る。
    float3 N = normalize(pin.world_normal.xyz);
	float3 T = normalize(pin.world_tangent.xyz);
	float tangent_sigma = pin.world_tangent.w;
	T = normalize(T - N * dot(N, T));
	float3 B = normalize(cross(N, T) * tangent_sigma);

	// 法線マップの[0,1]を[-1,1]へ戻し、TBN基底でワールド法線へ変換する。
	float3 mapped_normal =
		texture_maps[1].Sample(sampler_states[LINEAR], pin.texcoord).xyz;
	mapped_normal = mapped_normal * 2.0f - 1.0f;
	N = normalize(
		mapped_normal.x * T +
		mapped_normal.y * B +
		mapped_normal.z * N);

	// ランバート拡散反射に、ハーフベクトルを使った鏡面反射を加える。
    float3 L = normalize(-light_direction.xyz);
    float3 diffuse = color.rgb * max(0, dot(N, L));
	float3 V = normalize(camera_position.xyz - pin.world_position.xyz);
	float3 H = normalize(V + L);
	float3 specular = pow(max(0, dot(N, H)), 128.0f);
    return float4(diffuse + specular, alpha) * pin.color;
}
