#include "Device.h"
#include "GraphicsConfig.h"

using Microsoft::WRL::ComPtr;

bool directx12_device::initialize(bool enable_debug_layer)
{
	UINT factory_flags = 0;
#ifdef _DEBUG
	if (enable_debug_layer)
	{
		ComPtr<ID3D12Debug> debug;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf()))))
		{
			debug->EnableDebugLayer();
			factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#else
	(void)enable_debug_layer;
#endif
	if (FAILED(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(factory_.GetAddressOf())))) return false;

	ComPtr<IDXGIAdapter1> adapter;
	for (UINT index = 0; factory_->EnumAdapterByGpuPreference(
		index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
		IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf())) != DXGI_ERROR_NOT_FOUND; ++index)
	{
		DXGI_ADAPTER_DESC1 desc{};
		adapter->GetDesc1(&desc);
		if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
			SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
				IID_PPV_ARGS(device_.ReleaseAndGetAddressOf())))) break;
	}
	if (!device_)
	{
		ComPtr<IDXGIAdapter> warp;
		if (FAILED(factory_->EnumWarpAdapter(IID_PPV_ARGS(warp.GetAddressOf()))) ||
			FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0,
				IID_PPV_ARGS(device_.GetAddressOf())))) return false;
	}

	if (!graphics_queue_.initialize(device_.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT, L"Graphics Queue") ||
		!compute_queue_.initialize(device_.Get(), D3D12_COMMAND_LIST_TYPE_COMPUTE, L"Compute Queue") ||
		!copy_queue_.initialize(device_.Get(), D3D12_COMMAND_LIST_TYPE_COPY, L"Copy Queue")) return false;

	// シェーダー可視ヒープは種類ごとに1つだけ設定できるため、ゲーム全体で共有する。
	if (!shader_descriptors_.initialize(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		graphics_config12::shader_descriptor_count, true) ||
		!sampler_descriptors_.initialize(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			graphics_config12::sampler_descriptor_count, true) ||
		!rtv_descriptors_.initialize(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			graphics_config12::rtv_descriptor_count, false) ||
		!dsv_descriptors_.initialize(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
			graphics_config12::dsv_descriptor_count, false)) return false;

	D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
	if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
		capabilities_.resource_binding = options.ResourceBindingTier;
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
	if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
		capabilities_.raytracing = options5.RaytracingTier;
	D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6{};
	if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &options6, sizeof(options6))))
		capabilities_.variable_rate_shading = options6.VariableShadingRateTier;
	D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
	if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7))))
		capabilities_.mesh_shader = options7.MeshShaderTier;
	return true;
}

void directx12_device::wait_idle()
{
	copy_queue_.wait_idle();
	compute_queue_.wait_idle();
	graphics_queue_.wait_idle();
}
