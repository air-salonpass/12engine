#pragma once

class scene_registry;
class physics_system;

// OS入力を直接読まず、アプリ側で確定した入力を受け取る。単体テストや別入力機器にも同じ処理を使える。
struct playtest_input
{
    float right = 0;
    float forward = 0;
    float camera_yaw_degrees = 0;
    bool jump_pressed = false;
    bool reset_pressed = false;
    bool sprint_held = false;
};

class playtest_system
{
public:
    // 視点計算をOSから分離する。移動はyawだけ、視線はyawとpitchを使うため、真上を見ても歩行速度は一定。
    static void apply_mouse_look(float& yaw, float& pitch, float dx, float dy, float sensitivity);
    static void view_direction(float yaw, float pitch, float direction[3]);
    bool begin(scene_registry& scene);
    // Physicsの前に速度とジャンプ要求を書き、後にTriggerと落下結果を処理する。
    void update_input(scene_registry& scene, const playtest_input& input);
    void after_physics(scene_registry& scene, const physics_system& physics);
};
