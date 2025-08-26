#include "Memo.h"
#include "../GameManager/GameManager.h"


Memo::Memo()
{
	memo.resize(0);
}

Memo::~Memo()
{
	memo.resize(0);
}

void Memo::load()
{
	std::unique_ptr<char[]> s;
	int len = 0;
	std::string fileName = MEMO_INI;
	fileName = SAVE_CURRENT_FOLDER + fileName;
	if (File::readFile(fileName, s, len) && s != nullptr && len > 0)
	{
		INIReader ini(s);
		int count = ini.GetInteger("Memo", u8"Count", 0);
		memo.resize(count);
		for (size_t i = 0; i < memo.size(); i++)
		{
			memo[i] = ini.Get("Memo", convert::formatString("%d", i), u8"");
		}
	}
	//gm->memoMenu->reRange((int)memo.size() > 0 ? (int)memo.size() - 1 : 0);
}

void Memo::save()
{
	std::string m = u8"[Memo]\r\n";
	int count = memo.size();
	m += convert::formatString("Count=%d\r\n", count);
	for (size_t i = 0; i < memo.size(); i++)
	{
		m += convert::formatString("%d=%s\r\n", i, memo[i].c_str());
	}
	std::string fileName = MEMO_INI;
	fileName = SAVE_CURRENT_FOLDER + fileName;
	File::writeFile(fileName, (void *)m.c_str(), m.length());
}

void Memo::add(const std::string & str)
{
	if (str == u8"")
	{
		return;
	}
	std::string newStr = memoStrHead + str;

	auto strs = convert::splitString(newStr, memoLine);
	
	for (size_t i = 0; i < strs.size(); i++)
	{
		memo.insert(memo.begin() + i, strs[i]);
	}
	gm->menu->memoMenu->reRange((int)memo.size() > 0 ? (int)memo.size() - 1 : 0);
}

void Memo::clear()
{
	memo.clear();
	gm->menu->memoMenu->reset();
}
