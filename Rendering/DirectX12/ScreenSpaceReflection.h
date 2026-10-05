#pragma once

#include "Renderer.h"
#include "SceneRenderTarget.h"

#include <DirectXMath.h>
#include <memory>
#include <wrl.h>

// Scene Color/Depth/Normalを画面空間でレイマーチし、BackBufferへ合成するSSRパス。
// 新しい画面効果も「入力Render TargetをSRV化→専用Passで出力→次用途へBarrier」の形で追加する。
// 同じResourceをRTVとSRVとして同時にBindingしないこと。画面外の物体が映らないのはSSRの仕様。
class screen_space_reflection12
{
public:
	bool initialize(std::shared_ptr<directx12_device> device);
	void begin_frame(UINT frame_index);
	void render(ID3D12GraphicsCommandList* command_list, const scene_render_target12& scene,
		const DirectX::XMFLOAT4X4& projection, float strength, float max_distance,
		float thickness, std::uint32_t steps, float exposure = 1.0f);

private:
	struct constants
	{
		DirectX::XMFLOAT4X4 projection{};
		DirectX::XMFLOAT4X4 inverse_projection{};
		DirectX::XMFLOAT4 parameters{}; // x:強さ y:距離 z:厚み w:ステップ数
		DirectX::XMFLOAT4 display_parameters{ 1, 0, 0, 0 }; // x:露出
	};

	std::shared_ptr<directx12_device> device_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state_;
	Microsoft::WRL::ComPtr<ID3D12Resource> constants_buffer_;
	std::byte* mapped_constants_ = nullptr;
	UINT frame_index_ = 0;
};
