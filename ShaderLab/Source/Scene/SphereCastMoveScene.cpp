#include <algorithm>
#include <imgui.h>
#include <ImGuizmo.h>
#include <SphereCast.h>
#include "Graphics.h"
#include "Scene/SphereCastMoveScene.h"

// コンストラクタ
SphereCastMoveScene::SphereCastMoveScene()
{
	ID3D11Device* device = Graphics::Instance().GetDevice();
	float screenWidth = Graphics::Instance().GetScreenWidth();
	float screenHeight = Graphics::Instance().GetScreenHeight();

	position = { 13.0f, 0.5f, 16.0f };

	// カメラ設定
	camera.SetPerspectiveFov(
		DirectX::XMConvertToRadians(45),	// 画角
		screenWidth / screenHeight,			// 画面アスペクト比
		0.1f,								// ニアクリップ
		1000.0f								// ファークリップ
	);
	camera.SetLookAt(
		{ position.x, position.y + 12, position.z + 12 },	// 視点
		{ position.x, position.y, position.z },				// 注視点
		{ 0, 1, 0 }			// 上ベクトル
	);
	cameraController.SyncCameraToController(camera);

	// モデル読み込み
	character = std::make_shared<Model>(device, "Data/Model/Mr.Incredible/Mr.Incredible.glb");
	stage = std::make_shared<Model>(device, "Data/Model/Sponza/gltf/Sponza.gltf");
	//stage = std::make_shared<Model>(device, "Data/Model/Stage/ExampleStage.glb");

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
		}
	}
}

// 更新処理
void SphereCastMoveScene::Update(float elapsedTime)
{
	// カメラ更新処理
	cameraController.Update();
	cameraController.SyncControllerToCamera(camera);

	return;	//	授業で処理落ちを防ぐための簡易対応

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
	DirectX::XMFLOAT3 move;
	{
		const DirectX::XMFLOAT3& front = camera.GetFront();
		const DirectX::XMFLOAT3& right = camera.GetRight();
		float frontLengthXZ = sqrtf(front.x * front.x + front.z * front.z);
		float rightLengthXZ = sqrtf(right.x * right.x + right.z * right.z);
		float frontX = front.x / frontLengthXZ;
		float frontZ = front.z / frontLengthXZ;
		float rightX = right.x / rightLengthXZ;
		float rightZ = right.z / rightLengthXZ;
		move.x = frontX * vec.z + rightX * vec.x;
		move.z = frontZ * vec.z + rightZ * vec.x;
	}
	const float gravity = 3.0f * elapsedTime;
	{
		move.y = -gravity;
	}

	// 移動処理
	DirectX::XMFLOAT3 moveXZ = { move.x, 0, move.z };
	DirectX::XMFLOAT3 moveY = { 0, move.y, 0 };
	MoveAndSlide(moveXZ, false);
	MoveAndSlide(moveY, true);
	//MoveAndSlide(move, false);
}

// 描画処理
void SphereCastMoveScene::Render(float elapsedTime)
{
	ID3D11DeviceContext* dc = Graphics::Instance().GetDeviceContext();
	RenderState* renderState = Graphics::Instance().GetRenderState();
	PrimitiveRenderer* primitiveRenderer = Graphics::Instance().GetPrimitiveRenderer();
	ShapeRenderer* shapeRenderer = Graphics::Instance().GetShapeRenderer();
	ModelRenderer* modelRenderer = Graphics::Instance().GetModelRenderer();

	// キャラクタートランスフォーム更新
	DirectX::XMMATRIX WorldTransform = DirectX::XMMatrixTranslation(position.x, position.y, position.z);
	DirectX::XMFLOAT4X4 worldTransform;
	DirectX::XMStoreFloat4x4(&worldTransform, WorldTransform);
	character->UpdateTransform(worldTransform);

	// レンダーステート設定
	dc->OMSetDepthStencilState(renderState->GetDepthStencilState(DepthState::TestAndWrite), 0);
	dc->OMSetBlendState(renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);
	dc->RSSetState(renderState->GetRasterizerState(RasterizerState::SolidCullNone));

	// カプセル描画
	const DirectX::XMFLOAT4 capsuleColor = { 0, 1, 0, 1 };
	DirectX::XMMATRIX CapsuleTransform = DirectX::XMMatrixTranslation(position.x, position.y, position.z);
	DirectX::XMFLOAT4X4 capsuleTransform;
	DirectX::XMStoreFloat4x4(&capsuleTransform, CapsuleTransform);
	capsuleTransform._42 += radius + stepOffset * 0.5f;
	shapeRenderer->DrawCapsule(capsuleTransform, radius, stepOffset, capsuleColor);
	shapeRenderer->DrawCapsule(capsuleTransform, radius + skinWidth, stepOffset, { 1,0,0,1 });
	shapeRenderer->Render(dc, camera.GetView(), camera.GetProjection());

	// レンダーステート設定
	dc->RSSetState(renderState->GetRasterizerState(RasterizerState::SolidCullBack));

	// 描画コンテキスト設定
	RenderContext rc;
	rc.deviceContext = dc;
	rc.renderState = renderState;
	rc.camera = &camera;

	//	ライト情報を設定
	rc.lightManager = &lightManager;

	// モデル描画
	modelRenderer->Draw(ShaderId::Phong, stage);
	modelRenderer->Draw(ShaderId::Phong, character);
	modelRenderer->Render(rc);
}

// GUI描画処理
void SphereCastMoveScene::DrawGUI()
{
	ImVec2 pos = ImGui::GetMainViewport()->GetWorkPos();
	ImGui::SetNextWindowPos(ImVec2(pos.x + 10, pos.y + 10), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_Once);

	if (ImGui::Begin(u8"スフィアキャストを用いた移動処理"))
	{
		ImGui::DragFloat3(u8"Position", &position.x, 0.01f);
		ImGui::DragFloat(u8"Radius", &radius, 0.01f, 0.01f, 0);
		ImGui::DragFloat(u8"SkinWidth", &skinWidth, 0.01f, 0.01f, 0);
		ImGui::DragFloat(u8"StepOffset", &stepOffset, 0.01f, 0.01f, 0);
		ImGui::DragFloat(u8"SlopeLimit", &slopeLimit, 1);

		auto dirLight = lightManager.GetDirectionalLight();
		ImGui::SliderFloat3(u8"DirLight Direction",
			&dirLight.direction.x, -1, 1);
		ImGui::ColorEdit3(u8"DirLight Color",
			&dirLight.color.x);
		lightManager.SetDirectionalLight(dirLight);
	}
	ImGui::End();
}

// スフィアキャスト
bool SphereCastMoveScene::SphereCast(
	const DirectX::XMFLOAT3& origin,
	const DirectX::XMFLOAT3& direction,
	float radius,
	float& distance,
	DirectX::XMFLOAT3& hitPosition,
	DirectX::XMFLOAT3& hitNormal)
{
	bool hit = false;
	DirectX::XMVECTOR Start = DirectX::XMLoadFloat3(&origin);
	DirectX::XMVECTOR Direction = DirectX::XMLoadFloat3(&direction);
	DirectX::XMVECTOR End = DirectX::XMVectorAdd(Start, DirectX::XMVectorScale(Direction, distance));

	for (const CollisionMesh::Triangle& triangle : collisionMesh.triangles)
	{
		DirectX::XMVECTOR Positions[3] =
		{
			DirectX::XMLoadFloat3(&triangle.positions[0]),
			DirectX::XMLoadFloat3(&triangle.positions[1]),
			DirectX::XMLoadFloat3(&triangle.positions[2])
		};

		SphereCastResult result;
		if (IntersectSphereCastVsTriangle(Start, End, radius, Positions, &result))
		{
			if (distance < result.distance) continue;
			distance = result.distance;
			DirectX::XMVECTOR HitPosition = DirectX::XMVectorAdd(Start, DirectX::XMVectorScale(Direction, distance));
			DirectX::XMStoreFloat3(&hitPosition, HitPosition);
			DirectX::XMStoreFloat3(&hitNormal, result.normal);
			hit = true;
		}
	}
	return hit;
}

// 移動＆滑り
void SphereCastMoveScene::MoveAndSlide(const DirectX::XMFLOAT3& move, bool vertical)
{
	DirectX::XMVECTOR Position = DirectX::XMLoadFloat3(&position);
	DirectX::XMVECTOR Move = DirectX::XMLoadFloat3(&move);
	DirectX::XMVECTOR CenterOffset = DirectX::XMVectorSet(0, radius + stepOffset, 0, 0);

	// スフィアキャストによる移動＆滑り処理
	{

	}

	DirectX::XMStoreFloat3(&position, Position);
}
