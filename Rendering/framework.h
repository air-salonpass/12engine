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
#include <DirectXMath.h>

#include "geometric_primitive.h"
#include <wrl.h>
#include "static_mesh.h"
#include "skinned_mesh.h"
#include "framebuffer.h"
#include "fullscreen_quad.h"
#include "gltf_model.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern ImWchar glyphRangesJapanese[];
#endif

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define FULLSCREEN FALSE
#define APPLICATION_NAME L"X3DGP"

class framework
{
public:
	CONST HWND hwnd;
	ID3D11Device* device;
	ID3D11DeviceContext* immediate_context;
	IDXGISwapChain* swap_chain;
	ID3D11RenderTargetView* render_target_view;
	ID3D11DepthStencilView* depth_stencil_view;

	ID3D11DepthStencilState* depth_stencil_states[4]{};

	ID3D11BlendState* blend_states[4]{};

	framework(HWND hwnd);
	~framework();

	framework(const framework&) = delete;
	framework& operator=(const framework&) = delete;
	framework(framework&&) noexcept = delete;
	framework& operator=(framework&&) noexcept = delete;

	int run()
	{
		MSG msg{};

		if (!initialize())
		{
			return 0;
		}

#ifdef USE_IMGUI
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 14.0f, nullptr, glyphRangesJapanese);
		ImGui_ImplWin32_Init(hwnd);
		ImGui_ImplDX11_Init(device, immediate_context);
		ImGui::StyleColorsDark();
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
				tictoc.tick();
				calculate_frame_stats();
				update(tictoc.time_interval());
				render(tictoc.time_interval());
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

	LRESULT CALLBACK handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
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
		case WM_CREATE:
			break;
		case WM_KEYDOWN:
			if (wparam == VK_ESCAPE)
			{
				PostMessage(hwnd, WM_CLOSE, 0, 0);
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
	std::vector<gltf_model::node> animatedGltfNodes;

	// UNIT32: HDRシーン用、半解像度の高輝度抽出用フレームバッファと全画面転送処理。
	std::unique_ptr<framebuffer> framebuffers[8];
	std::unique_ptr<fullscreen_quad> bit_block_transfer;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shaders[8];
	// UNIT40: HDRキャプチャから生成したskybox・拡散IEM・鏡面PMREM・GGX LUTを保持する。
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shader_resource_views[8];
	struct scene_constants
	{
		DirectX::XMFLOAT4X4 view_projection;
		DirectX::XMFLOAT4 light_direction;
		DirectX::XMFLOAT4 camera_position;
		// UNIT40: HDR環境光だけを調整し、直接光や背景の明るさへ影響させない。
		float ibl_intensity;
		DirectX::XMFLOAT3 scene_padding;
	};
	struct post_effect_constants
	{
		float luminance_threshold;
		float gaussian_sigma;
		float bloom_intensity;
		float exposure;
	};

	ID3D11Buffer* constant_buffers[8]{};

	std::unique_ptr<geometric_primitive> geometric_primitives[8];
	bool initialize();
	void update(float elapsed_time/*Elapsed seconds from last frame*/);
	void render(float elapsed_time/*Elapsed seconds from last frame*/);
	bool uninitialize();

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

	// ブルームと最終出力をCPU／ImGuiから調整する値。Releaseでも初期値を使って描画する。
	float luminanceThreshold = 0.8f;
	float gaussianSigma = 1.0f;
	float bloomIntensity = 1.0f;
	float toneMappingExposure = 1.2f;
	// UNIT40: 正式生成したHDRの高輝度を保ちつつ、白い材質の飽和を抑える初期強度。
	float iblIntensity = 0.35f;

	float lightX = 0.0f;
	float lightY = 0.0f;
	float lightZ = 1.0f;

	// UNIT1～41の成果を一画面で確認するショーケース表示。Releaseでも初期値のまま全項目を描画する。
	bool showSpriteShowcase = false;
	bool showPrimitiveShowcase = false;
	bool showStaticMeshShowcase = false;
	bool showSkinnedMeshShowcase = true;
	bool showGltfShowcase = true;
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
	float gltfPosition[3] = { 0.65f, 0.75f, 0.8f };
	float gltfRotation[3] = { 0.0f, 0.0f, 0.0f };
	// UNIT41: FBX2glTFが保持した単位系に合わせ、変換済みモデルを確認できる大きさへ拡大する。
	float gltfScale = 1000.0f;
	int gltfAnimationIndex = 0;
	float gltfAnimationTime = 0.0f;
	bool gltfAnimationPlaying = true;

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
			outs << L"X3DGP" << L" : FPS : " << fps << L" / " << L"Frame Time : " << 1000.0f / fps << L" (ms)";
			SetWindowTextW(hwnd, outs.str().c_str());

			frames_per_second = 0;
			count_by_seconds += 1.0f;
		}
	}
};
