#include "ScriptApiList.h"
#include "../core/ScriptConverter.h"
#include "../core/ScriptCallTemplate.h"

#include <QCoreApplication>
#include <QSet>

QVector<ApiInfo> buildScriptApiList()
{
    QVector<ApiInfo> apis;

    auto parameterizedSignature = [](const QString& apiName)
    {
        ScriptCallDefinition definition;
        return ScriptCallTemplate::findDefinition(apiName, definition)
            ? definition.signature() : apiName + QStringLiteral("(...)");
    };

    auto addApi = [&apis](const QString& signature, const QString& description, const QString& tooltip) {
        ApiInfo info;
        info.signature = signature;
        info.description = description;
        info.tooltip = tooltip;
        apis.append(info);
    };

    // ===== 调试输出 =====
    addApi("printf(str)",
        QCoreApplication::translate("ScriptApiList", "输出调试信息到日志"),
        QCoreApplication::translate("ScriptApiList", "printf(str)\n输出调试信息到日志控制台\n参数:\n  str (string) - 要输出的字符串\n示例: printf(\"调试信息: value=%d\", 10)"));

    // ===== 变量操作 =====
    addApi(parameterizedSignature(QStringLiteral("Assign")),
        QCoreApplication::translate("ScriptApiList", "给变量赋值"),
        QCoreApplication::translate("ScriptApiList", "Assign(varName, value)\n将指定值赋给脚本变量\n参数:\n  varName (string) - 变量名\n  value (number) - 要赋的值\n示例: Assign(\"questState\", 1)"));

    addApi("GetVar(varName)",
        QCoreApplication::translate("ScriptApiList", "获取变量值"),
        QCoreApplication::translate("ScriptApiList", "GetVar(varName)\n获取指定脚本变量的值\n参数:\n  varName (string) - 变量名\n返回值: (number) 变量的当前值\n示例: local state = GetVar(\"questState\")"));

    addApi(parameterizedSignature(QStringLiteral("Add")),
        QCoreApplication::translate("ScriptApiList", "变量值增加"),
        QCoreApplication::translate("ScriptApiList", "Add(varName, value)\n将指定变量的值增加指定数值\n参数:\n  varName (string) - 变量名\n  value (number) - 增加的值\n示例: Add(\"killCount\", 1)"));

    // ===== 对话系统 =====
    addApi("Talk(part)",
        QCoreApplication::translate("ScriptApiList", "显示对话（按段落名）"),
        QCoreApplication::translate("ScriptApiList", "Talk(part)\n根据段落名显示对话，从地图对话文件中读取\n参数:\n  part (string) - 对话段落名\n示例: Talk(\"greeting\")\n\nTalk(fromIdx, toIdx)\n根据索引范围显示对话\n参数:\n  fromIdx (number) - 起始对话索引\n  toIdx (number) - 结束对话索引\n示例: Talk(1, 5)"));

    addApi(parameterizedSignature(QStringLiteral("Say")),
        QCoreApplication::translate("ScriptApiList", "显示指定说话者的对话"),
        QCoreApplication::translate("ScriptApiList", "Say(str)\n显示对话内容（无头像）\n参数:\n  str (string) - 对话内容文本\n示例: Say(\"你好，欢迎来到剑侠世界！\")\n\nSay(str, index)\n显示带头像的对话\n参数:\n  str (string) - 对话内容文本\n  index (number) - 头像索引（-1为无头像）\n示例: Say(\"我一定会找到灵儿的！\", 0)"));

    addApi("Choose(message, optionA, optionB, varName)",
        QCoreApplication::translate("ScriptApiList", "显示二选一对话框"),
        QCoreApplication::translate("ScriptApiList", "Choose(message, optionA, optionB, varName)\n显示二选一对话框，将选择结果存入变量\n参数:\n  message (string) - 提示信息文本索引\n  optionA (string) - 选项A文本索引\n  optionB (string) - 选项B文本索引\n  varName (string) - 存储选择结果的变量名（1=选A, 2=选B）\n示例: Choose(\"是否接受任务？\", \"接受\", \"拒绝\", \"questChoice\")"));

    addApi("ChooseEx(message, option1, option2, ..., varName)",
        QCoreApplication::translate("ScriptApiList", "Display a multi-option choice dialog"),
        QCoreApplication::translate("ScriptApiList", "ChooseEx(message, option1, option2, ..., varName)\nDisplays a multi-option choice dialog. Options may include conditions in braces, for example {$koucai >= 2}. The selected original option index is stored in varName.\nExample: ChooseEx(\"Choose:\", \"A\", \"B{$Flag == 1}\", \"choice\")"));

    addApi("ChoosePlus(speakerName, portraitIndex, dialogPosition, message, option1, option2, ..., varName)",
        QCoreApplication::translate("ScriptApiList", "Display a speaker choice dialog"),
        QCoreApplication::translate("ScriptApiList", "ChoosePlus(speakerName, portraitIndex, dialogPosition, message, option1, option2, ..., varName)\nDisplays a speaker choice dialog and stores the selected zero-based option index in varName. #name uses the current player name when available.\nExample: ChoosePlus(\"#name\", \"2\", \"0\", \"Choose:\", \"A\", \"B\", \"choice\")"));

    addApi("Select(messageIdx, optionAIdx, optionBIdx, varName)",
        QCoreApplication::translate("ScriptApiList", "显示选择对话框（按索引）"),
        QCoreApplication::translate("ScriptApiList", "Select(messageIdx, optionAIdx, optionBIdx, varName)\n按对话索引显示二选一对话框\n参数:\n  messageIdx (number) - 提示信息对话索引\n  optionAIdx (number) - 选项A对话索引\n  optionBIdx (number) - 选项B对话索引\n  varName (string) - 存储选择结果的变量名\n示例: Select(1, 2, 3, \"choice\")"));

    addApi("ShowGiveGoodsWin(targetGoodsName, successScript, failScript)",
        QCoreApplication::translate("ScriptApiList", "Run a give-goods result script"),
        QCoreApplication::translate("ScriptApiList", "ShowGiveGoodsWin(targetGoodsName, successScript, failScript)\nChecks whether the player has the target goods by ini file or display name, then runs successScript or failScript.\nExample: ShowGiveGoodsWin(\"Peach Wood Sword\", \"GiveSuccess.txt\", \"GiveFail.txt\")"));

    addApi("ShowStealWin(npcName, successScript, failScript)",
        QCoreApplication::translate("ScriptApiList", "Open the NPC stealing dialog"),
        QCoreApplication::translate("ScriptApiList", "ShowStealWin(npcName, successScript, failScript)\nOpens a stealing choice dialog using the target NPC BagGoods field. On success, adds the selected goods and runs successScript; on failure, runs failScript.\nExample: ShowStealWin(\"Innkeeper\", \"StealSuccess.txt\", \"StealFail.txt\")"));

    addApi("ShowMessage(str)",
        QCoreApplication::translate("ScriptApiList", "显示消息提示"),
        QCoreApplication::translate("ScriptApiList", "ShowMessage(str)\n在屏幕上显示消息提示\n参数:\n  str (string) - 消息文本\n示例: ShowMessage(\"任务完成！\")"));

    addApi("ShowSystemMsg(str, stayTime)",
        QCoreApplication::translate("ScriptApiList", "显示系统消息提示"),
        QCoreApplication::translate("ScriptApiList", "ShowSystemMsg(str)\n显示系统消息提示\n参数:\n  str (string) - 消息文本\n  stayTime (number) - 停留毫秒数，可省略，默认3000\n示例: ShowSystemMsg(\"测试消息\", 6000)"));

    addApi("DisplayMessage(text)",
        QCoreApplication::translate("ScriptApiList", "显示消息（居中）"),
        QCoreApplication::translate("ScriptApiList", "DisplayMessage(text)\n在屏幕中央显示消息\n参数:\n  text (string) - 消息文本\n示例: DisplayMessage(\"恭喜通关！\")"));

    addApi("AddToMemo(str)",
        QCoreApplication::translate("ScriptApiList", "添加到备忘录"),
        QCoreApplication::translate("ScriptApiList", "AddToMemo(str)\n将文本添加到游戏备忘录中\n参数:\n  str (string) - 要添加的文本\n示例: AddToMemo(\"在长安城找到李大侠\")"));

    addApi("Memo(str)",
        QCoreApplication::translate("ScriptApiList", "添加到备忘录（同AddToMemo）"),
        QCoreApplication::translate("ScriptApiList", "Memo(str)\n将文本添加到游戏备忘录中（与AddToMemo功能相同）\n参数:\n  str (string) - 要添加的文本\n示例: Memo(\"记住去药店买药\")"));

    addApi("ClearMemo()",
        QCoreApplication::translate("ScriptApiList", "清空备忘录"),
        QCoreApplication::translate("ScriptApiList", "ClearMemo()\n清空游戏备忘录中的所有内容\n参数: 无\n示例: ClearMemo()"));

    // ===== 画面效果 =====
    addApi("FadeIn()",
        QCoreApplication::translate("ScriptApiList", "画面淡入"),
        QCoreApplication::translate("ScriptApiList", "FadeIn()\n画面从黑色淡入\n参数: 无\n示例: FadeIn()"));

    addApi("FadeOut()",
        QCoreApplication::translate("ScriptApiList", "画面淡出"),
        QCoreApplication::translate("ScriptApiList", "FadeOut()\n画面淡出到黑色\n参数: 无\n示例: FadeOut()"));

    addApi("SetFadeLum(lum)",
        QCoreApplication::translate("ScriptApiList", "设置淡入淡出亮度"),
        QCoreApplication::translate("ScriptApiList", "SetFadeLum(lum)\n设置画面淡入淡出的亮度值\n参数:\n  lum (number) - 亮度值（0-255）\n示例: SetFadeLum(128)"));

    addApi("SetMainLum(lum)",
        QCoreApplication::translate("ScriptApiList", "设置主亮度"),
        QCoreApplication::translate("ScriptApiList", "SetMainLum(lum)\n设置画面主亮度\n参数:\n  lum (number) - 亮度值（0-255）\n示例: SetMainLum(200)"));

    // ===== 音频控制 =====
    addApi("PlayMusic(fileName)",
        QCoreApplication::translate("ScriptApiList", "播放背景音乐"),
        QCoreApplication::translate("ScriptApiList", "PlayMusic(fileName)\n播放指定的背景音乐文件\n参数:\n  fileName (string) - 音乐文件名（支持mp3/wav）\n示例: PlayMusic(\"bgm_village.mp3\")"));

    addApi("PlayRandomMusic(fileA, fileB, fileC)",
        QCoreApplication::translate("ScriptApiList", "随机播放背景音乐"),
        QCoreApplication::translate("ScriptApiList", "PlayRandomMusic(fileA, fileB, fileC)\n从给定的音乐文件中随机选择一首播放\n参数:\n  fileA (string) - 音乐文件A\n  fileB (string) - 音乐文件B（可为空串）\n  fileC (string) - 音乐文件C（可为空串）\n示例: PlayRandomMusic(\"bgm1.mp3\", \"bgm2.mp3\", \"bgm3.mp3\")"));

    addApi("StopMusic()",
        QCoreApplication::translate("ScriptApiList", "停止背景音乐"),
        QCoreApplication::translate("ScriptApiList", "StopMusic()\n停止当前播放的背景音乐\n参数: 无\n示例: StopMusic()"));

    addApi(parameterizedSignature(QStringLiteral("PlaySound")),
        QCoreApplication::translate("ScriptApiList", "播放音效"),
        QCoreApplication::translate("ScriptApiList", "PlaySound(fileName)\n播放指定的音效文件\n参数:\n  fileName (string) - 音效文件名\n示例: PlaySound(\"se_door.wav\")"));

    // ===== 脚本控制 =====
    addApi(parameterizedSignature(QStringLiteral("RunScript")),
        QCoreApplication::translate("ScriptApiList", "运行指定脚本文件"),
        QCoreApplication::translate("ScriptApiList", "RunScript(fileName)\n运行指定的脚本文件，依次在地图目录、物品目录、公共目录中查找\n参数:\n  fileName (string) - 脚本文件名\n示例: RunScript(\"scene/scene01.txt\")"));

    addApi("Sleep(milliseconds)",
        QCoreApplication::translate("ScriptApiList", "暂停脚本执行"),
        QCoreApplication::translate("ScriptApiList", "Sleep(milliseconds)\n暂停脚本执行指定毫秒数\n参数:\n  milliseconds (number) - 暂停时间（毫秒）\n示例: Sleep(1000)"));

    // ===== 镜头控制 =====
    addApi("MoveScreen(direction, distance)",
        QCoreApplication::translate("ScriptApiList", "移动镜头"),
        QCoreApplication::translate("ScriptApiList", "MoveScreen(direction, distance)\nMoveScreen(direction, frameCount, speed)\n按参数个数兼容两代镜头移动协议\n参数:\n  direction (number) - 方向\n  distance (number) - 两参数形式的总移动距离（剑二原版）\n  frameCount (number) - 三参数形式的持续帧数（JxqyHD/新剑）\n  speed (number) - 三参数形式的每帧速度，不可省略\n示例:\n  MoveScreen(0, 100)\n  MoveScreen(0, 50, 2)"));

    addApi("MoveScreenEx(x, y, speed)",
        QCoreApplication::translate("ScriptApiList", "移动镜头到指定位置"),
        QCoreApplication::translate("ScriptApiList", "MoveScreenEx(x, y, speed)\n将镜头移动到指定坐标位置\n参数:\n  x (number) - 目标X坐标\n  y (number) - 目标Y坐标\n  speed (number) - 移动速度\n示例: MoveScreenEx(500, 300, 3)"));

    // ===== 视频播放 =====
    addApi("PlayMovie(fileName)",
        QCoreApplication::translate("ScriptApiList", "播放视频"),
        QCoreApplication::translate("ScriptApiList", "PlayMovie(fileName)\n播放指定的视频文件\n参数:\n  fileName (string) - 视频文件名\n示例: PlayMovie(\"intro.avi\")"));

    addApi("StopMovie()",
        QCoreApplication::translate("ScriptApiList", "停止播放视频"),
        QCoreApplication::translate("ScriptApiList", "StopMovie()\n停止当前播放的视频\n参数: 无\n示例: StopMovie()"));

    // ===== 地图操作 =====
    addApi("LoadMap(mapName)",
        QCoreApplication::translate("ScriptApiList", "加载地图"),
        QCoreApplication::translate("ScriptApiList", "LoadMap(mapName)\n加载指定的地图文件，会清空当前NPC和物体\n参数:\n  mapName (string) - 地图文件名\n示例: LoadMap(\"scene01.map\")"));

    addApi("FreeMap()",
        QCoreApplication::translate("ScriptApiList", "释放当前地图"),
        QCoreApplication::translate("ScriptApiList", "FreeMap()\n释放当前加载的地图资源\n参数: 无\n示例: FreeMap()"));

    addApi("LoadGame(index)",
        QCoreApplication::translate("ScriptApiList", "加载存档"),
        QCoreApplication::translate("ScriptApiList", "LoadGame(index)\n加载指定索引的存档文件\n参数:\n  index (number) - 存档索引（0开始）\n示例: LoadGame(0)"));

    addApi("SaveGame()",
        QCoreApplication::translate("ScriptApiList", "保存存档"),
        QCoreApplication::translate("ScriptApiList", "SaveGame()\n保存当前游戏进度到存档\n参数: 无\n示例: SaveGame()"));

    addApi("ClearAllSave()",
        QCoreApplication::translate("ScriptApiList", "清除用户存档槽"),
        QCoreApplication::translate("ScriptApiList", "ClearAllSave()\n清除当前资源包下的 rpg1-rpg7 用户存档和对应截图，不清除当前运行存档或初始模板\n参数: 无\n示例: ClearAllSave()"));

    addApi("EnableSave()",
        QCoreApplication::translate("ScriptApiList", "允许手动存档"),
        QCoreApplication::translate("ScriptApiList", "EnableSave()\n允许玩家在存读档界面手动保存\n参数: 无\n示例: EnableSave()"));

    addApi("DisableSave()",
        QCoreApplication::translate("ScriptApiList", "禁止手动存档"),
        QCoreApplication::translate("ScriptApiList", "DisableSave()\n禁止玩家在存读档界面手动保存\n参数: 无\n示例: DisableSave()"));

    addApi("SetMapPos(x, y)",
        QCoreApplication::translate("ScriptApiList", "设置地图视口位置"),
        QCoreApplication::translate("ScriptApiList", "SetMapPos(x, y)\n设置地图视口位置，镜头不再跟随玩家\n参数:\n  x (number) - X坐标\n  y (number) - Y坐标\n示例: SetMapPos(100, 200)"));

    addApi("SetMapTrap(idx, trapFile)",
        QCoreApplication::translate("ScriptApiList", "设置当前地图陷阱"),
        QCoreApplication::translate("ScriptApiList", "SetMapTrap(idx, trapFile)\n设置当前地图指定索引的陷阱触发脚本\n参数:\n  idx (number) - 陷阱索引（1-19，0 表示无陷阱）\n  trapFile (string) - 触发脚本文件名\n示例: SetMapTrap(1, \"trap/trap01.txt\")"));

    addApi("SaveMapTrap()",
        QCoreApplication::translate("ScriptApiList", "保存地图陷阱数据"),
        QCoreApplication::translate("ScriptApiList", "SaveMapTrap()\n保存当前地图的陷阱数据\n参数: 无\n示例: SaveMapTrap()"));

    addApi("SetMapTime(time)",
        QCoreApplication::translate("ScriptApiList", "设置地图时间"),
        QCoreApplication::translate("ScriptApiList", "SetMapTime(time)\n设置地图时间（影响光照等效果）\n参数:\n  time (number) - 时间值\n示例: SetMapTime(12)"));

    addApi("SetTrap(mapName, idx, trapFile)",
        QCoreApplication::translate("ScriptApiList", "设置指定地图陷阱"),
        QCoreApplication::translate("ScriptApiList", "SetTrap(mapName, idx, trapFile)\n设置指定地图的陷阱触发脚本\n参数:\n  mapName (string) - 地图名称\n  idx (number) - 陷阱索引（1-19，0 表示无陷阱）\n  trapFile (string) - 触发脚本文件名\n示例: SetTrap(\"scene01\", 1, \"trap/trap01.txt\")"));

    addApi("ChangeASFColor(r, g, b)",
        QCoreApplication::translate("ScriptApiList", "改变角色精灵颜色"),
        QCoreApplication::translate("ScriptApiList", "ChangeASFColor(r, g, b)\n改变角色ASF精灵的色调\n参数:\n  r (number) - 红色分量（0-255）\n  g (number) - 绿色分量（0-255）\n  b (number) - 蓝色分量（0-255）\n示例: ChangeASFColor(255, 128, 0)"));

    addApi("ChangeMapColor(r, g, b)",
        QCoreApplication::translate("ScriptApiList", "改变地图颜色"),
        QCoreApplication::translate("ScriptApiList", "ChangeMapColor(r, g, b)\n改变地图的色调\n参数:\n  r (number) - 红色分量（0-255）\n  g (number) - 绿色分量（0-255）\n  b (number) - 蓝色分量（0-255）\n示例: ChangeMapColor(100, 100, 200)"));

    addApi("DisableMapScroll()",
        QCoreApplication::translate("ScriptApiList", "禁用地图滚动"),
        QCoreApplication::translate("ScriptApiList", "DisableMapScroll()\n禁用地图视口跟随玩家滚动\n参数: 无\n示例: DisableMapScroll()"));

    addApi("EnableMapScroll()",
        QCoreApplication::translate("ScriptApiList", "启用地图滚动"),
        QCoreApplication::translate("ScriptApiList", "EnableMapScroll()\n启用地图视口跟随玩家滚动\n参数: 无\n示例: EnableMapScroll()"));

    // ===== 物体操作 =====
    addApi("LoadObj(fileName)",
        QCoreApplication::translate("ScriptApiList", "加载物体数据"),
        QCoreApplication::translate("ScriptApiList", "LoadObj(fileName)\n加载指定物体数据文件\n参数:\n  fileName (string) - 物体数据文件名\n示例: LoadObj(\"scene01.obj\")"));

    addApi("SaveObj(fileName)",
        QCoreApplication::translate("ScriptApiList", "保存物体数据"),
        QCoreApplication::translate("ScriptApiList", "SaveObj()\n保存物体数据到当前文件\n参数: 无\n示例: SaveObj()\n\nSaveObj(fileName)\n保存物体数据到指定文件\n参数:\n  fileName (string) - 物体数据文件名\n示例: SaveObj(\"scene01.obj\")"));

    addApi("AddObj(iniName, x, y, dir)",
        QCoreApplication::translate("ScriptApiList", "添加物体"),
        QCoreApplication::translate("ScriptApiList", "AddObj(iniName, x, y, dir)\n在指定位置添加物体\n参数:\n  iniName (string) - 物体INI配置名\n  x (number) - X坐标\n  y (number) - Y坐标\n  dir (number) - 方向（可省略，默认0）\n示例: AddObj(\"chest\", 100, 200, 0)"));

    addApi("DelObj(name)",
        QCoreApplication::translate("ScriptApiList", "删除指定物体"),
        QCoreApplication::translate("ScriptApiList", "DelObj(name)\n删除指定名称的物体\n参数:\n  name (string) - 物体名称（空串则删除当前交互物体）\n示例: DelObj(\"chest01\")"));

    addApi("DelCurObj()",
        QCoreApplication::translate("ScriptApiList", "删除当前物体"),
        QCoreApplication::translate("ScriptApiList", "DelCurObj()\n删除当前正在交互的物体\n参数: 无\n示例: DelCurObj()"));

    addApi("SetObjPos(name, x, y)",
        QCoreApplication::translate("ScriptApiList", "设置物体位置"),
        QCoreApplication::translate("ScriptApiList", "SetObjPos(name, x, y)\n设置指定物体的位置\n参数:\n  name (string) - 物体名称（空串则操作当前交互物体）\n  x (number) - X坐标\n  y (number) - Y坐标\n示例: SetObjPos(\"chest01\", 150, 250)"));

    addApi("SetObjKind(name, kind)",
        QCoreApplication::translate("ScriptApiList", "设置物体类型"),
        QCoreApplication::translate("ScriptApiList", "SetObjKind(name, kind)\n设置指定物体的类型\n参数:\n  name (string) - 物体名称（空串则操作当前交互物体）\n  kind (number) - 物体类型\n示例: SetObjKind(\"chest01\", 2)"));

    addApi("SetObjScript(name, scriptFile)",
        QCoreApplication::translate("ScriptApiList", "设置物体脚本"),
        QCoreApplication::translate("ScriptApiList", "SetObjScript(name, scriptFile)\n设置指定物体的关联脚本\n参数:\n  name (string) - 物体名称（空串则操作当前交互物体）\n  scriptFile (string) - 脚本文件名\n示例: SetObjScript(\"chest01\", \"obj/chest01.txt\")"));

    addApi("ClearBody()",
        QCoreApplication::translate("ScriptApiList", "清除尸体"),
        QCoreApplication::translate("ScriptApiList", "ClearBody()\n清除场景中的尸体\n参数: 无\n示例: ClearBody()"));

    addApi("OpenBox()",
        QCoreApplication::translate("ScriptApiList", "打开当前宝箱"),
        QCoreApplication::translate("ScriptApiList", "OpenBox()\n打开当前交互的宝箱物体\n参数: 无\n示例: OpenBox()"));

    addApi("CloseBox()",
        QCoreApplication::translate("ScriptApiList", "关闭当前宝箱"),
        QCoreApplication::translate("ScriptApiList", "CloseBox()\n关闭当前交互的宝箱物体\n参数: 无\n示例: CloseBox()"));

    addApi("OpenObj(name)",
        QCoreApplication::translate("ScriptApiList", "打开物体交互"),
        QCoreApplication::translate("ScriptApiList", "OpenObj(name)\n打开指定名称物体的交互（如宝箱）\n参数:\n  name (string) - 物体名称\n示例: OpenObj(\"chest01\")"));

    // ===== NPC操作 =====
    addApi("LoadNpc(fileName)",
        QCoreApplication::translate("ScriptApiList", "加载NPC数据"),
        QCoreApplication::translate("ScriptApiList", "LoadNpc(fileName)\n同步加载并立即替换当前NPC数据，不显示资源加载界面\n参数:\n  fileName (string) - NPC数据文件名\n示例: LoadNpc(\"scene01.npc\")"));

    addApi("SaveNpc(fileName)",
        QCoreApplication::translate("ScriptApiList", "保存NPC数据"),
        QCoreApplication::translate("ScriptApiList", "SaveNpc()\n保存NPC数据到当前文件\n参数: 无\n示例: SaveNpc()\n\nSaveNpc(fileName)\n保存NPC数据到指定文件\n参数:\n  fileName (string) - NPC数据文件名\n示例: SaveNpc(\"scene01.npc\")"));

    addApi("AddNpc(iniName, x, y, dir)",
        QCoreApplication::translate("ScriptApiList", "添加NPC"),
        QCoreApplication::translate("ScriptApiList", "AddNpc(iniName, x, y, dir)\n在指定位置添加NPC\n参数:\n  iniName (string) - NPC的INI配置名\n  x (number) - X坐标\n  y (number) - Y坐标\n  dir (number) - 朝向（可省略，默认0）\n示例: AddNpc(\"hero\", 100, 200, 0)"));

    addApi("DelNpc(name)",
        QCoreApplication::translate("ScriptApiList", "删除指定NPC"),
        QCoreApplication::translate("ScriptApiList", "DelNpc(name)\n删除指定名称的NPC\n参数:\n  name (string) - NPC名称\n示例: DelNpc(\"guard01\")"));

    addApi("SetNpcRes(name, resName)",
        QCoreApplication::translate("ScriptApiList", "设置NPC资源文件"),
        QCoreApplication::translate("ScriptApiList", "SetNpcRes(name, resName)\n设置指定NPC的资源文件并重新初始化\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  resName (string) - 资源文件路径\n示例: SetNpcRes(\"hero\", \"npc/hero2.asf\")"));

    addApi("SetNpcScript(name, scriptFile)",
        QCoreApplication::translate("ScriptApiList", "设置NPC脚本"),
        QCoreApplication::translate("ScriptApiList", "SetNpcScript(name, scriptFile)\n设置指定NPC的交互脚本\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  scriptFile (string) - 脚本文件路径\n示例: SetNpcScript(\"hero\", \"npc/hero.txt\")"));

    addApi("SetNpcDeathScript(name, scriptFile)",
        QCoreApplication::translate("ScriptApiList", "设置NPC死亡脚本"),
        QCoreApplication::translate("ScriptApiList", "SetNpcDeathScript(name, scriptFile)\n设置指定NPC死亡时触发的脚本\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  scriptFile (string) - 脚本文件路径\n示例: SetNpcDeathScript(\"boss01\", \"npc/boss_death.txt\")"));

    addApi("NpcGoto(name, x, y)",
        QCoreApplication::translate("ScriptApiList", "NPC移动到指定位置"),
        QCoreApplication::translate("ScriptApiList", "NpcGoto(name, x, y)\n让指定NPC移动到目标位置\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  x (number) - 目标X坐标\n  y (number) - 目标Y坐标\n示例: NpcGoto(\"guard01\", 300, 400)"));

    addApi("NpcGotoEx(name, x, y)",
        QCoreApplication::translate("ScriptApiList", "NPC移动到指定位置（扩展）"),
        QCoreApplication::translate("ScriptApiList", "NpcGotoEx(name, x, y)\n让指定NPC移动到目标位置（扩展模式）\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  x (number) - 目标X坐标\n  y (number) - 目标Y坐标\n示例: NpcGotoEx(\"guard01\", 300, 400)"));

    addApi("NpcGotoDir(name, dir, distance)",
        QCoreApplication::translate("ScriptApiList", "NPC朝指定方向移动"),
        QCoreApplication::translate("ScriptApiList", "NpcGotoDir(name, dir, distance)\n让指定NPC朝指定方向移动指定距离\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  dir (number) - 方向\n  distance (number) - 移动距离\n示例: NpcGotoDir(\"guard01\", 2, 100)"));

    addApi("SetNpcDestination(name, x, y)",
        QCoreApplication::translate("ScriptApiList", "设置NPC异步移动目标"),
        QCoreApplication::translate("ScriptApiList", "SetNpcDestination(name, x, y)\n设置指定NPC的异步移动目标，移动中仍可由战斗AI调度\n参数:\n  name (string) - NPC名称\n  x (number) - 目标X坐标\n  y (number) - 目标Y坐标\n示例: SetNpcDestination(\"guard01\", 83, 90)"));

    addApi("FollowNpc(follower, leader)",
        QCoreApplication::translate("ScriptApiList", "NPC跟随另一个NPC"),
        QCoreApplication::translate("ScriptApiList", "FollowNpc(follower, leader)\n让一个NPC跟随另一个NPC移动\n参数:\n  follower (string) - 跟随者NPC名称\n  leader (string) - 被跟随者NPC名称\n示例: FollowNpc(\"pet01\", \"hero\")"));

    addApi("FollowPlayer(follower)",
        QCoreApplication::translate("ScriptApiList", "NPC跟随玩家"),
        QCoreApplication::translate("ScriptApiList", "FollowPlayer(follower)\n让指定NPC跟随玩家移动\n参数:\n  follower (string) - 跟随者NPC名称\n示例: FollowPlayer(\"pet01\")"));

    addApi("EnableNpcAI()",
        QCoreApplication::translate("ScriptApiList", "启用NPC AI"),
        QCoreApplication::translate("ScriptApiList", "EnableNpcAI()\n启用全局NPC AI行为\n参数: 无\n示例: EnableNpcAI()"));

    addApi("DisableNpcAI()",
        QCoreApplication::translate("ScriptApiList", "禁用NPC AI"),
        QCoreApplication::translate("ScriptApiList", "DisableNpcAI()\n禁用全局NPC AI行为\n参数: 无\n示例: DisableNpcAI()"));

    addApi("NpcAttack(name, x, y)",
        QCoreApplication::translate("ScriptApiList", "NPC攻击指定位置"),
        QCoreApplication::translate("ScriptApiList", "NpcAttack(name, x, y)\n让指定NPC向目标位置发起攻击\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  x (number) - 目标X坐标\n  y (number) - 目标Y坐标\n示例: NpcAttack(\"guard01\", 300, 400)"));

    addApi("SetNpcPos(name, x, y)",
        QCoreApplication::translate("ScriptApiList", "设置NPC位置（瞬移）"),
        QCoreApplication::translate("ScriptApiList", "SetNpcPos(name, x, y)\n直接设置指定NPC的位置（瞬移，不播放移动动画）\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  x (number) - X坐标\n  y (number) - Y坐标\n示例: SetNpcPos(\"guard01\", 100, 200)"));

    addApi("SetNpcDir(name, dir)",
        QCoreApplication::translate("ScriptApiList", "设置NPC朝向"),
        QCoreApplication::translate("ScriptApiList", "SetNpcDir(name, dir)\n设置指定NPC的朝向\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  dir (number) - 方向值\n示例: SetNpcDir(\"guard01\", 4)"));

    addApi("SetNpcKind(name, kind)",
        QCoreApplication::translate("ScriptApiList", "设置NPC类型"),
        QCoreApplication::translate("ScriptApiList", "SetNpcKind(name, kind)\n设置指定NPC的类型\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  kind (number) - NPC类型\n示例: SetNpcKind(\"guard01\", 1)"));

    addApi("SetNpcLevel(name, level)",
        QCoreApplication::translate("ScriptApiList", "设置NPC等级"),
        QCoreApplication::translate("ScriptApiList", "SetNpcLevel(name, level)\n设置指定NPC的等级\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  level (number) - 等级\n示例: SetNpcLevel(\"boss01\", 10)"));

    addApi("SetNpcAction(name, action, x?, y?)",
        QCoreApplication::translate("ScriptApiList", "设置NPC动作"),
        QCoreApplication::translate("ScriptApiList", "SetNpcAction(name, action, x?, y?)\n设置指定NPC的当前动作\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  action (number) - 脚本动作号（按资源 NpcActionProfile 映射；-1=站立兼容）\n  x, y (number, optional) - 移动、攻击等动作的目标格坐标\n常用编号: 0/1=站立, 2=走, 3=跑, 4=跳, 5-7=攻击, 8=施法, 11=死亡；9/10/12-15 由资源动作协议解释\n示例: SetNpcAction(\"guard01\", 2, 10, 20)"));

    addApi("SetNpcRelation(name, relation)",
        QCoreApplication::translate("ScriptApiList", "设置NPC关系"),
        QCoreApplication::translate("ScriptApiList", "SetNpcRelation(name, relation)\n设置指定NPC与玩家的关系类型\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  relation (number) - 关系类型\n示例: SetNpcRelation(\"guard01\", 1)"));

    addApi("SetNpcActionType(name, strollIntent)",
        QCoreApplication::translate("ScriptApiList", "设置NPC行为类型"),
        QCoreApplication::translate("ScriptApiList", "SetNpcActionType(name, strollIntent)\n设置指定NPC的巡逻/行为意图类型\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  strollIntent (number) - 行为意图值\n示例: SetNpcActionType(\"guard01\", 3)"));

    addApi("SetNpcActionFile(name, action, fileName)",
        QCoreApplication::translate("ScriptApiList", "设置NPC动作文件"),
        QCoreApplication::translate("ScriptApiList", "SetNpcActionFile(name, action, fileName)\n加载指定NPC的动作文件\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  action (number) - 动作编号\n  fileName (string) - 动作文件路径\n示例: SetNpcActionFile(\"hero\", 1, \"action/special.act\")"));

    addApi("NpcSpecialAction(name, fileName)",
        QCoreApplication::translate("ScriptApiList", "NPC执行特殊动作"),
        QCoreApplication::translate("ScriptApiList", "NpcSpecialAction(name, fileName)\n让指定NPC执行特殊动作\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  fileName (string) - 特殊动作文件路径\n示例: NpcSpecialAction(\"hero\", \"action/dance.act\")"));

    addApi("NpcSpecialActionEx(name, fileName)",
        QCoreApplication::translate("ScriptApiList", "NPC执行扩展特殊动作"),
        QCoreApplication::translate("ScriptApiList", "NpcSpecialActionEx(name, fileName)\n让指定NPC加载并执行扩展特殊动作\n参数:\n  name (string) - NPC名称（空串则操作当前交互NPC）\n  fileName (string) - 特殊动作文件路径\n示例: NpcSpecialActionEx(\"hero\", \"action/skill.act\")"));

    addApi("GetNpcState(name, stateName, varName)",
        QCoreApplication::translate("ScriptApiList", "Read an NPC state value"),
        QCoreApplication::translate("ScriptApiList", "GetNpcState(name, stateName, varName)\nReads an NPC state such as KindValue, KindValueMax, Attack, Defend, Evade, Life, Thew, or Mana into a script variable.\nExample: GetNpcState(\"shopkeeper\", \"KindValue\", \"kindValue\")"));

    addApi("AddKindValue(name, value)",
        QCoreApplication::translate("ScriptApiList", "Adjust NPC affinity"),
        QCoreApplication::translate("ScriptApiList", "AddKindValue(name, value)\nAdds to the NPC KindValue affinity field, clamped to 0 and KindValueMax when present.\nExample: AddKindValue(\"shopkeeper\", -500)"));

    addApi("SetMapNpcAttr(name, attributes, npcFileName)",
        QCoreApplication::translate("ScriptApiList", "Update NPC file attributes"),
        QCoreApplication::translate("ScriptApiList", "SetMapNpcAttr(name, attributes, npcFileName)\nUpdates all matching Name entries in an NPC save/template file. attributes uses Key:Value pairs separated by semicolons.\nExample: SetMapNpcAttr(\"shopkeeper\", \"Kind:1;Relation:1;KindValue:3500\", \"map001.npc\")"));

    addApi("SetNpcTalkContent(name, content, npcFileName)",
        QCoreApplication::translate("ScriptApiList", "Update NPC auto talk text"),
        QCoreApplication::translate("ScriptApiList", "SetNpcTalkContent(name, content)\nSets the current map NPC auto-talk content. With npcFileName, also updates that NPC file.\nExample: SetNpcTalkContent(\"shopkeeper\", \"I will remember this.\", \"map001.npc\")"));

    addApi("TalkSelfTip(name, message, appendText)",
        QCoreApplication::translate("ScriptApiList", "Show NPC bubble text"),
        QCoreApplication::translate("ScriptApiList", "TalkSelfTip(name, message, appendText)\nShows NPC self/bubble text; appendText is optional and is concatenated to message.\nExample: TalkSelfTip(\"shopkeeper\", \"Affinity is \", GetVar(\"kindValue\"))"));

    addApi("SetAllNpcIsEnemy()",
        QCoreApplication::translate("ScriptApiList", "Make all NPCs hostile"),
        QCoreApplication::translate("ScriptApiList", "SetAllNpcIsEnemy()\nSets every NPC on the current map to battle kind and hostile relation.\nExample: SetAllNpcIsEnemy()"));

    addApi("ShowSignalTip(name, signalIndex, signalType)",
        QCoreApplication::translate("ScriptApiList", "Show NPC head icon"),
        QCoreApplication::translate("ScriptApiList", "ShowSignalTip(name, signalIndex, signalType)\nShows a persistent icon above an NPC. signalIndex is read from ini/ui/tips/SignalFile.ini and assets are loaded from asf/signal. signalType t0 shakes and t1 blinks.\nExample: ShowSignalTip(\"shopkeeper\", 23, \"t1\")"));

    addApi("SetSignalTipHidden(name)",
        QCoreApplication::translate("ScriptApiList", "Hide NPC head icon"),
        QCoreApplication::translate("ScriptApiList", "SetSignalTipHidden(name)\nHides the persistent signal icon above an NPC.\nExample: SetSignalTipHidden(\"shopkeeper\")"));

    addApi("ShowNpc(name, isShow)",
        QCoreApplication::translate("ScriptApiList", "显示/隐藏NPC"),
        QCoreApplication::translate("ScriptApiList", "ShowNpc(name, isShow)\n设置指定NPC的显示/隐藏状态\n参数:\n  name (string) - NPC名称\n  isShow (number) - 1=显示（切换到站立动作） 0=隐藏（切换到隐藏动作）\n示例: ShowNpc(\"spy01\", 0)"));

    addApi("SetNpcMagicFile(name, fileName)",
        QCoreApplication::translate("ScriptApiList", "设置NPC魔法文件"),
        QCoreApplication::translate("ScriptApiList", "SetNpcMagicFile(name, fileName)\n设置指定NPC的远程攻击魔法资源文件\n参数:\n  name (string) - NPC名称\n  fileName (string) - 魔法文件路径\n示例: SetNpcMagicFile(\"mage01\", \"magic/fireball.mag\")"));

    addApi("SetNpcMagicLevel(name, level)",
        QCoreApplication::translate("ScriptApiList", "设置NPC魔法等级"),
        QCoreApplication::translate("ScriptApiList", "SetNpcMagicLevel(name, level)\n设置指定NPC的魔法等级\n参数:\n  name (string) - NPC名称\n  level (number) - 魔法等级\n示例: SetNpcMagicLevel(\"mage01\", 5)"));

    addApi("SetNpcClickScript(name, scriptFile)",
        QCoreApplication::translate("ScriptApiList", "设置NPC点击脚本"),
        QCoreApplication::translate("ScriptApiList", "SetNpcClickScript(name, scriptFile)\n设置指定NPC被点击时触发的脚本\n参数:\n  name (string) - NPC名称\n  scriptFile (string) - 脚本文件路径\n示例: SetNpcClickScript(\"npc01\", \"npc/click.txt\")"));

    addApi("MergeNpc(fileName)",
        QCoreApplication::translate("ScriptApiList", "合并NPC数据"),
        QCoreApplication::translate("ScriptApiList", "MergeNpc(fileName)\n将指定NPC数据文件合并到当前场景（不清除已有NPC）\n参数:\n  fileName (string) - NPC数据文件名\n示例: MergeNpc(\"reinforce.npc\")"));

    // ===== NPC属性修改 =====
    addApi("ChangeLife(name, value)",
        QCoreApplication::translate("ScriptApiList", "改变NPC生命值（百分比）"),
        QCoreApplication::translate("ScriptApiList", "ChangeLife(name, value)\n按百分比设置NPC生命值（基于最大生命值）\n参数:\n  name (string) - NPC名称\n  value (number) - 百分比值（如50表示设为50%最大生命值）\n示例: ChangeLife(\"boss01\", 50)"));

    addApi("ChangeMana(name, value)",
        QCoreApplication::translate("ScriptApiList", "改变NPC法力值（百分比）"),
        QCoreApplication::translate("ScriptApiList", "ChangeMana(name, value)\n按百分比设置NPC法力值（基于最大法力值）\n参数:\n  name (string) - NPC名称\n  value (number) - 百分比值\n示例: ChangeMana(\"mage01\", 80)"));

    addApi("ChangeThew(name, value)",
        QCoreApplication::translate("ScriptApiList", "改变NPC体力值（百分比）"),
        QCoreApplication::translate("ScriptApiList", "ChangeThew(name, value)\n按百分比设置NPC体力值（基于最大体力值）\n参数:\n  name (string) - NPC名称\n  value (number) - 百分比值\n示例: ChangeThew(\"hero\", 100)"));

    // ===== 玩家操作 =====
    addApi("LoadPlayer(index)",
        QCoreApplication::translate("ScriptApiList", "加载玩家数据"),
        QCoreApplication::translate("ScriptApiList", "LoadPlayer(index)\n加载指定索引的玩家数据\n参数:\n  index (number) - 角色索引（可省略，默认-1为当前角色）\n示例: LoadPlayer(0)"));

    addApi("SavePlayer(index)",
        QCoreApplication::translate("ScriptApiList", "保存玩家数据"),
        QCoreApplication::translate("ScriptApiList", "SavePlayer(index)\n保存玩家数据到指定索引\n参数:\n  index (number) - 角色索引（可省略，默认-1为当前角色）\n示例: SavePlayer(0)"));

    addApi("SetPlayerPos(x, y)",
        QCoreApplication::translate("ScriptApiList", "设置玩家位置（瞬移）"),
        QCoreApplication::translate("ScriptApiList", "SetPlayerPos(x, y)\n直接设置玩家位置（瞬移，同时移动伙伴位置）\n参数:\n  x (number) - X坐标\n  y (number) - Y坐标\n示例: SetPlayerPos(100, 200)"));

    addApi("SetPlayerDir(dir)",
        QCoreApplication::translate("ScriptApiList", "设置玩家朝向"),
        QCoreApplication::translate("ScriptApiList", "SetPlayerDir(dir)\n设置玩家朝向\n参数:\n  dir (number) - 方向值\n示例: SetPlayerDir(4)"));

    addApi("SetPlayerScn()",
        QCoreApplication::translate("ScriptApiList", "设置镜头跟随玩家"),
        QCoreApplication::translate("ScriptApiList", "SetPlayerScn()\n设置镜头跟随玩家，恢复默认视角\n参数: 无\n示例: SetPlayerScn()"));

    addApi("SetPlayerLum(lum)",
        QCoreApplication::translate("ScriptApiList", "设置玩家亮度"),
        QCoreApplication::translate("ScriptApiList", "SetPlayerLum(lum)\n设置玩家亮度\n参数:\n  lum (number) - 亮度值\n示例: SetPlayerLum(128)"));

    addApi("SetLevelFile(fileName)",
        QCoreApplication::translate("ScriptApiList", "设置等级配置文件"),
        QCoreApplication::translate("ScriptApiList", "SetLevelFile(fileName)\n设置玩家等级配置文件并重新加载\n参数:\n  fileName (string) - 等级文件路径\n示例: SetLevelFile(\"config/level.dat\")"));

    addApi("SetMagicLevel(magicName, level)",
        QCoreApplication::translate("ScriptApiList", "设置魔法等级"),
        QCoreApplication::translate("ScriptApiList", "SetMagicLevel(magicName, level)\n设置指定魔法的等级\n参数:\n  magicName (string) - 魔法名称\n  level (number) - 魔法等级\n示例: SetMagicLevel(\"fireball\", 5)"));

    addApi("MoveMagic(magicName, position)",
        QCoreApplication::translate("ScriptApiList", "移动魔法到快捷栏位置"),
        QCoreApplication::translate("ScriptApiList", "MoveMagic(magicName, position)\n将指定魔法移动到快捷栏的指定位置\n参数:\n  magicName (string) - 魔法名称\n  position (number) - 快捷栏位置（从1开始）\n示例: MoveMagic(\"fireball\", 1)"));

    addApi("SetPlayerLevel(level)",
        QCoreApplication::translate("ScriptApiList", "设置玩家等级"),
        QCoreApplication::translate("ScriptApiList", "SetPlayerLevel(level)\n设置玩家等级\n参数:\n  level (number) - 等级\n示例: SetPlayerLevel(10)"));

    addApi("SetPlayerState(state)",
        QCoreApplication::translate("ScriptApiList", "设置玩家战斗状态"),
        QCoreApplication::translate("ScriptApiList", "SetPlayerState(state)\n设置玩家战斗状态\n参数:\n  state (number) - 状态值（0=非战斗, 非0=战斗）\n示例: SetPlayerState(1)"));

    addApi("EnableRun()",
        QCoreApplication::translate("ScriptApiList", "启用跑步"),
        QCoreApplication::translate("ScriptApiList", "EnableRun()\n启用玩家跑步功能\n参数: 无\n示例: EnableRun()"));

    addApi("DisableRun()",
        QCoreApplication::translate("ScriptApiList", "禁用跑步"),
        QCoreApplication::translate("ScriptApiList", "DisableRun()\n禁用玩家跑步功能\n参数: 无\n示例: DisableRun()"));

    addApi("EnableJump()",
        QCoreApplication::translate("ScriptApiList", "启用跳跃"),
        QCoreApplication::translate("ScriptApiList", "EnableJump()\n启用玩家跳跃功能\n参数: 无\n示例: EnableJump()"));

    addApi("DisableJump()",
        QCoreApplication::translate("ScriptApiList", "禁用跳跃"),
        QCoreApplication::translate("ScriptApiList", "DisableJump()\n禁用玩家跳跃功能\n参数: 无\n示例: DisableJump()"));

    addApi("EnableFight()",
        QCoreApplication::translate("ScriptApiList", "启用战斗"),
        QCoreApplication::translate("ScriptApiList", "EnableFight()\n启用玩家战斗功能\n参数: 无\n示例: EnableFight()"));

    addApi("DisableFight()",
        QCoreApplication::translate("ScriptApiList", "禁用战斗"),
        QCoreApplication::translate("ScriptApiList", "DisableFight()\n禁用玩家战斗功能\n参数: 无\n示例: DisableFight()"));

    addApi("PlayerGoto(x, y)",
        QCoreApplication::translate("ScriptApiList", "玩家走到指定位置"),
        QCoreApplication::translate("ScriptApiList", "PlayerGoto(x, y)\n让玩家走到目标位置\n参数:\n  x (number) - 目标X坐标\n  y (number) - 目标Y坐标\n示例: PlayerGoto(300, 400)"));

    addApi("PlayerGotoEx(x, y)",
        QCoreApplication::translate("ScriptApiList", "玩家走到指定位置（扩展）"),
        QCoreApplication::translate("ScriptApiList", "PlayerGotoEx(x, y)\n让玩家走到目标位置（扩展模式）\n参数:\n  x (number) - 目标X坐标\n  y (number) - 目标Y坐标\n示例: PlayerGotoEx(300, 400)"));

    addApi("PlayerRunTo(x, y)",
        QCoreApplication::translate("ScriptApiList", "玩家跑到指定位置"),
        QCoreApplication::translate("ScriptApiList", "PlayerRunTo(x, y)\n让玩家跑到目标位置\n参数:\n  x (number) - 目标X坐标\n  y (number) - 目标Y坐标\n示例: PlayerRunTo(300, 400)"));

    addApi("PlayerJumpTo(x, y)",
        QCoreApplication::translate("ScriptApiList", "玩家跳到指定位置"),
        QCoreApplication::translate("ScriptApiList", "PlayerJumpTo(x, y)\n让玩家跳到目标位置\n参数:\n  x (number) - 目标X坐标\n  y (number) - 目标Y坐标\n示例: PlayerJumpTo(300, 400)"));

    addApi("PlayerGotoDir(dir, distance)",
        QCoreApplication::translate("ScriptApiList", "玩家朝指定方向移动"),
        QCoreApplication::translate("ScriptApiList", "PlayerGotoDir(dir, distance)\n让玩家朝指定方向移动指定距离\n参数:\n  dir (number) - 方向\n  distance (number) - 移动距离\n示例: PlayerGotoDir(2, 50)"));

    addApi("PlayerChange(index)",
        QCoreApplication::translate("ScriptApiList", "切换玩家角色"),
        QCoreApplication::translate("ScriptApiList", "PlayerChange(index)\n切换到指定索引的角色，保存当前角色数据并加载新角色\n参数:\n  index (number) - 角色索引\n示例: PlayerChange(1)"));

    // ===== 属性增减 =====
    addApi("AddLife(value)",
        QCoreApplication::translate("ScriptApiList", "增加玩家生命值"),
        QCoreApplication::translate("ScriptApiList", "AddLife(value)\n增加玩家当前生命值\n参数:\n  value (number) - 增加值\n示例: AddLife(50)"));

    addApi("AddLifeMax(value)",
        QCoreApplication::translate("ScriptApiList", "增加玩家生命上限"),
        QCoreApplication::translate("ScriptApiList", "AddLifeMax(value)\n增加玩家最大生命值\n参数:\n  value (number) - 增加值\n示例: AddLifeMax(100)"));

    addApi("AddThew(value)",
        QCoreApplication::translate("ScriptApiList", "增加玩家体力值"),
        QCoreApplication::translate("ScriptApiList", "AddThew(value)\n增加玩家当前体力值\n参数:\n  value (number) - 增加值\n示例: AddThew(30)"));

    addApi("AddThewMax(value)",
        QCoreApplication::translate("ScriptApiList", "增加玩家体力上限"),
        QCoreApplication::translate("ScriptApiList", "AddThewMax(value)\n增加玩家最大体力值\n参数:\n  value (number) - 增加值\n示例: AddThewMax(50)"));

    addApi("AddMana(value)",
        QCoreApplication::translate("ScriptApiList", "增加玩家法力值"),
        QCoreApplication::translate("ScriptApiList", "AddMana(value)\n增加玩家当前法力值\n参数:\n  value (number) - 增加值\n示例: AddMana(20)"));

    addApi("AddManaMax(value)",
        QCoreApplication::translate("ScriptApiList", "增加玩家法力上限"),
        QCoreApplication::translate("ScriptApiList", "AddManaMax(value)\n增加玩家最大法力值\n参数:\n  value (number) - 增加值\n示例: AddManaMax(50)"));

    addApi("AddAttack(value)",
        QCoreApplication::translate("ScriptApiList", "增加玩家攻击力"),
        QCoreApplication::translate("ScriptApiList", "AddAttack(value)\n增加玩家攻击力\n参数:\n  value (number) - 增加值\n示例: AddAttack(5)"));

    addApi("AddDefend(value)",
        QCoreApplication::translate("ScriptApiList", "增加玩家防御力"),
        QCoreApplication::translate("ScriptApiList", "AddDefend(value)\n增加玩家防御力\n参数:\n  value (number) - 增加值\n示例: AddDefend(3)"));

    addApi("AddEvade(value)",
        QCoreApplication::translate("ScriptApiList", "增加玩家闪避率"),
        QCoreApplication::translate("ScriptApiList", "AddEvade(value)\n增加玩家闪避率\n参数:\n  value (number) - 增加值\n示例: AddEvade(2)"));

    addApi("AddExp(value)",
        QCoreApplication::translate("ScriptApiList", "增加玩家经验值"),
        QCoreApplication::translate("ScriptApiList", "AddExp(value)\n增加玩家经验值\n参数:\n  value (number) - 增加值\n示例: AddExp(100)"));

    addApi("AddMoney(value)",
        QCoreApplication::translate("ScriptApiList", "增加/减少玩家金钱"),
        QCoreApplication::translate("ScriptApiList", "AddMoney(value)\n增加或减少玩家金钱，正数增加并提示获得，负数减少并提示失去\n参数:\n  value (number) - 金钱变化值（正数增加，负数减少，0无效果）\n示例: AddMoney(500)"));

    addApi("AddRandMoney(minValue, maxValue)",
        QCoreApplication::translate("ScriptApiList", "增加随机金钱"),
        QCoreApplication::translate("ScriptApiList", "AddRandMoney(minValue, maxValue)\n在指定范围内随机增加金钱\n参数:\n  minValue (number) - 最小值\n  maxValue (number) - 最大值\n示例: AddRandMoney(100, 500)"));

    addApi("EquipGoods(listIndex, partIndex)",
        QCoreApplication::translate("ScriptApiList", "装备物品"),
        QCoreApplication::translate("ScriptApiList", "EquipGoods(listIndex, partIndex)\n使用/装备指定位置的物品\n参数:\n  listIndex (number) - 物品在列表中的位置（从1开始）\n  partIndex (number) - 装备部位索引\n示例: EquipGoods(1, 1)"));

    addApi("AddGoods(name)",
        QCoreApplication::translate("ScriptApiList", "添加物品"),
        QCoreApplication::translate("ScriptApiList", "AddGoods(name)\n给玩家添加指定名称的物品\n参数:\n  name (string) - 物品名称\n示例: AddGoods(\"sword_iron\")"));

    addApi("AddRandGoods(fileName)",
        QCoreApplication::translate("ScriptApiList", "随机添加物品"),
        QCoreApplication::translate("ScriptApiList", "AddRandGoods(fileName)\n从指定物品列表文件中随机添加物品\n参数:\n  fileName (string) - 物品列表文件名\n示例: AddRandGoods(\"loot/chest_drop.txt\")"));

    addApi("DelGoods(name)",
        QCoreApplication::translate("ScriptApiList", "删除物品"),
        QCoreApplication::translate("ScriptApiList", "DelGoods(name)\n删除玩家指定名称的物品\n参数:\n  name (string) - 物品名称\n示例: DelGoods(\"sword_iron\")"));

    addApi("AddMagic(name)",
        QCoreApplication::translate("ScriptApiList", "添加魔法/技能"),
        QCoreApplication::translate("ScriptApiList", "AddMagic(name)\n给当前角色添加指定魔法/技能\n参数:\n  name (string) - 魔法名称\n示例: AddMagic(\"fireball\")"));

    addApi("AddTalent(name)",
        QCoreApplication::translate("ScriptApiList", "Add a talent/passive skill"),
        QCoreApplication::translate("ScriptApiList", "AddTalent(name)\nAdds a talent or passive skill using the same storage as magic skills.\nExample: AddTalent(\"player-talent-steal.ini\")"));

    addApi("AddOneMagic(playerName, magicName)",
        QCoreApplication::translate("ScriptApiList", "给指定角色添加魔法"),
        QCoreApplication::translate("ScriptApiList", "AddOneMagic(playerName, magicName)\n给指定角色添加魔法/技能\n参数:\n  playerName (string) - 角色名称\n  magicName (string) - 魔法名称\n示例: AddOneMagic(\"hero\", \"thunder\")"));

    addApi("DelMagic(name)",
        QCoreApplication::translate("ScriptApiList", "删除魔法/技能"),
        QCoreApplication::translate("ScriptApiList", "DelMagic(name)\n删除指定名称的魔法/技能\n参数:\n  name (string) - 魔法名称\n示例: DelMagic(\"fireball\")"));

    addApi("AddMagicExp(name, addexp)",
        QCoreApplication::translate("ScriptApiList", "增加魔法经验值"),
        QCoreApplication::translate("ScriptApiList", "AddMagicExp(name, addexp)\n增加指定魔法的经验值\n参数:\n  name (string) - 魔法名称\n  addexp (number) - 增加的经验值\n示例: AddMagicExp(\"fireball\", 100)"));

    addApi("GetPlayerMagicLevel(magicName, varName)",
        QCoreApplication::translate("ScriptApiList", "获取玩家武功等级"),
        QCoreApplication::translate("ScriptApiList", "GetPlayerMagicLevel(magicName, varName)\n获取当前玩家指定武功等级并存入变量，未学会时为0\n参数:\n  magicName (string) - 武功配置文件名\n  varName (string) - 存储等级的变量名\n示例: GetPlayerMagicLevel(\"player-magic-银钩铁划.ini\", \"tmp\")"));

    addApi("FullLife()",
        QCoreApplication::translate("ScriptApiList", "恢复满生命值"),
        QCoreApplication::translate("ScriptApiList", "FullLife()\n将玩家生命值恢复到最大值\n参数: 无\n示例: FullLife()"));

    addApi("FullThew()",
        QCoreApplication::translate("ScriptApiList", "恢复满体力值"),
        QCoreApplication::translate("ScriptApiList", "FullThew()\n将玩家体力值恢复到最大值\n参数: 无\n示例: FullThew()"));

    addApi("FullMana()",
        QCoreApplication::translate("ScriptApiList", "恢复满法力值"),
        QCoreApplication::translate("ScriptApiList", "FullMana()\n将玩家法力值恢复到最大值\n参数: 无\n示例: FullMana()"));

    addApi("UpdateState()",
        QCoreApplication::translate("ScriptApiList", "更新玩家状态显示"),
        QCoreApplication::translate("ScriptApiList", "UpdateState()\n刷新更新玩家界面状态显示\n参数: 无\n示例: UpdateState()"));

    addApi("SaveGoods(index)",
        QCoreApplication::translate("ScriptApiList", "保存物品数据"),
        QCoreApplication::translate("ScriptApiList", "SaveGoods(index)\n保存物品数据到指定索引\n参数:\n  index (number) - 存档索引（可省略，默认-1）\n示例: SaveGoods(0)"));

    addApi("LoadGoods(index)",
        QCoreApplication::translate("ScriptApiList", "加载物品数据"),
        QCoreApplication::translate("ScriptApiList", "LoadGoods(index)\n加载指定索引的物品数据\n参数:\n  index (number) - 存档索引（可省略，默认-1）\n示例: LoadGoods(0)"));

    addApi("ClearGoods()",
        QCoreApplication::translate("ScriptApiList", "清空物品栏"),
        QCoreApplication::translate("ScriptApiList", "ClearGoods()\n清空玩家所有物品\n参数: 无\n示例: ClearGoods()"));

    addApi("GetGoodsNum(name)",
        QCoreApplication::translate("ScriptApiList", "获取物品数量"),
        QCoreApplication::translate("ScriptApiList", "GetGoodsNum(name)\n获取指定物品的数量\n参数:\n  name (string) - 物品名称\n示例: GetGoodsNum(\"potion_hp\")"));

    addApi("GetMoneyNum()",
        QCoreApplication::translate("ScriptApiList", "获取金钱数量"),
        QCoreApplication::translate("ScriptApiList", "GetMoneyNum()\n获取玩家当前金钱数量\n参数: 无\n示例: GetMoneyNum()"));

    addApi("SetMoneyNum(value)",
        QCoreApplication::translate("ScriptApiList", "设置金钱数量"),
        QCoreApplication::translate("ScriptApiList", "SetMoneyNum(value)\n直接设置玩家的金钱数量\n参数:\n  value (number) - 金钱数量\n示例: SetMoneyNum(1000)"));

    addApi("LimitMana(limit)",
        QCoreApplication::translate("ScriptApiList", "限制法力上限"),
        QCoreApplication::translate("ScriptApiList", "LimitMana(limit)\n设置法力值上限限制\n参数:\n  limit (number) - 法力上限值\n示例: LimitMana(50)"));

    // ===== 买卖系统 =====
    addApi("BuyGoods(fileName)",
        QCoreApplication::translate("ScriptApiList", "打开购买界面"),
        QCoreApplication::translate("ScriptApiList", "BuyGoods(fileName)\n打开购买物品界面\n参数:\n  fileName (string) - 商品列表文件名\n示例: BuyGoods(\"shop/weapon_shop.txt\")"));

    addApi("BuyGoodsOnly(fileName)",
        QCoreApplication::translate("ScriptApiList", "打开仅购买界面"),
        QCoreApplication::translate("ScriptApiList", "BuyGoodsOnly(fileName)\n打开仅购买物品界面，不允许从玩家背包向商店出售；等价于 BuyGoods(..., canSellSelfGoods=false)\n参数:\n  fileName (string) - 商品列表文件名\n示例: BuyGoodsOnly(\"衣3级.ini\")"));

    addApi("SellGoods(fileName)",
        QCoreApplication::translate("ScriptApiList", "打开出售界面"),
        QCoreApplication::translate("ScriptApiList", "SellGoods()\n打开出售物品界面\n参数: 无\n示例: SellGoods()\n\nSellGoods(fileName)\n打开出售界面并加载指定商品列表\n参数:\n  fileName (string) - 商品列表文件名\n示例: SellGoods(\"shop/sell_list.txt\")"));

    // ===== 界面控制 =====
    addApi("ReturnToTitle()",
        QCoreApplication::translate("ScriptApiList", "返回标题画面"),
        QCoreApplication::translate("ScriptApiList", "ReturnToTitle()\n返回游戏标题画面\n参数: 无\n示例: ReturnToTitle()"));

    addApi("EnableInput()",
        QCoreApplication::translate("ScriptApiList", "启用玩家输入"),
        QCoreApplication::translate("ScriptApiList", "EnableInput()\n启用玩家输入控制\n参数: 无\n示例: EnableInput()"));

    addApi("DisableInput()",
        QCoreApplication::translate("ScriptApiList", "禁用玩家输入"),
        QCoreApplication::translate("ScriptApiList", "DisableInput()\n禁用玩家输入控制（过场动画时使用）\n参数: 无\n示例: DisableInput()"));

    addApi("HideInterface()",
        QCoreApplication::translate("ScriptApiList", "隐藏游戏界面"),
        QCoreApplication::translate("ScriptApiList", "HideInterface()\n隐藏所有游戏界面元素\n参数: 无\n示例: HideInterface()"));

    addApi("ShowInterface()",
        QCoreApplication::translate("ScriptApiList", "显示游戏界面"),
        QCoreApplication::translate("ScriptApiList", "ShowInterface()\n显示所有游戏界面元素\n参数: 无\n示例: ShowInterface()"));

    addApi("HideBottomWnd()",
        QCoreApplication::translate("ScriptApiList", "隐藏底部窗口"),
        QCoreApplication::translate("ScriptApiList", "HideBottomWnd()\n隐藏游戏底部操作窗口\n参数: 无\n示例: HideBottomWnd()"));

    addApi("ShowBottomWnd()",
        QCoreApplication::translate("ScriptApiList", "显示底部窗口"),
        QCoreApplication::translate("ScriptApiList", "ShowBottomWnd()\n显示游戏底部操作窗口\n参数: 无\n示例: ShowBottomWnd()"));

    addApi("HideMouseCursor()",
        QCoreApplication::translate("ScriptApiList", "隐藏鼠标光标"),
        QCoreApplication::translate("ScriptApiList", "HideMouseCursor()\n隐藏游戏鼠标光标\n参数: 无\n示例: HideMouseCursor()"));

    addApi("ShowMouseCursor()",
        QCoreApplication::translate("ScriptApiList", "显示鼠标光标"),
        QCoreApplication::translate("ScriptApiList", "ShowMouseCursor()\n显示游戏鼠标光标\n参数: 无\n示例: ShowMouseCursor()"));

    addApi("DrawBackground()",
        QCoreApplication::translate("ScriptApiList", "兼容占位（语义未确认）"),
        QCoreApplication::translate("ScriptApiList", "DrawBackground()\n历史脚本兼容占位；原始语义未确认，当前不会触发额外绘制\n参数: 无\n示例: DrawBackground()"));

    // ===== 天气效果 =====
    addApi("ShowSnow(bsnow)",
        QCoreApplication::translate("ScriptApiList", "显示下雪效果"),
        QCoreApplication::translate("ScriptApiList", "ShowSnow(bsnow)\n显示或设置下雪效果\n参数:\n  bsnow (number) - 雪花效果参数\n示例: ShowSnow(1)"));

    addApi("ShowRandomSnow()",
        QCoreApplication::translate("ScriptApiList", "显示随机下雪效果"),
        QCoreApplication::translate("ScriptApiList", "ShowRandomSnow()\n显示随机下雪效果\n参数: 无\n示例: ShowRandomSnow()"));

    addApi("ShowRain(brain)",
        QCoreApplication::translate("ScriptApiList", "显示下雨效果"),
        QCoreApplication::translate("ScriptApiList", "ShowRain(brain)\n显示或设置下雨效果\n参数:\n  brain (number) - 雨效果参数\n示例: ShowRain(1)"));

    addApi("BeginRain(configFileName)",
        QCoreApplication::translate("ScriptApiList", "开始下雨（带配置）"),
        QCoreApplication::translate("ScriptApiList", "BeginRain(configFileName)\n使用指定配置文件开始下雨效果\n参数:\n  configFileName (string) - 雨效果配置文件名\n示例: BeginRain(\"config/heavy_rain.txt\")"));

    addApi("EndRain()",
        QCoreApplication::translate("ScriptApiList", "停止下雨"),
        QCoreApplication::translate("ScriptApiList", "EndRain()\n停止下雨效果\n参数: 无\n示例: EndRain()"));

    // ===== 水面效果 =====
    addApi("OpenWaterEffect()",
        QCoreApplication::translate("ScriptApiList", "开启水面效果"),
        QCoreApplication::translate("ScriptApiList", "OpenWaterEffect()\n开启水面波纹效果\n参数: 无\n示例: OpenWaterEffect()"));

    addApi("CloseWaterEffect()",
        QCoreApplication::translate("ScriptApiList", "关闭水面效果"),
        QCoreApplication::translate("ScriptApiList", "CloseWaterEffect()\n关闭水面波纹效果\n参数: 无\n示例: CloseWaterEffect()"));

    // ===== 特效 =====
    addApi("ClearEffect()",
        QCoreApplication::translate("ScriptApiList", "清除所有特效"),
        QCoreApplication::translate("ScriptApiList", "ClearEffect()\n清除场景中所有特效\n参数: 无\n示例: ClearEffect()"));

    // ===== 查询函数 =====
    addApi("CheckYear(varName)",
        QCoreApplication::translate("ScriptApiList", "检查年份并存入变量"),
        QCoreApplication::translate("ScriptApiList", "CheckYear(varName)\n获取当前年份并存入指定变量\n参数:\n  varName (string) - 存储年份的变量名\n示例: CheckYear(\"currentYear\")"));

    addApi("GetRandNum(varName, minVal, maxVal)",
        QCoreApplication::translate("ScriptApiList", "获取随机数"),
        QCoreApplication::translate("ScriptApiList", "GetRandNum(varName, minVal, maxVal)\n生成指定范围内的随机数并存入变量\n参数:\n  varName (string) - 存储随机数的变量名\n  minVal (number) - 最小值\n  maxVal (number) - 最大值\n示例: GetRandNum(\"randResult\", 1, 100)"));

    addApi("GetPlayerLevel(varName)",
        QCoreApplication::translate("ScriptApiList", "获取玩家等级"),
        QCoreApplication::translate("ScriptApiList", "GetPlayerLevel(varName)\n获取当前玩家等级并存入变量\n参数:\n  varName (string) - 存储等级的变量名\n示例: GetPlayerLevel(\"playerLevel\")"));

    addApi("SetWalkIsRun(value)",
        QCoreApplication::translate("ScriptApiList", "设置行走改为跑步"),
        QCoreApplication::translate("ScriptApiList", "SetWalkIsRun(value)\n设置玩家后续行走意图是否按跑步处理\n参数:\n  value (number) - 大于0表示行走按跑步处理，0表示恢复默认\n示例: SetWalkIsRun(1)"));

    addApi("GetNpcCount(kind, relation)",
        QCoreApplication::translate("ScriptApiList", "获取NPC数量"),
        QCoreApplication::translate("ScriptApiList", "GetNpcCount(kind, relation)\n获取指定类型和关系的NPC数量\n参数:\n  kind (number) - NPC类型\n  relation (number) - NPC关系\n示例: GetNpcCount(1, 0)"));

    // ===== 伙伴系统 =====
    addApi("SetPartnerLevel(level)",
        QCoreApplication::translate("ScriptApiList", "设置伙伴等级"),
        QCoreApplication::translate("ScriptApiList", "SetPartnerLevel(level)\n设置伙伴的等级\n参数:\n  level (number) - 等级值\n示例: SetPartnerLevel(5)"));

    addApi("GetPartnerIdx(varName)",
        QCoreApplication::translate("ScriptApiList", "获取伙伴索引"),
        QCoreApplication::translate("ScriptApiList", "GetPartnerIdx(varName)\n获取当前伙伴的索引并存入变量\n参数:\n  varName (string) - 存储伙伴索引的变量名\n示例: GetPartnerIdx(\"partnerIndex\")"));

    // ===== 观察系统 =====
    addApi("Watch(name1, name2, watchType)",
        QCoreApplication::translate("ScriptApiList", "设置NPC观察"),
        QCoreApplication::translate("ScriptApiList", "Watch(name1, name2, watchType)\n设置NPC之间的观察关系\n参数:\n  name1 (string) - 观察者NPC名称\n  name2 (string) - 被观察者NPC名称\n  watchType (number) - 观察类型（可省略，默认0）\n示例: Watch(\"guard01\", \"hero\", 0)"));

    // ===== 限时系统 =====
    addApi("OpenTimeLimit(seconds)",
        QCoreApplication::translate("ScriptApiList", "开启限时模式"),
        QCoreApplication::translate("ScriptApiList", "OpenTimeLimit(seconds)\n开启限时模式，在指定秒数后触发超时\n参数:\n  seconds (number) - 限时秒数\n示例: OpenTimeLimit(60)"));

    addApi("CloseTimeLimit()",
        QCoreApplication::translate("ScriptApiList", "关闭限时模式"),
        QCoreApplication::translate("ScriptApiList", "CloseTimeLimit()\n关闭限时模式\n参数: 无\n示例: CloseTimeLimit()"));

    addApi("HideTimerWnd()",
        QCoreApplication::translate("ScriptApiList", "隐藏计时器窗口"),
        QCoreApplication::translate("ScriptApiList", "HideTimerWnd()\n隐藏限时模式的计时器窗口\n参数: 无\n示例: HideTimerWnd()"));

    addApi("SetTimeScript(seconds, scriptFile)",
        QCoreApplication::translate("ScriptApiList", "设置定时触发脚本"),
        QCoreApplication::translate("ScriptApiList", "SetTimeScript(seconds, scriptFile)\n设置在指定秒数后自动执行脚本\n参数:\n  seconds (number) - 延迟秒数\n  scriptFile (string) - 要执行的脚本文件名\n示例: SetTimeScript(30, \"event/timer_event.txt\")"));

    QSet<QString> documentedNames;
    for (const ApiInfo& api : apis)
        documentedNames.insert(api.signature.section('(', 0, 0).trimmed().toLower());

    for (const std::string& runtimeName : ScriptConverter::runtimeApiNames())
    {
        const QString name = QString::fromStdString(runtimeName);
        if (documentedNames.contains(name))
            continue;
        addApi(name + "(...)",
            QCoreApplication::translate("ScriptApiList", "运行时脚本 API"),
            QCoreApplication::translate("ScriptApiList",
                "%1(...)\n该名称已由当前 C++ 运行时注册；参数和返回约定请结合脚本接口实现或现有脚本样本确认。")
                .arg(name));
    }

    return apis;
}
