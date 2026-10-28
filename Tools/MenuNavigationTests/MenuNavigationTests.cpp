#include "../../Rendering/Systems/MenuGamepadNavigation.h"
#include "../../Rendering/imgui/imgui_internal.h"
#include <iostream>
#include <stdexcept>

static void require(bool value,const char* reason) { if (!value) throw std::runtime_error(reason); }
int main()
{
    ImGui::CreateContext();
    try
    {
        auto& io=ImGui::GetIO(); io.DisplaySize=ImVec2(1280,720); io.DeltaTime=1.0f/60; io.IniFilename=nullptr;
        for (int i=0;i<ImGuiKey_COUNT;++i) io.KeyMap[i]=i;
        unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        engine_ui::menu_pad pad; pad.connected=true;
        ImGuiID first=0,second=0; bool pressed=false,initial=true; float volume=.5f;
        const auto frame=[&](bool enabled=true,bool focused=true,bool blocked=false)
        {
            engine_ui::apply_menu_gamepads(&pad,1,enabled,focused,blocked);
            ImGui::NewFrame(); ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(ImVec2(400,400));
            if (initial) { ImGui::SetNextWindowFocus(); initial=false; }
            ImGui::Begin("Menu",nullptr,ImGuiWindowFlags_NoSavedSettings);
            first=ImGui::GetID("Host"); ImGui::Button("Host"); ImGui::SetItemDefaultFocus();
            second=ImGui::GetID("Search"); pressed |= ImGui::Button("Search");
            ImGui::SliderFloat("Volume",&volume,0,1);
            ImGui::End(); ImGui::Render();
        };
        frame(); frame();
        // マウスからパッドへ切り替えた最初の方向入力は、先頭項目へのフォーカス取得に使われる。
        pad.up=true; frame(); pad.up=false; frame(); frame();
        require(ImGui::GetCurrentContext()->NavId==first,"gamepad acquires menu focus");
        pad.down=true; frame(); pad.down=false; frame(); frame();
        require(ImGui::GetCurrentContext()->NavId==second,"gamepad down selects Search");
        pad.accept=true; frame(); pad.accept=false; frame();
        require(pressed,"gamepad accept activates selected button");
        pad.down=true; frame(); pad.down=false; frame(); frame();
        pad.accept=true; frame(); pad.accept=false; frame();
        pad.right=true; for (int i=0;i<20;++i) frame(); pad.right=false; frame();
        require(volume>.5f,"gamepad adjusts actual ImGui slider");
        pad.accept=true; engine_ui::apply_menu_gamepads(&pad,1,true,true,true);
        require(io.NavInputs[ImGuiNavInput_Activate]==0,"screen transition blocks held accept");
        engine_ui::apply_menu_gamepads(&pad,1,true,false,false);
        require(io.NavInputs[ImGuiNavInput_Activate]==0,"inactive window cannot activate UI");
        engine_ui::apply_menu_gamepads(&pad,1,false,true,false);
        require(!(io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) && io.NavInputs[ImGuiNavInput_Activate]==0,"combat disables menu navigation");
        ImGui::DestroyContext(); std::cout<<"PASS menu navigation: selection / activation / slider / held / focus / combat\n"; return 0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; ImGui::DestroyContext(); return 1; }
}
