# ゲーム制作に向けた残課題と実装順

この文書は現在の機能と、ゲーム制作を進めるための残課題を整理しています。将来の候補を含み、全項目が実装済みという意味ではありません。

## 現在このエンジンにあるもの

- ネイティブDirectX 12 Device、Graphics／Compute／Copy Queue、Fence
- フレーム別CommandAllocator／CommandList、Waitable SwapChain
- VSync、任意FPS上限、バックグラウンドFPS
- glTF／GLB、PBR、IBL、Animation、Mesh Shader、SSR／平面反射
- Entityと型別Component Storage、Scene保存、Undo／Redo
- Gameplay Tag検索、Collision／TriggerのEnter・Stay・Exitイベント
- 単体Entity Prefabの保存／配置
- Scene／Game View、Hierarchy、Project、Inspector、Gizmo

## 優先度が高い不足機能

| 優先度 | 不足機能 | チーム制作で必要な理由 |
| --- | --- | --- |
| 最優先 | Componentの共通Serializer／Factory | Component追加のたびにSceneとPrefabのJSON処理を手書きすると、保存漏れと競合が増える |
| 最優先 | 親子Transform／Hierarchy | キャラクター装備、ステージ部品、複合Prefabを一体として移動できない |
| 最優先 | Prefab Apply／Revert／Override | 元Prefab修正を配置済みInstanceへ反映できず、同じ修正を手作業で繰り返すことになる |
| 最優先 | Physics高度化 | 基本Collider／RigidBody／Character Controllerは実装済み。回転形状、CCD、Joint、Shape Cast、デバッグ表示が残る |
| 高 | 入力Action Map | キーボード／ゲームパッド差をゲームコードから分離する必要がある |
| 高 | Script登録とライフサイクル | キャラクター固有ロジックをエンジン本体へ書かずに追加するために必要 |
| 高 | マップ配置支援 | グリッドスナップ、複数選択、整列、Static指定、範囲複製が必要 |
| 高 | Asset ID／Metaファイル | ファイル移動でScene参照が壊れる問題を防ぐ |
| 高 | GPU Profiler／CPU Profiler | 「重い」をDraw、Present、Upload、System単位で切り分ける必要がある |
| 中 | Dynamic Resolution／View非表示時の描画停止 | GPU負荷が高いSceneで目標FPSを維持する |
| 中 | Audio、UI Canvas、Text、Button | 実際のゲーム画面と操作を完成させるために必要 |
| 中 | Culling、LOD、Instancing | 広いマップや同一障害物を大量配置するときに必要 |
| 中 | Scene差分に強い保存形式 | 1つの巨大JSONでGit競合することを避ける |

## Prefabの実装段階

現在の`.jprefab`は、モデルを持つ単体Entityを保存・配置できます。次は次の順番で拡張します。

1. `parent_entity_component`を追加し、親子Transformを解決するSystemを作る。
2. PrefabへRoot以下の複数Entityと親子関係を保存する。
3. `prefab_instance_component`へ元Prefab ID、Instance ID、Override一覧を保存する。
4. InspectorにApply、Revert、Unpackを追加する。
5. 元Prefab更新時に、Overrideされていない値だけをInstanceへ反映する。

## マップツールの実装段階

現在のScene保存、Projectブラウザ、Gizmo、複製を基礎として利用します。

1. Map Object Component：Static／Dynamic、Layer、配置用Grid Sizeを保持する。**実装済み**
2. Ctrl以外でも常時使えるグリッドスナップ、床平面／Collider表面へのRay配置、回転・Scale・自動Collider設定。**実装済み**
3. 複数選択、整列、等間隔配置、範囲複製を追加する。
4. 同一Mesh＋MaterialをInstance Batchへまとめる。
5. Mapをチャンク分割し、カメラから遠いチャンクをロード・描画しない。
6. Collider生成とNavMesh／スポーン地点などのゲーム用マーカーを追加する。

Map Objectの状態はComponentへ保存し、配置処理はEditor System、描画最適化はD3D12 Rendererへ分離します。マップ編集の都合を`gltf_renderer12`へ直接書き込まないでください。

## 軽量化の次段階

- Scene／Game Viewがタブの裏または0サイズなら、そのView用Render Passを記録しない。
- Game ViewへRender Scaleを追加し、負荷に応じて解像度を変える。
- Timestamp QueryでGPU Pass時間を測り、CPUフレーム時間と分けて表示する。
- Frustum Culling、LOD、同一モデルのInstancingを追加する。
- Upload専用ContextとCopy Queueを共通化し、Assetごとの一時Command List生成と同期待ちを減らす。
- GPU Resource／DescriptorをFence値付き遅延解放キューへ入れる。

見た目を変えずに安全に導入できる順番は、Profiler、非表示View停止、Culling、Instancing、Dynamic Resolutionです。


## 制作全体の機能計画

プランナー向け操作、提出用素材への差し替え、ゲーム用2D UI、火・水・地形の導入可否を含む比較は[チーム制作の機能計画](../Documentation/TEAM_FEATURE_PLAN.md)にまとめています。
