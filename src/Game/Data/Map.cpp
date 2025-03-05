#include "Map.h"
#include "../GameManager/GameManager.h"

#ifdef pi
#undef pi
#endif // pi
#define pi 3.1415926

Map::Map()
{
	priority = epMap;
}

Map::~Map()
{
	freeResource();
}

bool Map::load(const std::string & fileName)
{
	freeMpc();
	freeData();
	GameLog::write("load map file %s\n", fileName.c_str());
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(fileName, s);
	if (s != nullptr && len > 0)
	{	
		bool ret = load(s, len);
		return ret;
	}
	GameLog::write("map file %s doesn't exist!\n", fileName.c_str());
	return false;
}

bool Map::load(std::unique_ptr<char[]>& temp_d, int len)
{
	auto d = temp_d.get();
#define mapReadData(_dst, _len) \
	memcpy(_dst, d, _len);\
	d += _len;\
	size -= _len;

	freeData();

	int size = len;
	//check length of head
	if (size >= MAP_HEAD_LEN)
	{
		data = std::make_shared<MapData>();
		if (data == nullptr)
		{
			return false;
		}
	}
	else
	{
		return false;
	}
	//read head
	mapReadData(&data->head, MAP_HEADSTR_LEN);

	if (!compareMapHead(data.get()))
	{
		data = nullptr;
		return false;
	}

	mapReadData(data->head.dataNil, MAP_nullptr);
	mapReadData(data->head.path, MAP_PATH);
	mapReadData(&data->head.dataLen, 4);
	mapReadData(&data->head.width, 4);
	mapReadData(&data->head.height, 4);
	mapReadData(&data->head.infoLen, 4);
	mapReadData(&data->head.nameLen, 4);
	mapReadData(data->head.dataNil2, MAP_nullptr_2);
	data->head.infoLen = 0x40;
	data->head.nameLen = 0x20;
	if (data->head.width < 0)
	{
		data->head.width = 0;
	}
	if (data->head.height < 0)
	{
		data->head.height = 0;
	}

	//check length of data
	
	if ((data->head.nameLen <= 0) || (data->head.infoLen != data->head.nameLen + 32) || (size < MAP_MPC_COUNT * data->head.infoLen + (int)sizeof(MapTile) * data->head.width * data->head.height))
	{
		data = nullptr;
		return false;
	}

	//read mpc info
	for (size_t i = 0; i < MAP_MPC_COUNT; i++)
	{
		data->mpc.mpc[i].name = std::make_unique<char[]>(data->head.nameLen);
		mapReadData(data->mpc.mpc[i].name.get(), data->head.nameLen);
		mapReadData(&data->mpc.mpc[i].index, 4);
		mapReadData(&data->mpc.mpc[i].dynamic, 4);
		mapReadData(&data->mpc.mpc[i].obstacle, 4);
		mapReadData(&data->mpc.mpc[i].nil, 4);
		d += 16;
		size -= 16;		
	}

	//read data
	data->tile.resize(data->head.height);
	for (int i = 0; i < data->head.height; i++)
	{
		data->tile[i].resize(data->head.width);
	}

	
	for (int i = 0; i < data->head.height; i++)
	{
		for (int j = 0; j < data->head.width; j++)
		{
			for (size_t k = 0; k < MAP_TILE_LAYER; k++)
			{
				mapReadData(&data->tile[i][j].layer[k].frame, 1);
				mapReadData(&data->tile[i][j].layer[k].mpc, 1);
			}
			mapReadData(&data->tile[i][j].obstacle, 1);
			mapReadData(&data->tile[i][j].trap, 1);
			mapReadData(&data->tile[i][j].end[0], 1);
			mapReadData(&data->tile[i][j].end[1], 1);
		}
	}

	//load map mpc
	loadMapMpc();

	initTime();

	return true;
}

Point Map::getElementPosition(Point pos, Point cenTile, Point cenScreen, PointEx cenTileOffset)
{
	pos.y -= TILE_HEIGHT / 2;
	return getMousePosition(pos, cenTile, cenScreen, cenTileOffset);
}

Point Map::getMousePosition(Point mouse, Point cenTile, Point cenScreen, PointEx cenTileOffset)
{
	Point point;
	int line = std::abs(cenTile.y % 2);
	cenScreen.x -= (int)round(cenTileOffset.x);
	cenScreen.y -= (int)round(cenTileOffset.y);
	float x = (float)(mouse.x - cenScreen.x);
	float y = (float)(mouse.y - cenScreen.y);
	float lx = y / TILE_HEIGHT - x / TILE_WIDTH;
	float ly = x / TILE_WIDTH + y / TILE_HEIGHT;
	int px = 0;
	int py = 0;
	if (lx < 0)
	{
		px = (int)lx;
	}
	else
	{
		px = (int)lx + 1;
	}
	if (ly < 0)
	{
		py = (int)ly;
	}
	else
	{
		py = (int)ly + 1;
	}
	point.y = px + py + cenTile.y;
	int line2 = std::abs(point.y % 2);
	if (line == line2)
	{
		point.x = (py - px) / 2 + cenTile.x;
	}
	else
	{
		if (line == 0)
		{
			if (py >= px)
			{
				point.x = (py - px) / 2 + cenTile.x;
			}
			else
			{
				point.x = (py - px) / 2 + cenTile.x - 1;
			}
		}
		else
		{
			if (py >= px)
			{
				point.x = (py - px) / 2 + cenTile.x + 1;
			}
			else
			{
				point.x = (py - px) / 2 + cenTile.x;
			}
		}
	}

	return point;
}

Point Map::getTilePosition(Point tile, Point cenTile, Point cenScreen, PointEx cenTileOffset)
{
	Point point;
	int line = std::abs(cenTile.y % 2);
	cenScreen.x -= (int)round(cenTileOffset.x);
	cenScreen.y -= (int)round(cenTileOffset.y);
	int line2 = std::abs(tile.y % 2);
	int x = tile.x - cenTile.x;
	int y = tile.y - cenTile.y;
	if (line == line2)
	{
		point.x = x * TILE_WIDTH + cenScreen.x;
		point.y = y * TILE_HEIGHT / 2 + cenScreen.y;
	}
	else
	{
		point.y = y * TILE_HEIGHT / 2 + cenScreen.y;
		if (line == 0)
		{
			point.x = x * TILE_WIDTH + cenScreen.x + TILE_WIDTH / 2;
		}
		else
		{
			point.x = x * TILE_WIDTH + cenScreen.x - TILE_WIDTH / 2;
		}
	}
	return point;
}

Point Map::getTileCenter(Point tile, Point cenTile, Point cenScreen, PointEx offset)
{
	Point pos = getTilePosition(tile, cenTile, cenScreen, offset);
	pos.y -= TILE_HEIGHT / 2;
	return pos;
}

//获得距离，从斜45度矫正为平视地图比例的菱形
double Map::getTileDistance(Point from, PointEx fromOffset, Point to, PointEx toOffset)
{
	auto pos = getTilePosition(to, from);
	return hypot(((double)pos.x + toOffset.x - fromOffset.x) / TILE_WIDTH  * MapXRatio, ((double)pos.y + toOffset.y - fromOffset.y) / TILE_HEIGHT);
}

void Map::loadMapMpc()
{
	freeMpc();

	engine->delay(1);
	if (data != nullptr)
	{
		mapMpc = std::make_shared<MapMpc>();

		for (size_t i = 0; i < MAP_MPC_COUNT; i++)
		{
			std::string mpcName = data->head.path;
			if (mpcName == "")
			{
				mpcName = "mpc\\map\\" + gm->mapName + "\\";
			}
			if (mpcName.length() > 1 && mpcName.c_str()[mpcName.length() - 1] != '\\')
			{
				mpcName += "\\";
			}
			mpcName += data->mpc.mpc[i].name.get();
			mapMpc->mpc[i].img = IMP::createIMPImage(mpcName);

		}

	}
}


int Map::NormalizeDirection(int direction)
{
    if (direction > 7)
    {
        direction = direction % 8;
    }
    if (direction < 0)
    {
        direction = direction % 8 + 8;
    }
    return direction;
}

std::deque<Point> Map::getPathAstar(Point from, Point to)
{
    // 定义节点结构体
    struct Node {
        Point pos;
        float g;  // 从起点到当前节点的实际代价
        float h;  // 从当前节点到目标节点的预估代价
        float f;  // f = g + h
        std::shared_ptr<Node> parent;

        Node(Point p, float g_val, float h_val, std::shared_ptr<Node> par)
            : pos(p), g(g_val), h(h_val), f(g_val + h_val), parent(par) {}

        // 重载比较运算符，用于优先队列
        bool operator>(const Node& other) const {
            return f > other.f;
        }
        bool operator<(const Node& other) const {
            return f < other.f;
        }
        bool operator==(const Node& other) const {
            return f == other.f;
        }
        
        class Compare
        {
        public:
            bool operator()(std::shared_ptr<Node> node1, std::shared_ptr<Node> node2)
            {
                return node1->f > node2->f;
            }
        };
    };
    

    
    std::deque<Point> path;
    if (to == from)
    {
        return path;
    }
    int w, h;
    if (data == nullptr)
    {
        return path;
    }
    
    w = data->head.width;
    h = data->head.height;
    if (w <= 0 || h <= 0)
    {
        return path;
    }
    
    if (!isInMap(from) || !isInMap(to))
    {
        return path;
    }
    
    // 开放列表，使用优先队列
    std::priority_queue<std::shared_ptr<Node>, std::vector<std::shared_ptr<Node>>, Node::Compare> openList;
    // 关闭列表
    std::vector<std::vector<bool>> closedList(h, std::vector<bool>(w, false));
    // 节点信息
    std::vector<std::vector<std::shared_ptr<Node>>> nodes(h, std::vector<std::shared_ptr<Node>>(w, nullptr));

    // 初始化起点节点
    auto startNode = std::make_shared<Node>(from, 0, calDistance(from, to), nullptr);
    openList.push(startNode);
    nodes[from.y][from.x] = startNode;
    int count_times = 0;
    while (!openList.empty() && (count_times++ < 128*128) )
    {
        // 取出 f 值最小的节点
        auto current = openList.top();
        openList.pop();

        if (current->pos == to)
        {
            // 找到目标节点，回溯路径
            std::shared_ptr<Node> temp = current;
            while (temp->parent)
            {
                path.push_front(temp->pos);
                temp = temp->parent;
            }
//            path.push_front(from);
            break;
        }

        // 将当前节点加入关闭列表
        closedList[current->pos.y][current->pos.x] = true;

        // 获取相邻节点
        for (int i = 0; i < 8; i++)
        {
            auto neighbor = getSubPoint(current->pos, i);
            if ((!canWalk(neighbor) && (neighbor != to)) || closedList[neighbor.y][neighbor.x])
            {
                continue;
            }
            
            if (i % 2 == 0)
            {
                if (!canPass(getSubPoint(current->pos, NormalizeDirection(i - 1))) || !canPass(getSubPoint(current->pos, NormalizeDirection(i + 1))))
                {
                    continue;
                }
            }
            
            float temp_tentativeG = 1.0f;
            if (i % 4 == 2)
            {
                temp_tentativeG = 1.414f * temp_tentativeG;
            }
            else if (i % 2 != 0)
            {
                temp_tentativeG = 1.732f / 2.0f * temp_tentativeG;
            }
            float tentativeG = current->g + temp_tentativeG;
            
            if (!nodes[neighbor.y][neighbor.x])
            {
                // 节点未被访问过
                auto newNode = std::make_shared<Node>(neighbor, tentativeG, calDistance(neighbor, to), current);
                openList.push(newNode);
                nodes[neighbor.y][neighbor.x] = newNode;
            }
            else if (tentativeG < nodes[neighbor.y][neighbor.x]->g)
            {
                // 找到更优路径
                nodes[neighbor.y][neighbor.x]->parent = current;
                nodes[neighbor.y][neighbor.x]->g = tentativeG;
                nodes[neighbor.y][neighbor.x]->f = tentativeG + nodes[neighbor.y][neighbor.x]->h;
                openList.push(nodes[neighbor.y][neighbor.x]);
            }
        }
    }
    return path;
}

std::deque<Point> Map::getPathTraversal(Point from, Point to)
{
	std::deque<Point> path;
	path.resize(0);
	if (to == from)
	{
		return path;
	}
	int w, h;
	if (data == nullptr)
	{
		return path;
	}
	w = data->head.width;
	h = data->head.height;
	if (w <= 0 || h <= 0)
	{
		return path;
	}

	PathMap pathMap;
	pathMap.map.resize(h);
	for (int i = 0; i < h; i++)
	{
		pathMap.map[i].resize(w);
	}
	pathMap.w = w;
	pathMap.h = h;

	if (!isInMap(&pathMap, from) || !isInMap(&pathMap, to))
	{
		return path;
	}
	/*
	if (!canWalk(to))
	{
		return path;
	}
	*/
	pathMap.map[from.y][from.x].index = 0;
	std::vector<Point> stepList;
	stepList.resize(0);
	stepList.push_back(from);
	int leftStep = MAX_STEP;

	bool found = false;
	while (leftStep--)
	{
		std::vector<Point> newList;
		newList.resize(0);
		for (size_t i = 0; i < stepList.size(); i++)
		{
			std::vector<Point> tempList = getSubStep(&pathMap, stepList[i], to, MAX_STEP - leftStep);
			if (pathMap.map[to.y][to.x].index >= 0)
			{
				found = true;
			}
			for (size_t i = 0; i < tempList.size(); i++)
			{
				newList.push_back(tempList[i]);
			}
		}
		if (found)
		{
			break;
		}
		if (newList.size() == 0)
		{
			break;
		}
		stepList = newList;
	}

	if (found)
	{
		Point step = to;
		path.insert(path.begin(), step);
		while (true)
		{
			step = pathMap.map[step.y][step.x].from;
			if (step == from)
			{
				break;
			}
			else
			{
				path.insert(path.begin(), step);
			}
		}
	}

	return path;
}

std::deque<Point> Map::getPath(Point from, Point to)
{
    return getPathAstar(from, to);
//    return getPathTraversal(from, to);
}

std::deque<Point> Map::getRadiusPath(Point from, Point to, int radius)
{	
	auto result = getPath(from, to);
	if (result.size() == 0)
	{
		return result;
	}
	if (radius >= (int)result.size() )
	{
		result.resize(0);
		return result;
	}
	for (int i = 0; i < radius; i++)
	{
		result.pop_back();
	}
	return result;
}

std::deque<Point> Map::getStepPath(Point from, Point to, int stepCount)
{
	std::deque<Point> result;
	Point pos = from;
	int stepIdx = 0;
	while (0 < stepCount--)
	{
		stepIdx++;
		if (pos == to)
		{
			return result;
		}
		int stepLength = 3;
		if (stepIdx == 1)
		{
			stepLength = 5;
		}
		for (int i = 0; i < stepLength; i++)
		{
			int d = i;
			switch (d)
			{
			case 2: 
				d = -1;
				break;
			case 3:
				d = 2;
				break;
			case 4:
				d = -2;
				break;
			default:
				break;
			}
			std::vector<Point> tempSteps = gm->map->getSubPointEx(pos, NPC::calDirection(pos, to) + d);
			if (tempSteps.size() > 0)
			{
				bool canContinue = true;
				for (size_t j = 0; j < tempSteps.size() - 1; j++)
				{
					if (!gm->map->canPass(tempSteps[j]))
					{
						canContinue = false;
					}
				}
				if (canContinue && tempSteps.size() > 0)
				{
					if (!gm->map->canWalk(tempSteps[tempSteps.size() - 1]))
					{
						canContinue = false;
					}
				}
				if (canContinue)
				{
					pos = tempSteps[tempSteps.size() - 1];
					result.push_back(pos);
					break;
				}
			}
		}	
	}
	return result;
}

std::deque<Point> Map::getStep(Point from, Point to)
{
	std::deque<Point> result;
	if (from == to)
	{
		return result;
	}
	int dir = NPC::calDirection(from, to);
	Point pos = gm->map->getSubPoint(from, dir);
	if (gm->map->canWalk(pos))
	{
		result.push_back(pos);
	}
	return result;
}

std::deque<Point> Map::getPassPath(Point from, Point to, Point flyDirection, Point dest)
{
	std::deque<Point> result;
	result.resize(0);
	result.push_back(from);

	if (flyDirection.is_zero())
	{
		return result;
	}
	if (from == to)
	{
		return result;
	}

	double angle = calFlyDirection(flyDirection);

	Point nowStep = from;
	int leftStep = calDistance(from, to);
	while (leftStep--)
	{
		bool nextStep = true;
		std::vector<Point> stepList = getLineSubStep(nowStep, dest, angle);
		for (size_t i = 0; i < stepList.size(); i++)
		{
			if (stepList[i] != to)
			{
				result.push_back(stepList[i]);
			}
			else
			{
				return result;
			}
		}
		nowStep = stepList[stepList.size() - 1];
	}
	return result;
}

std::deque<Point> Map::getPassPathEx(Point from, PointEx fromOffset, Point to, PointEx toOffset, Point flyDirection)
{
	std::deque<Point> result;
	result.resize(0);
	if (flyDirection.is_zero())
	{
		return result;
	}
	if (from == to)
	{
		return result;
	}
	double angle = calFlyDirection(flyDirection);
	int leftStep = calDistance(from, to) * 2;
	Point nowStep = from;
	PointEx nowOffset = fromOffset;
	result.push_back(nowStep);
	while ((nowStep != to) && (leftStep-- > 0))
	{
		auto nextStep = getLineSubStepEx(nowStep, nowOffset, angle);
		nowStep = nextStep.point;
		nowOffset = nextStep.pointEx;
		result.push_back(nowStep);
	}
	return result;
}

Point Map::getJumpPath(Point from, Point to)
{
	if (from == to)
	{
		return from;
	}
	double angle;
	if (from.x == to.x && std::abs(from.y - to.y) % 2 == 0)
	{
		if (from.y > to.y)
		{
			angle = pi / 2;
		}
		else
		{
			angle = pi * 3 / 2;
		}
	}
	else if (from.y == to.y)
	{
		if (from.x > to.x)
		{
			angle = pi;
		}
		else
		{
			angle = 0;
		}
	}
	else
	{
		Point pos = getTilePosition(to, from, { 0, 0 }, { 0, 0 });
		angle = atan2((double)-pos.y * ((double)TILE_WIDTH / TILE_HEIGHT), (double)pos.x);
		if (angle < 0)
		{
			angle += 2 * pi;
		}
	}
	Point lastStep = from;
	Point nowStep = from;
	while (true)
	{
		bool nextStep = true;
		std::vector<Point> stepList = getLineSubStep(nowStep, to, angle);
		if (stepList.size() == 0)
		{
			break;
		}
		for (size_t i = 0; i < stepList.size(); i++)
		{

			if (!canJump(stepList[i]))
			{
				nextStep = false;
				break;
			}
		}
		if (nextStep)
		{
			nowStep = stepList[stepList.size() - 1];

			if (canWalk(stepList[stepList.size() - 1]))
			{
				lastStep = stepList[stepList.size() - 1];
				if (haveTraps(nowStep))
				{
					break;
				}
			}
			if (stepList[stepList.size() - 1] == to)
			{
				break;
			}	
		}
		else
		{
			break;
		}
	}
	return lastStep;
}

bool Map::canView(Point from, Point to)
{
	if (from == to)
	{
		return true;
	}
	double angle;
	if (from.x == to.x && std::abs(from.y - to.y) % 2 == 0)
	{
		if (from.y > to.y)
		{
			angle = pi / 2;
		}
		else
		{
			angle = pi * 3 / 2;
		}
	}
	else if (from.y == to.y)
	{
		if (from.x > to.x)
		{
			angle = pi;
		}
		else
		{
			angle = 0;
		}
	}
	else
	{
		Point pos = getTilePosition(to, from, { 0, 0 }, { 0, 0 });
		angle = atan2((double)-pos.y * ((double)TILE_WIDTH / TILE_HEIGHT), (double)pos.x);
		if (angle < 0)
		{
			angle += 2 * pi;
		}
	}
	Point nowStep = from;
	while (true)
	{
		std::vector<Point> stepList = getLineSubStep(nowStep, to, angle);
		if (stepList.size() == 0)
		{
			return false;
		}
		bool canViewNext = false;
		for (size_t i = 0; i < stepList.size() - 1; i++)
		{
			if (canViewTile(stepList[i]))
			{
				canViewNext = true;
			}
		}
		if (stepList.size() > 1 && canViewNext == false)
		{
			return false;
		}
		if (stepList[stepList.size() - 1] == to)
		{
			return true;
		}
		if (!canViewTile(stepList[stepList.size() - 1]))
		{
			return false;
		}
		nowStep = stepList[stepList.size() - 1];
	}
	return false;
}


bool Map::canWalk(Point pos)
{
	if (pos.x < 0 || pos.y < 0 || pos.x >= data->head.width || pos.y >= data->head.height)
	{
		return false;
	}
	if (data->tile[pos.y][pos.x].obstacle != toJumpTrans && data->tile[pos.y][pos.x].obstacle != toJumpOpaque
		&& data->tile[pos.y][pos.x].obstacle != toTrans && data->tile[pos.y][pos.x].obstacle != toObstacle)
	{
		if (dataMap.tile[pos.y][pos.x].npcIndex.size() == 0 && dataMap.tile[pos.y][pos.x].stepIndex.size() == 0)
		{
			if (dataMap.tile[pos.y][pos.x].objIndex.size() == 0)
			{
				return true;
			}
			else
			{
				for (size_t i = 0; i < dataMap.tile[pos.y][pos.x].objIndex.size(); i++)
				{
					int objKind = gm->objectManager->objectList[dataMap.tile[pos.y][pos.x].objIndex[i]]->kind;
					if (objKind == okBox || objKind == okDoor || objKind == okOrnament)
					{
						return false;
					}
				}
				return true;
			}
		}
	}
	return false;
}

bool Map::canJump(Point pos)
{
	if (pos.x < 0 || pos.y < 0 || pos.x >= data->head.width || pos.y >= data->head.height)
	{
		return false;
	}
	if (data->tile[pos.y][pos.x].obstacle != toObstacle && data->tile[pos.y][pos.x].obstacle != toTrans)
	{
		/*if (dataMap.tile[pos.y][pos.x].npcIndex.size() == 0 && dataMap.tile[pos.y][pos.x].stepIndex.size() == 0)
		{
			
		}*/
		if (dataMap.tile[pos.y][pos.x].objIndex.size() > 0)
		{
			for (size_t i = 0; i < dataMap.tile[pos.y][pos.x].objIndex.size(); i++)
			{
				int objKind = gm->objectManager->objectList[dataMap.tile[pos.y][pos.x].objIndex[i]]->kind;
				if (objKind == okDoor)
				{
					return false;
				}
			}
		}
		return true;
	}
	return false;
}

int Map::getTrapIndex(Point pos)
{
	if (data->tile[pos.y][pos.x].trap != 0)
	{
		return data->tile[pos.y][pos.x].trap;
	}
	return 0;
}

std::string Map::getTrapName(Point pos)
{
	if (data->tile[pos.y][pos.x].trap != 0) 
	{
		return gm->traps.get(gm->mapName, data->tile[pos.y][pos.x].trap);
	}
	return "";
}

bool Map::haveTraps(Point pos)
{
	if (!data) { return false; }
	if (data->tile[pos.y][pos.x].trap == 0 || gm->traps.get(gm->mapName, data->tile[pos.y][pos.x].trap) == "")
	{
		return false;
	}
	return true;
}

bool Map::canFly(Point pos)
{
	if (pos.x < 0 || pos.y < 0 || pos.x >= data->head.width || pos.y >= data->head.height)
	{
		return false;
	}
	if (data->tile[pos.y][pos.x].obstacle != toObstacle && data->tile[pos.y][pos.x].obstacle != toJumpOpaque)
	{
		if (dataMap.tile[pos.y][pos.x].objIndex.size() == 0)
		{
			return true;
		}
		else
		{
			for (size_t i = 0; i < dataMap.tile[pos.y][pos.x].objIndex.size(); i++)
			{
				int objKind = gm->objectManager->objectList[dataMap.tile[pos.y][pos.x].objIndex[i]]->kind;
				if (objKind == okDoor || objKind == okOrnament)
				{
					return false;
				}
			}
			return true;
		}		
	}
	return false;
}

bool Map::canViewTile(Point pos)
{
	if (pos.x < 0 || pos.y < 0 || pos.x >= data->head.width || pos.y >= data->head.height)
	{
		return false;
	}
	if (data->tile[pos.y][pos.x].obstacle != toObstacle && data->tile[pos.y][pos.x].obstacle != toJumpOpaque)
	{
		if (dataMap.tile[pos.y][pos.x].objIndex.size() == 0)
		{
			return true;
		}
		else
		{
			for (size_t i = 0; i < dataMap.tile[pos.y][pos.x].objIndex.size(); i++)
			{
				int objKind = gm->objectManager->objectList[dataMap.tile[pos.y][pos.x].objIndex[i]]->kind;
				if (objKind == okDoor)
				{
					return false;
				}
			}
			return true;
		}
	}
	return false;
}

bool Map::canPass(Point pos)
{
	return (canWalk(pos) || canFly(pos));
	/*if (canWalk(pos))
	{
		return true;
	}
	if (canFly(pos))
	{
		if (dataMap.tile[pos.y][pos.x].npcIndex.size() == 0 && dataMap.tile[pos.y][pos.x].stepIndex.size() == 0)
		{
			if (dataMap.tile[pos.y][pos.x].objIndex.size() == 0)
			{
				return true;
			}
			else
			{
				for (size_t i = 0; i < dataMap.tile[pos.y][pos.x].objIndex.size(); i++)
				{
					int objKind = gm->objectManager->objectList[dataMap.tile[pos.y][pos.x].objIndex[i]]->kind;
					if (objKind == okBox || objKind == okDoor || objKind == okOrnament)
					{
						return false;
					}
				}
				return true;
			}
		}
	}
	return false;*/
}

Point Map::getSubPoint(Point from, int direction)
{
	if (direction < 0)
	{
		direction += 8;
	}
	else if (direction > 7)
	{
		direction -= 8;
	}
	int line = std::abs(from.y) % 2;
	Point to = from;
	switch (direction)
	{
	case 0:
		to.y += 2;
		break;
	case 1:
		to.y++;
		to.x += line - 1;
		break;
	case 2:
		to.x--;
		break;
	case 3:
		to.y--;
		to.x += line - 1;
		break;
	case 4:
		to.y -= 2;
		break;
	case 5:
		to.y--;
		to.x += line;
		break;
	case 6:
		to.x++;
		break;
	case 7:
		to.y++;
		to.x += line;
		break;
	default:
		break;
	}
	return to;
}

std::vector<Point> Map::getSubPointEx(Point from, int direction)
{

	std::vector<Point> result;
	if (direction < 0)
	{
		direction += 8;
	}
	else if (direction > 7)
	{
		direction -= 8;
	}
	int line = std::abs(from.y) % 2;
	Point to = from;
	if (direction % 2 == 0)
	{
		result.push_back(getSubPoint(from, direction - 1));
		result.push_back(getSubPoint(from, direction + 1));
	}
	switch (direction)
	{
	case 0:
		to.y += 2;
		break;
	case 1:
		to.y++;
		to.x += line - 1;
		break;
	case 2:
		to.x--;
		break;
	case 3:
		to.y--;
		to.x += line - 1;
		break;
	case 4:
		to.y -= 2;
		break;
	case 5:
		to.y--;
		to.x += line;
		break;
	case 6:
		to.x++;
		break;
	case 7:
		to.y++;
		to.x += line;
		break;
	default:
		break;
	}
	result.push_back(to);
	return result;
}

int Map::calDistance(Point from, Point to)
{
	int line1 = std::abs(from.y) % 2;
	int line2 = std::abs(to.y) % 2;
	if (line1 == line2)
	{
		return std::abs(to.y - from.y) / 2 + abs(to.x - from.x);
	}
	else
	{
		if (line1 == 0)
		{
			if (to.x >= from.x)
			{
				return std::abs(to.y - from.y) / 2 + 1 + to.x - from.x;
			}
			else
			{
				return std::abs(to.y - from.y) / 2 + from.x - to.x;
			}
		}
		else
		{
			if (to.x > from.x)
			{
				return std::abs(to.y - from.y) / 2 + to.x - from.x;

			}
			else
			{
				return std::abs(to.y - from.y) / 2 + 1 + from.x - to.x;
			}
		}
	}
}


void Map::drawMap()
{
	int w, h;
	engine->getWindowSize(w, h);
	Point cenScreen;
	cenScreen.x = (int)w / 2;
	cenScreen.y = (int)h / 2;
	int xscal, yscal;
	xscal = cenScreen.x / TILE_WIDTH + 2;
	yscal = cenScreen.y / TILE_HEIGHT * 2 + 2;
	int tileHeightScal = 15;
	Point cenTile = gm->camera->position;
	PointEx offset = gm->camera->offset;

	for (int i = cenTile.y - yscal; i < cenTile.y + yscal + tileHeightScal; i++)
	{
		for (int j = cenTile.x - xscal; j < cenTile.x + xscal; j++)
		{
			if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
			{
				continue;
			}
			Point tile = { j, i };
			drawTile(0, tile, cenTile, cenScreen, offset);
		}
	}

	EffectMap emap = gm->effectManager->createMap(cenTile.x - xscal, cenTile.y - yscal, xscal * 2, yscal * 2 + tileHeightScal);
	
	for (int i = cenTile.y - yscal; i < cenTile.y + yscal + tileHeightScal; i++)
	{
		for (int j = cenTile.x - xscal; j < cenTile.x + xscal; j++)
		{
			if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
			{
				continue;
			}
			for (size_t k = 0; k < emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index.size(); k++)
			{
				if (gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]] != nullptr)
				{
					if ((double)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->offset.y < 0 || (double)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.y > 0)
					{
						gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->draw(cenTile, cenScreen, offset);
					}
				}
			}
			
			Point tile = { j, i };
			drawTile(1, tile, cenTile, cenScreen, offset);

			for (size_t k = 0; k < dataMap.tile[i][j].objIndex.size(); k++)
			{
				gm->objectManager->drawOBJ(dataMap.tile[i][j].objIndex[k], cenTile, cenScreen, offset);
			}
			for (size_t k = 0; k < dataMap.tile[i][j].npcIndex.size(); k++)
			{
				if (dataMap.tile[i][j].npcIndex[k] == 0)
				{
					if (!gm->player->isJumping() || gm->player->jumpState != jsJumping)
					{
						gm->player->draw(cenTile, cenScreen, offset);
					}
				}
				else
				{
					gm->npcManager->drawNPC(dataMap.tile[i][j].npcIndex[k] - 1, cenTile, cenScreen, offset);
				}
			}

			for (size_t k = 0; k < emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index.size(); k++)
			{
				if (gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]] != nullptr)
				{
					if ((double)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->offset.y >= 0 && gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.y <= 0)
					{
						gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->draw(cenTile, cenScreen, offset);
					}
				}
			}
		}
	}
	/*
	int cline = abs(cenTile.y) % 2;
	for (int iy = cenTile.y - yscal - xscal * 2 - cline; iy < cenTile.y + yscal + tileHeightScal; iy += 2)
	{
		int line = std::abs(iy) % 2;
		int cx = cenTile.x;
		for (int ix = 0; ix < xscal * 2 + 2; ix++)
		{
			int i = iy + ix;
			int j = cx;
			cx += line - 1;
			line = 1 - line;
			if (i >= cenTile.y - yscal && i < cenTile.y + yscal + tileHeightScal && j >= cenTile.x - xscal && j < cenTile.x + xscal)
			{
				if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
				{
					continue;
				}
				for (size_t k = 0; k < emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index.size(); k++)
				{
					if (gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]] != nullptr)
					{
						if ((double)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.x / TILE_WIDTH + (double)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.y / TILE_WIDTH > 0)
						{
							gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->draw(cenTile, cenScreen, offset);
						}

					}
				}
			}
		}
		line = std::abs(iy) % 2;
		cx = cenTile.x + line;
		line = 1 - line;
		for (int ix = 1; ix < xscal * 2 + 2; ix++)
		{
			int i = iy + ix;
			int j = cx;
			cx += line;
			line = 1 - line;
			if (i >= cenTile.y - yscal && i < cenTile.y + yscal + tileHeightScal && j >= cenTile.x - xscal && j < cenTile.x + xscal)
			{
				if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
				{
					continue;
				}
				for (size_t k = 0; k < emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index.size(); k++)
				{
					if (gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]] != nullptr)
					{
						if (-(double)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.x / TILE_WIDTH + (double)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.y / TILE_WIDTH > 0)
						{
							gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->draw(cenTile, cenScreen, offset);
						}
					}
				}
			}
		}
		line = std::abs(iy) % 2;
		cx = cenTile.x;
		for (int ix = 0; ix < xscal * 2 + 2; ix++)
		{			
			int i = iy + ix;	
			int j = cx;
			cx += line - 1;
			line = 1 - line;
			if (i >= cenTile.y - yscal && i < cenTile.y + yscal + tileHeightScal && j >= cenTile.x - xscal && j < cenTile.x + xscal)
			{
				if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
				{
					continue;
				}				

				drawTile(1, { j, i }, cenTile, cenScreen, offset);

				for (size_t k = 0; k < dataMap.tile[i][j].objIndex.size(); k++)
				{
					gm->objectManager->drawOBJ(dataMap.tile[i][j].objIndex[k], cenTile, cenScreen, offset);
				}
				for (size_t k = 0; k < dataMap.tile[i][j].npcIndex.size(); k++)
				{
					if (dataMap.tile[i][j].npcIndex[k] == 0)
					{
						if (!gm->player->isJumping() || gm->player->jumpState != jsJumping)
						{
							gm->player->draw(cenTile, cenScreen, offset);
						}
					}
					else
					{
						gm->npcManager->drawNPC(dataMap.tile[i][j].npcIndex[k] - 1, cenTile, cenScreen, offset);
					}
				}
			}
		}
		line = std::abs(iy) % 2;
		cx = cenTile.x + line;
		line = 1 - line;
		for (int ix = 1; ix < xscal * 2 + 2; ix++)
		{
			int i = iy + ix;
			int j = cx;	
			cx += line;
			line = 1 - line;
			
			if (i >= cenTile.y - yscal && i < cenTile.y + yscal + tileHeightScal && j >= cenTile.x - xscal && j < cenTile.x + xscal)
			{
				if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
				{
					continue;
				}
				
				drawTile(1, { j, i }, cenTile, cenScreen, offset);

				for (size_t k = 0; k < dataMap.tile[i][j].objIndex.size(); k++)
				{
					gm->objectManager->drawOBJ(dataMap.tile[i][j].objIndex[k], cenTile, cenScreen, offset);
				}
				for (size_t k = 0; k < dataMap.tile[i][j].npcIndex.size(); k++)
				{
					if (dataMap.tile[i][j].npcIndex[k] == 0)
					{
						if (!gm->player->isJumping() || gm->player->jumpState != jsJumping)
						{
							gm->player->draw(cenTile, cenScreen, offset);
						}
					}
					else
					{
						gm->npcManager->drawNPC(dataMap.tile[i][j].npcIndex[k] - 1, cenTile, cenScreen, offset);
					}
				}	
			}
		}
		line = std::abs(iy) % 2;
		cx = cenTile.x;
		for (int ix = 0; ix < xscal * 2; ix++)
		{
			int i = iy + ix;
			int j = cx;
			cx += line - 1;
			line = 1 - line;
			if (i >= cenTile.y - yscal && i < cenTile.y + yscal + tileHeightScal && j >= cenTile.x - xscal && j < cenTile.x + xscal)
			{
				if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
				{
					continue;
				}
				for (size_t k = 0; k < emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index.size(); k++)
				{
					if (gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]] != nullptr)
					{
						if ((double)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.x / TILE_WIDTH + (double)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.y / TILE_WIDTH <= 0)
						{
							gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->draw(cenTile, cenScreen, offset);
						}
						
					}
				}
			}
		}
		line = std::abs(iy) % 2;
		cx = cenTile.x + line;
		line = 1 - line;
		for (int ix = 1; ix < xscal * 2; ix++)
		{
			int i = iy + ix;
			int j = cx;
			cx += line;
			line = 1 - line;
			if (i >= cenTile.y - yscal && i < cenTile.y + yscal + tileHeightScal && j >= cenTile.x - xscal && j < cenTile.x + xscal)
			{
				if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
				{
					continue;
				}
				for (size_t k = 0; k < emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index.size(); k++)
				{
					if (gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]] != nullptr)
					{
						if (-(double)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.x / TILE_WIDTH + (double)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.y / TILE_WIDTH <= 0)
						{
							gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->draw(cenTile, cenScreen, offset);
						}
					}
				}
			}		
		}

	}
	*/
	/*
	for (int i = cenTile.y - yscal; i < cenTile.y + yscal + tileHeightScal; i++)
	{
		for (int j = cenTile.x - xscal; j < cenTile.x + xscal; j++)
		{
			//npcManager->draw(tile, cenTile, cenScreen, offset);
			if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
			{
				continue;
			}
			drawTile(1, { j, i }, cenTile, cenScreen, offset);

			for (size_t k = 0; k < dataMap.tile[i][j].objIndex.size(); k++)
			{
				gm->objectManager->drawOBJ(dataMap.tile[i][j].objIndex[k], cenTile, cenScreen, offset);
			}
			for (size_t k = 0; k < dataMap.tile[i][j].npcIndex.size(); k++)
			{
				if (dataMap.tile[i][j].npcIndex[k] == 0)
				{
					if (!gm->player->isJumping() || gm->player->jumpState != jsJumping)
					{
						gm->player->draw(cenTile, cenScreen, offset);
					}
				}
				else
				{
					gm->npcManager->drawNPC(dataMap.tile[i][j].npcIndex[k] - 1, cenTile, cenScreen, offset);
				}
			}
			for (size_t k = 0; k < emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index.size(); k++)
			{
				if (gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]] != nullptr)
				{
					gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->draw(cenTile, cenScreen, offset);
				}
			}
		}
		for (int j = cenTile.x - xscal; j < cenTile.x + xscal; j++)
		{
			for (size_t k = 0; k < emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index.size(); k++)
			{
				if (gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]] != nullptr)
				{
					gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->draw(cenTile, cenScreen, offset);
				}
			}
		}
	}
	*/
	/*
	for (int i = 0; i < yscal * 2 + tileHeightScal; i++)
	{
		for (int j = 0; j < 2 * xscal; j++)
		{
			for (int k = 0; k < (int)emap.tile[i][j].index.size(); k++)
			{
				if (gm->effectManager->effectList[emap.tile[i][j].index[k]] != nullptr)
				{
					gm->effectManager->effectList[emap.tile[i][j].index[k]]->draw(cenTile, cenScreen, offset);
				}
			}
		}
	}
	*/
	
	for (int i = cenTile.y - yscal; i < cenTile.y + yscal + tileHeightScal; i++)
	{
		for (int j = cenTile.x - xscal; j < cenTile.x + xscal; j++)
		{
			if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
			{
				continue;
			}
			Point tile = { j, i };
			drawTile(2, tile, cenTile, cenScreen, offset);
		}
	}
	

	if (!gm->objectManager->drawOBJSelectedAlpha(cenTile, cenScreen, offset))
	{
		gm->npcManager->drawNPCSelectedAlpha(cenTile, cenScreen, offset);
	}
	if (gm->player->isJumping() && gm->player->jumpState == jsJumping)
	{
		gm->player->draw(cenTile, cenScreen, offset);
	}
	else if (Config::playerAlpha)
	{
		gm->player->drawAlpha(cenTile, cenScreen, offset);
	}
}

void Map::createDataMap()
{
	dataMap.tile.resize(0);
	int w, h;
	if (data == nullptr)
	{
		return;
	}
	w = data->head.width;
	h = data->head.height;
	if (w <= 0 || h <= 0)
	{
		return;
	}
	dataMap.tile.resize(h);
	for (int i = 0; i < h; i++)
	{
		dataMap.tile[i].resize(w);
		for (int j = 0; j < w; j++)
		{
			dataMap.tile[i][j].npcIndex.resize(0);
			dataMap.tile[i][j].objIndex.resize(0);
			dataMap.tile[i][j].stepIndex.resize(0);
		}
	}
	Point pos = gm->player->position;
	if (pos.x >= 0 && pos.x < 0 + w && pos.y >= 0 && pos.y < 0 + h)
	{
		dataMap.tile[pos.y][pos.x].npcIndex.push_back(0);
		if (gm->player->isWalking() || gm->player->isRunning())
		{
			if (gm->player->stepState == ssOut && gm->player->stepList.size() > 0)
			{
				addStepToDataMap(gm->player->stepList[0], 0);
			}
		}
		else if (gm->player->isJumping())
		{
			if (gm->player->jumpState != jsDown && gm->player->stepList.size() > 0)
			{
				addStepToDataMap(gm->player->stepList[0], 0);
			}
		}
	}
	for (size_t i = 0; i < gm->npcManager->npcList.size(); i++)
	{
		if (gm->npcManager->npcList[i] != nullptr && gm->npcManager->npcList[i]->position.x >= 0 && gm->npcManager->npcList[i]->position.x < w && gm->npcManager->npcList[i]->position.y >= 0 && gm->npcManager->npcList[i]->position.y < h)
		{
			gm->npcManager->npcList[i]->npcIndex = i + 1;
			dataMap.tile[gm->npcManager->npcList[i]->position.y][gm->npcManager->npcList[i]->position.x].npcIndex.push_back(i + 1);
		}
		if (gm->npcManager->npcList[i] != nullptr && (gm->npcManager->npcList[i]->isWalking() || gm->npcManager->npcList[i]->isRunning()))
		{
			if (gm->npcManager->npcList[i]->stepState == ssOut && gm->npcManager->npcList[i]->stepList.size() > 0)
			{
				addStepToDataMap(gm->npcManager->npcList[i]->stepList[0], i + 1);
			}
		}
	}
	for (size_t i = 0; i < gm->objectManager->objectList.size(); i++)
	{
		if (gm->objectManager->objectList[i] != nullptr && gm->objectManager->objectList[i]->position.x >= 0 && gm->objectManager->objectList[i]->position.x < w && gm->objectManager->objectList[i]->position.y >= 0 && gm->objectManager->objectList[i]->position.y < h)
		{
			dataMap.tile[gm->objectManager->objectList[i]->position.y][gm->objectManager->objectList[i]->position.x].objIndex.push_back(i);
		}
	}
}

void Map::deleteObjectFromDataMap(Point pos, int idx)
{
	if (pos.x >= 0 && pos.y >= 0 && pos.x < data->head.width && pos.y < data->head.height)
	{
		int found = -1;
		for (size_t i = 0; i < dataMap.tile[pos.y][pos.x].objIndex.size(); i++)
		{
			if (dataMap.tile[pos.y][pos.x].objIndex[i] == idx)
			{
				found = i;
				break;
			}
		}
		if (found >= 0)
		{
			dataMap.tile[pos.y][pos.x].objIndex.erase(dataMap.tile[pos.y][pos.x].objIndex.begin() + found);
		}
	}
}

void Map::addObjectToDataMap(Point pos, int idx)
{
	if (pos.x >= 0 && pos.y >= 0 && pos.x < data->head.width && pos.y < data->head.height)
	{
		dataMap.tile[pos.y][pos.x].objIndex.push_back(idx);
	}
}

void Map::changeObjectInDataMap(Point pos, int idx, int newIdx)
{
	if (pos.x >= 0 && pos.y >= 0 && pos.x < data->head.width && pos.y < data->head.height)
	{
		for (size_t i = 0; i < dataMap.tile[pos.y][pos.x].stepIndex.size(); i++)
		{
			if (dataMap.tile[pos.y][pos.x].stepIndex[i] == idx)
			{
				dataMap.tile[pos.y][pos.x].stepIndex[i] = newIdx;
				break;
			}
		}
	}
}

void Map::deleteStepFromDataMap(Point pos, int idx)
{
	if (pos.x >= 0 && pos.y >= 0 && pos.x < data->head.width && pos.y < data->head.height)
	{
		int found = -1;
		for (size_t i = 0; i < dataMap.tile[pos.y][pos.x].stepIndex.size(); i++)
		{
			if (dataMap.tile[pos.y][pos.x].stepIndex[i] == idx)
			{
				found = i;
				break;
			}
		}
		if (found >= 0)
		{
			dataMap.tile[pos.y][pos.x].stepIndex.erase(dataMap.tile[pos.y][pos.x].stepIndex.begin() + found);
		}
	}
}

void Map::addStepToDataMap(Point pos, int idx)
{
	if (pos.x >= 0 && pos.y >= 0 && pos.x < data->head.width && pos.y < data->head.height)
	{
		dataMap.tile[pos.y][pos.x].stepIndex.push_back(idx);
	}
}

void Map::changeStepDataMap(Point pos, int idx, int newIdx)
{
	if (pos.x >= 0 && pos.y >= 0 && pos.x < data->head.width && pos.y < data->head.height)
	{
		for (size_t i = 0; i < dataMap.tile[pos.y][pos.x].stepIndex.size(); i++)
		{
			if (dataMap.tile[pos.y][pos.x].stepIndex[i] == idx)
			{
				dataMap.tile[pos.y][pos.x].stepIndex[i] = newIdx;
				break;
			}
		}
	}
}

void Map::deleteNPCFromDataMap(Point pos, int idx)
{
	if (pos.x >= 0 && pos.y >= 0 && pos.x < data->head.width && pos.y < data->head.height)
	{
		int found = -1;
		for (size_t i = 0; i < dataMap.tile[pos.y][pos.x].npcIndex.size(); i++)
		{
			if (dataMap.tile[pos.y][pos.x].npcIndex[i] == idx)
			{
				found = i;
				break;
			}
		}
		if (found >= 0)
		{
			for (size_t i = found; i < dataMap.tile[pos.y][pos.x].npcIndex.size() - 1; i++)
			{
				dataMap.tile[pos.y][pos.x].npcIndex[i] = dataMap.tile[pos.y][pos.x].npcIndex[i + 1];
			}
			dataMap.tile[pos.y][pos.x].npcIndex.resize(dataMap.tile[pos.y][pos.x].npcIndex.size() - 1);
		}
	}
}

void Map::addNPCToDataMap(Point pos, int idx)
{
	if (pos.x >= 0 && pos.y >= 0 && pos.x < data->head.width && pos.y < data->head.height)
	{
		dataMap.tile[pos.y][pos.x].npcIndex.push_back(idx);
	}
}

void Map::changeNPCInDataMap(Point pos, int idx, int newIdx)
{
	if (pos.x >= 0 && pos.y >= 0 && pos.x < data->head.width && pos.y < data->head.height)
	{
		for (size_t i = 0; i < dataMap.tile[pos.y][pos.x].npcIndex.size(); i++)
		{
			if (dataMap.tile[pos.y][pos.x].npcIndex[i] == idx)
			{
				dataMap.tile[pos.y][pos.x].npcIndex[i] = newIdx;
				break;
			}
		}
	}
}

void Map::freeResource()
{
	freeMpc();
	freeData();
}

void Map::freeMpc()
{
	if (mapMpc != nullptr)
	{
		for (size_t i = 0; i < MAP_MPC_COUNT; i++)
		{
			mapMpc->mpc[i].img = nullptr;
		}
		mapMpc = nullptr;
	}
}

void Map::freeData()
{
	if (data != nullptr)
	{
		data->tile.resize(0);
		for (size_t i = 0; i < MAP_MPC_COUNT; i++)
		{
			if (data->mpc.mpc[i].name.get() != nullptr)
			{
				data->mpc.mpc[i].name = std::unique_ptr<char[]>(nullptr);
			}
		}
		data = nullptr;
	}
}

void Map::drawTile(int layer, Point tile, Point cenTile, Point cenScreen, PointEx offset)
{
	
	if (data->tile[tile.y][tile.x].layer[layer].mpc == 0)
	{
		return;
	}
	int x, y;
	Point point = getTilePosition(tile, cenTile, cenScreen, offset);
	if (data->mpc.mpc[data->tile[tile.y][tile.x].layer[layer].mpc - 1].dynamic != 0)
	{
		_shared_image img = IMP::loadImageForTime(mapMpc->mpc[data->tile[tile.y][tile.x].layer[layer].mpc - 1].img, getTime(), &x, &y);	
		engine->drawImage(img, point.x - x, point.y - y);
	}
	else
	{
		_shared_image img = IMP::loadImage(mapMpc->mpc[data->tile[tile.y][tile.x].layer[layer].mpc - 1].img, data->tile[tile.y][tile.x].layer[layer].frame, &x, &y);
		engine->drawImage(img, point.x - x, point.y - y);
	}	

}

bool Map::isInMap(PathMap * pathMap, Point pos)
{
	if (pos.x < 0 || pos.y < 0 || pos.x >= pathMap->w || pos.y >= pathMap->h)
	{
		return false;
	}
	return true;
}

bool Map::isInMap(Point pos)
{
	if (pos.x < 0 || pos.y < 0 || pos.x >= data->head.width || pos.y >= data->head.height)
	{
		return false;
	}
	return true;
}

double Map::calFlyDirection(Point flyDirection)
{
	double angle;
	if (flyDirection.x == 0)
	{
		if (flyDirection.y > 0)
		{
			angle = pi * 3 / 2;
		}
		else
		{
			angle = pi / 2;
		}
	}
	else if (flyDirection.y == 0)
	{
		if (flyDirection.x > 0)
		{
			angle = 0;
		}
		else
		{
			angle = pi;
		}
	}
	else
	{
		//angle = atan2((double)-flyDirection.y * TILE_HEIGHT / TILE_WIDTH, (double)flyDirection.x);
		angle = atan2((double)-flyDirection.y, (double)flyDirection.x);
		if (angle < 0)
		{
			angle += 2 * pi;
		}
	}
	return angle;
}

LinePathPoint Map::getLineSubStepEx(Point from, PointEx fromOffset, double angle)
{
	LinePathPoint result;
	result.point = from;
	result.pointEx = fromOffset;
	result.pointEx.x /= (TILE_WIDTH / 2 / MapXRatio);
	result.pointEx.y /= (TILE_HEIGHT / 2);
	int dir = 0;
	if (angle <= pi / 4 || angle > pi * 7 / 4)
	{		
		auto p = atan2(result.pointEx.y, 1.0 - result.pointEx.x);
		if (angle > pi)
		{
			angle -= 2 * pi;
		}
		if (p > angle)
		{
			result.point = getSubPoint(from, 7);
			dir = 7;
		}
		else
		{
			result.point = getSubPoint(from, 5);
			dir = 5;
		}
		if (angle < 0)
		{
			angle += 2 * pi;
		}
	}
	else if (angle <= pi * 3 / 4)
	{
		auto p = atan2(1.0 + result.pointEx.y, - result.pointEx.x);
		if (p < 0)
		{
			p += 2 * pi;
		}
		if (p > angle)
		{
			result.point = getSubPoint(from, 5);
			dir = 5;
		}
		else
		{
			result.point = getSubPoint(from, 3);
			dir = 3;
		}
	}
	else if (angle <= pi * 5 / 4)
	{
		auto p = atan2(result.pointEx.y, -1.0 - result.pointEx.x);
		if (p < 0)
		{
			p += 2 * pi;
		}
		if (p > angle)
		{
			result.point = getSubPoint(from, 3);
			dir = 3;
		}
		else
		{
			result.point = getSubPoint(from, 1);
			dir = 1;
		}
	}
	else
	{
		auto p = atan2(-1.0 + result.pointEx.y, - result.pointEx.x);
		if (p < 0)
		{
			p += 2 * pi;
		}
		if (p > angle)
		{
			result.point = getSubPoint(from, 1);
			dir = 1;
		}
		else
		{
			result.point = getSubPoint(from, 7);
			dir = 7;
		}
	}
	auto newpos = getTilePosition(result.point, from);
	result.pointEx.x -= double(newpos.x) / (TILE_WIDTH / 2);
	result.pointEx.y -= double(newpos.y) / (TILE_HEIGHT / 2);
	result.pointEx.x *= (TILE_WIDTH / 2);
	result.pointEx.y *= (TILE_HEIGHT / 2);
	return result;
}

std::vector<Point> Map::getLineSubStep(Point from, Point to, double angle)
{
	std::vector<Point> result;
	result.resize(0);
	if (from == to)
	{
		result.push_back(from);
		return result;
	}
	//angle以向右为x、向上为y正方向的坐标系，与屏幕向下y增大的方向相反
	//向右时
	int line = std::abs(from.y) % 2;
	if (angle == 0.0 || angle == 2 * pi)
	{
		Point pos;
		pos.x = from.x + line;
		pos.y = from.y - 1;
		result.push_back(pos);
		pos.x = from.x + line;
		pos.y = from.y + 1;
		result.push_back(pos);
		pos.x = from.x + 1;
		pos.y = from.y;
		result.push_back(pos);
	}
	//向上时
	else if (angle == pi / 2)
	{
		Point pos;
		pos.x = from.x - 1 + line;
		pos.y = from.y - 1;
		result.push_back(pos);
		pos.x = from.x + line;
		pos.y = from.y - 1;
		result.push_back(pos);
		pos.x = from.x;
		pos.y = from.y - 2;
		result.push_back(pos);
	}
	//向左时
	else if (angle == pi)
	{
		Point pos;
		pos.x = from.x - 1 + line;
		pos.y = from.y - 1;
		result.push_back(pos);
		pos.x = from.x - 1 + line;
		pos.y = from.y + 1;
		result.push_back(pos);
		pos.x = from.x - 1;
		pos.y = from.y;
		result.push_back(pos);
	}
	//向下时
	else if (angle == pi * 3 / 2)
	{
		Point pos;
		pos.x = from.x - 1 + line;
		pos.y = from.y + 1;
		result.push_back(pos);
		pos.x = from.x + line;
		pos.y = from.y + 1;
		result.push_back(pos);
		pos.x = from.x;
		pos.y = from.y + 2;
		result.push_back(pos);
	}
	else
	{
		Point newPos = getTilePosition(to, from, { 0, 0 }, { 0, 0 });
		double newAngle = atan2((double)-newPos.y * ((double)TILE_WIDTH / TILE_HEIGHT), (double)newPos.x);
		if (newAngle < 0)
		{
			newAngle += 2 * pi;
		}
		if (angle == atan2(1, 1))
		{
			Point pos;
			pos.x = from.x + line;
			pos.y = from.y - 1;
			result.push_back(pos);
		}
		else if (angle == atan2(1, -1))
		{
			Point pos;
			pos.x = from.x - 1 + line;
			pos.y = from.y - 1;
			result.push_back(pos);
		}
		else if (angle == atan2(-1, -1))
		{
			Point pos;
			pos.x = from.x - 1 + line;
			pos.y = from.y + 1;
			result.push_back(pos);
		}
		else if (angle == atan2(-1, 1))
		{
			Point pos;
			pos.x = from.x + line;
			pos.y = from.y + 1;
			result.push_back(pos);
		}
		else if (angle < pi / 4 || angle > 7 * pi / 4)
		{
			if (angle > pi)
			{
				angle -= 2 * pi;				
			}
			if (newAngle > pi)
			{
				newAngle -= 2 * pi;
			}
			if (newAngle < angle)
			{
				Point pos;
				pos.x = from.x + line;
				pos.y = from.y + 1;
				result.push_back(pos);
			}
			else if (newAngle > angle)
			{
				Point pos;
				pos.x = from.x + line;
				pos.y = from.y - 1;
				result.push_back(pos);
			}
			else
			{
				if (angle < 0)
				{
					Point pos;
					pos.x = from.x + line;
					pos.y = from.y + 1;
					result.push_back(pos);
				}
				else
				{
					Point pos;
					pos.x = from.x + line;
					pos.y = from.y - 1;
					result.push_back(pos);
				}			
			}
		}
		else if (angle > pi / 4 && angle < 3 * pi / 4)
		{
			if (newAngle < angle)
			{
				Point pos;
				pos.x = from.x + line;
				pos.y = from.y - 1;
				result.push_back(pos);
			}
			else if (newAngle > angle)
			{
				Point pos;
				pos.x = from.x - 1 + line;
				pos.y = from.y - 1;
				result.push_back(pos);
			}
			else
			{
				if (angle < pi / 2)
				{
					Point pos;
					pos.x = from.x + line;
					pos.y = from.y - 1;
					result.push_back(pos);
				}
				else
				{
					Point pos;
					pos.x = from.x - 1 + line;
					pos.y = from.y - 1;
					result.push_back(pos);
				}
			}
		}
		else if (angle > 3 * pi / 4 && angle < 5 * pi / 4)
		{
			if (newAngle < angle)
			{
				Point pos;
				pos.x = from.x - 1 + line;
				pos.y = from.y - 1;
				result.push_back(pos);
			}
			else if (newAngle > angle)
			{
				Point pos;
				pos.x = from.x - 1 + line;
				pos.y = from.y + 1;
				result.push_back(pos);
			}
			else
			{
				if (angle < pi)
				{
					Point pos;
					pos.x = from.x - 1 + line;
					pos.y = from.y - 1;
					result.push_back(pos);
				}
				else
				{
					Point pos;
					pos.x = from.x - 1 + line;
					pos.y = from.y + 1;
					result.push_back(pos);
				}
			}
		}
		else
		{
			if (newAngle < angle)
			{
				Point pos;
				pos.x = from.x - 1 + line;
				pos.y = from.y + 1;
				result.push_back(pos);
			}
			else if (newAngle > angle)
			{
				Point pos;
				pos.x = from.x + line;
				pos.y = from.y + 1;
				result.push_back(pos);
			}
			else
			{
				if (angle < pi * 3 / 2)
				{
					Point pos;
					pos.x = from.x - 1 + line;
					pos.y = from.y + 1;
					result.push_back(pos);
				}
				else
				{
					Point pos;
					pos.x = from.x + line;
					pos.y = from.y + 1;
					result.push_back(pos);
				}
			}
		}
	}
	return result;
}

bool Map::getSlantPath(std::vector<Point>& subStep, int line, PathMap* pathMap, Point from, Point to, int stepIndex)
{
	Point pos;
	for (int i = line - 1; i < line + 1; i++)
	{
		for (int j = -1; j < 2; j += 2)
		{
			pos.x = from.x + i;
			pos.y = from.y + j;
			if (pos == to)
			{
				pathMap->map[pos.y][pos.x].from = from;
				pathMap->map[pos.y][pos.x].index = stepIndex;
				subStep.push_back(pos);
				return true;
			}
			else if (isInMap(pathMap, pos) && canWalk(pos))
			{
				if (pathMap->map[pos.y][pos.x].index < 0)
				{
					pathMap->map[pos.y][pos.x].from = from;
					pathMap->map[pos.y][pos.x].index = stepIndex;
					auto tileCost = getTileDistance(pos, { 0, 0 }, from, { 0, 0 });
					pathMap->map[pos.y][pos.x].cost = tileCost + pathMap->map[from.y][from.x].cost;
					subStep.push_back(pos);
				}
				else if (pathMap->map[pos.y][pos.x].index == stepIndex)
				{
					auto tileCost = getTileDistance(pos, { 0, 0 }, from, { 0, 0 });
					auto totalCost = tileCost + pathMap->map[from.y][from.x].cost;
					if (pathMap->map[pos.y][pos.x].cost > totalCost)
					{
						pathMap->map[pos.y][pos.x].from = from;
						//pathMap->map[pos.y][pos.x].index = stepIndex;
						pathMap->map[pos.y][pos.x].cost = totalCost;
					}
				}
			}
		}
	}
	return false;
}

bool Map::getVHPath(std::vector<Point>& subStep, int line, PathMap* pathMap, Point from, Point to, int stepIndex)
{
	Point pos;
	for (int i = 0; i < 4; i++)
	{
		pos.x = (i - 1) % 2 + from.x;
		pos.y = ((i - 2) % 2) * 2 + from.y;
		if ((isInMap(pathMap, pos) && canWalk(pos) && (pathMap->map[pos.y][pos.x].index < 0 || pathMap->map[pos.y][pos.x].index == stepIndex)) || (pos == to))
		{
			bool canGo = false;
			if (i == 0)
			{
				Point tempPos1, tempPos2;
				tempPos1.x = from.x - 1 + line;
				tempPos1.y = from.y - 1;
				tempPos2.x = from.x - 1 + line;
				tempPos2.y = from.y + 1;
				if (canPass(tempPos1) && canPass(tempPos2))
				{
					canGo = true;
				}
			}
			else if (i == 1)
			{
				Point tempPos1, tempPos2;
				tempPos1.x = from.x - 1 + line;
				tempPos1.y = from.y - 1;
				tempPos2.x = from.x + line;
				tempPos2.y = from.y - 1;
				if (canPass(tempPos1) && canPass(tempPos2))
				{
					canGo = true;
				}
			}
			else if (i == 2)
			{
				Point tempPos1, tempPos2;
				tempPos1.x = from.x + line;
				tempPos1.y = from.y - 1;
				tempPos2.x = from.x + line;
				tempPos2.y = from.y + 1;
				if (canPass(tempPos1) && canPass(tempPos2))
				{
					canGo = true;
				}

			}
			else if (i == 3)
			{
				Point tempPos1, tempPos2;
				tempPos1.x = from.x - 1 + line;
				tempPos1.y = from.y + 1;
				tempPos2.x = from.x + line;
				tempPos2.y = from.y + 1;
				if (canPass(tempPos1) && canPass(tempPos2))
				{
					canGo = true;
				}

			}
			if (canGo)
			{
				if (pos == to)
				{
					pathMap->map[pos.y][pos.x].from = from;
					pathMap->map[pos.y][pos.x].index = stepIndex;
					subStep.push_back(pos);
					return true;
				}
				else
				{
					if (pathMap->map[pos.y][pos.x].index < 0)
					{
						pathMap->map[pos.y][pos.x].from = from;
						pathMap->map[pos.y][pos.x].index = stepIndex;
						auto tileCost = getTileDistance(pos, { 0, 0 }, from, { 0, 0 });
						pathMap->map[pos.y][pos.x].cost = tileCost + pathMap->map[from.y][from.x].cost;
						subStep.push_back(pos);
					}
					else if (pathMap->map[pos.y][pos.x].index == stepIndex)
					{
						auto tileCost = getTileDistance(pos, { 0, 0 }, from, { 0, 0 });
						auto totalCost = tileCost + pathMap->map[from.y][from.x].cost;
						if (pathMap->map[pos.y][pos.x].cost > totalCost)
						{
							pathMap->map[pos.y][pos.x].from = from;
							//pathMap->map[pos.y][pos.x].index = stepIndex;
							pathMap->map[pos.y][pos.x].cost = totalCost;
						}
					}
				}
			}
		}
	}
	return false;
}

std::vector<Point> Map::getSubStep(PathMap * pathMap, Point from, Point to, int stepIndex)
{
	std::vector<Point> subStep;
	subStep.resize(0);
	if (pathMap->map[to.y][to.x].index >= 0)
	{
		return subStep;
	}
	if (!isInMap(pathMap, from))
	{
		return subStep;
	}
	int line = std::abs(from.y) % 2;

	auto dir = NPC::calDirection(from, to);
	if (dir % 2 == 0)
	{
		if (getVHPath(subStep, line, pathMap, from, to, stepIndex))
		{
			return subStep;
		}
		getSlantPath(subStep, line, pathMap, from, to, stepIndex);
	}
	else
	{
		if (getSlantPath(subStep, line, pathMap, from, to, stepIndex))
		{
			return subStep;
		}
		getVHPath(subStep, line, pathMap, from, to, stepIndex);
	}
	
	return subStep;
	
}

bool Map::compareMapHead(MapData * md)
{
	//比较文件头部数据，以确认是否为MAP文件
	if (md == nullptr)
	{
		return false;
	}

	for (int i = 0; i < MAP_HEADSTR_LEN; i++)
	{
		if (MAP_HEADSTR[i] != md->head.head[i])
		{
			return false;
		}
	}

	return true;
	
}
