#include "SaveGeneration.h"

#include "../../File/File.h"

namespace
{
std::string normalizeVirtualPathKey(std::string path)
{
	for (char& character : path)
	{
		if (character == '/')
		{
			character = '\\';
		}
		else if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(
				character + ('a' - 'A'));
		}
	}
	while (!path.empty() && path.back() == '\\')
	{
		path.pop_back();
	}
	return path;
}

}

bool SaveGeneration::NormalizeGenerationDirectory(
	const std::string& directoryName,
	std::string& normalizedDirectory)
{
	normalizedDirectory.clear();
	if (!File::isSafeResourcePath(directoryName))
	{
		return false;
	}

	std::string path = normalizeVirtualPathKey(
		directoryName);
	if (path.empty())
	{
		return false;
	}

	std::vector<std::string> components;
	std::size_t componentBegin = 0;
	while (componentBegin < path.size())
	{
		const std::size_t separator =
			path.find('\\', componentBegin);
		const std::size_t componentEnd =
			separator == std::string::npos
				? path.size()
				: separator;
		const std::string component =
			path.substr(
				componentBegin,
				componentEnd - componentBegin);
		if (component.empty() ||
			component == "." ||
			component == "..")
		{
			return false;
		}
		components.push_back(component);
		if (separator == std::string::npos)
		{
			break;
		}
		componentBegin = separator + 1;
	}
	if (components.size() < 2 ||
		components.front() != "save")
	{
		return false;
	}

	normalizedDirectory = components.front();
	for (std::size_t index = 1;
		index < components.size();
		++index)
	{
		normalizedDirectory += "\\";
		normalizedDirectory += components[index];
	}
	return true;
}

const char* SaveGeneration::DescribeError(SaveGenerationError error)
{
	switch (error)
	{
	case SaveGenerationError::None:
		return "none";
	case SaveGenerationError::GameIniInvalid:
		return "game.ini is invalid";
	case SaveGenerationError::Cancelled:
		return "operation cancelled";
	case SaveGenerationError::PublicationFailed:
		return "direct copy failed";
	}
	return "unknown";
}
