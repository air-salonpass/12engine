#include "skinned_mesh.hlsli"

float4 main(VS_OUT pin) : SV_TARGET
{
    float3 N =
        normalize(pin.world_normal.xyz);

    float3 L =
        normalize(-light_direction.xyz);

    // UNIT11～12の形状比較で逆光側も輪郭を読めるよう、弱い環境光を加える。
    float diffuse =
        0.45f + 0.55f * max(0, dot(N, L));

    return float4(diffuse, diffuse, diffuse, 1)
        * pin.color;
}
