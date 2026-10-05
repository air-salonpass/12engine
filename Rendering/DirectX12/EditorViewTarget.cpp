#include "EditorViewTarget.h"

#include <algorithm>

editor_view_target12::~editor_view_target12()
{
	if (!device_) return;
	if (rtv_.valid()) device_->rtv_descriptors().release(rtv_);
	if (srv_.valid()) device_->shader_descriptors().release(srv_);
}

bool editor_view_target12::initialize(std::shared_ptr<directx12_device> device, UINT width, UINT height)
{
	device_ = std::move(device);
	if (!device_) return false;
	width_ = (std::max)(width, 1u);
	height_ = (std::max)(height, 1u);
	rtv_ = device_->rtv_descriptors().allocate();
	srv_ = device_->shader_descriptors().allocate();
	return rtv_.valid() && srv_.valid() && create_resource();
}

bool editor_view_target12::resize(UINT width, UINT height)
{
	width = (std::max)(width, 1u);
	height = (std::max)(height, 1u);
	if (width == width_ && height == height_) return true;
	texture_.Reset();
	width_ = width;
	height_ = height;
	shader_read_state_ = false;
	return create_resource();
}

bool editor_view_target12::create_resource()
{
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	heap.CreationNodeMask = heap.VisibleNodeMask = 1;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width_;
	desc.Height = height_;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_CLEAR_VALUE clear{};
	clear.Format = desc.Format;
	if (FAILED(device_->get()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(texture_.GetAddressOf())))) return false;
	device_->get()->CreateRenderTargetView(texture_.Get(), nullptr, rtv_.cpu);
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = desc.Format;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	device_->get()->CreateShaderResourceView(texture_.Get(), &srv, srv_.cpu);
	return true;
}

void editor_view_target12::begin(ID3D12GraphicsCommandList* command_list, const float clear_color[4])
{
	if (!command_list) return;
	if (shader_read_state_)
	{
		transition(command_list, texture_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_RENDER_TARGET);
		shader_read_state_ = false;
	}
	command_list->OMSetRenderTargets(1, &rtv_.cpu, FALSE, nullptr);
	command_list->ClearRenderTargetView(rtv_.cpu, clear_color, 0, nullptr);
	D3D12_VIEWPORT viewport{ 0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1 };
	D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
	command_list->RSSetViewports(1, &viewport);
	command_list->RSSetScissorRects(1, &scissor);
}

void editor_view_target12::end(ID3D12GraphicsCommandList* command_list)
{
	if (!command_list || shader_read_state_) return;
	transition(command_list, texture_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	shader_read_state_ = true;
}

void editor_view_target12::transition(ID3D12GraphicsCommandList* command_list, ID3D12Resource* resource,
	D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	command_list->ResourceBarrier(1, &barrier);
}
