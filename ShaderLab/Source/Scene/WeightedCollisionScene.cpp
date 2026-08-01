#include <imgui.h>
#include <ImGuizmo.h>
#include "Graphics.h"
#include "Scene/WeightedCollisionScene.h"

// コンストラクタ
WeightedCollisionScene::WeightedCollisionScene()
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
		{ 0, 10, -10 },		// 視点
		{ 0, 0, 0 },		// 注視点
		{ 0, 1, 0 }			// 上ベクトル
	);
	cameraController.SyncCameraToController(camera);

	// オブジェクト初期化
	objs[0].position = { -1, 0, 0 };
	objs[0].color = { 1, 0, 0, 1 };
	objs[0].weight = 1.0f;
	objs[1].position = { 1, 0, 0 };
	objs[1].color = { 0, 0, 1, 1 };
	objs[1].weight = 10.0f;
}

// 更新処理
void WeightedCollisionScene::Update(float elapsedTime)
{
	// カメラ更新処理
	cameraController.Update();
	cameraController.SyncControllerToCamera(camera);

	// オブジェクト移動操作
	const float speed = 1.0f * elapsedTime;
	DirectX::XMFLOAT3 vec[2] = {};
	if (GetAsyncKeyState('W') & 0x8000)
	{
		vec[0].z += speed;
	}
	if (GetAsyncKeyState('S') & 0x8000)
	{
		vec[0].z -= speed;
	}
	if (GetAsyncKeyState('D') & 0x8000)
	{
		vec[0].x += speed;
	}
	if (GetAsyncKeyState('A') & 0x8000)
	{
		vec[0].x -= speed;
	}
	if (GetAsyncKeyState(VK_UP) & 0x8000)
	{
		vec[1].z += speed;
	}
	if (GetAsyncKeyState(VK_DOWN) & 0x8000)
	{
		vec[1].z -= speed;
	}
	if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
	{
		vec[1].x += speed;
	}
	if (GetAsyncKeyState(VK_LEFT) & 0x8000)
	{
		vec[1].x -= speed;
	}
	// カメラの向きを考慮した移動処理
	const DirectX::XMFLOAT3& front = camera.GetFront();
	const DirectX::XMFLOAT3& right = camera.GetRight();
	float frontLengthXZ = sqrtf(front.x * front.x + front.z * front.z);
	float rightLengthXZ = sqrtf(right.x * right.x + right.z * right.z);
	float frontX = front.x / frontLengthXZ;
	float frontZ = front.z / frontLengthXZ;
	float rightX = right.x / rightLengthXZ;
	float rightZ = right.z / rightLengthXZ;
	for (int i = 0; i < 2; ++i)
	{
		objs[i].position.x += frontX * vec[i].z + rightX * vec[i].x;
		objs[i].position.z += frontZ * vec[i].z + rightZ * vec[i].x;
	}

	// 球と球の衝突処理
	for (int i = 0; i < _countof(objs); ++i)
	{
		Object& a = objs[i];
		for (int j = i + 1; j < _countof(objs); ++j)
		{
			Object& b = objs[j];
		}
	}
}

// 描画処理
void WeightedCollisionScene::Render(float elapsedTime)
{
	ID3D11DeviceContext* dc = Graphics::Instance().GetDeviceContext();
	RenderState* renderState = Graphics::Instance().GetRenderState();
	PrimitiveRenderer* primitiveRenderer = Graphics::Instance().GetPrimitiveRenderer();
	ShapeRenderer* shapeRenderer = Graphics::Instance().GetShapeRenderer();

	// レンダーステート設定
	dc->OMSetBlendState(renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);
	dc->OMSetDepthStencilState(renderState->GetDepthStencilState(DepthState::TestAndWrite), 0);
	dc->RSSetState(renderState->GetRasterizerState(RasterizerState::SolidCullNone));

	// グリッド描画
	primitiveRenderer->DrawGrid(20, 1);
	primitiveRenderer->Render(dc, camera.GetView(), camera.GetProjection(), D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	// シェイプ描画
	for (const Object& obj : objs)
	{
		shapeRenderer->DrawSphere(obj.position, obj.radius, obj.color);
	}
	shapeRenderer->Render(dc, camera.GetView(), camera.GetProjection());
}

// GUI描画処理
void WeightedCollisionScene::DrawGUI()
{
	ImVec2 pos = ImGui::GetMainViewport()->GetWorkPos();

	ImGui::SetNextWindowPos(ImVec2(pos.x + 10, pos.y + 10), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(300, 180), ImGuiCond_Once);

	if (ImGui::Begin(u8"重みのある衝突処理", nullptr, ImGuiWindowFlags_NoNavInputs))
	{
		ImGui::Text(u8"[0]操作：WASD");
		ImGui::Text(u8"[1]操作：方向キー");
		
		ImGui::Columns(3, "columns");
		ImGui::SetColumnWidth(0, 50);
		ImGui::SetColumnWidth(1, 100);
		ImGui::SetColumnWidth(2, 100);
		ImGui::Separator();

		ImGui::Text("Index"); ImGui::NextColumn();
		ImGui::Text("Weight"); ImGui::NextColumn();
		ImGui::Text("Color"); ImGui::NextColumn();
		ImGui::Separator();

		for (int i = 0; i < _countof(objs); ++i)
		{
			Object& obj = objs[i];

			ImGui::PushID(&obj);
			ImGui::Text("[%d]", i); ImGui::NextColumn();
			ImGui::SliderFloat("", &obj.weight, 0.1f, 10, "%.1f");	ImGui::NextColumn();
			ImGui::ColorEdit4("color", &obj.color.x, ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs);	ImGui::NextColumn();
			ImGui::PopID();
			ImGui::Separator();
		}
		ImGui::Columns(1);
	}
	ImGui::End();
}
