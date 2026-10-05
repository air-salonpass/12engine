#include "SceneGeometryRenderer.h"

#include <cstring>
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace
{
	constexpr const char* floor_shader = R"(
cbuffer Constants:register(b0) { row_major float4x4 world; row_major float4x4 view;
 row_major float4x4 projection; float4 floor_color; float4 render_parameters; };
Texture2D reflection_texture:register(t0); SamplerState linear_clamp:register(s0);
struct Input { float3 position:POSITION; float3 normal:NORMAL; };
struct Varying { float4 position:SV_POSITION; float3 view_normal:NORMAL; float3 world_position:TEXCOORD; };
struct Output { float4 color:SV_TARGET0; float4 normal:SV_TARGET1; };
Varying VSMain(Input input) {
 Varying o; float4 wp=mul(float4(input.position,1),world); o.world_position=wp.xyz;
 o.position=mul(mul(wp,view),projection); o.view_normal=normalize(mul(mul(input.normal,(float3x3)world),(float3x3)view)); return o;
}
Output PSMain(Varying input) {
 float2 uv=input.position.xy/max(render_parameters.xy,float2(1,1));
 float4 reflected=reflection_texture.SampleLevel(linear_clamp,uv,0);
 float amount=saturate(reflected.a*render_parameters.z);
 Output o; o.color=float4(lerp(floor_color.rgb,reflected.rgb,amount),1);
 o.normal=float4(normalize(input.view_normal)*0.5+0.5,1); return o;
})";

	bool compile(const char* entry, const char* target, ComPtr<ID3DBlob>& blob)
	{
		ComPtr<ID3DBlob> errors;
		return SUCCEEDED(D3DCompile(floor_shader, std::strlen(floor_shader), "SceneFloor12", nullptr,
			nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.GetAddressOf(), errors.GetAddressOf()));
	}
}

bool scene_geometry_renderer12::initialize(std::shared_ptr<directx12_device> device)
{
	device_ = std::move(device);
	if (!device_) return false;
	D3D12_ROOT_PARAMETER parameters[2]{};
	parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	parameters[0].Descriptor.ShaderRegister = 0;
	parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	D3D12_DESCRIPTOR_RANGE reflection_range{};
	reflection_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	reflection_range.NumDescriptors = 1;
	reflection_range.BaseShaderRegister = 0;
	parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[1].DescriptorTable.NumDescriptorRanges = 1;
	parameters[1].DescriptorTable.pDescriptorRanges = &reflection_range;
	parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_ROOT_SIGNATURE_DESC root_desc{};
	root_desc.NumParameters = _countof(parameters);
	root_desc.pParameters = parameters;
	root_desc.NumStaticSamplers = 1;
	root_desc.pStaticSamplers = &sampler;
	root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	ComPtr<ID3DBlob> serialized, errors;
	if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
		serialized.GetAddressOf(), errors.GetAddressOf())) ||
		FAILED(device_->get()->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
			IID_PPV_ARGS(root_signature_.GetAddressOf())))) return false;
	ComPtr<ID3DBlob> vs, ps;
	if (!compile("VSMain", "vs_5_1", vs) || !compile("PSMain", "ps_5_1", ps)) return false;
	const D3D12_INPUT_ELEMENT_DESC layout[]{
		{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(vertex,position),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(vertex,normal),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
	pipeline.pRootSignature = root_signature_.Get();
	pipeline.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	pipeline.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	pipeline.InputLayout = { layout, _countof(layout) };
	pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipeline.NumRenderTargets = 2;
	pipeline.RTVFormats[0] = pipeline.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	pipeline.DSVFormat = directx12_renderer::depth_format;
	pipeline.SampleDesc.Count = 1;
	pipeline.SampleMask = UINT_MAX;
	pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pipeline.RasterizerState.DepthClipEnable = TRUE;
	pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	pipeline.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	pipeline.DepthStencilState.DepthEnable = TRUE;
	pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	if (FAILED(device_->get()->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(pipeline_state_.GetAddressOf())))) return false;

	// 大きな1枚床を2三角形で構成し、余分な円や波紋用オブジェクトは作らない。
	const vertex vertices[]{
		{{-30,0,-30},{0,1,0}}, {{-30,0,30},{0,1,0}}, {{30,0,30},{0,1,0}},
		{{-30,0,-30},{0,1,0}}, {{30,0,30},{0,1,0}}, {{30,0,-30},{0,1,0}}
	};
	D3D12_HEAP_PROPERTIES upload_heap{};
	upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	upload_heap.CreationNodeMask = upload_heap.VisibleNodeMask = 1;
	D3D12_RESOURCE_DESC vertex_desc{};
	vertex_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertex_desc.Width = sizeof(vertices);
	vertex_desc.Height = 1;
	vertex_desc.DepthOrArraySize = 1;
	vertex_desc.MipLevels = 1;
	vertex_desc.SampleDesc.Count = 1;
	vertex_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if (FAILED(device_->get()->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &vertex_desc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(vertex_buffer_.GetAddressOf())))) return false;
	void* mapped_vertices = nullptr;
	D3D12_RANGE no_read{ 0, 0 };
	if (FAILED(vertex_buffer_->Map(0, &no_read, &mapped_vertices))) return false;
	std::memcpy(mapped_vertices, vertices, sizeof(vertices));
	vertex_buffer_->Unmap(0, nullptr);
	vertex_view_.BufferLocation = vertex_buffer_->GetGPUVirtualAddress();
	vertex_view_.SizeInBytes = sizeof(vertices);
	vertex_view_.StrideInBytes = sizeof(vertex);

	D3D12_RESOURCE_DESC constants_desc = vertex_desc;
	constants_desc.Width = 256 * directx12_renderer::frame_count;
	if (FAILED(device_->get()->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &constants_desc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(constants_buffer_.GetAddressOf())))) return false;
	return SUCCEEDED(constants_buffer_->Map(0, &no_read, reinterpret_cast<void**>(&mapped_constants_)));
}

void scene_geometry_renderer12::begin_frame(UINT frame_index)
{
	frame_index_ = frame_index % directx12_renderer::frame_count;
}

void scene_geometry_renderer12::render_floor(ID3D12GraphicsCommandList* command_list,
	const XMFLOAT4X4& view, const XMFLOAT4X4& projection, float floor_height, const XMFLOAT3& color,
	D3D12_GPU_DESCRIPTOR_HANDLE reflection, UINT target_width, UINT target_height, float reflection_strength)
{
	if (!command_list || !mapped_constants_) return;
	constants data{};
	XMStoreFloat4x4(&data.world, XMMatrixTranslation(0, floor_height, 0));
	data.view = view;
	data.projection = projection;
	data.color = { color.x, color.y, color.z, 1.0f };
	data.render_parameters = { static_cast<float>(target_width), static_cast<float>(target_height),
		(std::max)(reflection_strength, 0.0f), 0.0f };
	const std::size_t offset = static_cast<std::size_t>(frame_index_) * 256;
	std::memcpy(mapped_constants_ + offset, &data, sizeof(data));
	command_list->SetGraphicsRootSignature(root_signature_.Get());
	command_list->SetPipelineState(pipeline_state_.Get());
	command_list->SetGraphicsRootConstantBufferView(0, constants_buffer_->GetGPUVirtualAddress() + offset);
	ID3D12DescriptorHeap* heaps[]{ device_->shader_descriptors().heap() };
	command_list->SetDescriptorHeaps(1, heaps);
	command_list->SetGraphicsRootDescriptorTable(1, reflection);
	command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	command_list->IASetVertexBuffers(0, 1, &vertex_view_);
	command_list->DrawInstanced(6, 1, 0, 0);
}
