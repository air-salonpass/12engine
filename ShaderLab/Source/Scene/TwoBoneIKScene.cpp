#include <algorithm>
#include <imgui.h>
#include <ImGuizmo.h>
#include "Graphics.h"
#include "Scene/TwoBoneIKScene.h"

// コンストラクタ
TwoBoneIKScene::TwoBoneIKScene()
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
		{ 10, 5, 5 },		// 視点
		{ 0, 0, 5 },		// 注視点
		{ 0, 1, 0 }			// 上ベクトル
	);
	cameraController.SyncCameraToController(camera);

	// ボーンデータ初期化
	for (int i = 0; i < _countof(bones); ++i)
	{
		Bone& bone = bones[i];

		if (i == 0)
		{
			bone.localPosition = { 0, 0, 0 };
			bone.localRotation = { 0, 0, 0, 1 };
			bone.parent = nullptr;
			bone.child = &bones[i + 1];
		}
		else
		{
			bone.localPosition = { 0, 0, 2 };
			bone.localRotation = { 0, 0, 0, 1 };
			bone.parent = &bones[i - 1];
			bone.child = (i != _countof(bones) - 1) ? &bones[i + 1] : nullptr;
		}

		// 行列計算
		DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(bone.localPosition.x, bone.localPosition.y, bone.localPosition.z);
		DirectX::XMMATRIX R = DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&bone.localRotation));
		DirectX::XMMATRIX LocalTransform = DirectX::XMMatrixMultiply(R, T);
		DirectX::XMMATRIX ParentWorldTransform = bone.parent != nullptr ? DirectX::XMLoadFloat4x4(&bone.parent->worldTransform) : DirectX::XMMatrixIdentity();
		DirectX::XMMATRIX WorldTransform = DirectX::XMMatrixMultiply(LocalTransform, ParentWorldTransform);
		DirectX::XMStoreFloat4x4(&bone.worldTransform, WorldTransform);
	}
	// ターゲット行列を初期化
	targetWorldTransform = bones[_countof(bones) - 1].worldTransform;

	// ポールターゲット行列を初期化
	Bone& midBone = bones[2];
	DirectX::XMMATRIX MidWorldTransform = DirectX::XMLoadFloat4x4(&midBone.worldTransform);
	DirectX::XMMATRIX PoleLocalTransform = DirectX::XMMatrixTranslation(0, 1, 0);
	DirectX::XMMATRIX PoleWorldTransform = DirectX::XMMatrixMultiply(PoleLocalTransform, MidWorldTransform);
	DirectX::XMStoreFloat4x4(&poleLocalTransform, PoleLocalTransform);
	DirectX::XMStoreFloat4x4(&poleWorldTransform, PoleWorldTransform);
}

// 更新処理
void TwoBoneIKScene::Update(float elapsedTime)
{
	// カメラ更新処理
	cameraController.Update();
	cameraController.SyncControllerToCamera(camera);

	// ポールターゲットのワールド行列を更新
	{
		// 中間ボーン付近にポールターゲットを配置する
		Bone& midBone = bones[2];
		DirectX::XMMATRIX MidWorldTransform = DirectX::XMLoadFloat4x4(&midBone.worldTransform);
		DirectX::XMMATRIX PoleLocalTransform = DirectX::XMLoadFloat4x4(&poleLocalTransform);
		DirectX::XMMATRIX PoleWorldTransform = DirectX::XMMatrixMultiply(PoleLocalTransform, MidWorldTransform);
		DirectX::XMStoreFloat4x4(&poleWorldTransform, PoleWorldTransform);
	}
	// ポールターゲットをギズモで動かす
	const DirectX::XMFLOAT4X4& view = camera.GetView();
	const DirectX::XMFLOAT4X4& projection = camera.GetProjection();
	if (GetAsyncKeyState(VK_SPACE) & 0x8000)
	{
		DirectX::XMMATRIX PoleWorldTransform = DirectX::XMLoadFloat4x4(&poleWorldTransform);
		ImGuizmo::Manipulate(
			&view._11, &projection._11,
			ImGuizmo::TRANSLATE,
			ImGuizmo::LOCAL,
			&poleWorldTransform._11,
			nullptr);
		PoleWorldTransform = DirectX::XMLoadFloat4x4(&poleWorldTransform);

		Bone& midBone = bones[2];
		DirectX::XMMATRIX MidWorldTransform = DirectX::XMLoadFloat4x4(&midBone.worldTransform);
		DirectX::XMMATRIX InverseMidWorldTransform = DirectX::XMMatrixInverse(nullptr, MidWorldTransform);
		DirectX::XMMATRIX PoleLocalTransform = DirectX::XMMatrixMultiply(PoleWorldTransform, InverseMidWorldTransform);
		DirectX::XMStoreFloat4x4(&poleLocalTransform, PoleLocalTransform);
	}
	// ターゲットをギズモで動かす
	else
	{
		ImGuizmo::Manipulate(
			&view._11, &projection._11,
			ImGuizmo::TRANSLATE,
			ImGuizmo::WORLD,
			&targetWorldTransform._11,
			nullptr);
	}

	// ターゲット座標とポール座標を取得
	DirectX::XMMATRIX TargetWorldTransform = DirectX::XMLoadFloat4x4(&targetWorldTransform);
	DirectX::XMVECTOR TargetWorldPosition = TargetWorldTransform.r[3];
	DirectX::XMMATRIX PoleWorldTransform = DirectX::XMLoadFloat4x4(&poleWorldTransform);
	DirectX::XMVECTOR PoleWorldPosition = PoleWorldTransform.r[3];

	// 初期姿勢に戻す
	for (Bone& bone : bones)
	{
		bone.localRotation = { 0, 0, 0, 1 };
	}
	bones[0].UpdateWorldTransforms();

	// 3つの関節を使ってIK制御をする
	Bone& rootBone = bones[1];	// 根元ボーン
	Bone& midBone = bones[2];	// 中間ボーン
	Bone& tipBone = bones[3];	// 先端ボーン

	// 先端ボーン座標がターゲット座標に近づくように根本ボーンと中間ボーンを回転制御する
	{

	}

}

// 描画処理
void TwoBoneIKScene::Render(float elapsedTime)
{
	ID3D11DeviceContext* dc = Graphics::Instance().GetDeviceContext();
	RenderState* renderState = Graphics::Instance().GetRenderState();
	PrimitiveRenderer* primitiveRenderer = Graphics::Instance().GetPrimitiveRenderer();
	ShapeRenderer* shapeRenderer = Graphics::Instance().GetShapeRenderer();

	// ボーン描画
	for (size_t i = 1; i < _countof(bones) - 1; ++i)
	{
		const Bone& bone = bones[i];
		const Bone& child = bones[i + 1];

		DirectX::XMFLOAT4X4 world;
		DirectX::XMMATRIX World = DirectX::XMLoadFloat4x4(&bone.worldTransform);
		float length = child.localPosition.z;
		World.r[0] = DirectX::XMVectorScale(DirectX::XMVector3Normalize(World.r[0]), length);
		World.r[1] = DirectX::XMVectorScale(DirectX::XMVector3Normalize(World.r[1]), length);
		World.r[2] = DirectX::XMVectorScale(DirectX::XMVector3Normalize(World.r[2]), length);
		DirectX::XMStoreFloat4x4(&world, World);
		primitiveRenderer->DrawAxis(world, { 1, 1, 0, 1 });
		shapeRenderer->DrawBone(bone.worldTransform, length, { 1, 1, 0, 1 });
	}
	// ポールターゲット描画
	DirectX::XMFLOAT3 poleWorldPosition = { poleWorldTransform._41, poleWorldTransform._42, poleWorldTransform._43 };
	shapeRenderer->DrawSphere(poleWorldPosition, 0.1f, { 0, 1, 0, 1 });

	// レンダーステート設定
	dc->OMSetBlendState(renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);
	dc->OMSetDepthStencilState(renderState->GetDepthStencilState(DepthState::TestAndWrite), 0);
	dc->RSSetState(renderState->GetRasterizerState(RasterizerState::SolidCullNone));

	// グリッド描画
	primitiveRenderer->DrawGrid(20, 1);
	primitiveRenderer->Render(dc, camera.GetView(), camera.GetProjection(), D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	// シェイプ描画
	shapeRenderer->Render(dc, camera.GetView(), camera.GetProjection());
}

// GUI描画処理
void TwoBoneIKScene::DrawGUI()
{
	ImVec2 pos = ImGui::GetMainViewport()->GetWorkPos();

	ImGui::SetNextWindowPos(ImVec2(pos.x + 10, pos.y + 10), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_Once);

	if (ImGui::Begin(u8"２本のボーンIK制御"))
	{
		ImGui::Text(u8"Spaceキー押下中：ポールターゲット操作");
	}
	ImGui::End();
}
