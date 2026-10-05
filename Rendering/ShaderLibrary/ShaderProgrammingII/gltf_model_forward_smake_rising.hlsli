#include "gltf_model_forward.hlsli"

struct VS_OUT_SR
{
    float4 position : SV_POSITION;
    float4 w_position : POSITION;
    float4 w_normal : NORMAL;
    float4 w_tangent : TANGENT;
    float2 texcoord : TEXCOORD;
    uint vertex_id : VERTEXID;
};

struct GS_OUT
{
    float4 position : SV_POSITION;
    float3 texcoord : TEXCOORD;
};

//  生成するポリゴン数
//  無限に増やす事はできず、生成する頂点の数×頂点のサイズが1024を超えてはいけないという制限があります。
//  なので現状は下記のような状態となります。
//  (24 * 3) * (7) = 504 
//  注意事項
//  ギリギリのサイズは48なのですが、なぜそれをしていないのか。
//  今回煙の処理を半透明を重ねて表現していますが、その関係で半透明による大量の上書きが発生します。
//  要はポリゴン数次第で描画負荷が高くなるので、もし使いたい場合は個数を減らしてください。
//  現実的に使えるサイズは12ぐらいが限度だと思います(もっと少ない方が良い)。
#define FACE_DENSITY 24

cbuffer SMOKE_RISIN_CONSTANT_BUFFER : register(b4)
{
	float	smoke_vertical_length; //  煙の立ち上るサイズ
	float	smoke_horizontal_range;
	float	smoke_step;	//	閾値
	float	smoke_alpha; //  煙の透過度

    float   smoke_tiling;
	float	smoke_vertical_speed;
    float2  dummy;
};
