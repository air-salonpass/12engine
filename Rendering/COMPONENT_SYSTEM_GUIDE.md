# Jisaku Engine Component / System開発ガイド

## 目的

Jisaku Engineでは、Scene上の機能を継承中心のGameObjectクラスへ集約せず、
Entityへ小さなComponentを追加し、Systemが必要なComponentだけを処理する方式で実装します。
今後追加するゲーム機能も、原則としてこのECSへ追加してください。

Direct3D Device、SwapChain、Command Contextなどの描画基盤はECSへ入れません。
ECSが管理するのは、保存・複製・編集したいSceneとゲームの状態です。

## 現在のフォルダ構成

状態を持つComponent、処理を行うSystem、データを管理するRegistryを別ファイルへ分離しています。

```text
Rendering/
├─ Scene/
│  ├─ Entity.h
│  └─ SceneRegistry.h
├─ Components/
│  ├─ TransformComponent.h
│  ├─ ModelRendererComponent.h
│  ├─ AnimationComponent.h
│  └─ 機能ごとのComponent.h
└─ Systems/
   ├─ SceneSystem.h
   ├─ SceneSystemManager.h
   ├─ PostProcessSystem.h / .cpp
   └─ 機能ごとのSystem.h / .cpp
```

描画資源を使うAnimation Systemは`DirectX12/AnimationSystem12.h / .cpp`にあります。
ゲームルールだけで完結するSystemは`Systems/`、D3D12のBuffer更新まで責任を持つSystemは
`DirectX12/`へ置き、どちらの場合もComponentに状態、Systemに処理を分離してください。

Componentは状態データだけ、Systemは更新処理だけを担当します。
`SceneRegistry.h`はEntityとComponent Storage、`SceneSystemManager.h`はSystemの登録順と呼び出しだけを管理します。

| Component | 用途 |
| --- | --- |
| `name_component` | Hierarchyへ表示する名前 |
| `transform_component` | 位置、回転、一様スケール |
| `model_renderer_component` | 共有モデルの参照先と表示状態 |
| `animation_component` | クリップ、時間、再生、ループ、姿勢 |
| `camera_component` | 注視点、距離、FOV、回転、操作感度 |
| `directional_light_component` | 平行光源の向きと有効状態 |
| `reflection_floor_component` | 反射床とSSR設定 |
| `post_process_component` | Bloom、FXAA、DOFなどの画面効果 |
| `rendering_settings_component` | VSync、IBL、Mesh Shaderなど |
| `animation_controller_component` | Animation State、Clip、速度、Loop |
| `prefab_component` | 配置元Prefabのパス |
| `gameplay_tag_component` | Player、Enemy、Goalなどゲーム用途の識別子 |
| `map_object_component` | Static分類、Map Layer、配置グリッド幅 |
| `collider_component` | Box／Sphere／Capsule、Layer、Mask、Trigger |
| `rigid_body_component` | Static／Kinematic／Dynamic、質量、速度、重力、摩擦、反発 |
| `character_controller_component` | キャラクター移動要求、接地、重力、ジャンプ設定 |

`scene_registry`はEntity IDとComponentの保存領域を管理します。
描画側はModel Rendererを持つEntityを列挙し、AnimationやTransformがあれば組み合わせて処理します。

複数Componentを必要とするSystemは、次のように対象Entityだけを列挙できます。

```cpp
registry.for_each<transform_component, model_renderer_component>(
    [](scene_entity entity, transform_component& transform,
       model_renderer_component& renderer)
    {
        // TransformとModel Rendererの両方を持つEntityだけがここへ来る。
    });
```

## 新機能を追加する手順

1. `Components/機能名Component.h`へデータだけを持つComponentを追加する。
2. `scene.add<機能名_component>(entity)`でEntityへ追加する。型別Storageは初回追加時に自動生成される。
3. `Systems/機能名System.h/.cpp`へ処理を作る。
4. Native D3D12側の更新順へSystemを追加する。
5. InspectorへComponent単位の編集UIを追加する。
6. Sceneの複製・保存・読込み対象へComponentを追加する。
7. Scene保存対象にしたい値はグローバル変数ではなくComponentへ置く。

例として、体力機能は次のようにデータと処理を分離します。

```cpp
struct health_component
{
    float current = 100.0f;
    float maximum = 100.0f;
};

// Damage SystemはHealthを持つEntityだけを処理する。
void apply_damage(scene_registry& scene, float damage)
{
    scene.for_each<health_component>([damage](scene_entity, health_component& health)
    {
        health.current = (std::max)(0.0f, health.current - damage);
    });
}
```

## Component設計ルール

- Componentは状態データを中心にし、大きな処理を持たせない。
- Systemは必要なComponentがないEntityを安全に無視する。
- Entity同士を生ポインターで結ばず、必要ならEntity IDを保存する。
- モデルやテクスチャなどの重いAssetは共有し、ComponentにはAsset IDを保存する。
- エディタ専用状態とゲームSceneの状態を混在させない。
- 新しい巨大なManagerや多段継承GameObjectを作らない。
- ComponentはRegistryの複製、削除、Undo／Redo Snapshotへ自動的に含まれる。
- Scene／PrefabへのJSON保存とInspector表示はComponentごとに追加する。
- System間で処理順が必要な場合は、依存順を明示してフレーム処理へ登録する。

## 現在の更新と描画順序

実際の呼出し元は `DirectX12/NativePreview.cpp` です。新しいSystemはファイルを作るだけでは動かず、`update` から呼びます。

1. `update` の `poll_model_load` で非同期読込み結果を回収する。
2. リサイズ要求を反映し、Pause／Stepを考慮した `scene_delta` を求める。
3. Animationを更新する。
4. 反射床の高さを床ColliderのTransformへ反映する。
5. Physicsを更新する。移動要求はこの前、衝突イベントを読む処理はこの後へ追加する。
6. `render` でカメラ行列を計算し、平面反射、床、モデル、画面効果を描く。
7. DebugではInspectorやGizmoなどの編集UIを処理する。

ゲーム機能とモデル描画の追加手順は [実装案内](Documentation/IMPLEMENTATION_MAP.md) を参照してください。

## ゲームSceneの作成と保存

上部ツールバーからSceneを管理できます。

- `New Scene`：空のSceneを作成
- `Open Scene`：`.jscene`ファイルを開く
- `Save Scene`：現在のSceneを上書き保存
- `Save As`：名前を付けて保存
- `Ctrl+S`：上書き保存
- `Ctrl+Shift+S`：名前を付けて保存

Project / AssetsでglTFまたはGLBをダブルクリックすると、現在のSceneへModel Entityとして追加されます。
同じAssetを複数回配置した場合、GPUモデルは共有され、TransformやAnimation状態だけがEntityごとに分かれます。
ステージ、キャラクター、障害物はすべてModel Entityとして配置し、Hierarchyで選択してGizmoで移動できます。

`.jscene`はJSON形式で、次の情報を保存します。

- Entity名
- Transform
- Model RendererのAssetパスと表示状態
- Animation状態
- Animation ControllerのState
- Camera
- Directional Light
- Reflection FloorとSSR
- Post Process
- Rendering Settings

Scene読込み時は保存されたAssetパスから必要なモデルを再読込みします。
現在は一つのSceneで最大8種類の異なるモデルAssetを使用できます。同じAssetのEntity複製数にはこの制限はありません。

Reflection Floorは標準で平面反射を使います。同じAnimation Poseを床面対称に別Render Targetへ描くため、SSRのように画面外で鏡像が途切れません。`Stable Planar Reflection`をOFFにすると画面内の色と深度を利用するSSRへ切り替わり、画面内情報だけを使う挙動を確認できます。

## 次に追加する場合の推奨Component

- 既存のCollider／RigidBodyを利用するゲーム固有Component
- `fighter_controller_component`
- `hit_box_component`と`hurt_box_component`
- `audio_source_component`
- `script_component`
- `parent_component`と`children_component`
- 複数Entity Prefabの親子関係・Override管理用Component
- `camera_follow_component`

格闘ゲーム固有機能も、入力、移動、攻撃判定、体力、演出を別Componentへ分けると、
キャラクターごとの差し替えとInspector調整がしやすくなります。

## PrefabとUndo／Redo

Prefabは`.jprefab`として保存・配置できます。Project / AssetsでPrefabをダブルクリックして再配置できます。
Component Storageは型ごとに自動登録され、Entityの複製・削除とEditorのUndo／Redo Snapshotへ自動的に参加します。
Undo／Redoは最大64履歴で、`Ctrl+Z`／`Ctrl+Y`またはツールバーから実行できます。

ただしJSONのScene／Prefab保存は、外部ファイルとの互換性を保つため明示的なSerializerです。新しいComponentを永続化するときは、保存と読込みの両方を同時に追加してください。
