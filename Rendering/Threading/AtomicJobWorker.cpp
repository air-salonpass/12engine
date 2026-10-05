#include "AtomicJobWorker.h"

#include <utility>

atomic_job_worker::atomic_job_worker()
	: thread_([this] { run(); })
{
}

atomic_job_worker::~atomic_job_worker()
{
	stop();
}

bool atomic_job_worker::submit(job_type job)
{
	if (!job || stop_requested_.load(std::memory_order_acquire))
	{
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(job_mutex_);
		// 既存ジョブを上書きしない。大量ロード時は上位のResourceManager側でキューイングする。
		if (job_pending_.load(std::memory_order_relaxed) || busy_.load(std::memory_order_acquire))
		{
			return false;
		}
		pending_job_ = std::move(job);
		job_pending_.store(true, std::memory_order_release);
	}
	wakeup_.notify_one();
	return true;
}

void atomic_job_worker::stop()
{
	bool expected = false;
	if (!stop_requested_.compare_exchange_strong(
		expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
	{
		if (thread_.joinable()) thread_.join();
		return;
	}
	wakeup_.notify_all();
	if (thread_.joinable()) thread_.join();
	running_.store(false, std::memory_order_release);
}

void atomic_job_worker::run()
{
	while (!stop_requested_.load(std::memory_order_acquire))
	{
		job_type job;
		{
			std::unique_lock<std::mutex> lock(job_mutex_);
			wakeup_.wait(lock, [this]
			{
				return stop_requested_.load(std::memory_order_acquire) ||
					job_pending_.load(std::memory_order_acquire);
			});
			if (stop_requested_.load(std::memory_order_acquire)) break;
			job = std::move(pending_job_);
			pending_job_ = {};
			job_pending_.store(false, std::memory_order_release);
			busy_.store(true, std::memory_order_release);
		}
		try
		{
			job();
		}
		catch (...)
		{
			// 例外はジョブ側で結果へ変換する。ワーカースレッドを越えて送出しない。
		}
		busy_.store(false, std::memory_order_release);
	}
	busy_.store(false, std::memory_order_release);
	running_.store(false, std::memory_order_release);
}
