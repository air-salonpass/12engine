#include <imgui.h>
#include <ImGuizmo.h>
#include "Graphics.h"
#include "Scene/SpaceDivisionRaycastScene.h"

// コンストラクタ
SpaceDivisionRaycastScene::SpaceDivisionRaycastScene()
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
		{ 0, 10, 10 },		// 視点
		{ 0, 0, 0 },		// 注視点
		{ 0, 1, 0 }			// 上ベクトル
	);
	cameraController.SyncCameraToController(camera);

	// モデル読み込み
	character = std::make_shared<Model>(device, "Data/Model/Mr.Incredible/Mr.Incredible.glb");
	stage = std::make_shared<Model>(device, "Data/Model/Stage/ExampleStage.glb");

	DirectX::XMVECTOR VolumeMin = DirectX::XMVectorReplicate(FLT_MAX);
	DirectX::XMVECTOR VolumeMax = DirectX::XMVectorReplicate(-FLT_MAX);

	// 頂点データをワールド空間変換し、三角形データを作成
	for (const Model::Mesh& mesh : stage->GetMeshes())
	{
		DirectX::XMMATRIX WorldTransform = DirectX::XMLoadFloat4x4(&mesh.node->worldTransform);
		for (size_t i = 0; i < mesh.indices.size(); i += 3)
		{
			// 頂点データをワールド空間変換
			uint32_t a = mesh.indices.at(i + 0);
			uint32_t b = mesh.indices.at(i + 1);
			uint32_t c = mesh.indices.at(i + 2);
			DirectX::XMVECTOR A = DirectX::XMLoadFloat3(&mesh.vertices.at(a).position);
			DirectX::XMVECTOR B = DirectX::XMLoadFloat3(&mesh.vertices.at(b).position);
			DirectX::XMVECTOR C = DirectX::XMLoadFloat3(&mesh.vertices.at(c).position);
			A = DirectX::XMVector3Transform(A, WorldTransform);
			B = DirectX::XMVector3Transform(B, WorldTransform);
			C = DirectX::XMVector3Transform(C, WorldTransform);

			// 法線ベクトルを算出
			DirectX::XMVECTOR N = DirectX::XMVector3Cross(DirectX::XMVectorSubtract(B, A), DirectX::XMVectorSubtract(C, A));
			if (DirectX::XMVector3Equal(N, DirectX::XMVectorZero()))
			{
				// 面を構成できない場合は除外
				continue;
			}
			N = DirectX::XMVector3Normalize(N);

			// 三角形データを格納
			CollisionMesh::Triangle& triangle = collisionMesh.triangles.emplace_back();
			DirectX::XMStoreFloat3(&triangle.positions[0], A);
			DirectX::XMStoreFloat3(&triangle.positions[1], B);
			DirectX::XMStoreFloat3(&triangle.positions[2], C);
			DirectX::XMStoreFloat3(&triangle.normal, N);

			// モデル全体のAABBを計測
			VolumeMin = DirectX::XMVectorMin(VolumeMin, A);
			VolumeMin = DirectX::XMVectorMin(VolumeMin, B);
			VolumeMin = DirectX::XMVectorMin(VolumeMin, C);
			VolumeMax = DirectX::XMVectorMax(VolumeMax, A);
			VolumeMax = DirectX::XMVectorMax(VolumeMax, B);
			VolumeMax = DirectX::XMVectorMax(VolumeMax, C);
		}
	}

	// モデル全体のAABB
	DirectX::XMFLOAT3 volumeMin, volumeMax;
	DirectX::XMStoreFloat3(&volumeMin, VolumeMin);
	DirectX::XMStoreFloat3(&volumeMax, VolumeMax);

	// モデル全体のAABBからXZ平面に指定のサイズで分割されたコリジョンエリアを作成する
	{
		const int cellSize = 4;

	}
}

// 更新処理
void SpaceDivisionRaycastScene::Update(float elapsedTime)
{
	// カメラ更新処理
	cameraController.Update();
	cameraController.SyncControllerToCamera(camera);

	// オブジェクト移動操作
	const float speed = 5.0f * elapsedTime;
	DirectX::XMFLOAT3 vec = { 0, 0, 0 };
	if (GetAsyncKeyState(VK_UP) & 0x8000)
	{
		vec.z += speed;
	}
	if (GetAsyncKeyState(VK_DOWN) & 0x8000)
	{
		vec.z -= speed;
	}
	if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
	{
		vec.x += speed;
	}
	if (GetAsyncKeyState(VK_LEFT) & 0x8000)
	{
		vec.x -= speed;
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
	characterPosition.x += frontX * vec.z + rightX * vec.x;
	characterPosition.z += frontZ * vec.z + rightZ * vec.x;

	float distance = 100.0f;
	DirectX::XMFLOAT3 start = { characterPosition.x, characterPosition.y + 1.0f, characterPosition.z };
	DirectX::XMFLOAT3 end = { characterPosition.x, characterPosition.y + -1.0f, characterPosition.z };
	DirectX::XMFLOAT3 hitPosition, hitNormal;
	timer.Tick();
	if (Raycast(start, end, hitPosition, hitNormal))
	{
		characterPosition.y = hitPosition.y;
	}
	timer.Tick();

	// 時間計測
	totalTime += timer.TimeInterval();
	frames++;
	if (frames == 60)
	{
		averageTime = totalTime / frames;
		frames = 0;
		totalTime = 0;
	}
}

// 描画処理
void SpaceDivisionRaycastScene::Render(float elapsedTime)
{
	ID3D11DeviceContext* dc = Graphics::Instance().GetDeviceContext();
	RenderState* renderState = Graphics::Instance().GetRenderState();
	PrimitiveRenderer* primitiveRenderer = Graphics::Instance().GetPrimitiveRenderer();
	ShapeRenderer* shapeRenderer = Graphics::Instance().GetShapeRenderer();
	ModelRenderer* modelRenderer = Graphics::Instance().GetModelRenderer();

	// キャラクタートランスフォーム更新
	DirectX::XMMATRIX WorldTransform = DirectX::XMMatrixTranslation(characterPosition.x, characterPosition.y, characterPosition.z);
	DirectX::XMFLOAT4X4 worldTransform;
	DirectX::XMStoreFloat4x4(&worldTransform, WorldTransform);
	character->UpdateTransform(worldTransform);

	// レンダーステート設定
	dc->OMSetDepthStencilState(renderState->GetDepthStencilState(DepthState::TestAndWrite), 0);
	dc->OMSetBlendState(renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);
	dc->RSSetState(renderState->GetRasterizerState(RasterizerState::SolidCullNone));

	// 三角形エッジ描画
	const DirectX::XMFLOAT4 edgeColor = { 0, 0, 1, 1 };
	for (CollisionMesh::Triangle& triangle : collisionMesh.triangles)
	{
		primitiveRenderer->AddVertex(triangle.positions[0], edgeColor);
		primitiveRenderer->AddVertex(triangle.positions[1], edgeColor);
		primitiveRenderer->AddVertex(triangle.positions[1], edgeColor);
		primitiveRenderer->AddVertex(triangle.positions[2], edgeColor);
		primitiveRenderer->AddVertex(triangle.positions[2], edgeColor);
		primitiveRenderer->AddVertex(triangle.positions[0], edgeColor);
	}
	primitiveRenderer->Render(dc, camera.GetView(), camera.GetProjection(), D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	// バウンディングボックス描画
	const DirectX::XMFLOAT4 boxColor = { 0, 1, 0, 1 };
	const DirectX::XMFLOAT3 boxAngle = { 0, 0, 0 };
	for (CollisionMesh::Area& area : collisionMesh.areas)
	{
		shapeRenderer->DrawBox(area.boundingBox.Center, boxAngle, area.boundingBox.Extents, boxColor);
	}
	shapeRenderer->Render(dc, camera.GetView(), camera.GetProjection());

	// レンダーステート設定
	dc->RSSetState(renderState->GetRasterizerState(RasterizerState::SolidCullBack));

	// 描画コンテキスト設定
	RenderContext rc;
	rc.deviceContext = dc;
	rc.renderState = renderState;
	rc.camera = &camera;

	// モデル描画
	modelRenderer->Draw(ShaderId::Basic, stage);
	modelRenderer->Draw(ShaderId::Basic, character);
	modelRenderer->Render(rc);

	// レンダーステート設定
	dc->OMSetBlendState(renderState->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);

	// 三角形ポリゴン描画
	const DirectX::XMFLOAT4 polygonColor = { 1, 0, 0, 0.5f };
	for (CollisionMesh::Triangle& triangle : collisionMesh.triangles)
	{
		primitiveRenderer->AddVertex(triangle.positions[0], polygonColor);
		primitiveRenderer->AddVertex(triangle.positions[1], polygonColor);
		primitiveRenderer->AddVertex(triangle.positions[2], polygonColor);
	}
	primitiveRenderer->Render(dc, camera.GetView(), camera.GetProjection(), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

// GUI描画処理
void SpaceDivisionRaycastScene::DrawGUI()
{
	ImVec2 pos = ImGui::GetMainViewport()->GetWorkPos();
	ImGui::SetNextWindowPos(ImVec2(pos.x + 10, pos.y + 10), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(300, 100), ImGuiCond_Once);

	if (ImGui::Begin(u8"空間分割レイキャスト"))
	{
		ImGui::Checkbox(u8"空間分割", &spaceDivision);
		ImGui::InputFloat(u8"処理時間", &averageTime, 0, 0, "%.7f", ImGuiInputTextFlags_ReadOnly);
	}
	ImGui::End();
}

// レイキャスト
bool SpaceDivisionRaycastScene::Raycast(
	const DirectX::XMFLOAT3& start,
	const DirectX::XMFLOAT3& end,
	DirectX::XMFLOAT3& hitPosition,
	DirectX::XMFLOAT3& hitNormal)
{
	bool hit = false;
	DirectX::XMVECTOR Start = DirectX::XMLoadFloat3(&start);
	DirectX::XMVECTOR End = DirectX::XMLoadFloat3(&end);
	DirectX::XMVECTOR Vec = DirectX::XMVectorSubtract(End, Start);
	DirectX::XMVECTOR Direction = DirectX::XMVector3Normalize(Vec);
	float distance = DirectX::XMVectorGetX(DirectX::XMVector3Length(Vec));

	// 空間分割せず、普通にレイキャストをする
	if (!spaceDivision)
	{
		for (const CollisionMesh::Triangle& triangle : collisionMesh.triangles)
		{
			DirectX::XMVECTOR A = DirectX::XMLoadFloat3(&triangle.positions[0]);
			DirectX::XMVECTOR B = DirectX::XMLoadFloat3(&triangle.positions[1]);
			DirectX::XMVECTOR C = DirectX::XMLoadFloat3(&triangle.positions[2]);

			float dist = distance;
			if (DirectX::TriangleTests::Intersects(Start, Direction, A, B, C, dist))
			{
				if (distance < dist) continue;
				distance = dist;
				hitNormal = triangle.normal;
				hit = true;
			}
		}
	}
	// 空間分割したデータを使い、レイキャストを高速に処理する
	else
	{

	}
	if (hit)
	{
		DirectX::XMVECTOR HitPosition = DirectX::XMVectorAdd(Start, DirectX::XMVectorScale(Direction, distance));
		DirectX::XMStoreFloat3(&hitPosition, HitPosition);
	}
	return hit;
}
