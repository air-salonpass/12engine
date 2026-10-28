#include "../../Rendering/Networking/StateArchive.h"
#include "../../Rendering/Networking/ReplayEffects.h"
#include "../NetworkSampleGame.h"
#include "../../Rendering/Networking/UdpTransport.h"
#include "../../Rendering/Networking/RoomHandshake.h"
#include "../../Rendering/Networking/ConfirmedEffects.h"
#include "../../Rendering/Networking/ContentFingerprint.h"
#include "../../Rendering/Networking/LanDiscovery.h"
#include "../../Rendering/Networking/ConnectionAddress.h"
#include <chrono>
#include <iostream>
#include <thread>

using namespace engine_net;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct peer
{
    network_sample::game game;
    rollback_session simulation;
    peer_channel channel;
    static network_sample::game initial_game(std::uint32_t seed)
    { network_sample::game result; result.random = seed; return result; }
    explicit peer(unsigned player, std::uint64_t session = 1234, std::uint32_t seed = 0x13572468)
        : game(initial_game(seed)), simulation(player, 2, game.callbacks()), channel(simulation, 0x4e455453414d504c, session) {}
};
static bool complete(const peer& p, frame_id target)
{
    return p.simulation.next_frame() == target && p.simulation.remote_confirmed() >= target - 1 &&
        p.channel.peer_acknowledged() != no_frame && p.channel.peer_acknowledged() >= target - 1;
}
static void verify(const peer& p, frame_id target)
{
    require(p.channel.error().empty(), p.channel.error().c_str());
    require(p.game.save() == network_sample::reference(target).save(), "final state differs from offline replay");
}
static void test_history_and_wire()
{
    frame_history<bytes> history;
    history.store(0, {1}); history.store(256, {2});
    require(!history.find(0) && history.find(256) && *history.find(256) == bytes{2}, "history wrap confused frames");
    input_packet packet;
    packet.player = 1; packet.input_size = 2; packet.game = 2; packet.session = 0x0102030405060708ull;
    packet.records.push_back({42, {1, 0}});
    auto data = wire::encode(packet);
    require(data.size() == 46 && data[8] == 1 && data[15] == 8, "wire byte order invalid");
    input_packet decoded;
    require(wire::decode(data, decoded) && decoded.records[0].frame == 42 && decoded.session == packet.session,
        "packet round trip failed");
    for (std::size_t count = 0; count < data.size(); ++count)
        require(!wire::decode(bytes(data.begin(), data.begin() + count), decoded), "truncated packet accepted");
    data.push_back(0); require(!wire::decode(data, decoded), "trailing data accepted");
    data = wire::encode(packet); data[0] ^= 1; require(!wire::decode(data, decoded), "invalid magic accepted");
    data = wire::encode(packet); data[6] = 65; require(!wire::decode(data, decoded), "oversize input accepted");
    peer a(0), wrong_session(1, 999);
    require(!a.channel.receive(wrong_session.channel.outgoing()) && !a.channel.connected(), "wrong session connected");
    packet.player = 1; packet.input_size = 2; packet.game = 0x4e455453414d504c; packet.session = 1234;
    packet.acknowledged = 0; packet.records.clear();
    require(!a.channel.receive(wire::encode(packet)), "future acknowledgement accepted");
    packet.acknowledged = no_frame; packet.records.push_back({256, {0, 0}});
    require(!a.channel.receive(wire::encode(packet)) && a.simulation.remote_confirmed() == -1,
        "unbounded future frame accepted");
}
static void test_prediction_and_rewind()
{
    network_sample::game game;
    rollback_session simulation(0, 2, game.callbacks());
    require(!simulation.advance({1}), "wrong local input size accepted");
    for (frame_id frame = 0; frame < 8; ++frame)
        require(simulation.advance(network_sample::input(0, frame)), "prediction did not advance");
    require(!simulation.advance({0, 0}) && simulation.next_frame() == 8, "prediction limit ignored");
    for (frame_id frame = 0; frame < 8; ++frame) simulation.receive_remote(frame, network_sample::input(1, frame));
    require(simulation.reconcile(), "reconcile failed");
    require(game.save() == network_sample::reference(8).save(), "rollback did not restore previous buttons/RNG");
    require(simulation.statistics().rollbacks > 0 && game.replay_steps > 0, "rollback path never executed");
    require(simulation.receive_remote(0, network_sample::input(1, 0)) == input_result::duplicate, "duplicate not recognized");
    require(simulation.receive_remote(0, {2, 1}) == input_result::conflict && !simulation.error().empty(),
        "changed confirmed input accepted");
    network_sample::game bounded_game;
    rollback_session bounded(0, 2, bounded_game.callbacks(), 8, 1);
    require(!bounded.error().empty() && !bounded.can_advance(), "state size limit ignored");
}
static void test_faults()
{
    peer a(0), b(1);
    peer* peers[]{&a, &b};
    struct delayed { int due; unsigned destination; bytes packet; };
    std::vector<delayed> queue;
    std::uint32_t random = 0x12345;
    const auto next_random = [&]() { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
    constexpr frame_id target = 1200; // 履歴の循環も複数回通す
    for (int tick = 0; tick < 30000; ++tick)
    {
        // 受信順も反転させ 欠落だけでなく順序逆転と重複を通す
        for (std::size_t index = queue.size(); index > 0; --index)
            if (queue[index - 1].due <= tick)
            {
                auto item = std::move(queue[index - 1]);
                queue.erase(queue.begin() + index - 1);
                peers[item.destination]->channel.receive(item.packet);
            }
        for (unsigned player = 0; player < 2; ++player)
        {
            auto& p = *peers[player];
            require(p.channel.error().empty(), p.channel.error().c_str());
            if (next_random() % 5 != 0)
            {
                auto packet = p.channel.outgoing();
                queue.push_back({tick + 1 + static_cast<int>(next_random() % 12), 1 - player, packet});
                if (next_random() % 11 == 0) queue.push_back({tick + 16, 1 - player, std::move(packet)});
            }
            if (p.simulation.next_frame() < target && p.channel.can_advance())
                require(p.channel.advance(network_sample::input(player, p.simulation.next_frame())), "fault test advance failed");
        }
        if (complete(a, target) && complete(b, target))
        {
            verify(a, target); verify(b, target);
            require(a.simulation.statistics().rollbacks && b.simulation.statistics().rollbacks, "fault test did not roll back both peers");
            std::cout << "faults: 1200 frames, 20% simulated drop, reorder/duplicate, rollbacks="
                << a.simulation.statistics().rollbacks + b.simulation.statistics().rollbacks << '\n';
            return;
        }
    }
    throw std::runtime_error("fault test timed out");
}
static void test_desync()
{
    peer a(0), b(1, 1234, 0x13572469);
    // 同じ入力で結果が変わるゲーム実装を 意図的に作って検出経路を通す
    for (int tick = 0; tick < 100; ++tick)
    {
        a.channel.receive(b.channel.outgoing());
        b.channel.receive(a.channel.outgoing());
        if (!a.channel.error().empty() || !b.channel.error().empty())
        {
            require(a.channel.error().find("desynchronized") != std::string::npos ||
                b.channel.error().find("desynchronized") != std::string::npos, "wrong desync error");
            return;
        }
        if (a.channel.can_advance()) a.channel.advance(network_sample::input(0, a.simulation.next_frame()));
        if (b.channel.can_advance()) b.channel.advance(network_sample::input(1, b.simulation.next_frame()));
    }
    throw std::runtime_error("state checksum did not detect desync");
}
static void test_real_udp()
{
    udp_transport sockets[2];
    require(sockets[0].open({}) && sockets[1].open({}), "UDP bind failed");
    udp_transport duplicate;
    require(!duplicate.open(sockets[0].local_endpoint()), "occupied UDP port was reused");
    ipv4_endpoint empty_source; bytes empty_payload{1};
    require(sockets[0].receive(empty_source, empty_payload) == receive_status::empty && empty_payload.empty(),
        "empty UDP poll did not return immediately");
    require(!sockets[0].send(sockets[1].local_endpoint(), bytes(1201, 0)), "oversized UDP send accepted");
    peer a(0), b(1);
    peer* peers[]{&a, &b};
    constexpr frame_id target = 300;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline)
    {
        for (unsigned player = 0; player < 2; ++player)
        {
            auto& p = *peers[player];
            require(sockets[player].send(sockets[1 - player].local_endpoint(), p.channel.outgoing()), "UDP send failed");
            for (unsigned count = 0; count < 64; ++count)
            {
                ipv4_endpoint source; bytes payload;
                const auto status = sockets[player].receive(source, payload);
                if (status == receive_status::empty) break;
                require(status != receive_status::failed, "UDP receive failed");
                if (status == receive_status::received && source == sockets[1 - player].local_endpoint()) p.channel.receive(payload);
            }
            require(p.channel.error().empty(), p.channel.error().c_str());
            if (p.simulation.next_frame() < target && p.channel.can_advance())
                require(p.channel.advance(network_sample::input(player, p.simulation.next_frame())), "UDP simulation failed");
        }
        if (complete(a, target) && complete(b, target))
        {
            verify(a, target); verify(b, target);
            std::cout << "real UDP: 300 frames, both states match offline reference\n";
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("real UDP test timed out");
}
// 入れ子と例外で抑止状態が残らないことを確認する。音声機器は不要。
void test_replay_effect_gate()
{
    engine_net::replay_effect_gate gate;
    require(!gate.suppressed(), "normal output enabled");
    try
    {
        auto outer = gate.suppress();
        require(gate.suppressed(), "replay output suppressed");
        { auto inner = gate.suppress(); require(gate.suppressed(), "nested replay"); }
        require(gate.suppressed(), "outer replay still active");
        throw 7;
    }
    catch (int marker) { require(marker == 7, "test unwind marker"); }
    require(!gate.suppressed(), "exception restores normal output");
}

namespace archive_test
{
struct sample { int number = -7; float timer = 0.25f; bool flag = true; std::string text = "state"; std::vector<int> values{1,2,3}; };
template<class A> void state_fields(A& a, sample& v) { a(v.number, v.timer, v.flag, v.text, v.values); }
}
static void test_state_archive()
{
    using namespace engine_net::state_archive;
    archive_test::sample state; writer out; out(state); const auto data = std::move(out).finish();
    archive_test::sample restored; restored.number = 0; reader in(data); in(restored); in.finish();
    writer second; second(restored); require(std::move(second).finish() == data, "canonical state archive roundtrip");
    for (int fault = 0; fault < 4; ++fault)
    {
        auto bad = data;
        if (fault == 0) bad.pop_back();
        if (fault == 1) bad.push_back(0);
        if (fault == 2) bad[12] = 2;
        if (fault == 3) for (unsigned i = 13; i < 21; ++i) bad[i] = 255;
        bool rejected = false;
        try { reader input(bad); archive_test::sample value; input(value); input.finish(); }
        catch (const std::exception&) { rejected = true; }
        require(rejected, "malformed state archive rejected");
    }
    bool rejected = false;
    try { writer huge; huge(std::string(item_limit + 1, 'x')); }
    catch (const std::length_error&) { rejected = true; }
    require(rejected, "archive string limit");
    rejected = false;
    try { writer nan; nan((std::numeric_limits<float>::infinity)()); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "nonfinite archive field rejected");
    network_sample::game game; auto callbacks = game.callbacks();
    callbacks.predict_remote = [](frame_id, const bytes&) { return bytes{0}; };
    rollback_session invalid_prediction(0, 2, callbacks);
    require(!invalid_prediction.advance(network_sample::input(0, 0)) && !invalid_prediction.error().empty(), "invalid predictor size stops session");
}
static void test_custom_state_checksum()
{
    network_sample::game game; auto callbacks = game.callbacks();
    unsigned calls = 0;
    callbacks.state_checksum = [&](const bytes&) { ++calls; return std::uint64_t{123456789}; };
    rollback_session simulation(0, 2, callbacks);
    require(!simulation.confirmed_hash(0), "unconfirmed custom hash unavailable");
    simulation.receive_remote(0, network_sample::input(1, 0));
    require(simulation.advance(network_sample::input(0, 0)), "custom checksum advance");
    require(simulation.confirmed_hash(0) == std::optional<std::uint64_t>{123456789} && calls == 1,
        "custom checksum called only for confirmed State");
}
static void test_room_handshake()
{
    using room = engine_net::room_handshake;
    bytes initial(27754);
    for (std::size_t i = 0; i < initial.size(); ++i) initial[i] = static_cast<std::uint8_t>(i * 37);
    room host(true, 123, 456, initial), guest(false, 123, 789);
    auto hello = guest.outgoing();
    auto wrong = hello; wrong[29] ^= 1;
    room wrong_game(true, 123, 456, initial);
    require(wrong_game.receive(wrong) && wrong_game.incompatibility() == room::mismatch::game, "wrong room game reported");
    require(host.receive(hello), "room hello accepted");
    auto other = hello; other[13] ^= 1;
    require(!host.receive(other), "another join nonce rejected");
    auto chunk = host.outgoing();
    auto truncated = chunk; truncated.pop_back();
    require(!guest.receive(truncated), "truncated State chunk rejected");
    require(guest.receive(chunk) && guest.receive(chunk), "duplicate State chunk accepted");
    // 制御・ACK・STARTの欠落を含む再送。最終Stateの全バイト一致を確認する。
    for (unsigned tick = 0; tick < 1000 && (!host.ready() || !guest.ready()); ++tick)
    {
        if (tick % 5 != 0) guest.receive(host.outgoing());
        if (tick % 7 != 0) host.receive(guest.outgoing());
    }
    require(host.ready() && guest.ready() && guest.initial_state() == initial && host.session() == guest.session(),
        "room State transfer with dropped packets");
    const auto stale = host.outgoing();
    guest.request_rematch(); host.receive(guest.outgoing());
    bool refused = false; try { host.restart(initial); } catch (const std::logic_error&) { refused = true; }
    require(refused, "rematch requires both players");
    host.request_rematch();
    initial[42] ^= 1;
    host.restart(initial);
    for (unsigned tick = 0; tick < 1000 && (!host.ready() || !guest.ready()); ++tick)
    {
        if (tick % 5 != 0) guest.receive(host.outgoing());
        if (tick % 7 != 0) host.receive(guest.outgoing());
    }
    require(host.ready() && guest.ready() && guest.session() == 457 && guest.initial_state() == initial,
        "agreed rematch State retransmission");
    require(!guest.receive(stale), "old match packet rejected after rematch");
    require(guest.receive(host.outgoing(true)) && guest.closed(), "room leave");
    room oversized(false, 123, 789);
    auto corrupt = chunk;
    // sizeの4バイトは30..33。上限を超える宣言で確保させない。
    corrupt[30] = 0x7f;
    require(!oversized.receive(corrupt) && oversized.initial_state().empty(), "room allocation bound");
    for (unsigned field = 0; field < 3; ++field)
    {
        room::manifest local{2, 100, 200}, remote = local; ++remote[field];
        room a(true, 123, 456, initial, local), b(false, 123, 789, {}, remote);
        require(a.receive(b.outgoing()) && a.incompatibility() == static_cast<room::mismatch>(field + 2), "manifest difference classified");
        require(b.receive(a.outgoing()) && b.incompatibility() == static_cast<room::mismatch>(field + 2) && b.initial_state().empty(),
            "mismatch rejected before State allocation");
    }
    confirmed_effects<int> effects;
    effects.replace(0, {1}); effects.replace(0, {2}); effects.replace(1, {3}); effects.replace(1, {});
    std::vector<int> emitted; effects.commit_through(1, [&](int e) { emitted.push_back(e); });
    effects.commit_through(1, [&](int e) { emitted.push_back(e); });
    require(emitted == std::vector<int>{2}, "confirmed effects replace, remove and deduplicate");
    content_fingerprint before, after; before.append("abc", 3); after.append("abd", 3);
    require(before.value() != after.value(), "content mutation fingerprint");
}
static void test_lan_discovery()
{
    ipv4_endpoint address;
    require(parse_connection_address("192.168.1.10:27020",address) && address.address==std::array<std::uint8_t,4>{192,168,1,10} && address.port==27020,"LAN connection address");
    require(connection_address(address)=="192.168.1.10:27020","connection address round trip");
    const auto unchanged=address;
    for (const char* invalid:{"100.1.2.3","100.1.2.3:0","100.1.2.3:65536","100.1.2.3:999999999999999999999999","100.1.2.3:-1","100.1.2.3:27020:1","256.1.2.3:20","0.0.0.0:20","255.255.255.255:20","224.0.0.1:20"})
        require(!parse_connection_address(invalid,address) && address==unchanged,"reject malformed endpoint without mutation");
    require(parse_connection_address("127.0.0.1:65535",address),"loopback endpoint for tests");
    for (const auto& local:local_ipv4_addresses()) require(local.endpoint.port==0 && local.endpoint.address[0]!=127,"local adapter enumeration");
    using discovery = lan_discovery;
    std::uint64_t nonce=0; std::uint16_t port=0;
    auto data=discovery::packet(true,123,456,27020);
    require(discovery::decode(data,true,123,nonce,port) && nonce==456 && port==27020,"discovery response decode");
    require(!discovery::decode(data,true,124,nonce,port),"discovery wrong game");
    data.push_back(0); require(!discovery::decode(data,true,123,nonce,port),"discovery trailing bytes");
    require(!discovery::decode(discovery::packet(true,123,456,0),true,123,nonce,port),"discovery invalid port");
    require(!discovery::decode(discovery::packet(false,123,0,0),false,123,nonce,port),"discovery invalid nonce");
    discovery server, browser;
    require(server.advertise(123,27020),"discovery bind host");
    require(browser.search(123),"discovery open ephemeral browser");
    const auto deadline=discovery::clock::now()+std::chrono::seconds(2);
    while (browser.rooms().empty() && discovery::clock::now()<deadline)
    { browser.update(); server.update(); browser.update(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    require(!browser.rooms().empty(),"real UDP discovery response");
    for (int i=0;i<5;++i) { server.update(); browser.update(); }
    for (const auto& r:browser.rooms()) require(r.endpoint.port==27020,"discovery advertised game port");
    // 再検索では前回の一覧を破棄し、別ゲームの応答を拾わない。
    require(browser.search(124) && browser.rooms().empty(),"discovery clear stale list");
    for (int i=0;i<30;++i) { browser.update(); server.update(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    require(browser.rooms().empty(),"real UDP discovery different game ignored");
    server.close(); browser.close();
}
int main()
{
    try
    {
        test_lan_discovery(); test_room_handshake(); test_custom_state_checksum(); test_state_archive(); test_replay_effect_gate(); test_history_and_wire(); test_prediction_and_rewind(); test_faults(); test_desync(); test_real_udp();
        std::cout << "network tests passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
