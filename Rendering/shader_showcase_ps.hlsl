#include "fullscreen_quad.hlsli"

// シェーダープログラミングII・IIIの主要ポストエフェクトを、共通の全画面パスで試せるようにまとめる。
cbuffer SHADER_SHOWCASE_CONSTANTS : register(b1)
{
	uint enabled_effects;
	float effect_time;
	float flow_strength;
	float flow_speed;

	float chromatic_strength;
	float vignette_strength;
	float vignette_smoothness;
	float radial_strength;

	float2 radial_center;
	float radial_radius;
	float dof_focus_distance;

	float dof_range;
	float dof_blur_size;
	float fxaa_strength;
	float showcase_padding;

	// Unit08と同じく、深度から視点空間座標を復元して反射レイを画面へ再投影する。
	row_major float4x4 projection_transform;
	row_major float4x4 inverse_projection_transform;
	float4 world_up_view;
	float4 ssr_parameters;
};

#define EFFECT_FLOW_DISTORTION 1
#define EFFECT_DEPTH_OF_FIELD 2
#define EFFECT_VIGNETTE 4
#define EFFECT_CHROMATIC_ABERRATION 8
#define EFFECT_RADIAL_BLUR 16
#define EFFECT_FXAA 32
#define EFFECT_DEMO_BACKGROUND 64
#define EFFECT_SPLIT_COMPARISON 128
#define EFFECT_SCREEN_SPACE_REFLECTION 256

#define POINT_SAMPLER 0
#define LINEAR_SAMPLER 1

Texture2D scene_map : register(t0);
Texture2D depth_map : register(t1);
SamplerState sampler_states[3] : register(s0);

float luminance(float3 color)
{
	return dot(color, float3(0.299f, 0.587f, 0.114f));
}

float3 demo_background(float2 uv)
{
	// 細線・色境界・明るい点を同時に置き、FXAA・色収差・ブラーの差を見つけやすくする。
	float2 centered = uv - 0.5f;
	float3 color = lerp(float3(0.015f, 0.025f, 0.055f), float3(0.055f, 0.015f, 0.075f), uv.y);
	float2 grid_uv = abs(frac(uv * float2(32.0f, 18.0f)) - 0.5f);
	float fine_grid = 1.0f - smoothstep(0.455f, 0.49f, max(grid_uv.x, grid_uv.y));
	color += fine_grid * float3(0.025f, 0.10f, 0.16f);

	// 斜めの白黒ストライプは、FXAAによるジャギー低減を確認するためのテストパターン。
	float stripe_region = 1.0f - smoothstep(0.0f, 0.015f, abs(uv.y - 0.83f));
	float stripes = step(0.5f, frac((uv.x + uv.y * 0.35f) * 55.0f));
	color = lerp(color, lerp(float3(0.04f, 0.04f, 0.05f), 0.9f.xxx, stripes), stripe_region);

	// RGBの発光円は、色収差とラジアルブラーで色の分離・残像が見える目印になる。
	float pulse = 0.015f * sin(effect_time * 2.0f);
	float red_orb = 1.0f - smoothstep(0.045f + pulse, 0.055f + pulse, length(uv - float2(0.18f, 0.24f)));
	float green_orb = 1.0f - smoothstep(0.045f, 0.055f, length(uv - float2(0.50f, 0.20f)));
	float blue_orb = 1.0f - smoothstep(0.045f - pulse, 0.055f - pulse, length(uv - float2(0.82f, 0.24f)));
	color += float3(red_orb * 2.2f, green_orb * 2.2f, blue_orb * 2.2f);

	// 中央から広がる同心円は、フロー歪みと放射ブラーの方向を読み取りやすくする。
	float rings = 1.0f - smoothstep(0.035f, 0.08f, abs(frac(length(centered) * 18.0f - effect_time * 0.25f) - 0.5f));
	color += rings * float3(0.08f, 0.16f, 0.22f);
	return color;
}

float4 sample_scene(float2 uv)
{
	float4 scene = scene_map.SampleLevel(sampler_states[LINEAR_SAMPLER], uv, 0.0f);
	if ((enabled_effects & EFFECT_DEMO_BACKGROUND) != 0)
	{
		// 深度1.0の背景画素だけを置き換え、読み込んだモデルはそのまま前景として残す。
		float depth = depth_map.SampleLevel(sampler_states[LINEAR_SAMPLER], uv, 0.0f).r;
		float background_mask = smoothstep(0.9995f, 1.0f, depth);
		scene.rgb = lerp(scene.rgb, demo_background(uv), background_mask);
	}
	return scene;
}

float2 apply_flow_distortion(float2 uv)
{
	// II Unit01のフローマップの考え方を使い、時間変化する2方向の流れでUVを歪ませる。
	float phase = effect_time * flow_speed;
	float2 flow = float2(
		sin(uv.y * 18.0f + phase) + cos(uv.y * 7.0f - phase * 0.7f),
		cos(uv.x * 16.0f - phase * 0.8f) + sin(uv.x * 9.0f + phase));
	return uv + flow * flow_strength * 0.004f;
}

float3 sample_depth_of_field(float2 uv, float3 sharp_color, float2 texel_size)
{
	// II Unit02-03と同様に深度から被写体距離を復元し、焦点面から離れた画素ほどぼかす。
	const float near_clip = 0.1f;
	const float far_clip = 100.0f;
	float depth = depth_map.Sample(sampler_states[LINEAR_SAMPLER], uv).r;
	float view_distance = near_clip * far_clip /
		max(far_clip - depth * (far_clip - near_clip), 0.00001f);
	float normalized_distance = view_distance / far_clip;
	float blur_amount = saturate(
		abs(normalized_distance - dof_focus_distance) / max(dof_range, 0.0001f));

	static const float2 directions[8] =
	{
		float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1),
		float2(0.707f, 0.707f), float2(-0.707f, 0.707f),
		float2(0.707f, -0.707f), float2(-0.707f, -0.707f)
	};
	float3 blurred = sharp_color;
	[unroll]
	for (int index = 0; index < 8; ++index)
	{
		blurred += sample_scene(
			uv + directions[index] * texel_size * dof_blur_size).rgb;
	}
	return lerp(sharp_color, blurred / 9.0f, blur_amount);
}

float3 sample_radial_blur(float2 uv, float3 original)
{
	// III Unit03の中心方向サンプリングを、画面解像度に依存しない強度へ調整する。
	float2 direction = radial_center - uv;
	float mask = saturate(length(direction) / max(radial_radius, 0.0001f));
	float3 accumulated = original;
	[unroll]
	for (int index = 1; index < 8; ++index)
	{
		float amount = radial_strength * (index / 7.0f);
		accumulated += sample_scene(uv + direction * amount).rgb;
	}
	return lerp(original, accumulated / 8.0f, mask);
}

float3 sample_fxaa(float2 uv, float2 texel_size, float3 source)
{
	// III Unit10のFXAAを簡略化し、周囲の輝度差が大きい輪郭だけを平滑化する。
	float3 north = sample_scene(uv + float2(0, -texel_size.y)).rgb;
	float3 south = sample_scene(uv + float2(0, texel_size.y)).rgb;
	float3 west = sample_scene(uv + float2(-texel_size.x, 0)).rgb;
	float3 east = sample_scene(uv + float2(texel_size.x, 0)).rgb;
	float minimum_luminance = min(luminance(source), min(min(luminance(north), luminance(south)), min(luminance(west), luminance(east))));
	float maximum_luminance = max(luminance(source), max(max(luminance(north), luminance(south)), max(luminance(west), luminance(east))));
	float edge = saturate((maximum_luminance - minimum_luminance) * 4.0f);
	float3 average = (north + south + west + east) * 0.25f;
	return lerp(source, average, edge * fxaa_strength);
}

float3 reconstruct_view_position(float2 uv, float depth)
{
	float4 clip_position = float4(
		uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f),
		depth,
		1.0f);
	float4 view_position = mul(clip_position, inverse_projection_transform);
	return view_position.xyz / max(view_position.w, 0.00001f);
}

float2 project_view_position(float3 view_position)
{
	float4 clip_position = mul(float4(view_position, 1.0f), projection_transform);
	float2 ndc = clip_position.xy / max(clip_position.w, 0.00001f);
	return ndc * float2(0.5f, -0.5f) + 0.5f;
}

float4 apply_screen_space_reflection(float2 uv, float2 texel_size, float3 surface_color)
{
	float center_depth = depth_map.SampleLevel(sampler_states[POINT_SAMPLER], uv, 0.0f).r;
	float3 reflection_result = surface_color;
	float floor_mask = 0.0f;
	if (center_depth < 0.9999f)
	{
		float3 view_position = reconstruct_view_position(uv, center_depth);
		float right_depth = depth_map.SampleLevel(sampler_states[POINT_SAMPLER], uv + float2(texel_size.x, 0), 0.0f).r;
		float down_depth = depth_map.SampleLevel(sampler_states[POINT_SAMPLER], uv + float2(0, texel_size.y), 0.0f).r;
		float3 right_position = reconstruct_view_position(uv + float2(texel_size.x, 0), right_depth);
		float3 down_position = reconstruct_view_position(uv + float2(0, texel_size.y), down_depth);
		float3 view_normal = normalize(cross(down_position - view_position, right_position - view_position));
		float3 view_direction = normalize(view_position);
		if (dot(view_normal, -view_direction) < 0.0f)
		{
			view_normal = -view_normal;
		}

		// G-Bufferの材質情報がない現在の前方描画では、上向き面を反射床として識別する。
		floor_mask = saturate((abs(dot(view_normal, normalize(world_up_view.xyz))) - 0.72f) * 5.0f);
		if (floor_mask > 0.001f)
		{
			float strength = ssr_parameters.x;
			float max_distance = ssr_parameters.y;
			float thickness = max(ssr_parameters.z, 0.001f);
			float stride = max(ssr_parameters.w, 0.02f);
			// 反射床は平面なので、深度差分の不安定な推定法線ではなく正確な上方向を使う。
			float3 reflection_normal = normalize(world_up_view.xyz);
			if (dot(reflection_normal, -view_direction) < 0.0f)
			{
				reflection_normal = -reflection_normal;
			}
			float3 reflected_direction = normalize(reflect(view_direction, reflection_normal));
			// 交差許容幅のthicknessを開始位置に流用すると、反射面が床より大きく浮いて像が縦に伸びる。
			// 自己交差を避けるための開始オフセットは数センチ相当に制限する。
			float origin_offset = min(thickness * 0.05f, 0.03f);
			float3 ray_position = view_position + reflection_normal * origin_offset;
			float travelled = 0.0f;

			// Shader III Unit08と同じ方式で視点空間を進み、深度バッファを越えた画素を反射点とする。
			[loop]
			// 細い手足にも当たりやすい40ステップとし、アニメーション中の反射欠けを抑える。
			for (int iteration = 0; iteration < 64; ++iteration)
			{
				float3 previous_ray_position = ray_position;
				ray_position += reflected_direction * stride;
				travelled += stride;
				if (travelled > max_distance || ray_position.z <= 0.0f)
				{
					break;
				}

				float2 ray_uv = project_view_position(ray_position);
				if (any(ray_uv <= 0.001f) || any(ray_uv >= 0.999f))
				{
					break;
				}

				float sampled_depth = depth_map.SampleLevel(sampler_states[POINT_SAMPLER], ray_uv, 0.0f).r;
				if (sampled_depth < 0.9999f)
				{
					float3 sampled_position = reconstruct_view_position(ray_uv, sampled_depth);
					float depth_difference = ray_position.z - sampled_position.z;
					if (depth_difference > 0.0f && depth_difference < thickness)
					{
						// 交差した1ステップを二分探索し、粗いレイ間隔による縦筋を減らす。
						float3 near_position = previous_ray_position;
						float3 far_position = ray_position;
						[unroll]
						for (int refinement = 0; refinement < 4; ++refinement)
						{
							float3 middle_position = (near_position + far_position) * 0.5f;
							float2 middle_uv = project_view_position(middle_position);
							float middle_depth = depth_map.SampleLevel(sampler_states[POINT_SAMPLER], middle_uv, 0.0f).r;
							float3 middle_surface = reconstruct_view_position(middle_uv, middle_depth);
							if (middle_position.z > middle_surface.z)
							{
								far_position = middle_position;
							}
							else
							{
								near_position = middle_position;
							}
						}
						ray_uv = project_view_position(far_position);
						float2 edge_distance = abs(ray_uv * 2.0f - 1.0f);
						float edge_fade = saturate(1.0f - max(edge_distance.x, edge_distance.y));
						float distance_fade = saturate(1.0f - travelled / max_distance);
						float fresnel = 0.60f + 0.40f * pow(1.0f - saturate(dot(-view_direction, reflection_normal)), 5.0f);
						float reflection_amount = strength * floor_mask * edge_fade * distance_fade * fresnel;
						reflection_result = lerp(surface_color, sample_scene(ray_uv).rgb, saturate(reflection_amount));
						break;
					}
				}
			}
		}
	}
	// alphaには床判定を返し、呼び出し側で床の反射だけを平滑化できるようにする。
	return float4(reflection_result, floor_mask);
}

float4 main(VS_OUT input) : SV_TARGET
{
	uint width;
	uint height;
	scene_map.GetDimensions(width, height);
	float2 texel_size = rcp(float2(width, height));
	const bool split_comparison = (enabled_effects & EFFECT_SPLIT_COMPARISON) != 0;
	const bool effects_side = !split_comparison || input.texcoord.x >= 0.5f;
	// 比較時は左右それぞれへシーン全体を縮め、同じ場所を加工前・加工後で見比べられるようにする。
	float2 uv = input.texcoord;
	if (split_comparison)
	{
		uv.x = frac(input.texcoord.x * 2.0f);
	}
	const float2 original_uv = uv;
	float4 original_source = sample_scene(original_uv);
	if (!effects_side)
	{
		return original_source;
	}

	if ((enabled_effects & EFFECT_FLOW_DISTORTION) != 0)
	{
		uv = apply_flow_distortion(uv);
	}

	float4 source = sample_scene(uv);
	float3 color = source.rgb;
	if ((enabled_effects & EFFECT_DEPTH_OF_FIELD) != 0)
	{
		color = sample_depth_of_field(uv, color, texel_size);
	}
	if ((enabled_effects & EFFECT_SCREEN_SPACE_REFLECTION) != 0)
	{
		float4 center_reflection = apply_screen_space_reflection(uv, texel_size, color);
		color = center_reflection.rgb;
	}
	if ((enabled_effects & EFFECT_RADIAL_BLUR) != 0)
	{
		color = sample_radial_blur(uv, color);
	}
	if ((enabled_effects & EFFECT_CHROMATIC_ABERRATION) != 0)
	{
		// III Unit06の倍率色収差として、画面中心から外側ほどRGBの読取り位置を分離する。
		float2 radial = uv - 0.5f;
		float2 offset = radial * dot(radial, radial) * chromatic_strength;
		color.r = sample_scene(uv + offset).r;
		color.b = sample_scene(uv - offset).b;
	}
	if ((enabled_effects & EFFECT_FXAA) != 0)
	{
		color = sample_fxaa(uv, texel_size, color);
	}
	if ((enabled_effects & EFFECT_VIGNETTE) != 0)
	{
		// III Unit05の周辺減光。中央は維持し、画面端を滑らかに暗くする。
		float2 centered = (uv - 0.5f) * float2((float)width / height, 1.0f);
		float vignette = 1.0f - smoothstep(
			max(0.0f, 1.0f - vignette_smoothness),
			1.0f,
			length(centered) * vignette_strength);
		color *= vignette;
	}

	// Before/Afterの境界へ細い白線を引き、比較領域がすぐ分かるようにする。
	if (split_comparison && abs(input.texcoord.x - 0.5f) < texel_size.x * 1.5f)
	{
		color = 0.85f.xxx;
	}
	return float4(color, source.a);
}
