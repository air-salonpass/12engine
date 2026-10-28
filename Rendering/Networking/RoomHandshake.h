#pragma once
#include "PeerChannel.h"

namespace engine_net
{
// 接続時だけ開始Stateを分割転送する。毎フレームの入力転送はPeerChannelが担当する。
// 送信元IP/portの固定、再送間隔、切断タイムアウトは呼出し側で管理する。
// nonce/sessionは古い試合の取り違え防止であり、認証・暗号化ではない。
class room_handshake
{
public:
    static constexpr std::size_t chunk_size = 1024, state_limit = 1024 * 1024, header_size = 70;
    enum class message : unsigned { hello = 1, chunk, acknowledge, ready, start, leave, reject, rematch };
    enum class mismatch { none, game, schema, build, assets };
    using manifest = std::array<std::uint64_t, 3>; // State形式、実行コード、素材の内容。
    room_handshake(bool host, std::uint64_t game, std::uint64_t nonce, bytes initial = {}, manifest content = {})
        : host_(host), game_(game), nonce_(host ? 0 : nonce), session_(host ? nonce : 0), state_(std::move(initial)), content_(content)
    {
        if (!game || !nonce || (host && (state_.empty() || state_.size() > state_limit)) || (!host && !state_.empty()))
            throw std::invalid_argument("invalid room configuration");
        if (host) digest_ = digest(state_);
    }
    bool ready() const { return ready_; }
    bool closed() const { return closed_; }
    bool has_peer() const { return nonce_ && session_; }
    mismatch incompatibility() const { return mismatch_; }
    bool rematch_requested() const { return requested_; }
    bool peer_rematch_requested() const { return peer_requested_; }
    void request_rematch() { if (ready_) requested_ = true; }
    // 双方の同意後だけホストが呼ぶ。新しい試合番号へ移り、古い入力を受け付けない。
    void restart(bytes initial)
    {
        if (!host_ || !ready_ || !requested_ || !peer_requested_ || session_ == UINT64_MAX || initial.empty() || initial.size() > state_limit)
            throw std::logic_error("invalid room restart");
        ++session_; state_ = std::move(initial); digest_ = digest(state_); index_ = 0;
        ready_ = complete_ = requested_ = peer_requested_ = false;
    }
    std::uint64_t session() const { return session_; }
    const bytes& initial_state() const { return state_; }
    std::size_t transferred() const { return (std::min)(state_.size(), std::size_t(index_) * chunk_size); }
    static std::uint64_t digest(const bytes& data)
    {
        std::uint64_t h = 14695981039346656037ull;
        for (auto b : data) { h ^= b; h *= 1099511628211ull; }
        return h;
    }
    static bool control_packet(const bytes& data)
    { return data.size() >= 4 && data[0] == 'J' && data[1] == 'E' && data[2] == 'R' && data[3] == '1'; }
    bytes outgoing(bool leave = false) const
    {
        if (host_ && !nonce_) return {};
        const auto kind = leave ? message::leave : mismatch_ != mismatch::none ? message::reject :
            ready_ && requested_ ? message::rematch : host_ ? (ready_ ? message::start : message::chunk) :
            (!session_ ? message::hello : (complete_ ? message::ready : message::acknowledge));
        bytes out;
        wire::append(out, 0x4a455231, 4); wire::append(out, 2, 1); wire::append(out, unsigned(kind), 1);
        wire::append(out, nonce_, 8); wire::append(out, session_, 8); wire::append(out, game_, 8);
        wire::append(out, state_.size(), 4); wire::append(out, index_, 4); wire::append(out, digest_, 8);
        for (auto value : content_) wire::append(out, value, 8);
        if (kind == message::chunk)
        {
            const auto begin = std::size_t(index_) * chunk_size;
            const auto end = (std::min)(state_.size(), begin + chunk_size);
            out.insert(out.end(), state_.begin() + begin, state_.begin() + end);
        }
        return out;
    }
    // trueは今回の相手からの妥当な制御パケット。未知形式は状態を変更せず無視する。
    bool receive(const bytes& data)
    {
        if (!control_packet(data) || data.size() < header_size || data.size() > header_size + chunk_size) return false;
        std::size_t p = 4;
        if (wire::read(data, p, 1) != 2) return false;
        const auto kind = static_cast<message>(wire::read(data, p, 1));
        const auto nonce = wire::read(data, p, 8), session = wire::read(data, p, 8), game = wire::read(data, p, 8);
        const auto size = wire::read(data, p, 4), index = wire::read(data, p, 4), hash = wire::read(data, p, 8);
        manifest content{}; for (auto& value : content) value = wire::read(data, p, 8);
        if (!nonce || size > state_limit) return false;
        const auto compare = [&] {
            mismatch_ = game != game_ ? mismatch::game : content[0] != content_[0] ? mismatch::schema :
                content[1] != content_[1] ? mismatch::build : content[2] != content_[2] ? mismatch::assets : mismatch::none;
        };
        if (host_ && kind == message::hello)
        {
            if (session || size || index || hash || data.size() != header_size || (nonce_ && nonce_ != nonce)) return false;
            nonce_ = nonce; compare(); return true;
        }
        if (nonce != nonce_ || !session) return false;
        if (!host_ && ((kind == message::chunk && index == 0) || kind == message::reject))
        {
            if (session_ && session != session_ && !(ready_ && requested_ && session_ != UINT64_MAX && session == session_ + 1)) return false;
            compare();
            if (mismatch_ != mismatch::none) { session_ = session; return true; }
            if (session_ && session != session_)
            {
                session_ = 0; index_ = 0; state_.clear(); digest_ = 0;
                complete_ = ready_ = requested_ = peer_requested_ = false;
            }
        }
        if (game != game_ || content != content_ || (session_ && session != session_)) return false;
        if (kind == message::leave && data.size() == header_size && session_)
        { closed_ = true; return true; }
        if (kind == message::rematch && ready_ && data.size() == header_size && size == state_.size() && hash == digest_)
        { peer_requested_ = true; return true; }
        if (host_)
        {
            if (data.size() != header_size || size != state_.size() || hash != digest_) return false;
            if (kind == message::acknowledge && index <= (state_.size() + chunk_size - 1) / chunk_size)
            {
                // ACKの飛び越しで未送信データを省かない。重複ACKは再送のため受理する。
                if (index == index_ + 1 && index * chunk_size < state_.size()) ++index_;
                return index == index_ || index == index_ + 1;
            }
            if (kind == message::ready && index == (state_.size() + chunk_size - 1) / chunk_size)
            { ready_ = true; return true; }
            return false;
        }
        if (kind == message::chunk)
        {
            if (!size || index >= (size + chunk_size - 1) / chunk_size ||
                data.size() != header_size + (std::min)(std::uint64_t(chunk_size), size - index * chunk_size)) return false;
            if (!session_)
            {
                if (index != 0) return false;
                session_ = session; digest_ = hash; state_.resize(static_cast<std::size_t>(size));
            }
            if (size != state_.size() || hash != digest_ || index > index_) return false;
            if (index == index_)
            {
                std::copy(data.begin() + header_size, data.end(), state_.begin() + index_ * chunk_size);
                ++index_;
                if (index_ == (size + chunk_size - 1) / chunk_size)
                {
                    if (digest(state_) != digest_) throw std::invalid_argument("room initial State checksum mismatch");
                    complete_ = true;
                }
            }
            return true;
        }
        if (kind == message::start && complete_ && data.size() == header_size && size == state_.size() && hash == digest_)
        { ready_ = true; return true; }
        return false;
    }
private:
    bool host_ = false, complete_ = false, ready_ = false, closed_ = false;
    std::uint64_t game_ = 0, nonce_ = 0, session_ = 0, digest_ = 0;
    std::uint32_t index_ = 0;
    bytes state_;
    manifest content_{};
    mismatch mismatch_ = mismatch::none;
    bool requested_ = false, peer_requested_ = false;
};
}
