#pragma once

#include <cstddef>
#include <string>

// GPUモデルは共有し、EntityにはAsset参照と表示状態だけを持たせる。
struct model_renderer_component
{
	std::size_t model_slot = 0;
	bool visible = true;
	std::string asset_path;
};
