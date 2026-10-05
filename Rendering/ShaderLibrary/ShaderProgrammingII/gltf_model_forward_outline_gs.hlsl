#include "gltf_model_forward_outline.hlsli"

[maxvertexcount(6)]
void main(triangle VS_OUT input[3], inout TriangleStream<GS_OUT> output)
{
	//	•\–Ê“o˜^
    int i;
    for (i = 0; i < 3; i++)
    {
        GS_OUT element = (GS_OUT) 0;
        element.vs = input[i];
        element.is_outline = false;
        output.Append(element);
    }
    output.RestartStrip();

	//	— –Ê“o˜^
    for (i = 2; i >= 0; i--)
    {
        GS_OUT element = (GS_OUT) 0;
        element.vs = input[i];
        element.vs.w_position.w = 1;
        element.vs.w_position.xyz = input[i].w_position.xyz + input[i].w_normal.xyz * outline_size;
        element.vs.position = mul(element.vs.w_position, view_projection_transform);
        element.is_outline = true;

        output.Append(element);
    }

    output.RestartStrip();
}
