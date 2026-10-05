#include "gltf_model_forward.hlsli"

// https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#reference-textureinfo
struct texture_info
{
	int index; // required.
	int texcoord; // The set index of texture's TEXCOORD attribute used for texture coordinate mapping.
};
// https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#reference-material-normaltextureinfo
struct normal_texture_info
{
	int index; // required
	int texcoord; // The set index of texture's TEXCOORD attribute used for texture coordinate mapping.
	float scale; // scaledNormal = normalize((<sampled normal texture value> * 2.0 - 1.0) * vec3(<normal scale>, <normal scale>, 1.0))
};
// https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#reference-material-occlusiontextureinfo
struct occlusion_texture_info
{
	int index; // required
	int texcoord; // The set index of texture's TEXCOORD attribute used for texture coordinate mapping.
	float strength; // A scalar parameter controlling the amount of occlusion applied. A value of `0.0` means no occlusion. A value of `1.0` means full occlusion. This value affects the final occlusion value as: `1.0 + strength * (<sampled occlusion texture value> - 1.0)`.
};
// https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#reference-material-pbrmetallicroughness
struct pbr_metallic_roughness
{
	float4 basecolor_factor; // len = 4. default [1,1,1,1]
	texture_info basecolor_texture;
	float metallic_factor; // default 1
	float roughness_factor; // default 1
	texture_info metallic_roughness_texture;
};
struct material_constants
{
	float3 emissive_factor; // length 3. default [0, 0, 0]
	int alpha_mode; // "OPAQUE" : 0, "MASK" : 1, "BLEND" : 2
	float alpha_cutoff; // default 0.5
	int double_sided; // default false;

	pbr_metallic_roughness pbr_metallic_roughness;

	normal_texture_info normal_texture;
	occlusion_texture_info occlusion_texture;
	texture_info emissive_texture;
};
StructuredBuffer<material_constants> materials : register(t0);

#define BASECOLOR_TEXTURE 0
#define METALLIC_ROUGHNESS_TEXTURE 1
#define NORMAL_TEXTURE 2
#define EMISSIVE_TEXTURE 3
#define OCCLUSION_TEXTURE 4
Texture2D<float4> material_textures[5] : register(t1);

#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2
SamplerState sampler_states[3] : register(s0);

//	シャドウマップ
Texture2D shadow_map : register(t10);
SamplerState shadow_sampler_state : register(s10);

//  IBL用テクスチャ
TextureCube diffuse_iem : register(t33);
TextureCube specular_pmrem : register(t34);
Texture2D lut_ggx : register(t35);

float4 main(VS_OUT pin, bool is_front_face : SV_IsFrontFace) : SV_TARGET
{
    material_constants m = materials[material];

	//	ベースカラーを取得
    float4 base_color = m.pbr_metallic_roughness.basecolor_factor;
    if (m.pbr_metallic_roughness.basecolor_texture.index > -1)
    {
        float4 sampled = material_textures[BASECOLOR_TEXTURE].Sample(sampler_states[ANISOTROPIC], pin.texcoord);
        sampled.rgb = pow(sampled.rgb, GammaFactor);
        base_color *= sampled;
    }
     
    //  透明度調整
    clip(base_color.a - 0.01f);

	//	自己発光色を取得
    float3 emisive_color = m.emissive_factor;
    if (m.emissive_texture.index > -1)
    {
        float3 emisive = material_textures[EMISSIVE_TEXTURE].Sample(sampler_states[ANISOTROPIC], pin.texcoord).rgb;
        emisive.rgb = pow(emisive.rgb, GammaFactor);
        emisive_color.rgb *= emisive.rgb;
    }

	//	法線/従法線/接線
    float3 N = normalize(pin.w_normal.xyz);
    float3 T = has_tangent ? normalize(pin.w_tangent.xyz) : float3(1, 0, 0);
    float sigma = has_tangent ? pin.w_tangent.w : 1.0;
    T = normalize(T - N * dot(N, T));
    float3 B = normalize(cross(N, T) * sigma);
	//	裏面描画の場合は反転しておく
    if (is_front_face == false)
    {
        T = -T;
        B = -B;
        N = -N;
    }

	//	法線マッピング
    if (m.normal_texture.index > -1)
    {
        float4 sampled = material_textures[NORMAL_TEXTURE].Sample(sampler_states[LINEAR], pin.texcoord);
        float3 normal_factor = sampled.xyz;
        normal_factor = (normal_factor * 2.0f) - 1.0f;
        normal_factor = normalize(normal_factor * float3(m.normal_texture.scale, m.normal_texture.scale, 1.0f));
        N = normalize((normal_factor.x * T) + (normal_factor.y * B) + (normal_factor.z * N));
    }

	//	金属質/粗さを取得
    float roughness = m.pbr_metallic_roughness.roughness_factor;
    float metalness = m.pbr_metallic_roughness.metallic_factor;
    if (m.pbr_metallic_roughness.metallic_roughness_texture.index > -1)
    {
        float4 sampled = material_textures[METALLIC_ROUGHNESS_TEXTURE].Sample(sampler_states[LINEAR], pin.texcoord);
        roughness *= sampled.g;
        metalness *= sampled.b;
    }
    
	//	光の遮蔽値を取得
    float occlusion_factor = 1.0f;
    if (m.occlusion_texture.index > -1)
    {
        float4 sampled = material_textures[OCCLUSION_TEXTURE].Sample(sampler_states[LINEAR], pin.texcoord);
        occlusion_factor *= sampled.r;
    }
    const float occlusion_strength = m.occlusion_texture.strength;

	//	(非金属部分)
    float4 albedo = base_color;

	//	入射光のうち拡散反射になる割合
    float3 diffuse_reflectance = lerp(albedo.rgb, 0.0f, metalness);

	//	垂直反射時のフレネル反射率(非金属でも最低4%は鏡面反射する)
    float3 F0 = lerp(0.04f, albedo.rgb, metalness);

	//	視線ベクトル
    float3 V = normalize(pin.w_position.xyz - camera_position.xyz);

	//	直接光のシェーディング
    float3 total_diffuse = 0, total_specular = 0;
	{
		// 平行光源の処理
		{
            float3 diffuse = (float3) 0, specular = (float3) 0;
            float3 L = normalize(directional_light.direction.xyz);
            DirectBRDF(diffuse_reflectance, F0, N, V, L,
					   directional_light.color.rgb * directional_light.color.a, roughness,
					   diffuse, specular);

			//	平行光源用シャドウマップ
            float depth = shadow_map.Sample(shadow_sampler_state, pin.shadow_texcoord.xy).r;
			//	深度値を比較して影かどうかを判定する
            if (pin.shadow_texcoord.z - depth > shadow_bias)
            {
                diffuse *= shadow_attenuation;
                specular *= shadow_attenuation;
            }

            total_diffuse += diffuse;
            total_specular += specular;
        }

		//	点光源
        for (int i = 0; i < 8; ++i)
        {
            float3 L = pin.w_position.xyz - point_light[i].position.xyz;
            float len = length(L);
            if (len >= point_light[i].range)
                continue;
            float attenuateLength = saturate(1.0f - len / point_light[i].range);
            float attenuation = attenuateLength * attenuateLength;
            L /= len;
            float3 diffuse = (float3) 0, specular = (float3) 0;
            DirectBRDF(diffuse_reflectance, F0, N, V, L,
					   point_light[i].color.rgb * point_light[i].color.a, roughness,
					   diffuse, specular);
            total_diffuse += diffuse * attenuation;
            total_specular += specular * attenuation;
        }

		//	スポットライト
        for (int j = 0; j < 8; ++j)
        {
            float3 L = pin.w_position.xyz - spot_light[j].position.xyz;
            float len = length(L);
            if (len >= spot_light[j].range)
                continue;
            float attenuateLength = saturate(1.0f - len / spot_light[j].range);
            float attenuation = attenuateLength * attenuateLength;
            L /= len;
            float3 spotDirection = normalize(spot_light[j].direction.xyz);
            float innerCorn = cos(spot_light[j].innerCorn);
            float outerCorn = cos(spot_light[j].outerCorn);
            float angle = dot(spotDirection, L);
            float area = innerCorn - outerCorn;
            attenuation *= saturate(1.0f - (innerCorn - angle) / area);

            float3 diffuse = (float3) 0, specular = (float3) 0;
            DirectBRDF(diffuse_reflectance, F0, N, V, L,
					   spot_light[j].color.rgb * spot_light[j].color.a, roughness,
					   diffuse, specular);
            total_diffuse += diffuse * attenuation;
            total_specular += specular * attenuation;
        }
    }

	//	IBL処理
    total_diffuse += DiffuseIBL(N, V, roughness, diffuse_reflectance, F0, diffuse_iem, sampler_states[LINEAR]);
    total_specular += SpecularIBL(N, V, roughness, F0, lut_ggx, specular_pmrem, sampler_states[LINEAR]);

	//	遮蔽処理
    total_diffuse = lerp(total_diffuse, total_diffuse * occlusion_factor, occlusion_strength);
    total_specular = lerp(total_specular, total_specular * occlusion_factor, occlusion_strength);

	//	色生成
    float3 color = total_diffuse + total_specular + emisive_color;
    return float4(pow(color.rgb, 1.0f / GammaFactor), base_color.a);
}

