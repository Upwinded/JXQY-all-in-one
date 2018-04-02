#pragma once
#include "../../Component/Component.h"

class ColumnMenu :
	public Panel
{
public:
	ColumnMenu();
	~ColumnMenu();

	TransImage * image = NULL;
	ColumnImage * columnLife = NULL;
	ColumnImage * columnThew = NULL;
	ColumnImage * columnMana = NULL;

	void updateState();

private:
	virtual void onUpdate();
	virtual void init();
	virtual void freeResource();
};