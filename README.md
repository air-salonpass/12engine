# 自作エンジン

ネイティブDirect3D 12でモデル描画とゲーム制作を行う自作エンジンです。実装の入口、機能別ファイル、ゲーム機能の追加手順は [実装案内](Rendering/Documentation/IMPLEMENTATION_MAP.md) にまとめています。

## DirectX 12移行状況

`Rendering/DirectX12`には、D3D11On12を使わないネイティブD3D12実装を置いています。

- Device、Direct/Compute/Copy Queue、QueueごとのFence
- `FrameResource`へ分離した2フレーム分のCommandAllocator、CommandList、Fence
- shader-visible CBV/SRV/UAV Descriptor Heapの共有管理
- flip-discard SwapChain、BackBuffer、Depth Buffer、リサイズ
- glTF/GLBのD3D12 Vertex/Index Buffer化
- LINEAR/STEPアニメーション補間とスキニング関節行列
- D3D12 Inspectorでのクリップ選択、再生／停止、ループ、速度、時間スクラブ
- glTF用Root Signature、PSO、ネイティブD3D12描画
- Scene Color/Depth/Normal G-bufferとSSRレイマーチ
- `imgui_impl_dx12`によるScene／Game二画面、Hierarchy、Inspector（Debugのみ）
- ImGuizmoのON／OFF、移動・回転・拡縮、Ctrlスナップ
- Scene上だけで有効になる右ドラッグ視点操作とホイールズーム
- New／Open／Save／Save Asと既存`.jscene`互換のシーン保存
- 同じGPUモデル資産を共有する複製配置
- WICによる外部画像／GLB埋込み画像のD3D12 Texture・SRV生成
- Base Color、Metallic/Roughness、Normal、Occlusion、Emissiveを使うPBR材質
- `diffuse_iem.dds`／`specular_pmrem.dds`／`lut_ggx.dds`を使うネイティブD3D12 IBL
- IBL強度のInspector調整と`.jscene`保存（背景Cubeは描画せず材質の環境光だけに使用）
- 複数種類のモデルを共有管理する非同期Resource Manager
- フォルダ／モデルアイコン付きProjectブラウザとダブルクリック配置
- HDR Scene表示とSSR適用後Game表示の個別トーンマッピング・露出調整
- 波紋・円・グリッドを含まない単色のD3D12反射床
- 床の表示、色、高さと選択モデルの自動接地
- glTFのOPAQUE／MASK／BLEND、Alpha Cutoff、両面材質
- F11／Alt+Enterのボーダーレス全画面
- 非同期モデル読込と、GPU完了後のフレーム境界での安全な差し替え
- `std::atomic`制御の常駐ジョブワーカー
- Scene Registryへ分離したTransform、Model Renderer、Animation、Camera、Light、Reflection、Post Process、Rendering Settings
- Entityごとに独立したAnimation Pose（同一モデルAssetを複製しても別々に再生可能）
- Animation Controller Componentと、モデルのクリップから自動生成するState切替
- `.jprefab`へのPrefab保存／配置とProjectブラウザからの読込み
- 型を追加するだけでStorageへ自動登録されるComponent Registry
- Scene全体のUndo／Redo（最大64履歴、Ctrl+Z／Ctrl+Y）
- CPUで生成したMeshletとD3D12 Mesh Shader描画（非対応GPU・特殊材質は通常PSOへ自動フォールバック）
- Scene／Gameパネルの実表示解像度に合わせたRender Target（余分なPixel描画と縦横比ずれを削減）
- FPS／平均フレーム時間をタイトルバーとEditorツールバーへ表示
- Waitable SwapChain、高精度FPS上限、非アクティブ時のBackground FPS
- Play／Pause／Step、Ctrl+D、Ctrl+S、Delete、Fフォーカス
- Map Object ComponentによるStatic／Layer分類、Scene共通のスナップとCtrl一時吸着
- 固定60 Hz Physics、Box／Sphere／Capsule Collider、Trigger、Layer／Mask
- Static／Kinematic／Dynamic RigidBody、重力、反発、摩擦、軸固定
- Character Controller用Componentと接地判定
- Collision／TriggerのEnter・Stay・Exitイベント、Gameplay Tag検索
- 汎用Map Tool（Sceneクリック配置、Grid／Collider吸着、Yaw／Scale、自動Static Collider）
- Sweep-and-Prune Broad Phase、Raycast、Overlap Sphere、接触／Trigger結果
- D3D11 Renderer、D3D11 ImGui Backend、FBX SDK、DirectXTKの実装ファイルとビルド／リンク依存を撤去

作業ディレクトリを`Rendering`にして次のように起動します。`--dx12`は不要です。

```powershell
..\x64\Debug\3dgp.exe
```

標準起動はネイティブD3D12です。D3D11On12やD3D11への実行時フォールバックはありません。ReleaseではEditor用ImGui／ImGuizmoをコンパイルせず、ゲーム表示だけを実行します。

## 現在の機能

- glTF / GLBモデルの読込みとPBR表示
- アニメーションクリップの選択、再生、ループ、速度、時間スクラブ
- モデルのノード・メッシュ・マテリアル情報表示
- 原点グリッド、モデル境界ボックス、オービットカメラ
- ImGuizmoによるシーン上でのTransform編集
  - `W`: 移動
  - `E`: 回転
  - `R`: 拡縮
  - Local / World座標系の切替
  - Inspectorの位置・回転・スケールと双方向同期
- `F`: モデルへカメラをフォーカス
- `F11` または `Alt+Enter`: フルスクリーン切替
  - ボーダーレス全画面へ切り替え、モニター解像度に合わせて描画バッファを自動再生成
  - 全画面中の`Esc`でウィンドウ表示へ復帰
- 右ドラッグ: オービット、中ドラッグ: パン、ホイール: ズーム
- Unity系のダークテーマ、移動・リサイズ可能なエディタパネル
- Scene／Gameの上下2画面表示（Gizmoとカメラ操作はScene側だけに表示）
- グリッドと境界ワイヤーは初期状態OFF。必要な場合だけModel Viewerから表示
- HDR、トーンマッピング、IBL、SSR

## 描画と反射の構成

モデルはglTF／GLBで読み込み、PBR材質と手続き環境光で描画します。任意の環境画像によるIBLも追加できます。
床の反射は平面反射を標準とし、InspectorでSSRへ切り替えられます。

- 平面反射：同じモデルとアニメーション姿勢を床に対して反転して描き、床へ合成します。モデルを追加で描くため描画負荷が増えます。
- SSR：描画済みの色・深度・法線から反射方向を探索します。画面外の物体は反射できず、画面端でフェードします。
- 床の色・高さ、反射強度、SSRの探索距離・厚み・ステップ数はInspectorから調整します。
- `Rendering/ShaderLibrary/` は保管用のHLSLです。ファイルが存在していても現在の実行経路で使われているとは限りません。実行経路を調べるときは `GltfRenderer.cpp`、`ScreenSpaceReflection.cpp` のシェーダー生成箇所から読みます。

初期モデルは歩行アニメーション付きCesiumManです。床への自動接地はモデル境界の最下点を使います。
モデル描画までの詳細な流れと確認箇所は [実装案内](Rendering/Documentation/IMPLEMENTATION_MAP.md) を参照してください。

## 動作安定化

- VSyncを標準で有効化し、無制限描画によるGPU負荷の急増を抑制
- 最小化中は描画を休止
- 長いフレーム時間を制限し、復帰時のアニメーション飛びを抑制
- glTF / GLBの読込みをワーカースレッド化し、読込み中も画面を更新
- 起動モデルをワーカーで読み込み、画像不要の手続き環境光ですぐに照明を利用
- 1024px RGBA16F IBLから実用Mipだけを直接読み、任意IBLを有効にした際のI/O／GPU転送を約145MBから約6MBへ削減
- D3D12 Debug LayerはVisual Studioのデバッグ実行中だけ有効化
- メインスレッドと読込みスレッドでCOMを明示的に初期化

## ビルド

Scene構成は`Rendering/Scene`、Componentは`Rendering/Components`、Systemは`Rendering/Systems`へ機能単位で分割しています。

DirectX 12描画基盤とチーム制作時の同期・リソース管理ルールは`Rendering/DirectX12/TEAM_DEVELOPMENT_GUIDE.md`にまとめています。CPUスレッドの制御フラグには`std::atomic`を使用し、GPU同期にはQueueごとのFenceを使用します。
新しいComponent・Systemの追加方法と登録順は[`Rendering/COMPONENT_SYSTEM_GUIDE.md`](Rendering/COMPONENT_SYSTEM_GUIDE.md)を参照してください。
DirectX 12とECSの経験がないメンバーは、実装例付きの[`Rendering/DirectX12/BEGINNER_IMPLEMENTATION_GUIDE.md`](Rendering/DirectX12/BEGINNER_IMPLEMENTATION_GUIDE.md)から読んでください。
今後必要な機能とPrefab／マップツールの実装順は[`Rendering/DirectX12/TEAM_PRODUCTION_GAP_ANALYSIS.md`](Rendering/DirectX12/TEAM_PRODUCTION_GAP_ANALYSIS.md)にまとめています。
Collider、RigidBody、Character Controllerの使い方は[`Rendering/Documentation/PHYSICS_GUIDE.md`](Rendering/Documentation/PHYSICS_GUIDE.md)を参照してください。

プランナー向けのマップ配置手順は[`Rendering/Documentation/MAP_EDITOR_GUIDE.md`](Rendering/Documentation/MAP_EDITOR_GUIDE.md)を参照してください。

旧D3D11コードやリンク設定の再混入は、リポジトリ直下で次の検査を実行すると確認できます。

```powershell
.\Tools\VerifyNativeD3D12.ps1
```

Visual Studio 2022で `JisakuEngine.sln` を開き、`Debug | x64` または `Release | x64` を選択してビルドします。

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' .\JisakuEngine.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m
```

実行時の作業ディレクトリは `Rendering` にします。モデル入力はglTF／GLBへ統一しており、Autodesk FBX SDKはリンクしません。

## Unity風エディタへ向けた次の候補

1. コリジョン・当たり判定の可視化
2. Animation Controllerの遷移条件・Blend Tree
3. 親子Transformと複数選択
4. DXR／Variable Rate Shading（Feature Support確認とフォールバック必須）

以後追加するエンジン側コードには、処理の目的と設計意図が分かる日本語コメントを付けます。


## チーム制作と3D見本

[機能比較と実装順](Rendering/Documentation/TEAM_FEATURE_PLAN.md)に、プランナー向け編集・提出素材・火/水/地形・ゲーム用2D UIの計画をまとめています。

Debug版のProjectにあるSamplesボタンから、3Dマップ3種とPrefab6種を開けます。[見本の使い方](Rendering/resources/samples/README.md)を確認してください。


## ライティングと材質の調整

太陽の色・強さ、外部画像なしの環境光、落ち影、SSAO、HDR色調を追加しています。ProjectのSamplesから04/05の照明見本を開けます。[調整・保存・実装手順](Rendering/Documentation/LIGHTING_GUIDE.md)を参照してください。

## 歩いて確認できる3Dステージ

Project → Samples → Maps の 06_Ruins_Courtyard.jscene に、中庭・橋・階段・ゴールを備えた見本を追加しました。Walk TestでWASD移動・Spaceジャンプ・R復帰、Escのポーズ画面から編集へ戻れます。[操作と実装手順](Rendering/Documentation/STAGE_SAMPLE_GUIDE.md)を参照してください。

## 配置したPlayerをPlayで操作

06の見本は一人称Playerを配置済みです。Playで操作開始、マウスで視点、WASDで移動、Spaceでジャンプ、Escのポーズ画面から「編集に戻る」を選んで編集状態へ戻ります。[Playerの配置・設定・実装](Rendering/Documentation/FIRST_PERSON_PLAYER_GUIDE.md)を参照してください。
