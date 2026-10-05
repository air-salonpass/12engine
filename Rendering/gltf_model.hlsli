// UNIT34: glTFは属性ごとに独立した頂点バッファを持つSoA形式で入力する。
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
	float4 world_position : POSITION;
	float4 world_normal : NORMAL;
	float4 world_tangent : TANGENT;
	float2 texcoord : TEXCOORD;
};

// ノードごと・Primitiveごとに変わる描画情報をb0で受け取る。
cbuffer PRIMITIVE_CONSTANT_BUFFER : register(b0)
{
	row_major float4x4 world;
	int material;
	int has_tangent;
	int skin;
	int primitive_pad;
};

// frameworkが全3Dモデルへ共通設定しているカメラ・ライト情報。
cbuffer SCENE_CONSTANT_BUFFER : register(b1)
{
	row_major float4x4 view_projection;
	float4 light_direction;
	float4 camera_position;
	// UNIT40: HDR由来の拡散・鏡面環境光へ共通で掛ける強度。
	float ibl_intensity;
	// 通常モデルは1.0、床へ重ねる鏡像は0～1の透過率を受け取る。
	float reflection_alpha;
	float2 scene_padding;
};

// UNIT37: CPUで計算した最大512関節のスキニング行列を頂点シェーダーへ渡す。
static const uint PRIMITIVE_MAX_JOINTS = 512;
cbuffer PRIMITIVE_JOINT_CONSTANTS : register(b2)
{
	row_major float4x4 joint_matrices[PRIMITIVE_MAX_JOINTS];
};
