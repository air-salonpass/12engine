struct VS_OUT
{
    float4 position : SV_POSITION;
	// SSR用床を含む基本形状の面方向を、ピクセル単位の照明へ渡す。
	float4 world_normal : NORMAL;
    float4 color : COLOR;
};

cbuffer OBJECT_CONSTANT_BUFFER : register(b0)
{
    row_major float4x4 world;
    float4 material_color;
};

cbuffer SCENE_CONSTANT_BUFFER : register(b1)
{
    row_major float4x4 view_projection;
    float4 light_direction;
};
