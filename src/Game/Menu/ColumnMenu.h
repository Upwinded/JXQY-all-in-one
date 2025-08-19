#pragma once
#include "../../Component/Component.h"

class ColumnMenu :
	public Panel
{
public:
	ColumnMenu();
	virtual ~ColumnMenu();

	virtual void init() override;

	std::shared_ptr<TransImage> image = nullptr;
	std::shared_ptr<ColumnImage> columnLife = nullptr;
	std::shared_ptr<ColumnImage> columnThew = nullptr;
	std::shared_ptr<ColumnImage> columnMana = nullptr;

	void updateState();

private:
	virtual void onUpdate();

	void freeResource();
};