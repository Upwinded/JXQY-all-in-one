#pragma once
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 
#endif
#include <cmath>
#include <string>
#include <deque>
#include "../Image/IMP.h"


//格子宽度（像素）
#define TILE_WIDTH 64
//格子高度（像素）
#define TILE_HEIGHT 32
//等角视角下屏幕横向像素与逻辑距离的比例
#define MapXRatio 2.0f

//默认窗口宽度
#define DEFAULT_WINDOW_WIDTH 1280
//默认窗口高度
#define DEFAULT_WINDOW_HEIGHT 720
//移动端默认窗口宽度
#define MOBILE_DEFAULT_WINDOW_WIDTH 1100
//移动端默认窗口高度
#define MOBILE_DEFAULT_WINDOW_HEIGHT 500

//经验倍率
//伤害倍率
#define DAMAGE_RATE ((float)0.8)

#define CONFIG_INI "common\\config\\config.ini"
//存档列表文件名
#define SAVE_LIST_FILE "list.ini"
//当前存档文件夹
#define SAVE_CURRENT_FOLDER "save\\game\\"
//手动存档文件夹（%d为1至7；新游戏模板固定在ini/save）
#define SAVE_FOLDER "save\\rpg%d\\"
//自动存档文件夹
#define SAVE_AUTO_FOLDER "save\\rpg_auto\\"
//截图文件夹
#define SHOT_FOLDER "save\\shot\\"

//新存档截图使用标准 PNG；旧版私有裸像素截图仍以 .bmp 名称只读兼容。
#define SHOT_PNG "rpg%d.png"
#define LEGACY_SHOT_BMP "rpg%d.bmp"
//全局配置文件名
#define GLOBAL_INI "game.ini"
//#define PLAYER_INI "player.ini"
//玩家配置文件名（不含扩展名）
#define PLAYER_INI_NAME "player"
//玩家配置文件扩展名
#define PLAYER_INI_EXT ".ini"
//#define PARTNER_INI_NAME "partner.ini"
//伙伴配置文件名（不含扩展名）
#define PARTNER_INI_NAME "partner"
//伙伴配置文件扩展名
#define PARTNER_INI_EXT ".ini"
//伙伴索引配置文件名
#define PARTNER_IDX_INI "partneridx.ini"
//备忘录文件名
#define MEMO_INI "memo.txt"
//陷阱配置文件名
#define TRAPS_INI "traps.ini"
#define TRAP_TRIGGERED_INDICES_INI "trapindexignore.ini"
//地图文件夹
#define MAP_FOLDER "map\\"
//视频文件夹
#define VIDEO_FOLDER "video\\"
//音效文件夹
#define SOUND_FOLDER "sound\\"
//音乐文件夹
#define MUSIC_FOLDER "music\\"
//MPC资源文件夹
#define MPC_FOLDER "mpc\\"
//ASF资源文件夹（月影传说等游戏使用asf/目录存放图片资源）
#define ASF_FOLDER "asf\\"
//INI配置文件夹
#define INI_FOLDER "ini\\"
//NPC/OBJ模板文件夹（C#版 ini\save\ 回退路径）
#define INI_SAVE_FOLDER "ini\\save\\"

//脚本文件夹
#define SCRIPT_FOLDER "script\\"
//等级配置文件夹
#define LEVEL_FOLDER "ini\\level\\"

//头像资源文件夹
#define HEAD_FOLDER "mpc\\portrait\\"
//头像资源文件夹（ASF版本）
#define HEAD_FOLDER_ASF "asf\\portrait\\"
//头像文件配置路径
#define HEAD_FILE_NAME "ini\\ui\\dialog\\headfile.ini"

//NPC配置文件夹
#define NPC_INI_FOLDER "ini\\npc\\"
//NPC资源配置文件夹
#define NPC_RES_INI_FOLDER "ini\\npcres\\"
//NPC资源文件夹
#define NPC_RES_FOLDER "mpc\\character\\"
//NPC资源文件夹（ASF版本）
#define NPC_RES_FOLDER_ASF "asf\\character\\"

//物体配置文件夹
#define OBJECT_INI_FOLDER "ini\\obj\\"
//物体资源配置文件夹
#define OBJECT_RES_INI_FOLDER  "ini\\objres\\"
//物体资源文件夹
#define OBJECT_RES_FOLDER  "mpc\\object\\"
//物体资源文件夹（ASF版本）
#define OBJECT_RES_FOLDER_ASF  "asf\\object\\"

//公共脚本文件夹
#define SCRIPT_COMMON_FOLDER "script\\common\\"
//物品脚本文件夹
#define SCRIPT_GOODS_FOLDER "script\\goods\\"
//地图脚本文件夹
#define SCRIPT_MAP_FOLDER "script\\map\\"

//地图文件夹
#define MAP_FOLDER "map\\"
//变量配置文件名
#define VARIABLE_INI "variable.ini"
//变量节名
#define VARIABLE_SECTION "variable"
//地图配置文件夹
#define INI_MAP_FOLDER "ini\\map\\"
//地图名称列表配置文件名
#define INI_MAP_NAME_LIST "mapname.ini"
//对话文件名
#define TALK_FILE "talk.txt"

//菜单项数量
#define MENU_ITEM_COUNT 9

//物品栏数量定义
#define GOODS_COUNT 81
//物品快捷栏数量
#define GOODS_TOOLBAR_COUNT 3
//身体装备栏数量
#define GOODS_BODY_COUNT 7
//物品资源文件夹
#define GOODS_RES_FOLDER "mpc\\goods\\"
//物品资源文件夹（ASF版本）
#define GOODS_RES_FOLDER_ASF "asf\\goods\\"
//#define GOODS_INI "goods.ini"
//物品配置文件名（不含扩展名）
#define GOODS_INI_NAME "goods"
//物品配置文件扩展名
#define GOODS_INI_EXT ".ini"
//物品配置文件夹
#define INI_GOODS_FOLDER "ini\\goods\\"

//买卖物品栏数量
#define BUYSELL_GOODS_COUNT 81
//买卖配置文件夹
#define BUYSELL_FOLDER "ini\\buy\\"


//魔法栏数量定义
#define MAGIC_COUNT 36
//魔法快捷栏数量
#define MAGIC_TOOLBAR_COUNT 5
//魔法修炼栏数量
#define MAGIC_PRACTISE_COUNT 1
//魔法资源文件夹
#define MAGIC_RES_FOLDER "mpc\\magic\\"
//魔法资源文件夹（ASF版本）
#define MAGIC_RES_FOLDER_ASF "asf\\magic\\"
//魔法配置文件名（不含扩展名）
#define MAGIC_INI_NAME "magic"
//魔法配置文件扩展名
#define MAGIC_INI_EXT ".ini"
//魔法配置文件夹
#define INI_MAGIC_FOLDER "ini\\magic\\"
//魔法最大等级
#define MAGIC_MAX_LEVEL 10

//特效配置文件名
#define EFFECT_INI "proj.ini"
//特效资源文件夹
#define EFFECT_RES_FOLDER "mpc\\effect\\"
//特效资源文件夹（ASF版本）
#define EFFECT_RES_FOLDER_ASF "asf\\effect\\"

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
//NPC跟随的跑步检测范围，超出此范围尝试跑步寻找
#define NPC_FOLLOW_RADIUS_RUN (5 * NPC_FOLLOW_RADIUS)
//NPC尝试跟随失败后的重试间隔
#define NPC_FOLLOW_INTERVAL 200
//NPC跟随重试的随机额外间隔范围（毫秒）
#define NPC_FOLLOW_INTERVAL_RANDOM_RANGE 300
//NPC战斗扫描间隔（毫秒）
#define NPC_BATTLE_SCAN_INTERVAL 80
#define DEFAULT_NPC_OBJ_TIME_SCRIPT_INTERVAL 1000
//攻击到达安全格子数（攻击判定时额外增加的安全距离）
#define ATTACK_REACH_SAFETY_TILES 1
//武功类延迟时间

//连续型飞行技能每个effect的施放间隔
#define MAGIC_CONTINUOUS_INTERVAL 15
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
// Magic projectile speed multiplier shared by all trilogy games.
#define MAGIC_FLYING_SPEED_SCALE 2.0f
//魔法最大施法距离（格子数）
#define MAGIC_MAX_CAST_DISTANCE 20

//格子障碍物类型
enum TileObstacle
{
	toTrans = 0x40,      //可通过
	toJumpTrans = 0x60,  //可跳跃通过
	toObstacle = 0x80,   //障碍物（不可通过）
	toJumpOpaque = 0xA0, //可跳跃但不透明
};

// Barrier bytes are bit fields.  Original trilogy maps contain low-bit
// metadata combined with the standard flags (for example 0x41, 0x62, 0x81
// and 0x83), so exact enum comparisons misclassify real tiles.
inline bool tileObstacleAllowsWalk(uint8_t obstacle)
{
	return (obstacle & (toTrans | toObstacle)) == 0;
}

inline bool tileObstacleAllowsJump(uint8_t obstacle)
{
	constexpr uint8_t canJumpOver = 0x20;
	return obstacle == 0 || (obstacle & canJumpOver) != 0;
}

inline bool tileObstacleAllowsMagic(uint8_t obstacle)
{
	return obstacle == 0 || (obstacle & toTrans) != 0;
}

inline bool tileObstacleAllowsSight(uint8_t obstacle)
{
	return (obstacle & toObstacle) == 0;
}

//拖拽类型
enum DragType
{
	dtGoods = 1, //拖拽物品
	dtMagic = 2, //拖拽魔法
	dtSell = 3,  //拖拽出售
	dtBuy = 4,   //拖拽购买
};

//特效格子数据
struct EffectTile
{
	std::vector<int> index; //特效索引列表
};

//特效地图数据
struct EffectMap
{
	std::vector<std::vector<EffectTile>> tile; //特效格子二维数组
};


//路径列表
struct pathList
{
	std::vector<Point> point; //路径点集合
};

//游戏状态
enum GameState
{
	gsNone,    //无状态
	gsRunning, //游戏运行中
	gsMenu,    //菜单状态
	gsScript   //脚本执行状态
};

// 运行时只接受 Ver3.0；Ver2.0 解析仅保留在编辑器迁移边界。
#define MAP_HEADSTR_V3 "MAP File Ver3.0"
#define MAP_HEADSTR MAP_HEADSTR_V3
//地图文件标识字符串长度
#define MAP_HEADSTR_LEN 16
//地图文件头保留空数据长度
#define MAP_nullptr 16
//地图路径字段长度
#define MAP_PATH 32 
//地图文件头保留空数据2长度
#define MAP_nullptr_2 108
//地图文件头总长度
#define MAP_HEAD_LEN MAP_HEADSTR_LEN + MAP_nullptr + MAP_PATH + 20 + MAP_nullptr_2
#define MAP_V3_FLAG_UTF8 0x01

//地图文件头信息
struct MapHead
{
	char head[MAP_HEADSTR_LEN];      //地图文件标识头
	char dataNil[MAP_nullptr];       //保留空数据
	char path[MAP_PATH];             //地图路径
	int dataLen = 0;                 //数据长度
	int width = 0;                   //地图宽度（格子数）
	int height = 0;                  //地图高度（格子数）
	int infoLen = 0x40;              //信息段长度
	int nameLen = 0x20;              //名称段长度
	char dataNil2[MAP_nullptr_2];    //保留空数据2
};

//#define MAP_MPC_PATH 32
//地图MPC最大数量
#define MAP_MPC_COUNT 255

//MPC资源信息
struct MpcInfo
{
	std::unique_ptr<char[]> name = nullptr; //MPC名称
	int index = 0;                          //MPC索引
	int dynamic = 0;                        //是否动态（0=静态，1=动态）
	int obstacle = 0;                       //是否为障碍物（0=非障碍，1=障碍）
	int nil = 0;                            //保留字段
};

//地图MPC路径信息
struct MapMpcPath
{
	MpcInfo mpc[MAP_MPC_COUNT]; //MPC信息数组
};

//地图格子图层
struct MapTileLayer
{
	unsigned char frame = 0; //帧索引
	unsigned char mpc = 0;   //MPC资源索引
};

//地图图层数量
#define MAP_TILE_LAYER 3

//地图格子数据
struct MapTile
{
	MapTileLayer layer[MAP_TILE_LAYER]; //图层数组
	unsigned char obstacle = 0;         //障碍物标记
	unsigned char trap = 0;             //陷阱标记
	unsigned char end[2] = { 0x00, 0x1F }; //结束标记
};

//地图数据
struct MapData
{
	MapHead head;                           //地图文件头
	std::string mpcPath;                    //MAP 3.0 扩展 MPC 路径；旧格式读取时等于 head.path
	MapMpcPath mpc;                         //MPC路径信息
	std::vector<std::vector<MapTile>> tile; //地图格子二维数组
};

//MPC资源
struct Mpc
{
	_shared_imp img = nullptr; //MPC图像资源
};

//地图MPC资源集合
struct MapMpc
{
	Mpc mpc[MAP_MPC_COUNT]; //MPC资源数组
};
