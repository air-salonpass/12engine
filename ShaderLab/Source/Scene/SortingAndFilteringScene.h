#pragma once

#include <vector>
#include "Scene.h"

// ソート＆フィルタリング処理シーン
class SortingAndFilteringScene : public Scene
{
public:
	SortingAndFilteringScene();
	~SortingAndFilteringScene() override = default;

	// GUI描画処理
	void DrawGUI() override;

private:
	// フィルタリング
	void Filtering();
private:
	// 属性
	static const int ATTR_NONE	= 0;
	static const int ATTR_WATER	= 1 << 0;
	static const int ATTR_FIRE	= 1 << 1;
	static const int ATTR_WIND	= 1 << 2;
	static const int ATTR_ALL	= ATTR_WATER | ATTR_FIRE | ATTR_WIND;

	// レアリティ
	enum
	{
		RARITY_N,
		RARITY_R,
		RARITY_SR,
		RARITY_SSR,
	};

	// ソートタイプ
	enum
	{
		SORT_NONE,
		SORT_NAME,
		SORT_RARITY,
		SORT_PRICE,
		SORT_POWER,
	};

	// 武器データ
	struct Weapon
	{
		const char*		name;
		int				price;
		int				power;
		int				rarity;
		int				attr;
	};

	std::vector<Weapon>		weapons;
	std::vector<Weapon*>	displayWeapons;
	int						sortType = SORT_NONE;
	int						selectRarity = -1;
	int						filterAttr = 0;
};
