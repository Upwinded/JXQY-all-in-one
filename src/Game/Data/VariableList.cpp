#include "VariableList.h"
#include "SaveIniPersistence.h"
#include "../../Launch/EditorRunRuntimeTraceWriter.h"
#include "../GameManager/SaveFileManager.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

namespace
{
std::string normalizedVariableName(std::string name)
{
	for (char& character : name)
	{
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(
				character + ('a' - 'A'));
		}
	}
	return name;
}

std::map<std::string, std::string> variableSnapshot(
	const std::shared_ptr<INIReader>& ini)
{
	std::map<std::string, std::string> result;
	if (ini == nullptr)
	{
		return result;
	}
	for (const std::string& name :
		ini->GetSectionKeys(VARIABLE_SECTION))
	{
		result[name] = ini->Get(
			VARIABLE_SECTION,
			name,
			"");
	}
	return result;
}

bool variablePresent(
	const std::shared_ptr<INIReader>& ini,
	const std::string& name)
{
	if (ini == nullptr)
	{
		return false;
	}
	const std::string normalizedName =
		normalizedVariableName(name);
	const std::vector<std::string> keys =
		ini->GetSectionKeys(VARIABLE_SECTION);
	return std::any_of(
		keys.cbegin(),
		keys.cend(),
		[&normalizedName](const std::string& key)
		{
			return normalizedVariableName(key) ==
				normalizedName;
		});
}

std::optional<std::string> canonicalRealValue(float value)
{
	if (!std::isfinite(value))
	{
		return std::nullopt;
	}
	if (value == 0.0f)
	{
		return std::string("0");
	}

	std::string canonical;
#if (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE < 11) || defined(__APPLE__)
	// GCC 9 and 10 provide integer std::to_chars only, while Apple's floating-
	// point overload requires newer deployment targets. Use a classic-locale
	// stream with the same max_digits10 precision on those supported platforms.
	std::ostringstream stream;
	stream.imbue(std::locale::classic());
	stream << std::defaultfloat
		<< std::setprecision(
			std::numeric_limits<float>::max_digits10)
		<< value;
	if (!stream)
	{
		return std::nullopt;
	}
	canonical = stream.str();
#else
	char buffer[64] = {};
	const std::to_chars_result result =
		std::to_chars(
			std::begin(buffer),
			std::end(buffer),
			value,
			std::chars_format::general,
			std::numeric_limits<float>::max_digits10);
	if (result.ec != std::errc())
	{
		return std::nullopt;
	}
	canonical.assign(buffer, result.ptr);
#endif
	const std::size_t exponentOffset =
		canonical.find_first_of("eE");
	if (exponentOffset == std::string::npos)
	{
		return canonical;
	}

	canonical[exponentOffset] = 'e';
	std::size_t exponentDigits = exponentOffset + 1;
	bool negativeExponent = false;
	if (exponentDigits < canonical.size() &&
		(canonical[exponentDigits] == '+' ||
			canonical[exponentDigits] == '-'))
	{
		negativeExponent =
			canonical[exponentDigits] == '-';
		++exponentDigits;
	}
	while (exponentDigits + 1 < canonical.size() &&
		canonical[exponentDigits] == '0')
	{
		++exponentDigits;
	}
	const std::string exponent =
		canonical.substr(exponentDigits);
	canonical.erase(exponentOffset + 1);
	if (negativeExponent)
	{
		canonical.push_back('-');
	}
	canonical += exponent;
	return canonical;
}
}

VariableList::VariableList()
{
}


VariableList::~VariableList()
{
	freeResource();
}

bool VariableList::load(std::string* failureReason)
{
	if (failureReason != nullptr)
	{
		failureReason->clear();
	}
	const std::string fileName =
		SaveFileManager::CurrentPath() + VARIABLE_INI;
	std::shared_ptr<INIReader> loadedIni;
	const SaveIniPersistence::ReadStatus status =
		SaveIniPersistence::read(fileName, loadedIni);
	if (status == SaveIniPersistence::ReadStatus::Unreadable ||
		status == SaveIniPersistence::ReadStatus::Malformed ||
		(status == SaveIniPersistence::ReadStatus::Loaded &&
			(loadedIni == nullptr ||
				!loadedIni->HasSection(VARIABLE_SECTION))))
	{
		if (failureReason != nullptr)
		{
			*failureReason =
				u8"变量文件 variable.ini 无法读取或格式错误";
		}
		return false;
	}
	if (loadedIni == nullptr)
	{
		loadedIni = std::make_shared<INIReader>();
	}

	const std::map<std::string, std::string> before =
		variableSnapshot(ini);
	ini = std::move(loadedIni);
	const std::map<std::string, std::string> after =
		variableSnapshot(ini);
	std::map<std::string, std::string> names = before;
	names.insert(after.cbegin(), after.cend());
	for (const auto& item : names)
	{
		const auto beforeValue = before.find(item.first);
		const auto afterValue = after.find(item.first);
		const std::string oldValue =
			beforeValue != before.cend()
				? beforeValue->second
				: std::string();
		const std::string newValue =
			afterValue != after.cend()
				? afterValue->second
				: std::string();
		enqueueChange(
			item.first,
			EditorRun::RuntimeTraceVariableValueType::String,
			oldValue,
			newValue,
			(beforeValue == before.cend()) !=
				(afterValue == after.cend()));
	}
	return true;
}

void VariableList::ensureInitialized()
{
	if (ini == nullptr)
	{
		ini = std::make_shared<INIReader>();
	}
}

bool VariableList::save()
{
	if (ini == nullptr)
	{
		return false;
	}
	std::string fileName =
		SaveFileManager::CurrentPath() + VARIABLE_INI;
	return ini->saveToFile(fileName);
}

void VariableList::clearExcept(const std::vector<std::string>& keepNames)
{
	const std::map<std::string, std::string> before =
		variableSnapshot(ini);
	std::vector<std::pair<std::string, int>> keptValues;
	if (ini != nullptr)
	{
		for (const auto& name : keepNames)
		{
			keptValues.push_back({ name, getInteger(name) });
		}
	}

	ini = std::make_shared<INIReader>();
	for (const auto& item : keptValues)
	{
		ini->SetInteger(VARIABLE_SECTION, item.first, item.second);
	}
	const std::map<std::string, std::string> after =
		variableSnapshot(ini);
	std::map<std::string, std::string> names = before;
	names.insert(after.cbegin(), after.cend());
	for (const auto& item : names)
	{
		const auto beforeValue = before.find(item.first);
		const auto afterValue = after.find(item.first);
		enqueueChange(
			item.first,
			EditorRun::RuntimeTraceVariableValueType::String,
			beforeValue != before.cend()
				? beforeValue->second
				: std::string(),
			afterValue != after.cend()
				? afterValue->second
				: std::string(),
			(beforeValue == before.cend()) !=
				(afterValue == after.cend()));
	}
}

std::string VariableList::get(const std::string & name)
{
	if (ini == nullptr)
	{
		return "";
	}
	return ini->Get(VARIABLE_SECTION, name, "");
}

int VariableList::getInteger(const std::string & name)
{
	if (ini == nullptr)
	{
		return 0;
	}

	int ret = ini->GetInteger(VARIABLE_SECTION, name, 0);
	return ret;
}

float VariableList::getReal(const std::string & name)
{
	if (ini == nullptr)
	{
		return 0.0;
	}
	return ini->GetReal(VARIABLE_SECTION, name, 0.0);
}

bool VariableList::getBoolean(const std::string & name)
{
	if (ini == nullptr)
	{
		return false;
	}
	return ini->GetBoolean(VARIABLE_SECTION, name, false);
}

void VariableList::set(const std::string & name, std::string & value)
{
	if (ini == nullptr)
	{
		return;
	}
	const bool wasPresent = variablePresent(ini, name);
	const std::string beforeValue =
		ini->Get(VARIABLE_SECTION, name, "");
	ini->Set(VARIABLE_SECTION, name, value);
	const bool isPresent = variablePresent(ini, name);
	const std::string afterValue =
		ini->Get(VARIABLE_SECTION, name, "");
	enqueueChange(
		normalizedVariableName(name),
		EditorRun::RuntimeTraceVariableValueType::String,
		beforeValue,
		afterValue,
		wasPresent != isPresent ||
			beforeValue != afterValue);
}

void VariableList::setInteger(const std::string & name, int value)
{
	if (ini == nullptr)
	{
		return;
	}
	const bool wasPresent = variablePresent(ini, name);
	const std::string beforeRaw =
		ini->Get(VARIABLE_SECTION, name, "");
	const int beforeValue = static_cast<int>(
		ini->GetInteger(
			VARIABLE_SECTION,
			name,
			0));
	ini->SetInteger(
		VARIABLE_SECTION,
		name,
		value);
	const bool isPresent = variablePresent(ini, name);
	const std::string afterRaw =
		ini->Get(VARIABLE_SECTION, name, "");
	const int afterValue = static_cast<int>(
		ini->GetInteger(
			VARIABLE_SECTION,
			name,
			0));
	enqueueChange(
		normalizedVariableName(name),
		EditorRun::RuntimeTraceVariableValueType::Integer,
		std::to_string(beforeValue),
		std::to_string(afterValue),
		wasPresent != isPresent ||
			beforeRaw != afterRaw);
}

void VariableList::setReal(const std::string & name, float value)
{
	if (ini == nullptr)
	{
		return;
	}
	const bool wasPresent = variablePresent(ini, name);
	const std::string beforeRaw =
		ini->Get(VARIABLE_SECTION, name, "");
	const float beforeValue = ini->GetReal(
		VARIABLE_SECTION,
		name,
		0.0f);
	ini->SetReal(
		VARIABLE_SECTION,
		name,
		value);
	const bool isPresent = variablePresent(ini, name);
	const std::string afterRaw =
		ini->Get(VARIABLE_SECTION, name, "");
	const float afterValue = ini->GetReal(
		VARIABLE_SECTION,
		name,
		0.0f);
	// Trace v1 has no canonical spelling for NaN or infinity. Preserve the
	// existing INI mutation but suppress only that unrepresentable event so an
	// InvalidEvent cannot close the session's trace writer.
	const std::optional<std::string> beforeCanonical =
		canonicalRealValue(beforeValue);
	const std::optional<std::string> afterCanonical =
		canonicalRealValue(afterValue);
	if (beforeCanonical && afterCanonical)
	{
		enqueueChange(
			normalizedVariableName(name),
			EditorRun::RuntimeTraceVariableValueType::Real,
			*beforeCanonical,
			*afterCanonical,
			wasPresent != isPresent ||
				beforeRaw != afterRaw);
	}
}

void VariableList::setBoolean(const std::string & name, bool value)
{
	if (ini == nullptr)
	{
		return;
	}
	const bool wasPresent = variablePresent(ini, name);
	const std::string beforeRaw =
		ini->Get(VARIABLE_SECTION, name, "");
	const bool beforeValue = ini->GetBoolean(
		VARIABLE_SECTION,
		name,
		false);
	ini->SetBoolean(
		VARIABLE_SECTION,
		name,
		value);
	const bool isPresent = variablePresent(ini, name);
	const std::string afterRaw =
		ini->Get(VARIABLE_SECTION, name, "");
	const bool afterValue = ini->GetBoolean(
		VARIABLE_SECTION,
		name,
		false);
	enqueueChange(
		normalizedVariableName(name),
		EditorRun::RuntimeTraceVariableValueType::Boolean,
		beforeValue ? "true" : "false",
		afterValue ? "true" : "false",
		wasPresent != isPresent ||
			beforeRaw != afterRaw);
}

void VariableList::setRuntimeTraceContext(
	EditorRun::RuntimeTraceWriter* writer,
	std::function<std::uint64_t()> executionId)
{
	runtimeTraceWriter = writer;
	currentExecutionId = std::move(executionId);
}

void VariableList::enqueueChange(
	const std::string& name,
	EditorRun::RuntimeTraceVariableValueType valueType,
	const std::string& beforeValue,
	const std::string& afterValue,
	bool storageChanged)
{
	// Trace v1 has no presence bit. Keep missing-to-empty and
	// empty-to-missing mutations observable as equal-valued change events.
	if (runtimeTraceWriter == nullptr ||
		name.empty() ||
		(!storageChanged &&
			beforeValue == afterValue))
	{
		return;
	}
	EditorRun::RuntimeTraceVariableChangeEvent change;
	const std::uint64_t executionId =
		currentExecutionId ? currentExecutionId() : 0;
	if (executionId > 0)
	{
		change.executionId = executionId;
	}
	change.variableName = name;
	change.valueType = valueType;
	change.beforeValue = beforeValue;
	change.afterValue = afterValue;
	EditorRun::RuntimeTraceEvent event;
	event.payload = std::move(change);
	(void)runtimeTraceWriter->enqueue(std::move(event));
}

void VariableList::freeResource()
{
	if (ini != nullptr)
	{
		ini = nullptr;
	}
}
