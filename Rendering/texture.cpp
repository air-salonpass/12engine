#include "texture.h"

#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>
using namespace DirectX;

#include <wrl.h>
using namespace Microsoft::WRL;

#include <map>
#include <string>
using namespace std;

#include <cassert>

#include <filesystem>
#include <memory>

// エラー内容をデバッガで確認するための補助マクロ。
#ifndef hr_trace
#define hr_trace(hr) (hr)
#endif

#ifndef _ASSERT_EXPR
#define _ASSERT_EXPR(expr, msg) assert(expr)
#endif

static map<wstring, ComPtr<ID3D11ShaderResourceView>> resources;

HRESULT load_texture_from_file(
    ID3D11Device* device,
    const wchar_t* filename,
    ID3D11ShaderResourceView** shader_resource_view,
    D3D11_TEXTURE2D_DESC* texture2d_desc)
{
    HRESULT hr{ S_OK };
    ComPtr<ID3D11Resource> resource;

    auto it = resources.find(filename);
    if (it != resources.end())
    {
        *shader_resource_view = it->second.Get();
        (*shader_resource_view)->AddRef();
        (*shader_resource_view)->GetResource(resource.GetAddressOf());
    }
    else
    {
        // UNIT31:
        // 元画像と同じ場所・同じ名前のDDS（例: image.png -> image.dds）を探す。
        // DDSはGPU向けに圧縮済みでミップマップも保持できるため、存在するときは優先して読み込む。
        std::filesystem::path dds_filename(filename);
        dds_filename.replace_extension(L".dds");

        if (std::filesystem::exists(dds_filename))
        {
            // 圧縮済みDDSからテクスチャとシェーダーリソースビューを生成する。
            hr = CreateDDSTextureFromFile(
                device,
                dds_filename.c_str(),
                resource.GetAddressOf(),
                shader_resource_view);
        }
        else
        {
            // DDSがまだ作られていない画像は、従来どおりWICでPNGなどを直接読み込む。
            hr = CreateWICTextureFromFile(
                device,
                filename,
                resource.GetAddressOf(),
                shader_resource_view);
        }

        _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
        if (FAILED(hr))
        {
            // Releaseではassertが無効になるため、失敗時にnullリソースを参照せず呼び出し元へ返す。
            return hr;
        }

        // キャッシュのキーは呼び出し元が指定した元画像名に統一し、2回目以降の読込を省略する。
        resources.insert(make_pair(filename, *shader_resource_view));
    }

    ComPtr<ID3D11Texture2D> texture2d;
    hr = resource.Get()->QueryInterface<ID3D11Texture2D>(texture2d.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    texture2d->GetDesc(texture2d_desc);

    return hr;
}

HRESULT load_texture_from_memory(
    ID3D11Device* device,
    const void* data,
    size_t size,
    ID3D11ShaderResourceView** shader_resource_view)
{
    // UNIT36: まずDDSとして解釈し、失敗した場合はPNG／JPEGなどを扱えるWICへ切り替える。
    ComPtr<ID3D11Resource> resource;
    HRESULT hr = CreateDDSTextureFromMemory(
        device,
        reinterpret_cast<const uint8_t*>(data),
        size,
        resource.GetAddressOf(),
        shader_resource_view);
    if (FAILED(hr))
    {
        resource.Reset();
        hr = CreateWICTextureFromMemory(
            device,
            reinterpret_cast<const uint8_t*>(data),
            size,
            resource.GetAddressOf(),
            shader_resource_view);
    }

    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
    return hr;
}

void release_all_textures()
{
    resources.clear();
}

HRESULT make_dummy_texture(
    ID3D11Device* device,
    ID3D11ShaderResourceView** shader_resource_view,
    DWORD value,
    UINT dimension)
{
    HRESULT hr{ S_OK };

    D3D11_TEXTURE2D_DESC texture2d_desc{};

    texture2d_desc.Width = dimension;
    texture2d_desc.Height = dimension;
    texture2d_desc.MipLevels = 1;
    texture2d_desc.ArraySize = 1;
    texture2d_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture2d_desc.SampleDesc.Count = 1;
    texture2d_desc.Usage = D3D11_USAGE_DEFAULT;
    texture2d_desc.BindFlags =
        D3D11_BIND_SHADER_RESOURCE;

    size_t texels =
        dimension * dimension;

    std::unique_ptr<DWORD[]> sysmem =
        std::make_unique<DWORD[]>(texels);

    for (size_t i = 0; i < texels; i++)
    {
        sysmem[i] = value;
    }

    D3D11_SUBRESOURCE_DATA subresource_data{};

    subresource_data.pSysMem =
        sysmem.get();

    subresource_data.SysMemPitch =
        sizeof(DWORD) * dimension;

    Microsoft::WRL::ComPtr<ID3D11Texture2D>
        texture2d;

    hr = device->CreateTexture2D(
        &texture2d_desc,
        &subresource_data,
        texture2d.GetAddressOf());

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};

    srv_desc.Format =
        texture2d_desc.Format;

    srv_desc.ViewDimension =
        D3D11_SRV_DIMENSION_TEXTURE2D;

    srv_desc.Texture2D.MipLevels = 1;

    hr = device->CreateShaderResourceView(
        texture2d.Get(),
        &srv_desc,
        shader_resource_view);

    return hr;
}
