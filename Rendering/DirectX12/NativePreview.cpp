#include "NativePreview.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <commdlg.h>
#include <fstream>
#include <unordered_set>
#include <windowsx.h>
#include "../tinygltf-release/json.hpp"

#ifdef USE_IMGUI
#include "../imgui/imgui.h"
#include "EditorUiHelp.h"
#include "../imgui/imgui_internal.h"
#include "../imgui/imgui_impl_win32.h"
#include "../ThirdParty/ImGuizmo/ImGuizmo.h"
extern ImWchar glyphRangesJapanese[];
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
#endif

using namespace DirectX;

namespace
{
#ifdef USE_IMGUI
	// 同梱ImGuiはBeginDisabled導入前の版。無効化と薄い表示を対で積み、同じWindow内で戻す。
	void begin_editor_disabled(bool disabled)
	{
		ImGui::PushItemFlag(ImGuiItemFlags_Disabled, disabled);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (disabled ? 0.5f : 1.0f));
	}
	void end_editor_disabled() { ImGui::PopStyleVar(); ImGui::PopItemFlag(); }
#endif
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
		const UINT half_height = window_height > 136 ? (window_height - 40) / 2 - 48 : 1u;
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
	if (!sun_shadow_.initialize(renderer_.device())) { startup_error_code_ = 19; return false; }
	if (!scene_geometry_.initialize(renderer_.device())) { startup_error_code_ = 21; return false; }
	const auto [target_width, target_height] = scene_target_size(width, height);
	if (!scene_target_.initialize(renderer_.device(), target_width, target_height)) { startup_error_code_ = 13; return false; }
	if (!reflection_target_.initialize(renderer_.device(), target_width, target_height)) { startup_error_code_ = 22; return false; }
	if (!scene_display_target_.initialize(renderer_.device(), target_width, target_height)) { startup_error_code_ = 20; return false; }
	if (!game_target_.initialize(renderer_.device(), target_width, target_height)) { startup_error_code_ = 18; return false; }
	if (!ssr_.initialize(renderer_.device())) { startup_error_code_ = 14; return false; }
	model_resources_.initialize(renderer_.device());
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
	// 初期モデルは確認用Asset。照明は画像不要の手続き環境光から始める。
	request_model_load(initial_model_path.string(), false);
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
	// 見た目の床と物理床を同じEntityへ置き、ゲーム側で別の高さになる事故を防ぐ。
	auto& floor_transform = scene_.add_transform(reflection_floor_entity_);
	floor_transform.position[1] = floor.height - 0.05f;
	auto& floor_collider = scene_.add_collider(reflection_floor_entity_);
	floor_collider.half_extents[0] = 50.0f;
	floor_collider.half_extents[1] = 0.05f;
	floor_collider.half_extents[2] = 50.0f;
	auto& floor_body = scene_.add_rigid_body(reflection_floor_entity_);
	floor_body.type = rigid_body_type::static_body;
	post_process_entity_ = scene_.create_entity("Post Process Volume");
	 scene_.add_post_process(post_process_entity_).exposure = 1.15f;
	rendering_settings_entity_ = scene_.create_entity("Rendering Settings");
	scene_.add_rendering_settings(rendering_settings_entity_);
}

void native_preview12::load_optional_environment()
{
	const auto* settings = scene_.rendering_settings(rendering_settings_entity_);
	if (!settings || settings->ibl_intensity <= 0 || environment_load_attempted_) return;
	environment_load_attempted_ = true;
	// 環境画像は任意の追加照明。既定の起動と手続き見本は、このDDSが無くても動く。
	// 読込みは描画命令の記録前に一度だけ行う。失敗しても毎フレーム再試行して重くしない。
	for (const auto& path : { std::filesystem::path(L"resources/environments/hilly_terrain_01_puresky"),
		std::filesystem::path(L"Rendering/resources/environments/hilly_terrain_01_puresky") })
	{
		if (!std::filesystem::exists(path)) continue;
		auto environment = std::make_unique<environment_lighting12>();
		if (environment->initialize(renderer_.device(), path))
		{
			environment_lighting_ = std::move(environment);
			return;
		}
	}
	editor_error_ = "Optional IBL could not be loaded; procedural lighting remains active.";
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

void native_preview12::apply_map_placement(scene_entity entity, const float position[3], float rotation_y,
	float scale, float grid_size, bool add_box_collider)
{
	if (entity == null_scene_entity) return;
	auto* transform = scene_.transform(entity);
	if (!transform) return;
	transform->position[0] = position[0];
	transform->position[1] = position[1];
	transform->position[2] = position[2];
	transform->rotation[1] = rotation_y;
	transform->scale = (std::max)(scale, 0.001f);
	transform->scale3[0] = transform->scale3[1] = transform->scale3[2] = transform->scale;

	auto& map_object = scene_.add_map_object(entity);
	map_object.is_static = true;
	map_object.snap_to_grid = true;
	map_object.grid_size = (std::max)(grid_size, 0.01f);

	if (const auto* renderer = scene_.renderer(entity))
	{
		if (const auto model = model_resources_.find(renderer->asset_path))
		{
			// Assetの原点が足元とは限らないため、Hit位置へBounds下端を合わせる。
			*transform=map_editor_system::model_transform(position,model->bounds_min().y,rotation_y,scale);
			if (add_box_collider)
			{
				auto& collider = scene_.add_collider(entity);
				const XMFLOAT3 minimum = model->bounds_min();
				const XMFLOAT3 maximum = model->bounds_max();
				collider.shape = collider_shape::box;
				collider.center[0] = (minimum.x + maximum.x) * 0.5f;
				collider.center[1] = (minimum.y + maximum.y) * 0.5f;
				collider.center[2] = (minimum.z + maximum.z) * 0.5f;
				collider.half_extents[0] = (std::max)((maximum.x - minimum.x) * 0.5f, 0.01f);
				collider.half_extents[1] = (std::max)((maximum.y - minimum.y) * 0.5f, 0.01f);
				collider.half_extents[2] = (std::max)((maximum.z - minimum.z) * 0.5f, 0.01f);
				auto& body = scene_.add_rigid_body(entity);
				body.type = rigid_body_type::static_body;
			}
		}
	}
	selected_entity_ = entity;
}

void native_preview12::request_map_placement(const std::string& asset, const float position[3])
{
	if (walk_test_) return;
	if (asset.empty() || std::filesystem::is_directory(std::filesystem::path(asset))) return;
	const std::filesystem::path path(asset);
	std::string extension = path.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	const map_editor_settings settings = map_editor_system_.settings();
	if (extension == ".jprefab")
	{
		if (load_prefab(asset))
		{
			// Prefabの縦横比を保持し、配置設定は倍率として掛ける。全軸を同じ値で上書きすると
			// 薄い床や細長い壁が立方体になり、保存済みColliderとも大きさが食い違ってしまう。
			const transform_component original = *scene_.transform(selected_entity_);
			const auto* original_map = scene_.map_object(selected_entity_);
			const bool is_static = original_map ? original_map->is_static : true;
			apply_map_placement(selected_entity_, position, settings.rotation_y_degrees,
				settings.uniform_scale, settings.grid_size, settings.add_box_collider);
			auto& placed = *scene_.transform(selected_entity_);
			for (int axis = 0; axis < 3; ++axis) placed.scale3[axis] *= original.scale3[axis];
			placed.scale = (placed.scale3[0] + placed.scale3[1] + placed.scale3[2]) / 3.0f;
			for (int axis = 0; axis < 3; ++axis) placed.rotation[axis] += original.rotation[axis];
			scene_.map_object(selected_entity_)->is_static = is_static;
		}
		return;
	}
	if (extension != ".gltf" && extension != ".glb") return;

	const std::string normalized = normalized_asset_path(asset);
	if (model_resources_.find(normalized))
	{
		push_undo();
		const scene_entity entity = create_model_entity(normalized);
		apply_map_placement(entity, position, settings.rotation_y_degrees,
			settings.uniform_scale, settings.grid_size, settings.add_box_collider);
		return;
	}
	pending_map_placements_.push_back({ normalized, {position[0], position[1], position[2]},
		settings.rotation_y_degrees, settings.uniform_scale, settings.grid_size, settings.add_box_collider });
	request_model_load(normalized, false);
}

int native_preview12::run()
{
	if (!initialize()) return startup_error_code_;
	MSG message{};
	auto previous = std::chrono::steady_clock::now();
	while (message.message != WM_QUIT)
	{
		// Present待ちの渋滞を作らず、入力を表示可能なフレームの直前で取得する。
		renderer_.wait_for_frame_latency();
		// 入力は1フレームにまとめて処理する。1件ごとにcontinueすると、マウス移動中は
		// 入力処理と画面待ちだけを繰り返し、update/renderへ到達しにくくなる。
		// 上限を設け、入力が届き続ける場合も描画時間を確保する。残りは次フレームで処理する。
		constexpr unsigned int max_messages_per_frame = 256;
		for (unsigned int count = 0; count < max_messages_per_frame &&
			PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count)
		{
			if (message.message == WM_QUIT) break;
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		// 終了要求を受けたフレームでは更新・描画を行わず、ループ後のshutdownへ進む。
		if (message.message == WM_QUIT) break;
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
		const rendering_settings_component* frame_settings = scene_.rendering_settings(rendering_settings_entity_);
		if (frame_settings)
		{
			// VSyncとFPS Limitを同時に待つと周期がずれるため、前景のVSync中はLimiterを止める。
			const bool background = GetForegroundWindow() != hwnd_;
			const int requested_limit = background ? frame_settings->background_frame_rate :
				(frame_settings->vsync ? 0 : frame_settings->frame_rate_limit);
			frame_limiter_.wait(static_cast<std::uint32_t>((std::max)(requested_limit, 0)));
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
	load_optional_environment();
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
	const float scene_delta = (walk_test_ && player_paused_) ? 0.0f : (play_mode_ ? elapsed_time : (step_requested_ ? 1.0f / 60.0f : 0.0f));
	// 新しいゲームSystemは原則この付近から呼ぶ。Pause中に動かすEditor処理にはelapsed_time、
	// ゲーム時間に従う処理にはscene_deltaを渡して、Pause/Stepの挙動を統一すること。
	animation_system_.update(scene_, model_resources_, scene_delta);
	if (walk_test_)
	{
		playtest_input input{};
		const bool active = !player_paused_ && GetForegroundWindow()==hwnd_ && (!first_person_active_ || mouse_look_);
		auto held = [active](int key) { return active && (GetAsyncKeyState(key)&0x8000)!=0; };
		input.right = float(held('D') || held(VK_RIGHT))-float(held('A') || held(VK_LEFT));
		input.forward = float(held('W') || held(VK_UP))-float(held('S') || held(VK_DOWN));
		input.jump_pressed = held(VK_SPACE) && !walk_jump_held_;
		input.reset_pressed = held('R') && !walk_reset_held_;
        input.sprint_held = held(VK_SHIFT);
		walk_jump_held_ = held(VK_SPACE); walk_reset_held_ = held('R');
		if (const auto* camera = scene_.camera(camera_entity_)) input.camera_yaw_degrees=camera->yaw_degrees;
		playtest_system_.update_input(scene_,input);
        update_player_cursor();
	}
	if (auto* floor = scene_.reflection_floor(reflection_floor_entity_))
	{
		if (auto* floor_transform = scene_.transform(reflection_floor_entity_))
			floor_transform->position[1] = floor->height - 0.05f;
		// 非表示の確認用床が落下テストを止めないよう、物理側の有効状態もそろえる。
		if (auto* collider = scene_.collider(reflection_floor_entity_)) collider->enabled=floor->enabled;
	}
	// Physicsは固定時間Stepを内部で管理する。ゲームのPause中はscene_delta=0なので停止する。
	physics_system_.update(scene_, scene_delta);
	if (walk_test_)
	{
		playtest_system_.after_physics(scene_,physics_system_);
		scene_.for_each<transform_component,playtest_controller_component>(
			[&](scene_entity entity,const transform_component& transform,const playtest_controller_component& test)
			{
				if (!test.checkpoint_valid) return;
				if (auto* camera = scene_.camera(camera_entity_))
				{
					if (first_person_active_)
					{
						camera->target[0]=transform.position[0];
						const float desired=transform.position[1]+(std::clamp)(test.eye_height,0.2f,3.0f);
						const auto* controller=scene_.character_controller(entity);
						// 足元の段差補正は即時、視点だけ滑らかに追従する。空中や復帰時は遅延させない。
						if (!controller || !controller->grounded || std::abs(desired-player_eye_y_)>1.0f) player_eye_y_=desired;
						else player_eye_y_+=(desired-player_eye_y_)*(1.0f-std::exp(-18.0f*scene_delta));
						camera->target[1]=player_eye_y_;
						camera->target[2]=transform.position[2];
						return;
					}
					camera->target[0]=transform.position[0]; camera->target[1]=transform.position[1]+1.0f;
					camera->target[2]=transform.position[2]; camera->distance=(std::clamp)(test.camera_distance,3.0f,20.0f);
					// 注視点から背後へ調べ、壁より手前へカメラを寄せる。プレイヤー自身は検索対象から除く。
					const float yaw=XMConvertToRadians(camera->yaw_degrees), pitch=XMConvertToRadians(camera->pitch_degrees);
					const float direction[3]{std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch)};
					physics_raycast_hit hit{};
					if (physics_system_.raycast(scene_,camera->target,direction,camera->distance,0xffffffffu,hit,entity))
						camera->distance=(std::max)(0.4f,hit.distance-0.25f);
				}
			});
	}
	step_requested_ = false;
}

void native_preview12::request_model_load(const std::string& filename, bool add_placement_when_ready)
{
	if (filename.empty()) return;
	if (auto loaded = model_resources_.find(filename))
	{
		if (add_placement_when_ready)
		{
			push_undo();
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
			push_undo();
			selected_entity_ = create_model_entity(filename);
		}
		for (auto placement = pending_map_placements_.begin(); placement != pending_map_placements_.end();)
		{
			if (normalized_asset_path(placement->asset) != normalized_asset_path(filename))
			{
				++placement;
				continue;
			}
			push_undo();
			const scene_entity entity = create_model_entity(filename);
			apply_map_placement(entity, placement->position, placement->rotation_y,
				placement->scale, placement->grid_size, placement->add_box_collider);
			placement = pending_map_placements_.erase(placement);
		}
	}
	if (!model_resources_.loading())
	{
		// 失敗した素材を待ち続けてPlay開始を塞がない。正常な配置は上の完了処理で既に取り出している。
		pending_map_placements_.erase(std::remove_if(pending_map_placements_.begin(),pending_map_placements_.end(),
			[&](const auto& placement) { return !model_resources_.find(placement.asset); }),pending_map_placements_.end());
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
	if (walk_test_) return;
	if (model_resources_.loading()) return;
	push_undo();
	renderer_.wait_idle();
	scene_.clear();
	animation_system_.clear();
	physics_system_.clear();
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
	physics_system_.clear();
}

void native_preview12::start_walk_test()
{
	if (walk_test_ || model_resources_.loading() || !pending_map_placements_.empty()) return;
	// 物理で動く箱・プレイヤー・カメラをまとめて退避し、停止時に編集状態へ戻す。
	auto snapshot=std::make_unique<editor_snapshot>(capture_snapshot());
	if (!playtest_system_.begin(scene_)) { editor_error_=u8"有効なCollider・Character Controller・Playtest Controllerを持つ操作用Entityを1つ用意してください。"; return; }
	walk_test_snapshot_=std::move(snapshot);
	walk_previous_play_=play_mode_; walk_previous_gizmo_=gizmo_enabled_;
	walk_test_=true; play_mode_=true; gizmo_enabled_=false;
	player_paused_=false;
	walk_jump_held_=walk_reset_held_=false;
	map_editor_system_.settings().enabled=false;
	physics_system_.clear();
	if (auto* camera=scene_.camera(camera_entity_)) { camera->yaw_degrees=0; camera->pitch_degrees=28; camera->fov_degrees=65; }
	scene_.for_each<transform_component,playtest_controller_component>([&](scene_entity entity,const transform_component& transform,const playtest_controller_component& test)
	{
		if (!test.checkpoint_valid) return;
		active_player_=entity; first_person_active_=test.first_person;
		player_eye_y_=transform.position[1]+test.eye_height;
		mouse_sensitivity_=(std::clamp)(test.mouse_sensitivity,0.01f,0.5f);
		if (first_person_active_)
		{
			auto* camera=scene_.camera(camera_entity_);
			// Orbitカメラのyawは右回りの視線と符号が逆なので、配置のY回転を反転して開始する。
			camera->yaw_degrees=-transform.rotation[1]; camera->pitch_degrees=0;
			camera->fov_degrees=(std::clamp)(test.field_of_view,45.0f,110.0f);
		}
	});
	if (first_person_active_)
	{
		RAWINPUTDEVICE mouse{0x01,0x02,0,hwnd_};
		if (!RegisterRawInputDevices(&mouse,1,sizeof(mouse)))
		{ stop_walk_test(); editor_error_=u8"マウス入力の開始に失敗しました。"; return; }
		mouse_look_=true;
		update_player_cursor();
	}
}

void native_preview12::stop_walk_test()
{
	if (!walk_test_) return;
	walk_test_=false;
	if (first_person_active_) { RAWINPUTDEVICE mouse{0x01,0x02,RIDEV_REMOVE,nullptr}; RegisterRawInputDevices(&mouse,1,sizeof(mouse)); }
	first_person_active_=false; mouse_look_=false; active_player_=null_scene_entity;
	player_paused_=false;
	ClipCursor(nullptr); SetCursor(LoadCursor(nullptr,IDC_ARROW));
	if (walk_test_snapshot_) restore_snapshot(*walk_test_snapshot_);
	walk_test_snapshot_.reset();
	play_mode_=walk_previous_play_; gizmo_enabled_=walk_previous_gizmo_;
	right_dragging_=false; ReleaseCapture();
}

void native_preview12::toggle_play()
{
	if (walk_test_) { set_player_paused(!player_paused_); return; }
	bool has_player=false;
	scene_.for_each<playtest_controller_component>([&](scene_entity,const auto&) { has_player=true; });
	// PlayerがあるSceneでは編集時間の切替だけで終わらず、その配置から操作を開始する。
	if (has_player) start_walk_test(); else play_mode_=!play_mode_;
}

void native_preview12::update_player_cursor()
{
	if (!first_person_active_ || player_paused_ || !mouse_look_ || GetForegroundWindow()!=hwnd_) return;
	RECT area{}; GetClientRect(hwnd_,&area);
	POINT a{area.left,area.top},b{area.right,area.bottom}; ClientToScreen(hwnd_,&a); ClientToScreen(hwnd_,&b);
	RECT screen{a.x,a.y,b.x,b.y}; ClipCursor(&screen); SetCursor(nullptr);
}

void native_preview12::set_player_paused(bool paused)
{
	player_paused_=paused;
	mouse_look_=first_person_active_ && !paused;
	if (paused) { right_dragging_=false; ReleaseCapture(); }
	if (paused) { ClipCursor(nullptr); SetCursor(LoadCursor(nullptr,IDC_ARROW)); }
	else update_player_cursor();
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

// Prefabは単体EntityをJSONへ保存する。Componentを増やすときはこの保存処理とload_prefabを対で更新する。
// Scene保存は別関数なので、save_scene/load_sceneにも同じ項目が必要。手順はDocumentation/IMPLEMENTATION_MAP.mdを参照。
bool native_preview12::save_prefab(const std::string& filename, scene_entity entity)
{
	if (walk_test_) return false;
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
	if (const auto* tag = scene_.gameplay_tag(entity)) root["entity"]["gameplay_tag"] = tag->value;
	if (const auto* test=scene_.get<playtest_controller_component>(entity))
		root["entity"]["playtest_controller"]={{"move_speed",test->move_speed},{"sprint_multiplier",test->sprint_multiplier},{"respawn_height",test->respawn_height},{"camera_distance",test->camera_distance},{"first_person",test->first_person},{"eye_height",test->eye_height},{"mouse_sensitivity",test->mouse_sensitivity},{"field_of_view",test->field_of_view}};
	if (const auto* controller = scene_.animation_controller(entity))
	{
		root["entity"]["animation_controller"] = {
			{"enabled", controller->enabled}, {"current_state", controller->current_state},
			{"requested_state", controller->requested_state}, {"states", json::array()} };
		for (const auto& state : controller->states)
			root["entity"]["animation_controller"]["states"].push_back({
				{"name", state.name}, {"clip", state.clip}, {"speed", state.speed}, {"loop", state.loop} });
	}
	if (const auto* map_object = scene_.map_object(entity))
		root["entity"]["map_object"] = {{"static", map_object->is_static},
			{"snap_to_grid", map_object->snap_to_grid}, {"grid_size", map_object->grid_size},
			{"layer", map_object->layer}};
	if (const auto* collider = scene_.collider(entity))
		root["entity"]["collider"] = {{"shape", static_cast<int>(collider->shape)},
			{"center", {collider->center[0], collider->center[1], collider->center[2]}},
			{"half_extents", {collider->half_extents[0], collider->half_extents[1], collider->half_extents[2]}},
			{"radius", collider->radius}, {"height", collider->height}, {"layer", collider->layer},
			{"mask", collider->collision_mask}, {"trigger", collider->trigger}, {"enabled", collider->enabled}};
	if (const auto* body = scene_.rigid_body(entity))
		root["entity"]["rigid_body"] = {{"type", static_cast<int>(body->type)}, {"mass", body->mass},
			{"velocity", {body->velocity[0], body->velocity[1], body->velocity[2]}},
			{"gravity_scale", body->gravity_scale}, {"restitution", body->restitution}, {"friction", body->friction},
			{"freeze_position", {body->freeze_position[0], body->freeze_position[1], body->freeze_position[2]}},
			{"enabled", body->enabled}};
	if (const auto* controller = scene_.character_controller(entity))
		root["entity"]["character_controller"] = {{"gravity", controller->gravity},
			{"jump_speed", controller->jump_speed}, {"slope_limit", controller->slope_limit_degrees},
			{"step_height", controller->step_height}, {"enabled", controller->enabled}};
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
	if (walk_test_) return false;
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
		if (value.contains("gameplay_tag"))
			scene_.add_gameplay_tag(entity).value = value.value("gameplay_tag", "Untagged");
		if (value.contains("playtest_controller"))
		{
			const auto& source=value["playtest_controller"];
			auto& test=scene_.add<playtest_controller_component>(entity);
			test.move_speed=source.value("move_speed",test.move_speed);
            test.sprint_multiplier=source.value("sprint_multiplier",test.sprint_multiplier);
			test.respawn_height=source.value("respawn_height",test.respawn_height);
			test.camera_distance=source.value("camera_distance",test.camera_distance);
            test.first_person=source.value("first_person",test.first_person);
            test.eye_height=source.value("eye_height",test.eye_height);
            test.mouse_sensitivity=source.value("mouse_sensitivity",test.mouse_sensitivity);
            test.field_of_view=source.value("field_of_view",test.field_of_view);
		}
		if (value.contains("map_object"))
		{
			const json& source = value["map_object"];
			auto& map_object = scene_.add_map_object(entity);
			map_object.is_static = source.value("static", map_object.is_static);
			map_object.snap_to_grid = source.value("snap_to_grid", map_object.snap_to_grid);
			map_object.grid_size = (std::max)(0.01f, source.value("grid_size", map_object.grid_size));
			map_object.layer = source.value("layer", map_object.layer);
		}
		if (value.contains("collider"))
		{
			const json& source = value["collider"];
			auto& collider = scene_.add_collider(entity);
			collider.shape = static_cast<collider_shape>((std::clamp)(source.value("shape", 0), 0, 2));
			read3(source.value("center", json::array()), collider.center);
			read3(source.value("half_extents", json::array()), collider.half_extents);
			collider.radius = source.value("radius", collider.radius); collider.height = source.value("height", collider.height);
			collider.layer = source.value("layer", collider.layer); collider.collision_mask = source.value("mask", collider.collision_mask);
			collider.trigger = source.value("trigger", collider.trigger); collider.enabled = source.value("enabled", collider.enabled);
		}
		if (value.contains("rigid_body"))
		{
			const json& source = value["rigid_body"];
			auto& body = scene_.add_rigid_body(entity);
			body.type = static_cast<rigid_body_type>((std::clamp)(source.value("type", 2), 0, 2));
			body.mass = source.value("mass", body.mass); read3(source.value("velocity", json::array()), body.velocity);
			body.gravity_scale = source.value("gravity_scale", body.gravity_scale);
			body.restitution = source.value("restitution", body.restitution); body.friction = source.value("friction", body.friction);
			if (const json freeze = source.value("freeze_position", json::array()); freeze.is_array())
				for (std::size_t i = 0; i < 3 && i < freeze.size(); ++i) body.freeze_position[i] = freeze[i].get<bool>();
			body.enabled = source.value("enabled", body.enabled);
		}
		if (value.contains("character_controller"))
		{
			const json& source = value["character_controller"];
			auto& controller = scene_.add_character_controller(entity);
			controller.gravity = source.value("gravity", controller.gravity); controller.jump_speed = source.value("jump_speed", controller.jump_speed);
			controller.slope_limit_degrees = source.value("slope_limit", controller.slope_limit_degrees);
			controller.step_height = source.value("step_height", controller.step_height); controller.enabled = source.value("enabled", controller.enabled);
		}
		scene_.add_prefab(entity).source_path = filename;
		request_model_load(asset, false);
		selected_entity_ = entity;
		return true;
	}
	catch (const std::exception& exception) { editor_error_ = exception.what(); return false; }
}

// Scene全体の状態をJSONへ保存する。RegistryのコピーでUndoに含まれても、JSONへは自動保存されない。
// 新しいComponentの値はここで書き、load_sceneで読み、古いSceneに項目がない場合の既定値も用意する。
bool native_preview12::save_scene(const std::string& filename)
{
	// 【Component追加時の保存入口】Sceneに永続化する値をここへ追加する。
	// Assetは絶対ポインターではなく再読込み可能なpath/IDを保存し、GPU Resourceは保存しない。
	using nlohmann::json;
	if (walk_test_) return false;
	if (filename.empty()) return false;
	json root;
	root["format"] = "JisakuEngineScene";
	root["version"] = 1;
	map_editor_system_.sanitize();
	const auto& tools=map_editor_system_.settings();
	root["editor"]={{"start_paused",!play_mode_},{"snap",tools.snap_to_grid},{"grid_size",tools.grid_size},
		{"rotation_step",tools.rotation_step},{"scale_step",tools.scale_step},{"debug_lines",tools.debug_lines}};
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
			{"enabled", directional_light->enabled}, {"color", {directional_light->color[0],directional_light->color[1],directional_light->color[2]}},
			{"intensity", directional_light->intensity}, {"sky_color", {directional_light->sky_color[0],directional_light->sky_color[1],directional_light->sky_color[2]}},
			{"ground_color", {directional_light->ground_color[0],directional_light->ground_color[1],directional_light->ground_color[2]}},
			{"ambient_intensity", directional_light->ambient_intensity}, {"shadows", directional_light->shadows},
			{"shadow_strength", directional_light->shadow_strength}, {"shadow_distance", directional_light->shadow_distance},
			{"shadow_bias", directional_light->shadow_bias}}}
	});
	root["entities"].push_back({ {"name", "Post Process Volume"},
		{"post_process", {{"exposure",post->exposure}, {"ambient_occlusion",post->ambient_occlusion},
			{"ao_strength",post->ao_strength}, {"ao_radius",post->ao_radius}}} });
	root["entities"].push_back({
		{"name", "Reflection Settings"},
		{"reflection_floor", {
			{"enabled", floor->enabled}, {"reflection_enabled", floor->reflection_enabled},
			{"planar_reflection", floor->planar_reflection}, {"strength", floor->strength},
			{"max_distance", floor->max_distance}, {"thickness", floor->thickness}, {"stride", floor->stride},
			{"steps", floor->steps}, {"exposure", post->exposure}, {"ibl_intensity", settings->ibl_intensity},
			{"vsync", settings->vsync}, {"frame_rate_limit", settings->frame_rate_limit},
			{"background_frame_rate", settings->background_frame_rate},
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
		if (const auto* tag = scene_.gameplay_tag(entity))
			root["entities"].back()["gameplay_tag"] = tag->value;
		if (const auto* test=scene_.get<playtest_controller_component>(entity))
			root["entities"].back()["playtest_controller"]={{"move_speed",test->move_speed},{"sprint_multiplier",test->sprint_multiplier},{"respawn_height",test->respawn_height},{"camera_distance",test->camera_distance},{"first_person",test->first_person},{"eye_height",test->eye_height},{"mouse_sensitivity",test->mouse_sensitivity},{"field_of_view",test->field_of_view}};
		if (const auto* map_object = scene_.map_object(entity))
		{
			root["entities"].back()["map_object"] = {{"static", map_object->is_static},
				{"snap_to_grid", map_object->snap_to_grid}, {"grid_size", map_object->grid_size},
				{"layer", map_object->layer}};
		}
		if (const auto* collider = scene_.collider(entity))
			root["entities"].back()["collider"] = {{"shape", static_cast<int>(collider->shape)},
				{"center", {collider->center[0], collider->center[1], collider->center[2]}},
				{"half_extents", {collider->half_extents[0], collider->half_extents[1], collider->half_extents[2]}},
				{"radius", collider->radius}, {"height", collider->height}, {"layer", collider->layer},
				{"mask", collider->collision_mask}, {"trigger", collider->trigger}, {"enabled", collider->enabled}};
		if (const auto* body = scene_.rigid_body(entity))
			root["entities"].back()["rigid_body"] = {{"type", static_cast<int>(body->type)}, {"mass", body->mass},
				{"velocity", {body->velocity[0], body->velocity[1], body->velocity[2]}},
				{"gravity_scale", body->gravity_scale}, {"restitution", body->restitution}, {"friction", body->friction},
				{"freeze_position", {body->freeze_position[0], body->freeze_position[1], body->freeze_position[2]}}, {"enabled", body->enabled}};
		if (const auto* controller = scene_.character_controller(entity))
			root["entities"].back()["character_controller"] = {{"gravity", controller->gravity},
				{"jump_speed", controller->jump_speed}, {"slope_limit", controller->slope_limit_degrees},
				{"step_height", controller->step_height}, {"enabled", controller->enabled}};
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
	if (walk_test_) return false;
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
		physics_system_.clear();
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
				scene_.rendering_settings(rendering_settings_entity_)->frame_rate_limit = reflection.value("frame_rate_limit", scene_.rendering_settings(rendering_settings_entity_)->frame_rate_limit);
				scene_.rendering_settings(rendering_settings_entity_)->background_frame_rate = reflection.value("background_frame_rate", scene_.rendering_settings(rendering_settings_entity_)->background_frame_rate);
				scene_.rendering_settings(rendering_settings_entity_)->mesh_shaders = reflection.value("mesh_shaders", scene_.rendering_settings(rendering_settings_entity_)->mesh_shaders);
				if (reflection.contains("color") && reflection["color"].is_array())
					for (std::size_t i = 0; i < 3 && i < reflection["color"].size(); ++i) component.color[i] = reflection["color"][i].get<float>();
			}
			if (entity.contains("directional_light"))
			{
				const json& source_light = entity["directional_light"];
				auto& component = *scene_.directional_light(light_entity_);
				component.enabled = source_light.value("enabled", component.enabled);
				component.intensity = source_light.value("intensity",component.intensity);
				component.ambient_intensity = source_light.value("ambient_intensity",component.ambient_intensity);
				component.shadows = source_light.value("shadows",component.shadows);
				component.shadow_strength = source_light.value("shadow_strength",component.shadow_strength);
				component.shadow_distance = source_light.value("shadow_distance",component.shadow_distance);
				component.shadow_bias = source_light.value("shadow_bias",component.shadow_bias);
				auto read_light_color = [&](const char* key, float target[3])
				{
					if (source_light.contains(key) && source_light[key].is_array())
						for (std::size_t i=0; i<3 && i<source_light[key].size(); ++i) target[i]=source_light[key][i].get<float>();
				};
				read_light_color("color",component.color);
				read_light_color("sky_color",component.sky_color);
				read_light_color("ground_color",component.ground_color);
				if (source_light.contains("direction") && source_light["direction"].is_array())
					for (std::size_t i = 0; i < 3 && i < source_light["direction"].size(); ++i) component.direction[i] = source_light["direction"][i].get<float>();
			}
			if (entity.contains("post_process"))
			{
				const auto& source = entity["post_process"];
				auto& component = *scene_.post_process(post_process_entity_);
				component.exposure = source.value("exposure",component.exposure);
				component.ambient_occlusion = source.value("ambient_occlusion",component.ambient_occlusion);
				component.ao_strength = source.value("ao_strength",component.ao_strength);
				component.ao_radius = source.value("ao_radius",component.ao_radius);
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
			if (entity.contains("gameplay_tag"))
				scene_.add_gameplay_tag(model_entity).value = entity.value("gameplay_tag", "Untagged");
			if (entity.contains("playtest_controller"))
			{
				const auto& source=entity["playtest_controller"];
				auto& test=scene_.add<playtest_controller_component>(model_entity);
				test.move_speed=source.value("move_speed",test.move_speed);
            test.sprint_multiplier=source.value("sprint_multiplier",test.sprint_multiplier);
				test.respawn_height=source.value("respawn_height",test.respawn_height);
				test.camera_distance=source.value("camera_distance",test.camera_distance);
            test.first_person=source.value("first_person",test.first_person);
            test.eye_height=source.value("eye_height",test.eye_height);
            test.mouse_sensitivity=source.value("mouse_sensitivity",test.mouse_sensitivity);
            test.field_of_view=source.value("field_of_view",test.field_of_view);
			}
			if (entity.contains("map_object"))
			{
				const json& source = entity["map_object"];
				auto& map_object = scene_.add_map_object(model_entity);
				map_object.is_static = source.value("static", map_object.is_static);
				map_object.snap_to_grid = source.value("snap_to_grid", map_object.snap_to_grid);
				map_object.grid_size = (std::max)(0.01f, source.value("grid_size", map_object.grid_size));
				map_object.layer = source.value("layer", map_object.layer);
			}
			if (entity.contains("collider"))
			{
				const json& source = entity["collider"];
				auto& collider = scene_.add_collider(model_entity);
				collider.shape = static_cast<collider_shape>((std::clamp)(source.value("shape", 0), 0, 2));
				auto read3 = [](const json& value, float target[3]) { if (value.is_array()) for (std::size_t i = 0; i < 3 && i < value.size(); ++i) target[i] = value[i].get<float>(); };
				read3(source.value("center", json::array()), collider.center); read3(source.value("half_extents", json::array()), collider.half_extents);
				collider.radius = source.value("radius", collider.radius); collider.height = source.value("height", collider.height);
				collider.layer = source.value("layer", collider.layer); collider.collision_mask = source.value("mask", collider.collision_mask);
				collider.trigger = source.value("trigger", collider.trigger); collider.enabled = source.value("enabled", collider.enabled);
			}
			if (entity.contains("rigid_body"))
			{
				const json& source = entity["rigid_body"];
				auto& body = scene_.add_rigid_body(model_entity);
				body.type = static_cast<rigid_body_type>((std::clamp)(source.value("type", 2), 0, 2));
				body.mass = source.value("mass", body.mass);
				if (const json velocity = source.value("velocity", json::array()); velocity.is_array()) for (std::size_t i = 0; i < 3 && i < velocity.size(); ++i) body.velocity[i] = velocity[i].get<float>();
				body.gravity_scale = source.value("gravity_scale", body.gravity_scale); body.restitution = source.value("restitution", body.restitution);
				body.friction = source.value("friction", body.friction); body.enabled = source.value("enabled", body.enabled);
				if (const json freeze = source.value("freeze_position", json::array()); freeze.is_array()) for (std::size_t i = 0; i < 3 && i < freeze.size(); ++i) body.freeze_position[i] = freeze[i].get<bool>();
			}
			if (entity.contains("character_controller"))
			{
				const json& source = entity["character_controller"];
				auto& controller = scene_.add_character_controller(model_entity);
				controller.gravity = source.value("gravity", controller.gravity); controller.jump_speed = source.value("jump_speed", controller.jump_speed);
				controller.slope_limit_degrees = source.value("slope_limit", controller.slope_limit_degrees);
				controller.step_height = source.value("step_height", controller.step_height); controller.enabled = source.value("enabled", controller.enabled);
			}
			if (!asset.empty())
			{
				request_model_load(asset, false);
				if (selected_entity_ == null_scene_entity) selected_entity_ = model_entity;
			}
		}
		current_scene_path_ = filename;
#ifdef USE_IMGUI
		play_mode_=!root.value("editor",json::object()).value("start_paused",false);
		const auto editor=root.value("editor",json::object());
		auto& tools=map_editor_system_.settings();
		tools.snap_to_grid=editor.value("snap",true); tools.grid_size=editor.value("grid_size",1.0f);
		tools.rotation_step=editor.value("rotation_step",15.0f); tools.scale_step=editor.value("scale_step",0.1f);
		tools.debug_lines=editor.value("debug_lines",true); map_editor_system_.sanitize();
#endif
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
	const model_placement_preview preview=model_preview_;
	model_preview_.visible=false;
	const float aspect = static_cast<float>(scene_target_.width()) / static_cast<float>((std::max)(scene_target_.height(), 1u));
	// UIからSceneを復元してもフレーム途中の参照が無効にならないよう、描画設定を値で確定する。
	const camera_component camera = *scene_.camera(camera_entity_);
	const reflection_floor_component floor = *scene_.reflection_floor(reflection_floor_entity_);
	const directional_light_component light = *scene_.directional_light(light_entity_);
	const post_process_component post = *scene_.post_process(post_process_entity_);
	const rendering_settings_component settings = *scene_.rendering_settings(rendering_settings_entity_);
	const float orbit_yaw = XMConvertToRadians(camera.yaw_degrees);
	const float orbit_pitch = XMConvertToRadians(camera.pitch_degrees);
	XMVECTOR target = XMVectorSet(camera.target[0], camera.target[1], camera.target[2], 1);
	XMVECTOR eye = XMVectorSet(
		camera.target[0] + std::sin(orbit_yaw) * std::cos(orbit_pitch) * camera.distance,
		camera.target[1] + std::sin(orbit_pitch) * camera.distance,
		camera.target[2] - std::cos(orbit_yaw) * std::cos(orbit_pitch) * camera.distance, 1);
	if (first_person_active_)
	{
		// Playerの目を原点に視線を作る。カメラをモデルの後ろへ引かず、上下視点で移動速度も変えない。
		eye=target;
		float direction[3]{}; playtest_system::view_direction(camera.yaw_degrees,camera.pitch_degrees,direction);
		target=eye+XMVectorSet(direction[0],direction[1],direction[2],0);
	}
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
	// 光源から見た深度を先に作る。影用にも通常表示と同じEntityのAnimation Poseを渡し、
	// キャラクター本体と影が別の時刻で動くことを防ぐ。1枚の影MapをScene/Gameで共有する。
	XMVECTOR sun = XMVectorSet(light.direction[0], light.direction[1], light.direction[2], 0);
	if (XMVectorGetX(XMVector3LengthSq(sun)) < 0.0001f) sun = XMVectorSet(0.35f, 0.8f, -0.45f, 0);
	sun = XMVector3Normalize(sun);
	XMFLOAT3 normalized_sun{};
	XMStoreFloat3(&normalized_sun,sun);
	const float shadow_extent = (std::clamp)(light.shadow_distance, 4.0f, 100.0f);
	const XMVECTOR light_up = std::abs(XMVectorGetY(sun)) > 0.95f ? XMVectorSet(0,0,1,0) : XMVectorSet(0,1,0,0);
	const XMMATRIX shadow_view_matrix = XMMatrixLookAtLH(target + sun * shadow_extent * 2.0f, target, light_up);
	XMMATRIX shadow_projection_matrix = XMMatrixOrthographicLH(shadow_extent, shadow_extent, 0.1f, shadow_extent * 4.0f);
	// 光源の投影中心をTexelへ丸め、カメラを少し動かしただけで影がちらつくのを抑える。
	const XMVECTOR origin = XMVector3TransformCoord(XMVectorZero(), shadow_view_matrix * shadow_projection_matrix);
	const float texel_scale = sun_shadow_map12::resolution * 0.5f;
	shadow_projection_matrix.r[3] += XMVectorSet(
		(std::round(XMVectorGetX(origin)*texel_scale)-XMVectorGetX(origin)*texel_scale)/texel_scale,
		(std::round(XMVectorGetY(origin)*texel_scale)-XMVectorGetY(origin)*texel_scale)/texel_scale, 0, 0);
	XMFLOAT4X4 shadow_view{}, shadow_projection{};
	XMStoreFloat4x4(&shadow_view, shadow_view_matrix);
	XMStoreFloat4x4(&shadow_projection, shadow_projection_matrix);
	lighting_constants12 lighting{};
	XMStoreFloat4x4(&lighting.light_view_projection, shadow_view_matrix * shadow_projection_matrix);
	lighting.sun_color_intensity = { light.color[0], light.color[1], light.color[2], light.enabled ? (std::max)(light.intensity,0.0f) : 0.0f };
	lighting.sky_color_intensity = { light.sky_color[0], light.sky_color[1], light.sky_color[2], (std::max)(light.ambient_intensity,0.0f) };
	lighting.ground_color_intensity = { light.ground_color[0], light.ground_color[1], light.ground_color[2], 0.0f };
	lighting.shadow_parameters = { light.enabled && light.shadows ? (std::clamp)(light.shadow_strength,0.0f,1.0f) : 0.0f,
		(std::clamp)(light.shadow_bias,0.0f,0.01f), 1.0f/sun_shadow_map12::resolution, 0.015f };
	XMStoreFloat4(&lighting.sun_direction, sun);
	model_renderer_.set_lighting(lighting, sun_shadow_.srv());
	sun_shadow_.begin(commands);
	if (lighting.shadow_parameters.x > 0.0f)
		scene_.for_each<transform_component, model_renderer_component>(
			[&](scene_entity entity, transform_component& transform, model_renderer_component& renderer)
			{
				if (!renderer.visible) return;
				if (auto model = model_resources_.find(renderer.asset_path))
					model_renderer_.render(commands, *model, make_world(transform), shadow_view, shadow_projection,
						nullptr, 0.0f, animation_system_.pose(entity), {}, false, false, true);
			});
	sun_shadow_.end(commands);
	// モデル表示の入口。TransformとModel Rendererの両方があり、Asset読込みが完了したEntityだけ描く。
	// 材質や関節をGPUへ渡す詳細はGltfRenderer.cpp、ファイル解析はGltfModel.cppで確認する。
	auto render_models = [&](bool reflected)
	{
		scene_.for_each<transform_component, model_renderer_component>(
			[&](scene_entity entity, transform_component& transform, model_renderer_component& model_component)
			{
				if (!model_component.visible || (first_person_active_ && entity==active_player_)) return;
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
						normalized_sun,
						settings.mesh_shaders, reflected);
				}
			});
	};
	const float reflection_clear[4]{ 0, 0, 0, 0 };
	reflection_target_.begin(commands, reflection_clear);
	if (floor.enabled && floor.reflection_enabled && floor.planar_reflection) render_models(true);
	reflection_target_.end(commands);

	// Sceneだけへ仮モデルを足す。Gameへ渡す前に必要な場合だけ通常Sceneを再描画する。
	bool preview_rendered=false;
	auto render_scene_image=[&](bool show_preview)
	{
	scene_target_.begin(commands, clear);
	if (floor.enabled) scene_geometry_.render_floor(commands, view, projection, floor.height,
		{ floor.color[0], floor.color[1], floor.color[2] }, reflection_target_.color_srv(),
		scene_target_.width(), scene_target_.height(),
		floor.reflection_enabled && floor.planar_reflection ? floor.strength : 0.0f,
		model_renderer_.lighting_address(), sun_shadow_.srv(), { XMVectorGetX(eye), XMVectorGetY(eye), XMVectorGetZ(eye) });
	render_models(false);
	if (show_preview && preview.visible && !walk_test_)
	{
		if (auto model=model_resources_.find(preview.asset))
		{
			model_renderer_.render(commands,*model,make_world(preview.transform),view,projection,
				environment_lighting_.get(),settings.ibl_intensity,nullptr,normalized_sun,settings.mesh_shaders);
			preview_rendered=true;
		}
	}
	scene_target_.end(commands);
	};
	render_scene_image(true);
	ssr_.begin_frame(renderer_.frame_index());
#ifdef USE_IMGUI
	scene_display_target_.begin(commands, clear);
	ssr_.render(commands, scene_target_, projection, view, 0.0f, floor.max_distance, floor.thickness,
		static_cast<std::uint32_t>(floor.steps), post.exposure, post.ambient_occlusion ? post.ao_strength : 0.0f, post.ao_radius, model_renderer_.lighting_address());
	scene_display_target_.end(commands);
	if (preview_rendered) render_scene_image(false);
	game_target_.begin(commands, clear);
	ssr_.render(commands, scene_target_, projection, view,
		floor.reflection_enabled && !floor.planar_reflection ? floor.strength : 0.0f,
		floor.max_distance, floor.thickness, static_cast<std::uint32_t>(floor.steps), post.exposure, post.ambient_occlusion ? post.ao_strength : 0.0f, post.ao_radius, model_renderer_.lighting_address());
	game_target_.end(commands);
#else
	const auto back_buffer_rtv = renderer_.current_rtv();
	commands->OMSetRenderTargets(1, &back_buffer_rtv, FALSE, nullptr);
	ssr_.render(commands, scene_target_, projection, view,
		floor.reflection_enabled && !floor.planar_reflection ? floor.strength : 0.0f,
		floor.max_distance, floor.thickness, static_cast<std::uint32_t>(floor.steps), post.exposure, post.ambient_occlusion ? post.ao_strength : 0.0f, post.ao_radius, model_renderer_.lighting_address());
#endif

#ifdef USE_IMGUI
	const auto back_buffer_rtv = renderer_.current_rtv();
	commands->OMSetRenderTargets(1, &back_buffer_rtv, FALSE, nullptr);
	imgui_.new_frame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImGuizmo::BeginFrame();
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	const float toolbar_height = 40.0f;
	const float side_width = (std::max)(320.0f, display.x * 0.28f);
	const float view_width = (std::max)(1.0f, display.x - side_width);
	const float view_height = (std::max)(1.0f, (display.y - toolbar_height) * 0.5f);
	const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(display.x, toolbar_height));
	ImGui::Begin("Toolbar", nullptr, fixed | ImGuiWindowFlags_NoTitleBar);
    // よく使う操作だけを常設し、保存形式・配置補助・表示設定は用途別メニューへ収納する。
    begin_editor_disabled(walk_test_);
    if (editor_ui::Button(u8"ファイル")) ImGui::OpenPopup("FileOperations");
    if (ImGui::BeginPopup("FileOperations"))
    {
        if (editor_ui::MenuItem(u8"新しいシーン")) new_scene();
        if (editor_ui::MenuItem(u8"シーンを開く")) browse_scene(false);
        if (editor_ui::MenuItem(u8"名前を付けて保存", "Ctrl+Shift+S")) browse_scene(true);
        ImGui::Separator();
        if (editor_ui::MenuItem(u8"モデルを追加")) browse_model();
        if (editor_ui::MenuItem(u8"Prefabを開く")) browse_prefab(false);
        if (editor_ui::MenuItem(u8"選択物をPrefabとして保存", nullptr, false, scene_.renderer(selected_entity_)!=nullptr)) browse_prefab(true);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (editor_ui::Button(u8"編集")) ImGui::OpenPopup("EditOperations");
    if (ImGui::BeginPopup("EditOperations"))
    {
        if (editor_ui::MenuItem(u8"選択物を複製", "Ctrl+D",false,scene_.renderer(selected_entity_)!=nullptr))
        {
            push_undo(); selected_entity_=scene_.duplicate(selected_entity_);
            if (auto* transform=scene_.transform(selected_entity_)) transform->position[0]+=0.5f;
        }
        if (editor_ui::MenuItem(u8"選択物へカメラを寄せる", "F")) focus_selected();
        if (editor_ui::MenuItem(u8"選択物を確認用床へ置く",nullptr,false,scene_.renderer(selected_entity_)!=nullptr))
        {
            auto* transform=scene_.transform(selected_entity_); auto* model=scene_.renderer(selected_entity_);
            if (auto resource=model_resources_.find(model->asset_path))
            { push_undo(); transform->position[1]=floor.height-resource->bounds_min().y*transform->scale3[1]; }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (editor_ui::Button(u8"表示・詳細")) ImGui::OpenPopup("ViewOperations");
    if (ImGui::BeginPopup("ViewOperations"))
    {
        editor_ui::MenuItem(u8"Gizmoを表示",nullptr,&gizmo_enabled_);
        if (editor_ui::MenuItem(u8"全画面", "F11")) toggle_fullscreen();
        if (editor_ui::MenuItem(u8"1フレーム進める",nullptr,false,!play_mode_)) step_requested_=true;
        if (editor_ui::MenuItem(u8"歩行テストを開始")) start_walk_test();
        ImGui::EndPopup();
    }
    ImGui::SameLine(); if (editor_ui::Button(u8"開く")) browse_scene(false);
    ImGui::SameLine(); if (editor_ui::Button(u8"保存")) { if(current_scene_path_.empty()) browse_scene(true); else save_scene(current_scene_path_); }
    ImGui::SameLine(); if (editor_ui::Button("Undo")) undo();
    ImGui::SameLine(); if (editor_ui::Button("Redo")) redo();
    ImGui::SameLine(); if (editor_ui::Button(u8"移動 W")) gizmo_operation_=0;
    ImGui::SameLine(); if (editor_ui::Button(u8"回転 E")) gizmo_operation_=1;
    ImGui::SameLine(); if (editor_ui::Button(u8"拡縮 R")) gizmo_operation_=2;
    end_editor_disabled();
    bool player_in_scene=false;
    scene_.for_each<playtest_controller_component>([&](scene_entity,const auto&) { player_in_scene=true; });
    ImGui::SameLine();
    if (editor_ui::Button(walk_test_ ? (player_paused_ ? "Resume" : "Pause") : (player_in_scene ? "Play" : (play_mode_ ? "Pause" : "Play")))) toggle_play();
    ImGui::SameLine(); ImGui::TextDisabled("FPS %.0f", ImGui::GetIO().Framerate);
    if (model_resources_.loading()) { ImGui::SameLine(); ImGui::TextDisabled(u8"読込み中..."); }
    ImGui::End();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
	ImGui::SetNextWindowPos(ImVec2(0, toolbar_height));
	ImGui::SetNextWindowSize(ImVec2(view_width, view_height));
	ImGui::Begin("Scene", nullptr, fixed | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	begin_editor_disabled(walk_test_);
	auto& scene_tools=map_editor_system_.settings();
	editor_ui::Checkbox("Snap",&scene_tools.snap_to_grid);
	// 項目の直後で右クリックを受ける。GizmoやImageを描いた後では対象が変わってしまう。
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("SnapSettings");
	// 自動幅Popupに全幅入力欄を置くと幅が縮み続けるため、読みやすい幅を先に確定する。
	if (ImGui::IsPopupOpen("SnapSettings")) ImGui::SetNextWindowSize(ImVec2(300,0),ImGuiCond_Always);
	if (ImGui::BeginPopup("SnapSettings"))
	{
		editor_ui::DragFloat("Move Step (m)",&scene_tools.grid_size,0.01f,0.01f,100.0f);
		editor_ui::DragFloat("Rotate Step (deg)",&scene_tools.rotation_step,1.0f,1.0f,180.0f);
		editor_ui::DragFloat("Scale Step",&scene_tools.scale_step,0.01f,0.01f,10.0f);
		ImGui::EndPopup();
	}
	ImGui::SameLine(); ImGui::SetNextItemWidth(80);
	ImGui::DragFloat("Grid (m)",&scene_tools.grid_size,0.01f,0.01f,100.0f,"%.2f"); editor_ui::help("Grid (m)");
	ImGui::SameLine();
	if (editor_ui::Button("Scene View")) ImGui::OpenPopup("SceneDisplaySettings");
	if (ImGui::BeginPopup("SceneDisplaySettings"))
	{
		editor_ui::MenuItem("Debug Lines",nullptr,&scene_tools.debug_lines);
		ImGui::EndPopup();
	}
	map_editor_system_.sanitize();
	end_editor_disabled();
	const ImVec2 available = ImGui::GetContentRegionAvail();
	const float texture_aspect = static_cast<float>(scene_target_.width()) / scene_target_.height();
	ImVec2 image_size = available;
	if (image_size.x / (std::max)(image_size.y, 1.0f) > texture_aspect) image_size.x = image_size.y * texture_aspect;
	else image_size.y = image_size.x / texture_aspect;
	const ImVec2 image_min = ImGui::GetCursorScreenPos();
	ImGui::Image(reinterpret_cast<ImTextureID>(scene_display_target_.srv().ptr), image_size);
	scene_hovered_ = ImGui::IsItemHovered();
	// ドロップもクリック配置も、表示中のImageの矩形から同じRayを作る。
	auto position_at_mouse=[&](bool on_collider,float position[3])
	{
		map_editor_ray ray{};
		if (!map_editor_system_.make_ray(ImGui::GetIO().MousePos.x,ImGui::GetIO().MousePos.y,
			image_min.x,image_min.y,image_size.x,image_size.y,view,projection,ray)) return false;
		bool found=false;
		if (on_collider)
		{
			physics_raycast_hit hit{};
			if (physics_system_.raycast(scene_,ray.origin,ray.direction,1000,0xffffffffu,hit))
			{ std::copy_n(hit.point,3,position); found=true; }
		}
		if (!found) found=map_editor_system_.intersect_plane(ray,scene_tools.plane_height,position);
		if (found) map_editor_system_.snap(position,ImGui::GetIO().KeyCtrl);
		return found;
	};
	auto place_asset_at_mouse=[&](const std::string& asset,bool on_collider)
	{
		float position[3]{};
		if (position_at_mouse(on_collider,position)) request_map_placement(asset,position);
		else editor_error_=u8"配置先が見つかりません。水平面の高さやカメラの向きを確認してください。";
	};
	// Imageの直後に登録し、Gizmoや設定欄をドロップ先として誤認させない。
	bool model_dropped=false;
	if (!walk_test_ && !drag_cancelled_ && ImGui::BeginDragDropTarget())
	{
		if (const auto* payload=ImGui::AcceptDragDropPayload("JISAKU_MODEL_ASSET",ImGuiDragDropFlags_AcceptBeforeDelivery))
		{
			if (payload->DataSize>1 && payload->DataSize<32768 && static_cast<const char*>(payload->Data)[payload->DataSize-1]=='\0')
			{
				const std::string asset=static_cast<const char*>(payload->Data);
				if (payload->IsDelivery()) { model_dropped=true; place_asset_at_mouse(asset,true); }
				else
				{
					float position[3]{};
					if (position_at_mouse(true,position))
					{
						auto model=model_resources_.find(asset);
						model_preview_.asset=asset;
						model_preview_.transform=map_editor_system::model_transform(position,model ? model->bounds_min().y : 0,
							scene_tools.rotation_y_degrees,scene_tools.uniform_scale);
						model_preview_.visible=true;
					}
				}
			}
		}
		ImGui::EndDragDropTarget();
	}
	if (model_preview_.visible)
	{
		const ImVec2 mouse=ImGui::GetIO().MousePos;
		const char* message=model_resources_.find(model_preview_.asset) ? u8"配置プレビュー：離して確定 / Escで取消" : u8"プレビュー用モデルを読込み中...";
		ImGui::GetWindowDrawList()->AddText(ImVec2(mouse.x+16,mouse.y+16),IM_COL32(110,230,255,255),message);
	}
	if (!model_dropped && gizmo_enabled_ && scene_.transform(selected_entity_) && scene_.renderer(selected_entity_))
	{
		const ImGuizmo::OPERATION operation = gizmo_operation_ == 1 ? ImGuizmo::ROTATE :
			gizmo_operation_ == 2 ? ImGuizmo::SCALE : ImGuizmo::TRANSLATE;
		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
		ImGuizmo::SetRect(image_min.x, image_min.y, image_size.x, image_size.y);
		const float translation_snap = scene_tools.grid_size;
		float snap[3]{ translation_snap, translation_snap, translation_snap };
		if (operation == ImGuizmo::ROTATE) snap[0] = snap[1] = snap[2] = scene_tools.rotation_step;
		else if (operation == ImGuizmo::SCALE) snap[0] = snap[1] = snap[2] = scene_tools.scale_step;
		editor_snapshot gizmo_before = capture_snapshot();
		// 全物体でScene上部の設定を使う。CtrlはSnapがOFFの間も一時的に有効にする。
		const bool use_snap = map_editor_system_.snapping(ImGui::GetIO().KeyCtrl);
		if (ImGuizmo::Manipulate(&view._11, &projection._11, operation,
			operation == ImGuizmo::SCALE ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
			&world._11, nullptr, use_snap ? snap : nullptr))
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
	if (!walk_test_ && scene_tools.debug_lines)
    scene_.for_each<transform_component,collider_component>([&](scene_entity entity,transform_component& transform_value,collider_component& collider_value)
    {
        if (!collider_value.enabled) return;
        const auto* collider=&collider_value;
		if (const auto* transform = &transform_value)
		{
			// Debug表示は実際のPhysics判定と同じAxis Aligned境界を描く。
			// Sphere／Capsuleも現段階では衝突に使う外接境界を表示する。
			const float scale_x = std::abs(transform->scale3[0]);
			const float scale_y = std::abs(transform->scale3[1]);
			const float scale_z = std::abs(transform->scale3[2]);
			XMFLOAT3 size{};
			if (collider->shape == collider_shape::box)
				size = { collider->half_extents[0] * scale_x * 2.0f,
					collider->half_extents[1] * scale_y * 2.0f, collider->half_extents[2] * scale_z * 2.0f };
			else if (collider->shape == collider_shape::sphere)
			{
				const float diameter = collider->radius * (std::max)({ scale_x, scale_y, scale_z }) * 2.0f;
				size = { diameter, diameter, diameter };
			}
			else
			{
				const float diameter = collider->radius * (std::max)(scale_x, scale_z) * 2.0f;
				size = { diameter, (std::max)(collider->height * scale_y, diameter), diameter };
			}
			XMFLOAT4X4 collider_world{};
			XMStoreFloat4x4(&collider_world,
				XMMatrixScaling(size.x, size.y, size.z) *
				XMMatrixTranslation(transform->position[0] + collider->center[0] * scale_x,
					transform->position[1] + collider->center[1] * scale_y,
					transform->position[2] + collider->center[2] * scale_z));
			// 面を塗ると大きい床の選択時にステージが隠れるため、12本の辺だけを重ねる。
			XMFLOAT3 corners[8]{};
			const XMMATRIX clip=XMLoadFloat4x4(&collider_world)*XMLoadFloat4x4(&view)*XMLoadFloat4x4(&projection);
			for (int i=0;i<8;++i) XMStoreFloat3(&corners[i],XMVector3TransformCoord(
				XMVectorSet((i&1)?0.5f:-0.5f,(i&2)?0.5f:-0.5f,(i&4)?0.5f:-0.5f,1),clip));
			auto* draw=ImGui::GetWindowDrawList();
			draw->PushClipRect(image_min,ImVec2(image_min.x+image_size.x,image_min.y+image_size.y),true);
			for (int i=0;i<8;++i) for (int bit=1;bit<=4;bit*=2) if (!(i&bit))
			{
				const auto& a=corners[i]; const auto& b=corners[i|bit];
				if (a.z<=0 || a.z>=1 || b.z<=0 || b.z>=1) continue;
				draw->AddLine(ImVec2(image_min.x+(a.x+1)*0.5f*image_size.x,image_min.y+(1-a.y)*0.5f*image_size.y),
					ImVec2(image_min.x+(b.x+1)*0.5f*image_size.x,image_min.y+(1-b.y)*0.5f*image_size.y),entity==selected_entity_ ? IM_COL32(100,235,130,240) : (collider->trigger ? IM_COL32(255,190,70,180) : IM_COL32(90,180,255,150)));
			}
			draw->PopClipRect();
		}
    });
	if (!walk_test_ && map_editor_system_.settings().enabled && scene_hovered_ &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
        place_asset_at_mouse(selected_asset_path_,scene_tools.place_on_collider);
    // マップ部品が多い場合もHierarchyを探し回らず、Sceneで見えているColliderをクリックして選べる。
	if (!walk_test_ && !model_dropped && !map_editor_system_.settings().enabled && scene_hovered_ &&
		ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
	{
		map_editor_ray ray{}; physics_raycast_hit hit{};
		if (map_editor_system_.make_ray(ImGui::GetIO().MousePos.x,ImGui::GetIO().MousePos.y,
			image_min.x,image_min.y,image_size.x,image_size.y,view,projection,ray) &&
			physics_system_.raycast(scene_,ray.origin,ray.direction,1000.0f,0xffffffffu,hit)) selected_entity_=hit.entity;
	}
	ImGui::End();

	ImGui::SetNextWindowPos(ImVec2(0, toolbar_height + view_height));
	ImGui::SetNextWindowSize(ImVec2(view_width, view_height));
	ImGui::Begin("Game", nullptr, fixed | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	const ImVec2 game_available = ImGui::GetContentRegionAvail();
	ImVec2 game_size = game_available;
	if (game_size.x / (std::max)(game_size.y, 1.0f) > texture_aspect) game_size.x = game_size.y * texture_aspect;
	else game_size.y = game_size.x / texture_aspect;
	const ImVec2 game_min=ImGui::GetCursorScreenPos();
	ImGui::Image(reinterpret_cast<ImTextureID>(game_target_.srv().ptr), game_size);
	if (walk_test_)
	{
		const char* message=u8"WASD/矢印: 移動  マウス: 視点  Shift: ダッシュ  Space: ジャンプ  R: 復帰  Esc: ポーズ";
		scene_.for_each<playtest_controller_component>([&](scene_entity,const playtest_controller_component& test)
			{ if (test.goal_reached) message=u8"GOAL! ゴール到達。Escのポーズ画面から編集へ戻れます。"; });
		auto* draw=ImGui::GetWindowDrawList();
		draw->AddRectFilled(ImVec2(game_min.x+8,game_min.y+8),ImVec2(game_min.x+game_size.x-8,game_min.y+38),IM_COL32(15,20,25,210),4);
		draw->AddText(ImVec2(game_min.x+16,game_min.y+14),IM_COL32(255,240,210,255),message);
        if (first_person_active_) {
            const ImVec2 center(game_min.x+game_size.x*.5f,game_min.y+game_size.y*.5f);
            draw->AddLine(ImVec2(center.x-5,center.y),ImVec2(center.x+5,center.y),IM_COL32(255,255,255,200));
            draw->AddLine(ImVec2(center.x,center.y-5),ImVec2(center.x,center.y+5),IM_COL32(255,255,255,200));
        }
	}
	ImGui::End();
	ImGui::PopStyleVar();

	if (walk_test_ && player_paused_) ImGui::OpenPopup("Pause Menu");
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x*0.5f,ImGui::GetIO().DisplaySize.y*0.5f),ImGuiCond_Always,ImVec2(0.5f,0.5f));
	if (ImGui::BeginPopupModal("Pause Menu",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (!walk_test_ || !player_paused_) ImGui::CloseCurrentPopup();
		else
		{
		editor_ui::TextUnformatted(u8"ポーズ中");
		editor_ui::TextUnformatted(u8"再開するか、配置を元に戻して編集へ戻れます。");
		if (editor_ui::Button(u8"再開",ImVec2(240,36))) { ImGui::CloseCurrentPopup(); set_player_paused(false); }
		if (editor_ui::Button(u8"編集に戻る",ImVec2(240,36))) { ImGui::CloseCurrentPopup(); stop_walk_test(); }
		}
		ImGui::EndPopup();
	}
	ImGui::SetNextWindowPos(ImVec2(view_width, toolbar_height));
	ImGui::SetNextWindowSize(ImVec2(side_width, view_height));
	ImGui::Begin("Hierarchy / Project", nullptr, fixed);
	begin_editor_disabled(walk_test_);
	if (ImGui::BeginTabBar("SceneBrowserTabs"))
	{
		if (ImGui::BeginTabItem("Hierarchy"))
		{
			for (const scene_entity entity : scene_.entities())
			{
				// 同じ表示名でも別Entity。名前をImGuiの識別子にすると行が衝突するため、IDを分ける。
				ImGui::PushID(static_cast<int>(entity));
				const name_component* name = scene_.name(entity);
				const std::string row=(name ? name->value : "Entity")+"  ["+std::to_string(entity)+"]";
				if (ImGui::Selectable(row.c_str(), selected_entity_ == entity))
					selected_entity_ = entity;
				editor_ui::explain(u8"クリックで選択し、Inspectorで設定を調整します。Fでモデルへカメラを寄せ、Ctrl+Dで複製、Ctrl+Zで編集を取り消せます。");
				ImGui::PopID();
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Project"))
		{
			std::filesystem::path directory(asset_browser_directory_);
			std::error_code asset_error;
			if (!std::filesystem::exists(directory, asset_error)) directory = std::filesystem::current_path(asset_error);
			if (editor_ui::Button("Up") && directory.has_parent_path())
			{
				asset_browser_directory_ = directory.parent_path().string();
				asset_navigation_time_ = ImGui::GetTime();
			}
			ImGui::SameLine();
			ImGui::TextWrapped("%s", directory.filename().string().c_str());
			// 見本への入口を固定し、初参加の人がフォルダー構成を知らなくても辿れるようにする。
			if (editor_ui::Button("Samples"))
			{
				asset_browser_directory_ = "resources/samples";
				asset_navigation_time_ = ImGui::GetTime();
			}
			ImGui::Separator();
			std::vector<std::filesystem::path> entries;
			for (std::filesystem::directory_iterator iterator(directory, asset_error), end;
				!asset_error && iterator != end; iterator.increment(asset_error))
			{
				std::string extension = iterator->path().extension().string();
				std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
				if (iterator->is_directory(asset_error) || extension == ".gltf" || extension == ".glb" || extension == ".jprefab" || extension == ".jscene")
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
			std::string requested_scene;
			for (std::size_t index = 0; index < entries.size(); ++index)
			{
				const auto& path = entries[index];
				const bool is_directory = std::filesystem::is_directory(path, asset_error);
				ImGui::PushID(static_cast<int>(index));
				const ImVec2 tile_min = ImGui::GetCursorScreenPos();
				const float tile_width = (std::max)(96.0f, ImGui::GetContentRegionAvail().x);
				ImGui::InvisibleButton("Asset", ImVec2(tile_width, 42.0f));
				const bool hovered = ImGui::IsItemHovered();
				editor_ui::explain(is_directory ? u8"ダブルクリックでフォルダーを開きます。" :
					path.extension()==".jscene" ? u8"ダブルクリックでこのマップを開きます。現在のシーンを置き換えるため、必要な編集は先に保存してください。" :
					path.extension()==".jprefab" ? u8"クリックで配置素材として選択。ダブルクリックで部品をシーンへ追加します。配置ツールをONにするとScene上へ配置できます。" :
					u8"クリックで配置素材として選択。ダブルクリックでモデルをシーンへ追加します。初回の読込みは非同期で進みます。");
				const bool selected = selected_asset_path_ == path.string();
				ImDrawList* draw = ImGui::GetWindowDrawList();
				if (selected || hovered) draw->AddRectFilled(tile_min, ImVec2(tile_min.x + tile_width, tile_min.y + 42.0f),
					ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered), 4.0f);
				if (is_directory)
				{
					// フォルダーは矩形を組み合わせて描く。外部アイコン画像がなくてもProjectを操作できる。
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
					else if (path.extension() == ".jscene") requested_scene = path.string();
					else requested_model = path.string();
				}
				std::string drag_extension=path.extension().string();
				std::transform(drag_extension.begin(),drag_extension.end(),drag_extension.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (!is_directory && (drag_extension==".glb" || drag_extension==".gltf") && ImGui::BeginDragDropSource())
				{
					// ImGuiがパスをコピーする。ローカル変数へのポインターを次フレームへ保持しない。
					const std::string asset=path.string();
					ImGui::SetDragDropPayload("JISAKU_MODEL_ASSET",asset.c_str(),asset.size()+1);
					if (!drag_cancelled_ && preview_requested_asset_!=asset)
					{
						preview_requested_asset_=asset;
						request_model_load(asset,false);
					}
					ImGui::TextUnformatted(drag_cancelled_ ? u8"配置を取り消しました" : u8"Sceneで仮表示 / 離して配置 / Escで取消");
					ImGui::TextUnformatted(path.filename().string().c_str());
					ImGui::EndDragDropSource();
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
			// マップ見本もProjectから開ける。読込み中などで開けなければ失敗を表示する。
			if (!requested_scene.empty() && !load_scene(requested_scene))
				editor_error_ = u8"シーンを開けません。素材の読込み完了を待って、もう一度開いてください。";
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	end_editor_disabled();
	ImGui::End();
	if (!ImGui::GetDragDropPayload()) { preview_requested_asset_.clear(); drag_cancelled_=false; }

	ImGui::SetNextWindowPos(ImVec2(view_width, toolbar_height + view_height));
	ImGui::SetNextWindowSize(ImVec2(side_width, view_height));
	ImGui::Begin("Inspector", nullptr, fixed);
	// Triggerは押し戻さないため、接触を数値でも確認できるようにする。
	// ゲームの演出やクリア判定はここでは行わず、ゲームSystemでevents()を読む。
	if (editor_ui::CollapsingHeader("Physics Monitor"))
	{
		ImGui::Text(u8"接触: %zu件 / 変化: %zu件", physics_system_.contacts().size(), physics_system_.events().size());
		std::size_t displayed = 0;
		for (const auto& event : physics_system_.events())
		{
			if (displayed++ >= 16) break;
			const auto* first = scene_.name(event.contact.first);
			const auto* second = scene_.name(event.contact.second);
			const char* phase = event.phase == physics_event_phase::enter ? u8"入った" :
				(event.phase == physics_event_phase::stay ? u8"触れている" : u8"離れた");
			ImGui::TextWrapped("%s %s: %s / %s", event.contact.trigger ? u8"範囲検知" : u8"衝突", phase,
				first ? first->value.c_str() : "Entity", second ? second->value.c_str() : "Entity");
		}
	}
	begin_editor_disabled(walk_test_);
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
	if (editor_ui::CollapsingHeader("Map Tools", ImGuiTreeNodeFlags_DefaultOpen))
	{
		auto& map_settings = map_editor_system_.settings();
		editor_ui::Checkbox("Placement Mode", &map_settings.enabled);
		ImGui::TextWrapped(u8"配置する素材: %s", selected_asset_path_.empty() ? u8"ProjectでモデルまたはPrefabを選択" :
			selected_asset_path_.c_str());
		const char* placement_modes[]{ u8"指定した高さの水平面", u8"当たり判定の表面" };
		int placement_mode = map_settings.place_on_collider ? 1 : 0;
		if (editor_ui::Combo("Placement Surface", &placement_mode, placement_modes, 2))
			map_settings.place_on_collider = placement_mode == 1;
		editor_ui::Checkbox("Snap XZ To Grid", &map_settings.snap_to_grid);
		editor_ui::DragFloat("Placement Grid", &map_settings.grid_size, 0.05f, 0.01f, 100.0f, "%.2f m");
		map_settings.grid_size = (std::max)(map_settings.grid_size, 0.01f);
		if (!map_settings.place_on_collider)
			editor_ui::DragFloat("Plane Height", &map_settings.plane_height, 0.05f, -1000.0f, 1000.0f, "%.2f m");
		editor_ui::DragFloat("Placement Yaw", &map_settings.rotation_y_degrees, 1.0f, -360.0f, 360.0f, "%.0f deg");
		ImGui::SameLine();
		if (editor_ui::Button("+90")) map_settings.rotation_y_degrees += 90.0f;
		editor_ui::DragFloat("Placement Scale", &map_settings.uniform_scale, 0.05f, 0.001f, 100.0f, "%.2f");
		map_settings.uniform_scale = (std::max)(map_settings.uniform_scale, 0.001f);
		editor_ui::Checkbox("Add Static Box Collider", &map_settings.add_box_collider);
		ImGui::TextDisabled(u8"素材を選び、配置をONにしてSceneをクリック。Escで終了。");
		ImGui::Separator();
	}
	if (const auto* name = scene_.name(selected_entity_)) ImGui::Text("Entity %u: %s", selected_entity_, name->value.c_str());
	if (scene_.valid(selected_entity_) && !scene_.gameplay_tag(selected_entity_))
	{
		if (editor_ui::Button("Add Gameplay Tag Component"))
		{
			push_undo(inspector_before);
			inspector_history_recorded = true;
			scene_.add_gameplay_tag(selected_entity_);
		}
	}
	if (auto* tag = scene_.gameplay_tag(selected_entity_))
	{
		ImGui::Separator(); editor_ui::TextUnformatted("Gameplay Tag Component");
		char tag_buffer[64]{};
		strncpy_s(tag_buffer, tag->value.c_str(), sizeof(tag_buffer) - 1);
		if (editor_ui::InputText("Tag", tag_buffer, sizeof(tag_buffer))) tag->value = tag_buffer;
		record_component_edit();
		ImGui::TextDisabled(u8"入力例: Player / Enemy / Checkpoint / Goal / Hazard");
	}
	if (auto* selected_transform = scene_.transform(selected_entity_))
	{
		ImGui::Separator(); editor_ui::TextUnformatted("Transform Component");
		editor_ui::DragFloat3("Position", selected_transform->position, 0.01f);
		record_component_edit();
		editor_ui::DragFloat3("Rotation", selected_transform->rotation, 0.25f);
		record_component_edit();
		if (editor_ui::DragFloat3("Scale", selected_transform->scale3, 0.01f, 0.001f, 100.0f))
			selected_transform->scale = (selected_transform->scale3[0] + selected_transform->scale3[1] + selected_transform->scale3[2]) / 3.0f;
		record_component_edit();
	}
	if (auto* model_component = scene_.renderer(selected_entity_))
	{
		ImGui::Separator(); editor_ui::TextUnformatted("Model Renderer Component");
		editor_ui::Checkbox("Visible", &model_component->visible);
		record_component_edit();
		ImGui::TextWrapped("%s", model_component->asset_path.c_str());
		auto model = model_resources_.find(model_component->asset_path);
		if (model)
		{
			ImGui::Text(u8"形状: %d / 材質: %d", static_cast<int>(model->meshes().size()), static_cast<int>(model->materials().size()));
			ImGui::Text(u8"画像読込み: %d / %d", static_cast<int>(model->loaded_texture_count()), static_cast<int>(model->textures().size()));
			if (auto* animation = scene_.animation(selected_entity_); animation && !model->animations().empty())
			{
				ImGui::Separator(); editor_ui::TextUnformatted("Animation Component");
				animation->clip = (std::clamp)(animation->clip, 0, static_cast<int>(model->animations().size()) - 1);
				auto* controller = scene_.animation_controller(selected_entity_);
				if (controller && !controller->states.empty())
				{
					controller->requested_state = (std::clamp)(controller->requested_state, 0,
						static_cast<int>(controller->states.size()) - 1);
					if (editor_ui::BeginCombo("State", controller->states[controller->requested_state].name.c_str()))
					{
						for (int index = 0; index < static_cast<int>(controller->states.size()); ++index)
							if (ImGui::Selectable(controller->states[index].name.c_str(), index == controller->requested_state))
							{
								if (!inspector_history_recorded) { push_undo(inspector_before); inspector_history_recorded = true; }
								controller->requested_state = index;
							}
						ImGui::EndCombo();
					}
					editor_ui::Checkbox("Controller Enabled", &controller->enabled);
					record_component_edit();
				}
				else
				{
					const std::string& current_name = model->animations()[animation->clip].name;
					if (editor_ui::BeginCombo("Clip", current_name.empty() ? u8"名前のない動き" : current_name.c_str()))
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
				editor_ui::Checkbox("Playing", &animation->playing);
				record_component_edit();
				ImGui::SameLine(); editor_ui::Checkbox("Loop", &animation->loop);
				record_component_edit();
				editor_ui::SliderFloat("Speed", &animation->speed, 0.0f, 3.0f, "%.2fx");
				record_component_edit();
				const float duration = (std::max)(model->animations()[animation->clip].duration, 0.001f);
				editor_ui::SliderFloat("Time", &animation->time, 0.0f, duration, "%.3f sec");
				record_component_edit();
			}
		}
	}
	if (scene_.renderer(selected_entity_) && !scene_.map_object(selected_entity_))
	{
		ImGui::Separator();
		if (editor_ui::Button("Add Map Object Component"))
		{
			push_undo(inspector_before);
			inspector_history_recorded = true;
			scene_.add_map_object(selected_entity_);
		}
	}
	if (auto* map_object = scene_.map_object(selected_entity_))
	{
		ImGui::Separator(); editor_ui::TextUnformatted("Map Object Component");
		editor_ui::Checkbox("Static Map Object", &map_object->is_static);
		record_component_edit();
		ImGui::TextWrapped(u8"移動・回転・拡縮の吸着はScene上部のSnapで設定します。部品ごとの旧格子設定は保存互換用に保持します。");
		editor_ui::InputInt("Map Layer", &map_object->layer);
		record_component_edit();
		ImGui::TextDisabled(u8"分類用の設定です。一括描画による最適化は未対応です。");
	}
	if (scene_.renderer(selected_entity_) && !scene_.collider(selected_entity_))
	{
		ImGui::Separator();
		if (editor_ui::Button("Add Collider Component"))
		{
			push_undo(inspector_before);
			inspector_history_recorded = true;
			scene_.add_collider(selected_entity_);
		}
	}
	if (auto* collider = scene_.collider(selected_entity_))
	{
		ImGui::Separator(); editor_ui::TextUnformatted("Collider Component");
		editor_ui::Checkbox("Collider Enabled", &collider->enabled); record_component_edit();
		const char* shapes[]{ u8"箱", u8"球", u8"カプセル (近似)" };
		int shape = static_cast<int>(collider->shape);
		if (editor_ui::Combo("Shape", &shape, shapes, 3)) collider->shape = static_cast<collider_shape>(shape);
		record_component_edit();
		editor_ui::DragFloat3("Collider Center", collider->center, 0.01f); record_component_edit();
		if (collider->shape == collider_shape::box)
		{
			editor_ui::DragFloat3("Half Extents", collider->half_extents, 0.01f, 0.01f, 1000.0f); record_component_edit();
		}
		else
		{
			editor_ui::DragFloat("Radius", &collider->radius, 0.01f, 0.01f, 1000.0f); record_component_edit();
			if (collider->shape == collider_shape::capsule)
			{
				editor_ui::DragFloat("Capsule Height", &collider->height, 0.01f, collider->radius * 2.0f, 1000.0f); record_component_edit();
			}
		}
		int layer = static_cast<int>(collider->layer);
		if (editor_ui::SliderInt("Collision Layer", &layer, 0, 31)) collider->layer = static_cast<std::uint32_t>(layer);
		record_component_edit();
		editor_ui::InputScalar("Collision Mask", ImGuiDataType_U32, &collider->collision_mask, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
		record_component_edit();
		editor_ui::Checkbox("Is Trigger", &collider->trigger); record_component_edit();
	}
	if (scene_.collider(selected_entity_) && !scene_.rigid_body(selected_entity_) && !scene_.character_controller(selected_entity_))
	{
		if (editor_ui::Button("Add RigidBody Component"))
		{
			push_undo(inspector_before); inspector_history_recorded = true;
			scene_.add_rigid_body(selected_entity_);
		}
		ImGui::SameLine();
		if (editor_ui::Button("Add Character Controller"))
		{
			push_undo(inspector_before); inspector_history_recorded = true;
			scene_.add_character_controller(selected_entity_);
			auto& selected_collider = *scene_.collider(selected_entity_);
			selected_collider.shape = collider_shape::capsule;
			selected_collider.radius = 0.4f;
			selected_collider.height = 1.8f;
			selected_collider.center[1] = 0.9f;
		}
	}
	if (auto* body = scene_.rigid_body(selected_entity_))
	{
		ImGui::Separator(); editor_ui::TextUnformatted("RigidBody Component");
		editor_ui::Checkbox("RigidBody Enabled", &body->enabled); record_component_edit();
		const char* body_types[]{ u8"固定", u8"速度で移動", u8"重力で移動" };
		int body_type = static_cast<int>(body->type);
		if (editor_ui::Combo("Body Type", &body_type, body_types, 3)) body->type = static_cast<rigid_body_type>(body_type);
		record_component_edit();
		editor_ui::DragFloat("Mass", &body->mass, 0.05f, 0.001f, 10000.0f); record_component_edit();
		editor_ui::DragFloat3("Velocity", body->velocity, 0.05f); record_component_edit();
		editor_ui::SliderFloat("Gravity Scale", &body->gravity_scale, 0.0f, 5.0f); record_component_edit();
		editor_ui::SliderFloat("Restitution", &body->restitution, 0.0f, 1.0f); record_component_edit();
		editor_ui::SliderFloat("Friction", &body->friction, 0.0f, 1.0f); record_component_edit();
		editor_ui::Checkbox("Freeze X", &body->freeze_position[0]); record_component_edit();
		editor_ui::Checkbox("Freeze Y", &body->freeze_position[1]); record_component_edit();
		editor_ui::Checkbox("Freeze Z", &body->freeze_position[2]); record_component_edit();
	}
	if (auto* controller = scene_.character_controller(selected_entity_))
	{
		ImGui::Separator(); editor_ui::TextUnformatted("Character Controller Component");
		editor_ui::Checkbox("Controller Enabled", &controller->enabled); record_component_edit();
		editor_ui::DragFloat3("Desired Velocity", controller->desired_velocity, 0.05f); record_component_edit();
		editor_ui::DragFloat("Gravity", &controller->gravity, 0.1f, 0.0f, 100.0f); record_component_edit();
		editor_ui::DragFloat("Jump Speed", &controller->jump_speed, 0.1f, 0.0f, 100.0f); record_component_edit();
		editor_ui::SliderFloat("Slope Limit", &controller->slope_limit_degrees, 0.0f, 89.0f); record_component_edit();
		editor_ui::DragFloat("Step Height", &controller->step_height, 0.01f, 0.0f, 2.0f); record_component_edit();
		ImGui::Text(u8"接地: %s", controller->grounded ? u8"している" : u8"していない");
	}
	if (auto* selected_camera = scene_.camera(selected_entity_))
	{
		ImGui::Separator(); editor_ui::TextUnformatted("Camera Component");
		editor_ui::DragFloat3("Target", selected_camera->target, 0.01f);
		record_component_edit();
		editor_ui::SliderFloat("Distance", &selected_camera->distance, 0.5f, 50.0f);
		record_component_edit();
		editor_ui::SliderFloat("FOV", &selected_camera->fov_degrees, 20.0f, 100.0f);
		record_component_edit();
		editor_ui::DragFloat("Yaw", &selected_camera->yaw_degrees, 0.25f);
		record_component_edit();
		editor_ui::DragFloat("Pitch", &selected_camera->pitch_degrees, 0.25f);
		record_component_edit();
	}
	if (scene_.character_controller(selected_entity_) && !scene_.get<playtest_controller_component>(selected_entity_))
	{
		if (editor_ui::Button("Add Playtest Controller")) { push_undo(); scene_.add<playtest_controller_component>(selected_entity_); }
	}
	if (auto* test=scene_.get<playtest_controller_component>(selected_entity_))
	{
		ImGui::Separator(); editor_ui::TextUnformatted("Player Controller");
        editor_ui::Checkbox("First Person",&test->first_person); record_component_edit();
        editor_ui::SliderFloat("Eye Height (m)",&test->eye_height,0.2f,3.0f); record_component_edit();
        editor_ui::SliderFloat("Mouse Sensitivity",&test->mouse_sensitivity,0.01f,0.5f); record_component_edit();
        editor_ui::SliderFloat("Player FOV",&test->field_of_view,45.0f,110.0f); record_component_edit();
		editor_ui::SliderFloat("Walk Speed (m/s)",&test->move_speed,0.5f,12.0f); record_component_edit();
		editor_ui::SliderFloat("Sprint Multiplier",&test->sprint_multiplier,1.0f,3.0f); record_component_edit();
        editor_ui::DragFloat("Fall Respawn Y",&test->respawn_height,0.1f); record_component_edit();
		editor_ui::SliderFloat("Follow Camera Distance",&test->camera_distance,3.0f,20.0f); record_component_edit();
		ImGui::TextWrapped(u8"上部のプレイで操作確認。Checkpoint・Goal・Hazardタグの検知範囲へ反応します。");
	}
	if (auto* selected_floor = scene_.reflection_floor(selected_entity_))
	{
		ImGui::Separator(); editor_ui::TextUnformatted("Reflection Floor Component");
		editor_ui::Checkbox("Visible", &selected_floor->enabled);
		record_component_edit();
		editor_ui::Checkbox("Reflection Enabled", &selected_floor->reflection_enabled);
		record_component_edit();
		editor_ui::Checkbox("Stable Planar Reflection", &selected_floor->planar_reflection);
		record_component_edit();
		ImGui::TextDisabled(selected_floor->planar_reflection ?
			u8"床面の鏡像を別に描画します。" : u8"画面内だけで反射を探します。画面外の物体は欠けます。");
		editor_ui::DragFloat("Floor Height", &selected_floor->height, 0.01f);
		record_component_edit();
		editor_ui::ColorEdit3("Floor Color", selected_floor->color);
		record_component_edit();
		editor_ui::SliderFloat("Strength", &selected_floor->strength, 0.0f, 1.5f);
		record_component_edit();
		if (!selected_floor->planar_reflection)
		{
			editor_ui::SliderFloat("Ray Distance", &selected_floor->max_distance, 1.0f, 50.0f);
			record_component_edit();
			editor_ui::SliderFloat("Thickness", &selected_floor->thickness, 0.01f, 1.0f);
			record_component_edit();
			editor_ui::SliderInt("Steps", &selected_floor->steps, 8, 128);
			record_component_edit();
		}
	}
	if (auto* selected_light = scene_.directional_light(selected_entity_))
	{
		ImGui::Separator(); editor_ui::TextUnformatted("Directional Light Component");
		editor_ui::Checkbox("Enabled", &selected_light->enabled);
		record_component_edit();
		editor_ui::DragFloat3("Direction", selected_light->direction, 0.01f, -1.0f, 1.0f);
		record_component_edit();
		editor_ui::ColorEdit3("Sun Color", selected_light->color);
		record_component_edit();
		editor_ui::SliderFloat("Sun Intensity", &selected_light->intensity, 0.0f, 10.0f);
		record_component_edit();
		editor_ui::ColorEdit3("Sky Color", selected_light->sky_color);
		record_component_edit();
		editor_ui::ColorEdit3("Ground Bounce Color", selected_light->ground_color);
		record_component_edit();
		editor_ui::SliderFloat("Ambient Intensity", &selected_light->ambient_intensity, 0.0f, 3.0f);
		record_component_edit();
		editor_ui::Checkbox("Sun Shadows", &selected_light->shadows);
		record_component_edit();
		editor_ui::SliderFloat("Shadow Strength", &selected_light->shadow_strength, 0.0f, 1.0f);
		record_component_edit();
		editor_ui::SliderFloat("Shadow Range (m)", &selected_light->shadow_distance, 4.0f, 100.0f);
		record_component_edit();
		editor_ui::SliderFloat("Shadow Bias", &selected_light->shadow_bias, 0.0f, 0.005f, "%.5f");
		record_component_edit();
		ImGui::TextWrapped(u8"太陽は主光源、空と地面は回り込む光です。影の範囲を狭めると近くの影が細かくなります。");
	}
	if (auto* selected_post = scene_.post_process(selected_entity_))
	{
		ImGui::Separator(); editor_ui::TextUnformatted("Post Process Component");
		editor_ui::SliderFloat("Exposure", &selected_post->exposure, 0.1f, 4.0f);
		record_component_edit();
		editor_ui::Checkbox("Ambient Occlusion", &selected_post->ambient_occlusion);
		record_component_edit();
		editor_ui::SliderFloat("AO Strength", &selected_post->ao_strength, 0.0f, 2.0f);
		record_component_edit();
		editor_ui::SliderFloat("AO Radius (m)", &selected_post->ao_radius, 0.05f, 3.0f);
		record_component_edit();
	}
	if (auto* selected_settings = scene_.rendering_settings(selected_entity_))
	{
		ImGui::Separator(); editor_ui::TextUnformatted("Rendering Settings Component");
		editor_ui::Checkbox("VSync", &selected_settings->vsync);
		record_component_edit();
		editor_ui::SliderInt("FPS Limit (0 = Unlimited)", &selected_settings->frame_rate_limit, 0, 240);
		record_component_edit();
		editor_ui::SliderInt("Background FPS", &selected_settings->background_frame_rate, 1, 60);
		record_component_edit();
		ImGui::TextDisabled(selected_settings->vsync ?
			u8"前面のFPSは画面の更新周期に同期します。" : u8"前面のFPSを設定した上限に抑えます。");
		if (model_renderer_.mesh_shader_available())
		{
			editor_ui::Checkbox("Mesh Shaders", &selected_settings->mesh_shaders);
			record_component_edit();
		}
		else ImGui::TextDisabled(u8"Mesh Shader: GPUまたはドライバーが未対応です。");
		editor_ui::SliderFloat("IBL Intensity", &selected_settings->ibl_intensity, 0.0f, 2.0f);
		record_component_edit();
	}
	end_editor_disabled();
	if (!model_resources_.last_error().empty()) ImGui::TextWrapped(u8"素材の読込みエラー: %s", model_resources_.last_error().c_str());
	if (!editor_error_.empty()) ImGui::TextWrapped(u8"操作エラー: %s", editor_error_.c_str());
	ImGui::End();
	ImGui::Render();
	imgui_.render(commands);
#endif
	renderer_.end_frame(settings.vsync);
}

void native_preview12::shutdown()
{
	if (walk_test_) stop_walk_test();
	model_resources_.stop();
	renderer_.wait_idle();
#ifdef USE_IMGUI
	imgui_.shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif
	animation_system_.clear();
	physics_system_.clear();
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
	case WM_KILLFOCUS:
		if (walk_test_) set_player_paused(true);
        mouse_look_=false; ClipCursor(nullptr); SetCursor(LoadCursor(nullptr,IDC_ARROW)); return 0;
	case WM_LBUTTONDOWN:
		if (first_person_active_ && !player_paused_) { mouse_look_=true; update_player_cursor(); } break;
	case WM_SETCURSOR:
		if (first_person_active_ && !player_paused_ && mouse_look_ && LOWORD(lparam)==HTCLIENT) { SetCursor(nullptr); return TRUE; } break;
	case WM_INPUT:
		if (first_person_active_ && !player_paused_ && mouse_look_ && GetForegroundWindow()==hwnd_)
		{
			RAWINPUT input{}; UINT bytes=sizeof(input);
			if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam),RID_INPUT,&input,&bytes,sizeof(RAWINPUTHEADER))!=UINT(-1) &&
				input.header.dwType==RIM_TYPEMOUSE && !(input.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE))
			{
				if (auto* camera=scene_.camera(camera_entity_))
				{
					playtest_system::apply_mouse_look(camera->yaw_degrees,camera->pitch_degrees,
						float(input.data.mouse.lLastX),float(input.data.mouse.lLastY),mouse_sensitivity_);
				}
			}
		}
		break; // DefWindowProcがRaw Inputの内部領域を後処理する。
	case WM_SIZE:
		if (wparam != SIZE_MINIMIZED) {
			pending_width_.store(LOWORD(lparam), std::memory_order_release);
			pending_height_.store(HIWORD(lparam), std::memory_order_release);
		} return 0;
	case WM_RBUTTONDOWN:
		if (first_person_active_ || (walk_test_ && player_paused_)) return 0;
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
		if (first_person_active_ || (walk_test_ && player_paused_)) return 0;
		if (scene_hovered_)
			if (auto* camera = scene_.camera(camera_entity_)) camera->distance = (std::clamp)(camera->distance -
				GET_WHEEL_DELTA_WPARAM(wparam) / 120.0f * camera->wheel_zoom_speed, 0.5f, 50.0f);
		return 0;
	case WM_DESTROY: PostQuitMessage(0); return 0;
	case WM_KEYDOWN:
		// プレイ中のEscはポーズ切替。停止と配置復元はポーズ画面から明示的に選ぶ。
		if (walk_test_) { if (wparam==VK_ESCAPE && !(lparam & (1LL<<30))) set_player_paused(!player_paused_); return 0; }
#ifdef USE_IMGUI
		if (wparam==VK_ESCAPE && ImGui::GetDragDropPayload())
		{
			drag_cancelled_=true; model_preview_.visible=false; return 0;
		}
		// Popupを閉じるEscをアプリ終了へ流さない。閉じる処理はImGuiが次フレームで行う。
		if (wparam==VK_ESCAPE && ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup)) return 0;
#endif
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
		if (wparam == VK_SPACE) { toggle_play(); return 0; }
		if (wparam == 'F') { focus_selected(); return 0; }
		if (wparam == 'W') { gizmo_operation_ = 0; return 0; }
		if (wparam == 'E') { gizmo_operation_ = 1; return 0; }
		if (wparam == 'R') { gizmo_operation_ = 2; return 0; }
		if (wparam == VK_ESCAPE) {
			// 配置中のEscは終了操作だけに使い、誤ってEditor自体を閉じない。
			if (map_editor_system_.settings().enabled) map_editor_system_.settings().enabled = false;
			else if (borderless_fullscreen_) toggle_fullscreen(); else PostMessageW(hwnd, WM_CLOSE, 0, 0);
			return 0;
		} break;
	}
	return DefWindowProcW(hwnd, message, wparam, lparam);
}
