#include "framebuffer.h"
#include "misc.h"

framebuffer::framebuffer(ID3D11Device* device, uint32_t width, uint32_t height)
{
	HRESULT hr{ S_OK };

	// ブルーム合成前の1.0を超える明るさも失わないよう、HDRのRGBA16Fで色バッファを作る。
	Microsoft::WRL::ComPtr<ID3D11Texture2D> render_target_buffer;
	D3D11_TEXTURE2D_DESC texture2d_desc{};
	texture2d_desc.Width = width;
	texture2d_desc.Height = height;
	texture2d_desc.MipLevels = 1;
	texture2d_desc.ArraySize = 1;
	texture2d_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	texture2d_desc.SampleDesc.Count = 1;
	texture2d_desc.SampleDesc.Quality = 0;
	texture2d_desc.Usage = D3D11_USAGE_DEFAULT;
	texture2d_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	hr = device->CreateTexture2D(&texture2d_desc, nullptr, render_target_buffer.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	D3D11_RENDER_TARGET_VIEW_DESC render_target_view_desc{};
	render_target_view_desc.Format = texture2d_desc.Format;
	render_target_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	hr = device->CreateRenderTargetView(
		render_target_buffer.Get(),
		&render_target_view_desc,
		render_target_view.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc{};
	shader_resource_view_desc.Format = texture2d_desc.Format;
	shader_resource_view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	shader_resource_view_desc.Texture2D.MipLevels = 1;
	hr = device->CreateShaderResourceView(
		render_target_buffer.Get(),
		&shader_resource_view_desc,
		shader_resource_views[0].GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	// 深度テクスチャはDSVとSRVで形式を変えて参照するため、TYPELESS形式で作成する。
	Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_stencil_buffer;
	texture2d_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	texture2d_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	hr = device->CreateTexture2D(&texture2d_desc, nullptr, depth_stencil_buffer.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	D3D11_DEPTH_STENCIL_VIEW_DESC depth_stencil_view_desc{};
	depth_stencil_view_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depth_stencil_view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	hr = device->CreateDepthStencilView(
		depth_stencil_buffer.Get(),
		&depth_stencil_view_desc,
		depth_stencil_view.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	shader_resource_view_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	shader_resource_view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	hr = device->CreateShaderResourceView(
		depth_stencil_buffer.Get(),
		&shader_resource_view_desc,
		shader_resource_views[1].GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	viewport.Width = static_cast<float>(width);
	viewport.Height = static_cast<float>(height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
}

void framebuffer::clear(
	ID3D11DeviceContext* immediate_context,
	float r,
	float g,
	float b,
	float a,
	float depth)
{
	const float color[4]{ r, g, b, a };
	immediate_context->ClearRenderTargetView(render_target_view.Get(), color);
	immediate_context->ClearDepthStencilView(
		depth_stencil_view.Get(),
		D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
		depth,
		0);
}

void framebuffer::activate(ID3D11DeviceContext* immediate_context)
{
	// 現在の画面用状態を退避してから、描画先をオフスクリーンへ切り替える。
	viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	immediate_context->RSGetViewports(&viewport_count, cached_viewports);
	immediate_context->OMGetRenderTargets(
		1,
		cached_render_target_view.ReleaseAndGetAddressOf(),
		cached_depth_stencil_view.ReleaseAndGetAddressOf());

	immediate_context->RSSetViewports(1, &viewport);
	ID3D11RenderTargetView* render_target = render_target_view.Get();
	immediate_context->OMSetRenderTargets(1, &render_target, depth_stencil_view.Get());
}

void framebuffer::deactivate(ID3D11DeviceContext* immediate_context)
{
	// オフスクリーン描画前に使っていた描画先とビューポートへ復帰する。
	immediate_context->RSSetViewports(viewport_count, cached_viewports);
	ID3D11RenderTargetView* render_target = cached_render_target_view.Get();
	immediate_context->OMSetRenderTargets(1, &render_target, cached_depth_stencil_view.Get());
}
