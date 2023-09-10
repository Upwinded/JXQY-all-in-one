#include "SkillsPanel.h"
#include "../GameManager/GameManager.h"

SkillsPanel::SkillsPanel()
{
	init();
	priority = epMin;
	coverMouse = false;
	canCallBack = true;
}

SkillsPanel::~SkillsPanel()
{
	freeResource();
}

void SkillsPanel::onChildCallBack(Element * child)
{
#ifdef _MOBILE
	if (child == nullptr) { return; }
	result = child->getResult();
	if ((result & erMouseLDown))
	{
		if (child == attackBtn)
		{
			clickIndex = SKILL_PANEL_ATTACK;
		}
		else if (child == sitBtn)
		{
			clickIndex = SKILL_PANEL_SIT;
		}
		else if (child == skillBtn1)
		{
			clickIndex = SKILL_PANEL_SKIIL1;
		}
		else if (child == skillBtn2)
		{
			clickIndex = SKILL_PANEL_SKIIL2;
		}
		else if (child == skillBtn3)
		{
			clickIndex = SKILL_PANEL_SKIIL3;
		}
		else if (child == skillBtn4)
		{
			clickIndex = SKILL_PANEL_SKIIL4;
		}
		else if (child == skillBtn5)
		{
			clickIndex = SKILL_PANEL_SKIIL5;
		}
		else
        {
            for (int i = 0; i < FASTBTN_COUNT; ++i)
            {
                if (child == fastBtn[i])
                {
                    clickIndex = SKILL_PANEL_FAST_SELECT + i;
                }
            }
		}	
	}
	if ((result & erDragEnd))
	{
		auto component = (DragRoundButton*)child;
		dragEndPosition = component->getDragPosition();
	}
	if (parent != nullptr && parent->canCallBack)
	{
		parent->onChildCallBack(this);
	}
#endif

}


void SkillsPanel::onUpdate()
{

}

void SkillsPanel::init()
{
	freeResource();
	initFromIni("ini\\ui\\mobile\\skills\\window.ini");
	attackBtn = addRoundButton("ini\\ui\\mobile\\skills\\attack.ini");
	sitBtn = addRoundButton("ini\\ui\\mobile\\skills\\sit.ini");
	skillBtn1 = addRoundButton("ini\\ui\\mobile\\skills\\skill1.ini");
	skillBtn2 = addRoundButton("ini\\ui\\mobile\\skills\\skill2.ini");
	skillBtn3 = addRoundButton("ini\\ui\\mobile\\skills\\skill3.ini");
	skillBtn4 = addRoundButton("ini\\ui\\mobile\\skills\\skill4.ini");
	skillBtn5 = addRoundButton("ini\\ui\\mobile\\skills\\skill5.ini");
	for (int i = 0; i < FASTBTN_COUNT; ++i) {
		addComponent(TextButton, fastBtn[i], convert::formatString("ini\\ui\\mobile\\skills\\fastbtn%d.ini", i + 1));
		fastBtn[i]->visible = false;
	}
	addComponent(DragRoundButton, rightJumpBtn, "ini\\ui\\mobile\\skills\\rightjump.ini");
	rightJumpBtn->setIMP("mpc\\character\\jump.mpc");
	setChildRectReferToParent();
}

void SkillsPanel::freeResource()
{
	if (impImage != nullptr)
	{
		IMP::clearIMPImage(impImage);
		//delete impImage;
		impImage = nullptr;
	}

	freeCom(attackBtn);
	freeCom(sitBtn);
	freeCom(skillBtn1);
	freeCom(skillBtn2);
	freeCom(skillBtn3);
	freeCom(skillBtn4);
	freeCom(skillBtn5);
	for (int i = 0; i < FASTBTN_COUNT; ++i)
	{
		freeCom(fastBtn[i]);
	}
	freeCom(rightJumpBtn);
}
