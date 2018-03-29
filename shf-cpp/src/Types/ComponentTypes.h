#pragma once

enum ComponentType
{
	ctImage = 0,
	ctPanel,
	ctButton,
	ctScrollbar,
	ctVideo
};

enum ScrollbarStyle
{
	ssHorizontal,
	ssVertical = 1,	
};

enum Align
{
	alNone = 0,
	alClient = 1,
	alLeft = 2,
	alRight = 3,
	alTop = 4,
	alBottom = 5,
	alLTCorner = 6,
	alRTCorner = 7,
	alLBCorner = 8,
	alRBCorner = 9,
	alCenter = 10,
	alLeftCenter = 11,
	alRightCenter = 12,
	alTopCenter = 13,
	alBottomCenter = 14,	
};