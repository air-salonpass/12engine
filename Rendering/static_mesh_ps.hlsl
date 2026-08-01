#include "static_mesh.hlsli"

Texture2D color_map : register(t0);
Texture2D normal_map : register(t1);

SamplerState point_sampler_state : register(s0);
SamplerState linear_sampler_state : register(s1);
SamplerState anisotropic_sampler_state : register(s2);

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 color =
        color_map.Sample(
            anisotropic_sampler_state,
            pin.texcoord);

    float alpha =
        color.a;

    float3 N =
        normalize(pin.world_normal.xyz);

    // ====================================
    // Normal Mapping
    // ====================================

    float3 T =
        float3(1.0001f, 0, 0);

    float3 B =
        normalize(cross(N, T));

    T =
        normalize(cross(B, N));

    float4 normal =
        normal_map.Sample(
            linear_sampler_state,
            pin.texcoord);

    normal =
        (normal * 2.0f) - 1.0f;

    normal.w = 0;

    N =
        normalize(
            (normal.x * T) +
            (normal.y * B) +
            (normal.z * N));

    // ====================================
    // Lighting
    // ====================================

    float3 L =
    normalize(-light_direction.xyz);

    float ambient = 0.2f;

    float3 diffuse =
    color.rgb *
    (ambient + max(0, dot(N, L)));

    float3 V =
    normalize(
        camera_position.xyz -
        pin.world_position.xyz);

    float3 specular =
    pow(
        max(
            0,
            dot(
                N,
                normalize(V + L))),
        128);

    return float4(
    diffuse + specular,
    alpha) * pin.color;
}