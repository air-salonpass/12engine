#ifndef __GBUFFER_HLSLI__
#define __GBUFFER_HLSLI__

//  シェーディングモデル定数
static const int shading_model_unlit    = 0; //  シェーディング計算：ライティング無し
static const int shading_model_pbr      = 1; //  シェーディング計算：物理ベースを用いた計算

//  ピクセルシェーダーへの出力用構造体
struct PSGBufferOut
{
    float4 base_color : SV_TARGET0;
    float4 emissive_color : SV_TARGET1;
    float4 normal : SV_TARGET2;
    float4 parameter : SV_TARGET3;
    float depth : SV_TARGET4;
    float4 velocity : SV_TARGET5;
};

//  GBuffer情報構造体
struct GBufferData
{
    float3 base_color;          //  ベースカラー
    int shading_model;          //  シェーディング方式
    float3 emissive_color;      //  自己発光色
    float3 w_normal;            //  ワールド法線
    float3 w_position;          //  ワールド座標
    float2 velocity;            //  速度
    float linear_depth;         //  線形深度(Decode時のみ)
    float roughness;            //  粗さ
    float metalness;            //  金属質
    float occlusion_factor;     //  遮蔽値
    float occlusion_strength;   //  遮蔽強度
};

//  GBufferテクスチャ受け渡し用構造体
struct PSGBufferTextures
{
    Texture2D<float4> base_color;
    Texture2D<float4> emissive_color;
    Texture2D<float4> normal;
    Texture2D<float4> parameter;
    Texture2D<float> depth;
    Texture2D<float4> velocity;
    SamplerState state;
};

//  UV空間上の速度計算用関数
float2 calculate_uvspace_velocity(float4 current_clip_position, float4 previous_clip_position)
{
    current_clip_position /= current_clip_position.w;
    previous_clip_position /= previous_clip_position.w;
    current_clip_position.xy = current_clip_position.xy * float2(0.5f, -0.5f) + 0.5f;
    previous_clip_position.xy = previous_clip_position.xy * float2(0.5f, -0.5f) + 0.5f;
    return (current_clip_position.xy - previous_clip_position.xy);
}

//  非線形深度から線形深度へ変換
float convert_projection_depth_to_linear_depth(float depth, float4 z_buffer_parameteres)
{
    return 1.0f / (z_buffer_parameteres.x * depth + z_buffer_parameteres.y);
}

//  線形深度から非線形深度へ変換
float convert_linear_depth_to_projection_depth(float depth, float4 z_buffer_parameteres)
{
    return (1.0f / depth - z_buffer_parameteres.y) / z_buffer_parameteres.x;
}

//  GBufferDataに纏めた情報をピクセルシェーダーの出力用構造体に変換
PSGBufferOut EncodeGBuffer(in GBufferData data, matrix view_projection_matrix, float4 z_buffer_parameteres)
{
    PSGBufferOut ret = (PSGBufferOut) 0;
    ret.base_color.rgb = data.base_color;
    ret.base_color.a = data.shading_model / 32.0f;
    ret.emissive_color.rgb = data.emissive_color;
    ret.emissive_color.a = 1;
    ret.normal.rgb = data.w_normal;
    ret.normal.a = 1;
    ret.parameter.r = data.occlusion_factor;
    ret.parameter.g = data.roughness;
    ret.parameter.b = data.metalness;
    ret.parameter.a = data.occlusion_strength;
    float4 position = mul(float4(data.w_position, 1.0f), view_projection_matrix);
    ret.depth = convert_projection_depth_to_linear_depth(position.z / position.w, z_buffer_parameteres);
    
    ret.velocity.r = data.velocity.x;
    ret.velocity.g = data.velocity.y;
    ret.velocity.b = 1;
    ret.velocity.a = 1;
    return ret;
};

//  ピクセルシェーダーの出力用構造体からGBufferData情報に変換
GBufferData DecodeGBuffer(PSGBufferTextures textures, float2 uv, matrix inverse_view_projection_transform, float4 z_buffer_parameteres)
{
    //  各テクスチャから情報を取得
    float4 base_color = textures.base_color.Sample(textures.state, uv);
    float4 emissive_color = textures.emissive_color.Sample(textures.state, uv);
    float4 normal = textures.normal.Sample(textures.state, uv);
    float4 parameter = textures.parameter.Sample(textures.state, uv);
    float linear_depth = textures.depth.Sample(textures.state, uv);
    float4 velocity = textures.velocity.Sample(textures.state, uv);

    GBufferData ret = (GBufferData) 0;
    ret.base_color = base_color.rgb;
    ret.shading_model = (int) (base_color.a * 32.0f + 0.5f);
    ret.emissive_color = emissive_color.rgb;
    ret.w_normal = normal.rgb;
    ret.occlusion_factor = parameter.r;
    ret.roughness = parameter.g;
    ret.metalness = parameter.b;
    ret.occlusion_strength = parameter.a;
    ret.linear_depth = linear_depth;
    ret.velocity = velocity.xy;
    float4 position = float4(uv.xy * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), convert_linear_depth_to_projection_depth(linear_depth, z_buffer_parameteres), 1);
    position = mul(position, inverse_view_projection_transform);
    ret.w_position = position.xyz / position.w;
    return ret;
}

#endif  //  __GBUFFER_HLSLI__
