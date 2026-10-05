#pragma once

#include <string>

// Prefabから生成したEntityについて、元ファイルとの関連だけを保持する。
// TransformやAnimationは通常Componentとして複製されるため、実体はPrefabへ依存しない。
struct prefab_component
{
	std::string source_path;
};
