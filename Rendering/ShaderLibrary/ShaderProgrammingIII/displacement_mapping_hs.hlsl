#include "displacement_mapping.hlsli"

float distance_factor(float4 position)
{
    float camera_distance = distance(mul(position, world).xyz, camera_position.xyz);
    return clamp(1.0f - camera_distance / max(lod_distance_max, 0.001f), 0.001f, 1.0f);
}

HS_CONSTANT_OUT patch_constants(InputPatch<HS_IN, 3> patch)
{
    HS_CONSTANT_OUT o;
    float3 f = float3(distance_factor(patch[0].position), distance_factor(patch[1].position), distance_factor(patch[2].position));
	// Unit01: サンプルはedge/innerの大きい方をLOD係数に使うため、inner=0でも面が崩れない。
	float tessellation = max(edge_factor, inside_factor);
	o.edge[0] = tessellation * 0.5f * (f.y + f.z);
	o.edge[1] = tessellation * 0.5f * (f.x + f.z);
	o.edge[2] = tessellation * 0.5f * (f.x + f.y);
	o.inside = tessellation * (f.x + f.y + f.z) / 3.0f;
    return o;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("patch_constants")]
DS_IN main(InputPatch<HS_IN, 3> patch, uint id : SV_OutputControlPointID)
{
    DS_IN o;
    o.position = patch[id].position;
    o.color = patch[id].color;
	o.normal = patch[id].normal;
    o.texcoord = patch[id].texcoord;
    return o;
}
