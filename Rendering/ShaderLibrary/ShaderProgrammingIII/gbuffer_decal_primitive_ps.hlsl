#include "gbuffer_decal_primitive.hlsli"
#include "gbuffer.hlsli"
Texture2D<float> scene_depth : register(t4);
Texture2D<float4> decal_map : register(t6);
SamplerState linear_sampler : register(s1);

float4 main(DECAL_PS_IN pin) : SV_TARGET
{
    // Unit04: 深度から復元したワールド座標をデカール空間へ変換してUVを作る。
    float2 uv = pin.position.xy * viewport_size.zw;
    float depth = scene_depth.Sample(linear_sampler, uv);
    float projection_depth = convert_linear_depth_to_projection_depth(depth, z_buffer_parameteres);
    float4 clip_position = float4(uv * float2(2,-2) + float2(-1,1), projection_depth, 1);
    float4 world_position = mul(clip_position, inverse_view_projection_transform); world_position /= world_position.w;
    float4 local = mul(world_position, decal_inverse_world);
    clip(abs(local.xyz) - 0.5f);
    float2 decal_uv = local.xy + 0.5f;
    float4 color = decal_map.Sample(linear_sampler, decal_uv) * decal_color;
    clip(color.a - 0.5f);
    return color;
}
