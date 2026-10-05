#include "sky_box.hlsli"

TextureCube environment_map : register(t0);
SamplerState environment_sampler : register(s0);

// Unit03-02: Draw the same prefiltered environment used by the PBR reflections.
float4 main(VS_OUT pin) : SV_TARGET
{
    float3 eye = normalize(pin.world_position.xyz - camera_position.xyz);
    float3 ambient = ambient_color.rgb * ambient_color.a;
    return float4(ambient, 1.0f) * environment_map.SampleLevel(environment_sampler, eye, 0) * pin.color;
}
