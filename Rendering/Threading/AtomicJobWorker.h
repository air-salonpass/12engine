#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

// 1本の常駐スレッドで短い非同期ジョブを順番に処理する。
// スレッドの起動・停止・処理中状態はstd::atomicで公開し、ジョブ本体だけをmutexで受け渡す。
// submitは待たず、空きがない場合falseを返す。複数ジョブの順番管理は呼び出し側のQueueで行う。
// Job内からScene/ImGui/D3D12 Command Listを操作せず、結果をMain Threadへ返す用途に限定する。
class atomic_job_worker
{
public:
	using job_type = std::function<void()>;

	atomic_job_worker();
	~atomic_job_worker();
	atomic_job_worker(const atomic_job_worker&) = delete;
	atomic_job_worker& operator=(const atomic_job_worker&) = delete;

	bool submit(job_type job);
	void stop();
	bool busy() const noexcept { return busy_.load(std::memory_order_acquire); }
	bool running() const noexcept { return running_.load(std::memory_order_acquire); }

private:
	void run();

	std::thread thread_;
	std::mutex job_mutex_;
	std::condition_variable wakeup_;
	job_type pending_job_;
	// releaseで公開した状態を別スレッドがacquireで読むことで、前後のメモリ可視性を保証する。
	std::atomic<bool> running_{ true };
	std::atomic<bool> stop_requested_{ false };
	std::atomic<bool> job_pending_{ false };
	std::atomic<bool> busy_{ false };
};
