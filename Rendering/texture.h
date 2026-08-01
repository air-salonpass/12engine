#pragma once

#include <d3d11.h>
#include <cstddef>

// 画像ファイルを読み込み、ピクセルシェーダーから参照できるSRVを生成する。
HRESULT load_texture_from_file(
    ID3D11Device* device,
    const wchar_t* filename,
    ID3D11ShaderResourceView** shader_resource_view,
    D3D11_TEXTURE2D_DESC* texture2d_desc
);

// UNIT36: glbなどのメモリ上に埋め込まれたDDS／WIC画像からSRVを生成する。
HRESULT load_texture_from_memory(
    ID3D11Device* device,
    const void* data,
    size_t size,
    ID3D11ShaderResourceView** shader_resource_view);

// ファイル読込時に共有している全テクスチャを解放する。
void release_all_textures();

HRESULT make_dummy_texture(
    ID3D11Device* device,
    ID3D11ShaderResourceView** shader_resource_view,
    DWORD value,
    UINT dimension);
