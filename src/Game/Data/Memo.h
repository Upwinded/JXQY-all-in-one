#pragma once

#include "../GameTypes.h"
#include "../../libconvert/libconvert.h"
#include "../../File/File.h"
#include "../../File/INIReader.h"

#define memoLine 9
#define memoStrHead u8"●"
class Memo
{
public:
	Memo();
	virtual ~Memo();

	std::deque<std::string> memo;

	void load();
	void save();
	void add(const std::string & str);
	void clear();
};

