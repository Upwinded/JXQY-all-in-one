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
}

void Camera::flyToEx(int dir, int distance)
{
	if (distance > 0)
	{
		setFlyTo(dir, distance);
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
	if (gm->map->data == nullptr)
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
	std::vector<std::shared_ptr<NPC>> npcList;
	for (int i = cenTile.y + yscal + tileHeightScal - 1; i >= cenTile.y - yscal; i--)
	{
		for (int j = cenTile.x - xscal; j < cenTile.x + xscal; j++)
		{
			if ( i >= 0 && i < gm->map->data->head.height && j >= 0 && j < gm->map->data->head.width)
			{
				for (auto iter = gm->map->dataMap.tile[i][j].npcList.begin(); iter != gm->map->dataMap.tile[i][j].npcList.end(); iter++)
				{
					if (*iter != nullptr && *iter != gm->player)
					{
						npcList.push_back(*iter);
					}					
				}
			}
		}
	}
	for (size_t i = 0; i < npcList.size(); i++)
	{
		gm->npcManager->removeChild(npcList[i]);
		gm->npcManager->addChild(npcList[i]);
	}
}

void Camera::onUpdate()
{
	auto frameTime = getFrameTime();
	auto lastPosition = position;
	auto lastOffset = offset;
	if (followPlayer)
	{
		std::shared_ptr<NPC> npcPlayer = gm->player;
		if (followNPC != nullptr && gm->npcManager->findNPC(std::dynamic_pointer_cast<NPC>(followNPC)))
		{
			npcPlayer = std::dynamic_pointer_cast<NPC>(followNPC);
		}
		else
		{
			followNPC = nullptr;
		}

		position = npcPlayer->position;
		offset = npcPlayer->offset;	
	
		if (gm->map->data != nullptr)
		{
			int mapw = gm->map->data->head.width;
			int maph = gm->map->data->head.height;
			int w, h;
			engine->getWindowSize(w, h);

			int line = std::abs(position.y) % 2;		
			int hscal = h / TILE_HEIGHT * 2 + 2;
			if (hscal + 1 > maph)
			{
				position.y = maph / 2;
				offset.y = -TILE_HEIGHT / 2;;
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
			if (wscal + 2 > mapw)
			{
				position.x = mapw / 2 - 1;
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
					else if (position.x >= mapw - wscal / 2 - 2)
					{
						position.x = mapw - wscal / 2 - 2;
						offset.x = -TILE_WIDTH / 2;
					}
				}
				else
				{
					if (position.x <= wscal / 2)
					{
						position.x = wscal / 2;
						offset.x = TILE_WIDTH / 2;
					}
					else if ((position.x > mapw - wscal / 2 - 2) || (position.x == mapw - wscal / 2 - 2 && offset.x > 0))
					{
						position.x = mapw - wscal / 2 - 2;
						offset.x = 0;
					}
				}
			}
		}
	}
	else if (flying)
	{
		double l = hypot(distanceToFly.x, distanceToFly.y);
		PointEx frameFlyDistance = { distanceToFly.x / l * cameraSpeed * frameTime * Config::getGameSpeed(), distanceToFly.y / l * cameraSpeed * frameTime * Config::getGameSpeed()};
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
		if (not flying)
		{
			if (position == gm->player->position)
			{
				setFollowPlayer();
			}
		}
	}
	auto differencePoint = gm->map->getTilePosition(position, lastPosition);
	differencePosition.y = ((double)differencePoint.y) - lastOffset.y + offset.y;
	differencePosition.x = ((double)differencePoint.x) - lastOffset.x + offset.x;
}

void Camera::onEvent()
{
	reArrangeNPC();
}
