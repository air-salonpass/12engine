#include "FullscreenQuad.hlsli"

VS_OUT main(uint vin : SV_VERTEXID) {
	VS_OUT vout;
	// 割り算の割り切れた数と余りの数で4隅のUV座標を求める
	vout.texcoord.x = vin % 2;
	vout.texcoord.y = vin / 2;
	// 求めたUV座標からNDC座標を求める
	vout.position.xy = vout.texcoord * float2(2, -2)
					 - float2(1, -1);
	vout.position.z = 0;
	vout.position.w = 1;
	return vout;
}
