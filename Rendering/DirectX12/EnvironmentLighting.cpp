#include "EnvironmentLighting.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
	constexpr std::uint32_t dds_magic = 0x20534444;
	constexpr std::uint32_t d3dfmt_a16b16g16r16f = 113;
	constexpr std::uint32_t dds_caps2_cubemap = 0x200;

	std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
	{
		std::uint32_t value = 0;
		if (offset + sizeof(value) <= bytes.size()) std::memcpy(&value, bytes.data() + offset, sizeof(value));
		return value;
	}

	void transition(ID3D12GraphicsCommandList* commands, ID3D12Resource* resource)
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = resource;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commands->ResourceBarrier(1, &barrier);
	}
}

environment_lighting12::~environment_lighting12()
{
	release(diffuse_);
	release(specular_);
	release(lut_);
}

void environment_lighting12::release(dds_texture& texture)
{
	if (device_ && texture.srv.valid()) device_->shader_descriptors().release(texture.srv);
	texture.srv = {};
	texture.resource.Reset();
}

bool environment_lighting12::initialize(std::shared_ptr<directx12_device> device,
	const std::filesystem::path& directory)
{
	device_ = std::move(device);
	if (!device_) return false;
	// IEMは拡散反射、PMREMは粗さ別鏡面反射、LUTはBRDF積分項として使用する。
	// 元教材の1024px RGBA16Fは合計約145MBある。実用解像度のMipだけを直接読み、起動時転送を約6MBへ抑える。
	return load_rgba16f_dds(directory / L"diffuse_iem.dds", true, 128, diffuse_) &&
		load_rgba16f_dds(directory / L"specular_pmrem.dds", true, 256, specular_) &&
		load_rgba16f_dds(directory / L"lut_ggx.dds", false, 256, lut_);
}

bool environment_lighting12::load_rgba16f_dds(const std::filesystem::path& filename,
	bool require_cube, UINT maximum_dimension, dds_texture& target)
{
	std::ifstream input(filename, std::ios::binary);
	if (!input) return false;
	std::vector<std::uint8_t> bytes(128);
	input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (bytes.size() < 128 || read_u32(bytes, 0) != dds_magic || read_u32(bytes, 4) != 124 ||
		read_u32(bytes, 76) != 32 || read_u32(bytes, 84) != d3dfmt_a16b16g16r16f) return false;
	const UINT source_height = read_u32(bytes, 12);
	const UINT source_width = read_u32(bytes, 16);
	const UINT source_mip_count = (std::max)(read_u32(bytes, 28), 1u);
	const bool cube = (read_u32(bytes, 112) & dds_caps2_cubemap) != 0;
	if (!source_width || !source_height || (require_cube && !cube)) return false;
	UINT first_mip = 0;
	while (first_mip + 1 < source_mip_count &&
		((std::max)(source_width >> first_mip, 1u) > maximum_dimension ||
		 (std::max)(source_height >> first_mip, 1u) > maximum_dimension)) ++first_mip;
	const UINT width = (std::max)(source_width >> first_mip, 1u);
	const UINT height = (std::max)(source_height >> first_mip, 1u);
	const UINT mip_count = source_mip_count - first_mip;
	const UINT array_size = cube ? 6u : 1u;
	const UINT subresource_count = mip_count * array_size;

	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width;
	desc.Height = height;
	desc.DepthOrArraySize = static_cast<UINT16>(array_size);
	desc.MipLevels = static_cast<UINT16>(mip_count);
	desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	desc.SampleDesc.Count = 1;
	D3D12_HEAP_PROPERTIES default_heap{};
	default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	default_heap.CreationNodeMask = default_heap.VisibleNodeMask = 1;
	if (FAILED(device_->get()->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(target.resource.GetAddressOf())))) return false;

	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(subresource_count);
	std::vector<UINT> row_counts(subresource_count);
	std::vector<UINT64> row_sizes(subresource_count);
	UINT64 upload_size = 0;
	device_->get()->GetCopyableFootprints(&desc, 0, subresource_count, 0, footprints.data(),
		row_counts.data(), row_sizes.data(), &upload_size);
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
	ComPtr<ID3D12Resource> upload;
	if (FAILED(device_->get()->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload.GetAddressOf())))) return false;

	std::uint8_t* mapped = nullptr;
	D3D12_RANGE no_read{ 0, 0 };
	if (FAILED(upload->Map(0, &no_read, reinterpret_cast<void**>(&mapped)))) return false;
	std::uint64_t source_offset = 128;
	for (UINT array_index = 0; array_index < array_size; ++array_index)
	{
		UINT mip_width = source_width;
		UINT mip_height = source_height;
		for (UINT source_mip = 0; source_mip < source_mip_count; ++source_mip)
		{
			const std::size_t source_pitch = static_cast<std::size_t>(mip_width) * 8;
			const std::size_t source_size = source_pitch * mip_height;
			if (source_mip >= first_mip)
			{
				const UINT subresource = source_mip - first_mip + array_index * mip_count;
				input.seekg(static_cast<std::streamoff>(source_offset), std::ios::beg);
				for (UINT row = 0; row < mip_height; ++row)
				{
					input.read(reinterpret_cast<char*>(mapped + footprints[subresource].Offset +
						static_cast<std::size_t>(row) * footprints[subresource].Footprint.RowPitch),
						static_cast<std::streamsize>(source_pitch));
					if (!input) { upload->Unmap(0, nullptr); return false; }
				}
			}
			source_offset += source_size;
			mip_width = (std::max)(mip_width / 2, 1u);
			mip_height = (std::max)(mip_height / 2, 1u);
		}
	}
	upload->Unmap(0, nullptr);

	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList> commands;
	if (FAILED(device_->get()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(allocator.GetAddressOf()))) ||
		FAILED(device_->get()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
			IID_PPV_ARGS(commands.GetAddressOf())))) return false;
	for (UINT subresource = 0; subresource < subresource_count; ++subresource)
	{
		D3D12_TEXTURE_COPY_LOCATION source{};
		source.pResource = upload.Get();
		source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		source.PlacedFootprint = footprints[subresource];
		D3D12_TEXTURE_COPY_LOCATION destination{};
		destination.pResource = target.resource.Get();
		destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		destination.SubresourceIndex = subresource;
		commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
	}
	transition(commands.Get(), target.resource.Get());
	if (FAILED(commands->Close())) return false;
	ID3D12CommandList* lists[]{ commands.Get() };
	const std::uint64_t fence = device_->graphics_queue().execute(lists, 1);
	if (!device_->graphics_queue().wait(fence)) return false;

	target.srv = device_->shader_descriptors().allocate();
	if (!target.srv.valid()) return false;
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = desc.Format;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	if (cube)
	{
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srv.TextureCube.MipLevels = mip_count;
	}
	else
	{
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = mip_count;
	}
	device_->get()->CreateShaderResourceView(target.resource.Get(), &srv, target.srv.cpu);
	target.mip_count = mip_count;
	return true;
}
