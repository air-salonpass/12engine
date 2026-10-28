#include "../NetworkSampleGame.h"
#include "../../Rendering/Networking/UdpTransport.h"
#include <chrono>
#include <iostream>
#include <thread>

using namespace engine_net;
int main(int argc, char** argv)
{
    try
    {
        unsigned player = 2, local_port = 0, peer_port = 0, frames = 300;
        std::uint64_t session_number = 0;
        std::string local_address = "127.0.0.1", remote_address = "127.0.0.1";
        bool fast = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string option = argv[i];
            if (option == "--fast") { fast = true; continue; }
            if (option == "--help")
            {
                std::cout << "NetworkDemo --player 0|1 --local-port N --peer-port N --session N "
                    "[--bind IPv4] [--peer IPv4] [--frames 300] [--fast]\n";
                return 0;
            }
            if (i + 1 >= argc) throw std::runtime_error("missing option value");
            const std::string value = argv[++i];
            if (option == "--bind") local_address = value;
            else if (option == "--peer") remote_address = value;
            else
            {
                std::size_t consumed = 0;
                const auto number = std::stoull(value, &consumed);
                if (consumed != value.size()) throw std::runtime_error("invalid numeric option");
                if (option == "--session") session_number = number;
                else if (option == "--player" && number <= 1) player = static_cast<unsigned>(number);
                else if (option == "--local-port" && number > 0 && number <= 65535) local_port = static_cast<unsigned>(number);
                else if (option == "--peer-port" && number > 0 && number <= 65535) peer_port = static_cast<unsigned>(number);
                else if (option == "--frames" && number > 0 && number <= 36000) frames = static_cast<unsigned>(number);
                else throw std::runtime_error("unknown option or out-of-range value");
            }
        }
        if (player > 1 || local_port == 0 || peer_port == 0 || session_number == 0)
            throw std::runtime_error("player, ports and nonzero session are required; use --help");
        ipv4_endpoint local, remote;
        if (!ipv4_endpoint::parse(local_address, static_cast<std::uint16_t>(local_port), local) ||
            !ipv4_endpoint::parse(remote_address, static_cast<std::uint16_t>(peer_port), remote))
            throw std::runtime_error("numeric IPv4 address required");
        udp_transport transport;
        if (!transport.open(local)) throw std::runtime_error("UDP bind failed: " + std::to_string(transport.last_error()));
        network_sample::game game;
        rollback_session simulation(player, 2, game.callbacks());
        peer_channel channel(simulation, 0x4e455453414d504c, session_number);
        const auto started = std::chrono::steady_clock::now();
        const auto deadline = started + std::chrono::seconds(frames / 60 + 15);
        auto next_step = started, next_send = started;
        bool simulation_clock_started = false;
        std::optional<std::chrono::steady_clock::time_point> finished;
        std::cout << "Waiting for peer; local player=" << player << " port=" << local_port << '\n';
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto now = std::chrono::steady_clock::now();
            for (unsigned count = 0; count < 64; ++count)
            {
                ipv4_endpoint source; bytes payload;
                const auto status = transport.receive(source, payload);
                if (status == receive_status::empty) break;
                if (status == receive_status::failed) throw std::runtime_error("UDP receive failed");
                if (status == receive_status::received && source == remote) channel.receive(payload);
            }
            if (!channel.error().empty()) throw std::runtime_error(channel.error());
            if (channel.connected() && !simulation_clock_started)
            {
                next_step = now;
                simulation_clock_started = true;
            }
            if (!channel.can_advance() && simulation.next_frame() < frames) next_step = now;
            if (now >= next_send)
            {
                transport.send(remote, channel.outgoing());
                next_send = now + std::chrono::milliseconds(5);
            }
            if (simulation.next_frame() < frames && channel.can_advance() && (fast || now >= next_step))
            {
                // このサンプルでは自動操作 実ゲームでは確定フレーム分の操作状態をここへ渡す
                if (!channel.advance(network_sample::input(player, simulation.next_frame())))
                    throw std::runtime_error("simulation advance failed");
                // 現在時刻から予約し直すとSleepの丸め誤差が積み重なるため 期限を累積する
                next_step += std::chrono::microseconds(16667);
                if (now - next_step > std::chrono::milliseconds(100)) next_step = now;
            }
            if (simulation.next_frame() == frames && simulation.remote_confirmed() >= frames - 1 &&
                channel.peer_acknowledged() != no_frame && channel.peer_acknowledged() >= frames - 1)
            {
                if (!finished) finished = now;
                // 最後のACKを相手が受け取れるよう 完了後もしばらく送受信する
                if (now - *finished >= std::chrono::milliseconds(500))
                {
                    if (game.save() != network_sample::reference(frames).save())
                        throw std::runtime_error("final state differs from offline reference");
                    std::cout << "SYNC OK frames=" << frames << " hash=" << *simulation.confirmed_hash(frames - 1)
                        << " rollbacks=" << simulation.statistics().rollbacks
                        << " replayed=" << simulation.statistics().replayed_frames
                        << " positions=" << game.position[0] << ',' << game.position[1]
                        << " hp=" << game.health[0] << ',' << game.health[1] << '\n';
                    return 0;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw std::runtime_error("peer connection or input exchange timed out");
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
