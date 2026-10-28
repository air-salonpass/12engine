# ライティング・影・リアル寄りの材質を調整する

## 見本から始める

Debug版を起動し、Project → Samples → Mapsから次のSceneを開きます。作業中のSceneは先に保存してください。

- `04_Lighting_Materials.jscene`：左から陶器、銀、銅、粗い石の4球。すべてコードで生成し、外部モデル・画像は使いません。光沢の幅、色、金属反射、接地影を比べます。
- `05_Artist_Model_Preview.jscene`：既存のDamagedHelmetを使う差し替え練習用の見本。Normal Mapや金属材質のあるモデルを確認できます。地形・火・水には外部素材を追加していません。

05のモデルの著作権表示・条件は `resources/gltf/DamagedHelmet/README.md` に残しています。サンプルで使えることと、提出素材に採用することは区別します。提出時はアート担当のモデルへ差し替えます。

## プランナーが調整する順番

Hierarchyで `Directional Light` を選びます。InspectorのMap Toolsを閉じるか、下へスクロールすると照明設定が見えます。

1. `Direction`で太陽の向きを決めます。値は物体から光へ向かう方向です。Yを正にすると上から照らします。ゼロ方向の場合は既定の斜め上方向で描きます。
2. `Sun Color`と`Sun Intensity`で主光源を調整します。Intensityはまず3前後を目安にします。黄色や青へ寄せすぎる前に白に近い色で形を確認します。
3. `Sky Color`、`Ground Bounce Color`、`Ambient Intensity`で、影の中へ回り込む光を調整します。環境画像がなくても上下で違う色の照明が入ります。実際の周囲の形や反射を計算するGIではなく、空と地面を近似する方式です。Rendering SettingsのIBL Intensityは初期値0で、0より大きくしたときだけ任意の環境画像を一度読み込みます。
4. `Sun Shadows`で影の有無を比べ、`Shadow Strength`で濃さを決めます。
5. `Shadow Range (m)`はカメラの注視点付近を影で覆う範囲です。狭いステージなら20～32m程度から始めます。同じ2048pxでも範囲を広げるほど輪郭が粗くなります。
6. 影に縞が出る場合は`Shadow Bias`を少し上げます。上げすぎると影が物体から離れるため、0.0006～0.001前後から調整します。

続いてHierarchyの`Post Process Volume`を選びます。

- `Exposure`：画面全体の明るさ。まず1にし、光の強さを調整してから仕上げます。明るい部分はフィルム調のカーブで圧縮して表示します。
- `Ambient Occlusion`：床との接触や隅の陰影。SSAOという、画面の深度を調べる近似処理です。
- `AO Strength`：接地の強調量。0.5～1程度から調整します。
- `AO Radius (m)`：近くの面を探す範囲。0.3～0.8m程度を目安にします。大きすぎると輪郭に暗い縁が出やすくなります。

調整後はSave Sceneで保存します。照明とAOはScene全体の設定です。モデル単体のPrefabを保存する操作には含まれません。古いSceneに項目がなくても既定値で開けます。
Undo/RedoはComponentの状態を戻します。光の色・強さ・影・AOの編集も既存のInspectorと同じ履歴へ参加します。

## 材質をリアルに見せる条件

モデルはglTF／GLBのPBR材質を使います。Base Color、Metallic、Roughness、Normal、Occlusion、Emissiveを読みます。
金属は表面の色が反射色へ効き、粗さが低いほどハイライトが狭くなります。すべてを金属にしたり、粗さを0へ寄せたりすると材質の違いが分かりにくくなります。

材質球はglTF内の係数だけで違いを作っています。完成モデルではアート担当のTextureを同じ入力へ差し替えられます。外部の地面画像・炎画像・水画像を使う前提にはしていません。

接線が省略されたモデルではUVから接線を生成します。Normal Mapを読む方向を面ごとにそろえるためのデータです。非一様Scaleでは法線を逆転置行列で変換し、鏡像では接線の向きを補正します。カメラに対する表裏も通常描画とMesh Shaderでそろえています。

## 実装を読む順番

| ファイル | 担当 |
| --- | --- |
| `Components/DirectionalLightComponent.h` | 太陽・空・地面・影の保存したい値 |
| `Components/PostProcessComponent.h` | 露出・SSAOの状態。その他の保留項目が存在しても描画されるとは限らない |
| `DirectX12/NativePreview.cpp` の `render` | ComponentをGPU用の値へ組み立て、影→反射→通常描画→表示の順に呼ぶ |
| `DirectX12/SunShadowMap.h` | 深度TextureとDSV/SRVの生成、深度描画/読み取りのBarrier |
| `DirectX12/LightingFrame.h` | CPUから渡す照明定数。HLSLのb2と対応 |
| `DirectX12/LightingShader.h` | 外部画像なしの環境光、影の3×3深度比較。モデルと床の共通HLSL |
| `DirectX12/GltfRenderer.cpp` | 材質による照明、影用PSO、Alpha Cutoff、通常とMeshのBinding |
| `DirectX12/GltfModel.cpp` | UVから接線を生成し、頂点BufferとMeshlet用データへ入れる |
| `DirectX12/Shaders/GltfMeshMS.hlsl` | Mesh Shader側の行列・法線・接線・ワールド位置出力 |
| `DirectX12/SceneGeometryRenderer.cpp` | 単色床への照明、影、平面反射の合成 |
| `DirectX12/ScreenSpaceReflection.cpp` | SSAO、任意のSSR、手続き空、HDR色調。Scene/Gameで定数領域を分ける |
| `Tools/GenerateLightingSample.ps1` | 材質球と04/05のScene再生成。既存の編集結果を上書きするので別名保存して使う |

## 描画機能を拡張するときの手順

1. 保存したい値はComponentへ追加します。GPU ResourceやDescriptorはComponentに入れません。
2. `LightingFrame.h`と`LightingShader.h`のb2を同じ並びにします。型や行列の向きも合わせます。CPUとGPUのサイズが違うと色や影が壊れます。
3. `NativePreview::render`でフレームの値を確定し、`set_lighting`でフレーム用Upload Bufferへ一度書きます。
4. 新しい影の入力や定数を増やすなら、通常頂点・Mesh・床のRoot SignatureとBindingを一緒に更新します。現在は影Texture=t12、照明=b2、影Sampler=s2です。Meshlet用t8～t11とは重ねません。
5. Inspectorの編集UIとUndo登録、`save_scene`/`load_scene`を対で更新します。モデル単体Prefabへ必要な値ならPrefab側にも追加します。
6. 古いScene、新しい見本、保存して再読込み、Undo/Redo、Debug/Releaseを確認します。Mesh ShaderをON/OFFして結果が一致することも確認します。

影用深度は同じGraphics Queue上で書き込み、Barrier後に通常描画で読みます。次のフレームも同じQueueで順序を保ちます。破棄前はアプリ側でGPU完了を待ちます。CPUのatomicでGPU完了を代用しません。
影にもEntityごとの確定したAnimation Poseを渡します。影を描く処理でアニメーションの時刻は進めません。

## 現在の範囲と負荷

太陽の影は2048pxの1枚です。広大なマップ向けの段階的な影Map（CSM）、色付きの透過影、Point/Spot Lightの影は未実装です。BLEND材質は不透明な影を落とさず、MASK材質はAlpha Cutoffの穴を影にも反映します。
SSAOは画面内の面だけを使い、現状は照明成分を分けて持たないため最終HDR色へ控えめに合成する近似です。完全な間接光・GIではありません。

重いときは、影をOFF→AOをOFF→床反射をOFFの順に比較します。影はモデルを深度用に追加で描き、AOは画素ごとに12点を調べます。VSync中のFPSだけでは余力が分からないため、厳密な負荷評価にはGPU Passごとの計測が別途必要です。