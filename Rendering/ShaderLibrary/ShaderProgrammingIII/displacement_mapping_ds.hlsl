#include "displacement_mapping.hlsli"
Texture2D<float4> height_map : register(t1);
SamplerState point_sampler : register(s0);

[domain("tri")]
DS_OUT main(HS_CONSTANT_OUT constants, float3 barycentric : SV_DomainLocation, const OutputPatch<DS_IN, 3> patch)
{
    DS_OUT o;
    float4 local = patch[0].position * barycentric.x + patch[1].position * barycentric.y + patch[2].position * barycentric.z;
    o.texcoord = patch[0].texcoord * barycentric.x + patch[1].texcoord * barycentric.y + patch[2].texcoord * barycentric.z;
    o.color = patch[0].color * barycentric.x + patch[1].color * barycentric.y + patch[2].color * barycentric.z;
    float3 local_normal = normalize(patch[0].normal * barycentric.x + patch[1].normal * barycentric.y + patch[2].normal * barycentric.z);
    o.normal = normalize(mul(local_normal, (float3x3)world));
    // Unit01: 高さマップの値だけ、回転後の面法線方向へ頂点を押し出す。
    float4 world_position = mul(local, world);
    world_position.xyz += o.normal * height_map.SampleLevel(point_sampler, o.texcoord, 0).r * height_scale;
    o.position = mul(world_position, view_projection_transform);
    return o;
}
