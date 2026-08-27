#pragma once

#include "../GameTypes.h"
#include "../../libconvert/libconvert.h"
#include "../../File/File.h"
#include "../../File/INIReader.h"

#define memoLine 9
#define memoStrHead "●"
class Memo
{
public:
	Memo();
	virtual ~Memo();

	std::deque<std::string> memo;

	// Legacy save generations may omit both memo aliases. When allowMissing is
	// true that case commits an empty memo, while an existing unreadable or
	// malformed alias still fails without changing the current memo.
	bool load(bool allowMissing = false);
	bool save();
	void add(const std::string & str);
	void remove(const std::string& str);
	void clear();
};
