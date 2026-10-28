#pragma once

// マップの通路幅・接地・ジャンプ・Triggerを歩いて確認するための設定。
// Transform、Collider、Character Controllerと組み合わせる。完成ゲームの操作Systemとは独立して使える。
struct playtest_controller_component
{
    float move_speed = 5.0f;
    float sprint_multiplier = 1.8f;
    float respawn_height = -6.0f;
    float camera_distance = 7.0f;
    // 一人称では足元からeye_height(m)の位置で見る。モデルの拡大率とは独立した実寸。
    bool first_person = true;
    float eye_height = 1.55f;
    float mouse_sensitivity = 0.12f;
    float field_of_view = 75.0f;
    // 以下はテスト中の状態。Scene/Prefabへは上の設定だけを保存し、開始時に現在位置から初期化する。
    float checkpoint[3]{};
    bool checkpoint_valid = false;
    bool goal_reached = false;
    unsigned int respawns = 0;
};
