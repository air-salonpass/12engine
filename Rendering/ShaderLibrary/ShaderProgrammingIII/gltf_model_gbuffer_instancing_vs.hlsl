#include "gltf_model.hlsli"

VS_OUT main(INSTANCING_VS_IN vin)
{
    float sigma = vin.tangent.w;

    VS_OUT vout = (VS_OUT)0;

    float4x4 world_matrix, previous_world_matrix;
    world_matrix._11_12_13_14 = vin.world0;
    world_matrix._21_22_23_24 = vin.world1;
    world_matrix._31_32_33_34 = vin.world2;
    world_matrix._41_42_43_44 = vin.world3;
    world_matrix = mul(world, world_matrix);

    previous_world_matrix._11_12_13_14 = vin.previous_world0;
    previous_world_matrix._21_22_23_24 = vin.previous_world1;
    previous_world_matrix._31_32_33_34 = vin.previous_world2;
    previous_world_matrix._41_42_43_44 = vin.previous_world3;
    previous_world_matrix = mul(previous_world, previous_world_matrix);

    vin.position.w = 1;
    vout.position = mul(vin.position, mul(world_matrix, view_projection_transform));
    vout.w_position = mul(vin.position, world_matrix);

    vout.current_clip_position = vout.position;
    vout.previous_clip_position = mul(vin.position, mul(previous_world_matrix, previous_view_projection_transform));

    vin.normal.w = 0;
    vout.w_normal = normalize(mul(vin.normal, world_matrix));

    vin.tangent.w = 0;
    vout.w_tangent = normalize(mul(vin.tangent, world_matrix));
    vout.w_tangent.w = sigma;

    vout.texcoord = vin.texcoord;
	
    return vout;
}
