#pragma once
#include "GameElement.h"
#include <limits>

enum ObjectKind
{
	okOrnament = 0,
	okBox = 1,
	okBody = 2,
	okSound = 3,
	okRndSound = 4,
	okDoor = 5,
	okTrap = 6,
	okPickup = 7,
	okPickupLegacy = 8
};

inline bool isPickupObjectKind(int kind)
{
	return kind == okPickup || kind == okPickupLegacy;
}

inline bool isObjectObstacleKind(int kind)
{
	return kind == okOrnament || kind == okBox || kind == okDoor;
}

inline bool isObjectAutoPlayKind(int kind)
{
	return kind == okOrnament || kind == okTrap || isPickupObjectKind(kind);
}

inline bool shouldStartObjectResourceAnimation(int kind, bool animationLoaded, bool hasPersistedState)
{
	return isPickupObjectKind(kind) && animationLoaded && !hasPersistedState;
}

inline bool shouldUseObjectRightScriptForPrimaryInteraction(const std::string& scriptFile, const std::string& scriptFileRight)
{
	return scriptFile == "" && scriptFileRight != "";
}

inline bool canSelectObjectForInteraction(int scriptFileJustTouch, bool hasAnyInteractScript)
{
	return scriptFileJustTouch <= 0 && hasAnyInteractScript;
}

inline bool canTriggerObjectTouchScript(int scriptFileJustTouch, bool hasPrimaryInteractScript)
{
	return scriptFileJustTouch > 0 && hasPrimaryInteractScript;
}

inline bool canObjectTrapDamageNpcKind(int npcKind)
{
	// Matches C# Character.IsFighter for NPC-list occupants: Fighter(1) or Partner(3).
	return npcKind == 1 || npcKind == 3;
}

constexpr UTime OBJECT_TRAP_DAMAGE_CYCLE_UNSET = static_cast<UTime>(-1);

inline UTime getObjectTrapDamageCycle(UTime elapsedMilliseconds, UTime intervalMilliseconds)
{
	if (intervalMilliseconds == 0)
	{
		return OBJECT_TRAP_DAMAGE_CYCLE_UNSET;
	}
	return elapsedMilliseconds / intervalMilliseconds;
}

inline bool isObjectTrapDamageCycleDue(UTime elapsedMilliseconds, UTime intervalMilliseconds, UTime lastDamageCycle)
{
	UTime cycle = getObjectTrapDamageCycle(elapsedMilliseconds, intervalMilliseconds);
	return cycle != OBJECT_TRAP_DAMAGE_CYCLE_UNSET && cycle != lastDamageCycle;
}

enum ObjectLum
{
	olNone = 0,
	olRed = 1,
	olGreen = 2,
	olBlue = 3,
	olGray = 4,
	olAlpha = 5 // >= 5
};

enum ObjectAction
{
	oaStay = 0,
	oaPlaying = 1,
	oaOpening = 2,
	oaClosing = 3,
};

inline bool isObjectResourceAnimationFinished(int kind, int action, UTime elapsedMilliseconds, UTime durationMilliseconds)
{
	return isPickupObjectKind(kind)
		&& action == oaPlaying
		&& durationMilliseconds > 0
		&& elapsedMilliseconds >= durationMilliseconds;
}

inline UTime combineObjectActionElapsed(UTime persistedElapsed, UTime now, UTime beginTime)
{
	const UTime runtimeElapsed = now >= beginTime ? now - beginTime : 0;
	if (runtimeElapsed > std::numeric_limits<UTime>::max() - persistedElapsed)
	{
		return std::numeric_limits<UTime>::max();
	}
	return persistedElapsed + runtimeElapsed;
}

struct ObjectRes
{
	std::string imageFile = "";
	std::string shadowFile = "";
	std::string soundFile = "";
	std::string animationFile = "";
	std::string animationShadowFile = "";
	_shared_imp image = nullptr;
	_shared_imp shadow = nullptr;
	_shared_imp animation = nullptr;
	_shared_imp animationShadow = nullptr;
};

class Object :
	public GameElement
{
public:
	Object();
	virtual ~Object();

	void openBox();
	void closeBox();

	bool selecting = false;

	void drawAlpha(Point cenTile, Point cenScreen, PointEx coffset);
	void draw(Point cenTile, Point cenScreen, PointEx coffset, uint32_t colorStyle);

	int nowAction = oaStay;
	_shared_image getActionImage(int * offsetx, int * offsety);
	_shared_image getActionShadow(int * offsetx, int * offsety);

	std::string objName = "";
	std::string objectFile = "";
	std::string objectFileMovie = "";
	std::string scriptFile = "";
	std::string scriptFileRight = "";
	std::string timerScriptFile = "";
	std::string reviveNpcIni = "";
	std::string wavFile = "";
	int kind = okOrnament;
	int objectType = 0;
	int canInteractDirectly = 0;
	int scriptFileJustTouch = 0;
	int height = 0;

	int lum = olNone;
	int damage = 0;
	int frame = 0;

	UTime damageTime = 0;
	UTime damageInterval = 0;
	UTime lastTrapDamageCycle = OBJECT_TRAP_DAMAGE_CYCLE_UNSET;
	UTime randSoundTime = 0;
	UTime timerScriptInterval = DEFAULT_NPC_OBJ_TIME_SCRIPT_INTERVAL;
	UTime timerScriptElapsed = 0;
	UTime millisecondsToRemove = 0;

	_music sound = nullptr;
	_channel channel = nullptr;

	void initSound(const std::string & fileName);
	void initRes(const std::string & fileName);
	void setKind(int newKind);
	virtual void saveToIni(INIReader * ini, const std::string & section);
	virtual void initFromIni(INIReader * ini, const std::string & section);
	
	ObjectRes res;

	Point getPosition() const { return position; }
	void setPosition(Point newPos);
	PointEx getOffset() const { return offset; }
	void setOffset(PointEx newOffset);
	void removeFromDataMap();
	std::string getScriptFile(bool useRightScript = false) const;
	bool hasInteractScript(bool useRightScript = false) const;
	bool hasAnyInteractScript() const;
	bool shouldUseRightScriptForPrimaryInteraction() const;
	bool canSelectForInteraction() const;
	static bool isInteractDistanceReached(int distance, int canInteractDirectly)
	{
		return canInteractDirectly > 0 || distance <= 1;
	}
	bool canInteractAtDistance(int distance) const { return isInteractDistanceReached(distance, canInteractDirectly); }
	UTime getTrapDamageElapsedMilliseconds() const;
	UTime getActionElapsedMilliseconds() const;

	virtual bool mouseInRect(int x, int y);

private:
	void freeResource();
	void freeSound();
	void freeRes();
	void removeSelf();
	bool applyTrapDamage();
	void restartActionClock();
	UTime actionElapsedBase = 0;

	virtual void onUpdate();
	virtual void onEvent();
	virtual void onMouseLeftDown(int x, int y);
	virtual void onMouseLeftUp(int x, int y);
	virtual void onMouseMoveOut();

};
