#include <functional>
#include <imgui.h>
#include <ImGuizmo.h>
#include "Graphics.h"
#include "Scene/LookAtScene.h"

// コンストラクタ
LookAtScene::LookAtScene()
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
		{ 3, 2, 3 },		// 視点
		{ 0, 1, 0 },		// 注視点
		{ 0, 1, 0 }			// 上ベクトル
	);
	cameraController.SyncCameraToController(camera);

	// モデル読み込み
	character = std::make_shared<Model>(device, "Data/Model/unitychan/unitychan.glb");
	character->GetNodePoses(nodePoses);

	// 頭ノード取得
	int headNodeIndex = character->GetNodeIndex("Character1_Head");
	Model::Node& headNode = character->GetNodes().at(headNodeIndex);

	// 初期姿勢時の頭ノードのローカル空間前方向を求める
	{

	}

	// ターゲット位置
	targetPosition = { 0, 2, 1 };

}

// 更新処理
void LookAtScene::Update(float elapsedTime)
{
	// カメラ更新処理
	cameraController.Update();
	cameraController.SyncControllerToCamera(camera);

	const int animationIndex = character->GetAnimationIndex("Idle");
	
	// 指定時間のアニメーションの姿勢を取得
	character->ComputeAnimation(animationIndex, animationSeconds, nodePoses);

	// アニメーション時間更新
	const Model::Animation& animation = character->GetAnimations().at(animationIndex);
	animationSeconds += elapsedTime;
	if (animationSeconds > animation.secondsLength)
	{
		animationSeconds -= animation.secondsLength;
	}

	// 姿勢更新
	character->SetNodePoses(nodePoses);

	// キャラクタートランスフォーム更新
	DirectX::XMFLOAT4X4 worldTransform;
	DirectX::XMStoreFloat4x4(&worldTransform, DirectX::XMMatrixIdentity());
	character->UpdateTransform(worldTransform);

	// ワールド行列更新処理関数
	std::function<void(Model::Node&)> updateWorldTransforms = [&](Model::Node& node)
	{
		DirectX::XMMATRIX S = DirectX::XMMatrixScaling(node.scale.x, node.scale.y, node.scale.z);
		DirectX::XMMATRIX R = DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&node.rotation));
		DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(node.position.x, node.position.y, node.position.z);
		DirectX::XMMATRIX LocalTransform = S * R * T;
		DirectX::XMMATRIX ParentGlobalTransform = node.parent != nullptr
			? DirectX::XMLoadFloat4x4(&node.parent->globalTransform)
			: DirectX::XMMatrixIdentity();
		DirectX::XMMATRIX GlobalTransform = LocalTransform * ParentGlobalTransform;
		DirectX::XMMATRIX WorldTransform = GlobalTransform * DirectX::XMLoadFloat4x4(&worldTransform);
		DirectX::XMStoreFloat4x4(&node.localTransform, LocalTransform);
		DirectX::XMStoreFloat4x4(&node.globalTransform, GlobalTransform);
		DirectX::XMStoreFloat4x4(&node.worldTransform, WorldTransform);

		for (Model::Node* child : node.children)
		{
			updateWorldTransforms(*child);
		}
	};

	// 頭ノード取得
	int headNodeIndex = character->GetNodeIndex("Character1_Head");
	Model::Node& headNode = character->GetNodes().at(headNodeIndex);

	// 頭がターゲット位置を正面に捉えるように回転させる
	{

	}
}

// 描画処理
void LookAtScene::Render(float elapsedTime)
{
	ID3D11DeviceContext* dc = Graphics::Instance().GetDeviceContext();
	RenderState* renderState = Graphics::Instance().GetRenderState();
	PrimitiveRenderer* primitiveRenderer = Graphics::Instance().GetPrimitiveRenderer();
	ShapeRenderer* shapeRenderer = Graphics::Instance().GetShapeRenderer();
	ModelRenderer* modelRenderer = Graphics::Instance().GetModelRenderer();

	// ターゲット位置をギズモで動かす
	const DirectX::XMFLOAT4X4& view = camera.GetView();
	const DirectX::XMFLOAT4X4& projection = camera.GetProjection();
	DirectX::XMFLOAT4X4 world;
	DirectX::XMStoreFloat4x4(&world, DirectX::XMMatrixTranslation(targetPosition.x, targetPosition.y, targetPosition.z));
	ImGuizmo::Manipulate(
		&view._11, &projection._11,
		ImGuizmo::TRANSLATE,
		ImGuizmo::WORLD,
		&world._11,
		nullptr);
	targetPosition.x = world._41;
	targetPosition.y = world._42;
	targetPosition.z = world._43;

	// レンダーステート設定
	dc->OMSetBlendState(renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);
	dc->OMSetDepthStencilState(renderState->GetDepthStencilState(DepthState::TestAndWrite), 0);
	dc->RSSetState(renderState->GetRasterizerState(RasterizerState::SolidCullNone));

	// グリッド描画
	primitiveRenderer->DrawGrid(20, 1);
	primitiveRenderer->Render(dc, camera.GetView(), camera.GetProjection(), D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	// ターゲット球描画
	shapeRenderer->DrawSphere(targetPosition, 0.1f, { 1, 0, 0, 1 });
	shapeRenderer->Render(dc, camera.GetView(), camera.GetProjection());

	// 描画コンテキスト設定
	RenderContext rc;
	rc.deviceContext = dc;
	rc.renderState = renderState;
	rc.camera = &camera;

	// モデル描画
	modelRenderer->Draw(ShaderId::Basic, character);
	modelRenderer->Render(rc);
}

// GUI描画処理
void LookAtScene::DrawGUI()
{
	ImVec2 pos = ImGui::GetMainViewport()->GetWorkPos();
	ImGui::SetNextWindowPos(ImVec2(pos.x + 10, pos.y + 10), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(300, 100), ImGuiCond_Once);

	if (ImGui::Begin(u8"ルックアット"))
	{
	}
	ImGui::End();
}
