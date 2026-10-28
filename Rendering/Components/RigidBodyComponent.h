#pragma once

enum class rigid_body_type
{
	static_body,
	kinematic_body,
	dynamic_body
};

// 物理運動の状態。Transformは最終的な位置、RigidBodyは速度と物性を担当する。
// staticは動かない地形、kinematicはゲームコードで動かす床、dynamicは力で動く物体に使う。
struct rigid_body_component
{
	rigid_body_type type = rigid_body_type::dynamic_body;
	float mass = 1.0f;
	float velocity[3]{ 0.0f, 0.0f, 0.0f };
	float gravity_scale = 1.0f;
	float restitution = 0.0f;
	float friction = 0.6f;
	bool freeze_position[3]{ false, false, false };
	bool enabled = true;
};
