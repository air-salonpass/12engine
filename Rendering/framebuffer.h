#pragma once

#include <cstdint>
#include <d3d11.h>
#include <wrl.h>

// UNIT32: シーンを画面へ直接出さず、一度テクスチャへ描画するためのフレームバッファ。
class framebuffer
{
public:
	framebuffer(ID3D11Device* device, uint32_t width, uint32_t height);
	virtual ~framebuffer() = default;

	// 色はHDR処理用のRGBA16F、深度は後から参照できるSRV付きで保持する。
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_view;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_stencil_view;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shader_resource_views[2];
	D3D11_VIEWPORT viewport{};

	void clear(
		ID3D11DeviceContext* immediate_context,
		float r = 0.0f,
		float g = 0.0f,
		float b = 0.0f,
		float a = 1.0f,
		float depth = 1.0f);
	void activate(ID3D11DeviceContext* immediate_context);
	void deactivate(ID3D11DeviceContext* immediate_context);

private:
	// activate前の画面用描画先とビューポートを保存し、deactivate時に元へ戻す。
	UINT viewport_count{ D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE };
	D3D11_VIEWPORT cached_viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> cached_render_target_view;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> cached_depth_stencil_view;
};
