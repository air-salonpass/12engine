#include "NativePreview.h"

#include <DirectXMath.h>
#include <filesystem>
#include <commdlg.h>
#include <fstream>
#include <unordered_set>
#include <windowsx.h>
#include "../tinygltf-release/json.hpp"

#ifdef USE_IMGUI
#include "../imgui/imgui.h"
#include "../imgui/imgui_impl_win32.h"
#include "../ThirdParty/ImGuizmo/ImGuizmo.h"
extern ImWchar glyphRangesJapanese[];
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
#endif

using namespace DirectX;

namespace
{
	std::string wide_to_ansi(const wchar_t* text)
	{
		if (!text || !*text) return {};
		const int bytes = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
		std::string result(static_cast<std::size_t>(bytes), '\0');
		WideCharToMultiByte(CP_ACP, 0, text, -1, result.data(), bytes, nullptr, nullptr);
		result.pop_back();
		return result;
	}

	std::pair<UINT, UINT> scene_target_size(UINT window_width, UINT window_height)
	{
#ifdef USE_IMGUI
		// Editorの左右・上下分割後の実表示サイズで描き、余分なPixel処理と縦横比のずれをなくす。
		const UINT side = (std::max)(320u, static_cast<UINT>(window_width * 0.28f));
		const UINT width = (std::max)(window_width > side + 8 ? window_width - side - 8 : 1u, 1u);
		const UINT half_height = window_height > 112 ? (window_height - 64) / 2 - 24 : 1u;
		return { width, (std::max)(half_height, 1u) };
#else
		return { (std::max)(window_width, 1u), (std::max)(window_height, 1u) };
#endif
	}

	std::string normalized_asset_path(const std::string& filename)
	{
		const std::filesystem::path path(filename);
		if (path.is_relative()) return path.lexically_normal().string();
		try { return std::filesystem::weakly_canonical(path).string(); }
		catch (...) { return path.lexically_normal().string(); }
	}
}

bool native_preview12::initialize()
{
	RECT area{};
	GetClientRect(hwnd_, &area);
	const UINT width = (std::max)(1L, area.right - area.left);
	const UINT height = (std::max)(1L, area.bottom - area.top);
	if (!renderer_.initialize(hwnd_, width, height,
#ifdef _DEBUG
		// 通常起動ではDebug Layerの大きな初期化負荷を避け、VSデバッグ実行時だけ有効にする。
		IsDebuggerPresent() != FALSE
#else
		false
#endif
	)) { startup_error_code_ = 11; return false; }
	if (!model_renderer_.initialize(renderer_.device())) { startup_error_code_ = 120 + model_renderer_.initialization_error(); return false; }
	if (!scene_geometry_.initialize(renderer_.device())) { startup_error_code_ = 21; return false; }
	const auto [target_width, target_height] = scene_target_size(width, height);
	if (!scene_target_.initialize(renderer_.device(), target_width, target_height)) { startup_error_code_ = 13; return false; }
	if (!reflection_target_.initialize(renderer_.device(), target_width, target_height)) { startup_error_code_ = 22; return false; }
	if (!scene_display_target_.initialize(renderer_.device(), target_width, target_height)) { startup_error_code_ = 20; return false; }
	if (!game_target_.initialize(renderer_.device(), target_width, target_height)) { startup_error_code_ = 18; return false; }
	if (!ssr_.initialize(renderer_.device())) { startup_error_code_ = 14; return false; }
	model_resources_.initialize(renderer_.device());
	// 教材フォルダ自体は変更せず、エンジン側から完成済みIBLアセットを読み取り専用で利用する。
	const std::filesystem::path environment_candidates[]{
		L".\\resources\\environments\\hilly_terrain_01_puresky",
		L".\\Rendering\\resources\\environments\\hilly_terrain_01_puresky" };
	std::filesystem::path environment_directory;
	for (const auto& path : environment_candidates)
	{
		if (std::filesystem::exists(path))
		{
			environment_directory = path;
			break;
		}
	}

	const std::filesystem::path candidates[]{
		L".\\resources\\gltf\\CesiumMan\\CesiumMan.gltf",
		L".\\Rendering\\resources\\gltf\\CesiumMan\\CesiumMan.gltf" };
	std::filesystem::path initial_model_path;
	for (const auto& path : candidates)
		if (std::filesystem::exists(path)) { initial_model_path = path.lexically_normal(); break; }
	if (initial_model_path.empty()) { startup_error_code_ = 15; return false; }
	create_scene_services();
	// 最初のモデルもワーカーへ渡し、WindowとEditorを先に表示する。
	selected_entity_ = scene_.create_entity(initial_model_path.filename().string());
	scene_.add_transform(selected_entity_);
	auto& initial_renderer = scene_.add_model_renderer(selected_entity_);
	initial_renderer.asset_path = initial_model_path.string();
	initial_renderer.visible = true;
	scene_.add_animation(selected_entity_);
	// Model Workerを先に開始し、その並行時間でIBLを準備する。最初の描画時点では照明が完成している。
	request_model_load(initial_model_path.string(), false);
	if (!environment_directory.empty())
	{
		environment_lighting_ = std::make_unique<environment_lighting12>();
		if (!environment_lighting_->initialize(renderer_.device(), environment_directory))
		{
			environment_lighting_.reset();
			editor_error_ = "IBL environment could not be loaded.";
		}
	}
	const std::filesystem::path asset_candidates[]{ L".\\resources\\gltf", L".\\Rendering\\resources\\gltf" };
	for (const auto& path : asset_candidates)
		if (std::filesystem::exists(path)) { asset_browser_directory_ = path.lexically_normal().string(); break; }

#ifdef USE_IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO().IniFilename = nullptr;
	ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\meiryo.ttc", 15.0f, nullptr, glyphRangesJapanese);
	ImGui::StyleColorsDark();
	if (!ImGui_ImplWin32_Init(hwnd_)) { startup_error_code_ = 16; return false; }
	if (!imgui_.initialize(renderer_.device(), directx12_renderer::frame_count,
		directx12_renderer::color_format)) { startup_error_code_ = 17; return false; }
#endif
	startup_error_code_ = 0;
	return true;
}

void native_preview12::create_scene_services()
{
	// Cameraや描画設定もScene Entityにして、保存・複製可能なゲーム状態と同じ規則で扱う。
	camera_entity_ = scene_.create_entity("Main Camera");
	auto& camera = scene_.add_camera(camera_entity_);
	camera.distance = 4.0f;
	camera.pitch_degrees = XMConvertToDegrees(0.15f);
	light_entity_ = scene_.create_entity("Directional Light");
	auto& light = scene_.add_directional_light(light_entity_);
	light.direction[0] = 0.35f; light.direction[1] = 0.8f; light.direction[2] = -0.45f;
	reflection_floor_entity_ = scene_.create_entity("Reflection Floor");
	auto& floor = scene_.add_reflection_floor(reflection_floor_entity_);
	floor.strength = 0.75f;
	floor.thickness = 0.2f;
	post_process_entity_ = scene_.create_entity("Post Process Volume");
	 scene_.add_post_process(post_process_entity_).exposure = 1.15f;
	rendering_settings_entity_ = scene_.create_entity("Rendering Settings");
	scene_.add_rendering_settings(rendering_settings_entity_);
}

scene_entity native_preview12::create_model_entity(const std::string& filename)
{
	auto model = model_resources_.find(filename);
	if (!model) return null_scene_entity;
	const scene_entity entity = scene_.create_entity(
		std::filesystem::path(model->filename()).filename().string());
	auto& transform = scene_.add_transform(entity);
	transform.scale = 1.0f;
	auto& renderer = scene_.add_model_renderer(entity);
	renderer.asset_path = model->filename();
	renderer.visible = true;
	if (!model->animations().empty())
	{
		scene_.add_animation(entity);
		auto& controller = scene_.add_animation_controller(entity);
		for (std::size_t index = 0; index < model->animations().size(); ++index)
		{
			const std::string& source_name = model->animations()[index].name;
			controller.states.push_back({ source_name.empty() ? "State " + std::to_string(index) : source_name,
				static_cast<int>(index), 1.0f, true });
		}
	}
	return entity;
}

int native_preview12::run()
{
	if (!initialize()) return startup_error_code_;
	MSG message{};
	auto previous = std::chrono::steady_clock::now();
	while (message.message != WM_QUIT)
	{
		if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); continue; }
		if (IsIconic(hwnd_)) { WaitMessage(); continue; }
		const auto now = std::chrono::steady_clock::now();
		const float elapsed = (std::min)(0.1f, std::chrono::duration<float>(now - previous).count());
		previous = now;
		update(elapsed);
		render();
		fps_elapsed_ += elapsed;
		++fps_frames_;
		if (fps_elapsed_ >= 0.5f)
		{
			const float fps = fps_frames_ / fps_elapsed_;
			wchar_t title[160]{};
			const wchar_t* asset_state = model_resources_.loading() ? L"Loading" :
				(model_resources_.last_error().empty() ? L"Ready" : L"Load Error");
			swprintf_s(title, L"Jisaku Engine - DirectX 12 | FPS: %.0f | %.2f ms | %s",
				fps, 1000.0f / (std::max)(fps, 0.01f), asset_state);
			SetWindowTextW(hwnd_, title);
			fps_elapsed_ = 0.0f;
			fps_frames_ = 0;
		}
	}
	shutdown();
	return static_cast<int>(message.wParam);
}

void native_preview12::update(float elapsed_time)
{
	// 【System追加位置】非同期完了をMain Threadへ反映してからゲームSystemを更新する。
	// 物理→当たり判定→Animationのように依存がある場合は、この並びを実行順として明示する。
	// System実行中にEntityを大量追加・削除する場合は、要求を一旦Queueへ貯めて更新後に反映する。
	poll_model_load();
	const UINT width = pending_width_.exchange(0, std::memory_order_acq_rel);
	const UINT height = pending_height_.exchange(0, std::memory_order_acq_rel);
	if (width && height)
	{
		renderer_.resize(width, height);
		const auto [target_width, target_height] = scene_target_size(width, height);
		scene_target_.resize(target_width, target_height);
		reflection_target_.resize(target_width, target_height);
		scene_display_target_.resize(target_width, target_height);
		game_target_.resize(target_width, target_height);
	}
	const float scene_delta = play_mode_ ? elapsed_time : (step_requested_ ? 1.0f / 60.0f : 0.0f);
	// 新しいゲームSystemは原則この付近から呼ぶ。Pause中に動かすEditor処理にはelapsed_time、
	// ゲーム時間に従う処理にはscene_deltaを渡して、Pause/Stepの挙動を統一すること。
	animation_system_.update(scene_, model_resources_, scene_delta);
	step_requested_ = false;
}

void native_preview12::request_model_load(const std::string& filename, bool add_placement_when_ready)
{
	if (filename.empty()) return;
	if (auto loaded = model_resources_.find(filename))
	{
		if (add_placement_when_ready)
		{
			selected_entity_ = create_model_entity(loaded->filename());
		}
		return;
	}
	const std::string normalized = normalized_asset_path(filename);
	if (add_placement_when_ready) assets_to_place_when_ready_.insert(normalized);
	if (!model_resources_.request(normalized) && add_placement_when_ready)
	{
		// 既にキュー済みの場合も、完了通知を待って配置する。
		assets_to_place_when_ready_.insert(normalized);
	}
}

void native_preview12::poll_model_load()
{
	const std::vector<std::string> loaded = model_resources_.update();
	if (loaded.empty()) return;
	// Descriptorを再利用せず資産を追加するため、描画中モデルを止めずに公開できる。
	for (const std::string& filename : loaded)
	{
		// Scene/Prefab読込み中に先に生成したEntityへ、AssetのClip一覧からControllerを補完する。
		if (auto model = model_resources_.find(filename); model && !model->animations().empty())
		{
			scene_.for_each<model_renderer_component>([&](scene_entity entity, model_renderer_component& renderer)
			{
				if (model_resources_.find(renderer.asset_path).get() != model.get() || scene_.animation_controller(entity)) return;
				if (!scene_.animation(entity)) scene_.add_animation(entity);
				auto& controller = scene_.add_animation_controller(entity);
				for (std::size_t index = 0; index < model->animations().size(); ++index)
				{
					const std::string& source_name = model->animations()[index].name;
					controller.states.push_back({ source_name.empty() ? "State " + std::to_string(index) : source_name,
						static_cast<int>(index), 1.0f, true });
				}
			});
		}
		if (assets_to_place_when_ready_.erase(filename) > 0)
		{
			selected_entity_ = create_model_entity(filename);
		}
	}
}

bool native_preview12::browse_model()
{
	wchar_t filename[MAX_PATH]{};
	OPENFILENAMEW dialog{};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = hwnd_;
	dialog.lpstrFilter = L"glTF Model (*.gltf;*.glb)\0*.gltf;*.glb\0All Files (*.*)\0*.*\0";
	dialog.lpstrFile = filename;
	dialog.nMaxFile = _countof(filename);
	dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	if (!GetOpenFileNameW(&dialog)) return false;
	push_undo();
	request_model_load(wide_to_ansi(filename));
	return true;
}

void native_preview12::new_scene()
{
	if (model_resources_.loading()) return;
	push_undo();
	renderer_.wait_idle();
	scene_.clear();
	animation_system_.clear();
	create_scene_services();
	selected_entity_ = null_scene_entity;
	current_scene_path_.clear();
}

native_preview12::editor_snapshot native_preview12::capture_snapshot() const
{
	editor_snapshot snapshot;
	snapshot.scene = scene_;
	snapshot.selected = selected_entity_;
	snapshot.camera = camera_entity_;
	snapshot.light = light_entity_;
	snapshot.floor = reflection_floor_entity_;
	snapshot.post_process = post_process_entity_;
	snapshot.rendering_settings = rendering_settings_entity_;
	return snapshot;
}

void native_preview12::restore_snapshot(const editor_snapshot& snapshot)
{
	scene_ = snapshot.scene;
	selected_entity_ = snapshot.selected;
	camera_entity_ = snapshot.camera;
	light_entity_ = snapshot.light;
	reflection_floor_entity_ = snapshot.floor;
	post_process_entity_ = snapshot.post_process;
	rendering_settings_entity_ = snapshot.rendering_settings;
	// PoseはScene Componentから次フレームに再計算するため、古いEntity IDのCacheを破棄する。
	animation_system_.clear();
}

void native_preview12::push_undo()
{
	push_undo(capture_snapshot());
}

void native_preview12::push_undo(editor_snapshot snapshot)
{
	constexpr std::size_t maximum_history = 64;
	if (undo_history_.size() >= maximum_history) undo_history_.erase(undo_history_.begin());
	undo_history_.push_back(std::move(snapshot));
	redo_history_.clear();
}

void native_preview12::undo()
{
	if (undo_history_.empty()) return;
	redo_history_.push_back(capture_snapshot());
	const editor_snapshot snapshot = std::move(undo_history_.back());
	undo_history_.pop_back();
	restore_snapshot(snapshot);
}

void native_preview12::redo()
{
	if (redo_history_.empty()) return;
	undo_history_.push_back(capture_snapshot());
	const editor_snapshot snapshot = std::move(redo_history_.back());
	redo_history_.pop_back();
	restore_snapshot(snapshot);
}

void native_preview12::focus_selected()
{
	auto* transform = scene_.transform(selected_entity_);
	auto* renderer = scene_.renderer(selected_entity_);
	auto* camera = scene_.camera(camera_entity_);
	if (!transform || !renderer || !camera) return;
	if (auto model = model_resources_.find(renderer->asset_path))
	{
		const auto& minimum = model->bounds_min();
		const auto& maximum = model->bounds_max();
		camera->target[0] = transform->position[0] + (minimum.x + maximum.x) * 0.5f * transform->scale3[0];
		camera->target[1] = transform->position[1] + (minimum.y + maximum.y) * 0.5f * transform->scale3[1];
		camera->target[2] = transform->position[2] + (minimum.z + maximum.z) * 0.5f * transform->scale3[2];
		const float extent = (std::max)({
			(maximum.x - minimum.x) * transform->scale3[0],
			(maximum.y - minimum.y) * transform->scale3[1],
			(maximum.z - minimum.z) * transform->scale3[2], 0.25f });
		camera->distance = (std::clamp)(extent * 2.2f, 0.5f, 50.0f);
	}
}

void native_preview12::toggle_fullscreen()
{
	if (!borderless_fullscreen_)
	{
		windowed_style_ = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
		windowed_placement_.length = sizeof(WINDOWPLACEMENT);
		GetWindowPlacement(hwnd_, &windowed_placement_);
		MONITORINFO monitor{ sizeof(MONITORINFO) };
		if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor)) return;
		// 排他フルスクリーンを使わず、SwapChainの再生成なしで安全にモニター全体へ広げる。
		SetWindowLongPtrW(hwnd_, GWL_STYLE, windowed_style_ & ~WS_OVERLAPPEDWINDOW);
		SetWindowPos(hwnd_, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
			monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		borderless_fullscreen_ = true;
	}
	else
	{
		SetWindowLongPtrW(hwnd_, GWL_STYLE, windowed_style_);
		SetWindowPlacement(hwnd_, &windowed_placement_);
		SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		borderless_fullscreen_ = false;
	}
}

bool native_preview12::browse_scene(bool save_dialog)
{
	wchar_t filename[MAX_PATH]{};
	OPENFILENAMEW dialog{};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = hwnd_;
	dialog.lpstrFilter = L"Jisaku Scene (*.jscene)\0*.jscene\0All Files (*.*)\0*.*\0";
	dialog.lpstrFile = filename;
	dialog.nMaxFile = _countof(filename);
	dialog.lpstrDefExt = L"jscene";
	dialog.Flags = OFN_PATHMUSTEXIST | (save_dialog ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
	const BOOL selected = save_dialog ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
	if (!selected) return false;
	return save_dialog ? save_scene(wide_to_ansi(filename)) : load_scene(wide_to_ansi(filename));
}

bool native_preview12::browse_prefab(bool save_dialog)
{
	wchar_t filename[MAX_PATH]{};
	OPENFILENAMEW dialog{};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = hwnd_;
	dialog.lpstrFilter = L"Jisaku Prefab (*.jprefab)\0*.jprefab\0All Files (*.*)\0*.*\0";
	dialog.lpstrFile = filename;
	dialog.nMaxFile = _countof(filename);
	dialog.lpstrDefExt = L"jprefab";
	dialog.Flags = OFN_PATHMUSTEXIST | (save_dialog ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
	const BOOL selected = save_dialog ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
	if (!selected) return false;
	return save_dialog ? save_prefab(wide_to_ansi(filename), selected_entity_) : load_prefab(wide_to_ansi(filename));
}

bool native_preview12::save_prefab(const std::string& filename, scene_entity entity)
{
	// 【Component追加時】Prefabへ残したいComponentは、ここでキー名と全フィールドを書き出し、
	// load_prefab側にも同じキーの読込みを追加する。片側だけの実装はデータ消失の原因になる。
	using nlohmann::json;
	const auto* name = scene_.name(entity);
	const auto* transform = scene_.transform(entity);
	const auto* renderer = scene_.renderer(entity);
	if (filename.empty() || !transform || !renderer) return false;
	const auto* animation = scene_.animation(entity);
	json root{
		{"format", "JisakuEnginePrefab"}, {"version", 1},
		{"entity", {
			{"name", name ? name->value : "Prefab"},
			{"transform", {
				{"position", {transform->position[0], transform->position[1], transform->position[2]}},
				{"rotation", {transform->rotation[0], transform->rotation[1], transform->rotation[2]}},
				{"scale3", {transform->scale3[0], transform->scale3[1], transform->scale3[2]}}}},
			{"model_renderer", {{"asset", renderer->asset_path}, {"visible", renderer->visible}}}
		}}
	};
	if (animation)
		root["entity"]["animation"] = {{"clip", animation->clip}, {"time", animation->time},
			{"playing", animation->playing}, {"loop", animation->loop}, {"speed", animation->speed}};
	if (const auto* controller = scene_.animation_controller(entity))
	{
		root["entity"]["animation_controller"] = {
			{"enabled", controller->enabled}, {"current_state", controller->current_state},
			{"requested_state", controller->requested_state}, {"states", json::array()} };
		for (const auto& state : controller->states)
			root["entity"]["animation_controller"]["states"].push_back({
				{"name", state.name}, {"clip", state.clip}, {"speed", state.speed}, {"loop", state.loop} });
	}
	try
	{
		std::ofstream output(std::filesystem::path(filename), std::ios::binary | std::ios::trunc);
		if (!output) return false;
		output << root.dump(2);
		scene_.add_prefab(entity).source_path = filename;
		return true;
	}
	catch (const std::exception& exception) { editor_error_ = exception.what(); return false; }
}

bool native_preview12::load_prefab(const std::string& filename)
{
	// 古いPrefabに新しいキーが無くても既定値で開けるよう、value("key", default)を使う。
	// ファイル由来のEntity IDは信用せず、現在のscene_registryで新しいIDを発行する。
	using nlohmann::json;
	try
	{
		std::ifstream input(std::filesystem::path(filename), std::ios::binary);
		if (!input) return false;
		const json root = json::parse(input);
		if (root.value("format", "") != "JisakuEnginePrefab" || !root.contains("entity")) return false;
		const json& value = root["entity"];
		const std::string asset = value.value("model_renderer", json::object()).value("asset", "");
		if (asset.empty()) return false;
		push_undo();
		const scene_entity entity = scene_.create_entity(value.value("name", "Prefab"));
		auto& transform = scene_.add_transform(entity);
		auto read3 = [](const json& source, float target[3]) {
			if (source.is_array()) for (std::size_t i = 0; i < 3 && i < source.size(); ++i) target[i] = source[i].get<float>();
		};
		if (value.contains("transform"))
		{
			read3(value["transform"].value("position", json::array()), transform.position);
			read3(value["transform"].value("rotation", json::array()), transform.rotation);
			read3(value["transform"].value("scale3", json::array()), transform.scale3);
			transform.scale = (transform.scale3[0] + transform.scale3[1] + transform.scale3[2]) / 3.0f;
		}
		auto& renderer = scene_.add_model_renderer(entity);
		renderer.asset_path = asset;
		renderer.visible = value["model_renderer"].value("visible", true);
		if (value.contains("animation"))
		{
			auto& animation = scene_.add_animation(entity);
			animation.clip = value["animation"].value("clip", 0);
			animation.time = value["animation"].value("time", 0.0f);
			animation.playing = value["animation"].value("playing", true);
			animation.loop = value["animation"].value("loop", true);
			animation.speed = value["animation"].value("speed", 1.0f);
		}
		if (value.contains("animation_controller"))
		{
			const json& source = value["animation_controller"];
			auto& controller = scene_.add_animation_controller(entity);
			controller.enabled = source.value("enabled", true);
			controller.current_state = source.value("current_state", 0);
			controller.requested_state = source.value("requested_state", controller.current_state);
			for (const json& state : source.value("states", json::array()))
				controller.states.push_back({ state.value("name", "State"), state.value("clip", 0),
					state.value("speed", 1.0f), state.value("loop", true) });
		}
		scene_.add_prefab(entity).source_path = filename;
		request_model_load(asset, false);
		selected_entity_ = entity;
		return true;
	}
	catch (const std::exception& exception) { editor_error_ = exception.what(); return false; }
}

bool native_preview12::save_scene(const std::string& filename)
{
	// 【Component追加時の保存入口】Sceneに永続化する値をここへ追加する。
	// Assetは絶対ポインターではなく再読込み可能なpath/IDを保存し、GPU Resourceは保存しない。
	using nlohmann::json;
	if (filename.empty()) return false;
	json root;
	root["format"] = "JisakuEngineScene";
	root["version"] = 1;
	root["entities"] = json::array();
	const camera_component* camera = scene_.camera(camera_entity_);
	const reflection_floor_component* floor = scene_.reflection_floor(reflection_floor_entity_);
	const post_process_component* post = scene_.post_process(post_process_entity_);
	const rendering_settings_component* settings = scene_.rendering_settings(rendering_settings_entity_);
	root["entities"].push_back({
		{"name", "Main Camera"},
		{"camera", {
			{"target", {camera->target[0], camera->target[1], camera->target[2]}}, {"distance", camera->distance}, {"fov", camera->fov_degrees},
			{"yaw", camera->yaw_degrees}, {"pitch", camera->pitch_degrees}, {"active", camera->active}
		}}
	});
	const directional_light_component* directional_light = scene_.directional_light(light_entity_);
	root["entities"].push_back({
		{"name", "Directional Light"},
		{"directional_light", {{"direction", {directional_light->direction[0], directional_light->direction[1], directional_light->direction[2]}},
			{"enabled", directional_light->enabled}}}
	});
	root["entities"].push_back({
		{"name", "Reflection Settings"},
		{"reflection_floor", {
			{"enabled", floor->enabled}, {"reflection_enabled", floor->reflection_enabled},
			{"planar_reflection", floor->planar_reflection}, {"strength", floor->strength},
			{"max_distance", floor->max_distance}, {"thickness", floor->thickness}, {"stride", floor->stride},
			{"steps", floor->steps}, {"exposure", post->exposure}, {"ibl_intensity", settings->ibl_intensity},
			{"vsync", settings->vsync},
			{"mesh_shaders", settings->mesh_shaders}, {"height", floor->height},
			{"color", {floor->color[0], floor->color[1], floor->color[2]}}
		}}
	});
	for (const scene_entity entity : scene_.entities())
	{
		const auto* name = scene_.name(entity);
		const auto* transform = scene_.transform(entity);
		const auto* renderer = scene_.renderer(entity);
		if (!transform || !renderer) continue;
		const auto* animation = scene_.animation(entity);
		const float uniform_scale = (transform->scale3[0] + transform->scale3[1] + transform->scale3[2]) / 3.0f;
		root["entities"].push_back({
				{"name", name ? name->value : "Model"},
				{"transform", {
					{"position", {transform->position[0], transform->position[1], transform->position[2]}},
					{"rotation", {transform->rotation[0], transform->rotation[1], transform->rotation[2]}},
					{"scale", uniform_scale}, {"scale3", {transform->scale3[0], transform->scale3[1], transform->scale3[2]}}
				}},
				{"model_renderer", {{"asset", renderer->asset_path}, {"visible", renderer->visible}}},
				{"animation", {{"clip", animation ? animation->clip : 0}, {"time", animation ? animation->time : 0.0f},
					{"playing", animation ? animation->playing : false}, {"loop", animation ? animation->loop : true},
					{"speed", animation ? animation->speed : 1.0f}}}
			});
		if (const auto* controller = scene_.animation_controller(entity))
		{
			json& saved = root["entities"].back()["animation_controller"];
			saved = {{"enabled", controller->enabled}, {"current_state", controller->current_state},
				{"requested_state", controller->requested_state}, {"states", json::array()}};
			for (const auto& state : controller->states)
				saved["states"].push_back({{"name", state.name}, {"clip", state.clip},
					{"speed", state.speed}, {"loop", state.loop}});
		}
	}
	try
	{
		std::ofstream output(std::filesystem::path(filename), std::ios::binary | std::ios::trunc);
		if (!output) return false;
		output << root.dump(2);
		current_scene_path_ = filename;
		return true;
	}
	catch (const std::exception& exception)
	{
		editor_error_ = exception.what();
		return false;
	}
}

bool native_preview12::load_scene(const std::string& filename)
{
	// 【Component追加時の読込み入口】save_sceneと同じJSONキーを読み、欠損時はComponent既定値を使う。
	// Sceneを公開した後にロード待ちで停止せず、Assetはrequest_model_loadで非同期要求する。
	using nlohmann::json;
	if (filename.empty() || model_resources_.loading()) return false;
	try
	{
		std::ifstream input(std::filesystem::path(filename), std::ios::binary);
		if (!input) return false;
		const json root = json::parse(input);
		if (root.value("format", "") != "JisakuEngineScene" || !root.contains("entities")) return false;
		push_undo();
		scene_.clear();
		animation_system_.clear();
		create_scene_services();
		selected_entity_ = null_scene_entity;
		for (const json& entity : root["entities"])
		{
			if (entity.contains("camera"))
			{
				const json& camera = entity["camera"];
				auto& component = *scene_.camera(camera_entity_);
				component.distance = camera.value("distance", component.distance);
				component.yaw_degrees = camera.value("yaw", component.yaw_degrees);
				component.pitch_degrees = camera.value("pitch", component.pitch_degrees);
				component.fov_degrees = camera.value("fov", component.fov_degrees);
				component.active = camera.value("active", component.active);
				if (camera.contains("target") && camera["target"].is_array())
					for (std::size_t i = 0; i < 3 && i < camera["target"].size(); ++i) component.target[i] = camera["target"][i].get<float>();
			}
			if (entity.contains("reflection_floor"))
			{
				const json& reflection = entity["reflection_floor"];
				auto& component = *scene_.reflection_floor(reflection_floor_entity_);
				component.strength = reflection.value("strength", component.strength);
				component.enabled = reflection.value("enabled", component.enabled);
				component.reflection_enabled = reflection.value("reflection_enabled", component.reflection_enabled);
				component.planar_reflection = reflection.value("planar_reflection", component.planar_reflection);
				component.max_distance = reflection.value("max_distance", component.max_distance);
				component.thickness = reflection.value("thickness", component.thickness);
				component.stride = reflection.value("stride", component.stride);
				component.steps = reflection.value("steps", component.steps);
				component.height = reflection.value("height", component.height);
				scene_.post_process(post_process_entity_)->exposure = reflection.value("exposure", scene_.post_process(post_process_entity_)->exposure);
				scene_.rendering_settings(rendering_settings_entity_)->ibl_intensity = reflection.value("ibl_intensity", scene_.rendering_settings(rendering_settings_entity_)->ibl_intensity);
				scene_.rendering_settings(rendering_settings_entity_)->vsync = reflection.value("vsync", scene_.rendering_settings(rendering_settings_entity_)->vsync);
				scene_.rendering_settings(rendering_settings_entity_)->mesh_shaders = reflection.value("mesh_shaders", scene_.rendering_settings(rendering_settings_entity_)->mesh_shaders);
				if (reflection.contains("color") && reflection["color"].is_array())
					for (std::size_t i = 0; i < 3 && i < reflection["color"].size(); ++i) component.color[i] = reflection["color"][i].get<float>();
			}
			if (entity.contains("directional_light"))
			{
				const json& source_light = entity["directional_light"];
				auto& component = *scene_.directional_light(light_entity_);
				component.enabled = source_light.value("enabled", component.enabled);
				if (source_light.contains("direction") && source_light["direction"].is_array())
					for (std::size_t i = 0; i < 3 && i < source_light["direction"].size(); ++i) component.direction[i] = source_light["direction"][i].get<float>();
			}
			if (!entity.contains("model_renderer")) continue;
			const std::string asset = entity["model_renderer"].value("asset", "");
			const scene_entity model_entity = scene_.create_entity(
				entity.value("name", std::filesystem::path(asset).filename().string()));
			auto& transform_component = scene_.add_transform(model_entity);
			auto& renderer_component = scene_.add_model_renderer(model_entity);
			renderer_component.asset_path = asset;
			renderer_component.visible = entity["model_renderer"].value("visible", true);
			if (entity.contains("transform"))
			{
				const json& transform = entity["transform"];
				auto read3 = [](const json& value, float target[3]) {
					if (value.is_array()) for (std::size_t i = 0; i < 3 && i < value.size(); ++i) target[i] = value[i].get<float>();
				};
				read3(transform.value("position", json::array()), transform_component.position);
				read3(transform.value("rotation", json::array()), transform_component.rotation);
				if (transform.contains("scale3")) read3(transform["scale3"], transform_component.scale3);
				else transform_component.scale3[0] = transform_component.scale3[1] = transform_component.scale3[2] = transform.value("scale", 1.0f);
				transform_component.scale = (transform_component.scale3[0] + transform_component.scale3[1] + transform_component.scale3[2]) / 3.0f;
			}
			if (entity.contains("animation"))
			{
				auto& animation = scene_.add_animation(model_entity);
				animation.clip = entity["animation"].value("clip", 0);
				animation.time = entity["animation"].value("time", 0.0f);
				animation.playing = entity["animation"].value("playing", true);
				animation.loop = entity["animation"].value("loop", true);
				animation.speed = entity["animation"].value("speed", 1.0f);
			}
			if (entity.contains("animation_controller"))
			{
				const json& source = entity["animation_controller"];
				auto& controller = scene_.add_animation_controller(model_entity);
				controller.enabled = source.value("enabled", true);
				controller.current_state = source.value("current_state", 0);
				controller.requested_state = source.value("requested_state", controller.current_state);
				for (const json& state : source.value("states", json::array()))
					controller.states.push_back({state.value("name", "State"), state.value("clip", 0),
						state.value("speed", 1.0f), state.value("loop", true)});
			}
			if (!asset.empty())
			{
				request_model_load(asset, false);
				if (selected_entity_ == null_scene_entity) selected_entity_ = model_entity;
			}
		}
		current_scene_path_ = filename;
		return true;
	}
	catch (const std::exception& exception)
	{
		editor_error_ = exception.what();
		return false;
	}
}

void native_preview12::render()
{
	// renderはComponentの確定値を読むだけにし、ゲームルールやAnimation時間を更新しない。
	// Scene/Game/Reflectionで同じEntityを複数回描くため、ここで状態を進めると表示が食い違う。
	const float clear[4]{ 0.025f, 0.03f, 0.045f, 1.0f };
	ID3D12GraphicsCommandList* commands = renderer_.begin_frame(clear);
	if (!commands) return;
	const float aspect = static_cast<float>(scene_target_.width()) / static_cast<float>((std::max)(scene_target_.height(), 1u));
	const camera_component& camera = *scene_.camera(camera_entity_);
	const reflection_floor_component& floor = *scene_.reflection_floor(reflection_floor_entity_);
	const directional_light_component& light = *scene_.directional_light(light_entity_);
	const post_process_component& post = *scene_.post_process(post_process_entity_);
	const rendering_settings_component& settings = *scene_.rendering_settings(rendering_settings_entity_);
	const float orbit_yaw = XMConvertToRadians(camera.yaw_degrees);
	const float orbit_pitch = XMConvertToRadians(camera.pitch_degrees);
	const XMVECTOR target = XMVectorSet(camera.target[0], camera.target[1], camera.target[2], 1);
	const XMVECTOR eye = XMVectorSet(
		camera.target[0] + std::sin(orbit_yaw) * std::cos(orbit_pitch) * camera.distance,
		camera.target[1] + std::sin(orbit_pitch) * camera.distance,
		camera.target[2] - std::cos(orbit_yaw) * std::cos(orbit_pitch) * camera.distance, 1);
	XMFLOAT4X4 world, view, projection;
	auto make_world = [](const transform_component& transform)
	{
		XMFLOAT4X4 result{};
		XMStoreFloat4x4(&result,
			XMMatrixScaling(transform.scale3[0], transform.scale3[1], transform.scale3[2]) *
			XMMatrixRotationRollPitchYaw(XMConvertToRadians(transform.rotation[0]),
				XMConvertToRadians(transform.rotation[1]), XMConvertToRadians(transform.rotation[2])) *
			XMMatrixTranslation(transform.position[0], transform.position[1], transform.position[2]));
		return result;
	};
	if (const auto* selected_transform = scene_.transform(selected_entity_)) world = make_world(*selected_transform);
	else XMStoreFloat4x4(&world, XMMatrixIdentity());
	XMStoreFloat4x4(&view, XMMatrixLookAtLH(eye, target, XMVectorSet(0, 1, 0, 0)));
	XMStoreFloat4x4(&projection, XMMatrixPerspectiveFovLH(XMConvertToRadians(camera.fov_degrees), aspect, 0.05f, 200.0f));

	model_renderer_.begin_frame(renderer_.frame_index());
	scene_geometry_.begin_frame(renderer_.frame_index());
	auto render_models = [&](bool reflected)
	{
		scene_.for_each<transform_component, model_renderer_component>(
			[&](scene_entity entity, transform_component& transform, model_renderer_component& model_component)
			{
				if (!model_component.visible) return;
				if (auto model = model_resources_.find(model_component.asset_path))
				{
					XMFLOAT4X4 object_world = make_world(transform);
					if (reflected)
					{
						// y=floor.heightの平面で同じPoseを反転し、画面外でも欠けない鏡像を作る。
						const XMMATRIX mirror = XMMatrixReflect(XMVectorSet(0, 1, 0, -floor.height));
						XMStoreFloat4x4(&object_world, XMLoadFloat4x4(&object_world) * mirror);
					}
					model_renderer_.render(commands, *model, object_world, view, projection,
						environment_lighting_.get(), settings.ibl_intensity, animation_system_.pose(entity),
						light.enabled ? XMFLOAT3{ light.direction[0], light.direction[1], light.direction[2] } : XMFLOAT3{},
						settings.mesh_shaders, reflected);
				}
			});
	};
	const float reflection_clear[4]{ 0, 0, 0, 0 };
	reflection_target_.begin(commands, reflection_clear);
	if (floor.enabled && floor.reflection_enabled && floor.planar_reflection) render_models(true);
	reflection_target_.end(commands);

	scene_target_.begin(commands, clear);
	if (floor.enabled) scene_geometry_.render_floor(commands, view, projection, floor.height,
		{ floor.color[0], floor.color[1], floor.color[2] }, reflection_target_.color_srv(),
		scene_target_.width(), scene_target_.height(),
		floor.reflection_enabled && floor.planar_reflection ? floor.strength : 0.0f);
	render_models(false);
	scene_target_.end(commands);
	ssr_.begin_frame(renderer_.frame_index());
#ifdef USE_IMGUI
	scene_display_target_.begin(commands, clear);
	ssr_.render(commands, scene_target_, projection, 0.0f, floor.max_distance, floor.thickness,
		static_cast<std::uint32_t>(floor.steps), post.exposure);
	scene_display_target_.end(commands);
	game_target_.begin(commands, clear);
	ssr_.render(commands, scene_target_, projection,
		floor.reflection_enabled && !floor.planar_reflection ? floor.strength : 0.0f,
		floor.max_distance, floor.thickness, static_cast<std::uint32_t>(floor.steps), post.exposure);
	game_target_.end(commands);
#else
	const auto back_buffer_rtv = renderer_.current_rtv();
	commands->OMSetRenderTargets(1, &back_buffer_rtv, FALSE, nullptr);
	ssr_.render(commands, scene_target_, projection,
		floor.reflection_enabled && !floor.planar_reflection ? floor.strength : 0.0f,
		floor.max_distance, floor.thickness, static_cast<std::uint32_t>(floor.steps), post.exposure);
#endif

#ifdef USE_IMGUI
	const auto back_buffer_rtv = renderer_.current_rtv();
	commands->OMSetRenderTargets(1, &back_buffer_rtv, FALSE, nullptr);
	imgui_.new_frame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImGuizmo::BeginFrame();
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	const float toolbar_height = 64.0f;
	const float side_width = (std::max)(320.0f, display.x * 0.28f);
	const float view_width = (std::max)(1.0f, display.x - side_width);
	const float view_height = (std::max)(1.0f, (display.y - toolbar_height) * 0.5f);
	const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(display.x, toolbar_height));
	ImGui::Begin("Toolbar", nullptr, fixed | ImGuiWindowFlags_NoTitleBar);
	if (ImGui::Button("New Scene")) new_scene();
	ImGui::SameLine(); if (ImGui::Button("Open Scene")) browse_scene(false);
	ImGui::SameLine(); if (ImGui::Button("Save Scene")) {
		if (current_scene_path_.empty()) browse_scene(true); else save_scene(current_scene_path_);
	}
	ImGui::SameLine(); if (ImGui::Button("Save As")) browse_scene(true);
	ImGui::SameLine(); if (ImGui::Button("Save Prefab") && scene_.renderer(selected_entity_)) browse_prefab(true);
	ImGui::SameLine(); if (ImGui::Button("Open Prefab")) browse_prefab(false);
	ImGui::SameLine(); if (ImGui::Button("Undo")) undo();
	ImGui::SameLine(); if (ImGui::Button("Redo")) redo();
	ImGui::SameLine();
	if (ImGui::Button("Open Model...")) browse_model();
	if (ImGui::Button("Duplicate") && scene_.renderer(selected_entity_))
	{
		push_undo();
		selected_entity_ = scene_.duplicate(selected_entity_);
		if (auto* transform = scene_.transform(selected_entity_)) transform->position[0] += 0.5f;
	}
	ImGui::SameLine(); if (ImGui::Button("Focus")) focus_selected();
	ImGui::SameLine(); if (ImGui::Button("Place on Floor") && scene_.renderer(selected_entity_))
	{
		auto* transform = scene_.transform(selected_entity_);
		auto* model_component = scene_.renderer(selected_entity_);
		if (transform && model_component)
			if (auto model = model_resources_.find(model_component->asset_path))
				transform->position[1] = floor.height - model->bounds_min().y * transform->scale3[1];
	}
	ImGui::SameLine();
	if (ImGui::Button("Move (W)")) gizmo_operation_ = 0;
	ImGui::SameLine(); if (ImGui::Button("Rotate (E)")) gizmo_operation_ = 1;
	ImGui::SameLine(); if (ImGui::Button("Scale (R)")) gizmo_operation_ = 2;
	ImGui::SameLine(); ImGui::Checkbox("Gizmo", &gizmo_enabled_);
	ImGui::SameLine(); if (ImGui::Button(play_mode_ ? "Pause" : "Play")) play_mode_ = !play_mode_;
	ImGui::SameLine(); if (!play_mode_ && ImGui::Button("Step")) step_requested_ = true;
	ImGui::SameLine(); if (ImGui::Button("Fullscreen")) toggle_fullscreen();
	ImGui::SameLine(); ImGui::TextDisabled("DX12 | FPS %.0f | %.2f ms", ImGui::GetIO().Framerate,
		1000.0f / (std::max)(ImGui::GetIO().Framerate, 0.01f));
	if (model_resources_.loading()) { ImGui::SameLine(); ImGui::TextDisabled("Loading..."); }
	ImGui::End();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
	ImGui::SetNextWindowPos(ImVec2(0, toolbar_height));
	ImGui::SetNextWindowSize(ImVec2(view_width, view_height));
	ImGui::Begin("Scene", nullptr, fixed | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	const ImVec2 available = ImGui::GetContentRegionAvail();
	const float texture_aspect = static_cast<float>(scene_target_.width()) / scene_target_.height();
	ImVec2 image_size = available;
	if (image_size.x / (std::max)(image_size.y, 1.0f) > texture_aspect) image_size.x = image_size.y * texture_aspect;
	else image_size.y = image_size.x / texture_aspect;
	const ImVec2 image_min = ImGui::GetCursorScreenPos();
	ImGui::Image(reinterpret_cast<ImTextureID>(scene_display_target_.srv().ptr), image_size);
	scene_hovered_ = ImGui::IsItemHovered();
	if (gizmo_enabled_ && scene_.transform(selected_entity_) && scene_.renderer(selected_entity_))
	{
		const ImGuizmo::OPERATION operation = gizmo_operation_ == 1 ? ImGuizmo::ROTATE :
			gizmo_operation_ == 2 ? ImGuizmo::SCALE : ImGuizmo::TRANSLATE;
		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
		ImGuizmo::SetRect(image_min.x, image_min.y, image_size.x, image_size.y);
		float snap[3]{ 0.25f, 0.25f, 0.25f };
		if (operation == ImGuizmo::ROTATE) snap[0] = snap[1] = snap[2] = 15.0f;
		else if (operation == ImGuizmo::SCALE) snap[0] = snap[1] = snap[2] = 0.1f;
		editor_snapshot gizmo_before = capture_snapshot();
		if (ImGuizmo::Manipulate(&view._11, &projection._11, operation,
			operation == ImGuizmo::SCALE ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
			&world._11, nullptr, ImGui::GetIO().KeyCtrl ? snap : nullptr))
		{
			transform_component& selected = *scene_.transform(selected_entity_);
			ImGuizmo::DecomposeMatrixToComponents(&world._11, selected.position, selected.rotation, selected.scale3);
			selected.scale = (selected.scale3[0] + selected.scale3[1] + selected.scale3[2]) / 3.0f;
		}
		const bool gizmo_using = ImGuizmo::IsUsing();
		if (gizmo_using && !gizmo_was_using_) push_undo(std::move(gizmo_before));
		gizmo_was_using_ = gizmo_using;
	}
	else gizmo_was_using_ = false;
	ImGui::End();

	ImGui::SetNextWindowPos(ImVec2(0, toolbar_height + view_height));
	ImGui::SetNextWindowSize(ImVec2(view_width, view_height));
	ImGui::Begin("Game", nullptr, fixed | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	const ImVec2 game_available = ImGui::GetContentRegionAvail();
	ImVec2 game_size = game_available;
	if (game_size.x / (std::max)(game_size.y, 1.0f) > texture_aspect) game_size.x = game_size.y * texture_aspect;
	else game_size.y = game_size.x / texture_aspect;
	ImGui::Image(reinterpret_cast<ImTextureID>(game_target_.srv().ptr), game_size);
	ImGui::End();
	ImGui::PopStyleVar();

	ImGui::SetNextWindowPos(ImVec2(view_width, toolbar_height));
	ImGui::SetNextWindowSize(ImVec2(side_width, view_height));
	ImGui::Begin("Hierarchy / Project", nullptr, fixed);
	if (ImGui::BeginTabBar("SceneBrowserTabs"))
	{
		if (ImGui::BeginTabItem("Hierarchy"))
		{
			for (const scene_entity entity : scene_.entities())
			{
				const name_component* name = scene_.name(entity);
				if (ImGui::Selectable(name ? name->value.c_str() : "Entity", selected_entity_ == entity))
					selected_entity_ = entity;
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Project"))
		{
			std::filesystem::path directory(asset_browser_directory_);
			std::error_code asset_error;
			if (!std::filesystem::exists(directory, asset_error)) directory = std::filesystem::current_path(asset_error);
			if (ImGui::Button("Up") && directory.has_parent_path())
			{
				asset_browser_directory_ = directory.parent_path().string();
				asset_navigation_time_ = ImGui::GetTime();
			}
			ImGui::SameLine();
			ImGui::TextWrapped("%s", directory.filename().string().c_str());
			ImGui::Separator();
			std::vector<std::filesystem::path> entries;
			for (std::filesystem::directory_iterator iterator(directory, asset_error), end;
				!asset_error && iterator != end; iterator.increment(asset_error))
			{
				std::string extension = iterator->path().extension().string();
				std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
				if (iterator->is_directory(asset_error) || extension == ".gltf" || extension == ".glb" || extension == ".jprefab")
					entries.push_back(iterator->path());
			}
			std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right)
			{
				const bool left_directory = std::filesystem::is_directory(left);
				const bool right_directory = std::filesystem::is_directory(right);
				return left_directory != right_directory ? left_directory : left.filename() < right.filename();
			});
			std::string requested_directory;
			std::string requested_model;
			std::string requested_prefab;
			for (std::size_t index = 0; index < entries.size(); ++index)
			{
				const auto& path = entries[index];
				const bool is_directory = std::filesystem::is_directory(path, asset_error);
				ImGui::PushID(static_cast<int>(index));
				const ImVec2 tile_min = ImGui::GetCursorScreenPos();
				const float tile_width = (std::max)(96.0f, ImGui::GetContentRegionAvail().x);
				ImGui::InvisibleButton("Asset", ImVec2(tile_width, 42.0f));
				const bool hovered = ImGui::IsItemHovered();
				const bool selected = selected_asset_path_ == path.string();
				ImDrawList* draw = ImGui::GetWindowDrawList();
				if (selected || hovered) draw->AddRectFilled(tile_min, ImVec2(tile_min.x + tile_width, tile_min.y + 42.0f),
					ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered), 4.0f);
				if (is_directory)
				{
					// Fluent System Iconsと同じ黄系配色のフォルダ形状。画像未ロード時も自然な見た目を保つ。
					draw->AddRectFilled(ImVec2(tile_min.x + 9, tile_min.y + 13), ImVec2(tile_min.x + 39, tile_min.y + 34), IM_COL32(244, 183, 52, 255), 3.0f);
					draw->AddRectFilled(ImVec2(tile_min.x + 12, tile_min.y + 8), ImVec2(tile_min.x + 27, tile_min.y + 17), IM_COL32(255, 211, 92, 255), 2.0f);
				}
				else
				{
					draw->AddRectFilled(ImVec2(tile_min.x + 12, tile_min.y + 10), ImVec2(tile_min.x + 36, tile_min.y + 34), IM_COL32(62, 139, 214, 255), 3.0f);
				}
				draw->AddText(ImVec2(tile_min.x + 48, tile_min.y + 12), ImGui::GetColorU32(ImGuiCol_Text), path.filename().string().c_str());
				if (ImGui::IsItemClicked()) selected_asset_path_ = path.string();
				if (hovered && ImGui::IsMouseDoubleClicked(0) && ImGui::GetTime() - asset_navigation_time_ > 0.35)
				{
					if (is_directory) requested_directory = path.string();
					else if (path.extension() == ".jprefab") requested_prefab = path.string();
					else requested_model = path.string();
				}
				ImGui::PopID();
			}
			if (!requested_directory.empty())
			{
				asset_browser_directory_ = requested_directory;
				asset_navigation_time_ = ImGui::GetTime();
			}
			if (!requested_model.empty()) request_model_load(requested_model);
			if (!requested_prefab.empty()) load_prefab(requested_prefab);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();

	ImGui::SetNextWindowPos(ImVec2(view_width, toolbar_height + view_height));
	ImGui::SetNextWindowSize(ImVec2(side_width, view_height));
	ImGui::Begin("Inspector", nullptr, fixed);
	const editor_snapshot inspector_before = capture_snapshot();
	// 【Component追加時のInspector入口】選択Entityからscene_.get<新Component>()を取得し、
	// 値を編集する各Widgetの直後でrecord_component_edit()を呼ぶとUndo履歴へ参加できる。
	// ゲームに必要な値をImGuiローカル変数だけへ保存しないこと（ReleaseではUI自体が無い）。
	bool inspector_history_recorded = false;
	const auto record_component_edit = [&]()
	{
		if (!inspector_history_recorded && ImGui::IsItemActivated())
		{
			push_undo(inspector_before);
			inspector_history_recorded = true;
		}
	};
	if (const auto* name = scene_.name(selected_entity_)) ImGui::Text("Entity %u: %s", selected_entity_, name->value.c_str());
	if (auto* selected_transform = scene_.transform(selected_entity_))
	{
		ImGui::Separator(); ImGui::TextUnformatted("Transform Component");
		ImGui::DragFloat3("Position", selected_transform->position, 0.01f);
		record_component_edit();
		ImGui::DragFloat3("Rotation", selected_transform->rotation, 0.25f);
		record_component_edit();
		if (ImGui::DragFloat3("Scale", selected_transform->scale3, 0.01f, 0.001f, 100.0f))
			selected_transform->scale = (selected_transform->scale3[0] + selected_transform->scale3[1] + selected_transform->scale3[2]) / 3.0f;
		record_component_edit();
	}
	if (auto* model_component = scene_.renderer(selected_entity_))
	{
		ImGui::Separator(); ImGui::TextUnformatted("Model Renderer Component");
		ImGui::Checkbox("Visible", &model_component->visible);
		record_component_edit();
		ImGui::TextWrapped("%s", model_component->asset_path.c_str());
		auto model = model_resources_.find(model_component->asset_path);
		if (model)
		{
			ImGui::Text("Meshes: %d  Materials: %d", static_cast<int>(model->meshes().size()), static_cast<int>(model->materials().size()));
			ImGui::Text("Textures: %d / %d", static_cast<int>(model->loaded_texture_count()), static_cast<int>(model->textures().size()));
			if (auto* animation = scene_.animation(selected_entity_); animation && !model->animations().empty())
			{
				ImGui::Separator(); ImGui::TextUnformatted("Animation Component");
				animation->clip = (std::clamp)(animation->clip, 0, static_cast<int>(model->animations().size()) - 1);
				auto* controller = scene_.animation_controller(selected_entity_);
				if (controller && !controller->states.empty())
				{
					controller->requested_state = (std::clamp)(controller->requested_state, 0,
						static_cast<int>(controller->states.size()) - 1);
					if (ImGui::BeginCombo("State", controller->states[controller->requested_state].name.c_str()))
					{
						for (int index = 0; index < static_cast<int>(controller->states.size()); ++index)
							if (ImGui::Selectable(controller->states[index].name.c_str(), index == controller->requested_state))
							{
								if (!inspector_history_recorded) { push_undo(inspector_before); inspector_history_recorded = true; }
								controller->requested_state = index;
							}
						ImGui::EndCombo();
					}
					ImGui::Checkbox("Controller Enabled", &controller->enabled);
					record_component_edit();
				}
				else
				{
					const std::string& current_name = model->animations()[animation->clip].name;
					if (ImGui::BeginCombo("Clip", current_name.empty() ? "Unnamed Clip" : current_name.c_str()))
					{
						for (int index = 0; index < static_cast<int>(model->animations().size()); ++index)
						{
							const std::string fallback = "Clip " + std::to_string(index);
							const std::string& clip_name = model->animations()[index].name;
							if (ImGui::Selectable(clip_name.empty() ? fallback.c_str() : clip_name.c_str(), index == animation->clip))
							{ if (!inspector_history_recorded) { push_undo(inspector_before); inspector_history_recorded = true; }
							  animation->clip = index; animation->time = 0.0f; }
						}
						ImGui::EndCombo();
					}
				}
				ImGui::Checkbox("Playing", &animation->playing);
				record_component_edit();
				ImGui::SameLine(); ImGui::Checkbox("Loop", &animation->loop);
				record_component_edit();
				ImGui::SliderFloat("Speed", &animation->speed, 0.0f, 3.0f, "%.2fx");
				record_component_edit();
				const float duration = (std::max)(model->animations()[animation->clip].duration, 0.001f);
				ImGui::SliderFloat("Time", &animation->time, 0.0f, duration, "%.3f sec");
				record_component_edit();
			}
		}
	}
	if (auto* selected_camera = scene_.camera(selected_entity_))
	{
		ImGui::Separator(); ImGui::TextUnformatted("Camera Component");
		ImGui::DragFloat3("Target", selected_camera->target, 0.01f);
		record_component_edit();
		ImGui::SliderFloat("Distance", &selected_camera->distance, 0.5f, 50.0f);
		record_component_edit();
		ImGui::SliderFloat("FOV", &selected_camera->fov_degrees, 20.0f, 100.0f);
		record_component_edit();
		ImGui::DragFloat("Yaw", &selected_camera->yaw_degrees, 0.25f);
		record_component_edit();
		ImGui::DragFloat("Pitch", &selected_camera->pitch_degrees, 0.25f);
		record_component_edit();
	}
	if (auto* selected_floor = scene_.reflection_floor(selected_entity_))
	{
		ImGui::Separator(); ImGui::TextUnformatted("Reflection Floor Component");
		ImGui::Checkbox("Visible", &selected_floor->enabled);
		record_component_edit();
		ImGui::Checkbox("Reflection Enabled", &selected_floor->reflection_enabled);
		record_component_edit();
		ImGui::Checkbox("Stable Planar Reflection", &selected_floor->planar_reflection);
		record_component_edit();
		ImGui::TextDisabled(selected_floor->planar_reflection ?
			"Planar mirror: stable and complete" : "SSR: screen-space and may clip off-screen objects");
		ImGui::DragFloat("Floor Height", &selected_floor->height, 0.01f);
		record_component_edit();
		ImGui::ColorEdit3("Floor Color", selected_floor->color);
		record_component_edit();
		ImGui::SliderFloat("Strength", &selected_floor->strength, 0.0f, 1.5f);
		record_component_edit();
		if (!selected_floor->planar_reflection)
		{
			ImGui::SliderFloat("Ray Distance", &selected_floor->max_distance, 1.0f, 50.0f);
			record_component_edit();
			ImGui::SliderFloat("Thickness", &selected_floor->thickness, 0.01f, 1.0f);
			record_component_edit();
			ImGui::SliderInt("Steps", &selected_floor->steps, 8, 128);
			record_component_edit();
		}
	}
	if (auto* selected_light = scene_.directional_light(selected_entity_))
	{
		ImGui::Separator(); ImGui::TextUnformatted("Directional Light Component");
		ImGui::Checkbox("Enabled", &selected_light->enabled);
		record_component_edit();
		ImGui::DragFloat3("Direction", selected_light->direction, 0.01f, -1.0f, 1.0f);
		record_component_edit();
	}
	if (auto* selected_post = scene_.post_process(selected_entity_))
	{
		ImGui::Separator(); ImGui::TextUnformatted("Post Process Component");
		ImGui::SliderFloat("Exposure", &selected_post->exposure, 0.1f, 4.0f);
		record_component_edit();
	}
	if (auto* selected_settings = scene_.rendering_settings(selected_entity_))
	{
		ImGui::Separator(); ImGui::TextUnformatted("Rendering Settings Component");
		ImGui::Checkbox("VSync", &selected_settings->vsync);
		record_component_edit();
		if (model_renderer_.mesh_shader_available())
		{
			ImGui::Checkbox("Mesh Shaders", &selected_settings->mesh_shaders);
			record_component_edit();
		}
		else ImGui::TextDisabled("Mesh Shaders: GPU / driver unsupported");
		ImGui::SliderFloat("IBL Intensity", &selected_settings->ibl_intensity, 0.0f, 2.0f);
		record_component_edit();
	}
	if (!model_resources_.last_error().empty()) ImGui::TextWrapped("Load error: %s", model_resources_.last_error().c_str());
	if (!editor_error_.empty()) ImGui::TextWrapped("Scene error: %s", editor_error_.c_str());
	ImGui::End();
	ImGui::Render();
	imgui_.render(commands);
#endif
	renderer_.end_frame(settings.vsync);
}

void native_preview12::shutdown()
{
	model_resources_.stop();
	renderer_.wait_idle();
#ifdef USE_IMGUI
	imgui_.shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif
	animation_system_.clear();
	scene_.clear();
}

LRESULT native_preview12::handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
#ifdef USE_IMGUI
	ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam);
#endif
	const bool fullscreen_shortcut =
		(message == WM_KEYDOWN && wparam == VK_F11) ||
		(message == WM_SYSKEYDOWN && wparam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000));
	if (fullscreen_shortcut)
	{
		if ((lparam & (1LL << 30)) == 0) toggle_fullscreen();
		return 0;
	}
	switch (message)
	{
	case WM_SIZE:
		if (wparam != SIZE_MINIMIZED) {
			pending_width_.store(LOWORD(lparam), std::memory_order_release);
			pending_height_.store(HIWORD(lparam), std::memory_order_release);
		} return 0;
	case WM_RBUTTONDOWN:
		if (scene_hovered_) { right_dragging_ = true; last_mouse_ = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) }; SetCapture(hwnd); } return 0;
	case WM_RBUTTONUP:
		right_dragging_ = false; ReleaseCapture(); return 0;
	case WM_MOUSEMOVE:
		if (right_dragging_) {
			const POINT current{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
			if (auto* camera = scene_.camera(camera_entity_))
			{
				camera->yaw_degrees += (current.x - last_mouse_.x) * camera->orbit_sensitivity;
				camera->pitch_degrees = (std::clamp)(camera->pitch_degrees +
					(current.y - last_mouse_.y) * camera->orbit_sensitivity, -77.0f, 77.0f);
			}
			last_mouse_ = current;
		} return 0;
	case WM_MOUSEWHEEL:
		if (scene_hovered_)
			if (auto* camera = scene_.camera(camera_entity_)) camera->distance = (std::clamp)(camera->distance -
				GET_WHEEL_DELTA_WPARAM(wparam) / 120.0f * camera->wheel_zoom_speed, 0.5f, 50.0f);
		return 0;
	case WM_DESTROY: PostQuitMessage(0); return 0;
	case WM_KEYDOWN:
		if ((GetKeyState(VK_CONTROL) & 0x8000) && wparam == 'Z') { undo(); return 0; }
		if ((GetKeyState(VK_CONTROL) & 0x8000) && wparam == 'Y') { redo(); return 0; }
		if ((GetKeyState(VK_CONTROL) & 0x8000) && wparam == 'S')
		{
			if (GetKeyState(VK_SHIFT) & 0x8000) browse_scene(true);
			else if (current_scene_path_.empty()) browse_scene(true); else save_scene(current_scene_path_);
			return 0;
		}
		if ((GetKeyState(VK_CONTROL) & 0x8000) && wparam == 'D' && scene_.renderer(selected_entity_))
		{
			push_undo();
			selected_entity_ = scene_.duplicate(selected_entity_);
			if (auto* transform = scene_.transform(selected_entity_)) transform->position[0] += 0.5f;
			return 0;
		}
		if (wparam == VK_DELETE && scene_.renderer(selected_entity_))
		{
			push_undo();
			scene_.destroy(selected_entity_);
			selected_entity_ = null_scene_entity;
			return 0;
		}
		if (wparam == VK_SPACE) { play_mode_ = !play_mode_; return 0; }
		if (wparam == 'F') { focus_selected(); return 0; }
		if (wparam == 'W') { gizmo_operation_ = 0; return 0; }
		if (wparam == 'E') { gizmo_operation_ = 1; return 0; }
		if (wparam == 'R') { gizmo_operation_ = 2; return 0; }
		if (wparam == VK_ESCAPE) {
			if (borderless_fullscreen_) toggle_fullscreen(); else PostMessageW(hwnd, WM_CLOSE, 0, 0);
			return 0;
		} break;
	}
	return DefWindowProcW(hwnd, message, wparam, lparam);
}
