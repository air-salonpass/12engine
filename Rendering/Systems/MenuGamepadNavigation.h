#pragma once
#include "../imgui/imgui.h"
#include <algorithm>

namespace engine_ui
{
// OS固有のパッド取得とUIを分ける。DualSense/XInputを同じボタンの意味に正規化して渡す。
// 呼出し位置はWin32_NewFrameの後、ImGui::NewFrameの前。逆順ではWin32側に上書きされる。
// 接続画面や設定画面だけ有効にし、戦闘中はUIへ入力を流さない。
struct menu_pad
{
    bool connected=false, accept=false, cancel=false;
    bool up=false, down=false, left=false, right=false, slow=false, fast=false;
    float x=0, y=0;
};
inline void apply_menu_gamepads(const menu_pad* pads, unsigned count, bool enabled, bool focused, bool blocked)
{
    auto& io=ImGui::GetIO();
    const auto flags=ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    if (enabled) io.ConfigFlags |= flags; else io.ConfigFlags &= ~flags;
    std::fill(std::begin(io.NavInputs),std::end(io.NavInputs),0.0f);
    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    if (!enabled || !focused) return;
    // 既存メニューのEnter決定も維持する。文字入力中のEnterは入力欄自身へ任せる。
    const int enter=io.KeyMap[ImGuiKey_Enter];
    if (!blocked && !io.WantTextInput && !io.KeyAlt && enter>=0 && io.KeysDown[enter])
        io.NavInputs[ImGuiNavInput_Activate]=1.0f;
    for (unsigned i=0;i<count;++i)
    {
        const auto& p=pads[i]; if (!p.connected) continue;
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        if (blocked) continue;
        const auto set=[&](ImGuiNavInput key,bool value) { if (value) io.NavInputs[key]=1.0f; };
        set(ImGuiNavInput_Activate,p.accept); set(ImGuiNavInput_Cancel,p.cancel);
        // スティックも選択移動へ割り当てる。0.5のデッドゾーンで触れただけの誤操作を防ぐ。
        set(ImGuiNavInput_DpadUp,p.up || p.y>0.5f); set(ImGuiNavInput_DpadDown,p.down || p.y< -0.5f);
        set(ImGuiNavInput_DpadLeft,p.left || p.x< -0.5f); set(ImGuiNavInput_DpadRight,p.right || p.x>0.5f);
        set(ImGuiNavInput_TweakSlow,p.slow); set(ImGuiNavInput_TweakFast,p.fast);
    }
}
}
