#include	"lights.hlsli"
#include    "common.hlsli"

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
	float4 w_position : POSITION;
	float4 w_normal : NORMAL;
	float4 w_tangent : TANGENT;
    float2 texcoord : TEXCOORD;
    float3 shadow_texcoord : TEXCOORD1;
    float4 current_clip_position : CLIP_POSITION0;
    float4 previous_clip_position : CLIP_POSITION1;
};

cbuffer PRIMITIVE_CONSTANT_BUFFER : register(b0)
{
    row_major float4x4 world;
    row_major float4x4 previous_world;
	int material;
    int has_tangent;
	int skin;
	int pad;
};

#include "shading_functions.hlsli"
