#include "../Game/Data/MagicRegionFile.h"
#include "../Game/Data/Magic.h"
#include "../File/File.h"

#include <cmath>
#include <cstdio>
#include <fstream>
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

bool near(float a, float b)
{
	return std::fabs(a - b) < 0.001f;
}

bool writeTestFile(const std::string& fileName, const std::string& content)
{
	std::ofstream out(fileName, std::ios::binary | std::ios::trunc);
	out.write(content.data(), static_cast<std::streamsize>(content.size()));
	return out.good();
}
}

int main(int argc, char* argv[])
{
	if (argc == 3)
	{
		File::setAssetsCollectionRoot(argv[1]);
		File::setActiveResourceRoot(argv[1]);
		MagicRegionFile realRegionFile;
		bool ok = check(loadMagicRegionFile(argv[2], realRegionFile), "loads production tiled region file");
		size_t itemCount = 0;
		for (const auto& directionItems : realRegionFile)
		{
			itemCount += directionItems.size();
		}
		ok = check(realRegionFile.size() == 8, "production region file stores eight direction buckets") && ok;
		ok = check(itemCount > 0, "production region file contains expanded effects") && ok;
		return ok ? 0 : 1;
	}

	const std::string fileName = "magic-region-file-test.json";
	File::setAssetsCollectionRoot(".");
	bool ok = true;
	ok = check(writeTestFile(fileName,
			"{"
			"\"height\":3,"
			"\"width\":3,"
			"\"layers\":["
			"{\"name\":\"8\",\"width\":3,\"height\":3,\"data\":[0,0,0,0,9,0,0,0,0]},"
			"{\"name\":\"0\",\"width\":3,\"height\":3,\"data\":[0,1,0,0,0,2,0,0,0]},"
			"{\"name\":\"1\",\"width\":3,\"height\":3,\"data\":[0,0,0,0,0,0,0,0,0]}"
			"],"
			"\"tilesets\":[{\"firstgid\":1,\"tiles\":{\"1\":{\"animation\":[{\"duration\":100},{\"duration\":150}]}}}]"
			"}"), "writes valid tiled region fixture") && ok;

	MagicRegionFile regionFile;
	ok = check(mmkTimeStop == 23, "keeps C# time stopper MoveKind value") && ok;
	ok = check(loadMagicRegionFile(fileName, regionFile), "loads tiled region file") && ok;
	ok = check(regionFile.size() == 8, "stores eight direction buckets") && ok;
	if (regionFile.size() >= 2)
	{
		ok = check(regionFile[0].size() == 3, "expands delay animation frames") && ok;
		ok = check(regionFile[1].empty(), "keeps empty direction layer") && ok;
		if (regionFile[0].size() == 3)
		{
			ok = check(near(regionFile[0][0].offset.x, -32.0f) && near(regionFile[0][0].offset.y, -16.0f),
				"calculates staggered tile offset") && ok;
			ok = check(regionFile[0][0].delay == 0, "uses zero delay without animation") && ok;
			ok = check(near(regionFile[0][1].offset.x, 64.0f) && near(regionFile[0][1].offset.y, 0.0f),
				"calculates same-row tile offset") && ok;
			ok = check(regionFile[0][1].delay == 100 && regionFile[0][2].delay == 150,
				"copies tile animation durations") && ok;
		}
	}

	const std::string validBeginLayer =
		"{\"name\":\"8\",\"width\":1,\"height\":1,\"data\":[1]}";
	ok = check(writeTestFile(fileName,
		"{\"layers\":[{\"name\":\"8\",\"width\":1.5,\"height\":1,\"data\":[1]}]}"),
		"writes fractional-dimension fixture") && ok;
	ok = check(!loadMagicRegionFile(fileName, regionFile), "rejects fractional layer dimensions") && ok;
	ok = check(regionFile.empty(), "failed load clears the previous parsed region") && ok;

	ok = check(writeTestFile(fileName,
		"{\"layers\":[{\"name\":\"8\",\"width\":2147483648,\"height\":1,\"data\":[]}]}"),
		"writes overflowing-dimension fixture") && ok;
	ok = check(!loadMagicRegionFile(fileName, regionFile), "rejects integer fields outside int range") && ok;

	ok = check(writeTestFile(fileName,
		"{\"layers\":[{\"name\":\"8\",\"width\":513,\"height\":1,\"data\":[]}]}"),
		"writes excessive-dimension fixture") && ok;
	ok = check(!loadMagicRegionFile(fileName, regionFile), "rejects excessive layer dimensions") && ok;

	ok = check(writeTestFile(fileName,
		"{\"layers\":[" + validBeginLayer + "," + validBeginLayer + "]}"),
		"writes duplicate-layer fixture") && ok;
	ok = check(!loadMagicRegionFile(fileName, regionFile), "rejects duplicate direction layers") && ok;

	std::string deeplyNested = "{\"extra\":";
	for (int depth = 0; depth < 66; depth++)
	{
		deeplyNested += '[';
	}
	deeplyNested += '0';
	for (int depth = 0; depth < 66; depth++)
	{
		deeplyNested += ']';
	}
	deeplyNested += ",\"layers\":[" + validBeginLayer + "]}";
	ok = check(writeTestFile(fileName, deeplyNested), "writes deeply nested fixture") && ok;
	ok = check(!loadMagicRegionFile(fileName, regionFile), "rejects json beyond the parser depth budget") && ok;

	{
		std::ofstream out(fileName, std::ios::binary | std::ios::trunc);
		out.seekp(MagicRegionFileSafety::MaximumFileBytes);
		out.put('x');
		ok = check(out.good(), "writes oversized region fixture") && ok;
	}
	ok = check(!loadMagicRegionFile(fileName, regionFile), "rejects region files beyond the byte budget") && ok;

	std::remove(fileName.c_str());
	return ok ? 0 : 1;
}
