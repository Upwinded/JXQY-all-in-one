#include "ConfigDrivenPanel.h"
#include "../File/File.h"
#include "../File/INIReader.h"
#include "../Game/GameManager/GameManager.h"
#include "../libconvert/libconvert.h"
#include "../File/log.h"

namespace
{
bool containsCurrentRunOwner(const PElement& subtree)
{
	if (subtree == nullptr)
	{
		return false;
	}
	if (Element::isCurrentRunOwner(subtree.get()))
	{
		return true;
	}
	for (const PElement& child : subtree->children)
	{
		if (containsCurrentRunOwner(child))
		{
			return true;
		}
	}
	return false;
}
}

ConfigDrivenPanel::ConfigDrivenPanel()
{
	name = "ConfigDrivenPanel";
}

ConfigDrivenPanel::~ConfigDrivenPanel()
{
	freeResource();
}

void ConfigDrivenPanel::init()
{
	if (!loadedMenuDefinitionFile.empty())
	{
		std::string menuDefinitionFile = loadedMenuDefinitionFile;
		freeResource();
		loadMenuDefinition(menuDefinitionFile);
		setChildRectReferToParent();
	}
}

void ConfigDrivenPanel::loadMenuDefinition(const std::string& menuDefinitionFile)
{
	loadedMenuDefinitionFile = menuDefinitionFile;

	std::unique_ptr<char[]> content;
	int length = File::readFile(menuDefinitionFile, content);
	if (content == nullptr || length == 0)
	{
		GameLog::write("no menu definition file: %s\n", menuDefinitionFile.c_str());
		return;
	}

	INIReader ini(content);

	name = ini.Get("menu", "name", name);

	std::string windowFile = ini.Get("menu", "window", "");
	if (!windowFile.empty())
	{
		initFromIniFileName(windowFile);
	}

	int componentIndex = 1;
	while (true)
	{
		std::string section = convert::formatString("component%d", componentIndex);
		std::string type = ini.Get(section, "type", "");
		if (type.empty())
		{
			break;
		}

		ComponentDefinition definition;
		definition.type = type;
		definition.name = ini.Get(section, "name", "");
		definition.file = ini.Get(section, "file", "");
		definition.bind = ini.Get(section, "bind", "");
		definition.format = ini.Get(section, "format", "%d");
		definition.controllerUp = ini.Get(section, "controllerup", "");
		definition.controllerDown = ini.Get(section, "controllerdown", "");
		definition.controllerLeft = ini.Get(section, "controllerleft", "");
		definition.controllerRight = ini.Get(section, "controllerright", "");

		componentDefinitions.push_back(definition);

		auto component = createComponentByType(definition.type, definition.file);
		if (component != nullptr)
		{
			componentMap[definition.name] = component;
		}

		if (!definition.bind.empty())
		{
			DataBinding binding;
			binding.componentName = definition.name;
			binding.formatStr = definition.format;

			auto bindParts = convert::splitString(definition.bind, ",");
			for (auto& part : bindParts)
			{
				binding.bindPaths.push_back(part);
			}

			std::vector<int> testValues(binding.bindPaths.size(), 0);
			std::string testOutput;
			if (convert::formatIntegerValues(binding.formatStr, testValues, testOutput))
			{
				dataBindings.push_back(binding);
			}
			else
			{
				GameLog::write("invalid integer data binding format for component: %s\n",
					definition.name.c_str());
			}
		}

		componentIndex++;
	}

	int subMenuIndex = 1;
	while (true)
	{
		std::string section = convert::formatString("submenu%d", subMenuIndex);
		std::string subMenuFile = ini.Get(section, "file", "");
		if (subMenuFile.empty())
		{
			break;
		}

		SubMenuDefinition subMenuDef;
		subMenuDef.name = ini.Get(section, "name", "");
		subMenuDef.file = subMenuFile;
		subMenuDefinitions.push_back(subMenuDef);

		auto subMenu = createSubMenu(subMenuFile);
		if (subMenu != nullptr)
		{
			subMenus.push_back(subMenu);
		}

		subMenuIndex++;
	}
}

std::shared_ptr<BaseComponent> ConfigDrivenPanel::getComponentByName(
	const std::string& componentName)
{
	return static_cast<const ConfigDrivenPanel&>(*this)
		.getComponentByName(componentName);
}

std::shared_ptr<BaseComponent> ConfigDrivenPanel::getComponentByName(
	const std::string& componentName) const
{
	auto iter = componentMap.find(componentName);
	if (iter != componentMap.end())
	{
		return iter->second;
	}
	return nullptr;
}

std::shared_ptr<BaseComponent> ConfigDrivenPanel::createComponentByType(
	const std::string& type, const std::string& iniFile)
{
	auto component = ComponentRegistry::getInstance().create(type);
	if (component == nullptr)
	{
		GameLog::write("unknown component type: %s\n", type.c_str());
		return nullptr;
	}

	std::unique_ptr<char[]> content;
	int length = File::readFile(iniFile, content);
	if (content == nullptr || length == 0)
	{
		GameLog::write("no ini file: %s\n", iniFile.c_str());
		return nullptr;
	}
	INIReader ini(content);

	auto scrollbar = std::dynamic_pointer_cast<Scrollbar>(component);
	if (scrollbar)
	{
		scrollbar->initFromIniWithName(ini, iniFile);
	}
	else
	{
		component->initFromIni(ini);
	}

	addChild(component);
	return component;
}

std::shared_ptr<ConfigDrivenPanel> ConfigDrivenPanel::createSubMenu(const std::string& menuFile)
{
	auto subMenu = std::make_shared<ConfigDrivenPanel>();
	subMenu->loadMenuDefinition(menuFile);
	addChild(subMenu);
	return subMenu;
}

void ConfigDrivenPanel::updateDataBindings()
{
	for (auto& binding : dataBindings)
	{
		auto component = getComponentByName(binding.componentName);
		if (component == nullptr)
		{
			continue;
		}

		auto label = std::dynamic_pointer_cast<Label>(component);
		if (label == nullptr)
		{
			continue;
		}

		std::vector<int> values;
		values.reserve(binding.bindPaths.size());
		for (const auto& bindPath : binding.bindPaths)
		{
			values.push_back(GameManager::getInstance()->getBindValue(bindPath));
		}

		std::string formattedValue;
		if (convert::formatIntegerValues(binding.formatStr, values, formattedValue))
		{
			label->setStr(formattedValue);
		}
	}
}

void ConfigDrivenPanel::onEvent()
{
	updateDataBindings();
}

void ConfigDrivenPanel::onWindowResize(int width, int height)
{
	PElement runningDynamicSubtree;
	for (const PElement& child : children)
	{
		if (!containsCurrentRunOwner(child))
		{
			continue;
		}

		bool configurationOwned = false;
		for (const auto& componentEntry : componentMap)
		{
			if (componentEntry.second.get() == child.get())
			{
				configurationOwned = true;
				break;
			}
		}
		if (!configurationOwned)
		{
			for (const auto& subMenu : subMenus)
			{
				if (subMenu.get() == child.get())
				{
					configurationOwned = true;
					break;
				}
			}
		}
		if (!configurationOwned)
		{
			runningDynamicSubtree = child;
		}
		break;
	}

	init();

	if (runningDynamicSubtree != nullptr)
	{
		bool alreadyAttached = false;
		for (const PElement& child : children)
		{
			if (child.get() == runningDynamicSubtree.get())
			{
				alreadyAttached = true;
				break;
			}
		}
		if (!alreadyAttached)
		{
			addChild(runningDynamicSubtree);
		}
	}
}

void ConfigDrivenPanel::freeResource()
{
	impImage = nullptr;
	componentMap.clear();
	componentDefinitions.clear();
	subMenuDefinitions.clear();
	dataBindings.clear();
	subMenus.clear();
	removeAllChild();
}
