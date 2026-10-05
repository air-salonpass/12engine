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

//  シャドウマップ用テクスチャ
Texture2D shadow_map : register(t10);
SamplerState shadow_sampler_state : register(s10);

//  平行光源情報
cbuffer LIGHT_CONSTANT_BUFFER : register(b2)
{
    directional_lights directional_light;
    int use_shadow; //	影を使用しているかどうか
    float shadow_attenuation; //	影色
    float shadow_bias; //	深度バイアス
    float shadow_dummy;
    row_major float4x4 light_view_projection; //	ライトの位置から見た射影行列
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

    float3 total_diffuse = (float3) 0, total_specular = (float3) 0;
    if (data.shading_model != shading_model_unlit)
    {
        float attenuation = 1;
        if (use_shadow)
        {
	        //  シャドウマップ用のパラメーター計算
            float3 shadow_texcoord;
	        {
		        // ライトから見たNDC座標を算出
                float4 wvpPos = mul(float4(data.w_position.xyz, 1.0f), light_view_projection);
		        // NDC座標からUV座標を算出する
                wvpPos /= wvpPos.w;
                wvpPos.y = -wvpPos.y;
                wvpPos.xy = 0.5f * wvpPos.xy + 0.5f;
                shadow_texcoord = wvpPos.xyz;
            }
		    //	平行光源用シャドウマップ
            float depth = shadow_map.Sample(shadow_sampler_state, shadow_texcoord.xy).r;
		    //	深度値を比較して影かどうかを判定する
            if (shadow_texcoord.z - depth > shadow_bias)
            {
                attenuation = shadow_attenuation;
            }
        }

        LightingData lighting_data;
        lighting_data.attenuation = attenuation;
        lighting_data.direction = normalize(directional_light.direction.xyz);
        lighting_data.color = directional_light.color.rgb * directional_light.color.a;
        lighting_data.camera_position = camera_position.xyz;
        DirectLighting result = integrate_bxdf(data, lighting_data);
        total_diffuse += result.diffuse;
        total_specular += result.specular;
    }
    return float4(total_diffuse + total_specular, 1);
}
