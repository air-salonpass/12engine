#include "Renderer.h"

#include <algorithm>

bool directx12_renderer::initialize(HWND hwnd, UINT width, UINT height, bool enable_debug_layer)
{
	width_ = (std::max)(width, 1u);
	height_ = (std::max)(height, 1u);
	device_ = std::make_shared<directx12_device>();
	if (!device_->initialize(enable_debug_layer) || !create_swap_chain(hwnd) ||
		!create_frame_resources() || !create_depth_buffer())
	{
		return false;
	}
	return true;
}

bool directx12_renderer::create_swap_chain(HWND hwnd)
{
	DXGI_SWAP_CHAIN_DESC1 desc{};
	desc.Width = width_;
	desc.Height = height_;
	desc.Format = color_format;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = frame_count;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.Scaling = DXGI_SCALING_STRETCH;

	Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain1;
	if (FAILED(device_->factory()->CreateSwapChainForHwnd(
		device_->graphics_queue().get(), hwnd, &desc, nullptr, nullptr,
		swap_chain1.GetAddressOf())) || FAILED(swap_chain1.As(&swap_chain_))) return false;
	device_->factory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
	frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
	return true;
}

bool directx12_renderer::create_frame_resources()
{
	for (UINT index = 0; index < frame_count; ++index)
	{
		frame_resource& frame = frames_[index];
		if (!frame.rtv.valid()) frame.rtv = device_->rtv_descriptors().allocate();
		if (!frame.rtv.valid() ||
			FAILED(swap_chain_->GetBuffer(index, IID_PPV_ARGS(frame.back_buffer.GetAddressOf())))) return false;
		device_->get()->CreateRenderTargetView(frame.back_buffer.Get(), nullptr, frame.rtv.cpu);
		frame.back_buffer->SetName(L"JisakuEngine Back Buffer");
		if (!frame.command_allocator && FAILED(device_->get()->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(frame.command_allocator.GetAddressOf())))) return false;
		if (!frame.command_list)
		{
			if (FAILED(device_->get()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
				frame.command_allocator.Get(), nullptr, IID_PPV_ARGS(frame.command_list.GetAddressOf())))) return false;
			frame.command_list->Close();
		}
	}
	return true;
}

bool directx12_renderer::create_depth_buffer()
{
	if (!dsv_.valid()) dsv_ = device_->dsv_descriptors().allocate();
	if (!dsv_.valid()) return false;
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heap.CreationNodeMask = 1;
	heap.VisibleNodeMask = 1;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width_;
	desc.Height = height_;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = depth_format;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_CLEAR_VALUE clear{};
	clear.Format = depth_format;
	clear.DepthStencil.Depth = 1.0f;
	if (FAILED(device_->get()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(depth_buffer_.GetAddressOf())))) return false;
	device_->get()->CreateDepthStencilView(depth_buffer_.Get(), nullptr, dsv_.cpu);
	return true;
}

ID3D12GraphicsCommandList* directx12_renderer::begin_frame(const float clear_color[4])
{
	if (recording_) return nullptr;
	frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
	frame_resource& frame = frames_[frame_index_];
	if (!device_->graphics_queue().wait(frame.fence_value) ||
		FAILED(frame.command_allocator->Reset()) ||
		FAILED(frame.command_list->Reset(frame.command_allocator.Get(), nullptr))) return nullptr;

	transition(frame.command_list.Get(), frame.back_buffer.Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	frame.command_list->OMSetRenderTargets(1, &frame.rtv.cpu, FALSE, &dsv_.cpu);
	frame.command_list->ClearRenderTargetView(frame.rtv.cpu, clear_color, 0, nullptr);
	frame.command_list->ClearDepthStencilView(dsv_.cpu, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f };
	D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
	frame.command_list->RSSetViewports(1, &viewport);
	frame.command_list->RSSetScissorRects(1, &scissor);
	recording_ = true;
	return frame.command_list.Get();
}

bool directx12_renderer::end_frame(bool vsync)
{
	if (!recording_) return false;
	frame_resource& frame = frames_[frame_index_];
	transition(frame.command_list.Get(), frame.back_buffer.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	if (FAILED(frame.command_list->Close())) return false;
	ID3D12CommandList* lists[]{ frame.command_list.Get() };
	frame.fence_value = device_->graphics_queue().execute(lists, _countof(lists));
	recording_ = false;
	return frame.fence_value != 0 && SUCCEEDED(swap_chain_->Present(vsync ? 1 : 0, 0));
}

void directx12_renderer::transition(ID3D12GraphicsCommandList* command_list, ID3D12Resource* resource,
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

void directx12_renderer::release_size_dependent_resources()
{
	depth_buffer_.Reset();
	for (auto& frame : frames_)
	{
		frame.back_buffer.Reset();
		frame.fence_value = 0;
	}
}

bool directx12_renderer::resize(UINT width, UINT height)
{
	if (!swap_chain_ || recording_ || width == 0 || height == 0 ||
		(width == width_ && height == height_)) return width == width_ && height == height_;
	wait_idle();
	release_size_dependent_resources();
	width_ = width;
	height_ = height;
	if (FAILED(swap_chain_->ResizeBuffers(frame_count, width_, height_, color_format, 0))) return false;
	frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
	return create_frame_resources() && create_depth_buffer();
}

void directx12_renderer::wait_idle()
{
	if (device_) device_->wait_idle();
}
