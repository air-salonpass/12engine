#include "gltf_model.hlsli"

VS_OUT main(VS_IN vin)
{
    VS_OUT vout = (VS_OUT)0;

	vin.position.w = 1;
    vout.position = mul(vin.position, mul(world, view_projection_transform));
	return vout;
}
