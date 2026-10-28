#include "FrameLimiter.h"

#include <algorithm>
#include <thread>

frame_limiter12::frame_limiter12()
{
	// Windows 10 1803以降では高精度タイマーを利用できる。
	// 作成できない環境でもsleep_forへ自動的にフォールバックする。
	wait_timer_ = CreateWaitableTimerExW(
		nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
}

frame_limiter12::~frame_limiter12()
{
	if (wait_timer_) CloseHandle(wait_timer_);
}

void frame_limiter12::reset()
{
	scheduled_ = false;
	next_frame_ = {};
}

void frame_limiter12::wait(std::uint32_t frames_per_second)
{
	if (frames_per_second == 0)
	{
		reset();
		return;
	}

	// 異常な入力で0に近い周期を作らないよう、Editor設定の上限もここで防御する。
	frames_per_second = (std::clamp)(frames_per_second, 1u, 1000u);
	const auto interval = std::chrono::duration_cast<clock::duration>(
		std::chrono::duration<double>(1.0 / static_cast<double>(frames_per_second)));
	const auto now = clock::now();
	if (!scheduled_ || now > next_frame_ + interval)
	{
		next_frame_ = now + interval;
		scheduled_ = true;
	}
	else
	{
		next_frame_ += interval;
	}

	// タイマーの誤差を吸収するため、最後の0.25msは短いyield待機にする。
	constexpr auto spin_margin = std::chrono::microseconds(250);
	const auto before_sleep = clock::now();
	if (next_frame_ - before_sleep > spin_margin)
	{
		const auto sleep_duration = next_frame_ - before_sleep - spin_margin;
		bool waited = false;
		if (wait_timer_)
		{
			LARGE_INTEGER due_time{};
			const auto units_100ns = (std::max)(std::int64_t{ 1 },
				std::chrono::duration_cast<std::chrono::nanoseconds>(sleep_duration).count() / 100);
			due_time.QuadPart = -units_100ns;
			if (SetWaitableTimer(wait_timer_, &due_time, 0, nullptr, nullptr, FALSE))
			{
				WaitForSingleObject(wait_timer_, INFINITE);
				waited = true;
			}
		}
		if (!waited) std::this_thread::sleep_for(sleep_duration);
	}
	while (clock::now() < next_frame_) std::this_thread::yield();
}
