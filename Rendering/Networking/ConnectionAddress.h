#pragma once
#include "UdpTransport.h"

namespace engine_net
{
// 人が相手へ渡すIPv4:port。公開の部屋ID・認証トークン・NAT越えの情報ではない。
inline std::string connection_address(const ipv4_endpoint& endpoint)
{
    const auto& a=endpoint.address;
    return std::to_string(a[0])+"."+std::to_string(a[1])+"."+std::to_string(a[2])+"."+
        std::to_string(a[3])+":"+std::to_string(endpoint.port);
}
inline bool parse_connection_address(const std::string& text,ipv4_endpoint& out)
{
    const auto colon=text.find(':');
    if (colon==std::string::npos || colon==0 || colon+1==text.size()) return false;
    unsigned port=0;
    for (std::size_t i=colon+1;i<text.size();++i)
    {
        if (text[i]<'0' || text[i]>'9' || port>6553) return false;
        port=port*10+unsigned(text[i]-'0'); if (port>65535) return false;
    }
    ipv4_endpoint candidate;
    if (!port || !ipv4_endpoint::parse(text.substr(0,colon),static_cast<std::uint16_t>(port),candidate)) return false;
    // 放送・マルチキャスト・未指定宛への誤接続を防ぐ。相手の識別と到達可能性は接続時に別途確認する。
    if (!candidate.address[0] || candidate.address[0]>=224) return false;
    out=candidate; return true;
}
}
