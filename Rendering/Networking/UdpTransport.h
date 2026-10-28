#pragma once
#include "RollbackSession.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace engine_net
{
struct ipv4_endpoint
{
    std::array<std::uint8_t, 4> address{127, 0, 0, 1};
    std::uint16_t port = 0;
    bool operator==(const ipv4_endpoint& other) const { return address == other.address && port == other.port; }
    bool operator!=(const ipv4_endpoint& other) const { return !(*this == other); }
    static bool parse(const std::string& numeric_address, std::uint16_t port, ipv4_endpoint& out);
};
enum class receive_status { empty, received, discarded, failed };
struct local_ipv4_address
{
    ipv4_endpoint endpoint;
    std::string adapter_name;
};
// PC内の有効なIPv4を表示するための列挙。ネットワーク設定の変更は行わない。
std::vector<local_ipv4_address> local_ipv4_addresses();
// Windows用の非ブロッキングUDP 所有と破棄はこのクラスで完結させる
// ゲームの状態やEntityには触れない メインスレッドからpollし受信結果を同期層へ渡す
class udp_transport
{
public:
    static constexpr std::size_t max_datagram = 1200;
    udp_transport();
    ~udp_transport();
    udp_transport(const udp_transport&) = delete;
    udp_transport& operator=(const udp_transport&) = delete;
    bool open(const ipv4_endpoint& local);
    // LAN検索を行うソケットだけ明示的に有効化する。通常の対戦通信には不要。
    bool enable_broadcast();
    void close();
    bool send(const ipv4_endpoint& destination, const bytes& payload);
    receive_status receive(ipv4_endpoint& source, bytes& payload);
    ipv4_endpoint local_endpoint() const;
    int last_error() const;
private:
    struct implementation;
    std::unique_ptr<implementation> impl_;
};
}
