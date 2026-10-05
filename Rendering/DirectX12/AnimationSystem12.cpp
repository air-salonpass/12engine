#include "AnimationSystem12.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

void animation_system12::update(scene_registry& registry,
	model_resource_manager12& resources, float delta_seconds)
{
	registry.for_each<animation_component, animation_controller_component>(
		[](scene_entity, animation_component& animation, animation_controller_component& controller)
		{
			if (!controller.enabled || controller.states.empty()) return;
			controller.requested_state = (std::clamp)(controller.requested_state, 0,
				static_cast<int>(controller.states.size()) - 1);
			if (controller.current_state != controller.requested_state)
			{
				controller.current_state = controller.requested_state;
				animation.time = 0.0f;
			}
			const auto& state = controller.states[controller.current_state];
			animation.clip = state.clip;
			animation.speed = state.speed;
			animation.loop = state.loop;
		});

	std::unordered_set<scene_entity> alive;
	registry.for_each<model_renderer_component, animation_component>(
		[&](scene_entity entity, model_renderer_component& renderer, animation_component& state)
		{
			alive.insert(entity);
			auto model = resources.find(renderer.asset_path);
			if (!model || model->animations().empty())
			{
				poses_.erase(entity);
				return;
			}

			state.clip = (std::clamp)(state.clip, 0,
				static_cast<int>(model->animations().size()) - 1);
			const auto& clip = model->animations()[static_cast<std::size_t>(state.clip)];
			if (state.playing) state.time += delta_seconds * state.speed;
			if (clip.duration > 0.0f && state.time >= clip.duration)
			{
				if (state.loop) state.time = std::fmod(state.time, clip.duration);
				else { state.time = clip.duration; state.playing = false; }
			}
			model->sample_animation(static_cast<std::size_t>(state.clip), state.time,
				state.loop, poses_[entity]);
		});

	// Scene削除済みEntityの大きなPose配列をフレーム境界で回収する。
	for (auto iterator = poses_.begin(); iterator != poses_.end();)
		if (alive.find(iterator->first) == alive.end()) iterator = poses_.erase(iterator);
		else ++iterator;
}

const std::vector<gltf_model12::node>* animation_system12::pose(scene_entity entity) const
{
	const auto found = poses_.find(entity);
	return found == poses_.end() ? nullptr : &found->second;
}

void animation_system12::clear()
{
	poses_.clear();
}
