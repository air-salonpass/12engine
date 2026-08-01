// tinygltfの実装部は、この翻訳単位で一度だけ生成する。
#define TINYGLTF_IMPLEMENTATION
#include "gltf_model.h"

#include "misc.h"
#include "shader.h"
#include "texture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <stack>

namespace
{
	// UNIT33では画像をGPUへ取り込まないため、埋め込み画像があってもデコードを省略する。
	bool null_load_image_data(
		tinygltf::Image*,
		const int,
		std::string*,
		std::string*,
		int,
		int,
		const unsigned char*,
		int,
		void*)
	{
		return true;
	}

	// Accessorがインターリーブされていても、指定型の連続配列へ安全に展開する。
	template<typename T>
	std::vector<T> read_accessor_values(
		const tinygltf::Model& source_model,
		const int accessor_index)
	{
		const tinygltf::Accessor& accessor = source_model.accessors.at(accessor_index);
		const tinygltf::BufferView& view = source_model.bufferViews.at(accessor.bufferView);
		const tinygltf::Buffer& buffer = source_model.buffers.at(view.buffer);
		const int source_stride = accessor.ByteStride(view);
		const size_t stride = source_stride > 0 ? static_cast<size_t>(source_stride) : sizeof(T);
		const size_t first_byte = view.byteOffset + accessor.byteOffset;

		_ASSERT_EXPR(stride >= sizeof(T), L"glTF accessor stride is smaller than its value type.");
		_ASSERT_EXPR(
			accessor.count == 0 ||
			first_byte + stride * (accessor.count - 1) + sizeof(T) <= buffer.data.size(),
			L"glTF accessor exceeds its source buffer.");

		std::vector<T> values(accessor.count);
		for (size_t value_index = 0; value_index < accessor.count; ++value_index)
		{
			std::memcpy(
				&values.at(value_index),
				buffer.data.data() + first_byte + stride * value_index,
				sizeof(T));
		}
		return values;
	}

	// Accessorの要素数と成分型を、D3D11の頂点・インデックス形式へ変換する。
	DXGI_FORMAT convert_format(const tinygltf::Accessor& accessor)
	{
		switch (accessor.type)
		{
		case TINYGLTF_TYPE_SCALAR:
			switch (accessor.componentType)
			{
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
				return DXGI_FORMAT_R8_UINT;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
				return DXGI_FORMAT_R16_UINT;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
				return DXGI_FORMAT_R32_UINT;
			default:
				_ASSERT_EXPR(FALSE, L"This scalar component type is not supported.");
				return DXGI_FORMAT_UNKNOWN;
			}

		case TINYGLTF_TYPE_VEC2:
			if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
			{
				return DXGI_FORMAT_R32G32_FLOAT;
			}
			break;

		case TINYGLTF_TYPE_VEC3:
			if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
			{
				return DXGI_FORMAT_R32G32B32_FLOAT;
			}
			break;

		case TINYGLTF_TYPE_VEC4:
			switch (accessor.componentType)
			{
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
				return DXGI_FORMAT_R8G8B8A8_UINT;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
				return DXGI_FORMAT_R16G16B16A16_UINT;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
				return DXGI_FORMAT_R32G32B32A32_UINT;
			case TINYGLTF_COMPONENT_TYPE_FLOAT:
				return DXGI_FORMAT_R32G32B32A32_FLOAT;
			default:
				break;
			}
			break;

		default:
			break;
		}

		_ASSERT_EXPR(FALSE, L"This accessor type or component type is not supported.");
		return DXGI_FORMAT_UNKNOWN;
	}
}

gltf_model::gltf_model(ID3D11Device* device, const std::string& filename)
	: filename(filename)
{
	tinygltf::TinyGLTF loader;
	loader.SetImageLoader(null_load_image_data, nullptr);

	tinygltf::Model source_model;
	std::string error;
	std::string warning;
	bool succeeded{ false };

	// 拡張子に応じて、JSON形式のgltfとバイナリ形式のglbを読み分ける。
	if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".glb")
	{
		succeeded = loader.LoadBinaryFromFile(
			&source_model,
			&error,
			&warning,
			filename);
	}
	else if (filename.size() >= 5 && filename.substr(filename.size() - 5) == ".gltf")
	{
		succeeded = loader.LoadASCIIFromFile(
			&source_model,
			&error,
			&warning,
			filename);
	}

	if (!succeeded)
	{
		load_error = error.empty() ? "Failed to load glTF/GLB file." : error;
		if (!warning.empty())
		{
			load_error += "\n" + warning;
		}
		return;
	}

	// glTFのシーン一覧と、各シーンのルートノード番号を保持する。
	for (const tinygltf::Scene& source_scene : source_model.scenes)
	{
		scene& destination_scene = scenes.emplace_back();
		destination_scene.name = source_scene.name;
		destination_scene.nodes = source_scene.nodes;
	}
	default_scene = source_model.defaultScene >= 0 ? source_model.defaultScene : 0;

	fetch_nodes(source_model);
	fetch_meshes(device, source_model);
	fetch_materials(device, source_model);
	fetch_textures(device, source_model);
	fetch_animations(source_model);

	// glTFの各属性を頂点スロット0～5へ別々に結び付けるSoA入力レイアウトを作る。
	D3D11_INPUT_ELEMENT_DESC input_element_desc[]
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 3, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "JOINTS", 0, DXGI_FORMAT_R16G16B16A16_UINT, 4, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "WEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 5, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	create_vs_from_cso(
		device,
		"gltf_model_vs.cso",
		vertex_shader.ReleaseAndGetAddressOf(),
		input_layout.ReleaseAndGetAddressOf(),
		input_element_desc,
		_countof(input_element_desc));
	create_ps_from_cso(
		device,
		"gltf_model_ps.cso",
		pixel_shader.ReleaseAndGetAddressOf());

	// UNIT38: PBRで使う全テクスチャをglTF標準のREPEATアドレスで参照する。
	// POINT・LINEAR・ANISOTROPICをs0～s2へ並べ、HLSL側の定義と一致させる。
	const D3D11_FILTER material_filters[3]
	{
		D3D11_FILTER_MIN_MAG_MIP_POINT,
		D3D11_FILTER_MIN_MAG_MIP_LINEAR,
		D3D11_FILTER_ANISOTROPIC
	};
	for (size_t sampler_index = 0; sampler_index < _countof(material_sampler_states); ++sampler_index)
	{
		D3D11_SAMPLER_DESC sampler_desc{};
		sampler_desc.Filter = material_filters[sampler_index];
		sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		sampler_desc.MaxAnisotropy = sampler_index == 2 ? 16 : 1;
		sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sampler_desc.MinLOD = 0.0f;
		sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
		const HRESULT sampler_result = device->CreateSamplerState(
			&sampler_desc,
			material_sampler_states[sampler_index].ReleaseAndGetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(sampler_result), hr_trace(sampler_result));
	}

	D3D11_BUFFER_DESC constant_buffer_desc{};
	constant_buffer_desc.ByteWidth = sizeof(primitive_constants);
	constant_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	constant_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	HRESULT hr = device->CreateBuffer(
		&constant_buffer_desc,
		nullptr,
		primitive_constant_buffer.ReleaseAndGetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	// UNIT37: 最大512関節分のスキニング行列を頂点シェーダーb2へ渡す。
	constant_buffer_desc.ByteWidth = sizeof(primitive_joint_constants);
	hr = device->CreateBuffer(
		&constant_buffer_desc,
		nullptr,
		primitive_joint_constant_buffer.ReleaseAndGetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
	loaded = true;
}

void gltf_model::fetch_nodes(const tinygltf::Model& source_model)
{
	// ノード名、メッシュ参照、子ノード一覧とローカル変換を順番どおり取り込む。
	for (const tinygltf::Node& source_node : source_model.nodes)
	{
		node& destination_node = nodes.emplace_back();
		destination_node.name = source_node.name;
		destination_node.skin = source_node.skin;
		destination_node.mesh = source_node.mesh;
		destination_node.children = source_node.children;

		if (!source_node.matrix.empty())
		{
			// glTFの列優先行列をDirectXの行ベクトル用として読み、TRSへ分解する。
			DirectX::XMFLOAT4X4 matrix{};
			for (size_t row = 0; row < 4; ++row)
			{
				for (size_t column = 0; column < 4; ++column)
				{
					matrix.m[row][column] =
						static_cast<float>(source_node.matrix.at(4 * row + column));
				}
			}

			DirectX::XMVECTOR scale;
			DirectX::XMVECTOR rotation;
			DirectX::XMVECTOR translation;
			const bool succeeded = DirectX::XMMatrixDecompose(
				&scale,
				&rotation,
				&translation,
				DirectX::XMLoadFloat4x4(&matrix));
			_ASSERT_EXPR(succeeded, L"Failed to decompose glTF node matrix.");

			DirectX::XMStoreFloat3(&destination_node.scale, scale);
			DirectX::XMStoreFloat4(&destination_node.rotation, rotation);
			DirectX::XMStoreFloat3(&destination_node.translation, translation);
		}
		else
		{
			if (source_node.scale.size() == 3)
			{
				destination_node.scale =
				{
					static_cast<float>(source_node.scale[0]),
					static_cast<float>(source_node.scale[1]),
					static_cast<float>(source_node.scale[2])
				};
			}
			if (source_node.translation.size() == 3)
			{
				destination_node.translation =
				{
					static_cast<float>(source_node.translation[0]),
					static_cast<float>(source_node.translation[1]),
					static_cast<float>(source_node.translation[2])
				};
			}
			if (source_node.rotation.size() == 4)
			{
				destination_node.rotation =
				{
					static_cast<float>(source_node.rotation[0]),
					static_cast<float>(source_node.rotation[1]),
					static_cast<float>(source_node.rotation[2]),
					static_cast<float>(source_node.rotation[3])
				};
			}
		}
	}

	cumulate_transforms(nodes);
}

void gltf_model::cumulate_transforms(std::vector<node>& target_nodes)
{
	using namespace DirectX;
	if (scenes.empty() || default_scene < 0 || default_scene >= static_cast<int>(scenes.size()))
	{
		return;
	}

	// ルートから深さ優先でたどり、親のグローバル変換を子へ累積する。
	std::stack<XMFLOAT4X4> parent_global_transforms;
	std::function<void(int)> traverse = [&](int node_index)
	{
		_ASSERT_EXPR(
			node_index >= 0 && node_index < static_cast<int>(target_nodes.size()),
			L"glTF node index is out of range.");
		if (node_index < 0 || node_index >= static_cast<int>(target_nodes.size()))
		{
			return;
		}

		node& current_node = target_nodes.at(node_index);
		const XMMATRIX scaling = XMMatrixScaling(
			current_node.scale.x,
			current_node.scale.y,
			current_node.scale.z);
		const XMMATRIX rotation = XMMatrixRotationQuaternion(XMVectorSet(
			current_node.rotation.x,
			current_node.rotation.y,
			current_node.rotation.z,
			current_node.rotation.w));
		const XMMATRIX translation = XMMatrixTranslation(
			current_node.translation.x,
			current_node.translation.y,
			current_node.translation.z);

		XMStoreFloat4x4(
			&current_node.global_transform,
			scaling * rotation * translation *
			XMLoadFloat4x4(&parent_global_transforms.top()));

		for (const int child_index : current_node.children)
		{
			parent_global_transforms.push(current_node.global_transform);
			traverse(child_index);
			parent_global_transforms.pop();
		}
	};

	const XMFLOAT4X4 identity
	{
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};
	for (const int root_node_index : scenes.at(default_scene).nodes)
	{
		parent_global_transforms.push(identity);
		traverse(root_node_index);
		parent_global_transforms.pop();
	}
}

void gltf_model::fetch_meshes(ID3D11Device* device, const tinygltf::Model& source_model)
{
	HRESULT hr{ S_OK };
	buffers.resize(source_model.buffers.size());

	// glTFのバイナリデータを、頂点・インデックスの両方に使えるGPUバッファへ転送する。
	for (size_t buffer_index = 0; buffer_index < source_model.buffers.size(); ++buffer_index)
	{
		const tinygltf::Buffer& source_buffer = source_model.buffers.at(buffer_index);
		_ASSERT_EXPR(!source_buffer.data.empty(), L"glTF buffer is empty.");
		_ASSERT_EXPR(
			source_buffer.data.size() <= (std::numeric_limits<UINT>::max)(),
			L"glTF buffer is too large for D3D11.");
		if (source_buffer.data.empty() ||
			source_buffer.data.size() > (std::numeric_limits<UINT>::max)())
		{
			continue;
		}

		D3D11_BUFFER_DESC buffer_desc{};
		buffer_desc.ByteWidth = static_cast<UINT>(source_buffer.data.size());
		buffer_desc.Usage = D3D11_USAGE_DEFAULT;
		buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER | D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA subresource_data{};
		subresource_data.pSysMem = source_buffer.data.data();

		hr = device->CreateBuffer(
			&buffer_desc,
			&subresource_data,
			buffers.at(buffer_index).GetAddressOf());
		_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
	}

	// 各Primitiveが参照するAccessorを、D3D11用のバッファビュー情報へ変換する。
	for (const tinygltf::Mesh& source_mesh : source_model.meshes)
	{
		mesh& destination_mesh = meshes.emplace_back();
		destination_mesh.name = source_mesh.name;

		for (const tinygltf::Primitive& source_primitive : source_mesh.primitives)
		{
			mesh::primitive& destination_primitive = destination_mesh.primitives.emplace_back();
			destination_primitive.material = source_primitive.material;

			if (source_primitive.indices > -1)
			{
				const tinygltf::Accessor& accessor =
					source_model.accessors.at(source_primitive.indices);
				const tinygltf::BufferView& source_buffer_view =
					source_model.bufferViews.at(accessor.bufferView);
				const int stride = accessor.ByteStride(source_buffer_view);
				_ASSERT_EXPR(stride > 0, L"Invalid glTF index buffer stride.");

				destination_primitive.index_buffer_view.format = convert_format(accessor);
				destination_primitive.index_buffer_view.buffer = source_buffer_view.buffer;
				destination_primitive.index_buffer_view.stride_in_bytes =
					stride > 0 ? static_cast<size_t>(stride) : 0;
				destination_primitive.index_buffer_view.byte_offset =
					source_buffer_view.byteOffset + accessor.byteOffset;
				destination_primitive.index_buffer_view.count = accessor.count;
			}

			for (const auto& attribute : source_primitive.attributes)
			{
				const tinygltf::Accessor& accessor = source_model.accessors.at(attribute.second);
				if (attribute.first == "POSITION" &&
					accessor.minValues.size() >= 3 && accessor.maxValues.size() >= 3)
				{
					const DirectX::XMFLOAT3 accessor_min
					{
						static_cast<float>(accessor.minValues[0]),
						static_cast<float>(accessor.minValues[1]),
						static_cast<float>(accessor.minValues[2])
					};
					const DirectX::XMFLOAT3 accessor_max
					{
						static_cast<float>(accessor.maxValues[0]),
						static_cast<float>(accessor.maxValues[1]),
						static_cast<float>(accessor.maxValues[2])
					};
					if (!has_bounds)
					{
						bounding_box_min = accessor_min;
						bounding_box_max = accessor_max;
						has_bounds = true;
					}
					else
					{
						bounding_box_min.x = (std::min)(bounding_box_min.x, accessor_min.x);
						bounding_box_min.y = (std::min)(bounding_box_min.y, accessor_min.y);
						bounding_box_min.z = (std::min)(bounding_box_min.z, accessor_min.z);
						bounding_box_max.x = (std::max)(bounding_box_max.x, accessor_max.x);
						bounding_box_max.y = (std::max)(bounding_box_max.y, accessor_max.y);
						bounding_box_max.z = (std::max)(bounding_box_max.z, accessor_max.z);
					}
				}
				const tinygltf::BufferView& source_buffer_view =
					source_model.bufferViews.at(accessor.bufferView);
				const int stride = accessor.ByteStride(source_buffer_view);
				_ASSERT_EXPR(stride > 0, L"Invalid glTF vertex buffer stride.");

				buffer_view destination_buffer_view{};
				destination_buffer_view.format = convert_format(accessor);
				destination_buffer_view.buffer = source_buffer_view.buffer;
				destination_buffer_view.stride_in_bytes =
					stride > 0 ? static_cast<size_t>(stride) : 0;
				destination_buffer_view.byte_offset =
					source_buffer_view.byteOffset + accessor.byteOffset;
				destination_buffer_view.count = accessor.count;

				destination_primitive.vertex_buffer_views.emplace(
					attribute.first,
					destination_buffer_view);
			}
		}
	}
}

void gltf_model::fetch_materials(ID3D11Device* device, const tinygltf::Model& source_model)
{
	// glTFの各マテリアルから、PBR係数と後続UNITで使うテクスチャ参照を取り込む。
	for (const tinygltf::Material& source_material : source_model.materials)
	{
		material& destination = materials.emplace_back();
		destination.name = source_material.name;

		for (size_t component = 0; component < 3; ++component)
		{
			destination.data.emissive_factor[component] =
				static_cast<float>(source_material.emissiveFactor.at(component));
		}
		destination.data.alpha_mode = source_material.alphaMode == "MASK" ? 1 :
			source_material.alphaMode == "BLEND" ? 2 : 0;
		destination.data.alpha_cutoff = static_cast<float>(source_material.alphaCutoff);
		destination.data.double_sided = source_material.doubleSided ? 1 : 0;

		pbr_metallic_roughness& pbr = destination.data.pbr_metallic_roughness_data;
		for (size_t component = 0; component < 4; ++component)
		{
			pbr.basecolor_factor[component] = static_cast<float>(
				source_material.pbrMetallicRoughness.baseColorFactor.at(component));
		}
		pbr.basecolor_texture.index =
			source_material.pbrMetallicRoughness.baseColorTexture.index;
		pbr.basecolor_texture.texcoord =
			source_material.pbrMetallicRoughness.baseColorTexture.texCoord;
		pbr.metallic_factor = static_cast<float>(
			source_material.pbrMetallicRoughness.metallicFactor);
		pbr.roughness_factor = static_cast<float>(
			source_material.pbrMetallicRoughness.roughnessFactor);
		pbr.metallic_roughness_texture.index =
			source_material.pbrMetallicRoughness.metallicRoughnessTexture.index;
		pbr.metallic_roughness_texture.texcoord =
			source_material.pbrMetallicRoughness.metallicRoughnessTexture.texCoord;

		destination.data.normal_texture.index = source_material.normalTexture.index;
		destination.data.normal_texture.texcoord = source_material.normalTexture.texCoord;
		destination.data.normal_texture.scale =
			static_cast<float>(source_material.normalTexture.scale);
		destination.data.occlusion_texture.index = source_material.occlusionTexture.index;
		destination.data.occlusion_texture.texcoord = source_material.occlusionTexture.texCoord;
		destination.data.occlusion_texture.strength =
			static_cast<float>(source_material.occlusionTexture.strength);
		destination.data.emissive_texture.index = source_material.emissiveTexture.index;
		destination.data.emissive_texture.texcoord = source_material.emissiveTexture.texCoord;
	}

	// マテリアルが省略されたglTFでも、シェーダーが安全に0番を参照できるよう既定値を用意する。
	if (materials.empty())
	{
		materials.emplace_back();
		materials.back().name = "Default Material";
	}

	// CPU構造とHLSL構造は同じ96バイトでなければ、StructuredBufferの添字がずれる。
	static_assert(sizeof(material::cbuffer) == 96, "Material buffer layout must match HLSL.");
	std::vector<material::cbuffer> material_data;
	material_data.reserve(materials.size());
	for (const material& source : materials)
	{
		material_data.emplace_back(source.data);
	}

	D3D11_BUFFER_DESC buffer_desc{};
	buffer_desc.ByteWidth = static_cast<UINT>(sizeof(material::cbuffer) * material_data.size());
	buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	buffer_desc.StructureByteStride = sizeof(material::cbuffer);
	D3D11_SUBRESOURCE_DATA subresource_data{};
	subresource_data.pSysMem = material_data.data();

	Microsoft::WRL::ComPtr<ID3D11Buffer> material_buffer;
	HRESULT hr = device->CreateBuffer(
		&buffer_desc,
		&subresource_data,
		material_buffer.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	D3D11_SHADER_RESOURCE_VIEW_DESC resource_view_desc{};
	resource_view_desc.Format = DXGI_FORMAT_UNKNOWN;
	resource_view_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	resource_view_desc.Buffer.NumElements = static_cast<UINT>(material_data.size());
	hr = device->CreateShaderResourceView(
		material_buffer.Get(),
		&resource_view_desc,
		material_resource_view.ReleaseAndGetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
}

void gltf_model::fetch_textures(ID3D11Device* device, const tinygltf::Model& source_model)
{
	// Texture番号から、実際に画像を持つImage番号へたどれるよう参照関係を保存する。
	for (const tinygltf::Texture& source_texture : source_model.textures)
	{
		texture& destination = textures.emplace_back();
		destination.name = source_texture.name;
		destination.source = source_texture.source;
	}

	// Image番号とSRV番号を常に一致させ、マテリアルから安全に参照できるようにする。
	texture_resource_views.resize(source_model.images.size());
	for (size_t image_index = 0; image_index < source_model.images.size(); ++image_index)
	{
		const tinygltf::Image& source_image = source_model.images.at(image_index);
		image& destination = images.emplace_back();
		destination.name = source_image.name;
		destination.width = source_image.width;
		destination.height = source_image.height;
		destination.component = source_image.component;
		destination.bits = source_image.bits;
		destination.pixel_type = source_image.pixel_type;
		destination.buffer_view = source_image.bufferView;
		destination.mime_type = source_image.mimeType;
		destination.uri = source_image.uri;
		destination.as_is = source_image.as_is;

		ID3D11ShaderResourceView* resource_view{};
		HRESULT hr{ E_FAIL };
		if (source_image.bufferView > -1)
		{
			// glb内の画像はBufferViewが示すバイト列をDDS／WICローダーへ直接渡す。
			const tinygltf::BufferView& source_view =
				source_model.bufferViews.at(source_image.bufferView);
			const tinygltf::Buffer& source_buffer =
				source_model.buffers.at(source_view.buffer);
			const unsigned char* data = source_buffer.data.data() + source_view.byteOffset;
			hr = load_texture_from_memory(
				device,
				data,
				source_view.byteLength,
				&resource_view);
		}
		else if (!source_image.uri.empty())
		{
			// 外部画像はgltfファイルと同じフォルダーを基準にして読み込む。
			const std::filesystem::path image_path =
				std::filesystem::path(filename).parent_path() /
				std::filesystem::path(source_image.uri);
			D3D11_TEXTURE2D_DESC texture_desc{};
			hr = load_texture_from_file(
				device,
				image_path.wstring().c_str(),
				&resource_view,
				&texture_desc);
		}

		if (SUCCEEDED(hr) && resource_view)
		{
			texture_resource_views.at(image_index).Attach(resource_view);
		}
	}
}

void gltf_model::fetch_animations(const tinygltf::Model& source_model)
{
	// スキンが参照する逆バインド行列と関節ノード番号をCPU側へ取り込む。
	for (const tinygltf::Skin& source_skin : source_model.skins)
	{
		skin& destination = skins.emplace_back();
		destination.joints = source_skin.joints;
		_ASSERT_EXPR(
			destination.joints.size() <= PRIMITIVE_MAX_JOINTS,
			L"glTF skin exceeds the supported joint count.");

		if (source_skin.inverseBindMatrices > -1)
		{
			destination.inverse_bind_matrices =
				read_accessor_values<DirectX::XMFLOAT4X4>(
					source_model,
					source_skin.inverseBindMatrices);
		}
		else
		{
			// inverseBindMatrices省略時はglTF仕様に従って単位行列を使用する。
			const DirectX::XMFLOAT4X4 identity
			{
				1, 0, 0, 0,
				0, 1, 0, 0,
				0, 0, 1, 0,
				0, 0, 0, 1
			};
			destination.inverse_bind_matrices.assign(destination.joints.size(), identity);
		}
		_ASSERT_EXPR(
			destination.inverse_bind_matrices.size() == destination.joints.size(),
			L"glTF skin joint and inverse bind matrix counts do not match.");
	}

	// アニメーションの時間列と、各チャンネルが変更するTRS値をAccessorから展開する。
	for (const tinygltf::Animation& source_animation : source_model.animations)
	{
		animation& destination = animations.emplace_back();
		destination.name = source_animation.name;

		for (const tinygltf::AnimationSampler& source_sampler : source_animation.samplers)
		{
			animation::sampler& sampler = destination.samplers.emplace_back();
			sampler.input = source_sampler.input;
			sampler.output = source_sampler.output;
			sampler.interpolation = source_sampler.interpolation;

			if (destination.timelines.find(sampler.input) == destination.timelines.end())
			{
				destination.timelines.emplace(
					sampler.input,
					read_accessor_values<float>(source_model, sampler.input));
			}
		}

		for (const tinygltf::AnimationChannel& source_channel : source_animation.channels)
		{
			animation::channel& channel = destination.channels.emplace_back();
			channel.sampler = source_channel.sampler;
			channel.target_node = source_channel.target_node;
			channel.target_path = source_channel.target_path;

			const tinygltf::AnimationSampler& source_sampler =
				source_animation.samplers.at(source_channel.sampler);
			if (channel.target_path == "scale" &&
				destination.scales.find(source_sampler.output) == destination.scales.end())
			{
				destination.scales.emplace(
					source_sampler.output,
					read_accessor_values<DirectX::XMFLOAT3>(source_model, source_sampler.output));
			}
			else if (channel.target_path == "rotation" &&
				destination.rotations.find(source_sampler.output) == destination.rotations.end())
			{
				destination.rotations.emplace(
					source_sampler.output,
					read_accessor_values<DirectX::XMFLOAT4>(source_model, source_sampler.output));
			}
			else if (channel.target_path == "translation" &&
				destination.translations.find(source_sampler.output) == destination.translations.end())
			{
				destination.translations.emplace(
					source_sampler.output,
					read_accessor_values<DirectX::XMFLOAT3>(source_model, source_sampler.output));
			}
		}

		// 全チャンネルのうち最も遅い最終キー時刻を、このクリップの再生時間とする。
		for (const auto& timeline : destination.timelines)
		{
			if (!timeline.second.empty())
			{
				destination.duration = (std::max)(destination.duration, timeline.second.back());
			}
		}
	}
}

void gltf_model::animate(
	const size_t animation_index,
	const float time,
	std::vector<node>& animated_nodes)
{
	using namespace DirectX;
	if (animation_index >= animations.size())
	{
		return;
	}
	if (animated_nodes.size() != nodes.size())
	{
		animated_nodes = nodes;
	}

	const animation& current_animation = animations.at(animation_index);
	const float sample_time = current_animation.duration > 0.0f ?
		std::fmod((std::max)(time, 0.0f), current_animation.duration) : 0.0f;

	for (const animation::channel& channel : current_animation.channels)
	{
		if (channel.sampler < 0 ||
			channel.sampler >= static_cast<int>(current_animation.samplers.size()) ||
			channel.target_node < 0 ||
			channel.target_node >= static_cast<int>(animated_nodes.size()))
		{
			continue;
		}

		const animation::sampler& sampler = current_animation.samplers.at(channel.sampler);
		const std::vector<float>& timeline = current_animation.timelines.at(sampler.input);
		if (timeline.empty())
		{
			continue;
		}

		size_t keyframe_index = 0;
		size_t next_keyframe_index = 0;
		float interpolation_factor = 0.0f;
		if (timeline.size() > 1)
		{
			const auto upper = std::upper_bound(timeline.begin(), timeline.end(), sample_time);
			next_keyframe_index = upper == timeline.end() ? timeline.size() - 1 :
				static_cast<size_t>(upper - timeline.begin());
			keyframe_index = next_keyframe_index > 0 ? next_keyframe_index - 1 : 0;
			const float interval = timeline.at(next_keyframe_index) - timeline.at(keyframe_index);
			if (interval > 0.0f && next_keyframe_index != keyframe_index)
			{
				interpolation_factor =
					(sample_time - timeline.at(keyframe_index)) / interval;
			}
			if (sampler.interpolation == "STEP")
			{
				interpolation_factor = 0.0f;
			}
		}

		// 平行移動・拡大縮小は線形補間、回転はクォータニオンの球面線形補間を使う。
		node& target = animated_nodes.at(channel.target_node);
		if (channel.target_path == "scale")
		{
			const auto& values = current_animation.scales.at(sampler.output);
			XMStoreFloat3(
				&target.scale,
				XMVectorLerp(
					XMLoadFloat3(&values.at(keyframe_index)),
					XMLoadFloat3(&values.at(next_keyframe_index)),
					interpolation_factor));
		}
		else if (channel.target_path == "rotation")
		{
			const auto& values = current_animation.rotations.at(sampler.output);
			XMStoreFloat4(
				&target.rotation,
				XMQuaternionNormalize(XMQuaternionSlerp(
					XMLoadFloat4(&values.at(keyframe_index)),
					XMLoadFloat4(&values.at(next_keyframe_index)),
					interpolation_factor)));
		}
		else if (channel.target_path == "translation")
		{
			const auto& values = current_animation.translations.at(sampler.output);
			XMStoreFloat3(
				&target.translation,
				XMVectorLerp(
					XMLoadFloat3(&values.at(keyframe_index)),
					XMLoadFloat3(&values.at(next_keyframe_index)),
					interpolation_factor));
		}
	}

	cumulate_transforms(animated_nodes);
}

void gltf_model::render(
	ID3D11DeviceContext* immediate_context,
	const DirectX::XMFLOAT4X4& world,
	const std::vector<node>& animated_nodes)
{
	using namespace DirectX;
	if (!loaded || scenes.empty() ||
		default_scene < 0 || default_scene >= static_cast<int>(scenes.size()))
	{
		return;
	}
	const std::vector<node>& render_nodes = animated_nodes.empty() ? nodes : animated_nodes;
	_ASSERT_EXPR(render_nodes.size() == nodes.size(), L"Animated glTF node count does not match.");
	if (render_nodes.size() != nodes.size())
	{
		return;
	}

	// UNIT34のシェーダーとSoA入力レイアウトを設定し、三角形リストとして描画する。
	immediate_context->VSSetShader(vertex_shader.Get(), nullptr, 0);
	immediate_context->PSSetShader(pixel_shader.Get(), nullptr, 0);
	immediate_context->IASetInputLayout(input_layout.Get());
	immediate_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	// UNIT38: baseColorだけでなく法線・金属度・粗さ・AOも、モデル専用のREPEATサンプラーで読む。
	ID3D11SamplerState* material_samplers[3]
	{
		material_sampler_states[0].Get(),
		material_sampler_states[1].Get(),
		material_sampler_states[2].Get()
	};
	immediate_context->PSSetSamplers(0, _countof(material_samplers), material_samplers);
	// UNIT35: t0へマテリアル配列を結び、Primitiveのmaterial番号で色を選択する。
	ID3D11ShaderResourceView* material_view = material_resource_view.Get();
	immediate_context->PSSetShaderResources(0, 1, &material_view);

	const char* attribute_names[6]
	{
		"POSITION",
		"NORMAL",
		"TANGENT",
		"TEXCOORD_0",
		"JOINTS_0",
		"WEIGHTS_0"
	};

	std::function<void(int)> traverse = [&](int node_index)
	{
		_ASSERT_EXPR(
			node_index >= 0 && node_index < static_cast<int>(render_nodes.size()),
			L"glTF render node index is out of range.");
		if (node_index < 0 || node_index >= static_cast<int>(render_nodes.size()))
		{
			return;
		}

		const node& current_node = render_nodes.at(node_index);
		if (current_node.skin > -1)
		{
			_ASSERT_EXPR(current_node.skin < static_cast<int>(skins.size()), L"glTF skin index is out of range.");
			if (current_node.skin < static_cast<int>(skins.size()))
			{
				const skin& current_skin = skins.at(current_node.skin);
				primitive_joint_constants joint_data{};
				const size_t joint_count = (std::min)(
					current_skin.joints.size(),
					PRIMITIVE_MAX_JOINTS);
				for (size_t joint_index = 0; joint_index < joint_count; ++joint_index)
				{
					const int joint_node_index = current_skin.joints.at(joint_index);
					_ASSERT_EXPR(
						joint_node_index >= 0 && joint_node_index < static_cast<int>(render_nodes.size()),
						L"glTF joint node index is out of range.");
					if (joint_node_index < 0 || joint_node_index >= static_cast<int>(render_nodes.size()))
					{
						continue;
					}

					// メッシュノード空間へ戻した関節行列を作り、頂点のウェイト合成に使用する。
					XMStoreFloat4x4(
						&joint_data.matrices[joint_index],
						XMLoadFloat4x4(&current_skin.inverse_bind_matrices.at(joint_index)) *
						XMLoadFloat4x4(&render_nodes.at(joint_node_index).global_transform) *
						XMMatrixInverse(nullptr, XMLoadFloat4x4(&current_node.global_transform)));
				}
				immediate_context->UpdateSubresource(
					primitive_joint_constant_buffer.Get(),
					0,
					nullptr,
					&joint_data,
					0,
					0);
				ID3D11Buffer* joint_buffer = primitive_joint_constant_buffer.Get();
				immediate_context->VSSetConstantBuffers(2, 1, &joint_buffer);
			}
		}
		if (current_node.mesh > -1)
		{
			_ASSERT_EXPR(
				current_node.mesh < static_cast<int>(meshes.size()),
				L"glTF mesh index is out of range.");
			if (current_node.mesh < static_cast<int>(meshes.size()))
			{
				const mesh& current_mesh = meshes.at(current_node.mesh);
				for (const mesh::primitive& primitive : current_mesh.primitives)
				{
					const int material_index =
						primitive.material >= 0 && primitive.material < static_cast<int>(materials.size()) ?
						primitive.material : 0;
					const material& current_material = materials.at(material_index);

					// UNIT36: baseColor、metallic/roughness、normal、emissive、occlusionをt1～t5へ並べる。
					const int texture_indices[5]
					{
						current_material.data.pbr_metallic_roughness_data.basecolor_texture.index,
						current_material.data.pbr_metallic_roughness_data.metallic_roughness_texture.index,
						current_material.data.normal_texture.index,
						current_material.data.emissive_texture.index,
						current_material.data.occlusion_texture.index
					};
					ID3D11ShaderResourceView* material_texture_views[5]{};
					for (size_t slot = 0; slot < _countof(texture_indices); ++slot)
					{
						const int texture_index = texture_indices[slot];
						if (texture_index < 0 || texture_index >= static_cast<int>(textures.size()))
						{
							continue;
						}

						const int image_index = textures.at(texture_index).source;
						if (image_index >= 0 && image_index < static_cast<int>(texture_resource_views.size()))
						{
							material_texture_views[slot] = texture_resource_views.at(image_index).Get();
						}
					}
					immediate_context->PSSetShaderResources(
						1,
						_countof(material_texture_views),
						material_texture_views);

					// 存在する属性だけ対応スロットへ設定し、未収録属性はnullバッファにする。
					ID3D11Buffer* vertex_buffers[6]{};
					UINT strides[6]{};
					UINT offsets[6]{};
					for (size_t slot = 0; slot < _countof(attribute_names); ++slot)
					{
						if (!primitive.has(attribute_names[slot]))
						{
							continue;
						}

						const buffer_view& view =
							primitive.vertex_buffer_views.at(attribute_names[slot]);
						_ASSERT_EXPR(
							view.buffer >= 0 && view.buffer < static_cast<int>(buffers.size()),
							L"glTF vertex buffer index is out of range.");
						if (view.buffer < 0 || view.buffer >= static_cast<int>(buffers.size()))
						{
							continue;
						}

						vertex_buffers[slot] = buffers.at(view.buffer).Get();
						strides[slot] = static_cast<UINT>(view.stride_in_bytes);
						offsets[slot] = static_cast<UINT>(view.byte_offset);
					}
					immediate_context->IASetVertexBuffers(
						0,
						_countof(vertex_buffers),
						vertex_buffers,
						strides,
						offsets);

					// glTF内のノード階層変換へ、呼び出し側の配置・座標系変換を合成する。
					primitive_constants primitive_data{};
					primitive_data.material = material_index;
					primitive_data.has_tangent = primitive.has("TANGENT") ? 1 : 0;
					primitive_data.skin = current_node.skin;
					XMStoreFloat4x4(
						&primitive_data.world,
						XMLoadFloat4x4(&current_node.global_transform) *
						XMLoadFloat4x4(&world));
					immediate_context->UpdateSubresource(
						primitive_constant_buffer.Get(),
						0,
						nullptr,
						&primitive_data,
						0,
						0);
					ID3D11Buffer* primitive_buffer = primitive_constant_buffer.Get();
					immediate_context->VSSetConstantBuffers(0, 1, &primitive_buffer);
					immediate_context->PSSetConstantBuffers(0, 1, &primitive_buffer);

					if (primitive.index_buffer_view.buffer > -1)
					{
						const buffer_view& index_view = primitive.index_buffer_view;
						_ASSERT_EXPR(
							index_view.buffer < static_cast<int>(buffers.size()),
							L"glTF index buffer index is out of range.");
						if (index_view.buffer < static_cast<int>(buffers.size()))
						{
							immediate_context->IASetIndexBuffer(
								buffers.at(index_view.buffer).Get(),
								index_view.format,
								static_cast<UINT>(index_view.byte_offset));
							immediate_context->DrawIndexed(
								static_cast<UINT>(index_view.count),
								0,
								0);
						}
					}
					else if (primitive.has("POSITION"))
					{
						immediate_context->Draw(
							static_cast<UINT>(
								primitive.vertex_buffer_views.at("POSITION").count),
							0);
					}
				}
			}
		}

		for (const int child_index : current_node.children)
		{
			traverse(child_index);
		}
	};

	// デフォルトシーンのルートノードから全子ノードを再帰的に描画する。
	for (const int root_node_index : scenes.at(default_scene).nodes)
	{
		traverse(root_node_index);
	}
}
