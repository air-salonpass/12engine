#include "gltf_model_forward_smake_rising.hlsli"

[maxvertexcount(FACE_DENSITY * 3)]
void main(triangle VS_OUT_SR input[3], inout TriangleStream<GS_OUT> output)
{
    //  煙の部分だけ描画
    int i, j;

    float amplitude_factor = cos(options.z);
    for (j = 0; j < FACE_DENSITY; ++j)
    {
        float smoke_density = 1.0f - (float) j / FACE_DENSITY;
        float rising_value = (smoke_vertical_length * (1 + j)) / (1 + FACE_DENSITY);
        for (i = 0; i < 3; i++)
        {
            GS_OUT element = (GS_OUT) 0;

            //  真上方向に立ち昇らせる
            float3 w_position = input[i].w_position.xyz;
            float factor = sin(input[i].vertex_id + i) * 0.25f + 0.75f;
            w_position += float3(0, rising_value * factor, 0);
            
            //  時間経過で左右にブレさせる
            w_position += sin(input[i].vertex_id + amplitude_factor) * float3(smoke_horizontal_range, 0, smoke_horizontal_range);

            element.position = mul(float4(w_position, 1), view_projection_transform);
            element.texcoord.xy = input[i].texcoord;
            element.texcoord.z = smoke_density + factor;

            output.Append(element);
        }
        //  上に行けば行くほど揺らぎを大きくさせる
        amplitude_factor += (2.0f * PI) / FACE_DENSITY;
        
        output.RestartStrip();
    }
}
