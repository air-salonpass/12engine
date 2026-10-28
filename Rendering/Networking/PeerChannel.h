#pragma once
#include "RollbackSession.h"

namespace engine_net
{
struct input_record { frame_id frame = 0; bytes input; };
struct input_packet
{
    unsigned player = 0, input_size = 0;
    std::uint64_t session = 0, game = 0;
    frame_id acknowledged = no_frame, hash_frame = no_frame;
    std::uint64_t hash = 0;
    std::vector<input_record> records;
};
namespace wire
{
inline void append(bytes& out, std::uint64_t value, unsigned width)
{
    for (unsigned i = width; i > 0; --i) out.push_back(static_cast<std::uint8_t>(value >> ((i - 1) * 8)));
}
inline std::uint64_t read(const bytes& data, std::size_t& offset, unsigned width)
{
    std::uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i) value = (value << 8) | data[offset++];
    return value;
}
inline bytes encode(const input_packet& packet)
{
    if (packet.player > 1 || packet.input_size == 0 || packet.input_size > 64 || packet.records.size() > 16)
        return {};
    bytes out;
    out.reserve(40 + packet.records.size() * (4 + packet.input_size));
    append(out, 0x4a454e31, 4); // JEN1 構造体のメモリをそのまま送らず バイト順を固定する
    append(out, 1, 1);
    append(out, packet.player, 1);
    append(out, packet.input_size, 1);
    append(out, packet.records.size(), 1);
    append(out, packet.session, 8); append(out, packet.game, 8);
    append(out, packet.acknowledged, 4); append(out, packet.hash_frame, 4); append(out, packet.hash, 8);
    for (const auto& record : packet.records)
    {
        if (record.input.size() != packet.input_size || record.frame == no_frame) return {};
        append(out, record.frame, 4);
        out.insert(out.end(), record.input.begin(), record.input.end());
    }
    return out;
}
inline bool decode(const bytes& data, input_packet& result)
{
    // 長さを先に検査し 未検証の受信データで配列やメモリ確保サイズを決めない
    if (data.size() < 40 || data.size() > 1200) return false;
    std::size_t offset = 0;
    if (read(data, offset, 4) != 0x4a454e31 || read(data, offset, 1) != 1) return false;
    input_packet packet;
    packet.player = static_cast<unsigned>(read(data, offset, 1));
    packet.input_size = static_cast<unsigned>(read(data, offset, 1));
    const auto count = read(data, offset, 1);
    if (packet.player > 1 || packet.input_size == 0 || packet.input_size > 64 || count > 16 ||
        data.size() != 40 + count * (4 + packet.input_size)) return false;
    packet.session = read(data, offset, 8); packet.game = read(data, offset, 8);
    packet.acknowledged = static_cast<frame_id>(read(data, offset, 4));
    packet.hash_frame = static_cast<frame_id>(read(data, offset, 4));
    packet.hash = read(data, offset, 8);
    for (unsigned i = 0; i < count; ++i)
    {
        input_record record;
        record.frame = static_cast<frame_id>(read(data, offset, 4));
        if (record.frame == no_frame || (i > 0 && record.frame <= packet.records.back().frame)) return false;
        record.input.assign(data.begin() + offset, data.begin() + offset + packet.input_size);
        offset += packet.input_size;
        packet.records.push_back(std::move(record));
    }
    result = std::move(packet);
    return true;
}
}

// 予測と状態復元はRollbackSession 送受信はUdpTransport このクラスは再送と照合だけ担当
// gameは入力仕様や対戦ルールが同じビルドを区別する番号 sessionは今回の試合の番号
// 番号は認証用の秘密鍵ではない 初期実装は信頼できるLAN相手での検証用
class peer_channel
{
public:
    peer_channel(rollback_session& simulation, std::uint64_t game, std::uint64_t session)
        : simulation_(simulation), game_(game), session_(session)
    {
        if (game == 0 || session == 0) throw std::invalid_argument("game/session id must be nonzero");
    }
    bool connected() const { return connected_; }
    std::string error() const { return error_.empty() ? simulation_.error() : error_; }
    frame_id peer_acknowledged() const { return acknowledged_; }
    std::uint64_t rejected_packets() const { return rejected_; }
    bool can_advance() const
    {
        const std::uint64_t first = acknowledged_ == no_frame ? 0 : static_cast<std::uint64_t>(acknowledged_) + 1;
        return connected_ && error().empty() && simulation_.can_advance() &&
            static_cast<std::uint64_t>(simulation_.next_frame()) - first < rollback_session::history_capacity - 1;
    }
    bool advance(const bytes& input)
    {
        if (!can_advance() || !simulation_.advance(input)) return false;
        check_hash();
        return error().empty();
    }
    bytes outgoing()
    {
        if (!simulation_.reconcile()) return {};
        check_hash();
        if (!error().empty()) return {};
        input_packet packet;
        packet.player = simulation_.local_player(); packet.input_size = simulation_.input_size();
        packet.session = session_; packet.game = game_;
        if (simulation_.remote_confirmed() >= 0) packet.acknowledged = static_cast<frame_id>(simulation_.remote_confirmed());
        const frame_id first = acknowledged_ == no_frame ? 0 : acknowledged_ + 1;
        // 未確認の古い入力から繰り返し送る 最新だけを送ると欠落した入力を回収できない
        for (frame_id frame = first; frame < simulation_.next_frame() && packet.records.size() < 16; ++frame)
        {
            const auto* input = simulation_.local_input(frame);
            if (!input) { error_ = "unacknowledged input history expired"; return {}; }
            packet.records.push_back({frame, *input});
        }
        if (simulation_.next_frame() > 0 && simulation_.remote_confirmed() >= 0)
        {
            const auto frame = static_cast<frame_id>((std::min)(
                static_cast<std::int64_t>(simulation_.next_frame() - 1), simulation_.remote_confirmed()));
            if (const auto hash = simulation_.confirmed_hash(frame)) { packet.hash_frame = frame; packet.hash = *hash; }
        }
        return wire::encode(packet);
    }
    bool receive(const bytes& data)
    {
        input_packet packet;
        if (!wire::decode(data, packet) || packet.game != game_ || packet.session != session_ ||
            packet.player == simulation_.local_player() || packet.input_size != simulation_.input_size() ||
            (packet.acknowledged != no_frame && packet.acknowledged >= simulation_.next_frame()) ||
            (packet.hash_frame != no_frame && static_cast<std::uint64_t>(packet.hash_frame) >=
                static_cast<std::uint64_t>(simulation_.next_frame()) + rollback_session::history_capacity))
        { ++rejected_; return false; }
        for (const auto& record : packet.records)
            if (static_cast<std::uint64_t>(record.frame) >= static_cast<std::uint64_t>(simulation_.next_frame()) +
                rollback_session::history_capacity) { ++rejected_; return false; }
        for (const auto& record : packet.records)
        {
            const auto result = simulation_.receive_remote(record.frame, record.input);
            if (result == input_result::conflict || result == input_result::invalid) { ++rejected_; return false; }
        }
        if (packet.acknowledged != no_frame && (acknowledged_ == no_frame || packet.acknowledged > acknowledged_))
            acknowledged_ = packet.acknowledged;
        if (packet.hash_frame != no_frame && (pending_hash_frame_ == no_frame || packet.hash_frame >= pending_hash_frame_))
        { pending_hash_frame_ = packet.hash_frame; pending_hash_ = packet.hash; }
        connected_ = true;
        if (!simulation_.reconcile()) return false;
        check_hash();
        return error().empty();
    }
private:
    void check_hash()
    {
        if (pending_hash_frame_ == no_frame) return;
        if (const auto hash = simulation_.confirmed_hash(pending_hash_frame_))
        {
            if (*hash != pending_hash_) error_ = "confirmed game state desynchronized";
            pending_hash_frame_ = no_frame;
        }
    }
    rollback_session& simulation_;
    std::uint64_t game_, session_;
    frame_id acknowledged_ = no_frame, pending_hash_frame_ = no_frame;
    std::uint64_t pending_hash_ = 0, rejected_ = 0;
    bool connected_ = false;
    std::string error_;
};
}
