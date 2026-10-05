#pragma once

// Sceneビューを制御するオービットカメラの状態。
struct camera_component
{
	float target[3]{ 0.0f, 0.8f, 0.0f };
	float distance = 6.5f;
	float fov_degrees = 40.0f;
	float wheel_zoom_speed = 0.5f;
	float yaw_degrees = 0.0f;
	float pitch_degrees = 0.0f;
	float orbit_sensitivity = 0.2f;
	bool active = true;
};
