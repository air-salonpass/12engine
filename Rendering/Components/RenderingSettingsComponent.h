#pragma once

// Renderer全体へ適用するScene保存可能な設定。
struct rendering_settings_component
{
	bool vsync = true;
	bool ground_grid = false;
	bool model_bounds = false;
	bool backdrop = false;
	// 対応GPUではMeshletをDispatchMeshで描画し、非対応GPUでは自動的に通常描画へ戻る。
	bool mesh_shaders = true;
	float ibl_intensity = 0.35f;
};
