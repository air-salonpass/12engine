#pragma once

#include <DirectXMath.h>
#include <d3d12.h>

// 1フレームの照明をGPUへ渡す定数。モデルと床で同じ設定を使い、明るさの差を防ぐ。
// LightingShader.hのb2と順序・サイズを一致させる。GPU資源はComponentへ保存しない。
struct lighting_constants12
{
    DirectX::XMFLOAT4X4 light_view_projection{};
    DirectX::XMFLOAT4 sun_color_intensity{ 1.0f, 0.91f, 0.78f, 3.0f };
    DirectX::XMFLOAT4 sky_color_intensity{ 0.32f, 0.48f, 0.72f, 0.65f };
    DirectX::XMFLOAT4 ground_color_intensity{ 0.18f, 0.14f, 0.11f, 0.0f };
    // x:影の濃さ、y:比較時の深度の余白、z:1画素のUV幅、w:法線方向の押出し距離。
    DirectX::XMFLOAT4 shadow_parameters{ 0.8f, 0.001f, 1.0f / 2048.0f, 0.025f };
    // xyz:物体から光源へ向かうワールド方向。床もモデルと同じ光の向きを使う。
    DirectX::XMFLOAT4 sun_direction{ 0.35f, 0.8f, -0.45f, 0.0f };
};

static_assert(sizeof(lighting_constants12) == 144, "HLSL b2との配置を確認してください。");
