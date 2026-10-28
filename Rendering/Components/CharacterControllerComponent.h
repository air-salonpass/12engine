#pragma once

// プレイヤーやNPCをRigidBodyの回転から独立して移動させるComponent。
// ゲームSystemはdesired_velocityへXZ移動速度を設定し、Physics Systemが衝突を解決する。
struct character_controller_component
{
	float desired_velocity[3]{ 0.0f, 0.0f, 0.0f };
	float vertical_velocity = 0.0f;
	float gravity = 19.6f;
	float jump_speed = 7.0f;
	float slope_limit_degrees = 50.0f;
	// 接地中に登れるBox地形の最大段差(m)。0で無効。頭上が塞がる場合やジャンプ中は登らない。
    float step_height = 0.3f;
	bool grounded = false;
	bool enabled = true;
};
