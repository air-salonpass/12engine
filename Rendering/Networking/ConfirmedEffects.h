#pragma once
#include "FrameHistory.h"
#include <stdexcept>
#include <vector>

namespace engine_net
{
// 予測中の音・振動要求をフレームごとに保管し、再計算で置換する。
// 入力が確定してから一度だけ出力するので、訂正で消えた音も鳴らさない。
// この履歴自体はsave/loadしない。外へ出した結果を巻き戻すことはできないため。
template<class Event> class confirmed_effects
{
public:
    void replace(frame_id frame, std::vector<Event> events)
    {
        if (frame < next_ || static_cast<std::uint64_t>(frame) >= static_cast<std::uint64_t>(next_) + 256)
            throw std::logic_error("effect history outside uncommitted range");
        pending_.store(frame, std::move(events));
    }
    template<class Emit> void commit_through(frame_id confirmed, Emit emit)
    {
        while (next_ <= confirmed)
        {
            const auto* events = pending_.find(next_);
            if (!events) throw std::logic_error("confirmed effect frame missing");
            for (const auto& event : *events) emit(event);
            ++next_;
        }
    }
    frame_id next_frame() const { return next_; }
private:
    frame_history<std::vector<Event>> pending_;
    frame_id next_ = 0;
};
}
