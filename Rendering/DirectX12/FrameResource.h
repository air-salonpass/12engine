#pragma once

#include "DescriptorAllocator.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

// CPUが次のフレームを準備している間もGPUは前フレームを実行するため、
// 上書きしてはいけないCommandAllocator等をSwapChainのBuffer単位で保持する。
// fence_valueが完了するまでは、この構造体に属する資源を再利用しないこと。
struct frame_resource12
{
	Microsoft::WRL::ComPtr<ID3D12Resource> back_buffer;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;
	descriptor_handle rtv;
	std::uint64_t fence_value = 0;
};
