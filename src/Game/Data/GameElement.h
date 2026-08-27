#pragma once
#include "../../Element/Element.h"
#include "../GameTypes.h"
#include "math.h"

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

	_channel playSoundFile(const std::string & fileName, float x = 0.0f, float y = 0.0f, float volume = -1.0f);
	void getNewPosition(Point pos, PointEx off, Point * newPos, PointEx * newOff);
	void updateEffectPosition(UTime ftime, float flySpeed);
	void updateJumpingPosition(UTime ftime, float flySpeed);
	void updatePosition();


	//得到的是当前元素的像素位置,实际在中心点下方距离(TILE_HEIGHT/2)处
	//因所有元素位置都下移了，所以检测碰撞时，可以直接使用
	Point getScreenPosition(Point cenTile, PointEx cenOffset);
	Point getDrawPosition(std::shared_ptr<GameElement> camera);

	bool checkCollide(std::shared_ptr<GameElement> ge1, std::shared_ptr<GameElement> ge2);
	bool checkCollide(std::shared_ptr<GameElement> ge);

	virtual void onCollide(std::shared_ptr<GameElement> ge) {};

	virtual void draw() {};

	virtual void updateFrameTime() override;

	static int normalizeDir(int dir)
	{
		return ((dir % 8) + 8) % 8;
	}

protected:
	float soundVolume = 1.0;

};
