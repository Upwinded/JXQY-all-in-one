#include "Camera.h"
#include "Map.h"
#include "../GameManager/GameManager.h"

Camera::Camera()
{
	priority = epCamera;
	needEvents = true;
}


Camera::~Camera()
{
}

void Camera::flyTo(Point d)
{
	setFlyTo(d);
	run();
	if (position.x == gm->player.position.x && position.y == gm->player.position.y)
	{
		setFollowPlayer();
	}
}

void Camera::setFlyTo(Point d)
{
	followPlayer = false;
	flyTime = 0;
	dest = d;
	Point pos = Map::getTilePosition(d, position, { 0, 0 }, { 0, 0 });
	pos.x -= (int)offset.x;
	pos.y -= (int)offset.y;
	double l = hypot(pos.x, pos.y);
	endFlyTime = (unsigned int)std::abs(l / TILE_WIDTH / (((double)cameraSpeed) * SPEED_TIME));
	flyingDirection = { pos.x, pos.y };
	flying = true;
}

void Camera::setFollowPlayer()
{
	followPlayer = true;
}

void Camera::reArrangeNPC()
{
	if (gm->map.data == NULL)
	{
		return;
	}
	int w, h;
	engine->getWindowSize(&w, &h);
	Point cenScreen;
	cenScreen.x = (int)w / 2;
	cenScreen.y = (int)h / 2;
	int xscal, yscal;
	xscal = cenScreen.x / TILE_WIDTH + 3;
	yscal = cenScreen.y / TILE_HEIGHT * 2 + 2;
	int tileHeightScal = 10;
	Point cenTile = position;
	std::vector<int> npcIndex = {};
	npcIndex.resize(0);
	for (int i = cenTile.y + yscal + tileHeightScal - 1; i >= cenTile.y - yscal; i--)
	{
		for (int j = cenTile.x - xscal; j < cenTile.x + xscal; j++)
		{
			if ( i >= 0 && i < gm->map.data->head.height && j >= 0 && j < gm->map.data->head.width)
			{
				for (size_t k = 0; k < gm->map.dataMap.tile[i][j].npcIndex.size(); k++)
				{
					if (gm->map.dataMap.tile[i][j].npcIndex[k] > 0)
					{
						npcIndex.push_back(gm->map.dataMap.tile[i][j].npcIndex[k] - 1);
					}					
				}
			}
		}
	}
	for (size_t i = 0; i < npcIndex.size(); i++)
	{
		gm->npcManager.removeChild(gm->npcManager.npcList[npcIndex[i]]);
		gm->npcManager.addChild(gm->npcManager.npcList[npcIndex[i]]);
	}
}

void Camera::onUpdate()
{
	unsigned int frameTime = getFrameTime();
	if (followPlayer)
	{
		position = gm->player.position;
		offset = gm->player.offset;
		
		if (gm->map.data != NULL)
		{
			int mapw = gm->map.data->head.width;
			int maph = gm->map.data->head.height;
			int w, h;
			engine->getWindowSize(&w, &h);

			int line = std::abs(position.y) % 2;		
			int hscal = h / TILE_HEIGHT * 2 + 2;
			if (hscal > maph)
			{
				int line2 = (maph / 2) % 2;
				if (line == line2)
				{
					position.y = maph / 2;
					if (line == 1)
					{
						offset.y = TILE_HEIGHT / 2;
					}
					else
					{
						offset.y = 0;
					}
				}
				else
				{
					if (line == 1)
					{
						position.y = maph / 2 - 1;
						offset.y = TILE_HEIGHT / 2;
					}
					else
					{
						position.y = maph / 2 + 1;
						offset.y = 0;
					}
				}
			}
			else
			{
				int line2 = std::abs(hscal / 2 - 1) % 2;
				int line3 = std::abs(maph - hscal / 2 - 2) % 2;
				if (line == 0)
				{
					if ((position.y < hscal / 2 - 1 - line2) || (position.y == hscal / 2 - 1 - line2 && offset.y < 0))
					{
						position.y = hscal / 2 - 1 - line2;
						offset.y = 0;
					}
					else if ((position.y > maph - hscal / 2 - 2 - line3) || (position.y == maph - hscal / 2 - 2 - line3 && offset.y > 0))
					{
						position.y = maph - hscal / 2 - 2 - line3;
						offset.y = 0;
					}
				}
				else
				{
					if ((position.y < hscal / 2 - 2 - line2) || (position.y == hscal / 2 - 2 - line2))
					{
						position.y = hscal / 2 - 2 - line2;
						offset.y = TILE_HEIGHT / 2;
					}
					else if ((position.y > maph - hscal / 2 - 1 - line3) || (position.y == maph - hscal / 2 - 1 - line3))
					{
						position.y = maph - hscal / 2 - 1 - line3;
						offset.y = -TILE_HEIGHT / 2;
					}
				}
			}

			line = std::abs(position.y) % 2;
			int wscal = w / TILE_WIDTH + 1;
			if (wscal > mapw)
			{
				position.x = mapw / 2;
				if (line == 1)
				{
					offset.x = 0;
				}
				else
				{
					offset.x = TILE_WIDTH / 2;
				}
			}
			else
			{
				if (line == 1)
				{
					if (position.x < wscal / 2 || (position.x == wscal / 2 && offset.x < 0))
					{
						position.x = wscal / 2;
						offset.x = 0;
					}
					else if ((position.x > mapw - wscal / 2 - 1) || (position.x == mapw - wscal / 2 - 1))
					{
						position.x = mapw - wscal / 2 - 1;
						offset.x = -TILE_WIDTH / 2;
					}
				}
				else
				{
					if (position.x < wscal / 2 || (position.x == wscal / 2))
					{
						position.x = wscal / 2;
						offset.x = TILE_WIDTH / 2;
					}
					else if ((position.x > mapw - wscal / 2 - 1) || (position.x == mapw - wscal / 2 - 1 && offset.x > 0))
					{
						position.x = mapw - wscal / 2 - 1;
						offset.x = 0;
					}
				}
			}
		}
	}
	else if (flying)
	{
		flyTime += frameTime;
		if (flyTime >= endFlyTime)
		{
			position = dest;
			flyingDirection = { 0, 0 };
			offset = { 0.0, 0.0 };
			flying = false;
			running = false;
		}
		else
		{
			updateFlyingPosition(frameTime, cameraSpeed);
		}
	}
}

void Camera::onEvent()
{
	reArrangeNPC();
}
