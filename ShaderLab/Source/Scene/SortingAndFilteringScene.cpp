#include <algorithm>
#include <imgui.h>
#include "Graphics.h"
#include "Scene/SortingAndFilteringScene.h"

// コンストラクタ
SortingAndFilteringScene::SortingAndFilteringScene()
{
	weapons = {
		// name                 price  power rarity      attr       
		{ u8"はがねのつるぎ",     100,    5,  RARITY_N,   ATTR_NONE },
		{ u8"ほのおのつるぎ",     500,   50,  RARITY_SR,  ATTR_FIRE },
		{ u8"ほのおのやり",       300,   20,  RARITY_R,   ATTR_FIRE },
		{ u8"みずてっぽう",       200,   10,  RARITY_R,   ATTR_WATER},
		{ u8"かぜのつえ",         550,   30,  RARITY_SR,  ATTR_WIND },
		{ u8"かぜのやり",         150,   10,  RARITY_R,   ATTR_WIND },
		{ u8"かみなりのつるぎ",   750,   10,  RARITY_SR,  ATTR_FIRE | ATTR_WIND },
		{ u8"こおりのつるぎ",     950,   10,  RARITY_SR,  ATTR_WATER | ATTR_WIND },
		{ u8"えくすかりばー",    1000,  100,  RARITY_SSR, ATTR_ALL  },
	};

	// 表示用リストに登録
	for (Weapon& weapon : weapons)
	{
		displayWeapons.emplace_back(&weapon);
	}
}

// GUI描画処理
void SortingAndFilteringScene::DrawGUI()
{
	ImVec2 pos = ImGui::GetMainViewport()->GetWorkPos();
	ImGui::SetNextWindowPos(ImVec2(pos.x + 10, pos.y + 10), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Once);
	
	if (ImGui::Begin(u8"ソート＆フィルタリング処理"))
	{
		const char* sortNames[] = { "", "Name", "Rarity", "Price", "Power" };
		const char* rarityNames[] = { "", "N", "R", "SR", "SSR" };

		// ソート方法切り替え
		if (ImGui::Combo("Sort", &sortType, sortNames, _countof(sortNames)))
		{
			Filtering();
		}

		// レアリティの選択
		int rarityIndex = selectRarity + 1;
		if (ImGui::Combo("Rarity", &rarityIndex, rarityNames, _countof(rarityNames)))
		{
			selectRarity = rarityIndex - 1;
			Filtering();
		}

		// 属性の選択
		bool changed = false;
		bool fire = filterAttr & ATTR_FIRE;
		bool water = filterAttr & ATTR_WATER;
		bool wind = filterAttr & ATTR_WIND;
		changed |= ImGui::Checkbox("Fire", &fire); ImGui::SameLine();
		changed |= ImGui::Checkbox("Water", &water); ImGui::SameLine();
		changed |= ImGui::Checkbox("Wind", &wind);
		if (changed)
		{
			filterAttr = 0;
			if (fire) filterAttr |= ATTR_FIRE;
			if (water) filterAttr |= ATTR_WATER;
			if (wind) filterAttr |= ATTR_WIND;

			Filtering();
		}

		// 表の描画
		ImGui::Columns(5, "columns");
		ImGui::SetColumnWidth(0, 100);
		ImGui::SetColumnWidth(1, 60);
		ImGui::SetColumnWidth(2, 250);
		ImGui::SetColumnWidth(3, 60);
		ImGui::SetColumnWidth(4, 60);
		ImGui::Separator();

		ImGui::Text("Name"); ImGui::NextColumn();
		ImGui::Text("Rarity"); ImGui::NextColumn();
		ImGui::Text("Attr"); ImGui::NextColumn();
		ImGui::Text("Price"); ImGui::NextColumn();
		ImGui::Text("Power"); ImGui::NextColumn();
		ImGui::Separator();

		for (const Weapon* weapon : displayWeapons)
		{
			ImGui::Text(weapon->name); ImGui::NextColumn();
			ImGui::Text(rarityNames[weapon->rarity + 1]); ImGui::NextColumn();

			bool fire = weapon->attr & ATTR_FIRE;
			bool water = weapon->attr & ATTR_WATER;
			bool wind = weapon->attr & ATTR_WIND;

			ImGui::Checkbox("Fire", &fire); ImGui::SameLine();
			ImGui::Checkbox("Water", &water); ImGui::SameLine();
			ImGui::Checkbox("Wind", &wind);
			ImGui::NextColumn();

			ImGui::Text("%d", weapon->price);
			ImGui::NextColumn();

			ImGui::Text("%d", weapon->power);
			ImGui::NextColumn();

			ImGui::Separator();
		}
		ImGui::Columns(1);
	}

	ImGui::End();
}

// フィルタリング
void SortingAndFilteringScene::Filtering()
{
	// 表示用リストをクリア
	displayWeapons.clear();

}
