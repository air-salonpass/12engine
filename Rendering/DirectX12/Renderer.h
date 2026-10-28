#pragma once

#include "Device.h"
#include "FrameResource.h"
#include "GraphicsConfig.h"

#include <array>
#include <cstdint>
#include <memory>

// スワップチェーン、フレーム別CommandAllocator、深度バッファを管理するネイティブD3D12レンダラー。
//
// 描画機能は begin_frame() と end_frame() の間だけCommand Listへ記録する。
// GPUが使用中のAllocator/Upload領域を上書きせず、frame_index()と同じ世代の領域を使うこと。
// 毎フレームwait_idle()を呼ぶとCPU/GPU並列性が失われるため、終了・Resize等に限定する。
class directx12_renderer
{
public:
	static constexpr UINT frame_count = graphics_config12::frame_count;
	static constexpr DXGI_FORMAT color_format = DXGI_FORMAT_R8G8B8A8_UNORM;
	static constexpr DXGI_FORMAT depth_format = DXGI_FORMAT_D32_FLOAT;

	~directx12_renderer();
	bool initialize(HWND hwnd, UINT width, UINT height, bool enable_debug_layer);
	bool resize(UINT width, UINT height);
	ID3D12GraphicsCommandList* begin_frame(const float clear_color[4]);
	bool end_frame(bool vsync);
	void wait_idle();
	// フレーム先行数が上限へ達した場合だけ待機する。毎フレームのGPU完了待ちとは異なる。
	void wait_for_frame_latency();

	std::shared_ptr<directx12_device> device() const { return device_; }
	UINT width() const { return width_; }
	UINT height() const { return height_; }
	UINT frame_index() const { return frame_index_; }
	D3D12_CPU_DESCRIPTOR_HANDLE current_rtv() const { return frames_[frame_index_].rtv.cpu; }
	D3D12_CPU_DESCRIPTOR_HANDLE dsv() const { return dsv_.cpu; }

private:
	bool create_swap_chain(HWND hwnd);
	bool create_frame_resources();
	bool create_depth_buffer();
	void release_size_dependent_resources();
	static void transition(ID3D12GraphicsCommandList* command_list, ID3D12Resource* resource,
		D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

	std::shared_ptr<directx12_device> device_;
	Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;
	HANDLE frame_latency_waitable_ = nullptr;
	std::array<frame_resource12, frame_count> frames_{};
	Microsoft::WRL::ComPtr<ID3D12Resource> depth_buffer_;
	descriptor_handle dsv_;
	UINT frame_index_ = 0;
	UINT width_ = 0;
	UINT height_ = 0;
	bool recording_ = false;
};
