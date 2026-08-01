#include "Scene.hlsli"

//	頂点シェーダーからピクセルシェーダーへ渡すもの
struct VS_OUT
{
	float4 vertex	: SV_POSITION;
	float2 texcoord	: TEXCOORD;
	float3 normal	: NORMAL;
	float3 position : POSITION;
	float4 tangent	: TANGENT;	//	<ここをfloat4に変更
};
