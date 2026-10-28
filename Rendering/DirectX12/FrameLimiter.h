#pragma once

#include <chrono>
#include <cstdint>
#include <windows.h>

// VSyncとは独立したCPU側のフレーム上限制御。
// 通常のSleepだけではWindowsのタイマー粒度で寝過ぎる場合があるため、
// 高精度Waitable Timerで目標直前まで休み、最後の短時間だけyieldする。
class frame_limiter12
{
public:
	frame_limiter12();
	~frame_limiter12();
	frame_limiter12(const frame_limiter12&) = delete;
	frame_limiter12& operator=(const frame_limiter12&) = delete;

	// frames_per_secondが0なら上限なし。VSync中は二重待機を避けるため0を渡す。
	void wait(std::uint32_t frames_per_second);
	void reset();
	bool high_resolution_timer_available() const noexcept { return wait_timer_ != nullptr; }

private:
	using clock = std::chrono::steady_clock;
	HANDLE wait_timer_ = nullptr;
	clock::time_point next_frame_{};
	bool scheduled_ = false;
};
