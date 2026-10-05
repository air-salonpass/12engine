#include "gltf_model_forward.hlsli"

[maxvertexcount(3)]
void main(triangle VS_OUT input[3], inout TriangleStream<VS_OUT> output)
{
    //  ’¸“_–ˆ‚Ì–@ü“™‚ğ•½‹Ï‰»
    float3 w_normal = input[0].w_normal.xyz + input[1].w_normal.xyz + input[2].w_normal.xyz;
    w_normal = normalize(w_normal);

    float4 w_tangent = input[0].w_tangent + input[1].w_tangent + input[2].w_tangent;
    w_tangent.xyz = normalize(w_tangent.xyz);
    w_tangent.w /= 3;

	//	–Ê“o˜^
    for (int i = 0; i < 3; i++)
    {
        VS_OUT element = input[i];
        element.w_normal = float4(w_normal, input[i].w_normal.w);
        element.w_tangent = w_tangent;
        output.Append(element);
    }
    output.RestartStrip();
}
