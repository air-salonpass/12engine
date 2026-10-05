#include "static_mesh.hlsli"

VS_OUT main(float4 position : POSITION, float4 normal : NORMAL, float2 texcoord : TEXCOORD)
{
	VS_OUT vout;
    vout.position = mul(position, mul(world, view_projection_transform));

	vout.world_position = mul(position, world);
	normal.w = 0;
	vout.world_normal = normalize(mul(normal, world));

    vout.color = float4(1, 1, 1, 1);
	vout.texcoord = texcoord;

	return vout;
}


