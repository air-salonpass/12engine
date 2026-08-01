#pragma once
#include "sprite.h"
#include <vector>

class sprite_batch : public sprite
{
public:
    sprite_batch(ID3D11Device* device, const wchar_t* filename, size_t max_sprites);
    ~sprite_batch();

    void begin(ID3D11DeviceContext* immediate_context);

    void render(ID3D11DeviceContext* immediate_context,
        float dx, float dy, float dw, float dh,
        float r, float g, float b, float a,
        float angle,
        float sx, float sy, float sw, float sh);

    void end(ID3D11DeviceContext* immediate_context);

private:
    std::vector<vertex> vertices;
    size_t max_vertices;

    ID3D11Buffer* vertex_buffer = nullptr;
};