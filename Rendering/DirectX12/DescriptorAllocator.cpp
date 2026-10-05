#include "DescriptorAllocator.h"

bool descriptor_allocator::initialize(ID3D12Device* device,
	D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t capacity, bool shader_visible)
{
	if (!device || capacity == 0) return false;
	D3D12_DESCRIPTOR_HEAP_DESC desc{};
	desc.Type = type;
	desc.NumDescriptors = capacity;
	desc.Flags = shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(heap_.ReleaseAndGetAddressOf()))))
	{
		return false;
	}
	descriptor_size_ = device->GetDescriptorHandleIncrementSize(type);
	cpu_start_ = heap_->GetCPUDescriptorHandleForHeapStart();
	if (shader_visible) gpu_start_ = heap_->GetGPUDescriptorHandleForHeapStart();
	capacity_ = capacity;
	shader_visible_ = shader_visible;
	next_index_.store(0, std::memory_order_release);
	return true;
}

descriptor_handle descriptor_allocator::allocate()
{
	std::uint32_t index = UINT32_MAX;
	{
		std::lock_guard<std::mutex> lock(recycled_mutex_);
		if (!recycled_indices_.empty())
		{
			index = recycled_indices_.back();
			recycled_indices_.pop_back();
		}
	}
	if (index == UINT32_MAX)
	{
		index = next_index_.fetch_add(1, std::memory_order_acq_rel);
		if (index >= capacity_) return {};
	}
	descriptor_handle handle{};
	handle.index = index;
	handle.cpu.ptr = cpu_start_.ptr + static_cast<SIZE_T>(index) * descriptor_size_;
	if (shader_visible_)
	{
		handle.gpu.ptr = gpu_start_.ptr + static_cast<UINT64>(index) * descriptor_size_;
	}
	return handle;
}

void descriptor_allocator::release(descriptor_handle handle)
{
	if (!handle.valid() || handle.index >= capacity_) return;
	std::lock_guard<std::mutex> lock(recycled_mutex_);
	recycled_indices_.push_back(handle.index);
}
