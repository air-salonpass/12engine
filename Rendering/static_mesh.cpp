#include "framework.h"
#include "static_mesh.h"

#include <fstream>

#include <filesystem>
#include <algorithm>
#include "texture.h"

using namespace DirectX;

static_mesh::static_mesh(
	ID3D11Device* device,
	const wchar_t* obj_filename)
{
	std::vector<vertex> vertices;
	std::vector<uint32_t> indices;

	uint32_t current_index = 0;

	std::vector<XMFLOAT3> positions;
	std::vector<XMFLOAT3> normals;
	std::vector<XMFLOAT2> texcoords;
	std::vector<std::wstring> mtl_filenames;

	std::wifstream fin(obj_filename);

	_ASSERT_EXPR(fin, L"OBJ file not found");

	wchar_t command[256];

	while (fin)
	{
		fin >> command;

		if (0 == wcscmp(command, L"v"))
		{
			float x, y, z;

			fin >> x >> y >> z;

			positions.push_back({ x,y,z });

			fin.ignore(1024, L'\n');
		}
		else if (0 == wcscmp(command, L"vn"))
		{
			float i, j, k;

			fin >> i >> j >> k;

			normals.push_back({ i,j,k });

			fin.ignore(1024, L'\n');
		}
		else if (0 == wcscmp(command, L"vt"))
		{
			float u, v;

			fin >> u >> v;

			texcoords.push_back(
				{
					u,
					1.0f - v
				});

			fin.ignore(1024, L'\n');
		}
		else if (0 == wcscmp(command, L"f"))
		{
			for (size_t i = 0; i < 3; i++)
			{
				vertex vtx{};

				size_t v, vt, vn;

				fin >> v;

				vtx.position = positions.at(v - 1);

				if (L'/' == fin.peek())
				{
					fin.ignore(1);

					if (L'/' != fin.peek())
					{
						fin >> vt;

						vtx.texcoord =
							texcoords.at(vt - 1);
					}

					if (L'/' == fin.peek())
					{
						fin.ignore(1);

						fin >> vn;

						vtx.normal = normals.at(vn - 1);
					}
				}

				vertices.push_back(vtx);

				indices.push_back(current_index++);
			}

			fin.ignore(1024, L'\n');
		}
		else if (0 == wcscmp(command, L"usemtl"))
		{
			wchar_t usemtl[256];

			fin >> usemtl;

			subsets.push_back(
				{
					usemtl,
					static_cast<uint32_t>(indices.size()),
					0
				});

			fin.ignore(1024, L'\n');
		}
		else if (0 == wcscmp(command, L"mtllib"))
		{
			wchar_t mtllib[256];

			fin >> mtllib;

			mtl_filenames.push_back(mtllib);
		}
		else
		{
			fin.ignore(1024, L'\n');
		}
	}

	fin.close();

	if (!positions.empty())
	{
		bounding_box_min = positions[0];
		bounding_box_max = positions[0];

		for (const auto& p : positions)
		{
			bounding_box_min.x =
				min(bounding_box_min.x, p.x);

			bounding_box_min.y =
				min(bounding_box_min.y, p.y);

			bounding_box_min.z =
				min(bounding_box_min.z, p.z);

			bounding_box_max.x =
				max(bounding_box_max.x, p.x);

			bounding_box_max.y =
				max(bounding_box_max.y, p.y);

			bounding_box_max.z =
				max(bounding_box_max.z, p.z);
		}
	}

	if (!mtl_filenames.empty())
	{
		std::filesystem::path mtl_filename(obj_filename);

		mtl_filename.replace_filename(
			std::filesystem::path(
				mtl_filenames[0]).filename());

		fin.open(mtl_filename);

		/*_ASSERT_EXPR(fin, L"MTL file not found");*/

		if (fin)
		{
			while (fin)
			{
				fin >> command;

				if (0 == wcscmp(command, L"newmtl"))
				{
					fin.ignore();

					wchar_t newmtl[256];

					material mat;

					fin >> newmtl;

					mat.name = newmtl;

					materials.push_back(mat);

					fin.ignore(1024, L'\n');
				}
				else if (0 == wcscmp(command, L"map_Kd"))
				{
					fin.ignore();

					wchar_t map_Kd[256];

					fin >> map_Kd;

					std::filesystem::path tex_path(obj_filename);

					tex_path.replace_filename(
						std::filesystem::path(map_Kd).filename());

					if (!materials.empty())
					{
						materials.back().texture_filenames[0] =
							tex_path.wstring();
					}

					fin.ignore(1024, L'\n');
				}
				else if (
					0 == wcscmp(command, L"map_bump") ||
					0 == wcscmp(command, L"bump"))
				{
					fin.ignore();

					wchar_t map_bump[256];

					fin >> map_bump;

					std::filesystem::path tex_path(
						obj_filename);

					tex_path.replace_filename(
						std::filesystem::path(
							map_bump).filename());

					if (!materials.empty())
					{
						materials.back()
							.texture_filenames[1]
							= tex_path.wstring();
					}

					fin.ignore(1024, L'\n');
				}
				else if (0 == wcscmp(command, L"Kd"))
				{
					float r, g, b;

					fin >> r >> g >> b;

					if (!materials.empty())
					{
						materials.back().Kd = { r, g, b, 1.0f };
					}

					fin.ignore(1024, L'\n');
				}
				else
				{
					fin.ignore(1024, L'\n');
				}
			}
			fin.close();
		}



		if (!subsets.empty())
		{
			auto it = subsets.rbegin();

			it->index_count =
				static_cast<uint32_t>(indices.size())
				- it->index_start;

			for (++it; it != subsets.rend(); ++it)
			{
				it->index_count =
					(it - 1)->index_start
					- it->index_start;
			}
		}
	}

	if (materials.size() == 0)
	{
		for (const subset& subset : subsets)
		{
			material mat;
			mat.name = subset.usemtl;
			materials.push_back(mat);
		}
	}

	create_com_buffers(
		device,
		vertices.data(),
		vertices.size(),
		indices.data(),
		indices.size());

	index_count =
		static_cast<UINT>(indices.size());

	// =====================================================
	// Input Layout
	// =====================================================

	D3D11_INPUT_ELEMENT_DESC input_element_desc[] =
	{
		{
			"POSITION",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			0,
			D3D11_INPUT_PER_VERTEX_DATA,
			0
		},
		{
			"NORMAL",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			12,
			D3D11_INPUT_PER_VERTEX_DATA,
			0
		},
		{
			"TEXCOORD",
			0,
			DXGI_FORMAT_R32G32_FLOAT,
			0,
			24,
			D3D11_INPUT_PER_VERTEX_DATA,
			0
		}
	};

	// =====================================================
	// Shader
	// =====================================================

	create_vs_from_cso(
		device,
		"./static_mesh_vs.cso",
		vertex_shader.GetAddressOf(),
		input_layout.GetAddressOf(),
		input_element_desc,
		ARRAYSIZE(input_element_desc));

	create_ps_from_cso(
		device,
		"./static_mesh_ps.cso",
		pixel_shader.GetAddressOf());

	D3D11_TEXTURE2D_DESC texture2d_desc{};

	for (material& mat : materials)
	{
		if (!mat.texture_filenames[0].empty())
		{
			load_texture_from_file(
				device,
				mat.texture_filenames[0].c_str(),
				mat.shader_resource_views[0].GetAddressOf(),
				&texture2d_desc);
		}
		else
		{
			make_dummy_texture(
				device,
				mat.shader_resource_views[0].GetAddressOf(),
				0xFFFFFFFF,
				16);
		}

		if (!mat.texture_filenames[1].empty())
		{
			load_texture_from_file(
				device,
				mat.texture_filenames[1].c_str(),
				mat.shader_resource_views[1].GetAddressOf(),
				&texture2d_desc);
		}
		else
		{
			make_dummy_texture(
				device,
				mat.shader_resource_views[1].GetAddressOf(),
				0xFFFF7F7F,
				16);
		}
	}

	// =====================================================
	// Constant Buffer
	// =====================================================

	HRESULT hr{ S_OK };

	D3D11_BUFFER_DESC buffer_desc{};

	buffer_desc.ByteWidth =
		sizeof(constants);

	buffer_desc.Usage =
		D3D11_USAGE_DEFAULT;

	buffer_desc.BindFlags =
		D3D11_BIND_CONSTANT_BUFFER;

	hr = device->CreateBuffer(
		&buffer_desc,
		nullptr,
		constant_buffer.GetAddressOf());

	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
}

void static_mesh::create_com_buffers(
	ID3D11Device* device,
	const vertex* vertices,
	size_t vertex_count,
	const uint32_t* indices,
	size_t index_count)
{
	HRESULT hr{ S_OK };

	D3D11_BUFFER_DESC buffer_desc{};

	buffer_desc.ByteWidth =
		static_cast<UINT>(
			sizeof(vertex) * vertex_count);

	buffer_desc.Usage =
		D3D11_USAGE_DEFAULT;

	buffer_desc.BindFlags =
		D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA subresource_data{};

	subresource_data.pSysMem = vertices;

	hr = device->CreateBuffer(
		&buffer_desc,
		&subresource_data,
		vertex_buffer.GetAddressOf());

	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	buffer_desc.ByteWidth =
		static_cast<UINT>(
			sizeof(uint32_t) * index_count);

	buffer_desc.BindFlags =
		D3D11_BIND_INDEX_BUFFER;

	subresource_data.pSysMem = indices;

	hr = device->CreateBuffer(
		&buffer_desc,
		&subresource_data,
		index_buffer.GetAddressOf());

	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
}

void static_mesh::render(
	ID3D11DeviceContext* immediate_context,
	const XMFLOAT4X4& world,
	const XMFLOAT4& material_color,
	ID3D11PixelShader* custom_pixel_shader)
{
	UINT stride = sizeof(vertex);
	UINT offset = 0;

	immediate_context->IASetVertexBuffers(
		0,
		1,
		vertex_buffer.GetAddressOf(),
		&stride,
		&offset);

	immediate_context->IASetIndexBuffer(
		index_buffer.Get(),
		DXGI_FORMAT_R32_UINT,
		0);

	immediate_context->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	immediate_context->IASetInputLayout(
		input_layout.Get());

	immediate_context->VSSetShader(
		vertex_shader.Get(),
		nullptr,
		0);

	if (custom_pixel_shader)
	{
		immediate_context->PSSetShader(
			custom_pixel_shader,
			nullptr,
			0);
	}
	else
	{
		immediate_context->PSSetShader(
			pixel_shader.Get(),
			nullptr,
			0);
	}

	for (const material& mat : materials)
	{
		if (mat.shader_resource_views[0])
		{
			ID3D11ShaderResourceView* srv =
				mat.shader_resource_views[0].Get();

			immediate_context->PSSetShaderResources(
				0,
				1,
				&srv);
		}

		if (mat.shader_resource_views[1])
		{
			ID3D11ShaderResourceView* srv =
				mat.shader_resource_views[1].Get();

			immediate_context->PSSetShaderResources(
				1,
				1,
				&srv);
		}

		constants data{};

		data.world = world;

		XMStoreFloat4(
			&data.material_color,
			XMLoadFloat4(&material_color) *
			XMLoadFloat4(&mat.Kd));

		immediate_context->UpdateSubresource(
			constant_buffer.Get(),
			0,
			nullptr,
			&data,
			0,
			0);

		immediate_context->VSSetConstantBuffers(
			0,
			1,
			constant_buffer.GetAddressOf());

		immediate_context->PSSetConstantBuffers(
			0,
			1,
			constant_buffer.GetAddressOf());

		for (const subset& sub : subsets)
		{
			if (sub.usemtl == mat.name)
			{
				immediate_context->DrawIndexed(
					sub.index_count,
					sub.index_start,
					0);
			}
		}
	}

	// usemtl ‚ª–³‚¢OBJ‘Îô
	if (subsets.empty())
	{
		constants data{};

		data.world = world;
		data.material_color = material_color;

		immediate_context->UpdateSubresource(
			constant_buffer.Get(),
			0,
			nullptr,
			&data,
			0,
			0);

		immediate_context->VSSetConstantBuffers(
			0,
			1,
			constant_buffer.GetAddressOf());

		immediate_context->PSSetConstantBuffers(
			0,
			1,
			constant_buffer.GetAddressOf());

		immediate_context->DrawIndexed(
			index_count,
			0,
			0);
	}
}