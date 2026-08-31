#pragma once
#include "../../File/INIReader.h"
#include "../GameTypes.h"
#include "../../libconvert/libconvert.h"

#include <set>

class Traps
{
public:
	Traps();
	explicit Traps(const std::shared_ptr<INIReader>& iniReader);
	virtual ~Traps();

	static constexpr int MinimumScriptIndex = 1;
	static constexpr int MaximumScriptIndex = 255;
	static constexpr int MaximumTriggeredIndicesFileBytes = 4096;
	static bool isValidIndex(int index);

	std::string get(const std::string & mapName, int index);
	void set(const std::string & mapName, int index, const std::string & value);
	bool hasTriggered(int index) const;
	void markTriggered(int index);
	void reactivate(int index);
	void beginMapVisit();
	bool load(std::string* failureReason = nullptr);
	bool loadInitialTemplate(std::string* failureReason = nullptr);
	bool saveDefinitions();
	bool save();
	void resetToEmpty();
	void freeResource();

private:
	void loadDefinitions();
	bool loadTriggeredIndices(
		std::set<int>& loadedIndices,
		std::string* failureReason);
	bool saveTriggeredIndices() const;
	void removeInvalidZeroKeys();
	std::shared_ptr<INIReader> ini = nullptr;
	std::set<int> triggeredIndices;
};
