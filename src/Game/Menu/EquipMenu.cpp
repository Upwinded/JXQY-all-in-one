#include "EquipMenu.h"
#include "../GameManager/GameManager.h"
#include "../../libconvert/libconvert.h"
#include "../Data/Global.h"
#include "MenuResource.h"
#include "../../File/INIReader.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace
{
std::string normalizeEquipmentPart(std::string part)
{
	std::transform(part.begin(), part.end(), part.begin(),
		[](unsigned char ch)
		{
			return static_cast<char>(std::tolower(ch));
		});
	return part;
}
}

EquipMenu::EquipMenu()
{
	name = "EquipMenu";
	visible = false;
	init();
}


EquipMenu::~EquipMenu()
{
	freeResource();
}

void EquipMenu::showAttributes(bool value)
{
	if (!equipmentTab || !attributesTab) return;
	deactivateControllerFocus();
	if (gm && gm->menu) gm->menu->cancelControllerInteraction();
	showingAttributes = value;
	equipmentTab->checked = !value;
	attributesTab->checked = value;
	for (const auto& entry : componentMap)
	{
		const auto& key = entry.first;
		if (key.rfind("detail", 0) == 0)
		{
			entry.second->visible = entry.second->activated = value;
		}
		else if (key.rfind("item", 0) == 0 || key.rfind("part", 0) == 0 || key.rfind("lab", 0) == 0)
		{
			entry.second->visible = entry.second->activated = !value;
		}
	}
	updateAttributes();
}

void EquipMenu::updateAttributes()
{
	if (!showingAttributes || !gm || !gm->player) return;
	const auto partner = getSelectedPartner();
	const auto player = partner ? partner : std::static_pointer_cast<NPC>(gm->player);
	std::vector<std::string> values = {
		convert::formatString(u8"攻击  %d", player->getAttack()),
		convert::formatString(u8"防御  %d", player->getDefend()),
		convert::formatString(u8"附加攻击一  %d", player->getAttack2()),
		convert::formatString(u8"附加防御一  %d", player->getDefend2()),
		convert::formatString(u8"附加攻击二  %d", player->getAttack3()),
		convert::formatString(u8"附加防御二  %d", player->getDefend3()),
		convert::formatString(u8"身法  %d", player->getEvade()),
		convert::formatString(u8"医术  %d", player->leechcraft),
		convert::formatString(u8"跳跃距离  %d", player->jumpRadius),
		convert::formatString(u8"移速倍率  %.2f", player->getMoveSpeedFold()),
		convert::formatString(u8"银两  %d", gm->player->money) };
	if (gm->global.feature.rageSystem && !partner)
	{
		values.push_back(convert::formatString(u8"怒气  %d / %d", gm->player->rage, gm->player->rageMax));
		values.push_back(convert::formatString(u8"暴击概率  %.1f%%", gm->player->getCriticalChancePercent()));
		values.push_back(convert::formatString(u8"暴击增伤  %d%%", gm->player->getCriticalDamagePercent()));
	}
	for (int index = 0; index < 14; ++index)
	{
		if (auto label = getComponentByName<Label>("detail" + std::to_string(index)))
		{
			label->setStr(index < static_cast<int>(values.size()) ? values[index] : "");
		}
	}
}

void EquipMenu::updateCharacterBindings()
{
	const auto partner = getSelectedPartner();
	if (!partner)
	{
		updateDataBindings();
		return;
	}
	const std::map<std::string, int> values = {
		{ "player.level", partner->level }, { "player.exp", partner->exp },
		{ "player.levelUpExp", partner->levelUpExp },
		{ "player.info.attack", partner->getAttack() }, { "player.effectiveAttack", partner->getAttack() },
		{ "player.info.defend", partner->getDefend() }, { "player.effectiveDefend", partner->getDefend() },
		{ "player.info.evade", partner->getEvade() }, { "player.effectiveEvade", partner->getEvade() },
		{ "player.life", partner->life }, { "player.info.lifeMax", partner->getLifeMax() },
		{ "player.thew", partner->thew }, { "player.info.thewMax", partner->getThewMax() },
		{ "player.mana", partner->mana }, { "player.info.manaMax", partner->getManaMax() }
	};
	for (const auto& binding : dataBindings)
	{
		auto label = getComponentByName<Label>(binding.componentName);
		if (!label) continue;
		std::vector<int> numbers;
		for (const auto& path : binding.bindPaths)
		{
			const auto found = values.find(path);
			numbers.push_back(found != values.end() ? found->second : gm->getBindValue(path));
		}
		std::string text;
		if (convert::formatIntegerValues(binding.formatStr, numbers, text)) label->setStr(text);
	}
}

void EquipMenu::updateGoods()
{
	if (showingPartner && !getSelectedPartner())
	{
		selectPartner(nullptr);
		return;
	}
	updatePanelImage();
	updatePlayerNameDisplay();

	for (size_t i = 0; i < GOODS_BODY_COUNT; i++)
	{
		updateGoods(i);
	}
	refreshControllerTransferHighlight();
}

void EquipMenu::updateGoods(int index)
{
	if (index < 0 || index >= GOODS_BODY_COUNT)
	{
		return;
	}
	if (item[index] == nullptr)
	{
		return;
	}
	const auto partner = getSelectedPartner();
	item[index]->canDrag = item[index]->canDrop = !partner || partner->canChangePartnerEquipment();
	item[index]->dragType = partner ? dtPartnerEquipment : dtGoods;
	item[index]->dragIndex = partner ? index : gm->goodsManager.equipIndex(index);
	if (partner)
	{
		const auto file = partner->getEquipmentFileByPartIndex(index);
		partnerGoods[index].reset();
		item[index]->impImage = nullptr;
		item[index]->setStr("");
		if (!file.empty())
		{
			partnerGoods[index] = std::make_shared<Goods>();
			partnerGoods[index]->initFromIni(file);
			item[index]->impImage = MenuResource::createGoodsMenuImage(partnerGoods[index]);
			item[index]->setStr("1");
		}
		updateCharacterBindings();
		return;
	}
	partnerGoods[index].reset();
	int listIndex = gm->goodsManager.equipIndex(index);
	if (listIndex < 0 || listIndex >= gm->goodsManager.listLength())
	{
		return;
	}
	item[index]->impImage = nullptr;
	if (gm->goodsManager.goodsList[listIndex].iniFile.empty() || gm->goodsManager.goodsList[listIndex].goods == nullptr || gm->goodsManager.goodsList[listIndex].number <= 0)
	{
		item[index]->setStr("");
		gm->goodsManager.goodsList[listIndex].clear();
	}
	else
	{
		item[index]->impImage = MenuResource::createGoodsMenuImage(gm->goodsManager.goodsList[listIndex].goods);
		item[index]->setStr(convert::formatString("%d", gm->goodsManager.goodsList[listIndex].number));
	}
	gm->player->calInfo();
	gm->menu->stateMenu->updateLabel();
	updateCharacterBindings();
}

void EquipMenu::updateMagicDisplay()
{
	updatePanelImage();

	for (int i = 0; i < static_cast<int>(magicDisplayItem.size()); i++)
	{
		updateMagicDisplay(i);
	}
	refreshControllerTransferHighlight();
}

void EquipMenu::updateMagicDisplay(int index)
{
	if (index < 0 || index >= static_cast<int>(magicDisplayItem.size()))
	{
		return;
	}
	if (magicDisplayItem[index] == nullptr)
	{
		magicDisplayIniFile[index].clear();
		return;
	}

	if (getSelectedPartner())
	{
		magicDisplayItem[index]->canDrag = magicDisplayItem[index]->canDrop = false;
		magicDisplayIniFile[index].clear();
		magicDisplayItem[index]->impImage = nullptr;
		return;
	}
	magicDisplayItem[index]->canDrop = true;
	std::string iniFile;
	int listIndex = getMagicDisplayStartIndex() + index;
	if (gm->magicManager.magicListExists(listIndex))
	{
		iniFile = gm->magicManager.magicList[listIndex].iniFile;
	}

	magicDisplayItem[index]->canDrag = false;
	magicDisplayItem[index]->dragIndex = listIndex;
	if (iniFile.empty())
	{
		if (!magicDisplayIniFile[index].empty())
		{
			magicDisplayIniFile[index].clear();
			magicDisplayItem[index]->impImage = nullptr;
		}
		return;
	}

	magicDisplayItem[index]->canDrag = true;
	if (magicDisplayIniFile[index] == iniFile)
	{
		return;
	}

	magicDisplayIniFile[index] = iniFile;
	magicDisplayItem[index]->impImage = MenuResource::createMagicMenuImage(gm->magicManager.magicList[listIndex].magic);
}

int EquipMenu::getMagicDisplayStartIndex() const
{
	if (magicScrollbar == nullptr)
	{
		return gm->magicManager.storeBegin();
	}
	return gm->magicManager.storeBegin() + magicScrollbar->position * magicScrollbar->lineSize;
}

void EquipMenu::quickEquipMagic(int sourceIndex)
{
	if (getSelectedPartner()) return;
	if (!gm->magicManager.magicListExists(sourceIndex))
	{
		return;
	}

	if (gm->menu->practiceMenu != nullptr && gm->menu->practiceMenu->visible)
	{
		gm->magicManager.exchange(sourceIndex, gm->magicManager.practiceIndex());
		updateMagicDisplay();
		gm->menu->practiceMenu->updateMagic();
		updateMagicRelatedMenus(sourceIndex);
		return;
	}

	if (gm->menu->bottomMenu == nullptr)
	{
		return;
	}

	for (int index = gm->magicManager.bottomBegin(); index <= gm->magicManager.bottomEnd(); index++)
	{
		if (!gm->magicManager.magicListExists(index))
		{
			gm->magicManager.exchange(sourceIndex, index);
			updateMagicDisplay();
			gm->menu->bottomMenu->updateMagicItem(gm->magicManager.bottomSlot(index));
			updateMagicRelatedMenus(sourceIndex);
			return;
		}
	}
	gm->showMessage("快捷武功栏已满");
}

void EquipMenu::updateMagicRelatedMenus(int sourceIndex)
{
	if (gm->menu->magicMenu != nullptr)
	{
		gm->menu->magicMenu->updateMagic();
	}
	if (gm->magicManager.isBottomIndex(sourceIndex) && gm->menu->bottomMenu != nullptr)
	{
		gm->menu->bottomMenu->updateMagicItem();
	}
	else if (gm->magicManager.isPracticeIndex(sourceIndex) && gm->menu->practiceMenu != nullptr)
	{
		gm->menu->practiceMenu->updateMagic();
	}
}

void EquipMenu::loadNewSwordPartnerNames()
{
	newSwordPartnerNames.clear();
	newSwordPartnerNames.resize(4);

	INIReader partnerIni("partneridx.ini");
	for (int index = 0; index <= 3; index++)
	{
		newSwordPartnerNames[index] = partnerIni.Get("Init", convert::formatString("%d", index), "");
	}

	INIReader playerNameIni("ini\\playername.ini");
	for (int index = 0; index <= 3; index++)
	{
		if (newSwordPartnerNames[index].empty())
		{
			newSwordPartnerNames[index] = playerNameIni.Get("Name", convert::formatString("Name%d", index + 1), "");
		}
	}
}

int EquipMenu::getNewSwordPartnerIndex(const std::string& partnerName) const
{
	if (partnerName.empty())
	{
		return -1;
	}

	for (int index = 0; index < static_cast<int>(newSwordPartnerNames.size()) && index < 4; index++)
	{
		if (newSwordPartnerNames[index] == partnerName)
		{
			return index;
		}
	}
	return -1;
}

void EquipMenu::updatePlayerNameDisplay()
{
	if (!gm || !gm->player) return;
	if (characterSelector)
	{
		updateCharacterSelector(*characterSelector);
	}
	if (auto playerName = getComponentByName<Label>("playerName"))
	{
		const auto partner = getSelectedPartner();
		playerName->setStr(partner ? partner->npcName : gm->player->npcName);
	}
	if (gm == nullptr || !gm->global.feature.equipPlayerNameImages || playerNameImages.empty())
	{
		return;
	}

	bool available[4] = { false, false, false, false };
	int currentIndex = gm->global.data.characterIndex;
	const int displayedIndex = getSelectedPartner() ? getNewSwordPartnerIndex(getSelectedPartner()->npcName) : currentIndex;
	if (currentIndex < 0)
	{
		currentIndex = 0;
	}
	if (currentIndex >= 0 && currentIndex < 4)
	{
		available[currentIndex] = true;
	}

	auto partners = gm->partnerManager.findPartnersFromNPCManager();
	for (auto& partner : partners)
	{
		if (partner == nullptr || !partner->canViewPartnerDetails())
		{
			continue;
		}
		int partnerIndex = getNewSwordPartnerIndex(partner->npcName);
		if (partnerIndex >= 0 && partnerIndex < 4)
		{
			available[partnerIndex] = true;
		}
	}

	for (int index = 0; index < static_cast<int>(playerNameImages.size()) && index < 4; index++)
	{
		if (playerNameImages[index] == nullptr)
		{
			continue;
		}
		if (index == displayedIndex)
		{
			playerNameImages[index]->frameIndex = 2;
		}
		else
		{
			playerNameImages[index]->frameIndex = available[index] ? 1 : 0;
		}
		if (index < static_cast<int>(playerNameButtons.size()) && playerNameButtons[index])
		{
			playerNameButtons[index]->activated = available[index];
		}
	}
	if (focusedPlayerName >= 0 && !playerNameButtons[focusedPlayerName]->activated)
	{
		focusControllerEquipment();
	}
}

std::shared_ptr<NPC> EquipMenu::getSelectedPartner() const
{
	const auto partner = selectedPartner.lock();
	return gm && partner && gm->partnerManager.isActivePartner(partner)
		&& partner->canViewPartnerDetails() ? partner : nullptr;
}

void EquipMenu::updateCharacterSelector(TextButton& button) const
{
	if (!gm || !gm->player) return;
	const auto partners = gm->partnerManager.findPartnersFromNPCManager();
	button.activated = std::any_of(partners.begin(), partners.end(),
		[](const auto& partner) { return partner && partner->canViewPartnerDetails(); });
	button.visible = button.activated || gm->global.feature.qingyuUi;
	const auto partner = getSelectedPartner();
	button.setStr((partner ? partner->npcName : gm->player->npcName) + (button.activated ? "  >" : ""));
}

bool EquipMenu::cycleCharacter()
{
	if (!gm) return false;
	const auto current = getSelectedPartner();
	bool found = !current;
	for (const auto& partner : gm->partnerManager.findPartnersFromNPCManager())
	{
		if (!partner || !partner->canViewPartnerDetails()) continue;
		if (found) return selectPartner(partner);
		if (partner == current) found = true;
	}
	return selectPartner(nullptr);
}

bool EquipMenu::openPartnerByCharacterIndex(int index)
{
	if (!gm || !gm->menu) return false;
	std::shared_ptr<NPC> partner;
	if (index != gm->global.data.characterIndex)
	{
		for (const auto& candidate : gm->partnerManager.findPartnersFromNPCManager())
		{
			if (candidate && candidate->canViewPartnerDetails() && getNewSwordPartnerIndex(candidate->npcName) == index)
			{
				partner = candidate;
				break;
			}
		}
		if (!partner) return false;
	}
	if (!selectPartner(partner)) return false;
	return focusControllerEquipment();
}

bool EquipMenu::selectPartner(const std::shared_ptr<NPC>& partner)
{
	if (!gm || !gm->menu || (partner && (!gm->partnerManager.isActivePartner(partner)
		|| !partner->canViewPartnerDetails()))) return false;
	gm->menu->cancelControllerInteraction();
	cancelPointerInteraction();
	if (gm->menu->goodsMenu) gm->menu->goodsMenu->cancelPointerInteraction();
	deactivateControllerFocus();
	selectedPartner = partner;
	showingPartner = partner != nullptr;
	if (magicScrollbar) magicScrollbar->position = 0;
	for (auto& file : magicDisplayIniFile) file.clear();
	for (auto& item : magicDisplayItem) if (item) item->impImage = nullptr;
	updateGoods();
	updateMagicDisplay();
	updateAttributes();
	if (gm->menu->stateMenu) gm->menu->stateMenu->updateLabel();
	return true;
}

bool EquipMenu::equipOnSelectedPartner(int goodsIndex, int partIndex)
{
	std::string message;
	const bool result = gm->partnerManager.equipOnePlayerGoodsOnPartner(getSelectedPartner(), goodsIndex, partIndex, &message);
	if (!result && !message.empty()) gm->showMessage(message);
	gm->goodsManager.updateMenu();
	return result;
}

bool EquipMenu::unequipSelectedPartner(int partIndex)
{
	std::string message;
	const bool result = gm->partnerManager.unequipPartnerGoodsToPlayerBag(getSelectedPartner(), partIndex, &message);
	if (!result && !message.empty()) gm->showMessage(message);
	gm->goodsManager.updateMenu();
	return result;
}

int EquipMenu::getPartIndex(const std::string & part)
{
	std::string normalizedPart = normalizeEquipmentPart(part);
	if (normalizedPart == "head")
	{
		return 0;
	}
	else if (normalizedPart == "neck")
	{
		return 1;
	}
	else if (normalizedPart == "body")
	{
		return 2;
	}
	else if (normalizedPart == "back")
	{
		return 3;
	}
	else if (normalizedPart == "hand")
	{
		return 4;
	}
	else if (normalizedPart == "wrist")
	{
		return 5;
	}
	else if (normalizedPart == "foot")
	{
		return 6;
	}
	return -1;
}

void EquipMenu::updatePanelImage()
{
	if (gm == nullptr || !gm->global.feature.characterPanelImages)
	{
		return;
	}

	int panelIndex = getSelectedPartner() ? getNewSwordPartnerIndex(getSelectedPartner()->npcName) : gm->global.data.characterIndex;
	if (panelIndex < 0)
	{
		panelIndex = 0;
	}
	if (loadedPanelIndex == panelIndex)
	{
		return;
	}

	std::string fileName = "panel7.asf";
	if (panelIndex > 0)
	{
		fileName = convert::formatString("panel7%c.asf", (char)('a' + panelIndex));
	}
	impImage = IMP::createIMPImage(std::string("asf\\ui\\common\\") + fileName);
	loadedPanelIndex = panelIndex;
}

void EquipMenu::onUpdate()
{
	if (!visible) return;
	updateAttributePointEntry();
	if (showingPartner && !getSelectedPartner()) selectPartner(nullptr);
	updateCharacterBindings();
	updatePlayerNameDisplay();
	updateAttributes();
}

void EquipMenu::onEvent()
{
	if (!visible)
	{
		return;
	}

	if (showingPartner && !getSelectedPartner()) selectPartner(nullptr);
	updateCharacterBindings();
	updatePlayerNameDisplay();
	if (characterSelector && characterSelector->getResult(erClick))
	{
		cycleCharacter();
		return;
	}
	if (attributePointEntry && attributePointEntry->visible && attributePointEntry->getResult(erClick))
	{
		gm->menu->openAttributePoints(shared_from_this(), attributePointEntry);
		return;
	}
	for (int index = 0; index < static_cast<int>(playerNameButtons.size()); ++index)
	{
		if (playerNameButtons[index] && playerNameButtons[index]->getResult(erClick))
		{
			openPartnerByCharacterIndex(index);
			return;
		}
	}
	if (equipmentTab && equipmentTab->getResult(erClick)) showAttributes(false);
	if (attributesTab && attributesTab->getResult(erClick)) showAttributes(true);
	updateAttributes();
	if (showingAttributes) return;
	updateMagicDisplay();
	if (gm != nullptr && gm->menu != nullptr
		&& gm->menu->controllerTransfers().active()
		&& currentDragItem != nullptr)
	{
		gm->menu->cancelControllerInteraction();
	}

	for (size_t i = 0; i < GOODS_BODY_COUNT; i++)
	{
		if (item[i] == nullptr) continue;

		unsigned int ret = item[i]->getResult();
		int listIndex = gm->goodsManager.equipIndex(static_cast<int>(i));
		if (const auto partner = getSelectedPartner())
		{
			if ((ret & erShowHint) && partnerGoods[i]) gm->menu->showGoodsToolTip(getMySharedPtr(), partnerGoods[i], item[i]);
			if (ret & erHideHint) gm->menu->toolTip->visible = false;
#ifdef __MOBILE__
			if (ret & (erClick | erMouseRDown)) unequipSelectedPartner(static_cast<int>(i));
#else
			if (ret & erMouseRDown) unequipSelectedPartner(static_cast<int>(i));
#endif
			if (ret & erDropped)
			{
				gm->menu->cancelControllerInteraction();
				if (item[i]->dropType == dtGoods) equipOnSelectedPartner(item[i]->dropIndex, static_cast<int>(i));
				else if (item[i]->dropType == dtPartnerEquipment)
				{
					std::string message;
					gm->partnerManager.exchangePartnerEquipmentSlots(partner, item[i]->dropIndex, static_cast<int>(i), &message);
					if (!message.empty()) gm->showMessage(message);
					updateGoods();
				}
			}
			continue;
		}
#ifndef __MOBILE__
		if (gm->global.feature.qingyuUi && (ret & erClick)
			&& gm->goodsManager.goodsListExists(listIndex))
		{
			gm->menu->showGoodsToolTip(getMySharedPtr(),
				gm->goodsManager.goodsList[listIndex].goods, item[i], true);
			item[i]->resetHint();
			continue;
		}
#endif
		if (ret & erShowHint)
		{
			if (gm->goodsManager.goodsListExists(listIndex))
			{
				gm->menu->showGoodsToolTip(
					getMySharedPtr(),
					gm->goodsManager.goodsList[listIndex].goods,
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
#ifdef __MOBILE__
		if (gm->global.feature.qingyuUi && (ret & (erClick | erMouseRDown)))
#else
		if (ret & erMouseRDown)
#endif
		{
			gm->menu->cancelControllerInteraction();
			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
			if (gm->global.feature.qingyuUi)
			{
				activateControllerEquipment(listIndex, static_cast<int>(i));
			}
		}

		if (ret & erDropped)
		{
			gm->menu->cancelControllerInteraction();
			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
			if (item[i]->dropType == dtGoods)
			{
				if (gm->goodsManager.goodsListExists(item[i]->dropIndex))
				{
					std::string message;
					if (gm->goodsManager.canEquipGoodsAt(item[i]->dropIndex, i, gm->player, &message))
					{
						gm->goodsManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateGoods(i);
					}
					else if (!message.empty())
					{
						gm->showMessage(message);
					}
					if (gm->goodsManager.isStoreIndex(item[i]->dropIndex))
					{
						updateGoods(i);
						gm->menu->goodsMenu->updateGoods();
					}
					else if (gm->goodsManager.isBottomIndex(item[i]->dropIndex))
					{
						updateGoods(i);
						gm->menu->bottomMenu->updateGoodsItem();
					}
					else
					{
						updateGoods();
					}
				}
			}
		}
	}

	for (int i = 0; i < static_cast<int>(magicDisplayItem.size()); i++)
	{
		if (magicDisplayItem[i] == nullptr)
		{
			continue;
		}

		unsigned int ret = magicDisplayItem[i]->getResult();
		if (getSelectedPartner()) continue;
		if (ret & erShowHint)
		{
			int listIndex = magicDisplayItem[i]->dragIndex;
			if (gm->magicManager.magicListExists(listIndex))
			{
				gm->menu->showMagicToolTip(
					getMySharedPtr(),
					gm->magicManager.magicList[listIndex].magic,
					gm->magicManager.magicList[listIndex].level,
					magicDisplayItem[i]);
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
#ifdef __MOBILE__
		if ((ret & erClick) || (ret & erMouseRDown))
#else
		if (ret & erMouseRDown)
#endif
		{
			gm->menu->cancelControllerInteraction();
			quickEquipMagic(magicDisplayItem[i]->dragIndex);
			gm->menu->toolTip->visible = false;
			magicDisplayItem[i]->resetHint();
		}
		if (ret & erDropped)
		{
			gm->menu->cancelControllerInteraction();
			gm->menu->toolTip->visible = false;
			magicDisplayItem[i]->resetHint();
			if (magicDisplayItem[i]->dropType == dtMagic)
			{
				int targetIndex = magicDisplayItem[i]->dragIndex;
				int sourceIndex = magicDisplayItem[i]->dropIndex;
				if (gm->magicManager.isStoreIndex(targetIndex) && gm->magicManager.magicListExists(sourceIndex))
				{
					gm->magicManager.exchange(sourceIndex, targetIndex);
					updateMagicDisplay();
					updateMagicRelatedMenus(sourceIndex);
				}
			}
		}
	}
}

void EquipMenu::init()
{
	freeResource();
	loadMenuDefinition("ini\\ui\\equip\\equip.menu.ini");
	updatePanelImage();

	image = getComponentByName<ImageContainer>("image");
	title = getComponentByName<ImageContainer>("title");
	equipmentTab = getComponentByName<CheckBox>("equipmentTab");
	attributesTab = getComponentByName<CheckBox>("attributesTab");
	if (!attributesTab) showingAttributes = false;
	magicScrollbar = getComponentByName<Scrollbar>("magicScrollbar");
	playerNameImages.clear();
	for (int i = 0; i < 4; i++)
	{
		playerNameImages.push_back(getComponentByName<ImageContainer>(convert::formatString("showname%02d", i)));
	}
	loadNewSwordPartnerNames();
	characterSelector = getComponentByName<TextButton>("characterSelector");
	playerNameButtons.resize(playerNameImages.size());
	if (gm->global.feature.equipPlayerNameImages)
	{
		for (size_t index = 0; index < playerNameImages.size(); ++index)
		{
			if (!playerNameImages[index]) continue;
			auto button = std::make_shared<Button>();
			button->name = "partnerName" + std::to_string(index);
			button->rect = playerNameImages[index]->rect;
			addChild(button);
			playerNameButtons[index] = button;
		}
	}

	for (size_t i = 0; i < GOODS_BODY_COUNT; i++)
	{
		std::string itemName = convert::formatString("item%d", i + 1);
		item[i] = getComponentByName<Item>(itemName);
		if (item[i])
		{
			item[i]->dragType = dtGoods;
			item[i]->dragIndex = gm->goodsManager.equipIndex(static_cast<int>(i));
			item[i]->canShowHint = true;
		}
	}

	magicDisplayItem.clear();
	magicDisplayIniFile.clear();
	for (int i = 1;; i++)
	{
		std::string itemName = convert::formatString("magic%02d", i);
		auto magicItem = getComponentByName<Item>(itemName);
		if (!magicItem)
		{
			break;
		}
		magicItem->dragType = dtMagic;
		magicItem->dragIndex = gm->magicManager.storeBegin() + i - 1;
		magicItem->canDrop = true;
		magicItem->canShowHint = true;
		magicDisplayItem.push_back(magicItem);
		magicDisplayIniFile.emplace_back();
	}

	if (magicScrollbar != nullptr)
	{
		magicScrollbar->pageSize = static_cast<int>(magicDisplayItem.size());
		int lineSize = std::max(1, magicScrollbar->lineSize);
		int visibleCount = std::max(1, magicScrollbar->pageSize);
		int scrollableCount = std::max(0, gm->global.magicLayout.storeCount() - visibleCount);
		magicScrollbar->min = 0;
		magicScrollbar->max = (scrollableCount + lineSize - 1) / lineSize;
		magicScrollbar->position = magicScrollbar->min;
	}

	setChildRectReferToParent();
	configureControllerFocus();
	updatePlayerNameDisplay();
	updateMagicDisplay();
	showAttributes(showingAttributes);
}

void EquipMenu::onWindowResize(int width, int height)
{
	const auto partner = getSelectedPartner();
	ConfigDrivenPanel::onWindowResize(width, height);
	selectedPartner = partner;
	showingPartner = partner != nullptr;
	updateGoods();
	updateMagicDisplay();
	updateAttributes();
	if (gm && gm->menu && gm->menu->stateMenu) gm->menu->stateMenu->updateLabel();
}

void EquipMenu::freeResource()
{
	attributePointEntryFocused = false;
	attributePointEntry.reset();
	selectedPartner.reset();
	showingPartner = false;
	characterSelectorFocused = false;
	characterSelector.reset();
	focusedPlayerName = -1;
	playerNameButtons.clear();
	tabFocused = false;
	equipmentTab = nullptr;
	attributesTab = nullptr;
	// The router borrows both slot controllers. Detach it before clearing the
	// borrowed targets so an active target can still be deactivated safely.
	controllerPaneRouter.clear();
	equipmentSlotController.clear();
	magicSlotController.clear();
	image = nullptr;
	title = nullptr;
	magicScrollbar = nullptr;
	playerNameImages.clear();
	for (size_t i = 0; i < GOODS_BODY_COUNT; i++)
	{
		item[i] = nullptr;
	}
	magicDisplayItem.clear();
	magicDisplayIniFile.clear();
	newSwordPartnerNames.clear();
	loadedPanelIndex = -1;
	ConfigDrivenPanel::freeResource();
}

void EquipMenu::updateAttributePointEntry()
{
	const bool available = gm && gm->menu && gm->global.attributePointRules.enabled
		&& gm->global.feature.stateEquipIntegratedLayout && showingAttributes && !getSelectedPartner();
	if (!attributePointEntry && available)
	{
		attributePointEntry = std::make_shared<FlatTextButton>();
		attributePointEntry->setUTF8Str(u8"属性分配");
		attributePointEntry->setFontSize(16);
		addChild(attributePointEntry);
	}
	if (attributePointEntry)
	{
		attributePointEntry->visible = attributePointEntry->activated = available;
		attributePointEntry->rect = { rect.x + std::max(0, rect.w - 100), rect.y + 8, 88, 44 };
		if (!available) { attributePointEntryFocused = false; attributePointEntry->setFocused(false); }
	}
}

void EquipMenu::onSetChildRect()
{
	updateAttributePointEntry();
}

void EquipMenu::configureControllerFocus()
{
	// Rebinding a config-driven panel replaces the borrowed target contents.
	// Clear the router first so it cannot retain an active old binding.
	controllerPaneRouter.clear();

	std::vector<std::shared_ptr<Item>> equipmentItems;
	equipmentItems.reserve(GOODS_BODY_COUNT);
	for (const auto& equipmentItem : item)
	{
		equipmentItems.push_back(equipmentItem);
	}

	SlotInteractionBinding equipmentBinding =
		MenuController::makeControllerSlotInteractionBinding(
			gm,
			ControllerSlotKind::Goods,
			ControllerSlotDomain::PlayerEquipment);
	equipmentBinding.grid.focusIdPrefix = "equipment-item-";
	equipmentBinding.grid.items = std::move(equipmentItems);
	equipmentBinding.grid.fixedColumnCount = equipmentTab ? 4 : 3;
	equipmentBinding.grid.resolveLogicalIndex = [this](int visibleIndex)
	{
		if (gm == nullptr || visibleIndex < 0 || visibleIndex >= GOODS_BODY_COUNT)
		{
			return -1;
		}
		const int logicalIndex = gm->goodsManager.equipIndex(visibleIndex);
		return gm->goodsManager.isEquipIndex(logicalIndex) ? logicalIndex : -1;
	};
	equipmentBinding.grid.primary = [this](int logicalIndex, int visibleIndex)
	{
		activateControllerEquipment(logicalIndex, visibleIndex);
	};
	equipmentBinding.grid.details = [this](int logicalIndex, int visibleIndex)
	{
		showControllerEquipmentDetails(logicalIndex, visibleIndex);
	};
	equipmentBinding.grid.hideDetails = [this]() { hideControllerDetails(); };
	equipmentBinding.resolveContext = [this]() -> std::shared_ptr<const void> { return getSelectedPartner(); };
	equipmentSlotController.bind(std::move(equipmentBinding));
	configureEquipmentControllerNeighbours();

	SlotInteractionBinding magicBinding =
		MenuController::makeControllerSlotInteractionBinding(
			gm,
			ControllerSlotKind::Magic,
			ControllerSlotDomain::MagicList);
	magicBinding.grid.focusIdPrefix = "integrated-magic-item-";
	magicBinding.grid.items = magicDisplayItem;
	magicBinding.grid.scrollbar = magicScrollbar;
	magicBinding.grid.resolveLogicalIndex = [this](int visibleIndex)
	{
		return getControllerMagicIndex(visibleIndex);
	};
	magicBinding.grid.primary = [this](int logicalIndex, int visibleIndex)
	{
		activateControllerMagic(logicalIndex, visibleIndex);
	};
	magicBinding.grid.details = [this](int logicalIndex, int visibleIndex)
	{
		showControllerMagicDetails(logicalIndex, visibleIndex);
	};
	magicBinding.grid.hideDetails = [this]() { hideControllerDetails(); };
	magicBinding.grid.refreshAfterScroll = [this]() { updateMagicDisplay(); };
	magicSlotController.bind(std::move(magicBinding));

	controllerPaneRouter.registerTargetPane(
		EquipmentControllerPaneId,
		equipmentSlotController);
	controllerPaneRouter.registerTargetPane(
		MagicControllerPaneId,
		magicSlotController);
	controllerPaneRouter.setDefaultPane(EquipmentControllerPaneId);
}

void EquipMenu::configureEquipmentControllerNeighbours()
{
	if (equipmentTab) return;
	auto connect = [this](int from, UIFocusDirection direction, int to)
	{
		equipmentSlotController.setNeighbour(from, direction, to);
	};
	const bool threeColumnLegacyLayout = item[0] != nullptr && item[2] != nullptr
		&& item[2]->rect.x < item[0]->rect.x;
	if (threeColumnLegacyLayout)
	{
		connect(0, UIFocusDirection::Right, 1);
		connect(0, UIFocusDirection::Down, 2);
		connect(1, UIFocusDirection::Left, 0);
		connect(1, UIFocusDirection::Down, 4);
		connect(2, UIFocusDirection::Up, 0);
		connect(2, UIFocusDirection::Right, 3);
		connect(2, UIFocusDirection::Down, 5);
		connect(3, UIFocusDirection::Up, 0);
		connect(3, UIFocusDirection::Left, 2);
		connect(3, UIFocusDirection::Right, 4);
		connect(3, UIFocusDirection::Down, 6);
		connect(4, UIFocusDirection::Up, 1);
		connect(4, UIFocusDirection::Left, 3);
		connect(4, UIFocusDirection::Down, 6);
		connect(5, UIFocusDirection::Up, 2);
		connect(5, UIFocusDirection::Right, 6);
		connect(6, UIFocusDirection::Up, 4);
		connect(6, UIFocusDirection::Left, 5);
		return;
	}

	connect(0, UIFocusDirection::Right, 1);
	connect(0, UIFocusDirection::Down, 5);
	connect(1, UIFocusDirection::Left, 0);
	connect(1, UIFocusDirection::Down, 4);
	connect(2, UIFocusDirection::Up, 0);
	connect(2, UIFocusDirection::Left, 5);
	connect(2, UIFocusDirection::Right, 4);
	connect(2, UIFocusDirection::Down, 6);
	connect(3, UIFocusDirection::Up, 4);
	connect(3, UIFocusDirection::Left, 6);
	connect(4, UIFocusDirection::Up, 1);
	connect(4, UIFocusDirection::Left, 2);
	connect(4, UIFocusDirection::Down, 3);
	connect(5, UIFocusDirection::Up, 0);
	connect(5, UIFocusDirection::Right, 2);
	connect(5, UIFocusDirection::Down, 6);
	connect(6, UIFocusDirection::Up, 5);
	connect(6, UIFocusDirection::Right, 3);
}

bool EquipMenu::activateControllerFocus(ControllerFocusTarget target)
{
	if (target == ControllerFocusTarget::MagicList)
	{
		return focusControllerMagicList();
	}
	if (target == ControllerFocusTarget::Default
		|| target == ControllerFocusTarget::PlayerEquipment)
	{
		return focusControllerEquipment();
	}
	return false;
}

bool EquipMenu::focusControllerEquipment()
{
	updateAttributePointEntry();
	if (attributePointEntryFocused || characterSelectorFocused || focusedPlayerName >= 0) deactivateControllerFocus();
	if (showingAttributes) return focusControllerElement(attributePointEntry && attributePointEntry->visible
		? PElement(attributePointEntry) : PElement(attributesTab));
	tabFocused = false;
	return controllerPaneRouter.activatePane(EquipmentControllerPaneId);
}

bool EquipMenu::focusControllerMagicList()
{
	return controllerPaneRouter.activatePane(MagicControllerPaneId);
}

bool EquipMenu::isControllerFocusActive() const
{
	return attributePointEntryFocused || characterSelectorFocused || focusedPlayerName >= 0 || tabFocused || controllerPaneRouter.isActive();
}

void EquipMenu::deactivateControllerFocus()
{
	attributePointEntryFocused = false;
	if (attributePointEntry) attributePointEntry->setFocused(false);
	characterSelectorFocused = false;
	if (characterSelector) characterSelector->setFocused(false);
	focusedPlayerName = -1;
	for (const auto& button : playerNameButtons) if (button) button->setFocused(false);
	tabFocused = false;
	if (equipmentTab) equipmentTab->setFocused(false);
	if (attributesTab) attributesTab->setFocused(false);
	controllerPaneRouter.deactivate();
	hideControllerDetails();
}

PElement EquipMenu::controllerFocusedElement() const
{
	if (attributePointEntryFocused) return attributePointEntry;
	if (characterSelectorFocused) return characterSelector;
	if (focusedPlayerName >= 0) return playerNameButtons[focusedPlayerName];
	if (tabFocused) return showingAttributes ? attributesTab : equipmentTab;
	return controllerPaneRouter.controllerFocusedElement();
}

std::vector<PElement> EquipMenu::controllerFocusCandidates() const
{
	auto candidates = showingAttributes ? std::vector<PElement>{} : controllerPaneRouter.controllerFocusCandidates();
	if (attributePointEntry && attributePointEntry->visible) candidates.push_back(attributePointEntry);
	if (characterSelector && characterSelector->visible && characterSelector->activated) candidates.push_back(characterSelector);
	for (const auto& button : playerNameButtons)
	{
		if (button && button->visible && button->activated) candidates.push_back(button);
	}
	if (equipmentTab && attributesTab)
	{
		candidates.push_back(equipmentTab);
		candidates.push_back(attributesTab);
	}
	return candidates;
}

bool EquipMenu::focusControllerElement(const PElement& element)
{
	if (element && element == attributePointEntry && element->visible && element->activated)
	{
		deactivateControllerFocus();
		attributePointEntryFocused = true;
		element->setFocused(true);
		return true;
	}
	if (element && element == characterSelector && element->visible && element->activated)
	{
		deactivateControllerFocus();
		characterSelectorFocused = true;
		element->setFocused(true);
		return true;
	}
	for (int index = 0; index < static_cast<int>(playerNameButtons.size()); ++index)
	{
		if (element && element == playerNameButtons[index] && element->visible && element->activated)
		{
			deactivateControllerFocus();
			focusedPlayerName = index;
			element->setFocused(true);
			return true;
		}
	}
	if (element && element->visible && element->activated && (element == equipmentTab || element == attributesTab))
	{
		showAttributes(element == attributesTab);
		tabFocused = true;
		element->setFocused(true);
		return true;
	}
	if (showingAttributes) return false;
	deactivateControllerFocus();
	hideControllerDetails();
	return controllerPaneRouter.focusControllerElement(element);
}

bool EquipMenu::controllerFocusElementMatchesTarget(
	const PElement& element,
	ControllerFocusTarget target) const
{
	auto contains = [&element](const std::vector<PElement>& candidates)
	{
		return std::find(candidates.begin(), candidates.end(), element)
			!= candidates.end();
	};
	if (target == ControllerFocusTarget::Default
		|| target == ControllerFocusTarget::PlayerEquipment)
	{
		if (element && element == characterSelector) return true;
		if (element && element == attributePointEntry) return true;
		for (const auto& button : playerNameButtons) if (element && element == button) return true;
		if (element && (element == equipmentTab || element == attributesTab)) return true;
		return contains(equipmentSlotController.controllerFocusCandidates());
	}
	if (target == ControllerFocusTarget::MagicList)
	{
		return contains(magicSlotController.controllerFocusCandidates());
	}
	return false;
}

void EquipMenu::activateControllerEquipment(int logicalIndex, int visibleIndex)
{
	if (getSelectedPartner())
	{
		unequipSelectedPartner(visibleIndex);
		return;
	}
	if (gm == nullptr || gm->menu == nullptr
		|| !gm->goodsManager.isEquipIndex(logicalIndex))
	{
		return;
	}
	if (!gm->goodsManager.goodsListExists(logicalIndex))
	{
		return;
	}
	std::string message;
	if (!gm->goodsManager.unequipToFirstStoreSlot(logicalIndex, &message)
		&& !message.empty())
	{
		gm->showMessage(message);
	}
}
void EquipMenu::showControllerEquipmentDetails(
	int logicalIndex, int visibleIndex)
{
	if (getSelectedPartner())
	{
		if (visibleIndex >= 0 && visibleIndex < GOODS_BODY_COUNT && partnerGoods[visibleIndex])
			gm->menu->showGoodsToolTip(getMySharedPtr(), partnerGoods[visibleIndex], item[visibleIndex]);
		else hideControllerDetails();
		return;
	}
	if (gm == nullptr || gm->menu == nullptr || gm->menu->toolTip == nullptr
		|| gm->menu->upMenu == nullptr || visibleIndex < 0
		|| visibleIndex >= GOODS_BODY_COUNT || item[visibleIndex] == nullptr
		|| !gm->goodsManager.goodsListExists(logicalIndex))
	{
		hideControllerDetails();
		return;
	}
	gm->menu->showGoodsToolTip(
		getMySharedPtr(),
		gm->goodsManager.goodsList[logicalIndex].goods,
		item[visibleIndex]);
}

int EquipMenu::getControllerMagicIndex(int visibleIndex) const
{
	if (gm == nullptr || visibleIndex < 0
		|| visibleIndex >= static_cast<int>(magicDisplayItem.size())
		|| magicDisplayItem[visibleIndex] == nullptr)
	{
		return -1;
	}
	const int logicalIndex = getMagicDisplayStartIndex() + visibleIndex;
	return !getSelectedPartner() && gm->magicManager.isStoreIndex(logicalIndex)
		&& logicalIndex < gm->magicManager.listLength()
		? logicalIndex : -1;
}

void EquipMenu::activateControllerMagic(int logicalIndex, int)
{
	if (gm == nullptr || gm->menu == nullptr
		|| !gm->magicManager.isStoreIndex(logicalIndex))
	{
		return;
	}
	hideControllerDetails();
	quickEquipMagic(logicalIndex);
}
void EquipMenu::showControllerMagicDetails(int logicalIndex, int visibleIndex)
{
	if (getSelectedPartner() || gm == nullptr || gm->menu == nullptr || gm->menu->toolTip == nullptr
		|| gm->menu->upMenu == nullptr || visibleIndex < 0
		|| visibleIndex >= static_cast<int>(magicDisplayItem.size())
		|| magicDisplayItem[visibleIndex] == nullptr
		|| !gm->magicManager.magicListExists(logicalIndex))
	{
		hideControllerDetails();
		return;
	}
	gm->menu->showMagicToolTip(
		getMySharedPtr(),
		gm->magicManager.magicList[logicalIndex].magic,
		gm->magicManager.magicList[logicalIndex].level,
		magicDisplayItem[visibleIndex]);
}

void EquipMenu::hideControllerDetails()
{
	if (gm != nullptr && gm->menu != nullptr)
	{
		gm->menu->hideToolTip();
	}
}

void EquipMenu::refreshControllerTransferHighlight()
{
	equipmentSlotController.refreshTransferSelection();
	magicSlotController.refreshTransferSelection();
}

bool EquipMenu::onHandleUIAction(UIAction action)
{
	if (attributePointEntryFocused && action == UIAction::Confirm && gm && gm->menu)
		return gm->menu->openAttributePoints(shared_from_this(), attributePointEntry);
	if (characterSelectorFocused)
	{
		if (action == UIAction::Confirm)
		{
			cycleCharacter();
			return focusControllerElement(characterSelector);
		}
		const bool selectorAboveEquipment = characterSelector && item[0]
			&& characterSelector->rect.y < item[0]->rect.y;
		if (selectorAboveEquipment && action == UIAction::NavigateRight && equipmentTab)
		{
			return focusControllerElement(showingAttributes ? attributesTab : equipmentTab);
		}
		if (action == UIAction::Cancel
			|| action == (selectorAboveEquipment ? UIAction::NavigateDown : UIAction::NavigateUp))
		{
			deactivateControllerFocus();
			return selectorAboveEquipment ? focusControllerEquipment()
				: focusControllerElement(item[GOODS_BODY_COUNT - 1]);
		}
		return false;
	}
	if (focusedPlayerName >= 0)
	{
		if (action == UIAction::Confirm) return openPartnerByCharacterIndex(focusedPlayerName);
		if (action == UIAction::NavigateDown || action == UIAction::Cancel) return focusControllerEquipment();
		if (action == UIAction::NavigateLeft || action == UIAction::NavigateRight)
		{
			const int count = static_cast<int>(playerNameButtons.size());
			const int delta = action == UIAction::NavigateLeft ? -1 : 1;
			for (int offset = 1; offset <= count; ++offset)
			{
				const int index = (focusedPlayerName + delta * offset + count) % count;
				if (playerNameButtons[index] && playerNameButtons[index]->activated)
					return focusControllerElement(playerNameButtons[index]);
			}
		}
		return true;
	}
	if ((action == UIAction::NavigateDown || action == UIAction::NavigateUp) && equipmentSlotController.isActive()
		&& characterSelector && characterSelector->visible && characterSelector->activated)
	{
		const auto focused = equipmentSlotController.controllerFocusedElement();
		const bool down = action == UIAction::NavigateDown;
		const bool selectorInDirection = focused && (down
			? characterSelector->rect.y > focused->rect.y : characterSelector->rect.y < focused->rect.y);
		const bool hasNextSlot = focused && std::any_of(std::begin(item), std::end(item), [&](const auto& slot)
		{
			return slot && (down ? slot->rect.y > focused->rect.y : slot->rect.y < focused->rect.y);
		});
		if (selectorInDirection && !hasNextSlot) return focusControllerElement(characterSelector);
	}
	if (action == UIAction::NavigateUp && equipmentSlotController.isActive()
		&& equipmentSlotController.focusedVisibleIndex() < 4)
	{
		for (const auto& button : playerNameButtons)
			if (button && button->activated) return focusControllerElement(button);
	}
	if (equipmentTab && attributesTab)
	{
		if (action == UIAction::Cancel && showingAttributes)
		{
			return focusControllerElement(equipmentTab);
		}
		if (tabFocused)
		{
			if (action == UIAction::NavigateLeft || action == UIAction::NavigateRight)
			{
				const auto source = showingAttributes ? attributesTab : equipmentTab;
				const int sourceX = source->rect.x + source->rect.w / 2;
				const bool right = action == UIAction::NavigateRight;
				PElement target;
				int nearestDistance = 0;
				for (const PElement& candidate : { PElement(characterSelector), PElement(equipmentTab), PElement(attributesTab) })
				{
					if (!candidate || candidate == source || !candidate->visible || !candidate->activated) continue;
					const int distance = (candidate->rect.x + candidate->rect.w / 2 - sourceX) * (right ? 1 : -1);
					if (distance > 0 && (!target || distance < nearestDistance))
					{
						target = candidate;
						nearestDistance = distance;
					}
				}
				return target && focusControllerElement(target);
			}
			if (action == UIAction::Confirm || action == UIAction::NavigateDown)
			{
					if (showingAttributes) return attributePointEntry && attributePointEntry->visible
						? focusControllerElement(attributePointEntry) : true;
				deactivateControllerFocus();
				return focusControllerEquipment();
			}
			return false;
		}
		if (action == UIAction::NavigateUp && equipmentSlotController.isActive()
			&& equipmentSlotController.focusedVisibleIndex() < 4)
			return focusControllerElement(equipmentTab);
	}
	return controllerPaneRouter.handleAction(action);
}
