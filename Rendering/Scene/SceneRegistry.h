#pragma once

#include "Entity.h"
#include "../Components/AnimationComponent.h"
#include "../Components/AnimationControllerComponent.h"
#include "../Components/CameraComponent.h"
#include "../Components/DirectionalLightComponent.h"
#include "../Components/ModelRendererComponent.h"
#include "../Components/NameComponent.h"
#include "../Components/PostProcessComponent.h"
#include "../Components/PrefabComponent.h"
#include "../Components/ReflectionFloorComponent.h"
#include "../Components/RenderingSettingsComponent.h"
#include "../Components/TransformComponent.h"

#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// Entity IDと型別Component Storageだけを管理する。ゲーム処理はSystemsへ置く。
//
// 【チーム実装時の入口】
// 1. 新しい状態は Components/○○Component.h に「データだけ」を定義する。
// 2. scene.add<○○_component>(entity) でEntityへ付与する。
// 3. 毎フレームの処理はSystemへ置き、for_eachで必要なComponentだけを列挙する。
// 4. Sceneへ保存する値はNativePreview.cppの保存・読込みを必ず同時に追加する。
//
// Registry自体はスレッドセーフではない。ゲーム状態の追加・削除・更新はMain Threadの
// フレーム境界で行い、Workerからは完了結果だけをキュー経由で受け取ること。
class scene_registry
{
public:
	scene_registry() = default;
	scene_registry(const scene_registry& other) { *this = other; }
	scene_registry& operator=(const scene_registry& other)
	{
		if (this == &other) return *this;
		next_entity_ = other.next_entity_;
		entities_ = other.entities_;
		storages_.clear();
		for (const auto& [type, storage] : other.storages_) storages_[type] = storage->clone();
		return *this;
	}
	scene_registry(scene_registry&&) noexcept = default;
	scene_registry& operator=(scene_registry&&) noexcept = default;

	scene_entity create_entity(const std::string& name)
	{
		const scene_entity entity = next_entity_++;
		entities_.push_back(entity);
		add<name_component>(entity) = name_component{ name };
		return entity;
	}

	scene_entity create_model_entity(const std::string& name, std::size_t model_slot, bool animated)
	{
		const scene_entity entity = create_entity(name);
		add<transform_component>(entity);
		add<model_renderer_component>(entity) = model_renderer_component{ model_slot, true, {} };
		if (animated) add<animation_component>(entity);
		return entity;
	}

	scene_entity duplicate(scene_entity source)
	{
		if (!valid(source)) return null_scene_entity;
		const scene_entity entity = next_entity_++;
		entities_.push_back(entity);
		const name_component* source_name = get<name_component>(source);
		const std::string duplicate_name = make_duplicate_name(source_name ? source_name->value : "Entity");
		for (auto& [type, storage] : storages_) storage->duplicate(source, entity);
		add<name_component>(entity).value = duplicate_name;
		return entity;
	}

	template<class Component>
	// 型ごとのStorageは初回add時に自動生成されるため、Registry本体への型登録は不要。
	// ただしInspectorとJSON保存は自動ではないので、新規Component追加時に別途実装する。
	Component& add(scene_entity entity) { return assure_storage<Component>().values[entity]; }
	camera_component& add_camera(scene_entity entity) { return add<camera_component>(entity); }
	transform_component& add_transform(scene_entity entity) { return add<transform_component>(entity); }
	model_renderer_component& add_model_renderer(scene_entity entity) { return add<model_renderer_component>(entity); }
	animation_component& add_animation(scene_entity entity) { return add<animation_component>(entity); }
	animation_controller_component& add_animation_controller(scene_entity entity) { return add<animation_controller_component>(entity); }
	directional_light_component& add_directional_light(scene_entity entity) { return add<directional_light_component>(entity); }
	reflection_floor_component& add_reflection_floor(scene_entity entity) { return add<reflection_floor_component>(entity); }
	post_process_component& add_post_process(scene_entity entity) { return add<post_process_component>(entity); }
	rendering_settings_component& add_rendering_settings(scene_entity entity) { return add<rendering_settings_component>(entity); }
	prefab_component& add_prefab(scene_entity entity) { return add<prefab_component>(entity); }

	void destroy(scene_entity entity)
	{
		entities_.erase(std::remove(entities_.begin(), entities_.end(), entity), entities_.end());
		for (auto& [type, storage] : storages_) storage->erase(entity);
	}

	void clear()
	{
		entities_.clear();
		storages_.clear();
		next_entity_ = 1;
	}

	bool valid(scene_entity entity) const
	{
		return entity != null_scene_entity &&
			std::find(entities_.begin(), entities_.end(), entity) != entities_.end();
	}

	const std::vector<scene_entity>& entities() const { return entities_; }

	// 参照プロジェクトと同様に、指定したComponentを全て持つEntityだけをSystemへ渡す。
	// 例: for_each<transform_component, health_component>(...) と書くと、
	// 両方を持つEntityだけが対象になる。System側で全Entityを手作業検索しないこと。
	template<class... Components, class Function>
	void for_each(Function&& function)
	{
		static_assert(sizeof...(Components) > 0, "Componentを1種類以上指定してください。");
		for (const scene_entity entity : entities_)
		{
			auto components = std::tuple<Components*...>{ get<Components>(entity)... };
			const bool has_all = std::apply(
				[](auto*... component) { return (... && (component != nullptr)); }, components);
			if (has_all)
			{
				std::apply([&](auto*... component) { function(entity, *component...); }, components);
			}
		}
	}

	template<class Component>
	Component* get(scene_entity entity)
	{
		const auto found_storage = storages_.find(std::type_index(typeid(Component)));
		if (found_storage == storages_.end()) return nullptr;
		auto* storage = static_cast<component_storage<Component>*>(found_storage->second.get());
		const auto found = storage->values.find(entity);
		return found == storage->values.end() ? nullptr : &found->second;
	}

	// 既存のEditor・Serializerコードを読みやすく保つ型別アクセサ。
	name_component* name(scene_entity entity) { return get<name_component>(entity); }
	transform_component* transform(scene_entity entity) { return get<transform_component>(entity); }
	model_renderer_component* renderer(scene_entity entity) { return get<model_renderer_component>(entity); }
	animation_component* animation(scene_entity entity) { return get<animation_component>(entity); }
	animation_controller_component* animation_controller(scene_entity entity) { return get<animation_controller_component>(entity); }
	camera_component* camera(scene_entity entity) { return get<camera_component>(entity); }
	directional_light_component* directional_light(scene_entity entity) { return get<directional_light_component>(entity); }
	reflection_floor_component* reflection_floor(scene_entity entity) { return get<reflection_floor_component>(entity); }
	post_process_component* post_process(scene_entity entity) { return get<post_process_component>(entity); }
	rendering_settings_component* rendering_settings(scene_entity entity) { return get<rendering_settings_component>(entity); }
	prefab_component* prefab(scene_entity entity) { return get<prefab_component>(entity); }

private:
	struct component_storage_base
	{
		virtual ~component_storage_base() = default;
		virtual void erase(scene_entity entity) = 0;
		virtual void duplicate(scene_entity source, scene_entity destination) = 0;
		virtual std::unique_ptr<component_storage_base> clone() const = 0;
	};

	template<class Component>
	struct component_storage final : component_storage_base
	{
		// Componentが通常のコピーを行える型なら、複製とUndo/Redo Snapshotへ自動参加する。
		// GPU Resourceや生ポインターはComponentへ直接持たせず、Asset ID/Entity IDを保存する。
		std::unordered_map<scene_entity, Component> values;
		void erase(scene_entity entity) override { values.erase(entity); }
		void duplicate(scene_entity source, scene_entity destination) override
		{
			const auto found = values.find(source);
			if (found != values.end()) values[destination] = found->second;
		}
		std::unique_ptr<component_storage_base> clone() const override
		{
			return std::make_unique<component_storage<Component>>(*this);
		}
	};

	template<class Component>
	component_storage<Component>& assure_storage()
	{
		const std::type_index type(typeid(Component));
		auto found = storages_.find(type);
		if (found == storages_.end())
			found = storages_.emplace(type, std::make_unique<component_storage<Component>>()).first;
		return *static_cast<component_storage<Component>*>(found->second.get());
	}

	std::string make_duplicate_name(const std::string& source) const
	{
		std::string candidate = source + " (Copy)";
		int suffix = 2;
		const auto exists = [this](const std::string& value)
		{
			for (const scene_entity entity : entities_)
				if (const auto* name = const_cast<scene_registry*>(this)->get<name_component>(entity);
					name && name->value == value) return true;
			return false;
		};
		while (exists(candidate))
		{
			candidate = source + " (Copy " + std::to_string(suffix++) + ")";
		}
		return candidate;
	}

	scene_entity next_entity_ = 1;
	std::vector<scene_entity> entities_;
	std::unordered_map<std::type_index, std::unique_ptr<component_storage_base>> storages_;
};
