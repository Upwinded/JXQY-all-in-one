#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"

class MagicMenu :
	public Panel
{
public:
	MagicMenu();
	virtual ~MagicMenu();

	void init() override;

	std::shared_ptr<ImageContainer> title = nullptr;
	std::shared_ptr<ImageContainer> image = nullptr;

	std::shared_ptr<Scrollbar> scrollbar = nullptr;

	std::shared_ptr<Item> item[MENU_ITEM_COUNT] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

	void updateMagic();
	void updateMagic(int index);

private:
	int position = -1;
	void onEvent() override;


	void freeResource() override;

};

