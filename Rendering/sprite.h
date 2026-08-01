#pragma once
#include <d3d11.h>
#include <directxmath.h>
#include <string>

class sprite
{
public:
    sprite(ID3D11Device* device, const wchar_t* filename);
    ~sprite();

    void render(ID3D11DeviceContext* ctx,
        float dx, float dy,
        float dw, float dh,
        float r, float g, float b, float a,
        float angle);

    void render(ID3D11DeviceContext* ctx,
        float dx, float dy,
        float dw, float dh,
        float r, float g, float b, float a,
        float angle,
        float sx, float sy, float sw, float sh);
    void textout(
        ID3D11DeviceContext* immediate_context,
        std::string s,
        float x, float y,
        float w, float h,
        float r, float g, float b, float a);

private:
    void render_internal(
        ID3D11DeviceContext* ctx,
        float dx, float dy,
        float dw, float dh,
        float r, float g, float b, float a,
        float u0, float v0,
        float u1, float v1,
        float angle
    );

protected:
    ID3D11VertexShader* vertex_shader{};
    ID3D11PixelShader* pixel_shader{};
    ID3D11InputLayout* input_layout{};
    ID3D11Buffer* vertex_buffer{};

    ID3D11ShaderResourceView* shader_resource_view{};
    D3D11_TEXTURE2D_DESC texture2d_desc{};

    struct vertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT4 color;
        DirectX::XMFLOAT2 texcoord;
    };
};