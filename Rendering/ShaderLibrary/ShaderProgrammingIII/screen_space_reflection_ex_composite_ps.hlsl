#include "deferred_rendering.hlsli"

#include "screen_space_reflection_ex.hlsli"

Texture2D<float4> gbuffer_base_color : register(t0); //< ここがシーンバッファに書き換わるので注意
Texture2D<float4> gbuffer_emissive_color : register(t1);
Texture2D<float4> gbuffer_normal : register(t2);
Texture2D<float4> gbuffer_parameter : register(t3);
Texture2D<float> gbuffer_depth : register(t4);
Texture2D<float4> gbuffer_velocity : register(t5);

Texture2D<float4> screen_space_reflection_map : register(t10);

#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2
#define LINEAR_BORDER_BLACK 3
#define LINEAR_BORDER_WHITE 4
SamplerState sampler_states[5] : register(s0);

float4 main(VS_OUT pin) : SV_TARGET
{
    //  GBufferテクスチャから情報をデコード
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

	//  反射映像取得
    float4 reflection_color = screen_space_reflection_map.Sample(sampler_states[LINEAR], pin.texcoord);
    if (reflection_color.a >= 0.01f && data.roughness >= 0.01f && is_flag(screen_space_reflection_flags_consideration_rougness))
    {
        //  テクスチャサイズ取得
        float2 texture_size;
        screen_space_reflection_map.GetDimensions(texture_size.x, texture_size.y);

        //  粗さを考慮して簡易的にブラーをかけた反射映像を作成
        static const int Size = 6;
        static const float separation = 4.0f;

        float4 accum_color = (float4) 0;
        float count = 0.0f;
        [unroll] for (int i = -Size; i <= Size; ++i)
        {
            [unroll] for (int j = -Size; j <= Size; ++j)
            {
                float2 delta = (float2(i, j) * separation) / texture_size;
                float4 sampling_color = screen_space_reflection_map.Sample(sampler_states[LINEAR], pin.texcoord.xy + delta);
                accum_color += sampling_color;
                count += 1.0f;
            }
        }

        //  粗さを考慮
        reflection_color = lerp(reflection_color, accum_color / count, data.roughness);
    }

    //  反射映像を適応
    float3 color = lerp(data.base_color.rgb, reflection_color.rgb, reflection_color.a);
    return float4(pow(color.rgb, 1.0f / GammaFactor), 1);
}
