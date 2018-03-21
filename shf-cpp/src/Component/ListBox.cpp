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

void ListBox::initFromIni(const std::string & fileName)
{
	freeResource();
	char * s = NULL;
	int len = 0;
	len = PakFile::readFile(fileName, &s);
	if (s == NULL || len == 0)
	{
		printf("no ini file: %s\n", fileName.c_str());
		return;
	}
	INIReader ini = INIReader::INIReader(s);
	delete[] s;
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
			itemButton[i] = new Button;
			itemName[i] = ini.Get("Items", convert::formatString("%d", i + 1), "");
			
			itemButton[i]->loadSound(soundName, 1);
			
			itemButton[i]->image[0] = IMP::createIMPImageFromImage(engine->createUnicodeText(convert::GBKToUnicode(itemName[i]), itemSize, color));
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
		if (itemButton[i] != NULL)
		{
			delete itemButton[i];
			itemButton[i] = NULL;
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
					IMP::clearIMPImage(itemButton[index]->image[0]);
					delete itemButton[index]->image[0];
					itemButton[index]->image[0] = IMP::createIMPImageFromImage(engine->createUnicodeText(convert::GBKToUnicode(itemName[index]), itemSize, color));					
				}
				index = i;
				IMP::clearIMPImage(itemButton[index]->image[0]);
				delete itemButton[index]->image[0];
				itemButton[index]->image[0] = IMP::createIMPImageFromImage(engine->createUnicodeText(convert::GBKToUnicode(itemName[index]), itemSize, selColor));
			}			 
		}
	}
}
