#pragma once

#include <cstddef>
#include <memory>

class gltf_model;
class scene_registry;

// SystemがAssetへ安全にアクセスするための、1フレーム限定Context。
struct scene_system_context
{
	std::unique_ptr<gltf_model>* models = nullptr;
	std::size_t model_count = 0;
};

// 各Systemは必要なComponentだけを取得し、フレーム更新処理を実装する。
class scene_system
{
public:
	virtual ~scene_system() = default;
	virtual void update(scene_registry& registry, scene_system_context& context,
		float delta_seconds) = 0;
};
