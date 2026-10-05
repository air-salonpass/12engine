#include "sprite.hlsli"
#include "scene_constant_buffer.hlsli"
#include "lights.hlsli"
#include "shading_functions.hlsli"

cbuffer RAY_MARCHING_CONSTANT_BUFFER : register(b2) { int iteration; float3 ray_padding; };
cbuffer LIGHT_CONSTANT_BUFFER : register(b3)
{
    float4 ambient_color;
    uint4 light_count;
    directional_lights directional_light;
    point_lights point_light[8];
    spot_lights spot_light[8];
};

float sphere_distance(float3 p) { return length(p - float3(1, 0, 0)) - 2.0f; }
// Unit05-01: 教材と完成サンプルの中心(-1,0,0)・半径2の箱距離。
float box_distance(float3 p) { return length(max(abs(p - float3(-1, 0, 0)) - float3(2, 2, 2), 0.0f)); }
float scene_distance(float3 p) { return min(sphere_distance(p), box_distance(p)); }
float3 scene_normal(float3 p)
{
    const float e = 0.01f;
    return normalize(float3(scene_distance(p + float3(e,0,0)) - scene_distance(p - float3(e,0,0)),
                            scene_distance(p + float3(0,e,0)) - scene_distance(p - float3(0,e,0)),
                            scene_distance(p + float3(0,0,e)) - scene_distance(p - float3(0,0,e))));
}

// Unit05-01: Sphere tracing advances by the signed-distance estimate.
float4 main(VS_OUT pin) : SV_TARGET
{
    float4 clip = float4(pin.texcoord * float2(2,-2) + float2(-1,1), 1, 1);
    float4 far_position = mul(clip, inverse_view_projection_transform);
    float3 ray_direction = normalize(far_position.xyz / far_position.w - camera_position.xyz);
    float3 ray_position = camera_position.xyz;
    for (int i = 0; i < max(iteration, 1); ++i)
    {
        float distance_to_scene = scene_distance(ray_position);
        if (distance_to_scene < 0.0001f)
        {
            float3 normal = scene_normal(ray_position);
            float3 light = CalcLambert(normal, directional_light.direction.xyz,
                directional_light.color.rgb, 1.0f) + ambient_color.rgb;
            return float4(light, 1.0f);
        }
        if (distance(ray_position, camera_position.xyz) > camera_clip_distance.y) break;
        ray_position += ray_direction * max(distance_to_scene, 0.001f);
    }
    return float4(0,0,0,0);
}
