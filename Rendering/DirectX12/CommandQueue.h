#pragma once

#include <atomic>
#include <cstdint>
#include <d3d12.h>
#include <mutex>
#include <wrl.h>

// Direct/Compute/Copyの各キューが独立したFenceを持つ。
// CPUスレッドの停止状態とGPU Fence値は別物なので混同しないこと。
class command_queue
{
public:
	command_queue() = default;
	~command_queue();
	bool initialize(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, const wchar_t* name);
	std::uint64_t execute(ID3D12CommandList* const* lists, UINT count);
	bool wait(std::uint64_t fence_value);
	void wait_idle();

	ID3D12CommandQueue* get() const { return queue_.Get(); }
	ID3D12Fence* fence() const { return fence_.Get(); }
	std::uint64_t completed_value() const { return fence_ ? fence_->GetCompletedValue() : 0; }

private:
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
	HANDLE fence_event_ = nullptr;
	std::atomic<std::uint64_t> next_fence_value_{ 1 };
	// 同じFence Eventを複数CPUスレッドが同時利用しないよう、待機区間だけを直列化する。
	std::mutex wait_mutex_;
	std::mutex submission_mutex_;
};
