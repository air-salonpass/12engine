#include "CommandQueue.h"

command_queue::~command_queue()
{
	wait_idle();
	if (fence_event_) CloseHandle(fence_event_);
}

bool command_queue::initialize(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, const wchar_t* name)
{
	D3D12_COMMAND_QUEUE_DESC desc{};
	desc.Type = type;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	if (FAILED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(queue_.GetAddressOf()))) ||
		FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.GetAddressOf()))))
	{
		return false;
	}
	queue_->SetName(name);
	fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	return fence_event_ != nullptr;
}

std::uint64_t command_queue::execute(ID3D12CommandList* const* lists, UINT count)
{
	if (!queue_ || !fence_ || !lists || count == 0) return 0;
	std::lock_guard<std::mutex> lock(submission_mutex_);
	queue_->ExecuteCommandLists(count, lists);
	const std::uint64_t value = next_fence_value_.fetch_add(1, std::memory_order_acq_rel);
	return SUCCEEDED(queue_->Signal(fence_.Get(), value)) ? value : 0;
}

bool command_queue::wait(std::uint64_t fence_value)
{
	if (fence_value == 0 || fence_->GetCompletedValue() >= fence_value) return true;
	std::lock_guard<std::mutex> lock(wait_mutex_);
	if (fence_->GetCompletedValue() >= fence_value) return true;
	if (FAILED(fence_->SetEventOnCompletion(fence_value, fence_event_))) return false;
	return WaitForSingleObject(fence_event_, INFINITE) == WAIT_OBJECT_0;
}

void command_queue::wait_idle()
{
	if (!queue_ || !fence_) return;
	std::uint64_t value = 0;
	{
		std::lock_guard<std::mutex> lock(submission_mutex_);
		value = next_fence_value_.fetch_add(1, std::memory_order_acq_rel);
		if (FAILED(queue_->Signal(fence_.Get(), value))) return;
	}
	wait(value);
}
