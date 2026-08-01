#pragma once

#include <memory>
#include "Scene.h"
#include "Camera.h"
#include "Light.h"
#include "Model.h"

// ルートモーション処理シーン
class RootMotionScene : public Scene
{
public:
	RootMotionScene();
	~RootMotionScene() override = default;

	// 更新処理
	void Update(float elapsedTime) override;

	// 描画処理
	void Render(float elapsedTime) override;

	// GUI描画処理
	void DrawGUI() override;

private:
	Camera								camera;
	LightManager						lightManager;
	std::shared_ptr<Model>				character;
	DirectX::XMFLOAT3					position = { 0, 0, 0 };
	DirectX::XMFLOAT3					angle = { 0, 0, 0 };
	DirectX::XMFLOAT3					scale = { 1, 1, 1 };
	DirectX::XMFLOAT4X4					worldTransform = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	std::vector<Model::NodePose>		nodePoses;
	int									animationIndex = -1;
	float								animationSeconds = 0;
	float								oldAnimationSeconds = 0;
	bool								animationLoop = false;
};
