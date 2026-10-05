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
    float3 direction;       //  ライト方向
    float3 color;           //  ライトカラー
    float attenuation;      //  ライト減衰値
    float3 camera_position; //  視点
};

DirectLighting integrate_bxdf(in GBufferData gbuffer_data, in LightingData lighting_data)
{
    DirectLighting result = (DirectLighting)0;
    switch (gbuffer_data.shading_model)
    {
        case shading_model_unlit:
        {
            //  ライティングは行わないので何もしない
            break;
        }
        case shading_model_pbr:
        {
            float3 albedo = gbuffer_data.base_color;
            float3 normal = normalize(gbuffer_data.w_normal);
            float3 view_vector = normalize(gbuffer_data.w_position.xyz - lighting_data.camera_position.xyz);
            float3 diffuse_reflectance = lerp(albedo.rgb, 0.0f, gbuffer_data.metalness);
            float3 fresnel0 = lerp(0.04f, albedo.rgb, gbuffer_data.metalness);
            DirectBRDF(diffuse_reflectance, fresnel0, normal, view_vector, lighting_data.direction,
                        lighting_data.color, gbuffer_data.roughness, result.diffuse, result.specular);
            result.diffuse *= lighting_data.attenuation;
            result.specular *= lighting_data.attenuation;

	        //	自己遮蔽
            result.diffuse = lerp(result.diffuse, result.diffuse * gbuffer_data.occlusion_factor, gbuffer_data.occlusion_strength);
            result.specular = lerp(result.specular, result.specular * gbuffer_data.occlusion_factor, gbuffer_data.occlusion_strength);
            break;
        }
    }

    return result;
}

#endif // __SHADING_MODEL_HLSLI__
