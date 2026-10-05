#include "tessellation_primitive.hlsli"

HS_CONSTANT_OUT patch_constants(InputPatch<HS_IN, 3> patch, uint patch_id : SV_PrimitiveID)
{
    HS_CONSTANT_OUT o;
    // Unit01: 三角形の各辺と内部の分割数をGPUへ指示する。
    o.edge[0] = o.edge[1] = o.edge[2] = max(edge_factor, 1.0f);
    o.inside = max(inside_factor, 1.0f);
    return o;
}

[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")]
[outputcontrolpoints(3)][patchconstantfunc("patch_constants")]
DS_IN main(InputPatch<HS_IN, 3> patch, uint id : SV_OutputControlPointID, uint patch_id : SV_PrimitiveID)
{
    DS_IN o; o.position = patch[id].position; o.color = patch[id].color; o.texcoord = patch[id].texcoord; return o;
}
