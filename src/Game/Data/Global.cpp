#include "Global.h"
#include "../../libconvert/libconvert.h"
#include "ColorStyle.h"
#include "../../File/log.h"
#include "../../Resource/ResourceManager.h"
#include "../../Resource/ResourceManifest.h"
#include "../GameManager/SaveFileManager.h"
#include <algorithm>
#include <set>


//Global Global::global;
//Global * Global::this_ = &Global::global;

namespace
{
constexpr char UI_SETTINGS_INI[] = "ini\\ui\\ui_settings.ini";

constexpr int TRILOGY_EXTENDED_GOODS_STORE_BEGIN = 0;
constexpr int TRILOGY_EXTENDED_GOODS_STORE_END = 197;
constexpr int TRILOGY_EXTENDED_GOODS_EQUIP_BEGIN = 200;
constexpr int TRILOGY_EXTENDED_GOODS_EQUIP_END = 206;
constexpr int TRILOGY_EXTENDED_GOODS_BOTTOM_BEGIN = 220;
constexpr int TRILOGY_EXTENDED_GOODS_BOTTOM_END = 222;

constexpr int TRILOGY_EXTENDED_MAGIC_STORE_BEGIN = 0;
constexpr int TRILOGY_EXTENDED_MAGIC_STORE_END = 35;
constexpr int TRILOGY_EXTENDED_MAGIC_BOTTOM_BEGIN = 39;
constexpr int TRILOGY_EXTENDED_MAGIC_BOTTOM_END = 43;
constexpr int TRILOGY_EXTENDED_MAGIC_PRACTICE_INDEX = 48;
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

void resetUiProfileFeatures(GameFeatureFlags& feature)
{
	feature.extendedInventoryLayout = false;
	feature.topButtonsLayout = false;
	feature.stateEquipIntegratedLayout = false;
	feature.hideRightMenusWithIntegratedEquip = false;
	feature.practiceMenuDisabled = false;
	feature.magicButtonOpensIntegratedEquip = false;
	feature.equipPlayerNameImages = false;
	feature.characterPanelImages = false;
	feature.partnerHeadMenu = false;
	feature.largeMenuImages = false;
	feature.menuResourceProfile = mrpDefault;
	feature.mapThumbnailLayout = mtlpDefault;
}

bool applyUiProfileFeatures(const std::string& profileName, GameFeatureFlags& feature)
{
	std::string profile = toLowerAscii(profileName);
	if (profile != "jxqy2" && profile != "yycs" && profile != "xjxqy")
	{
		return false;
	}

	resetUiProfileFeatures(feature);
	if (profile == "yycs")
	{
		feature.extendedInventoryLayout = true;
		feature.topButtonsLayout = true;
		feature.characterPanelImages = true;
		feature.partnerHeadMenu = true;
		feature.largeMenuImages = true;
		feature.menuResourceProfile = mrpYycs;
		feature.mapThumbnailLayout = mtlpYycs;
	}
	else if (profile == "xjxqy")
	{
		feature.extendedInventoryLayout = true;
		feature.topButtonsLayout = true;
		feature.stateEquipIntegratedLayout = true;
		feature.hideRightMenusWithIntegratedEquip = true;
		feature.practiceMenuDisabled = true;
		feature.magicButtonOpensIntegratedEquip = true;
		feature.equipPlayerNameImages = true;
		feature.partnerHeadMenu = true;
		feature.menuResourceProfile = mrpXjxqy;
		feature.mapThumbnailLayout = mtlpXjxqy;
	}
	return true;
}

void applyBooleanFeatureOverride(const ResourceManifest& manifest, const std::string& featureName, bool& value)
{
	auto feature = manifest.features.find(toLowerAscii(featureName));
	if (feature != manifest.features.end())
	{
		value = feature->second;
	}
}

int normalizeOneBasedIndex(long value, int fallback)
{
	if (value <= 0)
	{
		return fallback;
	}
	return static_cast<int>(value - 1);
}

int readOneBasedIndex(INIReader& ini, const std::string& section, const std::string& key, int fallback)
{
	return normalizeOneBasedIndex(ini.GetInteger(section, key, fallback + 1), fallback);
}

int safeRangeCount(int begin, int end)
{
	return end >= begin ? end - begin + 1 : 0;
}

bool isValidRange(int begin, int end)
{
	return begin >= 0 && end >= begin;
}

bool rangeContains(int begin, int end, int index)
{
	return index >= begin && index <= end;
}

bool rangesOverlap(int begin1, int end1, int begin2, int end2)
{
	return begin1 <= end2 && begin2 <= end1;
}

GoodsListLayout makeDefaultGoodsLayout(const GameFeatureFlags& feature)
{
	GoodsListLayout layout;
	if (feature.extendedInventoryLayout)
	{
		layout.listType = 0;
		layout.storeBegin = TRILOGY_EXTENDED_GOODS_STORE_BEGIN;
		layout.storeEnd = TRILOGY_EXTENDED_GOODS_STORE_END;
		layout.equipBegin = TRILOGY_EXTENDED_GOODS_EQUIP_BEGIN;
		layout.equipEnd = TRILOGY_EXTENDED_GOODS_EQUIP_END;
		layout.bottomBegin = TRILOGY_EXTENDED_GOODS_BOTTOM_BEGIN;
		layout.bottomEnd = TRILOGY_EXTENDED_GOODS_BOTTOM_END;
	}
	return layout;
}

MagicListLayout makeDefaultMagicLayout(const GameFeatureFlags& feature)
{
	MagicListLayout layout;
	if (feature.extendedInventoryLayout)
	{
		layout.storeBegin = TRILOGY_EXTENDED_MAGIC_STORE_BEGIN;
		layout.storeEnd = TRILOGY_EXTENDED_MAGIC_STORE_END;
		layout.bottomBegin = TRILOGY_EXTENDED_MAGIC_BOTTOM_BEGIN;
		layout.bottomEnd = TRILOGY_EXTENDED_MAGIC_BOTTOM_END;
		layout.practiceIndex = TRILOGY_EXTENDED_MAGIC_PRACTICE_INDEX;
	}
	return layout;
}

bool isValidGoodsLayout(const GoodsListLayout& layout)
{
	if (!isValidRange(layout.storeBegin, layout.storeEnd) ||
		!isValidRange(layout.bottomBegin, layout.bottomEnd) ||
		!isValidRange(layout.equipBegin, layout.equipEnd))
	{
		return false;
	}
	if (rangesOverlap(layout.storeBegin, layout.storeEnd, layout.bottomBegin, layout.bottomEnd) ||
		rangesOverlap(layout.storeBegin, layout.storeEnd, layout.equipBegin, layout.equipEnd) ||
		rangesOverlap(layout.bottomBegin, layout.bottomEnd, layout.equipBegin, layout.equipEnd))
	{
		return false;
	}
	return layout.listLength() > 0;
}

bool isValidMagicLayout(const MagicListLayout& layout)
{
	if (!isValidRange(layout.storeBegin, layout.storeEnd) ||
		!isValidRange(layout.bottomBegin, layout.bottomEnd) ||
		layout.practiceIndex < 0)
	{
		return false;
	}
	if (rangesOverlap(layout.storeBegin, layout.storeEnd, layout.bottomBegin, layout.bottomEnd) ||
		rangeContains(layout.storeBegin, layout.storeEnd, layout.practiceIndex) ||
		rangeContains(layout.bottomBegin, layout.bottomEnd, layout.practiceIndex))
	{
		return false;
	}
	return layout.listLength() > 0;
}
}

int GoodsListLayout::storeCount() const
{
	return safeRangeCount(storeBegin, storeEnd);
}

int GoodsListLayout::bottomCount() const
{
	return safeRangeCount(bottomBegin, bottomEnd);
}

int GoodsListLayout::equipCount() const
{
	return safeRangeCount(equipBegin, equipEnd);
}

int GoodsListLayout::listLength() const
{
	return std::max({ storeEnd, bottomEnd, equipEnd }) + 1;
}

bool GoodsListLayout::isStoreIndex(int index) const
{
	return index >= storeBegin && index <= storeEnd;
}

bool GoodsListLayout::isBottomIndex(int index) const
{
	return index >= bottomBegin && index <= bottomEnd;
}

bool GoodsListLayout::isEquipIndex(int index) const
{
	return index >= equipBegin && index <= equipEnd;
}

int GoodsListLayout::bottomIndex(int index) const
{
	return bottomBegin + index;
}

int GoodsListLayout::equipIndex(int index) const
{
	return equipBegin + index;
}

int GoodsListLayout::bottomSlot(int index) const
{
	return index - bottomBegin;
}

int GoodsListLayout::equipSlot(int index) const
{
	return index - equipBegin;
}

int MagicListLayout::storeCount() const
{
	return safeRangeCount(storeBegin, storeEnd);
}

int MagicListLayout::bottomCount() const
{
	return safeRangeCount(bottomBegin, bottomEnd);
}

int MagicListLayout::listLength() const
{
	return std::max({ storeEnd, bottomEnd, practiceIndex }) + 1;
}

bool MagicListLayout::isStoreIndex(int index) const
{
	return index >= storeBegin && index <= storeEnd;
}

bool MagicListLayout::isBottomIndex(int index) const
{
	return index >= bottomBegin && index <= bottomEnd;
}

bool MagicListLayout::isPracticeIndex(int index) const
{
	return index == practiceIndex;
}

int MagicListLayout::bottomIndex(int index) const
{
	return bottomBegin + index;
}

int MagicListLayout::bottomSlot(int index) const
{
	return index - bottomBegin;
}

Global::Global()
{
}


Global::~Global()
{
}

int Global::getPartnerFollowRadius() const
{
	return partnerFollowRadius;
}

int Global::getPartnerFollowRunRadius() const
{
	return partnerFollowRunRadius;
}

void Global::applyResourceManifestFeatures(const ResourceManifest& manifest)
{
	feature = GameFeatureFlags();
	minimumMagicDamage = manifest.resolvedMinimumMagicDamage();
	magicEffectCalculationMode =
		manifest.resolvedMagicEffectCalculationMode();
	partnerFollowRadius = manifest.resolvedPartnerFollowRadius();
	partnerFollowRunRadius = manifest.resolvedPartnerFollowRunRadius();
	levelUpThresholdMode = manifest.resolvedLevelUpThresholdMode();
	npcActionProfile = manifest.resolvedNpcActionProfile();
	npcRuntimeProfile = manifest.resolvedNpcRuntimeProfile();
	specialActionMode = manifest.resolvedSpecialActionMode();
	addLifeMode = manifest.resolvedAddLifeMode();

	if (!manifest.uiProfile.empty())
	{
		if (!applyUiProfileFeatures(manifest.uiProfile, feature))
		{
			GameLog::write("Global: unknown UI.Profile %s; keeping universal layout defaults\n",
				manifest.uiProfile.c_str());
		}
	}

	applyBooleanFeatureOverride(manifest, "FreezeVisualEffect", feature.freezeVisualEffect);
	applyBooleanFeatureOverride(manifest, "PoisonVisualEffect", feature.poisonVisualEffect);
	applyBooleanFeatureOverride(manifest, "PetrifyVisualEffect", feature.petrifyVisualEffect);
	applyBooleanFeatureOverride(manifest, "MagicTriggerAtAnimationEnd", feature.magicTriggerAtAnimationEnd);
	applyBooleanFeatureOverride(manifest, "LumAsBrightness", feature.lumAsBrightness);
	applyBooleanFeatureOverride(manifest, "AmbientLumOverlay", feature.ambientLumOverlay);
	applyBooleanFeatureOverride(manifest, "TopButtonsLayout", feature.topButtonsLayout);
	applyBooleanFeatureOverride(manifest, "ExtendedInventoryLayout", feature.extendedInventoryLayout);
	applyBooleanFeatureOverride(manifest, "StateEquipIntegratedLayout", feature.stateEquipIntegratedLayout);
	applyBooleanFeatureOverride(manifest, "HideRightMenusWithIntegratedEquip", feature.hideRightMenusWithIntegratedEquip);
	applyBooleanFeatureOverride(manifest, "PracticeMenuDisabled", feature.practiceMenuDisabled);
	applyBooleanFeatureOverride(manifest, "MagicButtonOpensIntegratedEquip", feature.magicButtonOpensIntegratedEquip);
	applyBooleanFeatureOverride(manifest, "EquipPlayerNameImages", feature.equipPlayerNameImages);
	applyBooleanFeatureOverride(manifest, "CharacterPanelImages", feature.characterPanelImages);
	applyBooleanFeatureOverride(manifest, "PartnerHeadMenu", feature.partnerHeadMenu);
	applyBooleanFeatureOverride(manifest, "LargeMenuImages", feature.largeMenuImages);
	applyBooleanFeatureOverride(manifest, "RageSystem", feature.rageSystem);
	applyBooleanFeatureOverride(manifest, "RainSceneTint", feature.rainSceneTint);
	// 保持布局依赖的闭包，避免单个开关拼出不可到达的半套 UI。
	if (feature.stateEquipIntegratedLayout)
	{
		feature.topButtonsLayout = true;
	}
	else
	{
		feature.hideRightMenusWithIntegratedEquip = false;
		feature.magicButtonOpensIntegratedEquip = false;
	}

	static const std::set<std::string> knownBooleanFeatures = {
		"freezevisualeffect",
		"poisonvisualeffect",
		"petrifyvisualeffect",
		"magictriggeratanimationend",
		"lumasbrightness",
		"ambientlumoverlay",
		"topbuttonslayout",
		"extendedinventorylayout",
		"stateequipintegratedlayout",
		"hiderightmenuswithintegratedequip",
		"practicemenudisabled",
		"magicbuttonopensintegratedequip",
		"equipplayernameimages",
		"characterpanelimages",
		"partnerheadmenu",
		"largemenuimages",
		"ragesystem",
		"rainscenetint"
	};
	for (const auto& manifestFeature : manifest.features)
	{
		if (knownBooleanFeatures.find(manifestFeature.first) == knownBooleanFeatures.end())
		{
			GameLog::write("Global: unknown manifest feature %s ignored\n", manifestFeature.first.c_str());
		}
	}
}

void Global::loadUiSettings()
{
	GoodsListLayout defaultGoodsLayout = makeDefaultGoodsLayout(feature);
	MagicListLayout defaultMagicLayout = makeDefaultMagicLayout(feature);
	goodsLayout = defaultGoodsLayout;
	magicLayout = defaultMagicLayout;

	INIReader ini(UI_SETTINGS_INI);
	if (ini.ParseError() != 0)
	{
		return;
	}

	goodsLayout.listType = ini.GetInteger("GoodsInit", "GoodsListType", goodsLayout.listType);
	goodsLayout.storeBegin = readOneBasedIndex(ini, "GoodsInit", "StoreIndexBegin", goodsLayout.storeBegin);
	goodsLayout.storeEnd = readOneBasedIndex(ini, "GoodsInit", "StoreIndexEnd", goodsLayout.storeEnd);
	goodsLayout.equipBegin = readOneBasedIndex(ini, "GoodsInit", "EquipIndexBegin", goodsLayout.equipBegin);
	goodsLayout.equipEnd = readOneBasedIndex(ini, "GoodsInit", "EquipIndexEnd", goodsLayout.equipEnd);
	goodsLayout.bottomBegin = readOneBasedIndex(ini, "GoodsInit", "BottomIndexBegin", goodsLayout.bottomBegin);
	goodsLayout.bottomEnd = readOneBasedIndex(ini, "GoodsInit", "BottomIndexEnd", goodsLayout.bottomEnd);

	magicLayout.storeBegin = readOneBasedIndex(ini, "MagicInit", "StoreIndexBegin", magicLayout.storeBegin);
	magicLayout.storeEnd = readOneBasedIndex(ini, "MagicInit", "StoreIndexEnd", magicLayout.storeEnd);
	magicLayout.bottomBegin = readOneBasedIndex(ini, "MagicInit", "BottomIndexBegin", magicLayout.bottomBegin);
	magicLayout.bottomEnd = readOneBasedIndex(ini, "MagicInit", "BottomIndexEnd", magicLayout.bottomEnd);
	magicLayout.practiceIndex = readOneBasedIndex(ini, "MagicInit", "XiuLianIndex", magicLayout.practiceIndex);
	magicLayout.hideStartIndex = ini.GetInteger("MagicInit", "HideStartIndex", magicLayout.hideStartIndex);

	if (!isValidGoodsLayout(goodsLayout))
	{
		goodsLayout = defaultGoodsLayout;
	}
	if (!isValidMagicLayout(magicLayout))
	{
		magicLayout = defaultMagicLayout;
	}
}

bool Global::load()
{
	const std::string fileName =
		SaveFileManager::CurrentPath() + GLOBAL_INI;
	INIReader ini(fileName);
	if (ini.ParseError() != 0)
	{
		GameLog::write(
			"Global: invalid game state INI %s\n",
			fileName.c_str());
		return false;
	}

	const ResourceManifest& manifest =
		ResourceManager::instance().getActiveManifest();
	useWav = manifest.useWav;
	applyResourceManifestFeatures(manifest);

	data.mapName = ini.Get("State", "Map", "");
	data.npcName = ini.Get("State", "Npc", "");
	data.objName = ini.Get("State", "Obj", "");
	data.bgmName = ini.Get("State", "Bgm", "");

    data.characterIndex = ini.GetInteger("State", "Chr", -1);

    data.asfStyle = ini.GetColor("Option", "AsfStyle", 0xFFFFFF);
    data.mpcStyle = ini.GetColor("Option", "MpcStyle", 0xFFFFFF);
	const std::string legacyAsfStyle = ini.Get("Option", "AsfStyle", "");
	const std::string legacyMpcStyle = ini.Get("Option", "MpcStyle", "");
	if ((data.asfStyle & 0x00FFFFFF) == 0 &&
		convert::splitString(legacyAsfStyle, ",").size() < 4)
	{
		data.asfStyle = ColorStyle::Grayscale;
	}
	if ((data.mpcStyle & 0x00FFFFFF) == 0 &&
		convert::splitString(legacyMpcStyle, ",").size() < 4)
	{
		data.mpcStyle = ColorStyle::Grayscale;
	}
    
    data.mainLum = ini.GetInteger("Option", "MainLum", 31);
	data.fadeLum = ini.GetInteger("Option", "FadeLum", 0);
	data.mapTime = ini.GetInteger("Option", "mapTime", mtDay);
	data.snowShow = ini.GetBoolean("Option", "SnowShow", false);
	data.rainShow = ini.GetBoolean("Option", "RainShow", false);
	data.NPCAI = ini.GetBoolean("Option", "NPCAI", true);
	data.PartnerCombat = ini.GetBoolean("Option", "PartnerCombat", false);
	data.canInput = ini.GetBoolean("Option", "CanInput", true);
	data.saveDisabled = ini.GetBoolean("Option", "SaveDisabled", false);
	data.dropDisabled = ini.GetBoolean("Option", "DropDisabled", false);
	data.scriptShowMapPos = ini.GetBoolean("Option", "ScriptShowMapPos", false);
    
    data.waterEffect = ini.GetBoolean("Option", "Water", false);
	data.rainFile = ini.Get("Option", "RainFile", "");

	return true;
}
bool Global::save()
{
	std::string fileName =
		SaveFileManager::CurrentPath() + GLOBAL_INI;
	// global.ini contains only mutable run state. Static resource behavior is
	// rebuilt from game_profile.ini whenever a game or save is loaded.
	INIReader ini;

	ini.Set("State", "Map", data.mapName);
	ini.Set("State", "Npc", data.npcName);
	ini.Set("State", "Obj", data.objName);
	ini.Set("State", "Bgm", data.bgmName);
    
    ini.SetInteger("State", "Chr", data.characterIndex);
    
    ini.SetColor("Option", "AsfStyle", data.asfStyle);
    ini.SetColor("Option", "MpcStyle", data.mpcStyle);

	ini.SetInteger("Option", "MainLum", data.mainLum);
	ini.SetInteger("Option", "FadeLum", data.fadeLum);
	ini.SetInteger("Option", "mapTime", data.mapTime);
	ini.SetBoolean("Option", "SnowShow", data.snowShow);
	ini.SetBoolean("Option", "RainShow", data.rainShow);
	ini.SetBoolean("Option", "NPCAI", data.NPCAI);
	ini.SetBoolean("Option", "PartnerCombat", data.PartnerCombat);
	ini.SetBoolean("Option", "CanInput", data.canInput);
	ini.SetBoolean("Option", "SaveDisabled", data.saveDisabled);
	ini.SetBoolean("Option", "DropDisabled", data.dropDisabled);
	ini.SetBoolean("Option", "ScriptShowMapPos", data.scriptShowMapPos);
    
    ini.SetBoolean("Option", "Water", data.waterEffect);
    ini.Set("Option", "RainFile", data.rainFile);

	return ini.saveToFile(fileName);
}
/*
Global * Global::getInstance()
{
	return this_;
}
*/
