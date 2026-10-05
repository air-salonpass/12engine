#pragma once

#include "GltfModel.h"
#include "ModelResourceManager.h"
#include "../Scene/SceneRegistry.h"

#include <unordered_map>
#include <vector>

// D3D12用Animation System。
// Asset本体を変更せずEntity IDごとにPoseを保持するため、同一モデルの複製も別々に再生できる。
// ゲーム側は攻撃・待機・移動などの判定結果をrequested_stateへ設定するだけにし、
// clip/time/poseの更新はこのSystemへ任せる。描画コードから再生時間を進めないこと。
class animation_system12
{
public:
	void update(scene_registry& registry, model_resource_manager12& resources, float delta_seconds);
	const std::vector<gltf_model12::node>* pose(scene_entity entity) const;
	void clear();

private:
	std::unordered_map<scene_entity, std::vector<gltf_model12::node>> poses_;
};
