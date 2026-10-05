#include "deferred_rendering.hlsli"

#include "filter_functions.hlsli"

#include "screen_space_reflection_ex.hlsli"

Texture2D<float4> gbuffer_base_color : register(t0);
Texture2D<float4> gbuffer_emissive_color : register(t1);
Texture2D<float4> gbuffer_normal : register(t2);
Texture2D<float4> gbuffer_parameter : register(t3);
Texture2D<float> gbuffer_depth : register(t4);
Texture2D<float4> gbuffer_velocity : register(t5);

Texture2D<float4> screen_space_reflection_uv_map : register(t10);
Texture2D<float4> scene_color_map : register(t11);

#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2
#define LINEAR_BORDER_BLACK 3
#define LINEAR_BORDER_WHITE 4
SamplerState sampler_states[5] : register(s0);

SamplerState screen_space_reflection_sampler : register(s10);

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

    //  シェーディングモデルがunlitなら処理しない
    if (data.shading_model == shading_model_unlit)
        return (float4) 0;

    //  反射UVマップを取得
    float4 uv = screen_space_reflection_uv_map.Sample(screen_space_reflection_sampler, pin.texcoord);

    //  透過している所は周辺情報を調べて色を補正
    if (uv.a <= 0.01f && is_flag(screen_space_reflection_flags_consideration_uv_hole))
    {
        //  テクスチャサイズ取得
        float2 texture_size;
        scene_color_map.GetDimensions(texture_size.x, texture_size.y);

        //  周辺情報を取得して確認
        int size = 2;
        float separation = 1.0f;
        float4 accum_uv = (float4) 0;
        float count = 0.0f;
        [unroll] for (int i = -size; i <= size; ++i)
        {
            [unroll] for (int j = -size; j <= size; ++j)
            {
                if (j == 0 && j == 0)
                    continue;
                float2 delta = (float2(i, j) * separation) / texture_size;
                accum_uv += screen_space_reflection_uv_map.Sample(screen_space_reflection_sampler, pin.texcoord.xy + delta);
                count += 1.0f;
            }
        }
        uv = accum_uv / count;
    }

    float4 color = scene_color_map.Sample(sampler_states[LINEAR_BORDER_BLACK], uv.xy);
    color.a *= saturate(uv.w);

    return color;
}
