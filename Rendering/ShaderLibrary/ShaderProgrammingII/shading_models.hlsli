#ifndef __SHADING_MODEL_HLSLI__
#define __SHADING_MODEL_HLSLI__


#include "gbuffer.hlsli"
#include "shading_functions.hlsli"

struct DirectLighting
{
    float3 diffuse;
    float3 specular;
};

struct LightingData
{
    float3 direction; //  ライト方向
    float attenuation; //  減衰値
    float3 color; //  ライトカラー
    float3 camera_position; //  視点
    float dummy;
};

DirectLighting integrate_bxdf(in GBufferData gbuffer_data, in LightingData lighting_data)
{
    DirectLighting result = (DirectLighting) 0;
    switch (gbuffer_data.shading_model)
    {
        default:
        {
            //  ライティングしないので計算は省く
            break;
        }
        case shading_model_phong_shading:
        {
            float3 view_vector = normalize(gbuffer_data.w_position.xyz - lighting_data.camera_position.xyz);

            result.diffuse = gbuffer_data.base_color * CalcLambert(gbuffer_data.w_normal, lighting_data.direction, lighting_data.color, 1) * lighting_data.attenuation;
            result.specular = CalcPhongSpecular(gbuffer_data.w_normal, lighting_data.direction, view_vector, lighting_data.color, 1) * lighting_data.attenuation;

            break;
        }
    }

    return result;
}

#endif // __SHADING_MODEL_HLSLI__
