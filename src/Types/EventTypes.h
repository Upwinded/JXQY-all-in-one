#pragma once

#include <vector>
#include <memory>
#include <queue>


enum EventType
{
	ET_QUIT = 0x100,
	ET_KEYDOWN = 0x300,
	ET_KEYUP,
	ET_MOUSEMOTION = 0x400,
	ET_MOUSEDOWN,
	ET_MOUSEUP,
	ET_FINGERDOWN = 0x700,
	ET_FINGERUP,
	ET_FINGERMOTION
};

typedef int64_t EventTouchID;
//鼠标也会返回触摸消息,TouchID=-1
#define TOUCH_MOUSEID SDL_TOUCH_MOUSEID 
#define TOUCH_UNTOUCHEDID -2

enum MouseButtonCode
{
	MBC_MOUSE_LEFT = 1,
	MBC_MOUSE_RIGHT = 3
};

enum KeyCode
{
	KEY_A = 4,
	KEY_B = 5,
	KEY_C = 6,
	KEY_D = 7,
	KEY_E = 8,
	KEY_F = 9,
	KEY_G = 10,
	KEY_H = 11,
	KEY_I = 12,
	KEY_J = 13,
	KEY_K = 14,
	KEY_L = 15,
	KEY_M = 16,
	KEY_N = 17,
	KEY_O = 18,
	KEY_P = 19,
	KEY_Q = 20,
	KEY_R = 21,
	KEY_S = 22,
	KEY_T = 23,
	KEY_U = 24,
	KEY_V = 25,
	KEY_W = 26,
	KEY_X = 27,
	KEY_Y = 28,
	KEY_Z = 29,

	KEY_1 = 30,
	KEY_2 = 31,
	KEY_3 = 32,
	KEY_4 = 33,
	KEY_5 = 34,
	KEY_6 = 35,
	KEY_7 = 36,
	KEY_8 = 37,
	KEY_9 = 38,
	KEY_0 = 39,

	KEY_RETURN = 40,
	KEY_ESCAPE = 41,
	KEY_BACKSPACE = 42,
	KEY_TAB = 43,
	KEY_SPACE = 44,

	KEY_MINUS = 45,
	KEY_EQUALS = 46,
	KEY_LEFTBRACKET = 47,
	KEY_RIGHTBRACKET = 48,
	KEY_BACKSLASH = 49,

	KEY_F1 = 58,
	KEY_F2 = 59,
	KEY_F3 = 60,
	KEY_F4 = 61,
	KEY_F5 = 62,
	KEY_F6 = 63,
	KEY_F7 = 64,
	KEY_F8 = 65,
	KEY_F9 = 66,
	KEY_F10 = 67,
	KEY_F11 = 68,
	KEY_F12 = 69,

	KEY_RIGHT = 79,
	KEY_LEFT = 80,
	KEY_DOWN = 81,
	KEY_UP = 82,

	KEY_LCTRL = 224,
	KEY_LSHIFT = 225,
	KEY_LALT = 226,

	KEY_RCTRL = 228,
	KEY_RSHIFT = 229,
	KEY_RALT = 230,
};

enum InitErrorType
{
	initOK = 0x00000000,
	sdlError = 0x00000001,
	soundError = 0x00000002,
	videoError = 0x00000004,
	LZOError = 0x00000008,
	initError = 0xFFFFFFFF
};


struct AEvent
{
	EventType eventType;
	EventTouchID eventData;
	int eventX = 0;
    int eventY = 0;
	bool eventRepeat = true;

	AEvent() {}

	AEvent(EventType aEventType, EventTouchID aEventData, int aEventX, int aEventY) 
	{ 
		eventType = aEventType; 
		eventData = aEventData; 
		eventX = aEventX; 
		eventY = aEventY; 
	}

	AEvent(EventType aEventType, EventTouchID aEventData, int aEventX, int aEventY, bool eRepeat)
	{
		eventType = aEventType;
		eventData = aEventData;
		eventX = aEventX;
		eventY = aEventY;
		eventRepeat = eRepeat;
	}
};

struct EventList
{
	std::queue<AEvent> event;
};