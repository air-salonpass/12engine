#pragma once

// 描画方式に依存しない、Entityの位置・回転・一様スケール。
struct transform_component
{
	float position[3]{ 0.0f, 0.0f, 0.0f };
	float rotation[3]{ 0.0f, 0.0f, 0.0f };
	float scale = 1.0f;
	// D3D12 EditorではGizmoの非一様スケールを保持する。scaleは旧Scene互換用に残す。
	float scale3[3]{ 1.0f, 1.0f, 1.0f };
};
