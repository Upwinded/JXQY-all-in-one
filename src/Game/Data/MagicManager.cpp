#include "MagicManager.h"
#include "../../File/INIReader.h"
#include "SaveIniPersistence.h"
#include "../../GameplayAutomation/GameplayAutomationSession.h"
#include "DefeatedNpcExperience.h"
#include "../../libconvert/libconvert.h"
#include "../../File/log.h"
#include "../GameManager/GameManager.h"
#include "../GameManager/SaveFileManager.h"
#include "../../File/File.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>

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

int addExperienceSaturated(const MagicInfo& info, int addedExperience)
{
	if (info.magic->definedLearningLevelLimit > 0 && info.level >= info.magic->definedLearningLevelLimit)
	{
		return info.exp;
	}
	const int64_t total = static_cast<int64_t>(info.exp)
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
		magic->experienceOwner = { true, magic };
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

void saveMagicInfoToIni(INIReader& ini, const std::string& section, const MagicInfo& info)
{
	ini.Set(section, "IniFile", info.iniFile);
	ini.SetInteger(section, "Level", info.level);
	ini.SetInteger(section, "Exp", info.exp);
	ini.SetInteger(section, "HideCount", info.hideCount);
	ini.SetInteger(section, "LastIndexWhenHide", info.lastIndexWhenHide);
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

int findMagicIndexInList(const std::vector<MagicInfo>& list, const std::weak_ptr<Magic>& source)
{
	const auto magic = source.lock();
	if (magic == nullptr)
	{
		return -1;
	}
	for (int i = 0; i < static_cast<int>(list.size()); ++i)
	{
		if (magicInfoIsUsable(list[i]) && list[i].magic == magic)
		{
			return i;
		}
	}
	return -1;
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

bool MagicManager::load(int index, std::string* failureReason)
{
	if (failureReason != nullptr)
	{
		failureReason->clear();
	}
	std::string fName =
		SaveFileManager::CurrentPath() + MAGIC_INI_NAME;
	std::string displayName = MAGIC_INI_NAME;
	if (index >= 0)
	{
		fName += convert::formatString("%d", index);
		displayName += convert::formatString("%d", index);
	}
	fName += MAGIC_INI_EXT;
	displayName += MAGIC_INI_EXT;

	std::shared_ptr<INIReader> loadedIni;
	const SaveIniPersistence::ReadStatus status =
		SaveIniPersistence::read(fName, loadedIni);
	if (status == SaveIniPersistence::ReadStatus::Empty ||
		status == SaveIniPersistence::ReadStatus::Unreadable ||
		status == SaveIniPersistence::ReadStatus::Malformed)
	{
		if (failureReason != nullptr)
		{
			*failureReason = u8"武功数据文件无法读取或格式错误：" +
				displayName;
		}
		return false;
	}

	MagicManager loadedManager;
	if (status == SaveIniPersistence::ReadStatus::Loaded)
	{
		if (loadedIni == nullptr || !loadedIni->HasSection("Head"))
		{
			if (failureReason != nullptr)
			{
				*failureReason = u8"武功数据缺少 [Head]：" + displayName;
			}
			return false;
		}
		const long count = loadedIni->GetInteger("Head", "Count", -1);
		if (count < 0)
		{
			if (failureReason != nullptr)
			{
				*failureReason = u8"武功数据数量无效：" + displayName;
			}
			return false;
		}

		const std::string currentUseMagicFile =
			loadedIni->Get("Head", "CurrentUseMagicFile", "");
		for (const auto& section : loadedIni->GetSectionNames())
		{
			int sectionIndex = 0;
			const std::string replacementPrefix = "replacementlist";
			if (section.compare(0, replacementPrefix.size(), replacementPrefix) == 0
				&& parsePositiveSectionIndex(section.substr(replacementPrefix.size()), sectionIndex))
			{
				const std::string key = loadedIni->Get(section, "Key", "");
				if (key.empty())
				{
					GameLog::write("MagicManager: skip replacement list without a key %s\n", section.c_str());
					continue;
				}
				auto& list = loadedManager.replaceMagicListCache[key];
				list.resize(static_cast<size_t>(loadedManager.listLength()));
				for (size_t i = 0; i < list.size(); ++i)
				{
					const std::string itemSection = section + ":" + std::to_string(i + 1);
					if (loadedIni->HasSection(itemSection))
					{
						loadMagicInfoFromIni(*loadedIni, itemSection, list[i], 1);
					}
				}
				continue;
			}
			if (!parsePositiveSectionIndex(section, sectionIndex))
			{
				continue;
			}
			if (sectionIndex >= loadedManager.hideStartIndex())
			{
				const int hiddenIndex =
					sectionIndex == loadedManager.hideStartIndex()
					? 0
					: sectionIndex - loadedManager.hideStartIndex() - 1;
				if (hiddenIndex >= 0 &&
					hiddenIndex < static_cast<int>(
						loadedManager.hiddenMagicList.size()))
				{
					loadMagicInfoFromIni(
						*loadedIni,
						section,
						loadedManager.hiddenMagicList[hiddenIndex],
						0);
				}
				continue;
			}

			const int listIndex = sectionIndex - 1;
			if (listIndex >= 0 &&
				listIndex < static_cast<int>(loadedManager.magicList.size()))
			{
				loadMagicInfoFromIni(
					*loadedIni,
					section,
					loadedManager.magicList[listIndex],
					1);
			}
		}
		const long savedCurrentUseIndex = loadedIni->GetInteger("Head", "CurrentUseMagicIndex", -1);
		const int currentUseIndex = savedCurrentUseIndex >= 0 && savedCurrentUseIndex <= loadedManager.listLength()
			? static_cast<int>(savedCurrentUseIndex) - 1
			: findMagicIndexInList(loadedManager.magicList, currentUseMagicFile);
		if (loadedManager.isBottomIndex(currentUseIndex) && loadedManager.magicListExists(currentUseIndex))
		{
			loadedManager.recordCurrentUseMagic(currentUseIndex);
		}
	}

	freeResource();
	magicList = std::move(loadedManager.magicList);
	hiddenMagicList = std::move(loadedManager.hiddenMagicList);
	replaceMagicListCache = std::move(loadedManager.replaceMagicListCache);
	currentUseMagic = std::move(loadedManager.currentUseMagic);
	hitExperienceLevelFactor = loadedManager.hitExperienceLevelFactor;
	practiceKillExperienceFraction =
		loadedManager.practiceKillExperienceFraction;
	currentUseKillExperienceFraction =
		loadedManager.currentUseKillExperienceFraction;
	usesConfiguredExperienceRules =
		loadedManager.usesConfiguredExperienceRules;
	// Character load/rollback also replaces Goods next. Its final refresh must
	// see both saved lists; outgoing equipment can otherwise clamp the loaded
	// attributes or grant its magic into the incoming character's list.
	return true;
}

bool MagicManager::save(int index)
{
	INIReader ini;
	std::string section = "Head";
	ini.SetInteger(section, "Count", 0);
	int count = 0;
	const auto& listToSave = isInReplaceMagicList ? replaceMagicListBackup : magicList;
	const int currentUseIndex = findMagicIndexInList(magicList, currentUseMagic);
	// Loading ends the form. Keep the selected toolbar slot in the primary list
	// without changing the active form's experience target while saving.
	const bool hasSavedSelection = currentUseIndex >= 0 && isBottomIndex(currentUseIndex)
		&& currentUseIndex < static_cast<int>(listToSave.size()) && magicInfoIsUsable(listToSave[currentUseIndex]);
	ini.Set(section, "CurrentUseMagicFile", hasSavedSelection ? listToSave[currentUseIndex].iniFile : "");
	ini.SetInteger(section, "CurrentUseMagicIndex", hasSavedSelection ? currentUseIndex + 1 : 0);
	for (size_t i = 0; i < listToSave.size(); i++)
	{
		if (magicInfoIsUsable(listToSave[i]))
		{
			count++;
			section = convert::formatString("%d", i + 1);
			saveMagicInfoToIni(ini, section, listToSave[i]);
		}
	}
	for (size_t i = 0; i < hiddenMagicList.size(); i++)
	{
		if (magicInfoIsUsable(hiddenMagicList[i]))
		{
			section = convert::formatString("%d", hideStartIndex() + static_cast<int>(i) + 1);
			saveMagicInfoToIni(ini, section, hiddenMagicList[i]);
		}
	}
	// A transformation ends on load, but its learned progress must survive.
	// Keep caches in the same character file/generation as the primary list.
	size_t replacementIndex = 0;
	for (const auto& cached : replaceMagicListCache)
	{
		const std::string listSection = "ReplacementList" + std::to_string(++replacementIndex);
		ini.Set(listSection, "Key", cached.first);
		const auto& list = isInReplaceMagicList && cached.first == currentReplaceMagicListKey
			? magicList : cached.second;
		for (size_t i = 0; i < list.size(); ++i)
		{
			if (magicInfoIsUsable(list[i]))
			{
				saveMagicInfoToIni(ini, listSection + ":" + std::to_string(i + 1), list[i]);
			}
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
	currentUseMagic.reset();
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
	currentUseMagic.reset();
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

void MagicManager::replaceMagicList(const std::string& replacementList, const std::string& sourceIdentity)
{
	if (replacementList.empty())
	{
		return;
	}
	const auto names = parseReplaceMagicNames(replacementList);
	std::string replacementKey = replacementList;
	if (!sourceIdentity.empty())
	{
		// Published player forms identify progress by character, source magic name
		// and parsed files. This is an INI value, not a path to an external file.
		replacementKey = sourceIdentity + "_";
		for (size_t i = 0; i < names.size(); ++i)
		{
			if (i > 0) replacementKey += "_";
			replacementKey += names[i];
		}
		replacementKey += ".ini";
	}
	if (!isInReplaceMagicList)
	{
		replaceMagicListBackup = magicList;
	}
	else
	{
		replaceMagicListCache[currentReplaceMagicListKey] = magicList;
		if (currentReplaceMagicListKey == replacementKey)
		{
			return;
		}
	}

	const int currentUseIndex = findMagicIndexInList(magicList, currentUseMagic);
	currentReplaceMagicListKey = replacementKey;
	auto cacheIter = replaceMagicListCache.find(replacementKey);
	if (cacheIter != replaceMagicListCache.end())
	{
		magicList = cacheIter->second;
	}
	else
	{
		std::vector<MagicInfo> replacementListData(static_cast<size_t>(listLength()), MagicInfo());
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
		replaceMagicListCache[replacementKey] = replacementListData;
		magicList = replacementListData;
	}
	isInReplaceMagicList = true;
	currentUseMagic.reset();
	recordCurrentUseMagic(currentUseIndex);
	refreshPlayerMagicAttributes();
	updateMenu();
}

int MagicManager::primaryFreeIndex(bool talent) const
{
	const auto& primaryList = primaryMagicList();
	if (talent && gm != nullptr && gm->global.magicLayout.talentBegin >= 0)
	{
		for (int i = gm->global.magicLayout.talentBegin;
			i <= gm->global.magicLayout.talentEnd && i < static_cast<int>(primaryList.size()); ++i)
		{
			if (primaryList[static_cast<size_t>(i)].iniFile.empty()) return i;
		}
		return -1;
	}
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
	const int currentUseIndex = findMagicIndexInList(magicList, currentUseMagic);
	replaceMagicListCache[currentReplaceMagicListKey] = magicList;
	magicList = replaceMagicListBackup;
	replaceMagicListBackup.clear();
	currentReplaceMagicListKey.clear();
	isInReplaceMagicList = false;
	currentUseMagic.reset();
	recordCurrentUseMagic(currentUseIndex);
	refreshPlayerMagicAttributes();
	updateMenu();
}

void MagicManager::addTalentJumpRadius(const MagicInfo& info)
{
	if (gm == nullptr || gm->player == nullptr || !gm->global.feature.separateTalentSlots
		|| gm->global.magicLayout.talentBegin < 0 || info.magic == nullptr)
	{
		return;
	}
	const auto& layout = gm->global.magicLayout;
	const auto& primary = primaryMagicList();
	bool talent = false;
	for (int i = layout.talentBegin; i <= layout.talentEnd && i < static_cast<int>(primary.size()); ++i)
	{
		if (&primary[i] == &info) talent = true;
	}
	if (!talent && info.lastIndexWhenHide >= layout.talentBegin && info.lastIndexWhenHide <= layout.talentEnd)
	{
		talent = std::any_of(hiddenMagicList.begin(), hiddenMagicList.end(),
			[&](const MagicInfo& hidden) { return &hidden == &info; });
	}
	if (talent)
	{
		const int64_t radius = static_cast<int64_t>(gm->player->jumpRadius) + info.magic->level[info.level].jumpRadius;
		gm->player->jumpRadius = static_cast<int>(std::clamp<int64_t>(radius, INT_MIN, INT_MAX));
	}
}

bool MagicManager::tryAdvanceMagicLevel(MagicInfo& info)
{
	bool leveledUp = false;
	const int levelLimit = info.magic->definedLearningLevelLimit > 0
		? std::min(MAGIC_MAX_LEVEL, info.magic->definedLearningLevelLimit) : MAGIC_MAX_LEVEL;
	while (info.level < levelLimit)
	{
		const int levelUpExperience =
			info.magic->level[info.level].levelupExp;
		// 零或负门槛只停止升级，不截断已累计的经验。
		if (levelUpExperience <= 0 || info.exp < levelUpExperience)
		{
			break;
		}
		info.level++;
		addTalentJumpRadius(info);
		leveledUp = true;
	}
	return leveledUp;
}

void MagicManager::addPracticeExp(int addexp)
{
	int index = practiceIndex();
	if (magicListExists(index))
	{
		magicList[index].exp = addExperienceSaturated(
			magicList[index],
			addexp);
		gm->menu->practiceMenu->updateExp();
		if (tryAdvanceMagicLevel(magicList[index]))
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
		info.level >= MAGIC_MAX_LEVEL ||
		(info.magic->definedLearningLevelLimit > 0 && info.level >= info.magic->definedLearningLevelLimit))
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
	const auto owner = Magic::getExperienceOwner(e->magicDispatchContext);
	if (owner.assigned)
	{
		if (auto* info = findExperienceOwner(owner))
		{
			addUseExperience(*info, addexp);
		}
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
			addUseExperience(magicList[i], addexp);
			break;
		}
	}
}

void MagicManager::addUseExperience(MagicInfo& info, int addexp)
{
	info.exp = addExperienceSaturated(info, addexp);
	if (tryAdvanceMagicLevel(info))
	{
		refreshPlayerMagicAttributes();
		if (gm != nullptr && gm->menu != nullptr && gm->menu->practiceMenu != nullptr)
		{
			gm->menu->practiceMenu->updateExp();
			gm->menu->practiceMenu->updateLevel();
		}
		if (gm != nullptr)
		{
			gm->showMessage(convert::formatString("%s的等级提升了！", info.magic->name.c_str(), info.level));
		}
	}
}

MagicInfo* MagicManager::findExperienceOwner(const MagicExperienceOwner& owner, std::string* listKey, int* slot)
{
	const auto source = owner.magic.lock();
	if (!owner.assigned || source == nullptr)
	{
		return nullptr;
	}
	const auto find = [&](std::vector<MagicInfo>& list, const std::string& key) -> MagicInfo*
	{
		for (size_t i = 0; i < list.size(); ++i)
		{
			if (magicInfoIsUsable(list[i]) && list[i].magic == source)
			{
				if (listKey != nullptr) *listKey = key;
				if (slot != nullptr) *slot = static_cast<int>(i + 1);
				return &list[i];
			}
		}
		return nullptr;
	};
	// The active vector is authoritative; its cache may still contain an old
	// value copy of the same learned object until the next list switch.
	if (auto* info = find(magicList, isInReplaceMagicList ? "replacement:" + currentReplaceMagicListKey : "primary")) return info;
	if (auto* info = find(hiddenMagicList, "hidden")) return info;
	if (auto* info = find(replaceMagicListBackup, "primary")) return info;
	for (auto& cached : replaceMagicListCache)
	{
		if (isInReplaceMagicList && cached.first == currentReplaceMagicListKey) continue;
		if (auto* info = find(cached.second, "replacement:" + cached.first)) return info;
	}
	return nullptr;
}

void MagicManager::saveExperienceOwner(INIReader& ini, const std::string& section, const MagicExperienceOwner& owner)
{
	ini.SetBoolean(section, "ExperienceOwnerKnown", owner.assigned);
	std::string key;
	int slot = 0;
	const auto* info = findExperienceOwner(owner, &key, &slot);
	ini.Set(section, "ExperienceOwnerList", key);
	ini.SetInteger(section, "ExperienceOwnerSlot", slot);
	ini.Set(section, "ExperienceOwnerEntryFile", info != nullptr ? info->iniFile : "");
	ini.SetInteger(section, "ExperienceOwnerCharacter", gm->global.data.characterIndex);
}

MagicExperienceOwner MagicManager::loadExperienceOwner(const INIReader& ini, const std::string& section)
{
	MagicExperienceOwner owner;
	owner.assigned = ini.GetBoolean(section, "ExperienceOwnerKnown", false);
	if (!owner.assigned || ini.GetInteger(section, "ExperienceOwnerCharacter", INT_MIN) != gm->global.data.characterIndex)
	{
		return owner;
	}
	const std::string key = ini.Get(section, "ExperienceOwnerList", "");
	const long slot = ini.GetInteger(section, "ExperienceOwnerSlot", 0);
	const std::vector<MagicInfo>* list = nullptr;
	if (key == "primary") list = &primaryMagicList();
	else if (key == "hidden") list = &hiddenMagicList;
	else if (key.compare(0, 12, "replacement:") == 0)
	{
		const std::string replacement = key.substr(12);
		if (isInReplaceMagicList && replacement == currentReplaceMagicListKey) list = &magicList;
		else if (auto it = replaceMagicListCache.find(replacement); it != replaceMagicListCache.end()) list = &it->second;
	}
	if (list != nullptr && slot > 0 && static_cast<size_t>(slot) <= list->size())
	{
		const auto& info = (*list)[static_cast<size_t>(slot - 1)];
		if (magicInfoIsUsable(info) && equalsMagicFileName(info.iniFile, ini.Get(section, "ExperienceOwnerEntryFile", "")))
		{
			owner.magic = info.magic;
		}
	}
	return owner;
}

void MagicManager::addHitExp(std::shared_ptr<Effect> e, int targetLevel)
{
	if (!usesConfiguredExperienceRules || e == nullptr)
	{
		return;
	}
	const int hitExperience = hitExperienceForTargetLevel(targetLevel);
	if (Magic::getExperienceOwner(e->magicDispatchContext).assigned)
	{
		addUseExp(e, hitExperience);
		return;
	}
	const std::string& experienceMagicFile = e->magic.experienceOwnerMagicFile.empty()
		? e->magic.iniName
		: e->magic.experienceOwnerMagicFile;
	if (findMagic(experienceMagicFile) != nullptr)
	{
		addUseExperience(experienceMagicFile, hitExperience);
	}
	else if (auto* info = findExperienceOwner({ true, currentUseMagic }))
	{
		addUseExperience(*info, hitExperience);
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

	addKillExp(
		e,
		scaledExperience,
		practiceKillExperienceFraction,
		currentUseKillExperienceFraction);
}

void MagicManager::addKillExp(
	std::shared_ptr<Effect> e,
	double automaticExperience,
	float practiceFraction,
	float useFraction)
{
	addPracticeExp(floorAutomaticExperience(
		automaticExperience,
		practiceFraction));
	if (auto* info = findExperienceOwner({ true, currentUseMagic }))
	{
		addUseExperience(*info,
			floorAutomaticExperience(
				automaticExperience,
				useFraction));
	}
}

void MagicManager::recordCurrentUseMagic(int listIndex)
{
	if (!isBottomIndex(listIndex) || !magicListExists(listIndex))
	{
		return;
	}
	const auto& selectedMagic = magicList[static_cast<size_t>(listIndex)].magic;
	currentUseMagic = selectedMagic->disableUse == 0 ? selectedMagic : nullptr;
}

void MagicManager::finishMagicUse(const std::shared_ptr<Magic>& sourceMagic, UTime coldTime, bool recordCurrentUse)
{
	if (sourceMagic == nullptr)
	{
		return;
	}
	// Entries can move or be backed up while the cast animation is running.
	// Match the learned object, not a slot or another list's same-named magic.
	const auto update = [&](std::vector<MagicInfo>& list)
	{
		for (auto& info : list)
		{
			if (info.magic == sourceMagic)
			{
				info.remainColdMilliseconds = coldTime;
				if (recordCurrentUse)
				{
					currentUseMagic = sourceMagic->disableUse == 0 ? sourceMagic : nullptr;
				}
			}
		}
	};
	update(magicList);
	update(hiddenMagicList);
	update(replaceMagicListBackup);
	for (auto& cached : replaceMagicListCache)
	{
		update(cached.second);
	}
#if defined(JXQY_ENABLE_AUTOMATION_HOOKS)
	GameplayAutomationSession::skillUsed(sourceMagic);
#endif
}

void MagicManager::addMagicExp(const std::string & magicName, int addexp)
{
	MagicInfo * m = findPrimaryMagic(magicName);
	if (m != nullptr)
	{
		m->exp = addExperienceSaturated(*m, addexp);
		if (tryAdvanceMagicLevel(*m))
		{
			refreshPlayerMagicAttributes();
		}
	}
}

void MagicManager::addMagic(const std::string & magicName)
{
	addPrimaryMagic(magicName, true, true);
}

MagicInfo* MagicManager::addPrimaryMagic(const std::string& magicName, bool showMessage, bool refreshAttributes, bool talent)
{
	if (magicName.empty())
	{
		return nullptr;
	}
	if (talent && gm != nullptr && gm->global.feature.separateTalentSlots && gm->global.magicLayout.talentBegin < 0)
	{
		GameLog::write("MagicManager: unavailable talent slots for %s\n", magicName.c_str());
		return nullptr;
	}
	MagicInfo* existingMagic = nullptr;
	if (talent && gm != nullptr && gm->global.magicLayout.talentBegin >= 0)
	{
		auto& list = primaryMagicList();
		for (int i = gm->global.magicLayout.talentBegin;
			i <= gm->global.magicLayout.talentEnd && i < static_cast<int>(list.size()); ++i)
		{
			if (magicInfoIsUsable(list[i]) && equalsMagicFileName(list[i].iniFile, magicName))
			{
				existingMagic = &list[i];
				break;
			}
		}
	}
	else
	{
		existingMagic = findPrimaryMagic(magicName);
	}
	if (existingMagic != nullptr)
	{
		return existingMagic;
	}

	int index = primaryFreeIndex(talent);
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
	addTalentJumpRadius(info);
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
		const auto* current = findExperienceOwner({ true, currentUseMagic });
		if (current != nullptr && equalsMagicFileName(current->iniFile, magicName))
		{
			currentUseMagic.reset();
		}
		clearMagicInfo(*m);
		refreshPlayerMagicAttributes();
		updateMenu();
	}
}

void MagicManager::clearPrimaryMagicList()
{
	currentUseMagic.reset();
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
		if (currentUseMagic.lock() == info.magic)
		{
			currentUseMagic.reset();
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
	else if (gm != nullptr && gm->global.magicLayout.talentBegin >= 0
		&& movedInfo.lastIndexWhenHide >= gm->global.magicLayout.talentBegin
		&& movedInfo.lastIndexWhenHide <= gm->global.magicLayout.talentEnd)
	{
		targetIndex = primaryFreeIndex(true);
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
			&& (currentUseMagic.lock() == magicList[index1].magic
				|| currentUseMagic.lock() == magicList[index2].magic))
		{
			currentUseMagic.reset();
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
	currentUseMagic.reset();
	loadExperienceRules();
}

namespace
{
bool tryParseMagicExperienceRules(
	const char* data,
	int size,
	int& hitExperienceLevelFactor,
	float& practiceKillExperienceFraction,
	float& currentUseKillExperienceFraction)
{
	if (data == nullptr || size <= 0)
	{
		return false;
	}
	std::unique_ptr<char[]> buffer(new char[static_cast<size_t>(size) + 1]);
	std::memcpy(buffer.get(), data, static_cast<size_t>(size));
	buffer[static_cast<size_t>(size)] = '\0';
	const INIReader ini(buffer);
	if (ini.ParseError() != 0)
	{
		return false;
	}

	const long hitLevelFactor = ini.GetInteger("HitMagicExp", "LevelFactor", -1);
	const float practiceFraction = ini.GetReal("XiuLianMagicExp", "Fraction", -1.0f);
	const float currentUseFraction = ini.GetReal("UseMagicExp", "Fraction", -1.0f);
	if (hitLevelFactor < 0 || hitLevelFactor > INT_MAX
		|| practiceFraction < 0.0f || currentUseFraction < 0.0f)
	{
		return false;
	}

	hitExperienceLevelFactor = static_cast<int>(hitLevelFactor);
	practiceKillExperienceFraction = practiceFraction;
	currentUseKillExperienceFraction = currentUseFraction;
	return true;
}
}

void MagicManager::loadExperienceRules()
{
	hitExperienceLevelFactor = 0;
	practiceKillExperienceFraction = 1.0f;
	currentUseKillExperienceFraction = 1.0f;
	usesConfiguredExperienceRules = false;

	// 沿资源根链（当前包 → 依赖包声明序 → common）取第一个三键齐全的完整
	// 配置；MOD 自带的旧格式 [Exp] 表不构成配置，自动继承基底包的规则。
	int hitLevelFactor = 0;
	float practiceFraction = 1.0f;
	float useFraction = 1.0f;
	const bool configured = File::visitReadableResources(
		{std::string("ini\\level\\MagicExp.ini")},
		[&](const std::string&, std::unique_ptr<char[]>& data, int size)
		{
			return tryParseMagicExperienceRules(
				data.get(),
				size,
				hitLevelFactor,
				practiceFraction,
				useFraction);
		});
	if (configured)
	{
		hitExperienceLevelFactor = hitLevelFactor;
		practiceKillExperienceFraction = practiceFraction;
		currentUseKillExperienceFraction = useFraction;
		usesConfiguredExperienceRules = true;
	}
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
