#pragma once

#include <vector>
#include "../Types/Types.h"
#include "../Engine/Engine.h"
#include "../Image/IMP.h"
#include "../File/PakFile.h"
#include "../File/INIReader.h"
#include "../Types/Types.h"

class Element
{
public:
	Element();
	~Element();	
	void addChild(Element * child);
	void removeChild(Element * child);
	void removeAllChild();
	void changeParent(Element * p);
	void setChildActivated(Element * child, bool activated);

	//按照自身x,y位置设置子元素的区域
	void setChildRect(int setLevel);
	unsigned int getResult();
	bool getResult(unsigned int ret);
	int index = -1;
	int type = -1;

protected:
	Engine * engine = NULL;

	static std::vector<Element *> runningElement;

public:

	unsigned char priority = 128;	//优先级，0最大，255最小，默认128
	ElementType elementType = etElement;
	int elementTypeData = 0;

	std::string name = "Element";

	Element * parent = NULL;
	std::vector<Element *> children = {};
	Time timer;						//计时器
	unsigned int LastTime = 0;

	int dragType = -1;
	int dragIndex = -1;
	int dropIndex = -1;
	int dropType = -1;

public:
	virtual void initAllTime();
	virtual void initTime();
	virtual unsigned int getTime();
	virtual unsigned int getFrameTime();
	virtual void setPaused(bool paused);
public:

	bool visible = true;			//是否可见，只有可见状态才可以draw和检测鼠标进入
	bool activated = true;			//是否激活，只有处于激活状态时才可以draw和处理事件

	bool drawFullScreen = false;	//全屏绘图元素，绘制时从最外层的全屏元素开始，忽略其下面的元素
	bool rectFullScreen = false;	//为true时，忽略rect限制，在全屏范围检测鼠标事件
	bool eventOccupied = false;		//为true时，独占事件，处理事件后，其它元素不能处理事件
	bool coverMouse = true;			//为true时，遮盖鼠标，在其下面的元素无法检测触发鼠标进入事件
	bool needEvents = true;			//是否需要处理事件
	bool onlyCheckRect = false;
	bool needArrangeChild = true;
	bool canDraw = true;

	bool canCallBack = false;		//返回值调用回调函数

	bool canDrag = false;			//是否可以拖拽
	bool canDrop = false;			//是否可以接受放置


protected:

	int mouseLDownX = 0;
	int mouseLDownY = 0;
	unsigned int mouseLDownTime = 0;

	bool mouseLeftPressed = false;	//辅助变量，鼠标位于区域内时有效，鼠标左键点击状态变量
	bool mouseMoveIn = false;		//鼠标进入状态变量
	bool mouseLDownInRect = false;	//鼠标在区域中左键按下

	bool nextFrame = false;

	static bool dragging;
	static int dragParam[2];
	static Element * dragItem;
	int dragX = 0;
	int dragY = 0;
	int dragRange = 1;

protected:
	void reArrangeChildren();
	virtual bool mouseInRect();
	//先交给每个child处理事件,再处理自身事件
	void handleEvents(bool vis, bool * canCoverMouse);
	void checkDrag();
	void drawSelf();
	void drawAll();
	void update(bool r, bool events, bool vis, bool * canCoverMouse);
	void updateAll(bool * canCoverMouse);
	void frame();
	bool initial();
	void handleRun();
	void exit();
	void quit();
	bool running = false;
public:
	unsigned int run();
	unsigned int result = erNone;	//某些情况下的返回值
	Rect rect = { 0, 0, 0, 0 };		//元素占据的范围，用于鼠标进出检测等

	void freeAll();
	virtual void freeResource() { result = erNone; };
	virtual void initFromIni(std::string fileName) {};
protected:
	virtual void onEvent() {};
	//处理事件，已处理返回true，未处理返回false，未处理的事件将继续交给其它元素进行处理
	virtual bool onHandleEvent(AEvent * e) { return false; };
	//鼠标进入触发的事件
	virtual void onMouseMoveIn() {};
	//鼠标移出触发的事件
	virtual void onMouseMoveOut() {};
	//鼠标左键按下触发的事件
	virtual void onMouseLeftDown() {};
	//鼠标左键抬起触发的事件
	virtual void onMouseLeftUp() {};
	//鼠标点击事件
	virtual void onClick() {};
	//拖拽放置事件
	virtual void onDrop(Element * src, int param1, int param2) {};
	//拖拽事件
	virtual void onDrag(int * param1, int * param2) {};
	//设置控件继承区域触发事件
	virtual void onSetChildRect() {};
	//正在拖拽，拖拽时每帧都会调用
	virtual void onDragging() {};
	//拖拽结束时调用，可在此函数中取消拖拽状态，避免放置事件触发
	virtual void onDragEnd(Element * dst) {};
	//画拖拽的画面
	virtual void onDrawDrag() {};
	//所需画的东西写在这里
	virtual void onDraw() {};
	//初始化时调用的事件
	virtual bool onInitial() { return true; };
	//在开始run并且初始化之后触发的事件
	virtual void onRun() {};
	//离开时触发的事件
	virtual void onExit() {};		
	//在每帧更新状态的触发事件
	virtual void onUpdate() {};

public:
	//供子元素回调
	virtual void onChildCallBack(Element * child) {};
};
