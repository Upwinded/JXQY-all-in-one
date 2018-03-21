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
	alClient,
	alLeft,
	alRight,
	alTop,
	alBottom,
	alLTCorner,
	alRTCorner,
	alLBCorner,
	alRBCorner,
};