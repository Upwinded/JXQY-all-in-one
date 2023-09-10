#pragma once
#include "../../Element/Element.h"
#include "../GameTypes.h"
#include "math.h"

#define MapXRatio 1.414f

class GameElement:
	public Element
{
public:
	GameElement();
	virtual ~GameElement();

	int speed = 1;

	UTime actionBeginTime = 0;
	UTime actionLastTime = 0;

	int state = 0;

	//时间慢放倍数
	float timeSlow = 1.0f;

	//范围，以TILE_HEIGHT或TILE_WIDTH为单位
	float radius = 0.4f;

	Point position = { 0, 0 };
	PointEx offset = { 0.0, 0.0 };
	int direction = 0;
	Point flyingDirection = { 0, 0 };

	void playSoundFile(const std::string & fileName, float x = 0.0f, float y = 0.0f, float volume = -1.0f);
	void getNewPosition(Point pos, PointEx off, Point * newPos, PointEx * newOff);
	void updateEffectPosition(UTime ftime, double flySpeed);
	void updateJumpingPosition(UTime ftime, double flySpeed);
	void updatePosition();

	virtual UTime getFrameTime();
	virtual void beginNewState(int newState);

	//得到的是当前元素的像素位置,实际在中心点下方距离(TILE_HEIGHT/2)处
	//因所有元素位置都下移了，所以检测碰撞时，可以直接使用
	Point getPosition(Point cenTile, PointEx cenOffset);
	Point getPosition(GameElement * camera);

	bool checkCollide(GameElement * ge1, GameElement * ge2);
	bool checkCollide(GameElement * ge);

	virtual void onCollide(GameElement * ge) {};

	virtual void draw() {};

	UTime stateBeginTime = 0;

};

