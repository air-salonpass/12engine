#include "SceneRenderTarget.h"

#include <algorithm>

scene_render_target12::~scene_render_target12()
{
	release_descriptors();
}

bool scene_render_target12::initialize(std::shared_ptr<directx12_device> device, UINT width, UINT height)
{
	device_ = std::move(device);
	width_ = (std::max)(width, 1u);
	height_ = (std::max)(height, 1u);
	if (!device_) return false;
	rtv_ = device_->rtv_descriptors().allocate();
	normal_rtv_ = device_->rtv_descriptors().allocate();
	dsv_ = device_->dsv_descriptors().allocate();
	color_srv_ = device_->shader_descriptors().allocate();
	depth_srv_ = device_->shader_descriptors().allocate();
	normal_srv_ = device_->shader_descriptors().allocate();
	return rtv_.valid() && normal_rtv_.valid() && dsv_.valid() && color_srv_.valid() &&
		depth_srv_.valid() && normal_srv_.valid() && create_resources();
}

bool scene_render_target12::resize(UINT width, UINT height)
{
	width = (std::max)(width, 1u);
	height = (std::max)(height, 1u);
	if (width == width_ && height == height_) return true;
	// 呼び出し側は、サイズ変更前に該当フレームのGPU Fence完了を待つこと。
	color_.Reset();
	depth_.Reset();
	normal_.Reset();
	width_ = width;
	height_ = height;
	shader_read_state_ = false;
	return create_resources();
}

bool scene_render_target12::create_resources()
{
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	heap.CreationNodeMask = heap.VisibleNodeMask = 1;
	D3D12_RESOURCE_DESC color_desc{};
	color_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	color_desc.Width = width_;
	color_desc.Height = height_;
	color_desc.DepthOrArraySize = 1;
	color_desc.MipLevels = 1;
	color_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	color_desc.SampleDesc.Count = 1;
	color_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_CLEAR_VALUE color_clear{};
	color_clear.Format = color_desc.Format;
	if (FAILED(device_->get()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &color_desc,
		D3D12_RESOURCE_STATE_RENDER_TARGET, &color_clear, IID_PPV_ARGS(color_.GetAddressOf())))) return false;
	device_->get()->CreateRenderTargetView(color_.Get(), nullptr, rtv_.cpu);
	D3D12_SHADER_RESOURCE_VIEW_DESC color_srv{};
	color_srv.Format = color_desc.Format;
	color_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	color_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	color_srv.Texture2D.MipLevels = 1;
	device_->get()->CreateShaderResourceView(color_.Get(), &color_srv, color_srv_.cpu);
	// SSRでは視線反射ベクトルを復元するため、ビュー空間法線を別RenderTargetへ保存する。
	if (FAILED(device_->get()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &color_desc,
		D3D12_RESOURCE_STATE_RENDER_TARGET, &color_clear, IID_PPV_ARGS(normal_.GetAddressOf())))) return false;
	device_->get()->CreateRenderTargetView(normal_.Get(), nullptr, normal_rtv_.cpu);
	device_->get()->CreateShaderResourceView(normal_.Get(), &color_srv, normal_srv_.cpu);

	D3D12_RESOURCE_DESC depth_desc = color_desc;
	depth_desc.Format = DXGI_FORMAT_R32_TYPELESS;
	depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_CLEAR_VALUE depth_clear{};
	depth_clear.Format = DXGI_FORMAT_D32_FLOAT;
	depth_clear.DepthStencil.Depth = 1.0f;
	if (FAILED(device_->get()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depth_desc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_clear, IID_PPV_ARGS(depth_.GetAddressOf())))) return false;
	D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
	dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
	dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device_->get()->CreateDepthStencilView(depth_.Get(), &dsv_desc, dsv_.cpu);
	D3D12_SHADER_RESOURCE_VIEW_DESC depth_srv{};
	depth_srv.Format = DXGI_FORMAT_R32_FLOAT;
	depth_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	depth_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	depth_srv.Texture2D.MipLevels = 1;
	device_->get()->CreateShaderResourceView(depth_.Get(), &depth_srv, depth_srv_.cpu);
	return true;
}

void scene_render_target12::begin(ID3D12GraphicsCommandList* command_list, const float clear_color[4])
{
	if (!command_list) return;
	if (shader_read_state_)
	{
		transition(command_list, color_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		transition(command_list, depth_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		transition(command_list, normal_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		shader_read_state_ = false;
	}
	D3D12_CPU_DESCRIPTOR_HANDLE render_targets[]{ rtv_.cpu, normal_rtv_.cpu };
	command_list->OMSetRenderTargets(_countof(render_targets), render_targets, FALSE, &dsv_.cpu);
	command_list->ClearRenderTargetView(rtv_.cpu, clear_color, 0, nullptr);
	const float clear_normal[4]{ 0.5f, 0.5f, 1.0f, 0.0f };
	command_list->ClearRenderTargetView(normal_rtv_.cpu, clear_normal, 0, nullptr);
	command_list->ClearDepthStencilView(dsv_.cpu, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	D3D12_VIEWPORT viewport{ 0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1 };
	D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
	command_list->RSSetViewports(1, &viewport);
	command_list->RSSetScissorRects(1, &scissor);
}

void scene_render_target12::end(ID3D12GraphicsCommandList* command_list)
{
	if (!command_list || shader_read_state_) return;
	// この遷移後はImGuiのImage表示とSSRの入力テクスチャの両方で参照できる。
	transition(command_list, color_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	transition(command_list, depth_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	transition(command_list, normal_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	shader_read_state_ = true;
}

void scene_render_target12::transition(ID3D12GraphicsCommandList* command_list, ID3D12Resource* resource,
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

void scene_render_target12::release_descriptors()
{
	if (!device_) return;
	if (rtv_.valid()) device_->rtv_descriptors().release(rtv_);
	if (normal_rtv_.valid()) device_->rtv_descriptors().release(normal_rtv_);
	if (dsv_.valid()) device_->dsv_descriptors().release(dsv_);
	if (color_srv_.valid()) device_->shader_descriptors().release(color_srv_);
	if (depth_srv_.valid()) device_->shader_descriptors().release(depth_srv_);
	if (normal_srv_.valid()) device_->shader_descriptors().release(normal_srv_);
}
