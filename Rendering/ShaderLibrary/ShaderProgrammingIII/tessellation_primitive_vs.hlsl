#include "tessellation_primitive.hlsli"
// Unit01: 頂点をまだ投影せず、ハルシェーダーへ制御点として渡す。
HS_IN main(float4 position : POSITION, float4 normal : NORMAL, float2 texcoord : TEXCOORD)
{
    HS_IN o; o.position = position; o.color = material_color; o.texcoord = texcoord; return o;
}
