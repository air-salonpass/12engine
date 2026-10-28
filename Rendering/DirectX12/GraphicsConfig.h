#pragma once

#include <cstdint>
#include <dxgi1_6.h>

// DirectX 12基盤全体で共有する固定設定。
// 値を各クラスへ直接書くと、SwapChainやDescriptor Heapの前提が食い違うため、
// 描画基盤に関する容量はこのファイルだけで変更する。
namespace graphics_config12
{
	inline constexpr std::uint32_t frame_count = 2;
	// CPUが表示より先へ進み過ぎないよう、SwapChainから表示可能通知を受け取る。
	// 2フレームならCPUとGPUを並列化しつつ、入力遅延を増やし過ぎない。
	inline constexpr std::uint32_t maximum_frame_latency = 2;
	inline constexpr std::uint32_t frame_latency_timeout_ms = 100;
	inline constexpr std::uint32_t swap_chain_flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
	inline constexpr std::uint32_t shader_descriptor_count = 65536;
	inline constexpr std::uint32_t sampler_descriptor_count = 2048;
	inline constexpr std::uint32_t rtv_descriptor_count = 2048;
	inline constexpr std::uint32_t dsv_descriptor_count = 1024;
}
