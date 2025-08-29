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
	initFromIniFileName(u8"ini\\ui\\saveload\\window.ini");
	if (save)
	{
		saveBtn = addComponent<Button>(u8"ini\\ui\\saveload\\savebtn.ini");
	}
	if (load)
	{
		loadBtn = addComponent<Button>(u8"ini\\ui\\saveload\\loadbtn.ini");
	}
	exitBtn = addComponent<Button>(u8"ini\\ui\\saveload\\exitbtn.ini");
	snap = addComponent<ImageContainer>(u8"ini\\ui\\saveload\\snapbmp.ini");
	snap->stretch = true;
	listBox = addComponent<ListBox>(u8"ini\\ui\\saveload\\listbox.ini");

	setChildRectReferToParent();
}

void SaveLoad::onEvent()
{
	if (listBox == nullptr)
	{
		result = erExit;
		running = false;
		return;
	}
	if (index != listBox->index)
	{
		index = listBox->index;

		snap->impImage = nullptr;

		std::string imageName = SHOT_FOLDER + convert::formatString(SHOT_BMP, index + 1);

		std::unique_ptr<char[]> c_ptr;

		int len = 0;
		if (File::readFile(imageName, c_ptr, len))
		{
			auto c = c_ptr.get();
			if (len > SAVE_SHOT_HEAD_LEN)
			{
				bool match = true;
				for (size_t i = 0; i < SAVE_SHOT_HEAD_LEN; i++)
				{
					if (c[i] != SAVE_SHOT_HEAD[i])
					{
						match = false;
						break;
					}
				}
				len -= SAVE_SHOT_HEAD_LEN;
				c += SAVE_SHOT_HEAD_LEN;
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
					if (len >= w * h * SaveBMPPixelBytes)
					{
						auto bmp_ptr = std::make_unique<char[]>(len);
						memcpy(bmp_ptr.get(), c, len);
						_shared_image img = engine->loadSaveShotFromPixels(w, h, bmp_ptr, len);
						if (img != nullptr)
						{
							snap->impImage = IMP::createIMPImageFromImage(img);
						}
					}
				}
			}
		}
	}	
	if (exitBtn != nullptr && exitBtn->getResult(erClick))
	{
		result = erExit;
		running = false;
		return;
	}
	if (save && saveBtn != nullptr && saveBtn->getResult(erClick))
	{
		if (index >= 0)
		{
			result = erSave;
			running = false;
			return;
		}
		
	}
	if (load && loadBtn != nullptr && loadBtn->getResult(erClick))
	{
		if (index >= 0)
		{
			std::string tempName = convert::formatString(SAVE_FOLDER, index + 1);
			tempName += SAVE_LIST_FILE;
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
	impImage = nullptr;
	freeCom(snap);
	freeCom(saveBtn);
	freeCom(exitBtn);
	freeCom(loadBtn);
	freeCom(listBox);
}

bool SaveLoad::onHandleEvent(AEvent & e)
{
	if (e.eventType == ET_KEYDOWN)
	{
		if (e.eventData == KEY_ESCAPE)
		{
			running = false;
			result = erOK;
			return true;
		}
	}
	return false;
}
