// tinygltfの実装をD3D12経路へ独立させる。
// 画像はWICでGPU Texture化するためtinygltf内蔵STB画像処理は使用しない。
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "../tinygltf-release/tiny_gltf.h"
