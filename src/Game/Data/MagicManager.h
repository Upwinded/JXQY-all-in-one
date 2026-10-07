#pragma once
#include "Magic.h"
#include "Effect.h"
#include <map>
#include <vector>

struct MagicInfo
{
	std::shared_ptr<Magic> magic = nullptr;
	std::string iniFile = "";
	int level = 0;
	int exp = 0;
	int hideCount = 0;
	int lastIndexWhenHide = 0;
	UTime remainColdMilliseconds = 0;
};

class MagicManager
{
public:
	MagicManager();
	virtual ~MagicManager();

	MagicInfo* findMagic(const std::string & iniName);
	MagicInfo* findPrimaryMagic(const std::string& iniName);

	bool load(int index, std::string* failureReason = nullptr);
	bool save(int index);

	void freeResource();
	void clearMagicList();
	void refreshPlayerMagicAttributes();
	bool hasActiveReplaceMagicList() const { return isInReplaceMagicList; }
	void replaceMagicList(const std::string& replacementList, const std::string& sourceIdentity = "");
	void stopReplaceMagicList();
	int primaryFreeIndex(bool talent = false) const;
	bool primaryMagicListExists(int index) const;
	void addPracticeExp(int addexp);
	bool addPracticeExperienceToNextLevel();
	void addUseExp(std::shared_ptr<Effect> e, int addexp);
	void addHitExp(std::shared_ptr<Effect> e, int targetLevel);
	void saveExperienceOwner(INIReader& ini, const std::string& section, const MagicExperienceOwner& owner);
	MagicExperienceOwner loadExperienceOwner(const INIReader& ini, const std::string& section);
	void addKillExp(std::shared_ptr<Effect> e, double scaledAutomaticExperience);
	void addKillExp(
		std::shared_ptr<Effect> e,
		double automaticExperience,
		float practiceFraction,
		float useFraction);
	void recordCurrentUseMagic(int listIndex);
	void finishMagicUse(const std::shared_ptr<Magic>& sourceMagic, UTime coldTime, bool recordCurrentUse);
	void addMagicExp(const std::string & magicName, int addexp);
	void addMagic(const std::string & magicName);
	MagicInfo* addPrimaryMagic(const std::string& magicName, bool showMessage, bool refreshAttributes, bool talent = false);
	MagicInfo* addEquipmentMagic(const std::string& magicName, bool showMessage, bool refreshAttributes);
	void deleteMagic(const std::string & magicName);
	void deletePrimaryMagic(const std::string& magicName);
	void clearPrimaryMagicList();
	void setPrimaryMagicLevel(const std::string& magicName, int level);
	MagicInfo* setMagicHidden(const std::string& magicName, bool hidden, bool refreshAttributes, bool updateMenus);
	bool isMagicHidden(const std::string& magicName) const;
	void updateColdTimes(UTime frameTime);
	void updateMenu(int idx);
	void updateMenu();

	void exchange(int index1, int index2);

	void configureLayout();
	int listLength() const;
	int storeBegin() const;
	int storeEnd() const;
	int bottomCount() const;
	int bottomBegin() const;
	int bottomEnd() const;
	int practiceIndex() const;
	int bottomIndex(int index) const;
	int bottomSlot(int index) const;
	int hideStartIndex() const;
	bool isStoreIndex(int index) const;
	bool isBottomIndex(int index) const;
	bool isPracticeIndex(int index) const;
	std::vector<MagicInfo> magicList;
	bool magicListExists(int index);

	// npc �õ����书�б�
	std::shared_ptr<Magic> loadAttackMagic(const std::string & name);
	void tryCleanAttackMagic();
private:
	std::vector<MagicInfo> hiddenMagicList;
	std::vector<MagicInfo> replaceMagicListBackup;
	std::map<std::string, std::vector<MagicInfo>> replaceMagicListCache;
	bool isInReplaceMagicList = false;
	std::string currentReplaceMagicListKey;
	std::map<std::string, std::shared_ptr<Magic>> attackMagicList;
	int hitExperienceLevelFactor = 0;
	float practiceKillExperienceFraction = 1.0f;
	float currentUseKillExperienceFraction = 1.0f;
	bool usesConfiguredExperienceRules = false;
	std::weak_ptr<Magic> currentUseMagic;

	std::vector<MagicInfo>& primaryMagicList();
	const std::vector<MagicInfo>& primaryMagicList() const;
	void addUseExperience(const std::string& magicFile, int addexp);
	void addUseExperience(MagicInfo& info, int addexp);
	MagicInfo* findExperienceOwner(const MagicExperienceOwner& owner,
		std::string* listKey = nullptr, int* slot = nullptr);
	bool tryAdvanceMagicLevel(MagicInfo& info);
	void addTalentJumpRadius(const MagicInfo& info);
	void loadExperienceRules();
	int hitExperienceForTargetLevel(int targetLevel) const;
};
