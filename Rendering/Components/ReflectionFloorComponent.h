#pragma once

// 反射床とSSRの調整値を一つの機能単位として保持する。
struct reflection_floor_component
{
	bool enabled = true;
	bool reflection_enabled = true;
	// 床では画面外の情報も映せる平面反射を標準とし、OFF時は画面内の色と深度から反射を求めるSSRを使う。
	bool planar_reflection = true;
	float strength = 1.10f;
	float max_distance = 15.0f;
	float thickness = 0.50f;
	float stride = 0.25f;
	float height = 0.0f;
	float color[3]{ 0.12f, 0.15f, 0.20f };
	int steps = 48;
};
