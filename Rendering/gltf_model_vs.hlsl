#include "gltf_model.hlsli"

VS_OUT main(VS_IN input)
{
	const float tangent_sign = input.tangent.w;

	// UNIT37: 各頂点が持つ4関節の番号とウェイトから、頂点用スキニング行列を合成する。
	if (skin > -1)
	{
		row_major float4x4 skin_matrix =
			input.weights.x * joint_matrices[input.joints.x] +
			input.weights.y * joint_matrices[input.joints.y] +
			input.weights.z * joint_matrices[input.joints.z] +
			input.weights.w * joint_matrices[input.joints.w];

		input.position = mul(float4(input.position.xyz, 1.0), skin_matrix);
		input.normal = normalize(mul(float4(input.normal.xyz, 0.0), skin_matrix));
		input.tangent = normalize(mul(float4(input.tangent.xyz, 0.0), skin_matrix));
	}

	// ローカル頂点をノード階層込みのワールド座標、続いてクリップ座標へ変換する。
	input.position.w = 1.0;
	VS_OUT output;
	output.world_position = mul(input.position, world);
	output.position = mul(output.world_position, view_projection);

	// スキニング後の法線と接線もworld行列で方向変換する。
	input.normal.w = 0.0;
	output.world_normal = normalize(mul(input.normal, world));

	input.tangent.w = 0.0;
	output.world_tangent = normalize(mul(input.tangent, world));
	output.world_tangent.w = tangent_sign;
	output.texcoord = input.texcoord;
	return output;
}
