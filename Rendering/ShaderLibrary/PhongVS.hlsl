#include "Skinning.hlsli"
#include "Phong.hlsli"

VS_OUT main(
	float4 position		: POSITION, float4 boneWeights	: BONE_WEIGHTS, uint4  boneIndices	: BONE_INDICES, float2 texcoord		: TEXCOORD,
	float4 tangent		: TANGENT,	//< ‚±‚ê‚ð’Ç‰Á
	float3 normal		: NORMAL) {
	VS_OUT vout = (VS_OUT)0;
	position = SkinningPosition(position, boneWeights, boneIndices);
	vout.vertex = mul(position, viewProjection);
	vout.texcoord = texcoord;
    vout.normal = SkinningVector(normal, boneWeights, boneIndices);
    vout.tangent.xyz = SkinningVector(tangent.xyz, boneWeights, boneIndices);
    vout.tangent.w = tangent.w;
	
    vout.position = position;
	return vout;
}
