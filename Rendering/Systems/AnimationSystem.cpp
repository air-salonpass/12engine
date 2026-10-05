#include "AnimationSystem.h"
#include "../Scene/SceneRegistry.h"
#include "../gltf_model.h"

#include <algorithm>
#include <cmath>

void animation_system::update(scene_registry& registry,
	scene_system_context& context, float delta_seconds)
{
	registry.for_each<model_renderer_component, animation_component>(
		[&](scene_entity, model_renderer_component& renderer, animation_component& state)
		{
			if (!context.models || renderer.model_slot >= context.model_count ||
				!context.models[renderer.model_slot])
			{
				return;
			}

			gltf_model& model = *context.models[renderer.model_slot];
			if (model.animations.empty()) return;

			state.clip = (std::clamp)(state.clip, 0,
				static_cast<int>(model.animations.size()) - 1);
			const gltf_model::animation& animation =
				model.animations.at(static_cast<std::size_t>(state.clip));

			if (state.playing) state.time += delta_seconds * state.speed;
			if (animation.duration > 0.0f && state.time >= animation.duration)
			{
				if (state.loop)
					state.time = std::fmod(state.time, animation.duration);
				else
				{
					state.time = animation.duration;
					state.playing = false;
				}
			}

			// 描画Systemが同じ姿勢を本体と反射へ再利用できるようComponentへ保存する。
			model.animate(static_cast<std::size_t>(state.clip), state.time, state.pose);
		});
}
