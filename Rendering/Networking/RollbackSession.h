#pragma once
#include "FrameHistory.h"
#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine_net
{
using bytes = std::vector<std::uint8_t>;
constexpr frame_id no_frame = (std::numeric_limits<frame_id>::max)();
struct rollback_callbacks
{
    // Stateには座標だけでなく乱数種 入力の前回値 タイマー Entity生成番号も含める
    // GPUポインターや通信ソケットは含めない 同じ入力から同じ結果になることが必要
    std::function<bytes()> save;
    std::function<void(const bytes&)> load;
    // 入力は常にP1 P2の順 replay=trueでの音や実績の重複防止はゲーム側で実装する
    std::function<void(frame_id, const std::array<bytes, 2>&, bool replay)> step;
    // 押した瞬間や方向イベントを含む入力は、ゲーム側で長押しだけの予測へ変換する。
    // 未指定なら従来どおり直前入力を繰り返す。戻り値の長さはinput_sizeと一致させる。
    std::function<bytes(frame_id, const bytes&)> predict_remote;
    // ローカル表示設定も保存するゲームは、確定Stateから戦闘だけの比較値を取り出す。
    // 未指定なら従来どおり保存bytes全体のFNVを使う。認証や暗号化ではない。
    std::function<std::uint64_t(const bytes&)> state_checksum;
};
struct rollback_statistics
{
    std::uint64_t rollbacks = 0, replayed_frames = 0, predicted_frames = 0;
};
enum class input_result { accepted, duplicate, too_old, too_far_ahead, conflict, invalid };

// ゲーム 描画API 通信APIへ依存しない2人用の固定フレーム同期
// 通信待ちでadvanceできない場合は入力を消費せず 次の描画で受信を続ける
class rollback_session
{
public:
    static constexpr std::size_t history_capacity = 256;
    rollback_session(unsigned local_player, unsigned input_size, rollback_callbacks callbacks,
        unsigned prediction_frames = 8, std::size_t state_limit = 1024 * 1024)
        : local_player_(local_player), input_size_(input_size), callbacks_(std::move(callbacks)),
          prediction_frames_(prediction_frames), state_limit_(state_limit), neutral_(input_size, 0)
    {
        if (local_player > 1 || input_size == 0 || input_size > 64 || prediction_frames == 0 ||
            prediction_frames > 32 || state_limit == 0 || !callbacks_.save || !callbacks_.load || !callbacks_.step)
            throw std::invalid_argument("invalid rollback configuration");
        save_snapshot(0);
    }
    unsigned local_player() const { return local_player_; }
    unsigned input_size() const { return input_size_; }
    frame_id next_frame() const { return next_; }
    std::int64_t remote_confirmed() const { return confirmed_; }
    const std::string& error() const { return error_; }
    const rollback_statistics& statistics() const { return statistics_; }
    const bytes* local_input(frame_id frame) const { return local_.find(frame); }
    bool can_advance() const
    {
        return error_.empty() && next_ < no_frame - 1 &&
            static_cast<std::int64_t>(next_) - confirmed_ - 1 < prediction_frames_;
    }
    input_result receive_remote(frame_id frame, const bytes& value)
    {
        if (value.size() != input_size_ || frame == no_frame) return input_result::invalid;
        if (static_cast<std::uint64_t>(frame) + history_capacity <= next_) return input_result::too_old;
        if (static_cast<std::uint64_t>(frame) >= static_cast<std::uint64_t>(next_) + history_capacity)
            return input_result::too_far_ahead;
        if (const auto* previous = remote_.find(frame))
        {
            if (*previous == value) return input_result::duplicate;
            error_ = "remote input changed after confirmation";
            return input_result::conflict;
        }
        remote_.store(frame, value);
        while (confirmed_ < static_cast<std::int64_t>(no_frame) - 1 &&
            remote_.find(static_cast<frame_id>(confirmed_ + 1))) ++confirmed_;
        if (frame < next_)
        {
            const auto* used = used_remote_.find(frame);
            if (!used) { error_ = "rollback input history expired"; return input_result::too_old; }
            if (*used != value) dirty_ = (std::min)(dirty_, frame);
        }
        return input_result::accepted;
    }
    bool reconcile()
    {
        if (!error_.empty()) return false;
        if (dirty_ == no_frame) return true;
        const frame_id first = dirty_;
        dirty_ = no_frame;
        const auto* saved = snapshots_.find(first);
        if (!saved) { error_ = "rollback state history expired"; return false; }
        callbacks_.load(*saved);
        ++statistics_.rollbacks;
        for (frame_id frame = first; frame < next_; ++frame)
        {
            const auto* local = local_.find(frame);
            if (!local) { error_ = "local input history expired"; return false; }
            simulate(frame, *local, true);
            ++statistics_.replayed_frames;
            if (!error_.empty()) return false;
        }
        return true;
    }
    bool advance(const bytes& local_input)
    {
        if (local_input.size() != input_size_ || !reconcile() || !can_advance()) return false;
        local_.store(next_, local_input);
        simulate(next_, local_input, false);
        if (!error_.empty()) return false;
        ++next_;
        return true;
    }
    // 確定済みの状態だけ照合する 予測中の状態を比べると正常でも不一致になる
    std::optional<std::uint64_t> confirmed_hash(frame_id frame) const
    {
        if (!error_.empty() || dirty_ != no_frame || frame >= next_ || frame > confirmed_) return {};
        const auto* state = snapshots_.find(frame + 1);
        if (!state) return {};
        if (callbacks_.state_checksum) return callbacks_.state_checksum(*state);
        std::uint64_t hash = 14695981039346656037ull;
        for (const auto value : *state) { hash ^= value; hash *= 1099511628211ull; }
        return hash; // 同期診断用 暗号や改ざん防止の代用ではない
    }
private:
    void save_snapshot(frame_id frame)
    {
        auto state = callbacks_.save();
        if (state.size() > state_limit_) { error_ = "state exceeds configured byte limit"; return; }
        snapshots_.store(frame, std::move(state));
    }
    void simulate(frame_id frame, const bytes& local, bool replay)
    {
        bytes remote = neutral_;
        if (const auto* actual = remote_.find(frame)) remote = *actual;
        else
        {
            if (frame > 0) if (const auto* previous = used_remote_.find(frame - 1)) remote = *previous;
            if (callbacks_.predict_remote) remote = callbacks_.predict_remote(frame, remote);
            if (remote.size() != input_size_) { error_ = "predicted input has invalid size"; return; }
            if (!replay) ++statistics_.predicted_frames;
        }
        used_remote_.store(frame, remote);
        std::array<bytes, 2> inputs;
        inputs[local_player_] = local;
        inputs[1 - local_player_] = std::move(remote);
        callbacks_.step(frame, inputs, replay);
        save_snapshot(frame + 1);
    }
    unsigned local_player_, input_size_;
    rollback_callbacks callbacks_;
    unsigned prediction_frames_;
    std::size_t state_limit_;
    bytes neutral_;
    frame_id next_ = 0, dirty_ = no_frame;
    std::int64_t confirmed_ = -1;
    std::string error_;
    rollback_statistics statistics_;
    frame_history<bytes> local_, remote_, used_remote_, snapshots_;
};
}
