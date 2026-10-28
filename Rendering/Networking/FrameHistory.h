#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace engine_net
{
using frame_id = std::uint32_t;
// 配列位置だけでなくフレーム番号も照合する 循環後に古い入力を取り違えないため
template<class T, std::size_t Capacity = 256> class frame_history
{
    static_assert(Capacity > 1);
    struct entry { frame_id frame{}; std::optional<T> value; };
    std::array<entry, Capacity> entries_{};
public:
    void store(frame_id frame, T value)
    {
        auto& slot = entries_[frame % Capacity];
        slot.frame = frame;
        slot.value = std::move(value);
    }
    const T* find(frame_id frame) const
    {
        const auto& slot = entries_[frame % Capacity];
        return slot.frame == frame && slot.value ? &*slot.value : nullptr;
    }
};
}
