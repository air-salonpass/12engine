# 自作エンジン

完成済みの描画エンジンとシェーダーを土台に、ネイティブDirect3D 12で動作する自作エンジンです。元教材のフォルダーは変更せず、このプロジェクトへ組み込んだコードだけを編集します。

## DirectX 12移行状況

`Rendering/DirectX12`には、D3D11On12を使わないネイティブD3D12実装を置いています。

- Device、Direct/Compute/Copy Queue、QueueごとのFence
- 2フレーム分のCommandAllocator、CommandList、定数アップロード領域
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
- 教材の`diffuse_iem.dds`／`specular_pmrem.dds`／`lut_ggx.dds`を使うネイティブD3D12 IBL
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
- Play／Pause／Step、Ctrl+D、Ctrl+S、Delete、Fフォーカス
- D3D11 Renderer、D3D11 ImGui Backend、FBX SDK、DirectXTKへのビルド／リンク依存を撤去

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

## シェーダープログラミングII・III統合

元教材フォルダーは変更せず、HLSLを次の場所へ分類コピーしています。

- `Rendering/ShaderLibrary/ShaderProgrammingII/`：70ファイル
- `Rendering/ShaderLibrary/ShaderProgrammingIII/`：111ファイル

現在のModel Viewerは、表示内容を「アニメーションモデル＋SSR」だけへ絞っています。波紋・円・色付き基本形状・岩・他のポストエフェクト操作は非表示です。

- Shader II（参照用Shader Library）
  - Unit01を基にしたフロー歪み
  - Unit02-03を基にした深度連動の被写界深度
  - Bloom、PBR、IBLの教材コードを分類保存
- Shader III（参照用Shader Library。Native Previewで有効なのはUnit08 SSR）
  - Unit03 ラジアルブラー
  - Unit05 周辺減光
  - Unit06 色収差
  - Unit08 スクリーンスペースリフレクション（SSR）
  - Unit10 FXAA

### SSRデモ

起動すると、Shader IIIの実行例に近い次の状態で表示します。

- 歩行アニメーション付きCesiumMan
- 深度を書き込む広い反射床
- バウンディングボックス最下点による床への自動接地
- 深度からの視点空間座標・面法線復元
- 最大40ステップのスクリーン空間レイマーチ
- 反射強度、最大距離、厚み、ステップ幅のリアルタイム調整

SSRは画面内に存在する色と深度だけを利用するため、画面外の物体は映らず、画面端では反射がフェードします。この制約も教材のSSRと同じように確認できます。

教材ごとのHLSLは`ShaderLibrary`へ参照用に残し、現在のネイティブD3D12実行経路は要望どおりアニメーションとSSRを中心にしています。波紋、円、色付き図形、DOFなどは実行画面へ出しません。

## 動作安定化

- VSyncを標準で有効化し、無制限描画によるGPU負荷の急増を抑制
- 最小化中は描画を休止
- 長いフレーム時間を制限し、復帰時のアニメーション飛びを抑制
- glTF / GLBの読込みをワーカースレッド化し、読込み中も画面を更新
- 起動モデルをワーカーで読み込み、その並行時間でIBL DDSを準備して照明の時間差を防止
- 1024px RGBA16F IBLから実用Mipだけを直接読み、起動時I/O／GPU転送を約145MBから約6MBへ削減
- D3D12 Debug LayerはVisual Studioのデバッグ実行中だけ有効化
- メインスレッドと読込みスレッドでCOMを明示的に初期化

## ビルド

Scene構成は`Rendering/Scene`、Componentは`Rendering/Components`、Systemは`Rendering/Systems`へ機能単位で分割しています。

DirectX 12描画基盤とチーム制作時の同期・リソース管理ルールは`Rendering/DirectX12/TEAM_DEVELOPMENT_GUIDE.md`にまとめています。CPUスレッドの制御フラグには`std::atomic`を使用し、GPU同期にはQueueごとのFenceを使用します。
新しいComponent・Systemの追加方法と登録順は[`Rendering/COMPONENT_SYSTEM_GUIDE.md`](Rendering/COMPONENT_SYSTEM_GUIDE.md)を参照してください。

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
