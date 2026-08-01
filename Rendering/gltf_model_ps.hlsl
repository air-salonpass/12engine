#include "gltf_model.hlsli"

static const float PI = 3.14159265358979323846;

// UNIT39: IBLヘッダーも共通サンプラーを使うため、BRDFをインクルードする前に宣言する。
#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2
SamplerState sampler_states[3] : register(s0);

#include "bidirectional_reflectance_distribution_function.hlsli"

// UNIT35: CPU側のmaterial::cbufferと同じ順序・型で定義し、StructuredBufferから材質を読む。
struct texture_info
{
	int index;
	int texcoord;
};

struct normal_texture_info
{
	int index;
	int texcoord;
	float scale;
};

struct occlusion_texture_info
{
	int index;
	int texcoord;
	float strength;
};

struct pbr_metallic_roughness
{
	float4 basecolor_factor;
	texture_info basecolor_texture;
	float metallic_factor;
	float roughness_factor;
	texture_info metallic_roughness_texture;
};

struct material_constants
{
	float3 emissive_factor;
	int alpha_mode;
	float alpha_cutoff;
	int double_sided;
	pbr_metallic_roughness pbr;
	normal_texture_info normal_texture;
	occlusion_texture_info occlusion_texture;
	texture_info emissive_texture;
};

StructuredBuffer<material_constants> materials : register(t0);

// UNIT36: 各材質の5種類の画像を、CPU側と同じt1～t5の順番で受け取る。
#define BASECOLOR_TEXTURE 0
#define METALLIC_ROUGHNESS_TEXTURE 1
#define NORMAL_TEXTURE 2
#define EMISSIVE_TEXTURE 3
#define OCCLUSION_TEXTURE 4
Texture2D<float4> material_textures[5] : register(t1);

float4 main(VS_OUT input) : SV_TARGET
{
	const material_constants material_data = materials[material];

	// UNIT38: 色テクスチャはsRGB相当なので、2.2乗して線形空間へ戻してから係数を掛ける。
	float4 basecolor = material_data.pbr.basecolor_factor;
	if (material_data.pbr.basecolor_texture.index > -1)
	{
		const float4 sampled_basecolor =
			material_textures[BASECOLOR_TEXTURE].Sample(sampler_states[ANISOTROPIC], input.texcoord);
		basecolor *= float4(pow(saturate(sampled_basecolor.rgb), 2.2), sampled_basecolor.a);
	}

	// UNIT38: Metallic-Roughness画像ではGが粗さ、Bが金属度として格納されている。
	float metallic_factor = material_data.pbr.metallic_factor;
	float roughness_factor = material_data.pbr.roughness_factor;
	if (material_data.pbr.metallic_roughness_texture.index > -1)
	{
		const float4 metallic_roughness =
			material_textures[METALLIC_ROUGHNESS_TEXTURE].Sample(sampler_states[LINEAR], input.texcoord);
		roughness_factor *= metallic_roughness.g;
		metallic_factor *= metallic_roughness.b;
	}
	metallic_factor = saturate(metallic_factor);
	roughness_factor = clamp(roughness_factor, 0.04, 1.0);

	// UNIT38: AO画像のRチャンネルとstrengthから、遮蔽されていない割合を作る。
	float ambient_occlusion = 1.0;
	if (material_data.occlusion_texture.index > -1)
	{
		const float sampled_occlusion =
			material_textures[OCCLUSION_TEXTURE].Sample(sampler_states[LINEAR], input.texcoord).r;
		ambient_occlusion = lerp(1.0, sampled_occlusion, material_data.occlusion_texture.strength);
	}

	// UNIT38: 金属はベースカラーを鏡面色へ使い、非金属は標準反射率4%を使う。
	const float3 f0 = lerp(float3(0.04, 0.04, 0.04), basecolor.rgb, metallic_factor);
	const float3 f90 = float3(1.0, 1.0, 1.0);
	const float alpha_roughness = roughness_factor * roughness_factor;
	const float3 diffuse_color = lerp(basecolor.rgb, float3(0.0, 0.0, 0.0), metallic_factor);

	const float3 V = normalize(camera_position.xyz - input.world_position.xyz);
	const float3 L = normalize(-light_direction.xyz);
	float3 N = normalize(input.world_normal.xyz);

	// UNIT38: 頂点の接線が無い場合も、法線から直交する接線を組み立てて法線マップを適用する。
	const float3 tangent_reference =
		abs(N.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
	const float3 T =
		has_tangent ? normalize(input.world_tangent.xyz) : normalize(cross(tangent_reference, N));
	const float tangent_sign = has_tangent ? input.world_tangent.w : 1.0;
	const float3 B = normalize(cross(N, T)) * tangent_sign;
	if (material_data.normal_texture.index > -1)
	{
		float3 tangent_normal =
			material_textures[NORMAL_TEXTURE].Sample(sampler_states[LINEAR], input.texcoord).xyz * 2.0 - 1.0;
		tangent_normal.xy *= material_data.normal_texture.scale;
		N = normalize(tangent_normal.x * T + tangent_normal.y * B + tangent_normal.z * N);
	}

	// UNIT38: Lambert拡散反射とCook-Torrance GGX鏡面反射を1灯の平行光源で合成する。
	const float3 H = normalize(V + L);
	const float NoL = saturate(dot(N, L));
	const float NoV = saturate(dot(N, V));
	const float NoH = saturate(dot(N, H));
	const float VoH = saturate(dot(V, H));
	// UNIT39: 直接光が当たらない面にも、環境マップから拡散光と鏡面反射を与える。
	float3 diffuse = ibl_radiance_lambertian(
		N, V, roughness_factor, diffuse_color, f0) * ibl_intensity;
	float3 specular = ibl_radiance_ggx(
		N, V, roughness_factor, f0) * ibl_intensity;
	if (NoL > 0.0 && NoV > 0.0)
	{
		diffuse += brdf_lambertian(diffuse_color) * NoL;
		specular += brdf_specular_ggx(
			f0, f90, alpha_roughness, VoH, NoL, NoV, NoH) * NoL;
	}
	const float3 color = (diffuse + specular) * ambient_occlusion;

	// UNIT38: 発光色も線形空間へ戻し、照明計算の結果へ最後に加える。
	float3 emissive = material_data.emissive_factor;
	if (material_data.emissive_texture.index > -1)
	{
		emissive *= pow(saturate(
			material_textures[EMISSIVE_TEXTURE].Sample(sampler_states[ANISOTROPIC], input.texcoord).rgb), 2.2);
	}
	return float4(color + emissive, basecolor.a);
}
