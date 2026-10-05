#pragma once

#include "Device.h"

#include <memory>

#ifdef USE_IMGUI
// ImGuiのD3D12バックエンドだけを管理する。Win32入力とUI構築はEditor層の責務に分離する。
class editor_imgui12
{
public:
	bool initialize(std::shared_ptr<directx12_device> device, UINT frame_count, DXGI_FORMAT color_format);
	void new_frame();
	void render(ID3D12GraphicsCommandList* command_list);
	void shutdown();

private:
	std::shared_ptr<directx12_device> device_;
	descriptor_handle font_descriptor_{};
	bool initialized_ = false;
};
#endif
