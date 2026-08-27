#pragma once
#include "BaseComponent.h"
#include <map>
#include <functional>
#include <memory>
#include <string>

class ComponentRegistry
{
public:
	using CreatorFunc = std::function<std::shared_ptr<BaseComponent>()>;

	static ComponentRegistry& getInstance();

	void registerType(const std::string& typeName, CreatorFunc creator);
	std::shared_ptr<BaseComponent> create(const std::string& typeName);
	bool hasType(const std::string& typeName) const;
	std::vector<std::string> getRegisteredTypes() const;

private:
	ComponentRegistry() = default;
	ComponentRegistry(const ComponentRegistry&) = delete;
	ComponentRegistry& operator=(const ComponentRegistry&) = delete;

	std::map<std::string, CreatorFunc> registry;
};
