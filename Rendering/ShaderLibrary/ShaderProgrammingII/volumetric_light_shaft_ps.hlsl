#include "sprite.hlsli"
#include "scene_constant_buffer.hlsli"
#include "lights.hlsli"

Texture2D color_map : register(t0);
Texture2D depth_map : register(t1);
SamplerState samplers[3] : register(s0);
Texture2D shadow_map : register(t10);
SamplerState shadow_sampler : register(s10);

cbuffer LIGHT_SHAFT_CONSTANTS : register(b2)
{
    int iteration; float intensity; float2 shaft_padding;
    float fog_scale; float fog_length_density; float2 fog_padding;
    float4 wind_power;
};
cbuffer LIGHT_CONSTANTS : register(b3) { float4 ambient_color; uint4 light_count; directional_lights directional_light; point_lights points[8]; spot_lights spots[8]; };
cbuffer SHADOW_CONSTANTS : register(b4) { row_major float4x4 light_view_projection; float shadow_attenuation; float shadow_bias; float2 shadow_padding; };

float hash31(float3 p) { return frac(sin(dot(p, float3(12.9898f,78.233f,37.719f))) * 43758.5453f); }
float value_noise(float3 p)
{
    float3 i=floor(p), f=frac(p); f=f*f*(3.0f-2.0f*f);
    float n000=hash31(i), n100=hash31(i+float3(1,0,0)), n010=hash31(i+float3(0,1,0)), n110=hash31(i+float3(1,1,0));
    float n001=hash31(i+float3(0,0,1)), n101=hash31(i+float3(1,0,1)), n011=hash31(i+float3(0,1,1)), n111=hash31(i+1);
    return lerp(lerp(lerp(n000,n100,f.x),lerp(n010,n110,f.x),f.y),lerp(lerp(n001,n101,f.x),lerp(n011,n111,f.x),f.y),f.z);
}

// Unit05-02: March camera-to-surface and integrate only samples visible to the light.
float4 main(VS_OUT pin) : SV_TARGET
{
    float depth = depth_map.Sample(samplers[0], pin.texcoord).r;
    float4 ndc = float4(pin.texcoord * float2(2,-2) + float2(-1,1), depth, 1);
    float4 reconstructed = mul(ndc, inverse_view_projection_transform);
    float3 world_position = reconstructed.xyz / reconstructed.w;
    int steps = max(iteration, 1);
    float3 ray_step = (world_position - camera_position.xyz) / steps;
    float3 ray_position = camera_position.xyz + ray_step * hash31(float3(pin.texcoord, options.z));
    float shaft = 0.0f;
    for (int i=0; i<steps; ++i)
    {
        float4 shadow_position = mul(float4(ray_position,1), light_view_projection);
        shadow_position /= shadow_position.w; shadow_position.y = -shadow_position.y; shadow_position.xy = shadow_position.xy*0.5f+0.5f;
        float light_depth = shadow_map.Sample(shadow_sampler, shadow_position.xy).r;
        if (shadow_position.z - light_depth <= shadow_bias)
        {
            float3 fog_position = ray_position / max(fog_scale,0.001f) * 0.01f + wind_power.xyz * options.z;
            float density = value_noise(fog_position);
            shaft += intensity * density + length(ray_step) * fog_length_density * density;
        }
        ray_position += ray_step;
    }
    shaft /= steps;
    return float4(directional_light.color.rgb * directional_light.color.a * shaft, 1.0f);
}
