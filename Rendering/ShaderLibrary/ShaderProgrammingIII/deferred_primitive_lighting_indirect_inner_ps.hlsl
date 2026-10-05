#include "deferred_rendering.hlsli"

Texture2D<float4> gbuffer_base_color : register(t0);
Texture2D<float4> gbuffer_emissive_color : register(t1);
Texture2D<float4> gbuffer_normal : register(t2);
Texture2D<float4> gbuffer_parameter : register(t3);
Texture2D<float> gbuffer_depth : register(t4);
Texture2D<float4> gbuffer_velocity : register(t5);

#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2
SamplerState sampler_states[3] : register(s0);

//  IBL用テクスチャ
TextureCube diffuse_iem : register(t33);
TextureCube specular_pmrem : register(t34);
Texture2D lut_ggx : register(t35);

//  プローブ情報
cbuffer PROBE_CONSTANT_BUFFER : register(b2)
{
    float4 probe_position;
    float3 probe_area;
    float probe_blend_rate;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    //  GBufferテクスチャから情報をデコードする
    PSGBufferTextures gbuffer_textures;
    gbuffer_textures.base_color = gbuffer_base_color;
    gbuffer_textures.emissive_color = gbuffer_emissive_color;
    gbuffer_textures.normal = gbuffer_normal;
    gbuffer_textures.parameter = gbuffer_parameter;
    gbuffer_textures.depth = gbuffer_depth;
    gbuffer_textures.velocity = gbuffer_velocity;
    gbuffer_textures.state = sampler_states[POINT];
    GBufferData data;
    data = DecodeGBuffer(gbuffer_textures, pin.texcoord, inverse_view_projection_transform, z_buffer_parameteres);

    //  範囲判定
    clip(all(probe_position.xyz - probe_area <= data.w_position && data.w_position <= probe_position.xyz + probe_area) - 1);

    float3 total_diffuse = (float3) 0, total_specular = (float3) 0;
    if (data.shading_model != shading_model_unlit)
    {
        float3 albedo = data.base_color;
        float3 emissive_color = data.emissive_color;
        float3 N = normalize(data.w_normal);
        float3 V = normalize(data.w_position.xyz - camera_position.xyz);

	    //	入射光のうち拡散反射になる割合
        float3 diffuse_reflectance = lerp(albedo.rgb, 0.0f, data.metalness);

	    //	垂直反射時のフレネル反射率(非金属でも最低4%は鏡面反射する)
        float3 F0 = lerp(0.04f, albedo.rgb, data.metalness);

	    //	IBL処理
        total_diffuse += DiffuseIBL(N, V, data.roughness, diffuse_reflectance, F0, diffuse_iem, sampler_states[LINEAR]);
        total_specular += SpecularIBL(N, V, data.roughness, F0, lut_ggx, specular_pmrem, sampler_states[LINEAR]);

	    //	自己遮蔽
        total_diffuse = lerp(total_diffuse, total_diffuse * data.occlusion_factor, data.occlusion_strength);
        total_specular = lerp(total_specular, total_specular * data.occlusion_factor, data.occlusion_strength);
    }

    return float4(total_diffuse + total_specular, saturate(probe_blend_rate));
}
