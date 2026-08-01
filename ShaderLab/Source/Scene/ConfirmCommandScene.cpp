#include <imgui.h>
#include "Graphics.h"
#include "Scene/ConfirmCommandScene.h"

// コンストラクタ
ConfirmCommandScene::ConfirmCommandScene()
{
	ID3D11Device* device = Graphics::Instance().GetDevice();
	float screenWidth = Graphics::Instance().GetScreenWidth();
	float screenHeight = Graphics::Instance().GetScreenHeight();

	// カメラ設定
	camera.SetPerspectiveFov(
		DirectX::XMConvertToRadians(45),	// 画角
		screenWidth / screenHeight,			// 画面アスペクト比
		0.1f,								// ニアクリップ
		1000.0f								// ファークリップ
	);
	camera.SetLookAt(
		{ 8, 3, 0 },		// 視点
		{ 0, 2, 0 },		// 注視点
		{ 0, 1, 0 }			// 上ベクトル
	);

	// スプライト読み込み
	sprite = std::make_unique<Sprite>(device, "Data/Sprite/InputKeyIcon.png");

	// モデル読み込み
	character = std::make_shared<Model>(device, "Data/Model/RPG-Character/RPG-Character.glb");
	character->GetNodePoses(nodePoses);
}

// 更新処理
void ConfirmCommandScene::Update(float elapsedTime)
{
	// キー入力情報取得
	bool up = GetAsyncKeyState('W') & 0x8000;
	bool left = GetAsyncKeyState('A') & 0x8000;
	bool down = GetAsyncKeyState('S') & 0x8000;
	bool right = GetAsyncKeyState('D') & 0x8000;
	bool punch = GetAsyncKeyState('P') & 0x8000;
	bool kick = GetAsyncKeyState('K') & 0x8000;

	InputKey key = 0;
	key |= (!up && down && left && !right) ? KEY_1 : NOT_1;
	key |= (!up && down && !left && !right) ? KEY_2 : NOT_2;
	key |= (!up && down && !left && right) ? KEY_3 : NOT_3;
	key |= (!up && !down && left && !right) ? KEY_4 : NOT_4;
	key |= (!up && !down && !left && !right) ? KEY_5 : NOT_5;
	key |= (!up && !down && !left && right) ? KEY_6 : NOT_6;
	key |= (up && !down && left && !right) ? KEY_7 : NOT_7;
	key |= (up && !down && !left && !right) ? KEY_8 : NOT_8;
	key |= (up && !down && !left && right) ? KEY_9 : NOT_9;
	key |= punch ? KEY_P : NOT_P;
	key |= kick ? KEY_K : NOT_K;

	// キー入力設定
	SetInputKey(key);

	// コマンドを確認
	static const Command command = Command({ KEY_2, KEY_3, KEY_6, KEY_P });
	if (ConfirmCommand(command, 14))
	{
		animationIndex = character->GetAnimationIndex("Slash");
		animationSeconds = 0;
	}


	// 指定時間のアニメーション
	if (animationIndex >= 0)
	{
		character->ComputeAnimation(animationIndex, animationSeconds, nodePoses);

		// 時間更新
		const Model::Animation& animation = character->GetAnimations().at(animationIndex);
		animationSeconds += elapsedTime;
		if (animationSeconds > animation.secondsLength)
		{
			animationSeconds = animation.secondsLength;
		}
	}

	// ノードポーズ更新
	character->SetNodePoses(nodePoses);

	// トランスフォーム更新
	DirectX::XMFLOAT4X4 worldTransform;
	DirectX::XMStoreFloat4x4(&worldTransform, DirectX::XMMatrixIdentity());
	character->UpdateTransform(worldTransform);
}

// 描画処理
void ConfirmCommandScene::Render(float elapsedTime)
{
	ID3D11DeviceContext* dc = Graphics::Instance().GetDeviceContext();
	RenderState* renderState = Graphics::Instance().GetRenderState();
	PrimitiveRenderer* primitiveRenderer = Graphics::Instance().GetPrimitiveRenderer();
	ModelRenderer* modelRenderer = Graphics::Instance().GetModelRenderer();

	// レンダーステート設定
	dc->OMSetBlendState(renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);
	dc->OMSetDepthStencilState(renderState->GetDepthStencilState(DepthState::TestAndWrite), 0);
	dc->RSSetState(renderState->GetRasterizerState(RasterizerState::SolidCullNone));

	// グリッド描画
	primitiveRenderer->DrawGrid(20, 1);
	primitiveRenderer->Render(dc, camera.GetView(), camera.GetProjection(), D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	// 描画コンテキスト設定
	RenderContext rc;
	rc.deviceContext = dc;
	rc.renderState = renderState;
	rc.camera = &camera;
	rc.lightManager = &lightManager;

	// モデル描画
	modelRenderer->Draw(ShaderId::Basic, character);
	modelRenderer->Render(rc);

	// レンダーステート設定
	dc->OMSetBlendState(renderState->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);
	dc->OMSetDepthStencilState(renderState->GetDepthStencilState(DepthState::NoTestNoWrite), 0);
	dc->RSSetState(renderState->GetRasterizerState(RasterizerState::SolidCullNone));
	// サンプラステート設定
	ID3D11SamplerState* samplerStates[] =
	{
		rc.renderState->GetSamplerState(SamplerState::LinearWrap)
	};
	dc->PSSetSamplers(0, _countof(samplerStates), samplerStates);

	// キー入力バッファ描画
	DrawInputKeys(dc, 0, 160, inputKeys);
}

// GUI描画処理
void ConfirmCommandScene::DrawGUI()
{
	ImVec2 pos = ImGui::GetMainViewport()->GetWorkPos();

	ImGui::SetNextWindowPos(ImVec2(pos.x + 10, pos.y + 10), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_Once);

	if (ImGui::Begin(u8"コマンド判定処理"))
	{
		ImGui::Text(u8"方向キー：WASD");
		ImGui::Text(u8"パンチキー：P");
		ImGui::Text(u8"キックキー：K");
		ImGui::Text(u8"コマンド：236P");
	}
	ImGui::End();
}

// 入力キー設定
void ConfirmCommandScene::SetInputKey(InputKey key)
{
}

// コマンド確認
bool ConfirmCommandScene::ConfirmCommand(const Command& command, int frame)
{
	return false;
}

// 入力キー描画
void ConfirmCommandScene::DrawInputKeys(ID3D11DeviceContext* dc, float x, float y, const InputKey inputKeys[])
{
	const float iconSize = 112;
	const float size = iconSize * 0.5f;
	float offsetY = 0;
	// コマンドを逆順に確認する
	int index = 0;
	for (int index = 0; index < MAX_INPUT_KEY; ++index)
	{
		InputKey key = inputKeys[index];

		float offsetX = 0;
		float column = 0, row = 0;
		if (key & KEY_1) { column = 1; row = 3; }
		else if (key & KEY_2) { column = 2; row = 3; }
		else if (key & KEY_3) { column = 3; row = 3; }
		else if (key & KEY_4) { column = 3; row = 1; }
		//else if (key & KEY_5) { column = 4; row = 1; }
		else if (key & KEY_6) { column = 0; row = 2; }
		else if (key & KEY_7) { column = 0; row = 0; }
		else if (key & KEY_8) { column = 1; row = 0; }
		else if (key & KEY_9) { column = 2; row = 0; }
		if (key & (KEY_1 | KEY_2 | KEY_3 | KEY_4 | KEY_6 | KEY_7 | KEY_8 | KEY_9))
		{
			sprite->Render(dc, x + offsetX, y + offsetY, 0, size, size, iconSize * column, iconSize * row, iconSize, iconSize, 0, 1, 1, 1, 1);
			offsetX += size;
		}
		if (key & KEY_P)
		{
			sprite->Render(dc, x + offsetX, y + offsetY, 0, size, size, iconSize * 1, iconSize * 1, iconSize, iconSize, 0, 1, 1, 1, 1);
			offsetX += size;
		}
		if (key & KEY_K)
		{
			sprite->Render(dc, x + offsetX, y + offsetY, 0, size, size, iconSize * 2, iconSize * 1, iconSize, iconSize, 0, 1, 1, 1, 1);
			offsetX += size;
		}

		// 同じキーが続く場合はスキップ
		while (index < MAX_INPUT_KEY && (inputKeys[index] & key) == key)
		{
			index++;
		}
		if (offsetX > 0)
		{
			offsetY += size;
		}
	}
}
