#include "sprite.hlsli"

VS_OUT main(VS_IN vin)
{
	VS_OUT output;
	// sprite側ですでにNDCへ変換されているため、行列変換せずそのまま渡す。
	output.position = float4(vin.position, 1.0);
	output.color = vin.color;
	output.texcoord = vin.texcoord;
	return output;
}
