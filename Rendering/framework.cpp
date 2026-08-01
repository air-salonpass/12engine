#include "framework.h"
#include "shader.h"
#include "texture.h"
#include <DirectXMath.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <commdlg.h>
#include <filesystem>

namespace
{
	std::string to_native_path(const wchar_t* text)
	{
		if (!text || !*text)
		{
			return {};
		}
		const int size = WideCharToMultiByte(
			CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
		std::string result(static_cast<size_t>(size), '\0');
		WideCharToMultiByte(
			CP_ACP, 0, text, -1, result.data(), size, nullptr, nullptr);
		result.pop_back();
		return result;
	}
}

framework::framework(HWND hwnd) : hwnd(hwnd)
{
}

bool framework::load_gltf(const std::string& filename)
{
	modelLoadError.clear();
	auto candidate = std::make_unique<gltf_model>(device, filename);
	if (!candidate->loaded)
	{
		modelLoadError = candidate->load_error.empty() ?
			"The selected model could not be loaded." : candidate->load_error;
		return false;
	}

	gltf_models[0] = std::move(candidate);
	loadedModelPath = filename;
	gltfAnimationIndex = 0;
	gltfAnimationTime = 0.0f;
	gltfAnimationPlaying = !gltf_models[0]->animations.empty();
	focus_gltf_model();
	return true;
}

bool framework::browse_and_load_gltf()
{
	if (modelLoading)
	{
		return false;
	}
	wchar_t filename[MAX_PATH]{};
	OPENFILENAMEW dialog{};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = hwnd;
	dialog.lpstrFilter = L"glTF model (*.gltf;*.glb)\0*.gltf;*.glb\0All files (*.*)\0*.*\0";
	dialog.lpstrFile = filename;
	dialog.nMaxFile = _countof(filename);
	dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&dialog))
	{
		return false;
	}
	queue_gltf_load(to_native_path(filename));
	return true;
}

void framework::queue_gltf_load(const std::string& filename)
{
	if (modelLoading || filename.empty())
	{
		return;
	}
	modelLoadError.clear();
	pendingModelPath = filename;
	modelLoading = true;
	Microsoft::WRL::ComPtr<ID3D11Device> loading_device(device);
	try
	{
		pendingGltfModel = std::async(
			std::launch::async,
			[loading_device, filename]()
			{
				const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
				auto result = std::make_unique<gltf_model>(loading_device.Get(), filename);
				if (SUCCEEDED(com_result))
				{
					CoUninitialize();
				}
				return result;
			});
	}
	catch (const std::exception& exception)
	{
		modelLoading = false;
		modelLoadError = exception.what();
	}
}

void framework::poll_gltf_load()
{
	if (!modelLoading || !pendingGltfModel.valid() ||
		pendingGltfModel.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
	{
		return;
	}

	try
	{
		auto candidate = pendingGltfModel.get();
		if (candidate && candidate->loaded)
		{
			gltf_models[0] = std::move(candidate);
			loadedModelPath = pendingModelPath;
			gltfAnimationIndex = 0;
			gltfAnimationTime = 0.0f;
			gltfAnimationPlaying = !gltf_models[0]->animations.empty();
			focus_gltf_model();
		}
		else
		{
			modelLoadError = candidate && !candidate->load_error.empty() ?
				candidate->load_error : "The selected model could not be loaded.";
		}
	}
	catch (const std::exception& exception)
	{
		modelLoadError = exception.what();
	}
	modelLoading = false;
	pendingModelPath.clear();
}

void framework::focus_gltf_model()
{
	if (!gltf_models[0] || !gltf_models[0]->has_bounds)
	{
		return;
	}
	const DirectX::XMFLOAT3& bmin = gltf_models[0]->bounding_box_min;
	const DirectX::XMFLOAT3& bmax = gltf_models[0]->bounding_box_max;
	const float scale = (std::max)(std::abs(gltfScale), 0.0001f);
	cameraTarget[0] = gltfPosition[0] - (bmin.x + bmax.x) * 0.5f * scale;
	cameraTarget[1] = gltfPosition[1] + (bmin.y + bmax.y) * 0.5f * scale;
	cameraTarget[2] = gltfPosition[2] + (bmin.z + bmax.z) * 0.5f * scale;
	const float sx = (bmax.x - bmin.x) * scale;
	const float sy = (bmax.y - bmin.y) * scale;
	const float sz = (bmax.z - bmin.z) * scale;
	const float radius = 0.5f * std::sqrt(sx * sx + sy * sy + sz * sz);
	cameraDistance = (std::clamp)(radius * 2.5f, 0.5f, 100.0f);
}

bool framework::initialize()
{
	for (int i = 0; i < 8; i++)
	{
		sprites[i] = nullptr;
		sprite_batches[i] = nullptr;
	}

	HRESULT hr(S_OK);

	UINT create_device_flags(0);

#ifdef _DEBUG
	create_device_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL feature_levels(D3D_FEATURE_LEVEL_11_0);

	DXGI_SWAP_CHAIN_DESC swap_chain_desc{};

	swap_chain_desc.BufferCount = 1;
	swap_chain_desc.BufferDesc.Width = SCREEN_WIDTH;
	swap_chain_desc.BufferDesc.Height = SCREEN_HEIGHT;
	swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swap_chain_desc.BufferDesc.RefreshRate.Numerator = 60;
	swap_chain_desc.BufferDesc.RefreshRate.Denominator = 1;
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.OutputWindow = hwnd;
	swap_chain_desc.SampleDesc.Count = 1;
	swap_chain_desc.SampleDesc.Quality = 0;
	swap_chain_desc.Windowed = !FULLSCREEN;

	hr = D3D11CreateDeviceAndSwapChain(
		NULL,
		D3D_DRIVER_TYPE_HARDWARE,
		NULL,
		create_device_flags,
		&feature_levels,
		1,
		D3D11_SDK_VERSION,
		&swap_chain_desc,
		&swap_chain,
		&device,
		NULL,
		&immediate_context);

	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	ID3D11Texture2D* back_buffer{};

	hr = swap_chain->GetBuffer(
		0,
		__uuidof(ID3D11Texture2D),
		reinterpret_cast<LPVOID*>(&back_buffer));

	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	hr = device->CreateRenderTargetView(
		back_buffer,
		NULL,
		&render_target_view);

	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	back_buffer->Release();

	ID3D11Texture2D* depth_stencil_buffer{};

	D3D11_TEXTURE2D_DESC texture2d_desc{};

	texture2d_desc.Width = SCREEN_WIDTH;
	texture2d_desc.Height = SCREEN_HEIGHT;
	texture2d_desc.MipLevels = 1;
	texture2d_desc.ArraySize = 1;
	texture2d_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	texture2d_desc.SampleDesc.Count = 1;
	texture2d_desc.SampleDesc.Quality = 0;
	texture2d_desc.Usage = D3D11_USAGE_DEFAULT;
	texture2d_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	hr = device->CreateTexture2D(
		&texture2d_desc,
		NULL,
		&depth_stencil_buffer);

	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	D3D11_DEPTH_STENCIL_VIEW_DESC depth_stencil_view_desc{};

	depth_stencil_view_desc.Format = texture2d_desc.Format;
	depth_stencil_view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

	hr = device->CreateDepthStencilView(
		depth_stencil_buffer,
		&depth_stencil_view_desc,
		&depth_stencil_view);

	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	depth_stencil_buffer->Release();

	// UNIT7: 深度テストと深度書込みのON／OFFを組み合わせた4状態をすべて用意する。
	const BOOL depth_test_enabled[4]{ TRUE, TRUE, FALSE, FALSE };
	const D3D11_DEPTH_WRITE_MASK depth_write_masks[4]
	{
		D3D11_DEPTH_WRITE_MASK_ALL,
		D3D11_DEPTH_WRITE_MASK_ZERO,
		D3D11_DEPTH_WRITE_MASK_ALL,
		D3D11_DEPTH_WRITE_MASK_ZERO
	};
	for (size_t state_index = 0; state_index < 4; ++state_index)
	{
		D3D11_DEPTH_STENCIL_DESC depth_stencil_desc{};
		depth_stencil_desc.DepthEnable = depth_test_enabled[state_index];
		depth_stencil_desc.DepthWriteMask = depth_write_masks[state_index];
		depth_stencil_desc.DepthFunc = depth_test_enabled[state_index] ?
			D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_ALWAYS;
		hr = device->CreateDepthStencilState(
			&depth_stencil_desc,
			&depth_stencil_states[state_index]);
		_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
	}

	D3D11_BLEND_DESC blend_desc{};

	blend_desc.RenderTarget[0].BlendEnable = TRUE;
	blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	// アルファ値側の合成方法も明示する。未設定(0)は無効な列挙値になる。
	blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blend_desc.RenderTarget[0].RenderTargetWriteMask =
		D3D11_COLOR_WRITE_ENABLE_ALL;

	hr = device->CreateBlendState(&blend_desc, &blend_states[0]);
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	// UNIT8: 同じスプライトで加算・減算・乗算の違いも確認できるよう、各ブレンド式を保持する。
	blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	hr = device->CreateBlendState(&blend_desc, &blend_states[1]);
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
	hr = device->CreateBlendState(&blend_desc, &blend_states[2]);
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_COLOR;
	blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
	blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	hr = device->CreateBlendState(&blend_desc, &blend_states[3]);
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	D3D11_RASTERIZER_DESC rasterizer_desc{};

	rasterizer_desc.FillMode = D3D11_FILL_SOLID;
	rasterizer_desc.CullMode = D3D11_CULL_BACK;
	// 右手系から左手系へ反転した後の頂点順に合わせ、反時計回りを表面とする。
	rasterizer_desc.FrontCounterClockwise = TRUE;
	rasterizer_desc.DepthClipEnable = TRUE;

	hr = device->CreateRasterizerState(
		&rasterizer_desc,
		rasterizer_states[0].GetAddressOf());

	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	rasterizer_desc.FillMode = D3D11_FILL_WIREFRAME;

	hr = device->CreateRasterizerState(
		&rasterizer_desc,
		rasterizer_states[1].GetAddressOf());

	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	// 全画面画像は裏面を持たないため、面カリングなしのソリッド描画状態を用意する。
	rasterizer_desc.FillMode = D3D11_FILL_SOLID;
	rasterizer_desc.CullMode = D3D11_CULL_NONE;
	hr = device->CreateRasterizerState(
		&rasterizer_desc,
		rasterizer_states[2].GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	D3D11_VIEWPORT viewport{};

	viewport.Width = static_cast<float>(SCREEN_WIDTH);
	viewport.Height = static_cast<float>(SCREEN_HEIGHT);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	immediate_context->RSSetViewports(
		1,
		&viewport);

	// シェーダーのs0～s2へ渡す3種類のサンプラーを作る。
	const D3D11_FILTER sampler_filters[3]
	{
		D3D11_FILTER_MIN_MAG_MIP_POINT,
		D3D11_FILTER_MIN_MAG_MIP_LINEAR,
		D3D11_FILTER_ANISOTROPIC
	};

	for (size_t sampler_index = 0; sampler_index < 3; ++sampler_index)
	{
		D3D11_SAMPLER_DESC sampler_desc{};
		sampler_desc.Filter = sampler_filters[sampler_index];
		// UNIT36: glTF既定のREPEATへ合わせ、材質に使う異方性サンプラーだけWRAPにする。
		// POINT／LINEARは、ブラーが画像端の反対側を拾わないよう従来どおりBORDERを使う。
		const D3D11_TEXTURE_ADDRESS_MODE address_mode = sampler_index == 2 ?
			D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_BORDER;
		sampler_desc.AddressU = address_mode;
		sampler_desc.AddressV = address_mode;
		sampler_desc.AddressW = address_mode;
		sampler_desc.BorderColor[0] = 0.0f;
		sampler_desc.BorderColor[1] = 0.0f;
		sampler_desc.BorderColor[2] = 0.0f;
		sampler_desc.BorderColor[3] = 0.0f;
		sampler_desc.MaxAnisotropy = 16;
		sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

		hr = device->CreateSamplerState(
			&sampler_desc,
			&sampler_states[sampler_index]);
		_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
	}

	D3D11_BUFFER_DESC buffer_desc{};

	// カメラ・ライト情報を全描画物へ渡すシーン定数バッファを作る。
	buffer_desc.ByteWidth =
		sizeof(scene_constants);

	buffer_desc.Usage =
		D3D11_USAGE_DEFAULT;

	buffer_desc.BindFlags =
		D3D11_BIND_CONSTANT_BUFFER;

	device->CreateBuffer(
		&buffer_desc,
		nullptr,
		&constant_buffers[0]);

	// 高輝度抽出・ブラー・トーンマッピングの調整値をPSのb0へ渡す。
	buffer_desc.ByteWidth = sizeof(post_effect_constants);
	hr = device->CreateBuffer(
		&buffer_desc,
		nullptr,
		&constant_buffers[1]);
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	// =====================================================
	// Geometric Primitive
	// =====================================================

	geometric_primitives[0] =
		std::make_unique<geometric_primitive>(
			device,
			CUBE);

	geometric_primitives[1] =
		std::make_unique<geometric_primitive>(
			device,
			CYLINDER);

	geometric_primitives[2] =
		std::make_unique<geometric_primitive>(
			device,
			SPHERE);

	geometric_primitives[3] =
		std::make_unique<geometric_primitive>(
			device,
			CAPSULE);

	// =====================================================
	// Static Mesh
	// =====================================================

	static_meshes[0] =
		std::make_unique<static_mesh>(
			device,
			L"./resources/Rock/Rock.obj");

	// UNIT32のテスト背景。1600x900の画像を画面全体へ縮小して使用する。
	sprite_batches[0] = std::make_unique<sprite_batch>(
		device,
		L".\\resources\\screenshot.jpg",
		1);
	// UNIT2～10: テクセル切り出し・回転・頂点色・透過・バッチ描画を最終画面でも確認する。
	sprite_batches[1] = std::make_unique<sprite_batch>(
		device,
		L".\\resources\\player-sprites.png",
		4);

	// UNIT40: キャプチャツールでHDRから生成した4つのIBLテクスチャを、t32～t35と同じ順序で読む。
	D3D11_TEXTURE2D_DESC environment_texture_desc{};
	hr = load_texture_from_file(
		device,
		L".\\resources\\environments\\hilly_terrain_01_puresky\\skybox.dds",
		shader_resource_views[0].ReleaseAndGetAddressOf(),
		&environment_texture_desc);
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
	hr = load_texture_from_file(
		device,
		L".\\resources\\environments\\hilly_terrain_01_puresky\\diffuse_iem.dds",
		shader_resource_views[1].ReleaseAndGetAddressOf(),
		&environment_texture_desc);
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
	hr = load_texture_from_file(
		device,
		L".\\resources\\environments\\hilly_terrain_01_puresky\\specular_pmrem.dds",
		shader_resource_views[2].ReleaseAndGetAddressOf(),
		&environment_texture_desc);
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
	hr = load_texture_from_file(
		device,
		L".\\resources\\environments\\hilly_terrain_01_puresky\\lut_ggx.dds",
		shader_resource_views[3].ReleaseAndGetAddressOf(),
		&environment_texture_desc);
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	// UNIT30では大きなnico.fbxを使い、初回FBX解析と2回目のCereal読込を比較する。
	skinned_meshes[0] =
		std::make_unique<skinned_mesh>(
			device,
			".\\resources\\nico.fbx",
			false,
			30.0f); // アニメーションは1秒あたり30キーフレームで取り込む。

	// UNIT41: FBX2glTFでcube.004.fbxから変換したGLBを読み込み、変換結果を実機で確認する。
	load_gltf(".\\resources\\gltf\\CesiumMan\\CesiumMan.gltf");

	// 1280x720のHDRシーンと、ダウンサンプリングした640x360の高輝度画像を作る。
	framebuffers[0] = std::make_unique<framebuffer>(device, SCREEN_WIDTH, SCREEN_HEIGHT);
	framebuffers[1] = std::make_unique<framebuffer>(device, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
	bit_block_transfer = std::make_unique<fullscreen_quad>(device);

	// 全画面転送時に差し替える、高輝度抽出用PSとブルーム合成用PSを読み込む。
	create_ps_from_cso(
		device,
		"luminance_extraction_ps.cso",
		pixel_shaders[0].GetAddressOf());
	create_ps_from_cso(
		device,
		"blur_ps.cso",
		pixel_shaders[1].GetAddressOf());

	return true;
}

void framework::update(float elapsed_time)
{
	poll_gltf_load();
}

void framework::render(float elapsed_time)
{
	using namespace DirectX;
	// Releaseでは入力UIを無効化しても、保存された初期角度からカメラを計算できるようにする。
	float yaw = XMConvertToRadians(cameraYawDegrees);
	float pitch = XMConvertToRadians(cameraPitchDegrees);
	float sin_yaw = std::sin(yaw);
	float cos_yaw = std::cos(yaw);
	float sin_pitch = std::sin(pitch);
	float cos_pitch = std::cos(pitch);

#ifdef USE_IMGUI
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(390.0f, 700.0f), ImGuiCond_Always);
	ImGui::Begin("Model Viewer");
	if (ImGui::Button("Open glTF / GLB..."))
	{
		browse_and_load_gltf();
	}
	ImGui::SameLine();
	if (ImGui::Button("Focus (F)"))
	{
		focus_gltf_model();
	}
	ImGui::SameLine();
	ImGui::Checkbox("Visible", &showGltfShowcase);
	if (!loadedModelPath.empty())
	{
		ImGui::TextWrapped("%s", loadedModelPath.c_str());
	}
	if (!modelLoadError.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "%s", modelLoadError.c_str());
	}

	ImGui::Separator();
	ImGui::Checkbox("Ground Grid", &showViewerGrid);
	ImGui::SameLine();
	ImGui::Checkbox("Bounds", &showModelBounds);
	ImGui::SameLine();
	ImGui::Checkbox("Backdrop", &showBackdrop);

	if (gltf_models[0] && gltf_models[0]->loaded)
	{
		ImGui::Text(
			"Nodes %d  Meshes %d  Materials %d  Animations %d",
			static_cast<int>(gltf_models[0]->nodes.size()),
			static_cast<int>(gltf_models[0]->meshes.size()),
			static_cast<int>(gltf_models[0]->materials.size()),
			static_cast<int>(gltf_models[0]->animations.size()));

		if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (!gltf_models[0]->animations.empty())
			{
				gltfAnimationIndex = (std::clamp)(
					gltfAnimationIndex, 0,
					static_cast<int>(gltf_models[0]->animations.size()) - 1);
				const gltf_model::animation& selected_animation =
					gltf_models[0]->animations[static_cast<size_t>(gltfAnimationIndex)];
				const std::string selected_name = selected_animation.name.empty() ?
					("Animation " + std::to_string(gltfAnimationIndex)) : selected_animation.name;
				if (ImGui::BeginCombo("Clip", selected_name.c_str()))
				{
					for (size_t index = 0; index < gltf_models[0]->animations.size(); ++index)
					{
						const gltf_model::animation& animation = gltf_models[0]->animations[index];
						const std::string name = animation.name.empty() ?
							("Animation " + std::to_string(index)) : animation.name;
						const bool selected = static_cast<int>(index) == gltfAnimationIndex;
						if (ImGui::Selectable(name.c_str(), selected))
						{
							gltfAnimationIndex = static_cast<int>(index);
							gltfAnimationTime = 0.0f;
						}
						if (selected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				ImGui::Checkbox("Play", &gltfAnimationPlaying);
				ImGui::SameLine();
				ImGui::Checkbox("Loop", &gltfAnimationLoop);
				ImGui::DragFloat("Speed", &gltfAnimationSpeed, 0.05f, 0.0f, 4.0f, "%.2fx");
				const float duration = gltf_models[0]->animations[
					static_cast<size_t>(gltfAnimationIndex)].duration;
				ImGui::SliderFloat("Time", &gltfAnimationTime, 0.0f, duration, "%.3f sec");
			}
			else
			{
				ImGui::TextDisabled("This model has no animation clips.");
			}
		}

		if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::DragFloat3("Model Position", gltfPosition, 0.01f);
			ImGui::DragFloat3("Model Rotation", gltfRotation, 1.0f);
			ImGui::DragFloat("Model Scale", &gltfScale, 0.01f, 0.0001f, 1000.0f, "%.4f");
		}

		if (ImGui::CollapsingHeader("PBR Materials"))
		{
			for (size_t index = 0; index < gltf_models[0]->materials.size(); ++index)
			{
				const gltf_model::material& material = gltf_models[0]->materials[index];
				const std::string label = material.name.empty() ?
					("Material " + std::to_string(index)) : material.name;
				if (ImGui::TreeNode(reinterpret_cast<void*>(index + 1), "%s", label.c_str()))
				{
					const auto& pbr = material.data.pbr_metallic_roughness_data;
					ImGui::ColorButton(
						"Base Color",
						ImVec4(pbr.basecolor_factor[0], pbr.basecolor_factor[1],
							pbr.basecolor_factor[2], pbr.basecolor_factor[3]));
					ImGui::Text("Metalness %.3f", pbr.metallic_factor);
					ImGui::Text("Roughness %.3f", pbr.roughness_factor);
					ImGui::Text("Alpha mode %d", material.data.alpha_mode);
					ImGui::TreePop();
				}
			}
		}

		if (ImGui::CollapsingHeader("Scene Nodes"))
		{
			for (size_t index = 0; index < gltf_models[0]->nodes.size(); ++index)
			{
				const auto& node = gltf_models[0]->nodes[index];
				ImGui::BulletText("%s", node.name.empty() ? "(unnamed node)" : node.name.c_str());
			}
		}
	}
	ImGui::Separator();
	ImGui::TextDisabled("RMB: orbit  MMB: pan  Wheel: zoom  F: focus");
	ImGui::End();

	// 最終確認画面では右側へ固定し、左側の2D・3D展示物を隠さない幅にする。
	ImGui::SetNextWindowPos(
		ImVec2(static_cast<float>(SCREEN_WIDTH - 410), 10.0f),
		ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(400.0f, 700.0f), ImGuiCond_Always);
	ImGui::Begin("Rendering Settings");
	// 数値入力の幅を固定し、右側に表示される項目名がウインドウ外へ切れないようにする。
	ImGui::PushItemWidth(220.0f);

	// 最終確認に必要な2モデルの表示切り替えだけを上部へまとめる。
	if (ImGui::CollapsingHeader(
		"Showcase Visibility",
		ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("FBX Skinned Animation", &showSkinnedMeshShowcase);
		ImGui::Checkbox("UNIT41 Converted glTF", &showGltfShowcase);
	}

	ImGui::Separator();

	ImGui::Text("Camera");
	ImGui::DragFloat3(
		"Camera Target",
		cameraTarget,
		0.1f);

	// マウス位置を画面中央基準の-1～+1へ変換し、カーソル地点へ寄るズームに使う。
	const ImGuiIO& io = ImGui::GetIO();
	const ImVec2 mouse_position = io.MousePos;
	ImGui::Text(
		"Mouse Position: %.0f, %.0f",
		mouse_position.x,
		mouse_position.y);

	// シーン上で右ボタンを押しながら動かした量を、オービット角度へ変換する。
	if (io.MouseDown[1] && !io.WantCaptureMouse)
	{
		// 横はカメラ周回の向きを反転し、縦はドラッグ方向へ追従する符号にする。
		cameraYawDegrees -= io.MouseDelta.x * cameraOrbitSensitivity;
		cameraPitchDegrees += io.MouseDelta.y * cameraOrbitSensitivity;
		cameraPitchDegrees = (std::clamp)(cameraPitchDegrees, -89.0f, 89.0f);
	}
	ImGui::DragFloat2(
		"Orbit Yaw Pitch",
		&cameraYawDegrees,
		0.2f);
	ImGui::DragFloat(
		"Orbit Sensitivity",
		&cameraOrbitSensitivity,
		0.01f,
		0.01f,
		2.0f);

	// ImGuiで角度を直接編集した場合も、同じフレームのカメラへ反映する。
	yaw = XMConvertToRadians(cameraYawDegrees);
	pitch = XMConvertToRadians(cameraPitchDegrees);
	sin_yaw = std::sin(yaw);
	cos_yaw = std::cos(yaw);
	sin_pitch = std::sin(pitch);
	cos_pitch = std::cos(pitch);
	// 回転後の画面右方向と画面上方向。カーソル基準ズームの注視点移動にも使う。
	const XMFLOAT3 camera_right{ cos_yaw, 0.0f, sin_yaw };
	const XMFLOAT3 camera_up
	{
		-sin_yaw * sin_pitch,
		cos_pitch,
		cos_yaw * sin_pitch
	};
	if (io.MouseDown[2] && !io.WantCaptureMouse)
	{
		const float pan_scale = cameraDistance * 0.0015f;
		const float shift_right = -io.MouseDelta.x * pan_scale;
		const float shift_up = io.MouseDelta.y * pan_scale;
		cameraTarget[0] += camera_right.x * shift_right + camera_up.x * shift_up;
		cameraTarget[1] += camera_right.y * shift_right + camera_up.y * shift_up;
		cameraTarget[2] += camera_right.z * shift_right + camera_up.z * shift_up;
	}
	const bool focus_key_down = (GetAsyncKeyState('F') & 0x8000) != 0;
	if (focus_key_down && !focusKeyWasDown && !io.WantCaptureKeyboard)
	{
		focus_gltf_model();
	}
	focusKeyWasDown = focus_key_down;

	const float mouse_wheel = io.MouseWheel;
	if (mouse_wheel != 0.0f && !io.WantCaptureMouse)
	{
		const float previous_distance = cameraDistance;
		const float next_distance = (std::clamp)(
			cameraDistance - mouse_wheel * cameraWheelZoomSpeed,
			0.5f,
			100.0f);

		if (io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f &&
			mouse_position.x >= 0.0f && mouse_position.x <= io.DisplaySize.x &&
			mouse_position.y >= 0.0f && mouse_position.y <= io.DisplaySize.y)
		{
			const float normalized_x =
				(mouse_position.x / io.DisplaySize.x) * 2.0f - 1.0f;
			const float normalized_y =
				1.0f - (mouse_position.y / io.DisplaySize.y) * 2.0f;
			const float aspect_ratio = io.DisplaySize.x / io.DisplaySize.y;
			const float half_fov_tangent = std::tan(
				XMConvertToRadians(cameraFovDegrees) * 0.5f);
			const float distance_change = previous_distance - next_distance;
			const float shift_right = normalized_x * distance_change *
				half_fov_tangent * aspect_ratio;
			const float shift_up = normalized_y * distance_change * half_fov_tangent;

			// 回転後の注視面上でカーソルが指した地点を保つよう、注視点XYZを移動する。
			cameraTarget[0] +=
				camera_right.x * shift_right + camera_up.x * shift_up;
			cameraTarget[1] +=
				camera_right.y * shift_right + camera_up.y * shift_up;
			cameraTarget[2] +=
				camera_right.z * shift_right + camera_up.z * shift_up;
		}

		cameraDistance = next_distance;
	}
	ImGui::DragFloat(
		"Camera Distance",
		&cameraDistance,
		0.1f,
		0.5f,
		100.0f);
	ImGui::SliderFloat(
		"Camera FOV",
		&cameraFovDegrees,
		20.0f,
		90.0f,
		"%.1f deg");
	ImGui::DragFloat(
		"Wheel Zoom Speed",
		&cameraWheelZoomSpeed,
		0.05f,
		0.05f,
		10.0f);

	ImGui::Separator();

	// UNIT32のポストエフェクト値はデバッグ時だけUIへ表示し、Releaseでは初期値を使う。
	ImGui::Text("Post Process");
	ImGui::SliderFloat("Luminance Threshold", &luminanceThreshold, 0.0f, 2.0f);
	ImGui::SliderFloat("Gaussian Sigma", &gaussianSigma, 0.1f, 5.0f);
	ImGui::SliderFloat("Bloom Intensity", &bloomIntensity, 0.0f, 5.0f);
	ImGui::SliderFloat("Tone Mapping Exposure", &toneMappingExposure, 0.1f, 5.0f);

	ImGui::Separator();

	// UNIT41モデルの見た目を調整する操作だけを残し、読み込み件数などの説明表示は省く。
	ImGui::Text("UNIT41 glTF");
	if (gltf_models[0])
	{
		// UNIT40: HDR環境光の寄与だけをデバッグ時に比較・調整できるようにする。
		ImGui::SliderFloat("IBL Intensity", &iblIntensity, 0.0f, 2.0f, "%.2f");

		// アニメーションを持つglTFを読み込んだ場合だけ、再生操作を表示する。
		if (!gltf_models[0]->animations.empty())
		{
			ImGui::Checkbox("Play glTF Animation", &gltfAnimationPlaying);
			ImGui::DragInt(
				"glTF Animation Index",
				&gltfAnimationIndex,
				1.0f,
				0,
				static_cast<int>(gltf_models[0]->animations.size()) - 1);
			const size_t animation_index = static_cast<size_t>((std::clamp)(
				gltfAnimationIndex,
				0,
				static_cast<int>(gltf_models[0]->animations.size()) - 1));
			ImGui::SliderFloat(
				"glTF Animation Time",
				&gltfAnimationTime,
				0.0f,
				gltf_models[0]->animations.at(animation_index).duration,
				"%.2f sec");
		}

		// glTFモデルの配置値もデバッグUI内にまとめ、Releaseでは初期値だけを使用する。
		ImGui::DragFloat3("glTF Position", gltfPosition, 0.01f);
		ImGui::DragFloat3("glTF Rotation", gltfRotation, 1.0f);
		// UNIT41: センチメートルから変換された小型テストメッシュに合わせ、大きな倍率まで調整可能にする。
		ImGui::DragFloat("glTF Scale", &gltfScale, 1.0f, 0.01f, 5000.0f, "%.3f");
	}

	ImGui::Separator();

	ImGui::Text("Light");
	ImGui::DragFloat3(
		"Light Direction",
		&lightX,
		0.01f);

	ImGui::Separator();

	// 現在表示しているFBXアニメーションモデルの変換値を調整する。
	ImGui::Text("FBX Transform");

	ImGui::DragFloat3(
		"Position",
		cubePos,
		0.01f);

	ImGui::DragFloat3(
		"Rotation",
		cubeRot,
		1.0f);

	ImGui::DragFloat3(
		"Scale",
		cubeScale,
		0.01f);

	ImGui::ColorEdit4(
		"Color",
		cubeColor);

	ImGui::PopItemWidth();
	ImGui::End();
#endif

	// 前フレームでSRVとして使ったバッファを全スロットから外す。
	// 同じテクスチャをRTVとSRVへ同時バインドしたときのD3D11警告を防ぐ。
	ID3D11RenderTargetView* null_render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
	immediate_context->OMSetRenderTargets(
		_countof(null_render_target_views),
		null_render_target_views,
		nullptr);
	ID3D11ShaderResourceView* null_shader_resource_views[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
	immediate_context->VSSetShaderResources(
		0,
		_countof(null_shader_resource_views),
		null_shader_resource_views);
	immediate_context->PSSetShaderResources(
		0,
		_countof(null_shader_resource_views),
		null_shader_resource_views);

	const FLOAT back_buffer_color[]{ 0.2f, 0.2f, 0.2f, 1.0f };
	immediate_context->ClearRenderTargetView(render_target_view, back_buffer_color);
	immediate_context->ClearDepthStencilView(
		depth_stencil_view,
		D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
		1.0f,
		0);
	immediate_context->OMSetRenderTargets(1, &render_target_view, depth_stencil_view);

	// ここから背景とキャラクターをHDRのオフスクリーンバッファへ描く。
	framebuffers[0]->clear(immediate_context);
	framebuffers[0]->activate(immediate_context);

	D3D11_VIEWPORT viewport{};
	UINT num_viewports = 1;

	immediate_context->RSGetViewports(
		&num_viewports,
		&viewport);

	float aspect_ratio =
		viewport.Width / viewport.Height;

	// 画面の縦横比から透視投影行列を作る。
	XMMATRIX P =
		XMMatrixPerspectiveFovLH(
			XMConvertToRadians(cameraFovDegrees),
			aspect_ratio,
			0.1f,
			100.0f);

	XMVECTOR eye =
		XMVectorSet(
			cameraTarget[0] + sin_yaw * cos_pitch * cameraDistance,
			cameraTarget[1] + sin_pitch * cameraDistance,
			cameraTarget[2] - cos_yaw * cos_pitch * cameraDistance,
			1);

	// 右ドラッグで回転しても、常に指定した注視点を見る。
	XMVECTOR focus =
		XMVectorSet(
			cameraTarget[0],
			cameraTarget[1],
			cameraTarget[2],
			1);

	XMVECTOR up =
		XMVectorSet(0, 1, 0, 0);

	// カメラ位置・注視点・上方向からビュー行列を作る。
	XMMATRIX V =
		XMMatrixLookAtLH(
			eye,
			focus,
			up);

	// ビュー投影行列、ライト方向、カメラ位置をGPUへ転送する。
	scene_constants data{};

	XMStoreFloat4x4(
		&data.view_projection,
		V * P);

	data.light_direction =
	{
		lightX,
		lightY,
			lightZ,
		0
	};

	XMStoreFloat4(
		&data.camera_position,
		eye);
	// UNIT40: ImGuiで調整したIBL強度をPBRピクセルシェーダーへ渡す。
	data.ibl_intensity = iblIntensity;

	immediate_context->UpdateSubresource(
		constant_buffers[0],
		0,
		0,
		&data,
		0,
		0);

	immediate_context->VSSetConstantBuffers(
		1,
		1,
		&constant_buffers[0]);

	immediate_context->PSSetConstantBuffers(
		1,
		1,
		&constant_buffers[0]);

	// nicoの右手系・Y軸上向きを、描画で使う左手系・Y軸上向きへ変換する。
	// センチメートル単位のモデルなので、0.01倍してメートルへ合わせる。
	const XMFLOAT4X4 coordinate_system_transforms[]
	{
		{ -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, // 右手系・Y-up
		{  1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, // 左手系・Y-up
		{ -1, 0, 0, 0, 0, 0,-1, 0, 0, 1, 0, 0, 0, 0, 0, 1 }, // 右手系・Z-up
		{  1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1 }  // 左手系・Z-up
	};
	const float scale_factor = 0.01f;
	XMMATRIX C =
		XMLoadFloat4x4(&coordinate_system_transforms[0]) *
		XMMatrixScaling(scale_factor, scale_factor, scale_factor);

	// UIで指定した拡大縮小・回転・移動を座標系変換の後ろへ合成する。
	XMMATRIX S =
		XMMatrixScaling(
			cubeScale[0],
			cubeScale[1],
			cubeScale[2]);

	// スケルタルアニメーションの姿勢変化を確認しやすいよう、自動回転は無効にする。
	static bool autoRotate = false;
	static float autoAngle = 0.0f;

	if (autoRotate)
	{
		autoAngle += elapsed_time * 60.0f;
	}

	XMMATRIX R =
		XMMatrixRotationRollPitchYaw(
			XMConvertToRadians(cubeRot[0]) + XMConvertToRadians(autoAngle),
			XMConvertToRadians(cubeRot[1]) + XMConvertToRadians(autoAngle * 1.3f),
			XMConvertToRadians(cubeRot[2]));

	XMMATRIX T =
		XMMatrixTranslation(
			cubePos[0],
			cubePos[1],
			cubePos[2]);

	XMFLOAT4X4 world{};

	XMStoreFloat4x4(
		&world,
		C * S * R * T);

	// テクスチャ用サンプラーをピクセルシェーダーへ設定する。
	immediate_context->PSSetSamplers(0, 3, sampler_states);

	// 背景は面カリング・深度判定・深度書込みを使わず、オフスクリーン全体へ描く。
	if (showBackdrop)
	{
		immediate_context->RSSetState(rasterizer_states[2].Get());
		immediate_context->OMSetDepthStencilState(depth_stencil_states[3], 0);
		immediate_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
		sprite_batches[0]->begin(immediate_context);
		sprite_batches[0]->render(
			immediate_context,
			0.0f,
			0.0f,
			static_cast<float>(SCREEN_WIDTH),
			static_cast<float>(SCREEN_HEIGHT),
			1.0f, 1.0f, 1.0f, 1.0f,
			0.0f,
			0.0f, 0.0f, 1600.0f, 900.0f);
		sprite_batches[0]->end(immediate_context);
	}

	if (showSpriteShowcase)
	{
		// UNIT2～10: 同じスプライトシートから複数フレームを切り出し、回転と半透明を付けて一括描画する。
		ID3D11BlendState* sprite_blend_state =
			spriteBlendMode >= 0 && spriteBlendMode < 4 ?
			blend_states[spriteBlendMode] : nullptr;
		immediate_context->OMSetBlendState(
			sprite_blend_state,
			nullptr,
			0xFFFFFFFF);
		sprite_batches[1]->begin(immediate_context);
		for (int frame = 0; frame < 3; ++frame)
		{
			sprite_batches[1]->render(
				immediate_context,
				25.0f + frame * 78.0f,
				SCREEN_HEIGHT - 170.0f,
				84.0f,
				144.0f,
				1.0f,
				1.0f - frame * 0.12f,
				1.0f,
				0.88f,
				-8.0f + frame * 8.0f,
				frame * 140.0f,
				0.0f,
				140.0f,
				240.0f);
		}
		sprite_batches[1]->end(immediate_context);
		immediate_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	}

	// キャラクターでは背面カリングと深度判定・書込みを元に戻す。
	immediate_context->RSSetState(rasterizer_states[0].Get());
	immediate_context->OMSetDepthStencilState(depth_stencil_states[0], 0);
	if (showViewerGrid)
	{
		for (int line = -10; line <= 10; ++line)
		{
			const bool axis = line == 0;
			XMFLOAT4X4 grid_world{};
			XMStoreFloat4x4(
				&grid_world,
				XMMatrixScaling(20.0f, 0.006f, axis ? 0.025f : 0.012f) *
				XMMatrixTranslation(0.0f, -0.003f, static_cast<float>(line)));
			geometric_primitives[0]->render(
				immediate_context,
				grid_world,
				axis ? XMFLOAT4{ 0.85f, 0.18f, 0.18f, 1.0f } :
					XMFLOAT4{ 0.25f, 0.28f, 0.32f, 1.0f });

			XMStoreFloat4x4(
				&grid_world,
				XMMatrixScaling(axis ? 0.025f : 0.012f, 0.006f, 20.0f) *
				XMMatrixTranslation(static_cast<float>(line), -0.003f, 0.0f));
			geometric_primitives[0]->render(
				immediate_context,
				grid_world,
				axis ? XMFLOAT4{ 0.18f, 0.45f, 0.90f, 1.0f } :
					XMFLOAT4{ 0.25f, 0.28f, 0.32f, 1.0f });
		}
	}

	if (showcaseAutoRotate)
	{
		showcaseRotationDegrees = std::fmod(
			showcaseRotationDegrees + elapsed_time * 18.0f,
			360.0f);
	}

	if (showStaticMeshShowcase)
	{
		// UNIT13～16: OBJ・MTL・複数テクスチャ・法線マップを使う岩を、左側の展示物として描画する。
		const XMMATRIX static_mesh_world_matrix =
			XMMatrixScaling(0.38f, 0.38f, 0.38f) *
			XMMatrixRotationY(XMConvertToRadians(showcaseRotationDegrees)) *
			XMMatrixTranslation(-2.25f, 0.15f, 0.8f);
		XMFLOAT4X4 static_mesh_world{};
		XMStoreFloat4x4(&static_mesh_world, static_mesh_world_matrix);
		static_meshes[0]->render(
			immediate_context,
			static_mesh_world,
			{ 1.0f, 1.0f, 1.0f, 1.0f });
	}

	if (showPrimitiveShowcase)
	{
		// UNIT11～12: 正立方体・円柱・球・カプセルを色分けし、生成結果を一列で比較する。
		const XMFLOAT4 primitive_colors[4]
		{
			{ 0.95f, 0.35f, 0.25f, 1.0f },
			{ 0.25f, 0.70f, 0.95f, 1.0f },
			{ 0.45f, 0.90f, 0.40f, 1.0f },
			{ 0.95f, 0.75f, 0.25f, 1.0f }
		};
		immediate_context->RSSetState(
			showPrimitiveWireframe ?
			rasterizer_states[1].Get() : rasterizer_states[0].Get());
		for (size_t primitive_index = 0; primitive_index < 4; ++primitive_index)
		{
			const XMMATRIX primitive_world_matrix =
				XMMatrixScaling(0.34f, 0.34f, 0.34f) *
				XMMatrixRotationY(XMConvertToRadians(
					showcaseRotationDegrees + primitive_index * 25.0f)) *
				XMMatrixTranslation(
					-2.55f + primitive_index * 0.62f,
					1.35f,
					0.8f);
			XMFLOAT4X4 primitive_world{};
			XMStoreFloat4x4(&primitive_world, primitive_world_matrix);
			geometric_primitives[primitive_index]->render(
				immediate_context,
				primitive_world,
				primitive_colors[primitive_index]);
		}
		immediate_context->RSSetState(rasterizer_states[0].Get());
	}

	// 経過時間から現在のキーフレームを選び、最後まで再生したら先頭へ戻す。
	static float animation_tick = 0.0f;
	const animation::keyframe* current_keyframe = nullptr;
	if (!skinned_meshes[0]->animation_clips.empty())
	{
		const animation& clip = skinned_meshes[0]->animation_clips.front();
		if (!clip.sequence.empty() && clip.sampling_rate > 0.0f)
		{
			const float animation_duration =
				static_cast<float>(clip.sequence.size()) / clip.sampling_rate;
			animation_tick += elapsed_time;
			if (animation_duration > 0.0f)
			{
				animation_tick = std::fmod(animation_tick, animation_duration);
			}
			const size_t frame_index = (std::min)(
				static_cast<size_t>(animation_tick * clip.sampling_rate),
				clip.sequence.size() - 1);
			current_keyframe = &clip.sequence.at(frame_index);
		}
	}

	// FBXメッシュへワールド行列・UI指定色・現在の姿勢を渡して描画する。
	if (showSkinnedMeshShowcase)
	{
		// UNIT17～31: FBXの階層・材質・スキニング・アニメーション・DDS読込をまとめて表示する。
		skinned_meshes[0]->render(
			immediate_context,
			world,
			{
				cubeColor[0],
				cubeColor[1],
				cubeColor[2],
				cubeColor[3]
			},
			current_keyframe);
	}

	// glTFの右手系をX反転で左手系へ変換し、モデルごとの表示倍率と配置を合成する。
	// 面の向きが異なるデータも形状確認できるよう、glTF描画中はカリングを無効にする。
	const float safe_gltf_scale = (std::max)(gltfScale, 0.0001f);
	const XMMATRIX gltf_world_matrix =
		XMMatrixScaling(-safe_gltf_scale, safe_gltf_scale, safe_gltf_scale) *
		XMMatrixRotationRollPitchYaw(
			XMConvertToRadians(gltfRotation[0]),
			XMConvertToRadians(gltfRotation[1]),
			XMConvertToRadians(gltfRotation[2])) *
		XMMatrixTranslation(
			gltfPosition[0],
			gltfPosition[1],
			gltfPosition[2]);
	XMFLOAT4X4 gltf_world{};
	XMStoreFloat4x4(&gltf_world, gltf_world_matrix);
	immediate_context->RSSetState(rasterizer_states[2].Get());
	// UNIT39: glTFのPBRシェーダーが使う環境リソースを、予約したt32～t35へまとめて設定する。
	ID3D11ShaderResourceView* ibl_resource_views[4]
	{
		shader_resource_views[0].Get(),
		shader_resource_views[1].Get(),
		shader_resource_views[2].Get(),
		shader_resource_views[3].Get()
	};
	immediate_context->PSSetShaderResources(
		32,
		_countof(ibl_resource_views),
		ibl_resource_views);
	if (showGltfShowcase && !gltf_models[0]->animations.empty())
	{
		// 再生時間をクリップ長でループさせ、毎フレームのノード姿勢を計算して描画する。
		gltfAnimationIndex = (std::clamp)(
			gltfAnimationIndex,
			0,
			static_cast<int>(gltf_models[0]->animations.size()) - 1);
		const gltf_model::animation& animation =
			gltf_models[0]->animations.at(static_cast<size_t>(gltfAnimationIndex));
		if (gltfAnimationPlaying)
		{
			gltfAnimationTime += elapsed_time * gltfAnimationSpeed;
		}
		if (animation.duration > 0.0f && gltfAnimationTime >= animation.duration)
		{
			if (gltfAnimationLoop)
			{
				gltfAnimationTime = std::fmod(gltfAnimationTime, animation.duration);
			}
			else
			{
				gltfAnimationTime = animation.duration;
				gltfAnimationPlaying = false;
			}
		}
		gltf_models[0]->animate(
			static_cast<size_t>(gltfAnimationIndex),
			gltfAnimationTime,
			animatedGltfNodes);
		gltf_models[0]->render(immediate_context, gltf_world, animatedGltfNodes);
	}
	else if (showGltfShowcase)
	{
		gltf_models[0]->render(immediate_context, gltf_world);
	}
	if (showGltfShowcase && showModelBounds && gltf_models[0]->has_bounds)
	{
		const XMFLOAT3& bmin = gltf_models[0]->bounding_box_min;
		const XMFLOAT3& bmax = gltf_models[0]->bounding_box_max;
		const XMMATRIX bounds_matrix =
			XMMatrixScaling(
				bmax.x - bmin.x,
				bmax.y - bmin.y,
				bmax.z - bmin.z) *
			XMMatrixTranslation(
				(bmin.x + bmax.x) * 0.5f,
				(bmin.y + bmax.y) * 0.5f,
				(bmin.z + bmax.z) * 0.5f) *
			gltf_world_matrix;
		XMFLOAT4X4 bounds_world{};
		XMStoreFloat4x4(&bounds_world, bounds_matrix);
		immediate_context->RSSetState(rasterizer_states[1].Get());
		geometric_primitives[0]->render(
			immediate_context,
			bounds_world,
			{ 1.0f, 0.75f, 0.10f, 1.0f });
		immediate_context->RSSetState(rasterizer_states[2].Get());
	}
	// UNIT38: glTF専用サンプラーを使い終えたので、後段のブルーム処理用サンプラーへ戻す。
	immediate_context->PSSetSamplers(0, 3, sampler_states);

	// デバッグ用cubeのワイヤーフレームは非表示にする。必要になったら#if 1へ戻す。
#if 0
	auto bmin =
		static_meshes[0]->get_bounding_box_min();

	auto bmax =
		static_meshes[0]->get_bounding_box_max();

	float sx = bmax.x - bmin.x;
	float sy = bmax.y - bmin.y;
	float sz = bmax.z - bmin.z;

	float cx = (bmax.x + bmin.x) * 0.5f;
	float cy = (bmax.y + bmin.y) * 0.5f;
	float cz = (bmax.z + bmin.z) * 0.5f;

	XMMATRIX bboxS =
		XMMatrixScaling(sx, sy, sz);

	XMMATRIX bboxT =
		XMMatrixTranslation(
			cx,
			cy,
			cz);

	XMFLOAT4X4 bboxWorld;

	XMStoreFloat4x4(
		&bboxWorld,
		bboxS* S* R* T);

	immediate_context->RSSetState(
		rasterizer_states[1].Get());

	geometric_primitives[0]->render(
		immediate_context,
		bboxWorld,
		{ 1,0,0,1 });
#endif

	// HDRシーンの描画を終え、描画先を画面のバックバッファへ戻す。
	framebuffers[0]->deactivate(immediate_context);

	// ImGuiで調整したポストエフェクト値を、輝度抽出PSとブルームPSのb0へ渡す。
	const post_effect_constants post_effect_data
	{
		luminanceThreshold,
		gaussianSigma,
		bloomIntensity,
		toneMappingExposure
	};
	immediate_context->UpdateSubresource(
		constant_buffers[1],
		0,
		nullptr,
		&post_effect_data,
		0,
		0);
	immediate_context->PSSetConstantBuffers(0, 1, &constant_buffers[1]);

	// HDRシーンから高輝度成分だけを抽出し、半解像度へダウンサンプリングする。
	framebuffers[1]->clear(immediate_context);
	framebuffers[1]->activate(immediate_context);
	immediate_context->RSSetState(rasterizer_states[2].Get());
	immediate_context->OMSetDepthStencilState(depth_stencil_states[3], 0);
	ID3D11ShaderResourceView* scene_shader_resource_view =
		framebuffers[0]->shader_resource_views[0].Get();
	bit_block_transfer->blit(
		immediate_context,
		&scene_shader_resource_view,
		0,
		1,
		pixel_shaders[0].Get());
	framebuffers[1]->deactivate(immediate_context);

	// 元のHDRシーンと、ぼかした高輝度画像を合成して画面へ出力する。
	// blur_ps内でトーンマッピングとガンマ補正も行い、SDR表示へ変換する。
	ID3D11ShaderResourceView* post_process_views[2]
	{
		framebuffers[0]->shader_resource_views[0].Get(),
		framebuffers[1]->shader_resource_views[0].Get()
	};
	bit_block_transfer->blit(
		immediate_context,
		post_process_views,
		0,
		2,
		pixel_shaders[1].Get());

#ifdef USE_IMGUI
	ImGui::Render();

	ImGui_ImplDX11_RenderDrawData(
		ImGui::GetDrawData());
#endif

	swap_chain->Present(0, 0);
}

bool framework::uninitialize()
{
	if (immediate_context)
	{
		immediate_context->ClearState();
		immediate_context->Flush();
	}

	// Deviceより先に、Deviceが所有するUNIT32と既存の描画リソースを解放する。
	bit_block_transfer.reset();
	for (auto& framebuffer_object : framebuffers) framebuffer_object.reset();
	for (auto& sprite_batch_object : sprite_batches) sprite_batch_object.reset();
	for (auto& skinned_mesh_object : skinned_meshes) skinned_mesh_object.reset();
	for (auto& gltf_model_object : gltf_models) gltf_model_object.reset();
	for (auto& static_mesh_object : static_meshes) static_mesh_object.reset();
	for (auto& primitive_object : geometric_primitives) primitive_object.reset();
	for (auto& pixel_shader : pixel_shaders) pixel_shader.Reset();
	for (auto& rasterizer_state : rasterizer_states) rasterizer_state.Reset();
	release_all_textures();

	for (ID3D11Buffer*& constant_buffer : constant_buffers)
	{
		if (constant_buffer)
		{
			constant_buffer->Release();
			constant_buffer = nullptr;
		}
	}
	for (ID3D11DepthStencilState*& depth_stencil_state : depth_stencil_states)
	{
		if (depth_stencil_state)
		{
			depth_stencil_state->Release();
			depth_stencil_state = nullptr;
		}
	}
	for (ID3D11BlendState*& blend_state : blend_states)
	{
		if (blend_state)
		{
			blend_state->Release();
			blend_state = nullptr;
		}
	}

	if (render_target_view)
		render_target_view->Release();

	if (depth_stencil_view)
		depth_stencil_view->Release();

	if (swap_chain)
		swap_chain->Release();

	if (immediate_context)
		immediate_context->Release();

	// 自前で作成したサンプラーを解放する。
	for (ID3D11SamplerState*& sampler_state : sampler_states)
	{
		if (sampler_state)
		{
			sampler_state->Release();
			sampler_state = nullptr;
		}
	}

	if (device)
		device->Release();

	return true;
}

framework::~framework()
{
}
