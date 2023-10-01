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
	if (scrollbar != nullptr && memoText != nullptr)
	{
		for (int i = 0; i < MEMO_LINE; i++)
		{
			if (position + i < (int)GameManager::getInstance()->memo.memo.size())
			{
				memoText->mstr[i]->setStr(GameManager::getInstance()->memo.memo[i + position]);
			}
			else
			{
				memoText->mstr[i]->setStr("");
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
	initFromIniFileName("ini\\ui\\memo\\window.ini");
	title = addComponent<ImageContainer>("ini\\ui\\memo\\title.ini");
	image = addComponent<ImageContainer>("ini\\ui\\memo\\image.ini");
	scrollbar = addComponent<Scrollbar>("ini\\ui\\memo\\scrollbar.ini");
	memoText = addComponent<MemoText>("ini\\ui\\memo\\memo.ini");

	setChildRectReferToParent();
}

void MemoMenu::freeResource()
{
	impImage = nullptr;

	freeCom(memoText);
	freeCom(title);
	freeCom(image);
	freeCom(scrollbar);

}

void MemoMenu::onEvent()
{
	if (scrollbar != nullptr && memoText != nullptr && position != scrollbar->position)
	{
		position = scrollbar->position;
		reFresh();
	}
}
