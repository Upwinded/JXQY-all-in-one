#pragma once
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 
#endif
#include <cmath>
#include <string>
#include <deque>
#include "../Image/IMP.h"


#define TILE_WIDTH 64
#define TILE_HEIGHT 32

#define DEFAULT_WINDOW_WIDTH 1280
#define DEFAULT_WINDOW_HEIGHT 720
#define MOBILE_DEFAULT_WINDOW_WIDTH 1000
#define MOBILE_DEFAULT_WINDOW_HEIGHT 500

#define EXP_RATE ((float)3.0)
#define DAMAGE_RATE ((float)0.8)

#ifdef __ANDROID__
#define CONFIG_INI u8"config.ini"
#else
#define CONFIG_INI u8"config\\config.ini"
#endif
#define SAVE_LIST_FILE u8"list.ini"
#define SAVE_LIST_SECTION u8"save"
#define SAVE_LIST_COUNT_KEY u8"count"
#define SAVE_CURRENT_FOLDER u8"save\\game\\"
#define SAVE_FOLDER u8"save\\rpg%d\\"
#define SHOT_FOLDER u8"save\\shot\\"

#define SHOT_BMP u8"rpg%d.bmp"
#define GLOBAL_INI u8"game.ini"
//#define PLAYER_INI u8"player.ini"
#define PLAYER_INI_NAME u8"player"
#define PLAYER_INI_EXT u8".ini"
//#define PARTNER_INI_NAME u8"partner.ini"
#define PARTNER_INI_NAME u8"partner"
#define PARTNER_INI_EXT u8".ini"
#define MEMO_INI u8"memo.txt"
#define TRAPS_INI u8"traps.ini"
#define MAP_FOLDER u8"map\\"
#define VIDEO_FOLDER u8"video\\"
#define SOUND_FOLDER u8"sound\\"
#define MUSIC_FOLDER u8"music\\"
#define MPC_FOLDER u8"mpc\\"
#define INI_FOLDER u8"ini\\"

#define SCRIPT_FOLDER u8"script\\"
#define LEVEL_FOLDER u8"ini\\level\\"

#define HEAD_FOLDER u8"mpc\\portrait\\"
#define HEAD_FILE_NAME u8"ini\\ui\\dialog\\headfile.ini"

#define NPC_INI_FOLDER u8"ini\\npc\\"
#define NPC_RES_INI_FOLDER u8"ini\\npcres\\"
#define NPC_RES_FOLDER u8"mpc\\character\\"

#define OBJECT_INI_FOLDER u8"ini\\obj\\"
#define OBJECT_RES_INI_FOLDER  u8"ini\\objres\\"
#define OBJECT_RES_FOLDER  u8"mpc\\object\\"

#define SCRIPT_COMMON_FOLDER u8"script\\common\\"
#define SCRIPT_GOODS_FOLDER u8"script\\goods\\"
#define SCRIPT_MAP_FOLDER u8"script\\map\\"

#define MAP_FOLDER u8"map\\"
#define VARIABLE_INI u8"variable.ini"
#define VARIABLE_SECTION u8"variable"
#define INI_MAP_FOLDER u8"ini\\map\\"
#define INI_MAP_NAME_LIST u8"mapname.ini"
#define TALK_FILE u8"talk.txt"

#define SAVE_SHOT_HEAD u8"SAVESHOT"
#define SAVE_SHOT_HEAD_LEN 8

#define MENU_ITEM_COUNT 9

//物品栏数量定义
#define GOODS_COUNT 81
#define GOODS_TOOLBAR_COUNT 3
#define GOODS_BODY_COUNT 7
#define GOODS_RES_FOLDER u8"mpc\\goods\\"
//#define GOODS_INI u8"goods.ini"
#define GOODS_INI_NAME u8"goods"
#define GOODS_INI_EXT u8".ini"
#define INI_GOODS_FOLDER u8"ini\\goods\\"

#define BUYSELL_GOODS_COUNT 81
#define BUYSELL_FOLDER u8"ini\\buy\\"


//魔法栏数量定义
#define MAGIC_COUNT 36
#define MAGIC_TOOLBAR_COUNT 5
#define MAGIC_PRACTISE_COUNT 1
#define MAGIC_RES_FOLDER u8"mpc\\magic\\"
//#define MAGIC_INI u8"magic.ini"
#define MAGIC_INI_NAME u8"magic"
#define MAGIC_INI_EXT u8".ini"
#define INI_MAGIC_FOLDER u8"ini\\magic\\"
#define MAGIC_MAX_LEVEL 10

#define EFFECT_INI u8"proj.ini"
#define EFFECT_RES_FOLDER u8"mpc\\effect\\"

//声音距离参数，数值越大衰减越大
#define SOUND_FACTOR 0.5f
//声音最远距离
#define SOUND_FAREST 10000.0f
//NPC或OBJECT背景声音的播放间隔
#define SOUND_RAND_INTERVAL 6000

//施法动作开始后延迟施放武功，主角放技能时使用该延迟
#define PLAYER_MAGIC_DELAY 300
//每帧时间（原武功效果持续时间为帧数，在将其转为毫秒时使用该宏）
#define EFFECT_FRAME_TIME (1200.0/60.0)
//每两帧之间最大的间隔时间
#define MAX_FRAME_TIME 40
//人物移动和技能飞行速度参数
#define SPEED_TIME_DEFAULT 0.004
#define SPEED_TIME_MIN 0.0025
#define SPEED_TIME_MAX 0.006
//NPC闲逛间隔时间（毫秒）
#define NPC_WALK_INTERVAL 5000
//NPC闲逛间隔基础上增加随机额外间隔时间范围（毫秒）
#define NPC_WALK_INTERVAL_RANGE 10000
//NPC闲逛最大i数
#define NPC_WALK_STEP 3
//不进行寻路的NPC在攻击找人时的最大移动i数，i数走完时才会再次改变目的移动
#define NPC_STEP_MAX_COUNT 5
//NPC跟随检测范围，超出此范围进行寻找
#define NPC_FOLLOW_RADIUS 1


//武功类延迟时间

//连续型飞行技能每个effect的施放间隔
#define MAGIC_CONTINUOUS_INTERVAL 20
//圆形技能的effect个数
#define MAGIC_CIRCLE_COUNT 32
//圆形技能的相隔角度
#define MAGIC_CIRCLE_ANGLE_SPACE (2 * M_PI / MAGIC_CIRCLE_COUNT)
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

struct EffectTile
{
	std::vector<int> index;
};

struct EffectMap
{
	std::vector<std::vector<EffectTile>> tile;
};


struct pathList
{
	std::vector<Point> point;
};

enum GameState
{
	gsNone,
	gsRunning,
	gsMenu,
	gsScript
};

#define MAP_HEADSTR u8"MAP File Ver2.0"
#define MAP_HEADSTR_LEN 16
#define MAP_nullptr 16
#define MAP_PATH 32 
#define MAP_nullptr_2 108
#define MAP_HEAD_LEN MAP_HEADSTR_LEN + MAP_nullptr + MAP_PATH + 20 + MAP_nullptr_2

struct MapHead
{
	char head[MAP_HEADSTR_LEN];
	char dataNil[MAP_nullptr];
	char path[MAP_PATH];
	int dataLen = 0;
	int width = 0;
	int height = 0;
	int infoLen = 0x40;
	int nameLen = 0x20;
	char dataNil2[MAP_nullptr_2];
};

//#define MAP_MPC_PATH 32
#define MAP_MPC_COUNT 255

struct MpcInfo
{
	std::unique_ptr<char[]> name = nullptr;
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
	std::vector<std::vector<MapTile>> tile;
};

struct Mpc
{
	_shared_imp img = nullptr;
};

struct MapMpc
{
	Mpc mpc[MAP_MPC_COUNT];
};
