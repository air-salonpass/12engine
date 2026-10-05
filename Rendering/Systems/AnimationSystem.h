#pragma once

#include "SceneSystem.h"

// AnimationとModel Rendererを併せ持つEntityの姿勢を更新するSystem。
class animation_system final : public scene_system
{
public:
	void update(scene_registry& registry, scene_system_context& context,
		float delta_seconds) override;
};
