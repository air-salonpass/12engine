#pragma once

#include <memory>
#include "Scene.h"
#include "Camera.h"
#include "Light.h"
#include "Model.h"
#include "Sprite.h"

// コマンド判定処理シーン
class ConfirmCommandScene : public Scene
{
public:
	ConfirmCommandScene();
	~ConfirmCommandScene() override = default;

	// 更新処理
	void Update(float elapsedTime) override;

	// 描画処理
	void Render(float elapsedTime) override;

	// GUI描画処理
	void DrawGUI() override;
	
private:
	// ⑦⑧⑨
	// ④⑤⑥
	// ①②③
	using InputKey = unsigned int;
	using Command = std::vector<InputKey>;

	// 入力キー(押している状態)
	static const InputKey KEY_1 = (1 << 0);		// 左下
	static const InputKey KEY_2 = (1 << 1);		// 下
	static const InputKey KEY_3 = (1 << 2);		// 右下
	static const InputKey KEY_4 = (1 << 3);		// 左
	static const InputKey KEY_5 = (1 << 4);		// 中
	static const InputKey KEY_6 = (1 << 5);		// 右
	static const InputKey KEY_7 = (1 << 6);		// 左上
	static const InputKey KEY_8 = (1 << 7);		// 上
	static const InputKey KEY_9 = (1 << 8);		// 右上
	static const InputKey KEY_P = (1 << 9);		// パンチ
	static const InputKey KEY_K = (1 << 10);	// キック
	// 入力キー(押していない状態)
	static const InputKey NOT_1 = (1 << 11);	// 左下
	static const InputKey NOT_2 = (1 << 12);	// 下
	static const InputKey NOT_3 = (1 << 13);	// 右下
	static const InputKey NOT_4 = (1 << 14);	// 左
	static const InputKey NOT_5 = (1 << 15);	// 中
	static const InputKey NOT_6 = (1 << 16);	// 右
	static const InputKey NOT_7 = (1 << 17);	// 左上
	static const InputKey NOT_8 = (1 << 18);	// 上
	static const InputKey NOT_9 = (1 << 19);	// 右上
	static const InputKey NOT_P = (1 << 20);	// パンチ
	static const InputKey NOT_K = (1 << 21);	// キック

	// 入力キー設定
	void SetInputKey(InputKey key);

	// コマンド確認
	bool ConfirmCommand(const Command& command, int frame);

	// 入力キー描画
	void DrawInputKeys(ID3D11DeviceContext* dc, float x, float y, const InputKey inputKeys[]);

private:
	Camera								camera;
	LightManager						lightManager;
	std::unique_ptr<Sprite>				sprite;
	std::shared_ptr<Model>				character;
	std::vector<Model::NodePose>		nodePoses;
	int									animationIndex = -1;
	float								animationSeconds = 0;

	static const int MAX_INPUT_KEY = 60;
	InputKey							inputKeys[MAX_INPUT_KEY];
};
