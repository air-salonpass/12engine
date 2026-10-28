#pragma once

// 太陽の向き・色・強さと、空/地面から回り込む光を保持する。
// Directionは物体から光へ向かう方向。Intensityを上げるとHDRの明るさが増え、露出で表示を調整する。
struct directional_light_component
{
	float direction[3]{ 0.0f, 0.0f, 1.0f };
	bool enabled = true;
	float color[3]{ 1.0f, 0.91f, 0.78f };
	float intensity = 3.0f;
	float sky_color[3]{ 0.32f, 0.48f, 0.72f };
	float ground_color[3]{ 0.18f, 0.14f, 0.11f };
	float ambient_intensity = 0.65f;
	bool shadows = true;
	float shadow_strength = 0.85f;
	// カメラの注視点を中心とする影の範囲。広げるほど同じ解像度での輪郭が粗くなる。
	float shadow_distance = 32.0f;
	float shadow_bias = 0.0008f;
};
