#include "ToolTip.h"
#include "../../Engine/Engine.h"
#include "BuySellMenu.h"
#include "../GameManager/GameManager.h"

#include <algorithm>

namespace
{
void appendDetailLine(std::string& text, const std::string& line)
{
	if (line.empty())
	{
		return;
	}
	if (!text.empty())
	{
		text += "<enter>";
	}
	text += line;
}

std::string formatSignedAttributeValue(int value)
{
	if (value > 0)
	{
		return "+" + std::to_string(value);
	}
	return std::to_string(value);
}

void appendType1Attribute(std::string& text, const std::string& name, int value)
{
	if (value == 0)
	{
		return;
	}
	text += name + " " + formatSignedAttributeValue(value) + "  ";
}

void appendType1GroupedAttribute(std::string& text, const std::string& name,
	int primaryValue, int secondaryValue, int tertiaryValue)
{
	if (primaryValue == 0 && secondaryValue == 0 && tertiaryValue == 0)
	{
		return;
	}

	text += name + " ";
	if (primaryValue != 0)
	{
		text += formatSignedAttributeValue(primaryValue);
	}
	if (secondaryValue != 0 || tertiaryValue != 0)
	{
		text += "(" + formatSignedAttributeValue(secondaryValue) + ")";
		text += "(" + formatSignedAttributeValue(tertiaryValue) + ")";
	}
	text += "  ";
}

std::string buildType1GoodsAttributeText(const Goods& goods)
{
	std::string text;
	appendType1Attribute(text, "命", goods.life);
	appendType1Attribute(text, "体", goods.thew);
	appendType1Attribute(text, "气", goods.mana);
	appendType1GroupedAttribute(text, "攻", goods.attack, goods.attack2, goods.attack3);
	appendType1GroupedAttribute(text, "防", goods.defend, goods.defend2, goods.defend3);
	appendType1Attribute(text, "捷", goods.evade);
	appendType1Attribute(text, "命", goods.lifeMax);
	appendType1Attribute(text, "体", goods.thewMax);
	appendType1Attribute(text, "气", goods.manaMax);
	return text;
}

void appendType2Attribute(std::string& text, const std::string& name, int value)
{
	if (value != 0)
	{
		appendDetailLine(text, name + formatSignedAttributeValue(value));
	}
}

std::string buildType2GoodsAttributeText(const Goods& goods)
{
	std::string text;
	appendType2Attribute(text, "命", goods.life);
	appendType2Attribute(text, "体", goods.thew);
	appendType2Attribute(text, "气", goods.mana);
	appendType2Attribute(text, "攻", goods.attack);
	appendType2Attribute(text, "攻2 ", goods.attack2);
	appendType2Attribute(text, "攻3 ", goods.attack3);
	appendType2Attribute(text, "防", goods.defend);
	appendType2Attribute(text, "防2", goods.defend2);
	appendType2Attribute(text, "防3", goods.defend3);
	appendType2Attribute(text, "捷", goods.evade);
	appendType2Attribute(text, "命", goods.lifeMax);
	appendType2Attribute(text, "体", goods.thewMax);
	appendType2Attribute(text, "气", goods.manaMax);
	return text;
}
}

ToolTip::ToolTip()
{
	Element::name = "ToolTipMenu";
	setPriority(epMax);
	visible = false;
	needEvents = false;
	init();
}

ToolTip::~ToolTip()
{
	freeResource();
}

void ToolTip::setGoods(std::shared_ptr<Goods> goods)
{
	if (goods == nullptr)
	{
		return;
	}
	clearContent();
	currentContentIsMagic = false;

	std::string imageFile = !goods->image.empty() ? goods->image : goods->icon;
	std::string imageName = GOODS_RES_FOLDER_ASF + imageFile;

	if (image && !imageFile.empty()) image->impImage = IMP::createIMPImage(imageName);
	if (name)
	{
		if (layoutProfile == LayoutProfile::Xjxqy)
		{
			name->color = 0xDCF5E9AB;
		}
		name->setStr(goods->name.empty() ? "无名称" : goods->name);
	}

	std::string costLabel = "价格： ";
	int price = goods->getBuyPrice();
	auto buySellMenu = BuySellMenu::getInstance();
	if (buySellMenu != nullptr && buySellMenu->visible)
	{
		if (buySellMenu->bsKind == bsSell)
		{
			costLabel = "回收价格： ";
			price = goods->getSellPrice(buySellMenu->recyclePercent);
		}
		else
		{
			price = goods->getBuyPrice(buySellMenu->buyPercent);
		}
	}
	std::string costStr = costLabel;
	costStr += convert::formatString("%d", price);
	if (cost)
	{
		if (layoutProfile == LayoutProfile::Xjxqy)
		{
			cost->color = 0xDCFFFFFF;
		}
		cost->setStr(costStr);
	}

	std::string detailText;
	if (layoutProfile == LayoutProfile::Xjxqy)
	{
		std::string userRestriction = goods->userRestrictionText();
		if (!userRestriction.empty())
		{
			appendDetailLine(detailText, "使用者：" + userRestriction);
		}
		if (goods->minUserLevel > 0)
		{
			appendDetailLine(detailText,
				"等级需求：" + convert::formatString("%d", goods->minUserLevel));
		}
	}
	std::string attributeText;
	if (layoutProfile == LayoutProfile::Xjxqy)
	{
		attributeText = buildType2GoodsAttributeText(*goods);
	}
	else if (layoutProfile == LayoutProfile::Yycs)
	{
		attributeText = buildType1GoodsAttributeText(*goods);
	}
	else
	{
		attributeText = goods->effect;
	}
	if (attributeText.empty())
	{
		attributeText = goods->effect;
	}
	appendDetailLine(detailText, attributeText);
	if (intro1)
	{
		if (layoutProfile == LayoutProfile::Xjxqy)
		{
			intro1->color = 0xDCFFFFFF;
		}
		intro1->setStr(detailText);
	}
	if (intro2)
	{
		if (layoutProfile == LayoutProfile::Xjxqy)
		{
			intro2->color = 0xDCFFFFFF;
		}
		intro2->setStr(goods->intro.empty() ? "无简介" : goods->intro);
	}
	finishContentLayout();
}

void ToolTip::setMagic(std::shared_ptr<Magic> magic, int level)
{
	if (magic == nullptr)
	{
		return;
	}
	clearContent();
	currentContentIsMagic = true;

	std::string imageFile = !magic->image.empty() ? magic->image : magic->icon;
	std::string imageName = MAGIC_RES_FOLDER_ASF + imageFile;

	if (image && !imageFile.empty()) image->impImage = IMP::createIMPImage(imageName);
	if (name)
	{
		if (layoutProfile == LayoutProfile::Xjxqy)
		{
			name->color = 0xDCE1E16E;
		}
		name->setStr(magic->name.empty() ? "无名称" : magic->name);
	}
	std::string costStr = "等级： ";
	costStr += convert::formatString("%d", level);
	if (cost)
	{
		if (layoutProfile == LayoutProfile::Xjxqy)
		{
			cost->color = 0xDCFFFFFF;
		}
		cost->setStr(costStr);
	}

	std::string introText = magic->intro.empty() ? "无简介" : magic->intro;
	std::shared_ptr<Label> targetIntro = magicIntro != nullptr ? magicIntro : intro2;
	if (targetIntro)
	{
		if (layoutProfile == LayoutProfile::Xjxqy)
		{
			targetIntro->color = 0xDCFFFFFF;
		}
		targetIntro->setStr(introText);
	}
	finishContentLayout();
}

void ToolTip::showForOwner(PElement ownerElement)
{
	owner = ownerElement;
	visible = true;
}

void ToolTip::placeNearElement(const PElement& anchorElement)
{
	if (layoutProfile != LayoutProfile::Xjxqy || anchorElement == nullptr)
	{
		return;
	}

	constexpr int AnchorGap = 8;
	int windowWidth = 0;
	int windowHeight = 0;
	engine->getWindowSize(windowWidth, windowHeight);
	const Rect& anchorRect = anchorElement->rect;
	int newX = anchorRect.x + anchorRect.w + AnchorGap;
	if (newX + rect.w > windowWidth)
	{
		newX = anchorRect.x - rect.w - AnchorGap;
	}
	int newY = anchorRect.y;
	newX = std::max(0, std::min(newX, windowWidth - rect.w));
	newY = std::max(0, std::min(newY, windowHeight - rect.h));
	offsetRectTree(newX - rect.x, newY - rect.y);
}

void ToolTip::hide()
{
	visible = false;
	owner.reset();
}

void ToolTip::init()
{
	freeResource();
	layoutProfile = LayoutProfile::Jxqy2;
	auto gameManager = GameManager::getInstance();
	if (gameManager != nullptr)
	{
		if (gameManager->global.feature.menuResourceProfile == mrpYycs)
		{
			layoutProfile = LayoutProfile::Yycs;
		}
		else if (gameManager->global.feature.menuResourceProfile == mrpXjxqy)
		{
			layoutProfile = LayoutProfile::Xjxqy;
		}
	}
	loadMenuDefinition("ini\\ui\\tooltip\\tooltip.menu.ini");

	image = getComponentByName<ImageContainer>("image");
	intro1 = getComponentByName<Label>("intro1");
	intro2 = getComponentByName<Label>("intro2");
	magicIntro = getComponentByName<Label>("magicIntro");
	name = getComponentByName<Label>("name");
	cost = getComponentByName<Label>("cost");

	if (image) image->stretch = true;
	if (name) name->autoShrink = true;
	if (cost) cost->autoNextLine = true;
	if (intro1) intro1->autoNextLine = layoutProfile == LayoutProfile::Xjxqy;
	if (intro2) intro2->autoNextLine = true;
	if (magicIntro) magicIntro->autoNextLine = true;
	if (layoutProfile == LayoutProfile::Jxqy2)
	{
		auto setCompactFont = [](const std::shared_ptr<Label>& label,
			int maximumFontSize)
		{
			if (label == nullptr)
			{
				return;
			}
			label->fontSize = std::min(label->fontSize, maximumFontSize);
			label->minimumFontSize = std::min(label->fontSize, 12);
			label->invalidateTextLayout();
		};
		setCompactFont(name, 18);
		setCompactFont(cost, 18);
		setCompactFont(intro1, 16);
		setCompactFont(intro2, 16);
		setCompactFont(magicIntro, 16);
	}
	if (layoutProfile == LayoutProfile::Xjxqy)
	{
		if (name) name->horizontalAlignment = TextHorizontalAlignment::Center;
		if (cost) cost->horizontalAlignment = TextHorizontalAlignment::Center;
		if (intro1) intro1->horizontalAlignment = TextHorizontalAlignment::Center;
		if (intro2) intro2->horizontalAlignment = TextHorizontalAlignment::Left;
	}

	if (name) nameLayoutRect = name->rect;
	if (cost) costLayoutRect = cost->rect;
	if (intro1) intro1LayoutRect = intro1->rect;
	if (intro2) intro2LayoutRect = intro2->rect;

	setChildRectReferToParent();
}

void ToolTip::clearContent()
{
	if (image)
	{
		image->impImage = nullptr;
	}
	if (name) name->setStr("");
	if (cost) cost->setStr("");
	if (intro1) intro1->setStr("");
	if (intro2) intro2->setStr("");
	if (magicIntro) magicIntro->setStr("");
}

void ToolTip::finishContentLayout()
{
	if (layoutProfile != LayoutProfile::Xjxqy)
	{
		return;
	}
	reflowXjxqy();
	placeNearMouse();
}

void ToolTip::reflowXjxqy()
{
	const int topPadding = std::max(1, nameLayoutRect.y);
	const int bottomPadding = topPadding;
	const int nameToCostGap = std::max(0,
		costLayoutRect.y - nameLayoutRect.y - nameLayoutRect.h);
	const int costToDetailsGap = std::max(0,
		intro1LayoutRect.y - costLayoutRect.y - costLayoutRect.h);
	const int detailsToIntroGap = std::max(0,
		intro2LayoutRect.y - intro1LayoutRect.y - intro1LayoutRect.h);
	int currentY = rect.y + topPadding;

	auto placeLabel = [this, &currentY](const std::shared_ptr<Label>& label,
		const Rect& layoutRect, int gap)
	{
		if (label == nullptr || label->getStr().empty())
		{
			return false;
		}
		currentY += gap;
		label->rect.x = rect.x + layoutRect.x;
		label->rect.y = currentY;
		label->rect.w = layoutRect.w;
		label->rect.h = std::max(1, layoutRect.h);
		label->invalidateTextLayout();
		label->rect.h = std::max(label->fontSize, label->getRenderedTextHeight());
		currentY += label->rect.h;
		return true;
	};

	placeLabel(name, nameLayoutRect, 0);
	placeLabel(cost, costLayoutRect, nameToCostGap);
	bool hasDetails = placeLabel(intro1, intro1LayoutRect, costToDetailsGap);
	int introGap = hasDetails ? detailsToIntroGap : costToDetailsGap;
	if (currentContentIsMagic)
	{
		introGap += cost != nullptr ? cost->fontSize : 0;
	}
	placeLabel(intro2, intro2LayoutRect, introGap);
	rect.h = std::max(1, currentY - rect.y + bottomPadding);
}

void ToolTip::placeNearMouse()
{
	int mouseX = 0;
	int mouseY = 0;
	int windowWidth = 0;
	int windowHeight = 0;
	engine->getMousePosition(mouseX, mouseY);
	engine->getWindowSize(windowWidth, windowHeight);

	int newX = mouseX;
	int newY = mouseY;
	if (rect.w > 0)
	{
		newX = std::min(newX, windowWidth - rect.w);
	}
	if (rect.h > 0)
	{
		newY = std::min(newY, windowHeight - rect.h);
	}
	newX = std::max(0, newX);
	newY = std::max(0, newY);

	offsetRectTree(newX - rect.x, newY - rect.y);
}

void ToolTip::onDraw()
{
	if (layoutProfile == LayoutProfile::Xjxqy)
	{
		engine->fillRect(rect.x, rect.y, rect.w, rect.h, 0, 0, 0, 160);
		return;
	}
	if (!drawImagetoRect(rect, stretch))
	{
		engine->fillRect(rect.x, rect.y, rect.w, rect.h, 28, 20, 12, 220);
		engine->fillRect(rect.x, rect.y, rect.w, 2, 132, 102, 62, 255);
		engine->fillRect(rect.x, rect.y + rect.h - 2, rect.w, 2, 60, 42, 24, 255);
		engine->fillRect(rect.x, rect.y, 2, rect.h, 132, 102, 62, 255);
		engine->fillRect(rect.x + rect.w - 2, rect.y, 2, rect.h, 60, 42, 24, 255);
	}
}

void ToolTip::onUpdate()
{
	if (!visible)
	{
		return;
	}
	auto ownerElement = owner.lock();
	if (ownerElement == nullptr || !ownerElement->visible)
	{
		hide();
	}
}

void ToolTip::freeResource()
{
	name = nullptr;
	cost = nullptr;
	intro1 = nullptr;
	intro2 = nullptr;
	magicIntro = nullptr;
	image = nullptr;
	owner.reset();
	ConfigDrivenPanel::freeResource();
}
