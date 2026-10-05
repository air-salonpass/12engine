#include "framework.h"
#include "shader.h"
#include "texture.h"
#include <DirectXMath.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <commdlg.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include "tinygltf-release/json.hpp"
#include "Systems/AnimationSystem.h"
#include "Systems/PostProcessSystem.h"

namespace
{
	// SSR用床の上面をワールドY=0へ統一し、読み込んだモデルの接地計算にも共有する。
	constexpr float SSR_FLOOR_SURFACE_Y = 0.0f;

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

void framework::toggle_fullscreen()
{
	// UI描画中にも呼ばれるため、この場でGPUバッファを作り直さず次のupdateへ遅延する。
	fullscreenToggleRequested = true;
}

void framework::apply_fullscreen_toggle()
{
	if (!hwnd)
	{
		return;
	}

	if (!borderlessFullscreen)
	{
		windowedStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
		windowedPlacement.length = sizeof(WINDOWPLACEMENT);
		GetWindowPlacement(hwnd, &windowedPlacement);

		MONITORINFO monitor_info{ sizeof(MONITORINFO) };
		if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor_info))
		{
			return;
		}

		// DXGI排他モードを避け、デスクトップ解像度のまま安定して表示できるボーダーレス方式にする。
		SetWindowLongPtrW(hwnd, GWL_STYLE, windowedStyle & ~WS_OVERLAPPEDWINDOW);
		SetWindowPos(hwnd, HWND_TOP,
			monitor_info.rcMonitor.left,
			monitor_info.rcMonitor.top,
			monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
			monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		borderlessFullscreen = true;
	}
	else
	{
		SetWindowLongPtrW(hwnd, GWL_STYLE, windowedStyle);
		SetWindowPlacement(hwnd, &windowedPlacement);
		SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		borderlessFullscreen = false;
	}
}

bool framework::resize_render_targets(UINT width, UINT height)
{
	if (!swap_chain || !device || !immediate_context || width == 0 || height == 0)
	{
		return false;
	}
	if (width == renderWidth && height == renderHeight && render_target_view && depth_stencil_view)
	{
		return true;
	}

	// ResizeBuffers前に、旧BackBufferとオフスクリーンTextureへの全参照をGPUパイプラインから外す。
	ID3D11RenderTargetView* null_render_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
	immediate_context->OMSetRenderTargets(_countof(null_render_targets), null_render_targets, nullptr);
	ID3D11ShaderResourceView* null_resources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
	immediate_context->VSSetShaderResources(0, _countof(null_resources), null_resources);
	immediate_context->PSSetShaderResources(0, _countof(null_resources), null_resources);
	for (auto& framebuffer_object : framebuffers) framebuffer_object.reset();

	if (render_target_view)
	{
		render_target_view->Release();
		render_target_view = nullptr;
	}
	if (depth_stencil_view)
	{
		depth_stencil_view->Release();
		depth_stencil_view = nullptr;
	}

	HRESULT hr = swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
	if (FAILED(hr)) return false;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
	hr = swap_chain->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf()));
	if (SUCCEEDED(hr))
	{
		hr = device->CreateRenderTargetView(back_buffer.Get(), nullptr, &render_target_view);
	}
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
	if (FAILED(hr)) return false;

	D3D11_TEXTURE2D_DESC depth_desc{};
	depth_desc.Width = width;
	depth_desc.Height = height;
	depth_desc.MipLevels = 1;
	depth_desc.ArraySize = 1;
	depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depth_desc.SampleDesc.Count = 1;
	depth_desc.Usage = D3D11_USAGE_DEFAULT;
	depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_buffer;
	hr = device->CreateTexture2D(&depth_desc, nullptr, depth_buffer.GetAddressOf());
	if (SUCCEEDED(hr))
	{
		hr = device->CreateDepthStencilView(depth_buffer.Get(), nullptr, &depth_stencil_view);
	}
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
	if (FAILED(hr)) return false;

	// HDR、Bloom、Post ProcessもBackBufferと同じ実解像度へ揃え、UIとGizmoの座標ずれをなくす。
	framebuffers[0] = std::make_unique<framebuffer>(device, width, height);
	framebuffers[1] = std::make_unique<framebuffer>(device,
		(std::max)(width / 2, 1u), (std::max)(height / 2, 1u));
	framebuffers[2] = std::make_unique<framebuffer>(device, width, height);
	renderWidth = width;
	renderHeight = height;

	D3D11_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(width);
	viewport.Height = static_cast<float>(height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	immediate_context->RSSetViewports(1, &viewport);
	immediate_context->OMSetRenderTargets(1, &render_target_view, depth_stencil_view);
	return true;
}

#ifdef USE_IMGUI
void framework::setup_imgui_style()
{
	// Unity系エディタを意識した、低コントラストの背景と青いアクセントのダークテーマ。
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowPadding = ImVec2(12.0f, 10.0f);
	style.FramePadding = ImVec2(8.0f, 5.0f);
	style.ItemSpacing = ImVec2(8.0f, 6.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	style.ScrollbarSize = 13.0f;
	style.WindowRounding = 7.0f;
	style.ChildRounding = 5.0f;
	style.FrameRounding = 4.0f;
	style.PopupRounding = 5.0f;
	style.ScrollbarRounding = 8.0f;
	style.GrabRounding = 4.0f;
	style.TabRounding = 4.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;

	ImVec4* colors = style.Colors;
	colors[ImGuiCol_Text] = ImVec4(0.90f, 0.92f, 0.95f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.52f, 0.58f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.070f, 0.078f, 0.090f, 0.97f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.085f, 0.095f, 0.110f, 0.98f);
	colors[ImGuiCol_Border] = ImVec4(0.20f, 0.23f, 0.28f, 0.85f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.14f, 0.17f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.23f, 0.30f, 1.00f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.32f, 0.50f, 1.00f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.055f, 0.062f, 0.075f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.075f, 0.16f, 0.25f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.12f, 0.28f, 0.44f, 1.00f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.16f, 0.40f, 0.64f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.12f, 0.33f, 0.56f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.11f, 0.25f, 0.39f, 1.00f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.15f, 0.36f, 0.57f, 1.00f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.12f, 0.30f, 0.50f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.30f, 0.67f, 1.00f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.23f, 0.55f, 0.86f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.35f, 0.72f, 1.00f, 1.00f);
	colors[ImGuiCol_Separator] = ImVec4(0.18f, 0.21f, 0.26f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.20f, 0.48f, 0.76f, 0.25f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.25f, 0.58f, 0.90f, 0.70f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.30f, 0.66f, 1.00f, 1.00f);
}
#endif

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

	for (std::size_t slot = 0; slot < _countof(gltf_models); ++slot)
	{
		gltf_models[slot].reset();
		modelAssetPaths[slot].clear();
	}
	gltf_models[0] = std::move(candidate);
	modelAssetPaths[0] = filename;
	loadedModelPath = filename;
	gltfAnimationIndex = 0;
	gltfAnimationTime = 0.0f;
	gltfAnimationPlaying = !gltf_models[0]->animations.empty();
	reset_model_scene_entity();
	place_gltf_on_floor();
	sync_editor_to_selected_entity();
	focus_gltf_model();
	sync_editor_to_scene_components();
	sceneDirty = false;
	sceneDirtySuppressFrames = 2;
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
	if (modelLoading.load(std::memory_order_acquire) || filename.empty())
	{
		return;
	}
	const int loaded_slot = find_loaded_model_slot(filename);
	if (loaded_slot >= 0)
	{
		instantiate_model_asset(filename, static_cast<std::size_t>(loaded_slot));
		return;
	}
	const int free_slot = find_free_model_slot();
	if (free_slot < 0)
	{
		modelLoadError = "The Scene supports up to 8 unique model assets.";
		return;
	}
	modelLoadError.clear();
	pendingModelPath = filename;
	pendingModelSlot = static_cast<std::size_t>(free_slot);
	modelLoadCompleted.store(false, std::memory_order_release);
	modelLoading.store(true, std::memory_order_release);
	Microsoft::WRL::ComPtr<ID3D11Device> loading_device(device);
	const bool submitted = modelLoadWorker.submit(
		[this, loading_device, filename]()
	{
		std::unique_ptr<gltf_model> result;
		std::string error;
		try
		{
			const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			result = std::make_unique<gltf_model>(loading_device.Get(), filename);
			if (SUCCEEDED(com_result)) CoUninitialize();
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
		}
		catch (...)
		{
			error = "The model loading thread failed with an unknown error.";
		}
		{
			std::lock_guard<std::mutex> lock(modelLoadResultMutex);
			pendingLoadedGltfModel = std::move(result);
			pendingModelLoadError = std::move(error);
		}
		// releaseで公開し、メインスレッドが完成データより先に完了フラグを観測しないようにする。
		modelLoadCompleted.store(true, std::memory_order_release);
	});
	if (!submitted)
	{
		modelLoading.store(false, std::memory_order_release);
		modelLoadError = "The model loading worker is busy.";
	}
}

void framework::poll_gltf_load()
{
	if (!modelLoading.load(std::memory_order_acquire) ||
		!modelLoadCompleted.load(std::memory_order_acquire))
	{
		return;
	}

	std::unique_ptr<gltf_model> candidate;
	{
		std::lock_guard<std::mutex> lock(modelLoadResultMutex);
		candidate = std::move(pendingLoadedGltfModel);
		modelLoadError = std::move(pendingModelLoadError);
	}
	if (candidate)
	{
		if (candidate->loaded)
		{
			gltf_models[pendingModelSlot] = std::move(candidate);
			modelAssetPaths[pendingModelSlot] = pendingModelPath;
			loadedModelPath = pendingModelPath;
			instantiate_model_asset(pendingModelPath, pendingModelSlot);
		}
		else if (modelLoadError.empty())
		{
			modelLoadError = candidate && !candidate->load_error.empty() ?
				candidate->load_error : "The selected model could not be loaded.";
		}
	}
	modelLoadCompleted.store(false, std::memory_order_release);
	modelLoading.store(false, std::memory_order_release);
	pendingModelPath.clear();
}

int framework::find_loaded_model_slot(const std::string& filename) const
{
	for (std::size_t slot = 0; slot < _countof(modelAssetPaths); ++slot)
	{
		if (!modelAssetPaths[slot].empty() &&
			_stricmp(modelAssetPaths[slot].c_str(), filename.c_str()) == 0)
		{
			return static_cast<int>(slot);
		}
	}
	return -1;
}

int framework::find_free_model_slot() const
{
	for (std::size_t slot = 0; slot < _countof(gltf_models); ++slot)
		if (!gltf_models[slot]) return static_cast<int>(slot);
	return -1;
}

scene_entity framework::instantiate_model_asset(
	const std::string& filename, std::size_t model_slot)
{
	if (model_slot >= _countof(gltf_models) || !gltf_models[model_slot] ||
		!gltf_models[model_slot]->loaded)
	{
		return null_scene_entity;
	}

	std::string base_name = std::filesystem::path(filename).filename().string();
	if (base_name.empty()) base_name = "Model";
	std::string entity_name = base_name;
	int suffix = 2;
	const auto name_exists = [this](const std::string& value)
	{
		for (const scene_entity entity : sceneRegistry.entities())
			if (const name_component* name = sceneRegistry.name(entity))
				if (name->value == value) return true;
		return false;
	};
	while (name_exists(entity_name))
		entity_name = base_name + " (" + std::to_string(suffix++) + ")";

	const scene_entity entity = sceneRegistry.create_model_entity(
		entity_name, model_slot, !gltf_models[model_slot]->animations.empty());
	model_renderer_component* renderer = sceneRegistry.renderer(entity);
	transform_component* transform = sceneRegistry.transform(entity);
	if (renderer) renderer->asset_path = filename;
	if (transform && gltf_models[model_slot]->has_bounds)
	{
		// 新規配置時はモデル最下点を反射床へ合わせ、埋まり込みを避ける。
		transform->position[1] = SSR_FLOOR_SURFACE_Y + 0.02f -
			gltf_models[model_slot]->bounding_box_min.y * transform->scale;
	}
	selectedSceneEntity = entity;
	loadedModelPath = filename;
	sync_selected_entity_to_editor();
	sceneDirty = true;
	return entity;
}

void framework::create_new_scene()
{
	if (modelLoading) return;
	for (std::size_t slot = 0; slot < _countof(gltf_models); ++slot)
	{
		gltf_models[slot].reset();
		modelAssetPaths[slot].clear();
	}
	sceneRegistry.clear();
	create_scene_service_entities();
	selectedSceneEntity = cameraSceneEntity;
	loadedModelPath.clear();
	modelLoadError.clear();
	currentScenePath.clear();
	sceneDirty = false;
	sceneDirtySuppressFrames = 2;
}

bool framework::save_scene(const std::string& filename)
{
	using nlohmann::json;
	if (filename.empty()) return false;
	json root;
	root["format"] = "JisakuEngineScene";
	root["version"] = 1;
	root["entities"] = json::array();
	const auto float_array = [](const float* values, std::size_t count)
	{
		json result = json::array();
		for (std::size_t index = 0; index < count; ++index) result.push_back(values[index]);
		return result;
	};

	for (const scene_entity entity : sceneRegistry.entities())
	{
		json value;
		if (const name_component* name = sceneRegistry.name(entity)) value["name"] = name->value;
		if (const transform_component* component = sceneRegistry.transform(entity))
		{
			value["transform"] = {
				{ "position", float_array(component->position, 3) },
				{ "rotation", float_array(component->rotation, 3) },
				{ "scale", component->scale }
			};
		}
		if (const model_renderer_component* component = sceneRegistry.renderer(entity))
		{
			value["model_renderer"] = {
				{ "asset", component->asset_path }, { "visible", component->visible }
			};
		}
		if (const animation_component* component = sceneRegistry.animation(entity))
		{
			value["animation"] = {
				{ "clip", component->clip }, { "time", component->time },
				{ "playing", component->playing }, { "loop", component->loop },
				{ "speed", component->speed }
			};
		}
		if (const camera_component* component = sceneRegistry.camera(entity))
		{
			value["camera"] = {
				{ "target", float_array(component->target, 3) },
				{ "distance", component->distance }, { "fov", component->fov_degrees },
				{ "wheel_zoom", component->wheel_zoom_speed },
				{ "yaw", component->yaw_degrees }, { "pitch", component->pitch_degrees },
				{ "orbit_sensitivity", component->orbit_sensitivity },
				{ "active", component->active }
			};
		}
		if (const directional_light_component* component = sceneRegistry.directional_light(entity))
		{
			value["directional_light"] = {
				{ "direction", float_array(component->direction, 3) },
				{ "enabled", component->enabled }
			};
		}
		if (const reflection_floor_component* component = sceneRegistry.reflection_floor(entity))
		{
			value["reflection_floor"] = {
				{ "enabled", component->enabled },
				{ "reflection_enabled", component->reflection_enabled },
				{ "strength", component->strength }, { "max_distance", component->max_distance },
				{ "thickness", component->thickness }, { "stride", component->stride }
			};
		}
		if (const post_process_component* component = sceneRegistry.post_process(entity))
		{
			value["post_process"] = {
				{ "luminance_threshold", component->luminance_threshold },
				{ "gaussian_sigma", component->gaussian_sigma },
				{ "bloom_intensity", component->bloom_intensity }, { "exposure", component->exposure },
				{ "flow_enabled", component->flow_enabled }, { "dof_enabled", component->dof_enabled },
				{ "vignette_enabled", component->vignette_enabled },
				{ "chromatic_enabled", component->chromatic_enabled },
				{ "radial_blur_enabled", component->radial_blur_enabled },
				{ "fxaa_enabled", component->fxaa_enabled },
				{ "flow_strength", component->flow_strength }, { "flow_speed", component->flow_speed },
				{ "chromatic_strength", component->chromatic_strength },
				{ "vignette_strength", component->vignette_strength },
				{ "vignette_smoothness", component->vignette_smoothness },
				{ "radial_strength", component->radial_strength },
				{ "radial_center", float_array(component->radial_center, 2) },
				{ "radial_radius", component->radial_radius },
				{ "dof_focus_distance", component->dof_focus_distance },
				{ "dof_range", component->dof_range }, { "dof_blur_size", component->dof_blur_size },
				{ "fxaa_strength", component->fxaa_strength },
				{ "demo_background", component->demo_background_enabled },
				{ "split_comparison", component->split_comparison_enabled }
			};
		}
		if (const rendering_settings_component* component = sceneRegistry.rendering_settings(entity))
		{
			value["rendering_settings"] = {
				{ "vsync", component->vsync }, { "ground_grid", component->ground_grid },
				{ "model_bounds", component->model_bounds }, { "backdrop", component->backdrop },
				{ "ibl_intensity", component->ibl_intensity }
			};
		}
		root["entities"].push_back(std::move(value));
	}

	try
	{
		std::ofstream output(std::filesystem::path(filename), std::ios::binary | std::ios::trunc);
		if (!output) return false;
		output << root.dump(2);
		currentScenePath = filename;
		sceneDirty = false;
		sceneDirtySuppressFrames = 2;
		return true;
	}
	catch (const std::exception& exception)
	{
		modelLoadError = exception.what();
		return false;
	}
}

bool framework::load_scene(const std::string& filename)
{
	using nlohmann::json;
	if (filename.empty() || modelLoading) return false;
	try
	{
		std::ifstream input(std::filesystem::path(filename), std::ios::binary);
		if (!input) return false;
		json root = json::parse(input);
		if (root.value("format", "") != "JisakuEngineScene" || !root.contains("entities"))
			throw std::runtime_error("Unsupported Scene file.");

		std::unique_ptr<gltf_model> loaded_assets[8];
		std::string loaded_paths[8];
		std::unordered_map<std::string, std::size_t> asset_slots;
		for (const json& value : root["entities"])
		{
			if (!value.contains("model_renderer")) continue;
			const std::string asset = value["model_renderer"].value("asset", "");
			if (asset.empty() || asset_slots.find(asset) != asset_slots.end()) continue;
			if (asset_slots.size() >= _countof(loaded_assets))
				throw std::runtime_error("A Scene can use up to 8 unique model assets.");
			const std::size_t slot = asset_slots.size();
			auto candidate = std::make_unique<gltf_model>(device, asset);
			if (!candidate->loaded)
				throw std::runtime_error("Could not load Scene asset: " + asset);
			asset_slots.emplace(asset, slot);
			loaded_paths[slot] = asset;
			loaded_assets[slot] = std::move(candidate);
		}

		for (std::size_t slot = 0; slot < _countof(gltf_models); ++slot)
		{
			gltf_models[slot] = std::move(loaded_assets[slot]);
			modelAssetPaths[slot] = loaded_paths[slot];
		}
		sceneRegistry.clear();
		cameraSceneEntity = lightSceneEntity = reflectionFloorSceneEntity =
			postProcessSceneEntity = renderingSettingsSceneEntity = null_scene_entity;
		scene_entity first_model = null_scene_entity;
		const auto read_array = [](const json& source, float* target, std::size_t count)
		{
			if (!source.is_array()) return;
			for (std::size_t index = 0; index < count && index < source.size(); ++index)
				target[index] = source[index].get<float>();
		};

		for (const json& value : root["entities"])
		{
			const scene_entity entity = sceneRegistry.create_entity(value.value("name", "Entity"));
			if (value.contains("transform"))
			{
				transform_component& component = sceneRegistry.add_transform(entity);
				const json& data = value["transform"];
				read_array(data.value("position", json::array()), component.position, 3);
				read_array(data.value("rotation", json::array()), component.rotation, 3);
				component.scale = data.value("scale", 1.0f);
			}
			if (value.contains("model_renderer"))
			{
				const json& data = value["model_renderer"];
				const std::string asset = data.value("asset", "");
				model_renderer_component& component = sceneRegistry.add_model_renderer(entity);
				component.asset_path = asset;
				component.visible = data.value("visible", true);
				component.model_slot = asset_slots.at(asset);
				if (first_model == null_scene_entity) first_model = entity;
			}
			if (value.contains("animation"))
			{
				animation_component& component = sceneRegistry.add_animation(entity);
				const json& data = value["animation"];
				component.clip = data.value("clip", 0);
				component.time = data.value("time", 0.0f);
				component.playing = data.value("playing", true);
				component.loop = data.value("loop", true);
				component.speed = data.value("speed", 1.0f);
			}
			if (value.contains("camera"))
			{
				camera_component& component = sceneRegistry.add_camera(entity);
				const json& data = value["camera"];
				read_array(data.value("target", json::array()), component.target, 3);
				component.distance = data.value("distance", 6.5f);
				component.fov_degrees = data.value("fov", 40.0f);
				component.wheel_zoom_speed = data.value("wheel_zoom", 0.5f);
				component.yaw_degrees = data.value("yaw", 0.0f);
				component.pitch_degrees = data.value("pitch", 0.0f);
				component.orbit_sensitivity = data.value("orbit_sensitivity", 0.2f);
				component.active = data.value("active", true);
				cameraSceneEntity = entity;
			}
			if (value.contains("directional_light"))
			{
				directional_light_component& component = sceneRegistry.add_directional_light(entity);
				const json& data = value["directional_light"];
				read_array(data.value("direction", json::array()), component.direction, 3);
				component.enabled = data.value("enabled", true);
				lightSceneEntity = entity;
			}
			if (value.contains("reflection_floor"))
			{
				reflection_floor_component& component = sceneRegistry.add_reflection_floor(entity);
				const json& data = value["reflection_floor"];
				component.enabled = data.value("enabled", true);
				component.reflection_enabled = data.value("reflection_enabled", true);
				component.strength = data.value("strength", 1.1f);
				component.max_distance = data.value("max_distance", 15.0f);
				component.thickness = data.value("thickness", 0.5f);
				component.stride = data.value("stride", 0.25f);
				reflectionFloorSceneEntity = entity;
			}
			if (value.contains("post_process"))
			{
				post_process_component& component = sceneRegistry.add_post_process(entity);
				const json& data = value["post_process"];
				component.luminance_threshold = data.value("luminance_threshold", 0.8f);
				component.gaussian_sigma = data.value("gaussian_sigma", 1.0f);
				component.bloom_intensity = data.value("bloom_intensity", 1.0f);
				component.exposure = data.value("exposure", 1.2f);
				component.flow_enabled = data.value("flow_enabled", false);
				component.dof_enabled = data.value("dof_enabled", false);
				component.vignette_enabled = data.value("vignette_enabled", false);
				component.chromatic_enabled = data.value("chromatic_enabled", false);
				component.radial_blur_enabled = data.value("radial_blur_enabled", false);
				component.fxaa_enabled = data.value("fxaa_enabled", false);
				component.flow_strength = data.value("flow_strength", 0.35f);
				component.flow_speed = data.value("flow_speed", 1.0f);
				component.chromatic_strength = data.value("chromatic_strength", 0.025f);
				component.vignette_strength = data.value("vignette_strength", 1.15f);
				component.vignette_smoothness = data.value("vignette_smoothness", 0.45f);
				component.radial_strength = data.value("radial_strength", 0.05f);
				read_array(data.value("radial_center", json::array()), component.radial_center, 2);
				component.radial_radius = data.value("radial_radius", 0.15f);
				component.dof_focus_distance = data.value("dof_focus_distance", 0.065f);
				component.dof_range = data.value("dof_range", 0.04f);
				component.dof_blur_size = data.value("dof_blur_size", 3.0f);
				component.fxaa_strength = data.value("fxaa_strength", 0.65f);
				component.demo_background_enabled = data.value("demo_background", false);
				component.split_comparison_enabled = data.value("split_comparison", false);
				postProcessSceneEntity = entity;
			}
			if (value.contains("rendering_settings"))
			{
				rendering_settings_component& component = sceneRegistry.add_rendering_settings(entity);
				const json& data = value["rendering_settings"];
				component.vsync = data.value("vsync", true);
				component.ground_grid = data.value("ground_grid", false);
				component.model_bounds = data.value("model_bounds", false);
				component.backdrop = data.value("backdrop", false);
				component.ibl_intensity = data.value("ibl_intensity", 0.35f);
				renderingSettingsSceneEntity = entity;
			}
		}

		// 古いSceneファイルで共通Entityが欠けていても、必要なSystemを自動補完する。
		if (cameraSceneEntity == null_scene_entity)
		{
			cameraSceneEntity = sceneRegistry.create_entity("Main Camera");
			sceneRegistry.add_camera(cameraSceneEntity);
		}
		if (lightSceneEntity == null_scene_entity)
		{
			lightSceneEntity = sceneRegistry.create_entity("Directional Light");
			sceneRegistry.add_directional_light(lightSceneEntity);
		}
		if (reflectionFloorSceneEntity == null_scene_entity)
		{
			reflectionFloorSceneEntity = sceneRegistry.create_entity("Reflection Floor");
			sceneRegistry.add_reflection_floor(reflectionFloorSceneEntity);
		}
		if (postProcessSceneEntity == null_scene_entity)
		{
			postProcessSceneEntity = sceneRegistry.create_entity("Post Process Volume");
			sceneRegistry.add_post_process(postProcessSceneEntity);
		}
		if (renderingSettingsSceneEntity == null_scene_entity)
		{
			renderingSettingsSceneEntity = sceneRegistry.create_entity("Rendering Settings");
			sceneRegistry.add_rendering_settings(renderingSettingsSceneEntity);
		}

		selectedSceneEntity = first_model != null_scene_entity ? first_model : cameraSceneEntity;
		sync_scene_components_to_editor();
		sync_selected_entity_to_editor();
		currentScenePath = filename;
		sceneDirty = false;
		sceneDirtySuppressFrames = 2;
		modelLoadError.clear();
		return true;
	}
	catch (const std::exception& exception)
	{
		modelLoadError = exception.what();
		return false;
	}
}

bool framework::browse_save_scene(bool save_as)
{
	if (!save_as && !currentScenePath.empty()) return save_scene(currentScenePath);
	wchar_t filename[MAX_PATH]{};
	OPENFILENAMEW dialog{};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = hwnd;
	dialog.lpstrFilter = L"Jisaku Scene (*.jscene)\0*.jscene\0All files (*.*)\0*.*\0";
	dialog.lpstrFile = filename;
	dialog.nMaxFile = _countof(filename);
	dialog.lpstrDefExt = L"jscene";
	dialog.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
	return GetSaveFileNameW(&dialog) && save_scene(to_native_path(filename));
}

bool framework::browse_open_scene()
{
	if (modelLoading) return false;
	if (sceneDirty && MessageBoxW(hwnd,
		L"未保存の変更を破棄してSceneを開きますか？", L"Open Scene",
		MB_YESNO | MB_ICONQUESTION) != IDYES)
	{
		return false;
	}
	wchar_t filename[MAX_PATH]{};
	OPENFILENAMEW dialog{};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = hwnd;
	dialog.lpstrFilter = L"Jisaku Scene (*.jscene)\0*.jscene\0All files (*.*)\0*.*\0";
	dialog.lpstrFile = filename;
	dialog.nMaxFile = _countof(filename);
	dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	return GetOpenFileNameW(&dialog) && load_scene(to_native_path(filename));
}

void framework::focus_gltf_model()
{
	model_renderer_component* renderer = sceneRegistry.renderer(selectedSceneEntity);
	if (!renderer || renderer->model_slot >= _countof(gltf_models) ||
		!gltf_models[renderer->model_slot] || !gltf_models[renderer->model_slot]->has_bounds)
	{
		return;
	}
	gltf_model* selected_model = gltf_models[renderer->model_slot].get();
	const DirectX::XMFLOAT3& bmin = selected_model->bounding_box_min;
	const DirectX::XMFLOAT3& bmax = selected_model->bounding_box_max;
	const float scale = (std::max)(std::abs(gltfScale), 0.0001f);
	cameraTarget[0] = gltfPosition[0] - (bmin.x + bmax.x) * 0.5f * scale;
	const float model_min_y = gltfPosition[1] + bmin.y * scale;
	const float model_max_y = gltfPosition[1] + bmax.y * scale;
	// 反射床があるときは床を中心に本体と鏡像の両方が入るようフレーミングする。
	cameraTarget[1] = shaderSsrFloorEnabled ? SSR_FLOOR_SURFACE_Y :
		(model_min_y + model_max_y) * 0.5f;
	cameraTarget[2] = gltfPosition[2] + (bmin.z + bmax.z) * 0.5f * scale;
	const float sx = (bmax.x - bmin.x) * scale;
	const float sy = shaderSsrFloorEnabled ?
		2.0f * (std::max)(std::abs(model_min_y - SSR_FLOOR_SURFACE_Y),
			std::abs(model_max_y - SSR_FLOOR_SURFACE_Y)) :
		(bmax.y - bmin.y) * scale;
	const float sz = (bmax.z - bmin.z) * scale;
	const float radius = 0.5f * std::sqrt(sx * sx + sy * sy + sz * sz);
	const float framing_margin = shaderSsrFloorEnabled ? 3.5f : 2.5f;
	cameraDistance = (std::clamp)(radius * framing_margin, 0.5f, 100.0f);
}

void framework::place_gltf_on_floor()
{
	model_renderer_component* renderer = sceneRegistry.renderer(selectedSceneEntity);
	if (!renderer || renderer->model_slot >= _countof(gltf_models) ||
		!gltf_models[renderer->model_slot] || !gltf_models[renderer->model_slot]->has_bounds)
	{
		return;
	}
	// モデルの最下点を床より少し上へ合わせ、アニメーション中の足や形状が床へ埋まりにくくする。
	const float scale = (std::max)(std::abs(gltfScale), 0.0001f);
	gltfPosition[1] = SSR_FLOOR_SURFACE_Y + 0.02f -
		gltf_models[renderer->model_slot]->bounding_box_min.y * scale;
}

void framework::reset_model_scene_entity()
{
	sceneRegistry.clear();
	create_scene_service_entities();
	if (!gltf_models[0] || !gltf_models[0]->loaded)
	{
		selectedSceneEntity = cameraSceneEntity;
		return;
	}
	selectedSceneEntity = instantiate_model_asset(modelAssetPaths[0], 0);
}

void framework::create_scene_service_entities()
{
	// Scene共通機能もEntityとして生成し、以降は各Systemが対応コンポーネントを参照する。
	cameraSceneEntity = sceneRegistry.create_entity("Main Camera");
	sceneRegistry.add_camera(cameraSceneEntity);
	lightSceneEntity = sceneRegistry.create_entity("Directional Light");
	sceneRegistry.add_directional_light(lightSceneEntity);
	reflectionFloorSceneEntity = sceneRegistry.create_entity("Reflection Floor");
	sceneRegistry.add_reflection_floor(reflectionFloorSceneEntity);
	postProcessSceneEntity = sceneRegistry.create_entity("Post Process Volume");
	sceneRegistry.add_post_process(postProcessSceneEntity);
	renderingSettingsSceneEntity = sceneRegistry.create_entity("Rendering Settings");
	sceneRegistry.add_rendering_settings(renderingSettingsSceneEntity);
	sync_editor_to_scene_components();
}

void framework::select_scene_entity(scene_entity entity)
{
	if (!sceneRegistry.valid(entity) || entity == selectedSceneEntity) return;
	// 選択を変える直前にInspectorの値を現在Entityへ戻し、編集内容を失わないようにする。
	sync_editor_to_selected_entity();
	selectedSceneEntity = entity;
	sync_selected_entity_to_editor();
}

void framework::duplicate_selected_entity()
{
	// 現在は配置可能なModel Entityだけを複製対象にする。Scene設定Entityは一意に保つ。
	if (!sceneRegistry.valid(selectedSceneEntity) ||
		!sceneRegistry.renderer(selectedSceneEntity)) return;
	sync_editor_to_selected_entity();
	const scene_entity duplicate = sceneRegistry.duplicate(selectedSceneEntity);
	if (duplicate == null_scene_entity) return;
	selectedSceneEntity = duplicate;
	sync_selected_entity_to_editor();
	// Unityと同じく同じ位置へ複製し、直後にMove Gizmoで好きな場所へ配置できるようにする。
	gizmoOperation = 0;
	sceneDirty = true;
}

void framework::sync_selected_entity_to_editor()
{
	if (!sceneRegistry.valid(selectedSceneEntity)) return;
	if (const transform_component* transform = sceneRegistry.transform(selectedSceneEntity))
	{
		std::copy(std::begin(transform->position), std::end(transform->position), gltfPosition);
		std::copy(std::begin(transform->rotation), std::end(transform->rotation), gltfRotation);
		gltfScale = transform->scale;
	}
	if (const model_renderer_component* renderer = sceneRegistry.renderer(selectedSceneEntity))
	{
		showGltfShowcase = renderer->visible;
		loadedModelPath = renderer->asset_path;
	}
	if (const animation_component* animation = sceneRegistry.animation(selectedSceneEntity))
	{
		gltfAnimationIndex = animation->clip;
		gltfAnimationTime = animation->time;
		gltfAnimationPlaying = animation->playing;
		gltfAnimationLoop = animation->loop;
		gltfAnimationSpeed = animation->speed;
	}
}

void framework::sync_editor_to_selected_entity()
{
	if (!sceneRegistry.valid(selectedSceneEntity)) return;
	if (transform_component* transform = sceneRegistry.transform(selectedSceneEntity))
	{
		if (!std::equal(std::begin(gltfPosition), std::end(gltfPosition), transform->position) ||
			!std::equal(std::begin(gltfRotation), std::end(gltfRotation), transform->rotation) ||
			transform->scale != gltfScale) sceneDirty = true;
		std::copy(std::begin(gltfPosition), std::end(gltfPosition), transform->position);
		std::copy(std::begin(gltfRotation), std::end(gltfRotation), transform->rotation);
		transform->scale = gltfScale;
	}
	if (model_renderer_component* renderer = sceneRegistry.renderer(selectedSceneEntity))
	{
		if (renderer->visible != showGltfShowcase) sceneDirty = true;
		renderer->visible = showGltfShowcase;
	}
	if (animation_component* animation = sceneRegistry.animation(selectedSceneEntity))
	{
		if (animation->clip != gltfAnimationIndex || animation->time != gltfAnimationTime ||
			animation->playing != gltfAnimationPlaying || animation->loop != gltfAnimationLoop ||
			animation->speed != gltfAnimationSpeed) sceneDirty = true;
		animation->clip = gltfAnimationIndex;
		animation->time = gltfAnimationTime;
		animation->playing = gltfAnimationPlaying;
		animation->loop = gltfAnimationLoop;
		animation->speed = gltfAnimationSpeed;
	}
}

void framework::sync_scene_components_to_editor()
{
	if (camera_component* camera = sceneRegistry.camera(cameraSceneEntity))
	{
		std::copy_n(camera->target, 3, cameraTarget);
		cameraDistance = camera->distance;
		cameraFovDegrees = camera->fov_degrees;
		cameraWheelZoomSpeed = camera->wheel_zoom_speed;
		cameraYawDegrees = camera->yaw_degrees;
		cameraPitchDegrees = camera->pitch_degrees;
		cameraOrbitSensitivity = camera->orbit_sensitivity;
	}
	if (directional_light_component* light = sceneRegistry.directional_light(lightSceneEntity))
	{
		lightX = light->direction[0];
		lightY = light->direction[1];
		lightZ = light->direction[2];
	}
	if (reflection_floor_component* floor = sceneRegistry.reflection_floor(reflectionFloorSceneEntity))
	{
		shaderSsrFloorEnabled = floor->enabled;
		shaderSsrEnabled = floor->reflection_enabled;
		shaderSsrStrength = floor->strength;
		shaderSsrMaxDistance = floor->max_distance;
		shaderSsrThickness = floor->thickness;
		shaderSsrStride = floor->stride;
	}
	if (post_process_component* post = sceneRegistry.post_process(postProcessSceneEntity))
	{
		shaderEffectTime = post->effect_time;
		luminanceThreshold = post->luminance_threshold;
		gaussianSigma = post->gaussian_sigma;
		bloomIntensity = post->bloom_intensity;
		toneMappingExposure = post->exposure;
		shaderFlowEnabled = post->flow_enabled;
		shaderDofEnabled = post->dof_enabled;
		shaderVignetteEnabled = post->vignette_enabled;
		shaderChromaticEnabled = post->chromatic_enabled;
		shaderRadialBlurEnabled = post->radial_blur_enabled;
		shaderFxaaEnabled = post->fxaa_enabled;
		shaderDemoBackgroundEnabled = post->demo_background_enabled;
		shaderSplitComparisonEnabled = post->split_comparison_enabled;
		shaderFlowStrength = post->flow_strength;
		shaderFlowSpeed = post->flow_speed;
		shaderChromaticStrength = post->chromatic_strength;
		shaderVignetteStrength = post->vignette_strength;
		shaderVignetteSmoothness = post->vignette_smoothness;
		shaderRadialStrength = post->radial_strength;
		std::copy_n(post->radial_center, 2, shaderRadialCenter);
		shaderRadialRadius = post->radial_radius;
		shaderDofFocusDistance = post->dof_focus_distance;
		shaderDofRange = post->dof_range;
		shaderDofBlurSize = post->dof_blur_size;
		shaderFxaaStrength = post->fxaa_strength;
	}
	if (rendering_settings_component* settings =
		sceneRegistry.rendering_settings(renderingSettingsSceneEntity))
	{
		vsyncEnabled = settings->vsync;
		showViewerGrid = settings->ground_grid;
		showModelBounds = settings->model_bounds;
		showBackdrop = settings->backdrop;
		iblIntensity = settings->ibl_intensity;
	}
}

void framework::sync_editor_to_scene_components()
{
	if (camera_component* camera = sceneRegistry.camera(cameraSceneEntity))
	{
		camera_component previous = *camera;
		std::copy_n(cameraTarget, 3, camera->target);
		camera->distance = cameraDistance;
		camera->fov_degrees = cameraFovDegrees;
		camera->wheel_zoom_speed = cameraWheelZoomSpeed;
		camera->yaw_degrees = cameraYawDegrees;
		camera->pitch_degrees = cameraPitchDegrees;
		camera->orbit_sensitivity = cameraOrbitSensitivity;
		if (std::memcmp(&previous, camera, sizeof(camera_component)) != 0) sceneDirty = true;
	}
	if (directional_light_component* light = sceneRegistry.directional_light(lightSceneEntity))
	{
		directional_light_component previous = *light;
		light->direction[0] = lightX;
		light->direction[1] = lightY;
		light->direction[2] = lightZ;
		if (std::memcmp(&previous, light, sizeof(directional_light_component)) != 0) sceneDirty = true;
	}
	if (reflection_floor_component* floor = sceneRegistry.reflection_floor(reflectionFloorSceneEntity))
	{
		reflection_floor_component previous = *floor;
		floor->enabled = shaderSsrFloorEnabled;
		floor->reflection_enabled = shaderSsrEnabled;
		floor->strength = shaderSsrStrength;
		floor->max_distance = shaderSsrMaxDistance;
		floor->thickness = shaderSsrThickness;
		floor->stride = shaderSsrStride;
		if (std::memcmp(&previous, floor, sizeof(reflection_floor_component)) != 0) sceneDirty = true;
	}
	if (post_process_component* post = sceneRegistry.post_process(postProcessSceneEntity))
	{
		post_process_component previous = *post;
		post->effect_time = shaderEffectTime;
		post->luminance_threshold = luminanceThreshold;
		post->gaussian_sigma = gaussianSigma;
		post->bloom_intensity = bloomIntensity;
		post->exposure = toneMappingExposure;
		post->flow_enabled = shaderFlowEnabled;
		post->dof_enabled = shaderDofEnabled;
		post->vignette_enabled = shaderVignetteEnabled;
		post->chromatic_enabled = shaderChromaticEnabled;
		post->radial_blur_enabled = shaderRadialBlurEnabled;
		post->fxaa_enabled = shaderFxaaEnabled;
		post->demo_background_enabled = shaderDemoBackgroundEnabled;
		post->split_comparison_enabled = shaderSplitComparisonEnabled;
		post->flow_strength = shaderFlowStrength;
		post->flow_speed = shaderFlowSpeed;
		post->chromatic_strength = shaderChromaticStrength;
		post->vignette_strength = shaderVignetteStrength;
		post->vignette_smoothness = shaderVignetteSmoothness;
		post->radial_strength = shaderRadialStrength;
		std::copy_n(shaderRadialCenter, 2, post->radial_center);
		post->radial_radius = shaderRadialRadius;
		post->dof_focus_distance = shaderDofFocusDistance;
		post->dof_range = shaderDofRange;
		post->dof_blur_size = shaderDofBlurSize;
		post->fxaa_strength = shaderFxaaStrength;
		if (std::memcmp(&previous, post, sizeof(post_process_component)) != 0) sceneDirty = true;
	}
	if (rendering_settings_component* settings =
		sceneRegistry.rendering_settings(renderingSettingsSceneEntity))
	{
		rendering_settings_component previous = *settings;
		settings->vsync = vsyncEnabled;
		settings->ground_grid = showViewerGrid;
		settings->model_bounds = showModelBounds;
		settings->backdrop = showBackdrop;
		settings->ibl_intensity = iblIntensity;
		if (std::memcmp(&previous, settings, sizeof(rendering_settings_component)) != 0) sceneDirty = true;
	}
}

bool framework::initialize()
{
	// 参照プロジェクトと同様に、Systemの登録順をフレーム更新順として明示する。
	sceneSystemManager.clear();
	sceneSystemManager.add_system<post_process_system>();
	sceneSystemManager.add_system<animation_system>();

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
	if (SUCCEEDED(hr))
	{
		Microsoft::WRL::ComPtr<IDXGIFactory> dxgi_factory;
		if (SUCCEEDED(swap_chain->GetParent(IID_PPV_ARGS(dxgi_factory.GetAddressOf()))))
		{
			// DXGI標準のAlt+Enterを無効化し、F11と同じ安全な切替え処理へ一本化する。
			dxgi_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
		}
	}

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

	// 床を描くときにステンシルへ1を書き込み、後段の平面反射を床の範囲だけに制限する。
	D3D11_DEPTH_STENCIL_DESC floor_stencil_desc{};
	floor_stencil_desc.DepthEnable = TRUE;
	floor_stencil_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	floor_stencil_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	floor_stencil_desc.StencilEnable = TRUE;
	floor_stencil_desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
	floor_stencil_desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
	floor_stencil_desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
	floor_stencil_desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
	floor_stencil_desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	floor_stencil_desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
	floor_stencil_desc.BackFace = floor_stencil_desc.FrontFace;
	hr = device->CreateDepthStencilState(&floor_stencil_desc, &depth_stencil_states[4]);
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	// 鏡像は深度を書き換えず、床が記録したステンシル値の場所だけを通す。
	D3D11_DEPTH_STENCIL_DESC reflection_stencil_desc = floor_stencil_desc;
	reflection_stencil_desc.DepthEnable = FALSE;
	reflection_stencil_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	reflection_stencil_desc.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
	reflection_stencil_desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	reflection_stencil_desc.BackFace = reflection_stencil_desc.FrontFace;
	hr = device->CreateDepthStencilState(&reflection_stencil_desc, &depth_stencil_states[5]);
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

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

	// シェーダーII・IIIの共通エフェクト値を、最終全画面PSのb1へ渡す。
	buffer_desc.ByteWidth = sizeof(shader_showcase_constants);
	hr = device->CreateBuffer(
		&buffer_desc,
		nullptr,
		&constant_buffers[2]);
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

	// MITライセンスのFluent UI System IconsをProjectブラウザ用テクスチャとして読み込む。
	D3D11_TEXTURE2D_DESC editor_icon_desc{};
	load_texture_from_file(
		device,
		L".\\resources\\editor\\icons\\folder_filled.png",
		editor_icon_views[0].ReleaseAndGetAddressOf(),
		&editor_icon_desc);
	load_texture_from_file(
		device,
		L".\\resources\\editor\\icons\\cube_regular.png",
		editor_icon_views[1].ReleaseAndGetAddressOf(),
		&editor_icon_desc);

	// UNIT30では大きなnico.fbxを使い、初回FBX解析と2回目のCereal読込を比較する。
	skinned_meshes[0] =
		std::make_unique<skinned_mesh>(
			device,
			".\\resources\\nico.fbx",
			false,
			30.0f); // アニメーションは1秒あたり30キーフレームで取り込む。

	// アニメーションとSSRだけを確認できるよう、歩行アニメーションを持つモデルを初期表示する。
	load_gltf(".\\resources\\gltf\\CesiumMan\\CesiumMan.gltf");
	// モデル全体と床への映り込みが同時に見える、少し高い位置からの見下ろしへする。
	cameraDistance *= 1.15f;
	cameraYawDegrees = -12.0f;
	cameraPitchDegrees = 8.0f;
	sync_editor_to_scene_components();

	// 1280x720のHDRシーンと、ダウンサンプリングした640x360の高輝度画像を作る。
	framebuffers[0] = std::make_unique<framebuffer>(device, SCREEN_WIDTH, SCREEN_HEIGHT);
	framebuffers[1] = std::make_unique<framebuffer>(device, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
	// ブルーム合成後のSDR画像へ教材エフェクトを重ねるため、最終処理用の中間バッファを用意する。
	framebuffers[2] = std::make_unique<framebuffer>(device, SCREEN_WIDTH, SCREEN_HEIGHT);
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
	create_ps_from_cso(
		device,
		"shader_showcase_ps.cso",
		pixel_shaders[2].GetAddressOf());

	return true;
}

void framework::update(float elapsed_time)
{
	if (fullscreenToggleRequested)
	{
		fullscreenToggleRequested = false;
		apply_fullscreen_toggle();
	}
	poll_gltf_load();
	// Componentデータを処理する更新はSystemManager経由で一括実行する。
	scene_system_context system_context{};
	system_context.models = gltf_models;
	system_context.model_count = _countof(gltf_models);
	sceneSystemManager.update(sceneRegistry, system_context, elapsed_time);
	// Animation Systemが進めた時刻と再生状態をEditorキャッシュへ先に反映する。
	// これを行わないと、render()内のInspector同期が前フレームの時刻をComponentへ書き戻してしまう。
	sync_selected_entity_to_editor();

	// Rendererへ渡す一時キャッシュは、System更新後のComponentから取得する。
	if (post_process_component* post = sceneRegistry.post_process(postProcessSceneEntity))
	{
		shaderEffectTime = post->effect_time;
	}
}

void framework::render(float elapsed_time)
{
	using namespace DirectX;
	// ECSをScene状態の正本とし、既存レンダリング経路へ渡すフレームキャッシュを更新する。
	sync_scene_components_to_editor();
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
	// ImGuizmoはImGuiの1フレーム開始後に初期化し、同じ描画リストへギズモを追加する。
	ImGuizmo::BeginFrame();

	// Unity風にSceneとGameを左側へ上下配置し、Hierarchy・Project・Inspectorを右側へ並べる。
	const ImVec2 display_size = ImGui::GetIO().DisplaySize;
	const float toolbar_height = 38.0f;
	const float inspector_width = (std::clamp)(display_size.x * 0.22f, 260.0f, 350.0f);
	const float hierarchy_width = (std::clamp)(display_size.x * 0.15f, 180.0f, 230.0f);
	const float project_width = (std::clamp)(display_size.x * 0.20f, 220.0f, 300.0f);
	const float view_width = (std::max)(
		display_size.x - hierarchy_width - project_width - inspector_width, 320.0f);
	const float editor_height = (std::max)(display_size.y - toolbar_height, 360.0f);
	const float scene_view_height = editor_height * 0.5f;
	const float game_view_height = editor_height - scene_view_height;
	const ImGuiWindowFlags fixed_panel_flags =
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
	// 1280x720の描画結果を縦横比を崩さず各ビューへ収め、余った領域はレターボックスにする。
	const auto fit_game_aspect = [this](float rect[4])
	{
		const float target_aspect = static_cast<float>(renderWidth) /
			static_cast<float>((std::max)(renderHeight, 1u));
		if (rect[2] <= 0.0f || rect[3] <= 0.0f) return;
		const float current_aspect = rect[2] / rect[3];
		if (current_aspect > target_aspect)
		{
			const float fitted_width = rect[3] * target_aspect;
			rect[0] += (rect[2] - fitted_width) * 0.5f;
			rect[2] = fitted_width;
		}
		else
		{
			const float fitted_height = rect[2] / target_aspect;
			rect[1] += (rect[3] - fitted_height) * 0.5f;
			rect[3] = fitted_height;
		}
	};

	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowSize(ImVec2(display_size.x, toolbar_height));
	ImGui::Begin("##EditorToolbar", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (ImGui::Button("New Scene"))
	{
		if (!sceneDirty || MessageBoxW(hwnd,
			L"未保存の変更を破棄して新しいSceneを作りますか？", L"New Scene",
			MB_YESNO | MB_ICONQUESTION) == IDYES)
		{
			create_new_scene();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Open Scene")) browse_open_scene();
	ImGui::SameLine();
	if (ImGui::Button(sceneDirty ? "Save Scene *" : "Save Scene")) browse_save_scene(false);
	ImGui::SameLine();
	if (ImGui::Button("Save As")) browse_save_scene(true);
	ImGui::SameLine();
	if (ImGui::Button("Open Model...")) browse_and_load_gltf();
	ImGui::SameLine();
	if (ImGui::Button("Focus")) focus_gltf_model();
	ImGui::SameLine();
	if (ImGui::Button("Duplicate")) duplicate_selected_entity();
	ImGui::SameLine();
	// Unityと同様に、現在選ばれている移動・回転・拡縮ツールをボタンの色で判別できるようにする。
	auto draw_gizmo_tool_button = [this](const char* label, int operation)
	{
		const bool selected = gizmoOperation == operation;
		if (selected)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		}
		if (ImGui::Button(label)) gizmoOperation = operation;
		if (selected) ImGui::PopStyleColor();
	};
	draw_gizmo_tool_button("Move", 0);
	ImGui::SameLine();
	draw_gizmo_tool_button("Rotate", 1);
	ImGui::SameLine();
	draw_gizmo_tool_button("Scale", 2);
	ImGui::SameLine();
	if (ImGui::Button(gizmoLocalSpace ? "Local" : "Global"))
	{
		gizmoLocalSpace = !gizmoLocalSpace;
	}
	ImGui::SameLine();
	if (ImGui::Button(gizmoEnabled ? "Gizmo: On" : "Gizmo: Off"))
	{
		gizmoEnabled = !gizmoEnabled;
	}
	ImGui::SameLine();
	if (ImGui::Button(gltfAnimationPlaying ? "Pause" : "Play"))
	{
		gltfAnimationPlaying = !gltfAnimationPlaying;
	}
	ImGui::SameLine();
	if (ImGui::Button("Rendering")) showAdvancedRenderingWindow = !showAdvancedRenderingWindow;
	if (!currentScenePath.empty())
	{
		ImGui::SameLine();
		ImGui::TextDisabled("%s%s",
			std::filesystem::path(currentScenePath).filename().string().c_str(),
			sceneDirty ? " *" : "");
	}
	ImGui::End();

	ImGui::SetNextWindowPos(ImVec2(view_width, toolbar_height));
	ImGui::SetNextWindowSize(ImVec2(hierarchy_width, editor_height));
	ImGui::Begin("Hierarchy", nullptr, fixed_panel_flags);
	ImGui::TextDisabled("Scene");
	ImGui::Separator();
	if (!sceneRegistry.entities().empty())
	{
		scene_entity entity_to_delete = null_scene_entity;
		for (const scene_entity entity : sceneRegistry.entities())
		{
			name_component* name = sceneRegistry.name(entity);
			model_renderer_component* renderer = sceneRegistry.renderer(entity);
			if (!name) continue;
			ImGui::PushID(static_cast<int>(entity));
			const std::string label = renderer && !renderer->visible ?
				name->value + " (Hidden)" : name->value;
			if (ImGui::Selectable(label.c_str(), entity == selectedSceneEntity))
			{
				select_scene_entity(entity);
			}
			// Hierarchyの右クリックメニューからもUnity風に複製・削除できるようにする。
			if (ImGui::BeginPopupContextItem("EntityContext"))
			{
				if (renderer && ImGui::MenuItem("Duplicate"))
				{
					select_scene_entity(entity);
					duplicate_selected_entity();
				}
				if (renderer && ImGui::MenuItem("Delete")) entity_to_delete = entity;
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
		if (entity_to_delete != null_scene_entity)
		{
			sceneRegistry.destroy(entity_to_delete);
			sceneDirty = true;
			if (selectedSceneEntity == entity_to_delete)
			{
				selectedSceneEntity = sceneRegistry.entities().empty() ?
					null_scene_entity : sceneRegistry.entities().front();
				sync_selected_entity_to_editor();
			}
		}
		model_renderer_component* hierarchy_renderer = sceneRegistry.renderer(selectedSceneEntity);
		gltf_model* hierarchy_model = hierarchy_renderer &&
			hierarchy_renderer->model_slot < _countof(gltf_models) ?
			gltf_models[hierarchy_renderer->model_slot].get() : nullptr;
		if (hierarchy_model && ImGui::TreeNode("Selected Model Nodes"))
		{
			for (const auto& node : hierarchy_model->nodes)
				ImGui::BulletText("%s", node.name.empty() ? "(unnamed)" : node.name.c_str());
			ImGui::TreePop();
		}
	}
	ImGui::End();

	// Sceneウィンドウは背景を透明にし、背面に描かれたゲーム画面をビューポートとして見せる。
	ImGui::SetNextWindowPos(ImVec2(0.0f, toolbar_height));
	ImGui::SetNextWindowSize(ImVec2(view_width, scene_view_height));
	ImGui::Begin("Scene", nullptr, fixed_panel_flags | ImGuiWindowFlags_NoBackground);
	// GizmoをInspectorなどへ誤描画しないよう、Scene固有の描画先を保存する。
	editorSceneDrawList = ImGui::GetWindowDrawList();
	const ImVec2 scene_min = ImGui::GetWindowPos();
	const ImVec2 scene_content_min = ImGui::GetWindowContentRegionMin();
	const ImVec2 scene_content_max = ImGui::GetWindowContentRegionMax();
	editorSceneRect[0] = scene_min.x + scene_content_min.x;
	editorSceneRect[1] = scene_min.y + scene_content_min.y;
	editorSceneRect[2] = scene_content_max.x - scene_content_min.x;
	editorSceneRect[3] = scene_content_max.y - scene_content_min.y;
	fit_game_aspect(editorSceneRect);
	// 透明ウィンドウのHover判定には頼らず、マウス座標がScene矩形内かを直接調べる。
	const ImVec2 editor_mouse_position = ImGui::GetIO().MousePos;
	editorSceneHovered =
		editor_mouse_position.x >= editorSceneRect[0] &&
		editor_mouse_position.y >= editorSceneRect[1] &&
		editor_mouse_position.x < editorSceneRect[0] + editorSceneRect[2] &&
		editor_mouse_position.y < editorSceneRect[1] + editorSceneRect[3];
	// Sceneを左クリックした後だけカメラ入力を有効にし、他パネルをクリックしたら解除する。
	if (ImGui::IsMouseClicked(0)) editorSceneFocused = editorSceneHovered;
	ImGui::GetWindowDrawList()->AddRect(
		ImVec2(editorSceneRect[0], editorSceneRect[1]),
		ImVec2(editorSceneRect[0] + editorSceneRect[2], editorSceneRect[1] + editorSceneRect[3]),
		editorSceneFocused ? IM_COL32(55, 145, 230, 255) : IM_COL32(60, 68, 78, 255),
		0.0f, 0, editorSceneFocused ? 2.0f : 1.0f);
	// レターボックスが生じるフルスクリーンでも、案内文字を青いScene表示枠の内側へ揃える。
	ImGui::SetCursorScreenPos(ImVec2(editorSceneRect[0] + 8.0f, editorSceneRect[1] + 8.0f));
	ImGui::TextDisabled(editorSceneFocused ? "Scene Active" : "Click Scene to control camera");
	ImGui::End();

	// Gameビューには最終描画だけを表示し、編集用Gizmoとカメラ入力はSceneビューへ限定する。
	ImGui::SetNextWindowPos(ImVec2(0.0f, toolbar_height + scene_view_height));
	ImGui::SetNextWindowSize(ImVec2(view_width, game_view_height));
	ImGui::Begin("Game", nullptr, fixed_panel_flags | ImGuiWindowFlags_NoBackground);
	const ImVec2 game_min = ImGui::GetWindowPos();
	const ImVec2 game_content_min = ImGui::GetWindowContentRegionMin();
	const ImVec2 game_content_max = ImGui::GetWindowContentRegionMax();
	editorGameRect[0] = game_min.x + game_content_min.x;
	editorGameRect[1] = game_min.y + game_content_min.y;
	editorGameRect[2] = game_content_max.x - game_content_min.x;
	editorGameRect[3] = game_content_max.y - game_content_min.y;
	fit_game_aspect(editorGameRect);
	ImGui::End();

	ImGui::SetNextWindowPos(ImVec2(view_width + hierarchy_width, toolbar_height));
	ImGui::SetNextWindowSize(ImVec2(project_width, editor_height));
	ImGui::Begin("Project / Assets", nullptr, fixed_panel_flags);
	std::filesystem::path asset_directory(assetBrowserDirectory);
	std::error_code asset_error;
	if (!std::filesystem::exists(asset_directory, asset_error))
	{
		asset_directory = std::filesystem::current_path(asset_error);
		assetBrowserDirectory = asset_directory.string();
	}
	if (ImGui::Button("Up") && asset_directory.has_parent_path())
	{
		assetBrowserDirectory = asset_directory.parent_path().string();
		assetNavigationTime = ImGui::GetTime();
	}
	ImGui::SameLine();
	ImGui::TextWrapped("%s", assetBrowserDirectory.c_str());
	ImGui::Separator();
	std::vector<std::filesystem::path> asset_entries;
	for (std::filesystem::directory_iterator iterator(asset_directory, asset_error), end;
		!asset_error && iterator != end; iterator.increment(asset_error))
	{
		const std::filesystem::path path = iterator->path();
		std::string extension = path.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
		if (iterator->is_directory(asset_error) || extension == ".gltf" || extension == ".glb")
		{
			asset_entries.push_back(path);
		}
	}
	std::sort(asset_entries.begin(), asset_entries.end(), [](const auto& left, const auto& right)
	{
		const bool left_directory = std::filesystem::is_directory(left);
		const bool right_directory = std::filesystem::is_directory(right);
		return left_directory != right_directory ? left_directory : left.filename() < right.filename();
	});
	std::string requested_model_path;
	std::string requested_directory;
	ImGui::BeginChild("AssetGrid", ImVec2(0.0f, 0.0f), false);
	const float tile_width = 120.0f;
	const int tiles_per_row = (std::max)(1, static_cast<int>(ImGui::GetContentRegionAvail().x / tile_width));
	for (size_t asset_index = 0; asset_index < asset_entries.size(); ++asset_index)
	{
		const auto& path = asset_entries[asset_index];
		const bool directory = std::filesystem::is_directory(path, asset_error);
		const std::string filename = path.filename().string();
		ImGui::PushID(static_cast<int>(asset_index));
		const ImVec2 tile_size(tile_width - 8.0f, 86.0f);
		const ImVec2 tile_min = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("AssetTile", tile_size);
		const bool tile_hovered = ImGui::IsItemHovered();
		const bool tile_selected = selectedAssetPath == path.string();
		ImDrawList* asset_draw_list = ImGui::GetWindowDrawList();
		const auto asset_point = [&](float x, float y)
		{
			return ImVec2(tile_min.x + x, tile_min.y + y);
		};
		const ImU32 tile_color = ImGui::GetColorU32(
			tile_selected ? ImGuiCol_Header :
			tile_hovered ? ImGuiCol_HeaderHovered : ImGuiCol_FrameBg);
		asset_draw_list->AddRectFilled(tile_min, asset_point(tile_size.x, tile_size.y), tile_color, 4.0f);

		// Fluent UI System Iconsを優先し、画像が読めなかった場合だけコード描画へ戻す。
		ID3D11ShaderResourceView* editor_icon = editor_icon_views[directory ? 0 : 1].Get();
		if (editor_icon)
		{
			asset_draw_list->AddImage(
				reinterpret_cast<ImTextureID>(editor_icon),
				asset_point(28, 10), asset_point(84, 66));
		}
		else if (directory)
		{
			const ImU32 folder_body = IM_COL32(244, 183, 52, 255);
			const ImU32 folder_tab = IM_COL32(255, 211, 92, 255);
			const ImU32 folder_outline = IM_COL32(178, 119, 24, 255);
			asset_draw_list->AddRectFilled(asset_point(24, 25), asset_point(88, 60), folder_body, 5.0f);
			asset_draw_list->AddRectFilled(asset_point(28, 17), asset_point(57, 32), folder_tab, 4.0f);
			asset_draw_list->AddRect(asset_point(24, 25), asset_point(88, 60), folder_outline, 5.0f);
		}
		else
		{
			const ImU32 model_fill = IM_COL32(62, 139, 214, 255);
			const ImU32 model_line = IM_COL32(173, 219, 255, 255);
			asset_draw_list->AddRectFilled(asset_point(35, 25), asset_point(73, 59), model_fill, 3.0f);
			asset_draw_list->AddRect(asset_point(35, 25), asset_point(73, 59), model_line, 3.0f);
			asset_draw_list->AddLine(asset_point(35, 25), asset_point(48, 16), model_line, 2.0f);
			asset_draw_list->AddLine(asset_point(73, 25), asset_point(86, 16), model_line, 2.0f);
			asset_draw_list->AddLine(asset_point(48, 16), asset_point(86, 16), model_line, 2.0f);
			asset_draw_list->AddLine(asset_point(86, 16), asset_point(86, 50), model_line, 2.0f);
			asset_draw_list->AddLine(asset_point(73, 59), asset_point(86, 50), model_line, 2.0f);
		}
		std::string display_name = filename;
		while (display_name.size() > 4 &&
			ImGui::CalcTextSize((display_name + "...").c_str()).x > tile_size.x - 8.0f)
		{
			display_name.erase(display_name.end() - 1);
		}
		if (display_name != filename) display_name += "...";
		const ImVec2 text_size = ImGui::CalcTextSize(display_name.c_str());
		asset_draw_list->AddText(
			asset_point((tile_size.x - text_size.x) * 0.5f, 66.0f),
			ImGui::GetColorU32(ImGuiCol_Text), display_name.c_str());
		if (ImGui::IsItemClicked()) selectedAssetPath = path.string();
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
		{
			if (directory) requested_directory = path.string();
			else if (ImGui::GetTime() - assetNavigationTime > 0.40) requested_model_path = path.string();
		}
		ImGui::PopID();
		if ((asset_index + 1) % static_cast<size_t>(tiles_per_row) != 0) ImGui::SameLine();
	}
	ImGui::EndChild();
	if (!requested_directory.empty())
	{
		assetBrowserDirectory = requested_directory;
		assetNavigationTime = ImGui::GetTime();
	}
	if (!requested_model_path.empty()) queue_gltf_load(requested_model_path);
	ImGui::End();

	// 初期配置だけ指定し、利用者がUnityのパネルのように自由に移動・リサイズできるようにする。
	ImGui::SetNextWindowPos(ImVec2(view_width + hierarchy_width + project_width, toolbar_height));
	ImGui::SetNextWindowSize(ImVec2(inspector_width, display_size.y - toolbar_height));
	ImGui::Begin("Inspector", nullptr, fixed_panel_flags);
	model_renderer_component* selected_renderer = sceneRegistry.renderer(selectedSceneEntity);
	const bool selected_is_model = selected_renderer != nullptr;
	gltf_model* selected_gltf_model = selected_renderer &&
		selected_renderer->model_slot < _countof(gltf_models) ?
		gltf_models[selected_renderer->model_slot].get() : nullptr;
	if (sceneRegistry.valid(selectedSceneEntity))
	{
		if (const name_component* selected_name = sceneRegistry.name(selectedSceneEntity))
		{
			ImGui::Text("Entity %u", selectedSceneEntity);
			ImGui::TextWrapped("%s", selected_name->value.c_str());
		}
		if (selected_is_model && ImGui::Button("Duplicate Selected (Ctrl+D)"))
			duplicate_selected_entity();
		ImGui::Separator();
	}
	if (sceneRegistry.camera(selectedSceneEntity))
	{
		if (ImGui::CollapsingHeader("Camera Component", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::DragFloat3("Target", cameraTarget, 0.01f);
			ImGui::DragFloat("Distance", &cameraDistance, 0.05f, 0.5f, 100.0f);
			ImGui::SliderFloat("Field of View", &cameraFovDegrees, 20.0f, 90.0f, "%.1f deg");
			ImGui::DragFloat("Yaw", &cameraYawDegrees, 0.2f);
			ImGui::DragFloat("Pitch", &cameraPitchDegrees, 0.2f, -89.0f, 89.0f);
			ImGui::DragFloat("Orbit Sensitivity", &cameraOrbitSensitivity, 0.01f, 0.01f, 2.0f);
			ImGui::DragFloat("Wheel Zoom", &cameraWheelZoomSpeed, 0.05f, 0.05f, 10.0f);
		}
	}
	if (sceneRegistry.directional_light(selectedSceneEntity))
	{
		if (ImGui::CollapsingHeader("Directional Light Component", ImGuiTreeNodeFlags_DefaultOpen))
			ImGui::DragFloat3("Direction", &lightX, 0.01f);
	}
	if (sceneRegistry.reflection_floor(selectedSceneEntity))
	{
		if (ImGui::CollapsingHeader("Reflection Floor Component", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Enabled", &shaderSsrFloorEnabled);
			ImGui::Checkbox("Reflection", &shaderSsrEnabled);
			ImGui::SliderFloat("Strength", &shaderSsrStrength, 0.0f, 1.5f, "%.2f");
			ImGui::SliderFloat("Ray Distance", &shaderSsrMaxDistance, 1.0f, 50.0f, "%.1f");
			ImGui::SliderFloat("Thickness", &shaderSsrThickness, 0.02f, 1.5f, "%.2f");
			ImGui::SliderFloat("Ray Step", &shaderSsrStride, 0.05f, 1.0f, "%.2f");
		}
	}
	if (sceneRegistry.post_process(selectedSceneEntity))
	{
		if (ImGui::CollapsingHeader("Post Process Component", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::SliderFloat("Luminance", &luminanceThreshold, 0.0f, 2.0f);
			ImGui::SliderFloat("Gaussian Sigma", &gaussianSigma, 0.1f, 5.0f);
			ImGui::SliderFloat("Bloom", &bloomIntensity, 0.0f, 5.0f);
			ImGui::SliderFloat("Exposure", &toneMappingExposure, 0.1f, 5.0f);
			ImGui::Checkbox("Flow", &shaderFlowEnabled);
			ImGui::Checkbox("Depth of Field", &shaderDofEnabled);
			ImGui::Checkbox("Vignette", &shaderVignetteEnabled);
			ImGui::Checkbox("Chromatic Aberration", &shaderChromaticEnabled);
			ImGui::Checkbox("Radial Blur", &shaderRadialBlurEnabled);
			ImGui::Checkbox("FXAA", &shaderFxaaEnabled);
		}
	}
	if (sceneRegistry.rendering_settings(selectedSceneEntity))
	{
		if (ImGui::CollapsingHeader("Rendering Settings Component", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("VSync", &vsyncEnabled);
			ImGui::Checkbox("Ground Grid", &showViewerGrid);
			ImGui::Checkbox("Model Bounds", &showModelBounds);
			ImGui::Checkbox("Backdrop", &showBackdrop);
			ImGui::SliderFloat("IBL Intensity", &iblIntensity, 0.0f, 2.0f, "%.2f");
		}
	}
	if (ImGui::Button("Open glTF / GLB..."))
	{
		browse_and_load_gltf();
	}
	if (selected_is_model) ImGui::SameLine();
	if (selected_is_model && ImGui::Button("Focus (F)"))
	{
		focus_gltf_model();
	}
	if (selected_is_model)
	{
		ImGui::SameLine();
		ImGui::Checkbox("Visible", &showGltfShowcase);
		ImGui::Checkbox("Enable Gizmo", &gizmoEnabled);
	}
	if (ImGui::Button("Fullscreen (F11 / Alt+Enter)"))
	{
		toggle_fullscreen();
	}
	if (!loadedModelPath.empty())
	{
		ImGui::TextWrapped("%s", loadedModelPath.c_str());
	}
	if (!modelLoadError.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "%s", modelLoadError.c_str());
	}
	if (modelLoading)
	{
		// 大きなモデルの読込み中も描画ループを止めず、進行中であることを表示する。
		ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "Loading model...");
	}

	ImGui::Separator();
	if (selected_is_model && selected_gltf_model && selected_gltf_model->loaded)
	{
		ImGui::Text(
			"Nodes %d  Meshes %d  Materials %d  Animations %d",
			static_cast<int>(selected_gltf_model->nodes.size()),
			static_cast<int>(selected_gltf_model->meshes.size()),
			static_cast<int>(selected_gltf_model->materials.size()),
			static_cast<int>(selected_gltf_model->animations.size()));

		if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (!selected_gltf_model->animations.empty())
			{
				gltfAnimationIndex = (std::clamp)(
					gltfAnimationIndex, 0,
					static_cast<int>(selected_gltf_model->animations.size()) - 1);
				const gltf_model::animation& selected_animation =
					selected_gltf_model->animations[static_cast<size_t>(gltfAnimationIndex)];
				const std::string selected_name = selected_animation.name.empty() ?
					("Animation " + std::to_string(gltfAnimationIndex)) : selected_animation.name;
				if (ImGui::BeginCombo("Clip", selected_name.c_str()))
				{
					for (size_t index = 0; index < selected_gltf_model->animations.size(); ++index)
					{
						const gltf_model::animation& animation = selected_gltf_model->animations[index];
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
				const float duration = selected_gltf_model->animations[
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
			// Unityに近いW/E/R操作で、シーン上のギズモ種別を素早く切り替えられるようにする。
			if (ImGui::RadioButton("Move (W)", gizmoOperation == 0)) gizmoOperation = 0;
			ImGui::SameLine();
			if (ImGui::RadioButton("Rotate (E)", gizmoOperation == 1)) gizmoOperation = 1;
			ImGui::SameLine();
			if (ImGui::RadioButton("Scale (R)", gizmoOperation == 2)) gizmoOperation = 2;
			ImGui::Checkbox("Local Space", &gizmoLocalSpace);
			ImGui::DragFloat3("Model Position", gltfPosition, 0.01f);
			ImGui::DragFloat3("Model Rotation", gltfRotation, 1.0f);
			ImGui::DragFloat("Model Scale", &gltfScale, 0.01f, 0.0001f, 1000.0f, "%.4f");
			if (ImGui::Button("Place on Reflection Floor"))
			{
				place_gltf_on_floor();
				focus_gltf_model();
			}
		}

		if (false && ImGui::CollapsingHeader("Floor Reflection", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// 今回の確認対象をアニメーションとSSRへ絞り、教材デモ用の図形・背景操作は表示しない。
			ImGui::Checkbox("Enable Reflection", &shaderSsrEnabled);
			ImGui::SameLine();
			ImGui::Checkbox("Reflection Floor", &shaderSsrFloorEnabled);
			if (ImGui::Button("Place Model on Floor"))
			{
				place_gltf_on_floor();
				focus_gltf_model();
			}
			if (shaderSsrEnabled)
			{
				ImGui::SliderFloat("Reflection Strength", &shaderSsrStrength, 0.0f, 1.5f, "%.2f");
				if (!shaderSsrFloorEnabled)
				{
					// 平面床がないシーンではShader III Unit08のSSRパラメータを使用する。
					ImGui::SliderFloat("SSR Ray Distance", &shaderSsrMaxDistance, 1.0f, 50.0f, "%.1f");
					ImGui::SliderFloat("SSR Surface Thickness", &shaderSsrThickness, 0.02f, 1.5f, "%.2f");
					ImGui::SliderFloat("SSR Ray Step", &shaderSsrStride, 0.05f, 1.0f, "%.2f");
				}
			}
			ImGui::TextDisabled(shaderSsrFloorEnabled ?
				"Planar reflection: stable animated mirror" :
				"SSR fallback: Shader III Unit08");
		}

		// 過去の教材エフェクト操作はコードを残し、現在の簡易画面では非表示にする。
		if (false && ImGui::CollapsingHeader("Shader II / III Effects"))
		{
			// 教材のUNIT構成をそのまま並べず、用途が分かるエフェクト単位へ整理して操作させる。
			ImGui::Checkbox("Effect Demo Background", &shaderDemoBackgroundEnabled);
			ImGui::Checkbox("Split Before / After", &shaderSplitComparisonEnabled);
			ImGui::Checkbox("Colored 3D Test Objects", &showPrimitiveShowcase);
			ImGui::SameLine();
			ImGui::Checkbox("Rock", &showStaticMeshShowcase);
			if (shaderSplitComparisonEnabled)
			{
				ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "LEFT: Original   |   RIGHT: Effects");
			}
			ImGui::Text("Presets");
			if (ImGui::Button("Clean"))
			{
				// 比較用背景だけを残し、すべての加工を切った基準表示へ戻す。
				shaderFlowEnabled = shaderDofEnabled = shaderVignetteEnabled = false;
				shaderChromaticEnabled = shaderRadialBlurEnabled = shaderFxaaEnabled = false;
				shaderSsrEnabled = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("Cinematic"))
			{
				shaderFlowEnabled = false;
				shaderDofEnabled = true;
				shaderVignetteEnabled = true;
				shaderChromaticEnabled = true;
				shaderRadialBlurEnabled = false;
				shaderFxaaEnabled = true;
				shaderSsrEnabled = true;
				shaderVignetteStrength = 1.2f;
				shaderChromaticStrength = 0.035f;
			}
			ImGui::SameLine();
			if (ImGui::Button("Speed"))
			{
				shaderFlowEnabled = true;
				shaderDofEnabled = false;
				shaderVignetteEnabled = true;
				shaderChromaticEnabled = true;
				shaderRadialBlurEnabled = true;
				shaderFxaaEnabled = false;
				shaderSsrEnabled = true;
				shaderFlowStrength = 0.8f;
				shaderRadialStrength = 0.12f;
			}
			ImGui::SameLine();
			if (ImGui::Button("All"))
			{
				shaderFlowEnabled = shaderDofEnabled = shaderVignetteEnabled = true;
				shaderChromaticEnabled = shaderRadialBlurEnabled = shaderFxaaEnabled = true;
				shaderSsrEnabled = true;
			}
			if (ImGui::Button("SSR Demo (Unit08)"))
			{
				// Shader IIIの実行例に近い、反射床とモデルを中心にした見やすい状態へ切り替える。
				shaderFlowEnabled = shaderDofEnabled = shaderRadialBlurEnabled = false;
				shaderChromaticEnabled = false;
				shaderVignetteEnabled = false;
				shaderFxaaEnabled = true;
				shaderSsrEnabled = true;
				shaderSsrFloorEnabled = true;
				shaderDemoBackgroundEnabled = true;
				shaderSplitComparisonEnabled = false;
				showPrimitiveShowcase = true;
				showStaticMeshShowcase = true;
				cameraTarget[1] = 0.65f;
				cameraDistance = 7.0f;
				cameraPitchDegrees = 15.0f;
			}
			ImGui::Separator();
			if (ImGui::TreeNode("Shader Programming II"))
			{
				ImGui::Checkbox("Flow Distortion (Unit01)", &shaderFlowEnabled);
				if (shaderFlowEnabled)
				{
					ImGui::SliderFloat("Flow Strength", &shaderFlowStrength, 0.0f, 2.0f, "%.2f");
					ImGui::SliderFloat("Flow Speed", &shaderFlowSpeed, 0.0f, 4.0f, "%.2f");
				}
				ImGui::Checkbox("Depth of Field (Unit02-03)", &shaderDofEnabled);
				if (shaderDofEnabled)
				{
					ImGui::SliderFloat("Focus Distance", &shaderDofFocusDistance, 0.001f, 1.0f, "%.3f");
					ImGui::SliderFloat("DOF Range", &shaderDofRange, 0.001f, 0.25f, "%.3f");
					ImGui::SliderFloat("DOF Blur", &shaderDofBlurSize, 0.0f, 12.0f, "%.1f px");
				}
				ImGui::TextDisabled("Bloom / PBR / IBL are integrated in the render pipeline.");
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Shader Programming III"))
			{
				ImGui::Checkbox("Screen Space Reflection (Unit08)", &shaderSsrEnabled);
				if (shaderSsrEnabled)
				{
					ImGui::Checkbox("SSR Demo Floor", &shaderSsrFloorEnabled);
					ImGui::SliderFloat("SSR Strength", &shaderSsrStrength, 0.0f, 1.5f, "%.2f");
					ImGui::SliderFloat("SSR Distance", &shaderSsrMaxDistance, 1.0f, 50.0f, "%.1f");
					ImGui::SliderFloat("SSR Thickness", &shaderSsrThickness, 0.02f, 1.5f, "%.2f");
					ImGui::SliderFloat("SSR Step", &shaderSsrStride, 0.05f, 1.0f, "%.2f");
				}
				ImGui::Checkbox("Vignette (Unit05)", &shaderVignetteEnabled);
				if (shaderVignetteEnabled)
				{
					ImGui::SliderFloat("Vignette Strength", &shaderVignetteStrength, 0.0f, 2.0f, "%.2f");
					ImGui::SliderFloat("Vignette Smoothness", &shaderVignetteSmoothness, 0.05f, 1.0f, "%.2f");
				}
				ImGui::Checkbox("Chromatic Aberration (Unit06)", &shaderChromaticEnabled);
				if (shaderChromaticEnabled)
				{
					ImGui::SliderFloat("Chromatic Amount", &shaderChromaticStrength, 0.0f, 0.15f, "%.3f");
				}
				ImGui::Checkbox("Radial Blur (Unit03)", &shaderRadialBlurEnabled);
				if (shaderRadialBlurEnabled)
				{
					ImGui::SliderFloat("Radial Strength", &shaderRadialStrength, 0.0f, 0.25f, "%.3f");
					ImGui::SliderFloat2("Radial Center", shaderRadialCenter, 0.0f, 1.0f, "%.2f");
					ImGui::SliderFloat("Radial Mask", &shaderRadialRadius, 0.01f, 0.75f, "%.2f");
				}
				ImGui::Checkbox("FXAA (Unit10)", &shaderFxaaEnabled);
				if (shaderFxaaEnabled)
				{
					ImGui::SliderFloat("FXAA Strength", &shaderFxaaStrength, 0.0f, 1.0f, "%.2f");
				}
				ImGui::TreePop();
			}
		}

		if (false && ImGui::CollapsingHeader("PBR Materials"))
		{
			for (size_t index = 0; index < selected_gltf_model->materials.size(); ++index)
			{
				const gltf_model::material& material = selected_gltf_model->materials[index];
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

		if (false && ImGui::CollapsingHeader("Scene Nodes"))
		{
			for (size_t index = 0; index < selected_gltf_model->nodes.size(); ++index)
			{
				const auto& node = selected_gltf_model->nodes[index];
				ImGui::BulletText("%s", node.name.empty() ? "(unnamed node)" : node.name.c_str());
			}
		}
	}
	ImGui::Separator();
	ImGui::TextDisabled("Click Scene, then use mouse camera controls.  F: focus");
	ImGui::End();

	// 旧教材由来の詳細設定は残すが、通常作業を邪魔しないよう初回は折り畳んでおく。
	// カメラ操作はRendering設定ウィンドウの表示状態に依存させず、SceneがActiveなら毎フレーム処理する。
	const ImGuiIO& editor_io = ImGui::GetIO();
	const ImVec2 editor_mouse = editor_io.MousePos;
	if (editor_io.MouseDown[1] && editorSceneFocused && editorSceneHovered && !gizmoIsUsing && !gizmoIsOver)
	{
		cameraYawDegrees -= editor_io.MouseDelta.x * cameraOrbitSensitivity;
		cameraPitchDegrees += editor_io.MouseDelta.y * cameraOrbitSensitivity;
		cameraPitchDegrees = (std::clamp)(cameraPitchDegrees, -89.0f, 89.0f);
	}

	yaw = XMConvertToRadians(cameraYawDegrees);
	pitch = XMConvertToRadians(cameraPitchDegrees);
	sin_yaw = std::sin(yaw);
	cos_yaw = std::cos(yaw);
	sin_pitch = std::sin(pitch);
	cos_pitch = std::cos(pitch);
	const XMFLOAT3 editor_camera_right{ cos_yaw, 0.0f, sin_yaw };
	const XMFLOAT3 editor_camera_up
	{
		-sin_yaw * sin_pitch,
		cos_pitch,
		cos_yaw * sin_pitch
	};
	if (editor_io.MouseDown[2] && editorSceneFocused && editorSceneHovered && !gizmoIsUsing && !gizmoIsOver)
	{
		const float pan_scale = cameraDistance * 0.0015f;
		const float shift_right = -editor_io.MouseDelta.x * pan_scale;
		const float shift_up = editor_io.MouseDelta.y * pan_scale;
		cameraTarget[0] += editor_camera_right.x * shift_right + editor_camera_up.x * shift_up;
		cameraTarget[1] += editor_camera_right.y * shift_right + editor_camera_up.y * shift_up;
		cameraTarget[2] += editor_camera_right.z * shift_right + editor_camera_up.z * shift_up;
	}

	const bool editor_focus_key_down = (GetAsyncKeyState('F') & 0x8000) != 0;
	if (editor_focus_key_down && !focusKeyWasDown && !editor_io.WantCaptureKeyboard)
	{
		focus_gltf_model();
	}
	focusKeyWasDown = editor_focus_key_down;
	if (!editor_io.WantCaptureKeyboard && !gizmoIsUsing && gizmoEnabled)
	{
		if ((GetAsyncKeyState('W') & 0x8000) != 0) gizmoOperation = 0;
		if ((GetAsyncKeyState('E') & 0x8000) != 0) gizmoOperation = 1;
		if ((GetAsyncKeyState('R') & 0x8000) != 0) gizmoOperation = 2;
	}

	if (editor_io.MouseWheel != 0.0f && editorSceneFocused && editorSceneHovered && !gizmoIsUsing && !gizmoIsOver)
	{
		const float previous_distance = cameraDistance;
		const float next_distance = (std::clamp)(
			cameraDistance - editor_io.MouseWheel * cameraWheelZoomSpeed,
			0.5f,
			100.0f);
		if (editorSceneRect[2] > 0.0f && editorSceneRect[3] > 0.0f)
		{
			const float normalized_x =
				((editor_mouse.x - editorSceneRect[0]) / editorSceneRect[2]) * 2.0f - 1.0f;
			const float normalized_y =
				1.0f - ((editor_mouse.y - editorSceneRect[1]) / editorSceneRect[3]) * 2.0f;
			const float scene_aspect = editorSceneRect[2] / editorSceneRect[3];
			const float half_fov_tangent = std::tan(
				XMConvertToRadians(cameraFovDegrees) * 0.5f);
			const float distance_change = previous_distance - next_distance;
			const float shift_right = normalized_x * distance_change * half_fov_tangent * scene_aspect;
			const float shift_up = normalized_y * distance_change * half_fov_tangent;
			cameraTarget[0] += editor_camera_right.x * shift_right + editor_camera_up.x * shift_up;
			cameraTarget[1] += editor_camera_right.y * shift_right + editor_camera_up.y * shift_up;
			cameraTarget[2] += editor_camera_right.z * shift_right + editor_camera_up.z * shift_up;
		}
		cameraDistance = next_distance;
	}

	if (showAdvancedRenderingWindow)
	{
	ImGui::SetNextWindowPos(
		ImVec2(hierarchy_width + 24.0f, toolbar_height + 24.0f),
		ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(400.0f, 620.0f), ImGuiCond_Once);
	ImGui::Begin("Advanced Rendering", &showAdvancedRenderingWindow);
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
	// ギズモをドラッグしている間は、同じマウス操作でカメラが動かないようにする。
	if (false && io.MouseDown[1] && editorSceneFocused && editorSceneHovered && !gizmoIsUsing)
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
	if (false && io.MouseDown[2] && editorSceneFocused && editorSceneHovered && !gizmoIsUsing)
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
	if (!io.WantCaptureKeyboard && !gizmoIsUsing && gizmoEnabled)
	{
		// Unityと同じW/E/Rキー割当てを採用し、将来のエディタ操作へ自然に拡張できるようにする。
		if ((GetAsyncKeyState('W') & 0x8000) != 0) gizmoOperation = 0;
		if ((GetAsyncKeyState('E') & 0x8000) != 0) gizmoOperation = 1;
		if ((GetAsyncKeyState('R') & 0x8000) != 0) gizmoOperation = 2;
	}

	const float mouse_wheel = io.MouseWheel;
	if (false && mouse_wheel != 0.0f && editorSceneFocused && editorSceneHovered && !gizmoIsUsing)
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
	}
	// このフレームのInspector編集をECSへ確定してから各描画Systemを実行する。
	sync_editor_to_selected_entity();
	sync_editor_to_scene_components();
	if (sceneDirtySuppressFrames > 0)
	{
		// Scene生成・保存・読込み直後の同期差分はユーザー編集として扱わない。
		sceneDirty = false;
		--sceneDirtySuppressFrames;
	}
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
	// SceneとGameへ同じ完成映像を表示できるよう、3Dシーン自体は常に全解像度で描画する。
	D3D11_VIEWPORT game_viewport{};
	game_viewport.TopLeftX = 0.0f;
	game_viewport.TopLeftY = 0.0f;
	game_viewport.Width = static_cast<float>(renderWidth);
	game_viewport.Height = static_cast<float>(renderHeight);
	game_viewport.MinDepth = 0.0f;
	game_viewport.MaxDepth = 1.0f;
	immediate_context->RSSetViewports(1, &game_viewport);

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
	data.reflection_alpha = 1.0f;

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
	if (shaderSsrFloorEnabled)
	{
		// Shader III Unit08の平面モデルに相当する広い床を描き、深度からSSR対象面を識別できるようにする。
		XMFLOAT4X4 reflection_floor_world{};
		XMStoreFloat4x4(
			&reflection_floor_world,
			XMMatrixScaling(8.0f, 0.10f, 8.0f) *
			XMMatrixTranslation(0.0f, -0.05f, 1.0f));
		immediate_context->RSSetState(rasterizer_states[0].Get());
		immediate_context->OMSetDepthStencilState(depth_stencil_states[4], 1);
		geometric_primitives[0]->render(
			immediate_context,
			reflection_floor_world,
			{ 0.055f, 0.075f, 0.11f, 1.0f });
		immediate_context->OMSetDepthStencilState(depth_stencil_states[0], 0);
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
	#if 0
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

	// 平面床では画面外情報を失うSSRだけに頼らず、同じ姿勢のモデルを床面対称で描く。
	// ステンシルにより床の外へ鏡像がはみ出さず、アニメーションも本体と完全に同期する。
	auto render_planar_reflection = [&](const std::vector<gltf_model::node>& nodes)
	{
		if (!showGltfShowcase || !shaderSsrEnabled || !shaderSsrFloorEnabled)
		{
			return;
		}

		XMFLOAT4X4 reflected_world{};
		XMStoreFloat4x4(
			&reflected_world,
			gltf_world_matrix * XMMatrixScaling(1.0f, -1.0f, 1.0f));

		data.reflection_alpha = (std::clamp)(shaderSsrStrength * 0.32f, 0.05f, 0.65f);
		immediate_context->UpdateSubresource(constant_buffers[0], 0, nullptr, &data, 0, 0);
		immediate_context->OMSetDepthStencilState(depth_stencil_states[5], 1);
		immediate_context->OMSetBlendState(blend_states[0], nullptr, 0xFFFFFFFF);
		gltf_models[0]->render(immediate_context, reflected_world, nodes);

		immediate_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
		immediate_context->OMSetDepthStencilState(depth_stencil_states[0], 0);
		data.reflection_alpha = 1.0f;
		immediate_context->UpdateSubresource(constant_buffers[0], 0, nullptr, &data, 0, 0);
	};
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
		render_planar_reflection(animatedGltfNodes);
		gltf_models[0]->render(immediate_context, gltf_world, animatedGltfNodes);
	}
	else if (showGltfShowcase)
	{
		render_planar_reflection({});
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
	#endif

	immediate_context->RSSetState(rasterizer_states[2].Get());
	// 全Model Rendererが共有するIBLテクスチャを、描画Systemの実行前に一度だけ設定する。
	ID3D11ShaderResourceView* ecs_ibl_resource_views[4]
	{
		shader_resource_views[0].Get(), shader_resource_views[1].Get(),
		shader_resource_views[2].Get(), shader_resource_views[3].Get()
	};
	immediate_context->PSSetShaderResources(
		32, _countof(ecs_ibl_resource_views), ecs_ibl_resource_views);

	// Model Rendererコンポーネントを持つ全Entityを列挙する描画System。
	for (const scene_entity entity : sceneRegistry.entities())
	{
		transform_component* transform = sceneRegistry.transform(entity);
		model_renderer_component* renderer = sceneRegistry.renderer(entity);
		if (!transform || !renderer || !renderer->visible ||
			renderer->model_slot >= _countof(gltf_models) || !gltf_models[renderer->model_slot])
		{
			continue;
		}

		gltf_model* model = gltf_models[renderer->model_slot].get();
		const float entity_scale = (std::max)(transform->scale, 0.0001f);
		const XMMATRIX entity_world_matrix =
			XMMatrixScaling(-entity_scale, entity_scale, entity_scale) *
			XMMatrixRotationRollPitchYaw(
				XMConvertToRadians(transform->rotation[0]),
				XMConvertToRadians(transform->rotation[1]),
				XMConvertToRadians(transform->rotation[2])) *
			XMMatrixTranslation(
				transform->position[0], transform->position[1], transform->position[2]);
		XMFLOAT4X4 entity_world{};
		XMStoreFloat4x4(&entity_world, entity_world_matrix);

		animation_component* animation_state = sceneRegistry.animation(entity);
		// Animation Systemが計算した姿勢を、通常描画と反射描画の両方で共有する。
		const std::vector<gltf_model::node>* pose =
			animation_state && !model->animations.empty() ? &animation_state->pose : nullptr;

		// 反射Systemも同じEntityとアニメーション姿勢を使い、複製ごとの鏡像を描く。
		if (shaderSsrEnabled && shaderSsrFloorEnabled)
		{
			XMFLOAT4X4 reflected_world{};
			XMStoreFloat4x4(&reflected_world,
				entity_world_matrix * XMMatrixScaling(1.0f, -1.0f, 1.0f));
			data.reflection_alpha = (std::clamp)(shaderSsrStrength * 0.32f, 0.05f, 0.65f);
			immediate_context->UpdateSubresource(constant_buffers[0], 0, nullptr, &data, 0, 0);
			immediate_context->OMSetDepthStencilState(depth_stencil_states[5], 1);
			immediate_context->OMSetBlendState(blend_states[0], nullptr, 0xFFFFFFFF);
			if (pose) model->render(immediate_context, reflected_world, *pose);
			else model->render(immediate_context, reflected_world);
			immediate_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
			immediate_context->OMSetDepthStencilState(depth_stencil_states[0], 0);
			data.reflection_alpha = 1.0f;
			immediate_context->UpdateSubresource(constant_buffers[0], 0, nullptr, &data, 0, 0);
		}

		if (pose) model->render(immediate_context, entity_world, *pose);
		else model->render(immediate_context, entity_world);

		// Boundsは選択Entityだけに表示し、複製の識別をしやすくする。
		if (showModelBounds && entity == selectedSceneEntity && model->has_bounds)
		{
			const XMFLOAT3& bmin = model->bounding_box_min;
			const XMFLOAT3& bmax = model->bounding_box_max;
			const XMMATRIX bounds_matrix =
				XMMatrixScaling(bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z) *
				XMMatrixTranslation((bmin.x + bmax.x) * 0.5f,
					(bmin.y + bmax.y) * 0.5f, (bmin.z + bmax.z) * 0.5f) *
				entity_world_matrix;
			XMFLOAT4X4 bounds_world{};
			XMStoreFloat4x4(&bounds_world, bounds_matrix);
			immediate_context->RSSetState(rasterizer_states[1].Get());
			geometric_primitives[0]->render(
				immediate_context, bounds_world, { 1.0f, 0.75f, 0.10f, 1.0f });
			immediate_context->RSSetState(rasterizer_states[2].Get());
		}

		if (entity == selectedSceneEntity && animation_state)
		{
			// Animation Systemが更新した値を選択中のInspector表示へ戻す。
			gltfAnimationIndex = animation_state->clip;
			gltfAnimationTime = animation_state->time;
			gltfAnimationPlaying = animation_state->playing;
		}
	}

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

	// 元のHDRシーンと高輝度画像を中間バッファへ合成し、トーンマッピング済みSDR画像を作る。
	ID3D11ShaderResourceView* post_process_views[2]
	{
		framebuffers[0]->shader_resource_views[0].Get(),
		framebuffers[1]->shader_resource_views[0].Get()
	};
	framebuffers[2]->clear(immediate_context);
	framebuffers[2]->activate(immediate_context);
	bit_block_transfer->blit(
		immediate_context,
		post_process_views,
		0,
		2,
		pixel_shaders[1].Get());
	framebuffers[2]->deactivate(immediate_context);

	uint32_t enabled_effects = 0;
	if (shaderFlowEnabled) enabled_effects |= 1u;
	if (shaderDofEnabled) enabled_effects |= 2u;
	if (shaderVignetteEnabled) enabled_effects |= 4u;
	if (shaderChromaticEnabled) enabled_effects |= 8u;
	if (shaderRadialBlurEnabled) enabled_effects |= 16u;
	if (shaderFxaaEnabled) enabled_effects |= 32u;
	if (shaderDemoBackgroundEnabled) enabled_effects |= 64u;
	if (shaderSplitComparisonEnabled) enabled_effects |= 128u;
	// 平面床は欠落のない鏡像描画を使い、床がない場面だけUnit08のSSRへフォールバックする。
	if (shaderSsrEnabled && !shaderSsrFloorEnabled) enabled_effects |= 256u;
	shader_showcase_constants showcase_data{};
	showcase_data.enabled_effects = enabled_effects;
	showcase_data.effect_time = shaderEffectTime;
	showcase_data.flow_strength = shaderFlowStrength;
	showcase_data.flow_speed = shaderFlowSpeed;
	showcase_data.chromatic_strength = shaderChromaticStrength;
	showcase_data.vignette_strength = shaderVignetteStrength;
	showcase_data.vignette_smoothness = shaderVignetteSmoothness;
	showcase_data.radial_strength = shaderRadialStrength;
	showcase_data.radial_center = { shaderRadialCenter[0], shaderRadialCenter[1] };
	showcase_data.radial_radius = shaderRadialRadius;
	showcase_data.dof_focus_distance = shaderDofFocusDistance;
	showcase_data.dof_range = shaderDofRange;
	showcase_data.dof_blur_size = shaderDofBlurSize;
	showcase_data.fxaa_strength = shaderFxaaStrength;
	XMStoreFloat4x4(&showcase_data.projection_transform, P);
	XMStoreFloat4x4(
		&showcase_data.inverse_projection_transform,
		XMMatrixInverse(nullptr, P));
	XMStoreFloat4(
		&showcase_data.world_up_view,
		XMVector3TransformNormal(XMVectorSet(0, 1, 0, 0), V));
	showcase_data.ssr_parameters =
	{
		shaderSsrStrength,
		shaderSsrMaxDistance,
		shaderSsrThickness,
		shaderSsrStride
	};
	// II・IIIの有効化状態と調整値をb1へ送り、完成画像と深度を最後の全画面パスへ渡す。
	immediate_context->UpdateSubresource(
		constant_buffers[2],
		0,
		nullptr,
		&showcase_data,
		0,
		0);
	immediate_context->PSSetConstantBuffers(1, 1, &constant_buffers[2]);
	ID3D11ShaderResourceView* showcase_views[2]
	{
		framebuffers[2]->shader_resource_views[0].Get(),
		framebuffers[0]->shader_resource_views[1].Get()
	};
	const auto blit_finished_scene = [&]()
	{
		bit_block_transfer->blit(
			immediate_context,
			showcase_views,
			0,
			2,
			pixel_shaders[2].Get());
	};
#ifdef USE_IMGUI
	// Debug Editorでは完成映像をSceneとGameの2領域へ転送する。Gizmoは後段でSceneだけへ重ねる。
	const auto blit_to_editor_view = [&](const float rect[4])
	{
		D3D11_VIEWPORT editor_viewport{};
		editor_viewport.TopLeftX = rect[0];
		editor_viewport.TopLeftY = rect[1];
		editor_viewport.Width = (std::max)(rect[2], 1.0f);
		editor_viewport.Height = (std::max)(rect[3], 1.0f);
		editor_viewport.MinDepth = 0.0f;
		editor_viewport.MaxDepth = 1.0f;
		immediate_context->RSSetViewports(1, &editor_viewport);
		blit_finished_scene();
	};
	blit_to_editor_view(editorSceneRect);
	blit_to_editor_view(editorGameRect);
#else
	// Releaseはエディタ領域を持たないため、完成映像をウィンドウ全体へ一度だけ転送する。
	D3D11_VIEWPORT release_viewport{};
	release_viewport.Width = static_cast<float>(renderWidth);
	release_viewport.Height = static_cast<float>(renderHeight);
	release_viewport.MinDepth = 0.0f;
	release_viewport.MaxDepth = 1.0f;
	immediate_context->RSSetViewports(1, &release_viewport);
	blit_finished_scene();
#endif
	// 次フレームで同じテクスチャを描画先へ戻せるよう、PSのSRV参照を明示的に解除する。
	ID3D11ShaderResourceView* null_showcase_views[2]{};
	immediate_context->PSSetShaderResources(0, 2, null_showcase_views);

#ifdef USE_IMGUI
	model_renderer_component* gizmo_renderer = sceneRegistry.renderer(selectedSceneEntity);
	const bool gizmo_model_loaded = gizmo_renderer &&
		gizmo_renderer->model_slot < _countof(gltf_models) &&
		gltf_models[gizmo_renderer->model_slot] && gltf_models[gizmo_renderer->model_slot]->loaded;
	if (gizmoEnabled && gizmo_model_loaded && showGltfShowcase)
	{
		// 描画用の左右反転は含めず、エディタで理解しやすい正の一様スケールとして編集する。
		XMFLOAT4X4 gizmo_view{};
		XMFLOAT4X4 gizmo_projection{};
		XMFLOAT4X4 gizmo_transform{};
		const float safe_gltf_scale = (std::max)(gltfScale, 0.0001f);
		XMStoreFloat4x4(&gizmo_view, V);
		XMStoreFloat4x4(&gizmo_projection, P);
		XMStoreFloat4x4(
			&gizmo_transform,
			XMMatrixScaling(safe_gltf_scale, safe_gltf_scale, safe_gltf_scale) *
			XMMatrixRotationRollPitchYaw(
				XMConvertToRadians(gltfRotation[0]),
				XMConvertToRadians(gltfRotation[1]),
				XMConvertToRadians(gltfRotation[2])) *
			XMMatrixTranslation(gltfPosition[0], gltfPosition[1], gltfPosition[2]));

		const ImGuizmo::OPERATION operation =
			gizmoOperation == 1 ? ImGuizmo::ROTATE :
			gizmoOperation == 2 ? ImGuizmo::SCALE : ImGuizmo::TRANSLATE;
		// ワールド空間スケールは軸が崩れやすいため、拡縮時だけローカル空間へ固定する。
		const ImGuizmo::MODE mode =
			(gizmoLocalSpace || operation == ImGuizmo::SCALE) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
		ImGuizmo::SetOrthographic(false);
		// Manipulateは描画処理より後で呼ぶため、保存しておいたSceneの描画リストを明示的に指定する。
		ImGuizmo::SetDrawlist(editorSceneDrawList);
		ImGuizmo::SetID(1);
		ImGuizmo::Enable(true);
		ImGuizmo::SetGizmoSizeClipSpace(0.14f);
		ImGuizmo::SetRect(
			editorSceneRect[0],
			editorSceneRect[1],
			editorSceneRect[2],
			editorSceneRect[3]);

		// Ctrlを押している間は、Unity風に移動0.25、回転15度、拡縮0.1単位でスナップする。
		float gizmo_snap[3]{ 0.25f, 0.25f, 0.25f };
		if (operation == ImGuizmo::ROTATE)
		{
			gizmo_snap[0] = gizmo_snap[1] = gizmo_snap[2] = 15.0f;
		}
		else if (operation == ImGuizmo::SCALE)
		{
			gizmo_snap[0] = gizmo_snap[1] = gizmo_snap[2] = 0.1f;
		}
		float* snap = ImGui::GetIO().KeyCtrl ? gizmo_snap : nullptr;
		if (ImGuizmo::Manipulate(
			&gizmo_view._11,
			&gizmo_projection._11,
			operation,
			mode,
			&gizmo_transform._11,
			nullptr,
			snap))
		{
			float decomposed_scale[3]{};
			// ギズモの行列をInspectorと描画で共有している位置・回転・一様スケールへ戻す。
			ImGuizmo::DecomposeMatrixToComponents(
				&gizmo_transform._11,
				gltfPosition,
				gltfRotation,
				decomposed_scale);
			gltfScale = (std::max)(
				(std::abs(decomposed_scale[0]) + std::abs(decomposed_scale[1]) +
					std::abs(decomposed_scale[2])) / 3.0f,
				0.0001f);
			// Gizmoで変更されたTransformを、その場で選択Entityのコンポーネントへ確定する。
			sync_editor_to_selected_entity();
		}
		gizmoIsUsing = ImGuizmo::IsUsing();
		gizmoIsOver = ImGuizmo::IsOver();
	}
	else
	{
		gizmoIsUsing = false;
		gizmoIsOver = false;
	}

	ImGui::Render();

	ImGui_ImplDX11_RenderDrawData(
		ImGui::GetDrawData());
#endif

	// 通常はVSyncでフレーム生成をディスプレイ更新へ同期し、無制限描画による負荷の急増を防ぐ。
	swap_chain->Present(vsyncEnabled ? 1 : 0, 0);
}

bool framework::uninitialize()
{
	// GPU Assetより先にSystemを破棄し、終了後にComponentへ触れないようにする。
	sceneSystemManager.clear();
	// Deviceを使うロードジョブが残ったままGPUリソースを破棄しないよう、先にatomicワーカーを停止する。
	modelLoadWorker.stop();
	modelLoadCompleted.store(false, std::memory_order_release);
	modelLoading.store(false, std::memory_order_release);
	{
		std::lock_guard<std::mutex> lock(modelLoadResultMutex);
		pendingLoadedGltfModel.reset();
		pendingModelLoadError.clear();
	}
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
