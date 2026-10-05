#pragma once

// Bloom、FXAA、DOFなど、最終画面へ適用する設定データ。
struct post_process_component
{
	float effect_time = 0.0f;
	float luminance_threshold = 0.8f;
	float gaussian_sigma = 1.0f;
	float bloom_intensity = 1.0f;
	float exposure = 1.2f;
	bool flow_enabled = false;
	bool dof_enabled = false;
	bool vignette_enabled = false;
	bool chromatic_enabled = false;
	bool radial_blur_enabled = false;
	bool fxaa_enabled = false;
	bool demo_background_enabled = false;
	bool split_comparison_enabled = false;
	float flow_strength = 0.35f;
	float flow_speed = 1.0f;
	float chromatic_strength = 0.025f;
	float vignette_strength = 1.15f;
	float vignette_smoothness = 0.45f;
	float radial_strength = 0.05f;
	float radial_center[2]{ 0.5f, 0.5f };
	float radial_radius = 0.15f;
	float dof_focus_distance = 0.065f;
	float dof_range = 0.04f;
	float dof_blur_size = 3.0f;
	float fxaa_strength = 0.65f;
};
