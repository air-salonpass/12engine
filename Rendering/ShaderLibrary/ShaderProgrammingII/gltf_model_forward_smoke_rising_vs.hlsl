#include "gltf_model_forward_smake_rising.hlsli"

VS_OUT_SR main(VS_IN vin, uint vertex_id : SV_VERTEXID)
{
    float sigma = vin.tangent.w;

    VS_OUT_SR vout = (VS_OUT_SR) 0;

    vin.position.w = 1;
    vout.position = mul(vin.position, mul(world, view_projection_transform));
    vout.w_position = mul(vin.position, world);

    vin.normal.w = 0;
    vout.w_normal = normalize(mul(vin.normal, world));

    vin.tangent.w = 0;
    vout.w_tangent = normalize(mul(vin.tangent, world));
    vout.w_tangent.w = sigma;

    vout.texcoord = vin.texcoord;
    vout.vertex_id = vertex_id;
	
    return vout;
}
