#pragma once

#include "CommandQueue.h"
#include "DescriptorAllocator.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

// D3D12 DeviceとQueue/Descriptor Heapの所有者。
// ゲーム実装からこのクラスを直接操作せず、RendererまたはResource Manager経由で利用する。
// 新しいD3D12機能はsupported_features()を確認し、未対応GPU用の経路も必ず残すこと。
class directx12_device
{
public:
	struct capabilities
	{
		D3D12_RESOURCE_BINDING_TIER resource_binding = D3D12_RESOURCE_BINDING_TIER_1;
		D3D12_RAYTRACING_TIER raytracing = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
		D3D12_VARIABLE_SHADING_RATE_TIER variable_rate_shading = D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;
		D3D12_MESH_SHADER_TIER mesh_shader = D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
	};

	bool initialize(bool enable_debug_layer);
	void wait_idle();

	ID3D12Device* get() const { return device_.Get(); }
	IDXGIFactory6* factory() const { return factory_.Get(); }
	command_queue& graphics_queue() { return graphics_queue_; }
	command_queue& compute_queue() { return compute_queue_; }
	command_queue& copy_queue() { return copy_queue_; }
	descriptor_allocator& shader_descriptors() { return shader_descriptors_; }
	descriptor_allocator& sampler_descriptors() { return sampler_descriptors_; }
	descriptor_allocator& rtv_descriptors() { return rtv_descriptors_; }
	descriptor_allocator& dsv_descriptors() { return dsv_descriptors_; }
	const capabilities& supported_features() const { return capabilities_; }

private:
	Microsoft::WRL::ComPtr<IDXGIFactory6> factory_;
	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	command_queue graphics_queue_;
	command_queue compute_queue_;
	command_queue copy_queue_;
	descriptor_allocator shader_descriptors_;
	descriptor_allocator sampler_descriptors_;
	descriptor_allocator rtv_descriptors_;
	descriptor_allocator dsv_descriptors_;
	capabilities capabilities_{};
};
