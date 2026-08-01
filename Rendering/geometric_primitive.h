#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>
#include <cstdint>

#include "shader.h"

enum primitive_type
{
    CUBE,
    CYLINDER,
    SPHERE,
    CAPSULE
};

class geometric_primitive
{
public:

    struct vertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 normal;
    };

    struct constants
    {
        DirectX::XMFLOAT4X4 world;
        DirectX::XMFLOAT4 material_color;
    };

private:

    Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> index_buffer;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;

    Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer;

public:

   geometric_primitive(
	ID3D11Device* device,
	primitive_type type);
    virtual ~geometric_primitive() = default;

    void render(
        ID3D11DeviceContext* immediate_context,
        const DirectX::XMFLOAT4X4& world,
        const DirectX::XMFLOAT4& material_color);

protected:

    void create_com_buffers(
        ID3D11Device* device,
        const vertex* vertices,
        size_t vertex_count,
        const uint32_t* indices,
        size_t index_count);
};