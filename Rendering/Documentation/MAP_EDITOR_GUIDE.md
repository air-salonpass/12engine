# 汎用Map Editorガイド

このMap Toolは特定ジャンル専用ではありません。床、壁、足場、背景、障害物、建物など、glTF／GLB／Prefabで作られたScene部品を共通手順で配置します。

## 基本操作

1. `Project`タブで配置したい`.gltf`、`.glb`、`.jprefab`を1つ選択する。
2. Inspector上部の`Map Tools`を開く。
3. `Placement Mode`を有効にする。
4. Scene Viewをクリックして配置する。
5. 配置を終了するときはEscを押す。

配置したEntityには`map_object_component`が付き、通常のScene保存、Undo／Redo、Gizmo編集へ参加します。初回だけAssetの非同期ロードを待ちますが、Editor全体を停止させません。

## Placement Surface

- `Grid Plane`: 指定した`Plane Height`の水平面へ置く。見下ろしマップ、床、足場の作成に向く。
- `Collider Surface`: Rayが当たったCollider位置へ置く。地形や既存ステージ上へ装飾を置く用途に向く。何にも当たらない場合はGrid Planeへフォールバックする。

`Snap XZ To Grid`は高さを変えずXZだけを吸着します。立体的な地形でもColliderのHit高さを失わないためです。

## 共通設定

- `Placement Grid`: 配置間隔。
- `Placement Yaw`: 配置時のY回転。`+90`で直角に回す。
- `Placement Scale`: 配置時の一様Scale。
- `Add Static Box Collider`: Model BoundsからStatic Box Colliderを自動生成する。

自動ColliderはGraybox制作向けです。入口がある建物や凹形状は、単純なBoxでは塞がるため、子Colliderまたは専用Collision Meshへ置き換えてください。

## プランナー向け運用

- Asset名は用途が分かる名前にする。例: `Wall_2m`、`Floor_4m`、`Door_Frame`。
- 同じ規格の部品はPivot、正面方向、寸法を統一する。
- ゲーム上の意味はEntity名ではなくGameplay TagとComponentへ設定する。
- 頻繁に使う組合せはPrefab化し、モデルを毎回個別調整しない。
- Scene保存前にGame Viewで高さ、Collider、キャラクターの通路幅を確認する。

## 現在の制限

- 現在は単体選択。矩形複数選択と整列は次段階。
- Prefabは単体Entity形式。複数Entity PrefabとApply／Revertは次段階。
- Collider Surfaceへ位置は吸着するが、法線方向への自動回転は未実装。
- Asset Thumbnail、Brush消去、範囲塗り、Scene分割は未実装。

Map編集機能は`map_editor_system`へ追加し、D3D12 RendererやゲームSystemへEditor都合の分岐を追加しないでください。


## 3Dの見本で試す

ProjectのSamplesボタンからMapsとPrefabsを開けます。見本は外部モデル・画像を使わない生成形状で、配置・物理・Trigger・Animationを確認できます。[見本の操作手順](../resources/samples/README.md)を参照してください。

## ステージを歩いて確認する

06_Ruins_Courtyardには中庭・橋・階段・ゴールと、再利用用StageKit Prefabがあります。配置モードがOFFならSceneのColliderをクリックして選択できます。Walk Testでは操作と復帰を確認し、停止時に開始前の配置へ戻ります。通常のPlay/Pauseとは別の機能です。[具体的な制作手順](STAGE_SAMPLE_GUIDE.md)を参照してください。

## 一人称Playerを配置する

StageKit/Prefabs/Player_FirstPersonを一つ配置し、Playでその位置から操作できます。06には配置済みです。マウス視点・WASD移動・Spaceジャンプ、Escのポーズ画面から「編集に戻る」を選んで編集状態へ戻します。[詳しい設定方法](FIRST_PERSON_PLAYER_GUIDE.md)を参照してください。

## Projectからモデルをドラッグして置く

Projectの.glb/.gltfファイルをSceneの画像上へドラッグして離すと、その地点へモデルを配置します。配置モードをONにする必要はありません。ドロップ先のColliderを優先し、何も当たらなければ配置面の高さの水平面を使います。モデルの底を配置先へ合わせます。読込みは非同期で、初回は完了後に配置します。画像の外、Game、プレイ中には配置しません。Prefabは従来のクリック配置・ダブルクリック追加を使います。

配置完了後、Ctrl+Zで一つ戻し、Ctrl+Yでやり直せます。モデルを置いても判定は自動追加しません。当たり判定も必要なら「固定の箱型判定を自動追加」を有効にするか、Inspectorから追加します。Sceneを保存するとTransform・Model Renderer・Map Object・追加したCollider/RigidBodyが残ります。

## Scene上部のSnapとGrid (m)

SnapをONにするとモデル配置とGizmoの移動・回転・拡縮を指定した刻みへ合わせます。OFFでもCtrlを押している間は一時使用できます。Grid (m)は移動・配置の間隔で、Snapの右クリックから回転（既定15度）・拡縮（既定0.1）の刻みも変更できます。配置は地形の高さを保つためXZだけ、移動GizmoはXYZに吸着します。

Inspectorの「マップ配置ツール」の配置間隔・吸着ON/OFFも同じ設定です。従来のMap Objectごとのsnap_to_grid/grid_sizeはScene/Prefabの保存互換用に残していますが、編集時はScene上部の設定を全物体へ共通適用します。設定自体は編集環境の項目で、配置やTransformのUndo履歴には入りません。

## Scene View → Debug Lines

Scene画像の上にあるScene ViewメニューでDebug Lines（判定の線）を切り替えます。有効なColliderの外接枠を描き、選択物は緑、検知範囲は黄、ほかは青です。見た目だけの切替なので衝突は変わりません。GameやReleaseには線を描きません。編集時だけ表示し、Player操作中は非表示です。球・カプセルも現状は外接箱の線です。

Snap、Grid、回転/拡縮の刻み、Debug LinesはSceneのeditor項目に保存します。古いSceneに項目がなくても既定値で開きます。これらはビューポート共通設定なので単体Prefabへは追加しません。

## 実装の入口と確認

ProjectのInvisibleButtonへBeginDragDropSourceを登録し、パスをImGuiのPayloadへコピーします。SceneではImage直後のBeginDragDropTargetで受け取り、Imageの実表示矩形・view/projectionからMapEditorSystem::make_rayを呼びます。地面確認→水平面への代替→スナップ→request_map_placement→非同期完了時の配置という順序です。配置確定前にSnapshotを積み、Registryの複製・Undo/Redoへ参加させます。新ComponentやShaderは増やしていません。

Snapの座標計算と値の検証はSystems/MapEditorSystem、UI・保存・配置要求と線描画はDirectX12/NativePreview.cpp、項目の説明はEditorUiHelp.hを読みます。描画用のview/projectionと実際のImageの矩形を一致させ、Gizmoや設定欄をドロップ先と取り違えないようにしてください。

Tests/PlaytestTestsにはSnapのON/OFF、Ctrl一時使用、負座標、地面の高さの保持、不正な刻み、Imageの内外とRayの交差を追加しています。Debugでは実際のドラッグ配置・Undo/Redo、Snapの右クリック、Gizmo、線の表示、Sceneの保存・再読込みを確認します。Debug/Releaseを両方ビルドし、ReleaseへImGui依存を持ち込まないことを確認します。
## ドラッグ中のモデル表示と同名の物体の選択

モデルをProjectからSceneの画像へドラッグしている間、配置予定の位置へ3Dモデルを仮表示します。Snap・Ctrl・配置倍率・Y回転・モデル底面の補正は確定時と同じ計算です。初回の素材はドラッグ開始時から非同期で読み、未読込みの間は案内を表示します。読み終わるとモデルが表示されます。モデルがScene外へ出ると仮表示を消し、Sceneへ戻すと再表示します。

離すとその場所へ一つ配置し、Ctrl+Zで取り消せます。Escはドラッグを取消し、アプリは終了しません。押したまま動かしても確定しません。Scene以外へ離した場合も配置しません。仮表示はSceneのEntityではないため、Hierarchy・物理・Scene/Prefab保存・Undo履歴には入りません。Game画面にも仮モデルを表示しません。

Hierarchyの行は表示名ではなくEntity IDで区別します。同じモデル名の物体を何個置いても、それぞれ選択してInspectorで編集できます。行末の[番号]とInspectorのEntity番号が対応します。表示上の番号を加えるだけで保存済みの名前やAssetパスは変えません。Sceneを読み直した場合はEntity番号を割り直すため、別の番号になる場合があります。

実装ではmodel_placement_previewをRegistryとは別に保持し、AcceptBeforeDeliveryでドラッグ中の位置を更新、IsDeliveryのときだけ配置要求を出します。MapEditorSystem::model_transformで原点補正を共通化しています。UI確定後の次の描画フレームへ表示するため、追従には最大1フレームの遅れがあります。仮モデルを含むScene画像を作った後、ドラッグ中だけ通常Sceneを再描画してGameへ渡します。この間は追加の描画負荷が掛かります。仮モデルの影や平面反射は生成しません。

変更を確認する場合は、初回読込みと読込み済みのモデル、Scene内外への移動、Esc取消し、ドロップ後のUndo/Redoを試します。同じモデルを二つ置いてHierarchyの各行を選び、片方の位置だけを変更し、保存・再読込み後にも両方を選べることを確認してください。