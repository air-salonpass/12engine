# 実装案内：機能を探す・モデルを描く・ゲームを作る

## 最初に読む場所

このエンジンは、状態をComponentへ保存し、Systemで状態を更新し、Rendererで描画します。
Entityは「シーン内の一つの物体を識別する番号」です。Componentは位置・モデル・速度などのデータ、Systemはそのデータを使う処理です。
たとえばプレイヤーは、Transform、Model Renderer、Collider、Character Controllerなどを同じEntityへ付けて表します。

パスはリポジトリ直下からの相対パスです。ファイル名を検索し、対応する `.h` で使い方を、`.cpp` で実処理を読んでください。

| やりたいこと | 読む場所 | ここで分かること |
| --- | --- | --- |
| 起動から終了まで追う | `Rendering/DirectX12/NativePreview.cpp` の `run`、`initialize`、`update`、`render` | 初期化、毎フレーム更新、描画、終了の呼出し順 |
| 位置やゲーム状態を追加する | `Rendering/Components/`、`Rendering/Scene/SceneRegistry.h` | データ定義、Entityへの追加、検索、複製 |
| ゲームの処理を追加する | `Rendering/Systems/`、`NativePreview.cpp` の `update` | 機能別処理と呼出し位置。Systemを作るだけでは動かない |
| モデルを読み込む・配置する | `NativePreview.cpp` の `request_model_load`、`poll_model_load`、`create_model_entity` | 読込み予約からEntity配置まで |
| 読込み失敗や重複読込みを調べる | `Rendering/DirectX12/ModelResourceManager.cpp` | 非同期ジョブ、完了結果、共有Asset、エラー |
| モデルのファイル内容を調べる | `Rendering/DirectX12/GltfModel.h`、`.cpp` | 頂点・材質・Texture・関節・クリップの読込みとGPU資源作成 |
| モデルの見え方を変える | `Rendering/DirectX12/GltfRenderer.h`、`.cpp` | 行列、材質定数、描画設定とシェーダーの受渡し |
| アニメーションを変更する | `Rendering/DirectX12/AnimationSystem12.h`、`.cpp` | Entityごとの時刻・姿勢を更新する処理 |
| 画面やGPU同期を調べる | `Rendering/DirectX12/Renderer.h`、`.cpp`、`FrameResource.h`、`Device.h` | フレーム開始・終了、画面サイズ変更、GPU完了待ち |
| 床の反射を調べる | `NativePreview.cpp` の `render`、`SceneGeometryRenderer.cpp`、`ScreenSpaceReflection.cpp` | 平面反射の別描画、床への合成、SSR |
| 当たり判定・移動を追加する | `Rendering/Systems/PhysicsSystem.h`、`.cpp` | 固定時間更新、衝突・Triggerイベント、Raycast |
| マップの配置方法を変える | `Rendering/Systems/MapEditorSystem.h`、`.cpp`、`NativePreview.cpp` の配置関数 | マウスからRayを作る計算と、Entityを配置する処理 |
| 編集UI・保存を追加する | `NativePreview.cpp` の `render`、`save_scene`、`load_scene`、`save_prefab`、`load_prefab` | Inspector、JSON保存・読込み。SceneとPrefabは別々に対応が必要 |

## モデルが画面へ出るまで

1. ProjectでglTF／GLBを選ぶか、コードから `request_model_load` へパスを渡します。相対パスは実行時の作業ディレクトリ `Rendering` を基準にします。
2. `model_resource_manager12::request` が読込みを予約します。同じAssetは共有します。ワーカー内の `gltf_model12::load` がファイル解析と資源作成を進めるため、呼出し直後に描画できるとは限りません。
3. 毎フレームの `poll_model_load` が完了を回収します。`find` が返すモデルを使い、配置要求があれば `create_model_entity` でEntityを生成します。待機ループを追加するとEditorが止まるため、完了待ちは次のフレームへ持ち越します。
4. EntityのTransformに位置・回転・スケール、Model RendererにAssetパス・表示状態を持たせます。アニメーションの姿勢はEntityごとに更新するため、同じモデルを複製しても独立して再生できます。
5. `native_preview12::render` がTransformとModel RendererのあるEntityを列挙します。非表示または未ロードなら描画せず、ロード済みなら `gltf_renderer12::render` へモデル、ワールド・ビュー・射影行列、照明、姿勢を渡します。
6. `GltfRenderer.cpp` が頂点・材質・関節の情報をGPUへ渡して描画命令を記録します。平面反射では同じ姿勢を床面対称にも描きます。ゲーム時間や移動をここで進めると、通常表示と反射で状態が食い違うため、ゲーム更新は `update` 側で行います。
7. 描画結果のTextureを反射処理や表示処理が読み、DebugではScene／Gameと編集UI、Releaseではゲーム画面へ出力します。

表示されないときは、作業ディレクトリとAssetパス、読込みエラー、Model Rendererの `visible`、Transformの位置と大きさ、カメラの順に確認します。モデルがロードできることと、カメラに映ることは別です。

## ゲーム機能を一つ追加する手順

例として自動回転する障害物を作る場合、回転速度はComponent、回転処理はSystemに置きます。

1. `Components/RotatorComponent.h` に `float degrees_per_second = 90.0f;` を持つ `rotator_component` を定義します。Componentには保存したい状態を置きます。
2. 配置処理またはInspectorから、対象Entityへ `scene.add<rotator_component>(entity)` で追加します。型別の保存領域は初回追加時に作られるため、Registryに専用関数を増やすことは必須ではありません。
3. `Systems/` に更新処理を作り、`scene.for_each<transform_component, rotator_component>` で両方のComponentを持つEntityだけを処理します。`transform.rotation[1] += degrees_per_second * delta` のように、秒単位の経過時間を掛けます。Transformの回転値は度です。
4. `native_preview12::update` から呼びます。ゲーム処理には `scene_delta` を渡すとPauseで停止しStepで進みます。現在の順序はAnimation更新→歩行テスト入力→床Colliderの同期→Physics更新→歩行テストのTrigger・落下・カメラ更新です。移動要求をPhysicsへ渡すSystemはPhysicsより前、接触イベントを読むSystemは後に呼びます。
5. `.cpp` を追加したら `Rendering/3dgp.vcxproj` のビルド対象へ登録します。Visual Studio上の分類には `.filters` も更新します。ヘッダーを置くだけでは新しい `.cpp` はビルドされません。
6. `NativePreview.cpp` のInspectorへ追加・編集UIを用意します。Undo対象の編集では、値を変更する前にSnapshotを保存する既存UIの方法に合わせます。Releaseのゲーム進行はUIがなくても動くようにします。
7. `save_scene` と `load_scene`、`save_prefab` と `load_prefab` の両方へ項目を追加します。古いファイルに項目がなくても既定値で読めるようにします。Registryの複製に参加することと、JSONに保存されることは別です。
8. 配置、数値変更、複製、Undo／Redo、Scene再読込み、Prefab再配置、Pause／Stepを確認します。最後にDebugとReleaseをビルドし、Releaseでもゲーム処理が動くことを確認します。

物理移動では、毎フレームTransformを独自に押し戻すより、RigidBodyやCharacter Controllerの速度・要求値を書いてPhysicsへ渡します。詳細は `PHYSICS_GUIDE.md`、配置の操作は `MAP_EDITOR_GUIDE.md` を読んでください。

## DirectX 12の処理を変更するとき

- ResourceはGPUが使う頂点BufferやTexture本体、Descriptorはそれをシェーダーへ知らせる参照枠です。`DescriptorAllocator` で参照枠を確保し、本体と参照枠の両方の寿命を管理します。
- Command ListはGPUに実行してもらう命令の記録です。C++関数が戻ってもGPU処理が終わったとは限りません。フレーム資源の再利用と解放ではFence（GPUの完了番号）を確認します。
- BarrierはResourceの用途を切り替える命令です。たとえば描画先へ書いたTextureをSSRで読む前に、描画先からシェーダー入力へ切り替えます。具体例は `SceneRenderTarget.cpp` の `begin`／`end` で確認します。
- Root Signatureはシェーダーに渡す情報の配置規則、PSOはシェーダー・深度判定・合成などの描画設定です。材質項目を増やすときは `GltfRenderer.h` の定数、`.cpp` の書込み、HLSLの定義とregister指定を合わせます。
- `std::atomic` はCPUのスレッド間通知です。GPU完了待ちには使えません。現在の処理を調べるときは実装のQueueとFenceを追い、開発ガイドの将来方針と区別してください。

## 作業途中と将来機能の見分け方

Gitの変更・未追跡ファイルは「まだコミットされていない」ことを示します。これだけでは実装が未完成か、確認後にコミットしていないだけかは判断できません。

現在の拡張課題には、親子Transform、複数Entity Prefab、Prefab Apply／Revert、Componentの保存処理共通化、複数選択があります。物理では回転Box、正確なCapsule衝突、CCD、Jointなどが未対応です。これらは既存の基本機能と区別してください。
詳細は `DirectX12/TEAM_PRODUCTION_GAP_ANALYSIS.md` と `Documentation/PHYSICS_GUIDE.md` にあります。

## ライティングと材質の調整

太陽の色・強さ、外部画像なしの環境光、落ち影、SSAO、HDR色調を追加しています。ProjectのSamplesから04/05の照明見本を開けます。[調整・保存・実装手順](LIGHTING_GUIDE.md)を参照してください。

## 3Dステージと歩行確認

06_Ruins_Courtyardは配置・物理・Triggerを組み合わせた見本です。PlaytestControllerComponent、PlaytestSystem、NativePreviewのWalk Testから処理を追えます。[ステージの操作・保存・実装案内](STAGE_SAMPLE_GUIDE.md)を参照してください。

## 一人称Playerを組み込む

Player Controllerの設定はPlaytestControllerComponent、入力・視線計算はPlaytestSystem、Play開始・Raw Input・カメラ統合はNativePreviewにあります。[配置からPlayまでの手順](FIRST_PERSON_PLAYER_GUIDE.md)を参照してください。

上部ツールバーは頻度の低い操作を「ファイル」「編集」「表示・詳細」へ収納しています。ダッシュ・段差乗越え・Escポーズの設定と呼出し順は [一人称Playerガイド](FIRST_PERSON_PLAYER_GUIDE.md) を参照してください。

## 日本語UIと項目の説明

設定名とツールチップは DirectX12/EditorUiHelp.h で管理します。枠名と保存キーは維持します。[操作と追加手順](EDITOR_UI_GUIDE.md)を参照してください。

モデルのドラッグ配置・共通Snap・デバッグ線の操作と実装は [MAP_EDITOR_GUIDE.md](MAP_EDITOR_GUIDE.md) を参照してください。

ドラッグ中の3D仮表示はNativePreviewのmodel_placement_preview、確定と共通の位置補正はMapEditorSystem::model_transformです。HierarchyはEntity IDで行を区別し、同名の物体を個別選択できます。

## オンライン通信と巻き戻し同期の開発用基盤

`Rendering/Networking` はゲームに依存しないUDP・入力履歴・2人用ロールバックです。
`Tools/NetworkDemo` と `Tools/NetworkTests` が実際に呼び出す使用例です。
EditorのPlayへは未接続です。2週間制作では通常起動のVERSUS → LAN接続から部屋作成/参加/切断ができ、同一PCの2プロセスで実UDPと描画を併用した同期を確認しています。実装済みの範囲、他ゲームへの接続口、保存・Inspectorとの関係、Debug／Releaseの確認手順は [オンライン実装ガイド](ONLINE_NETWORKING_GUIDE.md) を参照してください。

再計算中の音・振動などの出力抑止には`Networking/ReplayEffects.h`の`replay_effect_gate`を使います。
出力入口で確認する使い方と、未実装のイベント照合の範囲は[オンライン実装ガイド](ONLINE_NETWORKING_GUIDE.md)を参照してください。


項目ごとのStateデータ化は`Networking/StateArchive.h`、イベントを繰り返さない入力予測の接続口は`rollback_callbacks::predict_remote`です。現在の範囲と上限は[オンライン実装ガイド](ONLINE_NETWORKING_GUIDE.md)の最新追記を参照してください。


2026-10-27: `rollback_callbacks::state_checksum`で、確定Stateの戦闘だけを比較する値を取り出せます。未指定は全bytes比較です。2週間制作では23 ComponentとruntimeのState形式、実モデルを使う2世界の仮想回線テストを追加しています。現在の範囲は[オンライン実装ガイド](ONLINE_NETWORKING_GUIDE.md)を参照してください。

開始Stateの分割転送と開始確認はNetworking/RoomHandshake.hです。共通部品はゲームのsave/load/stepへ接続して使用します。現在のエンジンEditorのScene/Prefab保存、Inspector、複製/Undoには接続していません。2週間制作側のOnlineBattle.cppが部屋と実時間のソケットを所有する使用例です。

接続前の内容比較はNetworking/ContentFingerprint.hとRoomHandshakeのmanifest、訂正後の音・振動要求の確定出力はNetworking/ConfirmedEffects.hです。部屋内再戦はrequest_rematch/restartで新しい試合番号へ移ります。ゲーム固有の接続例は2週間制作のOnlineBattle.cppとGameAudio.hです。


LANの部屋自動検索：[LAN_DISCOVERY_GUIDE.md](LAN_DISCOVERY_GUIDE.md)（共通部品と2週間制作の接続UI、手入力への切替）。


設定・接続画面のパッド操作：[CONTROLLER_MENU_GUIDE.md](CONTROLLER_MENU_GUIDE.md)。同じLANの接続手順と手入力：[LAN_CONNECTION_GUIDE.md](LAN_CONNECTION_GUIDE.md)。
