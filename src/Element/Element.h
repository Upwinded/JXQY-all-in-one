#pragma once

#include <vector>
#include "../Types/Types.h"
#include "../Engine/Engine.h"
#include "../Image/IMP.h"
#include "../File/PakFile.h"
#include "../File/INIReader.h"
#include "../Types/Types.h"

class Element;

using PElement = std::shared_ptr<Element>;

class Element
{
public:
	Element();
	virtual ~Element();
public:
	static void setAsTop(PElement child);
	static void removeFromTop(PElement child);
	void addChild(PElement child);
	void removeChild(PElement child);
	void removeAllChild();
	void setChildActivated(PElement child, bool activated);
	//按照自身x,y位置设置子元素的区域
	void setChildRectReferToParent(int setLevel = -1);
	unsigned int getResult();
	bool getResult(unsigned int ret);
	int index = -1;
	int type = -1;
	bool canCallBack = false;
private:
	static PElement _topParent;
	static bool _top_initialed;

protected:
	PElement getMySharedPtr();
	PElement getChildSharedPtr(Element* element);

	Engine* engine = nullptr;

	static std::vector<PElement> runningElement;
	Timer timer;						//计时器
public:

	unsigned char priority = 128;	//优先级，0最大，255最小，默认128
	ElementType elementType = etElement;
	int elementTypeData = 0;

	std::string name = "Element";

	Element * parent = nullptr;
	std::vector<PElement> children;

	UTime LastFrameTime = 0;

	int dragType = -1;
	int dragIndex = -1;
	int dropIndex = -1;
	int dropType = -1;

public:
	virtual void initAllTime();
	virtual void initTime();
	virtual UTime getTime();
	virtual UTime getFrameTime();
	virtual void setPaused(bool paused);
public:

	bool visible = true;			//是否可见，只有可见状态才可以draw和检测鼠标进入
	bool activated = true;			//是否激活，只有处于激活状态时才可以draw和处理事件

	bool drawFullScreen = false;	//全屏绘图元素，绘制时从最外层的全屏元素开始，忽略其下面的元素
	bool rectFullScreen = false;	//为true时，忽略rect限制，在全屏范围检测鼠标事件
	bool eventOccupied = false;		//为true时，独占事件，处理事件后，其它元素不能处理事件
	bool coverMouse = true;			//为true时，可以检测鼠标是否进入，优先级高的元素会优先检测
	bool needEvents = true;			//是否需要处理事件

	bool needArrangeChild = true;
	bool canDraw = true;

	bool canDrag = false;			//是否可以拖拽
	bool canDrop = false;			//是否可以接受放置

	void dragEnd();
	void setRunning(bool sRunning) { running = sRunning; }

protected:

	int mouseLDownX = 0;
	int mouseLDownY = 0;
public:
	EventTouchID touchingID = TOUCH_UNTOUCHEDID;	//鼠标进入状态变量
	EventTouchID touchingDownID = TOUCH_UNTOUCHEDID;	//鼠标在区域中左键按下
private:
	UTime touchingDownTime = 0;
	const UTime clickCheckMaxTime = 1000;

protected:
	bool nextFrame = false;

	static EventTouchID dragging;
	static int dragParam[2];
	static PElement currentDragItem;
	static Point dragTouchPosition;
	static Point dragDownPosition;
	int dragRange = 1;	//鼠标拖拽移动判定的像素范围
public:
	void freeAllChildren();
protected:
	void reArrangeChildren();
	virtual bool mouseInRect(int x, int y);
private:
	//先交给每个child处理事件,再处理自身事件
	bool handleEvent(AEvent& e);
	void handleEvents();
	bool checkAllTouchDown(EventTouchID id, int x, int y);
	bool checkTouchDown(EventTouchID id, int x, int y);
	bool checkAllTouchUp(EventTouchID id, int x, int y);
	bool checkTouchUp(EventTouchID id, int x, int y);
	bool checkAllTouchMotion(EventTouchID id, int x, int y, bool touchChecked);
	bool checkTouchMotion(EventTouchID id, int x, int y, bool touchChecked);
	void clearTouch();
	void clearAllTouch();
	void runningElementClearAllTouch();
	void drawSelf();
	void drawAll();
	void update();
	void updateAll();

	void postTreatmentAll();
	void postTreatment();
	void preTreatmentAll();
	void preTreatment();

	void allHandleEvents();
	void frame();
	bool initial();
	void handleRun();
	void exit();
protected:
	void quit();
	bool running = false;

public:
	unsigned int run();
	unsigned int stop(int ret = erNone);
	unsigned int result = erNone;	//某些情况下的返回值
	Rect rect = { 0, 0, 0, 0 };		//元素占据的范围，用于鼠标进出检测等

	void freeAll();
	virtual void freeResource() { result = erNone; };
	virtual void initFromIni(std::string fileName) {};

	bool isDragging();

protected:
	virtual void onEvent() {};
	//处理事件，已处理返回true，未处理返回false，未处理的事件将继续交给其它元素进行处理
	virtual bool onHandleEvent(AEvent & e) { return false; };
	//鼠标进入触发的事件
	virtual void onMouseMoveIn(int x, int y) {};
	//鼠标移出触发的事件
	virtual void onMouseMoveOut() {};
	//鼠标在当前元素上面移动
	virtual void onMouseMoving(int x, int y) {};
	//鼠标左键按下触发的事件
	virtual void onMouseLeftDown(int x, int y) {};
	//鼠标左键抬起触发的事件
	virtual void onMouseLeftUp(int x, int y) {};
	//鼠标点击事件
	virtual void onClick() {};
	//拖拽放置事件
	virtual void onDrop(PElement src, int param1, int param2) {};
	//设置控件继承区域触发事件
	virtual void onSetChildRect() {};
	//拖拽事件
	virtual void onDragBegin(int* param1, int* param2) {};
	//正在拖拽，拖拽时每帧都会调用
	virtual void onDragging(int x, int y) {};
	//拖拽结束时调用，可在此函数中取消拖拽状态，避免放置事件触发
	virtual void onDragEnd(PElement dst, int x, int y) {};
	//画拖拽的画面
	virtual void onDrawDrag(int x, int y) {};
	//所需画的东西写在这里
	virtual void onDraw() {};
	//所有子元素都画完后调用
	virtual void onDrawEnd() {};
	//初始化时调用的事件
	virtual bool onInitial() { return true; };
	//在开始run并且初始化之后触发的事件
	virtual void onRun() {};
	//离开时触发的事件
	virtual void onExit() {};		
	//在每帧更新状态的触发事件
	virtual void onUpdate() {};

	//每帧开始时运行
	virtual void onPreTreatment() {};
	//每帧结束时运行
	virtual void onPostTreatment() {};

public:
	//供子元素回调
	virtual void onChildCallBack(PElement child) {};
};
