#pragma once

#include <memory>
#include "Scene.h"
#include "Camera.h"
#include "FreeCameraController.h"

// 地形コリジョンシーン
class SphereVsTriangleCollisionScene : public Scene
{
public:
	SphereVsTriangleCollisionScene();
	~SphereVsTriangleCollisionScene() override = default;

	// 更新処理
	void Update(float elapsedTime) override;

	// 描画処理
	void Render(float elapsedTime) override;

	// GUI描画処理
	void DrawGUI() override;

private:
	// 球と三角形との交差を判定する
	static bool SphereIntersectTriangle(
		const DirectX::XMFLOAT3& sphereCenter,
		const float sphereRadius,
		const DirectX::XMFLOAT3& triangleVertexA,
		const DirectX::XMFLOAT3& triangleVertexB,
		const DirectX::XMFLOAT3& triangleVertexC,
		DirectX::XMFLOAT3& hitPosition,
		DirectX::XMFLOAT3& hitNormal);

private:
	struct Object
	{
		DirectX::XMFLOAT3	position = { 0, 0, 0 };
		float				radius = 0.5f;
		float				weight = 0.5f;
		DirectX::XMFLOAT4	color = { 1, 1, 0, 1 };
	};


	Camera								camera;
	FreeCameraController				cameraController;
	Object								obj;
};
