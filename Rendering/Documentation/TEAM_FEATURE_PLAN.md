# チーム制作の機能計画

## 目標と調査範囲

プランナーがC++やDirectX 12を知らなくても、3Dマップを作り、配置・調整・保存・テストプレイできることを目標にします。ゲーム内UIは3D画面へ重ねる2D描画として別に用意します。
モデル・UIなどの提出素材はアート担当の制作物へ差し替えます。既存の確認用モデルや環境画像を、そのまま提出素材として扱いません。炎・水・模様・発光などは計算で作る表現を優先します。

比較はローカルのソースを読んだ結果です。参照側の全機能を起動して確認した評価ではありません。「実装あり」は関連する処理があることを示し、完成度やバグのなさを保証しません。

## 先に固める機能

| 優先度 | 機能 | 現在の自作エンジン | 比較対象で確認した実装 | 次に必要なこと |
| --- | --- | --- | --- | --- |
| 最優先 | 入力中も止まらないEditor | メッセージ1件ごとに描画を飛ばす問題を修正。体感確認は別途必要 | 今回は性能比較していない | マウス静止・移動・ドラッグ時のCPU/GPU時間を比較する |
| 最優先 | 3Dマップ配置 | クリック配置、Grid、Collider面への吸着、回転、Scale、自動Collider | Scene View、Picking、Gizmo、Projectの実装あり | 配置予告、連続配置、消去、選択と配置モードの明確化 |
| 最優先 | 操作の安全性 | Undo/Redo、保存、Play/Pause/Stepあり | Undo、Scene操作の実装あり | 未保存表示、開く前の保存確認、自動保存、復元、Play終了時の編集状態復元 |
| 最優先 | 複数選択・整理 | 単体選択 | HierarchyとPrefab階層あり。複数選択の完成度は未評価 | 矩形選択、一括移動・複製・削除、整列、表示/編集ロック |
| 最優先 | 親子Transform・複合Prefab | 単体Entity Prefab | 階層保存・実体化、Apply/Revert実装あり | 親子関係、複数Entity保存、適用範囲表示、Overrideの仕様を決める |
| 最優先 | 保存とInspectorの項目共通化 | Scene/Prefabへ手書き登録 | ComponentFieldRegistryで保存・Inspector等の項目を共通化 | Component追加時の保存漏れを減らす仕組みを作る |
| 最優先 | アート素材への差し替え | モデル読込みは可能。参照はパス中心 | glTF Asset Manager、UI画像/フォント入力あり | 安定したAsset ID、再読込み、参照検索、欠損表示、差し替え時の設定維持 |
| 最優先 | 提出用パッケージ | エンジン単体のRelease構成あり | 提出素材検査の完成度は未確認 | 起動Scene指定、依存Assetの収集、テスト素材除外、別PCで起動確認 |
| 高 | ゲーム内2D UI | ImGuiはDebug Editor用。ゲーム用Canvasは未実装 | Canvas、RectTransform、Text、Button、UiPassあり | 画像、文字、ゲージ、アンカー、解像度対応、入力、画面遷移。絵はアート制作物を指定 |
| 高 | 入力・ゲーム動作登録 | Editor操作と基本物理あり | Input、ScriptRegistry、ScriptBehaviour、各ゲームScriptあり | Action Map、ゲームパッド、開始/更新/終了、Inspectorからの動作設定 |
| 高 | ゲームイベント配置 | Tag、Trigger Enter/Stay/Exitあり | ゲームScriptによる反応処理あり | スポーン、Goal、ダメージ、Scene遷移等を選択式で設定。最初から巨大なノードEditorは作らない |
| 高 | 物理の実用性 | Box/Sphere、Capsule近似、基本RigidBody、接地 | PhysicsWorld、物理デバッグ描画あり | 正確な形状、斜面・段差、回転Box、すり抜け対策。Characterの設定項目と実処理の対応を確認 |
| 高 | アニメーション | クリップ、独立姿勢、簡易State選択 | Animation関連処理あり。遷移機能の完成度は未評価 | 条件遷移、ブレンド、イベント、ルート移動の扱い |
| 高 | 音 | ゲーム用の統合は未実装 | AudioSystem、Source等あり | BGM/SE、音量、ループ、停止、Scene切替時の寿命 |
| 高 | 測定とエラー表示 | FPS表示、基本エラー表示 | GpuProfiler、Consoleあり | CPU/GPU Pass時間、ロード時間、エラー対象の選択、負荷上限の見える化 |
| 中 | 大規模マップの負荷管理 | 同一Asset共有。描画最適化は残課題 | HZB Culling、地形Chunk等あり | Frustum Culling、Instancing、LOD、非表示View停止、必要ならChunk読込み |
| 中 | 照明・影・材質調整 | 太陽の色/強度、手続き環境光、2048px影Map、SSAO、PBR、IBL、反射 | Lighting、Shadow、複数光源関連実装あり | Point/Spot Light、広範囲用CSM、材質プリセット、GPU Pass計測 |

## プランナー向けマップツールの完成条件

1. 新規Sceneと練習用マップを迷わず開ける。保存前に既存作業を失う操作を知らせる。
2. 床・壁・足場・装飾・ゲーム用マーカーを一覧から選び、配置先を予告表示する。
3. Grid幅、90度回転、表面吸着、連続配置、消去を操作できる。配置用Assetと選択中Entityを混同しない。
4. グループや複合Prefabを配置・複製でき、一括変更とUndoが成立する。
5. プレイヤー出現点、Goal、チェックポイント、Triggerなどを見分けられる。
6. Play前に欠損Asset、床抜け、スポーン埋まり、未設定の遷移先を検査する。
7. Playして戻っても制作中マップを壊さず、保存・再読込み・別PCで同じSceneを確認できる。

地形ブラシだけでマップツールが完成するわけではありません。屋内や足場中心なら、まずモジュール配置を完成させた方が制作を進められます。屋外の起伏が必要なら地形編集を加えます。

## 火・水・地形を導入する見通し

| 対象 | 確認した仕組み | 最初に作る版 | 後から増やすもの・負荷 |
| --- | --- | --- | --- |
| 火 | 3D格子のCompute更新、燃料/温度/速度/圧力、レイマーチ、手続きNoise、周囲の光 | 外部画像を使わないNoiseと色の変化による炎。位置・大きさ・色・強さをComponent化 | 体積炎、煙、熱歪み、光。現在の参照側設定は64³格子、最大4体積、圧力反復24回なので、軽い板状炎とは負荷が違う |
| 水面 | 頂点番号から水面を生成、波で変形、色/深度を読んで屈折、泡、反射。FFT経路もあり | 分割平面と波、深さによる色、Fresnel、手続き泡。色と深度の入力を既存描画へつなぐ | FFT、水中演出、濡れ、岸の泡、反射品質。画面色/深度のコピーと描画順の管理が必要 |
| 滝 | 頂点番号で面を生成し、描画直前の色/深度を読んで合成 | 流れるNoise、色、深度による境界ぼかし | 水しぶき、音、川や水面との接続 |
| 流れる・溜まる水 | 格子流体、粒子、PBF、表面描画など複数の仕組み | ゲームで本当に必要か先に決める。見た目だけなら水面を使う | 容器・流出・障害物・CPU側判定との同期。単独HLSLのコピーでは動かないため後回し |
| 地形 | CPU Heightmap、Chunk Mesh、Raycast、Collider、保存、ブラシ、層の塗分け | 盛る/削る/平らにする、Undo、保存、地面への配置、衝突までを一式で作る | Smooth、地表塗り、草、穴、Chunk最適化。地形の高さと衝突を一致させる必要がある |

すべて導入可能ですが、参照側のComponent継承・Bindless Texture配列・Buffer管理・PSO・描画Passをそのまま持ち込むと、こちらのRegistryやDescriptor管理と衝突します。必要なHLSLと計算を導入先へコピーし、GPU資源の所有、register番号、Root Signature、定数の並び、Barrier、Fence、描画形式をこちらへ合わせます。参照フォルダー自体は変更しません。

描画だけでなく、Component→Inspector→保存→Prefab→見本Sceneまでそろえて初めてプランナーが使える機能になります。

## 提出用素材とシェーダー表現

- 炎の揺らぎ、波、泡、空の色、発光、ディゾルブ、色変化、幾何学的UI背景・ゲージ等は手続き表現の候補です。
- キャラクター、建物固有の形、ロゴ、イラスト、UIアイコンはアート担当の素材を指定できるようにします。シェーダーは色調整・アニメーション・合成を担当できます。
- IBL用の環境画像、フォント、音、モデル内の埋込みTextureも差し替え対象の確認が必要です。モデルとUI画像だけを置換しても外部Assetが残る場合があります。
- 既存のCesiumMan等とDDS環境画像は確認用です。現在はIBL強度0ならDDSを読み込まず、手続き環境光だけで描画します。ただし確認用初期モデルと提出用Asset収集は別途対応が必要です。
- 今回の箱モデル見本はプログラム生成の操作練習用です。提出時の採用可否は制作ルールに合わせ、完成モデルの代わりとして無条件に扱いません。

## 実装順

1. マウス操作時の描画停止を修正し、3Dサンプルで操作と保存を確認する。
2. 未保存保護、Play復元、配置予告、複数選択、親子・複合Prefabを整える。
3. Component保存の共通化、Asset差し替え、スポーン/Trigger等の設定式ゲーム機能を整える。
4. ゲーム用2D UI、音、Scene遷移、提出用Asset収集を作る。
5. 必要なステージに合わせて手続き炎、水面、地形を追加し、Profilerで予算を確認する。
6. 高度な流体、体積炎、草、大規模地形は、ゲームで必要かと提出PCの性能を確認してから拡張する。

## 比較時に確認した実装の場所

以下は比較内容を再確認するための索引です。ルートはローカルの `hatake3187/Engin` です。

- `Source/System/Scene/ComponentFieldRegistry.h`：保存とInspector等の項目共通化。
- `Source/System/Scene/Serialization/PrefabSerializer.h`、`Source/System/Base/Application.cpp` の `ApplyPrefab` / `RevertPrefab`：階層保存とPrefab操作。
- `Source/Editor/Windows/TerrainWindow.cpp`、`Source/System/Terrain/TerrainSystem.h`：地形編集、保存、Raycast、描画/物理への接続。
- `Source/System/Graphic/Passes/Fire/FirePass.h`、`Shader/Fire/`：体積炎と手続きNoise。
- `Source/System/Graphic/Passes/Water/`、`Shader/Water/`：水面、滝、流体。
- `Shader/Common/TerrainLayers.hlsli`：地表の層ごとの色/法線/粗さの合成。現方式はTexture入力があるため、外部画像を使わない版へはそのまま流用しない。
- `Source/System/UI/`、`Source/System/Graphic/Passes/UI/`、`Source/System/Scene/Components/`：ゲームUI。
- `Source/System/Script/`、`Source/System/Audio/`、`Source/System/Physics/`、`Source/System/Graphic/Profile/`：動作登録、音、物理、GPU計測。