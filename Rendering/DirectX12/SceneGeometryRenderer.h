#pragma once

#include "Renderer.h"

#include <DirectXMath.h>
#include <array>
#include <memory>
#include <wrl.h>

// エディター確認用の反射床など、モデル資産に属さない最小限のシーン形状を描画する。
class scene_geometry_renderer12
{
public:
	bool initialize(std::shared_ptr<directx12_device> device);
	void begin_frame(UINT frame_index);
	void render_floor(ID3D12GraphicsCommandList* command_list,
		const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& projection,
		float floor_height, const DirectX::XMFLOAT3& color,
		D3D12_GPU_DESCRIPTOR_HANDLE reflection, UINT target_width, UINT target_height,
		float reflection_strength);

private:
	struct vertex
	{
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT3 normal;
	};
	struct constants
	{
		DirectX::XMFLOAT4X4 world{};
		DirectX::XMFLOAT4X4 view{};
		DirectX::XMFLOAT4X4 projection{};
		DirectX::XMFLOAT4 color{ 0.16f, 0.18f, 0.22f, 1.0f };
		DirectX::XMFLOAT4 render_parameters{}; // xy:描画サイズ、z:反射強度
	};

	std::shared_ptr<directx12_device> device_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state_;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> constants_buffer_;
	D3D12_VERTEX_BUFFER_VIEW vertex_view_{};
	std::byte* mapped_constants_ = nullptr;
	UINT frame_index_ = 0;
};
