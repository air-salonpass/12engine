#include "gltf_model_forward.hlsli"

VS_OUT main(VS_IN vin)
{
	float sigma = vin.tangent.w;

	VS_OUT vout;

	vin.position.w = 1;
    vout.position = mul(vin.position, mul(world, view_projection_transform));
	vout.w_position = mul(vin.position, world);

	vin.normal.w = 0;
	vout.w_normal = normalize(mul(vin.normal, world));

	vin.tangent.w = 0;
	vout.w_tangent = normalize(mul(vin.tangent, world));
	vout.w_tangent.w = sigma;

	vout.texcoord = vin.texcoord;

	// シャドウマップ用のパラメーター計算
	{
		// ライトから見たNDC座標を算出
		float4 wvpPos = mul(vin.position, mul(world, light_view_projection));
		// NDC座標からUV座標を算出する
		wvpPos /= wvpPos.w;
		wvpPos.y = -wvpPos.y;
		wvpPos.xy = 0.5f * wvpPos.xy + 0.5f;
		vout.shadow_texcoord = wvpPos.xyz;
    }
	
	return vout;
}
