#pragma once
#include "../../Component/Component.h"

class ColumnMenu :
	public Panel
{
public:
	ColumnMenu();
	virtual ~ColumnMenu();

	TransImage * image = nullptr;
	ColumnImage * columnLife = nullptr;
	ColumnImage * columnThew = nullptr;
	ColumnImage * columnMana = nullptr;

	void updateState();

private:
	virtual void onUpdate();
	virtual void init();
	void freeResource();
};