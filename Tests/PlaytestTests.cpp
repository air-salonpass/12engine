#include "../Rendering/Scene/SceneRegistry.h"
#include "../Rendering/Systems/PhysicsSystem.h"
#include "../Rendering/Systems/PlaytestSystem.h"
#include "../Rendering/Systems/MapEditorSystem.h"
#include "../Rendering/tinygltf-release/json.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <limits>

// GPUやOS入力なしで、実際の見本Sceneの移動・Trigger・落下を検証する。
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool near(float a,float b) { return std::abs(a-b)<0.01f; }
void test_map_editor()
{
    map_editor_system map;
    map.settings().snap_to_grid=false; map.settings().grid_size=.5f;
    float position[3]{1.24f,2.3f,-1.26f}; map.snap(position);
    require(near(position[0],1.24f),"snap off");
    map.snap(position,true);
    require(near(position[0],1) && near(position[1],2.3f) && near(position[2],-1.5f),"Ctrl snaps XZ while preserving surface height");
    map.settings().grid_size=std::numeric_limits<float>::quiet_NaN();
    map.settings().rotation_step=0; map.settings().scale_step=-1; map.sanitize();
    require(near(map.settings().grid_size,1) && near(map.settings().rotation_step,1) && near(map.settings().scale_step,.01f),"invalid step values");
    map.settings().snap_to_grid=true; require(map.snapping(false),"global snap enabled");
    DirectX::XMFLOAT4X4 view{},projection{};
    DirectX::XMStoreFloat4x4(&view,DirectX::XMMatrixLookAtLH(DirectX::XMVectorSet(0,5,-5,1),DirectX::XMVectorZero(),DirectX::XMVectorSet(0,1,0,0)));
    DirectX::XMStoreFloat4x4(&projection,DirectX::XMMatrixPerspectiveFovLH(1.0f,2.0f,.1f,100));
    map_editor_ray ray{};
    require(map.make_ray(500,300,100,100,800,400,view,projection,ray),"viewport ray");
    require(map.intersect_plane(ray,0,position) && near(position[0],0) && near(position[2],0),"drop ray intersects center floor");
    require(!map.make_ray(50,300,100,100,800,400,view,projection,ray),"outside image must not place");
    const float surface[3]{2,3,-4};
    const auto preview=map.model_transform(surface,-1.5f,90,2);
    require(near(preview.position[0],2) && near(preview.position[1],6) && near(preview.position[2],-4),"preview matches final bottom placement");
    require(near(preview.position[1]+(-1.5f)*preview.scale3[1],surface[1]),"scaled model bottom stays on surface");
    require(near(preview.rotation[1],90) && near(preview.scale3[2],2),"preview orientation and scale");
}
void test_step_limits(float obstacle_height,bool ceiling,bool trigger,float step_height,bool should_pass)
{
    scene_registry scene; physics_system physics;
    auto box=[&](const char* name,float y,float z,float width,float height,float depth) {
        auto entity=scene.create_entity(name); auto& t=scene.add_transform(entity);
        t.position[1]=y; t.position[2]=z; t.scale3[0]=width; t.scale3[1]=height; t.scale3[2]=depth;
        scene.add_collider(entity).center[1]=.5f; return entity;
    };
    box("Floor",-.5f,4,10,.5f,20);
    auto obstacle=box("Step",0,2,4,obstacle_height,1); scene.collider(obstacle)->trigger=trigger;
    if (ceiling) box("Ceiling",1.9f,2,4,.3f,2);
    auto player=box("Player",0,0,.65f,1.7f,.65f);
    auto& controller=scene.add_character_controller(player); controller.step_height=step_height;
    for(int frame=0;frame<180;++frame) { controller.desired_velocity[2]=3; physics.update(scene,1.0f/60); }
    const auto& t=*scene.transform(player);
    require(should_pass ? t.position[2]>5 : t.position[2]<1.3f,"step clearance/height/trigger rules");
    if(trigger) require(std::abs(t.position[1])<.01f,"trigger must not lift player");
}
scene_entity load_stage(scene_registry& scene,const char* path)
{
    nlohmann::json root; std::ifstream file(path); file>>root;
    scene_entity player=0;
    for (const auto& item:root.at("entities"))
    {
        auto entity=scene.create_entity(item.value("name","Entity"));
        if (!item.contains("transform")) continue;
        auto& transform=scene.add_transform(entity);
        for (int i=0;i<3;++i) { transform.position[i]=item["transform"]["position"][i]; transform.scale3[i]=item["transform"]["scale3"][i]; }
        if (item.contains("collider"))
        {
            const auto& source=item["collider"]; auto& collider=scene.add_collider(entity);
            collider.trigger=source.value("trigger",false);
            for (int i=0;i<3;++i) { collider.center[i]=source["center"][i]; collider.half_extents[i]=source["half_extents"][i]; }
        }
        if (item.contains("rigid_body")) scene.add_rigid_body(entity).type=static_cast<rigid_body_type>(item["rigid_body"]["type"].get<int>());
        if (item.contains("gameplay_tag")) scene.add_gameplay_tag(entity).value=item["gameplay_tag"];
        if (item.contains("playtest_controller")) {
            const auto& source=item["playtest_controller"]; auto& test=scene.add<playtest_controller_component>(entity);
            test.first_person=source.value("first_person",true); test.eye_height=source.value("eye_height",1.55f);
            scene.add_character_controller(entity).step_height=item["character_controller"].value("step_height",.3f); player=entity;
        }
    }
    return player;
}
void test_stair_route(const char* path,bool sprint,float delta)
{
    scene_registry scene; physics_system physics; playtest_system test;
    auto player=load_stage(scene,path); require(test.begin(scene),"route start");
    auto& position=*scene.transform(player); auto& state=*scene.get<playtest_controller_component>(player);
    int stationary=0;
    for(int frame=0;frame<int(30/delta) && !state.goal_reached;++frame)
    {
        const float previous_z=position.position[2];
        playtest_input input{}; input.forward=1; input.sprint_held=sprint;
        // 階段で左右にも入力し、真正面だけ通れる実装になっていないか調べる。
        if(previous_z>4 && previous_z<9) input.right=std::sin(frame*delta*8)*.45f;
        test.update_input(scene,input); physics.update(scene,delta); test.after_physics(scene,physics);
        if(delta>=1.0f/60 && previous_z>4 && previous_z<9 && position.position[2]<=previous_z+.001f) ++stationary;
    }
    require(state.goal_reached && state.respawns==0,"walk/sprint diagonal stairs must reach goal");
    require(stationary<=2,"stairs must not stall horizontal movement");
}

int main(int argc,char** argv)
{
    try
    {
        require(argc==2,"pass stage JSON path");
        test_map_editor();
        test_step_limits(.4f,false,false,.45f,true);
        test_step_limits(.7f,false,false,.45f,false);
        test_step_limits(.4f,true,false,.45f,false);
        test_step_limits(.4f,false,false,0,false);
        test_step_limits(.4f,false,true,.45f,true);
        test_stair_route(argv[1],false,1.0f/60);
        test_stair_route(argv[1],true,1.0f/30);
        test_stair_route(argv[1],true,1.0f/144);
        scene_registry scene; playtest_system test; physics_system physics;
        require(!test.begin(scene),"empty scene must not start");
        auto player=load_stage(scene,argv[1]); require(player!=0,"stage player missing");
        auto* collider=scene.collider(player); collider->enabled=false;
        require(!test.begin(scene),"disabled collider must not start"); collider->enabled=true;
        require(test.begin(scene),"stage must start");
        auto& controller=*scene.character_controller(player); auto& state=*scene.get<playtest_controller_component>(player);
        auto& position=*scene.transform(player);
        require(state.first_person && near(state.eye_height,1.55f),"sample first person configuration");
        float yaw=0,pitch=0,direction[3]{};
        test.apply_mouse_look(yaw,pitch,750,10000,.12f);
        require(near(yaw,-90) && near(pitch,85),"mouse right and pitch limit");
        test.view_direction(yaw,0,direction);
        require(near(direction[0],1) && near(direction[2],0),"look right direction");
        test.update_input(scene,{0,1,yaw});
        require(near(controller.desired_velocity[0],5),"walk follows mouse yaw");
        test.apply_mouse_look(yaw,pitch,0,-10000,.12f); require(near(pitch,-85),"look up limit");
        auto duplicate=scene.duplicate(player); require(!test.begin(scene),"multiple players rejected"); scene.destroy(duplicate);
        require(test.begin(scene),"restart with one player");
        test.update_input(scene,{1,1});
        require(near(std::hypot(controller.desired_velocity[0],controller.desired_velocity[2]),5),"diagonal speed");
        test.update_input(scene,{1,1,0,false,false,true});
        require(near(std::hypot(controller.desired_velocity[0],controller.desired_velocity[2]),9),"shift diagonal sprint speed");
        test.update_input(scene,{0,1,90}); require(near(controller.desired_velocity[0],-5),"camera relative forward");
        controller.grounded=false; controller.vertical_velocity=0;
        test.update_input(scene,{0,0,0,true}); require(controller.vertical_velocity==0,"air jump rejected");
        controller.grounded=true; test.update_input(scene,{0,0,0,true}); require(near(controller.vertical_velocity,7),"ground jump");
        controller.vertical_velocity=0; controller.grounded=false;
        // ジャンプ入力なしで階段を登れることを、実際の見本データで検証する。
        for (int frame=0;frame<1200 && !state.goal_reached;++frame)
        {
            test.update_input(scene,{0,1}); physics.update(scene,1.0f/60); test.after_physics(scene,physics);
        }
        std::cout<<"route: "<<position.position[0]<<", "<<position.position[1]<<", "<<position.position[2]<<" grounded="<<controller.grounded<<" vy="<<controller.vertical_velocity<<" step="<<controller.step_height<<"\n";
        require(state.goal_reached,"stage route must reach Goal"); require(state.checkpoint[2]>-4,"checkpoint reached");
        for(int frame=0;frame<260;++frame) {
            test.update_input(scene,{0,-1}); physics.update(scene,1.0f/60); test.after_physics(scene,physics);
        }
        require(position.position[2]<-9 && near(position.position[1],0) && state.respawns==0,"descend stairs without falling");
        const float checkpoint_z=state.checkpoint[2];
        position.position[1]=-20; test.after_physics(scene,physics);
        require(state.respawns==1 && near(position.position[2],checkpoint_z),"fall respawn");
        position.position[2]=-12; test.update_input(scene,{0,0,0,false,true}); require(state.respawns==2 && near(position.position[2],checkpoint_z),"manual respawn");
        physics.clear(); position.position[0]=8; position.position[1]=0; position.position[2]=-9;
        test.update_input(scene,{}); physics.update(scene,1.0f/60); test.after_physics(scene,physics);
        require(state.respawns==3 && near(position.position[2],checkpoint_z),"hazard respawn");
        scene_registry snapshot=scene; auto copy=scene.duplicate(player);
        require(scene.get<playtest_controller_component>(copy)!=nullptr,"duplicate includes component");
        scene=snapshot; require(!scene.valid(copy),"snapshot removes test entities");
        std::cout<<"PASS: route, input, jump, checkpoint, goal, respawn, duplicate, snapshot\n"; return 0;
    }
    catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<"\n"; return 1; }
}
