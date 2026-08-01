#pragma once

#include <memory>
#include <DirectXCollision.h>
#include "Scene.h"
#include "Camera.h"
#include "FreeCameraController.h"
#include "HighResolutionTimer.h"
#include "Model.h"

// スフィアキャスト移動シーン
class SphereCastMoveScene : public Scene
{
public:
	SphereCastMoveScene();
	~SphereCastMoveScene() override = default;

	// 更新処理
	void Update(float elapsedTime) override;

	// 描画処理
	void Render(float elapsedTime) override;

	// GUI描画処理
	void DrawGUI() override;

private:
	// スフィアキャスト
	bool SphereCast(
		const DirectX::XMFLOAT3& origin,
		const DirectX::XMFLOAT3& direction,
		float radius,
		float& distance,
		DirectX::XMFLOAT3& hitPosition,
		DirectX::XMFLOAT3& hitNormal);

	// 移動＆滑り
	void MoveAndSlide(
		const DirectX::XMFLOAT3& move,
		bool vertical);

private:
	struct CollisionMesh
	{
		struct Triangle
		{
			DirectX::XMFLOAT3	positions[3];
			DirectX::XMFLOAT3	normal;
		};
		std::vector<Triangle>	triangles;
	};

private:
	HighResolutionTimer					timer;
	Camera								camera;
	FreeCameraController				cameraController;
	std::shared_ptr<Model>				stage;
	std::shared_ptr<Model>				character;
	DirectX::XMFLOAT3					position = { 0, 0, 0 };
	float								radius = 0.5f;
	float								skinWidth = 0.01f;
	float								stepOffset = 0.1f;
	float								slopeLimit = 45.0f;
	CollisionMesh						collisionMesh;
	LightManager						lightManager;
};
