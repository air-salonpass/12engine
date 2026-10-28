#pragma once

// ステージ配置用Entityであることを表すComponent。
// 描画や物理Resourceを直接持たず、Editor配置と将来のBatch／Chunk分割に必要な分類だけを保存する。
struct map_object_component
{
	// trueの物体はゲーム中に動かさない前提。将来のInstancingや静的Collider結合の対象になる。
	bool is_static = true;
	// 旧Scene/Prefabの保存互換用。現在の編集ではScene上部のSnap/Gridを全物体へ共通適用する。
	bool snap_to_grid = true;
	float grid_size = 1.0f;
	// 0=Default。背景、障害物、装飾など、ゲーム側の用途分類に利用する。
	int layer = 0;
};
