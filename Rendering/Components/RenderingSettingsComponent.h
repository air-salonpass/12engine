#pragma once

// Renderer全体へ適用するScene保存可能な設定。
struct rendering_settings_component
{
	bool vsync = true;
	// 0は上限なし。VSyncが有効な前景表示では二重待機を避け、自動的に使用しない。
	int frame_rate_limit = 0;
	// 他のアプリを操作している間はGPUを休ませ、ノートPCの発熱と消費電力を抑える。
	int background_frame_rate = 10;
	bool ground_grid = false;
	bool model_bounds = false;
	bool backdrop = false;
	// 対応GPUではMeshletをDispatchMeshで描画し、非対応GPUでは自動的に通常描画へ戻る。
	bool mesh_shaders = true;
	// 既定は画像不要の手続き環境光。0より大きくした場合だけ任意のDDS環境画像を読み込む。
	float ibl_intensity = 0.0f;
};
