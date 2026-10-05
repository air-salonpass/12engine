#include "gltf_model_forward.hlsli"

VS_OUT main(INSTANCING_VS_IN vin)
{
    float sigma = vin.tangent.w;

    VS_OUT vout;

    float4x4 world_matrix;
    world_matrix._11_12_13_14 = vin.world0;
    world_matrix._21_22_23_24 = vin.world1;
    world_matrix._31_32_33_34 = vin.world2;
    world_matrix._41_42_43_44 = vin.world3;
    world_matrix = mul(world, world_matrix);

    vin.position.w = 1;
    vout.position = mul(vin.position, mul(world_matrix, view_projection_transform));
    vout.w_position = mul(vin.position, world_matrix);

    vin.normal.w = 0;
    vout.w_normal = normalize(mul(vin.normal, world_matrix));

    vin.tangent.w = 0;
    vout.w_tangent = normalize(mul(vin.tangent, world_matrix));
    vout.w_tangent.w = sigma;

    vout.texcoord = vin.texcoord;

	// シャドウマップ用のパラメーター計算
	{
		// ライトから見たNDC座標を算出
        float4 wvpPos = mul(vin.position, mul(world_matrix, light_view_projection));
		// NDC座標からUV座標を算出する
        wvpPos /= wvpPos.w;
        wvpPos.y = -wvpPos.y;
        wvpPos.xy = 0.5f * wvpPos.xy + 0.5f;
        vout.shadow_texcoord = wvpPos.xyz;
    }
	
    return vout;
}
