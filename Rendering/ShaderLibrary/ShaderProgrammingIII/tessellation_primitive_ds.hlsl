#include "tessellation_primitive.hlsli"

[domain("tri")]
DS_OUT main(HS_CONSTANT_OUT constants, float3 barycentric : SV_DomainLocation, const OutputPatch<DS_IN, 3> patch)
{
    DS_OUT o;
    // Unit01: 新しく生成された頂点を重心座標で補間し、通常のクリップ座標へ変換する。
    float4 local = patch[0].position * barycentric.x + patch[1].position * barycentric.y + patch[2].position * barycentric.z;
    o.color = patch[0].color * barycentric.x + patch[1].color * barycentric.y + patch[2].color * barycentric.z;
    o.texcoord = patch[0].texcoord * barycentric.x + patch[1].texcoord * barycentric.y + patch[2].texcoord * barycentric.z;
    o.position = mul(mul(local, world), view_projection_transform);
    return o;
}
