#include "ResourceManifest.h"
#include "ResourceIniReader.h"

#if !defined(JXQY_RESOURCE_MANIFEST_BUFFER_ONLY)
#include "../File/File.h"
#include "../File/log.h"
#endif

#include <set>
#include <cmath>
#include <climits>
#include <cstdlib>

namespace
{
std::string toLowerAscii(std::string value)
{
	for (char& ch : value)
	{
		if (ch >= 'A' && ch <= 'Z')
		{
			ch = static_cast<char>(ch + ('a' - 'A'));
		}
	}
	return value;
}

std::string trimAscii(std::string value)
{
	while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
		value.front() == '\r' || value.front() == '\n'))
	{
		value.erase(value.begin());
	}
	while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
		value.back() == '\r' || value.back() == '\n'))
	{
		value.pop_back();
	}
	return value;
}

bool hasIniKey(
	const ResourceIniReader& ini,
	const std::string& section,
	const std::string& key)
{
	return ini.hasKey(section, key);
}

bool tryParseFiniteNonNegativeDouble(
	const std::string& text,
	double& value)
{
	if (text.empty())
	{
		return false;
	}
	char* end = nullptr;
	const double parsed = std::strtod(text.c_str(), &end);
	if (end == text.c_str() || *end != '\0' ||
		!std::isfinite(parsed) || parsed < 0.0)
	{
		return false;
	}
	value = parsed;
	return true;
}

std::vector<std::string> splitCommaSeparated(
	const std::string& value)
{
	std::vector<std::string> result;
	std::size_t start = 0;
	while (start <= value.size())
	{
		const std::size_t separator = value.find(',', start);
		result.push_back(
			separator == std::string::npos
				? value.substr(start)
				: value.substr(start, separator - start));
		if (separator == std::string::npos)
		{
			break;
		}
		start = separator + 1;
	}
	return result;
}
}

#if !defined(JXQY_RESOURCE_MANIFEST_BUFFER_ONLY)
bool ResourceManifest::loadFromFile(const std::string& relativePath)
{
	std::unique_ptr<char[]> buffer;
	int len = File::readFile(relativePath, buffer);
	if (buffer == nullptr || len <= 0)
	{
		GameLog::write("ResourceManifest: can not open %s\n", relativePath.c_str());
		return false;
	}
	return loadFromBuffer(buffer.get(), len);
}
#endif

bool ResourceManifest::loadFromBuffer(const char* data, int len)
{
	if (data == nullptr || len <= 0)
	{
		return false;
	}

	const ResourceIniReader ini(
		data,
		static_cast<std::size_t>(len));

	id = ini.get("Game", "Id", id);
	name = ini.get("Game", "Name", name);
	author = trimAscii(ini.get("Game", "Author", author));
	releaseMetadata.displayVersion =
		trimAscii(ini.get("Game", "Version", ""));
	typeDefined = hasIniKey(ini, "Game", "Type");
	if (typeDefined)
	{
		type = static_cast<int>(
			ini.getInteger("Game", "Type", type));
	}
	useWav = ini.getBoolean("Game", "UseWav", useWav);

	defeatedNpcExperienceModeDefined =
		hasIniKey(ini, "Experience", "DefeatedNpcExperienceMode");
	if (defeatedNpcExperienceModeDefined)
	{
		const std::string mode = toLowerAscii(trimAscii(
			ini.get("Experience", "DefeatedNpcExperienceMode", "")));
		if (mode == "storedexperience")
		{
			defeatedNpcExperienceMode =
				DefeatedNpcExperienceMode::StoredExperience;
		}
		else if (mode == "levelproductwithbonus")
		{
			defeatedNpcExperienceMode =
				DefeatedNpcExperienceMode::LevelProductWithBonus;
		}
		else
		{
			defeatedNpcExperienceModeDefined = false;
		}
	}

	experienceMultiplierDefined =
		hasIniKey(ini, "Experience", "ExperienceMultiplier");
	if (experienceMultiplierDefined &&
		!tryParseFiniteNonNegativeDouble(
			ini.get("Experience", "ExperienceMultiplier", ""),
			experienceMultiplier))
	{
		experienceMultiplierDefined = false;
	}

	levelUpThresholdModeDefined =
		hasIniKey(ini, "Experience", "LevelUpThresholdMode");
	if (levelUpThresholdModeDefined)
	{
		const std::string mode = toLowerAscii(trimAscii(
			ini.get("Experience", "LevelUpThresholdMode", "")));
		if (mode == "greaterthanorequal")
		{
			levelUpThresholdMode =
				LevelUpThresholdMode::GreaterThanOrEqual;
		}
		else if (mode == "greaterthan")
		{
			levelUpThresholdMode = LevelUpThresholdMode::GreaterThan;
		}
		else
		{
			levelUpThresholdModeDefined = false;
		}
	}

	partnerFollowRadiusDefined =
		hasIniKey(ini, "Gameplay", "PartnerFollowRadius");
	if (partnerFollowRadiusDefined)
	{
		const long configuredRadius = ini.getInteger(
			"Gameplay", "PartnerFollowRadius", partnerFollowRadius);
		if (configuredRadius < 0 || configuredRadius > INT_MAX)
		{
			partnerFollowRadiusDefined = false;
		}
		else
		{
			partnerFollowRadius = static_cast<int>(configuredRadius);
		}
	}
	partnerFollowRunRadiusDefined =
		hasIniKey(ini, "Gameplay", "PartnerFollowRunRadius");
	if (partnerFollowRunRadiusDefined)
	{
		const long configuredRadius = ini.getInteger(
			"Gameplay", "PartnerFollowRunRadius", partnerFollowRunRadius);
		if (configuredRadius < 0 || configuredRadius > INT_MAX)
		{
			partnerFollowRunRadiusDefined = false;
		}
		else
		{
			partnerFollowRunRadius = static_cast<int>(configuredRadius);
		}
	}

	minimumMagicDamage = 10;
	minimumMagicDamageDefined =
		hasIniKey(ini, "Combat", "MinimumMagicDamage");
	if (minimumMagicDamageDefined)
	{
		const long configuredMinimum = ini.getInteger(
			"Combat", "MinimumMagicDamage", minimumMagicDamage);
		if (configuredMinimum < 0 || configuredMinimum > INT_MAX)
		{
			minimumMagicDamageDefined = false;
		}
		else
		{
			minimumMagicDamage = static_cast<int>(configuredMinimum);
		}
	}

	magicEffectCalculationModeDefined =
		hasIniKey(ini, "Combat", "MagicEffectCalculationMode");
	if (magicEffectCalculationModeDefined)
	{
		const std::string mode = toLowerAscii(trimAscii(
			ini.get("Combat", "MagicEffectCalculationMode", "")));
		if (mode == "replaceattack")
		{
			magicEffectCalculationMode =
				MagicEffectCalculationMode::ReplaceAttack;
		}
		else if (mode == "addtoattack")
		{
			magicEffectCalculationMode =
				MagicEffectCalculationMode::AddToAttack;
		}
		else
		{
			magicEffectCalculationModeDefined = false;
		}
	}

	npcActionProfileDefined =
		hasIniKey(ini, "Script", "NpcActionProfile");
	if (npcActionProfileDefined)
	{
		const std::string profile = toLowerAscii(trimAscii(
			ini.get("Script", "NpcActionProfile", "")));
		if (profile == "legacy")
		{
			npcActionProfile = ScriptNpcActionProfile::Legacy;
		}
		else if (profile == "yycs")
		{
			npcActionProfile = ScriptNpcActionProfile::Yycs;
		}
		else if (profile == "xjxqy")
		{
			npcActionProfile = ScriptNpcActionProfile::Xjxqy;
		}
		else
		{
			npcActionProfileDefined = false;
		}
	}

	npcRuntimeProfileDefined =
		hasIniKey(ini, "Script", "NpcRuntimeProfile");
	if (npcRuntimeProfileDefined)
	{
		const std::string profile = toLowerAscii(trimAscii(
			ini.get("Script", "NpcRuntimeProfile", "")));
		if (profile == "legacy")
		{
			npcRuntimeProfile = ScriptNpcRuntimeProfile::Legacy;
		}
		else if (profile == "trilogy")
		{
			npcRuntimeProfile = ScriptNpcRuntimeProfile::Trilogy;
		}
		else
		{
			npcRuntimeProfileDefined = false;
		}
	}

	specialActionModeDefined =
		hasIniKey(ini, "Script", "SpecialActionMode");
	if (specialActionModeDefined)
	{
		const std::string mode = toLowerAscii(trimAscii(
			ini.get("Script", "SpecialActionMode", "")));
		if (mode == "replace")
		{
			specialActionMode = ScriptSpecialActionMode::Replace;
		}
		else if (mode == "overlay")
		{
			specialActionMode = ScriptSpecialActionMode::Overlay;
		}
		else
		{
			specialActionModeDefined = false;
		}
	}

	addLifeModeDefined = hasIniKey(ini, "Script", "AddLifeMode");
	if (addLifeModeDefined)
	{
		const std::string mode = toLowerAscii(trimAscii(
			ini.get("Script", "AddLifeMode", "")));
		if (mode == "playerrules")
		{
			addLifeMode = ScriptAddLifeMode::PlayerRules;
		}
		else if (mode == "directclamp")
		{
			addLifeMode = ScriptAddLifeMode::DirectClamp;
		}
		else
		{
			addLifeModeDefined = false;
		}
	}

	levelUpMessage = ini.get(
		"LevelUp", "Message", levelUpMessage);
	levelUpRandomEffects.clear();
	for (std::string effect : splitCommaSeparated(
		ini.get("LevelUp", "RandomEffects", "")))
	{
		effect = trimAscii(effect);
		if (!effect.empty())
		{
			levelUpRandomEffects.push_back(effect);
		}
	}
	levelUpMaleEffect = trimAscii(ini.get(
		"LevelUp", "MaleEffect", ""));
	levelUpFemaleEffect = trimAscii(ini.get(
		"LevelUp", "FemaleEffect", ""));

	dependencyId =
		ini.get("Resource", "DependencyId", dependencyId);
	resourceOnly = ini.getBoolean(
		"Resource", "ResourceOnly", false);
	textEncodingConverted = ini.getBoolean(
		"Resource", "TextEncodingConverted", false);

	uiBaseId = ini.get("UI", "BaseId", uiBaseId);
	uiProfile = ini.get("UI", "Profile", uiProfile);
	preferLocalUi =
		ini.getBoolean("UI", "PreferLocal", preferLocalUi);

	features.clear();
	for (const auto& featureName :
		ini.sectionKeys("Features"))
	{
		features[toLowerAscii(featureName)] =
			ini.getBoolean(
				"Features",
				featureName,
				false);
	}

	saveNamespace =
		ini.get("Save", "Namespace", saveNamespace);

	releaseMetadata.releaseDate =
		trimAscii(ini.get("Release", "Date", ""));
	releaseMetadata.minimumEngineVersion =
		trimAscii(
			ini.get(
				"Release",
				"MinimumEngineVersion",
				""));
	releaseMetadata.coverPath =
		trimAscii(ini.get("Release", "Cover", ""));
	releaseMetadata.descriptionFilePath =
		trimAscii(
			ini.get(
				"Release",
				"DescriptionFile",
				""));

	std::string videos =
		ini.get("Startup", "Videos", "");
	startupVideos.clear();
	if (!videos.empty())
	{
		auto parts = splitCommaSeparated(videos);
		for (auto& part : parts)
		{
			// 去除首尾空白
			while (!part.empty() && (part.front() == ' ' || part.front() == '\t'))
			{
				part.erase(part.begin());
			}
			while (!part.empty() && (part.back() == ' ' || part.back() == '\t' || part.back() == '\r' || part.back() == '\n'))
			{
				part.pop_back();
			}
			if (!part.empty())
			{
				startupVideos.push_back(part);
			}
		}
	}

	titleMenu = ini.get("Title", "Menu", titleMenu);
	titleNewYearMenu =
		ini.get(
			"Title",
			"NewYearMenu",
			titleNewYearMenu);
	titleMusic = ini.get("Title", "Music", titleMusic);
	titleTeamVideo =
		ini.get(
			"Title",
			"TeamVideo",
			titleTeamVideo);
	teamInfoFile =
		ini.get("Team", "InfoFile", teamInfoFile);

	newGameScript =
		ini.get("NewGame", "Script", newGameScript);

	return true;
}

ResourceManifest ResourceManifest::createDefault(const std::string& root)
{
	ResourceManifest manifest;
	manifest.resourceRoot = root;
	manifest.id = "JXQY2";
	manifest.name = "Sword Heroes' Fate";
	manifest.author = "";
	manifest.releaseMetadata = {};
	manifest.type = 0;
	manifest.typeDefined = true;
	manifest.useWav = false;
	manifest.defeatedNpcExperienceMode =
		DefeatedNpcExperienceMode::StoredExperience;
	manifest.defeatedNpcExperienceModeDefined = false;
	manifest.experienceMultiplier = 3.0;
	manifest.experienceMultiplierDefined = false;
	manifest.levelUpThresholdMode =
		LevelUpThresholdMode::GreaterThanOrEqual;
	manifest.levelUpThresholdModeDefined = false;
	manifest.partnerFollowRadius = 1;
	manifest.partnerFollowRadiusDefined = false;
	manifest.partnerFollowRunRadius = 5;
	manifest.partnerFollowRunRadiusDefined = false;
	manifest.minimumMagicDamage = 10;
	manifest.minimumMagicDamageDefined = false;
	manifest.magicEffectCalculationMode =
		MagicEffectCalculationMode::ReplaceAttack;
	manifest.magicEffectCalculationModeDefined = false;
	manifest.npcActionProfile = ScriptNpcActionProfile::Legacy;
	manifest.npcActionProfileDefined = false;
	manifest.npcRuntimeProfile = ScriptNpcRuntimeProfile::Legacy;
	manifest.npcRuntimeProfileDefined = false;
	manifest.specialActionMode = ScriptSpecialActionMode::Replace;
	manifest.specialActionModeDefined = false;
	manifest.addLifeMode = ScriptAddLifeMode::PlayerRules;
	manifest.addLifeModeDefined = false;
	manifest.levelUpMessage = "{name}的等级得到提升！";
	manifest.levelUpRandomEffects.clear();
	manifest.levelUpMaleEffect = "";
	manifest.levelUpFemaleEffect = "";
	manifest.dependencyId = "";
	manifest.resourceOnly = false;
	manifest.textEncodingConverted = false;
	manifest.uiBaseId = "";
	manifest.uiProfile = "";
	manifest.preferLocalUi = true;
	manifest.features.clear();
	manifest.saveNamespace = "";
	manifest.startupVideos = { "logo.avi", "title.avi" };
	manifest.titleMenu = "ini\\ui\\title\\title.menu.ini";
	manifest.titleMusic = "ks64.mp3";
	manifest.titleTeamVideo = "team.avi";
	manifest.teamInfoFile = "";
	manifest.newGameScript = "newgame.txt";
	return manifest;
}

bool ResourceManifest::isValid() const
{
	return !trimAscii(id).empty();
}

bool ResourceManifest::isBaseGame() const
{
	return !resourceOnly && getDependencyIds().empty() &&
		typeDefined && type >= 0 && type <= 2;
}

bool ResourceManifest::isFeatureEnabled(const std::string& featureName, bool defaultValue) const
{
	auto feature = features.find(toLowerAscii(featureName));
	return feature != features.end() ? feature->second : defaultValue;
}

std::vector<std::string> ResourceManifest::getDependencyIds() const
{
	std::vector<std::string> result;
	std::set<std::string> seenIds;
	for (std::string dependency :
		splitCommaSeparated(dependencyId))
	{
		dependency = trimAscii(dependency);
		if (dependency.empty())
		{
			continue;
		}
		if (seenIds.insert(toLowerAscii(dependency)).second)
		{
			result.push_back(dependency);
		}
	}
	return result;
}

DefeatedNpcExperienceMode
ResourceManifest::resolvedDefeatedNpcExperienceMode() const
{
	if (defeatedNpcExperienceModeDefined)
	{
		return defeatedNpcExperienceMode;
	}
	return type == 0
		? DefeatedNpcExperienceMode::StoredExperience
		: DefeatedNpcExperienceMode::LevelProductWithBonus;
}

double ResourceManifest::resolvedExperienceMultiplier() const
{
	if (experienceMultiplierDefined)
	{
		return experienceMultiplier;
	}
	return type >= 0 && type <= 2 ? 3.0 : 1.0;
}

LevelUpThresholdMode ResourceManifest::resolvedLevelUpThresholdMode() const
{
	if (levelUpThresholdModeDefined)
	{
		return levelUpThresholdMode;
	}
	return type == 1 || type == 2
		? LevelUpThresholdMode::GreaterThan
		: LevelUpThresholdMode::GreaterThanOrEqual;
}

int ResourceManifest::resolvedPartnerFollowRadius() const
{
	if (partnerFollowRadiusDefined)
	{
		return partnerFollowRadius;
	}
	return type == 1 || type == 2 ? 2 : 1;
}

int ResourceManifest::resolvedPartnerFollowRunRadius() const
{
	return partnerFollowRunRadiusDefined ? partnerFollowRunRadius : 5;
}

int ResourceManifest::resolvedMinimumMagicDamage() const
{
	return minimumMagicDamageDefined ? minimumMagicDamage : 10;
}

MagicEffectCalculationMode ResourceManifest::resolvedMagicEffectCalculationMode() const
{
	return magicEffectCalculationModeDefined
		? magicEffectCalculationMode
		: MagicEffectCalculationMode::ReplaceAttack;
}

ScriptNpcActionProfile ResourceManifest::resolvedNpcActionProfile() const
{
	if (npcActionProfileDefined)
	{
		return npcActionProfile;
	}
	if (type == 1)
	{
		return ScriptNpcActionProfile::Yycs;
	}
	if (type == 2)
	{
		return ScriptNpcActionProfile::Xjxqy;
	}
	return ScriptNpcActionProfile::Legacy;
}

ScriptNpcRuntimeProfile ResourceManifest::resolvedNpcRuntimeProfile() const
{
	if (npcRuntimeProfileDefined)
	{
		return npcRuntimeProfile;
	}
	return type == 1 || type == 2
		? ScriptNpcRuntimeProfile::Trilogy
		: ScriptNpcRuntimeProfile::Legacy;
}

ScriptSpecialActionMode ResourceManifest::resolvedSpecialActionMode() const
{
	if (specialActionModeDefined)
	{
		return specialActionMode;
	}
	return type == 1 || type == 2
		? ScriptSpecialActionMode::Overlay
		: ScriptSpecialActionMode::Replace;
}

ScriptAddLifeMode ResourceManifest::resolvedAddLifeMode() const
{
	if (addLifeModeDefined)
	{
		return addLifeMode;
	}
	return type == 1 || type == 2
		? ScriptAddLifeMode::DirectClamp
		: ScriptAddLifeMode::PlayerRules;
}
