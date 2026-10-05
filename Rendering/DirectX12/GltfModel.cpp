#include "GltfModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace
{
	std::size_t component_size(int component_type)
	{
		switch (component_type)
		{
		case TINYGLTF_COMPONENT_TYPE_BYTE:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return 1;
		case TINYGLTF_COMPONENT_TYPE_SHORT:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return 2;
		case TINYGLTF_COMPONENT_TYPE_INT:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
		case TINYGLTF_COMPONENT_TYPE_FLOAT: return 4;
		case TINYGLTF_COMPONENT_TYPE_DOUBLE: return 8;
		default: return 0;
		}
	}

	int component_count(int type)
	{
		switch (type)
		{
		case TINYGLTF_TYPE_SCALAR: return 1;
		case TINYGLTF_TYPE_VEC2: return 2;
		case TINYGLTF_TYPE_VEC3: return 3;
		case TINYGLTF_TYPE_VEC4: return 4;
		case TINYGLTF_TYPE_MAT4: return 16;
		default: return 0;
		}
	}

	const unsigned char* element_address(const tinygltf::Model& model,
		const tinygltf::Accessor& accessor, std::size_t element)
	{
		const tinygltf::BufferView& view = model.bufferViews.at(accessor.bufferView);
		const tinygltf::Buffer& buffer = model.buffers.at(view.buffer);
		const int accessor_stride = accessor.ByteStride(view);
		const std::size_t packed_stride = component_size(accessor.componentType) * component_count(accessor.type);
		const std::size_t stride = accessor_stride > 0 ? static_cast<std::size_t>(accessor_stride) : packed_stride;
		return buffer.data.data() + view.byteOffset + accessor.byteOffset + stride * element;
	}

	float read_float_component(const unsigned char* data, int component_type, bool normalized)
	{
		switch (component_type)
		{
		case TINYGLTF_COMPONENT_TYPE_FLOAT: { float v; std::memcpy(&v, data, sizeof(v)); return v; }
		case TINYGLTF_COMPONENT_TYPE_DOUBLE: { double v; std::memcpy(&v, data, sizeof(v)); return static_cast<float>(v); }
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			return normalized ? static_cast<float>(*data) / 255.0f : static_cast<float>(*data);
		case TINYGLTF_COMPONENT_TYPE_BYTE:
			return normalized ? (std::max)(-1.0f, static_cast<float>(*reinterpret_cast<const std::int8_t*>(data)) / 127.0f) : static_cast<float>(*reinterpret_cast<const std::int8_t*>(data));
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: { std::uint16_t v; std::memcpy(&v, data, sizeof(v)); return normalized ? static_cast<float>(v) / 65535.0f : static_cast<float>(v); }
		case TINYGLTF_COMPONENT_TYPE_SHORT: { std::int16_t v; std::memcpy(&v, data, sizeof(v)); return normalized ? (std::max)(-1.0f, static_cast<float>(v) / 32767.0f) : static_cast<float>(v); }
		default: return 0.0f;
		}
	}

	std::uint32_t read_uint_component(const unsigned char* data, int component_type)
	{
		switch (component_type)
		{
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return *data;
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: { std::uint16_t v; std::memcpy(&v, data, sizeof(v)); return v; }
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: { std::uint32_t v; std::memcpy(&v, data, sizeof(v)); return v; }
		default: return 0;
		}
	}

	void read_float_vector(const tinygltf::Model& model, int accessor_index,
		std::size_t element, float* output, int output_count)
	{
		const tinygltf::Accessor& accessor = model.accessors.at(accessor_index);
		const unsigned char* source = element_address(model, accessor, element);
		const std::size_t step = component_size(accessor.componentType);
		const int count = (std::min)(output_count, component_count(accessor.type));
		for (int i = 0; i < count; ++i)
			output[i] = read_float_component(source + step * i, accessor.componentType, accessor.normalized);
	}

	void read_uint_vector(const tinygltf::Model& model, int accessor_index,
		std::size_t element, std::uint32_t* output, int output_count)
	{
		const tinygltf::Accessor& accessor = model.accessors.at(accessor_index);
		const unsigned char* source = element_address(model, accessor, element);
		const std::size_t step = component_size(accessor.componentType);
		const int count = (std::min)(output_count, component_count(accessor.type));
		for (int i = 0; i < count; ++i) output[i] = read_uint_component(source + step * i, accessor.componentType);
	}

	void resource_barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
		D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = resource;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = before;
		barrier.Transition.StateAfter = after;
		list->ResourceBarrier(1, &barrier);
	}

	bool decode_wic_image(const tinygltf::Model& model, const tinygltf::Image& image,
		const std::filesystem::path& base_directory, std::vector<std::uint8_t>& rgba,
		UINT& width, UINT& height)
	{
		std::vector<std::uint8_t> encoded;
		if (image.bufferView >= 0 && image.bufferView < static_cast<int>(model.bufferViews.size()))
		{
			const tinygltf::BufferView& view = model.bufferViews[image.bufferView];
			const tinygltf::Buffer& buffer = model.buffers.at(view.buffer);
			if (view.byteOffset + view.byteLength > buffer.data.size()) return false;
			encoded.assign(buffer.data.begin() + view.byteOffset,
				buffer.data.begin() + view.byteOffset + view.byteLength);
		}
		else if (!image.uri.empty() && image.uri.rfind("data:", 0) != 0)
		{
			std::ifstream input(base_directory / std::filesystem::path(image.uri), std::ios::binary);
			if (!input) return false;
			encoded.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
		}
		else return false;
		if (encoded.empty() || encoded.size() > UINT_MAX) return false;

		const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool uninitialize_com = SUCCEEDED(com_result);
		ComPtr<IWICImagingFactory> factory;
		ComPtr<IWICStream> stream;
		ComPtr<IWICBitmapDecoder> decoder;
		ComPtr<IWICBitmapFrameDecode> frame;
		ComPtr<IWICFormatConverter> converter;
		bool result = false;
		if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(factory.GetAddressOf()))) &&
			SUCCEEDED(factory->CreateStream(stream.GetAddressOf())) &&
			SUCCEEDED(stream->InitializeFromMemory(encoded.data(), static_cast<DWORD>(encoded.size()))) &&
			SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand,
				decoder.GetAddressOf())) && SUCCEEDED(decoder->GetFrame(0, frame.GetAddressOf())) &&
			SUCCEEDED(frame->GetSize(&width, &height)) && width > 0 && height > 0 &&
			SUCCEEDED(factory->CreateFormatConverter(converter.GetAddressOf())) &&
			SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
				WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
		{
			const std::size_t row_bytes = static_cast<std::size_t>(width) * 4;
			if (row_bytes <= UINT_MAX && row_bytes * height <= UINT_MAX)
			{
				rgba.resize(row_bytes * height);
				result = SUCCEEDED(converter->CopyPixels(nullptr, static_cast<UINT>(row_bytes),
					static_cast<UINT>(rgba.size()), rgba.data()));
			}
		}
		if (uninitialize_com) CoUninitialize();
		return result;
	}
}

gltf_model12::gltf_model12(std::shared_ptr<directx12_device> device)
	: device_(std::move(device))
{
}

gltf_model12::~gltf_model12()
{
	release_mesh_descriptors();
	release_texture_descriptors();
}

bool gltf_model12::load(const std::string& filename)
{
	loaded_ = false;
	error_.clear();
	filename_ = filename;
	release_mesh_descriptors();
	release_texture_descriptors();
	textures_.clear();
	materials_.clear();
	source_ = {};
	tinygltf::TinyGLTF loader;
	std::string warning;
	const std::filesystem::path path(filename);
	bool result = false;
	if (path.extension() == ".glb") result = loader.LoadBinaryFromFile(&source_, &error_, &warning, filename);
	else if (path.extension() == ".gltf") result = loader.LoadASCIIFromFile(&source_, &error_, &warning, filename);
	if (!result)
	{
		if (error_.empty()) error_ = "glTF/GLBの読み込みに失敗しました。";
		return false;
	}
	default_scene_ = source_.defaultScene >= 0 ? source_.defaultScene : 0;
	read_nodes();
	read_skins();
	read_animations();
	read_materials();
	loaded_ = build_gpu_resources();
	return loaded_;
}

bool gltf_model12::build_gpu_resources()
{
	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList> command_list;
	if (FAILED(device_->get()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.GetAddressOf()))) ||
		FAILED(device_->get()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
			IID_PPV_ARGS(command_list.GetAddressOf())))) return false;

	std::vector<ComPtr<ID3D12Resource>> uploads;
	if (!build_textures(command_list.Get(), uploads)) return false;
	meshes_.clear();
	bool bounds_initialized = false;
	for (const tinygltf::Mesh& source_mesh : source_.meshes)
	{
		mesh& target_mesh = meshes_.emplace_back();
		target_mesh.name = source_mesh.name;
		for (const tinygltf::Primitive& source_primitive : source_mesh.primitives)
		{
			const auto position_it = source_primitive.attributes.find("POSITION");
			if (position_it == source_primitive.attributes.end() || source_primitive.indices < 0) continue;
			const tinygltf::Accessor& positions = source_.accessors.at(position_it->second);
			std::vector<gltf_vertex12> vertices(positions.count);
			for (std::size_t vertex_index = 0; vertex_index < vertices.size(); ++vertex_index)
			{
				gltf_vertex12& vertex = vertices[vertex_index];
				read_float_vector(source_, position_it->second, vertex_index, &vertex.position.x, 3);
				auto read_attribute = [&](const char* name, float* output, int count)
				{
					const auto it = source_primitive.attributes.find(name);
					if (it != source_primitive.attributes.end()) read_float_vector(source_, it->second, vertex_index, output, count);
				};
				read_attribute("NORMAL", &vertex.normal.x, 3);
				read_attribute("TANGENT", &vertex.tangent.x, 4);
				read_attribute("TEXCOORD_0", &vertex.texcoord.x, 2);
				read_attribute("WEIGHTS_0", &vertex.weights.x, 4);
				const auto joints_it = source_primitive.attributes.find("JOINTS_0");
				if (joints_it != source_primitive.attributes.end())
					read_uint_vector(source_, joints_it->second, vertex_index, &vertex.joints.x, 4);
				if (!bounds_initialized)
				{
					bounds_min_ = bounds_max_ = vertex.position;
					bounds_initialized = true;
				}
				bounds_min_.x = (std::min)(bounds_min_.x, vertex.position.x);
				bounds_min_.y = (std::min)(bounds_min_.y, vertex.position.y);
				bounds_min_.z = (std::min)(bounds_min_.z, vertex.position.z);
				bounds_max_.x = (std::max)(bounds_max_.x, vertex.position.x);
				bounds_max_.y = (std::max)(bounds_max_.y, vertex.position.y);
				bounds_max_.z = (std::max)(bounds_max_.z, vertex.position.z);
			}

			const tinygltf::Accessor& index_accessor = source_.accessors.at(source_primitive.indices);
			std::vector<std::uint32_t> indices(index_accessor.count);
			for (std::size_t i = 0; i < indices.size(); ++i)
				indices[i] = read_uint_component(element_address(source_, index_accessor, i), index_accessor.componentType);

			primitive& target = target_mesh.primitives.emplace_back();
			target.material = source_primitive.material;
			target.index_count = static_cast<std::uint32_t>(indices.size());
			ComPtr<ID3D12Resource> vertex_upload;
			ComPtr<ID3D12Resource> index_upload;
			if (!upload_buffer(command_list.Get(), vertices.data(), vertices.size() * sizeof(gltf_vertex12),
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				target.vertex_buffer, vertex_upload) ||
				!upload_buffer(command_list.Get(), indices.data(), indices.size() * sizeof(std::uint32_t),
					D3D12_RESOURCE_STATE_INDEX_BUFFER, target.index_buffer, index_upload)) return false;
			uploads.push_back(vertex_upload);
			uploads.push_back(index_upload);
			target.vertex_view.BufferLocation = target.vertex_buffer->GetGPUVirtualAddress();
			target.vertex_view.SizeInBytes = static_cast<UINT>(vertices.size() * sizeof(gltf_vertex12));
			target.vertex_view.StrideInBytes = sizeof(gltf_vertex12);
			target.index_view.BufferLocation = target.index_buffer->GetGPUVirtualAddress();
			target.index_view.SizeInBytes = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
			target.index_view.Format = DXGI_FORMAT_R32_UINT;

			// 1 Meshletを最大64頂点・126三角形へ分割する。静的／スキンモデル共通の入力形式。
			std::vector<primitive::meshlet> meshlets;
			std::vector<std::uint32_t> meshlet_vertices;
			std::vector<std::uint32_t> meshlet_triangles;
			primitive::meshlet current{};
			std::vector<std::uint32_t> local_vertices;
			auto flush_meshlet = [&]()
			{
				if (!current.triangle_count) return;
				current.vertex_count = static_cast<std::uint32_t>(local_vertices.size());
				meshlets.push_back(current);
				meshlet_vertices.insert(meshlet_vertices.end(), local_vertices.begin(), local_vertices.end());
				current.vertex_offset += current.vertex_count;
				current.triangle_offset += current.triangle_count;
				current.vertex_count = current.triangle_count = 0;
				local_vertices.clear();
			};
			for (std::size_t triangle = 0; triangle + 2 < indices.size(); triangle += 3)
			{
				std::uint32_t global[3]{ indices[triangle], indices[triangle + 1], indices[triangle + 2] };
				std::uint32_t new_vertices = 0;
				for (std::uint32_t index : global)
					if (std::find(local_vertices.begin(), local_vertices.end(), index) == local_vertices.end()) ++new_vertices;
				if (current.triangle_count >= 126 || local_vertices.size() + new_vertices > 64) flush_meshlet();
				std::uint32_t local[3]{};
				for (int corner = 0; corner < 3; ++corner)
				{
					auto found = std::find(local_vertices.begin(), local_vertices.end(), global[corner]);
					if (found == local_vertices.end()) { local_vertices.push_back(global[corner]); local[corner] = static_cast<std::uint32_t>(local_vertices.size() - 1); }
					else local[corner] = static_cast<std::uint32_t>(found - local_vertices.begin());
				}
				meshlet_triangles.push_back(local[0] | (local[1] << 10) | (local[2] << 20));
				++current.triangle_count;
			}
			flush_meshlet();
			if (!meshlets.empty())
			{
				ComPtr<ID3D12Resource> meshlet_upload, vertex_index_upload, triangle_upload;
				const auto shader_resource = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
				if (upload_buffer(command_list.Get(), meshlets.data(), meshlets.size() * sizeof(primitive::meshlet),
					shader_resource, target.meshlet_buffer, meshlet_upload) &&
					upload_buffer(command_list.Get(), meshlet_vertices.data(), meshlet_vertices.size() * sizeof(std::uint32_t),
						shader_resource, target.meshlet_vertex_indices, vertex_index_upload) &&
					upload_buffer(command_list.Get(), meshlet_triangles.data(), meshlet_triangles.size() * sizeof(std::uint32_t),
						shader_resource, target.meshlet_triangles, triangle_upload))
				{
					uploads.push_back(meshlet_upload); uploads.push_back(vertex_index_upload); uploads.push_back(triangle_upload);
					target.meshlet_count = static_cast<std::uint32_t>(meshlets.size());
					auto create_srv = [&](ID3D12Resource* resource, UINT elements, UINT stride, descriptor_handle& handle)
					{
						handle = device_->shader_descriptors().allocate();
						if (!handle.valid()) return false;
						D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
						srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
						srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
						srv.Format = DXGI_FORMAT_UNKNOWN;
						srv.Buffer.NumElements = elements;
						srv.Buffer.StructureByteStride = stride;
						device_->get()->CreateShaderResourceView(resource, &srv, handle.cpu);
						return true;
					};
					if (!create_srv(target.vertex_buffer.Get(), static_cast<UINT>(vertices.size()), sizeof(gltf_vertex12), target.vertex_srv) ||
						!create_srv(target.meshlet_buffer.Get(), static_cast<UINT>(meshlets.size()), sizeof(primitive::meshlet), target.meshlet_srv) ||
						!create_srv(target.meshlet_vertex_indices.Get(), static_cast<UINT>(meshlet_vertices.size()), sizeof(std::uint32_t), target.meshlet_vertex_indices_srv) ||
						!create_srv(target.meshlet_triangles.Get(), static_cast<UINT>(meshlet_triangles.size()), sizeof(std::uint32_t), target.meshlet_triangles_srv))
						target.meshlet_count = 0;
				}
			}
		}
	}
	if (FAILED(command_list->Close())) return false;
	ID3D12CommandList* lists[]{ command_list.Get() };
	const std::uint64_t fence = device_->graphics_queue().execute(lists, _countof(lists));
	return device_->graphics_queue().wait(fence);
}

bool gltf_model12::upload_texture(ID3D12GraphicsCommandList* command_list,
	const std::vector<std::uint8_t>& rgba, UINT width, UINT height, texture& destination,
	ComPtr<ID3D12Resource>& upload)
{
	if (!command_list || rgba.size() < static_cast<std::size_t>(width) * height * 4) return false;
	D3D12_RESOURCE_DESC texture_desc{};
	texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texture_desc.Width = width;
	texture_desc.Height = height;
	texture_desc.DepthOrArraySize = 1;
	texture_desc.MipLevels = 1;
	texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texture_desc.SampleDesc.Count = 1;
	D3D12_HEAP_PROPERTIES default_heap{};
	default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	default_heap.CreationNodeMask = default_heap.VisibleNodeMask = 1;
	if (FAILED(device_->get()->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE,
		&texture_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(destination.resource.GetAddressOf())))) return false;

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
	UINT rows = 0;
	UINT64 row_size = 0;
	UINT64 upload_size = 0;
	device_->get()->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, &rows, &row_size, &upload_size);
	D3D12_RESOURCE_DESC upload_desc{};
	upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	upload_desc.Width = upload_size;
	upload_desc.Height = 1;
	upload_desc.DepthOrArraySize = 1;
	upload_desc.MipLevels = 1;
	upload_desc.SampleDesc.Count = 1;
	upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	D3D12_HEAP_PROPERTIES upload_heap = default_heap;
	upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	if (FAILED(device_->get()->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE,
		&upload_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload.GetAddressOf())))) return false;
	std::uint8_t* mapped = nullptr;
	D3D12_RANGE no_read{ 0, 0 };
	if (FAILED(upload->Map(0, &no_read, reinterpret_cast<void**>(&mapped)))) return false;
	const std::size_t source_pitch = static_cast<std::size_t>(width) * 4;
	for (UINT row = 0; row < height; ++row)
		std::memcpy(mapped + footprint.Offset + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
			rgba.data() + static_cast<std::size_t>(row) * source_pitch, source_pitch);
	upload->Unmap(0, nullptr);
	D3D12_TEXTURE_COPY_LOCATION source{};
	source.pResource = upload.Get();
	source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	source.PlacedFootprint = footprint;
	D3D12_TEXTURE_COPY_LOCATION target{};
	target.pResource = destination.resource.Get();
	target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	command_list->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
	resource_barrier(command_list, destination.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	destination.srv = device_->shader_descriptors().allocate();
	if (!destination.srv.valid()) return false;
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = texture_desc.Format;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	device_->get()->CreateShaderResourceView(destination.resource.Get(), &srv, destination.srv.cpu);
	return true;
}

bool gltf_model12::build_textures(ID3D12GraphicsCommandList* command_list,
	std::vector<ComPtr<ID3D12Resource>>& uploads)
{
	textures_.resize(source_.textures.size());
	const std::filesystem::path base = std::filesystem::path(filename_).parent_path();
	for (std::size_t index = 0; index < source_.textures.size(); ++index)
	{
		const int image_index = source_.textures[index].source;
		if (image_index < 0 || image_index >= static_cast<int>(source_.images.size())) continue;
		std::vector<std::uint8_t> pixels;
		UINT width = 0, height = 0;
		if (!decode_wic_image(source_, source_.images[image_index], base, pixels, width, height)) continue;
		ComPtr<ID3D12Resource> upload;
		if (!upload_texture(command_list, pixels, width, height, textures_[index], upload)) return false;
		uploads.push_back(std::move(upload));
	}
	return true;
}

void gltf_model12::read_materials()
{
	materials_.resize(source_.materials.size());
	for (std::size_t index = 0; index < source_.materials.size(); ++index)
	{
		const tinygltf::Material& source = source_.materials[index];
		material& target = materials_[index];
		const auto& pbr = source.pbrMetallicRoughness;
		if (pbr.baseColorFactor.size() == 4)
			target.base_color = { static_cast<float>(pbr.baseColorFactor[0]), static_cast<float>(pbr.baseColorFactor[1]),
				static_cast<float>(pbr.baseColorFactor[2]), static_cast<float>(pbr.baseColorFactor[3]) };
		target.metallic = static_cast<float>(pbr.metallicFactor);
		target.roughness = static_cast<float>(pbr.roughnessFactor);
		target.base_color_texture = pbr.baseColorTexture.index;
		target.metallic_roughness_texture = pbr.metallicRoughnessTexture.index;
		target.normal_texture = source.normalTexture.index;
		target.occlusion_texture = source.occlusionTexture.index;
		target.emissive_texture = source.emissiveTexture.index;
		target.alpha_mode = source.alphaMode == "MASK" ? 1 : source.alphaMode == "BLEND" ? 2 : 0;
		target.alpha_cutoff = static_cast<float>(source.alphaCutoff);
		target.double_sided = source.doubleSided;
		if (source.emissiveFactor.size() == 3)
			target.emissive = { static_cast<float>(source.emissiveFactor[0]), static_cast<float>(source.emissiveFactor[1]),
				static_cast<float>(source.emissiveFactor[2]) };
	}
}

D3D12_GPU_DESCRIPTOR_HANDLE gltf_model12::texture_srv(int texture_index) const
{
	if (texture_index < 0 || texture_index >= static_cast<int>(textures_.size()) ||
		!textures_[texture_index].srv.valid()) return {};
	return textures_[texture_index].srv.gpu;
}

std::size_t gltf_model12::loaded_texture_count() const
{
	return static_cast<std::size_t>(std::count_if(textures_.begin(), textures_.end(),
		[](const texture& value) { return value.resource && value.srv.valid(); }));
}

void gltf_model12::release_texture_descriptors()
{
	if (!device_) return;
	for (texture& value : textures_)
		if (value.srv.valid()) device_->shader_descriptors().release(value.srv);
}

void gltf_model12::release_mesh_descriptors()
{
	if (!device_) return;
	for (mesh& model_mesh : meshes_)
		for (primitive& value : model_mesh.primitives)
		{
			descriptor_handle* handles[]{ &value.vertex_srv, &value.meshlet_srv,
				&value.meshlet_vertex_indices_srv, &value.meshlet_triangles_srv };
			for (descriptor_handle* handle : handles)
				if (handle->valid()) { device_->shader_descriptors().release(*handle); *handle = {}; }
		}
}

bool gltf_model12::upload_buffer(ID3D12GraphicsCommandList* command_list, const void* data,
	std::size_t size, D3D12_RESOURCE_STATES final_state, ComPtr<ID3D12Resource>& destination,
	ComPtr<ID3D12Resource>& upload)
{
	if (!data || size == 0 || size > UINT64_MAX) return false;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = size;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	D3D12_HEAP_PROPERTIES default_heap{};
	default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	default_heap.CreationNodeMask = default_heap.VisibleNodeMask = 1;
	D3D12_HEAP_PROPERTIES upload_heap = default_heap;
	upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	if (FAILED(device_->get()->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(destination.GetAddressOf()))) ||
		FAILED(device_->get()->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload.GetAddressOf())))) return false;
	void* mapped = nullptr;
	D3D12_RANGE read_range{ 0, 0 };
	if (FAILED(upload->Map(0, &read_range, &mapped))) return false;
	std::memcpy(mapped, data, size);
	upload->Unmap(0, nullptr);
	command_list->CopyBufferRegion(destination.Get(), 0, upload.Get(), 0, size);
	resource_barrier(command_list, destination.Get(), D3D12_RESOURCE_STATE_COPY_DEST, final_state);
	return true;
}

void gltf_model12::read_nodes()
{
	nodes_.resize(source_.nodes.size());
	for (std::size_t index = 0; index < source_.nodes.size(); ++index)
	{
		const tinygltf::Node& source = source_.nodes[index];
		node& target = nodes_[index];
		target.name = source.name;
		target.mesh = source.mesh;
		target.skin = source.skin;
		target.children = source.children;
		for (int child : source.children) if (child >= 0 && child < static_cast<int>(nodes_.size())) nodes_[child].parent = static_cast<int>(index);
		if (source.scale.size() == 3) target.scale = { static_cast<float>(source.scale[0]), static_cast<float>(source.scale[1]), static_cast<float>(source.scale[2]) };
		if (source.rotation.size() == 4) target.rotation = { static_cast<float>(source.rotation[0]), static_cast<float>(source.rotation[1]), static_cast<float>(source.rotation[2]), static_cast<float>(source.rotation[3]) };
		if (source.translation.size() == 3) target.translation = { static_cast<float>(source.translation[0]), static_cast<float>(source.translation[1]), static_cast<float>(source.translation[2]) };
		if (source.matrix.size() == 16)
		{
			XMFLOAT4X4 matrix{};
			for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
				matrix.m[row][column] = static_cast<float>(source.matrix[row * 4 + column]);
			XMVECTOR scale, rotation, translation;
			if (XMMatrixDecompose(&scale, &rotation, &translation, XMLoadFloat4x4(&matrix)))
			{
				XMStoreFloat3(&target.scale, scale);
				XMStoreFloat4(&target.rotation, rotation);
				XMStoreFloat3(&target.translation, translation);
			}
		}
	}
	update_global_transforms();
	bind_nodes_ = nodes_;
}

void gltf_model12::read_skins()
{
	skins_.resize(source_.skins.size());
	for (std::size_t index = 0; index < source_.skins.size(); ++index)
	{
		const tinygltf::Skin& source_skin = source_.skins[index];
		skin& target = skins_[index];
		target.joints = source_skin.joints;
		target.inverse_bind_matrices.resize(source_skin.joints.size());
		for (auto& matrix : target.inverse_bind_matrices) XMStoreFloat4x4(&matrix, XMMatrixIdentity());
		if (source_skin.inverseBindMatrices >= 0)
		{
			for (std::size_t joint = 0; joint < target.inverse_bind_matrices.size(); ++joint)
				read_float_vector(source_, source_skin.inverseBindMatrices, joint,
					&target.inverse_bind_matrices[joint]._11, 16);
		}
	}
}

void gltf_model12::read_animations()
{
	animations_.clear();
	for (const tinygltf::Animation& source_animation : source_.animations)
	{
		animation& target = animations_.emplace_back();
		target.name = source_animation.name;
		for (const tinygltf::AnimationSampler& source_sampler : source_animation.samplers)
		{
			auto& sampler = target.samplers.emplace_back();
			sampler.input = source_sampler.input;
			sampler.output = source_sampler.output;
			sampler.interpolation = source_sampler.interpolation;
			if (target.timelines.find(sampler.input) == target.timelines.end())
			{
				const tinygltf::Accessor& accessor = source_.accessors.at(sampler.input);
				auto& timeline = target.timelines[sampler.input];
				timeline.resize(accessor.count);
				for (std::size_t key = 0; key < timeline.size(); ++key)
					read_float_vector(source_, sampler.input, key, &timeline[key], 1);
			}
		}

		for (const tinygltf::AnimationChannel& source_channel : source_animation.channels)
		{
			auto& channel = target.channels.emplace_back();
			channel.sampler = source_channel.sampler;
			channel.target_node = source_channel.target_node;
			channel.target_path = source_channel.target_path;
			if (channel.sampler < 0 || channel.sampler >= static_cast<int>(target.samplers.size())) continue;
			const int output = target.samplers[channel.sampler].output;
			const tinygltf::Accessor& accessor = source_.accessors.at(output);
			if (channel.target_path == "scale" || channel.target_path == "translation")
			{
				auto& values = channel.target_path == "scale" ? target.scales[output] : target.translations[output];
				if (values.empty())
				{
					values.resize(accessor.count);
					for (std::size_t key = 0; key < values.size(); ++key)
						read_float_vector(source_, output, key, &values[key].x, 3);
				}
			}
			else if (channel.target_path == "rotation")
			{
				auto& values = target.rotations[output];
				if (values.empty())
				{
					values.resize(accessor.count);
					for (std::size_t key = 0; key < values.size(); ++key)
						read_float_vector(source_, output, key, &values[key].x, 4);
				}
			}
		}

		for (const auto& [accessor, timeline] : target.timelines)
			if (!timeline.empty()) target.duration = (std::max)(target.duration, timeline.back());
	}
}

void gltf_model12::animate(std::size_t animation_index, float time, bool loop)
{
	std::vector<node> pose;
	sample_animation(animation_index, time, loop, pose);
	if (!pose.empty()) nodes_ = std::move(pose);
}

void gltf_model12::sample_animation(std::size_t animation_index, float time, bool loop,
	std::vector<node>& pose) const
{
	pose.clear();
	if (animation_index >= animations_.size()) return;
	pose = bind_nodes_.empty() ? nodes_ : bind_nodes_;
	const animation& clip = animations_[animation_index];
	// ループ無効時は末尾姿勢を保持し、再生終了後に先頭へ戻らないようにする。
	const float positive_time = (std::max)(time, 0.0f);
	const float sample_time = clip.duration > 0.0f ?
		(loop ? std::fmod(positive_time, clip.duration) : (std::min)(positive_time, clip.duration)) : 0.0f;

	for (const animation::channel& channel : clip.channels)
	{
		if (channel.sampler < 0 || channel.sampler >= static_cast<int>(clip.samplers.size()) ||
			channel.target_node < 0 || channel.target_node >= static_cast<int>(nodes_.size())) continue;
		const animation::sampler& sampler = clip.samplers[channel.sampler];
		const auto timeline_it = clip.timelines.find(sampler.input);
		if (timeline_it == clip.timelines.end() || timeline_it->second.empty()) continue;
		const std::vector<float>& timeline = timeline_it->second;
		const auto upper = std::upper_bound(timeline.begin(), timeline.end(), sample_time);
		const std::size_t next = upper == timeline.end() ? timeline.size() - 1 : static_cast<std::size_t>(upper - timeline.begin());
		const std::size_t current = next > 0 ? next - 1 : 0;
		const float interval = timeline[next] - timeline[current];
		float factor = interval > 0.0f ? (sample_time - timeline[current]) / interval : 0.0f;
		if (sampler.interpolation == "STEP") factor = 0.0f;

		// CUBICSPLINEは値と接線が3要素ずつ並ぶため、未対応時に誤った添字で読むことを避ける。
		if (sampler.interpolation == "CUBICSPLINE") continue;
		node& target = pose[channel.target_node];
		if (channel.target_path == "scale")
		{
			const auto& values = clip.scales.at(sampler.output);
			if (current < values.size() && next < values.size()) XMStoreFloat3(&target.scale,
				XMVectorLerp(XMLoadFloat3(&values[current]), XMLoadFloat3(&values[next]), factor));
		}
		else if (channel.target_path == "rotation")
		{
			const auto& values = clip.rotations.at(sampler.output);
			if (current < values.size() && next < values.size()) XMStoreFloat4(&target.rotation,
				XMQuaternionNormalize(XMQuaternionSlerp(XMLoadFloat4(&values[current]), XMLoadFloat4(&values[next]), factor)));
		}
		else if (channel.target_path == "translation")
		{
			const auto& values = clip.translations.at(sampler.output);
			if (current < values.size() && next < values.size()) XMStoreFloat3(&target.translation,
				XMVectorLerp(XMLoadFloat3(&values[current]), XMLoadFloat3(&values[next]), factor));
		}
	}
	for (std::size_t index = 0; index < pose.size(); ++index)
		if (pose[index].parent < 0) calculate_pose_node(pose, static_cast<int>(index), XMMatrixIdentity());
}

void gltf_model12::build_joint_matrices(int skin_index, std::vector<XMFLOAT4X4>& matrices,
	const std::vector<node>* pose) const
{
	matrices.clear();
	if (skin_index < 0 || skin_index >= static_cast<int>(skins_.size())) return;
	const skin& selected_skin = skins_[skin_index];
	matrices.resize(selected_skin.joints.size());
	for (std::size_t index = 0; index < selected_skin.joints.size(); ++index)
	{
		const int joint_node = selected_skin.joints[index];
		const std::vector<node>& evaluated_nodes = pose && pose->size() == nodes_.size() ? *pose : nodes_;
		if (joint_node < 0 || joint_node >= static_cast<int>(evaluated_nodes.size()))
		{
			XMStoreFloat4x4(&matrices[index], XMMatrixIdentity());
			continue;
		}
		// 行ベクトル規約に合わせ、逆バインド姿勢から現在の関節姿勢へ変換する。
		XMStoreFloat4x4(&matrices[index],
			XMLoadFloat4x4(&selected_skin.inverse_bind_matrices[index]) *
			XMLoadFloat4x4(&evaluated_nodes[joint_node].global_transform));
	}
}

void gltf_model12::calculate_node(int node_index, const XMMATRIX& parent)
{
	node& current = nodes_.at(node_index);
	const XMMATRIX local = XMMatrixScaling(current.scale.x, current.scale.y, current.scale.z) *
		XMMatrixRotationQuaternion(XMLoadFloat4(&current.rotation)) *
		XMMatrixTranslation(current.translation.x, current.translation.y, current.translation.z);
	const XMMATRIX global = local * parent;
	XMStoreFloat4x4(&current.global_transform, global);
	for (int child : current.children) calculate_node(child, global);
}

void gltf_model12::calculate_pose_node(std::vector<node>& pose, int node_index, const XMMATRIX& parent)
{
	node& current = pose.at(node_index);
	const XMMATRIX local = XMMatrixScaling(current.scale.x, current.scale.y, current.scale.z) *
		XMMatrixRotationQuaternion(XMLoadFloat4(&current.rotation)) *
		XMMatrixTranslation(current.translation.x, current.translation.y, current.translation.z);
	XMStoreFloat4x4(&current.global_transform, local * parent);
	for (int child : current.children) calculate_pose_node(pose, child, local * parent);
}

void gltf_model12::update_global_transforms()
{
	const XMMATRIX identity = XMMatrixIdentity();
	if (!source_.scenes.empty() && default_scene_ >= 0 && default_scene_ < static_cast<int>(source_.scenes.size()))
	{
		for (int root : source_.scenes[default_scene_].nodes) calculate_node(root, identity);
	}
	else
	{
		for (std::size_t index = 0; index < nodes_.size(); ++index)
			if (nodes_[index].parent < 0) calculate_node(static_cast<int>(index), identity);
	}
}
