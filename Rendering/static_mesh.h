#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl.h>

#include <vector>
#include <memory>
#include <string>

#include "misc.h"
#include "shader.h"

class static_mesh
{
public:

	struct vertex
	{
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT3 normal;
		DirectX::XMFLOAT2 texcoord;
	};

	struct constants
	{
		DirectX::XMFLOAT4X4 world;
		DirectX::XMFLOAT4 material_color;
	};

private:

	struct subset
	{
		std::wstring usemtl;
		uint32_t index_start{ 0 };
		uint32_t index_count{ 0 };
	};

	struct material
	{
		std::wstring name;

		DirectX::XMFLOAT4 Ka
		{
			0.2f, 0.2f, 0.2f, 1.0f
		};

		DirectX::XMFLOAT4 Kd
		{
			0.8f, 0.8f, 0.8f, 1.0f
		};

		DirectX::XMFLOAT4 Ks
		{
			1.0f, 1.0f, 1.0f, 1.0f
		};

		std::wstring texture_filenames[2];

		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
			shader_resource_views[2];
	};

	UINT index_count = 0;

	std::vector<subset> subsets;
	std::vector<material> materials;

	DirectX::XMFLOAT3 bounding_box_min;
	DirectX::XMFLOAT3 bounding_box_max;

	Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> index_buffer;

	Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
	Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;

	Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer;

public:

	static_mesh(
		ID3D11Device* device,
		const wchar_t* obj_filename);

	virtual ~static_mesh() = default;

	void render(
		ID3D11DeviceContext* immediate_context,
		const DirectX::XMFLOAT4X4& world,
		const DirectX::XMFLOAT4& material_color,
		ID3D11PixelShader* custom_pixel_shader = nullptr);
	const DirectX::XMFLOAT3& get_bounding_box_min() const
	{
		return bounding_box_min;
	}

	const DirectX::XMFLOAT3& get_bounding_box_max() const
	{
		return bounding_box_max;
	}

protected:

	void create_com_buffers(
		ID3D11Device* device,
		const vertex* vertices,
		size_t vertex_count,
		const uint32_t* indices,
		size_t index_count);
};