#include "MagicManager.h"
#include "../../File/INIReader.h"
#include "DefeatedNpcExperience.h"
#include "../../libconvert/libconvert.h"
#include "../../File/log.h"
#include "../GameManager/GameManager.h"
#include "../GameManager/SaveFileManager.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdlib>

namespace
{
std::string toLowerAscii(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char ch)
		{
			return static_cast<char>(std::tolower(ch));
		});
	return value;
}

bool equalsMagicFileName(const std::string& left, const std::string& right)
{
	return toLowerAscii(left) == toLowerAscii(right);
}

bool parsePositiveSectionIndex(const std::string& section, int& value)
{
	if (section.empty())
	{
		return false;
	}
	char* end = nullptr;
	long parsed = std::strtol(section.c_str(), &end, 10);
	if (end == section.c_str() || *end != '\0' || parsed <= 0 || parsed > INT_MAX)
	{
		return false;
	}
	value = static_cast<int>(parsed);
	return true;
}

void clearMagicInfo(MagicInfo& info)
{
	info.magic = nullptr;
	info.iniFile = "";
	info.level = 0;
	info.exp = 0;
	info.hideCount = 0;
	info.lastIndexWhenHide = 0;
	info.remainColdMilliseconds = 0;
}

int addExperienceSaturated(int currentExperience, int addedExperience)
{
	const int64_t total = static_cast<int64_t>(currentExperience)
		+ addedExperience;
	return static_cast<int>(std::clamp<int64_t>(
		total,
		INT_MIN,
		INT_MAX));
}

bool magicInfoIsUsable(const MagicInfo& info)
{
	return !info.iniFile.empty()
		&& info.magic != nullptr
		&& info.level >= 1
		&& info.level <= MAGIC_MAX_LEVEL;
}

std::shared_ptr<Magic> loadMagicResource(
	const std::string& magicName,
	const char* context)
{
	auto magic = std::make_shared<Magic>();
	magic->initFromIni(magicName);
	if (magic->loadSucceeded)
	{
		return magic;
	}
	GameLog::write(
		"MagicManager: skip unavailable Magic %s from %s\n",
		magicName.c_str(),
		context);
	return nullptr;
}

bool loadMagicInfoFromIni(INIReader& ini, const std::string& section, MagicInfo& info, int defaultHideCount)
{
	clearMagicInfo(info);
	info.iniFile = ini.Get(section, "IniFile", "");
	if (info.iniFile.empty())
	{
		return false;
	}
	info.level = static_cast<int>(std::clamp<long>(
		ini.GetInteger(section, "Level", 1),
		1,
		MAGIC_MAX_LEVEL));
	info.exp = static_cast<int>(std::clamp<long>(
		ini.GetInteger(section, "Exp", 0),
		0,
		INT_MAX));
	info.hideCount = defaultHideCount > 0
		? static_cast<int>(std::clamp<long>(
			ini.GetInteger(
				section, "HideCount", defaultHideCount),
			1,
			INT_MAX))
		: 0;
	info.lastIndexWhenHide = static_cast<int>(std::clamp<long>(
		ini.GetInteger(section, "LastIndexWhenHide", 0),
		0,
		INT_MAX));
	info.remainColdMilliseconds = 0;
	info.magic = loadMagicResource(info.iniFile, "save data");
	if (info.magic == nullptr)
	{
		clearMagicInfo(info);
		return false;
	}
	return true;
}

int findMagicIndexInList(const std::vector<MagicInfo>& list, const std::string& magicName)
{
	for (int i = 0; i < static_cast<int>(list.size()); i++)
	{
		if (magicInfoIsUsable(list[i])
			&& equalsMagicFileName(list[i].iniFile, magicName))
		{
			return i;
		}
	}
	return -1;
}

std::string trimString(const std::string& value)
{
	size_t first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
	{
		return "";
	}
	size_t last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

std::vector<std::string> parseReplaceMagicNames(const std::string& listString)
{
	std::vector<std::string> result;
	if (listString.empty() || listString == "无")
	{
		return result;
	}
	std::string normalized = listString;
	convert::replaceAllString(normalized, "\xEF\xBC\x9A", ":");
	convert::replaceAllString(normalized, "\xEF\xBC\x9B", ";");
	auto items = convert::splitString(normalized, ";");
	for (const auto& rawItem : items)
	{
		std::string item = trimString(rawItem);
		if (item.empty())
		{
			continue;
		}
		size_t colonPos = item.find(':');
		if (colonPos != std::string::npos)
		{
			item = trimString(item.substr(0, colonPos));
		}
		if (!item.empty())
		{
			result.push_back(item);
		}
	}
	return result;
}

MagicInfo makeReplacementMagicInfo(const std::string& magicName)
{
	MagicInfo info;
	info.magic = loadMagicResource(magicName, "replacement list");
	if (info.magic == nullptr)
	{
		return info;
	}
	info.iniFile = magicName;
	info.level = 1;
	info.exp = 0;
	info.hideCount = 1;
	info.lastIndexWhenHide = 0;
	info.remainColdMilliseconds = 0;
	return info;
}
}

MagicManager::MagicManager()
{
	configureLayout();
}

MagicManager::~MagicManager()
{
	freeResource();
}

MagicInfo * MagicManager::findMagic(const std::string & iniName)
{
	for (size_t i = 0; i < magicList.size(); i++)
	{
		if (magicInfoIsUsable(magicList[i])
			&& equalsMagicFileName(magicList[i].iniFile, iniName))
		{
			return &magicList[i];
		}
	}
	return nullptr;
}

MagicInfo* MagicManager::findPrimaryMagic(const std::string& iniName)
{
	auto& primaryList = primaryMagicList();
	for (size_t i = 0; i < primaryList.size(); i++)
	{
		if (magicInfoIsUsable(primaryList[i])
			&& equalsMagicFileName(primaryList[i].iniFile, iniName))
		{
			return &primaryList[i];
		}
	}
	return nullptr;
}

void MagicManager::load(int index)
{
	configureLayout();
	freeResource();
    std::string fName =
		SaveFileManager::CurrentPath() + MAGIC_INI_NAME;
    if (index >= 0)
    {
        fName += convert::formatString("%d", index);
    }
	fName += MAGIC_INI_EXT;
	INIReader ini(fName);
	currentUseMagicFile = ini.Get("Head", "CurrentUseMagicFile", "");

	for (const auto& section : ini.GetSectionNames())
	{
		int sectionIndex = 0;
		if (!parsePositiveSectionIndex(section, sectionIndex))
		{
			continue;
		}
		if (sectionIndex >= hideStartIndex())
		{
			int hiddenIndex = sectionIndex == hideStartIndex() ? 0 : sectionIndex - hideStartIndex() - 1;
			if (hiddenIndex >= 0 && hiddenIndex < static_cast<int>(hiddenMagicList.size()))
			{
				loadMagicInfoFromIni(ini, section, hiddenMagicList[hiddenIndex], 0);
			}
			continue;
		}

		int listIndex = sectionIndex - 1;
		if (listIndex >= 0 && listIndex < static_cast<int>(magicList.size()))
		{
			loadMagicInfoFromIni(ini, section, magicList[listIndex], 1);
		}
	}
	int currentUseIndex = findMagicIndexInList(magicList, currentUseMagicFile);
	if (currentUseIndex < 0 || !isBottomIndex(currentUseIndex))
	{
		currentUseMagicFile.clear();
	}
	refreshPlayerMagicAttributes();
}

bool MagicManager::save(int index)
{
	INIReader ini;
	std::string section = "Head";
	ini.SetInteger(section, "Count", 0);
	ini.Set(section, "CurrentUseMagicFile", currentUseMagicFile);
	int count = 0;
	const auto& listToSave = isInReplaceMagicList ? replaceMagicListBackup : magicList;
	for (size_t i = 0; i < listToSave.size(); i++)
	{
		if (magicInfoIsUsable(listToSave[i]))
		{
			count++;
			section = convert::formatString("%d", i + 1);
			ini.Set(section, "IniFile", listToSave[i].iniFile);
			ini.SetInteger(section, "Level", listToSave[i].level);
			ini.SetInteger(section, "Exp", listToSave[i].exp);
			ini.SetInteger(section, "HideCount", listToSave[i].hideCount);
			ini.SetInteger(section, "LastIndexWhenHide", listToSave[i].lastIndexWhenHide);
		}
	}
	for (size_t i = 0; i < hiddenMagicList.size(); i++)
	{
		if (magicInfoIsUsable(hiddenMagicList[i]))
		{
			section = convert::formatString("%d", hideStartIndex() + static_cast<int>(i) + 1);
			ini.Set(section, "IniFile", hiddenMagicList[i].iniFile);
			ini.SetInteger(section, "Level", hiddenMagicList[i].level);
			ini.SetInteger(section, "Exp", hiddenMagicList[i].exp);
			ini.SetInteger(section, "HideCount", hiddenMagicList[i].hideCount);
			ini.SetInteger(section, "LastIndexWhenHide", hiddenMagicList[i].lastIndexWhenHide);
		}
	}
	section = "Head";
	ini.SetInteger(section, "Count", count);
    std::string fName = MAGIC_INI_NAME;
    if (index >= 0)
    {
        fName += convert::formatString("%d", index);
    }
    fName += MAGIC_INI_EXT;
	const bool saved = ini.saveToFile(SaveFileManager::CurrentPath() + fName);
    
    SaveFileManager::AppendFile(fName);
	return saved;
}

void MagicManager::freeResource()
{
	if (isInReplaceMagicList)
	{
		magicList = replaceMagicListBackup;
	}
	isInReplaceMagicList = false;
	currentReplaceMagicListKey.clear();
	replaceMagicListBackup.clear();
	replaceMagicListCache.clear();
	for (size_t i = 0; i < magicList.size(); i++)
	{
		clearMagicInfo(magicList[i]);
	}
	for (size_t i = 0; i < hiddenMagicList.size(); i++)
	{
		clearMagicInfo(hiddenMagicList[i]);
	}
	attackMagicList.clear();
	currentUseMagicFile.clear();
}

void MagicManager::clearMagicList()
{
	if (isInReplaceMagicList)
	{
		magicList = replaceMagicListBackup;
	}
	isInReplaceMagicList = false;
	currentReplaceMagicListKey.clear();
	replaceMagicListBackup.clear();
	replaceMagicListCache.clear();
	currentUseMagicFile.clear();
	for (size_t i = 0; i < magicList.size(); i++)
	{
		clearMagicInfo(magicList[i]);
	}
	for (size_t i = 0; i < hiddenMagicList.size(); i++)
	{
		clearMagicInfo(hiddenMagicList[i]);
	}
	refreshPlayerMagicAttributes();
	updateMenu();
}

void MagicManager::refreshPlayerMagicAttributes()
{
	if (gm == nullptr || this != &gm->magicManager || gm->player == nullptr)
	{
		return;
	}
	gm->player->calInfo();
	gm->player->limitAttribute();
}

void MagicManager::replaceMagicList(const std::string& replacementList)
{
	if (replacementList.empty())
	{
		return;
	}
	if (!isInReplaceMagicList)
	{
		replaceMagicListBackup = magicList;
	}
	else
	{
		replaceMagicListCache[currentReplaceMagicListKey] = magicList;
		if (currentReplaceMagicListKey == replacementList)
		{
			return;
		}
	}

	currentReplaceMagicListKey = replacementList;
	auto cacheIter = replaceMagicListCache.find(replacementList);
	if (cacheIter != replaceMagicListCache.end())
	{
		magicList = cacheIter->second;
	}
	else
	{
		std::vector<MagicInfo> replacementListData(static_cast<size_t>(listLength()), MagicInfo());
		auto names = parseReplaceMagicNames(replacementList);
		size_t nameIndex = 0;
		for (int i = bottomBegin(); i <= bottomEnd() && i < listLength() && nameIndex < names.size(); i++)
		{
			replacementListData[static_cast<size_t>(i)] = makeReplacementMagicInfo(names[nameIndex]);
			nameIndex++;
		}
		for (int i = storeBegin(); i <= storeEnd() && i < listLength() && nameIndex < names.size(); i++)
		{
			replacementListData[static_cast<size_t>(i)] = makeReplacementMagicInfo(names[nameIndex]);
			nameIndex++;
		}
		replaceMagicListCache[replacementList] = replacementListData;
		magicList = replacementListData;
	}
	isInReplaceMagicList = true;
	refreshPlayerMagicAttributes();
	updateMenu();
}

int MagicManager::primaryFreeIndex() const
{
	const auto& primaryList = primaryMagicList();
	for (int i = storeBegin(); i <= storeEnd() && i < static_cast<int>(primaryList.size()); i++)
	{
		if (primaryList[static_cast<size_t>(i)].iniFile.empty())
		{
			return i;
		}
	}
	for (int i = bottomBegin(); i <= bottomEnd() && i < static_cast<int>(primaryList.size()); i++)
	{
		if (primaryList[static_cast<size_t>(i)].iniFile.empty())
		{
			return i;
		}
	}
	return -1;
}

bool MagicManager::primaryMagicListExists(int index) const
{
	const auto& primaryList = primaryMagicList();
	if (index >= 0 && index < static_cast<int>(primaryList.size()))
	{
		return magicInfoIsUsable(
			primaryList[static_cast<size_t>(index)]);
	}
	return false;
}

void MagicManager::stopReplaceMagicList()
{
	if (!isInReplaceMagicList)
	{
		return;
	}
	replaceMagicListCache[currentReplaceMagicListKey] = magicList;
	magicList = replaceMagicListBackup;
	replaceMagicListBackup.clear();
	currentReplaceMagicListKey.clear();
	isInReplaceMagicList = false;
	refreshPlayerMagicAttributes();
	updateMenu();
}

void MagicManager::addPracticeExp(int addexp)
{
	int index = practiceIndex();
	if (magicListExists(index))
	{
		magicList[index].exp = addExperienceSaturated(
			magicList[index].exp,
			addexp);
		gm->menu->practiceMenu->updateExp();
		bool lup = false;
		while (magicList[index].level < MAGIC_MAX_LEVEL && magicList[index].exp >= magicList[index].magic->level[magicList[index].level].levelupExp)
		{
			magicList[index].level++;
			lup = true;
		}
		if (lup)
		{
			refreshPlayerMagicAttributes();
			gm->menu->practiceMenu->updateExp();
			gm->menu->practiceMenu->updateLevel();
			gm->showMessage(convert::formatString("%s的等级提升了！", magicList[index].magic->name.c_str(), magicList[index].level));
		}
	}
}

bool MagicManager::addPracticeExperienceToNextLevel()
{
	const int index = practiceIndex();
	if (!magicListExists(index))
	{
		return false;
	}

	MagicInfo& info = magicList[static_cast<std::size_t>(index)];
	if (info.magic == nullptr || info.level < 1 ||
		info.level >= MAGIC_MAX_LEVEL)
	{
		return false;
	}

	const std::int64_t targetExperience =
		info.magic->level[info.level].levelupExp;
	const std::int64_t requiredExperience = std::max<std::int64_t>(
		0, targetExperience - static_cast<std::int64_t>(info.exp));
	if (targetExperience <= 0 || requiredExperience > INT_MAX)
	{
		return false;
	}

	const int previousLevel = info.level;
	addPracticeExp(static_cast<int>(requiredExperience));
	return info.level > previousLevel;
}

void MagicManager::addUseExp(std::shared_ptr<Effect> e, int addexp)
{
	if (e == nullptr)
	{
		return;
	}
	const std::string& experienceMagicFile = e->magic.experienceOwnerMagicFile.empty()
		? e->magic.iniName
		: e->magic.experienceOwnerMagicFile;
	addUseExperience(experienceMagicFile, addexp);
}

void MagicManager::addUseExperience(const std::string& magicFile, int addexp)
{
	for (size_t i = 0; i < magicList.size(); i++)
	{
		if (magicInfoIsUsable(magicList[i])
			&& equalsMagicFileName(magicList[i].iniFile, magicFile))
		{
			magicList[i].exp = addExperienceSaturated(
				magicList[i].exp,
				addexp);
			bool lup = false;
			while (magicList[i].level < MAGIC_MAX_LEVEL && magicList[i].exp >= magicList[i].magic->level[magicList[i].level].levelupExp)
			{
				magicList[i].level++;
				lup = true;
			}
			if (lup)
			{
				refreshPlayerMagicAttributes();
				gm->menu->practiceMenu->updateExp();
				gm->menu->practiceMenu->updateLevel();
				gm->showMessage(convert::formatString("%s的等级提升了！", magicList[i].magic->name.c_str(), magicList[i].level));
			}
			break;
		}
	}
}

void MagicManager::addHitExp(std::shared_ptr<Effect> e, int targetLevel)
{
	if (!usesConfiguredExperienceRules || e == nullptr)
	{
		return;
	}
	const int hitExperience = hitExperienceForTargetLevel(targetLevel);
	const std::string& experienceMagicFile = e->magic.experienceOwnerMagicFile.empty()
		? e->magic.iniName
		: e->magic.experienceOwnerMagicFile;
	if (findMagic(experienceMagicFile) != nullptr)
	{
		addUseExperience(experienceMagicFile, hitExperience);
	}
	else if (!currentUseMagicFile.empty())
	{
		addUseExperience(currentUseMagicFile, hitExperience);
	}
}

void MagicManager::addKillExp(
	std::shared_ptr<Effect> e,
	double scaledExperience)
{
	if (!usesConfiguredExperienceRules)
	{
		const int automaticExperience =
			floorAutomaticExperience(scaledExperience, 1.0);
		addPracticeExp(automaticExperience);
		addUseExp(e, automaticExperience);
		return;
	}

	addPracticeExp(floorAutomaticExperience(
		scaledExperience,
		practiceKillExperienceFraction));
	if (!currentUseMagicFile.empty())
	{
		addUseExperience(currentUseMagicFile,
			floorAutomaticExperience(
				scaledExperience,
				currentUseKillExperienceFraction));
	}
}

void MagicManager::recordCurrentUseMagic(int listIndex)
{
	if (!isBottomIndex(listIndex) || !magicListExists(listIndex))
	{
		return;
	}
	currentUseMagicFile = magicList[static_cast<size_t>(listIndex)].iniFile;
}

void MagicManager::addMagicExp(const std::string & magicName, int addexp)
{
	MagicInfo * m = findPrimaryMagic(magicName);
	if (m != nullptr)
	{
		m->exp = addExperienceSaturated(m->exp, addexp);
		bool lup = false;
		while (m->level < MAGIC_MAX_LEVEL && m->exp >= m->magic->level[m->level].levelupExp)
		{
			m->level++;
			lup = true;
		}
		if (lup)
		{
			refreshPlayerMagicAttributes();
		}
	}
}

void MagicManager::addMagic(const std::string & magicName)
{
	addPrimaryMagic(magicName, true, true);
}

MagicInfo* MagicManager::addPrimaryMagic(const std::string& magicName, bool showMessage, bool refreshAttributes)
{
	if (magicName.empty())
	{
		return nullptr;
	}
	MagicInfo* existingMagic = findPrimaryMagic(magicName);
	if (existingMagic != nullptr)
	{
		return existingMagic;
	}

	int index = primaryFreeIndex();
	if (index < 0)
	{
		return nullptr;
	}
	auto magic = loadMagicResource(magicName, "runtime add");
	if (magic == nullptr)
	{
		return nullptr;
	}

	auto& primaryList = primaryMagicList();
	MagicInfo& info = primaryList[static_cast<size_t>(index)];
	info.iniFile = magicName;
	info.level = 1;
	info.exp = 0;
	info.hideCount = 1;
	info.lastIndexWhenHide = 0;
	info.remainColdMilliseconds = 0;
	info.magic = magic;
	if (refreshAttributes)
	{
		refreshPlayerMagicAttributes();
	}
	if (!isInReplaceMagicList)
	{
		updateMenu(index);
	}
	else
	{
		updateMenu();
	}
	if (showMessage && gm != nullptr && info.magic != nullptr)
	{
		gm->showMessage(convert::formatString("学会了%s！", info.magic->name.c_str()));
	}
	return &info;
}

MagicInfo* MagicManager::addEquipmentMagic(const std::string& magicName, bool showMessage, bool refreshAttributes)
{
	return addPrimaryMagic(magicName, showMessage, refreshAttributes);
}

void MagicManager::deleteMagic(const std::string & magicName)
{
	deletePrimaryMagic(magicName);
}

void MagicManager::deletePrimaryMagic(const std::string& magicName)
{
	if (magicName.empty())
	{
		return;
	}
	MagicInfo * m = findPrimaryMagic(magicName);
	if (m != nullptr)
	{
		if (equalsMagicFileName(currentUseMagicFile, magicName))
		{
			currentUseMagicFile.clear();
		}
		clearMagicInfo(*m);
		refreshPlayerMagicAttributes();
		updateMenu();
	}
}

void MagicManager::clearPrimaryMagicList()
{
	currentUseMagicFile.clear();
	auto& primaryList = primaryMagicList();
	for (size_t i = 0; i < primaryList.size(); i++)
	{
		clearMagicInfo(primaryList[i]);
	}
	for (size_t i = 0; i < hiddenMagicList.size(); i++)
	{
		clearMagicInfo(hiddenMagicList[i]);
	}
	refreshPlayerMagicAttributes();
	updateMenu();
}

void MagicManager::setPrimaryMagicLevel(const std::string& magicName, int level)
{
	MagicInfo* info = findPrimaryMagic(magicName);
	if (info == nullptr || info->magic == nullptr)
	{
		return;
	}
	info->level = std::clamp(level, 1, MAGIC_MAX_LEVEL);
	info->exp = info->level > 1 ? info->magic->level[info->level - 1].levelupExp : 0;
	refreshPlayerMagicAttributes();
	updateMenu();
}

MagicInfo* MagicManager::setMagicHidden(const std::string& magicName, bool hidden, bool refreshAttributes, bool updateMenus)
{
	if (magicName.empty())
	{
		return nullptr;
	}

	if (hidden)
	{
		auto& primaryList = primaryMagicList();
		int listIndex = findMagicIndexInList(primaryList, magicName);
		if (listIndex < 0)
		{
			return nullptr;
		}
		MagicInfo& info = primaryList[listIndex];
		if (info.hideCount > 0)
		{
			info.hideCount--;
		}
		if (info.hideCount > 0)
		{
			if (refreshAttributes)
			{
				refreshPlayerMagicAttributes();
			}
			if (updateMenus)
			{
				isInReplaceMagicList ? updateMenu() : updateMenu(listIndex);
			}
			return &info;
		}
		int hiddenIndex = -1;
		for (int i = 0; i < static_cast<int>(hiddenMagicList.size()); i++)
		{
			if (hiddenMagicList[i].iniFile.empty())
			{
				hiddenIndex = i;
				break;
			}
		}
		if (hiddenIndex < 0)
		{
			info.hideCount = 1;
			return nullptr;
		}
		if (equalsMagicFileName(currentUseMagicFile, magicName))
		{
			currentUseMagicFile.clear();
		}

		MagicInfo movedInfo = info;
		movedInfo.hideCount = 0;
		movedInfo.lastIndexWhenHide = listIndex;
		hiddenMagicList[hiddenIndex] = movedInfo;
		clearMagicInfo(info);
		if (refreshAttributes)
		{
			refreshPlayerMagicAttributes();
		}
		if (updateMenus)
		{
			isInReplaceMagicList ? updateMenu() : updateMenu(listIndex);
		}
		return &hiddenMagicList[hiddenIndex];
	}

	auto& primaryList = primaryMagicList();
	int listIndex = findMagicIndexInList(primaryList, magicName);
	if (listIndex >= 0)
	{
		if (primaryList[listIndex].hideCount < INT_MAX)
		{
			primaryList[listIndex].hideCount++;
		}
		if (refreshAttributes)
		{
			refreshPlayerMagicAttributes();
		}
		if (updateMenus)
		{
			isInReplaceMagicList ? updateMenu() : updateMenu(listIndex);
		}
		return &primaryList[listIndex];
	}

	int hiddenIndex = findMagicIndexInList(hiddenMagicList, magicName);
	if (hiddenIndex < 0)
	{
		return nullptr;
	}

	MagicInfo movedInfo = hiddenMagicList[hiddenIndex];
	movedInfo.hideCount = 1;
	int targetIndex = -1;
	if (movedInfo.lastIndexWhenHide >= 0
		&& movedInfo.lastIndexWhenHide < static_cast<int>(primaryList.size())
		&& primaryList[movedInfo.lastIndexWhenHide].iniFile.empty())
	{
		targetIndex = movedInfo.lastIndexWhenHide;
	}
	else
	{
		for (int i = storeBegin(); i <= bottomEnd() && i < listLength(); i++)
		{
			if ((isStoreIndex(i) || isBottomIndex(i)) && primaryList[i].iniFile.empty())
			{
				targetIndex = i;
				break;
			}
		}
	}

	if (targetIndex < 0)
	{
		return nullptr;
	}

	clearMagicInfo(hiddenMagicList[hiddenIndex]);
	primaryList[targetIndex] = movedInfo;
	if (refreshAttributes)
	{
		refreshPlayerMagicAttributes();
	}
	if (updateMenus)
	{
		isInReplaceMagicList ? updateMenu() : updateMenu(targetIndex);
	}
	return &primaryList[targetIndex];
}

bool MagicManager::isMagicHidden(const std::string& magicName) const
{
	return findMagicIndexInList(hiddenMagicList, magicName) >= 0;
}

std::vector<MagicInfo>& MagicManager::primaryMagicList()
{
	return isInReplaceMagicList ? replaceMagicListBackup : magicList;
}

const std::vector<MagicInfo>& MagicManager::primaryMagicList() const
{
	return isInReplaceMagicList ? replaceMagicListBackup : magicList;
}

void MagicManager::updateColdTimes(UTime frameTime)
{
	if (frameTime == 0)
	{
		return;
	}
	for (auto& magicInfo : magicList)
	{
		if (magicInfo.remainColdMilliseconds == 0)
		{
			continue;
		}
		if (magicInfo.remainColdMilliseconds > frameTime)
		{
			magicInfo.remainColdMilliseconds -= frameTime;
		}
		else
		{
			magicInfo.remainColdMilliseconds = 0;
		}
	}
}

void MagicManager::updateMenu(int idx)
{
	if (gm == nullptr || gm->menu == nullptr)
	{
		return;
	}
	if (isStoreIndex(idx))
	{
		if (gm->menu->magicMenu != nullptr)
		{
			gm->menu->magicMenu->updateMagic();
		}
	}
	else if (isBottomIndex(idx))
	{
		if (gm->menu->bottomMenu != nullptr)
		{
			gm->menu->bottomMenu->updateMagicItem();
		}
	}
	else
	{
		if (gm->menu->practiceMenu != nullptr)
		{
			gm->menu->practiceMenu->updateMagic();
		}
	}
	if (gm->menu->equipMenu != nullptr)
	{
		gm->menu->equipMenu->updateMagicDisplay();
	}
}

void MagicManager::updateMenu()
{
	if (gm == nullptr || gm->menu == nullptr)
	{
		return;
	}
	if (gm->menu->magicMenu != nullptr)
	{
		gm->menu->magicMenu->updateMagic();
	}
	if (gm->menu->bottomMenu != nullptr)
	{
		gm->menu->bottomMenu->updateMagicItem();
	}
	if (gm->menu->practiceMenu != nullptr)
	{
		gm->menu->practiceMenu->updateMagic();
	}
	if (gm->menu->equipMenu != nullptr)
	{
		gm->menu->equipMenu->updateMagicDisplay();
	}
}

void MagicManager::exchange(int index1, int index2)
{
	if (index1 >= 0 && index2 >= 0 && index1 < listLength() && index2 < listLength())
	{
		if (isBottomIndex(index1) != isBottomIndex(index2)
			&& (equalsMagicFileName(currentUseMagicFile, magicList[index1].iniFile)
				|| equalsMagicFileName(currentUseMagicFile, magicList[index2].iniFile)))
		{
			currentUseMagicFile.clear();
		}
		MagicInfo tempInfo = magicList[index1];
		magicList[index1] = magicList[index2];
		magicList[index2] = tempInfo;
	}
}

bool MagicManager::magicListExists(int index)
{
	if (index >= 0 && index < listLength())
	{
		return magicInfoIsUsable(magicList[index]);
	}
	return false;
}

void MagicManager::configureLayout()
{
	int length = MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT;
	if (gm != nullptr)
	{
		length = gm->global.magicLayout.listLength();
	}
	if (length < 1)
	{
		length = MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT;
	}
	magicList.assign(static_cast<size_t>(length), MagicInfo());
	hiddenMagicList.assign(static_cast<size_t>(length), MagicInfo());
	currentUseMagicFile.clear();
	loadExperienceRules();
}

void MagicManager::loadExperienceRules()
{
	hitExperienceLevelFactor = 0;
	practiceKillExperienceFraction = 1.0f;
	currentUseKillExperienceFraction = 1.0f;
	usesConfiguredExperienceRules = false;

	INIReader ini("ini\\level\\MagicExp.ini");
	if (ini.ParseError() != 0)
	{
		return;
	}

	const long hitLevelFactor = ini.GetInteger("HitMagicExp", "LevelFactor", -1);
	const float practiceFraction = ini.GetReal("XiuLianMagicExp", "Fraction", -1.0f);
	const float currentUseFraction = ini.GetReal("UseMagicExp", "Fraction", -1.0f);
	if (hitLevelFactor < 0 || hitLevelFactor > INT_MAX
		|| practiceFraction < 0.0f || currentUseFraction < 0.0f)
	{
		return;
	}

	hitExperienceLevelFactor = static_cast<int>(hitLevelFactor);
	practiceKillExperienceFraction = practiceFraction;
	currentUseKillExperienceFraction = currentUseFraction;
	usesConfiguredExperienceRules = true;
}

int MagicManager::hitExperienceForTargetLevel(int targetLevel) const
{
	if (targetLevel <= 0 || hitExperienceLevelFactor <= 0)
	{
		return 0;
	}
	const long long experience = static_cast<long long>(targetLevel) * hitExperienceLevelFactor;
	if (experience > INT_MAX)
	{
		return INT_MAX;
	}
	return static_cast<int>(experience);
}

int MagicManager::listLength() const
{
	return static_cast<int>(magicList.size());
}

int MagicManager::storeBegin() const
{
	return gm != nullptr ? gm->global.magicLayout.storeBegin : 0;
}

int MagicManager::storeEnd() const
{
	return gm != nullptr ? gm->global.magicLayout.storeEnd : MAGIC_COUNT - 1;
}

int MagicManager::bottomCount() const
{
	return gm != nullptr ? gm->global.magicLayout.bottomCount() : MAGIC_TOOLBAR_COUNT;
}

int MagicManager::bottomBegin() const
{
	return gm != nullptr ? gm->global.magicLayout.bottomBegin : MAGIC_COUNT;
}

int MagicManager::bottomEnd() const
{
	return gm != nullptr ? gm->global.magicLayout.bottomEnd : MAGIC_COUNT + MAGIC_TOOLBAR_COUNT - 1;
}

int MagicManager::practiceIndex() const
{
	return gm != nullptr ? gm->global.magicLayout.practiceIndex : MAGIC_COUNT + MAGIC_TOOLBAR_COUNT;
}

int MagicManager::bottomIndex(int index) const
{
	return bottomBegin() + index;
}

int MagicManager::bottomSlot(int index) const
{
	return index - bottomBegin();
}

int MagicManager::hideStartIndex() const
{
	return gm != nullptr ? gm->global.magicLayout.hideStartIndex : 1000;
}

bool MagicManager::isStoreIndex(int index) const
{
	return index >= storeBegin() && index <= storeEnd();
}

bool MagicManager::isBottomIndex(int index) const
{
	return index >= bottomBegin() && index <= bottomEnd();
}

bool MagicManager::isPracticeIndex(int index) const
{
	return index == practiceIndex();
}

std::shared_ptr<Magic> MagicManager::loadAttackMagic(const std::string & name)
{	
	if (name.empty())
	{
		return std::shared_ptr<Magic>(nullptr);
	}

	auto m = attackMagicList.find(name);
	if (m != attackMagicList.end())
	{
		return m->second;
	}

	std::shared_ptr<Magic> am = std::make_shared<Magic>();
	am->initFromIni(name);
	attackMagicList[name] = am;
	return am;
}

void MagicManager::tryCleanAttackMagic()
{
	auto iter = attackMagicList.begin();
	while (iter != attackMagicList.end())
	{
		if (iter->second.use_count() <= 1)
		{
			iter->second->freeResource();
			iter->second = nullptr;
			iter = attackMagicList.erase(iter);
		}
		else
		{
			iter++;
		}
	}
}
