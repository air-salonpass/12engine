#include "gltf_model_gbuffer.hlsli"

VS_OUT main(VS_IN vin)
{
    float sigma = vin.tangent.w;

    VS_OUT vout = (VS_OUT) 0;

    vin.position.w = 1;
    vout.position = mul(vin.position, mul(world, view_projection_transform));
    vout.w_position = mul(vin.position, world);

    vin.normal.w = 0;
    vout.w_normal = normalize(mul(vin.normal, world));

    vin.tangent.w = 0;
    vout.w_tangent = normalize(mul(vin.tangent, world));
    vout.w_tangent.w = sigma;

    vout.texcoord = vin.texcoord;

    return vout;
}
