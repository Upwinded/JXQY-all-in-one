#pragma once
#include "Magic.h"
#include "Effect.h"
#include <vector>

struct MagicInfo
{
	Magic * magic = nullptr;
	std::string iniFile = "";
	int level = 0;
	int exp = 0;
};

struct AttackMagic
{
	std::string name = "";
	Magic * magic = nullptr;
};

class MagicManager
{
public:
	MagicManager();
	virtual ~MagicManager();

	MagicInfo * findMagic(const std::string & iniName);

	void load();
	void save();

	void freeResource();
	void addPracticeExp(int addexp);
	void addUseExp(Effect * e, int addexp);
	void addMagicExp(const std::string & magicName, int addexp);
	void addMagic(const std::string & magicName);
	void deleteMagic(const std::string & magicName);
	void updateMenu(int idx);
	void updateMenu();

	void exchange(int index1, int index2);

	const int magicListLength = MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT;
	MagicInfo magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT];
	bool magicListExists(int index);

	Magic * loadAttackMagic(const std::string & name);
	std::vector<AttackMagic> attackMagicList;
};

