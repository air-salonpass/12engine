#pragma once

#include <cstdint>

enum class collider_shape
{
	box,
	sphere,
	capsule
};

// 衝突形状だけを保持するComponent。GPU Resourceや衝突処理は持たない。
// layerは0～31、collision_maskの各bitは衝突を許可する相手Layerを表す。
struct collider_component
{
	collider_shape shape = collider_shape::box;
	float center[3]{ 0.0f, 0.0f, 0.0f };
	float half_extents[3]{ 0.5f, 0.5f, 0.5f };
	float radius = 0.5f;
	float height = 2.0f;
	std::uint32_t layer = 0;
	std::uint32_t collision_mask = 0xffffffffu;
	bool trigger = false;
	bool enabled = true;
};
