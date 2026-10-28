#include "GltfRenderer.h"
#include "LightingShader.h"

#include <algorithm>
#include <cstring>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace
{
	const std::string shader_source = std::string(lighting_shader_source12) + R"(
cbuffer SceneConstants : register(b0) {
 row_major float4x4 world; row_major float4x4 view; row_major float4x4 projection;
 row_major float4x4 inverse_view;
 row_major float4x4 normal_world;
 float4 base_color_factor; float4 emissive_metallic; float4 roughness_flags;
 uint skinned; float3 scene_padding;
 float4 environment_parameters;
 float4 light_direction;
};
cbuffer JointConstants : register(b1) { row_major float4x4 joint_matrices[256]; };
Texture2D base_color_texture:register(t0); Texture2D metallic_roughness_texture:register(t1);
Texture2D normal_texture:register(t2); Texture2D occlusion_texture:register(t3); Texture2D emissive_texture:register(t4);
TextureCube diffuse_iem:register(t5); TextureCube specular_pmrem:register(t6); Texture2D brdf_lut:register(t7);
SamplerState material_sampler:register(s0);
SamplerState environment_sampler:register(s1);
struct VSInput { float3 position:POSITION; float3 normal:NORMAL; float4 tangent:TANGENT;
 float2 texcoord:TEXCOORD; uint4 joints:JOINTS; float4 weights:WEIGHTS; };
struct VSOutput { float4 position:SV_POSITION; float3 normal:NORMAL; float3 tangent:TANGENT;
 float tangent_sign:TANGENT1; float2 uv:TEXCOORD; float3 view_position:TEXCOORD1; float3 world_position:TEXCOORD2; };
struct PSOutput { float4 color:SV_TARGET0; float4 normal:SV_TARGET1; };
VSOutput VSMain(VSInput input) {
 float4 p=float4(input.position,1); float3 n=input.normal; float3 t=input.tangent.xyz;
 if(skinned!=0) { float4x4 s=joint_matrices[input.joints.x]*input.weights.x;
  s+=joint_matrices[input.joints.y]*input.weights.y; s+=joint_matrices[input.joints.z]*input.weights.z;
  s+=joint_matrices[input.joints.w]*input.weights.w; p=mul(p,s); n=mul(n,(float3x3)s); t=mul(t,(float3x3)s); }
 float4 wp=mul(p,world); float4 vp=mul(wp,view); VSOutput o; o.position=mul(vp,projection);
 // 法線は逆転置行列で変換する。壁などの非一様Scaleで照明が歪むことを防ぐ。
 o.normal=normalize(mul(mul(n,(float3x3)normal_world),(float3x3)view));
 o.tangent=mul(mul(t,(float3x3)world),(float3x3)view);
 o.tangent_sign=input.tangent.w*(determinant((float3x3)world)<0?-1:1); o.uv=input.texcoord;
 o.view_position=vp.xyz; o.world_position=wp.xyz; return o;
}
// 影用には色を出力せず、MASK材質の穴だけを通常表示と同じAlpha Cutoffで判定する。
void PSShadow(VSOutput input) {
 if((uint)roughness_flags.z==1) {
  float alpha=base_color_factor.a;
  if(((uint)roughness_flags.y&1)!=0) alpha*=base_color_texture.Sample(material_sampler,input.uv).a;
  clip(alpha-roughness_flags.w);
 }
}
float stone_hash(float3 p) { return frac(sin(dot(p,float3(127.1,311.7,74.7)))*43758.5453); }
float stone_noise(float3 p) {
 float3 cell=floor(p); float3 f=frac(p); f=f*f*(3-2*f);
 float a=lerp(stone_hash(cell),stone_hash(cell+float3(1,0,0)),f.x);
 float b=lerp(stone_hash(cell+float3(0,1,0)),stone_hash(cell+float3(1,1,0)),f.x);
 float c=lerp(stone_hash(cell+float3(0,0,1)),stone_hash(cell+float3(1,0,1)),f.x);
 float d=lerp(stone_hash(cell+float3(0,1,1)),stone_hash(cell+float3(1,1,1)),f.x);
 return lerp(lerp(a,b,f.y),lerp(c,d,f.y),f.z);
}
PSOutput PSMain(VSOutput input, bool front_face:SV_IsFrontFace) {
 uint flags=(uint)roughness_flags.y; float4 base=base_color_factor;
 if((flags&1)!=0) { float4 tex=base_color_texture.Sample(material_sampler,input.uv); base*=float4(pow(tex.rgb,2.2),tex.a); }
 if((uint)roughness_flags.z==1 && base.a<roughness_flags.w) discard;
 float metallic=emissive_metallic.w; float roughness=roughness_flags.x;
 if((flags&2)!=0) { float4 mr=metallic_roughness_texture.Sample(material_sampler,input.uv); metallic*=mr.b; roughness*=mr.g; }
 // 石材だけに微細な色と粗さの差を加える。一般のアート材質は変更しない。
 if((flags&64)!=0) {
  float grain=stone_noise(input.world_position*7.0);
  float coarse=stone_noise(input.world_position*1.7);
  float detail_fade=saturate(1-length(fwidth(input.world_position))*10);
  base.rgb*=lerp(0.88,1.10,coarse)*lerp(1,lerp(0.9,1.08,grain),detail_fade);
  roughness=saturate(roughness+(grain-0.5)*0.12);
 }
 roughness=clamp(roughness,0.045,1); metallic=saturate(metallic);
 float3 n=normalize(input.normal);
 if((flags&4)!=0) { float3 t=normalize(input.tangent-n*dot(n,input.tangent)); float3 b=cross(n,t)*input.tangent_sign;
  float3 map=normal_texture.Sample(material_sampler,input.uv).xyz*2-1; n=normalize(map.x*t+map.y*b+map.z*n); }
 if(!front_face && scene_padding.y>0.5) n=-n;
 float ao=(flags&8)!=0?occlusion_texture.Sample(material_sampler,input.uv).r:1;
 float3 emissive=emissive_metallic.rgb;
 if((flags&16)!=0) emissive*=pow(emissive_texture.Sample(material_sampler,input.uv).rgb,2.2);
 float3 v=normalize(-input.view_position); float light_length=length(light_direction.xyz);
 float3 l=light_direction.xyz/max(light_length,0.0001); float3 h=normalize(v+l);
 float nv=saturate(dot(n,v)); float nl=saturate(dot(n,l)); float nh=saturate(dot(n,h)); float vh=saturate(dot(v,h));
 float alpha=max(roughness*roughness,0.04); float a2=alpha*alpha; float denominator=nh*nh*(a2-1)+1;
 float d=a2/max(3.14159265*denominator*denominator,0.0001);
 float k=(roughness+1)*(roughness+1)/8; float geometry=(nv/(nv*(1-k)+k))*(nl/(nl*(1-k)+k));
 float3 f0=lerp(float3(0.04,0.04,0.04),base.rgb,metallic); float3 fresnel=f0+(1-f0)*pow(1-vh,5);
 float3 specular=d*geometry*fresnel/max(4*nv*nl,0.001); float3 diffuse=(1-fresnel)*(1-metallic)*base.rgb/3.14159265;
 // 空と地面からの間接光を、面の向きと粗さに応じて近似する。外部HDRIが無くても金属が黒くならない。
 float3 n_world=normalize(mul(n,(float3x3)inverse_view));
 float3 reflection_world=normalize(mul(reflect(-v,n),(float3x3)inverse_view));
 float3 rough_fresnel=f0+(max(float3(1-roughness,1-roughness,1-roughness),f0)-f0)*pow(1-nv,5);
 float3 kd=(1-rough_fresnel)*(1-metallic);
 float3 ambient=(kd*base.rgb*procedural_environment(n_world)+
  procedural_environment(reflection_world)*rough_fresnel*lerp(0.9,0.35,roughness))*ao;
 if(environment_parameters.z>0.5) {
  float3 irradiance=diffuse_iem.SampleLevel(environment_sampler,n_world,0).rgb;
  float mip_level=roughness*max(environment_parameters.y-1,0);
  float3 prefiltered=specular_pmrem.SampleLevel(environment_sampler,reflection_world,mip_level).rgb;
  float2 brdf=brdf_lut.SampleLevel(environment_sampler,float2(nv,roughness),0).rg;
  float3 ibl=(kd*base.rgb*irradiance+prefiltered*(rough_fresnel*brdf.x+brdf.y))*ao*environment_parameters.x;
  ambient+=ibl;
 }
 float shadow=scene_padding.x>0.5?1:sunlight_visibility(input.world_position,n_world);
 float3 color=(diffuse+specular)*nl*sun_color_intensity.rgb*sun_color_intensity.a*shadow+
  ambient+emissive;
 PSOutput o; o.color=float4(color,base.a); o.normal=float4(n*0.5+0.5,1); return o;
})";

	bool compile_shader(const char* entry, const char* target, ComPtr<ID3DBlob>& bytecode)
	{
		ComPtr<ID3DBlob> errors;
		const UINT flags =
#ifdef _DEBUG
			D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
			D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
		return SUCCEEDED(D3DCompile(shader_source.c_str(), shader_source.size(), "GltfRenderer12", nullptr,
			nullptr, entry, target, flags, 0, bytecode.GetAddressOf(), errors.GetAddressOf()));
	}
	std::size_t align_cb(std::size_t bytes) { return (bytes + 255u) & ~std::size_t(255u); }

	std::vector<std::uint8_t> load_shader_binary(const wchar_t* filename)
	{
		wchar_t executable[MAX_PATH]{};
		if (!GetModuleFileNameW(nullptr, executable, _countof(executable))) return {};
		const std::filesystem::path path = std::filesystem::path(executable).parent_path() / filename;
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		if (!input) return {};
		const std::streamsize size = input.tellg();
		if (size <= 0) return {};
		std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
		input.seekg(0);
		input.read(reinterpret_cast<char*>(result.data()), size);
		return input ? result : std::vector<std::uint8_t>{};
	}

	// Pipeline State Streamは各Subobjectをポインター境界へ揃える必要がある。
	template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type, typename Value>
	struct alignas(void*) pipeline_subobject
	{
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
		Value value{};
	};

	struct mesh_pipeline_stream
	{
		pipeline_subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> root;
		pipeline_subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE> mesh;
		pipeline_subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> pixel;
		pipeline_subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC> blend;
		pipeline_subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT> sample_mask;
		pipeline_subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC> rasterizer;
		pipeline_subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, D3D12_DEPTH_STENCIL_DESC> depth;
		pipeline_subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE> topology;
		pipeline_subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> targets;
		pipeline_subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT> depth_format;
		pipeline_subobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC> samples;
	};
}

gltf_renderer12::~gltf_renderer12()
{
	if (device_ && fallback_srv_.valid()) device_->shader_descriptors().release(fallback_srv_);
}

bool gltf_renderer12::initialize(std::shared_ptr<directx12_device> device)
{
	initialization_error_ = 0;
	device_ = std::move(device);
	if (!device_) { initialization_error_ = 1; return false; }
	D3D12_ROOT_PARAMETER parameters[12]{};
	for (UINT index = 0; index < 2; ++index) {
		parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		parameters[index].Descriptor.ShaderRegister = index;
		parameters[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	}
	// b0の材質色はPixel Shaderも参照する。関節行列b1だけVertex限定にしてRoot Signatureを絞る。
	parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	D3D12_DESCRIPTOR_RANGE texture_ranges[8]{};
	for (UINT index = 0; index < _countof(texture_ranges); ++index)
	{
		texture_ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		texture_ranges[index].NumDescriptors = 1;
		texture_ranges[index].BaseShaderRegister = index;
		texture_ranges[index].OffsetInDescriptorsFromTableStart = 0;
		parameters[index + 2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameters[index + 2].DescriptorTable.NumDescriptorRanges = 1;
		parameters[index + 2].DescriptorTable.pDescriptorRanges = &texture_ranges[index];
		parameters[index + 2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	}
	// t12はMeshlet用t8～t11と重ならない位置へ置く。b2はモデルと床で共有する照明定数。
	D3D12_DESCRIPTOR_RANGE shadow_range{};
	shadow_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	shadow_range.NumDescriptors = 1; shadow_range.BaseShaderRegister = 12;
	parameters[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[10].DescriptorTable.NumDescriptorRanges = 1;
	parameters[10].DescriptorTable.pDescriptorRanges = &shadow_range;
	parameters[10].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	parameters[11].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	parameters[11].Descriptor.ShaderRegister = 2;
	parameters[11].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_STATIC_SAMPLER_DESC samplers[3]{};
	samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplers[0].AddressU = samplers[0].AddressV = samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	// 環境マップは端を跨いで補間しないようClampし、材質Samplerとはレジスタを分ける。
	samplers[1] = samplers[0];
	samplers[1].AddressU = samplers[1].AddressV = samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[1].ShaderRegister = 1;
	samplers[2] = samplers[1];
	samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	samplers[2].ShaderRegister = 2;
	D3D12_ROOT_SIGNATURE_DESC root_desc{};
	root_desc.NumParameters = _countof(parameters); root_desc.pParameters = parameters;
	root_desc.NumStaticSamplers = _countof(samplers); root_desc.pStaticSamplers = samplers;
	root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	ComPtr<ID3DBlob> serialized, errors;
	if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
		serialized.GetAddressOf(), errors.GetAddressOf())) ||
		FAILED(device_->get()->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
			IID_PPV_ARGS(root_signature_.GetAddressOf())))) { initialization_error_ = 2; return false; }

	ComPtr<ID3DBlob> vs, ps;
	if (!compile_shader("VSMain", "vs_5_1", vs)) { initialization_error_ = 3; return false; }
	if (!compile_shader("PSMain", "ps_5_1", ps)) { initialization_error_ = 4; return false; }
	const D3D12_INPUT_ELEMENT_DESC layout[] = {
		{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(gltf_vertex12,position),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(gltf_vertex12,normal),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"TANGENT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,offsetof(gltf_vertex12,tangent),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,offsetof(gltf_vertex12,texcoord),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"JOINTS",0,DXGI_FORMAT_R32G32B32A32_UINT,0,offsetof(gltf_vertex12,joints),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"WEIGHTS",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,offsetof(gltf_vertex12,weights),D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
	pipeline.pRootSignature=root_signature_.Get(); pipeline.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
	pipeline.PS={ps->GetBufferPointer(),ps->GetBufferSize()}; pipeline.InputLayout={layout,_countof(layout)};
	pipeline.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pipeline.NumRenderTargets=2;
	pipeline.DSVFormat=directx12_renderer::depth_format;
	pipeline.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
	pipeline.RTVFormats[1]=DXGI_FORMAT_R16G16B16A16_FLOAT;
	pipeline.SampleDesc.Count=1; pipeline.SampleMask=UINT_MAX;
	pipeline.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID; pipeline.RasterizerState.CullMode=D3D12_CULL_MODE_BACK;
	// LHカメラの投影後の向きに合わせる。逆だと閉じたモデルの内側を描いて材質が不自然になる。
	pipeline.RasterizerState.FrontCounterClockwise=FALSE; pipeline.RasterizerState.DepthClipEnable=TRUE;
	pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
	pipeline.BlendState.RenderTarget[1].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
	pipeline.DepthStencilState.DepthEnable=TRUE; pipeline.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
	pipeline.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
	if (FAILED(device_->get()->CreateGraphicsPipelineState(&pipeline,IID_PPV_ARGS(pipeline_state_.GetAddressOf())))) { initialization_error_ = 5; return false; }
	// glTFのdoubleSided材質では裏面カリングを無効化する。
	pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	if (FAILED(device_->get()->CreateGraphicsPipelineState(&pipeline,
		IID_PPV_ARGS(double_sided_pipeline_state_.GetAddressOf())))) { initialization_error_ = 9; return false; }
	// BLEND材質は通常のアルファ合成とし、後続オブジェクトの深度を塞がないようDepthWriteを止める。
	pipeline.BlendState.RenderTarget[0].BlendEnable = TRUE;
	pipeline.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	pipeline.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	pipeline.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	pipeline.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	pipeline.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	pipeline.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	if (FAILED(device_->get()->CreateGraphicsPipelineState(&pipeline,
		IID_PPV_ARGS(alpha_pipeline_state_.GetAddressOf())))) { initialization_error_ = 10; return false; }
	// 深度専用PSOは通常頂点経路を使うため、Mesh Shader非対応GPUでも同じ影を描ける。
	ComPtr<ID3DBlob> shadow_ps;
	if (!compile_shader("PSShadow", "ps_5_1", shadow_ps)) { initialization_error_ = 11; return false; }
	auto shadow_pipeline = pipeline;
	shadow_pipeline.PS = { shadow_ps->GetBufferPointer(), shadow_ps->GetBufferSize() };
	shadow_pipeline.NumRenderTargets = 0;
	for (auto& format : shadow_pipeline.RTVFormats) format = DXGI_FORMAT_UNKNOWN;
	shadow_pipeline.BlendState = {};
	shadow_pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	shadow_pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	shadow_pipeline.RasterizerState.DepthBias = 800;
	shadow_pipeline.RasterizerState.SlopeScaledDepthBias = 1.5f;
	if (FAILED(device_->get()->CreateGraphicsPipelineState(&shadow_pipeline,
		IID_PPV_ARGS(shadow_pipeline_state_.GetAddressOf())))) { initialization_error_ = 12; return false; }

	// Mesh Shaderは任意機能として構築する。非対応GPUや古いドライバーでは通常PSOをそのまま使う。
	if (device_->supported_features().mesh_shader != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
	{
		D3D12_ROOT_PARAMETER mesh_parameters[16]{};
		for (UINT index = 0; index < 2; ++index)
		{
			mesh_parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
			mesh_parameters[index].Descriptor.ShaderRegister = index;
			mesh_parameters[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}
		D3D12_DESCRIPTOR_RANGE mesh_ranges[12]{};
		for (UINT index = 0; index < _countof(mesh_ranges); ++index)
		{
			mesh_ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			mesh_ranges[index].NumDescriptors = 1;
			mesh_ranges[index].BaseShaderRegister = index;
			mesh_ranges[index].OffsetInDescriptorsFromTableStart = 0;
			mesh_parameters[index + 2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			mesh_parameters[index + 2].DescriptorTable.NumDescriptorRanges = 1;
			mesh_parameters[index + 2].DescriptorTable.pDescriptorRanges = &mesh_ranges[index];
			mesh_parameters[index + 2].ShaderVisibility = index < 8 ?
				D3D12_SHADER_VISIBILITY_PIXEL : D3D12_SHADER_VISIBILITY_MESH;
		}
		mesh_parameters[14] = parameters[10];
		mesh_parameters[15] = parameters[11];
		D3D12_ROOT_SIGNATURE_DESC mesh_root_desc{};
		mesh_root_desc.NumParameters = _countof(mesh_parameters);
		mesh_root_desc.pParameters = mesh_parameters;
		mesh_root_desc.NumStaticSamplers = _countof(samplers);
		mesh_root_desc.pStaticSamplers = samplers;
		ComPtr<ID3DBlob> mesh_serialized;
		const auto mesh_bytecode = load_shader_binary(L"GltfMeshMS.cso");
		ComPtr<ID3D12Device2> device2;
		if (!mesh_bytecode.empty() && SUCCEEDED(device_->get()->QueryInterface(IID_PPV_ARGS(device2.GetAddressOf()))) &&
			SUCCEEDED(D3D12SerializeRootSignature(&mesh_root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
				mesh_serialized.GetAddressOf(), errors.ReleaseAndGetAddressOf())) &&
			SUCCEEDED(device_->get()->CreateRootSignature(0, mesh_serialized->GetBufferPointer(), mesh_serialized->GetBufferSize(),
				IID_PPV_ARGS(mesh_root_signature_.GetAddressOf()))))
		{
			mesh_pipeline_stream stream{};
			stream.root.value = mesh_root_signature_.Get();
			stream.mesh.value = { mesh_bytecode.data(), mesh_bytecode.size() };
			stream.pixel.value = { ps->GetBufferPointer(), ps->GetBufferSize() };
			stream.blend.value = pipeline.BlendState;
			// Mesh Shader PSOはまずOPAQUE・片面材質に限定し、特殊材質は通常PSOへ戻す。
			stream.blend.value.RenderTarget[0].BlendEnable = FALSE;
			stream.sample_mask.value = UINT_MAX;
			stream.rasterizer.value = pipeline.RasterizerState;
			stream.rasterizer.value.CullMode = D3D12_CULL_MODE_BACK;
			stream.depth.value = pipeline.DepthStencilState;
			stream.depth.value.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
			stream.topology.value = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			stream.targets.value.NumRenderTargets = 2;
			stream.targets.value.RTFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
			stream.targets.value.RTFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
			stream.depth_format.value = directx12_renderer::depth_format;
			stream.samples.value.Count = 1;
			D3D12_PIPELINE_STATE_STREAM_DESC stream_desc{ sizeof(stream), &stream };
			mesh_shader_available_ = SUCCEEDED(device2->CreatePipelineState(&stream_desc,
				IID_PPV_ARGS(mesh_pipeline_state_.GetAddressOf())));
			if (mesh_shader_available_)
			{
				// 鏡像では座標系の向きが反転するため、カリングなしのMesh Shader PSOも用意する。
				stream.rasterizer.value.CullMode = D3D12_CULL_MODE_NONE;
				if (FAILED(device2->CreatePipelineState(&stream_desc,
					IID_PPV_ARGS(mesh_double_sided_pipeline_state_.GetAddressOf()))))
					mesh_shader_available_ = false;
			}
		}
	}

	D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_UPLOAD; heap.CreationNodeMask=heap.VisibleNodeMask=1;
	D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
	buffer.Width=upload_bytes_per_frame*directx12_renderer::frame_count; buffer.Height=1; buffer.DepthOrArraySize=1;
	buffer.MipLevels=1; buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if (FAILED(device_->get()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,
		D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(upload_buffer_.GetAddressOf())))) { initialization_error_ = 6; return false; }
	D3D12_RANGE no_read{0,0};
	if (FAILED(upload_buffer_->Map(0,&no_read,reinterpret_cast<void**>(&mapped_upload_)))) { initialization_error_ = 7; return false; }
	if (!create_fallback_texture()) { initialization_error_ = 8; return false; }
	return true;
}

bool gltf_renderer12::create_fallback_texture()
{
	D3D12_HEAP_PROPERTIES default_heap{};
	default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	default_heap.CreationNodeMask = default_heap.VisibleNodeMask = 1;
	D3D12_RESOURCE_DESC texture{};
	texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texture.Width = texture.Height = 1;
	texture.DepthOrArraySize = texture.MipLevels = 1;
	texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texture.SampleDesc.Count = 1;
	if (FAILED(device_->get()->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &texture,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(fallback_texture_.GetAddressOf())))) return false;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
	UINT rows = 0; UINT64 row_size = 0, upload_size = 0;
	device_->get()->GetCopyableFootprints(&texture, 0, 1, 0, &footprint, &rows, &row_size, &upload_size);
	D3D12_HEAP_PROPERTIES upload_heap = default_heap; upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = upload_size;
	buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1; buffer.SampleDesc.Count = 1;
	buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ComPtr<ID3D12Resource> upload_resource;
	if (FAILED(device_->get()->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload_resource.GetAddressOf())))) return false;
	std::uint8_t* mapped = nullptr; D3D12_RANGE no_read{ 0, 0 };
	if (FAILED(upload_resource->Map(0, &no_read, reinterpret_cast<void**>(&mapped)))) return false;
	const std::uint32_t white = 0xffffffffu;
	std::memcpy(mapped + footprint.Offset, &white, sizeof(white));
	upload_resource->Unmap(0, nullptr);
	ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> commands;
	if (FAILED(device_->get()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.GetAddressOf()))) ||
		FAILED(device_->get()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
			IID_PPV_ARGS(commands.GetAddressOf())))) return false;
	D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource = upload_resource.Get();
	source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; source.PlacedFootprint = footprint;
	D3D12_TEXTURE_COPY_LOCATION target{}; target.pResource = fallback_texture_.Get();
	target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	commands->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
	D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = fallback_texture_.Get(); barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commands->ResourceBarrier(1, &barrier);
	if (FAILED(commands->Close())) return false;
	ID3D12CommandList* lists[]{ commands.Get() };
	const std::uint64_t fence = device_->graphics_queue().execute(lists, 1);
	if (!device_->graphics_queue().wait(fence)) return false;
	fallback_srv_ = device_->shader_descriptors().allocate();
	if (!fallback_srv_.valid()) return false;
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = texture.Format;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	device_->get()->CreateShaderResourceView(fallback_texture_.Get(), &srv, fallback_srv_.cpu);
	return true;
}

void gltf_renderer12::begin_frame(UINT frame_index)
{
	frame_offset_=(frame_index%directx12_renderer::frame_count)*upload_bytes_per_frame; upload_cursor_=0;
	// 静的な床・壁ごとに256関節分の定数を複製しない。フレーム内で不変の単位行列を共有する。
	// 多数のマップ部品でもUpload領域を使い切らず、フレームごとのFence管理はそのまま保つ。
	joint_constants identity{};
	for (auto& matrix : identity.matrices) XMStoreFloat4x4(&matrix,XMMatrixIdentity());
	default_joint_address_ = upload(&identity,sizeof(identity));
}

void gltf_renderer12::set_lighting(const lighting_constants12& lighting, D3D12_GPU_DESCRIPTOR_HANDLE shadow)
{
	lighting_address_ = upload(&lighting, sizeof(lighting));
	shadow_srv_ = shadow;
}

D3D12_GPU_VIRTUAL_ADDRESS gltf_renderer12::upload(const void* data, std::size_t bytes)
{
	const std::size_t aligned=align_cb(bytes);
	if(!mapped_upload_||upload_cursor_+aligned>upload_bytes_per_frame) return 0;
	const std::size_t offset=frame_offset_+upload_cursor_; std::memcpy(mapped_upload_+offset,data,bytes);
	upload_cursor_+=aligned; return upload_buffer_->GetGPUVirtualAddress()+offset;
}

void gltf_renderer12::render(ID3D12GraphicsCommandList* command_list,const gltf_model12& model,
	const XMFLOAT4X4& world,const XMFLOAT4X4& view,const XMFLOAT4X4& projection,
	const environment_lighting12* environment,float ibl_intensity,
	const std::vector<gltf_model12::node>* pose,XMFLOAT3 light_direction,bool prefer_mesh_shader,
	bool force_double_sided, bool depth_only)
{
	if(!command_list||!model.loaded()) return;
	ID3D12DescriptorHeap* heaps[]{ device_->shader_descriptors().heap() };
	command_list->SetDescriptorHeaps(1, heaps);
	ComPtr<ID3D12GraphicsCommandList6> mesh_commands;
	if (mesh_shader_available_ && prefer_mesh_shader && !depth_only)
		command_list->QueryInterface(IID_PPV_ARGS(mesh_commands.GetAddressOf()));
	// 環境が未ロードなら白Textureを束縛し、Shader側のフラグでIBL参照を止める。
	const bool has_environment = environment && environment->valid();
	command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const auto& nodes = pose && pose->size() == model.nodes().size() ? *pose : model.nodes();
	for(std::size_t i=0;i<nodes.size();++i) if(nodes[i].parent<0)
		draw_node(command_list,model,static_cast<int>(i),world,view,projection,
			has_environment ? environment : nullptr,ibl_intensity,pose,light_direction,
			mesh_commands.Get(),prefer_mesh_shader,force_double_sided,depth_only);
}

void gltf_renderer12::draw_node(ID3D12GraphicsCommandList* command_list,const gltf_model12& model,int node_index,
	const XMFLOAT4X4& world,const XMFLOAT4X4& view,const XMFLOAT4X4& projection,
	const environment_lighting12* environment,float ibl_intensity,
	const std::vector<gltf_model12::node>* pose,XMFLOAT3 light_direction,
	ID3D12GraphicsCommandList6* mesh_commands,bool prefer_mesh_shader,bool force_double_sided,bool depth_only)
{
	const auto& nodes = pose && pose->size() == model.nodes().size() ? *pose : model.nodes();
	const auto& node=nodes.at(node_index);
	if(node.mesh>=0&&node.mesh<static_cast<int>(model.meshes().size())) {
		scene_constants constants{}; const bool skinned=node.skin>=0&&node.skin<static_cast<int>(model.skins().size());
		XMStoreFloat4x4(&constants.world,skinned?XMLoadFloat4x4(&world):XMLoadFloat4x4(&node.global_transform)*XMLoadFloat4x4(&world));
		constants.view=view; constants.projection=projection; constants.skinned=skinned?1u:0u;
		XMStoreFloat4x4(&constants.normal_world, XMMatrixTranspose(XMMatrixInverse(nullptr, XMLoadFloat4x4(&constants.world))));
		constants.padding[0] = force_double_sided ? 1.0f : 0.0f;
		// ビュー空間で計算した法線・反射方向をCubeMap用のワールド空間へ戻す。
		XMStoreFloat4x4(&constants.inverse_view, XMMatrixInverse(nullptr, XMLoadFloat4x4(&view)));
		constants.environment_parameters = {
			(std::max)(ibl_intensity, 0.0f),
			environment ? static_cast<float>(environment->specular_mip_count()) : 1.0f,
			environment ? 1.0f : 0.0f, environment ? 1.0f : 0.0f };
		XMVECTOR light = XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&light_direction), XMLoadFloat4x4(&view)));
		XMStoreFloat4(&constants.light_direction, XMVectorSetW(light, 0.0f));
		auto joint_address=default_joint_address_;
		if(skinned) {
			joint_constants joints{};
			for(auto& matrix:joints.matrices) XMStoreFloat4x4(&matrix,XMMatrixIdentity());
			std::vector<XMFLOAT4X4> matrices; model.build_joint_matrices(node.skin,matrices,pose);
			std::copy_n(matrices.begin(),(std::min)(matrices.size(),max_joints),joints.matrices);
			joint_address=upload(&joints,sizeof(joints));
		}
		if(!joint_address) return;
		for(const auto& primitive:model.meshes()[node.mesh].primitives) {
			const gltf_model12::material* material = primitive.material >= 0 &&
				primitive.material < static_cast<int>(model.materials().size()) ? &model.materials()[primitive.material] : nullptr;
			int texture_indices[5]{ -1, -1, -1, -1, -1 };
			std::uint32_t flags = 0;
			if (material)
			{
				constants.base_color_factor = material->base_color;
				constants.emissive_metallic = { material->emissive.x, material->emissive.y, material->emissive.z, material->metallic };
				constants.roughness_flags.x = material->roughness;
				texture_indices[0] = material->base_color_texture;
				texture_indices[1] = material->metallic_roughness_texture;
				texture_indices[2] = material->normal_texture;
				texture_indices[3] = material->occlusion_texture;
				texture_indices[4] = material->emissive_texture;
				constants.roughness_flags.z = static_cast<float>(material->alpha_mode);
				constants.roughness_flags.w = material->alpha_cutoff;
			}
			// 鏡像はカリング回避のため両面で描くが、材質そのものの裏面とは区別する。
			constants.padding[1] = material && material->double_sided && !force_double_sided ? 1.0f : 0.0f;
			// 半透明のガラス等を不透明な影として焼き込まない。透過影は今後の専用処理で扱う。
			if (depth_only && material && material->alpha_mode == 2) continue;
			D3D12_GPU_DESCRIPTOR_HANDLE texture_handles[5]{};
			for (UINT texture_slot = 0; texture_slot < 5; ++texture_slot)
			{
				D3D12_GPU_DESCRIPTOR_HANDLE handle = model.texture_srv(texture_indices[texture_slot]);
				if (handle.ptr != 0) flags |= 1u << texture_slot;
				else handle = fallback_srv_.gpu;
				texture_handles[texture_slot] = handle;
			}
			constants.roughness_flags.y = static_cast<float>(flags);
			if (material && material->procedural_stone) constants.roughness_flags.y=static_cast<float>(flags|64u);
			const auto scene_address=upload(&constants,sizeof(constants));
			if(!scene_address) return;
			const bool use_mesh = !depth_only && prefer_mesh_shader && mesh_commands && primitive.meshlet_count > 0 &&
				(!material || material->alpha_mode != 2);
			command_list->SetGraphicsRootSignature(use_mesh ? mesh_root_signature_.Get() : root_signature_.Get());
			command_list->SetPipelineState(depth_only ? shadow_pipeline_state_.Get() : use_mesh ?
				(force_double_sided || (material && material->double_sided) ? mesh_double_sided_pipeline_state_.Get() : mesh_pipeline_state_.Get()) :
				(material && material->alpha_mode == 2 ? alpha_pipeline_state_.Get() :
					(force_double_sided || (material && material->double_sided) ? double_sided_pipeline_state_.Get() : pipeline_state_.Get())));
			command_list->SetGraphicsRootConstantBufferView(0,scene_address);
			command_list->SetGraphicsRootConstantBufferView(1,joint_address);
			for (UINT texture_slot = 0; texture_slot < 5; ++texture_slot)
				command_list->SetGraphicsRootDescriptorTable(texture_slot + 2, texture_handles[texture_slot]);
			command_list->SetGraphicsRootDescriptorTable(7, environment ? environment->diffuse_srv() : fallback_srv_.gpu);
			command_list->SetGraphicsRootDescriptorTable(8, environment ? environment->specular_srv() : fallback_srv_.gpu);
			command_list->SetGraphicsRootDescriptorTable(9, environment ? environment->lut_srv() : fallback_srv_.gpu);
			command_list->SetGraphicsRootDescriptorTable(use_mesh ? 14 : 10, depth_only ? fallback_srv_.gpu : shadow_srv_);
			command_list->SetGraphicsRootConstantBufferView(use_mesh ? 15 : 11, lighting_address_);
			if (use_mesh)
			{
				command_list->SetGraphicsRootDescriptorTable(10, primitive.vertex_srv.gpu);
				command_list->SetGraphicsRootDescriptorTable(11, primitive.meshlet_srv.gpu);
				command_list->SetGraphicsRootDescriptorTable(12, primitive.meshlet_vertex_indices_srv.gpu);
				command_list->SetGraphicsRootDescriptorTable(13, primitive.meshlet_triangles_srv.gpu);
				mesh_commands->DispatchMesh(primitive.meshlet_count, 1, 1);
			}
			else
			{
				command_list->IASetVertexBuffers(0,1,&primitive.vertex_view);
				command_list->IASetIndexBuffer(&primitive.index_view);
				command_list->DrawIndexedInstanced(primitive.index_count,1,0,0,0);
			}
		}
	}
	for(int child:node.children) draw_node(command_list,model,child,world,view,projection,environment,ibl_intensity,pose,
		light_direction,mesh_commands,prefer_mesh_shader,force_double_sided,depth_only);
}
