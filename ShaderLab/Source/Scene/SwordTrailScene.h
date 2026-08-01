#pragma once

#include <memory>
#include "Scene.h"
#include "Camera.h"
#include "Light.h"
#include "Model.h"

// Œ•‚Ì‹OÕˆ—ƒV[ƒ“
class SwordTrailScene : public Scene
{
public:
	SwordTrailScene();
	~SwordTrailScene() override = default;

	// XVˆ—
	void Update(float elapsedTime) override;

	// •`‰æˆ—
	void Render(float elapsedTime) override;

	// GUI•`‰æˆ—
	void DrawGUI() override;

private:
	Camera								camera;
	LightManager						lightManager;
	std::shared_ptr<Model>				character;
	std::shared_ptr<Model>				weapon;
	std::vector<Model::NodePose>		nodePoses;
	float								animationSeconds = 0;
	bool								spline = false;

	static const int MAX_POLYGON = 16;
	DirectX::XMFLOAT3					trailPositions[2][MAX_POLYGON];
};
