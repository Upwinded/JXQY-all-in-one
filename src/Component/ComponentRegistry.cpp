#include "ComponentRegistry.h"

ComponentRegistry& ComponentRegistry::getInstance()
{
	static ComponentRegistry instance;
	return instance;
}

void ComponentRegistry::registerType(const std::string& typeName, CreatorFunc creator)
{
	registry[typeName] = creator;
}

std::shared_ptr<BaseComponent> ComponentRegistry::create(const std::string& typeName)
{
	auto iter = registry.find(typeName);
	if (iter != registry.end())
	{
		return iter->second();
	}
	return nullptr;
}

bool ComponentRegistry::hasType(const std::string& typeName) const
{
	return registry.find(typeName) != registry.end();
}

std::vector<std::string> ComponentRegistry::getRegisteredTypes() const
{
	std::vector<std::string> types;
	for (auto& pair : registry)
	{
		types.push_back(pair.first);
	}
	return types;
}
