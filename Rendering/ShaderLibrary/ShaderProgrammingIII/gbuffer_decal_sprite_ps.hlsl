#include "gbuffer_decal_sprite.hlsli"
#include "gbuffer.hlsli"

Texture2D<float> scene_depth : register(t4);
Texture2D<float4> decal_map : register(t6);
SamplerState linear_sampler : register(s1);

float4 main(VS_OUT pin) : SV_TARGET
{
    float depth = scene_depth.Sample(linear_sampler, pin.texcoord);
    float projection_depth = convert_linear_depth_to_projection_depth(depth, z_buffer_parameteres);
    float4 clip_position = float4(pin.texcoord * float2(2, -2) + float2(-1, 1), projection_depth, 1);
    float4 world_position = mul(clip_position, inverse_view_projection_transform);
    world_position /= world_position.w;

    // Unit04: 投影ボックス外の画素を破棄し、ローカルXYをデカールUVとして使う。
    float4 local_position = mul(world_position, decal_inverse_world);
    clip(0.5f - max(abs(local_position.x), max(abs(local_position.y), abs(local_position.z))));
    float2 decal_uv = local_position.xy + 0.5f;
    float4 color = decal_map.Sample(linear_sampler, decal_uv) * decal_color;
    clip(color.a - 0.01f);
    return color;
}
