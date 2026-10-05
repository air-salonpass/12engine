#include "pmx_model.hlsli"

VS_OUT main(VS_IN vin)
{
    VS_OUT vout;
    float3 position = 0, normal = 0;
    for (int index = 0; index < 4; ++index)
    {
        position += mul(float4(vin.position, 1), bone_transforms[vin.joints[index]]).xyz * vin.weights[index];
        normal += mul(float4(vin.normal, 0), bone_transforms[vin.joints[index]]).xyz * vin.weights[index];
    }
    vout.w_position = mul(float4(position, 1), world).xyz;
    vout.position = mul(float4(vout.w_position, 1), view_projection_transform);
    vout.w_normal = normalize(mul(float4(normal, 0), world)).xyz;
    vout.texcoord = vin.texcoord;

	// シャドウマップ用のパラメーター計算
    #if 0
	{
		// ライトから見たNDC座標を算出
        float4 wvpPos = mul(vin.position, mul(world, light_view_projection));
		// NDC座標からUV座標を算出する
        wvpPos /= wvpPos.w;
        wvpPos.y = -wvpPos.y;
        wvpPos.xy = 0.5f * wvpPos.xy + 0.5f;
        vout.shadow_texcoord = wvpPos.xyz;
    }
    #endif
	
    return vout;
}
