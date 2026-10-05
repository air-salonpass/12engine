#pragma once

#include "Device.h"

#include <memory>
#include <wrl.h>

// Scene/GameビューやSSRで共有する、描画可能かつシェーダーから参照可能な画面バッファ。
class scene_render_target12
{
public:
	scene_render_target12() = default;
	~scene_render_target12();
	scene_render_target12(const scene_render_target12&) = delete;
	scene_render_target12& operator=(const scene_render_target12&) = delete;

	bool initialize(std::shared_ptr<directx12_device> device, UINT width, UINT height);
	bool resize(UINT width, UINT height);
	void begin(ID3D12GraphicsCommandList* command_list, const float clear_color[4]);
	void end(ID3D12GraphicsCommandList* command_list);

	ID3D12Resource* color() const { return color_.Get(); }
	ID3D12Resource* depth() const { return depth_.Get(); }
	ID3D12Resource* normal() const { return normal_.Get(); }
	D3D12_GPU_DESCRIPTOR_HANDLE color_srv() const { return color_srv_.gpu; }
	D3D12_GPU_DESCRIPTOR_HANDLE depth_srv() const { return depth_srv_.gpu; }
	D3D12_GPU_DESCRIPTOR_HANDLE normal_srv() const { return normal_srv_.gpu; }
	D3D12_CPU_DESCRIPTOR_HANDLE rtv() const { return rtv_.cpu; }
	D3D12_CPU_DESCRIPTOR_HANDLE dsv() const { return dsv_.cpu; }
	UINT width() const { return width_; }
	UINT height() const { return height_; }

private:
	bool create_resources();
	static void transition(ID3D12GraphicsCommandList* command_list, ID3D12Resource* resource,
		D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
	void release_descriptors();

	std::shared_ptr<directx12_device> device_;
	Microsoft::WRL::ComPtr<ID3D12Resource> color_;
	Microsoft::WRL::ComPtr<ID3D12Resource> depth_;
	Microsoft::WRL::ComPtr<ID3D12Resource> normal_;
	descriptor_handle rtv_{}, normal_rtv_{}, dsv_{}, color_srv_{}, depth_srv_{}, normal_srv_{};
	UINT width_ = 0;
	UINT height_ = 0;
	bool shader_read_state_ = false;
};
