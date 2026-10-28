#pragma once

// モデルと床が共有するHLSL。外部画像なしで空・地面の反射光を近似する。
// t12 / s2 / b2はGltfRendererとSceneGeometryRendererのRoot Signatureへ登録する。
inline constexpr const char* lighting_shader_source12 = R"(
Texture2D<float> sun_shadow_map : register(t12);
SamplerState shadow_point_clamp : register(s2);
cbuffer LightingConstants : register(b2) {
 row_major float4x4 light_view_projection;
 float4 sun_color_intensity;
 float4 sky_color_intensity;
 float4 ground_color_intensity;
 float4 shadow_parameters;
 float4 sun_direction;
};

// 空側を青、地面側を暖色にし、向きに応じた間接光を作る。実写HDRIは不要。
float3 procedural_environment(float3 direction) {
 float sky = pow(saturate(direction.y * 0.5 + 0.5), 0.7);
 return lerp(ground_color_intensity.rgb, sky_color_intensity.rgb, sky) * sky_color_intensity.a;
}

// 画面外の遮蔽物も光源視点の深度に記録される。画面空間の影と異なり、カメラ外で消えない。
float sunlight_visibility(float3 world_position, float3 world_normal) {
 if (shadow_parameters.x <= 0) return 1;
 float4 clip = mul(float4(world_position + world_normal * shadow_parameters.w, 1), light_view_projection);
 float3 projected = clip.xyz / max(clip.w, 0.0001);
 float2 uv = projected.xy * float2(0.5, -0.5) + 0.5;
 if (any(uv <= 0) || any(uv >= 1) || projected.z <= 0 || projected.z >= 1) return 1;
 float visibility = 0;
 // 3x3の深度比較を平均し、影の輪郭を和らげる。自己影の縞はbiasと法線押出しで抑える。
 [unroll] for (int y=-1; y<=1; ++y) {
  [unroll] for (int x=-1; x<=1; ++x) {
   float sample_depth = sun_shadow_map.SampleLevel(shadow_point_clamp, uv + float2(x,y)*shadow_parameters.z, 0);
   visibility += (projected.z - shadow_parameters.y <= sample_depth) ? 1.0 : 0.0;
  }
 }
 return lerp(1.0, visibility / 9.0, saturate(shadow_parameters.x));
}
)";
