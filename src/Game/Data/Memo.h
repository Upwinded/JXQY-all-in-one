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

	// Legacy save generations may omit or contain an invalid optional memo.
	// When allowMissing is true, either case commits an empty memo; strict loads
	// still fail without changing the current memo.
	bool load(bool allowMissing = false);
	bool save();
	void add(const std::string & str);
	void remove(const std::string& str);
	void clear();
};
