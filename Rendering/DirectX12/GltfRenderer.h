#pragma once

#include "GltfModel.h"
#include "Renderer.h"
#include "EnvironmentLighting.h"

#include <cstddef>
#include <memory>
#include <wrl.h>

// glTFモデル専用のネイティブD3D12描画器。
// ゲーム側はPSOやRootSignatureを直接触らず、このクラスへ描画要求だけを渡す。
// Shaderを変更するときはHLSLのregister番号、Root Parameter、Descriptor Tableを必ず同時に更新する。
// Entity固有値はComponent/Poseから渡し、共有gltf_model12へ書き戻さないこと。
class gltf_renderer12
{
public:
	~gltf_renderer12();
	bool initialize(std::shared_ptr<directx12_device> device);
	int initialization_error() const { return initialization_error_; }
	void begin_frame(UINT frame_index);
	void render(ID3D12GraphicsCommandList* command_list, const gltf_model12& model,
		const DirectX::XMFLOAT4X4& world, const DirectX::XMFLOAT4X4& view,
		const DirectX::XMFLOAT4X4& projection,
		const environment_lighting12* environment = nullptr, float ibl_intensity = 0.0f,
		const std::vector<gltf_model12::node>* pose = nullptr,
		DirectX::XMFLOAT3 light_direction = { 0.35f, 0.8f, -0.45f },
		bool prefer_mesh_shader = true, bool force_double_sided = false);
	bool mesh_shader_available() const { return mesh_shader_available_; }

private:
	static constexpr std::size_t max_joints = 256;
	static constexpr std::size_t upload_bytes_per_frame = 4 * 1024 * 1024;
	struct scene_constants
	{
		DirectX::XMFLOAT4X4 world{}, view{}, projection{};
		DirectX::XMFLOAT4X4 inverse_view{};
		DirectX::XMFLOAT4 base_color_factor{ 1, 1, 1, 1 };
		DirectX::XMFLOAT4 emissive_metallic{ 0, 0, 0, 1 };
		DirectX::XMFLOAT4 roughness_flags{ 1, 0, 0, 0 };
		std::uint32_t skinned = 0;
		float padding[3]{};
		DirectX::XMFLOAT4 environment_parameters{};
		DirectX::XMFLOAT4 light_direction{ 0.35f, 0.8f, -0.45f, 0.0f };
	};
	struct joint_constants { DirectX::XMFLOAT4X4 matrices[max_joints]{}; };

	D3D12_GPU_VIRTUAL_ADDRESS upload(const void* data, std::size_t bytes);
	bool create_fallback_texture();
	void draw_node(ID3D12GraphicsCommandList* command_list, const gltf_model12& model,
		int node_index, const DirectX::XMFLOAT4X4& world,
		const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& projection,
		const environment_lighting12* environment, float ibl_intensity,
		const std::vector<gltf_model12::node>* pose, DirectX::XMFLOAT3 light_direction,
		ID3D12GraphicsCommandList6* mesh_commands, bool prefer_mesh_shader, bool force_double_sided);

	std::shared_ptr<directx12_device> device_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> double_sided_pipeline_state_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> alpha_pipeline_state_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mesh_root_signature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> mesh_pipeline_state_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> mesh_double_sided_pipeline_state_;
	Microsoft::WRL::ComPtr<ID3D12Resource> upload_buffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> fallback_texture_;
	descriptor_handle fallback_srv_{};
	std::byte* mapped_upload_ = nullptr;
	std::size_t frame_offset_ = 0;
	std::size_t upload_cursor_ = 0;
	int initialization_error_ = 0;
	bool mesh_shader_available_ = false;
};
