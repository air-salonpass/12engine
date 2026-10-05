#pragma once

// 再生位置をEntityごとに独立して保持する。計算済みPoseは描画Backend側のSystemが管理する。
struct animation_component
{
	int clip = 0;
	float time = 0.0f;
	bool playing = true;
	bool loop = true;
	float speed = 1.0f;
};
