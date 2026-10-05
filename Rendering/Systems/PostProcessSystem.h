#pragma once

#include "SceneSystem.h"

// 時間変化するPost Processの累積時間だけを更新するSystem。
class post_process_system final : public scene_system
{
public:
	void update(scene_registry& registry, scene_system_context& context,
		float delta_seconds) override;
};
