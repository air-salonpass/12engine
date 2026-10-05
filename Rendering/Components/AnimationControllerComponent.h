#pragma once

#include <string>
#include <vector>

struct animation_controller_state
{
	std::string name;
	int clip = 0;
	float speed = 1.0f;
	bool loop = true;
};

// ゲームロジックはrequested_stateだけを変更し、実際のClip切替はAnimation Systemが行う。
// 攻撃・待機・移動などの状態判定と描画用Animation処理を分離できる。
struct animation_controller_component
{
	bool enabled = true;
	int current_state = 0;
	int requested_state = 0;
	std::vector<animation_controller_state> states;
};
