#include "static_mesh.hlsli"

VS_OUT main(VS_IN vin)
{
    VS_OUT vout;

    vout.position =
        mul(vin.position,
        mul(world, view_projection));

    vout.world_position =
        mul(vin.position, world);

    float4 normal =
        float4(vin.normal, 0);

    vout.world_normal =
        normalize(mul(normal, world));

    vout.color =
        material_color;

    vout.texcoord =
        vin.texcoord;

    return vout;
}