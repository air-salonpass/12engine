#include <algorithm>
#include <imgui.h>
#include <ImGuizmo.h>
#include "Graphics.h"
#include "Scene/RaceRankingScene.h"

// コンストラクタ
RaceRankingScene::RaceRankingScene()
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
		{ 5, 50, 0 },		// 視点
		{ 5, 0, 5 },		// 注視点
		{ 0, 1, 0 }			// 上ベクトル
	);
	cameraController.SyncCameraToController(camera);

	// チェックポイント初期化
	checkPoints[0].position  = {  29.0f, 0.0f,  00.0f };
	checkPoints[1].position  = {  28.9f, 0.0f,  14.3f };
	checkPoints[2].position  = {  21.6f, 0.0f,  21.5f };
	checkPoints[3].position  = { -17.8f, 0.0f,  21.7f };
	checkPoints[4].position  = { -24.8f, 0.0f,  15.1f };
	checkPoints[5].position  = { -25.7f, 0.0f, - 4.3f };
	checkPoints[6].position  = { -19.8f, 0.0f, -11.1f };
	checkPoints[7].position  = { -10.0f, 0.0f, -11.8f };
	checkPoints[8].position  = { -02.9f, 0.0f, -05.0f };
	checkPoints[9].position  = { -02.9f, 0.0f,  04.1f };
	checkPoints[10].position = {  04.1f, 0.0f,  10.7f };
	checkPoints[11].position = {  13.6f, 0.0f,  10.5f };
	checkPoints[12].position = {  20.5f, 0.0f,  03.7f };
	checkPoints[13].position = {  20.5f, 0.0f, -05.8f };
	checkPoints[14].position = {  23.6f, 0.0f, -09.0f };
	checkPoints[15].position = {  25.6f, 0.0f, -08.6f };
	checkPoints[16].position = {  28.9f, 0.0f, -04.9f };

	for (int i = 0; i < _countof(checkPoints); ++i)
	{
		CheckPoint& checkPoint = checkPoints[i];
		const CheckPoint& nextCheckPoint = checkPoints[(i + 1) % _countof(checkPoints)];

		DirectX::XMVECTOR Position = DirectX::XMLoadFloat3(&checkPoint.position);
		DirectX::XMVECTOR NextPosition = DirectX::XMLoadFloat3(&nextCheckPoint.position);
		DirectX::XMVECTOR Vec = DirectX::XMVectorSubtract(NextPosition, Position);
		DirectX::XMVECTOR Direction = DirectX::XMVector3Normalize(Vec);
		DirectX::XMStoreFloat3(&checkPoint.direction, Direction);
	}

	// 車初期化
	cars[0].name = u8"Red";
	cars[0].nowCheckPointIndex = 0;
	cars[0].position = checkPoints[0].position;
	cars[0].color = { 1, 0, 0, 1 };
	cars[0].speed = 8.0f;
	cars[1].name = u8"Green";
	cars[1].nowCheckPointIndex = 0;
	cars[1].position = checkPoints[0].position;
	cars[1].color = { 0, 1, 0, 1 };
	cars[1].speed = 9.0f;
	cars[2].name = u8"Blue";
	cars[2].nowCheckPointIndex = 0;
	cars[2].position = checkPoints[0].position;
	cars[2].color = { 0, 0, 1, 1 };
	cars[2].speed = 10.0f;

	for (Car& car : cars)
	{
		rankingSortedCars.emplace_back(&car);
	}
}

// 更新処理
void RaceRankingScene::Update(float elapsedTime)
{
	// カメラ更新処理
	cameraController.Update();
	cameraController.SyncControllerToCamera(camera);

	// 車の位置更新
	int checkPoiontCount = _countof(checkPoints);
	for (int i = 0; i < _countof(cars); ++i)
	{
		Car& car = cars[i];
		const CheckPoint& nextCheckPoint = checkPoints[(car.nowCheckPointIndex + 1) % checkPoiontCount];

		// ターゲット位置算出
		DirectX::XMVECTOR NextCheckPointPosition = DirectX::XMLoadFloat3(&nextCheckPoint.position);
		DirectX::XMVECTOR NextCheckPointDirection = DirectX::XMLoadFloat3(&nextCheckPoint.direction);
		DirectX::XMVECTOR Cross = DirectX::XMVector3Cross(DirectX::XMVectorSet(0, 1, 0, 0), NextCheckPointDirection);
		Cross = DirectX::XMVector3Normalize(Cross);
		DirectX::XMVECTOR TargetPosition = DirectX::XMVectorAdd(NextCheckPointPosition, DirectX::XMVectorScale(Cross, i * car.radius * 2));
		// 移動処理
		DirectX::XMVECTOR Position = DirectX::XMLoadFloat3(&car.position);
		DirectX::XMVECTOR Direction = DirectX::XMVectorSubtract(TargetPosition, Position);
		Direction = DirectX::XMVector3Normalize(Direction);
		Position = DirectX::XMVectorAdd(Position, DirectX::XMVectorScale(Direction, car.speed * elapsedTime));
		// 位置更新
		car.oldPosition = car.position;
		DirectX::XMStoreFloat3(&car.position, Position);
		DirectX::XMStoreFloat3(&car.direction, Direction);
	}

	// チェックポイントの通過数と進行距離からランキングを判定し、ソート
	std::sort(rankingSortedCars.begin(), rankingSortedCars.end(),
		[this](const Car* lhs, const Car* rhs)
		{
			if (lhs->checkCount == rhs->checkCount)
			{
				return lhs->progress > rhs->progress;
			}
			return lhs->checkCount > rhs->checkCount;
		}
	);
	// ランキング順を保存
	for (size_t i = 0; i < rankingSortedCars.size(); ++i)
	{
		Car* car = rankingSortedCars.at(i);
		car->ranking = static_cast<int>(i + 1);
	}
	// チェックポイントの通過や進行距離を保存
	for (Car& car : cars)
	{
		// 一定時間で速度を変える
		car.timer -= elapsedTime;
		if (car.timer < 0.0f)
		{
			// 速度は順位が低いほど早くなるようにする
			car.speed = car.ranking * 2 + 5.0f;
			car.timer = car.ranking * 1.0f;
		}

		// チェックポイント情報取得
		const CheckPoint& nowCheckPoint = checkPoints[car.nowCheckPointIndex];
		const CheckPoint& nextCheckPoint = checkPoints[(car.nowCheckPointIndex + 1) % checkPoiontCount];

		DirectX::XMVECTOR OldCarPosition = DirectX::XMLoadFloat3(&car.oldPosition);
		DirectX::XMVECTOR NowCarPosition = DirectX::XMLoadFloat3(&car.position);
		DirectX::XMVECTOR NextCheckPointPosition = DirectX::XMLoadFloat3(&nextCheckPoint.position);
		DirectX::XMVECTOR NextCheckPointDirection = DirectX::XMLoadFloat3(&nextCheckPoint.direction);

		// チェックポイントの通過判定
		{
			// 通過した場合は以下のパラメータを更新する
			// car.nowCheckPointIndex ← 通過したチェックポイントのインデックス
			// car.checkPoint ← 通過したチェックポイントの数

		}
		// チェックポイント間の進行距離算出
		{
			// 毎フレーム以下のパラメータを更新する
			// car.progress ← チェックポイント間の進行距離

		}
	}

}

// 描画処理
void RaceRankingScene::Render(float elapsedTime)
{
	ID3D11DeviceContext* dc = Graphics::Instance().GetDeviceContext();
	RenderState* renderState = Graphics::Instance().GetRenderState();
	PrimitiveRenderer* primitiveRenderer = Graphics::Instance().GetPrimitiveRenderer();
	ShapeRenderer* shapeRenderer = Graphics::Instance().GetShapeRenderer();

	// 車描画
	for (Car& car : cars)
	{
		shapeRenderer->DrawSphere(car.position, car.radius, car.color);
	}

	// コース描画
	for (size_t i = 0; i < _countof(checkPoints); ++i)
	{
		const CheckPoint& checkPoint = checkPoints[i];
		const CheckPoint& nextCheckPoint = checkPoints[(i + 1) % _countof(checkPoints)];

		primitiveRenderer->AddVertex(checkPoint.position, { 1, 1, 1, 1 });
		primitiveRenderer->AddVertex(nextCheckPoint.position, { 1, 1, 1, 1 });
	}

	// レンダーステート設定
	dc->OMSetBlendState(renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);
	dc->OMSetDepthStencilState(renderState->GetDepthStencilState(DepthState::TestAndWrite), 0);
	dc->RSSetState(renderState->GetRasterizerState(RasterizerState::SolidCullNone));

	// シェイプ描画
	shapeRenderer->Render(dc, camera.GetView(), camera.GetProjection());
	primitiveRenderer->Render(dc, camera.GetView(), camera.GetProjection(), D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
}

// GUI描画処理
void RaceRankingScene::DrawGUI()
{
	ImVec2 pos = ImGui::GetMainViewport()->GetWorkPos();
	ImGui::SetNextWindowPos(ImVec2(pos.x + 300, pos.y + 100), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(430, 150), ImGuiCond_Once);

	if (ImGui::Begin(u8"レース順位判定処理"))
	{
		ImGui::Columns(5, "columns");
		ImGui::SetColumnWidth(0, 100);
		ImGui::SetColumnWidth(1, 70);
		ImGui::SetColumnWidth(2, 70);
		ImGui::SetColumnWidth(3, 100);
		ImGui::SetColumnWidth(4, 70);
		ImGui::Separator();

		ImGui::Text("Name"); ImGui::NextColumn();
		ImGui::Text("Speed"); ImGui::NextColumn();
		ImGui::Text("Ranking"); ImGui::NextColumn();
		ImGui::Text("CheckCount"); ImGui::NextColumn();
		ImGui::Text("Progress"); ImGui::NextColumn();
		ImGui::Separator();

		for (int i = 0; i < _countof(cars); ++i)
		{
			Car& car = cars[i];

			ImGui::Text("%s", car.name.c_str()); ImGui::NextColumn();
			ImGui::Text("%.1f", car.speed); ImGui::NextColumn();
			ImGui::Text("%d", car.ranking); ImGui::NextColumn();
			ImGui::Text("%d", car.checkCount); ImGui::NextColumn();
			ImGui::Text("%.1f", car.progress); ImGui::NextColumn();
			ImGui::Separator();
		}
		ImGui::Columns(1);
	}

	ImGui::End();

	// チェックポイント番号表示
	ID3D11DeviceContext* dc = Graphics::Instance().GetDeviceContext();
	D3D11_VIEWPORT viewport;
	UINT numViewports = 1;
	dc->RSGetViewports(&numViewports, &viewport);

	DirectX::XMMATRIX View = DirectX::XMLoadFloat4x4(&camera.GetView());
	DirectX::XMMATRIX Projection = DirectX::XMLoadFloat4x4(&camera.GetProjection());
	DirectX::XMMATRIX World = DirectX::XMMatrixIdentity();

	for (size_t i = 0; i < _countof(checkPoints); ++i)
	{
		const CheckPoint& checkPoint = checkPoints[i];

		DirectX::XMVECTOR WorldPosition = DirectX::XMLoadFloat3(&checkPoint.position);

		// ワールド座標からスクリーン座標へ変換
		DirectX::XMVECTOR ScreenPosition = DirectX::XMVector3Project(
			WorldPosition,
			viewport.TopLeftX,
			viewport.TopLeftY,
			viewport.Width,
			viewport.Height,
			viewport.MinDepth,
			viewport.MaxDepth,
			Projection,
			View,
			World
		);

		// スクリーン座標
		DirectX::XMFLOAT2 screenPosition;
		DirectX::XMStoreFloat2(&screenPosition, ScreenPosition);

		ImGui::SetNextWindowPos(ImVec2(pos.x + screenPosition.x, pos.y + screenPosition.y));
		ImGui::SetNextWindowBgAlpha(0.0f);

		char name[32];
		::sprintf_s(name, sizeof(name), "%zd", i);

		ImGui::Begin(name, nullptr,
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBackground |
			ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoScrollWithMouse |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoInputs);

		ImGui::Text(name);

		ImGui::End();
	}

	// 車番号と順位表示
	for (size_t i = 0; i < _countof(cars); ++i)
	{
		const Car& car = cars[i];

		DirectX::XMVECTOR WorldPosition = DirectX::XMLoadFloat3(&car.position);

		// ワールド座標からスクリーン座標へ変換
		DirectX::XMVECTOR ScreenPosition = DirectX::XMVector3Project(
			WorldPosition,
			viewport.TopLeftX,
			viewport.TopLeftY,
			viewport.Width,
			viewport.Height,
			viewport.MinDepth,
			viewport.MaxDepth,
			Projection,
			View,
			World
		);

		// スクリーン座標
		DirectX::XMFLOAT2 screenPosition;
		DirectX::XMStoreFloat2(&screenPosition, ScreenPosition);

		ImGui::SetNextWindowPos(ImVec2(pos.x + screenPosition.x, pos.y + screenPosition.y));
		ImGui::SetNextWindowBgAlpha(0.0f);

		char name[32];
		::sprintf_s(name, sizeof(name), "[%d]", car.ranking);

		ImGui::Begin(name, nullptr,
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBackground |
			ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoScrollWithMouse |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoInputs);

		ImGui::Text(name);

		ImGui::End();
	}

	ImGui::SetNextWindowPos(ImVec2(pos.x + 410, pos.y + 10), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(300, 30), ImGuiCond_Once);
}
