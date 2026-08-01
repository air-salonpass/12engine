#pragma once

#include <cstdint>
#include <d3d11.h>
#include <wrl.h>

// 頂点バッファを使わず、SV_VertexIDだけで画面全体を覆う四角形を描画する。
class fullscreen_quad
{
public:
	explicit fullscreen_quad(ID3D11Device* device);
	virtual ~fullscreen_quad() = default;

	void blit(
		ID3D11DeviceContext* immediate_context,
		ID3D11ShaderResourceView* const* shader_resource_views,
		uint32_t start_slot,
		uint32_t num_views,
		ID3D11PixelShader* replaced_pixel_shader = nullptr);

private:
	Microsoft::WRL::ComPtr<ID3D11VertexShader> embedded_vertex_shader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> embedded_pixel_shader;
};
