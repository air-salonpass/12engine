#include "sprite.h"
#include "misc.h"
#include <memory>
#include <cstdio>
#include <WICTextureLoader.h>

// =========================
// コンストラクタ
// =========================
sprite::sprite(ID3D11Device* device, const wchar_t* filename)
{
    HRESULT hr{};

    vertex vertices[4]{};

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(vertices);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = vertices;

    hr = device->CreateBuffer(&bd, &sd, &vertex_buffer);
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    // ===== VS =====
    FILE* fp{};
    fopen_s(&fp, "sprite_vs.cso", "rb");
    _ASSERT_EXPR_A(fp, "sprite_vs.cso not found");

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    std::unique_ptr<unsigned char[]> data = std::make_unique<unsigned char[]>(size);
    fread(data.get(), size, 1, fp);
    fclose(fp);

    hr = device->CreateVertexShader(data.get(), size, nullptr, &vertex_shader);
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    // 入力レイアウト
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,D3D11_APPEND_ALIGNED_ELEMENT,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,D3D11_APPEND_ALIGNED_ELEMENT,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,D3D11_APPEND_ALIGNED_ELEMENT,D3D11_INPUT_PER_VERTEX_DATA,0},
    };

    hr = device->CreateInputLayout(layout, 3, data.get(), size, &input_layout);
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    // ===== PS =====
    fopen_s(&fp, "sprite_ps.cso", "rb");
    _ASSERT_EXPR_A(fp, "sprite_ps.cso not found");

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    data = std::make_unique<unsigned char[]>(size);
    fread(data.get(), size, 1, fp);
    fclose(fp);

    hr = device->CreatePixelShader(data.get(), size, nullptr, &pixel_shader);
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    // ===== Texture =====
    ID3D11Resource* resource{};
    hr = DirectX::CreateWICTextureFromFile(device, filename, &resource, &shader_resource_view);
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    ID3D11Texture2D* texture2d{};
    hr = resource->QueryInterface<ID3D11Texture2D>(&texture2d);
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    texture2d->GetDesc(&texture2d_desc);

    texture2d->Release();
    resource->Release();
}

// =========================
// デストラクタ
// =========================
sprite::~sprite()
{
    if (shader_resource_view) shader_resource_view->Release();
    if (vertex_buffer) vertex_buffer->Release();
    if (input_layout) input_layout->Release();
    if (vertex_shader) vertex_shader->Release();
    if (pixel_shader) pixel_shader->Release();
}

// =========================
// 内部共通処理
// =========================
void sprite::render_internal(
    ID3D11DeviceContext* ctx,
    float dx, float dy,
    float dw, float dh,
    float r, float g, float b, float a,
    float u0, float v0,
    float u1, float v1,
    float angle)
{
    D3D11_VIEWPORT vp{};
    UINT num = 1;
    ctx->RSGetViewports(&num, &vp);

    float x0 = dx;
    float y0 = dy;

    float x1 = dx + dw;
    float y1 = dy;

    float x2 = dx;
    float y2 = dy + dh;

    float x3 = dx + dw;
    float y3 = dy + dh;

    float cx = dx + dw * 0.5f;
    float cy = dy + dh * 0.5f;

    auto rotate = [](float& x, float& y, float cx, float cy, float angle)
        {
            x -= cx;
            y -= cy;

            float rad = DirectX::XMConvertToRadians(angle);
            float c = cosf(rad);
            float s = sinf(rad);

            float tx = x;
            float ty = y;

            x = c * tx - s * ty;
            y = s * tx + c * ty;

            x += cx;
            y += cy;
        };

    rotate(x0, y0, cx, cy, angle);
    rotate(x1, y1, cx, cy, angle);
    rotate(x2, y2, cx, cy, angle);
    rotate(x3, y3, cx, cy, angle);

    x0 = 2.0f * x0 / vp.Width - 1.0f;
    y0 = 1.0f - 2.0f * y0 / vp.Height;

    x1 = 2.0f * x1 / vp.Width - 1.0f;
    y1 = 1.0f - 2.0f * y1 / vp.Height;

    x2 = 2.0f * x2 / vp.Width - 1.0f;
    y2 = 1.0f - 2.0f * y2 / vp.Height;

    x3 = 2.0f * x3 / vp.Width - 1.0f;
    y3 = 1.0f - 2.0f * y3 / vp.Height;

    D3D11_MAPPED_SUBRESOURCE ms{};
    ctx->Map(vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);

    vertex* v = (vertex*)ms.pData;

    v[0].position = { x0, y0, 0 };
    v[1].position = { x1, y1, 0 };
    v[2].position = { x2, y2, 0 };
    v[3].position = { x3, y3, 0 };

    v[0].color = v[1].color = v[2].color = v[3].color = { r,g,b,a };

    v[0].texcoord = { u0, v0 };
    v[1].texcoord = { u1, v0 };
    v[2].texcoord = { u0, v1 };
    v[3].texcoord = { u1, v1 };

    ctx->Unmap(vertex_buffer, 0);

    ctx->PSSetShaderResources(0, 1, &shader_resource_view);

    UINT stride = sizeof(vertex);
    UINT offset = 0;

    ctx->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->IASetInputLayout(input_layout);

    ctx->VSSetShader(vertex_shader, nullptr, 0);
    ctx->PSSetShader(pixel_shader, nullptr, 0);

    ctx->Draw(4, 0);
}

// =========================
// 通常描画
// =========================
void sprite::render(ID3D11DeviceContext* ctx,
    float dx, float dy,
    float dw, float dh,
    float r, float g, float b, float a,
    float angle)
{
    render_internal(ctx,
        dx, dy, dw, dh,
        r, g, b, a,
        0, 0, 1, 1,
        angle);
}

// =========================
// Texel描画
// =========================
void sprite::render(ID3D11DeviceContext* ctx,
    float dx, float dy,
    float dw, float dh,
    float r, float g, float b, float a,
    float angle,
    float sx, float sy, float sw, float sh)
{
    float tex_w = (float)texture2d_desc.Width;
    float tex_h = (float)texture2d_desc.Height;

    float u0 = sx / tex_w;
    float v0 = sy / tex_h;
    float u1 = (sx + sw) / tex_w;
    float v1 = (sy + sh) / tex_h;

    render_internal(ctx,
        dx, dy, dw, dh,
        r, g, b, a,
        u0, v0, u1, v1,
        angle);
}

void sprite::textout(
    ID3D11DeviceContext* immediate_context,
    std::string s,
    float x, float y,
    float w, float h,
    float r, float g, float b, float a)
{
    float sw = static_cast<float>(texture2d_desc.Width / 16);
    float sh = static_cast<float>(texture2d_desc.Height / 16);

    float carriage = 0;

    for (const char c : s)
    {
        render(
            immediate_context,
            x + carriage, y,
            w, h,
            r, g, b, a,
            0,
            sw * (c & 0x0F),
            sh * (c >> 4),
            sw, sh
        );

        carriage += w;
    }
}