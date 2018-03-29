#include "MemoMemu.h"
#include "../GameManager/GameManager.h"

MemoMenu::MemoMenu()
{
	init();
	visible = false;
}

MemoMenu::~MemoMenu()
{
	freeResource();
}

void MemoMenu::reFresh()
{
	if (scrollbar != NULL && memoText != NULL)
	{
		for (int i = 0; i < MEMO_LINE; i++)
		{
			if (position + i < (int)GameManager::getInstance()->memo.memo.size())
			{
				memoText->mstr[i]->setStr(convert::GBKToUnicode(GameManager::getInstance()->memo.memo[i + position]));
			}
			else
			{
				memoText->mstr[i]->setStr(L"");
			}
		}
	}	
}

void MemoMenu::reset()
{
	scrollbar->setPosition(0);
	position = scrollbar->position;
	reFresh();
}

void MemoMenu::reRange(int max)
{
	scrollbar->max = max;
	scrollbar->setPosition(scrollbar->position);
	position = scrollbar->position;
	reFresh();
}

void MemoMenu::init()
{
	freeResource();
	initFromIni("ini\\ui\\memo\\window.ini");	
	title = addImageContainer("ini\\ui\\memo\\title.ini");	
	image = addImageContainer("ini\\ui\\memo\\image.ini");
	scrollbar = addScrollbar("ini\\ui\\memo\\scrollbar.ini");
	memoText = addMemo("ini\\ui\\memo\\memo.ini");

	setChildRect();
}

void MemoMenu::freeResource()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}

	freeCom(memoText);
	freeCom(title);
	freeCom(image);
	freeCom(scrollbar);

}

void MemoMenu::onEvent()
{
	if (scrollbar != NULL && memoText != NULL && position != scrollbar->position)
	{
		position = scrollbar->position;
		reFresh();
	}
}
