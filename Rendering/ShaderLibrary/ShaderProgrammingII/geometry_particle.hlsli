// Unit06-02: ポイントをカメラ正対クアッドへ展開するGPUパーティクルシェーダー。
//  頂点情報
struct VS_IN
{
    float3 position : POSITION;
    float2 size : TEXCOORD;
    float4 color : COLOR;
    float4 param : PARAMETER;
};

struct GS_IN
{
    float3 position : POSITION;
    float2 size : TEXCOORD;
    float4 color : COLOR;
    float4 param : PARAMETER;
};

struct PS_IN
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD;
};

cbuffer GEOMETRY_PARTICLE_DATA : register(b0)
{
    float2 Size; //  パーティクルの大きさ
    float2 dummy;
};

#include "scene_constant_buffer.hlsli"


