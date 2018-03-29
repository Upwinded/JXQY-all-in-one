#include "SaveLoad.h"

SaveLoad::SaveLoad(bool s, bool l)
{
	save = s;
	load = l;
	init();
}

SaveLoad::~SaveLoad()
{
	freeResource();
}

void SaveLoad::init()
{
	freeResource();
	initFromIni("ini\\ui\\saveload\\window.ini");
	if (save)
	{
		saveBtn = addButton("ini\\ui\\saveload\\SaveBtn.ini");
	}
	if (load)
	{
		loadBtn = addButton("ini\\ui\\saveload\\LoadBtn.ini");
	}
	exitBtn = addButton("ini\\ui\\saveload\\ExitBtn.ini");
	snap = addImageContainer("ini\\ui\\saveload\\SnapBmp.ini");
	snap->stretch = true;
	listBox = addListBox("ini\\ui\\saveload\\ListBox.ini");

	setChildRect();
}

void SaveLoad::onEvent()
{
	if (listBox == NULL)
	{
		result = erExit;
		running = false;
		return;
	}
	if (index != listBox->index)
	{
		index = listBox->index;
		if (snap->impImage != NULL)
		{
			IMP::clearIMPImage(snap->impImage);
			delete snap->impImage;
			snap->impImage = NULL;
		}
		std::string imageName = SHOT_FOLDER + convert::formatString(SHOT_BMP, index + 1);

		char * c = NULL;
		int len = 0;
		if (File::readFile(imageName, &c, &len))
		{
			if (len > BMP16HeadLen)
			{
				bool match = true;
				for (size_t i = 0; i < BMP16HeadLen; i++)
				{
					if (c[i] != BMP16Head[i])
					{
						match = false;
						break;
					}
				}
				len -= 4;
				c += 4;
				if (match)
				{
					int w, h, nil;

#define readMem(d, size); \
	memcpy(d, c, size);\
	c += size;\
	len -= size;
					readMem(&w, 4);
					readMem(&h, 4);
					readMem(&nil, 4);
					if (len >= w * h * 2)
					{
						_image img = engine->createBMP16(w, h, c, len);
						if (img != NULL)
						{
							snap->impImage = IMP::createIMPImageFromImage(img);
						}
					}
				}
			}
		}


	}	
	if (exitBtn != NULL && exitBtn->getResult(erClick))
	{
		result = erExit;
		running = false;
		return;
	}
	if (save && saveBtn != NULL && saveBtn->getResult(erClick))
	{
		if (index >= 0)
		{
			result = erSave;
			running = false;
			return;
		}
		
	}
	if (load && loadBtn != NULL && loadBtn->getResult(erClick))
	{
		if (index >= 0)
		{
			std::string tempName = convert::formatString(SAVE_FOLDER, index + 1);
			tempName += GLOBAL_INI;
			if (File::fileExist(tempName))
			{
				result = erLoad;
				running = false;
				return;
			}
		}
	}
}

void SaveLoad::freeResource()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}
	freeCom(snap);
	freeCom(saveBtn);
	freeCom(exitBtn);
	freeCom(loadBtn);
	freeCom(listBox);
}

bool SaveLoad::onHandleEvent(AEvent * e)
{
	if (e->eventType == KEYDOWN)
	{
		if (e->eventData == KEY_ESCAPE)
		{
			running = false;
			result = erOK;
			return true;
		}
	}
	return false;
}
