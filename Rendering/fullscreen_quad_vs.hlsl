#include "fullscreen_quad.hlsli"

VS_OUT main(in uint vertex_id : SV_VERTEXID)
{
	VS_OUT output;
	const float2 positions[4] =
	{
		float2(-1, +1), float2(+1, +1),
		float2(-1, -1), float2(+1, -1)
	};
	const float2 texcoords[4] =
	{
		float2(0, 0), float2(1, 0),
		float2(0, 1), float2(1, 1)
	};

	output.position = float4(positions[vertex_id], 0, 1);
	output.texcoord = texcoords[vertex_id];
	return output;
}
