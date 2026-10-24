#include "AttributePointMenu.h"
#include "../GameManager/GameManager.h"
#include "../../Engine/Engine.h"
#include "../../libconvert/libconvert.h"

#include <algorithm>
#include <climits>
#include <numeric>

namespace
{
constexpr int RowHeight = 56;
constexpr unsigned int TextColor = 0xFFF5E8CB;
constexpr std::array<const char*, PlayerAttributeCount> AttributeNames = {
	u8"生命上限", u8"体力上限", u8"内力上限", u8"攻击", u8"身法", u8"防御", u8"暴击率", u8"暴击伤害"
};
}

AttributePointMenu::AttributePointMenu()
{
	name = "AttributePointMenu";
	visible = false;
	rectFullScreen = true;
	eventOccupied = true;
	canCallBack = true;
	setPriority(epMax + 3);
	const auto button = [&](const char* text)
	{
		auto result = std::make_shared<FlatTextButton>();
		result->setUTF8Str(text);
		result->setFontSize(16);
		addChild(result);
		return result;
	};
	for (std::size_t i = 0; i < plus.size(); ++i)
	{
		plus[i] = button("+");
		minus[i] = button(u8"−");
	}
	submitButton = button(u8"确认分配");
	resetButton = button(u8"撤销");
	closeButton = button(u8"返回");
	previousButton = button(u8"‹");
	nextButton = button(u8"›");
}

bool AttributePointMenu::open(const PElement& previousMenu, const PElement& previousFocus)
{
	if (visible || gm == nullptr || gm->player == nullptr || !gm->global.attributePointRules.enabled) return false;
	origin = previousMenu;
	returnFocus = previousFocus;
	openedCharacterIndex = gm->global.data.characterIndex;
	pending.fill(0);
	notice.clear();
	selected = firstRow = 0;
	visible = true;
	int width = 800, height = 600;
	if (engine != nullptr) engine->getWindowSize(width, height);
	layout(width, height);
	return true;
}

void AttributePointMenu::close(bool restoreFocus)
{
	if (!visible) return;
	cancelPointerInteraction();
	visible = false;
	pending.fill(0);
	if (engine != nullptr) engine->releasePhysicalInputsForContextTransition();
	const auto previous = origin.lock();
	const auto focus = returnFocus.lock();
	if (restoreFocus && previous && previous->visible && focus && gm != nullptr && gm->menu != nullptr)
	{
		auto* participant = dynamic_cast<ControllerFocusParticipant*>(previous.get());
		if (participant && participant->focusControllerElement(focus)) gm->menu->adoptControllerPointerFocus(previous, focus);
	}
}

int AttributePointMenu::pendingTotal() const
{
	return std::accumulate(pending.begin(), pending.end(), 0);
}

void AttributePointMenu::layout(int width, int height)
{
	rect = { 0, 0, width, height };
	const int margin = std::min(12, std::min(width, height) / 24);
	panel = { 0, 0, std::min(760, width - margin * 2), std::min(760, height - margin * 2) };
	panel.x = (width - panel.w) / 2;
	panel.y = (height - panel.h) / 2;
	rowsPerPage = std::clamp((panel.h - 216) / RowHeight, 1, static_cast<int>(PlayerAttributeCount));
	previousButton->rect = { panel.x + 12, panel.y + 66, 44, 44 };
	nextButton->rect = { panel.x + 64, panel.y + 66, 44, 44 };
	const int buttonWidth = (panel.w - 40) / 3;
	submitButton->rect = { panel.x + 12, panel.y + panel.h - 76, buttonWidth, 44 };
	resetButton->rect = { submitButton->rect.x + buttonWidth + 8, submitButton->rect.y, buttonWidth, 44 };
	closeButton->rect = { resetButton->rect.x + buttonWidth + 8, submitButton->rect.y, buttonWidth, 44 };
	cancelPointerInteraction();
	select(selected);
}

void AttributePointMenu::positionRows()
{
	for (std::size_t i = 0; i < plus.size(); ++i)
	{
		const bool shown = static_cast<int>(i) >= firstRow && static_cast<int>(i) < firstRow + rowsPerPage;
		plus[i]->visible = minus[i]->visible = shown;
		plus[i]->activated = shown && pendingTotal() < gm->player->attributePoints.available && pending[i] < INT_MAX;
		minus[i]->activated = shown && pending[i] > 0;
		const int y = panel.y + 116 + (static_cast<int>(i) - firstRow) * RowHeight + 4;
		plus[i]->rect = { panel.x + panel.w - 56, y, 44, 44 };
		minus[i]->rect = { plus[i]->rect.x - 52, y, 44, 44 };
	}
	previousButton->activated = firstRow > 0;
	nextButton->activated = firstRow + rowsPerPage < static_cast<int>(PlayerAttributeCount);
}

void AttributePointMenu::select(int index)
{
	selected = std::clamp(index, 0, static_cast<int>(PlayerAttributeCount) - 1);
	firstRow = selected / rowsPerPage * rowsPerPage;
	positionRows();
}

void AttributePointMenu::change(std::size_t index, int delta)
{
	select(static_cast<int>(index));
	if ((delta > 0 && pendingTotal() < gm->player->attributePoints.available && pending[index] < INT_MAX)
		|| (delta < 0 && pending[index] > 0)) pending[index] += delta;
	notice.clear();
	positionRows();
}

void AttributePointMenu::submit()
{
	if (openedCharacterIndex != gm->global.data.characterIndex || gm->inEvent || !gm->global.data.canInput)
	{
		close(false);
		return;
	}
	if (gm->player->allocateAttributePoints(pending, &notice))
	{
		pending.fill(0);
		notice = u8"分配成功";
	}
	positionRows();
}

bool AttributePointMenu::onHandleUIAction(UIAction action)
{
	if (!visible) return false;
	switch (action)
	{
	case UIAction::NavigateUp: select(selected - 1); break;
	case UIAction::NavigateDown: select(selected + 1); break;
	case UIAction::NavigateLeft: change(selected, -1); break;
	case UIAction::NavigateRight: change(selected, 1); break;
	case UIAction::PagePrevious:
	case UIAction::PanelPrevious:
	case UIAction::ScrollUp: select(selected - rowsPerPage); break;
	case UIAction::PageNext:
	case UIAction::PanelNext:
	case UIAction::ScrollDown: select(selected + rowsPerPage); break;
	case UIAction::Secondary: pending.fill(0); notice.clear(); positionRows(); break;
	case UIAction::Confirm: submit(); break;
	case UIAction::Cancel: close(); break;
	default: break;
	}
	return true;
}

bool AttributePointMenu::onHandleEvent(AEvent& event)
{
	if (event.eventType == ET_KEYDOWN)
	{
		if (event.eventData == SDL_SCANCODE_PAGEUP || event.eventData == SDL_SCANCODE_PAGEDOWN)
			return handleUIAction(event.eventData == SDL_SCANCODE_PAGEUP ? UIAction::PagePrevious : UIAction::PageNext);
		return dispatchKeyboardUIAction(event, *this) || visible;
	}
	if (event.eventType == ET_MOUSEWHEEL)
	{
		select(selected + (event.eventData > 0 ? -1 : 1));
		return true;
	}
	return false;
}

void AttributePointMenu::onChildCallBack(PElement child)
{
	if (!child || !child->getResult(erClick)) return;
	if (child == submitButton) submit();
	else if (child == closeButton) close();
	else if (child == resetButton) handleUIAction(UIAction::Secondary);
	else if (child == previousButton) select(selected - rowsPerPage);
	else if (child == nextButton) select(selected + rowsPerPage);
	else for (std::size_t i = 0; i < plus.size(); ++i)
	{
		if (child == plus[i]) { change(i, 1); break; }
		if (child == minus[i]) { change(i, -1); break; }
	}
}

void AttributePointMenu::onWindowResize(int width, int height)
{
	if (visible) layout(width, height);
}

void AttributePointMenu::onUpdate()
{
	if (!visible) return;
	if (openedCharacterIndex != gm->global.data.characterIndex || gm->inEvent || !gm->global.data.canInput
		|| !gm->global.attributePointRules.enabled) close(false);
	else positionRows();
}

void AttributePointMenu::drawText(CachedTextTexture& cache, const std::string& text,
	int x, int y, int width, int font, unsigned int color)
{
	const auto& image = cache.get(engine, text, font, color);
	int w = 0, h = 0;
	if (width <= 0 || !engine->getImageSize(image, w, h)) return;
	Rect source = { 0, 0, std::min(w, width), h };
	Rect destination = { x, y, source.w, source.h };
	engine->drawImageWithAlpha(image, &source, &destination, 255);
}

void AttributePointMenu::onDraw()
{
	if (engine == nullptr || gm == nullptr) return;
	engine->fillRect(rect.x, rect.y, rect.w, rect.h, 8, 10, 12, 200);
	engine->fillRect(panel.x, panel.y, panel.w, panel.h, 21, 27, 29, 255);
	drawText(titleTexture, u8"属性分配", panel.x + 12, panel.y + 12, panel.w - 24, 22, TextColor);
	drawText(balanceTexture, u8"可用：" + std::to_string(gm->player->attributePoints.available - pendingTotal())
		+ u8"  待用：" + std::to_string(pendingTotal()), panel.x + 12, panel.y + 42, panel.w - 24, 17, 0xFFFFD783);
	drawText(pageTexture, u8"第 " + std::to_string(firstRow / rowsPerPage + 1) + " / "
		+ std::to_string((PlayerAttributeCount + rowsPerPage - 1) / rowsPerPage) + u8" 页 · 基础属性",
		panel.x + 120, panel.y + 79, panel.w - 132, 14, TextColor);
	const auto& player = *gm->player;
	const std::array<double, PlayerAttributeCount> current = { static_cast<double>(player.lifeMax),
		static_cast<double>(player.thewMax), static_cast<double>(player.manaMax), static_cast<double>(player.attack),
		static_cast<double>(player.evade), static_cast<double>(player.defend),
		static_cast<double>(std::min(100.0f, std::max(0, player.level) * 0.1f + player.attributePoints.criticalChanceBonus)),
		static_cast<double>(player.level) + player.attributePoints.criticalDamageBonus };
	for (int row = firstRow; row < std::min(static_cast<int>(PlayerAttributeCount), firstRow + rowsPerPage); ++row)
	{
		const int y = panel.y + 116 + (row - firstRow) * RowHeight;
		engine->fillRect(panel.x + 12, y, panel.w - 24, RowHeight - 4, row == selected ? 71 : 12, row == selected ? 59 : 18, row == selected ? 35 : 20, 255);
		const int textWidth = panel.w - 136;
		drawText(names[row], std::string(AttributeNames[row]) + " [" + std::to_string(static_cast<std::int64_t>(player.attributePoints.spent[row]) + pending[row]) + "]",
			panel.x + 16, y + 5, textWidth, 16, TextColor);
		double next = current[row] + pending[row] * gm->global.attributePointRules.ratios[row];
		const bool percent = row >= static_cast<int>(PlayerAttribute::CriticalChance);
		if (row == static_cast<int>(PlayerAttribute::CriticalChance)) next = std::min(100.0, next);
		const std::string suffix = percent ? "%" : "";
		drawText(values[row], convert::formatString("%.4g", current[row]) + suffix + u8" → " + convert::formatString("%.4g", next) + suffix,
			panel.x + 16, y + 29, textWidth, 14, 0xFFCACEC7);
	}
	drawText(noticeTexture, notice, panel.x + 12, panel.y + panel.h - 102, panel.w - 24, 14, TextColor);
}

void AttributePointMenu::onDrawEnd()
{
	if (engine == nullptr || Element::currentRunOwnerBlocksParentInput()) return;
	if (!shouldPresentGamepadFocus(engine))
	{
		promptTextures.itemTextTextures.resize(1);
		drawText(promptTextures.itemTextTextures.front(), u8"加减调整 · Enter确认 · Esc撤回并返回",
			panel.x + 12, panel.y + panel.h - 24, panel.w - 24, 12, TextColor);
		return;
	}
	ControllerPromptDrawOptions options;
	options.x = panel.x + 12; options.y = panel.y + panel.h - 26;
	options.width = panel.w - 24; options.height = 24; options.fontSize = 12;
	using GameInput::InputAction;
	ControllerPromptPresenter::draw(engine, engine->inputActions(), {
		{ InputAction::Confirm, u8"确认分配" }, { InputAction::Cancel, u8"返回" }, { InputAction::Secondary, u8"撤销" }
	}, options, promptTextures);
}
