#pragma once

#include <deque>
#include <string>

namespace MemoPersistence
{
inline constexpr int MaximumLineCount = 4096;
inline constexpr int MaximumFileBytes = 16 * 1024 * 1024;
inline constexpr const char* CanonicalFileName = "memo.txt";
inline constexpr const char* CompatibleIniFileName = "memo.ini";

enum class ContentKind
{
	Memo,
	OtherIni,
	Invalid
};

bool parseText(const std::string& text, std::deque<std::string>& lines);
ContentKind classifyText(
	const std::string& text,
	std::deque<std::string>* memoLines = nullptr);
std::string serializeText(const std::deque<std::string>& lines);
}
