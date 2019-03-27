#pragma once

enum ElementPriority
{
	epDefault = 0x80,
	epMax = 0,
	epMin = 0xFF,	
	epComponent = 0x2A,
	epMenu = 0x20,
	epItem = 0x21,
	epButton = 0x22,
	epLabel = 0x23,
	epVideo = 0x24,
	epImage = 0x25,
	epWeather = 0x30,
	epFadeMask = 0x40,
	epPlayer = 0x50,
	epCamera = 0x51,
	epGameManager = 0x52,
	epEffectManager = 0x55,
	epNPC = 0x5A,
	epOBJ = 0x5B,
	epMap = 0x70
};

enum ElementType
{
	etNone = 0x0,
	etElement = 0x10,
	etScene = 0x100,
	etLayer = 0x1000,
	etMenu = 0x10000,
	etGoodsMenu,
	etMagicMenu,
	etStateMenu,
	etDocMenu,
	etMap = 0x11000,
	etVideoPlayer = 0x12000,
	etImageContainer,
	etButton = 0x100000,	
	etScrollbar,
	etStatus,
	etHint,
	etItem = 0x101000,
	etGoodsItem,
	etMagicItem,
	etNPC = 0x102000,
	etObject,
	etMagic,
	etEffect,
	ettrap,
	etScript = 0x1000000
};

enum ElementResult
{
	erNone = 0x0,
	erOK = 0b1,
	erExit = 0b10,
	erInitError = 0b100,
	erClick = 0b1000,
	erMouseLDown = 0b10000,
	erMouseLUp = 0b100000,
	erMouseRDown = 0b1000000,
	erMouseRUp = 0b10000000,
	erVideoStopped = 0b100000000,
	erDragEnd = 0b1000000000,
	erDragging = 0b10000000000,
	erDropped = 0b100000000000,
	erScrollbarSlided = 0b1000000000000,
	erSave = 0b10000000000000,
	erLoad = 0b100000000000000,
	erShowHint = 0b1000000000000000,
	erHideHint = 0b10000000000000000,

	erActionEnd = 0b100000000000000000,
	erLifeExhaust = 0b1000000000000000000,
	erExplode = 0b10000000000000000000,
	erRunDeathScript = 0b100000000000000000000,
};
