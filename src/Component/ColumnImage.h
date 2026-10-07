#pragma once
#include "ImageContainer.h"
class ColumnImage :
	public ImageContainer
{
public:
	ColumnImage();
	virtual ~ColumnImage();

	float percent = 1.0;
	float lagPercent = 1.0;
	void initFromIni(INIReader& ini) override;

private:
	bool horizontal = false;
	unsigned int fillColor = 0xFF527D6A;
	virtual void onDraw();
};

