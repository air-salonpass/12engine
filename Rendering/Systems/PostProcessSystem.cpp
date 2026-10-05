#include "PostProcessSystem.h"
#include "../Scene/SceneRegistry.h"

void post_process_system::update(scene_registry& registry,
	scene_system_context&, float delta_seconds)
{
	registry.for_each<post_process_component>(
		[delta_seconds](scene_entity, post_process_component& post_process)
		{
			// System側に時間計算を集約し、Rendererは結果を読むだけにする。
			post_process.effect_time += delta_seconds;
		});
}
