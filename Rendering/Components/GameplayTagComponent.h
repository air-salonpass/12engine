#pragma once

#include <string>

// ゲームロジックが表示名へ依存しないための識別子。
// 例: "Player", "Enemy", "Damage", "Goal"。名前はEditor表示用、Tagは判定用として分ける。
struct gameplay_tag_component
{
	std::string value = "Untagged";
};
