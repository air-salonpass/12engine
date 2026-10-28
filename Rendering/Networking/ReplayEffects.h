#pragma once
#include <cstddef>

namespace engine_net
{
// 巻き戻しの再計算だけ、音・振動など取り消せない外部出力を止める。
// ゲーム状態の更新は止めない。1スレッド専用で、入れ子と例外からの復帰にも対応する。
// 出力先のplay等の入口でsuppressedを確認し、再計算を行う範囲をscopeで囲む。
class replay_effect_gate
{
public:
    class scope
    {
    public:
        explicit scope(replay_effect_gate& gate) noexcept : gate_(gate) { ++gate_.depth_; }
        ~scope() { --gate_.depth_; }
        scope(const scope&) = delete;
        scope& operator=(const scope&) = delete;
        scope(scope&&) = delete;
        scope& operator=(scope&&) = delete;
    private:
        replay_effect_gate& gate_;
    };
    replay_effect_gate() = default;
    replay_effect_gate(const replay_effect_gate&) = delete;
    replay_effect_gate& operator=(const replay_effect_gate&) = delete;
    bool suppressed() const noexcept { return depth_ != 0; }
    scope suppress() noexcept { return scope(*this); }
private:
    std::size_t depth_ = 0;
};
}
