#include "skinned_mesh.hlsli"

VS_OUT main(VS_IN vin)
{
    VS_OUT vout;

	// 最大4本のボーン変形をウェイト付きで合成し、位置・法線・接線へ反映する。
	vin.normal.w = 0;
	float tangent_sigma = vin.tangent.w;
	vin.tangent.w = 0;
	float4 blended_position = 0;
	float4 blended_normal = 0;
	float4 blended_tangent = 0;
	for (int influence_index = 0;
		influence_index < 4;
		++influence_index)
	{
		const uint bone_index = vin.bone_indices[influence_index];
		const float bone_weight = vin.bone_weights[influence_index];
		blended_position += bone_weight *
			mul(vin.position, bone_transforms[bone_index]);
		blended_normal += bone_weight *
			mul(vin.normal, bone_transforms[bone_index]);
		blended_tangent += bone_weight *
			mul(vin.tangent, bone_transforms[bone_index]);
	}
	vin.position = float4(blended_position.xyz, 1.0f);
	vin.normal = float4(blended_normal.xyz, 0.0f);
	vin.tangent = float4(blended_tangent.xyz, 0.0f);

    // ローカル座標をクリップ座標とワールド座標へ変換する。
    vout.position = mul(vin.position, mul(world, view_projection));
    vout.world_position = mul(vin.position, world);
    // 法線は方向ベクトルなのでwを0にして平行移動の影響を除く。
    vout.world_normal = normalize(mul(vin.normal, world));
	// 接線もワールド空間へ移し、wには従法線を復元する符号を残す。
	vout.world_tangent = normalize(mul(vin.tangent, world));
	vout.world_tangent.w = tangent_sigma;
    vout.texcoord = vin.texcoord;

	// 通常描画では材質色を使用する。
	// ボーン影響度を確認するときだけ#if 0を#if 1へ変更し、影響ボーンごとの色を合成する。
#if 0
	vout.color = 0;
	const float4 bone_colors[4] =
	{
		{ 1, 0, 0, 1 },
		{ 0, 1, 0, 1 },
		{ 0, 0, 1, 1 },
		{ 1, 1, 1, 1 }
	};
	for (int influence_index = 0; influence_index < 4; ++influence_index)
	{
		vout.color +=
			bone_colors[vin.bone_indices[influence_index] % 4] *
			vin.bone_weights[influence_index];
	}
#else
    vout.color = material_color;
#endif
    return vout;
}
