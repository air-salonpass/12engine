#pragma once
#include "UdpTransport.h"
#include <algorithm>
#include <chrono>
#include <random>
#include <vector>

namespace engine_net
{
// 部屋検索は開始Stateや戦闘入力と別のUDP通信。Scene/Prefabには保存しない。
// ホストは待機中だけ27019で検索へ応答し、参加側はOSが選んだ一時ポートを使う。
// IPは応答の送信元から取得する。広告内の任意のIPを信用して接続しない。
class lan_discovery
{
public:
    using clock = std::chrono::steady_clock;
    static constexpr std::uint16_t discovery_port = 27019;
    struct room { ipv4_endpoint endpoint; clock::time_point seen; };
    static bytes packet(bool reply, std::uint64_t game, std::uint64_t nonce, std::uint16_t port)
    {
        bytes data(24, 0); data[0]='J'; data[1]='E'; data[2]='D'; data[3]=1; data[4]=reply ? 1 : 0;
        for (int i=0; i<8; ++i) { data[6+i]=std::uint8_t(game >> (i*8)); data[14+i]=std::uint8_t(nonce >> (i*8)); }
        data[22]=std::uint8_t(port); data[23]=std::uint8_t(port >> 8); return data;
    }
    static bool decode(const bytes& data, bool reply, std::uint64_t game, std::uint64_t& nonce, std::uint16_t& port)
    {
        if (data.size()!=24 || data[0]!='J' || data[1]!='E' || data[2]!='D' || data[3]!=1 ||
            data[4]!=(reply ? 1 : 0) || data[5]!=0) return false;
        std::uint64_t id=0; nonce=0;
        for (int i=0; i<8; ++i) { id |= std::uint64_t(data[6+i]) << (i*8); nonce |= std::uint64_t(data[14+i]) << (i*8); }
        port=std::uint16_t(data[22] | (std::uint16_t(data[23]) << 8));
        return id==game && nonce!=0 && (reply ? port!=0 : port==0);
    }
    void close() { socket_.close(); active_=false; }
    bool advertise(std::uint64_t game, std::uint16_t port)
    {
        close(); rooms_.clear(); error_.clear(); server_=true; game_=game; port_=port;
        active_=port && socket_.open({{0,0,0,0}, discovery_port});
        if (!active_) error_="部屋の自動案内を開始できません。詳細設定のIP接続は利用できます。";
        return active_;
    }
    bool search(std::uint64_t game)
    {
        close(); rooms_.clear(); error_.clear(); server_=false; game_=game;
        std::random_device random; nonce_=(std::uint64_t(random()) << 32) ^ random(); if (!nonce_) nonce_=1;
        active_=socket_.open({{0,0,0,0},0}) && socket_.enable_broadcast();
        if (!active_) { socket_.close(); error_="部屋検索用のUDPを開けません。詳細設定から接続してください。"; }
        began_=clock::now(); sent_={}; return active_;
    }
    void update()
    {
        if (!active_) return;
        const auto now=clock::now();
        if (!server_ && now-began_>=std::chrono::seconds(5)) { close(); return; }
        // 回線上の大量パケットでも1描画を占有しない。応答数と一覧の件数も制限する。
        int replies=0;
        for (int i=0; i<32; ++i)
        {
            ipv4_endpoint source; bytes data;
            const auto status=socket_.receive(source,data);
            if (status==receive_status::empty) break;
            if (status==receive_status::failed) { error_="部屋検索の受信に失敗しました。"; close(); break; }
            if (status!=receive_status::received) continue;
            std::uint64_t nonce; std::uint16_t port;
            if (!decode(data,!server_,game_,nonce,port)) continue;
            if (server_)
            {
                if (replies++<8) socket_.send(source,packet(true,game_,nonce,port_));
            }
            else if (nonce==nonce_)
            {
                source.port=port;
                const auto found=std::find_if(rooms_.begin(),rooms_.end(),[&](const room& r){ return r.endpoint==source; });
                if (found!=rooms_.end()) found->seen=now;
                else if (rooms_.size()<32) rooms_.push_back({source,now});
            }
        }
        if (!server_ && active_ && now-sent_>=std::chrono::milliseconds(500))
        {
            const auto query=packet(false,game_,nonce_,0);
            const bool sent=socket_.send({{255,255,255,255},discovery_port},query);
            socket_.send({{127,0,0,1},discovery_port},query); // 同一PCの確認にも対応。
            if (!sent) error_="LANへの検索送信に失敗しました。詳細設定のIP接続も試してください。";
            sent_=now;
        }
    }
    bool active() const { return active_; }
    const std::vector<room>& rooms() const { return rooms_; }
    const std::string& error() const { return error_; }
private:
    udp_transport socket_;
    bool active_=false, server_=false;
    std::uint64_t game_=0, nonce_=0;
    std::uint16_t port_=0;
    clock::time_point began_{}, sent_{};
    std::vector<room> rooms_;
    std::string error_;
};
}
