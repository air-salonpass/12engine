# 3D機能見本の使い方

このフォルダーは操作練習用です。01～04はコードで生成した形状と材質を使い、05だけ交換可能な確認用フリーモデルを使います。完成ゲームのアート素材とは分けて管理してください。

## 開き方

1. Visual StudioでDebug版をビルドし、作業ディレクトリを `Rendering` にして起動します。
2. 制作中のSceneがあれば、先に別名で保存します。現在はSceneを開く前の保存確認が未実装です。
3. 右上の `Project` タブで `Samples` ボタンを押します。
4. `Maps` をダブルクリックし、`.jscene` をダブルクリックします。Sceneは置き換わります。起動モデル読込み中は完了を待ってから開いてください。
5. `Hierarchy` で物体を選び、Sceneで位置・回転・拡縮、InspectorでComponentを確認します。Fで選択物へカメラを寄せられます。

サンプルのファイル名・Entity名はパスと検索の扱いやすさのため英数字です。この説明に用途をまとめています。

## マップ3種

| Scene | 何を見るか | 試す操作 |
| --- | --- | --- |
| `01_Map_Placement.jscene` | 4mの床9枚、壁2枚、段差3段、Goal Trigger、浮く目印 | Pauseして床や壁を複製・移動、Ctrl+Zで戻す。Map ToolsでPrefabを追加配置する |
| `02_Physics_And_Trigger.jscene` | 落下する箱3個、接地する黄色のCharacter Controller、緑のTrigger | Playで落下と衝突を確認。InspectorのPhysics Monitorで接触を読む。Pause/Stepで進める |
| `03_Shared_Animation.jscene` | 同じモデルを3つ共有し、別の速度・時刻で上下アニメーション | 各EntityのAnimationを停止・速度変更する。一つの変更がほかへ波及しないことを確認する |

黄色のControllerは接地確認の見本です。移動入力のゲームSystemはまだないため、WASDで歩くプレイヤーではありません。W/E/RはEditorの移動・回転・拡縮切替です。
緑のGoalはTrigger設定の見本で、クリア演出・Scene遷移は実装していません。Physics MonitorのTrigger Enter/Stay/Exitで確認します。Enter/Exitは短時間なので、Stayと接触数が観察しやすい項目です。

現在のPlay/Pauseは時間の進行を切り替える機能です。Play前の位置へ戻す機能は未実装なので、物理の見本をやり直すときはSceneを開き直します。Pauseは巻戻しではありません。

## Prefab6種

| Prefab | 内容 | 配置の目安 |
| --- | --- | --- |
| `Floor_4m.jprefab` | 4×0.25×4mの床、Static Box Collider | Grid 4で並べる |
| `Wall_4m.jprefab` | 4×2×0.25mの壁、Static Box Collider | Yaw 0/90度で壁を組む |
| `Crate_Dynamic.jprefab` | 1mの箱、Dynamic RigidBody | 高い位置へ移しPlayで落とす |
| `Goal_Trigger.jprefab` | 2×0.15×2mの緑の範囲、Goal Tag、Trigger | 物理の箱が触れる場所へ置く |
| `Controller_Standing.jprefab` | 黄色い箱、Box Collider、Character Controller | 地面より上へ置いて接地を確認 |
| `Floating_Marker.jprefab` | 紫の目印、見た目用上下アニメーションのモデル | Goal等の場所を示す装飾に使う |

1. ProjectのSamples→Prefabsで一つクリックします。ダブルクリックなら現在のSceneへ直接追加します。
2. InspectorのMap ToolsでPlacement ModeをONにします。
3. Placement Scaleはまず1、Add Static Box ColliderはOFFにします。Prefabに入っているColliderとRigidBodyを使うためです。ONは既存設定をStatic Boxへ置き換える操作です。
4. Grid Planeなら床面の高さをPlane Heightへ設定します（サンプル床の上面は通常0.25m、物理用床は0.5m）。Collider Surfaceなら既存の床に直接吸着します。
5. Sceneをクリックして配置し、Escで配置を終了します。Ctrl+Zで配置を取り消せます。
6. Save Asで自分のScene名を付けて保存します。

Prefabの縦横比は保持し、Placement Scaleを倍率として掛けます。現在は単体Entity形式なので、床と壁を一つにした複合Prefabはまだ作れません。
すべての箱の原点は底面中央です。モデルは幅1・高さ1・奥行1で、Transformのscale3を実寸として使います。Colliderの中心はローカル座標で(0,0.5,0)、Half Extentsは(0.5,0.5,0.5)です。
アニメーションはモデル内ノードの上下移動です。EntityのTransformやColliderが動く見本ではありません。

## 読込みと再生成

Assetパスは `resources/samples/Models/` からの相対パスです。Sceneファイルの場所を基準にする形式ではないため、作業ディレクトリは必ずRenderingにします。
01～03のSceneはIBL強度0・床反射OFFです。エンジン起動時には既存の確認用モデルを読みますが、IBL強度0なら環境画像は読みません。サンプルを外部Assetなしにしたことと、提出用起動構成の完成は別です。

リポジトリ直下で `./Tools/GenerateSamples.ps1` を実行すると見本を再生成します。同名ファイルは上書きするので、自分で編集したマップやPrefabは先に別名保存してください。
生成器はglTFに頂点・法線・インデックスを埋め込むため、モデル用の別binやTextureファイルは不要です。

## 照明とリアル寄りの材質を試す見本

- 04_Lighting_Materials.jscene：外部画像なしの陶器・銀・銅・粗い石。太陽の向き、影、粗さによる反射を比較します。
- 05_Artist_Model_Preview.jscene：既存のDamagedHelmetを使う交換可能なモデル見本。権利表示はresources/gltf/DamagedHelmet/README.mdに保持しています。提出時はアート制作物へ差し替えます。

04/05はTools/GenerateLightingSample.ps1で再生成します。照明設定、保存方法、実装の読む場所は[ライティングガイド](../../Documentation/LIGHTING_GUIDE.md)を参照してください。01～04の形状はコード生成、05だけ確認用フリーモデルを使います。地形・火・水の外部素材は追加していません。

## 歩いて確認する3Dステージ

06_Ruins_Courtyard.jsceneは外部モデル・画像に依存しない石造りの中庭です。橋・ジャンプ階段・チェックポイント・危険地帯・ゴールを備え、Walk Testで移動、Spaceでジャンプ、Rで復帰、Escのポーズ画面から開始前の配置へ戻れます。StageKit/Prefabsに8種類の追加部品があります。[操作・編集・実装ガイド](../../Documentation/STAGE_SAMPLE_GUIDE.md)を参照してください。通常のPlay/Pauseには巻戻しがありません。

## 一人称PlayerとPlay

06はPlayerを配置済みです。Playで一人称操作を開始し、マウスで視点、WASD移動、Spaceジャンプ、Escのポーズ画面で「編集に戻る」を選ぶと開始前の状態へ戻ります。StageKit/Prefabs/Player_FirstPersonをほかのSceneへ配置しても使えます。PlayerのないSceneのPlay/Pauseは従来どおり時間切替です。[一人称Playerガイド](../../Documentation/FIRST_PERSON_PLAYER_GUIDE.md)を参照してください。
