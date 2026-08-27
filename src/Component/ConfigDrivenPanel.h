#pragma once
#include "Panel.h"
#include "Scrollbar.h"
#include "ComponentRegistry.h"
#include <cstddef>
#include <map>
#include <vector>
#include <string>

class ConfigDrivenPanel :
	public Panel
{
public:
	ConfigDrivenPanel();
	virtual ~ConfigDrivenPanel();

	virtual void init() override;

	void loadMenuDefinition(const std::string& menuDefinitionFile);

	std::shared_ptr<BaseComponent> getComponentByName(
		const std::string& componentName);
	std::shared_ptr<BaseComponent> getComponentByName(
		const std::string& componentName) const;

	template<typename targetType>
	std::shared_ptr<targetType> getComponentByName(
		const std::string& componentName)
	{
		return std::dynamic_pointer_cast<targetType>(getComponentByName(componentName));
	}

	template<typename targetType>
	std::shared_ptr<targetType> getComponentByName(
		const std::string& componentName) const
	{
		return std::dynamic_pointer_cast<targetType>(getComponentByName(componentName));
	}

	struct ComponentDefinition
	{
		std::string type;
		std::string name;
		std::string file;
		std::string bind;
		std::string format;
		std::string controllerUp;
		std::string controllerDown;
		std::string controllerLeft;
		std::string controllerRight;
	};

	struct SubMenuDefinition
	{
		std::string name;
		std::string file;
	};

	struct DataBinding
	{
		std::string componentName;
		std::vector<std::string> bindPaths;
		std::string formatStr;
	};

	const std::vector<ComponentDefinition>& getComponentDefinitions() const { return componentDefinitions; }
	const std::vector<SubMenuDefinition>& getSubMenuDefinitions() const { return subMenuDefinitions; }
	const ConfigDrivenPanel* getSubMenuPanel(std::size_t index) const
	{
		return index < subMenus.size() ? subMenus[index].get() : nullptr;
	}

protected:
	virtual void onEvent() override;
	virtual void onWindowResize(int width, int height) override;
	virtual void freeResource() override;

	std::shared_ptr<BaseComponent> createComponentByType(const std::string& type, const std::string& iniFile);
	std::shared_ptr<ConfigDrivenPanel> createSubMenu(const std::string& menuFile);

	std::vector<ComponentDefinition> componentDefinitions;
	std::vector<SubMenuDefinition> subMenuDefinitions;
	std::map<std::string, std::shared_ptr<BaseComponent>> componentMap;
	std::vector<DataBinding> dataBindings;
	std::vector<std::shared_ptr<ConfigDrivenPanel>> subMenus;
	std::string loadedMenuDefinitionFile;

	void updateDataBindings();
};
