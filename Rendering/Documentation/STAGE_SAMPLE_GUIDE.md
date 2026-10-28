# 石造りの中庭ステージ：配置から歩行確認まで

`06_Ruins_Courtyard.jscene` は、102個の部品で作った3Dマップの見本です。石の中庭、門、細い橋、ジャンプ用階段、上段のゴールがつながっています。地形・模様は自作生成形状とシェーダーで表現し、外部モデル・画像は使いません。完成ゲームのアートではなく、通路幅、段差、当たり判定、配置の練習に使います。

## 最初に遊ぶ

1. x64 Debugをビルドし、作業ディレクトリを `Rendering` にして起動します。制作中のSceneは先に別名保存してください。
2. 右上の `Project` → `Samples` → `Maps` を開き、下へスクロールして `06_Ruins_Courtyard.jscene` をダブルクリックします。初期状態はPauseです。モデル読込みが完了するまで待ちます。
3. 上部の `Play` を押します。配置済みの青い箱を操作し、一人称のため自分のモデルは表示しません。マウスで視点を変えます。WASD／矢印キーで画面の向きに沿って移動、Spaceで接地中にジャンプします。
4. 正面の門をくぐり、黄色い床のチェックポイントへ進みます。細い橋を渡り、そのまま歩いて階段を上がり、緑のゴールへ向かいます。Shiftでダッシュできます。
5. Rで最後のチェックポイントへ戻ります。到達前は開始地点へ戻ります。赤い床に触れた場合やY=-6mより下へ落ちた場合も復帰します。橋の手すりの切れ目で落下確認ができます。ゴール到達時はGameに案内が出ます。
6. Escでポーズ画面を開き、「編集に戻る」で終了します。「再開」または再度Escでプレイを続けます。プレイヤー、物理で動いた箱、カメラ、選択状態がテスト開始前へ戻り、Play/PauseとGizmoも元に戻ります。配置モードは安全のためOFFになります。

テスト中は保存・配置・削除・Inspector編集を無効にし、実験結果を制作Sceneへ書き込まないようにしています。右上のPhysics Monitorでは接触を観察できます。カメラは背後のColliderをRaycastして壁の手前へ寄ります。これは点のRayによる簡易回避で、カメラ全体の体積による判定ではありません。First PersonをOFFにした追従視点ではScene上の右ドラッグで向きを変更できます。一人称の設定・操作は [FIRST_PERSON_PLAYER_GUIDE.md](FIRST_PERSON_PLAYER_GUIDE.md) を参照してください。

PlayerがあるSceneでは **Play** もWalk Testと同じ操作開始・停止時復元を使います。PlayerのないSceneの通常の `Play/Pause` は時間の切替だけで、状態を巻き戻しません。再挑戦は一度終了してから開始します。Sceneを開き直すと保存済み配置へ戻ります。

## 自分のマップへ編集する

Walk Testを終了してから、Sceneの物体をクリックして選択します。対象は有効なColliderのある物体です。装飾のようにColliderがない物体はHierarchyで選びます。Colliderは枠線で表示するため床を選んでも見た目を覆いません。W/E/Rで移動・回転・拡縮を切り替え、GizmoまたはInspectorで調整し、Ctrl+Z／Ctrl+Yで取消し・やり直しできます。

追加部品は `Samples` → `StageKit` → `Prefabs` にあります。

| Prefab | 用途と設定 |
| --- | --- |
| `StoneTile_4m` | 幅4m・高さ0.25m・奥行4mの床、Static Collider |
| `StoneWall_4m` | 幅4m・高さ2.4mの壁。Boxは回転しないため0/90度以外の壁は避ける |
| `Column_3m` | 高さ3mの柱、Static Collider |
| `Bridge_Tile` | 幅4m・奥行1mの橋床 |
| `Physics_Crate` | 動く金属箱、Dynamic RigidBody |
| `Checkpoint` | 黄色のTrigger、Gameplay Tag=`Checkpoint` |
| `Goal` | 緑のTrigger、Gameplay Tag=`Goal` |
| `Player_FirstPerson` / `Player_Tester` | 青い操作用の箱。Collider・Character Controller・Playtest Controllerを組合せ済み |

1. Prefabを一つ選び、InspectorのMap Toolsで `Placement Mode` をONにします。`Placement Scale=1`、`Placement Grid=0.5`、`Add Static Box Collider=OFF` から始めます。既存PrefabのTriggerやDynamic設定を自動Colliderで上書きしないためです。
2. 下段床の上面は0m、上段は2mです。Grid PlaneのPlane Heightを合わせるか、Collider Surfaceで既存の床面へ置きます。装飾の薄い敷石にはColliderがないため、その下の土台へ当たります。
3. Sceneをクリックして配置し、Escで配置を終了します。Undoは一回の配置単位です。Prefabの底面中央が配置基準で、縦横比を保って倍率を掛けます。
4. 操作用Entityは一つにします。Player_Testerを複製したままではWalk Testを開始できません。有効なCollider（Triggerではない）、有効なCharacter Controller、Playtest Controller、Transformが必要です。
5. `Save As` で自分のScene名へ保存し、開き直して位置・Trigger・操作速度が残ることを確認します。選択Entityだけを再利用する場合は `Save Prefab` を使います。複数EntityをまとめたPrefabは未対応です。

Playtest ControllerのInspectorで移動速度、落下復帰Y、希望カメラ距離を変更できます。数値変更・Component追加はUndoへ記録します。設定はScene/Prefabへ保存し、チェックポイント位置・復帰回数・ゴール到達状態は保存せず、開始時に初期化します。石の壁・門は開口部を塞がないよう柱・梁を別々のColliderにしています。

## 実装を読む順序・ゲーム機能へ広げる手順

1. `Components/PlaytestControllerComponent.h` が設定と実行中の状態です。Componentはデータだけを持ち、入力・物理処理を呼びません。
2. `Systems/PlaytestSystem.h/.cpp` の `begin` が有効な操作用Entityを一つ確認し、開始位置を復帰地点にします。`update_input` はカメラ向きからXZ速度を計算し、Character Controllerへ要求します。斜め入力は正規化し、接地している場合だけジャンプ速度を設定します。
3. `NativePreview.cpp::update` の順番はAnimation→歩行入力→表示床Colliderの同期→Physics→Trigger結果と落下判定→カメラ追従です。Physicsは固定60Hzです。Colliderを直接動かさず移動要求を先に渡し、接触イベントは後から読みます。非表示の確認用反射床はColliderも無効にしています。
4. `after_physics` はGameplay Tagで `Checkpoint` / `Goal` / `Hazard` を判別します。見た目の色やEntity名には依存しません。チェックポイントはEnter時、Goal/HazardはEnter/Stay時に処理します。Goalは案内表示のみで、Scene遷移や報酬は未実装です。
5. `start_walk_test` / `stop_walk_test` はRegistry全体のSnapshotを退避・復元し、PhysicsとAnimationの古いCacheを破棄します。GPUモデルは共有したままで、テスト中のゲーム状態だけを復元します。通常の編集Undo履歴とは分けて管理します。
6. Inspector、`save_scene` / `load_scene`、`save_prefab` / `load_prefab` に新Componentの設定を追加しています。将来パラメーターを増やすときもこの5箇所を更新します。Registryの型別コピーが複製とSnapshotに対応するだけでは、JSON保存には入りません。新しいSystemの.cppはvcxprojとfiltersにも登録します。
7. 見た目は `GltfModel.cpp` が材質extrasの `jisaku_stone: true` を読み、`GltfRenderer.cpp` が材質フラグbit64でワールド座標ノイズの石模様・粗さを適用します。遠方の細かい粒は画面上の変化量で弱めます。通常のアート材質には適用しません。太陽・影・環境光・AOは [LIGHTING_GUIDE.md](LIGHTING_GUIDE.md) を参照してください。
8. `GltfRenderer.cpp::begin_frame` は静的モデル用の単位関節行列をフレームに一つ確保します。各部品・各描画で同じ16KBを重複確保しないため、多数の床・壁と影を描いても定数領域を圧迫しにくくしています。スキニングが必要なモデルは姿勢ごとに確保します。

回転Box、正確なCapsule、連続衝突判定、歩ける傾斜面、複合Prefab、完成ゲーム向けのUI/入力割当はまだありません。高速移動や細い床を含む本番レベルでは追加対応が必要です。今回の部品は水平床と軸に沿うBoxで構成しています。角度90度の壁は見た目だけ回さず、Colliderの寸法も合わせてください。

## 再生成と確認

`Tools/GenerateStageSample.ps1` が06のScene、StageKitの7モデル・9Prefabを生成します。基本形状がない場合は先に `Tools/GenerateSamples.ps1` を実行します。再生成で同名ファイルを上書きするため、自作マップは別名にしてください。第三者素材や参考資料フォルダーには書き込みません。

リポジトリ直下のPowerShellから、次のテストを実行できます。

```powershell
$msbuild='C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe'
& $msbuild ./Tests/PlaytestTests.vcxproj /p:Configuration=Debug /p:Platform=x64 /verbosity:minimal
./x64/Tests/Debug/PlaytestTests.exe ./Rendering/resources/samples/Maps/06_Ruins_Courtyard.jscene
```

このテストは実際の06のColliderを読み、操作速度・ジャンプ条件・中庭からGoalまでの物理移動・Checkpoint・落下／手動／Hazard復帰・複製・Snapshot復元を検証します。描画や保存UIは検証しないため、Debug Editorでも読み込み、選択、配置、Undo、保存・再読込み、Walk Test終了の復元を確認してください。ReleaseでもエンジンとTestsをビルドします。現在のReleaseはEditor UIを含まず、06を選んでWalk Testする入口はDebug専用です。本番ゲームへのステージ選択・操作開始は別途ゲーム起動処理へ組み込んでください。
