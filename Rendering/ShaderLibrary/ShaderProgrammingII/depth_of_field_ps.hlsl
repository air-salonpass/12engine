#include "depth_of_field.hlsli"

Texture2D color_map : register(t0);
Texture2D bokeh_map : register(t1);
Texture2D depth_map : register(t2);
SamplerState sampler_states[3] : register(s0);

// Unit02-03: Blend the sharp and blurred scenes according to distance from focus.
float4 main(VS_OUT pin) : SV_TARGET
{
    float depth = depth_map.Sample(sampler_states[0], pin.texcoord).r;
    float view_space_z = camera_clip_distance.z /
        max(camera_clip_distance.y - depth * camera_clip_distance.w, 1.0e-5f);
    float blend = saturate(abs(view_space_z - focus_distance) / max(dof_range, 1.0e-5f));
    float3 sharp = color_map.Sample(sampler_states[1], pin.texcoord).rgb;
    float3 blurred = bokeh_map.Sample(sampler_states[1], pin.texcoord).rgb;
    return float4(lerp(sharp, blurred, blend), 1.0f);
}
