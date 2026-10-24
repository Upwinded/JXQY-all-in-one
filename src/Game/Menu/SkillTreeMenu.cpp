#include "SkillTreeMenu.h"
#include "../GameManager/GameManager.h"
#include "../../Engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr unsigned int TextColor = 0xFFF5E8CB;
constexpr int NodeHeight = 56;
}

SkillTreeMenu::SkillTreeMenu()
{
	name = "SkillTreeMenu";
	visible = false;
	rectFullScreen = true;
	eventOccupied = true;
	canCallBack = true;
	setPriority(epMax + 3);
	for (auto* button : { &learnButton, &closeButton, &upButton, &downButton,
		&previousGroup, &nextGroup, &previousDetails, &nextDetails })
	{
		*button = std::make_shared<FlatTextButton>();
		(*button)->setFontSize(16);
		addChild(*button);
	}
	closeButton->setUTF8Str(u8"返回");
	upButton->setUTF8Str(u8"↑");
	downButton->setUTF8Str(u8"↓");
	previousGroup->setUTF8Str(u8"‹");
	nextGroup->setUTF8Str(u8"›");
	previousDetails->setUTF8Str(u8"‹");
	nextDetails->setUTF8Str(u8"›");
}

bool SkillTreeMenu::open()
{
	if (visible || gm == nullptr || gm->player == nullptr || !gm->global.skillTreeEnabled) return false;
	const auto& tree = gm->magicManager.skillTree();
	if (tree.nodes.empty() || tree.groups.empty()) return false;
	for (const auto& button : nodeButtons) removeChild(button);
	nodeButtons.clear();
	nodeIds.clear();
	names.clear();
	descriptions.clear();
	for (const auto& node : tree.nodes)
	{
		Magic preview;
		preview.initFromIni(node.magicFile, false);
		nodeIds.push_back(node.id);
		names.push_back(preview.name.empty() ? node.magicFile : preview.name);
		descriptions.push_back(preview.intro);
		auto button = std::make_shared<FlatTextButton>();
		button->setFontSize(16);
		addChild(button);
		nodeButtons.push_back(button);
	}
	openedCharacterIndex = gm->global.data.characterIndex;
	group = 0;
	selected = 0;
	while (group + 1 < tree.groups.size() && std::none_of(tree.nodes.begin(), tree.nodes.end(),
		[&](const SkillTreeNode& node) { return node.groupId == tree.groups[group].id; })) ++group;
	for (std::size_t i = 0; i < tree.nodes.size(); ++i)
		if (tree.nodes[i].groupId == tree.groups[group].id) { selected = i; break; }
	scrollOffset = 0;
	detailPage = 0;
	confirming = false;
	notice.clear();
	detailContent.clear();
	int width = 800, height = 600;
	if (engine != nullptr) engine->getWindowSize(width, height);
	visible = true;
	layout(width, height);
	refreshContent();
	return true;
}

void SkillTreeMenu::close(bool restoreFocus)
{
	if (!visible) return;
	cancelPointerInteraction();
	visible = false;
	confirming = false;
	notice.clear();
	detailTextures.clear();
	if (engine != nullptr) engine->releasePhysicalInputsForContextTransition();
	if (restoreFocus && gm != nullptr && gm->menu != nullptr && gm->menu->magicMenu != nullptr
		&& gm->menu->magicMenu->visible)
	{
		auto menu = gm->menu->magicMenu;
		menu->focusControllerDefault();
		gm->menu->adoptControllerPointerFocus(menu, menu->controllerFocusedElement());
	}
}

void SkillTreeMenu::layout(int width, int height)
{
	rect = { 0, 0, std::max(1, width), std::max(1, height) };
	const int margin = std::min(12, std::min(rect.w, rect.h) / 24);
	panel = { 0, 0, std::min(1100, rect.w - margin * 2), std::min(760, rect.h - margin * 2) };
	panel.x = (rect.w - panel.w) / 2;
	panel.y = (rect.h - panel.h) / 2;
	const int left = panel.x + 12;
	const int widthInside = std::max(1, panel.w - 24);
	const int bodyTop = panel.y + 54;
	const int bodyHeight = std::max(96, panel.h - 142);
	if (panel.w >= 660 && panel.h >= 390)
	{
		const int chartWidth = widthInside * 53 / 100;
		chart = { left, bodyTop, chartWidth - 52, bodyHeight };
		details = { left + chartWidth + 12, bodyTop, widthInside - chartWidth - 12, bodyHeight };
	}
	else
	{
		const int detailHeight = std::min(118, std::max(96, bodyHeight / 2));
		chart = { left, bodyTop, widthInside - 52, std::max(96, bodyHeight - detailHeight - 10) };
		details = { left, chart.y + chart.h + 10, widthInside, detailHeight };
	}
	upButton->rect = { chart.x + chart.w + 8, chart.y, 44, 44 };
	downButton->rect = { upButton->rect.x, chart.y + chart.h - 44, 44, 44 };
	previousDetails->rect = { details.x + details.w - 96, details.y, 44, 44 };
	nextDetails->rect = { details.x + details.w - 44, details.y, 44, 44 };
	const int footerY = panel.y + panel.h - 76;
	previousGroup->rect = { left, footerY, 44, 44 };
	nextGroup->rect = { left + 52, footerY, 44, 44 };
	closeButton->rect = { left + widthInside - 80, footerY, 80, 44 };
	const bool multipleGroups = gm != nullptr && gm->magicManager.skillTree().groups.size() > 1;
	previousGroup->visible = previousGroup->activated = multipleGroups;
	nextGroup->visible = nextGroup->activated = multipleGroups;
	const int learnX = left + (multipleGroups ? 104 : 0);
	learnButton->rect = { learnX, footerY, std::max(44, closeButton->rect.x - 8 - learnX), 44 };
	detailLinesPerPage = std::max(1, (details.h - 52) / 22);
	detailContent.clear();
	cancelPointerInteraction();
	positionNodes(true);
	refreshContent();
}

void SkillTreeMenu::positionNodes(bool revealSelection)
{
	const auto& tree = gm->magicManager.skillTree();
	if (group >= tree.groups.size() || nodeButtons.size() != tree.nodes.size()) return;
	float minimumX = std::numeric_limits<float>::max(), maximumX = -minimumX;
	float minimumY = minimumX, maximumY = maximumX;
	for (const auto& node : tree.nodes)
	{
		if (node.groupId != tree.groups[group].id) continue;
		minimumX = std::min(minimumX, node.x);
		maximumX = std::max(maximumX, node.x);
		minimumY = std::min(minimumY, node.y);
		maximumY = std::max(maximumY, node.y);
	}
	if (minimumX > maximumX) return;
	scrollMaximum = std::max(0, static_cast<int>(std::ceil(maximumY - minimumY)) + NodeHeight - chart.h);
	if (revealSelection && selected < tree.nodes.size())
	{
		const int y = static_cast<int>(tree.nodes[selected].y - minimumY);
		if (y < scrollOffset) scrollOffset = y;
		if (y + NodeHeight > scrollOffset + chart.h) scrollOffset = y + NodeHeight - chart.h;
	}
	scrollOffset = std::clamp(scrollOffset, 0, scrollMaximum);
	const int nodeWidth = std::min(140, std::max(72, chart.w / 2 - 8));
	for (std::size_t i = 0; i < tree.nodes.size(); ++i)
	{
		const auto& node = tree.nodes[i];
		auto& button = nodeButtons[i];
		const int x = maximumX == minimumX ? (chart.w - nodeWidth) / 2
			: static_cast<int>((node.x - minimumX) / (maximumX - minimumX) * (chart.w - nodeWidth));
		button->rect = { chart.x + x, chart.y + static_cast<int>(node.y - minimumY) - scrollOffset, nodeWidth, NodeHeight };
		button->visible = button->activated = node.groupId == tree.groups[group].id
			&& button->rect.y >= chart.y && button->rect.y + NodeHeight <= chart.y + chart.h;
		button->setFocused(i == selected);
	}
	upButton->activated = scrollOffset > 0;
	downButton->activated = scrollOffset < scrollMaximum;
}

void SkillTreeMenu::selectNode(std::size_t index)
{
	if (index >= nodeButtons.size()) return;
	selected = index;
	detailPage = 0;
	confirming = false;
	notice.clear();
	positionNodes(true);
	refreshContent();
}

void SkillTreeMenu::navigate(int dx, int dy)
{
	const auto& tree = gm->magicManager.skillTree();
	if (selected >= tree.nodes.size()) return;
	const auto& current = tree.nodes[selected];
	const UIFocusDirection direction = dx < 0 ? UIFocusDirection::Left : dx > 0 ? UIFocusDirection::Right
		: dy < 0 ? UIFocusDirection::Up : UIFocusDirection::Down;
	std::optional<UIFocusSpatialScore> best;
	std::size_t next = selected;
	for (std::size_t i = 0; i < tree.nodes.size(); ++i)
	{
		const auto& node = tree.nodes[i];
		if (node.groupId != current.groupId || i == selected) continue;
		const auto score = scoreUIFocusSpatialCandidate(nodeButtons[selected]->rect, nodeButtons[i]->rect, direction, i);
		if (score && (!best || *score < *best)) { best = score; next = i; }
	}
	if (next != selected) selectNode(next);
}

void SkillTreeMenu::changeGroup(int direction)
{
	const auto& tree = gm->magicManager.skillTree();
	int next = static_cast<int>(group) + direction;
	while (next >= 0 && next < static_cast<int>(tree.groups.size()))
	{
		for (std::size_t i = 0; i < tree.nodes.size(); ++i)
			if (tree.nodes[i].groupId == tree.groups[next].id)
			{
				group = static_cast<std::size_t>(next);
				scrollOffset = 0;
				selectNode(i);
				return;
			}
		next += direction;
	}
}

void SkillTreeMenu::scroll(int direction)
{
	confirming = false;
	const int step = std::min(72, std::max(1, chart.h - NodeHeight));
	scrollOffset = std::clamp(scrollOffset + direction * step, 0, scrollMaximum);
	positionNodes(false);
	refreshContent();
}

void SkillTreeMenu::turnDetails(int direction)
{
	const int pages = std::max(1, static_cast<int>((detailLines.size() + detailLinesPerPage - 1) / detailLinesPerPage));
	detailPage = std::clamp(detailPage + direction, 0, pages - 1);
	refreshContent();
}

void SkillTreeMenu::confirmLearning()
{
	if (gm == nullptr || gm->inEvent || !gm->global.data.canInput
		|| openedCharacterIndex != gm->global.data.characterIndex) { close(false); return; }
	if (selected >= nodeIds.size()) return;
	notice = gm->magicManager.skillTreeLearningFailure(nodeIds[selected]);
	if (notice.empty())
	{
		if (!confirming) { confirming = true; notice = u8"再次确认将花费 1 个技能点。"; }
		else
		{
			const bool learned = gm->magicManager.learnSkillTreeNode(nodeIds[selected], &notice);
			confirming = false;
			if (learned) notice = u8"学习成功。";
		}
	}
	else confirming = false;
	detailPage = 0;
	refreshContent();
}

void SkillTreeMenu::refreshContent()
{
	const auto& tree = gm->magicManager.skillTree();
	if (selected >= tree.nodes.size() || names.size() != tree.nodes.size()) return;
	const auto& node = tree.nodes[selected];
	std::string prerequisites;
	for (auto required : node.requiredSkills)
	{
		if (!prerequisites.empty()) prerequisites += u8"、";
		prerequisites += names[required] + (tree.nodes[required].currentLevel > 0 ? u8"（已学）" : u8"（未学）");
	}
	int parentPoints = 0, groupPoints = 0;
	for (auto parent : node.parents) parentPoints += tree.nodes[parent].currentLevel;
	for (const auto& other : tree.nodes) if (other.groupId == node.groupId) groupPoints += other.currentLevel;
	const std::string content = (notice.empty() ? "" : notice + "<enter>")
		+ u8"角色等级：" + std::to_string(gm->player->level) + " / " + std::to_string(node.requiredLevel)
		+ u8"<enter>前置投入：" + std::to_string(parentPoints) + " / " + std::to_string(node.requiredPoints)
		+ u8"<enter>分支投入：" + std::to_string(groupPoints) + " / " + std::to_string(node.requiredTotalPoints)
		+ u8"<enter>前置技能：" + (prerequisites.empty() ? u8"无" : prerequisites)
		+ "<enter>" + descriptions[selected];
	if (content != detailContent)
	{
		detailContent = content;
		detailLines = TextLayout::wrapColorTaggedUtf8Text(content,
			TextLayout::charactersPerLineForWidth(details.w, 18), TextColor);
		detailTextures.clear();
	}
	const int pageCount = std::max(1, static_cast<int>((detailLines.size() + detailLinesPerPage - 1) / detailLinesPerPage));
	detailPage = std::clamp(detailPage, 0, pageCount - 1);
	previousDetails->activated = detailPage > 0;
	nextDetails->activated = detailPage + 1 < pageCount;
	learnButton->setUTF8Str(confirming ? u8"确认花费 1 点" : node.currentLevel >= node.maximumLevel
		? u8"已满级" : node.currentLevel == 0 ? u8"学习（1 点）" : u8"升级（1 点）");
	for (std::size_t i = 0; i < tree.nodes.size(); ++i)
	{
		const auto& current = tree.nodes[i];
		nodeButtons[i]->setUTF8Str(names[i] + " " + std::to_string(current.currentLevel));
		FlatTextButtonStyle style;
		style.normal.background = current.currentLevel > 0 ? FlatButtonColor{ 28, 65, 54, 255 }
			: current.unlocked ? FlatButtonColor{ 62, 45, 28, 255 } : FlatButtonColor{ 35, 36, 40, 255 };
		style.normal.textColor = current.unlocked ? TextColor : 0xFFAAAAAA;
		style.hovered.background = { 84, 67, 34, 255 };
		style.hovered.border = { 249, 215, 126, 255 };
		style.borderThickness = i == selected ? 3 : 1;
		nodeButtons[i]->setStyle(style);
	}
}

bool SkillTreeMenu::onHandleUIAction(UIAction action)
{
	if (!visible) return false;
	switch (action)
	{
	case UIAction::NavigateUp: navigate(0, -1); break;
	case UIAction::NavigateDown: navigate(0, 1); break;
	case UIAction::NavigateLeft: navigate(-1, 0); break;
	case UIAction::NavigateRight: navigate(1, 0); break;
	case UIAction::PanelPrevious: changeGroup(-1); break;
	case UIAction::PanelNext: changeGroup(1); break;
	case UIAction::ScrollUp: scroll(-1); break;
	case UIAction::ScrollDown: scroll(1); break;
	case UIAction::PagePrevious:
	case UIAction::ScrollLeft: turnDetails(-1); break;
	case UIAction::PageNext:
	case UIAction::ScrollRight:
	case UIAction::Details: turnDetails(1); break;
	case UIAction::Confirm: confirmLearning(); break;
	case UIAction::Cancel:
		if (confirming) { confirming = false; notice.clear(); refreshContent(); }
		else close();
		break;
	default: break;
	}
	return true;
}

bool SkillTreeMenu::onHandleEvent(AEvent& event)
{
	if (event.eventType == ET_KEYDOWN)
	{
		if (event.eventData == SDL_SCANCODE_PAGEUP || event.eventData == SDL_SCANCODE_PAGEDOWN)
			return handleUIAction(event.eventData == SDL_SCANCODE_PAGEUP ? UIAction::PagePrevious : UIAction::PageNext);
		if (event.eventData == SDL_SCANCODE_TAB) return handleUIAction(UIAction::PanelNext);
		return dispatchKeyboardUIAction(event, *this) || visible;
	}
	if (event.eventType == ET_MOUSEWHEEL && event.eventData != 0)
	{
		scroll(event.eventData > 0 ? -1 : 1);
		return true;
	}
	return false;
}

void SkillTreeMenu::onChildCallBack(PElement child)
{
	if (child == nullptr || !child->getResult(erClick)) return;
	if (child == learnButton) confirmLearning();
	else if (child == closeButton) close();
	else if (child == upButton) scroll(-1);
	else if (child == downButton) scroll(1);
	else if (child == previousGroup) changeGroup(-1);
	else if (child == nextGroup) changeGroup(1);
	else if (child == previousDetails) turnDetails(-1);
	else if (child == nextDetails) turnDetails(1);
	else
	{
		const auto found = std::find(nodeButtons.begin(), nodeButtons.end(), child);
		if (found != nodeButtons.end()) selectNode(static_cast<std::size_t>(found - nodeButtons.begin()));
	}
}

void SkillTreeMenu::onWindowResize(int width, int height)
{
	if (visible) layout(width, height);
}

void SkillTreeMenu::onUpdate()
{
	if (!visible) return;
	const auto& tree = gm->magicManager.skillTree();
	if (gm->inEvent || !gm->global.data.canInput || !gm->global.skillTreeEnabled || openedCharacterIndex != gm->global.data.characterIndex
		|| tree.nodes.size() != nodeIds.size()) { close(false); return; }
	for (std::size_t i = 0; i < nodeIds.size(); ++i)
		if (nodeIds[i] != tree.nodes[i].id) { close(false); return; }
	refreshContent();
}

void SkillTreeMenu::drawText(CachedTextTexture& cache, const std::string& text,
	int x, int y, int width, int font, unsigned int color)
{
	const auto& image = cache.get(engine, text, font, color);
	int w = 0, h = 0;
	if (width <= 0 || !engine->getImageSize(image, w, h)) return;
	Rect source = { 0, 0, std::min(w, width), h };
	Rect destination = { x, y, source.w, source.h };
	engine->drawImageWithAlpha(image, &source, &destination, 255);
}

void SkillTreeMenu::onDraw()
{
	if (engine == nullptr || gm == nullptr) return;
	const auto& tree = gm->magicManager.skillTree();
	if (selected >= tree.nodes.size() || group >= tree.groups.size()) return;
	engine->fillRect(rect.x, rect.y, rect.w, rect.h, 8, 10, 12, 200);
	engine->fillRect(panel.x, panel.y, panel.w, panel.h, 21, 27, 29, 255);
	engine->fillRect(chart.x, chart.y, chart.w, chart.h, 12, 18, 20, 255);
	drawText(titleTexture, u8"技能树 · " + tree.groups[group].name, panel.x + 12, panel.y + 14,
		panel.w - 186, panel.w < 340 ? 18 : 22, TextColor);
	drawText(pointsTexture, u8"技能点：" + std::to_string(gm->player->skillPoints), panel.x + panel.w - 164,
		panel.y + 17, 152, 18, 0xFFFFD783);
	const auto stroke = [&](Rect segment)
	{
		const int right = std::min(segment.x + segment.w, chart.x + chart.w);
		const int bottom = std::min(segment.y + segment.h, chart.y + chart.h);
		segment.x = std::max(segment.x, chart.x);
		segment.y = std::max(segment.y, chart.y);
		if (right > segment.x && bottom > segment.y)
			engine->fillRect(segment.x, segment.y, right - segment.x, bottom - segment.y, 145, 119, 70, 255);
	};
	for (std::size_t i = 0; i < tree.nodes.size(); ++i)
	{
		if (tree.nodes[i].groupId != tree.groups[group].id) continue;
		for (auto child : tree.nodes[i].children)
		{
			if (tree.nodes[child].groupId != tree.groups[group].id) continue;
			const auto& a = nodeButtons[i]->rect;
			const auto& b = nodeButtons[child]->rect;
			const int x1 = a.x + a.w / 2, x2 = b.x + b.w / 2;
			const int y1 = a.y + a.h / 2, y2 = b.y + b.h / 2;
			const int middle = y1 + (y2 - y1) / 2;
			stroke({ x1, std::min(y1, middle), 2, std::abs(middle - y1) + 2 });
			stroke({ std::min(x1, x2), middle, std::abs(x2 - x1) + 2, 2 });
			stroke({ x2, std::min(middle, y2), 2, std::abs(y2 - middle) + 2 });
		}
	}
	drawText(selectedTexture, names[selected], details.x, details.y + 1, details.w - 104, 20, TextColor);
	const auto& node = tree.nodes[selected];
	const int pages = std::max(1, static_cast<int>((detailLines.size() + detailLinesPerPage - 1) / detailLinesPerPage));
	drawText(pageTexture, std::to_string(node.currentLevel) + " / " + std::to_string(node.maximumLevel)
		+ u8"  详情 " + std::to_string(detailPage + 1) + "/" + std::to_string(pages),
		details.x, details.y + 26, details.w - 104, 15, 0xFFC3C8C3);
	detailTextures.resize(static_cast<std::size_t>(detailLinesPerPage) * 32);
	std::size_t texture = 0;
	for (int line = 0; line < detailLinesPerPage; ++line)
	{
		const auto index = static_cast<std::size_t>(detailPage * detailLinesPerPage + line);
		if (index >= detailLines.size()) break;
		int x = details.x;
		for (const auto& run : detailLines[index])
		{
			if (texture >= detailTextures.size()) detailTextures.emplace_back();
			drawText(detailTextures[texture], run.text, x, details.y + 52 + line * 22,
				details.x + details.w - x, 18, run.color);
			int w = 0, h = 0;
			engine->getImageSize(detailTextures[texture++].get(engine, run.text, 18, run.color), w, h);
			x += w;
		}
	}
}

void SkillTreeMenu::onDrawEnd()
{
	if (engine == nullptr || Element::currentRunOwnerBlocksParentInput()) return;
	if (!shouldPresentGamepadFocus(engine))
	{
		promptTextures.itemTextTextures.resize(1);
		drawText(promptTextures.itemTextTextures.front(), u8"点选技能 · 方向键选择 · Enter确认 · Esc返回",
			panel.x + 12, panel.y + panel.h - 25, panel.w - 24, 12, TextColor);
		return;
	}
	ControllerPromptDrawOptions options;
	options.x = panel.x + 12;
	options.y = panel.y + panel.h - 27;
	options.width = panel.w - 24;
	options.height = 24;
	options.fontSize = 12;
	using GameInput::InputAction;
	ControllerPromptPresenter::draw(engine, engine->inputActions(),
		{ { InputAction::Confirm, confirming ? u8"确认扣点" : u8"学习预览" },
		  { InputAction::Cancel, confirming ? u8"取消" : u8"返回" },
		  { InputAction::PreviousPage, u8"详情", { InputAction::NextPage } } }, options, promptTextures);
}
