#include	"lights.hlsli"

#include	"scene_constant_buffer.hlsli"

struct VS_IN
{
	float4 position : POSITION;
	float4 normal : NORMAL;
	float4 tangent : TANGENT;
	float2 texcoord : TEXCOORD;
	uint4 joints : JOINTS;
	float4 weights : WEIGHTS;
};

struct VS_OUT
{
	float4 position : SV_POSITION;
};

cbuffer PRIMITIVE_CONSTANT_BUFFER : register(b0)
{
	row_major float4x4 world;
	int material;
	int has_tangent;
	int skin;
	int pad;
};
