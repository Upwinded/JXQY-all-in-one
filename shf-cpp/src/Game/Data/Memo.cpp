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
	char * s = NULL;
	int len = 0;
	std::string fileName = MEMO_INI;
	fileName = DEFAULT_FOLDER + fileName;
	if (File::readFile(fileName, &s, &len) && s != NULL && len > 0)
	{
		INIReader ini = INIReader::INIReader(s);
		int count = ini.GetInteger("Memo", "Count", 0);
		memo.resize(count);
		for (size_t i = 0; i < memo.size(); i++)
		{
			memo[i] = ini.Get("Memo", convert::formatString("%d", i), "");
		}

		delete[] s;
	}
	//gm->memoMenu->reRange((int)memo.size() > 0 ? (int)memo.size() - 1 : 0);
}

void Memo::save()
{
	std::string m = "[Memo]\r\n";
	int count = memo.size();
	m += convert::formatString("Count=%d\r\n", count);
	for (size_t i = 0; i < memo.size(); i++)
	{
		m += convert::formatString("%d=%s\r\n", i, memo[i].c_str());
	}
	std::string fileName = MEMO_INI;
	fileName = DEFAULT_FOLDER + fileName;
	File::writeFile(fileName, (void *)m.c_str(), m.length());
}

void Memo::add(const std::string & str)
{
	if (str == "")
	{
		return;
	}
	std::string newStr = memoStrHead + str;
	std::wstring wstr = convert::GBKToUnicode(newStr);

	std::vector<std::wstring> wstrs = convert::splitWString(wstr, memoLine);
	
	for (size_t i = 0; i < wstrs.size(); i++)
	{
		memo.insert(memo.begin() + i, convert::unicodeToGBK(wstrs[i]));
	}
	gm->memoMenu->reRange((int)memo.size() > 0 ? (int)memo.size() - 1 : 0);
}

void Memo::clear()
{
	memo.clear();
	gm->memoMenu->reset();
}
