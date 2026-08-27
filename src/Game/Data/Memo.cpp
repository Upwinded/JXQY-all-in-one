#include "Memo.h"
#include "MemoPersistence.h"
#include "../../File/log.h"
#include "../../libconvert/libconvert.h"
#include "../GameManager/GameManager.h"
#include "../GameManager/SaveFileManager.h"

#include <algorithm>
#include <utility>

namespace
{
struct MemoAliasContent
{
	bool exists = false;
	MemoPersistence::ContentKind kind =
		MemoPersistence::ContentKind::Invalid;
	std::deque<std::string> lines;
};

bool readMemoAlias(
	const std::string& currentPath,
	const std::string& fileName,
	MemoAliasContent& content)
{
	content = {};
	const std::string virtualPath =
		currentPath + fileName;
	if (!File::fileExist(virtualPath))
	{
		return true;
	}
	content.exists = true;

	std::unique_ptr<char[]> data;
	int length = 0;
	if (!File::readFile(
			virtualPath,
			data,
			length,
			MemoPersistence::MaximumFileBytes) ||
		data == nullptr ||
		length <= 0)
	{
		return false;
	}
	content.kind = MemoPersistence::classifyText(
		std::string(
			data.get(),
			static_cast<std::size_t>(length)),
		&content.lines);
	return true;
}

bool inspectMemoAliases(
	const std::string& currentPath,
	MemoAliasContent& standard,
	MemoAliasContent& canonical)
{
	return readMemoAlias(
			currentPath,
			MemoPersistence::CompatibleIniFileName,
			standard) &&
		readMemoAlias(
			currentPath,
			MemoPersistence::CanonicalFileName,
			canonical);
}
}


Memo::Memo()
{
	memo.resize(0);
}

Memo::~Memo()
{
	memo.resize(0);
}

bool Memo::load(bool allowMissing)
{
	const std::string currentPath =
		SaveFileManager::CurrentPath();
	MemoAliasContent standard;
	MemoAliasContent canonical;
	if (!inspectMemoAliases(
			currentPath,
			standard,
			canonical))
	{
		return false;
	}
	if ((canonical.exists &&
			canonical.kind !=
				MemoPersistence::ContentKind::Memo) ||
		(standard.exists &&
			standard.kind ==
				MemoPersistence::ContentKind::Invalid))
	{
		GameLog::write(
			"Memo: invalid memo alias in %s\n",
			currentPath.c_str());
		return false;
	}
	if (canonical.exists)
	{
		memo = std::move(canonical.lines);
		return true;
	}
	if (standard.exists &&
		standard.kind ==
			MemoPersistence::ContentKind::Memo)
	{
		memo = std::move(standard.lines);
		return true;
	}
	if (allowMissing)
	{
		memo.clear();
		return true;
	}
	GameLog::write(
		"Memo: no semantic memo exists in %s\n",
		currentPath.c_str());
	return false;
}

bool Memo::save()
{
	const std::string currentPath =
		SaveFileManager::CurrentPath();
	MemoAliasContent standard;
	MemoAliasContent canonical;
	if (!inspectMemoAliases(
			currentPath,
			standard,
			canonical) ||
		(canonical.exists &&
			canonical.kind !=
				MemoPersistence::ContentKind::Memo) ||
		(standard.exists &&
			standard.kind ==
				MemoPersistence::ContentKind::Invalid))
	{
		return false;
	}

	// memo.txt is the only canonical runtime spelling. When importing a
	// semantic memo.ini, leave it untouched because that name is also used by
	// valid legacy object lists.
	const std::string outputFileName =
		MemoPersistence::CanonicalFileName;
	const std::string serialized =
		MemoPersistence::serializeText(memo);
	return File::writeFileChecked(
		currentPath + outputFileName,
		serialized.data(),
		static_cast<int>(serialized.size()));
}

void Memo::add(const std::string & str)
{
	if (str.empty())
	{
		return;
	}
	std::string newStr = memoStrHead + str;

	auto strs = convert::splitString(newStr, memoLine);
	
	const size_t insertCount = std::min(
		strs.size(), static_cast<size_t>(MemoPersistence::MaximumLineCount));
	for (size_t i = 0; i < insertCount; i++)
	{
		memo.insert(memo.begin() + i, strs[i]);
	}
	while (memo.size() > static_cast<size_t>(MemoPersistence::MaximumLineCount))
	{
		memo.pop_back();
	}
	if (gm != nullptr && gm->menu != nullptr && gm->menu->memoMenu != nullptr)
	{
		gm->menu->memoMenu->reRange((int)memo.size() > 0 ? (int)memo.size() - 1 : 0);
	}
}

void Memo::remove(const std::string& str)
{
	if (str.empty())
	{
		return;
	}
	std::string target = memoStrHead + str;
	auto lines = convert::splitString(target, memoLine);
	if (lines.empty() || lines.size() > memo.size())
	{
		return;
	}
	for (size_t i = 0; i + lines.size() <= memo.size(); i++)
	{
		bool matched = true;
		for (size_t j = 0; j < lines.size(); j++)
		{
			if (memo[i + j] != lines[j])
			{
				matched = false;
				break;
			}
		}
		if (matched)
		{
			memo.erase(memo.begin() + i, memo.begin() + i + lines.size());
			if (gm != nullptr && gm->menu != nullptr && gm->menu->memoMenu != nullptr)
			{
				gm->menu->memoMenu->reRange((int)memo.size() > 0 ? (int)memo.size() - 1 : 0);
			}
			return;
		}
	}
}

void Memo::clear()
{
	memo.clear();
	if (gm != nullptr && gm->menu != nullptr && gm->menu->memoMenu != nullptr)
	{
		gm->menu->memoMenu->reset();
	}
}
