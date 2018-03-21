#pragma once
#include "Magic.h"
#include "Effect.h"
#include <vector>

struct MagicInfo
{
	Magic * magic = NULL;
	std::string iniFile = "";
	int level = 0;
	int exp = 0;
};

struct AttackMagic
{
	std::string name = "";
	Magic * magic = NULL;
};

class MagicManager
{
public:
	MagicManager();
	~MagicManager();

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

	MagicInfo magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT];

	Magic * loadAttackMagic(const std::string & name);
	std::vector<AttackMagic> attackMagicList = {};
};

