#include "sky_box.hlsli"

#include "gbuffer.hlsli"

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

PSGBufferOut main(VS_OUT pin)
{
    //	視線ベクトル
    float3 E = normalize(pin.world_position.xyz - camera_position.xyz);

    //  スカイボックスから色を取得する
    float3 base_color = SampleSkybox(texture0, sampler0, E);

    //  環境光の色を適応しておく
    float3 ambient = ambient_color.rgb * ambient_color.a;

    //  GBufferDataに出力情報をまとめる
    GBufferData data = (GBufferData) 0;
    data.shading_model = shading_model_unlit;   //  空はライティング市内
    data.emissive_color = ambient * base_color.rgb * pin.color.rgb; //  空は自己発光扱い
    data.w_position = pin.world_position.xyz; //  一応ワールド座標を入れておく
    data.w_normal = -E; //  一応法線は視線方向としておく
    
    return EncodeGBuffer(data, view_projection_transform);
}
