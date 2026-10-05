#pragma once

#include <d3d11.h>
#include <windows.h>
#include <tchar.h>
#include <sstream>
#include "sprite.h"
#include "sprite_batch.h"

#include "misc.h"
#include "high_resolution_timer.h"

#include <memory>
#include <atomic>
#include <mutex>
#include <string>
#include <DirectXMath.h>

#include "geometric_primitive.h"
#include <wrl.h>
#include "static_mesh.h"
#include "skinned_mesh.h"
#include "framebuffer.h"
#include "fullscreen_quad.h"
#include "gltf_model.h"
#include "Scene/SceneRegistry.h"
#include "Systems/SceneSystemManager.h"
#include "Threading/AtomicJobWorker.h"
#include "WindowApplication.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#include "ThirdParty/ImGuizmo/ImGuizmo.h"
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern ImWchar glyphRangesJapanese[];
#endif

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define FULLSCREEN FALSE
#define APPLICATION_NAME L"Jisaku Engine Model Viewer"

class framework : public window_application
{
public:
	CONST HWND hwnd;
	ID3D11Device* device;
	ID3D11DeviceContext* immediate_context;
	IDXGISwapChain* swap_chain;
	ID3D11RenderTargetView* render_target_view;
	ID3D11DepthStencilView* depth_stencil_view;

	ID3D11DepthStencilState* depth_stencil_states[6]{};

	ID3D11BlendState* blend_states[4]{};

	framework(HWND hwnd);
	~framework() override;
	// Visual Studioのコード解析でも早い段階で認識できるよう、UIとウィンドウ制御の宣言をまとめる。
	void toggle_fullscreen();
#ifdef USE_IMGUI
	void setup_imgui_style();
#endif

	framework(const framework&) = delete;
	framework& operator=(const framework&) = delete;
	framework(framework&&) noexcept = delete;
	framework& operator=(framework&&) noexcept = delete;

	int run() override
	{
		MSG msg{};

		if (!initialize())
		{
			return 0;
		}

#ifdef USE_IMGUI
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::GetIO().IniFilename = nullptr;
		ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 14.0f, nullptr, glyphRangesJapanese);
		ImGui_ImplWin32_Init(hwnd);
		ImGui_ImplDX11_Init(device, immediate_context);
		setup_imgui_style();
#endif

		while (WM_QUIT != msg.message)
		{
			if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			else
			{
				if (IsIconic(hwnd))
				{
					WaitMessage();
					continue;
				}
				tictoc.tick();
				calculate_frame_stats();
				const float raw_elapsed_time = tictoc.time_interval();
				const float elapsed_time = raw_elapsed_time > 0.1f ? 0.1f : raw_elapsed_time;
				update(elapsed_time);
				render(elapsed_time);
			}
		}

#ifdef USE_IMGUI
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
#endif

#if 1
		BOOL fullscreen{};
		swap_chain->GetFullscreenState(&fullscreen, 0);
		if (fullscreen)
		{
			swap_chain->SetFullscreenState(FALSE, 0);
		}
#endif

		return uninitialize() ? static_cast<int>(msg.wParam) : 0;
	}

	LRESULT CALLBACK handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) override
	{
		// Ctrl+D/Ctrl+SはWM_KEYDOWNの初回だけ処理し、フレーム速度による多重実行を防ぐ。
		if (msg == WM_KEYDOWN && (lparam & (1LL << 30)) == 0 &&
			(GetKeyState(VK_CONTROL) & 0x8000))
		{
			if (wparam == 'D')
			{
				duplicate_selected_entity();
				return 0;
			}
			if (wparam == 'S')
			{
				browse_save_scene((GetKeyState(VK_SHIFT) & 0x8000) != 0);
				return 0;
			}
		}
		// F11とAlt+EnterはUI入力中でも使える、エディタ全体のフルスクリーンショートカットにする。
		const bool fullscreen_shortcut =
			(msg == WM_KEYDOWN && wparam == VK_F11) ||
			(msg == WM_SYSKEYDOWN && wparam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000));
		if (fullscreen_shortcut)
		{
			// キーリピートでは切替えず、1回押すごとに1回だけ状態を反転する。
			if ((lparam & (1LL << 30)) == 0)
			{
				toggle_fullscreen();
			}
			return 0;
		}
#ifdef USE_IMGUI
		if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) { return true; }
#endif
		switch (msg)
		{
		case WM_PAINT:
		{
			PAINTSTRUCT ps{};
			BeginPaint(hwnd, &ps);

			EndPaint(hwnd, &ps);
		}
		break;

		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		case WM_SIZE:
			if (wparam != SIZE_MINIMIZED)
			{
				const UINT width = LOWORD(lparam);
				const UINT height = HIWORD(lparam);
				if (width > 0 && height > 0) resize_render_targets(width, height);
			}
			break;
		case WM_CREATE:
			break;
		case WM_KEYDOWN:
			if (wparam == VK_ESCAPE)
			{
				// フルスクリーン中のEscは終了ではなく、まずウィンドウ表示へ戻す。
				if (borderlessFullscreen) toggle_fullscreen();
				else PostMessage(hwnd, WM_CLOSE, 0, 0);
			}
			break;
		case WM_ENTERSIZEMOVE:
			tictoc.stop();
			break;
		case WM_EXITSIZEMOVE:
			tictoc.start();
			break;
		default:
			return DefWindowProc(hwnd, msg, wparam, lparam);
		}
		return 0;
	}

private:
	sprite* sprites[8]{};
	std::unique_ptr<sprite_batch> sprite_batches[8];
	ID3D11SamplerState* sampler_states[3]{};
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_states[3];
	std::unique_ptr<static_mesh> static_meshes[8];
	std::unique_ptr<skinned_mesh> skinned_meshes[8];
	std::unique_ptr<gltf_model> gltf_models[8];

	// UNIT32: HDRシーン用、半解像度の高輝度抽出用フレームバッファと全画面転送処理。
	std::unique_ptr<framebuffer> framebuffers[8];
	std::unique_ptr<fullscreen_quad> bit_block_transfer;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shaders[8];
	// UNIT40: HDRキャプチャから生成したskybox・拡散IEM・鏡面PMREM・GGX LUTを保持する。
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shader_resource_views[8];
	// Projectブラウザで使うFluent UI System Iconsの画像を保持する。
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> editor_icon_views[2];
	struct scene_constants
	{
		DirectX::XMFLOAT4X4 view_projection;
		DirectX::XMFLOAT4 light_direction;
		DirectX::XMFLOAT4 camera_position;
		// UNIT40: HDR環境光だけを調整し、直接光や背景の明るさへ影響させない。
		float ibl_intensity;
		// 平面反射モデルの透過率。通常描画では1.0を指定する。
		float reflection_alpha;
		DirectX::XMFLOAT2 scene_padding;
	};
	struct post_effect_constants
	{
		float luminance_threshold;
		float gaussian_sigma;
		float bloom_intensity;
		float exposure;
	};
	struct shader_showcase_constants
	{
		// HLSL側のビットフラグと同じ並びにし、複数エフェクトを同時に有効化できるようにする。
		uint32_t enabled_effects;
		float effect_time;
		float flow_strength;
		float flow_speed;
		float chromatic_strength;
		float vignette_strength;
		float vignette_smoothness;
		float radial_strength;
		DirectX::XMFLOAT2 radial_center;
		float radial_radius;
		float dof_focus_distance;
		float dof_range;
		float dof_blur_size;
		float fxaa_strength;
		float padding;
		DirectX::XMFLOAT4X4 projection_transform;
		DirectX::XMFLOAT4X4 inverse_projection_transform;
		DirectX::XMFLOAT4 world_up_view;
		DirectX::XMFLOAT4 ssr_parameters;
	};

	ID3D11Buffer* constant_buffers[8]{};

	std::unique_ptr<geometric_primitive> geometric_primitives[8];
	bool initialize();
	void apply_fullscreen_toggle();
	bool resize_render_targets(UINT width, UINT height);
	void update(float elapsed_time/*Elapsed seconds from last frame*/);
	void render(float elapsed_time/*Elapsed seconds from last frame*/);
	bool uninitialize();
	bool browse_and_load_gltf();
	bool browse_open_scene();
	bool browse_save_scene(bool save_as = false);
	bool save_scene(const std::string& filename);
	bool load_scene(const std::string& filename);
	void create_new_scene();
	void create_scene_service_entities();
	scene_entity instantiate_model_asset(const std::string& filename, std::size_t model_slot);
	int find_loaded_model_slot(const std::string& filename) const;
	int find_free_model_slot() const;
	bool load_gltf(const std::string& filename);
	void queue_gltf_load(const std::string& filename);
	void poll_gltf_load();
	void focus_gltf_model();
	void place_gltf_on_floor();
	void reset_model_scene_entity();
	void select_scene_entity(scene_entity entity);
	void duplicate_selected_entity();
	void sync_selected_entity_to_editor();
	void sync_editor_to_selected_entity();
	void sync_scene_components_to_editor();
	void sync_editor_to_scene_components();

private:
	// =====================================================
// ImGui Parameters
// =====================================================

	// オービットカメラが常に注視するワールド座標。
	float cameraTarget[3] = { 0.0f, 0.8f, 0.0f };
	// 注視点からカメラまでの距離。正の値で保持し、描画時に-Z座標へ変換する。
	float cameraDistance = 6.5f;
	// UNIT11以降の3Dオブジェクト全体を見渡せるよう、画角もデバッグ画面から調整する。
	float cameraFovDegrees = 40.0f;
	// マウスホイール1目盛りで変化するカメラ距離。
	float cameraWheelZoomSpeed = 0.5f;
	// 注視点を中心にした水平角・垂直角と、右ドラッグの回転感度。
	float cameraYawDegrees = 0.0f;
	float cameraPitchDegrees = 0.0f;
	float cameraOrbitSensitivity = 0.2f;
	bool focusKeyWasDown = false;
	bool vsyncEnabled = true;

	// ブルームと最終出力をCPU／ImGuiから調整する値。Releaseでも初期値を使って描画する。
	float luminanceThreshold = 0.8f;
	float gaussianSigma = 1.0f;
	float bloomIntensity = 1.0f;
	float toneMappingExposure = 1.2f;
	// シェーダーII・IIIの教材機能をエディタ上で比較するための有効状態と調整値。
	bool shaderFlowEnabled = false;
	bool shaderDofEnabled = false;
	bool shaderVignetteEnabled = false;
	bool shaderChromaticEnabled = false;
	bool shaderRadialBlurEnabled = false;
	bool shaderFxaaEnabled = false;
	bool shaderDemoBackgroundEnabled = false;
	bool shaderSplitComparisonEnabled = false;
	bool shaderSsrEnabled = true;
	bool shaderSsrFloorEnabled = true;
	float shaderEffectTime = 0.0f;
	float shaderFlowStrength = 0.35f;
	float shaderFlowSpeed = 1.0f;
	float shaderChromaticStrength = 0.025f;
	float shaderVignetteStrength = 1.15f;
	float shaderVignetteSmoothness = 0.45f;
	float shaderRadialStrength = 0.05f;
	float shaderRadialCenter[2] = { 0.5f, 0.5f };
	float shaderRadialRadius = 0.15f;
	float shaderDofFocusDistance = 0.065f;
	float shaderDofRange = 0.04f;
	float shaderDofBlurSize = 3.0f;
	float shaderFxaaStrength = 0.65f;
	float shaderSsrStrength = 1.10f;
	float shaderSsrMaxDistance = 15.0f;
	// 厚みを抑えて誤ヒットを減らし、二分探索で交差位置を補正する。
	float shaderSsrThickness = 0.50f;
	float shaderSsrStride = 0.25f;
	// UNIT40: 正式生成したHDRの高輝度を保ちつつ、白い材質の飽和を抑える初期強度。
	float iblIntensity = 0.35f;

	float lightX = 0.0f;
	float lightY = 0.0f;
	float lightZ = 1.0f;

	// UNIT1～41の成果を一画面で確認するショーケース表示。Releaseでも初期値のまま全項目を描画する。
	bool showSpriteShowcase = false;
	bool showPrimitiveShowcase = false;
	bool showStaticMeshShowcase = false;
	bool showSkinnedMeshShowcase = false;
	bool showGltfShowcase = true;
	// 補助線は必要なときだけ表示し、通常のモデル確認画面はすっきり見せる。
	bool showViewerGrid = false;
	bool showModelBounds = false;
	bool showBackdrop = false;
	bool showPrimitiveWireframe = false;
	bool showcaseAutoRotate = false;
	int spriteBlendMode = 0;
	float showcaseRotationDegrees = 0.0f;

	float cubePos[3] =
	{
		-0.55f,
		0.0f,
		0.0f
	};

	float cubeRot[3] =
	{
		0.0f,
		180.0f,
		0.0f
	};

	float cubeScale[3] =
	{
		0.8f,
		0.8f,
		0.8f
	};

	float cubeColor[4] =
	{
		1.0f,
		1.0f,
		1.0f,
		1.0f
	};

	// UNIT41: FBXから変換した小型テストメッシュを、中央のFBXモデルと重ならない右側へ配置する。
	float gltfPosition[3] = { 0.0f, 0.0f, 0.0f };
	float gltfRotation[3] = { 0.0f, 0.0f, 0.0f };
	// UNIT41: FBX2glTFが保持した単位系に合わせ、変換済みモデルを確認できる大きさへ拡大する。
	float gltfScale = 1.0f;
	int gltfAnimationIndex = 0;
	float gltfAnimationTime = 0.0f;
	bool gltfAnimationPlaying = true;
	bool gltfAnimationLoop = true;
	float gltfAnimationSpeed = 1.0f;
	std::string loadedModelPath;
	std::string modelLoadError;
	std::string pendingModelPath;
	// モデルロード用CPUスレッドの制御状態はstd::atomicで同期する。
	// 完成したモデル本体は複合データなのでmutexを使い、atomicだけで無理に共有しない。
	atomic_job_worker modelLoadWorker;
	std::mutex modelLoadResultMutex;
	std::unique_ptr<gltf_model> pendingLoadedGltfModel;
	std::string pendingModelLoadError;
	std::atomic<bool> modelLoadCompleted{ false };
	std::atomic<bool> modelLoading{ false };
	std::size_t pendingModelSlot = 0;
	std::string modelAssetPaths[8];
	std::string currentScenePath;
	bool sceneDirty = false;
	int sceneDirtySuppressFrames = 0;
	int gizmoOperation = 0;
	// シーンギズモの座標系と使用状態を保持し、カメラ入力との競合を避ける。
	bool gizmoLocalSpace = false;
	bool gizmoIsUsing = false;
	bool gizmoIsOver = false;
	bool gizmoEnabled = true;
	#ifdef USE_IMGUI
	// Sceneウィンドウの描画リストへGizmoを確実に重ねるため、フレーム中だけ参照を保持する。
	ImDrawList* editorSceneDrawList = nullptr;
	#endif
	// Unity風エディタの各パネルとProjectブラウザの状態を保持する。
	bool editorSceneHovered = false;
	bool editorSceneFocused = false;
	bool showAdvancedRenderingWindow = false;
	// 排他モードを使わず、元のスタイルと配置を保存してボーダーレス全画面から安全に戻す。
	bool borderlessFullscreen = false;
	bool fullscreenToggleRequested = false;
	DWORD windowedStyle = 0;
	WINDOWPLACEMENT windowedPlacement{ sizeof(WINDOWPLACEMENT) };
	UINT renderWidth = SCREEN_WIDTH;
	UINT renderHeight = SCREEN_HEIGHT;
	float editorSceneRect[4]{ 240.0f, 42.0f, 720.0f, 480.0f };
	// GameビューはGizmoや選択枠を重ねず、プレイヤーが見る最終映像だけを表示する。
	float editorGameRect[4]{ 0.0f, 400.0f, 720.0f, 300.0f };
	std::string assetBrowserDirectory = ".\\resources\\gltf";
	std::string selectedAssetPath;
	double assetNavigationTime = -1.0;
	// モデル個体の状態はECSへ集約し、Hierarchy・Inspector・描画Systemから共有する。
	scene_registry sceneRegistry;
	// 更新Systemの実行順はManagerへ登録し、frameworkへ個別処理を増やさない。
	scene_system_manager sceneSystemManager;
	scene_entity selectedSceneEntity = null_scene_entity;
	scene_entity cameraSceneEntity = null_scene_entity;
	scene_entity lightSceneEntity = null_scene_entity;
	scene_entity reflectionFloorSceneEntity = null_scene_entity;
	scene_entity postProcessSceneEntity = null_scene_entity;
	scene_entity renderingSettingsSceneEntity = null_scene_entity;

	high_resolution_timer tictoc;
	uint32_t frames_per_second{ 0 };
	float count_by_seconds{ 0.0f };
	void calculate_frame_stats()
	{
		if (++frames_per_second, (tictoc.time_stamp() - count_by_seconds) >= 1.0f)
		{
			float fps = static_cast<float>(frames_per_second);
			std::wostringstream outs;
			outs.precision(6);
				outs << L"Jisaku Engine" << L" : FPS : " << fps << L" / " << L"Frame Time : " << 1000.0f / fps << L" (ms)";
			SetWindowTextW(hwnd, outs.str().c_str());

			frames_per_second = 0;
			count_by_seconds += 1.0f;
		}
	}
};
