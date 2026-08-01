#include <memory>
#include <sstream>
#include <imgui.h>

#include "Framework.h"
#include "Graphics.h"
#include "ImGuiRenderer.h"
#include "Scene/ModelViewerScene.h"
#include "Scene/WeightedCollisionScene.h"
#include "Scene/RaceRankingScene.h"
#include "Scene/SphereCastMoveScene.h"
#include "Scene/SwordTrailScene.h"
#include "Scene/RootMotionScene.h"
#include "Scene/RootMotionExScene.h"
#include "Scene/ConfirmCommandScene.h"
#include "Scene/SpaceDivisionRaycastScene.h"
#include "Scene/SortingAndFilteringScene.h"
#include "Scene/LookAtScene.h"
#include "Scene/TwoBoneIKScene.h"
#include "Scene/SphereVsTriangleCollisionScene.h"

// ポストプロセス描画クラス
#include "Misc.h"
#include "GpuResourceUtils.h"
class PostprocessRenderer{
	template<class T> using com_ptr = Microsoft::WRL::ComPtr<T>;
	com_ptr<ID3D11RenderTargetView> sceneRTV;
	com_ptr<ID3D11ShaderResourceView> sceneRTSRV;
	com_ptr<ID3D11DepthStencilView> sceneDSV;
	D3D11_VIEWPORT sceneViewport;

	com_ptr<ID3D11VertexShader> vertexShader;
	com_ptr<ID3D11PixelShader> pixelShader;

	struct color_filter {
		float	hueShift;	// 色相調整
		float	saturation;	// 彩度調整
		float	brightness;	// 明度調整
		float	dummy;
	};
	com_ptr<ID3D11Buffer> colorFilterCB;
	com_ptr<ID3D11PixelShader> colorFilterPS;


public:
	PostprocessRenderer() {
		HRESULT hr;
		auto device = Graphics::Instance().GetDevice();
		// シーンテクスチャ類生成
		{
			com_ptr<ID3D11Texture2D> texture2d;
			D3D11_TEXTURE2D_DESC texture2dDesc;
			texture2dDesc.Width = Graphics::Instance().GetScreenWidth();
			texture2dDesc.Height = Graphics::Instance().GetScreenHeight();
			texture2dDesc.MipLevels = 1;
			texture2dDesc.ArraySize = 1;
			texture2dDesc.SampleDesc.Count = 1;
			texture2dDesc.SampleDesc.Quality = 0;
			texture2dDesc.Usage = D3D11_USAGE_DEFAULT;
			texture2dDesc.CPUAccessFlags = 0;
			texture2dDesc.MiscFlags = 0;

			// レンダーターゲットビュー生成
			texture2dDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			texture2dDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			hr = device->CreateTexture2D(&texture2dDesc, nullptr,texture2d.ReleaseAndGetAddressOf());
			_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
			hr = device->CreateRenderTargetView(texture2d.Get(), nullptr, sceneRTV.ReleaseAndGetAddressOf());
			_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
			//	シェーダーリソースビュー生成
			hr = device->CreateShaderResourceView(texture2d.Get(), nullptr, sceneRTSRV.ReleaseAndGetAddressOf());
			_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

			// 深度ステンシルビュー生成
			texture2dDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			texture2dDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			hr = device->CreateTexture2D(&texture2dDesc, nullptr, texture2d.ReleaseAndGetAddressOf());
			_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
			hr = device->CreateDepthStencilView(texture2d.Get(), nullptr, sceneDSV.ReleaseAndGetAddressOf());
			_ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
		}

		// ビューポート
		sceneViewport.Width = Graphics::Instance().GetScreenWidth();
		sceneViewport.Height = Graphics::Instance().GetScreenHeight();
		sceneViewport.TopLeftX = 0;	sceneViewport.TopLeftY = 0;
		sceneViewport.MinDepth = 0;	sceneViewport.MaxDepth = 1;

		// デフォルトシェーダー生成
		GpuResourceUtils::LoadVertexShader(device,
			"Data/Shader/FullscreenQuadVS.cso", nullptr, 0,
			nullptr, vertexShader.ReleaseAndGetAddressOf());
		GpuResourceUtils::LoadPixelShader(device,
			"Data/Shader/FullscreenQuadPS.cso",
			pixelShader.ReleaseAndGetAddressOf());
		// カラーフィルター生成
		{
			GpuResourceUtils::LoadPixelShader(device, "Data/Shader/ColorFilterPS.cso",
				colorFilterPS.ReleaseAndGetAddressOf());
			GpuResourceUtils::CreateConstantBuffer(device, sizeof(color_filter),
				colorFilterCB.ReleaseAndGetAddressOf());
		}
	}
	// クリア
	void Clear(float r, float g, float b, float a) {
		auto dc = Graphics::Instance().GetDeviceContext();
		float color[4] = { r, g, b, a };
		dc->ClearRenderTargetView(sceneRTV.Get(), color);
		dc->ClearDepthStencilView(sceneDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0);
	}
	// レンダーターゲット/深度ステンシル設定
	void SetRenderTargets() {
		auto dc = Graphics::Instance().GetDeviceContext();
		dc->OMSetRenderTargets(1, sceneRTV.GetAddressOf(), sceneDSV.Get());
		dc->RSSetViewports(1, &sceneViewport);
	}
	// 描画処理
	void Render() {
		auto dc = Graphics::Instance().GetDeviceContext();
		auto rs = Graphics::Instance().GetRenderState();
		auto sampler = rs->GetSamplerState(SamplerState::LinearClamp);
		dc->IASetInputLayout(nullptr);
		dc->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		dc->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		dc->VSSetShader(vertexShader.Get(), nullptr, 0);
		//dc->PSSetShader(pixelShader.Get(), nullptr, 0);
		{
			dc->PSSetShader(colorFilterPS.Get(), nullptr, 0);
			color_filter constant;
			constant.brightness = 1.2f;
			constant.hueShift = 180;
			constant.saturation = 1.5f;
			dc->UpdateSubresource(colorFilterCB.Get(),
				0, nullptr, &constant, 0, 0);
			dc->PSSetConstantBuffers(0, 1,
				colorFilterCB.GetAddressOf());
		}
		dc->PSSetShaderResources(0, 1, sceneRTSRV.GetAddressOf());
		dc->PSSetSamplers(0, 1, &sampler);
		dc->Draw(4, 0);
		ID3D11ShaderResourceView* srvs[1] = { nullptr };
		dc->PSSetShaderResources(0, 1, srvs);
	}
};
bool usePostprocess = true;
std::unique_ptr<PostprocessRenderer> postprocessRenderer;

// 垂直同期間隔設定
static const int syncInterval = 1;

// コンストラクタ
Framework::Framework(HWND hWnd)
	: hWnd(hWnd)
{
	// グラフィックス初期化
	Graphics::Instance().Initialize(hWnd);

	// IMGUI初期化
	ImGuiRenderer::Initialize(hWnd, Graphics::Instance().GetDevice(), Graphics::Instance().GetDeviceContext());

	// シーン初期化
	//scene = std::make_unique<ModelViewerScene>();
	scene = std::make_unique<WeightedCollisionScene>();
	//scene = std::make_unique<RaceRankingScene>();
	//scene = std::make_unique<ConfirmCommandScene>();
	//scene = std::make_unique<SortingAndFilteringScene>();
	//scene = std::make_unique<RootMotionScene>();
	//scene = std::make_unique<RootMotionExScene>();
	//scene = std::make_unique<SwordTrailScene>();
	//scene = std::make_unique<SphereVsTriangleCollisionScene>();
	//scene = std::make_unique<TwoBoneIKScene>();
	//scene = std::make_unique<LookAtScene>();
	//scene = std::make_unique<SpaceDivisionRaycastScene>();
	//scene = std::make_unique<SphereCastMoveScene>();
	// ポストプロセスレンダラー生成
	postprocessRenderer = std::make_unique<PostprocessRenderer>();
}

// デストラクタ
Framework::~Framework()
{
	postprocessRenderer.reset();
	// IMGUI終了化
	ImGuiRenderer::Finalize();
}

// 更新処理
void Framework::Update(float elapsedTime)
{
	// IMGUIフレーム開始処理	
	ImGuiRenderer::NewFrame();

	// シーン更新処理
	scene->Update(elapsedTime);
}

// 描画処理
void Framework::Render(float elapsedTime){
	ID3D11DeviceContext* dc = Graphics::Instance().GetDeviceContext();
	if (usePostprocess) {
		postprocessRenderer->Clear(0.5f, 0.5f, 0.5f, 1);		// 画面クリア
		postprocessRenderer->SetRenderTargets();		// レンダーターゲット設定
	} else {
		Graphics::Instance().Clear(0.5f, 0.5f, 0.5f, 1);		// 画面クリア
		Graphics::Instance().SetRenderTargets();		// レンダーターゲット設定
	}

	// シーン描画処理
	scene->Render(elapsedTime);

	// ポストプロセス適応 (↑の中でUI系の描画をしていたら、ここではダメです。)
	if (usePostprocess) {
		Graphics::Instance().Clear(0.5f, 0.5f, 0.5f, 1);		// 画面クリア
		Graphics::Instance().SetRenderTargets();		// レンダーターゲット設定
		postprocessRenderer->Render();	// ポストプロセス適応
	}

	// シーンGUI描画処理
	scene->DrawGUI();

	// シーン切り替えGUI
	SceneSelectGUI();
#if 0
	// IMGUIデモウインドウ描画（IMGUI機能テスト用）
	ImGui::ShowDemoWindow();
#endif
	// IMGUI描画
	ImGuiRenderer::Render(dc);

	// 画面表示
	Graphics::Instance().Present(syncInterval);
}

template<class T>
void Framework::ChangeSceneButtonGUI(const char* name)
{
	if (ImGui::Button(name))
	{
		scene = std::make_unique<T>();
	}
}

// シーン切り替えGUI
void Framework::SceneSelectGUI()
{
	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	ImVec2 pos = ImGui::GetMainViewport()->GetWorkPos();
	float width = 210;
	float height = 490;
	ImGui::SetNextWindowPos(ImVec2(pos.x + displaySize.x - width - 10, pos.y + 10), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Once);

	if (ImGui::Begin("Scene"))
	{
		ChangeSceneButtonGUI<ModelViewerScene>(u8"00.モデルビューア");
		ChangeSceneButtonGUI<WeightedCollisionScene>(u8"01.重みのある衝突処理");
		ChangeSceneButtonGUI<RaceRankingScene>(u8"02.レース順位判定処理");
		ChangeSceneButtonGUI<SphereCastMoveScene>(u8"03.スフィアキャスト移動処理");
		ChangeSceneButtonGUI<SwordTrailScene>(u8"04.剣の軌跡処理");
		ChangeSceneButtonGUI<RootMotionScene>(u8"05.ルートモーション処理");
		ChangeSceneButtonGUI<RootMotionExScene>(u8"05.ルートモーションEX処理");
		ChangeSceneButtonGUI<ConfirmCommandScene>(u8"06.コマンド判定処理");
		ChangeSceneButtonGUI<SpaceDivisionRaycastScene>(u8"07.空間分割レイキャスト");
		ChangeSceneButtonGUI<SortingAndFilteringScene>(u8"08.ソート＆フィルタリング処理");
		ChangeSceneButtonGUI<LookAtScene>(u8"09.ルックアット処理");
		ChangeSceneButtonGUI<TwoBoneIKScene>(u8"10.2本のボーンIK制御");
		ChangeSceneButtonGUI<SphereVsTriangleCollisionScene>(u8"11.球と三角形の衝突処理");
	}
	ImGui::End();
}

// フレームレート計算
void Framework::CalculateFrameStats()
{
	// Code computes the average frames per second, and also the 
	// average time it takes to render one frame.  These stats 
	// are appended to the window caption bar.
	static int frames = 0;
	static float time_tlapsed = 0.0f;

	frames++;

	// Compute averages over one second period.
	if ((timer.TimeStamp() - time_tlapsed) >= 1.0f)
	{
		float fps = static_cast<float>(frames); // fps = frameCnt / 1
		float mspf = 1000.0f / fps;
		std::ostringstream outs;
		outs.precision(6);
		outs << "FPS : " << fps << " / " << "Frame Time : " << mspf << " (ms)";
		SetWindowTextA(hWnd, outs.str().c_str());

		// Reset for next average.
		frames = 0;
		time_tlapsed += 1.0f;
	}
}

// アプリケーションループ
int Framework::Run()
{
	MSG msg = {};

	while (WM_QUIT != msg.message)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			timer.Tick();
			CalculateFrameStats();

			float elapsedTime = timer.TimeInterval();
			Update(elapsedTime);
			Render(elapsedTime);
		}
	}
	return static_cast<int>(msg.wParam);
}

// メッセージハンドラ
LRESULT CALLBACK Framework::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGuiRenderer::HandleMessage(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc;
		hdc = BeginPaint(hWnd, &ps);
		EndPaint(hWnd, &ps);
		break;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_CREATE:
		break;
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) PostMessage(hWnd, WM_CLOSE, 0, 0);
		break;
	case WM_ENTERSIZEMOVE:
		// WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
		timer.Stop();
		break;
	case WM_EXITSIZEMOVE:
		// WM_EXITSIZEMOVE is sent when the user releases the resize bars.
		// Here we reset everything based on the new window dimensions.
		timer.Start();
		break;
	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}
