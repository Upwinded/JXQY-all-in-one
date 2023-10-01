#include "ListBox.h"


ListBox::ListBox()
{
	coverMouse = true;
	priority = epButton;
}


ListBox::~ListBox()
{
	freeResource();
}

void ListBox::initFromIni(INIReader & ini)
{
	freeResource();

	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	soundName = ini.Get("Init", "Sound", "");
	itemHeight = ini.GetInteger("Init", "ItemHeight", itemHeight);
	itemCount = ini.GetInteger("Init", "ItemCount", itemCount);
	
	std::string soundName = ini.Get("Init", "Sound", "");

	selColor = ini.GetColor("Init", "SelColor", color);
	color = ini.GetColor("Init", "Color", color);	


	if (itemCount > 0)
	{
		itemName.resize(itemCount);
		itemButton.resize(itemCount);
		
		
		for (size_t i = 0; i < itemName.size(); i++)
		{
			itemButton[i] = std::make_shared<Button>();
			itemName[i] = ini.Get("Items", convert::formatString("%d", i + 1), "");
			
			itemButton[i]->loadSound(soundName, 1);
			
			itemButton[i]->image[0] = IMP::createIMPImageFromImage(engine->createText(itemName[i], itemSize, color));
			itemButton[i]->rect = {rect.x, rect.y + ((int)i) * itemHeight, rect.w, itemHeight};
			addChild(itemButton[i]);
		}
	}
}

void ListBox::freeResource()
{
	index = -1;
	for (int i = 0; i < (int)itemButton.size(); i++)
	{
		if (itemButton[i] != nullptr)
		{
			itemButton[i] = nullptr;
		}	
	}
	itemButton.resize(0);
	itemName.resize(0);
	removeAllChild();
}

void ListBox::onEvent()
{
	for (int i = 0; i < itemCount; i++)
	{
		if (itemButton[i]->getResult(erMouseLDown))
		{
			if (index != i)
			{
				if (index >= 0)
				{
					itemButton[index]->image[0] = IMP::createIMPImageFromImage(engine->createText(itemName[index], itemSize, color));
				}
				index = i;
				itemButton[index]->image[0] = IMP::createIMPImageFromImage(engine->createText(itemName[index], itemSize, selColor));
			}			 
		}
	}
}
