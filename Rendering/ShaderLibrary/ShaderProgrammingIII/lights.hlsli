#ifndef __LIGHTS_HLSLI__
#define __LIGHTS_HLSLI__
// 平行光源
struct directional_lights
{
    float4 direction;
    float4 color;
};

// 点光源
struct point_lights
{
	float4	position;
	float4	color;
	float	range;
	float3	dummy;
};

// スポットライト
struct spot_lights
{
	float4	position;
	float4	direction;
	float4	color;
	float	range;
	float	innerCorn;
	float	outerCorn;
	float	dummy;
};

// 半球ライト
struct hemisphere_lights
{
    float4 sky_color;
    float4 ground_color;
    float weight;
    float3 dummy;
};

#endif	//	__LIGHTS_HLSLI__
