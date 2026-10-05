#include "terrain_primitive.hlsli"
HS_CONSTANT_OUT patch_constants(InputPatch<HS_IN, 3> p, uint id : SV_PrimitiveID)
{ HS_CONSTANT_OUT o; o.edge[0] = o.edge[1] = o.edge[2] = max(edge_factor, 1); o.inside = max(inside_factor, 1); return o; }
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")][outputcontrolpoints(3)][patchconstantfunc("patch_constants")]
DS_IN main(InputPatch<HS_IN, 3> p, uint id : SV_OutputControlPointID, uint patch_id : SV_PrimitiveID)
{ DS_IN o; o.position=p[id].position; o.color=p[id].color; o.texcoord=p[id].texcoord; return o; }
