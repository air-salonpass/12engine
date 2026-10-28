#pragma once

#include "../Scene/Entity.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

class scene_registry;

struct physics_contact
{
	scene_entity first = null_scene_entity;
	scene_entity second = null_scene_entity;
	float normal[3]{ 0.0f, 1.0f, 0.0f };
	float penetration = 0.0f;
	bool trigger = false;
};

struct physics_raycast_hit
{
	scene_entity entity = null_scene_entity;
	float point[3]{};
	float normal[3]{ 0.0f, 1.0f, 0.0f };
	float distance = 0.0f;
};

// UnityのOnCollisionEnter/Stay/Exit、OnTriggerEnter/Stay/Exitに相当する状態。
// ゲーム側は毎フレームevents()を走査し、phaseとtriggerを見て処理を分ける。
enum class physics_event_phase
{
	enter,
	stay,
	exit
};

struct physics_event
{
	physics_contact contact{};
	physics_event_phase phase = physics_event_phase::enter;
};

// 小～中規模のチーム制作向け固定時間Physics System。
// ゲームコードはComponentだけを変更し、衝突判定やTransform反映を直接実装しない。
class physics_system
{
public:
	void update(scene_registry& scene, float elapsed_seconds);
	void clear();
	const std::vector<physics_contact>& contacts() const noexcept { return contacts_; }
	const std::vector<physics_event>& events() const noexcept { return events_; }
	// 特定Entityに関係するイベントだけを取り出す。ゲームSystem側の簡単な実装用。
	std::vector<physics_event> events_for(scene_entity entity) const;
	// カメラ選択、攻撃判定、地面確認で共通利用する最短Hit検索。layer_maskは対象Layerのbit列。
	bool raycast(scene_registry& scene, const float origin[3], const float direction[3],
		float max_distance, unsigned int layer_mask, physics_raycast_hit& hit,
		scene_entity ignored_entity = null_scene_entity) const;
	// 爆発や範囲攻撃用。Triggerを含む重複しないEntity一覧を返す。
	std::vector<scene_entity> overlap_sphere(scene_registry& scene, const float center[3],
		float radius, unsigned int layer_mask) const;

private:
	void simulate(scene_registry& scene, float fixed_delta_seconds);
	void rebuild_events();
	static std::uint64_t make_contact_key(scene_entity first, scene_entity second) noexcept;
	float accumulator_ = 0.0f;
	std::vector<physics_contact> contacts_;
	std::vector<physics_event> events_;
	std::unordered_map<std::uint64_t, physics_contact> previous_contacts_;
};
