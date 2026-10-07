#include "MagicMenu.h"
#include "../GameManager/GameManager.h"
#include "../../libconvert/libconvert.h"
#include "MenuResource.h"
#include "../../Component/TextLayout.h"
#include <algorithm>
#include <utility>


MagicMenu::MagicMenu()
{
	name = "MagicMenu";
	visible = false;
	init();
}


MagicMenu::~MagicMenu()
{
	freeResource();
}

int MagicMenu::detailedMagicIndex() const
{
	if (detailedMagicFile.empty()) return -1;
	for (int index = displayBegin(); index < displayBegin() + displayCount(); ++index)
	{
		if (gm->magicManager.magicListExists(index)
			&& gm->magicManager.magicList[index].iniFile == detailedMagicFile) return index;
	}
	return -1;
}

bool MagicMenu::showDetails(int listIndex)
{
	if (!detailText || !isDisplayIndex(listIndex) || !gm->magicManager.magicListExists(listIndex)) return false;
	cancelControllerInteraction();
	detailedMagicFile = gm->magicManager.magicList[listIndex].iniFile;
	detailContent.clear();
	showingDetails = true;
	detailPage = 0;
	setDetailVisibility();
	updateDetails();
	detailFocused = detailFocus.focusDefault();
	return true;
}

void MagicMenu::setDetailVisibility()
{
	if (!detailText) return;
	const bool hasTalents = gm->global.magicLayout.talentBegin >= 0;
	for (const auto& entry : componentMap)
	{
		if (entry.first.rfind("heading", 0) == 0) continue;
		const bool detail = entry.first.rfind("detail", 0) == 0;
		entry.second->visible = entry.second->activated = detail == showingDetails;
	}
	if (magicTab) magicTab->visible = magicTab->activated = !showingDetails && hasTalents;
	if (talentTab) talentTab->visible = talentTab->activated = !showingDetails && hasTalents;
	if (auto caption = getComponentByName("listCaption"))
		caption->visible = caption->activated = !showingDetails && !hasTalents;
	if (auto heading = getComponentByName<Label>("heading"))
		heading->setStr(showingDetails ? (showingTalents ? u8"天赋详情" : u8"武功详情") : (hasTalents ? u8"武学" : u8"武功"));
	for (const char* name : { "detailPractice", "detailQuick" })
	{
		if (auto button = getComponentByName<FlatTextButton>(name))
			button->visible = button->activated = showingDetails && !showingTalents;
	}
}

void MagicMenu::closeDetails()
{
	if (!showingDetails) return;
	const int index = detailedMagicIndex();
	showingDetails = false;
	detailFocused = false;
	detailFocus.suspendFocus();
	setDetailVisibility();
	slotController.activate();
	if (index >= 0) slotController.focusLogicalIndex(index);
}

bool MagicMenu::assignDetailedMagic(bool practice)
{
	const int source = detailedMagicIndex();
	if (!showingDetails || showingTalents || source < 0 || !gm->magicManager.isStoreIndex(source)) return false;
	int target = practice ? gm->magicManager.practiceIndex() : -1;
	if (practice && gm->global.feature.practiceMenuDisabled) return false;
	if (!practice)
	{
		for (int index = gm->magicManager.bottomBegin(); index <= gm->magicManager.bottomEnd(); ++index)
		{
			if (!gm->magicManager.magicListExists(index))
			{
				target = index;
				break;
			}
		}
	}
	if (target < 0)
	{
		gm->showMessage(u8"快捷武功栏已满");
		return false;
	}
	gm->magicManager.exchange(source, target);
	closeDetails();
	gm->magicManager.updateMenu();
	gm->showMessage(practice ? u8"已设为修炼武功" : u8"已放入快捷武功栏");
	return true;
}

void MagicMenu::changeDetailPage(int direction)
{
	if (!showingDetails) return;
	detailPage += direction;
	updateDetails();
}

void MagicMenu::updateDetails()
{
	if (!showingDetails || !detailText || detailText->mstr.empty()) return;
	const int index = detailedMagicIndex();
	if (index < 0)
	{
		closeDetails();
		return;
	}
	const auto& info = gm->magicManager.magicList[index];
	const auto& magic = info.magic;
	const int level = std::clamp(info.level, 1, MAGIC_MAX_LEVEL);
	const auto& attributes = magic->level[level];
	if (auto label = getComponentByName<Label>("detailName")) label->setStr(magic->name);
	if (auto icon = getComponentByName<ImageContainer>("detailIcon"))
	{
		if (icon->impImage == nullptr || detailContent.empty()) icon->impImage = MenuResource::createMagicMenuImage(magic);
	}
	std::string text = convert::formatString(showingTalents ? u8"天赋等级：%d\n" : u8"武功等级：%d\n", level);
	const int limit = magic->definedLearningLevelLimit > 0 ? std::min(MAGIC_MAX_LEVEL, magic->definedLearningLevelLimit) : MAGIC_MAX_LEVEL;
	text += level >= limit ? u8"已达等级上限\n" : convert::formatString(u8"升级经验：%d / %d\n", info.exp, attributes.levelupExp);
	if (!showingTalents)
	{
		text += convert::formatString(u8"消耗：命%d 体%d 气%d\n", attributes.lifeCost, attributes.thewCost, attributes.manaCost);
		if (gm->global.feature.rageSystem && attributes.hasRageCost)
			text += convert::formatString(u8"怒气消耗：%d\n", attributes.rageCost);
		text += convert::formatString(u8"冷却：%.1f 秒\n", static_cast<double>(magic->coldMilliSeconds) / 1000.0);
		if (info.remainColdMilliseconds > 0)
			text += u8"剩余：" + std::to_string(info.remainColdMilliseconds / 1000 + (info.remainColdMilliseconds % 1000 != 0)) + u8" 秒\n";
	}
	for (const auto& value : std::vector<std::pair<const char*, int>>{
		{ u8"生命上限", attributes.lifeMax }, { u8"体力上限", attributes.thewMax }, { u8"内力上限", attributes.manaMax },
		{ u8"攻击", attributes.attack }, { u8"附加攻击一", attributes.attack2 }, { u8"附加攻击二", attributes.attack3 },
		{ u8"防御", attributes.defend }, { u8"附加防御一", attributes.defend2 }, { u8"附加防御二", attributes.defend3 },
		{ u8"身法", attributes.evade }, { u8"跳跃距离", attributes.jumpRadius } })
	{
		if (value.second != 0) text += convert::formatString("%s %+d\n", value.first, value.second);
	}
	text += "\n" + (magic->intro.empty() ? std::string(u8"暂无简介") : magic->intro);
	const int width = detailText->mstr.front()->rect.w;
	if (detailContent != text || detailWidth != width)
	{
		detailContent = text;
		detailWidth = width;
		detailLines.clear();
		for (const auto& line : TextLayout::wrapColorTaggedUtf8Text(text,
			TextLayout::charactersPerLineForWidth(width, detailText->fontSize), detailText->color))
		{
			std::string plain;
			for (const auto& run : line) plain += run.text;
			detailLines.push_back(std::move(plain));
		}
	}
	const int count = static_cast<int>(detailText->mstr.size());
	const int pages = std::max(1, (static_cast<int>(detailLines.size()) + count - 1) / count);
	detailPage = std::clamp(detailPage, 0, pages - 1);
	for (int row = 0; row < count; ++row)
	{
		const int line = detailPage * count + row;
		detailText->mstr[row]->setStr(line < static_cast<int>(detailLines.size()) ? detailLines[line] : "");
	}
	if (auto label = getComponentByName<Label>("detailPage")) label->setStr(convert::formatString("%d / %d", detailPage + 1, pages));
	if (auto button = getComponentByName<FlatTextButton>("detailPrevious")) button->visible = button->activated = detailPage > 0;
	if (auto button = getComponentByName<FlatTextButton>("detailNext")) button->visible = button->activated = detailPage + 1 < pages;
}

void MagicMenu::configureDetailFocus()
{
	detailFocus.clear();
	detailFocus.setInputAwarePresentation();
	if (!detailText) return;
	detailFocus.addVisualSpatialGroup("magic-details", {
		{ "detail-previous", getComponentByName("detailPrevious"), [this]() { changeDetailPage(-1); } },
		{ "detail-next", getComponentByName("detailNext"), [this]() { changeDetailPage(1); } },
		{ "detail-practice", getComponentByName("detailPractice"), [this]() { assignDetailedMagic(true); } },
		{ "detail-quick", getComponentByName("detailQuick"), [this]() { assignDetailedMagic(false); } },
		{ "detail-back", getComponentByName("detailBack"), [this]() { closeDetails(); } } });
	detailFocus.setDefaultFocus("detail-back");
	detailFocus.setCancelHandler([this]() { closeDetails(); });
	detailFocus.setPagePreviousHandler([this]() { changeDetailPage(-1); });
	detailFocus.setPageNextHandler([this]() { changeDetailPage(1); });
}

int MagicMenu::displayBegin() const
{
	return showingTalents ? gm->global.magicLayout.talentBegin : gm->magicManager.storeBegin();
}

int MagicMenu::displayCount() const
{
	return showingTalents ? gm->global.magicLayout.talentEnd - displayBegin() + 1 : gm->global.magicLayout.storeCount();
}

bool MagicMenu::isDisplayIndex(int index) const
{
	return index >= displayBegin() && index < displayBegin() + displayCount();
}

void MagicMenu::showTalents(bool value)
{
	closeDetails();
	const bool keepFocus = isControllerFocusActive();
	const bool previousCategory = showingTalents;
	cancelControllerInteraction();
	showingTalents = value && talentTab != nullptr && talentTab->visible;
	if (magicTab) magicTab->checked = !showingTalents;
	if (talentTab) talentTab->checked = showingTalents;
	if (scrollbar)
	{
		const int columns = std::max(1, scrollbar->lineSize);
		scrollbar->max = (std::max(0, displayCount() - static_cast<int>(item.size())) + columns - 1) / columns;
		scrollbar->setPosition(0);
	}
	for (auto& slot : item)
	{
		slot->canDrag = slot->canDrop = !showingTalents;
	}
	configureControllerFocus();
	updateMagic();
	if (auto hint = getComponentByName<Label>("hint"))
	{
		hint->setStr(showingTalents ? u8"已习得天赋 · 点击查看" : u8"点击查看详情 · 拖动整理");
	}
	if (previousCategory != showingTalents && !item.empty())
	{
		slotController.focusControllerElement(item.front());
		if (!keepFocus) slotController.deactivate();
	}
	else if (keepFocus) focusControllerDefault();
}

void MagicMenu::updateMagic()
{
	if (scrollbar != nullptr)
	{
		position = scrollbar->position;
	}
	for (size_t i = 0; i < item.size(); i++)
	{
		if (item[i] && scrollbar)
		{
			item[i]->dragIndex = displayBegin() + static_cast<int>(i) + scrollbar->position * scrollbar->lineSize;
		}
		updateMagic(static_cast<int>(i));
	}
	refreshControllerTransferHighlight();
}

void MagicMenu::updateMagic(int index)
{
	if (index < 0 || index >= static_cast<int>(item.size()))
	{
		return;
	}
	if (item[index] == nullptr || scrollbar == nullptr)
	{
		return;
	}

	item[index]->impImage = nullptr;

	int listIndex = displayBegin() + scrollbar->position * scrollbar->lineSize + index;
	MenuResource::updateMagicCooldown(item[index], gm->magicManager, listIndex);
	if (!isDisplayIndex(listIndex) || listIndex >= gm->magicManager.listLength())
	{
		return;
	}
	if (gm->magicManager.magicList[listIndex].magic != nullptr)
	{
		item[index]->impImage = MenuResource::createMagicMenuImage(gm->magicManager.magicList[listIndex].magic);
	}
}

void MagicMenu::onUpdate()
{
	if (visible && gm != nullptr)
	{
		updateDetails();
		for (size_t i = 0; i < item.size(); ++i)
		{
			MenuResource::updateMagicCooldown(item[i], gm->magicManager, getControllerItemIndex(static_cast<int>(i)));
		}
	}
}

void MagicMenu::onEvent()
{
	if (showingDetails)
	{
		updateDetails();
		for (const auto& name : { "detailPrevious", "detailNext", "detailPractice", "detailQuick", "detailBack" })
		{
			auto button = getComponentByName<FlatTextButton>(name);
			if (!button || !button->visible || !button->activated || !button->getResult(erClick)) continue;
			const std::string key = name;
			if (key == "detailPrevious") changeDetailPage(-1);
			else if (key == "detailNext") changeDetailPage(1);
			else if (key == "detailBack") closeDetails();
			else assignDetailedMagic(key == "detailPractice");
			break;
		}
		return;
	}
	if (magicTab && magicTab->getResult(erClick)) showTalents(false);
	if (talentTab && talentTab->getResult(erClick)) showTalents(true);
	if (gm != nullptr && gm->menu != nullptr
		&& gm->menu->controllerTransfers().active(ControllerSlotKind::Magic)
		&& currentDragItem != nullptr)
	{
		cancelControllerInteraction();
	}
	if (scrollbar != nullptr && position != scrollbar->position)
	{
		hideControllerItemDetails();
		position = scrollbar->position;
		updateMagic();
	}
	if (scrollbar == nullptr)
	{
		return;
	}

	for (size_t i = 0; i < item.size(); i++)
	{
		if (item[i] == nullptr) continue;

		int listIndex = displayBegin() + scrollbar->position * scrollbar->lineSize + static_cast<int>(i);
		int ret = item[i]->getResult();
		if (detailText && (ret & erClick))
		{
			showDetails(listIndex);
			return;
		}
		if (ret & erShowHint)
		{
			if (scrollbar && gm->magicManager.magicListExists(listIndex))
			{
				gm->menu->showMagicToolTip(
					getMySharedPtr(),
					gm->magicManager.magicList[listIndex].magic,
					gm->magicManager.magicList[listIndex].level,
					item[i]);
			}
			else
			{
				gm->menu->toolTip->visible = false;
			}

		}
		if (ret & erHideHint)
		{
			gm->menu->toolTip->visible = false;
		}
		if (showingTalents)
		{
			if (ret & (erClick | erMouseRDown)) showControllerItemDetails(static_cast<int>(i));
			continue;
		}
#ifdef __MOBILE__
		if (ret & erClick || ret & erMouseRDown)
#else
		if (ret & erMouseRDown)
#endif
		{
			cancelControllerInteraction();
			if (gm->magicManager.magicListExists(item[i]->dragIndex))
			{
				if (gm->menu->practiceMenu != nullptr && gm->menu->practiceMenu->visible == true)
				{
					gm->magicManager.exchange(item[i]->dragIndex, gm->magicManager.practiceIndex());
					updateMagic(i);
					gm->menu->practiceMenu->updateMagic();
				}
				else if (gm->menu->bottomMenu != nullptr)
				{
					for (int j = gm->magicManager.bottomBegin(); j <= gm->magicManager.bottomEnd(); ++j)
					{
						if (!gm->magicManager.magicListExists(j))
						{
							gm->magicManager.exchange(item[i]->dragIndex, j);
							updateMagic(i);
							gm->menu->bottomMenu->updateMagicItem(gm->magicManager.bottomSlot(j));
							break;
						}
					}
				}
			}

			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
		}
		if (ret & erDropped)
		{
			cancelControllerInteraction();
			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
			if (item[i]->dropType == dtMagic)
			{
				if (gm->magicManager.magicListExists(item[i]->dropIndex))
				{
					if (gm->magicManager.isStoreIndex(item[i]->dropIndex))
					{
						gm->magicManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateMagic();
					}
					else if (gm->magicManager.isBottomIndex(item[i]->dropIndex))
					{
						gm->magicManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateMagic(i);
						gm->menu->bottomMenu->updateMagicItem();
					}
					else if (gm->magicManager.isPracticeIndex(item[i]->dropIndex))
					{
						gm->magicManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateMagic(i);
						gm->menu->practiceMenu->updateMagic();
					}
				}
			}
		}
	}
}

void MagicMenu::init()
{
	freeResource();
	loadMenuDefinition("ini\\ui\\magic\\magic.menu.ini");

	title = getComponentByName<ImageContainer>("title");
	image = getComponentByName<ImageContainer>("image");
	scrollbar = getComponentByName<Scrollbar>("scrollbar");
	magicTab = getComponentByName<CheckBox>("magicTab");
	talentTab = getComponentByName<CheckBox>("talentTab");
	detailText = getComponentByName<MemoText>("detailText");
	if (detailText) detailText->canDrag = false;
	if (talentTab)
	{
		talentTab->visible = gm->global.magicLayout.talentBegin >= 0;
		talentTab->activated = talentTab->visible;
	}

	item.clear();
	for (int i = 1;; i++)
	{
		std::string itemName = convert::formatString("item%d", i);
		auto itemComponent = getComponentByName<Item>(itemName);
		if (!itemComponent)
		{
			break;
		}
		itemComponent->dragType = dtMagic;
		itemComponent->canShowHint = true;
		item.push_back(itemComponent);
	}

	if (scrollbar != nullptr)
	{
		scrollbar->pageSize = static_cast<int>(item.size());
		int lineSize = std::max(1, scrollbar->lineSize);
		int visibleCount = std::max(1, scrollbar->pageSize);
		int scrollableCount = std::max(0, gm->global.magicLayout.storeCount() - visibleCount);
		scrollbar->min = 0;
		scrollbar->max = (scrollableCount + lineSize - 1) / lineSize;
		scrollbar->position = scrollbar->min;
	}

	setChildRectReferToParent();
	configureDetailFocus();
	showTalents(showingTalents);
	setDetailVisibility();
}

void MagicMenu::freeResource()
{
	showingDetails = false;
	detailFocused = false;
	detailFocus.clear();
	detailText = nullptr;
	detailContent.clear();
	detailedMagicFile.clear();
	tabFocused = false;
	magicTab = nullptr;
	talentTab = nullptr;
	slotController.clear();
	title = nullptr;
	image = nullptr;
	scrollbar = nullptr;
	item.clear();
	ConfigDrivenPanel::freeResource();
}

void MagicMenu::configureControllerFocus()
{
	SlotInteractionBinding binding =
		MenuController::makeControllerSlotInteractionBinding(
			gm,
			ControllerSlotKind::Magic,
			ControllerSlotDomain::MagicList);
	binding.grid.focusIdPrefix = "magic-item-";
	if (showingTalents) binding.transfers = nullptr;
	binding.grid.items = item;
	binding.grid.scrollbar = scrollbar;
	binding.grid.resolveLogicalIndex = [this](int visibleIndex)
	{
		return getControllerItemIndex(visibleIndex);
	};
	binding.grid.primary = [this](int, int visibleIndex)
	{
		activateControllerItem(visibleIndex);
	};
	binding.grid.details = [this](int, int visibleIndex)
	{
		showControllerItemDetails(visibleIndex);
	};
	binding.grid.hideDetails = [this]() { hideControllerItemDetails(); };
	binding.grid.refreshAfterScroll = [this]()
	{
		position = scrollbar != nullptr ? scrollbar->position : -1;
		updateMagic();
	};
	slotController.bind(std::move(binding));
}

bool MagicMenu::activateControllerFocus(ControllerFocusTarget target)
{
	return (target == ControllerFocusTarget::Default
		|| target == ControllerFocusTarget::MagicList)
		&& focusControllerDefault();
}

bool MagicMenu::focusControllerDefault()
{
	deactivateControllerFocus();
	if (showingDetails) return detailFocused = detailFocus.focusDefault();
	return slotController.activate();
}

bool MagicMenu::isControllerFocusActive() const
{
	return (showingDetails && detailFocused) || tabFocused || slotController.isActive();
}

void MagicMenu::deactivateControllerFocus()
{
	detailFocused = false;
	detailFocus.suspendFocus();
	tabFocused = false;
	if (magicTab) magicTab->setFocused(false);
	if (talentTab) talentTab->setFocused(false);
	slotController.deactivate();
}

PElement MagicMenu::controllerFocusedElement() const
{
	if (showingDetails) return detailFocused ? detailFocus.getFocusedElement() : nullptr;
	if (tabFocused) return showingTalents ? talentTab : magicTab;
	return slotController.controllerFocusedElement();
}

std::vector<PElement> MagicMenu::controllerFocusCandidates() const
{
	if (showingDetails) return detailFocus.getAvailableFocusElements();
	auto candidates = slotController.controllerFocusCandidates();
	if (magicTab && talentTab && talentTab->visible)
	{
		candidates.push_back(magicTab);
		candidates.push_back(talentTab);
	}
	return candidates;
}

bool MagicMenu::focusControllerElement(const PElement& element)
{
	if (showingDetails) return detailFocused = detailFocus.focusElement(element);
	if (element != nullptr && element->visible && element->activated
		&& (element == magicTab || element == talentTab))
	{
		showTalents(element == talentTab);
		deactivateControllerFocus();
		tabFocused = true;
		element->setFocused(true);
		return true;
	}
	deactivateControllerFocus();
	return slotController.focusControllerElement(element);
}

int MagicMenu::getControllerItemIndex(int visibleIndex) const
{
	if (scrollbar == nullptr || visibleIndex < 0
		|| visibleIndex >= static_cast<int>(item.size())
		|| item[visibleIndex] == nullptr)
	{
		return -1;
	}
	const int listIndex = displayBegin()
		+ visibleIndex + scrollbar->position * scrollbar->lineSize;
	return isDisplayIndex(listIndex)
		&& listIndex < gm->magicManager.listLength()
		? listIndex
		: -1;
}

void MagicMenu::activateControllerItem(int visibleIndex)
{
	if (detailText)
	{
		showDetails(getControllerItemIndex(visibleIndex));
		return;
	}
	if (showingTalents)
	{
		showControllerItemDetails(visibleIndex);
		return;
	}
	const int sourceIndex = getControllerItemIndex(visibleIndex);
	if (sourceIndex < 0 || !gm->magicManager.magicListExists(sourceIndex))
	{
		return;
	}
	hideControllerItemDetails();
	if (gm->menu != nullptr && gm->menu->practiceMenu != nullptr
		&& gm->menu->practiceMenu->visible)
	{
		gm->magicManager.exchange(sourceIndex, gm->magicManager.practiceIndex());
		gm->magicManager.updateMenu();
		return;
	}
	for (int targetIndex = gm->magicManager.bottomBegin();
		targetIndex <= gm->magicManager.bottomEnd(); targetIndex++)
	{
		if (!gm->magicManager.magicListExists(targetIndex))
		{
			gm->magicManager.exchange(sourceIndex, targetIndex);
			gm->magicManager.updateMenu();
			return;
		}
	}
	gm->showMessage("快捷武功栏已满");
}
void MagicMenu::showControllerItemDetails(int visibleIndex)
{
	const int listIndex = getControllerItemIndex(visibleIndex);
	if (detailText)
	{
		showDetails(listIndex);
		return;
	}
	if (gm->menu == nullptr || gm->menu->toolTip == nullptr
		|| gm->menu->upMenu == nullptr || listIndex < 0
		|| !gm->magicManager.magicListExists(listIndex))
	{
		hideControllerItemDetails();
		return;
	}
	gm->menu->showMagicToolTip(
		getMySharedPtr(),
		gm->magicManager.magicList[listIndex].magic,
		gm->magicManager.magicList[listIndex].level,
		item[visibleIndex]);
}

void MagicMenu::hideControllerItemDetails()
{
	if (gm != nullptr && gm->menu != nullptr)
	{
		gm->menu->hideToolTip();
	}
}

void MagicMenu::refreshControllerTransferHighlight()
{
	slotController.refreshTransferSelection();
}

void MagicMenu::cancelControllerInteraction()
{
	if (gm != nullptr && gm->menu != nullptr
		&& gm->menu->controllerTransfers().active(ControllerSlotKind::Magic))
	{
		gm->menu->controllerTransfers().cancel();
	}
	deactivateControllerFocus();
	refreshControllerTransferHighlight();
	hideControllerItemDetails();
}

bool MagicMenu::onHandleUIAction(UIAction action)
{
	if (showingDetails) return detailFocused && detailFocus.handleAction(action);
	if (magicTab && talentTab && talentTab->visible)
	{
		if (tabFocused)
		{
			if (action == UIAction::NavigateLeft || action == UIAction::NavigateRight)
			{
				deactivateControllerFocus();
				showTalents(!showingTalents);
				tabFocused = true;
				(showingTalents ? talentTab : magicTab)->setFocused(true);
				return true;
			}
			if (action == UIAction::Confirm || action == UIAction::NavigateDown)
			{
				deactivateControllerFocus();
				return focusControllerDefault();
			}
			if (action == UIAction::Cancel) deactivateControllerFocus();
			else return false;
		}
		else if (action == UIAction::NavigateUp && scrollbar
			&& scrollbar->position == 0
			&& slotController.focusedVisibleIndex() >= 0
			&& slotController.focusedVisibleIndex() < scrollbar->lineSize)
		{
			slotController.deactivate();
			tabFocused = true;
			(showingTalents ? talentTab : magicTab)->setFocused(true);
			return true;
		}
	}
	return slotController.handleAction(action);
}
