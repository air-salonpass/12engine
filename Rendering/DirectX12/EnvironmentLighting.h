#pragma once

#include "Device.h"

#include <filesystem>
#include <memory>
#include <wrl.h>

// 教材のRGBA16F DDSからIBL用CubeMapとGGX LUTを生成する。
// 背景描画とは独立しているため、環境光だけを有効にできる。
class environment_lighting12
{
public:
	~environment_lighting12();
	bool initialize(std::shared_ptr<directx12_device> device, const std::filesystem::path& directory);

	D3D12_GPU_DESCRIPTOR_HANDLE diffuse_srv() const { return diffuse_.srv.gpu; }
	D3D12_GPU_DESCRIPTOR_HANDLE specular_srv() const { return specular_.srv.gpu; }
	D3D12_GPU_DESCRIPTOR_HANDLE lut_srv() const { return lut_.srv.gpu; }
	UINT specular_mip_count() const { return specular_.mip_count; }
	bool valid() const { return diffuse_.srv.valid() && specular_.srv.valid() && lut_.srv.valid(); }

private:
	struct dds_texture
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		descriptor_handle srv{};
		UINT mip_count = 1;
	};
	bool load_rgba16f_dds(const std::filesystem::path& filename, bool require_cube,
		UINT maximum_dimension, dds_texture& target);
	void release(dds_texture& texture);

	std::shared_ptr<directx12_device> device_;
	dds_texture diffuse_;
	dds_texture specular_;
	dds_texture lut_;
};
