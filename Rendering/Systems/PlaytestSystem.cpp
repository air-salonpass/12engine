#include "PlaytestSystem.h"
#include "PhysicsSystem.h"
#include "../Scene/SceneRegistry.h"
#include <algorithm>
#include <cmath>

namespace
{
    void respawn(transform_component& transform, character_controller_component& controller,
        playtest_controller_component& test)
    {
        if (!test.checkpoint_valid) return;
        std::copy_n(test.checkpoint,3,transform.position);
        controller.vertical_velocity = 0;
        controller.grounded = false;
        std::fill_n(controller.desired_velocity,3,0.0f);
        ++test.respawns;
    }
}

void playtest_system::apply_mouse_look(float& yaw,float& pitch,float dx,float dy,float sensitivity)
{
    sensitivity=(std::clamp)(sensitivity,0.01f,0.5f);
    yaw=std::remainder(yaw-dx*sensitivity,360.0f);
    pitch=(std::clamp)(pitch+dy*sensitivity,-85.0f,85.0f);
}

void playtest_system::view_direction(float yaw,float pitch,float direction[3])
{
    yaw*=3.14159265359f/180.0f; pitch*=3.14159265359f/180.0f;
    direction[0]=-std::sin(yaw)*std::cos(pitch);
    direction[1]=-std::sin(pitch);
    direction[2]=std::cos(yaw)*std::cos(pitch);
}

bool playtest_system::begin(scene_registry& scene)
{
    // 追従カメラと操作先を曖昧にしない。無効な構成はテスト開始前に拒否する。
    unsigned int candidates = 0;
    scene.for_each<transform_component,character_controller_component,playtest_controller_component,collider_component>(
        [&](scene_entity, auto&, const auto& controller, auto&, const auto& collider)
        { if (controller.enabled && collider.enabled && !collider.trigger) ++candidates; });
    if (candidates != 1) return false;
    bool found = false;
    scene.for_each<transform_component,character_controller_component,playtest_controller_component>(
        [&](scene_entity entity,transform_component& transform,character_controller_component& controller,playtest_controller_component& test)
        {
            test.checkpoint_valid = false;
            const auto* collider = scene.collider(entity);
            if (!controller.enabled || !collider || !collider->enabled || collider->trigger) return;
            std::copy_n(transform.position,3,test.checkpoint);
            test.checkpoint_valid = true;
            test.goal_reached = false;
            test.respawns = 0;
            controller.vertical_velocity = 0;
            std::fill_n(controller.desired_velocity,3,0.0f);
            found = true;
        });
    return found;
}

void playtest_system::update_input(scene_registry& scene,const playtest_input& input)
{
    float right = input.right, forward = input.forward;
    const float magnitude = std::sqrt(right*right+forward*forward);
    if (magnitude>1) { right/=magnitude; forward/=magnitude; }
    const float yaw = input.camera_yaw_degrees*3.14159265359f/180.0f;
    scene.for_each<transform_component,character_controller_component,playtest_controller_component>(
        [&](scene_entity,transform_component& transform,character_controller_component& controller,playtest_controller_component& test)
        {
            if (!test.checkpoint_valid || !controller.enabled) return;
            if (input.reset_pressed) respawn(transform,controller,test);
            const float speed = (std::clamp)(test.move_speed*(input.sprint_held ? (std::clamp)(test.sprint_multiplier,1.0f,3.0f) : 1.0f),0.0f,20.0f);
            controller.desired_velocity[0] = (right*std::cos(yaw)-forward*std::sin(yaw))*speed;
            controller.desired_velocity[2] = (forward*std::cos(yaw)+right*std::sin(yaw))*speed;
            if (input.jump_pressed && controller.grounded) controller.vertical_velocity = controller.jump_speed;
        });
}

void playtest_system::after_physics(scene_registry& scene,const physics_system& physics)
{
    scene.for_each<transform_component,character_controller_component,playtest_controller_component>(
        [&](scene_entity entity,transform_component& transform,character_controller_component& controller,playtest_controller_component& test)
        {
            if (!test.checkpoint_valid || !controller.enabled) return;
            bool hazard = false;
            for (const auto& event : physics.events())
            {
                if (!event.contact.trigger || event.phase==physics_event_phase::exit) continue;
                const auto other = event.contact.first==entity ? event.contact.second :
                    (event.contact.second==entity ? event.contact.first : null_scene_entity);
                const auto* tag = scene.gameplay_tag(other);
                if (!tag) continue;
                if (tag->value=="Checkpoint" && event.phase==physics_event_phase::enter)
                {
                    // 表示用マーカーの高さへ飛ばさず、プレイヤーが実際に到達した位置を復帰地点にする。
                    std::copy_n(transform.position,3,test.checkpoint);
                    test.checkpoint[1]+=0.1f;
                }
                if (tag->value=="Goal") test.goal_reached = true;
                if (tag->value=="Hazard") hazard = true;
            }
            if (hazard || transform.position[1]<test.respawn_height) respawn(transform,controller,test);
        });
}
