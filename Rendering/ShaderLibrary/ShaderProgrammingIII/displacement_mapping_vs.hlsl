#include "displacement_mapping.hlsli"

HS_IN main(float4 position : POSITION, float4 normal : NORMAL, float2 texcoord : TEXCOORD)
{
    HS_IN o = (HS_IN)0;
    o.position = position;
    o.normal = normal.xyz;
    o.color = material_color;
    o.texcoord = texcoord;
    return o;
}
