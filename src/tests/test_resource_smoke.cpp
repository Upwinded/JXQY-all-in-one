#include "../File/File.h"
#include "../File/INIReader.h"
#include "../Game/Data/MediaPathResolver.h"
#include "../Game/GameTypes.h"
#include "../Game/Data/GoodsManager.h"
#include "../Game/Data/Magic.h"
#include "../Image/ImagePackagePathCandidates.h"
#include "../Image/SafeImageDecoder.h"
#include "../Resource/ModReleaseAssets.h"
#include "../Resource/ResourceManager.h"
#include "TestTemporaryDirectory.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
constexpr const char* DefaultTitleMenu = "ini\\ui\\title\\title.menu.ini";
constexpr const char* DefaultNewGameScript = "newgame.txt";

bool check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		return false;
	}
	return true;
}

class ScopedResourceSmokeStateIsolation final
{
public:
	ScopedResourceSmokeStateIsolation() :
		root(makeUniqueTestDirectory(
			"jxqy_resource_smoke_state_isolation_test"))
	{
		std::error_code errorCode;
		std::filesystem::remove_all(root, errorCode);
		errorCode.clear();
		std::filesystem::create_directories(root, errorCode);
		ready = !errorCode;
		if (ready)
		{
			File::setPlatformStateParentForTests(
				root.generic_string());
		}
	}

	~ScopedResourceSmokeStateIsolation()
	{
		File::setPlatformStateParentForTests("");
		std::error_code errorCode;
		std::filesystem::remove_all(root, errorCode);
	}

	bool valid() const
	{
		return ready;
	}

private:
	std::filesystem::path root;
	bool ready = false;
};

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

std::string normalizeSlash(std::string value)
{
	for (char& ch : value)
	{
		if (ch == '\\')
		{
			ch = '/';
		}
	}
	return value;
}

std::string normalizeRoot(std::string value)
{
	value = normalizeSlash(value);
	if (!value.empty() && value.back() != '/')
	{
		value += '/';
	}
	return value;
}

std::string stripStartupVideoCondition(std::string value)
{
	const std::string newYearPrefix = "newyear:";
	if (value.rfind(newYearPrefix, 0) == 0)
	{
		return value.substr(newYearPrefix.size());
	}
	return value;
}

std::vector<std::string> newGameScriptCandidates(const std::string& scriptName)
{
	std::string name = scriptName.empty() ? DefaultNewGameScript : scriptName;
	return {
		"script\\goods\\" + name,
		"script\\common\\" + name,
	};
}

std::string joinRuntimePath(const std::string& directoryName, const std::string& fileName)
{
	if (fileName.empty())
	{
		return "";
	}
	std::string result = directoryName;
	if (!result.empty() && result.back() != '\\' && result.back() != '/')
	{
		result += '\\';
	}
	result += fileName;
	return result;
}

bool resolveRuntimeFile(
	const std::vector<std::string>& candidates,
	std::string& matchedCandidate,
	std::string& resolvedPath)
{
	for (const auto& candidate : candidates)
	{
		if (candidate.empty())
		{
			continue;
		}
		if (File::fileExist(candidate))
		{
			matchedCandidate = candidate;
			resolvedPath = File::getAssetsName(candidate);
			return true;
		}
	}
	return false;
}

bool checkRuntimeFile(
	const std::string& packId,
	const std::string& label,
	const std::vector<std::string>& candidates)
{
	std::string matchedCandidate;
	std::string resolvedPath;
	if (resolveRuntimeFile(candidates, matchedCandidate, resolvedPath))
	{
		std::cout << "OK: " << packId << ": " << label << " -> "
			<< matchedCandidate << " [" << resolvedPath << "]" << std::endl;
		return true;
	}

	std::cerr << "FAIL: " << packId << ": missing runtime resource for "
		<< label << ":";
	for (const auto& candidate : candidates)
	{
		std::cerr << " " << candidate;
	}
	std::cerr << std::endl;
	return false;
}

bool checkActivePackFile(
	const std::string& packId,
	const std::string& label,
	const std::string& fileName)
{
	if (File::activeResourceFileExist(fileName))
	{
		std::cout << "OK: " << packId << ": " << label << " -> "
			<< fileName << " [active package]" << std::endl;
		return true;
	}
	std::cerr << "FAIL: " << packId << ": missing active-package resource for "
		<< label << ": " << fileName << std::endl;
	return false;
}

bool isBaseGamePack(const ResourceManager::ResourcePack& pack)
{
	return pack.manifest.getDependencyIds().empty() && pack.manifest.type >= 0 && pack.manifest.type <= 2;
}

std::string packIdForLog(const ResourceManager::ResourcePack& pack)
{
	if (!pack.manifest.id.empty())
	{
		return pack.manifest.id;
	}
	return pack.rootPath;
}

bool checkPackProfileFields(const ResourceManager::ResourcePack& pack)
{
	bool ok = true;
	const std::string packId = packIdForLog(pack);
	const std::filesystem::path packRoot =
		std::filesystem::u8path(pack.rootPath);
	ok = check(packRoot.is_absolute(),
		packId + " exposes an absolute resource-pack root") && ok;
	ok = check(!pack.manifest.id.empty(), packId + " has effective Game.Id") && ok;
	ok = check(!pack.manifest.name.empty(), packId + " has effective Game.Name") && ok;
	ok = check(pack.manifest.textEncodingConverted,
		packId + " declares Resource.TextEncodingConverted=1") && ok;
	ok = check(!pack.manifest.saveNamespace.empty(), packId + " has Save.Namespace") && ok;
	ok = check(!pack.manifest.titleMenu.empty(), packId + " has Title.Menu") && ok;
	ok = check(!pack.manifest.newGameScript.empty(), packId + " has NewGame.Script") && ok;
	if (!pack.manifest.releaseMetadata.descriptionFilePath.empty())
	{
		const ModRelease::DescriptionReadResult description =
			ModRelease::readDescriptionFromPack(
				packRoot, pack.manifest.releaseMetadata);
		ok = check(description.succeeded() &&
				!description.utf8Text.empty(),
			packId + " reads its declared release description from the pack root"
				+ " (status=" + std::to_string(
					static_cast<int>(description.status)) + ")")
			&& ok;
	}
	if (!pack.manifest.releaseMetadata.coverPath.empty())
	{
		const ModRelease::CoverReadResult cover =
			ModRelease::readCoverFromPack(
				packRoot, pack.manifest.releaseMetadata);
		ok = check(cover.readyForDecode() &&
				!cover.encodedBytes.empty() &&
				cover.dimensions.width > 0 &&
				cover.dimensions.height > 0,
			packId + " reads its declared release cover from the pack root"
				+ " (status=" + std::to_string(
					static_cast<int>(cover.status)) + ")")
			&& ok;
	}
	if (!isBaseGamePack(pack))
	{
		ok = check(!pack.manifest.dependencyId.empty(), packId + " has Resource.DependencyId/Base") && ok;
	}
	return ok;
}

bool smokeSelectedPack(
	ResourceManager& manager,
	const ResourceManager::ResourcePack& pack,
	int index,
	std::map<std::string, std::vector<std::string>>& saveNamespaces)
{
	manager.setActiveResourcePack(index);
	const std::string packId = packIdForLog(pack);
	bool ok = true;

	ok = check(normalizeRoot(File::getActiveResourceRoot()) == normalizeRoot(pack.rootPath),
		packId + " applies active resource root") && ok;

	std::string activeSaveNamespace = File::getActiveSaveNamespace();
	ok = check(!activeSaveNamespace.empty(), packId + " applies a save namespace") && ok;
	if (!pack.manifest.saveNamespace.empty())
	{
		ok = check(activeSaveNamespace == pack.manifest.saveNamespace,
			packId + " applies manifest Save.Namespace") && ok;
	}
	saveNamespaces[toLowerAscii(activeSaveNamespace)].push_back(packId);

	std::string titleMenu = pack.manifest.titleMenu.empty() ? DefaultTitleMenu : pack.manifest.titleMenu;
	ok = checkRuntimeFile(packId, "Title.Menu", { titleMenu }) && ok;
	if (toLowerAscii(packId) == "xiaoxiangxing_1_022")
	{
		const auto dependencyIds = pack.manifest.getDependencyIds();
		ok = check(dependencyIds.size() == 2 && dependencyIds[0] == "JXQY2" &&
			dependencyIds[1] == "YYCS",
			"XIAOXIANGXING_1_022 uses JXQY2 first and YYCS as its secondary content base") && ok;
		ok = check(pack.manifest.uiBaseId == "YYCS" && pack.manifest.uiProfile == "YYCS",
			"XIAOXIANGXING_1_022 uses the independent YYCS UI base and layout profile") && ok;
		const std::string resolvedTitleMenu = toLowerAscii(normalizeSlash(File::getAssetsName(titleMenu)));
		ok = check(resolvedTitleMenu.find("/yycs/") != std::string::npos,
			"XIAOXIANGXING_1_022 missing title menu falls back to YYCS instead of JXQY2") && ok;
		const std::string secondaryContentResource = "asf/object/mpc142_l.asf";
		const std::string resolvedSecondaryContent = toLowerAscii(normalizeSlash(
			File::getAssetsName(secondaryContentResource)));
		ok = check(File::fileExist(secondaryContentResource) &&
			resolvedSecondaryContent.find("/yycs/") != std::string::npos,
			"XIAOXIANGXING_1_022 resolves missing ordinary content from its secondary YYCS parent") && ok;

		std::unique_ptr<char[]> newGameContent;
		const int newGameLength = File::readFile("script\\common\\newgame.txt", newGameContent);
		const std::string newGameText = newGameLength > 0 && newGameContent != nullptr
			? std::string(newGameContent.get(), static_cast<std::size_t>(newGameLength))
			: std::string();
		ok = check(newGameText.find("playmovie(\"begin.avi\")") != std::string::npos &&
			newGameText.find("playmovie(\"open.avi\")") == std::string::npos,
			"XIAOXIANGXING_1_022 requests the JXQY2 opening movie instead of the YYCS movie") && ok;
		const std::string resolvedOpeningMovie = toLowerAscii(normalizeSlash(
			File::getAssetsName("video\\begin.avi")));
		ok = check(File::fileExist("video\\begin.avi") &&
			resolvedOpeningMovie.find("/jxqy2/") != std::string::npos,
			"XIAOXIANGXING_1_022 resolves its opening movie from the primary JXQY2 parent") && ok;
	}
	if (!pack.manifest.titleNewYearMenu.empty())
	{
		ok = checkRuntimeFile(packId, "Title.NewYearMenu", { pack.manifest.titleNewYearMenu }) && ok;
	}
	ok = checkRuntimeFile(packId, "NewGame.Script", newGameScriptCandidates(pack.manifest.newGameScript)) && ok;

	std::vector<std::string> levelUpEffects =
		pack.manifest.levelUpRandomEffects;
	if (!pack.manifest.levelUpMaleEffect.empty())
	{
		levelUpEffects.push_back(pack.manifest.levelUpMaleEffect);
	}
	if (!pack.manifest.levelUpFemaleEffect.empty())
	{
		levelUpEffects.push_back(pack.manifest.levelUpFemaleEffect);
	}
	for (const std::string& levelUpEffect : levelUpEffects)
	{
		ok = checkRuntimeFile(
			packId,
			"LevelUp.Effect",
			{ joinRuntimePath("ini/magic", levelUpEffect) }) && ok;
	}

	if (!pack.manifest.titleMusic.empty())
	{
		ok = checkRuntimeFile(
			packId,
			"Title.Music",
			buildMediaAssetCandidates(
				MUSIC_FOLDER,
				pack.manifest.titleMusic,
				{ ".mp3", ".ogg", ".wma", ".wav" })) && ok;
	}
	if (!pack.manifest.titleTeamVideo.empty())
	{
		ok = checkActivePackFile(packId, "Title.TeamVideo",
			joinRuntimePath("video", pack.manifest.titleTeamVideo)) && ok;
	}
	if (!pack.manifest.teamInfoFile.empty())
	{
		ok = checkActivePackFile(packId, "Team.InfoFile",
			pack.manifest.teamInfoFile) && ok;
	}
	for (const auto& startupVideo : pack.manifest.startupVideos)
	{
		std::string videoName = stripStartupVideoCondition(startupVideo);
		if (videoName.empty())
		{
			continue;
		}
		ok = checkRuntimeFile(packId, "Startup.Video", {
			joinRuntimePath("video", videoName),
		}) && ok;
	}

	return ok;
}

bool runResourceSmoke(const std::filesystem::path& collectionRoot)
{
	ResourceManager& manager = ResourceManager::instance();
	bool ok = check(manager.initialize(collectionRoot.string()), "ResourceManager initializes " + collectionRoot.string());
	const auto& packs = manager.getDiscoveredPacks();
	ok = check(!packs.empty(), "ResourceManager discovers at least one resource pack") && ok;

	std::set<std::string> packIds;
	for (const auto& pack : packs)
	{
		const std::string packId = packIdForLog(pack);
		ok = check(packIds.insert(toLowerAscii(packId)).second,
			packId + " has a unique effective resource id") && ok;
		ok = checkPackProfileFields(pack) && ok;
	}

	std::map<std::string, std::vector<std::string>> saveNamespaces;
	for (int i = 0; i < (int)packs.size(); i++)
	{
		ok = smokeSelectedPack(manager, packs[i], i, saveNamespaces) && ok;
	}

	for (const auto& item : saveNamespaces)
	{
		if (item.first.empty())
		{
			continue;
		}
		if (item.second.size() > 1)
		{
			std::string message = "Save.Namespace is shared by:";
			for (const auto& packId : item.second)
			{
				message += " " + packId;
			}
			ok = check(false, message) && ok;
		}
	}

	return ok;
}

bool runAuthorAttributionSmoke(const ResourceManager& manager)
{
	struct ExpectedAuthor
	{
		const char* packId;
		const char* author;
	};

	const ExpectedAuthor expectedAuthors[] = {
		{ "JXQY2", u8"原版" },
		{ "YYCS", u8"原版" },
		{ "XJXQY", u8"原版" },
		{ "XINYUE_WUHEN_3_0", u8"teaqinpeng（小茶）" },
		{ "JIANGHU_YUCHEN_1_03", u8"teaqinpeng（小茶）" },
		{ "JIAN_ER_GAI_CHENGHE_1_041", u8"金大宝、no宇世无双、asd_zxcqwe、teaqinpeng" },
		{ "YUEMEIER_WAIZHUAN_1_053", u8"teaqinpeng（小茶）" },
		{ "JIANGHU_YUCHEN_2", u8"teaqinpeng（小茶）" },
		{ "XIAOXIANGXING_1_022", u8"金大宝、哀2005、asd_zxcqwe、teaqinpeng" },
		{ "XJXQY_TEST_MOD", "" },
		{ "XJXQY_TEST_FALLBACK", "" },
	};

	bool ok = true;
	const auto& packs = manager.getDiscoveredPacks();
	for (const auto& expected : expectedAuthors)
	{
		auto packIt = std::find_if(packs.begin(), packs.end(), [&expected](const ResourceManager::ResourcePack& pack)
			{
				return pack.manifest.id == expected.packId;
			});
		ok = check(packIt != packs.end(), std::string(expected.packId) + " author smoke pack is available") && ok;
		if (packIt == packs.end())
		{
			continue;
		}
		ok = check(packIt->manifest.author == expected.author,
			std::string(expected.packId) + " uses the expected Game.Author attribution") && ok;
	}
	return ok;
}

bool runChoosePanelResourceSmoke(ResourceManager& manager)
{
	struct ExpectedPanel
	{
		const char* packId;
		const char* matchedCandidate;
		int width;
		int height;
		const char* labelColor;
		int labelLeft;
		int labelTop;
		int labelWidth;
		int fontSize;
		int buttonBTop;
		int buttonHeight;
		int alignX;
		int alignY;
		int dialogHeight;
		int dialogAlignY;
	};

	const ExpectedPanel expectedPanels[] = {
		{ "JXQY2", "mpc/ui/dialog/panel.mpc", 440, 120, "40,32,24", 36, 18, 384, 17, 82, 24, 0, -96, 90, -96 },
		{ "XINYUE_WUHEN_3_0", "mpc/ui/dialog/panel.mpc", 440, 120, "40,32,24", 36, 18, 384, 17, 82, 24, 0, -96, 90, -96 },
		{ "JIAN_ER_GAI_CHENGHE_1_041", "mpc/ui/dialog/panel.mpc", 440, 120, "40,32,24", 36, 18, 384, 17, 82, 24, 0, -96, 90, -96 },
		{ "YYCS", "asf/ui/dialog/panel.asf", 438, 123, "20,20,20", 65, 30, 310, 18, 74, 22, 0, -85, 123, -85 },
		{ "YUEMEIER_WAIZHUAN_1_053", "asf/ui/dialog/panel.asf", 438, 123, "20,20,20", 65, 30, 310, 18, 74, 22, 0, -85, 123, -85 },
		{ "JIANGHU_YUCHEN_1_03", "asf/ui/dialog/panel.asf", 438, 123, "20,20,20", 65, 30, 310, 18, 74, 22, 0, -85, 123, -85 },
		{ "JIANGHU_YUCHEN_2", "asf/ui/dialog/panel.asf", 438, 123, "20,20,20", 65, 30, 310, 18, 74, 22, 0, -85, 123, -85 },
		{ "XIAOXIANGXING_1_022", "asf/ui/dialog/panel.asf", 438, 123, "20,20,20", 65, 30, 310, 18, 74, 22, 0, -85, 123, -85 },
	};

	bool ok = true;
	for (const auto& expected : expectedPanels)
	{
		if (!manager.setActiveResourcePackById(expected.packId))
		{
			ok = check(false, std::string(expected.packId) + " choose panel smoke pack is available") && ok;
			continue;
		}

		std::unique_ptr<char[]> content;
		int length = 0;
		const std::string windowIni = "ini\\ui\\choose\\window.ini";
		if (!File::readFile(windowIni, content, length) || content == nullptr || length <= 0)
		{
			ok = check(false, std::string(expected.packId) + " loads " + windowIni) && ok;
			continue;
		}

		INIReader ini(content);
		const std::string image = ini.Get("Init", "Image", "");
		ok = check(!image.empty(), std::string(expected.packId) + " choose window declares Init.Image") && ok;
		ok = check(ini.GetInteger("Init", "Width", 0) == expected.width &&
			ini.GetInteger("Init", "Height", 0) == expected.height,
			std::string(expected.packId) + " choose window uses the expected panel geometry") && ok;
		ok = check(toLowerAscii(ini.Get("Init", "Align", "")) == "albottomcenter" &&
			ini.GetInteger("Init", "AlignX", 0) == expected.alignX &&
			ini.GetInteger("Init", "AlignY", 0) == expected.alignY,
			std::string(expected.packId) + " choose window uses the expected panel anchor") && ok;
		std::unique_ptr<char[]> dialogContent;
		int dialogLength = 0;
		const std::string dialogWindowIni = "ini\\ui\\dialog\\window.ini";
		if (!File::readFile(dialogWindowIni, dialogContent, dialogLength) ||
			dialogContent == nullptr || dialogLength <= 0)
		{
			ok = check(false, std::string(expected.packId) + " loads " + dialogWindowIni) && ok;
		}
		else
		{
			INIReader dialogIni(dialogContent);
			ok = check(
				dialogIni.GetInteger("Init", "Width", 0) == expected.width &&
				dialogIni.GetInteger("Init", "Height", 0) == expected.dialogHeight &&
				toLowerAscii(dialogIni.Get("Init", "Align", "")) == "albottomcenter" &&
				dialogIni.GetInteger("Init", "AlignY", 0) == expected.dialogAlignY,
				std::string(expected.packId) + " ordinary dialog uses the expected base geometry") && ok;
		}

		std::string matchedCandidate;
		std::string resolvedPath;
		const auto candidates = ImagePackagePathCandidates::build(image);
		const bool resolved = resolveRuntimeFile(candidates, matchedCandidate, resolvedPath);
		ok = check(resolved, std::string(expected.packId) + " resolves choose panel image") && ok;
		if (resolved)
		{
			std::cout << "OK: " << expected.packId << ": ChooseUI.Image -> "
				<< matchedCandidate << " [" << resolvedPath << "]" << std::endl;
			ok = check(normalizeSlash(matchedCandidate) == expected.matchedCandidate,
				std::string(expected.packId) + " uses the expected choose panel candidate") && ok;
		}

		content.reset();
		length = 0;
		const std::string labelIni = "ini\\ui\\choose\\label.ini";
		if (!File::readFile(labelIni, content, length) || content == nullptr || length <= 0)
		{
			ok = check(false, std::string(expected.packId) + " loads " + labelIni) && ok;
			continue;
		}
		INIReader label(content);
		ok = check(label.Get("Init", "Color", "") == expected.labelColor,
			std::string(expected.packId) + " choose title uses the expected readable color") && ok;
		ok = check(label.GetInteger("Init", "Left", -1) == expected.labelLeft &&
			label.GetInteger("Init", "Top", -1) == expected.labelTop &&
			label.GetInteger("Init", "Width", 0) == expected.labelWidth &&
			label.GetInteger("Init", "Font", 0) == expected.fontSize,
			std::string(expected.packId) + " choose title spans the usable panel width") && ok;

		content.reset();
		length = 0;
		const std::string buttonBIni = "ini\\ui\\choose\\btnB.ini";
		if (!File::readFile(buttonBIni, content, length) || content == nullptr || length <= 0)
		{
			ok = check(false, std::string(expected.packId) + " loads " + buttonBIni) && ok;
			continue;
		}
		INIReader buttonB(content);
		ok = check(buttonB.GetInteger("Init", "Top", -1) == expected.buttonBTop &&
			buttonB.GetInteger("Init", "Height", 0) == expected.buttonHeight &&
			buttonB.GetInteger("Init", "Font", 0) == expected.fontSize,
			std::string(expected.packId) + " choose bottom option uses the expected inset, height, and font") && ok;
	}
	return ok;
}

bool runMobileAndMapLayoutResourceSmoke(ResourceManager& manager)
{
	bool ok = true;
	if (!manager.setActiveResourcePackById("JXQY2"))
	{
		return check(false, "JXQY2 mobile and map layout smoke pack is available");
	}

	struct ExpectedGeometry
	{
		const char* path;
		int left;
		int top;
		int width;
		int height;
	};

	const ExpectedGeometry mobileButtons[] = {
		{ "ini\\ui\\mobile\\skills\\fastbtn1.ini", -140, 114, 260, 55 },
		{ "ini\\ui\\mobile\\skills\\fastbtn2.ini", -140, 57, 260, 55 },
		{ "ini\\ui\\mobile\\skills\\fastbtn3.ini", -140, 0, 260, 55 },
		{ "ini\\ui\\mobile\\skills\\fastbtn4.ini", -140, -57, 260, 55 },
	};
	for (const auto& expected : mobileButtons)
	{
		std::unique_ptr<char[]> content;
		int length = 0;
		if (!File::readFile(expected.path, content, length) ||
			content == nullptr || length <= 0)
		{
			ok = check(false, std::string("JXQY2 loads ") + expected.path) && ok;
			continue;
		}
		INIReader ini(content);
		ok = check(ini.GetInteger("Init", "Left", 0) == expected.left &&
			ini.GetInteger("Init", "Top", 0) == expected.top &&
			ini.GetInteger("Init", "Width", 0) == expected.width &&
			ini.GetInteger("Init", "Height", 0) == expected.height,
			std::string(expected.path) + " uses the compact mobile skill layout") && ok;
	}

	const ExpectedGeometry mapElements[] = {
		{ "ini\\ui\\mapthumbnail\\window.ini", 0, 0, 340, 289 },
		{ "ini\\ui\\mapthumbnail\\thumbnail.ini", 34, 34, 272, 204 },
		{ "ini\\ui\\mapthumbnail\\mapname.ini", 33, 10, 272, 17 },
		{ "ini\\ui\\mapthumbnail\\closebtn.ini", 307, 9, 19, 19 },
	};
	for (const auto& expected : mapElements)
	{
		std::unique_ptr<char[]> content;
		int length = 0;
		if (!File::readFile(expected.path, content, length) ||
			content == nullptr || length <= 0)
		{
			ok = check(false, std::string("JXQY2 loads ") + expected.path) && ok;
			continue;
		}
		INIReader ini(content);
		ok = check(ini.GetInteger("Init", "Left", 0) == expected.left &&
			ini.GetInteger("Init", "Top", 0) == expected.top &&
			ini.GetInteger("Init", "Width", 0) == expected.width &&
			ini.GetInteger("Init", "Height", 0) == expected.height,
			std::string(expected.path) + " uses the compact JXQY2 map layout") && ok;
	}

	return ok;
}

bool runYesNoPanelAlignmentSmoke(ResourceManager& manager)
{
	const char* centeredPackIds[] = {
		"JXQY2",
		"YYCS",
		"JIANGHU_YUCHEN_1_03",
		"JIANGHU_YUCHEN_2",
		"YUEMEIER_WAIZHUAN_1_053",
		"XIAOXIANGXING_1_022",
	};

	bool ok = true;
	for (const char* packId : centeredPackIds)
	{
		if (!manager.setActiveResourcePackById(packId))
		{
			ok = check(false, std::string(packId) + " yes/no alignment smoke pack is available") && ok;
			continue;
		}

		std::unique_ptr<char[]> content;
		int length = 0;
		const std::string windowIni = "ini\\ui\\yesno\\window.ini";
		if (!File::readFile(windowIni, content, length) || content == nullptr || length <= 0)
		{
			ok = check(false, std::string(packId) + " loads " + windowIni) && ok;
			continue;
		}

		INIReader ini(content);
		ok = check(toLowerAscii(ini.Get("Init", "Align", "")) == "alcenter" &&
			ini.GetInteger("Init", "AlignX", 0) == 0 &&
			ini.GetInteger("Init", "AlignY", 0) == 0,
			std::string(packId) + " exit confirmation is centered without offsets") && ok;
	}
	return ok;
}

bool runTitleLayoutResourceSmoke(ResourceManager& manager)
{
	struct TitleWindowExpectation
	{
		const char* packId;
		const char* path;
	};
	const TitleWindowExpectation windows[] = {
		{ "JXQY2", "ini\\ui\\title\\window.ini" },
		{ "YYCS", "ini\\ui\\title\\window.ini" },
		{ "XJXQY", "ini\\ui\\title\\window.ini" },
		{ "XJXQY", "ini\\ui\\title\\window1.ini" },
		{ "XINYUE_WUHEN_3_0", "ini\\ui\\title\\window.ini" },
		{ "JIAN_ER_GAI_CHENGHE_1_041", "ini\\ui\\title\\window.ini" },
		{ "YUEMEIER_WAIZHUAN_1_053", "ini\\ui\\title\\window.ini" },
		{ "JIANGHU_YUCHEN_1_03", "ini\\ui\\title\\window.ini" },
		{ "JIANGHU_YUCHEN_2", "ini\\ui\\title\\window.ini" },
		{ "XIAOXIANGXING_1_022", "ini\\ui\\title\\window.ini" },
	};

	bool ok = true;
	for (const auto& expected : windows)
	{
		if (!manager.setActiveResourcePackById(expected.packId))
		{
			ok = check(false,
				std::string(expected.packId) + " title layout pack is available") && ok;
			continue;
		}

		std::unique_ptr<char[]> content;
		int length = 0;
		if (!File::readFile(expected.path, content, length) ||
			content == nullptr || length <= 0)
		{
			ok = check(false,
				std::string(expected.packId) + " loads " + expected.path) && ok;
			continue;
		}
		INIReader ini(content);
		ok = check(ini.GetInteger("Init", "Width", 0) == 640 &&
			ini.GetInteger("Init", "Height", 0) == 480 &&
			toLowerAscii(ini.Get("Init", "Align", "")) == "alclient" &&
			ini.GetBoolean("Init", "Stretch", false) &&
			ini.GetBoolean("Init", "KeepAspect", false) &&
			ini.GetBoolean("Init", "FadeMirroredBars", false),
			std::string(expected.packId) + " " + expected.path +
			" keeps one aspect-fit 640x480 title composition") && ok;

		std::string bitmap = ini.Get("Init", "Image", "");
		if (bitmap.empty())
		{
			bitmap = ini.Get("Init", "Bitmap", "");
		}
		std::unique_ptr<char[]> imageContent;
		int imageLength = 0;
		if (bitmap.empty() || !File::readFile(bitmap, imageContent, imageLength)
			|| imageContent == nullptr || imageLength <= 0)
		{
			ok = check(false, std::string(expected.packId)
				+ " resolves title bitmap " + bitmap) && ok;
			continue;
		}
		SDL_Surface* surface = SafeImageDecoder::loadSurface(
			imageContent.get(), imageLength);
		ok = check(surface != nullptr && surface->w == 640 && surface->h == 480,
			std::string(expected.packId) + " decodes its 640x480 title bitmap "
				+ bitmap) && ok;
		if (surface != nullptr)
		{
			SDL_DestroySurface(surface);
		}
	}

	const char* coverPackIds[] = {
		"YUEMEIER_WAIZHUAN_1_053",
		"JIANGHU_YUCHEN_1_03",
		"JIANGHU_YUCHEN_2",
		"XIAOXIANGXING_1_022",
	};
	const auto& packs = manager.getDiscoveredPacks();
	for (const char* packId : coverPackIds)
	{
		auto packIt = std::find_if(packs.begin(), packs.end(), [packId](const ResourceManager::ResourcePack& pack)
			{
				return pack.manifest.id == packId;
			});
		if (packIt == packs.end())
		{
			ok = check(false, std::string(packId) + " release-cover pack is available") && ok;
			continue;
		}
		const ModRelease::CoverReadResult cover = ModRelease::readCoverFromPack(
			std::filesystem::u8path(packIt->rootPath),
			packIt->manifest.releaseMetadata);
		SDL_Surface* surface = cover.readyForDecode() && !cover.encodedBytes.empty()
			? SafeImageDecoder::loadSurface(cover.encodedBytes.data(),
				static_cast<int>(cover.encodedBytes.size()))
			: nullptr;
		ok = check(!packIt->manifest.releaseMetadata.coverPath.empty()
			&& surface != nullptr,
			std::string(packId) + " declares and decodes its MOD release cover") && ok;
		if (surface != nullptr)
		{
			SDL_DestroySurface(surface);
		}
	}

	if (!manager.setActiveResourcePackById("JXQY2"))
	{
		return check(false, "JXQY2 title button pack is available") && ok;
	}
	struct TitleButtonExpectation
	{
		const char* fileName;
		int top;
	};
	const TitleButtonExpectation buttons[] = {
		{ "initbtn", 227 },
		{ "loadbtn", 287 },
		{ "teambtn", 347 },
		{ "exitbtn", 407 },
	};
	for (const auto& expected : buttons)
	{
		const std::string iniPath = std::string("ini\\ui\\title\\") +
			expected.fileName + ".ini";
		std::unique_ptr<char[]> iniContent;
		int iniLength = 0;
		if (!File::readFile(iniPath, iniContent, iniLength) ||
			iniContent == nullptr || iniLength <= 0)
		{
			ok = check(false, "JXQY2 loads " + iniPath) && ok;
			continue;
		}
		INIReader ini(iniContent);
		ok = check(ini.GetInteger("Init", "Left", 0) == 284 &&
			ini.GetInteger("Init", "Top", 0) == expected.top &&
			ini.GetInteger("Init", "Width", 0) == 274 &&
			ini.GetInteger("Init", "Height", 0) == 36,
			"JXQY2 " + iniPath + " uses native 640x480 button geometry") && ok;

		const std::string imagePath = std::string("mpc\\ui\\title\\") +
			expected.fileName + ".mpc";
		std::unique_ptr<char[]> imageContent;
		int imageLength = 0;
		const bool readImage = File::readFile(
			imagePath, imageContent, imageLength) &&
			imageContent != nullptr && imageLength >= 128;
		auto readLittleEndianInt32 = [&imageContent](int offset)
		{
			const auto* bytes = reinterpret_cast<const unsigned char*>(
				imageContent.get());
			return static_cast<int>(bytes[offset]) |
				(static_cast<int>(bytes[offset + 1]) << 8) |
				(static_cast<int>(bytes[offset + 2]) << 16) |
				(static_cast<int>(bytes[offset + 3]) << 24);
		};
		const bool nativeMpcCanvas = readImage &&
			std::string(imageContent.get(), 16) ==
				std::string("MPC File Ver2.0", 16) &&
			readLittleEndianInt32(68) == 274 &&
			readLittleEndianInt32(72) == 36 &&
			readLittleEndianInt32(76) == 2;
		ok = check(nativeMpcCanvas,
			"JXQY2 " + imagePath +
			" preserves the uncropped native button canvas") && ok;
	}
	return ok;
}

bool runXinyueLifeExchangeResourceSmoke(ResourceManager& manager)
{
	const std::string packId = "XINYUE_WUHEN_3_0";
	if (!manager.setActiveResourcePackById(packId))
	{
		return check(false, packId + " life-exchange production pack is available");
	}

	const std::string magicPath =
		u8"ini\\magic\\player-magic-满江红.ini";
	std::unique_ptr<char[]> content;
	int length = 0;
	if (!File::readFile(magicPath, content, length)
		|| content == nullptr || length <= 0)
	{
		return check(false, packId + " loads " + magicPath);
	}

	INIReader ini(content);
	return check(ini.GetInteger("Init", "MoveKind", 0) == mmkSelf
		&& ini.GetInteger("Init", "SpecialKind", 0) == mskAddLife
		&& ini.GetInteger("Level1", "Effect", 0) == -500
		&& ini.GetInteger("Level1", "ManaCost", 0) == -60,
		packId + " keeps the original nonlethal life-to-mana configuration");
}

bool runXiaoxiangNpcLifecycleResourceSmoke(ResourceManager& manager)
{
	const std::string packId = "XIAOXIANGXING_1_022";
	if (!manager.setActiveResourcePackById(packId))
	{
		return check(false, packId + " detached-caster production pack is available");
	}

	bool ok = true;
	std::unique_ptr<char[]> magicContent;
	int magicLength = File::readFile("ini\\magic\\001火药炮.ini", magicContent);
	ok = check(magicContent != nullptr && magicLength > 0,
		packId + " loads the production gunpowder-cannon Magic") && ok;
	if (magicContent != nullptr && magicLength > 0)
	{
		INIReader magicIni(magicContent);
		const std::string flyMagic = magicIni.Get("Init", "FlyMagic", "");
		const std::string explodeMagic = magicIni.Get("Init", "ExplodeMagicFile", "");
		ok = check(flyMagic == "001霹雳烟火弹爆炸.ini"
			&& explodeMagic == flyMagic,
			packId + " production cannon uses the same FlyMagic and ExplodeMagicFile child") && ok;
		ok = checkRuntimeFile(packId, "GunpowderCannon.FlyMagic", { "ini\\magic\\" + flyMagic }) && ok;
	}

	std::unique_ptr<char[]> npcContent;
	int npcLength = File::readFile("ini\\save\\lamg.npc", npcContent);
	ok = check(npcContent != nullptr && npcLength > 0,
		packId + " loads the production lamg NPC map") && ok;
	if (npcContent != nullptr && npcLength > 0)
	{
		INIReader npcIni(npcContent);
		int hostileCannonCasterCount = 0;
		for (const auto& section : npcIni.GetSectionNames())
		{
			if (npcIni.Get(section, "FlyIni", "") == "001火药炮.ini"
				&& npcIni.GetInteger(section, "Relation", 0) == 1)
			{
				hostileCannonCasterCount++;
			}
		}
		ok = check(hostileCannonCasterCount == 8,
			packId + " production lamg map has exactly eight hostile cannon casters") && ok;
	}

	std::unique_ptr<char[]> bodyOwnerContent;
	int bodyOwnerLength = File::readFile("ini\\npc\\超级唐影.ini", bodyOwnerContent);
	ok = check(bodyOwnerContent != nullptr && bodyOwnerLength > 0,
		packId + " loads a production NPC with a corpse resource") && ok;
	if (bodyOwnerContent != nullptr && bodyOwnerLength > 0)
	{
		INIReader bodyOwnerIni(bodyOwnerContent);
		const std::string bodyIni = bodyOwnerIni.Get("Init", "BodyIni", "");
		ok = check(bodyIni == "z12-唐影尸体.ini",
			packId + " production NPC keeps its concrete BodyIni") && ok;
		ok = checkRuntimeFile(packId, "SuperTangYing.BodyIni", { "ini\\obj\\" + bodyIni }) && ok;
	}

	std::unique_ptr<char[]> scriptedDeathContent;
	int scriptedDeathLength = File::readFile("ini\\npc\\夜行杀手0.ini", scriptedDeathContent);
	ok = check(scriptedDeathContent != nullptr && scriptedDeathLength > 0,
		packId + " loads a production NPC with a death script") && ok;
	if (scriptedDeathContent != nullptr && scriptedDeathLength > 0)
	{
		INIReader scriptedDeathIni(scriptedDeathContent);
		ok = check(scriptedDeathIni.Get("Init", "DeathScript", "") == "抓住杀手.txt",
			packId + " production NPC keeps its concrete DeathScript") && ok;
	}
	return ok;
}

bool runXjxqyLegacyStoryNpcResourceSmoke(ResourceManager& manager)
{
	const std::string packId = "XJXQY";
	if (!manager.setActiveResourcePackById(packId))
	{
		return check(false, packId + " production pack is available");
	}

	std::unique_ptr<char[]> npcContent;
	int npcLength = File::readFile("ini\\npc\\npc100_卢青.ini", npcContent);
	bool ok = check(npcContent != nullptr && npcLength > 0,
		packId + " loads the opening Lu Qing NPC resource");
	if (npcContent == nullptr || npcLength <= 0)
	{
		return false;
	}

	INIReader npcIni(npcContent);
	ok = check(npcIni.Get("Init", "Name", "") == "卢青"
		&& npcIni.Get("Init", "Life", "missing").empty()
		&& npcIni.Get("Init", "LifeMax", "missing").empty()
		&& npcIni.Get("Init", "DeathPersistenceVersion", "").empty()
		&& npcIni.Get("Init", "IsDeathInvoked", "").empty()
		&& npcIni.Get("Init", "IsDeath", "").empty(),
		packId + " opening Lu Qing keeps the legacy empty-life live NPC contract") && ok;
	const std::string npcResource = npcIni.Get("Init", "NpcIni", "");
	ok = check(npcResource == "npcres100_卢青.ini",
		packId + " opening Lu Qing keeps the expected character resource") && ok;
	ok = checkRuntimeFile(packId, "OpeningLuQing.NpcIni",
		{ "ini\\npcres\\" + npcResource }) && ok;

	std::unique_ptr<char[]> stoneObjectContent;
	int stoneObjectLength = File::readFile(
		"ini\\objres\\obj-石头.ini", stoneObjectContent);
	ok = check(stoneObjectContent != nullptr && stoneObjectLength > 0,
		packId + " loads the Hengshan blood-letter stone resource") && ok;
	if (stoneObjectContent != nullptr && stoneObjectLength > 0)
	{
		INIReader stoneObjectIni(stoneObjectContent);
		const std::string stoneImage = stoneObjectIni.Get(
			"Common", "Image", "");
		ok = check(stoneImage == "石头.asf",
			packId + " Hengshan stone keeps the real UTF-8 image name") && ok;
		ok = checkRuntimeFile(packId, "HengshanStone.Image",
			{ "asf\\object\\" + stoneImage }) && ok;
	}
	return ok;
}

void writeRawFile(const std::filesystem::path& path, const std::string& content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary);
	out << content;
}

std::filesystem::path createSyntheticCollection()
{
	namespace fs = std::filesystem;
	fs::path root = makeUniqueTestDirectory("jxqy_resource_smoke_test");
	fs::remove_all(root);
	fs::create_directories(root);

	writeRawFile(root / "resources.ini",
		"[Collection]\n"
		"CommonPath=common\n"
		"\n"
		"[Pack.BASE]\n"
		"Id=BASE\n"
		"Path=base\n"
		"Manifest=game_profile.ini\n"
		"Enabled=1\n"
		"\n"
		"[Pack.MOD]\n"
		"Id=MOD\n"
		"Path=mod\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"Enabled=1\n");

	writeRawFile(root / "base" / "game_profile.ini",
		"[Game]\n"
		"Id=BASE\n"
		"Name=Base Pack\n"
		"Type=0\n"
		"UseWav=0\n"
		"\n"
		"[Resource]\n"
		"TextEncodingConverted=1\n"
		"\n"
		"[Save]\n"
		"Namespace=BASE\n"
		"\n"
		"[Startup]\n"
		"Videos=logo.avi,newyear:newyear.avi\n"
		"\n"
		"[Title]\n"
		"Menu=ini\\ui\\title\\title.menu.ini\n"
		"NewYearMenu=ini\\ui\\title\\title_newyear.menu.ini\n"
		"Music=title.mp3\n"
		"TeamVideo=team.avi\n"
		"\n"
		"[NewGame]\n"
		"Script=newgame.txt\n");

	writeRawFile(root / "mod" / "game_profile.ini",
		"[Game]\n"
		"Id=MOD\n"
		"Name=Mod Pack\n"
		"Type=99\n"
		"UseWav=0\n"
		"\n"
		"[Resource]\n"
		"DependencyId=BASE\n"
		"TextEncodingConverted=1\n"
		"\n"
		"[Save]\n"
		"Namespace=MOD_SAVE\n"
		"\n"
		"[Startup]\n"
		"Videos=mod_logo.avi\n"
		"\n"
		"[Title]\n"
		"Menu=ini\\ui\\title\\mod.menu.ini\n"
		"Music=mod_title.mp3\n"
		"TeamVideo=\n"
		"\n"
		"[Team]\n"
		"InfoFile=team.txt\n"
		"\n"
		"[NewGame]\n"
		"Script=mod_newgame.txt\n");

	writeRawFile(root / "base" / "ini" / "ui" / "title" / "title.menu.ini", "[Menu]\n");
	writeRawFile(root / "base" / "ini" / "ui" / "title" / "title_newyear.menu.ini", "[Menu]\n");
	writeRawFile(root / "base" / "script" / "common" / "newgame.txt", "message('base')\n");
	writeRawFile(root / "base" / "music" / "title.mp3", "music");
	writeRawFile(root / "base" / "video" / "logo.avi", "video");
	writeRawFile(root / "base" / "video" / "newyear.avi", "video");
	writeRawFile(root / "base" / "video" / "team.avi", "video");

	writeRawFile(root / "mod" / "ini" / "ui" / "title" / "mod.menu.ini", "[Menu]\n");
	writeRawFile(root / "base" / "script" / "common" / "mod_newgame.txt", "message('mod')\n");
	writeRawFile(root / "base" / "music" / "mod_title.mp3", "music");
	writeRawFile(root / "base" / "video" / "mod_logo.avi", "video");
	writeRawFile(root / "mod" / "team.txt", "Mod Team\n");

	return root;
}

bool runGoodsCooldownSmoke()
{
	GoodsInfo goodsInfo;
	goodsInfo.iniFile = "drug.ini";
	goodsInfo.number = 2;
	goodsInfo.remainColdMilliseconds = 100;

	goodsInfo.updateColdTime(40);
	bool ok = check(goodsInfo.remainColdMilliseconds == 60, "goods cooldown decrements by frame time");

	goodsInfo.updateColdTime(100);
	ok = check(goodsInfo.remainColdMilliseconds == 0, "goods cooldown clamps to zero") && ok;

	goodsInfo.remainColdMilliseconds = 50;
	goodsInfo.clear();
	ok = check(goodsInfo.iniFile.empty(), "goods clear resets ini file") && ok;
	ok = check(goodsInfo.number == 0, "goods clear resets count") && ok;
	ok = check(goodsInfo.goods == nullptr, "goods clear resets pointer") && ok;
	ok = check(goodsInfo.remainColdMilliseconds == 0, "goods clear resets cooldown") && ok;
	return ok;
}

bool runTestModEquipmentTriggerSmoke(ResourceManager& manager)
{
	if (!manager.setActiveResourcePackById("XJXQY_TEST_MOD"))
	{
		return true;
	}

	bool ok = true;
	std::unique_ptr<char[]> content;
	int length = File::readFile("ini\\goods\\mod_test_equipment_trigger.ini", content);
	ok = check(content != nullptr && length > 0, "XJXQY_TEST_MOD equipment trigger goods file resolves") && ok;
	if (content == nullptr || length <= 0)
	{
		return false;
	}

	INIReader ini(content);
	const std::string section = "Init";
	std::string flyIni = ini.Get(section, "FlyIni", "");
	std::string flyIni2 = ini.Get(section, "FlyIni2", "");
	std::string counterMagic = ini.Get(section, "MagicToUseWhenBeAttacked", "");

	ok = check(ini.GetInteger(section, "Kind", 0) == 1, "XJXQY_TEST_MOD equipment trigger goods is equipment") && ok;
	ok = check(toLowerAscii(ini.Get(section, "Part", "")) == "hand", "XJXQY_TEST_MOD equipment trigger goods equips to hand") && ok;
	ok = check(flyIni == "mod_test_magic_equipment_fly.ini", "XJXQY_TEST_MOD equipment trigger loads FlyIni") && ok;
	ok = check(flyIni2 == "mod_test_magic_equipment_fly2.ini", "XJXQY_TEST_MOD equipment trigger loads FlyIni2") && ok;
	ok = check(counterMagic == "mod_test_magic_equipment_counter.ini", "XJXQY_TEST_MOD equipment trigger loads be-attacked magic") && ok;
	ok = check(ini.GetInteger(section, "MagicDirectionWhenBeAttacked", 0) == 1, "XJXQY_TEST_MOD equipment trigger loads be-attacked direction") && ok;
	ok = check(ini.GetInteger(section, "ChangeMoveSpeedPercent", 0) == 25, "XJXQY_TEST_MOD equipment trigger loads move speed percent") && ok;
	ok = check(ini.GetInteger(section, "AddMagicEffectPercent", 0) == 30, "XJXQY_TEST_MOD equipment trigger loads magic effect percent") && ok;
	ok = check(ini.GetInteger(section, "AddMagicEffectAmount", 0) == 2, "XJXQY_TEST_MOD equipment trigger loads magic effect amount") && ok;
	ok = check(ini.Get(section, "AddMagicEffectName", "") == "MOD_TEST_EQUIPMENT_POWER", "XJXQY_TEST_MOD equipment trigger loads magic effect name") && ok;
	ok = check(ini.Get(section, "AddMagicEffectType", "") == "Attack", "XJXQY_TEST_MOD equipment trigger loads magic effect type") && ok;
	ok = checkRuntimeFile("XJXQY_TEST_MOD", "Equipment.FlyIni", { "ini\\magic\\" + flyIni }) && ok;
	ok = checkRuntimeFile("XJXQY_TEST_MOD", "Equipment.FlyIni2", { "ini\\magic\\" + flyIni2 }) && ok;
	ok = checkRuntimeFile("XJXQY_TEST_MOD", "Equipment.MagicToUseWhenBeAttacked", { "ini\\magic\\" + counterMagic }) && ok;
	ok = checkRuntimeFile("XJXQY_TEST_MOD", "Equipment.AddMagicEffectName", { "ini\\magic\\mod_test_magic_equipment_power.ini" }) && ok;
	ok = checkRuntimeFile("XJXQY_TEST_MOD", "Equipment magic target NPC", { "ini\\npc\\mod_test_equipment_power_target_npc.ini" }) && ok;

	std::unique_ptr<char[]> additionalEffectMagicContent;
	int additionalEffectMagicLength = File::readFile("ini\\magic\\mod_test_magic_equipment_additional_freeze.ini", additionalEffectMagicContent);
	ok = check(additionalEffectMagicContent != nullptr && additionalEffectMagicLength > 0, "XJXQY_TEST_MOD equipment AdditionalEffect magic file resolves") && ok;
	if (additionalEffectMagicContent != nullptr && additionalEffectMagicLength > 0)
	{
		INIReader additionalEffectMagicIni(additionalEffectMagicContent);
		ok = check(additionalEffectMagicIni.GetInteger(section, "Effect", -1) == 0, "XJXQY_TEST_MOD equipment AdditionalEffect fixture is zero-damage") && ok;
		ok = check(additionalEffectMagicIni.GetInteger(section, "AdditionalEffect", 0) == 1, "XJXQY_TEST_MOD equipment AdditionalEffect fixture freezes on hit") && ok;
	}

	return ok;
}

bool runTestModMagicCollisionSmoke(ResourceManager& manager)
{
	if (!manager.setActiveResourcePackById("XJXQY_TEST_MOD"))
	{
		return true;
	}

	bool ok = true;
	std::unique_ptr<char[]> content;
	int length = File::readFile("ini\\magic\\mod_test_magic_summon.ini", content);
	ok = check(content != nullptr && length > 0, "XJXQY_TEST_MOD summon magic file resolves") && ok;
	if (content == nullptr || length <= 0)
	{
		return false;
	}

	INIReader ini(content);
	const std::string section = "Init";
	std::string npcFile = ini.Get(section, "NpcFile", "");
	ok = check(ini.GetInteger(section, "MoveKind", 0) == 22, "XJXQY_TEST_MOD summon magic uses MoveKind=22") && ok;
	ok = check(npcFile == "mod_test_summon_npc.ini", "XJXQY_TEST_MOD summon magic loads NpcFile") && ok;
	ok = check(ini.GetInteger(section, "MaxCount", 0) == 1, "XJXQY_TEST_MOD summon magic loads MaxCount") && ok;
	ok = checkRuntimeFile("XJXQY_TEST_MOD", "Summon.NpcFile", { "ini\\npc\\" + npcFile }) && ok;

	std::unique_ptr<char[]> probeContent;
	int probeLength = File::readFile("ini\\magic\\mod_test_magic_state_probe.ini", probeContent);
	ok = check(probeContent != nullptr && probeLength > 0, "XJXQY_TEST_MOD magic state probe file resolves") && ok;
	if (probeContent != nullptr && probeLength > 0)
	{
		INIReader probeIni(probeContent);
		ok = check(probeIni.GetInteger(section, "Count", 0) == 3, "XJXQY_TEST_MOD Magic.Count fixture has Init value") && ok;
		ok = check(probeIni.GetInteger("Level1", "Count", 0) == 4, "XJXQY_TEST_MOD Magic.Count fixture has Level1 value") && ok;
		ok = check(probeIni.GetInteger(section, "SpecialKindValue", 0) == 7, "XJXQY_TEST_MOD Magic.SpecialKindValue fixture has Init value") && ok;
		ok = check(probeIni.GetInteger("Level1", "SpecialKindValue", 0) == 8, "XJXQY_TEST_MOD Magic.SpecialKindValue fixture has Level1 value") && ok;
		ok = check(probeIni.GetInteger(section, "NoSpecialKindEffectExt", 0) == 1, "XJXQY_TEST_MOD Magic.NoSpecialKindEffectExt fixture remains readable") && ok;
		ok = check(probeIni.GetInteger(section, "MaxCount", 0) == 2, "XJXQY_TEST_MOD Magic.MaxCount fixture stays separate from Count") && ok;
	}
	return ok;
}

bool runTestModGoodsPricingSmoke(ResourceManager& manager)
{
	if (!manager.setActiveResourcePackById("XJXQY_TEST_MOD"))
	{
		return true;
	}

	bool ok = true;
	std::unique_ptr<char[]> drugContent;
	int drugLength = File::readFile("ini\\goods\\mod_test_goods_pricing_drug.ini", drugContent);
	ok = check(drugContent != nullptr && drugLength > 0, "XJXQY_TEST_MOD goods pricing drug file resolves") && ok;
	if (drugContent != nullptr && drugLength > 0)
	{
		INIReader drugIni(drugContent);
		const std::string section = "Init";
		ok = check(drugIni.GetInteger(section, "Kind", -1) == 0, "XJXQY_TEST_MOD goods pricing drug is drug") && ok;
		ok = check(drugIni.GetInteger(section, "Cost", -1) == 0, "XJXQY_TEST_MOD goods pricing drug uses default raw cost") && ok;
		ok = check(drugIni.GetInteger(section, "Life", 0) == 10, "XJXQY_TEST_MOD goods pricing drug loads Life") && ok;
		ok = check(drugIni.GetInteger(section, "Thew", 0) == 5, "XJXQY_TEST_MOD goods pricing drug loads Thew") && ok;
		ok = check(drugIni.GetInteger(section, "Mana", 0) == 3, "XJXQY_TEST_MOD goods pricing drug loads Mana") && ok;
	}

	std::unique_ptr<char[]> equipmentContent;
	int equipmentLength = File::readFile("ini\\goods\\mod_test_goods_pricing_equipment.ini", equipmentContent);
	ok = check(equipmentContent != nullptr && equipmentLength > 0, "XJXQY_TEST_MOD goods pricing equipment file resolves") && ok;
	if (equipmentContent != nullptr && equipmentLength > 0)
	{
		INIReader equipmentIni(equipmentContent);
		const std::string section = "Init";
		ok = check(equipmentIni.GetInteger(section, "Kind", -1) == 1, "XJXQY_TEST_MOD goods pricing equipment is equipment") && ok;
		ok = check(equipmentIni.GetInteger(section, "SellPrice", 0) == 33, "XJXQY_TEST_MOD goods pricing equipment loads SellPrice") && ok;
		ok = check(toLowerAscii(equipmentIni.Get(section, "Part", "")) == "hand", "XJXQY_TEST_MOD goods pricing equipment part resolves") && ok;
	}

	std::unique_ptr<char[]> randomContent;
	int randomLength = File::readFile("ini\\goods\\mod_test_goods_random.ini", randomContent);
	ok = check(randomContent != nullptr && randomLength > 0, "XJXQY_TEST_MOD goods random file resolves") && ok;
	if (randomContent != nullptr && randomLength > 0)
	{
		INIReader randomIni(randomContent);
		const std::string section = "Init";
		ok = check(randomIni.Get(section, "Attack", "") == "1>3", "XJXQY_TEST_MOD goods random keeps attack range") && ok;
		ok = check(randomIni.Get(section, "Defend", "") == "4,8", "XJXQY_TEST_MOD goods random keeps defend list") && ok;
		ok = check(randomIni.Get(section, "Evade", "") == "1>-5", "XJXQY_TEST_MOD goods random keeps reversed evade range") && ok;
		ok = check(randomIni.Get(section, "MagicIniWhenUse", "").find(',') != std::string::npos, "XJXQY_TEST_MOD goods random keeps random string list") && ok;
	}

	std::unique_ptr<char[]> lifecycleStackContent;
	int lifecycleStackLength = File::readFile("ini\\goods\\mod_test_goods_lifecycle_stack.ini", lifecycleStackContent);
	ok = check(lifecycleStackContent != nullptr && lifecycleStackLength > 0, "XJXQY_TEST_MOD goods lifecycle stack file resolves") && ok;
	if (lifecycleStackContent != nullptr && lifecycleStackLength > 0)
	{
		INIReader lifecycleStackIni(lifecycleStackContent);
		const std::string section = "Init";
		ok = check(lifecycleStackIni.Get(section, "Name", "") == "MOD_TEST_GOODS_LIFECYCLE_STACK", "XJXQY_TEST_MOD goods lifecycle stack loads display name") && ok;
	}

	std::unique_ptr<char[]> lifecycleMagicContent;
	int lifecycleMagicLength = File::readFile("ini\\goods\\mod_test_goods_lifecycle_magic.ini", lifecycleMagicContent);
	ok = check(lifecycleMagicContent != nullptr && lifecycleMagicLength > 0, "XJXQY_TEST_MOD goods lifecycle magic file resolves") && ok;
	if (lifecycleMagicContent != nullptr && lifecycleMagicLength > 0)
	{
		INIReader lifecycleMagicIni(lifecycleMagicContent);
		const std::string section = "Init";
		ok = check(lifecycleMagicIni.GetInteger(section, "Kind", -1) == 1, "XJXQY_TEST_MOD goods lifecycle magic is equipment") && ok;
		ok = check(toLowerAscii(lifecycleMagicIni.Get(section, "Part", "")) == "hand", "XJXQY_TEST_MOD goods lifecycle magic equips to hand") && ok;
		ok = check(lifecycleMagicIni.Get(section, "MagicIniWhenUse", "") == "mod_test_magic_equipment_fly.ini", "XJXQY_TEST_MOD goods lifecycle magic grants MagicIniWhenUse") && ok;
	}

	std::unique_ptr<char[]> boundAmmoContent;
	int boundAmmoLength = File::readFile("ini\\goods\\mod_test_goods_bound_ammo.ini", boundAmmoContent);
	ok = check(boundAmmoContent != nullptr && boundAmmoLength > 0, "XJXQY_TEST_MOD bound ammo goods file resolves") && ok;

	std::unique_ptr<char[]> boundMagicContent;
	int boundMagicLength = File::readFile("ini\\magic\\mod_test_magic_goods_bound.ini", boundMagicContent);
	ok = check(boundMagicContent != nullptr && boundMagicLength > 0, "XJXQY_TEST_MOD bound magic file resolves") && ok;
	if (boundMagicContent != nullptr && boundMagicLength > 0)
	{
		INIReader boundMagicIni(boundMagicContent);
		const std::string section = "Init";
		ok = check(boundMagicIni.Get(section, "GoodsName", "") == "mod_test_goods_bound_ammo.ini", "XJXQY_TEST_MOD bound magic consumes GoodsName") && ok;
	}

	std::unique_ptr<char[]> scriptBookContent;
	int scriptBookLength = File::readFile("ini\\goods\\mod_test_goods_script_book.ini", scriptBookContent);
	ok = check(scriptBookContent != nullptr && scriptBookLength > 0, "XJXQY_TEST_MOD goods script book file resolves") && ok;
	if (scriptBookContent != nullptr && scriptBookLength > 0)
	{
		INIReader scriptBookIni(scriptBookContent);
		const std::string section = "Init";
		ok = check(scriptBookIni.GetInteger(section, "Kind", -1) == 2, "XJXQY_TEST_MOD goods script book is event goods") && ok;
		ok = check(scriptBookIni.Get(section, "Script", "") == "mod_test_goods_script_book.txt", "XJXQY_TEST_MOD goods script book points to goods script") && ok;
	}
	std::unique_ptr<char[]> scriptBookScriptContent;
	int scriptBookScriptLength = File::readFile("script\\goods\\mod_test_goods_script_book.txt", scriptBookScriptContent);
	ok = check(scriptBookScriptContent != nullptr && scriptBookScriptLength > 0, "XJXQY_TEST_MOD goods script book script resolves") && ok;

	std::unique_ptr<char[]> scriptBookMagicContent;
	int scriptBookMagicLength = File::readFile("ini\\magic\\mod_test_magic_goods_script_book.ini", scriptBookMagicContent);
	ok = check(scriptBookMagicContent != nullptr && scriptBookMagicLength > 0, "XJXQY_TEST_MOD goods script book magic file resolves") && ok;
	return ok;
}

bool checkGambleMenuComponent(
	const std::string& packId,
	const std::string& fileName,
	bool requireImage,
	bool requireSound,
	bool allowEmptyImage)
{
	std::string runtimePath = "ini\\ui\\littlegame\\" + fileName;
	std::unique_ptr<char[]> content;
	int length = File::readFile(runtimePath, content);
	bool ok = check(content != nullptr && length > 0, packId + " gamble UI " + fileName + " resolves");
	if (content == nullptr || length <= 0)
	{
		return false;
	}

	INIReader ini(content);
	const std::string section = "Init";
	int width = ini.GetInteger(section, "Width", 0);
	int height = ini.GetInteger(section, "Height", 0);
	ok = check(width > 0 && height > 0, packId + " gamble UI " + fileName + " has a hit/draw rectangle") && ok;

	std::string image = ini.Get(section, "Image", "");
	if (image.empty())
	{
		image = ini.Get(section, "Bitmap", "");
	}
	if (requireImage)
	{
		ok = check(!image.empty(), packId + " gamble UI " + fileName + " declares an image") && ok;
	}
	if (!image.empty())
	{
		ok = checkRuntimeFile(packId, "GambleUI.Image." + fileName, { image }) && ok;
	}
	else
	{
		ok = check(allowEmptyImage, packId + " gamble UI " + fileName + " only omits image for transparent hit regions") && ok;
	}

	std::string sound = ini.Get(section, "Sound", "");
	if (requireSound)
	{
		ok = check(!sound.empty(), packId + " gamble UI " + fileName + " declares a sound") && ok;
	}
	if (!sound.empty())
	{
		ok = checkRuntimeFile(packId, "GambleUI.Sound." + fileName, { sound }) && ok;
	}

	return ok;
}

bool checkFishGameResources(const std::string& packId)
{
	std::unique_ptr<char[]> content;
	int length = File::readFile("ini\\ui\\fishgame\\window.ini", content);
	bool ok = check(content != nullptr && length > 0, packId + " FishGame manifest resolves");
	if (content == nullptr || length <= 0)
	{
		return false;
	}
	INIReader ini(content);
	ok = check(ini.ParseError() == 0
		&& ini.GetInteger("Window", "Width", 0) == 796
		&& ini.GetInteger("Window", "Height", 0) == 569,
		packId + " FishGame manifest keeps the MG 796x569 canvas") && ok;

	const std::vector<std::pair<std::string, std::string>> imageKeys = {
		{ "Window", "Frame" }, { "Window", "Background" }, { "Window", "Foreground" },
		{ "Window", "ProgressBack" }, { "Window", "ProgressFill" }, { "Window", "Cast" },
		{ "Window", "Pull" }, { "Window", "Struggle" }, { "Window", "LifeOn" },
		{ "Window", "LifeDown" }, { "MovieGetFish", "Image" }, { "MovieSb01", "Image" },
		{ "MoviePb", "Image" }, { "MovieQt", "Image" }, { "MovieSb02", "Image" },
		{ "MovieSy", "Image" }, { "MovieSh", "Image" }, { "MovieYq", "Image" },
		{ "MovieFish", "Image" },
	};
	for (const auto& [section, key] : imageKeys)
	{
		std::string path = ini.Get(section, key, "");
		ok = checkRuntimeFile(packId, "FishGame.PNG." + section + "." + key, { path }) && ok;
		std::unique_ptr<char[]> imageContent;
		int imageLength = File::readFile(path, imageContent);
		ok = check(imageContent != nullptr && imageLength >= 8
			&& static_cast<unsigned char>(imageContent[0]) == 0x89
			&& imageContent[1] == 'P' && imageContent[2] == 'N' && imageContent[3] == 'G'
			&& imageContent[4] == '\r' && imageContent[5] == '\n'
			&& static_cast<unsigned char>(imageContent[6]) == 0x1A && imageContent[7] == '\n',
			packId + " " + path + " is a standard PNG") && ok;
	}
	const std::array<const char*, 6> soundKeys = {
		"Cast", "Wait", "Bite", "Pull", "Reel", "Struggle"
	};
	for (const char* key : soundKeys)
	{
		std::string path = ini.Get("Sounds", key, "");
		ok = check(!path.empty() && File::isSafeResourcePath(path),
			packId + " FishGame.WAV." + key + " declares a safe optional resource path") && ok;
		if (path.empty() || !File::isSafeResourcePath(path) ||
			!File::fileExist(path))
		{
			std::cout << "SKIP: " << packId << " optional " << path
				<< " is not present in the distributed asset set\n";
			continue;
		}
		std::unique_ptr<char[]> soundContent;
		int soundLength = File::readFile(path, soundContent);
		ok = check(soundContent != nullptr && soundLength >= 12
			&& soundContent[0] == 'R' && soundContent[1] == 'I'
			&& soundContent[2] == 'F' && soundContent[3] == 'F'
			&& soundContent[8] == 'W' && soundContent[9] == 'A'
			&& soundContent[10] == 'V' && soundContent[11] == 'E',
			packId + " " + path + " is a standard WAV") && ok;
	}
	return ok;
}

bool checkDiceGameResources(const std::string& packId)
{
	std::unique_ptr<char[]> content;
	int length = File::readFile("ini\\ui\\dicegame\\window.ini", content);
	bool ok = check(content != nullptr && length > 0, packId + " DiceGame manifest resolves");
	if (content == nullptr || length <= 0)
	{
		return false;
	}
	INIReader ini(content);
	ok = check(ini.ParseError() == 0
		&& ini.GetInteger("Window", "Width", 0) == 549
		&& ini.GetInteger("Window", "Height", 0) == 300,
		packId + " DiceGame manifest keeps the MG 549x300 canvas") && ok;

	std::vector<std::pair<std::string, std::string>> imageKeys = {
		{ "Window", "Frame" }, { "Window", "PlayerPortrait" },
		{ "Window", "NpcPortrait" }, { "Window", "PlayerTalk" },
		{ "Window", "NpcTalk" },
		{ "Window", "Nameplate" }, { "Window", "Silver" },
		{ "Window", "Versus" }, { "Window", "ResultWin" },
		{ "Window", "ResultLose" }, { "Window", "ResultTie" },
	};
	for (int face = 1; face <= 6; face++)
	{
		imageKeys.push_back({ "Dice", "Face" + std::to_string(face) });
	}
	for (const auto& [section, key] : imageKeys)
	{
		std::string path = ini.Get(section, key, "");
		ok = checkRuntimeFile(packId, "DiceGame.PNG." + section + "." + key, { path }) && ok;
		std::unique_ptr<char[]> imageContent;
		int imageLength = File::readFile(path, imageContent);
		ok = check(imageContent != nullptr && imageLength >= 8
			&& static_cast<unsigned char>(imageContent[0]) == 0x89
			&& imageContent[1] == 'P' && imageContent[2] == 'N' && imageContent[3] == 'G'
			&& imageContent[4] == '\r' && imageContent[5] == '\n'
			&& static_cast<unsigned char>(imageContent[6]) == 0x1A && imageContent[7] == '\n',
			packId + " " + path + " is a standard PNG") && ok;
	}
	return ok;
}

bool runXjxqyGambleMenuResourceSmoke(ResourceManager& manager, bool requireFallbackPacks)
{
	if (!manager.setActiveResourcePackById("XJXQY"))
	{
		return !requireFallbackPacks;
	}

	bool ok = true;
	ok = checkFishGameResources("XJXQY") && ok;
	ok = checkDiceGameResources("XJXQY") && ok;
	ok = checkGambleMenuComponent("XJXQY", "window.ini", true, false, false) && ok;
	ok = checkGambleMenuComponent("XJXQY", "openbg.ini", true, false, false) && ok;
	ok = checkGambleMenuComponent("XJXQY", "gambling.ini", true, false, false) && ok;
	ok = checkGambleMenuComponent("XJXQY", "opening.ini", true, false, false) && ok;
	ok = checkGambleMenuComponent("XJXQY", "dice1.ini", true, false, false) && ok;
	ok = checkGambleMenuComponent("XJXQY", "dice2.ini", true, false, false) && ok;
	ok = checkGambleMenuComponent("XJXQY", "dice3.ini", true, false, false) && ok;
	ok = checkGambleMenuComponent("XJXQY", "gold.ini", true, false, false) && ok;
	ok = checkGambleMenuComponent("XJXQY", "message.ini", true, false, false) && ok;
	ok = checkGambleMenuComponent("XJXQY", "playerface.ini", true, false, false) && ok;
	ok = checkGambleMenuComponent("XJXQY", "bossface.ini", true, false, false) && ok;
	ok = checkGambleMenuComponent("XJXQY", "luface.ini", true, false, false) && ok;
	ok = checkGambleMenuComponent("XJXQY", "chipin.ini", true, true, false) && ok;
	ok = checkGambleMenuComponent("XJXQY", "quit.ini", true, true, false) && ok;
	ok = checkGambleMenuComponent("XJXQY", "arrowup.ini", true, true, false) && ok;
	ok = checkGambleMenuComponent("XJXQY", "arrowdn.ini", true, true, false) && ok;
	ok = checkGambleMenuComponent("XJXQY", "gamblebig.ini", false, true, true) && ok;
	ok = checkGambleMenuComponent("XJXQY", "gamblesmall.ini", false, true, true) && ok;
	ok = checkGambleMenuComponent("XJXQY", "labplayer.ini", false, false, true) && ok;
	ok = checkGambleMenuComponent("XJXQY", "labcomputer.ini", false, false, true) && ok;
	ok = checkGambleMenuComponent("XJXQY", "labchipin.ini", false, false, true) && ok;
	ok = checkGambleMenuComponent("XJXQY", "labmessage.ini", false, false, true) && ok;
	for (const std::string& fileName : { "labplayer.ini", "labcomputer.ini", "labchipin.ini" })
	{
		std::unique_ptr<char[]> labelContent;
		int labelLength = File::readFile("ini\\ui\\littlegame\\" + fileName, labelContent);
		ok = check(labelContent != nullptr && labelLength > 0,
			"XJXQY GambleUI value label resolves: " + fileName) && ok;
		if (labelContent != nullptr && labelLength > 0)
		{
			INIReader labelIni(labelContent);
			ok = check(labelIni.GetInteger("Init", "Top", 0) == 446
				&& labelIni.GetInteger("Init", "Width", 0) == 48
				&& labelIni.GetInteger("Init", "Height", 0) == 20
				&& labelIni.GetInteger("Init", "Font", 0) == 14,
				"XJXQY GambleUI value label stays inside its framed number field: " + fileName) && ok;
		}
	}

	if (manager.setActiveResourcePackById("XJXQY_TEST_MOD"))
	{
		ok = checkRuntimeFile("XJXQY_TEST_MOD", "GambleUI inherited window", { "ini\\ui\\littlegame\\window.ini" }) && ok;
		ok = checkRuntimeFile("XJXQY_TEST_MOD", "GambleUI inherited transparent big button", { "ini\\ui\\littlegame\\gamblebig.ini" }) && ok;
		ok = checkGambleMenuComponent("XJXQY_TEST_MOD", "chipin.ini", true, true, false) && ok;
		ok = checkGambleMenuComponent("XJXQY_TEST_MOD", "gamblesmall.ini", false, true, true) && ok;
	}
	bool testFallbackAvailable = manager.setActiveResourcePackById("XJXQY_TEST_FALLBACK");
	if (testFallbackAvailable)
	{
		ok = check(!File::fileExist("ini\\ui\\littlegame\\window.ini"),
			"XJXQY_TEST_FALLBACK intentionally selects a UI base without a gamble resource layout") && ok;
		ok = checkRuntimeFile("XJXQY_TEST_FALLBACK", "GambleUI fallback button sound",
			{ "sound\\界-大按钮.wav" }) && ok;
		ok = checkFishGameResources("XJXQY_TEST_FALLBACK") && ok;
		ok = checkDiceGameResources("XJXQY_TEST_FALLBACK") && ok;
	}
	else if (requireFallbackPacks)
	{
		ok = check(false, "required resource pack XJXQY_TEST_FALLBACK is registered and selectable") && ok;
	}
	bool jianghuYuchenAvailable = manager.setActiveResourcePackById("JIANGHU_YUCHEN_1_03");
	if (jianghuYuchenAvailable)
	{
		ok = check(!File::fileExist("ini\\ui\\littlegame\\window.ini"),
			"JIANGHU_YUCHEN_1_03 keeps the YYCS fallback only for trilogy Gamble") && ok;
		ok = checkRuntimeFile("JIANGHU_YUCHEN_1_03", "GambleUI fallback button sound",
			{ "sound\\界-大按钮.wav" }) && ok;
		ok = checkFishGameResources("JIANGHU_YUCHEN_1_03") && ok;
		ok = checkDiceGameResources("JIANGHU_YUCHEN_1_03") && ok;
	}
	else if (requireFallbackPacks)
	{
		ok = check(false, "required resource pack JIANGHU_YUCHEN_1_03 is registered and selectable") && ok;
	}
	return ok;
}

bool runTestModHubIsolationSmoke(ResourceManager& manager)
{
	if (!manager.setActiveResourcePackById("XJXQY_TEST_MOD"))
	{
		return false;
	}

	bool ok = true;
	constexpr int NormalNpcKind = 0;
	constexpr int FriendlyNpcRelation = 0;
	for (const std::string& fileName : {
		"mod_test_hub_guide.ini", "mod_test_hub_little_games_host.ini" })
	{
		std::unique_ptr<char[]> utilityContent;
		int utilityLength = File::readFile("ini\\npc\\" + fileName, utilityContent);
		ok = check(utilityContent != nullptr && utilityLength > 0,
			"XJXQY_TEST_MOD utility NPC fixture resolves: " + fileName) && ok;
		if (utilityContent != nullptr && utilityLength > 0)
		{
			INIReader utilityIni(utilityContent);
			ok = check(utilityIni.GetInteger("INIT", "Kind", -1) == NormalNpcKind
				&& utilityIni.GetInteger("INIT", "Relation", -1) == FriendlyNpcRelation,
				"XJXQY_TEST_MOD utility NPC remains an ordinary friendly non-combat NPC: "
					+ fileName) && ok;
		}
	}

	std::unique_ptr<char[]> meleeContent;
	std::unique_ptr<char[]> rangedContent;
	int meleeLength = File::readFile("ini\\npc\\mod_test_hub_enemy_melee.ini", meleeContent);
	int rangedLength = File::readFile("ini\\npc\\mod_test_hub_enemy_ranged.ini", rangedContent);
	ok = check(meleeContent != nullptr && meleeLength > 0
		&& rangedContent != nullptr && rangedLength > 0,
		"XJXQY_TEST_MOD hub enemy fixtures resolve") && ok;
	if (meleeContent != nullptr && rangedContent != nullptr)
	{
		INIReader meleeIni(meleeContent);
		INIReader rangedIni(rangedContent);
		ok = check(meleeIni.GetInteger("INIT", "Relation", -1) == 1
			&& rangedIni.GetInteger("INIT", "Relation", -1) == 1
			&& meleeIni.GetInteger("INIT", "Group", -1)
				== rangedIni.GetInteger("INIT", "Group", -2),
			"XJXQY_TEST_MOD hub enemies share one hostile group and do not attack each other") && ok;
	}

	std::unique_ptr<char[]> bootstrapContent;
	int bootstrapLength = File::readFile("script\\common\\mod_test_bootstrap.txt", bootstrapContent);
	ok = check(bootstrapContent != nullptr && bootstrapLength > 0,
		"XJXQY_TEST_MOD hub bootstrap resolves") && ok;
	if (bootstrapContent != nullptr && bootstrapLength > 0)
	{
		const std::string bootstrap(bootstrapContent.get(), static_cast<std::size_t>(bootstrapLength));
		for (int trapIndex = 1; trapIndex <= 3; trapIndex++)
		{
			const std::string clearTrap = "setmaptrap(" + std::to_string(trapIndex) + ", \"\");";
			ok = check(bootstrap.find(clearTrap) != std::string::npos,
				"XJXQY_TEST_MOD hub clears inherited Hengshan trap " + std::to_string(trapIndex)) && ok;
		}
	}
	return ok;
}

bool runObjectAnimationResourceSmoke(ResourceManager& manager)
{
	bool ok = true;
	if (manager.setActiveResourcePackById("JIANGHU_YUCHEN_1_03"))
	{
		std::unique_ptr<char[]> objectContent;
		int objectLength = File::readFile("ini\\obj\\m1级钱.ini", objectContent);
		ok = check(objectContent != nullptr && objectLength > 0,
			"JIANGHU_YUCHEN_1_03 legacy animated drop object resolves") && ok;
		if (objectContent != nullptr && objectLength > 0)
		{
			INIReader objectIni(objectContent);
			std::string staticResource = objectIni.Get("Init", "ObjFile", "");
			std::string animationResource = objectIni.Get("Init", "ObjFileMovie", "");
			ok = check(objectIni.GetInteger("Init", "Kind", -1) == 8
				&& staticResource == "obj-钱1.ini"
				&& animationResource == "obj-movie-钱1.ini",
				"JIANGHU_YUCHEN_1_03 legacy drop separates static and one-shot object resources") && ok;

			std::unique_ptr<char[]> staticContent;
			std::unique_ptr<char[]> animationContent;
			int staticLength = File::readFile("ini\\objres\\" + staticResource, staticContent);
			int animationLength = File::readFile("ini\\objres\\" + animationResource, animationContent);
			ok = check(staticContent != nullptr && staticLength > 0
				&& animationContent != nullptr && animationLength > 0,
				"JIANGHU_YUCHEN_1_03 legacy static and animation objres files resolve") && ok;
			if (staticContent != nullptr && animationContent != nullptr)
			{
				INIReader staticIni(staticContent);
				INIReader animationIni(animationContent);
				ok = check(staticIni.Get("Common", "Image", "") == "other/f_money_01.asf"
					&& animationIni.Get("Common", "Image", "") == "other/money_01.asf",
					"JIANGHU_YUCHEN_1_03 ObjFileMovie is an animated object image, not a video") && ok;
			}
		}
	}

	if (manager.setActiveResourcePackById("JXQY2"))
	{
		std::unique_ptr<char[]> objectContent;
		int objectLength = File::readFile("ini\\obj\\药罐.ini", objectContent);
		ok = check(objectContent != nullptr && objectLength > 0,
			"JXQY2 object Type production sample resolves") && ok;
		if (objectContent != nullptr && objectLength > 0)
		{
			INIReader objectIni(objectContent);
			ok = check(objectIni.GetInteger("Init", "Type", 0) == 2
				&& objectIni.GetInteger("Init", "Kind", -1) == 2,
				"JXQY2 object Type remains distinct from runtime Kind") && ok;
		}
	}
	return ok;
}
}

int main(int argc, char** argv)
{
	ScopedResourceSmokeStateIsolation stateIsolation;
	if (!stateIsolation.valid())
	{
		std::cerr <<
			"FAIL: create isolated config/save parent for resource smoke test\n";
		return 1;
	}
	if (argc > 1)
	{
		std::string firstArg = argv[1];
		bool requireGambleFallbackPacks = false;
		for (int argumentIndex = 1; argumentIndex < argc; argumentIndex++)
		{
			if (std::string(argv[argumentIndex]) == "--require-gamble-fallback-packs")
			{
				requireGambleFallbackPacks = true;
			}
		}
		if (firstArg == "--help" || firstArg == "-h")
		{
			std::cout << "Usage: jxqy-resource-smoke-tests [--assets] <assets-root> [--require-gamble-fallback-packs]\n"
				<< "Without assets-root, runs a synthetic CTest fixture.\n";
			return 0;
		}
		if (firstArg == "--assets" && argc > 2)
		{
			firstArg = argv[2];
		}
		bool ok = runResourceSmoke(std::filesystem::path(firstArg));
		ok = runAuthorAttributionSmoke(ResourceManager::instance()) && ok;
		ok = runChoosePanelResourceSmoke(ResourceManager::instance()) && ok;
		ok = runMobileAndMapLayoutResourceSmoke(ResourceManager::instance()) && ok;
		ok = runYesNoPanelAlignmentSmoke(ResourceManager::instance()) && ok;
		ok = runTitleLayoutResourceSmoke(ResourceManager::instance()) && ok;
		ok = runXinyueLifeExchangeResourceSmoke(ResourceManager::instance()) && ok;
		ok = runXiaoxiangNpcLifecycleResourceSmoke(ResourceManager::instance()) && ok;
		ok = runXjxqyLegacyStoryNpcResourceSmoke(ResourceManager::instance()) && ok;
		ok = runTestModEquipmentTriggerSmoke(ResourceManager::instance()) && ok;
		ok = runTestModMagicCollisionSmoke(ResourceManager::instance()) && ok;
		ok = runTestModGoodsPricingSmoke(ResourceManager::instance()) && ok;
		ok = runTestModHubIsolationSmoke(ResourceManager::instance()) && ok;
		ok = runXjxqyGambleMenuResourceSmoke(ResourceManager::instance(), requireGambleFallbackPacks) && ok;
		ok = runObjectAnimationResourceSmoke(ResourceManager::instance()) && ok;
		return ok ? 0 : 1;
	}

	std::filesystem::path syntheticRoot = createSyntheticCollection();
	bool ok = runResourceSmoke(syntheticRoot);
	ok = runGoodsCooldownSmoke() && ok;
	std::filesystem::remove_all(syntheticRoot);
	return ok ? 0 : 1;
}
