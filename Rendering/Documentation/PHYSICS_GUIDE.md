# Physics・当たり判定 実装ガイド

## Editorで物理オブジェクトを作る

1. HierarchyでモデルEntityを選ぶ。
2. Inspectorの`Add Collider Component`を押す。
3. Box、Sphere、Capsuleから形状を選び、Centerと大きさを調整する。
4. 動かす物体なら`Add RigidBody Component`、プレイヤーなら`Add Character Controller`を押す。
5. SceneまたはPrefabを保存する。物理Componentも同じJSONへ保存される。

反射床にはStatic Box Colliderが自動的に追加されます。Dynamic RigidBodyを付けたモデルはPlay中に重力で落下し、床や他のColliderと衝突します。

## Componentの役割

- `collider_component`：形状、Layer、Mask、Triggerを保持する。
- `rigid_body_component`：Body Type、質量、速度、重力、反発、摩擦を保持する。
- `character_controller_component`：キャラクターの希望速度、接地状態、重力、ジャンプ設定を保持する。
- `physics_system`：固定60 Hzで積分、衝突検出、押し戻し、Impulse解決を行う。

ComponentからPhysics Systemを直接呼ばないでください。ゲーム側Systemは速度や要求値をComponentへ書き、Physics SystemがTransformへ結果を反映します。

## Body Type

- Static：ステージ、壁、動かない障害物。速度を持たず、他の物体から押されない。
- Kinematic：動く床や扉。ゲームコードで速度を設定するが、Dynamic物体から押されない。
- Dynamic：重力と衝突Impulseで動く箱、破片、アイテム。

## LayerとMask

Layerは0～31です。Collision Maskは、衝突したい相手Layerのbitを1にします。両方のMaskが相手Layerを許可した場合だけ衝突します。

例：PlayerをLayer 1、EnemyをLayer 2として、PlayerのMaskへ`1 << 2`、EnemyのMaskへ`1 << 1`を含めます。

Triggerを有効にすると押し戻しは行わず、接触イベントだけが記録されます。攻撃判定、取得アイテム、ステージ遷移範囲に利用できます。

## Collision / Triggerイベント

`physics_system::events()`は固定物理更新の直後に、そのフレームの接触変化を返します。`phase`は`enter`、`stay`、`exit`の3種類で、`contact.trigger`がtrueならTriggerです。

```cpp
for (const physics_event& event : physics.events())
{
    if (event.phase != physics_event_phase::enter) continue;

    // Entity名ではなくGameplay Tagでゲーム上の役割を判断する。
    const auto* tag_a = scene.gameplay_tag(event.contact.first);
    const auto* tag_b = scene.gameplay_tag(event.contact.second);
    if (event.contact.trigger && tag_a && tag_b &&
        ((tag_a->value == "Player" && tag_b->value == "Goal") ||
         (tag_a->value == "Goal" && tag_b->value == "Player")))
    {
        // Scene切替要求などをQueueへ積み、Physics更新終了後に実行する。
    }
}
```

特定Entityに関係するイベントだけが必要なら`events_for(entity)`を使います。System走査中にEntityを直接削除すると参照が無効になるため、削除やScene切替は更新後へ遅延してください。

## Raycastと範囲検索

`physics_system::raycast()`は照準、接地確認、クリック選択に利用できます。`layer_mask`で対象を絞り、最も近いEntity、位置、法線、距離を返します。自身のColliderへ当てたくない場合は最後の引数へ除外するEntity IDを渡します。

`physics_system::overlap_sphere()`は攻撃範囲、爆発、索敵範囲に入ったEntity一覧を返します。ゲーム側で全Entityを走査して距離判定を複製しないでください。

## Character Controller

移動Systemから`desired_velocity[0]`と`desired_velocity[2]`へ毎フレーム速度を設定します。ジャンプ時は、`grounded`を確認して`vertical_velocity = jump_speed`にします。

```cpp
if (controller.grounded && jump_pressed)
{
    controller.vertical_velocity = controller.jump_speed;
}
controller.desired_velocity[0] = input_x * move_speed;
controller.desired_velocity[2] = input_z * move_speed;
```

## 現在の制限と次の拡張

- Boxは現在Axis Alignedで、Transformの回転をColliderへ反映しない。
- Capsule同士とCapsule対Boxは安定性優先の縦長AABB判定。
- Broad PhaseはX軸Sweep-and-Prune。非常に広いMapや大量の動的物体ではSpatial HashまたはBVHへの拡張が必要。
- 回転運動、Joint、Continuous Collision Detectionは未実装。
- 選択Colliderの外接境界表示とRaycast／Overlap Sphereは実装済み。正確なSphere／Capsule線表示とShape Castが残る。

これらをゲームコードへ個別実装せず、`PhysicsSystem`へ追加してください。形状やSolverを置き換えてもComponentとSceneデータを維持できるようにするためです。

## 小さい段差を自動で登る

接地中にXZ移動すると、Step Height以下の静的Boxの上へ登れます。最大高さは実寸m、0で無効です。見本の階段は一段0.4m、Playerの設定は0.45mです。持ち上げた位置と移動先の頭上が塞がると登りません。ジャンプ中・Trigger・Dynamicの箱は段差乗越えの対象外です。下りの小段差は接地面へ吸着し、大きな落下を自動で埋めません。

PhysicsSystemのsimulateで全Colliderを作ってからControllerの段差を調べ、その後に通常の衝突を解決します。吸着でめり込みをなくした床もContactへ記録するため、接地イベントが途切れにくくなります。位置補正後は作業中の境界も更新し、後続の衝突へ古い位置を使わないようにしています。

Tests/PlaytestTestsは30/60/144fps相当の時間刻みで歩行・ダッシュ・斜め入力による実マップの階段通過、下り、上限高さ、低い天井、Trigger、乗越え無効を検証します。