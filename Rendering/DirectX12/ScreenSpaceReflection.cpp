#include "ScreenSpaceReflection.h"

#include <algorithm>
#include <cstring>
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace
{
	constexpr const char* ssr_shader = R"(
Texture2D scene_color:register(t0); Texture2D scene_depth:register(t1); Texture2D scene_normal:register(t2);
SamplerState linear_clamp:register(s0);
cbuffer SsrConstants:register(b0) { row_major float4x4 projection; row_major float4x4 inverse_projection;
 float4 parameters; float4 display_parameters; };
struct Output { float4 position:SV_POSITION; float2 uv:TEXCOORD; };
Output VSMain(uint id:SV_VertexID) {
 Output o; o.uv=float2((id<<1)&2,id&2); o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1); return o;
}
float3 reconstruct_view(float2 uv,float depth) {
 float4 clip=float4(uv*float2(2,-2)+float2(-1,1),depth,1); float4 v=mul(clip,inverse_projection); return v.xyz/v.w;
}
float4 PSMain(Output input):SV_TARGET {
 float4 base=scene_color.SampleLevel(linear_clamp,input.uv,0); float depth=scene_depth.SampleLevel(linear_clamp,input.uv,0).r;
 if(depth>=0.99999 || parameters.x<=0) {
  float3 mapped=1-exp(-base.rgb*display_parameters.x); return float4(pow(saturate(mapped),1/2.2),base.a); }
 float3 p=reconstruct_view(input.uv,depth); float3 n=normalize(scene_normal.SampleLevel(linear_clamp,input.uv,0).xyz*2-1);
 float3 ray=normalize(reflect(normalize(p),n)); float stride=parameters.y/max(parameters.w,1); float3 cursor=p;
 float2 hitUv=0; float hit=0;
 [loop] for(uint i=0;i<(uint)parameters.w;i++) {
  cursor+=ray*stride; float4 clip=mul(float4(cursor,1),projection); if(clip.w<=0) break;
  float2 uv=clip.xy/clip.w*float2(0.5,-0.5)+0.5; if(any(uv<=0)||any(uv>=1)) break;
  float sampledDepth=scene_depth.SampleLevel(linear_clamp,uv,0).r; float3 sampled=reconstruct_view(uv,sampledDepth);
  float delta=cursor.z-sampled.z; if(delta>=0 && delta<parameters.z) { hitUv=uv; hit=1; break; }
 }
 float edge=saturate(1-max(abs(hitUv.x*2-1),abs(hitUv.y*2-1))); float fresnel=pow(1-saturate(dot(-normalize(p),n)),3);
 float3 reflection=scene_color.SampleLevel(linear_clamp,hitUv,0).rgb;
 float3 combined=lerp(base.rgb,reflection,saturate(parameters.x*hit*edge*(0.25+fresnel)));
 float3 mapped=1-exp(-combined*display_parameters.x); return float4(pow(saturate(mapped),1/2.2),base.a);
})";

	bool compile(const char* entry, const char* target, ComPtr<ID3DBlob>& blob)
	{
		ComPtr<ID3DBlob> errors;
		return SUCCEEDED(D3DCompile(ssr_shader, std::strlen(ssr_shader), "ScreenSpaceReflection12", nullptr,
			nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.GetAddressOf(), errors.GetAddressOf()));
	}
}

bool screen_space_reflection12::initialize(std::shared_ptr<directx12_device> device)
{
	device_ = std::move(device);
	if (!device_) return false;
	D3D12_DESCRIPTOR_RANGE ranges[3]{};
	D3D12_ROOT_PARAMETER parameters[4]{};
	for (UINT index = 0; index < 3; ++index) {
		ranges[index].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[index].NumDescriptors=1;
		ranges[index].BaseShaderRegister=index; ranges[index].OffsetInDescriptorsFromTableStart=0;
		parameters[index].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameters[index].DescriptorTable.NumDescriptorRanges=1; parameters[index].DescriptorTable.pDescriptorRanges=&ranges[index];
		parameters[index].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
	}
	parameters[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
	parameters[3].Descriptor.ShaderRegister=0; parameters[3].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_STATIC_SAMPLER_DESC sampler{}; sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.MaxLOD=D3D12_FLOAT32_MAX; sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_ROOT_SIGNATURE_DESC desc{}; desc.NumParameters=_countof(parameters); desc.pParameters=parameters;
	desc.NumStaticSamplers=1; desc.pStaticSamplers=&sampler;
	ComPtr<ID3DBlob> serialized,errors;
	if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,serialized.GetAddressOf(),errors.GetAddressOf()))||
		FAILED(device_->get()->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),
			IID_PPV_ARGS(root_signature_.GetAddressOf())))) return false;
	ComPtr<ID3DBlob> vs,ps; if(!compile("VSMain","vs_5_1",vs)||!compile("PSMain","ps_5_1",ps)) return false;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{}; pipeline.pRootSignature=root_signature_.Get();
	pipeline.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; pipeline.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
	pipeline.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pipeline.NumRenderTargets=1;
	pipeline.RTVFormats[0]=directx12_renderer::color_format; pipeline.SampleDesc.Count=1; pipeline.SampleMask=UINT_MAX;
	pipeline.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID; pipeline.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
	pipeline.RasterizerState.DepthClipEnable=TRUE; pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
	if(FAILED(device_->get()->CreateGraphicsPipelineState(&pipeline,IID_PPV_ARGS(pipeline_state_.GetAddressOf())))) return false;
	D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_UPLOAD; heap.CreationNodeMask=heap.VisibleNodeMask=1;
	D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width=512*directx12_renderer::frame_count;
	buffer.Height=1; buffer.DepthOrArraySize=1; buffer.MipLevels=1; buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if(FAILED(device_->get()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,IID_PPV_ARGS(constants_buffer_.GetAddressOf())))) return false;
	D3D12_RANGE no_read{0,0}; return SUCCEEDED(constants_buffer_->Map(0,&no_read,reinterpret_cast<void**>(&mapped_constants_)));
}

void screen_space_reflection12::begin_frame(UINT frame_index) { frame_index_=frame_index%directx12_renderer::frame_count; }

void screen_space_reflection12::render(ID3D12GraphicsCommandList* command_list,const scene_render_target12& scene,
	const XMFLOAT4X4& projection,float strength,float max_distance,float thickness,std::uint32_t steps,float exposure)
{
	if(!command_list||!mapped_constants_) return;
	constants data{}; data.projection=projection;
	XMStoreFloat4x4(&data.inverse_projection,XMMatrixInverse(nullptr,XMLoadFloat4x4(&projection)));
	data.parameters={strength,max_distance,thickness,static_cast<float>((std::max)(1u,(std::min)(steps,128u)))};
	data.display_parameters.x = (std::max)(exposure, 0.01f);
	const std::size_t offset=frame_index_*512; std::memcpy(mapped_constants_+offset,&data,sizeof(data));
	ID3D12DescriptorHeap* heaps[]{device_->shader_descriptors().heap()}; command_list->SetDescriptorHeaps(1,heaps);
	command_list->SetGraphicsRootSignature(root_signature_.Get()); command_list->SetPipelineState(pipeline_state_.Get());
	command_list->SetGraphicsRootDescriptorTable(0,scene.color_srv());
	command_list->SetGraphicsRootDescriptorTable(1,scene.depth_srv());
	command_list->SetGraphicsRootDescriptorTable(2,scene.normal_srv());
	command_list->SetGraphicsRootConstantBufferView(3,constants_buffer_->GetGPUVirtualAddress()+offset);
	command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	command_list->DrawInstanced(3,1,0,0);
}
