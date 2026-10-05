#include "geometric_primitive.hlsli"

VS_OUT main(
    float4 position : POSITION,
    float4 normal : NORMAL)
{
    VS_OUT vout;

	vout.position =
		mul(position,
		mul(world, view_projection));
	normal.w = 0.0f;
	vout.world_normal = normalize(mul(normal, world));

	// ライティングはピクセルシェーダーで一度だけ行い、床の上向き面も環境光で読める明るさを残す。
	vout.color = material_color;

    return vout;
}
