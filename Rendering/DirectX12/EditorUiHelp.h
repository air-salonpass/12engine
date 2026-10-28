#pragma once

#ifdef USE_IMGUI
#include "../imgui/imgui.h"
#include <cstring>
#include <string>
#include <utility>

// Editorの表示名と説明だけを管理する。Component名・JSONキー・Asset名は翻訳しない。
// 新しい項目は元の英語ラベル、表示名、目的・単位・注意点を一行追加し、下のWidgetを使う。
namespace editor_ui
{
    struct help_entry { const char* key; const char* title; const char* description; };
    inline constexpr help_entry entries[]{
        {"Snap",u8"Snap",u8"移動・回転・拡縮とモデルの配置を指定した刻みへ合わせます。OFFでもCtrlを押している間は一時有効。右クリックで各操作の刻みを設定できます。"},
        {"Grid (m)",u8"Grid (m)",u8"移動と配置の格子間隔です。SnapがON、またはCtrlを押している間だけ使います。モデルを置くときの高さは地形に合わせて保ちます。"},
        {"Move Step (m)",u8"移動の刻み (m)",u8"移動と配置で使用する間隔です。Scene上部のGridと同じ値です。"},
        {"Rotate Step (deg)",u8"回転の刻み (度)",u8"Gizmoで回転するときの刻みです。SnapがON、またはCtrl中に使用します。既定は15度。"},
        {"Scale Step",u8"拡縮の刻み",u8"Gizmoで大きさを変えるときの刻みです。SnapがON、またはCtrl中に使用します。既定は0.1。"},
        {"Scene View",u8"Scene View",u8"Sceneだけの補助表示を設定します。GameやReleaseの描画へデバッグ線は入りません。"},
        {"Debug Lines",u8"Debug Lines（判定の線）",u8"編集時のSceneに有効なColliderの枠線を表示します。選択物は緑、検知範囲は黄、ほかは青。判定やゲーム動作自体は変更しません。球・カプセルは外接箱の表示です。"},
        {"Physics Monitor",u8"物理の接触状況",u8"物体の衝突と範囲への出入りを確認します。検知範囲は物体を押し戻しません。表示は最大16件です。"},
        {"Map Tools",u8"マップ配置ツール",u8"Projectで選んだモデルやPrefabをSceneへ配置します。配置先・間隔・向きを決めてから配置モードを有効にします。"},
        {"Placement Mode",u8"クリックで配置",u8"有効にしてSceneをクリックすると選択素材を配置します。Escで配置を終了。配置の取消しはCtrl+Zです。"},
        {"Placement Surface",u8"配置先",u8"水平面は指定した高さへ、当たり判定の表面はマウスが指す床や壁へ配置します。表面が見つからない場合は水平面を使います。"},
        {"Snap XZ To Grid",u8"縦横を格子に合わせる",u8"配置時のX/Z位置を格子へ吸着します。高さは変えないため、床や段差の表面にも置けます。"},
        {"Placement Grid",u8"配置間隔 (m)",u8"配置位置をそろえる格子の幅です。見本の細かい部品は0.5m、4mの床は4mが目安です。"},
        {"Plane Height",u8"配置面の高さ (m)",u8"水平面へ配置するときのY座標です。見本の中庭は0m、上段は2m。素材の底面原点をこの高さへ置きます。"},
        {"Placement Yaw",u8"配置時の向き (度)",u8"配置する素材のY軸回転です。当たり判定のBoxは回転しないため、形状の寸法も確認してください。"},
        {"+90",u8"+90度",u8"次に配置する素材の向きを90度回します。既に置いた物体の回転は変えません。"},
        {"Placement Scale",u8"配置時の倍率",u8"素材の縦横比を保って拡大・縮小します。Prefabの寸法をそのまま使う場合は1にします。"},
        {"Add Static Box Collider",u8"固定の箱型判定を自動追加",u8"モデル全体を囲む箱の当たり判定を作ります。Prefabの判定・物理設定を置き換えるため、PlayerやTrigger付きPrefabではOFFにしてください。門などの開口部も箱で塞がります。"},
        {"Gameplay Tag Component",u8"ゲーム上の役割",u8"名前とは別に、ゲーム処理が役割を判断するための文字列を持ちます。モデルを差し替えても役割を保てます。"},
        {"Add Gameplay Tag Component",u8"ゲーム用タグを追加",u8"選択物へ役割を表すタグを追加します。Checkpoint・Goal・HazardはPlayerの復帰・到達判定に使います。"},
        {"Tag",u8"役割タグ",u8"Player / Enemy / Checkpoint / Goal / Hazardなどを入力します。タグの値はコードと完全一致が必要です。Checkpoint・Goal・Hazardは検知範囲の判定も有効にしてください。"},
        {"Transform Component",u8"位置・向き・大きさ",u8"選択物の配置を設定します。数値の並びはX（横）・Y（高さ）・Z（奥行）です。"},
        {"Position",u8"位置 (m)",u8"X/Y/Zの配置位置です。見本の部品とPlayerは底面中央が原点です。ドラッグで調整し、Ctrl+Zで戻せます。"},
        {"Rotation",u8"回転 (度)",u8"X/Y/Z軸周りの回転です。Playerの開始方向にはY回転を使います。現状のBox Colliderへ回転は反映されません。"},
        {"Scale",u8"大きさの倍率",u8"X/Y/Z別の拡大率です。見本の1m箱では各値が実寸mになります。目の高さや段差上限は別の実寸設定なので、Playerを拡縮したら見直してください。"},
        {"Model Renderer Component",u8"モデルの描画",u8"表示に使うモデルを参照します。同じ素材は共有しますが、各物体の位置やアニメーション状態は独立しています。"},
        {"Visible",u8"表示する",u8"選択した描画対象の表示を切り替えます。モデルを隠しても通常は当たり判定が残るため、Colliderの有効状態も確認してください。確認用の反射床は表示と判定が連動します。"},
        {"Animation Component",u8"アニメーション",u8"この物体の再生状態を調整します。モデル内の動きであり、EntityのTransformや当たり判定が同じ動きになるとは限りません。"},
        {"State",u8"動作の状態",u8"アニメーション制御に登録した状態へ切り替えます。状態名はモデルやゲーム側の登録内容をそのまま表示します。"},
        {"Clip",u8"再生する動き",u8"モデルに含まれるアニメーションを選びます。切替時に再生位置を先頭へ戻します。"},
        {"Controller Enabled",u8"制御を有効にする",u8"この制御Componentの処理を有効にします。無効にすると、登録された動作の制御やキャラクター移動が行われません。"},
        {"Playing",u8"再生する",u8"選択物のアニメーション再生を切り替えます。Editor全体のPlay/Pauseとは別で、ゲームがポーズ中は時間も止まります。"},
        {"Loop",u8"繰り返す",u8"動きの終端に到達したら先頭へ戻して再生します。"},
        {"Speed",u8"再生速度",u8"1が通常速度、0で停止、2で2倍速です。同じモデルを使う別の物体には影響しません。"},
        {"Time",u8"再生位置 (秒)",u8"アニメーション内の時刻を指定します。動きの確認には再生を止めてから調整します。"},
        {"Add Map Object Component",u8"マップ部品として設定",u8"選択物へ配置分類と格子設定を追加します。モデルや当たり判定は変更しません。"},
        {"Map Object Component",u8"マップ部品の設定",u8"配置済み部品の格子と分類です。上のマップ配置ツールは次に置く素材、こちらは選択済みの物体を設定します。"},
        {"Static Map Object",u8"動かないマップ部品",u8"分類用の設定です。これだけで物理的に固定されるわけではありません。固定する場合は物理の種類も「固定」にします。一括描画による最適化は未対応です。"},
        {"Always Snap To Grid",u8"移動を常に格子へ合わせる",u8"Gizmoで位置を動かすとき、この部品の格子間隔へ吸着させます。"},
        {"Grid Size",u8"この部品の格子間隔 (m)",u8"配置済み部品をGizmoで移動するときの吸着間隔です。次に配置する素材の間隔とは別です。"},
        {"Map Layer",u8"マップの分類番号",u8"部品を整理するための分類です。衝突する相手を制限する番号ではありません。物理のグループはCollider側で設定します。"},
        {"Add Collider Component",u8"当たり判定を追加",u8"衝突や検知に使う形状を追加します。形状の中心と寸法をモデルに合わせて調整してください。"},
        {"Collider Component",u8"当たり判定",u8"モデルの見た目とは独立した衝突形状です。Scene上の枠線で大きさを確認できます。"},
        {"Collider Enabled",u8"当たり判定を有効にする",u8"無効にすると衝突・範囲検知・Sceneクリック選択の対象から外れます。見た目の表示は別設定です。"},
        {"Shape",u8"判定の形",u8"箱・球・カプセルから選びます。箱は軸に沿う形状で回転しません。カプセルの衝突は現在、縦長の外接箱による近似です。"},
        {"Collider Center",u8"判定中心のずれ",u8"モデル原点からの相対位置です。1m箱の底面が原点ならY=0.5が中心です。Transformの拡大率も掛かります。"},
        {"Half Extents",u8"中心から端までの長さ",u8"箱の半分の寸法をX/Y/Zで指定します。0.5なら全幅は1です。さらにTransformの拡大率が掛かります。"},
        {"Radius",u8"半径",u8"球またはカプセルの半径です。Transformの拡大率も反映されます。"},
        {"Capsule Height",u8"カプセル全体の高さ",u8"半球部分を含む全高です。直径より小さくはできません。現在の衝突は外接箱による近似です。"},
        {"Collision Layer",u8"衝突グループ番号",u8"この物体が属する0～31のグループです。相手の衝突対象にこの番号のビットがある場合に判定します。"},
        {"Collision Mask",u8"衝突対象 (16進数)",u8"衝突を許可する相手グループをビットで指定します。FFFFFFFFは全グループ、00000001はグループ0のみ。双方の設定が許可している必要があります。"},
        {"Is Trigger",u8"押し戻さず範囲を検知",u8"ONでは物体を押し戻さず、入った・触れている・出たというイベントだけを出します。チェックポイントやゴールの範囲に使います。"},
        {"Add RigidBody Component",u8"物理で動く設定を追加",u8"重力・速度・質量などを追加します。床や壁は種類を「固定」にしてください。"},
        {"Add Character Controller",u8"キャラクター移動を追加",u8"接地・重力・ジャンプ・小段差乗越えを使う移動設定を追加します。操作するPlayerには、さらにPlayer Controllerが必要です。"},
        {"RigidBody Component",u8"物理による移動",u8"固定物・速度で動かす物体・重力で動く物体を設定します。キャラクター移動とは用途が異なります。"},
        {"RigidBody Enabled",u8"物理移動を有効にする",u8"この物理Componentによる移動を切り替えます。当たり判定自体の有効状態は別です。"},
        {"Body Type",u8"物理の種類",u8"固定は床や壁、速度指定はゲーム側から動かす物体、重力で移動は落下する箱などに使います。回転運動は未対応です。"},
        {"Mass",u8"質量",u8"重力で動く物体同士の押し合いに影響します。大きい物体ほど同じ衝突で動きにくくなります。"},
        {"Velocity",u8"移動速度 (m/秒)",u8"X/Y/Z方向の速度です。重力で移動する物体はY速度が毎物理ステップ変化します。"},
        {"Gravity Scale",u8"重力の倍率",u8"1が通常、0で重力なしです。重力で移動する物体へ適用します。"},
        {"Restitution",u8"跳ね返りやすさ",u8"0で跳ね返りを抑え、1で強く跳ね返ります。接触する双方の小さい値を使います。"},
        {"Friction",u8"滑りにくさ",u8"衝突時の接線速度を抑えます。大きいほど滑りにくくなりますが、完全な静止摩擦を再現する処理ではありません。"},
        {"Freeze X",u8"X方向の移動を固定",u8"物理によるX方向の移動速度を0にします。Editorの位置編集を禁止する設定ではありません。"},
        {"Freeze Y",u8"Y方向の移動を固定",u8"物理による高さ方向の移動速度を0にします。"},
        {"Freeze Z",u8"Z方向の移動を固定",u8"物理による奥行方向の移動速度を0にします。"},
        {"Character Controller Component",u8"キャラクターの移動・接地",u8"ゲームSystemから要求した速度で移動し、重力と衝突を解決します。操作入力はPlayer Controllerが渡します。"},
        {"Desired Velocity",u8"要求する移動速度",u8"ゲームSystemが渡すX/Z速度です。Play中はPlayer入力で上書きされます。上下移動は重力とジャンプで計算します。"},
        {"Gravity",u8"落下の加速度",u8"キャラクターが1秒ごとに落下速度を増す量です。大きいほど速く落ち、同じジャンプ速度で到達する高さは低くなります。"},
        {"Jump Speed",u8"ジャンプの初速",u8"Spaceを押した瞬間の上向き速度です。接地中だけジャンプできます。"},
        {"Slope Limit",u8"傾斜の上限 (未対応)",u8"保存用の設定値です。現在は歩ける傾斜面の判定に使っていません。Boxで作った水平な階段には段差乗越えを使います。"},
        {"Step Height",u8"登れる段差の高さ (m)",u8"接地中に自動で登れる静的な箱の高さです。0で無効。見本は一段0.4mなので0.45mに設定しています。高い壁・低い天井・検知範囲・動く箱は登りません。"},
        {"Camera Component",u8"カメラ",u8"編集用の注視点と見え方です。一人称Playerの目の高さ・画角はPlayer Controller側で設定します。"},
        {"Target",u8"注視する位置 (m)",u8"編集カメラが見つめるX/Y/Z座標です。一人称プレイ中はPlayerの目の位置に更新されます。"},
        {"Distance",u8"注視点からの距離 (m)",u8"編集カメラを注視点からどれだけ離すかを設定します。一人称では使用しません。"},
        {"FOV",u8"画角 (度)",u8"大きいほど広い範囲が映り、小さいほど拡大して見えます。Playerの画角はPlayer Controller側で指定します。"},
        {"Yaw",u8"左右の向き (度)",u8"編集カメラの水平回転です。Playerの開始方向はPlayerのTransformのY回転から決まります。"},
        {"Pitch",u8"上下の向き (度)",u8"編集カメラの見下ろし・見上げ角度です。"},
        {"Add Playtest Controller",u8"Player操作を追加",u8"キャラクターへWASD移動・マウス視点・ジャンプ・復帰を追加します。有効なColliderとCharacter Controllerをそろえ、Sceneに操作用Playerを一つ置いてPlayします。"},
        {"Player Controller",u8"Playerの操作・視点",u8"配置済みPlayerの操作設定です。Playで開始、Shiftでダッシュ、Spaceでジャンプ、Rで復帰、Escでポーズします。停止はポーズ画面の「編集に戻る」です。"},
        {"First Person",u8"一人称視点",u8"ONでPlayerの目から見ます。自分のモデルは非表示になり、影には残ります。OFFでは後ろから追従するカメラを使います。"},
        {"Eye Height (m)",u8"目の高さ (m)",u8"Playerの足元からの実寸です。見本は1.55m。モデルの拡大率に自動連動しないため、Colliderの全高より低く設定してください。"},
        {"Mouse Sensitivity",u8"マウスの感度",u8"マウス移動量に対する視点の回転量です。小さくするとゆっくり回ります。変更は次回のPlay開始時から反映します。"},
        {"Player FOV",u8"Playerの画角 (度)",u8"一人称プレイの縦方向の画角です。見本は75度。大きくすると周囲が広く見えます。次回のPlayから反映します。"},
        {"Walk Speed (m/s)",u8"歩く速さ (m/秒)",u8"WASD移動の速さです。斜め入力でも同じ速さになります。"},
        {"Sprint Multiplier",u8"ダッシュの倍率",u8"Shiftを押している間の速度倍率です。歩行5m/秒・倍率1.8なら9m/秒です。"},
        {"Fall Respawn Y",u8"落下で復帰する高さ (m)",u8"PlayerのY座標がこの値を下回ると、到達済みのCheckpointへ戻します。Checkpointに未到達ならPlayの開始地点へ戻ります。"},
        {"Follow Camera Distance",u8"追従カメラの距離 (m)",u8"一人称がOFFの場合だけ使う希望距離です。壁がある場合は手前へカメラを寄せます。"},
        {"Reflection Floor Component",u8"確認用の床と反射",u8"描画確認用の広い床です。06のマップは独自の床を使うため、この床は非表示にしています。"},
        {"Reflection Enabled",u8"床の反射を有効にする",u8"確認用床の映り込みを有効にします。モデルの材質自体の金属反射とは別です。"},
        {"Stable Planar Reflection",u8"平面鏡の反射を使う",u8"ONでは床の反対側から別にモデルを描きます。OFFでは画面内の情報を探すSSRを使うため、画面外の物体が映らないことがあります。"},
        {"Floor Height",u8"確認用床の高さ (m)",u8"広い確認用床のY座標です。マップ部品の床の高さは、その物体のTransformで変更します。"},
        {"Floor Color",u8"確認用床の色",u8"床そのものの色です。反射の強さとは独立しています。"},
        {"Strength",u8"床の反射の強さ",u8"確認用床へ合成する映り込みの強さです。反射が有効な場合に使います。"},
        {"Ray Distance",u8"反射を探す距離",u8"SSRが画面内で反射先を探す最大距離です。大きくしても画面外の物体は取得できません。"},
        {"Thickness",u8"反射判定の厚み",u8"SSRの深度判定で許容する厚みです。薄すぎると欠け、大きすぎると誤った反射が出る場合があります。"},
        {"Steps",u8"反射の探索回数",u8"SSRの探索を細かくする回数です。増やすと探索が細かくなりますが、描画負荷も上がります。"},
        {"Directional Light Component",u8"太陽の光・環境光",u8"太陽からの主な光と、空・地面から回り込む光を調整します。外部の環境画像なしでも照明を作れます。"},
        {"Enabled",u8"有効にする",u8"この光源を有効にします。太陽を無効にしても環境光の設定は独立して残ります。"},
        {"Direction",u8"太陽がある方向",u8"物体から見た太陽の方向です。Yを正にすると上から照らします。位置ではなく方向なので、成分の比率で向きが決まります。"},
        {"Sun Color",u8"太陽の色",u8"直接当たる光の色です。暖かい夕方や青白い昼などを作るときに調整します。"},
        {"Sun Intensity",u8"太陽の明るさ",u8"直接光の強さです。全体が白く飛ぶ場合は露出も確認してください。"},
        {"Sky Color",u8"空から回り込む光の色",u8"上向きの面へ届く環境光の色です。外部画像を使わず、空の光を近似します。"},
        {"Ground Bounce Color",u8"地面から回り込む光の色",u8"下側から回り込む環境光の色です。実際の間接光を自動計算した値ではなく、色で近似します。"},
        {"Ambient Intensity",u8"回り込む光の明るさ",u8"太陽が直接当たらない面の明るさを調整します。高くしすぎると陰影が平らに見えます。"},
        {"Sun Shadows",u8"太陽の影を描く",u8"モデルが太陽を遮る落ち影を描画します。影の範囲と濃さも調整できます。"},
        {"Shadow Strength",u8"影の濃さ",u8"太陽を遮った部分をどれだけ暗くするかを調整します。0で影の効果をなくします。"},
        {"Shadow Range (m)",u8"影を描く範囲 (m)",u8"カメラの注視点周辺で影を記録する範囲です。狭いほど細かくなりますが、範囲外では影がなくなります。ステージ全体を覆う値にしてください。"},
        {"Shadow Bias",u8"影の誤判定を防ぐずらし量",u8"表面が自分自身の影で縞になる場合は少し増やします。大きすぎると物体から影が浮いて見えます。"},
        {"Post Process Component",u8"画面全体の明るさ・接地感",u8"描画した画像へ露出と画面内の陰影を加えます。モデルや光源の設定とは別に全体の見え方を調整します。"},
        {"Exposure",u8"露出・画面の明るさ",u8"画面全体の明るさの倍率です。光源の強さを変える前に、全体が暗すぎないか確認してください。"},
        {"Ambient Occlusion",u8"接地部分や隙間を暗くする",u8"画面内の深度を使い、床との接点や物体の隙間へ陰影を加えます。画面外の形状や本当の間接光は計算しません。"},
        {"AO Strength",u8"隙間の陰影の強さ",u8"接地感を補う陰影の濃さです。強すぎると黒い縁が目立ちます。"},
        {"AO Radius (m)",u8"隙間の陰影を調べる範囲 (m)",u8"周辺の形状を調べる距離です。小物なら小さめ、大きな建物なら大きめにします。"},
        {"Rendering Settings Component",u8"描画と動作速度",u8"画面同期・FPS上限・環境画像による光などを設定します。"},
        {"VSync",u8"画面更新に同期する",u8"モニターの更新に合わせて描画します。ONのとき、前面でのFPS上限は画面同期側が決めます。"},
        {"FPS Limit (0 = Unlimited)",u8"FPS上限 (0で無制限)",u8"画面同期がOFFのときの上限です。高くすると操作を細かく反映できますが、CPU/GPUの使用量が増えます。"},
        {"Background FPS",u8"別アプリ操作中のFPS上限",u8"エンジンが前面にないときの描画回数を抑えます。Player操作中にフォーカスを失うと自動ポーズします。"},
        {"Mesh Shaders",u8"Mesh Shaderを使う",u8"対応GPUでモデルを小さい塊に分けて描画します。非対応の機能や材質は通常の描画へ戻ります。"},
        {"IBL Intensity",u8"環境画像の光の強さ",u8"確認用の外部環境画像による光を足します。0なら画像を読みません。外部素材を使わない見本では0のまま、太陽と手続き環境光を使います。"},
        {"Up",u8"上のフォルダー",u8"Projectで一つ上のフォルダーへ移動します。"},
        {"Samples",u8"サンプル",u8"機能見本へ移動します。Mapsはマップ、Prefabsは再利用部品、StageKitは石造りステージ用の部品です。"},
        {"Undo",u8"元に戻す",u8"直前の配置や設定変更を取り消します。Ctrl+Zでも操作できます。"},
        {"Redo",u8"やり直す",u8"取り消した編集をやり直します。Ctrl+Yでも操作できます。"},
        {"Play",u8"プレイ",u8"配置済みPlayerから操作を始めます。PlayerはSceneに一つ必要です。PlayerのないSceneでは時間を進めます。"},
        {"Pause",u8"一時停止",u8"Player操作中はポーズ画面を開きます。Escでも開けます。PlayerのないSceneでは時間を止めます。"},
        {"Resume",u8"再開",u8"ポーズした位置からプレイを続けます。"},
        {u8"ファイル",u8"ファイル",u8"シーンの新規作成・保存形式・モデルやPrefabの読込みをまとめています。"},
        {u8"編集",u8"編集",u8"選択物の複製、カメラを寄せる操作、確認用床への配置をまとめています。"},
        {u8"表示・詳細",u8"表示・詳細",u8"Gizmo・全画面・コマ送り・歩行テストなど、普段は使わない補助操作です。"},
        {u8"開く",u8"開く",u8"保存済みシーンへ切り替えます。今のシーンを残す場合は先に保存してください。サンプルはProjectの「サンプル」からも開けます。"},
        {u8"保存",u8"保存",u8"現在のシーンを保存します。初回は保存先を指定します。別のシーン名で残す場合はファイルの「名前を付けて保存」を使います。"},
        {u8"移動 W",u8"移動 W",u8"Gizmoを位置の移動に切り替えます。Player操作中のWは前進です。"},
        {u8"回転 E",u8"回転 E",u8"Gizmoを回転に切り替えます。Boxの当たり判定へ回転は反映されないため、判定寸法も確認してください。"},
        {u8"拡縮 R",u8"拡縮 R",u8"Gizmoを大きさの変更に切り替えます。Player操作中のRは復帰です。"},
        {u8"新しいシーン",u8"新しいシーン",u8"現在の配置を置き換えて空のシーンを作ります。残したい変更は先に保存してください。"},
        {u8"シーンを開く",u8"シーンを開く",u8"保存済みの配置と設定を読み込み、現在のシーンを置き換えます。"},
        {u8"名前を付けて保存",u8"名前を付けて保存",u8"現在のシーンを別の名前で保存します。サンプルを編集するときは先に別名で保存すると元を残せます。"},
        {u8"モデルを追加",u8"モデルを追加",u8"glTF/GLBを読み込んでシーンへ追加します。初回の読込みは非同期です。"},
        {u8"Prefabを開く",u8"Prefabを開く",u8"保存した単体Entityの設定を現在のシーンへ追加します。シーン全体は置き換えません。"},
        {u8"選択物をPrefabとして保存",u8"選択物をPrefabとして保存",u8"選択した一つの物体とComponent設定を部品として保存します。複数物体をまとめたPrefabには未対応です。"},
        {u8"選択物を複製",u8"選択物を複製",u8"選択物のComponentをコピーして少し横へずらします。Playerを二つにするとプレイを開始できないため、一つにしてください。"},
        {u8"選択物へカメラを寄せる",u8"選択物へカメラを寄せる",u8"選択したモデルを見やすい位置へ編集カメラを移動します。物体自体は動かしません。"},
        {u8"選択物を確認用床へ置く",u8"選択物を確認用床へ置く",u8"モデルの底を広い確認用床の高さへ合わせます。06の実際の地形へ吸着する操作ではありません。"},
        {u8"Gizmoを表示",u8"Gizmoを表示",u8"Sceneで選択物を動かす矢印・回転環・拡縮ハンドルを表示します。"},
        {u8"全画面",u8"全画面",u8"ウィンドウと全画面を切り替えます。F11でも切り替えられます。"},
        {u8"1フレーム進める",u8"1フレーム進める",u8"通常の時間停止中に、物理を1/60秒だけ進めます。Playerのポーズ中はこのメニューを使いません。"},
        {u8"歩行テストを開始",u8"歩行テストを開始",u8"配置済みPlayerを操作します。通常は上部の「プレイ」から開始できます。"},
        {u8"再開",u8"再開",u8"ポーズした場所からプレイを続け、マウスを視点操作へ戻します。"},
        {u8"編集に戻る",u8"編集に戻る",u8"プレイを終了して、配置・物理状態・カメラを開始前へ戻します。プレイ中に動いた位置を保存する操作ではありません。"},
    };

    inline const help_entry* find(const char* key)
    {
        for (const auto& entry:entries) if (std::strcmp(entry.key,key)==0) return &entry;
        return nullptr;
    }
    inline std::string label(const char* key)
    {
        const auto* entry=find(key);
        // ###以降へ固定キーを置く。今後表示名を直しても、編集項目のIDは変えない。
        return entry ? std::string(entry->title)+"###"+key : key;
    }
    inline void explain(const char* text)
    {
        if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) return;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,1.0f);
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize()*32.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos(); ImGui::EndTooltip(); ImGui::PopStyleVar();
    }
    inline void help(const char* key) { if (const auto* entry=find(key)) explain(entry->description); }

    // Tooltipには編集Widgetを置かない。呼出し元のIsItemActivated/Undo判定は元の項目を参照できる。
    #define EDITOR_HELP_WIDGET(Name) \
    template<class... Args> inline auto Name(const char* key,Args&&... args) { \
        const std::string display=label(key); \
        auto result=ImGui::Name(display.c_str(),std::forward<Args>(args)...); help(key); return result; }
    EDITOR_HELP_WIDGET(Button)
    EDITOR_HELP_WIDGET(MenuItem)
    EDITOR_HELP_WIDGET(Checkbox)
    EDITOR_HELP_WIDGET(CollapsingHeader)
    #undef EDITOR_HELP_WIDGET

    // 日本語の長い項目名が狭いInspectorから切れないよう、名前の下に入力欄を置く。
    // SetNextItemWidthは一回だけに適用されるので、Comboを開いてもWindow間で幅のStackが混ざらない。
    inline std::string field(const char* key)
    {
        const auto* entry=find(key);
        ImGui::TextWrapped("%s",entry ? entry->title : key); help(key);
        ImGui::SetNextItemWidth(-1.0f);
        return std::string("###")+key;
    }
    #define EDITOR_HELP_FIELD(Name) \
    template<class... Args> inline auto Name(const char* key,Args&&... args) { \
        const std::string display=field(key); \
        auto result=ImGui::Name(display.c_str(),std::forward<Args>(args)...); help(key); return result; }
    EDITOR_HELP_FIELD(DragFloat)
    EDITOR_HELP_FIELD(DragFloat3)
    EDITOR_HELP_FIELD(SliderFloat)
    EDITOR_HELP_FIELD(SliderInt)
    EDITOR_HELP_FIELD(InputInt)
    EDITOR_HELP_FIELD(InputText)
    EDITOR_HELP_FIELD(InputScalar)
    EDITOR_HELP_FIELD(Combo)
    EDITOR_HELP_FIELD(BeginCombo)
    EDITOR_HELP_FIELD(ColorEdit3)
    #undef EDITOR_HELP_FIELD

    inline void TextUnformatted(const char* text)
    {
        const auto* entry=find(text); ImGui::TextUnformatted(entry ? entry->title : text); help(text);
    }
}
#endif
