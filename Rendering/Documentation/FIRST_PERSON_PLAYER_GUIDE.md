# 配置したPlayerをPlayで一人称操作する

## サンプルで試す

1. x64 Debugで起動し、Project → Samples → Maps → `06_Ruins_Courtyard.jscene` を開きます。作業ディレクトリは `Rendering` です。制作中のSceneは先に別名保存してください。
2. モデル読込みが終わったら、上部の **Play** を押します。Scene内のPlayerの足元位置とY回転から開始します。PlayがPlayerを新規生成するのではなく、配置済みの一つを操作します。
3. マウスで視点、WASD／矢印で移動、Spaceでジャンプ、RでCheckpointへ復帰します。橋と階段を進み、緑のゴールへ到達できます。階段はStep Height=0.45mの範囲内なら歩いて登れます。Shiftを押している間はダッシュします。
4. **Esc** または上部の **Pause** でポーズ画面を開きます。「再開」で続け、「編集に戻る」で停止します。ポーズ中は物理・移動・視点を止め、カーソルを解放します。配置・カメラ・物理状態を開始前へ戻します。テスト中のScene保存と編集は無効です。歩行テストの入口は「表示・詳細」メニューにも収納しています。
5. Alt+Tabなどで別アプリへ移るとカーソルを解放し、移動入力と視点入力を止めます。エンジンへ戻ってポーズ画面の「再開」を押します。フォーカスを失った場合も自動でポーズします。復帰後に「再開」を選びます。

自分のモデルは一人称描画から除外し、目の中にモデルが映り込まないようにしています。落ち影にはモデルを使います。中央の十字は視線の目安で、攻撃やインタラクト判定はまだありません。

## 自分のSceneへ配置する

1. `Samples/StageKit/Prefabs/Player_FirstPerson.jprefab` を選びます。Map ToolsでPlacement ModeをON、Scale=1、Add Static Box ColliderをOFFにして床へ配置します。ダブルクリックで追加してからInspectorで位置調整しても構いません。
2. 操作用PlayerはSceneに一つだけ置きます。見本にはすでに配置済みなので、追加する場合は既存Playerと置き換えてください。複数ある場合や、有効なCollider・Character Controllerがない場合は開始せずエラーを表示します。
3. TransformのPositionが足元、Rotation Yが開始時の正面です。見本形状の正面は+Z、Y=90度で+Xを向きます。X/Z回転は初期視線には使いません。
4. Inspectorの **Player Controller** でFirst PersonをONにし、Eye Height、Mouse Sensitivity、Player FOV、Walk Speedを調整します。Eye Heightは足元からの実寸mです。モデルを拡縮しても自動連動しないため、Colliderの高さ内へ合わせます。Character ControllerのJump SpeedとGravityでジャンプを調整します。
5. Save AsでScene、Save Prefabで選択Playerを保存できます。再読込みしてからPlayで確認します。Component追加・数値変更・配置・複製は通常のUndo/Redoに対応します。Playerを複製した直後は二つになるため、一つを削除してから開始してください。

First PersonをOFFにすると従来の追従カメラに戻ります。PlayerのないSceneではPlay/Pauseは従来の時間切替です。PlayerのあるSceneのPlayは操作開始、ポーズ画面の「編集に戻る」はSnapshot復元という違いがあります。

## 実装の入口と変更手順

- `Components/PlaytestControllerComponent.h`：Player Controllerのデータです。既存の移動・復帰設定へ `first_person`、`eye_height`、`mouse_sensitivity`、`field_of_view` を追加しました。旧Sceneに新しいキーがなくても一人称の既定値で読みます。
- `Systems/PlaytestSystem.cpp`：入力から速度・ジャンプ要求、Checkpoint/Goal/Hazard、落下復帰を処理します。`apply_mouse_look` はマウス差分からyaw/pitchを更新し、上下を±85度に制限します。`view_direction` は描画用の視線を作ります。歩行はyawだけを使うので、上を見ても移動速度が落ちません。
- `DirectX12/NativePreview.cpp::toggle_play`：Player Componentがあれば `start_walk_test` を呼び、なければ通常のPlay/Pauseを切り替えます。開始時にScene全体をコピーし、配置済みPlayerを一つ選びます。停止時は元のSceneへ戻し、古いPhysics/Animation Cacheを消します。
- `handle_message` の `WM_INPUT`：Win32 Raw Inputの相対移動を受け取ります。毎フレーム画面中央へマウスを戻す方式ではなく、フレーム間の相対差分を使います。`WM_KILLFOCUS` と停止・終了でカーソル制限を解除します。視点操作とカーソル処理はEditor統合側、計算自体はSystem側へ分けています。
- `update`：入力要求→Physics→Trigger/復帰→Playerの足元+目の高さへカメラ更新の順です。`render` はその位置から視線へ向く行列を作り、一人称時の操作用モデルだけ通常描画から除きます。描画中にゲーム時間は進めません。
- Inspectorと `save_scene` / `load_scene`、`save_prefab` / `load_prefab`：設定を手動登録しています。新設定を増やしたらこの5箇所をそろえます。CheckpointやGoalなど実行中の状態は保存しません。Registryの型別コピーで複製・Undo・停止時復元に参加します。

新しいゲーム操作を追加する場合は、データをComponent、状態更新をSystem、OSのキー取得をNativePreviewへ置きます。例えば攻撃の入力をSystemへ渡し、PhysicsのRaycastで対象を調べ、結果を表示Componentへ記録します。今回、攻撃・しゃがみ・入力割当画面は未実装です。Colliderの制限は [PHYSICS_GUIDE.md](PHYSICS_GUIDE.md) を参照してください。

## 確認方法

Debugで06を開き、Play、マウス左右・上下、移動とジャンプ、R、Esc、再Play、フォーカス切替を確認します。停止後にPlayer位置と編集カメラが戻ること、保存・再読込みで設定が残ることも確認します。

`Tests/PlaytestTests.vcxproj` をDebug/Releaseでビルドし、生成した `PlaytestTests.exe` へ `Rendering/resources/samples/Maps/06_Ruins_Courtyard.jscene` を渡します。視点の符号・上下制限、視線と歩行の方向、複数Playerの拒否、実ステージのゴール到達と復帰をGPUなしで検証します。Raw Inputの到着と画面表示は実機でも確認します。

エンジン本体もx64 Debug/Releaseをビルドします。現在のReleaseはEditor UIとPlayボタンを含みません。今回の「配置→Play」の確認入口はDebug Editorです。提出ゲームの起動Scene読込み・Player開始をReleaseへ自動接続する起動機能は別途必要です。

## ダッシュ・段差・ポーズの調整

Shiftを押している間はWalk Speed×Sprint Multiplier（既定1.8倍、5m/s→9m/s）です。斜め入力は正規化します。倍率はPlayer ControllerのInspectorから変更でき、Scene/Prefabへ保存します。
Character ControllerのStep Heightは足元から登れる段差の実寸mで、見本は0.45mです。0で無効にできます。PhysicsSystemは接地中かつ落下方向のときだけ、移動先の静的Box地形の高さと頭上の空間を調べて登ります。高い壁・低い天井・Trigger・Dynamicの箱には登りません。下りも同じ高さ範囲だけ接地面へ吸着し、大きい落下は通常の重力に任せます。一人称カメラだけ高さを滑らかに追従させ、足元Colliderの反応を遅らせません。正確な傾斜面・回転Boxへの対応とは別です。

`NativePreview.cpp::set_player_paused` がポーズ状態とカーソルを管理し、updateのゲーム時間を0にします。Escはポーズ切替、画面の「編集に戻る」がSnapshot復元です。ポーズ状態そのものはScene/Prefabへ保存しません。

上部の常設操作は「開く・保存・Undo/Redo・移動/回転/拡縮・Play/Pause」です。「ファイル」へ新規・別名保存・モデル・Prefab、「編集」へ複製・フォーカス・床への配置、「表示・詳細」へGizmo・全画面・1フレーム進行・歩行テストを収納しています。Ctrl+S、Ctrl+Shift+S、Ctrl+D、F、W/E/R、F11も使えます。