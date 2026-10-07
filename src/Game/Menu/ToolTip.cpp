#include "ToolTip.h"
#include "../../Engine/Engine.h"
#include "BuySellMenu.h"
#include "../GameManager/GameManager.h"
#include "../../Component/TextLayout.h"

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

std::string buildType2GoodsAttributeText(const Goods& goods, bool qingyu)
{
	std::string text;
	appendType2Attribute(text, "命", goods.life);
	appendType2Attribute(text, "体", goods.thew);
	appendType2Attribute(text, "气", goods.mana);
	appendType2Attribute(text, "攻", goods.attack);
	appendType2Attribute(text, qingyu ? "附加攻击一 " : "攻2 ", goods.attack2);
	appendType2Attribute(text, qingyu ? "附加攻击二 " : "攻3 ", goods.attack3);
	appendType2Attribute(text, "防", goods.defend);
	appendType2Attribute(text, qingyu ? "附加防御一 " : "防2", goods.defend2);
	appendType2Attribute(text, qingyu ? "附加防御二 " : "防3", goods.defend3);
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

void ToolTip::setGoods(std::shared_ptr<Goods> goods, bool selling)
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
		if (selling)
		{
			costLabel = "卖出价： ";
			price = goods->getSellPrice(buySellMenu->recyclePercent);
		}
		else
		{
			costLabel = "买入价： ";
			price = goods->getBuyPrice(buySellMenu->buyPercent);
		}
	}
	std::string costStr = costLabel;
	costStr += convert::formatString("%d", price);
	if (selling && buySellMenu != nullptr && buySellMenu->visible)
	{
		if (buySellMenu->bsKind != bsSell && !buySellMenu->canSellSelfGoods)
		{
			costStr = "当前只能购买";
		}
		else if (price <= 0)
		{
			costStr = "不可出售";
		}
	}
	if (cost)
	{
		if (layoutProfile == LayoutProfile::Xjxqy)
		{
			cost->color = 0xDCFFFFFF;
		}
		cost->setStr(costStr);
	}

	std::string detailText;
	if (layoutProfile == LayoutProfile::Xjxqy || layoutProfile == LayoutProfile::Qingyu)
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
	if (layoutProfile == LayoutProfile::Xjxqy || layoutProfile == LayoutProfile::Qingyu)
	{
		attributeText = buildType2GoodsAttributeText(*goods, layoutProfile == LayoutProfile::Qingyu);
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
		targetIntro->setColorTagsEnabled(true);
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
	if ((layoutProfile != LayoutProfile::Xjxqy && layoutProfile != LayoutProfile::Qingyu) || anchorElement == nullptr)
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
	if (body)
	{
		const int maximumY = std::max(0, windowHeight - 108 - rect.h);
		newY = std::clamp(newY, std::min(80, maximumY), maximumY);
	}
	offsetRectTree(newX - rect.x, newY - rect.y);
}

void ToolTip::hide()
{
	if (expanded) return;
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
	if (gameManager != nullptr && gameManager->global.feature.qingyuUi)
	{
		layoutProfile = LayoutProfile::Qingyu;
	}
	loadMenuDefinition("ini\\ui\\tooltip\\tooltip.menu.ini");

	image = getComponentByName<ImageContainer>("image");
	intro1 = getComponentByName<Label>("intro1");
	intro2 = getComponentByName<Label>("intro2");
	magicIntro = getComponentByName<Label>("magicIntro");
	name = getComponentByName<Label>("name");
	cost = getComponentByName<Label>("cost");
	body = getComponentByName<MemoText>("body");

	if (image) image->stretch = true;
	if (name) name->autoShrink = true;
	if (cost) cost->autoNextLine = true;
	if (intro1) intro1->autoNextLine = layoutProfile == LayoutProfile::Xjxqy || layoutProfile == LayoutProfile::Qingyu;
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
	needEvents = body != nullptr;
	coverMouse = false;
	detailFocus.setInputAwarePresentation();
	detailFocus.addVisualSpatialGroup("item-details", {
		{ "previous", getComponentByName("previous"), [this]() { turnPage(UIAction::PagePrevious); } },
		{ "next", getComponentByName("next"), [this]() { turnPage(UIAction::PageNext); } },
		{ "back", getComponentByName("back"), [this]() { logicRunning = false; } } });
	detailFocus.setDefaultFocus("back");
	detailFocus.setCancelHandler([this]() { logicRunning = false; });
	updatePage();
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
	if (body && !body->mstr.empty())
	{
		lines.clear();
		std::string text = intro1 ? intro1->getStr() : "";
		appendDetailLine(text, intro2 ? intro2->getStr() : "");
		for (const auto& line : TextLayout::wrapColorTaggedUtf8Text(text,
			TextLayout::charactersPerLineForWidth(body->mstr.front()->rect.w, body->fontSize), body->color))
		{
			std::string formatted;
			for (const auto& run : line)
			{
				formatted += convert::formatString("<color=%u,%u,%u,%u>",
					(run.color >> 16) & 0xFFU, (run.color >> 8) & 0xFFU,
					run.color & 0xFFU, (run.color >> 24) & 0xFFU) + run.text;
			}
			lines.push_back(std::move(formatted));
		}
		page = 0;
		updatePage();
		placeNearMouse();
		return;
	}
	if (layoutProfile != LayoutProfile::Xjxqy && layoutProfile != LayoutProfile::Qingyu)
	{
		return;
	}
	reflowXjxqy();
	placeNearMouse();
}

void ToolTip::updatePage()
{
	if (!body || body->mstr.empty()) return;
	if (intro1) intro1->visible = false;
	if (intro2) intro2->visible = false;
	const int count = static_cast<int>(body->mstr.size());
	const int pages = std::max(1, (static_cast<int>(lines.size()) + count - 1) / count);
	page = std::clamp(page, 0, pages - 1);
	for (int row = 0; row < count; ++row)
	{
		const int index = page * count + row;
		body->mstr[row]->setColorTagsEnabled(true);
		body->mstr[row]->setStr(index < static_cast<int>(lines.size()) ? lines[index] : "");
	}
	if (auto label = getComponentByName<Label>("page"))
		label->setStr(convert::formatString("%d / %d", page + 1, pages));
	if (auto button = getComponentByName("previous")) button->visible = button->activated = expanded && page > 0;
	if (auto button = getComponentByName("next")) button->visible = button->activated = expanded && page + 1 < pages;
	if (auto button = getComponentByName("back")) button->visible = button->activated = expanded;
	if (auto hint = getComponentByName<Label>("hint"))
	{
		hint->visible = !expanded;
		hint->setStr(pages > 1 ? "滚轮 / 翻页键：翻阅说明" : "点击物品查看完整说明");
	}
}

bool ToolTip::turnPage(UIAction action)
{
	if (!visible || !body || body->mstr.empty()
		|| (action != UIAction::PagePrevious && action != UIAction::PageNext)
		|| lines.size() <= body->mstr.size()) return false;
	page += action == UIAction::PageNext ? 1 : -1;
	updatePage();
	return true;
}

void ToolTip::runDetails()
{
	if (!body) return;
	expanded = true;
	updatePage();
	int width = 0, height = 0;
	engine->getWindowSize(width, height);
	offsetRectTree((width - rect.w) / 2 - rect.x, std::max(0, (height - rect.h) / 2 - 14) - rect.y);
	detailFocus.focusDefault();
	run();
	expanded = false;
	detailFocus.suspendFocus();
	updatePage();
	hide();
}

void ToolTip::onEvent()
{
	if (!expanded) return;
	if (auto button = getComponentByName<FlatTextButton>("previous"))
		if (button->getResult(erClick)) turnPage(UIAction::PagePrevious);
	if (auto button = getComponentByName<FlatTextButton>("next"))
		if (button->getResult(erClick)) turnPage(UIAction::PageNext);
	if (auto button = getComponentByName<FlatTextButton>("back"))
		if (button->getResult(erClick)) logicRunning = false;
}

void ToolTip::onRun()
{
	if (expanded)
	{
		visible = true;
		detailFocus.focusDefault();
	}
}

bool ToolTip::onHandleEvent(AEvent& event)
{
	if (event.eventType == ET_MOUSEWHEEL && event.eventData != 0
		&& turnPage(event.eventData > 0 ? UIAction::PageNext : UIAction::PagePrevious)) return true;
	return expanded && dispatchKeyboardUIAction(event, *this);
}

bool ToolTip::onHandleUIAction(UIAction action)
{
	return turnPage(action) || (expanded && detailFocus.handleAction(action));
}

void ToolTip::onWindowResize(int width, int height)
{
	const auto previousOwner = owner;
	const std::string title = name ? name->getStr() : "";
	const std::string price = cost ? cost->getStr() : "";
	const std::string attributes = intro1 ? intro1->getStr() : "";
	const std::string description = intro2 ? intro2->getStr() : "";
	const int previousPage = page;
	ConfigDrivenPanel::onWindowResize(width, height);
	owner = previousOwner;
	if (name) name->setStr(title);
	if (cost) cost->setStr(price);
	if (intro1) intro1->setStr(attributes);
	if (intro2) intro2->setStr(description);
	finishContentLayout();
	page = previousPage;
	updatePage();
	if (expanded)
	{
		offsetRectTree((width - rect.w) / 2 - rect.x, std::max(0, (height - rect.h) / 2 - 14) - rect.y);
		detailFocus.focusDefault();
	}
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
	if (body)
	{
		const int maximumY = std::max(0, windowHeight - 108 - rect.h);
		newY = std::clamp(newY, std::min(80, maximumY), maximumY);
	}

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
	if (!visible || expanded)
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
	body = nullptr;
	detailFocus.clear();
	owner.reset();
	ConfigDrivenPanel::freeResource();
}
