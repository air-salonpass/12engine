#include "terrain_primitive.hlsli"
// Unit02: 粗い平面メッシュを地形の制御点として渡す。
HS_IN main(float4 position : POSITION, float4 normal : NORMAL, float2 texcoord : TEXCOORD)
{ HS_IN o; o.position = position; o.color = material_color; o.texcoord = texcoord; return o; }
