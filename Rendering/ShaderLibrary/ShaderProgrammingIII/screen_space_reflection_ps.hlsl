#include "sprite.hlsli"
#include "scene_constant_buffer.hlsli"
#include "gbuffer.hlsli"
#include "screen_space_reflection.hlsli"

Texture2D<float4> gbuffer_base_color : register(t0);
Texture2D<float4> gbuffer_emissive : register(t1);
Texture2D<float4> gbuffer_normal : register(t2);
Texture2D<float4> gbuffer_parameter : register(t3);
Texture2D<float> gbuffer_depth : register(t4);
Texture2D<float4> gbuffer_velocity : register(t5);
SamplerState sampler_states[5] : register(s0);

GBufferData read_gbuffer(float2 uv)
{
    PSGBufferTextures textures = { gbuffer_base_color, gbuffer_emissive, gbuffer_normal,
        gbuffer_parameter, gbuffer_depth, gbuffer_velocity, sampler_states[0] };
    return DecodeGBuffer(textures, uv, inverse_view_projection_transform, z_buffer_parameteres);
}

float4 main(VS_OUT pin) : SV_TARGET
{
    GBufferData data = read_gbuffer(pin.texcoord);
    float4 view_position = mul(float4(data.w_position, 1), view_transform);
    view_position /= view_position.w;
    float3 view_normal = normalize(mul(float4(data.w_normal, 0), view_transform).xyz);
    float3 reflected_step = normalize(reflect(normalize(view_position.xyz), view_normal));
    reflected_step *= ssr_max_distance / max(ssr_iteration, 1);
    float3 ray_position = view_position.xyz + reflected_step;

    // Unit08: 視線空間のレイを画面へ戻し、G-Bufferの線形深度を越えた位置を反射点とする。
    [loop] for (int i = 1; i < ssr_iteration; ++i)
    {
        float4 clip_position = mul(float4(ray_position, 1), projection_transform);
        float3 ndc = clip_position.xyz / clip_position.w;
        float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
        if (any(uv < 0) || any(uv > 1)) break;
        float ray_depth = convert_projection_depth_to_linear_depth(ndc.z, z_buffer_parameteres);
        float surface_depth = read_gbuffer(uv).linear_depth;
        if (ray_depth - surface_depth > 0)
        {
            float alpha = 1.0f;
            alpha *= smoothstep(1.0f, 0.8f, saturate(2.0f * abs(uv.x - 0.5f)));
            alpha *= smoothstep(1.0f, 0.8f, saturate(2.0f * abs(uv.y - 0.5f)));
            return float4(read_gbuffer(saturate(uv)).base_color, saturate(alpha));
        }
        ray_position += reflected_step;
    }
    return 0;
}
