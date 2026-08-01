#include "sprite_batch.h"
#include <cassert>
#include "misc.h"

sprite_batch::sprite_batch(ID3D11Device* device, const wchar_t* filename, size_t max_sprites)
    : sprite(device, filename)
    , max_vertices(max_sprites * 6)
{
    HRESULT hr{ S_OK };

    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.ByteWidth = sizeof(vertex) * max_vertices;
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = device->CreateBuffer(&buffer_desc, NULL, &vertex_buffer);
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
}
void sprite_batch::begin(ID3D11DeviceContext* immediate_context)
{
    vertices.clear();

    immediate_context->VSSetShader(vertex_shader, nullptr, 0);
    immediate_context->PSSetShader(pixel_shader, nullptr, 0);
    immediate_context->PSSetShaderResources(0, 1, &shader_resource_view);
}
void sprite_batch::render(ID3D11DeviceContext* immediate_context,
    float dx, float dy, float dw, float dh,
    float r, float g, float b, float a,
    float angle,
    float sx, float sy, float sw, float sh)
{
    D3D11_VIEWPORT viewport{};
    UINT num = 1;
    immediate_context->RSGetViewports(&num, &viewport);

    float x0 = dx;
    float y0 = dy;
    float x1 = dx + dw;
    float y1 = dy;
    float x2 = dx;
    float y2 = dy + dh;
    float x3 = dx + dw;
    float y3 = dy + dh;

    // NDC•ÏŠ·
    x0 = 2.0f * x0 / viewport.Width - 1.0f;
    y0 = 1.0f - 2.0f * y0 / viewport.Height;
    x1 = 2.0f * x1 / viewport.Width - 1.0f;
    y1 = 1.0f - 2.0f * y1 / viewport.Height;
    x2 = 2.0f * x2 / viewport.Width - 1.0f;
    y2 = 1.0f - 2.0f * y2 / viewport.Height;
    x3 = 2.0f * x3 / viewport.Width - 1.0f;
    y3 = 1.0f - 2.0f * y3 / viewport.Height;

    float u0 = sx / texture2d_desc.Width;
    float v0 = sy / texture2d_desc.Height;
    float u1 = (sx + sw) / texture2d_desc.Width;
    float v1 = (sy + sh) / texture2d_desc.Height;

    vertices.push_back({ {x0,y0,0},{r,g,b,a},{u0,v0} });
    vertices.push_back({ {x1,y1,0},{r,g,b,a},{u1,v0} });
    vertices.push_back({ {x2,y2,0},{r,g,b,a},{u0,v1} });

    vertices.push_back({ {x2,y2,0},{r,g,b,a},{u0,v1} });
    vertices.push_back({ {x1,y1,0},{r,g,b,a},{u1,v0} });
    vertices.push_back({ {x3,y3,0},{r,g,b,a},{u1,v1} });
}
void sprite_batch::end(ID3D11DeviceContext* immediate_context)
{
    HRESULT hr{ S_OK };

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = immediate_context->Map(vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    size_t count = vertices.size();
    assert(count <= max_vertices);

    vertex* data = reinterpret_cast<vertex*>(mapped.pData);
    if (data)
    {
        memcpy(data, vertices.data(), sizeof(vertex) * count);
    }

    immediate_context->Unmap(vertex_buffer, 0);

    UINT stride = sizeof(vertex);
    UINT offset = 0;
    immediate_context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
    immediate_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    immediate_context->IASetInputLayout(input_layout);

    immediate_context->Draw(static_cast<UINT>(count), 0);
}
sprite_batch::~sprite_batch()
{
    if (vertex_buffer)
    {
        vertex_buffer->Release();
        vertex_buffer = nullptr;
    }
}