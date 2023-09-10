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

void Camera::flyTo(int dir, int distance)
{
	if (distance > 0)
	{
		setFlyTo(dir, distance);
		run();
	}
	if (position.x == gm->player->position.x && position.y == gm->player->position.y)
	{
		setFollowPlayer();
	}
}

void Camera::setFlyTo(int dir, int distance)
{
	followPlayer = false;
	distanceToFly = { 0.0, 0.0 };
	distanceFlied = { 0.0, 0.0 };
	flyStartPosition = position;
	flyStartOffset = offset;
	if (dir < 0)
	{
		dir = 7 - (-dir) % 8;
	}
	dir = dir % 8;
	flyDirection = dir;
	printf("dir:%d\n", flyDirection);
	switch (flyDirection)
	{
	case 0:
		distanceToFly.y = flyRatio * distance * TILE_HEIGHT;
		break;
	case 1:
		distanceToFly.y = flyRatio * distance * TILE_HEIGHT / 2;
		distanceToFly.x = -flyRatio * distance * TILE_WIDTH / 2;
		break;
	case 2:
		distanceToFly.x = -flyRatio * distance * TILE_WIDTH / 2;
		break;
	case 3:
		distanceToFly.y = -flyRatio * distance * TILE_HEIGHT / 2;
		distanceToFly.x = -flyRatio * distance * TILE_WIDTH / 2;
		break;
	case 4:
		distanceToFly.y = -flyRatio * distance * TILE_HEIGHT;
		break;
	case 5:
		distanceToFly.y = -flyRatio * distance * TILE_HEIGHT / 2;
		distanceToFly.x = flyRatio * distance * TILE_WIDTH / 2;
		break;
	case 6:
		distanceToFly.x = flyRatio * distance * TILE_WIDTH / 2;
		break;
	case 7:
		distanceToFly.y = flyRatio * distance * TILE_HEIGHT / 2;
		distanceToFly.x = flyRatio * distance * TILE_WIDTH / 2;
		break;
	default:
		break;
	}
	flying = true;
}

void Camera::setFollowPlayer()
{
	followPlayer = true;
}

void Camera::reArrangeNPC()
{
	if (gm->map.data == nullptr)
	{
		return;
	}
	int w, h;
	engine->getWindowSize(w, h);
	Point cenScreen;
	cenScreen.x = (int)w / 2;
	cenScreen.y = (int)h / 2;
	int xscal, yscal;
	xscal = cenScreen.x / TILE_WIDTH + 3;
	yscal = cenScreen.y / TILE_HEIGHT * 2 + 2;
	int tileHeightScal = 10;
	Point cenTile = position;
	std::vector<int> npcIndex;
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
	auto frameTime = getFrameTime();
	auto lastPosition = position;
	auto lastOffset = offset;
	if (followPlayer)
	{
		NPC * npcPlayer = gm->player;
		if (followNPC != nullptr && gm->npcManager.findNPC((NPC *)followNPC))
		{
			npcPlayer = (NPC *)followNPC;
		}
		else
		{
			followNPC = nullptr;
		}

		position = npcPlayer->position;
		offset = npcPlayer->offset;	
	
		if (gm->map.data != nullptr)
		{
			int mapw = gm->map.data->head.width;
			int maph = gm->map.data->head.height;
			int w, h;
			engine->getWindowSize(w, h);

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
		double l = hypot(distanceToFly.x, distanceToFly.y);
		PointEx frameFlyDistance = { distanceToFly.x / l * cameraSpeed * frameTime * SPEED_TIME, distanceToFly.y / l * cameraSpeed * frameTime * SPEED_TIME };
		distanceFlied = distanceFlied + frameFlyDistance;
		offset = flyStartOffset + distanceFlied;
		position = flyStartPosition;
		if (std::abs(distanceFlied.x) >= std::abs(distanceToFly.x) && std::abs(distanceFlied.y) >= std::abs(distanceToFly.y))
		{
			flying = false;
			running = false;
			offset = flyStartOffset + distanceToFly;
			position = flyStartPosition;
			distanceToFly = { 0.0, 0.0 };
			distanceFlied = { 0.0, 0.0 };
		}
		updatePosition();
	}
	auto differencePoint = gm->map.getTilePosition(position, lastPosition);
	differencePosition.y = ((double)differencePoint.y) - lastOffset.y + offset.y;
	differencePosition.x = ((double)differencePoint.x) - lastOffset.x + offset.x;
}

void Camera::onEvent()
{
	reArrangeNPC();
}
