#pragma once

#include "EditorImGui.h"
#include "EditorViewTarget.h"
#include "EnvironmentLighting.h"
#include "AnimationSystem12.h"
#include "GltfRenderer.h"
#include "ModelResourceManager.h"
#include "ScreenSpaceReflection.h"
#include "SceneGeometryRenderer.h"
#include "../WindowApplication.h"
#include "../Scene/SceneRegistry.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

// D3D12エンジンの実行・Editor統合を行うアプリケーション層。
//
// 【チームでゲーム機能を追加する順番】
// 1. Componentsへ保存したいデータを追加する。
// 2. update()から機能別Systemを呼び、ゲーム状態を更新する。
// 3. render()は確定済みComponentを読み、描画要求だけをD3D12 Rendererへ渡す。
// 4. Debug Editorで調整する値はInspectorへUIを追加する。
// 5. Sceneに残す値はsave_scene/load_scene、Prefab対象ならsave_prefab/load_prefabへ追加する。
//
// NativePreviewへゲームロジックを直接増やし続けず、処理が一機能を超えたらSystemへ分離する。
// ReleaseではUSE_IMGUIが無効になるため、ゲーム進行をEditor UIの状態へ依存させないこと。
class native_preview12 final : public window_application
{
public:
	explicit native_preview12(HWND hwnd) : hwnd_(hwnd) {}
	int run() override;
	LRESULT handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) override;

private:
	bool initialize();
	// 1フレームのゲーム状態更新。入力→System更新→描画の順序を崩さない。
	void update(float elapsed_time);
	// Componentを変更せず、update()で確定した状態を読み取って描画する。
	void render();
	void shutdown();
	void request_model_load(const std::string& filename, bool add_placement_when_ready = true);
	scene_entity create_model_entity(const std::string& filename);
	void create_scene_services();
	void poll_model_load();
	bool browse_model();
	bool browse_scene(bool save_dialog);
	bool browse_prefab(bool save_dialog);
	bool save_scene(const std::string& filename);
	bool load_scene(const std::string& filename);
	bool save_prefab(const std::string& filename, scene_entity entity);
	bool load_prefab(const std::string& filename);
	void new_scene();
	void focus_selected();
	void toggle_fullscreen();
	struct editor_snapshot
	{
		// scene_registryのコピーで全Component Storageを複製し、Undo/Redoを成立させる。
		// Componentに外部所有の生ポインターを入れると、このコピーが危険になるので禁止。
		scene_registry scene;
		scene_entity selected = null_scene_entity;
		scene_entity camera = null_scene_entity;
		scene_entity light = null_scene_entity;
		scene_entity floor = null_scene_entity;
		scene_entity post_process = null_scene_entity;
		scene_entity rendering_settings = null_scene_entity;
	};
	editor_snapshot capture_snapshot() const;
	void restore_snapshot(const editor_snapshot& snapshot);
	void push_undo();
	void push_undo(editor_snapshot snapshot);
	void undo();
	void redo();

	HWND hwnd_ = nullptr;
	directx12_renderer renderer_;
	model_resource_manager12 model_resources_;
	scene_registry scene_;
	animation_system12 animation_system_;
	scene_entity selected_entity_ = null_scene_entity;
	scene_entity camera_entity_ = null_scene_entity;
	scene_entity light_entity_ = null_scene_entity;
	scene_entity reflection_floor_entity_ = null_scene_entity;
	scene_entity post_process_entity_ = null_scene_entity;
	scene_entity rendering_settings_entity_ = null_scene_entity;
	std::unordered_set<std::string> assets_to_place_when_ready_;
	std::string editor_error_;
	std::string current_scene_path_;
	std::string asset_browser_directory_;
	std::string selected_asset_path_;
	double asset_navigation_time_ = 0.0;
	gltf_renderer12 model_renderer_;
	std::unique_ptr<environment_lighting12> environment_lighting_;
	scene_geometry_renderer12 scene_geometry_;
	scene_render_target12 scene_target_;
	// 平面対称に描いたモデルを床Shaderで合成するための専用ターゲット。
	scene_render_target12 reflection_target_;
	editor_view_target12 scene_display_target_;
	editor_view_target12 game_target_;
	screen_space_reflection12 ssr_;
#ifdef USE_IMGUI
	editor_imgui12 imgui_;
#endif
	std::atomic<UINT> pending_width_{ 0 };
	std::atomic<UINT> pending_height_{ 0 };
	bool play_mode_ = true;
	bool step_requested_ = false;
	std::vector<editor_snapshot> undo_history_;
	std::vector<editor_snapshot> redo_history_;
	bool gizmo_was_using_ = false;
	bool right_dragging_ = false;
	bool scene_hovered_ = false;
	bool gizmo_enabled_ = true;
	int gizmo_operation_ = 0;
	POINT last_mouse_{};
	int startup_error_code_ = 1;
	bool borderless_fullscreen_ = false;
	DWORD windowed_style_ = 0;
	WINDOWPLACEMENT windowed_placement_{ sizeof(WINDOWPLACEMENT) };
	float fps_elapsed_ = 0.0f;
	std::uint32_t fps_frames_ = 0;
};
