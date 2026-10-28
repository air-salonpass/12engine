#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "UdpTransport.h"
#include <cstring>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace engine_net
{
namespace
{
sockaddr_in native_address(const ipv4_endpoint& endpoint)
{
    sockaddr_in value{};
    value.sin_family = AF_INET;
    value.sin_port = htons(endpoint.port);
    std::memcpy(&value.sin_addr, endpoint.address.data(), 4);
    return value;
}
ipv4_endpoint engine_address(const sockaddr_in& value)
{
    ipv4_endpoint endpoint;
    std::memcpy(endpoint.address.data(), &value.sin_addr, 4);
    endpoint.port = ntohs(value.sin_port);
    return endpoint;
}
}
struct udp_transport::implementation
{
    SOCKET socket = INVALID_SOCKET;
    bool started = false;
    int error = 0;
};
bool ipv4_endpoint::parse(const std::string& text, std::uint16_t port, ipv4_endpoint& out)
{
    in_addr parsed{};
    if (InetPtonA(AF_INET, text.c_str(), &parsed) != 1) return false;
    std::memcpy(out.address.data(), &parsed, 4);
    out.port = port;
    return true;
}
udp_transport::udp_transport() : impl_(std::make_unique<implementation>()) {}
udp_transport::~udp_transport() { close(); }
void udp_transport::close()
{
    if (impl_->socket != INVALID_SOCKET) closesocket(impl_->socket);
    impl_->socket = INVALID_SOCKET;
    if (impl_->started) WSACleanup();
    impl_->started = false;
}
bool udp_transport::open(const ipv4_endpoint& local)
{
    close();
    impl_->error = 0;
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0) { impl_->error = result; return false; }
    impl_->started = true;
    impl_->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->socket == INVALID_SOCKET) { impl_->error = WSAGetLastError(); close(); return false; }
    const auto address = native_address(local);
    u_long nonblocking = 1;
    // 同じポートを他のプロセスと曖昧に共有しない
    BOOL exclusive = TRUE;
    if (setsockopt(impl_->socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR ||
        bind(impl_->socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        ioctlsocket(impl_->socket, FIONBIO, &nonblocking) == SOCKET_ERROR)
    {
        impl_->error = WSAGetLastError(); close(); return false;
    }
    return true;
}
bool udp_transport::enable_broadcast()
{
    BOOL enabled = TRUE;
    if (impl_->socket == INVALID_SOCKET || setsockopt(impl_->socket, SOL_SOCKET, SO_BROADCAST,
        reinterpret_cast<const char*>(&enabled), sizeof(enabled)) == SOCKET_ERROR)
    { impl_->error = WSAGetLastError(); return false; }
    return true;
}
bool udp_transport::send(const ipv4_endpoint& destination, const bytes& payload)
{
    if (impl_->socket == INVALID_SOCKET || payload.empty() || payload.size() > max_datagram || destination.port == 0)
    { impl_->error = WSAEINVAL; return false; }
    const auto address = native_address(destination);
    const int result = sendto(impl_->socket, reinterpret_cast<const char*>(payload.data()),
        static_cast<int>(payload.size()), 0, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (result == SOCKET_ERROR) { impl_->error = WSAGetLastError(); return false; }
    impl_->error = 0;
    return result == payload.size();
}
receive_status udp_transport::receive(ipv4_endpoint& source, bytes& payload)
{
    payload.clear();
    if (impl_->socket == INVALID_SOCKET) { impl_->error = WSAENOTSOCK; return receive_status::failed; }
    std::array<char, max_datagram> buffer{};
    sockaddr_in address{};
    int length = sizeof(address);
    const int count = recvfrom(impl_->socket, buffer.data(), static_cast<int>(buffer.size()), 0,
        reinterpret_cast<sockaddr*>(&address), &length);
    if (count == SOCKET_ERROR)
    {
        impl_->error = WSAGetLastError();
        if (impl_->error == WSAEWOULDBLOCK) return receive_status::empty;
        // 大きすぎるデータとUDPの到達不能通知は破棄し 次のフレームも受信を続ける
        if (impl_->error == WSAEMSGSIZE || impl_->error == WSAECONNRESET) return receive_status::discarded;
        return receive_status::failed;
    }
    impl_->error = 0;
    source = engine_address(address);
    payload.assign(buffer.begin(), buffer.begin() + count);
    return receive_status::received;
}
ipv4_endpoint udp_transport::local_endpoint() const
{
    sockaddr_in address{};
    int size = sizeof(address);
    if (impl_->socket == INVALID_SOCKET || getsockname(impl_->socket, reinterpret_cast<sockaddr*>(&address), &size) != 0)
        return {};
    return engine_address(address);
}
int udp_transport::last_error() const { return impl_->error; }
std::vector<local_ipv4_address> local_ipv4_addresses()
{
    ULONG size=16384;
    std::vector<unsigned char> buffer(size);
    ULONG status=ERROR_BUFFER_OVERFLOW;
    for (int attempt=0;attempt<3 && status==ERROR_BUFFER_OVERFLOW;++attempt)
    {
        buffer.resize(size);
        status=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&size);
    }
    std::vector<local_ipv4_address> result;
    if (status!=NO_ERROR) return result;
    const auto utf8=[](const wchar_t* text)
    {
        if (!text) return std::string{};
        const int size=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
        std::string out(size>0 ? size : 0,'\0');
        if (size>0) { WideCharToMultiByte(CP_UTF8,0,text,-1,out.data(),size,nullptr,nullptr); out.pop_back(); }
        return out;
    };
    for (auto* adapter=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());adapter;adapter=adapter->Next)
    {
        if (adapter->OperStatus!=IfOperStatusUp || adapter->IfType==IF_TYPE_SOFTWARE_LOOPBACK) continue;
        const auto name=utf8(adapter->FriendlyName);
        for (auto* address=adapter->FirstUnicastAddress;address;address=address->Next)
        {
            if (!address->Address.lpSockaddr || address->Address.lpSockaddr->sa_family!=AF_INET) continue;
            auto endpoint=engine_address(*reinterpret_cast<sockaddr_in*>(address->Address.lpSockaddr)); endpoint.port=0;
            if (!endpoint.address[0] || endpoint.address[0]==127 || (endpoint.address[0]==169 && endpoint.address[1]==254)) continue;
            bool duplicate=false; for (const auto& old:result) if (old.endpoint==endpoint) duplicate=true;
            if (!duplicate) result.push_back({endpoint,name});
        }
    }
    return result;
}
}
