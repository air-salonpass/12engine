#pragma once

#include "SceneSystem.h"
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

// Systemの登録順をそのまま実行順として管理する。
class scene_system_manager
{
public:
	template<class System, class... Arguments>
	System& add_system(Arguments&&... arguments)
	{
		static_assert(std::is_base_of_v<scene_system, System>,
			"Systemはscene_systemを継承してください。");
		auto system = std::make_unique<System>(std::forward<Arguments>(arguments)...);
		System* result = system.get();
		systems_.push_back(std::move(system));
		return *result;
	}

	void update(scene_registry& registry, scene_system_context& context, float delta_seconds)
	{
		for (const auto& system : systems_)
		{
			system->update(registry, context, delta_seconds);
		}
	}

	void clear() { systems_.clear(); }

private:
	std::vector<std::unique_ptr<scene_system>> systems_;
};
