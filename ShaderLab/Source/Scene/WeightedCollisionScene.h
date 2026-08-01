#pragma once

#include <memory>
#include "Scene.h"
#include "Camera.h"
#include "FreeCameraController.h"

// 重みのある衝突処理シーン
class WeightedCollisionScene : public Scene
{
public:
	WeightedCollisionScene();
	~WeightedCollisionScene() override = default;

	// 更新処理
	void Update(float elapsedTime) override;

	// 描画処理
	void Render(float elapsedTime) override;

	// GUI描画処理
	void DrawGUI() override;

private:
	struct Object
	{
		DirectX::XMFLOAT3	position = { 0, 0, 0 };
		float				radius = 0.5f;
		float				weight = 5.0f;
		DirectX::XMFLOAT4	color = { 1, 1, 0, 1 };
	};

	Camera								camera;
	FreeCameraController				cameraController;
	Object								objs[2];
};
