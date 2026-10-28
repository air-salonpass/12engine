#pragma once

#include <DirectXMath.h>
#include "../Components/TransformComponent.h"

// Map Editorが画面上のMouse位置から作るワールド空間Ray。
// 配置先が床平面でもColliderでも同じRayを使い、ジャンル固有処理を持ち込まない。
struct map_editor_ray
{
	float origin[3]{};
	float direction[3]{ 0.0f, -1.0f, 0.0f };
};

struct map_editor_settings
{
	bool enabled = false;
	bool place_on_collider = false;
	bool snap_to_grid = true;
	bool add_box_collider = false;
	float grid_size = 1.0f;
	float plane_height = 0.0f;
	float rotation_y_degrees = 0.0f;
	float uniform_scale = 1.0f;
	float rotation_step = 15.0f;
	float scale_step = 0.1f;
	bool debug_lines = true;
};

// Editor専用の座標計算System。Scene生成やAsset読込みは行わず、配置座標だけを計算する。
// これによりRendererやPhysicsへ「Map編集モード」の分岐を増やさない。
class map_editor_system
{
public:
	map_editor_settings& settings() noexcept { return settings_; }
	const map_editor_settings& settings() const noexcept { return settings_; }

	bool make_ray(float mouse_x, float mouse_y, float view_x, float view_y,
		float view_width, float view_height, const DirectX::XMFLOAT4X4& view,
		const DirectX::XMFLOAT4X4& projection, map_editor_ray& ray) const;
	bool intersect_plane(const map_editor_ray& ray, float height, float position[3]) const;
	void snap(float position[3], bool temporary = false) const;
	bool snapping(bool temporary = false) const noexcept { return settings_.snap_to_grid || temporary; }
	// UI入力・Scene読込みのどちらから来ても、0や非数で座標計算を壊さないようそろえる。
	void sanitize();
	// 確定配置とプレビューで同じ原点補正を使う。SceneへEntityを作らずに見え方を計算できる。
	static transform_component model_transform(const float position[3],float bounds_min_y,float yaw,float scale);

private:
	map_editor_settings settings_{};
};
