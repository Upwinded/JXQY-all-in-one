#include "../Game/Data/MemoPersistence.h"

#include <deque>
#include <iostream>
#include <string>

namespace
{
bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}
}

int main()
{
	bool ok = true;
	std::deque<std::string> lines;
	const std::string valid =
		"[Memo]\n"
		"Count=2\n"
		"0=first\n"
		"1=second\n";
	ok = check(MemoPersistence::parseText(valid, lines) &&
		lines.size() == 2 && lines[0] == "first" && lines[1] == "second",
		"valid memo text parses in order") && ok;

	const std::string serialized = MemoPersistence::serializeText(lines);
	std::deque<std::string> reparsed;
	ok = check(MemoPersistence::parseText(serialized, reparsed) && reparsed == lines,
		"memo serialization roundtrips") && ok;

	const std::deque<std::string> unsafeLines =
	{
		"first\nsecond",
		std::string("third\0fourth", 12)
	};
	const std::deque<std::string> sanitizedLines =
	{
		"first second",
		"third fourth"
	};
	ok = check(
		MemoPersistence::parseText(
			MemoPersistence::serializeText(unsafeLines),
			reparsed) &&
			reparsed == sanitizedLines,
		"memo serialization replaces line breaks and embedded NUL bytes") && ok;

	for (const std::string& invalidCount : {
		"-1", "4097", "2147483647", "2junk", ""})
	{
		std::deque<std::string> rejected = { "stale" };
		const std::string text = "[Memo]\nCount=" + invalidCount + "\n0=value\n";
		ok = check(!MemoPersistence::parseText(text, rejected) && rejected.empty(),
			"invalid or excessive memo count is rejected without stale lines") && ok;
	}

	std::deque<std::string> classifiedLines;
	ok = check(
		MemoPersistence::classifyText(
			valid,
			&classifiedLines) ==
				MemoPersistence::ContentKind::Memo &&
			classifiedLines == lines,
		"valid Memo schema is classified with parsed lines") && ok;
	ok = check(
		MemoPersistence::classifyText(
			"[Head]\nCount=1\n[OBJ000]\nName=object\n",
			&classifiedLines) ==
				MemoPersistence::ContentKind::OtherIni &&
			classifiedLines.empty(),
		"valid non-Memo INI remains available for legacy object lists") && ok;
	ok = check(
		MemoPersistence::classifyText(
			"[Memo]\nCount=2junk\n") ==
				MemoPersistence::ContentKind::Invalid,
		"a declared malformed Memo schema is invalid") && ok;
	ok = check(
		MemoPersistence::classifyText(
			"[Head\nCount=1\n") ==
				MemoPersistence::ContentKind::Invalid,
		"malformed non-Memo INI is invalid") && ok;
	std::string embeddedNull =
		"[Memo]\nCount=0\n";
	embeddedNull.push_back('\0');
	embeddedNull += "[Head]\nCount=0\n";
	ok = check(
		MemoPersistence::classifyText(
			embeddedNull) ==
				MemoPersistence::ContentKind::Invalid,
		"embedded NUL content is invalid") && ok;
	ok = check(
		MemoPersistence::classifyText(
			"[Memo]\nCount=0\n") ==
				MemoPersistence::ContentKind::Memo,
		"an empty semantic memo remains valid") && ok;

	return ok ? 0 : 1;
}
