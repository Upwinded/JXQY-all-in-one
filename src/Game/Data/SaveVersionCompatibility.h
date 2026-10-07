#pragma once

#include "../../Resource/SemanticVersion.h"
#include "../../JxqyEngineVersion.h"

namespace SaveVersionCompatibility
{
inline bool validateEngineVersion(const std::string& version, bool initialTemplate,
	std::string& failureMessage)
{
	failureMessage.clear();
	const auto saved = ModRelease::parseSemanticVersion(version);
	const auto minimum = ModRelease::parseSemanticVersion(JxqyBuildVersion::MinimumCompatibleSaveEngineVersion);
	const auto current = ModRelease::parseSemanticVersion(JxqyBuildVersion::EngineVersion);
	if (!saved.succeeded())
	{
		failureMessage = u8"存档引擎版本格式错误";
		return false;
	}
	if (!minimum.succeeded() || !current.succeeded() ||
		ModRelease::compareSemanticVersionPrecedence(minimum.version, current.version) > 0)
	{
		failureMessage = u8"程序引擎版本配置错误";
		return false;
	}
	if (ModRelease::compareSemanticVersionPrecedence(saved.version, minimum.version) < 0)
	{
		failureMessage = initialTemplate ? u8"资源包版本过旧，请更新资源包后重开游戏"
			: u8"抱歉，之前有严重bug，旧存档不兼容，需要重开，请谅解！";
		return false;
	}
	if (ModRelease::compareSemanticVersionPrecedence(saved.version, current.version) > 0)
	{
		failureMessage = u8"存档由更高版本引擎创建，请更新程序后重试";
		return false;
	}
	return true;
}

inline std::string resourceVersionOrDefault(const std::string& version)
{
	return version.empty() ? "1.0.0" : version;
}

// Initial templates belong to the active resource release, not a saved run.
// Check this before loading a candidate into the running game.
inline bool validateResourceVersion(
	const std::string& savedVersion,
	const std::string& currentVersion,
	const std::string& minimumVersion,
	bool initialTemplate,
	std::string& failureMessage)
{
	failureMessage.clear();
	const auto current = ModRelease::parseResourceVersion(
		resourceVersionOrDefault(currentVersion));
	const auto minimum = ModRelease::parseResourceVersion(minimumVersion);
	if (!current.succeeded() || !minimum.succeeded() ||
		ModRelease::compareSemanticVersionPrecedence(
			minimum.version, current.version) > 0)
	{
		failureMessage = u8"资源包存档版本配置错误";
		return false;
	}
	const auto saved = initialTemplate ? current :
		ModRelease::parseResourceVersion(savedVersion);
	if (!saved.succeeded())
	{
		failureMessage = u8"存档资源版本格式错误";
		return false;
	}
	if (ModRelease::compareSemanticVersionPrecedence(
			saved.version, minimum.version) < 0)
	{
		failureMessage = u8"抱歉，存档的资源版本过旧，与当前资源包不兼容，需要重开，请谅解！";
		return false;
	}
	if (ModRelease::compareSemanticVersionPrecedence(
			saved.version, current.version) > 0)
	{
		failureMessage = u8"存档由更高版本资源包创建，请更新资源包后重试";
		return false;
	}
	return true;
}
}
