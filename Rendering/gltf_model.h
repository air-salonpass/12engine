#pragma once

#define NOMINMAX
#include <d3d11.h>
#include <directxmath.h>
#include <wrl.h>

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// UNIT33では画像・マテリアルはまだ扱わないため、tinygltfの画像デコード機能を無効にする。
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tinygltf-release/tiny_gltf.h"

// glTFのシーン階層とメッシュ参照を、後続UNITで描画できる形式へ取り込むクラス。
class gltf_model
{
public:
	struct node;

	gltf_model(ID3D11Device* device, const std::string& filename);
	virtual ~gltf_model() = default;
	void render(
		ID3D11DeviceContext* immediate_context,
		const DirectX::XMFLOAT4X4& world,
		const std::vector<node>& animated_nodes = {});
	void animate(size_t animation_index, float time, std::vector<node>& animated_nodes);

	struct scene
	{
		std::string name;
		std::vector<int> nodes; // シーン直下にあるルートノードのインデックス。
	};

	struct node
	{
		std::string name;
		int skin{ -1 };
		int mesh{ -1 };
		std::vector<int> children;

		// glTFノードが持つローカル変換。matrix指定の場合もTRSへ分解して格納する。
		DirectX::XMFLOAT4 rotation{ 0, 0, 0, 1 };
		DirectX::XMFLOAT3 scale{ 1, 1, 1 };
		DirectX::XMFLOAT3 translation{ 0, 0, 0 };
		DirectX::XMFLOAT4X4 global_transform
		{
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		};
	};

	// tinygltfのAccessorとBufferViewから、D3D11で描画時に必要な参照情報をまとめる。
	struct buffer_view
	{
		DXGI_FORMAT format{ DXGI_FORMAT_UNKNOWN };
		int buffer{ -1 };
		size_t stride_in_bytes{ 0 };
		size_t byte_offset{ 0 };
		size_t count{ 0 };
	};

	struct mesh
	{
		std::string name;
		struct primitive
		{
			int material{ -1 };
			std::map<std::string, buffer_view> vertex_buffer_views;
			buffer_view index_buffer_view;

			bool has(const char* attribute) const
			{
				const auto iterator = vertex_buffer_views.find(attribute);
				return iterator != vertex_buffer_views.end() && iterator->second.buffer > -1;
			}
		};
		std::vector<primitive> primitives;
	};

	// UNIT35: glTFマテリアルをGPUのStructuredBufferと同じ並びで保持する。
	// テクスチャ本体は後続UNITで読み込むため、ここでは参照番号とUV番号を取得する。
	struct texture_info
	{
		int index{ -1 };
		int texcoord{ 0 };
	};

	struct normal_texture_info
	{
		int index{ -1 };
		int texcoord{ 0 };
		float scale{ 1.0f };
	};

	struct occlusion_texture_info
	{
		int index{ -1 };
		int texcoord{ 0 };
		float strength{ 1.0f };
	};

	struct pbr_metallic_roughness
	{
		float basecolor_factor[4]{ 1, 1, 1, 1 };
		texture_info basecolor_texture;
		float metallic_factor{ 1.0f };
		float roughness_factor{ 1.0f };
		texture_info metallic_roughness_texture;
	};

	struct material
	{
		std::string name;
		struct cbuffer
		{
			float emissive_factor[3]{ 0, 0, 0 };
			int alpha_mode{ 0 }; // 0: OPAQUE、1: MASK、2: BLEND
			float alpha_cutoff{ 0.5f };
			int double_sided{ 0 };
			pbr_metallic_roughness pbr_metallic_roughness_data;
			normal_texture_info normal_texture;
			occlusion_texture_info occlusion_texture;
			texture_info emissive_texture;
		} data;
	};

	// UNIT36: glTFのTextureは使用するImage番号を、Imageは画像本体の所在を保持する。
	struct texture
	{
		std::string name;
		int source{ -1 };
	};

	struct image
	{
		std::string name;
		int width{ -1 };
		int height{ -1 };
		int component{ -1 };
		int bits{ -1 };
		int pixel_type{ -1 };
		int buffer_view{ -1 };
		std::string mime_type;
		std::string uri;
		bool as_is{ false };
	};

	// UNIT37: スキンは各関節の逆バインド行列と、対応するノード番号を保持する。
	struct skin
	{
		std::vector<DirectX::XMFLOAT4X4> inverse_bind_matrices;
		std::vector<int> joints;
	};

	// UNIT37: チャンネルが対象ノードとTRS要素を選び、サンプラーが時間列と値列を結び付ける。
	struct animation
	{
		std::string name;
		float duration{ 0.0f };
		struct channel
		{
			int sampler{ -1 };
			int target_node{ -1 };
			std::string target_path;
		};
		std::vector<channel> channels;

		struct sampler
		{
			int input{ -1 };
			int output{ -1 };
			std::string interpolation;
		};
		std::vector<sampler> samplers;

		std::unordered_map<int, std::vector<float>> timelines;
		std::unordered_map<int, std::vector<DirectX::XMFLOAT3>> scales;
		std::unordered_map<int, std::vector<DirectX::XMFLOAT4>> rotations;
		std::unordered_map<int, std::vector<DirectX::XMFLOAT3>> translations;
	};

	std::vector<scene> scenes;
	std::vector<node> nodes;
	std::vector<mesh> meshes;
	std::vector<material> materials;
	std::vector<texture> textures;
	std::vector<image> images;
	std::vector<skin> skins;
	std::vector<animation> animations;
	std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> texture_resource_views;
	std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> buffers;
	int default_scene{ 0 };
	bool loaded{ false };
	bool has_bounds{ false };
	DirectX::XMFLOAT3 bounding_box_min{ 0, 0, 0 };
	DirectX::XMFLOAT3 bounding_box_max{ 0, 0, 0 };
	std::string load_error;

private:
	std::string filename;

	// UNIT34: glTFのSoA頂点を描画するシェーダーと、Primitive単位の定数バッファ。
	Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
	Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;
	struct primitive_constants
	{
		DirectX::XMFLOAT4X4 world;
		int material{ -1 };
		int has_tangent{ 0 };
		int skin{ -1 };
		int pad{ 0 };
	};
	Microsoft::WRL::ComPtr<ID3D11Buffer> primitive_constant_buffer;
	static constexpr size_t PRIMITIVE_MAX_JOINTS = 512;
	struct primitive_joint_constants
	{
		DirectX::XMFLOAT4X4 matrices[PRIMITIVE_MAX_JOINTS];
	};
	Microsoft::WRL::ComPtr<ID3D11Buffer> primitive_joint_constant_buffer;
	// UNIT38: glTFのテクスチャ座標は0～1の外側も使用するため、材質描画専用のREPEATサンプラーを保持する。
	Microsoft::WRL::ComPtr<ID3D11SamplerState> material_sampler_states[3];
	// UNIT35: 全マテリアルをまとめてピクセルシェーダーへ渡すSRV。
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> material_resource_view;

	void fetch_nodes(const tinygltf::Model& source_model);
	void cumulate_transforms(std::vector<node>& target_nodes);
	void fetch_meshes(ID3D11Device* device, const tinygltf::Model& source_model);
	void fetch_materials(ID3D11Device* device, const tinygltf::Model& source_model);
	void fetch_textures(ID3D11Device* device, const tinygltf::Model& source_model);
	void fetch_animations(const tinygltf::Model& source_model);
};
