#pragma once

#include "Device.h"
#include <memory>

// 太陽視点で「最初に当たる物体までの深度」だけを描く2048pxの影用Texture。
// 通常描画の前にbegin→モデルの深度描画→endの順で呼ぶ。更新は同じGraphics Queue上で直列。
// サイズ変更や再確保は行わない。破棄前は所有するアプリ側でGPU完了を待つ。
class sun_shadow_map12
{
public:
    static constexpr UINT resolution = 2048;
    ~sun_shadow_map12()
    {
        if (device_ && dsv_.valid()) device_->dsv_descriptors().release(dsv_);
        if (device_ && srv_.valid()) device_->shader_descriptors().release(srv_);
    }
    bool initialize(std::shared_ptr<directx12_device> device)
    {
        device_ = std::move(device);
        if (!device_) return false;
        dsv_ = device_->dsv_descriptors().allocate();
        srv_ = device_->shader_descriptors().allocate();
        if (!dsv_.valid() || !srv_.valid()) return false;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = desc.Height = resolution;
        desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = 1.0f;
        if (FAILED(device_->get()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(depth_.GetAddressOf())))) return false;
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device_->get()->CreateDepthStencilView(depth_.Get(), &dsv, dsv_.cpu);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device_->get()->CreateShaderResourceView(depth_.Get(), &srv, srv_.cpu);
        return true;
    }
    void begin(ID3D12GraphicsCommandList* commands)
    {
        transition(commands, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        commands->OMSetRenderTargets(0, nullptr, FALSE, &dsv_.cpu);
        commands->ClearDepthStencilView(dsv_.cpu, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        D3D12_VIEWPORT viewport{ 0, 0, float(resolution), float(resolution), 0, 1 };
        D3D12_RECT scissor{ 0, 0, LONG(resolution), LONG(resolution) };
        commands->RSSetViewports(1, &viewport);
        commands->RSSetScissorRects(1, &scissor);
    }
    void end(ID3D12GraphicsCommandList* commands)
    {
        transition(commands, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE srv() const { return srv_.gpu; }
private:
    void transition(ID3D12GraphicsCommandList* commands, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = depth_.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        commands->ResourceBarrier(1, &barrier);
    }
    std::shared_ptr<directx12_device> device_;
    Microsoft::WRL::ComPtr<ID3D12Resource> depth_;
    descriptor_handle dsv_{}, srv_{};
};
