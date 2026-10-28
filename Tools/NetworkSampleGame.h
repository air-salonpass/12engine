#pragma once
#include "../Rendering/Networking/PeerChannel.h"
#include <cstdlib>

// 通信層にゲームのルールを埋め込まないための小さなサンプル
// 入力2バイトと状態34バイトを明示的に符号化し 構造体のpaddingやポインターを保存しない
namespace network_sample
{
using namespace engine_net;
struct game
{
    std::uint32_t frame = 0, random = 0x13572468;
    std::array<std::uint32_t, 2> position{1000, 1200}, health{1000, 1000}, cooldown{};
    std::array<std::uint8_t, 2> previous_buttons{};
    std::uint64_t loads = 0, replay_steps = 0; // 診断値は試合状態と分ける
    bytes save() const
    {
        bytes out;
        wire::append(out, frame, 4); wire::append(out, random, 4);
        for (auto value : position) wire::append(out, value, 4);
        for (auto value : health) wire::append(out, value, 4);
        for (auto value : previous_buttons) wire::append(out, value, 1);
        for (auto value : cooldown) wire::append(out, value, 4);
        return out;
    }
    void load(const bytes& data)
    {
        if (data.size() != 34) throw std::runtime_error("invalid sample state length");
        std::size_t offset = 0;
        frame = static_cast<std::uint32_t>(wire::read(data, offset, 4));
        random = static_cast<std::uint32_t>(wire::read(data, offset, 4));
        for (auto& value : position) value = static_cast<std::uint32_t>(wire::read(data, offset, 4));
        for (auto& value : health) value = static_cast<std::uint32_t>(wire::read(data, offset, 4));
        for (auto& value : previous_buttons) value = static_cast<std::uint8_t>(wire::read(data, offset, 1));
        for (auto& value : cooldown) value = static_cast<std::uint32_t>(wire::read(data, offset, 4));
        ++loads;
    }
    void step(frame_id number, const std::array<bytes, 2>& inputs, bool replay)
    {
        if (frame != number) throw std::runtime_error("sample frame sequence mismatch");
        for (unsigned player = 0; player < 2; ++player)
        {
            const auto& input = inputs[player];
            if (input.size() != 2 || input[0] > 2 || input[1] > 1) throw std::runtime_error("invalid sample input");
            if (cooldown[player]) --cooldown[player];
            if (input[0] == 1 && position[player] < 5000) position[player] += 2;
            if (input[0] == 2 && position[player] >= 2) position[player] -= 2;
            // 予測は長押し状態を繰り返す 押下判定の前回値もStateへ戻すことが重要
            if (input[1] && !previous_buttons[player] && cooldown[player] == 0)
            {
                random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                if (std::abs(static_cast<int>(position[0]) - static_cast<int>(position[1])) <= 300)
                {
                    const auto damage = 5 + random % 7;
                    auto& hp = health[1 - player];
                    hp = hp > damage ? hp - damage : 0;
                }
                cooldown[player] = 8;
            }
            previous_buttons[player] = input[1];
        }
        ++frame;
        if (replay) ++replay_steps;
    }
    rollback_callbacks callbacks()
    {
        return {[this] { return save(); }, [this](const bytes& state) { load(state); },
            [this](frame_id number, const std::array<bytes, 2>& input, bool replay) { step(number, input, replay); }};
    }
};
inline bytes input(unsigned player, frame_id frame)
{
    return {static_cast<std::uint8_t>((frame / 17 + player) % 3),
        static_cast<std::uint8_t>((frame + player * 11) % 23 < 3)};
}
inline game reference(frame_id frames)
{
    game result;
    for (frame_id frame = 0; frame < frames; ++frame) result.step(frame, {input(0, frame), input(1, frame)}, false);
    return result;
}
}
