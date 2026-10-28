#pragma once

#include <atomic>
#include <cstdint>
#include <d3d12.h>
#include <mutex>
#include <vector>
#include <wrl.h>

struct descriptor_handle
{
	D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
	D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
	std::uint32_t index = UINT32_MAX;
	bool valid() const noexcept { return index != UINT32_MAX; }
};

// GPUから参照できるDescriptor Heapを共有し、Textureや定数Bufferの参照枠を同じ場所から確保する。
// DescriptorはGPUが最後に参照したFenceの完了前にreleaseして再利用しないこと。
// 大量の一時Descriptorが必要な機能は、永続Asset用とフレーム一時用の寿命を分けて設計する。
class descriptor_allocator
{
public:
	bool initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
		std::uint32_t capacity, bool shader_visible);
	descriptor_handle allocate();
	void release(descriptor_handle handle);

	ID3D12DescriptorHeap* heap() const { return heap_.Get(); }
	std::uint32_t capacity() const { return capacity_; }

private:
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
	D3D12_CPU_DESCRIPTOR_HANDLE cpu_start_{};
	D3D12_GPU_DESCRIPTOR_HANDLE gpu_start_{};
	std::uint32_t descriptor_size_ = 0;
	std::uint32_t capacity_ = 0;
	bool shader_visible_ = false;
	std::atomic<std::uint32_t> next_index_{ 0 };
	std::mutex recycled_mutex_;
	std::vector<std::uint32_t> recycled_indices_;
};
