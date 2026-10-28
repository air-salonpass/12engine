#pragma once

#include "Device.h"

#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>

// tinygltfの実装本体はTinyGltfImplementation.cppへ分離し、このヘッダーでは宣言だけを使う。
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "../tinygltf-release/tiny_gltf.h"

// D3D12用に頂点をAoSへまとめた形式。将来Meshletを生成するときもこのCPU配列を入力にできる。
struct gltf_vertex12
{
	DirectX::XMFLOAT3 position{};
	DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
	DirectX::XMFLOAT4 tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
	DirectX::XMFLOAT2 texcoord{};
	DirectX::XMUINT4 joints{};
	DirectX::XMFLOAT4 weights{ 1.0f, 0.0f, 0.0f, 0.0f };
};

class gltf_model12
{
public:
	struct primitive
	{
		struct meshlet { std::uint32_t vertex_offset, vertex_count, triangle_offset, triangle_count; };
		Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer;
		Microsoft::WRL::ComPtr<ID3D12Resource> index_buffer;
		Microsoft::WRL::ComPtr<ID3D12Resource> meshlet_buffer;
		Microsoft::WRL::ComPtr<ID3D12Resource> meshlet_vertex_indices;
		Microsoft::WRL::ComPtr<ID3D12Resource> meshlet_triangles;
		D3D12_VERTEX_BUFFER_VIEW vertex_view{};
		D3D12_INDEX_BUFFER_VIEW index_view{};
		descriptor_handle vertex_srv{}, meshlet_srv{}, meshlet_vertex_indices_srv{}, meshlet_triangles_srv{};
		std::uint32_t index_count = 0;
		std::uint32_t meshlet_count = 0;
		int material = -1;
	};

	struct texture
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		descriptor_handle srv{};
	};

	// glTF Metallic-Roughness PBRで使用する係数とテクスチャ参照。
	struct material
	{
		DirectX::XMFLOAT4 base_color{ 1, 1, 1, 1 };
		DirectX::XMFLOAT3 emissive{};
		float metallic = 1.0f;
		float roughness = 1.0f;
		int base_color_texture = -1;
		int metallic_roughness_texture = -1;
		int normal_texture = -1;
		int occlusion_texture = -1;
		int emissive_texture = -1;
		int alpha_mode = 0; // 0: OPAQUE 1: MASK 2: BLEND
		float alpha_cutoff = 0.5f;
		bool double_sided = false;
		bool procedural_stone = false;
	};

	struct mesh
	{
		std::string name;
		std::vector<primitive> primitives;
	};

	struct node
	{
		std::string name;
		int mesh = -1;
		int skin = -1;
		int parent = -1;
		std::vector<int> children;
		DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
		DirectX::XMFLOAT4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
		DirectX::XMFLOAT3 translation{};
		DirectX::XMFLOAT4X4 global_transform{};
	};

	struct skin
	{
		std::vector<int> joints;
		std::vector<DirectX::XMFLOAT4X4> inverse_bind_matrices;
	};

	// glTFのAnimationSamplerとChannelを、実行時に参照しやすいCPU形式へ展開する。
	struct animation
	{
		struct sampler
		{
			int input = -1;
			int output = -1;
			std::string interpolation;
		};
		struct channel
		{
			int sampler = -1;
			int target_node = -1;
			std::string target_path;
		};

		std::string name;
		float duration = 0.0f;
		std::vector<sampler> samplers;
		std::vector<channel> channels;
		std::unordered_map<int, std::vector<float>> timelines;
		std::unordered_map<int, std::vector<DirectX::XMFLOAT3>> scales;
		std::unordered_map<int, std::vector<DirectX::XMFLOAT4>> rotations;
		std::unordered_map<int, std::vector<DirectX::XMFLOAT3>> translations;
	};

	explicit gltf_model12(std::shared_ptr<directx12_device> device);
	~gltf_model12();
	bool load(const std::string& filename);
	void update_global_transforms();
	void animate(std::size_t animation_index, float time, bool loop = true);
	void sample_animation(std::size_t animation_index, float time, bool loop,
		std::vector<node>& pose) const;
	void build_joint_matrices(int skin_index, std::vector<DirectX::XMFLOAT4X4>& matrices,
		const std::vector<node>* pose = nullptr) const;

	const std::vector<mesh>& meshes() const { return meshes_; }
	const std::vector<node>& nodes() const { return nodes_; }
	std::vector<node>& nodes() { return nodes_; }
	const std::vector<skin>& skins() const { return skins_; }
	const std::vector<animation>& animations() const { return animations_; }
	const std::vector<material>& materials() const { return materials_; }
	const std::vector<texture>& textures() const { return textures_; }
	std::size_t loaded_texture_count() const;
	D3D12_GPU_DESCRIPTOR_HANDLE texture_srv(int texture_index) const;
	const tinygltf::Model& source() const { return source_; }
	bool loaded() const { return loaded_; }
	const std::string& error() const { return error_; }
	const std::string& filename() const { return filename_; }
	const DirectX::XMFLOAT3& bounds_min() const { return bounds_min_; }
	const DirectX::XMFLOAT3& bounds_max() const { return bounds_max_; }

private:
	bool build_gpu_resources();
	bool upload_buffer(ID3D12GraphicsCommandList* command_list, const void* data,
		std::size_t size, D3D12_RESOURCE_STATES final_state,
		Microsoft::WRL::ComPtr<ID3D12Resource>& destination,
		Microsoft::WRL::ComPtr<ID3D12Resource>& upload);
	bool upload_texture(ID3D12GraphicsCommandList* command_list, const std::vector<std::uint8_t>& rgba,
		UINT width, UINT height, texture& destination, Microsoft::WRL::ComPtr<ID3D12Resource>& upload);
	void read_nodes();
	void read_skins();
	void read_animations();
	void read_materials();
	bool build_textures(ID3D12GraphicsCommandList* command_list,
		std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& uploads);
	void release_texture_descriptors();
	void release_mesh_descriptors();
	void calculate_node(int node_index, const DirectX::XMMATRIX& parent);
	static void calculate_pose_node(std::vector<node>& pose, int node_index,
		const DirectX::XMMATRIX& parent);

	std::shared_ptr<directx12_device> device_;
	tinygltf::Model source_;
	std::vector<mesh> meshes_;
	std::vector<node> nodes_;
	// Assetの初期姿勢は不変で保持し、EntityごとのPose生成元として共有する。
	std::vector<node> bind_nodes_;
	std::vector<skin> skins_;
	std::vector<animation> animations_;
	std::vector<material> materials_;
	std::vector<texture> textures_;
	DirectX::XMFLOAT3 bounds_min_{};
	DirectX::XMFLOAT3 bounds_max_{};
	int default_scene_ = 0;
	bool loaded_ = false;
	std::string error_;
	std::string filename_;
};
