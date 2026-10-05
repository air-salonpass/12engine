#include "pmx_model.hlsli"

#define DIFFUSE 0
#define SPHERE 1
#define RAMP 2
Texture2D<float4> textures[3] : register(t0);

SamplerState samplerstate : register(s0);

float3 CalcMMDRampShading(float3 N, float3 L, float3 C)
{
    if (texture_index.z <= -1)
        return C;
    
    float D = saturate(dot(N, -L) * 0.5f + 0.5f);
    float3 Ramp = textures[RAMP].Sample(samplerstate, float2(0.5f, D)).rgb;
    return C * Ramp;
}

float4 main(VS_OUT pin, bool is_front_face : SV_IsFrontFace) : SV_TARGET
{
    float4 base_color = diffuse_color;
    if (texture_index.x > -1)
    {
        float4 sampled = textures[DIFFUSE].Sample(samplerstate, pin.texcoord);
        sampled.rgb = pow(sampled.rgb, GammaFactor);
        base_color *= sampled;
    }
    clip(base_color.a - 0.01f);

    float3 V = normalize(pin.w_position.xyz - camera_position.xyz);
    float3 N = normalize(pin.w_normal.xyz);

	//	直接光のシェーディング
    float3 total_diffuse = 0, total_specular = 0;
	{
		// 平行光源の処理
		{
            float3 L = normalize(directional_light.direction.xyz);
            total_diffuse += CalcMMDRampShading(N, L, directional_light.color.rgb);
            total_specular += CalcPhongSpecular(N, L, V, directional_light.color.rgb, 1, max(1, specular_color.a));
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
            total_diffuse += CalcMMDRampShading(N, L, point_light[i].color.rgb) * attenuation;
            total_specular += CalcPhongSpecular(N, L, V, point_light[i].color.rgb, 1, max(1, specular_color.a)) * attenuation;
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

            total_diffuse += CalcMMDRampShading(N, L, spot_light[j].color.rgb) * attenuation;
            total_specular += CalcPhongSpecular(N, L, V, spot_light[j].color.rgb, 1, max(1, specular_color.a)) * attenuation;
        }
    }

	//	色生成
    float3 color = base_color.rgb * total_diffuse + total_specular * specular_color.rgb;
    return float4(pow(color.rgb, 1.0f / GammaFactor), base_color.a);
}

