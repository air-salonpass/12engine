#include "EditorImGui.h"

#ifdef USE_IMGUI
#include "../imgui/imgui.h"
#include "../imgui/imgui_impl_dx12.h"

bool editor_imgui12::initialize(std::shared_ptr<directx12_device> device, UINT frame_count,
	DXGI_FORMAT color_format)
{
	device_ = std::move(device);
	if (!device_) return false;
	font_descriptor_ = device_->shader_descriptors().allocate();
	if (!font_descriptor_.valid()) return false;
	// ImGui画像もモデルのテクスチャも同じshader-visible heapを共有する。
	initialized_ = ImGui_ImplDX12_Init(device_->get(), static_cast<int>(frame_count), color_format,
		device_->shader_descriptors().heap(), font_descriptor_.cpu, font_descriptor_.gpu);
	return initialized_;
}

void editor_imgui12::new_frame()
{
	if (initialized_) ImGui_ImplDX12_NewFrame();
}

void editor_imgui12::render(ID3D12GraphicsCommandList* command_list)
{
	if (!initialized_ || !command_list) return;
	// 描画直前に必ずheapを設定し、別パスが差し替えたDescriptorHeapの影響を受けないようにする。
	ID3D12DescriptorHeap* heaps[]{ device_->shader_descriptors().heap() };
	command_list->SetDescriptorHeaps(_countof(heaps), heaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list);
}

void editor_imgui12::shutdown()
{
	if (initialized_) ImGui_ImplDX12_Shutdown();
	initialized_ = false;
	if (device_ && font_descriptor_.valid()) device_->shader_descriptors().release(font_descriptor_);
	font_descriptor_ = {};
	device_.reset();
}
#endif
