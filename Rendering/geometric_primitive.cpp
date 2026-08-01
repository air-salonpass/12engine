#include "geometric_primitive.h"

#include <DirectXMath.h>
#include <cassert>
#include <vector>

using namespace DirectX;

geometric_primitive::geometric_primitive(
    ID3D11Device* device,
    primitive_type type)
{
    std::vector<vertex> vertices;
    std::vector<uint32_t> indices;

    float s = 0.5f;

    vertex cube_vertices[24] =
    {
        // Front
        {{-s,-s,-s}, { 0, 0,-1}},
        {{ s,-s,-s}, { 0, 0,-1}},
        {{ s, s,-s}, { 0, 0,-1}},
        {{-s, s,-s}, { 0, 0,-1}},

        // Back
        {{ s,-s, s}, { 0, 0, 1}},
        {{-s,-s, s}, { 0, 0, 1}},
        {{-s, s, s}, { 0, 0, 1}},
        {{ s, s, s}, { 0, 0, 1}},

        // Left
        {{-s,-s, s}, {-1, 0, 0}},
        {{-s,-s,-s}, {-1, 0, 0}},
        {{-s, s,-s}, {-1, 0, 0}},
        {{-s, s, s}, {-1, 0, 0}},

        // Right
        {{ s,-s,-s}, { 1, 0, 0}},
        {{ s,-s, s}, { 1, 0, 0}},
        {{ s, s, s}, { 1, 0, 0}},
        {{ s, s,-s}, { 1, 0, 0}},

        // Top
        {{-s, s,-s}, { 0, 1, 0}},
        {{ s, s,-s}, { 0, 1, 0}},
        {{ s, s, s}, { 0, 1, 0}},
        {{-s, s, s}, { 0, 1, 0}},

        // Bottom
        {{-s,-s, s}, { 0,-1, 0}},
        {{ s,-s, s}, { 0,-1, 0}},
        {{ s,-s,-s}, { 0,-1, 0}},
        {{-s,-s,-s}, { 0,-1, 0}},
    };

    uint32_t cube_indices[36] =
    {
        // Front
        0,2,1,
        0,3,2,

        // Back
        4,6,5,
        4,7,6,

        // Left
        8,10,9,
        8,11,10,

        // Right
        12,14,13,
        12,15,14,

        // Top
        16,18,17,
        16,19,18,

        // Bottom
        20,22,21,
        20,23,22
    };

    if (type == CUBE)
    {
        vertices.assign(
            cube_vertices,
            cube_vertices + 24);

        indices.assign(
            cube_indices,
            cube_indices + 36);
    }
    else if (type == CYLINDER)
    {
        const int slices = 32;

        float radius = 0.5f;
        float height = 1.0f;

        for (int i = 0; i <= slices; i++)
        {
            float angle = XM_2PI * i / slices;

            float x = cosf(angle) * radius;
            float z = sinf(angle) * radius;

            // 上
            vertices.push_back({
                {x, height * 0.5f, z},
                {cosf(angle), 0, sinf(angle)}
                });

            // 下
            vertices.push_back({
                {x, -height * 0.5f, z},
                {cosf(angle), 0, sinf(angle)}
                });
        }

        for (int i = 0; i < slices; i++)
        {
            int top1 = i * 2;
            int bottom1 = i * 2 + 1;

            int top2 = (i + 1) * 2;
            int bottom2 = (i + 1) * 2 + 1;

            // 側面
            indices.push_back(top1);
            indices.push_back(top2);
            indices.push_back(bottom1);

            indices.push_back(top2);
            indices.push_back(bottom2);
            indices.push_back(bottom1);
        }

        // =====================
        // 上面中心頂点
        // =====================

        int topCenterIndex = (int)vertices.size();

        vertices.push_back({
            {0, height * 0.5f, 0},
            {0, 1, 0}
            });

        // 上面インデックス
        for (int i = 0; i < slices; i++)
        {
            int top1 = i * 2;
            int top2 = (i + 1) * 2;

            indices.push_back(topCenterIndex);
            indices.push_back(top2);
            indices.push_back(top1);
        }

        // =====================
        // 下面中心頂点
        // =====================

        int bottomCenterIndex = (int)vertices.size();

        vertices.push_back({
            {0, -height * 0.5f, 0},
            {0, -1, 0}
            });

        // 下面インデックス
        for (int i = 0; i < slices; i++)
        {
            int bottom1 = i * 2 + 1;
            int bottom2 = (i + 1) * 2 + 1;

            indices.push_back(bottomCenterIndex);
            indices.push_back(bottom1);
            indices.push_back(bottom2);
        }
    }
    else if (type == SPHERE)
    {
        const int sliceCount = 16;
        const int stackCount = 16;

        float radius = 0.5f;

        for (int stack = 0; stack <= stackCount; stack++)
        {
            float phi = XM_PI * stack / stackCount;

            for (int slice = 0; slice <= sliceCount; slice++)
            {
                float theta = XM_2PI * slice / sliceCount;

                float x =
                    radius * sinf(phi) * cosf(theta);

                float y =
                    radius * cosf(phi);

                float z =
                    radius * sinf(phi) * sinf(theta);

                vertices.push_back({
                    {x,y,z},
                    {x,y,z}
                    });
            }
        }

        for (int stack = 0; stack < stackCount; stack++)
        {
            for (int slice = 0; slice < sliceCount; slice++)
            {
                int first =
                    stack * (sliceCount + 1) + slice;

                int second =
                    first + sliceCount + 1;

                indices.push_back(first);
                indices.push_back(second);
                indices.push_back(first + 1);

                indices.push_back(second);
                indices.push_back(second + 1);
                indices.push_back(first + 1);
            }
        }
    }
    else if (type == CAPSULE)
    {
        // UNIT12: 上下の半球を円柱部分でつないだカプセルを、緯度・経度のリングから生成する。
        const int sliceCount = 24;
        const int stackCount = 24;
        const float radius = 0.35f;
        const float halfCylinderHeight = 0.45f;

        for (int stack = 0; stack <= stackCount; ++stack)
        {
            const float phi = XM_PI * stack / stackCount;
            const float normalY = cosf(phi);
            // 赤道より上は上側、下は下側へずらし、中央に円柱となる区間を作る。
            const float centerY = normalY >= 0.0f ?
                halfCylinderHeight : -halfCylinderHeight;

            for (int slice = 0; slice <= sliceCount; ++slice)
            {
                const float theta = XM_2PI * slice / sliceCount;
                const float normalX = sinf(phi) * cosf(theta);
                const float normalZ = sinf(phi) * sinf(theta);

                vertices.push_back({
                    {
                        normalX * radius,
                        centerY + normalY * radius,
                        normalZ * radius
                    },
                    { normalX, normalY, normalZ }
                    });
            }
        }

        for (int stack = 0; stack < stackCount; ++stack)
        {
            for (int slice = 0; slice < sliceCount; ++slice)
            {
                const int first = stack * (sliceCount + 1) + slice;
                const int second = first + sliceCount + 1;

                indices.push_back(first);
                indices.push_back(second);
                indices.push_back(first + 1);
                indices.push_back(second);
                indices.push_back(second + 1);
                indices.push_back(first + 1);
            }
        }
    }

    D3D11_INPUT_ELEMENT_DESC input_element_desc[]
    {
        {
            "POSITION",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            D3D11_APPEND_ALIGNED_ELEMENT,
            D3D11_INPUT_PER_VERTEX_DATA,
            0
        },

        {
            "NORMAL",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            D3D11_APPEND_ALIGNED_ELEMENT,
            D3D11_INPUT_PER_VERTEX_DATA,
            0
        },
    };

    create_com_buffers(
        device,
        vertices.data(),
        vertices.size(),
        indices.data(),
        indices.size());

    create_vs_from_cso(
        device,
        "geometric_primitive_vs.cso",
        vertex_shader.GetAddressOf(),
        input_layout.GetAddressOf(),
        input_element_desc,
        ARRAYSIZE(input_element_desc));

    create_ps_from_cso(
        device,
        "geometric_primitive_ps.cso",
        pixel_shader.GetAddressOf());

    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.ByteWidth = sizeof(constants);
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    device->CreateBuffer(
        &buffer_desc,
        nullptr,
        constant_buffer.GetAddressOf());
}

void geometric_primitive::create_com_buffers(
    ID3D11Device* device,
    const vertex* vertices,
    size_t vertex_count,
    const uint32_t* indices,
    size_t index_count)
{
    D3D11_BUFFER_DESC buffer_desc{};
    D3D11_SUBRESOURCE_DATA subresource_data{};

    buffer_desc.ByteWidth =
        static_cast<UINT>(sizeof(vertex) * vertex_count);

    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    subresource_data.pSysMem = vertices;

    device->CreateBuffer(
        &buffer_desc,
        &subresource_data,
        vertex_buffer.ReleaseAndGetAddressOf());

    buffer_desc.ByteWidth =
        static_cast<UINT>(sizeof(uint32_t) * index_count);

    buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    subresource_data.pSysMem = indices;

    device->CreateBuffer(
        &buffer_desc,
        &subresource_data,
        index_buffer.ReleaseAndGetAddressOf());
}

void geometric_primitive::render(
    ID3D11DeviceContext* immediate_context,
    const XMFLOAT4X4& world,
    const XMFLOAT4& material_color)
{
    UINT stride = sizeof(vertex);
    UINT offset = 0;

    immediate_context->IASetVertexBuffers(
        0,
        1,
        vertex_buffer.GetAddressOf(),
        &stride,
        &offset);

    immediate_context->IASetIndexBuffer(
        index_buffer.Get(),
        DXGI_FORMAT_R32_UINT,
        0);

    immediate_context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    immediate_context->IASetInputLayout(input_layout.Get());

    immediate_context->VSSetShader(
        vertex_shader.Get(),
        nullptr,
        0);

    immediate_context->PSSetShader(
        pixel_shader.Get(),
        nullptr,
        0);

    constants data{ world, material_color };

    immediate_context->UpdateSubresource(
        constant_buffer.Get(),
        0,
        0,
        &data,
        0,
        0);

    immediate_context->VSSetConstantBuffers(
        0,
        1,
        constant_buffer.GetAddressOf());

    D3D11_BUFFER_DESC buffer_desc{};
    index_buffer->GetDesc(&buffer_desc);

    immediate_context->DrawIndexed(
        buffer_desc.ByteWidth / sizeof(uint32_t),
        0,
        0);
}
