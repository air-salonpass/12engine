#ifndef __GBUFFER_HLSLI__
#define __GBUFFER_HLSLI__

//  シェーディング方式を決めるためのId
static const int shading_model_unlit = 0; //  ライティング無し
static const int shading_model_phong_shading = 1; //  フォンシェーディング

static const int shading_model_max = 2; //  最大数

//  ピクセルシェーダーへの出力用構造体
struct PSGBufferOut
{
    float4 base_color : SV_TARGET0;
    float4 emissive_color : SV_TARGET1;
    float4 normal_depth : SV_TARGET2; 
};

//  GBuffer情報構造体
struct GBufferData
{
    float3 base_color; //  ベースカラー
    float3 emissive_color; //  自己発光色
    float3 w_normal; //  ワールド法線
    float3 w_position; //  ワールド座標
    float depth; //  深度(Decode時のみ)
    int shading_model; //  シェーディング方法を決めるId
};

//  GBufferDataに纏めた情報をピクセルシェーダーの出力用構造体に変換
PSGBufferOut EncodeGBuffer(in GBufferData data, matrix view_projection_matrix)
{
    PSGBufferOut ret = (PSGBufferOut) 0;
    ret.base_color.rgb = data.base_color;
    ret.base_color.a = 1.0f;

    ret.emissive_color.rgb = data.emissive_color;
    ret.emissive_color.a = 1.0f;

    ret.normal_depth.rgb = data.w_normal;
    float4 position = mul(float4(data.w_position, 1.0f), view_projection_matrix);
    ret.normal_depth.a = position.z / position.w;

    ret.base_color.a = ((float) data.shading_model) / shading_model_max;
    return ret;
};

// Unit04: Resources and inverse transform used to reconstruct world position.
struct PSGBufferTextures
{
    SamplerState state;
    Texture2D<float4> base_color;
    Texture2D<float4> emissive_color;
    Texture2D<float4> normal_depth;
};

GBufferData DecodeGBuffer(PSGBufferTextures textures, float2 uv, matrix inverse_view_projection_transform)
{
    float4 base = textures.base_color.SampleLevel(textures.state, uv, 0);
    float4 emissive = textures.emissive_color.SampleLevel(textures.state, uv, 0);
    float4 normal_depth = textures.normal_depth.SampleLevel(textures.state, uv, 0);
    GBufferData data = (GBufferData)0;
    data.base_color = base.rgb;
    data.emissive_color = emissive.rgb;
    data.w_normal = normalize(normal_depth.rgb);
    data.depth = normal_depth.a;
    float4 position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), data.depth, 1.0f);
    position = mul(position, inverse_view_projection_transform);
    data.w_position = position.xyz / position.w;
    data.shading_model = (int)(base.a * shading_model_max + 0.5f);
    return data;
}

#endif  //  __GBUFFER_HLSLI__
