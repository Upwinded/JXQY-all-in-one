#pragma once
#include "../../File/INIReader.h"
#include "../GameTypes.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace EditorRun
{
class RuntimeTraceWriter;
enum class RuntimeTraceVariableValueType;
}

class VariableList
{
public:
	VariableList();
	virtual ~VariableList();

	bool load(std::string* failureReason = nullptr);
	void ensureInitialized();
	bool save();
	void clearExcept(const std::vector<std::string>& keepNames);

	std::string get(const std::string & name);
	int getInteger(const std::string & name);	
	float getReal(const std::string & name);
	bool getBoolean(const std::string & name);

	void set(const std::string & name, std::string & value);
	void setInteger(const std::string & name, int value);
	void setReal(const std::string & name, float value);
	void setBoolean(const std::string & name, bool value);
	void setRuntimeTraceContext(
		EditorRun::RuntimeTraceWriter* runtimeTraceWriter,
		std::function<std::uint64_t()> currentExecutionId);

private:
	void enqueueChange(
		const std::string& name,
		EditorRun::RuntimeTraceVariableValueType valueType,
		const std::string& beforeValue,
		const std::string& afterValue,
		bool storageChanged = false);
	void freeResource();
	std::shared_ptr<INIReader> ini = nullptr;
	EditorRun::RuntimeTraceWriter* runtimeTraceWriter = nullptr;
	std::function<std::uint64_t()> currentExecutionId;
};
