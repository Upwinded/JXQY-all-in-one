#pragma once
#include "ImageContainer.h"
#include "Button.h"
#include "Scrollbar.h"
#include "Item.h"
#include "ListBox.h"
#include "Label.h"
#include "CheckBox.h"
#include "MemoText.h"
#include "ColumnImage.h"
#include "TalkLabel.h"
#include "TransImage.h"
#include "Joystick.h"
#include "RoundButton.h"
#include "TextButton.h"
#include "DragRoundButton.h"
#include "DragButton.h"
#include "FadeMask.h"
#include "VideoPlayer.h"


#define freeCom(component); \
	removeChild(component);\
	if (component.get() != nullptr)\
	{\
		component = nullptr;\
	}

class Panel :
	public ImageContainer
{
public:
	Panel();
	virtual ~Panel();

	Align align = alNone;
	int alignX = 0;
	int alignY = 0;
	void setAlign();
public:
	//virtual void initFromIni(const std::string& fileName) { BaseComponent::initFromIni(fileName); }
	virtual void initFromIni(INIReader & ini);
protected:

	template <typename T>
	auto addComponent(const std::string& fileName)
	{
		std::unique_ptr<char[]> s;
		int len = 0;
		len = PakFile::readFile(fileName, s);
		if (s == nullptr || len == 0)
		{
			GameLog::write("no ini file: %s\n", fileName.c_str());
			return std::shared_ptr<T>(nullptr);
		}
		INIReader ini(s);
		auto component = std::make_shared<T>();

		if (std::is_same<T, Scrollbar>::value)
		{
			component->initFromIniWithName(ini, fileName);
		}
		else
		{
			component->initFromIni(ini);
		}
		this->addChild(component);
		return component;
	}

	void freeResource();

	void resetRect(PElement e, int x, int y);
	void resetRect();

};

