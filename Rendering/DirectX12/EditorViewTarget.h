#pragma once

#include "Device.h"

#include <memory>
#include <wrl.h>

// SSR合成後のGame画面を保持し、ImGui::Imageへ渡すためのD3D12レンダーターゲット。
class editor_view_target12
{
public:
	editor_view_target12() = default;
	~editor_view_target12();
	editor_view_target12(const editor_view_target12&) = delete;
	editor_view_target12& operator=(const editor_view_target12&) = delete;

	bool initialize(std::shared_ptr<directx12_device> device, UINT width, UINT height);
	bool resize(UINT width, UINT height);
	void begin(ID3D12GraphicsCommandList* command_list, const float clear_color[4]);
	void end(ID3D12GraphicsCommandList* command_list);

	D3D12_CPU_DESCRIPTOR_HANDLE rtv() const { return rtv_.cpu; }
	D3D12_GPU_DESCRIPTOR_HANDLE srv() const { return srv_.gpu; }
	UINT width() const { return width_; }
	UINT height() const { return height_; }

private:
	bool create_resource();
	static void transition(ID3D12GraphicsCommandList* command_list, ID3D12Resource* resource,
		D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

	std::shared_ptr<directx12_device> device_;
	Microsoft::WRL::ComPtr<ID3D12Resource> texture_;
	descriptor_handle rtv_{};
	descriptor_handle srv_{};
	UINT width_ = 0;
	UINT height_ = 0;
	bool shader_read_state_ = false;
};
