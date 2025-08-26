#pragma once
#include "Magic.h"
#include "Effect.h"
#include <vector>

struct MagicInfo
{
	std::shared_ptr<Magic> magic = nullptr;
	std::string iniFile = u8"";
	int level = 0;
	int exp = 0;
};

class MagicManager
{
public:
	MagicManager();
	virtual ~MagicManager();

	MagicInfo* findMagic(const std::string & iniName);

	void load(int index);
	void save(int index);

	void freeResource();
	void addPracticeExp(int addexp);
	void addUseExp(std::shared_ptr<Effect> e, int addexp);
	void addMagicExp(const std::string & magicName, int addexp);
	void addMagic(const std::string & magicName);
	void deleteMagic(const std::string & magicName);
	void updateMenu(int idx);
	void updateMenu();

	void exchange(int index1, int index2);

	const int magicListLength = MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT;
	MagicInfo magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT];
	bool magicListExists(int index);

	// npc 用到的武功列表
	std::shared_ptr<Magic> loadAttackMagic(const std::string & name);
	void tryCleanAttackMagic();
private:
	std::map<std::string, std::shared_ptr<Magic>> attackMagicList;

};

