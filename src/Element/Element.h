#pragma once

#include <vector>
#include <list>
#include <atomic>
#include <functional>
#include "../Types/Types.h"
#include "../Engine/MediaTypes.h"
#include "../Engine/Timer.h"
#include "../Image/IMP.h"
#include "../File/File.h"
#include "../File/INIReader.h"

//每两帧之间最大的间隔时间
#define MAX_FRAME_TIME 40

class Element;
class Engine;
enum class UIAction;

using PElement = std::shared_ptr<Element>;

class Element : public std::enable_shared_from_this<Element>
{
	friend class CoreLifecycleTestAccess;
	friend class GamepadEssentialUITestAccess;
	friend class GamepadRPGMenuActionsTestAccess;
	friend class GamepadWorldRuntimeTestAccess;
	friend class MobileExternalInputRuntimeTestAccess;
	friend class UIFocusTestAccess;
public:
	using WindowCloseConfirmationHandler = std::function<bool(Element&)>;
	using FrameGlobalInputHandler = std::function<void(Engine*)>;
	using FrameInputEventHandler = std::function<void(const AEvent&, Engine*)>;
	using FrameSemanticInputHandler = std::function<bool(Engine*)>;
	using FrameGameplayInputHandler = std::function<void(Engine*)>;
	using InputContextTransitionHandler = std::function<void()>;

	Element();
	virtual ~Element();
public:

	static std::list<Element*> memList;
	void ShowMemList();
	static bool resizeRunningRoots(int width, int height);
	static void dispatchFrameGlobalInput(Engine* engine);
	// Handle application-level events without dispatching ordinary gameplay/UI
	// input. Exclusive loading loops use this after joining their worker so a
	// close request is neither lost nor handled while live state is mutating.
	bool dispatchApplicationEvent(AEvent& event);
	// Dispatch a semantic UI action to the current run() stack owner. Nested
	// elements form a modal boundary even before they implement UI actions.
	static bool dispatchUIAction(UIAction action);
	static bool isCurrentRunOwner(const Element* element);
	static bool currentRunOwnerBlocksParentInput();
	// Reset once at application startup. ET_QUIT is latched across nested run()
	// calls so a modal cannot consume the request and resume its outer scene.
	static void resetApplicationQuitState();
	// Latch terminal application exit in both Element and Engine state so the
	// current frame and every nested run() owner stop together.
	static void requestApplicationQuit();
	static void setWindowCloseConfirmationHandler(
		WindowCloseConfirmationHandler handler);
	static void setFrameGlobalInputHandler(FrameGlobalInputHandler handler);
	static void setFrameInputEventHandler(FrameInputEventHandler handler);
	static void setFrameSemanticInputHandler(FrameSemanticInputHandler handler);
	static void setFrameGameplayInputHandler(FrameGameplayInputHandler handler);
	static void setInputContextTransitionHandler(InputContextTransitionHandler handler);
	static bool isFrameSemanticInputBlocked();
	// Temporarily suppress raw mouse/touch delivery for every current run() root.
	// Keyboard, window, semantic input and per-frame onEvent processing continue.
	static void setRawPointerInputBlocked(bool blocked);
	static bool isRawPointerInputBlocked();

	void addChild(PElement child);
	void removeChild(PElement child);
	void removeAllChild();
	void setChildActivated(PElement child, bool activated);
	//按照自身x,y位置设置子元素的区域
	void setChildRectReferToParent(int setLevel = -1);
	unsigned int getResult();
	bool getResult(unsigned int ret);
	bool handleUIAction(UIAction action) { return onHandleUIAction(action); }
	int index = -1;
	int type = -1;
	bool canCallBack = false;
protected:
	PElement getMySharedPtr();
	// Raw pointer transaction owners opt in explicitly. The aggregate count is
	// propagated through ancestors so high-frequency pointer motion only visits
	// branches that contain an observer instead of walking the world tree.
	void setPointerEventPreviewEnabled(bool enabled);

	Engine* engine = nullptr;

	static std::vector<PElement> runningElement;
	static std::atomic<bool> applicationQuitRequested;
	static WindowCloseConfirmationHandler windowCloseConfirmationHandler;
	static FrameGlobalInputHandler frameGlobalInputHandler;
	static FrameInputEventHandler frameInputEventHandler;
	static FrameSemanticInputHandler frameSemanticInputHandler;
	static FrameGameplayInputHandler frameGameplayInputHandler;
	static InputContextTransitionHandler inputContextTransitionHandler;
	static bool frameSemanticInputBlocked;
	static bool rawPointerInputBlocked;
	static bool inputContextStarted;
	static bool applicationTimersPaused;
	static std::vector<std::weak_ptr<Element>> applicationPausedElements;
	static void setRunningElementsPaused(bool paused);
	bool stopForApplicationQuit();
private:
	Timer timer;						//计时器
public:

	unsigned char getPriority() const { return priority; }
	void setPriority(unsigned char value);
	
	ElementType elementType = etElement;
	int elementTypeData = 0;

	std::string name = "Element";

	Element* parent = nullptr;
	std::vector<PElement> children;

	int dragType = -1;
	int dragIndex = -1;
	int dropIndex = -1;
	int dropType = -1;
private:
	virtual UTime getTimerTime();

protected:
	UTime frameTime = 0;
	UTime unifiedTime = 0;
	virtual void updateFrameTime();
public:
	virtual void initAllTime();
	virtual void initTime();
	virtual void setTime(UTime t);
	virtual UTime getTime() const;
	virtual UTime getFrameTime();
	virtual void setPaused(bool paused);
	bool isPaused();
public:

	bool visible = true;			//是否可见，只有可见状态才可以draw和检测鼠标进入
	bool activated = true;			//是否激活，只有处于激活状态时才可以draw和处理事件
	void setFocused(bool value) { focused = value; }
	bool isFocused() const { return focused; }

	bool drawFullScreen = false;	//全屏绘图元素，绘制时从最外层的全屏元素开始，忽略其下面的元素
	bool rectFullScreen = false;	//为true时，忽略rect限制，在全屏范围检测鼠标事件
	bool eventOccupied = false;		//为true时，独占事件，处理事件后，其它元素不能处理事件
	bool coverMouse = true;			//为true时，可以检测鼠标是否进入，优先级高的元素会优先检测
	bool needEvents = true;			//是否需要处理事件

	bool needArrangeChild = true;
	bool canDraw = true;
	bool childrenNeedRearrange = false;

	bool canDrag = false;			//是否可以拖拽
	bool canDrop = false;			//是否可以接受放置

	void dragEnd();
	void cancelPointerInteraction();
	void cancelPointerInteraction(EventTouchID pointerID);
	// Reports the concrete top-level hit transaction acquired during the
	// pointer-down pass. Raw event routing can use this to stop a handled UI
	// click before it reaches a lower scene without turning an entire
	// nonmodal panel into a full-screen pointer barrier.
	bool hasPointerDownInTree(EventTouchID pointerID) const;
	// Finds the concrete target that the child-first pointer traversal would
	// select at these event coordinates without changing hover/down state.
	Element* findPointerHitTargetInTree(int x, int y);
	void setRunning(bool sRunning) { logicRunning = sRunning; }

protected:

	bool focused = false;			//语义焦点，独立于鼠标和触摸状态
	int mouseLDownX = 0;
	int mouseLDownY = 0;
public:
	EventTouchID touchingID = TOUCH_UNTOUCHEDID;	//鼠标进入状态变量
	EventTouchID touchingDownID = TOUCH_UNTOUCHEDID;	//鼠标在区域中左键按下
private:
	UTime touchingDownTime = 0;
	const UTime clickCheckMaxTime = 1000;

protected:
	UTime getTouchingDownElapsedTime() const
	{
		return getTime() >= touchingDownTime ? getTime() - touchingDownTime : 0;
	}
	Point getTouchingDownMoveDelta(int x, int y) const
	{
		return { x - mouseLDownX, y - mouseLDownY };
	}

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
	void offsetRectTree(int dx, int dy);
	void reArrangeChildren();
	virtual bool mouseInRect(int x, int y);
	virtual bool shouldKeepTouchWhenPointerLeaves(int x, int y);
private:
	void adjustPointerEventPreviewObserverCount(int delta);
	void previewPointerEvent(AEvent& e);
	bool pointerEventPreviewEnabled = false;
	unsigned int pointerEventPreviewObserverCount = 0;
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
	void clearAllResults();
	void cancelPointerInteractionTree(EventTouchID pointerID);
	void cancelAllPointerInteractionsTree();
	void runningElementClearAllTouch();
	void drawSelf();
	void drawAll();
	void update();
	void updateAll();

	void postTreatmentAll();
	void postTreatment();
	void preTreatmentAll();
	void preTreatment();
	void resizeAll(int width, int height);

	void allHandleEvents();
	void frame();
	bool initial();
	void handleRun();
	void exit();
protected:
	void quit();
	bool logicRunning = false;
	bool autoFreeResourceOnExit = false;

private:
	unsigned char priority = 128;	//优先级，0最大，255最小，默认128

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
	// Observe pointer input after hit state changes but before child-first
	// consumption. Transaction owners use this to retain the original down
	// even when the concrete control handles the raw event itself.
	virtual void onPreviewPointerEvent(AEvent& e) { (void)e; };
	//处理事件，已处理返回true，未处理返回false，未处理的事件将继续交给其它元素进行处理
	virtual bool onHandleEvent(AEvent & e) { return false; };
	// Keyboard and gamepad mappings use this semantic path. Implementations
	// must not synthesize pointer state or write child result flags.
	virtual bool onHandleUIAction(UIAction action) { (void)action; return false; };
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
	virtual void getChildScaleFactor(float& scaleX, float& scaleY) { scaleX = 1.0f; scaleY = 1.0f; }
	virtual void getChildLayoutOffset(int& offsetX, int& offsetY) { offsetX = 0; offsetY = 0; }
	//拖拽事件
	virtual void onDragBegin(int* param1, int* param2) {};
	//正在拖拽，拖拽时每帧都会调用
	virtual void onDragging(int x, int y) {};
	//拖拽结束时调用，可在此函数中取消拖拽状态，避免放置事件触发
	virtual void onDragEnd(PElement dst, int x, int y) {};
	// Platform cancellation is not a release. Derived controls that retain
	// pointer-owned state outside touchingID/touchingDownID must clear it here
	// without committing click, drop, or drag-end behavior.
	virtual bool onPointerInteractionCanceled(EventTouchID pointerID)
	{
		(void)pointerID;
		return false;
	}
	virtual void onAllPointerInteractionsCanceled() {}
	//画拖拽的画面
	virtual void onDrawDrag(int x, int y) {};
	//所需画的东西写在这里
	virtual void onDraw() {};
	// Allow a scene to compose itself and all of its children into an
	// offscreen target before presenting that composition. Returning true
	// requires onEndDrawComposition() to restore the previous render target.
	virtual bool onBeginDrawComposition() { return false; }
	// Modal overlays can remain stable while the parent composition is
	// transformed. Deferred children are drawn after onEndDrawComposition().
	virtual bool shouldDrawChildAfterComposition(
		const PElement& child) const { (void)child; return false; }
	virtual void onEndDrawComposition(bool completed) { (void)completed; }
	//所有子元素都画完后调用
	virtual void onDrawEnd() {};
	//初始化时调用的事件
	virtual bool onInitial() { return true; };
	//在开始run并且初始化之后触发的事件
	virtual void onRun() {};
	//离开时触发的事件
	virtual void onExit() { if (autoFreeResourceOnExit) freeResource(); };
	//在每帧更新状态的触发事件
	virtual void onUpdate() {};
	virtual bool shouldUpdateChild(PElement child) { (void)child; return true; };
	virtual void onWindowResize(int width, int height) {};

	//每帧开始时运行
	virtual void onPreTreatment() {};
	//每帧结束时运行
	virtual void onPostTreatment() {};

public:
	//供子元素回调
	virtual void onChildCallBack(PElement child) {};


private:
	// test only
	static int update_deep;
};
