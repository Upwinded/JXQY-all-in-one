#pragma once

#include <string>
#include <vector>
#include "../GameTypes.h"
#include "../../File/INIReader.h"
#include "../../Resource/ResourceManifest.h"

enum MapTime
{
	mtDay = 0,
	mtNight = 1,
	mtDusk = 2,
	mtDawn = 3,
};

struct GlobalData
{
	int state = gsNone;

	std::string mapName = "";
	std::string npcName = "";
	std::string objName = "";
	std::string bgmName = "";

	int characterIndex = -1;

	uint32_t asfStyle = 0xFFFFFF;
	uint32_t mpcStyle = 0xFFFFFF;

	uint32_t mainLum = 31;
	uint32_t fadeLum = 0;
	int mapTime = mtDay;

	std::string rainFile = "";
	bool waterEffect = false;
	bool snowShow = false;
	bool rainShow = false;

	bool NPCAI = true;
	bool PartnerCombat = false;
	bool canInput = true;
	bool saveDisabled = false;
	bool dropDisabled = false;
	bool scriptShowMapPos = false;

};

#define GAME_JXQY2 0
#define GAME_YYCS 1
#define GAME_XJXQY 2
#define GAME_CUSTOM 3

enum MenuResourceProfile
{
	mrpDefault = 0,
	mrpYycs = 1,
	mrpXjxqy = 2,
};

enum MapThumbnailLayoutProfile
{
	mtlpDefault = 0,
	mtlpYycs = 1,
	mtlpXjxqy = 2,
};

struct GameFeatureFlags
{
	bool freezeVisualEffect = true;
	bool poisonVisualEffect = true;
	bool petrifyVisualEffect = true;
	bool magicTriggerAtAnimationEnd = false;
	bool lumAsBrightness = true;
	bool ambientLumOverlay = true;
	bool topButtonsLayout = false;
	bool extendedInventoryLayout = false;
	bool stateEquipIntegratedLayout = false;
	bool hideRightMenusWithIntegratedEquip = false;
	bool practiceMenuDisabled = false;
	bool magicButtonOpensIntegratedEquip = false;
	bool equipPlayerNameImages = false;
	bool characterPanelImages = false;
	bool partnerHeadMenu = false;
	bool largeMenuImages = false;
	bool rageSystem = false;
	bool rainSceneTint = false;
	int menuResourceProfile = mrpDefault;
	int mapThumbnailLayout = mtlpDefault;
};

struct GoodsListLayout
{
	int listType = 0;
	int storeBegin = 0;
	int storeEnd = GOODS_COUNT - 1;
	int bottomBegin = GOODS_COUNT;
	int bottomEnd = GOODS_COUNT + GOODS_TOOLBAR_COUNT - 1;
	int equipBegin = GOODS_COUNT + GOODS_TOOLBAR_COUNT;
	int equipEnd = GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT - 1;

	int storeCount() const;
	int bottomCount() const;
	int equipCount() const;
	int listLength() const;
	bool isStoreIndex(int index) const;
	bool isBottomIndex(int index) const;
	bool isEquipIndex(int index) const;
	int bottomIndex(int index) const;
	int equipIndex(int index) const;
	int bottomSlot(int index) const;
	int equipSlot(int index) const;
};

struct MagicListLayout
{
	int storeBegin = 0;
	int storeEnd = MAGIC_COUNT - 1;
	int bottomBegin = MAGIC_COUNT;
	int bottomEnd = MAGIC_COUNT + MAGIC_TOOLBAR_COUNT - 1;
	int practiceIndex = MAGIC_COUNT + MAGIC_TOOLBAR_COUNT;
	int hideStartIndex = 1000;

	int storeCount() const;
	int bottomCount() const;
	int listLength() const;
	bool isStoreIndex(int index) const;
	bool isBottomIndex(int index) const;
	bool isPracticeIndex(int index) const;
	int bottomIndex(int index) const;
	int bottomSlot(int index) const;
};

class Global
{
public:
	Global();
	virtual ~Global();

	bool useWav = false;
	int minimumMagicDamage = 10;
	MagicEffectCalculationMode magicEffectCalculationMode =
		MagicEffectCalculationMode::ReplaceAttack;
	int partnerFollowRadius = NPC_FOLLOW_RADIUS;
	int partnerFollowRunRadius = NPC_FOLLOW_RADIUS_RUN;
	LevelUpThresholdMode levelUpThresholdMode =
		LevelUpThresholdMode::GreaterThanOrEqual;
	ScriptNpcActionProfile npcActionProfile =
		ScriptNpcActionProfile::Legacy;
	ScriptNpcRuntimeProfile npcRuntimeProfile =
		ScriptNpcRuntimeProfile::Legacy;
	ScriptSpecialActionMode specialActionMode =
		ScriptSpecialActionMode::Replace;
	ScriptAddLifeMode addLifeMode =
		ScriptAddLifeMode::PlayerRules;

	GameFeatureFlags feature;
	GoodsListLayout goodsLayout;
	MagicListLayout magicLayout;
	GlobalData data;

	// 覆盖顺序：通用默认值 -> UI.Profile 布局族 -> [Features] 显式开关。
	void applyResourceManifestFeatures(const ResourceManifest& manifest);
	int getPartnerFollowRadius() const;
	int getPartnerFollowRunRadius() const;
	void loadUiSettings();
	bool load();
	bool save();

};
