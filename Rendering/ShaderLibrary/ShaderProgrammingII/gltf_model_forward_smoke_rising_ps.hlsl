#include "gltf_model_forward_smake_rising.hlsli"

#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2
SamplerState sampler_states[3] : register(s0);

//	ノイズテクスチャ
Texture2D noise_map : register(t11);

float4 main(GS_OUT pin, bool is_front_face : SV_IsFrontFace) : SV_TARGET
{
    float2 screen_texcoord = pin.position.xy / viewport_size.xy;
    float smoke_density = pin.texcoord.z;

    float smoke_step_value = smoke_step;
    smoke_step_value *= abs(sin(smoke_density)) * 0.5f + 0.5f;

    float time1 = frac(options.z * (0.1f * smoke_vertical_speed));
    float time2 = frac(time1 + 0.5f);
    float2 texcoord1 = (pin.texcoord + time1) * smoke_tiling;
    float2 texcoord2 = (pin.texcoord + time2) * smoke_tiling;
    float noise1 = step(noise_map.Sample(sampler_states[LINEAR], texcoord1).r, smoke_step_value);
    float noise2 = step(noise_map.Sample(sampler_states[LINEAR], texcoord2).r, smoke_step_value);
    float lerp_rate = abs((time1 - 0.5f) * 2.0f);
    float noise = lerp(noise1, noise2, lerp_rate);
    float alpha = smoke_alpha * noise * smoke_density;

    return float4(1, 1, 1, alpha);
}
