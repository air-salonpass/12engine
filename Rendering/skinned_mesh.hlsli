struct VS_IN
{
    // 頂点バッファから受け取る1頂点分の情報。
    float4 position : POSITION;
    float4 normal : NORMAL;
	// xyzは接線、wは従法線の向きを決める符号。
	float4 tangent : TANGENT;
    float2 texcoord : TEXCOORD;
	// 最大4本分のボーン影響度と、対応するボーン番号を受け取る。
	float4 bone_weights : WEIGHTS;
	uint4 bone_indices : BONES;
};

struct VS_OUT
{
    // 頂点シェーダーからピクセルシェーダーへ渡す情報。
    float4 position : SV_POSITION;
    float4 world_position : POSITION;
    float4 world_normal : NORMAL;
	float4 world_tangent : TANGENT;
    float2 texcoord : TEXCOORD;
    float4 color : COLOR;
};

static const int MAX_BONES = 256;

cbuffer OBJECT_CONSTANT_BUFFER : register(b0)
{
	// オブジェクトごとに変化するワールド行列・材質色・ボーン行列。
    row_major float4x4 world;
    float4 material_color;
	row_major float4x4 bone_transforms[MAX_BONES];
};

cbuffer SCENE_CONSTANT_BUFFER : register(b1)
{
    // シーン全体で共有するカメラ・ライト情報。
    row_major float4x4 view_projection;
    float4 light_direction;
    float4 camera_position;
};
