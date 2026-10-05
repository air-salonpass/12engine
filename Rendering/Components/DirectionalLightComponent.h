#pragma once

// Sceneで使用する平行光源の向きと有効状態。
struct directional_light_component
{
	float direction[3]{ 0.0f, 0.0f, 1.0f };
	bool enabled = true;
};
