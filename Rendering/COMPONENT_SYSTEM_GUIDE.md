# Jisaku Engine Component / System開発ガイド

## 目的

Jisaku Engineでは、Scene上の機能を継承中心のGameObjectクラスへ集約せず、
Entityへ小さなComponentを追加し、Systemが必要なComponentだけを処理する方式で実装します。
今後追加するゲーム機能も、原則としてこのECSへ追加してください。

Direct3D Device、SwapChain、Command Contextなどの描画基盤はECSへ入れません。
ECSが管理するのは、保存・複製・編集したいSceneとゲームの状態です。

## 現在のフォルダ構成

参照プロジェクトと同じ考え方で、Component、System、Registryを別ファイルへ分離しています。

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
   ├─ AnimationSystem.h / .cpp
   ├─ PostProcessSystem.h / .cpp
   └─ 機能ごとのSystem.h / .cpp
```

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

## 現在のSystem順序

1. 非同期Asset読込み完了処理
2. `PostProcessSystem`
3. `AnimationSystem`
4. Editor入力とInspector編集
5. Camera更新
6. Reflection Floor描画
7. Model Renderer描画
8. Post Process描画
9. GizmoとEditor UI描画

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

Reflection Floorは標準で平面反射を使います。同じAnimation Poseを床面対称に別Render Targetへ描くため、SSRのように画面外で鏡像が途切れません。`Stable Planar Reflection`をOFFにするとShader III Unit08方式のSSRへ切り替わり、画面内情報だけを使う挙動を確認できます。

## 次に追加する場合の推奨Component

- `collider_component`と`rigid_body_component`
- `fighter_controller_component`
- `hit_box_component`と`hurt_box_component`
- `audio_source_component`
- `script_component`
- `parent_component`と`children_component`
- `prefab_component`
- `camera_follow_component`

格闘ゲーム固有機能も、入力、移動、攻撃判定、体力、演出を別Componentへ分けると、
キャラクターごとの差し替えとInspector調整がしやすくなります。

## PrefabとUndo／Redo

Prefabは`.jprefab`として保存・配置できます。Project / AssetsでPrefabをダブルクリックして再配置できます。
Component Storageは型ごとに自動登録され、Entityの複製・削除とEditorのUndo／Redo Snapshotへ自動的に参加します。
Undo／Redoは最大64履歴で、`Ctrl+Z`／`Ctrl+Y`またはツールバーから実行できます。

ただしJSONのScene／Prefab保存は、外部ファイルとの互換性を保つため明示的なSerializerです。新しいComponentを永続化するときは、保存と読込みの両方を同時に追加してください。
