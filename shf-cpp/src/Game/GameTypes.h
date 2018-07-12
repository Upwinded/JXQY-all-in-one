#pragma once
#include <string>
#include <deque>
#include "../../Image/IMP.h"

#define TILE_WIDTH 64
#define TILE_HEIGHT 32

#define DEFAULT_WINDOW_WIDTH 1280
#define DEFAULT_WINDOW_HEIGHT 720

#define CONFIG_INI "config.ini"
#define DEFAULT_FOLDER "save\\game\\"
#define SAVE_FOLDER "save\\rpg%d\\"
#define SHOT_FOLDER "save\\shot\\"
#define SHOT_BMP "rpg%d.bmp"
#define GLOBAL_INI "game.ini"
#define PLAYER_INI "player.ini"
#define PARTNER_INI "partner.ini"
#define MEMO_INI "memo.txt"
#define TRAPS_INI "traps.ini"
#define MAP_FOLDER "map\\"
#define VIDEO_FOLDER "video\\"
#define SOUND_FOLDER "sound\\"
#define MUSIC_FOLDER "music\\"
#define MPC_FOLDER "mpc\\"
#define INI_FOLDER "ini\\"

#define SCRIPT_FOLDER "script\\"
#define LEVEL_FOLDER "ini\\level\\"

#define HEAD_FOLDER "mpc\\portrait\\"

#define NPC_INI_FOLDER "ini\\npc\\"
#define NPC_RES_INI_FOLDER "ini\\npcres\\"
#define NPC_RES_FOLDER "mpc\\character\\"

#define OBJECT_INI_FOLDER "ini\\obj\\"
#define OBJECT_RES_INI_FOLDER  "ini\\objres\\"
#define OBJECT_RES_FOLDER  "mpc\\object\\"

#define SCRIPT_COMMON_FOLDER "script\\common\\"
#define SCRIPT_GOODS_FOLDER "script\\goods\\"
#define SCRIPT_MAP_FOLDER "script\\map\\"

#define MAP_FOLDER "map\\"
#define VARIABLE_INI "variable.ini"
#define VARIABLE_SECTION "Variable"

#define TALK_FILE "talk.txt"

#define BMP16Head "BM16"
#define BMP16HeadLen 4

#define MENU_ITEM_COUNT 9

//物品栏数量定义
#define GOODS_COUNT 81
#define GOODS_TOOLBAR_COUNT 3
#define GOODS_BODY_COUNT 7
#define GOODS_RES_FOLDER "mpc\\goods\\"
#define GOODS_INI "goods.ini"
#define GOODS_INI_FOLDER "ini\\goods\\"

#define BUYSELL_GOODS_COUNT 81
#define BUYSELL_FOLDER "ini\\buy\\"


//魔法栏数量定义
#define MAGIC_COUNT 36
#define MAGIC_TOOLBAR_COUNT 5
#define MAGIC_PRACTISE_COUNT 1
#define MAGIC_RES_FOLDER "mpc\\magic\\"
#define MAGIC_INI "magic.ini"
#define INI_MAGIC_FOLDER "ini\\magic\\"
#define MAGIC_MAX_LEVEL 10

#define EFFECT_INI "proj.ini"
#define EFFECT_RES_FOLDER "mpc\\effect\\"

//声音距离参数，数值越大衰减越大
#define SOUND_FACTOR 0.3f
//声音最远距离
#define SOUND_FAREST 10000.0f
//NPC或OBJECT背景声音的播放间隔
#define SOUND_RAND_INTERVAL 6000

//施法动作开始后延迟施放武功，主角放技能时使用该延迟
#define PLAYER_MAGIC_DELAY 300
//每帧时间（原武功效果持续时间为帧数，在将其转为毫秒时使用该宏）
#define EFFECT_FRAME_TIME (1000.0/60.0)
//每两帧之间最大的间隔时间
#define MAX_FRAME_TIME 40
//游戏运行速度参数
#define SPEED_TIME 0.004
//NPC闲逛间隔时间（毫秒）
#define NPC_WALK_INTERVAL 5000
//NPC闲逛间隔基础上增加随机额外间隔时间范围（毫秒）
#define NPC_WALK_INTERVAL_RANGE 10000
//NPC闲逛最大歩数
#define NPC_WALK_STEP 3
//不进行寻路的NPC在攻击找人时的最大移动歩数，歩数走完时才会再次改变目的移动
#define NPC_STEP_MAX_COUNT 5
//NPC跟随检测范围，超出此范围进行寻找
#define NPC_FOLLOW_RADIUS 1

#ifdef pi
#undef pi
#endif // pi
#define pi 3.1415926

//武功类延迟时间

//连续型飞行技能每个effect的施放间隔
#define MAGIC_CONTINUOUS_INTERVAL 20
//圆形技能的effect个数
#define MAGIC_CIRCLE_COUNT 32
//圆形技能的相隔角度
#define MAGIC_CIRCLE_ANGLE_SPACE (2 * pi / MAGIC_CIRCLE_COUNT)
//心形技能施放延迟等参数
#define MAGIC_HEART_DELAY 10
#define MAGIC_HEART_DECAY 0.1
//螺旋技能每个effect的施放间隔
#define MAGIC_CIRCLE_HELIX_INTERVAL 10
//跟随技能更新目标间隔(ms)
#define MAGIC_FOLLOW_DELAY 0
//跟随技能找寻目标范围
#define MAGIC_FOLLOW_RADIUS 10
//投掷技能曲线最高点系数（根据目标距离远近计算投掷最大高度）
#define MAGIC_THROW_HEIGHT 7.0

enum TileObstacle
{
	toTrans = 0x40,
	toJumpTrans = 0x60,
	toObstacle = 0x80,
	toJumpOpaque = 0xA0,
};

enum DragType
{
	dtGoods = 1,
	dtMagic = 2,
	dtSell = 3,
	dtBuy = 4,
};

struct DataTile
{
	std::vector<int> objIndex = {};
	std::vector<int> npcIndex = {};
	std::vector<int> stepIndex = {};
};

struct DataMap
{
	std::vector<std::vector<DataTile>> tile;
};

struct EffectTile
{
	std::vector<int> index = {};
};

struct EffectMap
{
	std::vector<std::vector<EffectTile>> tile;
};

struct Point
{
	int x;
	int y;
};

struct PointEx
{
	double x;
	double y;
};

struct pathList
{
	std::vector<Point> point = {};
};

enum GameState
{
	gsNone,
	gsRunning,
	gsMenu,
	gsScript
};

#define MAP_HEADSTR "MAP File Ver2.0"
#define MAP_HEADSTR_LEN 16
#define MAP_NULL 16
#define MAP_PATH 32 
#define MAP_NULL_2 108
#define MAP_HEAD_LEN MAP_HEADSTR_LEN + MAP_NULL + MAP_PATH + 20 + MAP_NULL_2

struct MapHead
{
	char head[MAP_HEADSTR_LEN];
	char dataNil[MAP_NULL];
	char path[MAP_PATH];
	int dataLen = 0;
	int width = 0;
	int height = 0;
	int infoLen = 0x40;
	int nameLen = 0x20;
	char dataNil2[MAP_NULL_2];
};

//#define MAP_MPC_PATH 32
#define MAP_MPC_COUNT 255

struct MpcInfo
{
	char * name = NULL;
	int index = 0;
	int dynamic = 0;
	int obstacle = 0;
	int nil = 0;
};

struct MapMpcPath
{
	MpcInfo mpc[MAP_MPC_COUNT];
};

struct MapTileLayer
{
	unsigned char frame = 0;
	unsigned char mpc = 0;
};

#define MAP_TILE_LAYER 3

struct MapTile
{
	MapTileLayer layer[MAP_TILE_LAYER];
	unsigned char obstacle = 0;
	unsigned char trap = 0;
	unsigned char end[2] = { 0x00, 0x1F };
};

struct MapData
{
	MapHead head;
	MapMpcPath mpc;
	std::vector<std::vector<MapTile>> tile = {};
};

struct Mpc
{
	IMPImage * img = NULL;
};

struct MapMpc
{
	Mpc mpc[MAP_MPC_COUNT];
};
