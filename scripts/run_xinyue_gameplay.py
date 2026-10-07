"""Play Xinyue with existing player controls and isolated manual saves."""
from __future__ import annotations

import argparse
import configparser
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid

from gameplay_automation import Client, AutomationError, npc_attackable
from run_chenghe_gameplay import save_hashes
from run_jxqy2_gameplay_smoke import HOME_VARIABLES, LIFE_ITEM, MANA_ITEM, item, reject, settle
from run_jxqy2_mainline import STORY_VARIABLES, fight as native_fight, go as native_go, idle, late_records, talk, verify
from run_yycs_gameplay import inventory, load_checkpoint, reachable_trap, trap_points
import run_jxqy2_mainline as mainline

RESOURCE_ID = "XINYUE_WUHEN_3_0"
RESOURCE_DIRECTORY = "新月无痕"
SAVE_NAMESPACE = RESOURCE_ID.lower()
VARIABLES = (*HOME_VARIABLES, *STORY_VARIABLES,
             "ChangAnLaoban", "ChangAnXiaofan", "ChangAnYangQiYe", "ChangAnYaYi",
             "ChanAnMGFirstEnter", "ChangAnGuanYin", "ChangAnShangGuanGuanJia", "ChangAnYuGuiFinish",
             "ChangAnCaiSong", "ChangAnYanRuoXue", "ChangAnYanRuoXueXiaoShi", "FengXueShanZhuanFinish", "XiYuanZhao",
             "TanMenFighting", "NaDaoMiJiMusic", "CuiYanMen2CunLan", "CuiYanMen2Finish", "CuiYanMen2Fighting",
             "CuiYanMen2DiZi", "CuiYanMen2AllFinish", "HanYanFirstEnter", "HanYanCTMSZL", "HanYanCTMHouMenDiZi",
             "HanYanTWBDiZi", "TianWangPiEr", "TianWanShouMenDiZi", "DuanJiaZhuangClose", "DuanHuanShan",
             "ZhenJieJu", "Zhenbeiju", "ZMG", "nuleizhi", "kongque", "fengshen", "shouhuo", "duanjinfu")


def fight(client, target, *, skills=(0,), allow_melee_fallback=True):
    state = idle(client)
    if not state.get("cheatInvincibilityEnabled"):
        return native_fight(client, target, skills=skills)
    for slot in skills:
        if not any(row["slot"] == state["layout"]["magicQuickBegin"] + slot for row in state["magic"]):
            raise AutomationError(f"Assigned learned skill is absent: {slot}")
    result = client.act("StartCombat", generation=state["generation"], targetId=target["id"],
                        radius=20, kills=1, skills=list(skills), allowMeleeFallback=allow_melee_fallback,
                        timeoutMs=240000, timeout=245)
    if result.get("kills") != 1:
        raise AutomationError(f"Battle did not observe a normal death: {result}")
    return idle(client)


def go(client, *coordinates, **options):
    options.setdefault("running", True)
    def clear_visible_enemy(c, target):
        state = idle(c)
        point = state["player"]["position"]
        target = next((row for row in state["targets"] if row["id"] == target["id"] and npc_attackable(row)), None)
        if target is None:
            return state
        if not target.get("visibleFromPlayer"):
            nearby = [row for row in state["targets"] if npc_attackable(row) and row.get("visibleFromPlayer")
                      and abs(row["position"]["x"] - point["x"]) * 2 + abs(row["position"]["y"] - point["y"]) <= 20]
            if not nearby:
                raise AutomationError("StartCombat: target_unreachable")
            target = min(nearby, key=lambda row: abs(row["position"]["x"] - point["x"]) * 2
                         + abs(row["position"]["y"] - point["y"]))
        return fight(c, target, allow_melee_fallback=False)
    options.setdefault("combat_handler", clear_visible_enemy)
    return native_go(client, *coordinates, **options)


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def checkpoint(client, output, name):
    state = client.observe(VARIABLES)
    if state.get("resourceId") != RESOURCE_ID or not state.get("outputHealthy"):
        raise AutomationError("Wrong game or missing required trace")
    identity = json.loads((output / "run.json").read_text(encoding="utf-8"))
    state.update(cheatAssisted=identity["cheatAssisted"],
                 assistanceRecords=identity.get("assistanceRecords", []))
    write_json(output / f"{name}.json", state)
    shutil.copyfile(client.snapshot(), output / f"{name}.png")
    return state


def source_candidates(assets):
    catalog = dict(resourceId=RESOURCE_ID, inventoryType="source-candidates", fullCoverage=False,
                   sources=[], choices=[], conditions=[], terminals=[])
    seen = set()
    for root in (assets / RESOURCE_DIRECTORY, assets / "jxqy2"):
        candidates = inventory(root)
        included = {source["path"] for source in candidates["sources"]} - seen
        for collection in ("sources", "choices", "conditions", "terminals"):
            catalog[collection].extend(dict(site, sourceRoot=str(root.resolve()))
                                       for site in candidates[collection] if site["path"] in included)
        seen.update(included)
    catalog["counts"] = dict(scripts=len(catalog["sources"]), choiceSites=len(catalog["choices"]),
                           choiceOptions=sum(len(site["options"]) for site in catalog["choices"]),
                           conditionalSites=len(catalog["conditions"]), mediaAndReturnSites=len(catalog["terminals"]))
    return catalog


def opening(client, output):
    client.wait_until(lambda state: state["scene"] == "Title")
    checkpoint(client, output, "01-title")
    client.activate("new-game")
    state = settle(client, map_name="沙漠之战")
    checkpoint(client, output, "02-desert")
    client.submit("MoveTo", generation=state["generation"], x=25, y=35,
                  running=True, timeoutMs=180000)
    settle(client, map_name="主角家", timeout=240)
    checkpoint(client, output, "03-home-introduction")
    client.save_or_load(0)
    for name in ("张如梦", "南宫彩虹", "南宫彩虹", "白煞", "黑煞"):
        talk(client, name)
    verify(client, NanGongCaiHong=2, ZhangRuMeng=1, HeiSha=1, BaiSha=1)
    for position in ((16, 15), (37, 57), (28, 31)):
        talk(client, "宝箱", position)
    state = client.observe()
    if item(state, LIFE_ITEM)["quantity"] != 5 or item(state, MANA_ITEM)["quantity"] != 5:
        raise AutomationError("Home medicine reward differs from its source")
    for filename in ("goods-jian-1-桃木剑.ini", "goods-pifeng-1-白布披风.ini",
                     "goods-cloth-1-书生服.ini", "goods-huwan-2-月勾.ini"):
        client.equip(item(client.observe(), filename)["slot"])
    client.assign_magic(item(client.observe(), "player-magic-寒霜掌.ini", "magic")["slot"], 0)
    client.assign_goods(item(client.observe(), LIFE_ITEM)["slot"], 0)
    client.save_or_load(1)
    before = checkpoint(client, output, "04-home-source-slot1")
    client.move(25, 52)
    load_checkpoint(client, 1)
    after = checkpoint(client, output, "05-home-reloaded")
    for key in ("map", "variables", "inventory", "magic", "equipment"):
        if after.get(key) != before.get(key):
            raise AutomationError(f"Home reload mismatch: {key}")
    if after["player"]["position"] != before["player"]["position"]:
        raise AutomationError("Home reload position mismatch")


def home_gate(client, output):
    before = checkpoint(client, output, "home-gate-before")
    if before.get("map") != "主角家.map" or any(before["variables"].get(name) not in (None, "", "0")
            for name in ("NanGongCaiHong", "ZhangRuMeng", "HeiSha", "BaiSha")):
        raise AutomationError("Home gate needs the normal pre-conversation source")
    go(client, 15, 67, script="主角家/trap-3.txt")
    after = checkpoint(client, output, "home-gate-refused")
    if after["map"] != before["map"] or after["variables"] != before["variables"]:
        raise AutomationError("Home gate did not preserve unmet prerequisites")


def departure(client, output, resource, difficulty):
    verify(client, NanGongCaiHong=2, ZhangRuMeng=1, HeiSha=1, BaiSha=1)
    go(client, 15, 67, script="主角家/trap-3.txt")
    if difficulty == "easy":
        for count in range(4):
            go(client, 14, 87, script="主角家/trap-2.txt")
            verify(client, HomeChoice=min(count + 1, 3))
    else:
        state = client.observe()
        point = reachable_trap(resource, state["map"], 1, state["player"]["position"],
                               avoid=trap_points(resource, state["map"], 2))
        go(client, *point, script="主角家/trap-1.txt")
    state = checkpoint(client, output, f"06-departure-{difficulty}")
    if state["map"] != "主角家-狂沙镇.map" or state["player"]["levelFile"] != f"level-{difficulty}.ini":
        raise AutomationError("Native difficulty exit did not apply")
    for filename in ("player-magic-无影神针.ini", "player-magic-白虹贯日.ini", "player-magic-天意剑诀.ini"):
        client.assign_magic(item(client.observe(), filename, "magic")["slot"])
    client.assign_practice(item(client.observe(), "player-magic-天意剑诀.ini", "magic")["slot"])
    client.save_or_load(2)
    checkpoint(client, output, f"07-{difficulty}-source-slot2")


def wolves(client, output):
    if idle(client)["map"] != "主角家-狂沙镇.map":
        raise AutomationError("Wolves require the desert village source")
    talk(client, "掌柜2")
    verify(client, ksOcunzhang=1)
    talk(client, "掌柜2")
    verify(client, ksOcunzhang=1)
    client.save_or_load(3)
    checkpoint(client, output, "wolves-task-source-slot3")
    go(client, 97, 171)
    state = client.observe(("ksOcunzhang",))
    if state["variables"].get("ksOcunzhang") != "2":
        target = next(actor for actor in state["targets"] if actor["name"] == "火狼" and npc_attackable(actor))
        fight(client, target, skills=(0,))
    verify(client, ksOcunzhang=2)
    client.save_or_load(4)
    checkpoint(client, output, "wolves-reward-source-slot4")
    for point in ((102, 126), (130, 100), (133, 91)):
        go(client, *point)
    before = client.observe()
    talk(client, "掌柜2")
    verify(client, ksOcunzhang=3)
    after = client.observe()
    if after["player"]["money"] != before["player"]["money"] + 500:
        raise AutomationError("Village reward did not grant 500 taels")
    client.equip(item(after, "goods-jian-2-桃花剑.ini")["slot"])
    before = client.observe()
    talk(client, "掌柜2")
    after = client.observe()
    if after["player"]["money"] != before["player"]["money"] or after["inventory"] != before["inventory"]:
        raise AutomationError("Repeated village conversation duplicated reward")
    client.save_or_load(5)
    checkpoint(client, output, "wolves-complete-slot5")


def desert_skills(client, output):
    state = idle(client)
    verify(client, ksOcunzhang=3)
    if state["map"] != "主角家-狂沙镇.map":
        raise AutomationError("Desert skills need the normal post-wolf source")
    go(client, 78, 160, destination="沙漠迷宫.map", script="主角家-狂沙镇/地图切换2.txt")
    state = checkpoint(client, output, "desert-entry")
    owners = {}
    for name, point in (("nuleizhi", (57, 146)), ("kongque", (85, 107))):
        matches = [row for row in state["targets"] if row.get("name") == "剑客B"
                   and row["position"] == dict(x=point[0], y=point[1]) and npc_attackable(row)]
        if name == "nuleizhi":
            # The template has a second swordsman on this tile; the grant owner has 2000 life.
            matches = [row for row in matches if row["life"] == 2000]
        if len(matches) != 1:
            raise AutomationError(f"Ambiguous actual skill owner {name}: {matches}")
        owners[name] = matches[0]["id"]
    client.save_or_load(6)
    for point in ((18, 83), (29, 75), (39, 81), (49, 72), (56, 57), (66, 63),
                  (73, 79), (82, 91), (83, 103)):
        go(client, *point)
    state = idle(client)
    target = next((row for row in state["targets"] if row["id"] == owners["kongque"] and npc_attackable(row)), None)
    if target:
        fight(client, target, skills=(0,))
    verify(client, kongque=1)
    item(client.observe(), "magic-孔雀翎.ini", "magic")
    checkpoint(client, output, "desert-kongque-learned")
    for point in ((83, 120), (78, 140), (70, 155), (63, 170), (53, 180), (43, 169),
                  (36, 154), (28, 139), (25, 116), (30, 96), (38, 110), (38, 140), (46, 144)):
        go(client, *point)
    target = next((row for row in client.observe()["targets"] if row["id"] == owners["nuleizhi"] and npc_attackable(row)), None)
    if target:
        fight(client, target, skills=(0,))
    verify(client, nuleizhi=1)
    item(client.observe(), "player-magic-怒雷指.ini", "magic")
    late_records(output, "trace.jsonl", completed_scripts=("沙漠迷宫/获得武功.txt", "script/common/linglve孔雀翎.txt"))
    checkpoint(client, output, "desert-both-skills-learned")
    go(client, 60, 152)
    talk(client, "时空门", (61, 152))
    state = idle(client)
    if state["map"] != "沙漠迷宫.map" or state["player"]["position"] != dict(x=11, y=64):
        raise AutomationError("Normal maze portal did not return to the entrance")
    go(client, 11, 63, destination="主角家-狂沙镇.map", script="沙漠迷宫/地图切换.txt", combat=False)
    client.save_or_load(6)
    checkpoint(client, output, "desert-complete-slot6")


def home_extras(client, output):
    if idle(client)["map"] != "主角家.map":
        raise AutomationError("Home additions require the normal home source")
    client.save_or_load(2)
    checkpoint(client, output, "home-additions-source-slot2")
    rewards = []
    for point in ((28, 32), (28, 33), (18, 12), (38, 56), (17, 10)):
        before = client.observe()
        talk(client, "宝箱", point)
        after = client.observe()
        reward = after["player"]["money"] - before["player"]["money"]
        chest = next(row for row in after["targets"] if row["kind"] == "object"
                     and row["position"] == dict(x=point[0], y=point[1]))
        if not 1000 <= reward <= 10000:
            raise AutomationError(f"Added money chest did not grant once and clear its binding: {point}")
        reject(client, "Interact", "action_rejected", generation=after["generation"], targetId=chest["id"])
        if client.observe()["player"]["money"] != after["player"]["money"]:
            raise AutomationError("Repeated added-chest interaction granted more money")
        rewards.append(dict(position=point, reward=reward, bindingCleared=True))
        checkpoint(client, output, f"home-added-chest-{point[0]}-{point[1]}")
    write_json(output / "home-added-chest-rewards.json", rewards)
    before = client.observe()
    npc = next(row for row in before["targets"] if row["name"] == "摊贩摆卖1"
               and row["position"] == dict(x=22, y=65))
    client.interact(npc["id"])
    state = client.wait_until(lambda value: "shop" in value, timeout=120, description="Yixixuan's normal dialogue and medicine shop")
    checkpoint(client, output, "home-yixixuan-shop")
    offered = item(state, LIFE_ITEM, "shop")
    if len(state["shop"]) != 3:
        raise AutomationError("Added NPC did not open the actual dependent low-grade medicine table")
    quantity = item(state, LIFE_ITEM)["quantity"]
    money = state["player"]["money"]
    client.buy(offered["slot"])
    state = client.observe()
    if item(state, LIFE_ITEM)["quantity"] != quantity + 1 or state["player"]["money"] != money - 130:
        raise AutomationError("Added NPC medicine purchase did not use the actual 130-tael cost")
    checkpoint(client, output, "home-yixixuan-purchase")
    client.ui("Cancel")
    idle(client)
    late_records(output, "trace.jsonl", completed_scripts=("script/common/aaa.txt", "script/common/大量银子.txt"))
    client.save_or_load(3)
    before = checkpoint(client, output, "home-additions-complete-slot3")
    load_checkpoint(client, 3)
    after = checkpoint(client, output, "home-additions-reloaded")
    if after["player"]["money"] != before["player"]["money"] or after["inventory"] != before["inventory"]:
        raise AutomationError("Added home rewards or purchase changed after normal reload")
    for point in (reward["position"] for reward in rewards):
        chest = next(row for row in after["targets"] if row["kind"] == "object"
                     and row["position"] == dict(x=point[0], y=point[1]))
        reject(client, "Interact", "action_rejected", generation=after["generation"], targetId=chest["id"])
    if client.observe()["player"]["money"] != after["player"]["money"]:
        raise AutomationError("An opened added chest granted again after reload")


def town(client, output, resource):
    state = idle(client)
    if state["map"] == "主角家-狂沙镇.map":
        go(client, 41, 302, script="主角家-狂沙镇/地图切换1.txt")
        state = checkpoint(client, output, "village-to-town-first-attempt")
        if state["map"] == "主角家-狂沙镇.map" and state["variables"].get("ksOcunzhang") == "2":
            # Maze return deliberately reloads enemies. Record the actual repeated
            # wolf reward before continuing through the original player interaction.
            for point in ((97, 171), (102, 126), (130, 100), (133, 91)):
                go(client, *point)
            before = client.observe()
            talk(client, "掌柜2")
            state = verify(client, ksOcunzhang=3)
            if (state["player"]["money"] != before["player"]["money"] + 500
                    or sum(row["quantity"] for row in state["inventory"] if row["file"] == "goods-jian-2-桃花剑.ini")
                    != sum(row["quantity"] for row in before["inventory"] if row["file"] == "goods-jian-2-桃花剑.ini") + 1):
                raise AutomationError("Observed repeat wolf reward differs from its actual script")
            checkpoint(client, output, "wolf-repeat-reward-observed")
            client.save_or_load(6)
            go(client, 41, 302, destination="狂沙镇.map")
    if idle(client)["map"] != "狂沙镇.map":
        raise AutomationError("Town needs the normal village or daytime-town source")
    verify(client, KsOQieHuan=0)
    client.save_or_load(0)
    checkpoint(client, output, "town-prerequisites-slot0")
    before_money = client.observe()["player"]["money"]
    for trap, script in ((1, "地图切换.txt"), (2, "地图切换1.txt"), (3, "地图切换2.txt")):
        state = idle(client)
        point = reachable_trap(resource, state["map"], trap, state["player"]["position"])
        go(client, *point, script="狂沙镇/" + script)
        state = verify(client, KsOQieHuan=0)
        if state["map"] != "狂沙镇.map" or state["player"]["money"] != before_money:
            raise AutomationError(f"Pre-rest exit {trap} changed its map or charged money")
        checkpoint(client, output, f"town-prerest-gate-{trap}-refused")
    if client.observe(("ChaiSongTalk",))["variables"].get("ChaiSongTalk") != "1":
        go(client, 88, 185, script="狂沙镇/柴嵩交谈.txt")
    verify(client, ChaiSongTalk=1)
    client.save_or_load(1)
    checkpoint(client, output, "town-before-rest-slot1")
    talk(client, "老板2", (99, 185))
    state = verify(client, KsOQieHuan=1)
    if (state["map"] != "狂沙镇夜.map" or state["player"]["money"] != before_money
            or any(state["player"][value] != state["player"][value + "Max"] for value in ("life", "mana"))):
        raise AutomationError("Native inn rest did not recover normally or unexpectedly charged money")
    if not any(row["kind"] == "npc" and row["name"] == "大黄和尚" for row in state["targets"]):
        raise AutomationError("The night list still lost its NPCs after the original numbering gap")
    checkpoint(client, output, "town-night-rest-complete")
    for trap, script in ((1, "地图切换.txt"), (2, "地图切换1.txt")):
        state = idle(client)
        point = reachable_trap(resource, state["map"], trap, state["player"]["position"])
        go(client, *point, script="狂沙镇夜/" + script)
        if verify(client, KsOQieHuan=1)["map"] != "狂沙镇夜.map":
            raise AutomationError("Night departure bypassed the rescue prerequisite")
    talk(client, "老板2", (99, 185))
    state = verify(client, KsOQieHuan=1)
    if state["player"]["money"] != before_money or state["map"] != "狂沙镇夜.map":
        raise AutomationError("Repeated inn interaction changed money or map")
    client.save_or_load(2)
    checkpoint(client, output, "town-night-source-slot2")


def rescue(client, output, resource):
    state = idle(client)
    verify(client, ChaiSongTalk=1, KsOQieHuan=1)
    if state["map"] in ("狂沙镇夜.map", "铁门寨.map"):
        if state["map"] == "狂沙镇夜.map":
            go(client, 129, 57, destination="狂沙镇-铁门寨.map")
            go(client, 12, 10, destination="铁门寨.map")
        state = checkpoint(client, output, "iron-entrance")
        owners = [row["id"] for row in state["targets"] if row["name"] == "剑客B"
                  and row["position"] in (dict(x=64, y=30), dict(x=63, y=29))]
        if len(owners) != 2:
            raise AutomationError("The two bound Fengshen branch owners are absent")
        client.save_or_load(3)
        go(client, 24, 89, script="铁门寨/进入山寨.txt")
        state = idle(client)
        path = reachable_trap(resource, state["map"], 2, state["player"]["position"],
                              avoid=trap_points(resource, state["map"], 4), with_path=True)
        for point in path[3:-1:3]:
            go(client, *point)
        go(client, *path[-1], script="铁门寨/地图切换1.txt")
        if verify(client, TMZOQieHuan=0)["map"] != "铁门寨.map":
            raise AutomationError("Iron-village gate bypassed the hall prerequisite")
        checkpoint(client, output, "iron-prehall-exit-refused")
        go(client, 22, 49, script="铁门寨/进入山寨大厅.txt")
        verify(client, TMZOQieHuan=1)
        for point in ((53, 37), (55, 28), (62, 30)):
            go(client, *point)
        for owner in owners:
            target = next((row for row in client.observe()["targets"] if row["id"] == owner and npc_attackable(row)), None)
            if target:
                fight(client, target)
        verify(client, fengshen=1)
        item(client.observe(), "封神十二剑.ini", "magic")
        late_records(output, "trace.jsonl", completed_scripts=("script/common/学会封神十二剑.txt", "script/common/学会封神十二剑死亡.txt"))
        client.save_or_load(4)
        checkpoint(client, output, "iron-fengshen-complete-slot4")
        go(client, 65, 13, destination="铁门寨-百花谷.map")
        verify(client, TmzBhgOQieHuan=1)
        client.save_or_load(5)
    elif state["map"] != "铁门寨-百花谷.map":
        raise AutomationError("Rescue needs the night or normal cave source")
    go(client, 54, 68, destination="百花谷.map", timeout=300)
    verify(client, BhgOfirstenter=1)
    go(client, 15, 21, script="百花谷/地图陷阱3.txt")
    go(client, 35, 10, destination="百花谷小屋.map")
    verify(client, BaiHuaGuOQieHuan=1)
    client.save_or_load(6)
    checkpoint(client, output, "rescue-before-release-slot6")
    go(client, 8, 25, destination="百花谷.map", script="百花谷小屋/交谈.txt", timeout=300)
    verify(client, BHGOTalkToYRX=1, BaiHuaGuOQieHuan=2, BHGOFire=1)
    checkpoint(client, output, "rescue-released-and-signalled")
    go(client, 35, 10, destination="百花谷小屋.map")
    talk(client, "燕若雪")
    state = verify(client, KsOQieHuan=2)
    if state["map"] != "狂沙镇.map" or state["player"]["canJump"]:
        raise AutomationError("Rescued companion did not return to town with its movement restriction")
    client.save_or_load(4)
    checkpoint(client, output, "rescue-returned-before-inn-slot4")
    go(client, 97, 187, script="狂沙镇/投店.txt", timeout=300)
    verify(client, KsOQieHuan=3)
    client.save_or_load(5)
    checkpoint(client, output, "rescue-returned-to-inn-slot5")


def longmen(client, output, resource):
    verify(client, KsOQieHuan=3)
    if idle(client)["map"] == "狂沙镇.map":
        go(client, 147, 258, destination="狂沙镇-龙门客栈.map")
        go(client, 19, 25, script="狂沙镇-龙门客栈/对话.txt")
        go(client, 27, 45, destination="龙门客栈.map")
    before = checkpoint(client, output, "longmen-added-source-slot0")
    if before["map"] != "龙门客栈.map":
        raise AutomationError("Longmen additions require normal inn arrival")
    stones = [row for row in before["targets"] if row["name"] == "石头" and npc_attackable(row)]
    if len(stones) != 8:
        raise AutomationError("The actual eight added mechanism stones are absent")
    client.save_or_load(0)
    if client.observe(("LMKZOQieHuan",))["variables"].get("LMKZOQieHuan") != "1":
        go(client, 25, 57, script="龙门客栈/柴嵩.txt")
    verify(client, LMKZOQieHuan=1)
    state = idle(client)
    path = reachable_trap(resource, state["map"], None, state["player"]["position"],
                          destination=(35, 72), avoid=trap_points(resource, state["map"], 5), with_path=True)
    for point in [*path[3:-1:3], path[-1]]:
        go(client, *point)
    for repeat in range(2):
        before = client.observe()
        merchants = [row for row in before["targets"] if row["name"] == "高级蒙面人2"
                     and row.get("interactive")]
        if len(merchants) != 1:
            raise AutomationError(f"Cannot identify Longmen's moving merchant: {merchants}")
        merchant = merchants[0]
        client.interact(merchant["id"])
        client.wait_until(lambda value: "shop" in value, timeout=120, description="mechanism-array merchant")
        checkpoint(client, output, f"longmen-merchant-shop-{repeat}")
        if not repeat:
            sword = item(client.observe(), "goods-jian-1-桃木剑.ini")
            client.sell(sword["slot"])
            after = client.observe()
            remaining = sum(row["quantity"] for row in after["inventory"] if row["file"] == sword["file"])
            if (remaining != sword["quantity"] - 1
                    or after["player"]["money"] != before["player"]["money"] + 210):
                raise AutomationError("Added merchant sale did not use the dependent 420/2 price")
            checkpoint(client, output, "longmen-merchant-sale")
        client.ui("Cancel")
        after = verify(client, shouhuo=1)
        if repeat and (after["inventory"] != before["inventory"]
                       or after["player"]["money"] != before["player"]["money"]):
            raise AutomationError("Repeated added-merchant dialogue changed items or money")
    client.save_or_load(1)
    checkpoint(client, output, "longmen-before-stones-slot1")
    for point in ((45, 30), (48, 25)):
        go(client, *point)
    for stone in stones:
        target = next((row for row in client.observe()["targets"]
                       if row["id"] == stone["id"] and npc_attackable(row)), None)
        if target:
            fight(client, target)
    state = checkpoint(client, output, "longmen-stones-defeated")
    if any(row["id"] in {stone["id"] for stone in stones} and npc_attackable(row) for row in state["targets"]):
        raise AutomationError("An added mechanism stone survived")
    body = next(row for row in state["targets"] if row["kind"] == "object"
                and row["name"] == "获得逍遥刀法" and row["position"] == dict(x=48, y=11))
    before_quantity = sum(row["quantity"] for row in state["inventory"] if row["file"] == "book-霹雳刀法.ini")
    client.save_or_load(2)
    go(client, 48, 13)
    client.interact(body["id"])
    state = idle(client)
    book = item(state, "book-霹雳刀法.ini")
    if book["quantity"] != before_quantity + 1:
        raise AutomationError("The bound stone body did not grant its original Pili book")
    reject(client, "Interact", "action_rejected", generation=state["generation"], targetId=body["id"])
    if item(client.observe(), book["file"])["quantity"] != book["quantity"]:
        raise AutomationError("The stone body granted its book twice")
    checkpoint(client, output, "longmen-pili-book-obtained")
    client.act("UseItem", generation=state["generation"], slot=book["slot"])
    idle(client)
    item(client.observe(), "player-magic-霹雳刀法.ini", "magic")
    late_records(output, "trace.jsonl", completed_scripts=("script/common/龙门七星.txt",
                 "script/common/捡到霹雳书.txt", "script/common/习得霹雳刀法.txt"))
    client.save_or_load(3)
    checkpoint(client, output, "longmen-pili-learned-slot3")


def changan_entry(client, output):
    verify(client, LMKZOQieHuan=1)
    if idle(client)["map"] != "龙门客栈.map":
        raise AutomationError("Changan entry requires the normal completed inn source")
    go(client, 54, 98, destination="龙门客栈-长安.map")
    go(client, 8, 33, script="龙门客栈-长安/聊天.txt")
    go(client, 14, 107, destination="长安.map")
    client.save_or_load(4)
    checkpoint(client, output, "changan-arrival-source-slot4")


def changan_story(client, output):
    # Reuse the required seal and inn route with Xinyue's battle configuration
    # and evidence records; this process runs one route at a time.
    previous_go, previous_checkpoint, previous_restock = mainline.go, mainline.checkpoint, mainline.restock
    mainline.go, mainline.checkpoint = go, checkpoint
    if client.observe().get("cheatInvincibilityEnabled"):
        mainline.restock = lambda *_args, **_options: None
    try:
        mainline.changan_to_fengxue(client, output)
    finally:
        mainline.go, mainline.checkpoint = previous_go, previous_checkpoint
        mainline.restock = previous_restock


def fengxue(client, output, resource):
    state = idle(client)
    if state["map"] != "风雪山庄.map":
        raise AutomationError("Fengxue requires its normal arrival source")
    client.save_or_load(0)
    checkpoint(client, output, "fengxue-entry-slot0")
    if client.observe(("FengXueShanZhuanFinish",))["variables"]["FengXueShanZhuanFinish"] != "1":
        if not any(target["name"] == "守囚室家丁" for target in state["targets"]):
            if not any(target["name"] == "家丁" and target.get("hostile") for target in state["targets"]):
                talk(client, "家丁", (67, 49))
            go(client, 49, 40, script="风雪山庄/地图陷阱2.txt")
        talk(client, "守囚室家丁", (48, 177))
    verify(client, FengXueShanZhuanFinish=1, ChangAnYanRuoXueXiaoShi=2)
    if client.observe(("XiYuanZhao",))["variables"]["XiYuanZhao"] != "1":
        talk(client, "赵升权", (19, 126))
    state = verify(client, XiYuanZhao=1)
    item(state, "player-magic-风雪狂刀.ini", "magic")
    talk(client, "柴嵩", (88, 95))
    talk(client, "赵无双", (89, 93))
    client.save_or_load(1)
    checkpoint(client, output, "fengxue-release-and-skill-slot1")
    go(client, 93, 13, destination="长安西郊.map")
    go(client, 19, 9, destination="长安.map")
    state = idle(client)
    # The local south exit only requires Fengxue's release. Follow a normal
    # street path outside the original optional Shangguan courtyard scene.
    path = reachable_trap(resource, state["map"], 4, state["player"]["position"], with_path=True,
                          avoid=trap_points(resource, state["map"], 7))
    write_json(output / "changan-to-bieli-player-path.json", dict(path=path, avoidedTrap=7))
    for point in path[4:-1:8]:
        go(client, *point)
    go(client, *path[-1], destination="别离村.map")
    client.save_or_load(2)
    checkpoint(client, output, "bieli-arrival-slot2")


def bieli_additions(client, output):
    before = checkpoint(client, output, "bieli-added-source-slot0")
    if before["map"] != "别离村.map":
        raise AutomationError("Bieli additions require the normal village arrival")
    client.save_or_load(0)
    silver_quantity = sum(row["quantity"] for row in before["inventory"] if row["file"] == "银饰.ini")
    silver_owner = next(row for row in before["targets"] if row["name"] == "天忍教双斧教众1"
                        and row["position"] == dict(x=27, y=162))
    for point in ((40, 54), (51, 60), (50, 86), (53, 93)):
        go(client, *point)
    before_shop = client.observe()
    merchant = next(row for row in before_shop["targets"] if row["name"] == "掌柜3"
                    and row["position"] == dict(x=53, y=92))
    client.interact(merchant["id"])
    client.wait_until(lambda value: "shop" in value, timeout=120, description="Bieli added merchant")
    checkpoint(client, output, "bieli-added-merchant-shop")
    client.ui("Cancel")
    after_shop = idle(client)
    if (after_shop["inventory"] != before_shop["inventory"]
            or after_shop["player"]["money"] != before_shop["player"]["money"]):
        raise AutomationError("Cancelling Bieli's added sale changed items or money")
    client.save_or_load(1)
    checkpoint(client, output, "bieli-before-silver-enemy-slot1")
    for point in ((36, 151), (30, 154), (27, 159)):
        go(client, *point)
    target = next((row for row in client.observe()["targets"]
                   if row["id"] == silver_owner["id"] and npc_attackable(row)), None)
    if target:
        fight(client, target)
    state = idle(client)
    if item(state, "银饰.ini")["quantity"] != silver_quantity + 1:
        raise AutomationError("The bound Bieli enemy did not grant exactly one silver accessory")
    late_records(output, "trace.jsonl", completed_scripts=("script/common/别离村子银饰.txt",))
    client.save_or_load(2)
    checkpoint(client, output, "bieli-silver-obtained-slot2")
    go(client, 20, 163, destination="别离村-翠烟门.map")
    client.save_or_load(3)
    checkpoint(client, output, "bieli-cuiyan-before-duanjin-slot3")
    go(client, 38, 84)
    targets = [row for row in client.observe()["targets"] if row["name"] == "孟廷威" and npc_attackable(row)]
    for target in targets:
        fight(client, target)
    state = verify(client, duanjinfu=1)
    item(state, "player-magic-断金斧.ini", "magic")
    late_records(output, "trace.jsonl", completed_scripts=("script/common/学会断金斧.txt",))
    client.save_or_load(4)
    checkpoint(client, output, "bieli-duanjin-learned-slot4")
    go(client, 3, 155, destination="翠烟门.map")
    talk(client, "春兰", (127, 95))
    state = verify(client, CuiYanMen2CunLan=1)
    item(state, "goods-sj-7-唐门宝箱.ini")
    talk(client, "秋依水", (128, 96))
    checkpoint(client, output, "cuiyan-delivery-complete")
    go(client, 146, 61, destination="别离村-翠烟门.map")
    go(client, 67, 24, destination="别离村.map")
    go(client, 82, 95, destination="别离村-唐门.map")
    client.save_or_load(5)
    checkpoint(client, output, "tangmen-approach-slot5")


def tangmen(client, output, resource, *, lose=False):
    state = idle(client)
    phase = client.observe(("TanMenFighting", "CuiYanMen2Finish"))["variables"]
    if state["map"] == "别离村-唐门.map" and phase["CuiYanMen2Finish"] != "1":
        client.save_or_load(0)
        checkpoint(client, output, "tangmen-before-entry-slot0")
        for point in ((5, 40), (5, 32), (4, 23), (4, 15), (7, 12)):
            go(client, *point)
        go(client, 12, 12, destination="唐门.map")
        state = verify(client, TanMenFighting=1)
    if state["map"] not in ("唐门.map", "别离村-唐门.map"):
        raise AutomationError("Tangmen requires its normal approach or capture scene")
    phase = client.observe(("TanMenFighting", "CuiYanMen2Finish"))["variables"]
    if state["map"] == "唐门.map" and (phase["TanMenFighting"] != "0" or phase["CuiYanMen2Finish"] != "1"):
        client.save_or_load(1)
        checkpoint(client, output, "tangmen-before-first-boss-slot1")
        bosses = [row for row in state["targets"] if row["name"] == "唐萧" and npc_attackable(row)
                  and row["position"] != dict(x=91, y=191)]
        if bosses:
            if len(bosses) != 1:
                raise AutomationError("Cannot identify Tangmen's bound initial boss")
            fight(client, bosses[0])
        late_records(output, "trace.jsonl", completed_scripts=("script/map/唐门/唐萧死亡.txt",))
        client.save_or_load(2)
        checkpoint(client, output, "tangmen-after-first-boss-slot2")
        if lose:
            assist_battle(client, output, 60)
            go(client, 92, 193, combat=False)
            state = idle(client)
            target = next(row for row in state["targets"] if row["name"] == "唐离" and npc_attackable(row))
            client.submit("StartCombat", generation=state["generation"], targetId=target["id"],
                          radius=20, kills=1, skills=[0], allowMeleeFallback=False, timeoutMs=180000)
            client.wait_until(lambda value: value["scene"] == "Title", timeout=185,
                              description="Tang Li's bound death returns to title")
            records = late_records(output, "trace.jsonl")
            if not any(row.get("eventType") == "script.start"
                       and row.get("virtualPath") == "script/map/唐门/唐离死亡.txt" for row in records):
                raise AutomationError("Tangmen title result lacks the bound Tang Li death script")
            checkpoint(client, output, "tangmen-tangli-death-title")
            return
        if not client.observe().get("cheatInvincibilityEnabled"):
            assist_battle(client, output, client.observe()["player"]["level"])
        go(client, 92, 193, combat=False, stop_when=lambda value: any(
            row["name"] == "唐离" and row.get("visibleFromPlayer")
            and abs(row["position"]["x"] - value["player"]["position"]["x"]) * 2
                + abs(row["position"]["y"] - value["player"]["position"]["y"]) <= 8
            for row in value["targets"]))
        assist_battle(client, output, client.observe()["player"]["level"], invincible=False)
        client.wait_until(lambda value: value.get("worldInput")
                          and value.get("variables", {}).get("TanMenFighting") == "0"
                          and value["variables"].get("CuiYanMen2Finish") == "1",
                          variables=("TanMenFighting", "CuiYanMen2Finish"), timeout=600,
                          description="normal Tangmen scripted capture")
        late_records(output, "trace.jsonl", completed_scripts=("script/map/唐门/主角被抓.txt",))
        checkpoint(client, output, "tangmen-capture-complete")
        assist_battle(client, output, client.observe()["player"]["level"])
    if not client.observe().get("cheatInvincibilityEnabled"):
        assist_battle(client, output, client.observe()["player"]["level"])
    state = verify(client, TanMenFighting=0, CuiYanMen2Finish=1)
    if state["player"]["life"] == 0:
        medicine = item(state, LIFE_ITEM)
        client.act("UseItem", generation=state["generation"], slot=medicine["slot"])
        state = idle(client)
        if state["player"]["life"] <= 0 or item(state, LIFE_ITEM)["quantity"] != medicine["quantity"] - 1:
            raise AutomationError("Tangmen recovery did not consume one medicine and restore life")
        checkpoint(client, output, "tangmen-medicine-recovery")
    if state["map"] == "唐门.map":
        if client.observe(("NaDaoMiJiMusic",))["variables"]["NaDaoMiJiMusic"] != "1":
            talk(client, "唐影", (109, 171))
        verify(client, NaDaoMiJiMusic=1)
        item(client.observe(), "book-唐门秘笈.ini")
        talk(client, "唐离", (82, 123))
        go(client, 47, 296, destination="别离村-唐门.map")
    state = idle(client)
    if not any(row["file"] == "player-magic-漫天花雨手法.ini" for row in state["magic"]):
        client.act("UseItem", generation=state["generation"], slot=item(state, "book-唐门秘笈.ini")["slot"])
        state = idle(client)
    item(state, "player-magic-漫天花雨手法.ini", "magic")
    path = reachable_trap(resource, state["map"], 1, state["player"]["position"], with_path=True)
    for point in path[3:-1:4]:
        go(client, *point)
    go(client, *path[-1], destination="别离村.map")
    go(client, 20, 163, destination="别离村-翠烟门.map")
    client.save_or_load(2)
    checkpoint(client, output, "tangmen-capture-and-book-complete-slot2")


def assist_battle(client, output, level, *, invincible=True):
    before = checkpoint(client, output, "native-assistance-before")
    client.open_menu("System")
    client.activate("options")
    client.activate("cheat-settings")
    if not client.observe().get("cheatModeEnabled"):
        client.activate("cheat-mode")
    if client.observe().get("cheatInvincibilityEnabled") != invincible:
        client.activate("invincibility")
    while client.observe()["player"]["level"] < level:
        previous = client.observe()["player"]["level"]
        client.activate("increase-player-level")
        if client.observe()["player"]["level"] <= previous:
            raise AutomationError("Native level assistance made no progress")
    for _ in range(5):
        if client.observe().get("worldInput"):
            break
        client.ui("Cancel")
    after = checkpoint(client, output, "native-assistance-after")
    if (not after.get("worldInput") or after.get("cheatInvincibilityEnabled") != invincible
            or after["variables"] != before["variables"] or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Native assistance changed observed story variables/money or failed")
    proof = dict(status="passed", source="native-options-menu", beforePlayer=before["player"],
                 afterPlayer=after["player"], beforeInventory=before["inventory"], afterInventory=after["inventory"],
                 requestedLevel=level, invincibility=invincible, plotVariablesUnchanged=True)
    write_json(output / f"native-assistance-proof-{int(time.time())}.json", proof)
    identity = json.loads((output / "run.json").read_text(encoding="utf-8"))
    identity["assistanceRecords"].append(dict(source="native-options-menu", requestedLevel=level, invincibility=invincible,
                                             startedAtMap=before["map"], beforeLevel=before["player"]["level"]))
    write_json(output / "run.json", identity)


def cuiyan_second(client, output):
    def route_go(c, *coordinates, **options):
        if options.get("destination") == "翠烟门.map" and c.observe()["map"] == "别离村-翠烟门.map":
            options["combat"] = True
        return go(c, *coordinates, **options)

    previous_go, previous_checkpoint, previous_fight = mainline.go, mainline.checkpoint, mainline.fight
    mainline.go, mainline.checkpoint = route_go, checkpoint
    mainline.fight = lambda c, target, skills=None: fight(c, target, skills=skills or (0,))
    try:
        mainline.cuiyan_second_visit(client, output)
    finally:
        mainline.go, mainline.checkpoint, mainline.fight = previous_go, previous_checkpoint, previous_fight
    checkpoint(client, output, "hanyang-arrival-slot2")


def hanyang_tianwang(client, output):
    state = idle(client)
    if state["map"] == "段家庄.map" and client.observe(("DuanHuanShan",))["variables"]["DuanHuanShan"] == "1":
        for point in ((43, 102), (42, 131), (49, 148), (64, 148), (73, 137),
                      (75, 110), (76, 82), (78, 60), (84, 42)):
            go(client, *point, combat=False)
        go(client, 92, 23, destination="汉阳-段家庄.map", combat=False)
        go(client, 29, 14, destination="汉阳.map", combat=False)
        talk(client, "史忠良", (89, 84))
        state = verify(client, DuanHuanShan=0, HanYanCTMSZL=3)
        item(state, "player-magic-大梦心法.ini", "magic")
        client.save_or_load(2)
        checkpoint(client, output, "hanyang-tianwang-and-duan-complete-slot2")
        return

    def route_fight(c, target, skills=None):
        if c.observe()["map"] != "段家庄.map":
            return fight(c, target, skills=skills or (0,))
        filename = "player-magic-依风剑法.ini"
        if item(c.observe(), filename, "magic")["level"] < 10:
            assist_magic(c, output, filename)
        else:
            c.assign_magic(item(c.observe(), filename, "magic")["slot"], 0)
        go(c, 58, 94, combat=False)
        target = next(row for row in c.observe()["targets"] if row["id"] == target["id"])
        if not target.get("visibleFromPlayer"):
            for point in ((43, 102), (47, 102)):
                go(c, *point, combat=False)
            target = next(row for row in c.observe()["targets"] if row["id"] == target["id"])
        if not target.get("visibleFromPlayer"):
            raise AutomationError("Duan's bound opponent remains behind an obstacle after the normal approach")
        fight(c, target, skills=(0,), allow_melee_fallback=False)
        client.save_or_load(3)
        checkpoint(c, output, "duan-primary-defeated-source-slot3")
        go(c, 53, 86, combat=False)
        for target in [row for row in c.observe()["targets"] if row["name"] == "段环山" and npc_attackable(row)]:
            current = next((row for row in c.observe()["targets"] if row["id"] == target["id"] and npc_attackable(row)), None)
            if current:
                fight(c, current, skills=(0,), allow_melee_fallback=False)
        client.save_or_load(4)
        checkpoint(c, output, "duan-all-added-defeated-slot4")
        return idle(c)

    if state["map"] == "段家庄.map":
        remaining = [row for row in state["targets"] if row["name"] == "段环山" and npc_attackable(row)]
        if not remaining:
            raise AutomationError("Duan's unfinished battle has no surviving bound opponents")
        route_fight(client, remaining[0])
        verify(client, DuanHuanShan=1)
        late_records(output, "trace.jsonl", completed_scripts=("script/map/段家庄/段环山死.txt",
                     "script/common/段家庄天气.txt"))
        return hanyang_tianwang(client, output)

    def route_talk(c, name, position=None):
        state = c.observe()
        if (name == "苹儿" and position == (57, 77) and state["map"] == "天王岛.map"
                and not any(row["file"] == "player-magic-逍遥刀法.ini" for row in state["magic"])):
            client.save_or_load(0)
            checkpoint(c, output, "tianwang-xiaoyao-before-slot0")
            go(c, 27, 140)
            targets = [row for row in c.observe()["targets"] if row["name"] == "杨瑛" and npc_attackable(row)
                       and row.get("hostile")]
            if len(targets) == 1:
                fight(c, targets[0])
            elif targets:
                raise AutomationError(f"Ambiguous added Yang Ying opponent: {targets}")
            state = idle(c)
            item(state, "player-magic-逍遥刀法.ini", "magic")
            late_records(output, "trace.jsonl", completed_scripts=("script/common/天王岛逍遥刀法.txt",))
            client.save_or_load(1)
            checkpoint(c, output, "tianwang-xiaoyao-learned-slot1")
        return previous_talk(c, name, position)

    previous_go, previous_checkpoint, previous_fight = mainline.go, mainline.checkpoint, mainline.fight
    previous_talk, previous_restock, previous_meditate = mainline.talk, mainline.restock, mainline.meditate
    mainline.go, mainline.checkpoint, mainline.talk = go, checkpoint, route_talk
    mainline.fight = route_fight
    if client.observe().get("cheatInvincibilityEnabled"):
        mainline.restock = lambda *_args, **_options: None
        mainline.meditate = lambda *_args, **_options: None
    try:
        mainline.hanyang_tianwang(client, output)
    finally:
        mainline.go, mainline.checkpoint, mainline.fight = previous_go, previous_checkpoint, previous_fight
        mainline.talk, mainline.restock, mainline.meditate = previous_talk, previous_restock, previous_meditate
    checkpoint(client, output, "hanyang-tianwang-and-duan-complete-slot2")


def tianwang_wood(client, output):
    state = verify(client, TianWangPiEr=4)
    if state["map"] == "汉阳.map":
        talk(client, "天王帮弟子", (41, 37))
    elif state["map"] != "天王岛.map":
        raise AutomationError("Added wood opponent requires the completed island quest")
    mainline.late_jump(client, (21, 115), (29, 115))
    talk(client, "梯子2", (36, 112))
    before = checkpoint(client, output, "tianwang-added-wood-before-slot0")
    targets = [row for row in before["targets"] if row["name"] == "木人2" and row.get("life") == 12345]
    if before["map"] != "天王岛-地下迷宫.map" or len(targets) != 1:
        raise AutomationError(f"Cannot identify the bound added wood opponent: {targets}")
    client.save_or_load(0)
    go(client, 14, 71, combat_handler=lambda c, target: fight(c, target, allow_melee_fallback=False))
    target = next((row for row in client.observe()["targets"]
                   if row["id"] == targets[0]["id"] and npc_attackable(row)), None)
    if target:
        fight(client, target, allow_melee_fallback=False)
    after = checkpoint(client, output, "tianwang-added-wood-defeated")
    late_records(output, "trace.jsonl", completed_scripts=("script/common/新建 文本文档.txt",))
    quantity_change = sum(row["quantity"] for row in after["inventory"]) - sum(row["quantity"] for row in before["inventory"])
    if after["player"]["money"] != before["player"]["money"] + 10000 or quantity_change not in (0, 1):
        raise AutomationError("Added wood opponent reward differs from the bound original script")
    write_json(output / "tianwang-added-wood-reward-proof.json", dict(status="passed", target=targets[0],
               beforeMoney=before["player"]["money"], afterMoney=after["player"]["money"],
               randomGoodsQuantityChange=quantity_change, beforeInventory=before["inventory"],
               afterInventory=after["inventory"], missingTableEntryStillUnconfirmed=True))
    client.save_or_load(1)
    checkpoint(client, output, "tianwang-added-wood-complete-slot1")


def to_zhongdu(client, output):
    state = verify(client, HanYanCTMSZL=3)
    if state["map"] == "汉阳.map":
        go(client, 79, 61, destination="汉阳-金兵营寨.map", combat=False)
    if idle(client)["map"] == "汉阳-金兵营寨.map":
        go(client, 32, 20, destination="金兵营寨.map", combat=False)
    if idle(client)["map"] == "金兵营寨.map":
        client.save_or_load(0)
        before = checkpoint(client, output, "camp-mask-before-slot0")
        go(client, 63, 98, combat_handler=lambda c, target: fight(c, target, allow_melee_fallback=False))
        state = idle(client)
        box = next(row for row in state["targets"] if row["kind"] == "object" and row["position"] == dict(x=63, y=96))
        quantity = sum(row["quantity"] for row in state["inventory"] if row["file"] == "异兽面具.ini")
        client.interact(box["id"])
        after = idle(client)
        mask = item(after, "异兽面具.ini")
        if mask["quantity"] != quantity + 1:
            raise AutomationError("The bound camp chest did not grant its mask")
        reject(client, "Interact", "action_rejected", generation=after["generation"], targetId=box["id"])
        client.equip(mask["slot"])
        equipped = idle(client)
        if equipped["player"]["lifeMax"] != after["player"]["lifeMax"] + 90:
            raise AutomationError("The added mask did not apply its defined life maximum")
        late_records(output, "trace.jsonl", completed_scripts=("script/common/金兵大营异兽面容.txt",))
        client.save_or_load(1)
        checkpoint(client, output, "camp-mask-obtained-and-equipped-slot1")
        go(client, 97, 6, destination="汉阳-中都1.map",
           combat_handler=lambda c, target: fight(c, target, allow_melee_fallback=False))
    if idle(client)["map"] == "汉阳-中都1.map":
        if any(row["name"] == "老人" and row.get("interactive") for row in client.observe()["targets"]):
            talk(client, "老人", (4, 12))
        mainline.late_jump(client, (10, 47), (18, 47))
        go(client, 24, 37, destination="汉阳-中都2.map", combat=False)
    go(client, 16, 24, destination="中都.map", combat=False)
    client.save_or_load(2)
    checkpoint(client, output, "zhongdu-arrival-slot2")


def zhongdu_shop(client, output, *, purchase=False):
    for point in ((84, 103), (97, 223), (70, 191)):
        go(client, *point, combat=False)
    state = idle(client)
    seller = next(row for row in state["targets"] if row["name"] == "剑客A" and row.get("interactive"))
    client.interact(seller["id"])
    state = client.wait_until(lambda value: "shop" in value, timeout=120, description="bound Xinyue special shop")
    checkpoint(client, output, "zhongdu-special-shop")
    if len(state["shop"]) != 29:
        raise AutomationError("Special shop must expose its 29 existing goods definitions")
    books = (("凤求凰宝典.ini", "player-magie-凤求凰.ini", 12000),
             ("凤翼天翔宝典.ini", "player-magie-凤凰展翅.ini", 15000))
    purchases = []
    if purchase:
        for filename, _magic, cost in books:
            before = client.observe()
            quantity = sum(row["quantity"] for row in before["inventory"] if row["file"] == filename)
            offered = item(before, filename, "shop")
            client.buy(offered["slot"])
            after = client.observe()
            if (item(after, filename)["quantity"] != quantity + 1
                    or after["player"]["money"] != before["player"]["money"] - cost):
                raise AutomationError("Special book purchase did not use the defined quantity and price")
            purchases.append(dict(file=filename, moneyBefore=before["player"]["money"],
                                  moneyAfter=after["player"]["money"], quantityBefore=quantity,
                                  quantityAfter=quantity + 1, cost=cost))
    else:
        if state["player"]["money"] >= books[0][2]:
            raise AutomationError("Insufficient-money check requires the unchanged low-money source")
        client.buy(item(state, books[0][0], "shop")["slot"])
        after = client.observe()
        if after["inventory"] != state["inventory"] or after["player"]["money"] != state["player"]["money"]:
            raise AutomationError("Unaffordable special book changed goods or money")
        write_json(output / "special-shop-insufficient-money-proof.json", dict(status="passed",
                   money=state["player"]["money"], cost=books[0][2], inventoryUnchanged=True, moneyUnchanged=True))
    # The bound script runs BuyGoods followed by SellGoods; close both normal panes.
    client.ui("Cancel")
    state = client.wait_until(lambda value: "shop" in value or value["worldInput"], timeout=10)
    if "shop" in state:
        before_cancel = client.observe()
        client.ui("Cancel")
        after = idle(client)
        if after["inventory"] != before_cancel["inventory"] or after["player"]["money"] != before_cancel["player"]["money"]:
            raise AutomationError("Cancelling the following sale changed goods or money")
    idle(client)
    late_records(output, "trace.jsonl", completed_scripts=("script/common/中都小仙女售货.txt",))
    if purchase:
        for filename, magic, _cost in books:
            state = idle(client)
            client.act("UseItem", generation=state["generation"], slot=item(state, filename)["slot"])
            item(idle(client), magic, "magic")
        write_json(output / "special-shop-purchase-proof.json", dict(status="passed", purchases=purchases))
        client.save_or_load(4)
        checkpoint(client, output, "zhongdu-special-books-learned-slot4")


def zhongdu_additions(client, output):
    if idle(client)["player"]["position"] == dict(x=25, y=317):
        go(client, 97, 223, script="中都/地图陷阱1.txt", combat=False)
    for point in ((97, 223), (84, 103), (75, 20), (69, 22)):
        go(client, *point, combat=False)
    client.save_or_load(0)
    before = checkpoint(client, output, "zhongdu-added-sword-before-slot0")
    quantity = sum(row["quantity"] for row in before["inventory"] if row["file"] == "剑王.ini")
    talk(client, "宝箱", (69, 20))
    after = idle(client)
    sword = item(after, "剑王.ini")
    if sword["quantity"] != quantity + 1 or after["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Added sword chest did not grant exactly one defined sword")
    box = next(row for row in after["targets"] if row["kind"] == "object" and row["position"] == dict(x=69, y=20))
    reject(client, "Interact", "action_rejected", generation=after["generation"], targetId=box["id"])
    late_records(output, "trace.jsonl", completed_scripts=("script/common/中都剑王.txt",))
    client.save_or_load(1)
    checkpoint(client, output, "zhongdu-added-sword-after-slot1")
    zhongdu_shop(client, output)
    client.save_or_load(2)
    checkpoint(client, output, "zhongdu-additions-and-unaffordable-shop-slot2")


def zhongdu_shaolin(client, output):
    confusion, phoenix = "player-magie-凤求凰.ini", "player-magie-凤凰展翅.ini"
    for filename in (confusion, phoenix):
        if item(client.observe(), filename, "magic")["level"] < 10:
            assist_magic(client, output, filename)
    client.save_or_load(5)
    checkpoint(client, output, "zhongdu-learned-magic-ready-slot5")
    prior = output / "zhongdu-underground-added-opponents-before-slot0.json"
    added_opponents = ([row["id"] for row in json.loads(prior.read_text(encoding="utf-8"))["targets"]
                        if row["name"] == "段环山" and npc_attackable(row)]
                       if client.observe()["map"] == "中都地下迷宫.map" and prior.exists() else [])

    def route_fight(c, target, skills=None):
        target = next((row for row in c.observe()["targets"] if row["id"] == target["id"]
                       and npc_attackable(row)), None)
        if target is None:
            return idle(c)
        filename = confusion
        c.assign_magic(item(c.observe(), filename, "magic")["slot"], 0)
        before = checkpoint(c, output, f"zhongdu-combat-before-{target['id']}")
        if target["name"] == "玄慈":
            c.save_or_load(3)
        started = time.monotonic()
        after = fight(c, target, allow_melee_fallback=False)
        write_json(output / f"zhongdu-combat-proof-{target['id']}.json", dict(status="passed",
                   target=target, magic=filename, magicLevel=10, allowMeleeFallback=False,
                   elapsedSeconds=time.monotonic() - started, beforePlayer=before["player"], afterPlayer=after["player"]))
        checkpoint(c, output, f"zhongdu-combat-after-{target['id']}")
        return after

    def route_go(c, *coordinates, **options):
        if options.get("destination") == "中都.map" and c.observe()["map"] == "中都地下迷宫.map":
            state = c.observe()
            entrance = json.loads((output / "zhongdu-underground-added-opponents-before-slot0.json").read_text(encoding="utf-8"))
            if state["generation"] != entrance["generation"]:
                raise AutomationError("Underground actor IDs changed before verifying the added opponents")
            survivors = [row for row in state["targets"] if row["id"] in added_opponents and npc_attackable(row)]
            write_json(output / "zhongdu-added-opponents-proof.json", dict(status="passed" if not survivors else "incomplete",
                       addedOpponentIds=added_opponents, survivors=survivors, sameGeneration=True))
            if survivors:
                raise AutomationError("Some added underground opponents still require reachable close combat")
            checkpoint(c, output, "zhongdu-added-opponents-defeated")
        if c.observe()["map"] == "中都地下迷宫.map":
            options["combat"] = True
        options.setdefault("combat_handler", lambda c, target: route_fight(c, target))
        state = go(c, *coordinates, **options)
        if options.get("destination") == "中都地下迷宫.map":
            added_opponents.extend(row["id"] for row in state["targets"] if row["name"] == "段环山" and npc_attackable(row))
            if len(added_opponents) != 9:
                raise AutomationError("Underground source does not contain the nine bound added opponents")
            c.save_or_load(0)
            checkpoint(c, output, "zhongdu-underground-added-opponents-before-slot0")
        return state

    def route_talk(c, name, position=None):
        if name == "女子1":
            before = checkpoint(c, output, "zhongdu-ouyang-rewards-before")
            corpse = c.wait_until(lambda value: any(row["name"] == "欧杨桐尸体" and row["kind"] == "object"
                                         for row in value["targets"]), timeout=15)
            target = next(row for row in corpse["targets"] if row["name"] == "欧杨桐尸体" and row["kind"] == "object")
            c.interact(target["id"])
            state = idle(c)
            if item(state, "狮子吼.ini")["quantity"] != 1:
                raise AutomationError("Ouyang's bound body did not grant its new book once")
            reject(c, "Interact", "action_rejected", generation=state["generation"], targetId=target["id"])
            c.act("UseItem", generation=state["generation"], slot=item(state, "狮子吼.ini")["slot"])
            item(idle(c), "player-magic-狮子吼.ini", "magic")
            mainline.late_object(c, 79, 133)
            state = idle(c)
            if item(state, "中都金砖.ini")["quantity"] != 1 or state["player"]["money"] != before["player"]["money"]:
                raise AutomationError("Changed gold chest did not grant exactly its defined item")
            box = next(row for row in state["targets"] if row["kind"] == "object" and row["position"] == dict(x=79, y=133))
            reject(c, "Interact", "action_rejected", generation=state["generation"], targetId=box["id"])
            late_records(output, "trace.jsonl", completed_scripts=("script/common/捡到狮子吼书.txt",
                         "script/goods/狮子吼.txt", "script/common/中都金砖.txt"))
            c.save_or_load(1)
            checkpoint(c, output, "zhongdu-ouyang-book-and-gold-after-slot1")
        return previous_talk(c, name, position)

    previous_go, previous_checkpoint, previous_fight = mainline.go, mainline.checkpoint, mainline.fight
    previous_talk = mainline.talk
    mainline.go, mainline.checkpoint, mainline.fight, mainline.talk = route_go, checkpoint, route_fight, route_talk
    try:
        if client.observe(("ZDBiWu",))["variables"].get("ZDBiWu") == "4":
            verify(client, ZDBiWu=4, OuYangMusic=1)
            route_go(client, 80, 159)
            money = client.observe()["player"]["money"]
            route_talk(client, "女子1", (80, 135))
            if not 9000 <= client.observe()["player"]["money"] - money <= 20000:
                raise AutomationError("The captive's bound random money reward differs")
            quantity = sum(row["quantity"] for row in client.observe()["inventory"])
            route_talk(client, "女子2", (78, 137))
            state = idle(client)
            if (sum(row["quantity"] for row in state["inventory"]) != quantity + 1
                    or any(row["name"] in ("女子1", "女子2") for row in state["targets"])):
                raise AutomationError("Captive rewards or removal differ")
            checkpoint(client, output, "side-zhongdu-captives-rescued")
            route_go(client, 80, 159)
            for point in ((77, 158), (71, 140), (60, 146), (50, 136), (43, 120), (32, 112)):
                route_go(client, *point)
            route_go(client, 22, 131, destination="中都.map")
        else:
            mainline.zhongdu_shaolin_side_story(client, output)
    finally:
        mainline.go, mainline.checkpoint, mainline.fight, mainline.talk = previous_go, previous_checkpoint, previous_fight, previous_talk
    client.save_or_load(2)
    checkpoint(client, output, "zhongdu-shaolin-and-added-rewards-complete-slot2")


def zhongdu_night(client, output):
    state = verify(client, ZDBiWu=4, OuYangMusic=1, ZhongDuHouHuaYuan=0)
    if state["map"] not in ("中都.map", "中都夜.map"):
        raise AutomationError("Night story needs the normal completed Shaolin city source")
    if state["map"] == "中都.map":
        for point in ((84, 103), (47, 93)):
            go(client, *point, combat=False, script="中都/龙音寺门口地图陷阱5.txt" if point == (47, 93) else None)
        go(client, 48, 92, script="中都/龙音寺门口地图陷阱6.txt", combat=False)
        verify(client, ZhongDuLYS=1)
        client.save_or_load(0)
        checkpoint(client, output, "zhongdu-longyin-meeting-complete-slot0")
    previous_go, previous_checkpoint, previous_fight = mainline.go, mainline.checkpoint, mainline.fight
    previous_talk, previous_side, previous_return = mainline.talk, mainline.zhongdu_shaolin_side_story, mainline.zhongdu_return_to_linan

    def completed_shaolin(c, _output, **_options):
        verify(c, ZDBiWu=4, OuYangMusic=1)

    def stop_before_return(c, _output, **_options):
        verify(c, ZhongDuHouHuaYuan=5)
        c.save_or_load(2)
        checkpoint(c, output, "zhongdu-night-story-complete-before-new-rewards-slot2")

    def route_talk(c, name, position=None):
        before = c.observe(VARIABLES)
        state = previous_talk(c, name, position)
        state = c.observe(VARIABLES)
        if name == "燕府家丁" and state["map"] == "中都夜.map":
            if state["player"]["money"] != before["player"]["money"] - 100:
                raise AutomationError("The defined manor entry fee was not 100 taels")
            c.save_or_load(1)
            checkpoint(c, output, "zhongdu-night-entry-fee-before-garden-slot1")
            for point in ((84, 103), (97, 223), (70, 334)):
                go(c, *point, combat=False)
            go(c, 20, 332, script="中都夜/trap-2.txt", combat=False)
            refused = checkpoint(c, output, "zhongdu-night-premature-exit-refused")
            if refused["map"] != "中都夜.map" or refused["variables"] != state["variables"]:
                raise AutomationError("Night exit changed map or story before its required meeting")
            for point in ((70, 334), (97, 223)):
                go(c, *point, combat=False)
        if name == "柴嵩" and state["variables"].get("ZhongDuTroublesRoom") == "4":
            if state["player"]["canJump"] or not any(row["name"] == "柴嵩" for row in state["targets"]):
                raise AutomationError("Chai Song did not join with the scripted movement restriction")
            c.save_or_load(4)
            checkpoint(c, output, "zhongdu-night-chai-joined-slot4")
            slot = output / "user-data/save" / SAVE_NAMESPACE / "rpg5"
            actors = configparser.ConfigParser(strict=False)
            actors.read(slot / "partner.ini", encoding="utf-8-sig")
            companions = [dict(actors[section]) for section in actors.sections()
                          if actors[section].get("name") == "柴嵩"]
            if len(companions) != 1 or companions[0].get("kind") != "3":
                raise AutomationError("Normal saved Chai Song was not a Kind=3 partner")
            write_json(output / "zhongdu-chai-joined-save-proof.json", dict(status="passed", actors=companions,
                       canJump=False, manualSlot=4, saveBytesEdited=False))
        if name == "燕若雪" and state["variables"].get("ZhongDuHouHuaYuan") == "5":
            if not state["player"]["canJump"] or any(row["name"] in ("柴嵩", "燕若雪", "婕儿") for row in state["targets"]):
                raise AutomationError("Night departure did not remove the companions and restore jumping")
            c.save_or_load(3)
            checkpoint(c, output, "zhongdu-night-added-enemies-before-slot3")
        return state

    mainline.go, mainline.checkpoint, mainline.fight, mainline.talk = go, checkpoint, fight, route_talk
    mainline.zhongdu_shaolin_side_story, mainline.zhongdu_return_to_linan = completed_shaolin, stop_before_return
    try:
        if state["map"] == "中都.map":
            mainline.zhongdu_town_story(client, output, craft_sword=False, treasury_chest_opened=True)
        else:
            for point in ((70, 334), (97, 223)):
                go(client, *point, combat=False)
            mainline.late_enter_night_garden(client)
            for stage in range(1, 5):
                route_talk(client, "燕若雪")
                verify(client, ZhongDuHouHuaYuan=stage)
            mainline.late_leave_night_garden(client)
            for point in ((97, 223), (84, 103), (75, 20), (79, 30)):
                go(client, *point, combat=False)
            route_talk(client, "柴嵩")
            verify(client, ZhongDuTroublesRoom=4)
            route_talk(client, "燕若雪")
            mainline.late_leave_night_garden(client)
            stop_before_return(client, output)
    finally:
        mainline.go, mainline.checkpoint, mainline.fight, mainline.talk = previous_go, previous_checkpoint, previous_fight, previous_talk
        mainline.zhongdu_shaolin_side_story, mainline.zhongdu_return_to_linan = previous_side, previous_return


def zhongdu_night_rewards(client, output):
    state = verify(client, ZhongDuHouHuaYuan=5)
    if state["map"] != "中都夜.map":
        raise AutomationError("Added night rewards require the normal completed garden source")
    initial = []
    for position in ((31, 263), (31, 261)):
        matches = [row for row in state["targets"] if row["name"] == "金国将领"
                   and row["position"] == dict(x=position[0], y=position[1]) and npc_attackable(row)]
        if len(matches) != 1:
            raise AutomationError(f"Cannot identify the bound night reward actor at {position}")
        initial.append(matches[0])
    client.save_or_load(0)
    checkpoint(client, output, "zhongdu-night-two-rewards-source-slot0")
    roar, phoenix = "player-magic-狮子吼.ini", "player-magie-凤凰展翅.ini"
    assist_magic(client, output, roar)
    client.assign_magic(item(client.observe(), roar, "magic")["slot"], 0)
    for point in ((70, 334), (70, 290), (36, 274)):
        go(client, *point)
    # The garden's southern street leads to the two new reward actors.
    go(client, 32, 266, combat=False)
    client.save_or_load(1)
    checkpoint(client, output, "zhongdu-night-near-rewards-before-slot1")
    proofs = []
    for original, magic in zip(initial, (roar, roar)):
        before = client.observe(VARIABLES)
        if before["generation"] != state["generation"]:
            raise AutomationError("Night actors changed world before the two reward fights")
        remaining = [row for row in before["targets"] if row["id"] == original["id"] and npc_attackable(row)]
        if remaining:
            client.assign_magic(item(before, magic, "magic")["slot"], 0)
            checkpoint(client, output, f"zhongdu-night-reward-before-{original['id']}")
            started = time.monotonic()
            fight(client, remaining[0], allow_melee_fallback=False)
            proofs.append(dict(target=original, magic=magic, allowMeleeFallback=False,
                               elapsedSeconds=time.monotonic() - started))
        after = checkpoint(client, output, f"zhongdu-night-reward-after-{original['id']}")
        if any(row["id"] == original["id"] and npc_attackable(row) for row in after["targets"]):
            raise AutomationError("The bound night reward actor survived")
    late_records(output, "trace.jsonl", completed_scripts=("script/common/邂逅夜明珠.txt",
                 "script/common/中都夜金兵头目.txt"))
    client.save_or_load(2)
    checkpoint(client, output, "zhongdu-night-two-rewards-complete-slot2")
    write_json(output / "zhongdu-night-two-rewards-proof.json", dict(status="passed", initialActors=initial,
               fights=proofs, sameGeneration=True, saveBytesEdited=False))


def to_linan(client, output):
    starting_map = idle(client)["map"]
    if starting_map not in ("中都夜.map", "稻香村.map", "临安城.map"):
        raise AutomationError("Linan return requires the normal completed Zhongdu night source")
    verify(client, ZhongDuHouHuaYuan=5)
    client.assign_magic(item(client.observe(), "player-magie-凤求凰.ini", "magic")["slot"], 0)
    previous_go, previous_checkpoint, previous_fight = mainline.go, mainline.checkpoint, mainline.fight
    previous_daoxiang = mainline.daoxiang_to_linan

    def leave_linan(c):
        if c.observe()["player"]["position"]["x"] <= 100:
            mainline.linan_approach(c, 60, 310)
        return mainline.late_leave_linan(c)

    def daoxiang_additions(c, _output, **_options):
        checkpoint(c, output, "daoxiang-changed-challenge-before")
        state = verify(c, DaoXiangFirstEnter=1)
        for name in ("王重阳", "洪七", "欧阳锋"):
            state = idle(c)
            targets = [row for row in state["targets"] if row["name"] == name and npc_attackable(row)]
            if targets:
                if len(targets) != 1:
                    raise AutomationError(f"Cannot identify the changed Daoxiang opponent {name}")
                fight(c, targets[0], allow_melee_fallback=False)
        verify(c, DaoXiangFight=3)
        c.save_or_load(0)
        checkpoint(c, output, "daoxiang-changed-challenge-complete-slot0")
        if not (output / "daoxiang-added-sell-shop.json").exists():
            for point in ((44, 276), (71, 263)):
                go(c, *point)
            before = c.observe()
            merchant = next(row for row in before["targets"] if row["name"] == "摊贩摆卖1"
                            and row.get("interactive") and row["position"] == dict(x=71, y=261))
            c.interact(merchant["id"])
            c.wait_until(lambda value: "shop" in value, timeout=120)
            checkpoint(c, output, "daoxiang-added-sell-shop")
            c.ui("Cancel")
            state = idle(c)
            if state["inventory"] != before["inventory"] or state["player"]["money"] != before["player"]["money"]:
                raise AutomationError("Cancelling the added Daoxiang shop changed goods or money")
            write_json(output / "daoxiang-shop-cancel-proof.json", dict(status="passed", moneyUnchanged=True,
                       inventoryUnchanged=True, generation=state["generation"]))
        else:
            late_records(output, "trace.jsonl", completed_scripts=("script/common/稻香村收货.txt",))
        state = c.observe()
        if state["player"]["position"]["x"] < 110:
            for point in ((77, 183), (97, 143)):
                go(c, *point)
            go(c, 123, 87, destination="临安城.map")
            leave_linan(c)
        for point in ((117, 143), (117, 183), (117, 233), (123, 234), (125, 232), (127, 227)):
            go(c, *point)
        c.save_or_load(1)
        before = checkpoint(c, output, "daoxiang-combined-magic-before-slot1")
        for filename in ("player-magic-花飞蝶舞剑.ini", "player-magic-逍遥刀法.ini"):
            item(before, filename, "magic")
        talk(c, "小男童", (126, 227))
        after = idle(c)
        item(after, "player-magic-天外飞仙.ini", "magic")
        if any(row["file"] in ("player-magic-花飞蝶舞剑.ini", "player-magic-逍遥刀法.ini") for row in after["magic"]):
            raise AutomationError("Daoxiang's two source magics were not removed by its bound script")
        if any(row["name"] == "小男童" for row in after["targets"]):
            raise AutomationError("The combined magic actor remained after his bound departure")
        late_records(output, "trace.jsonl", completed_scripts=("script/common/稻香村收货.txt",
                     "script/common/稻香村武功支线.txt"))
        c.save_or_load(2)
        checkpoint(c, output, "daoxiang-combined-magic-after-slot2")
        state = c.observe()
        if not any(row["file"] == "player-magic-天师符法.ini" for row in state["magic"]):
            c.act("UseItem", generation=state["generation"], slot=item(state, "book-天师符法秘笈.ini")["slot"])
            item(idle(c), "player-magic-天师符法.ini", "magic")
        for point in ((117, 183), (117, 143), (117, 103)):
            go(c, *point)
        return go(c, 123, 87, destination="临安城.map")

    mainline.go, mainline.checkpoint = go, checkpoint
    mainline.fight = lambda c, target, **_options: fight(c, target, allow_melee_fallback=False)
    mainline.daoxiang_to_linan = daoxiang_additions
    try:
        if starting_map == "中都夜.map":
            mainline.zhongdu_return_to_linan(client, output)
        else:
            if starting_map == "临安城.map":
                leave_linan(client)
            daoxiang_additions(client, output)
    finally:
        mainline.go, mainline.checkpoint, mainline.fight = previous_go, previous_checkpoint, previous_fight
        mainline.daoxiang_to_linan = previous_daoxiang
    client.save_or_load(3)
    checkpoint(client, output, "linan-arrival-after-daoxiang-additions-slot3")


def linan_additions(client, output):
    before = checkpoint(client, output, "linan-added-source-slot0")
    if before["map"] != "临安城.map":
        raise AutomationError("Linan additions require normal city arrival")
    strong = [row for row in before["targets"] if row["name"] == "剑客B" and row.get("life", 0) > 200000]
    weak = [row for row in before["targets"] if row["name"] == "金国将领" and npc_attackable(row)]
    if len(strong) != 3 or len(weak) != 7:
        raise AutomationError("The ten added Linan enemies are not present")
    client.save_or_load(0)
    if before["player"]["position"]["y"] > 280:
        for point in ((42, 309), (45, 291), (47, 270), (53, 258), (59, 246), (64, 233),
                      (73, 226), (78, 212), (82, 196), (87, 182), (94, 172), (100, 161),
                      (109, 155), (108, 144), (102, 131), (96, 119), (96, 116)):
            mainline.linan_approach(client, *point, combat=False)
    talk(client, "秘籍侍女")
    gated = checkpoint(client, output, "linan-added-maids-before-battle")
    if not all(any(row["name"] == name for row in gated["targets"]) for name in ("秘籍侍女", "秘籍侍")):
        raise AutomationError("Added maid dialogue removed the gate before its bound battle")
    assist_magic(client, output, "player-magic-天外飞仙.ini")
    client.assign_magic(item(client.observe(), "player-magie-凤求凰.ini", "magic")["slot"], 0)
    first = max(strong, key=lambda row: row["life"])
    fight(client, next(row for row in client.observe()["targets"] if row["id"] == first["id"]),
          allow_melee_fallback=False)
    for point in ((102, 131), (108, 144), (109, 155)):
        go(client, *point, combat=False)
    client.assign_magic(item(client.observe(), "player-magic-天外飞仙.ini", "magic")["slot"], 0)
    combined_kills = 0
    for original in weak:
        target = next((row for row in client.observe()["targets"]
                       if row["id"] == original["id"] and npc_attackable(row)), None)
        if target:
            fight(client, target, allow_melee_fallback=False)
            combined_kills += 1
    checkpoint(client, output, "linan-combined-magic-battle-complete")
    client.assign_magic(item(client.observe(), "player-magie-凤求凰.ini", "magic")["slot"], 0)
    for original in strong:
        target = next((row for row in client.observe()["targets"]
                       if row["id"] == original["id"] and npc_attackable(row)), None)
        if target:
            if original["life"] == 390000:
                for point in ((87, 182), (82, 196), (80, 206)):
                    go(client, *point, combat=False)
                target = next(row for row in client.observe()["targets"] if row["id"] == original["id"])
            fight(client, target, allow_melee_fallback=False)
    after = idle(client)
    ids = {row["id"] for row in [*strong, *weak]}
    if after["generation"] != before["generation"] or any(row["id"] in ids and npc_attackable(row)
                                                        for row in after["targets"]):
        raise AutomationError("An added enemy survived or the map changed during the battle")
    if any(row["name"] in ("秘籍侍女", "秘籍侍") for row in after["targets"]):
        raise AutomationError("The normal bound death script did not release the added gate")
    late_records(output, "trace.jsonl", completed_scripts=("script/common/客栈侍女.txt",
                 "script/common/临安秘籍侍女.txt"))
    client.save_or_load(1)
    checkpoint(client, output, "linan-added-enemies-defeated-slot1")
    for point in ((100, 161), (109, 155), (108, 144), (102, 131), (96, 119), (96, 107), (97, 100)):
        go(client, *point, combat=False)
    talk(client, "宝箱", (97, 98))
    learned = item(idle(client), "player-magic-圣手扑蝶.ini", "magic")
    checkpoint(client, output, "linan-added-chest-obtained")
    talk(client, "宝箱", (97, 98))
    if item(idle(client), learned["file"], "magic") != learned:
        raise AutomationError("Repeating the bound chest changed the learned magic")
    late_records(output, "trace.jsonl", completed_scripts=("script/common/临安圣手扑蝶.txt",))
    client.save_or_load(2)
    saved = checkpoint(client, output, "linan-added-content-slot2")
    load_checkpoint(client, 2)
    reloaded = checkpoint(client, output, "linan-added-content-reloaded")
    for key in ("map", "variables", "inventory", "magic"):
        if reloaded[key] != saved[key]:
            raise AutomationError(f"Linan added-content reload mismatch: {key}")
    write_json(output / "linan-added-content-proof.json", dict(status="passed", enemyIds=sorted(ids),
               enemiesNormallyDefeated=10, combinedMagicLevel=10, combinedMagicCombatCommands=combined_kills,
               normalGateDeathScriptCompleted=True, chestMagicLearned=True, repeatMagicUnchanged=True,
               normalSaveReloadMatched=True, saveBytesEdited=False))


def fengchi_additions(client, output):
    state = idle(client)
    client.assign_magic(item(state, "player-magic-天意剑诀.ini", "magic")["slot"], 0)
    client.assign_practice(item(client.observe(), "player-magic-风雪狂刀.ini", "magic")["slot"])
    previous_go, previous_checkpoint, previous_talk, previous_restock, previous_meditate = (
        mainline.go, mainline.checkpoint, mainline.talk, mainline.restock, mainline.meditate)

    def required_talk(c, name, *arguments, **options):
        if name == "守门家丁" and c.observe()["map"] == "凤池山庄.map":
            c.save_or_load(0)
            before = checkpoint(c, output, "fengchi-added-female-before-slot0")
            talk(c, "剑客A")
            after = checkpoint(c, output, "fengchi-added-female-after")
            item(after, "player-magie-洗髓易经.ini", "magic")
            late_records(output, "trace.jsonl", completed_scripts=("script/common/风池山庄意外.txt",))
            write_json(output / "fengchi-added-female-proof.json", dict(status="grant-passed",
                       beforeMoney=before["player"]["money"], afterMoney=after["player"]["money"],
                       beforeMagic=before["magic"], afterMagic=after["magic"], saveBytesEdited=False,
                       transactionIntentUnconfirmed=True))
            c.save_or_load(1)
        return talk(c, name, *arguments, **options)

    mainline.go, mainline.checkpoint, mainline.talk = go, checkpoint, required_talk
    if state.get("cheatInvincibilityEnabled"):
        mainline.restock = lambda *_args, **_options: None
        mainline.meditate = lambda *_args, **_options: None
    try:
        phase = client.observe(("FromFengChi",))["variables"].get("FromFengChi")
        if state["map"] == "临安城.map" and phase == "7":
            mainline.late_leave_linan(client)
            for point in ((97, 143), (77, 183), (79, 242)):
                go(client, *point)
            go(client, 100, 312, destination="霹雳堂.map")
        else:
            if state["map"] == "凤池山庄.map" and phase in ("", "0", "1"):
                required_talk(client, "守门家丁")
                mainline.late_return_to_linan(client)
            mainline.linan_and_fengchi(client, output)
    finally:
        mainline.go, mainline.checkpoint, mainline.talk, mainline.restock, mainline.meditate = (
            previous_go, previous_checkpoint, previous_talk, previous_restock, previous_meditate)
    client.save_or_load(2)
    checkpoint(client, output, "pilitang-arrival-after-fengchi-additions-slot2")


def pilitang_restored_swords(client, output):
    before = checkpoint(client, output, "pilitang-restored-swords-before-slot0")
    swords = [row for row in before["targets"] if row["name"] == "剑客B" and npc_attackable(row)]
    if (before["map"] != "霹雳堂.map" or len([row for row in before["targets"] if row["kind"] == "npc"]) != 65
            or sorted(row["life"] for row in swords) != [210000, 280000]):
        raise AutomationError("The fixed Pilitang template did not load both restored swords")
    client.save_or_load(0)
    client.assign_magic(item(before, "player-magie-凤求凰.ini", "magic")["slot"], 0)
    go(client, 20, 153, script="霹雳堂/trap1.txt", combat=False)
    resource = Path(json.loads((output / "run.json").read_text(encoding="utf-8"))["command"][2]) / RESOURCE_DIRECTORY
    for destination in ((57, 86), (52, 76)):
        state = idle(client)
        path = reachable_trap(resource, state["map"], 0, state["player"]["position"],
                              with_path=True, destination=destination)
        for point in (*path[5:-1:6], path[-1]):
            go(client, *point)
    for original in swords:
        target = next((row for row in client.observe()["targets"]
                       if row["id"] == original["id"] and npc_attackable(row)), None)
        if target:
            if not target["visibleFromPlayer"]:
                raise AutomationError("Restored sword remains behind an obstacle")
            fight(client, target, allow_melee_fallback=False)
    after = checkpoint(client, output, "pilitang-restored-swords-normally-defeated")
    if after["generation"] != before["generation"] or any(row["id"] in {actor["id"] for actor in swords}
           and npc_attackable(row) for row in after["targets"]):
        raise AutomationError("Restored swords did not both normally die in the same map")
    client.save_or_load(1)
    load_checkpoint(client, 1)
    reloaded = checkpoint(client, output, "pilitang-restored-swords-reloaded")
    for key in ("map", "variables", "inventory", "magic"):
        if after[key] != reloaded[key]:
            raise AutomationError(f"Restored-sword reload differs: {key}")
    write_json(output / "pilitang-restored-swords-proof.json", dict(status="passed", actualNpcCount=65,
               originalActors=swords, bothNormallyDefeated=True, sameGeneration=True,
               normalSaveReloadMatched=True, saveBytesEdited=False))


def pilitang(client, output):
    prior = output / "pilitang-before-slot0.json"
    before = (json.loads(prior.read_text(encoding="utf-8")) if prior.exists()
              else checkpoint(client, output, "pilitang-before-slot0"))
    if before["map"] != "霹雳堂.map":
        raise AutomationError("Pilitang requires its normal arrival save")
    if client.observe(("FromFengChi",))["variables"].get("FromFengChi") == "8":
        return pilitang_rewards(client, output)
    verify(client, FromFengChi=7)
    if not (output / "pilitang-xisui-normal-ordinary-kill.json").exists():
        client.save_or_load(0)
        assist_magic(client, output, "player-magic-圣手扑蝶.ini")
        guard = next(row for row in client.observe()["targets"]
                     if row["name"] == "守门弟子" and npc_attackable(row) and row["visibleFromPlayer"])
        fight(client, guard, allow_melee_fallback=False)
        checkpoint(client, output, "pilitang-shengshou-normal-kill")
        assist_magic(client, output, "player-magie-洗髓易经.ini")
        ordinary = next(row for row in client.observe()["targets"] if npc_attackable(row)
                        and row["visibleFromPlayer"] and row["life"] <= 870)
        fight(client, ordinary, allow_melee_fallback=False)
        checkpoint(client, output, "pilitang-xisui-normal-ordinary-kill")
    client.assign_magic(item(client.observe(), "player-magie-凤求凰.ini", "magic")["slot"], 0)
    if not (output / "pilitang-fengqiu-normal-kill-and-green-crystal.json").exists():
        go(client, 20, 153, script="霹雳堂/trap1.txt", combat=False)
        for point in ((26, 142), (29, 134)):
            go(client, *point, combat=False)
        state = idle(client)
        reward_boss = next(row for row in state["targets"]
                           if row["name"] == "雷晃" and row["id"] == next(
                               target["id"] for target in before["targets"]
                               if target["name"] == "雷晃" and target["life"] == 124500))
        if not reward_boss["visibleFromPlayer"]:
            raise AutomationError("Reward-bearing Lei Huang is behind an obstacle")
        fight(client, reward_boss, allow_melee_fallback=False)
    green_before = sum(row["quantity"] for row in before["inventory"] if row["file"] == "得到绿水晶.ini")
    state = checkpoint(client, output, "pilitang-fengqiu-normal-kill-and-green-crystal")
    if sum(row["quantity"] for row in state["inventory"] if row["file"] == "得到绿水晶.ini") != green_before + 1:
        raise AutomationError("Lei Huang's bound crystal reward differs")
    late_records(output, "trace.jsonl", completed_scripts=("script/common/得到绿水晶.txt",))
    client.assign_magic(item(state, "player-magie-凤求凰.ini", "magic")["slot"], 0)
    state = idle(client)
    resource = Path(json.loads((output / "run.json").read_text(encoding="utf-8"))["command"][2]) / RESOURCE_DIRECTORY
    path = reachable_trap(resource, state["map"], 0, state["player"]["position"],
                          with_path=True, destination=(57, 86))
    for point in path[5:-1:6]:
        go(client, *point)
    go(client, *path[-1])
    state = idle(client)
    if state["generation"] != before["generation"]:
        raise AutomationError("Pilitang changed maps before verifying the added Lei Huang")
    extra = next((row for row in state["targets"] if row["name"] == "雷晃" and npc_attackable(row)), None)
    if extra:
        if not extra["visibleFromPlayer"]:
            raise AutomationError("Added Lei Huang is behind an obstacle")
        fight(client, extra, allow_melee_fallback=False)
    checkpoint(client, output, "pilitang-both-lei-huang-normally-defeated")
    for point in ((60, 56), (66, 44)):
        go(client, *point, combat=False)
    boss = next(row for row in client.observe()["targets"] if row["name"] == "雷同" and npc_attackable(row))
    if not boss["visibleFromPlayer"]:
        raise AutomationError("Lei Tong is behind a hall obstacle")
    fight(client, boss, allow_melee_fallback=False)
    verify(client, FromFengChi=8, LinAnYanRuoXue=9)
    late_records(output, "trace.jsonl", completed_scripts=("script/map/霹雳堂/雷同死亡.txt",))
    return pilitang_rewards(client, output)


def pilitang_rewards(client, output):
    verify(client, FromFengChi=8, LinAnYanRuoXue=9)
    late_records(output, "trace.jsonl", completed_scripts=("script/map/霹雳堂/雷同死亡.txt",))
    state = client.wait_until(lambda state: any(row["kind"] == "object" and row["name"] == "雷同"
                             and row["position"] == dict(x=65, y=45) for row in state["targets"]),
                             timeout=15, description="Lei Tong's normal book-bearing corpse")
    if not any(row["file"] == "book-霹雳手法.ini" for row in state["inventory"]):
        talk(client, "雷同", (65, 45))
    state = idle(client)
    book = item(state, "book-霹雳手法.ini")
    corpse = next(row for row in state["targets"] if row["kind"] == "object" and row["name"] == "雷同"
                  and row["position"] == dict(x=65, y=45))
    reject(client, "Interact", "action_rejected", generation=state["generation"], targetId=corpse["id"])
    client.act("UseItem", generation=state["generation"], slot=book["slot"])
    item(idle(client), "player-magic-霹雳烈焰手法.ini", "magic")
    late_records(output, "trace.jsonl", completed_scripts=("script/map/霹雳堂/捡到书.txt",
                 "script/goods/book-霹雳烈焰手法.txt"))
    client.save_or_load(3)
    saved = checkpoint(client, output, "pilitang-rewards-and-learned-magic-slot3")
    load_checkpoint(client, 3)
    reloaded = checkpoint(client, output, "pilitang-rewards-reloaded")
    for key in ("map", "variables", "inventory", "magic"):
        if reloaded[key] != saved[key]:
            raise AutomationError(f"Pilitang reload differs: {key}")
    write_json(output / "pilitang-proof.json", dict(status="passed", saintButterflyPureSkillKill=True,
               marrowScriptPureSkillKill=True, crystalQuantityDelta=1, addedLeiHuangDefeated=True,
               leiTongNormalDeathCompleted=True, corpseBookLearned=True, repeatBookRejected=True,
               normalSaveReloadMatched=True, saveBytesEdited=False,
               restoredTwoSwordsVerified=False))


def tournament_entry(client, output):
    if idle(client)["map"] != "霹雳堂.map":
        raise AutomationError("Tournament approach requires the normal Pilitang completion")
    verify(client, FromFengChi=8, LinAnYanRuoXue=9)
    previous_go, previous_checkpoint = mainline.go, mainline.checkpoint
    mainline.go, mainline.checkpoint = go, checkpoint
    try:
        go(client, 8, 181, destination="稻香村.map")
        mainline.daoxiang_to_linan(client, output)
        mainline.late_enter_fengchi(client)
        verify(client, FromFengChi=9)
        client.save_or_load(3)
        checkpoint(client, output, "fengchi-before-shao-duel-slot3")
        go(client, 74, 197, script="凤池山庄/datingtalk.txt")
        mainline.late_return_to_linan(client)
        talk(client, "燕若雪")
        verify(client, LinAnChaiSong=2)
        client.save_or_load(5)
        checkpoint(client, output, "tournament-before-entry-slot5")
        talk(client, "柴嵩")
        state = verify(client, FCBW=1)
        if state["map"] != "凤池山庄-比武场.map":
            raise AutomationError("Normal tournament dialogue did not enter its map")
        client.save_or_load(0)
        checkpoint(client, output, "tournament-first-round-source-slot0")
    finally:
        mainline.go, mainline.checkpoint = previous_go, previous_checkpoint


def tournament(client, output):
    opponents = ("赵无双", "秋依水", "唐影", "孟廷威", "柴嵩", "杨干",
                 "邵骑风", "史忠良", "唐离", "赵升权", "独孤剑")
    rounds = []
    while idle(client)["map"] == "凤池山庄-比武场.map":
        state = client.observe(("FCBW", "HappyEnding"))
        stage = int(state["variables"].get("FCBW") or 0)
        if not 1 <= stage <= len(opponents):
            raise AutomationError(f"Unexpected active tournament stage: {stage}")
        if stage in (1, 11):
            client.save_or_load(0 if stage == 1 else 1)
            checkpoint(client, output, f"tournament-round-{stage}-normal-source")
        client.assign_magic(item(state, "player-magie-凤求凰.ini", "magic")["slot"], 0)
        state = idle(client)
        target = next(row for row in state["targets"] if row["name"] == opponents[stage - 1]
                      and npc_attackable(row))
        if not target["visibleFromPlayer"]:
            raise AutomationError(f"Tournament opponent is behind an obstacle: {target}")
        before = checkpoint(client, output, f"tournament-round-{stage}-before")
        result = client.act("StartCombat", generation=state["generation"], targetId=target["id"],
                            radius=20, kills=1, skills=[0], allowMeleeFallback=False,
                            timeoutMs=240000, timeout=245)
        if result.get("kills") != 1 and result.get("reason") != "world_changed":
            raise AutomationError(f"Tournament battle did not observe its bound outcome: {result}")
        state = client.wait_until(lambda value: value.get("worldInput") and (
                    value["map"] == "凤池山庄.map" or value["map"] == "凤池山庄-比武场.map"
                    and value["variables"].get("FCBW") not in (str(stage), "12")),
                    variables=("FCBW", "HappyEnding"), timeout=600, description="normal tournament next round")
        if (stage < 11 and (state["map"] != before["map"] or state["variables"].get("FCBW") != str(stage + 1))
                or stage == 11 and (state["map"] != "凤池山庄.map" or state["variables"].get("HappyEnding") != "1")):
            raise AutomationError("Tournament's actual result disagrees with its intended win")
        late_records(output, "trace.jsonl", completed_scripts=(f"script/map/凤池山庄-比武场/{opponents[stage - 1]}败.txt",))
        rounds.append(dict(stage=stage, target=target, actionResult=result, nextMap=state["map"],
                           nextStage=state["variables"].get("FCBW")))
        write_json(output / "tournament-rounds.json", rounds)
        checkpoint(client, output, f"tournament-round-{stage}-after")
    verify(client, HappyEnding=1)
    late_records(output, "trace.jsonl", completed_scripts=("script/map/凤池山庄-比武场/比武结束.txt",))
    client.save_or_load(2)
    checkpoint(client, output, "tournament-won-normal-slot2")


def tournament_loss(client, output):
    before = checkpoint(client, output, "tournament-loss-source")
    stage = int(client.observe(("FCBW",))["variables"].get("FCBW") or 0)
    if before["map"] != "凤池山庄-比武场.map" or stage not in (1, 11):
        raise AutomationError("Tournament loss requires a normal saved first or final round")
    assist_battle(client, output, before["player"]["level"], invincible=False)
    state = client.wait_until(lambda value: value.get("worldInput") and value.get("map") == "凤池山庄.map",
                             variables=("FCBW", "HappyEnding"), timeout=600,
                             description="player defeat and the complete spectator tournament branch")
    if state["variables"].get("HappyEnding") not in ("", "0") or state["player"]["life"] <= 0:
        raise AutomationError("Tournament loss did not restore the player with its actual losing result")
    required = ["script/map/凤池山庄-比武场/主角死亡.txt", "script/map/凤池山庄-比武场/比武结束.txt"]
    if stage == 1:
        required.extend(("script/map/凤池山庄-比武场/独孤剑.txt", "script/map/凤池山庄-比武场/唐离败.txt",
                         "script/map/凤池山庄-比武场/赵升权败.txt"))
    late_records(output, "trace.jsonl", completed_scripts=required)
    assist_battle(client, output, state["player"]["level"])
    client.save_or_load(2)
    checkpoint(client, output, "tournament-loss-complete-slot2")
    write_json(output / "tournament-loss-proof.json", dict(status="passed", defeatedAtStage=stage,
               subsequentSpectatorBranchCompleted=stage == 1, happyEnding=0,
               playerNormallyRestored=True, completedScripts=required))


def fengchi_night(client, output):
    state = idle(client)
    client.assign_magic(item(state, "player-magie-凤求凰.ini", "magic")["slot"], 0)
    if state["map"] == "凤池山庄.map":
        client.save_or_load(0)
        checkpoint(client, output, "fengchi-before-night-normal-slot0")
        go(client, 102, 154, destination="凤池山庄夜战.map", timeout=300, combat=False)
    state = verify(client, FromFengChi=12, FengChiFight=1)
    if state["map"] != "凤池山庄夜战.map":
        raise AutomationError("Fengchi battle did not reach its actual night map")
    if not (output / "fengchi-night-before-battle-slot3.json").exists():
        client.save_or_load(3)
        checkpoint(client, output, "fengchi-night-before-battle-slot3")
    records = late_records(output, "trace.jsonl")
    if not any(row.get("eventType") == "script.start" and row.get("virtualPath", "").endswith(
            "script/map/凤池山庄夜战/aodie.txt") for row in records):
        go(client, 105, 167, script="凤池山庄夜战/trap7.txt", combat=False)
        refused = checkpoint(client, output, "fengchi-night-exit-before-ao-refused")
        if refused["map"] != "凤池山庄夜战.map":
            raise AutomationError("The active battle exit did not refuse early departure")
        go(client, 111, 125)
        go(client, 113, 115)
        target = next((row for row in client.observe()["targets"] if row["name"] == "敖管家" and npc_attackable(row)), None)
        if target:
            if not target["visibleFromPlayer"]:
                raise AutomationError("Ao is behind the night hall obstacle")
            fight(client, target, allow_melee_fallback=False)
    late_records(output, "trace.jsonl", completed_scripts=("script/map/凤池山庄夜战/aodie.txt",))
    client.save_or_load(4)
    checkpoint(client, output, "fengchi-night-ao-normal-death-slot4")
    go(client, 107, 170, destination="临安大牢.map", combat=False)
    client.save_or_load(2)
    checkpoint(client, output, "fengchi-night-to-prison-normal-slot2")


def prison_approach(client, output):
    state = idle(client)
    if state["map"] != "临安大牢.map":
        raise AutomationError("Prison approach requires the normal captured-player source")
    client.save_or_load(0)
    checkpoint(client, output, "prison-before-door-and-magic-slot0")
    mainline.late_object(client, 42, 147)
    state = verify(client, LinAnDaLao11=1, FromFengChi=13)
    item(state, "player-magic-江翻海沸.ini", "magic")
    item(state, "goods-jian-5-龙泉剑.ini")
    late_records(output, "trace.jsonl", completed_scripts=("script/map/临安大牢/牢门.txt",))
    checkpoint(client, output, "prison-door-learned-and-sword")
    client.assign_magic(item(state, "player-magie-凤求凰.ini", "magic")["slot"], 0)
    go(client, 79, 78, destination="临安大牢-3.map")
    before = checkpoint(client, output, "prison-third-floor-three-shao-before")
    added = [row for row in before["targets"] if row["name"] == "邵骑风" and npc_attackable(row)]
    if sorted(row["life"] for row in added) != [191371, 221371, 391371]:
        raise AutomationError("The three modified third-floor Shao actors differ")
    go(client, 40, 26, combat=False)
    go(client, 64, 158, script="临安大牢-3/trap3.txt", combat=False)
    client.save_or_load(3)
    checkpoint(client, output, "prison-third-floor-before-three-shao-slot3")
    for original in sorted(added, key=lambda row: row["life"], reverse=True):
        state = idle(client)
        remaining = next((row for row in state["targets"] if row["id"] == original["id"] and npc_attackable(row)), None)
        if remaining:
            if not remaining["visibleFromPlayer"]:
                raise AutomationError("Third-floor Shao is behind an obstacle")
            fight(client, remaining, allow_melee_fallback=False)
    state = verify(client, FromFengChi=14)
    if state["generation"] != before["generation"] or any(row["id"] in {x["id"] for x in added}
                                                          and npc_attackable(row) for row in state["targets"]):
        raise AutomationError("A third-floor added Shao survived or changed maps")
    late_records(output, "trace.jsonl", completed_scripts=("script/map/临安大牢-3/邵骑风die.txt",))
    write_json(output / "prison-three-shao-proof.json", dict(status="passed", originalActors=added,
               allNormallyDefeated=True, sameGeneration=True, saveBytesEdited=False))
    go(client, 92, 174, destination="临安大牢第1层.map")
    client.save_or_load(1)
    checkpoint(client, output, "prison-before-companion-battle-normal-slot1")


def prison_companions(client, output, expected):
    flags = dict(zhao="Zhao", qiu="Qiu", cai="Cai", tang="Tang")
    deaths = set(flags.values()) if expected == "all" else set() if expected == "none" else {flags[expected]}
    state = client.observe((*flags.values(), "LinAnDie", "ShaoJiFeng"))
    if state["map"] != "临安大牢第1层.map" or state["variables"].get("ShaoJiFeng") not in ("", "0", "2"):
        raise AutomationError("Companion outcome needs the normal first-floor pre-battle source")
    checkpoint(client, output, "prison-companions-before-trigger")
    client.assign_magic(item(state, "player-magie-凤求凰.ini", "magic")["slot"], 0)
    if state["variables"].get("ShaoJiFeng") != "2":
        go(client, 15, 151, script="临安大牢第1层/trap-3.txt", combat=False)
        if expected == "qiu":
            state = idle(client)
            resource = Path(json.loads((output / "run.json").read_text(encoding="utf-8"))["command"][2]) / RESOURCE_DIRECTORY
            path = reachable_trap(resource, state["map"], 0, state["player"]["position"],
                                  with_path=True, destination=(13, 145))
            for point in (*path[5:-1:6], path[-1]):
                go(client, *point, combat=False)
                if client.observe(("Qiu",))["variables"].get("Qiu") == "1":
                    break
    if deaths:
        client.wait_until(lambda value: all(value["variables"].get(key) == "1" for key in deaths),
                          variables=(*flags.values(), "LinAnDie", "ShaoJiFeng"), timeout=180,
                          description="the selected companions' natural enemy-induced deaths")
    state = idle(client)
    target = next((row for row in state["targets"] if row["name"] == "邵骑风" and npc_attackable(row)), None)
    if target:
        if not target["visibleFromPlayer"]:
            resource = Path(json.loads((output / "run.json").read_text(encoding="utf-8"))["command"][2]) / RESOURCE_DIRECTORY
            path = reachable_trap(resource, state["map"], 0, state["player"]["position"],
                                  with_path=True, destination=(17, 125))
            for point in (*path[5:-1:6], path[-1]):
                go(client, *point)
                current = client.observe()
                target = next((row for row in current["targets"] if row["id"] == target["id"] and npc_attackable(row)), None)
                if target is None or target["visibleFromPlayer"]:
                    break
        if target and not target["visibleFromPlayer"]:
            raise AutomationError("First-floor Shao remains behind an obstacle")
        if target:
            fight(client, target, allow_melee_fallback=False)
    state = verify(client, ShaoJiFeng=2)
    state = client.observe((*flags.values(), "LinAnDie", "ShaoJiFeng"))
    actual = {key for key in flags.values() if state["variables"].get(key) == "1"}
    if actual != deaths or int(state["variables"].get("LinAnDie") or 0) != len(deaths):
        raise AutomationError(f"Companion result differs: expected {deaths}, observed {state['variables']}")
    names = dict(Zhao="赵姐姐死亡.txt", Qiu="秋姐姐死亡.txt", Cai="柴大哥死亡.txt", Tang="唐大哥死亡.txt")
    late_records(output, "trace.jsonl", completed_scripts=("script/map/临安大牢第1层/邵骑风die.txt",
                 *("script/map/临安大牢第1层/" + names[key] for key in deaths)))
    client.save_or_load(2)
    checkpoint(client, output, "prison-companions-after-battle-slot2")
    if expected == "all":
        go(client, 19, 119, script="临安大牢第1层/trap-5.txt", combat=False)
        go(client, 14, 130, script="临安大牢第1层/trap-4.txt", combat=False)
    previous_go, previous_checkpoint, previous_late = mainline.go, mainline.checkpoint, mainline.late_checkpoint
    mainline.go, mainline.checkpoint, mainline.late_checkpoint = go, checkpoint, checkpoint
    try:
        mainline.prison_exit(client, output)
    finally:
        mainline.go, mainline.checkpoint, mainline.late_checkpoint = previous_go, previous_checkpoint, previous_late
    state = verify(client, AnZang=1, TanHua=0 if expected == "all" else 1)
    client.save_or_load(4)
    checkpoint(client, output, "prison-companions-outcome-returned-linan-slot4")
    write_json(output / "prison-companions-proof.json", dict(status="passed", expectedDeaths=sorted(deaths),
               actualDeaths=sorted(actual), funeralCompleted=True,
               survivorConversation=expected != "all", normalReturnedToLinan=True))


def anran_magic(client, output):
    before = checkpoint(client, output, "anran-book-before-slot0")
    client.save_or_load(0)
    client.act("UseItem", generation=before["generation"], slot=item(before, "黯然销魂掌法.ini")["slot"])
    item(idle(client), "magic-黯然销魂掌.ini", "magic")
    late_records(output, "trace.jsonl", completed_scripts=("script/common/学会黯然销魂掌.txt",))
    assist_magic(client, output, "magic-黯然销魂掌.ini")
    state = idle(client)
    resource = Path(json.loads((output / "run.json").read_text(encoding="utf-8"))["command"][2]) / RESOURCE_DIRECTORY
    candidates = []
    for target in state["targets"]:
        if target["name"] == "金国双刀兵1" and npc_attackable(target) and target["life"] <= 1500:
            path = reachable_trap(resource, state["map"], 0, state["player"]["position"], with_path=True,
                                  destination=(target["position"]["x"], target["position"]["y"]))
            candidates.append((len(path), target["id"], path))
    if not candidates:
        raise AutomationError("The normal book source has no reachable suitable Anran target")
    _, target_id, path = min(candidates)
    for point in (*path[5:-2:6], path[-2]):
        go(client, *point, combat=False)
    target = next(row for row in client.observe()["targets"] if row["id"] == target_id and npc_attackable(row))
    if not target["visibleFromPlayer"]:
        raise AutomationError("Anran's normal target is behind an obstacle")
    fight(client, target, allow_melee_fallback=False)
    after = checkpoint(client, output, "anran-pure-skill-normal-kill-slot1")
    client.save_or_load(1)
    load_checkpoint(client, 1)
    reloaded = checkpoint(client, output, "anran-learned-and-kill-reloaded")
    for key in ("map", "variables", "inventory", "magic"):
        if after[key] != reloaded[key]:
            raise AutomationError(f"Anran save/reload differs: {key}")
    write_json(output / "anran-magic-proof.json", dict(status="passed", normalBookUsed=True,
               learnedFile="magic-黯然销魂掌.ini", pureSkillTarget=target, normalSaveReloadMatched=True,
               saveBytesEdited=False))


def linan_revenge_additions(client, output):
    state = idle(client)
    client.assign_magic(item(state, "player-magie-凤求凰.ini", "magic")["slot"], 0)
    client.save_or_load(0)
    phase = int(client.observe(("FromFengChi",))["variables"].get("FromFengChi") or 0)
    if state["map"] == "临安城.map":
        if phase == 14:
            if state["player"]["position"] == dict(x=153, y=309):
                go(client, 148, 299, script="临安城/first.txt", combat=False)
            go(client, 141, 139, script="临安城/trap9.txt", combat=False)
            verify(client, FromFengChi=15)
            go(client, 86, 165, script="临安城/trap12.txt", combat=False)
            verify(client, FromFengChi=16)
        if phase <= 16:
            merchant = mainline.late_target(client, "杂货摊贩", (133, 244))
            client.interact(merchant["id"])
            shop = client.wait_until(lambda value: "shop" in value, description="the normal mask merchant")
            mask = item(shop, "goods-sj-3-面具.ini", "shop")
            money = shop["player"]["money"]
            client.buy(mask["slot"])
            client.ui("Cancel")
            state = idle(client)
            checkpoint(client, output, "linan-mask-purchased")
            write_json(output / "linan-mask-fee-proof.json", dict(beforeMoney=money,
                       afterMoney=state["player"]["money"], actualFee=money-state["player"]["money"]))
            client.act("UseItem", generation=state["generation"], slot=item(state, "goods-sj-3-面具.ini")["slot"])
            verify(client, LinAnMianJu=1)
            go(client, 86, 165, script="临安城/trap12.txt", combat=False)
            verify(client, FromFengChi=17)
        phase = int(client.observe(("FromFengChi",))["variables"].get("FromFengChi") or 0)
        if phase == 17:
            for name in ("宋朝长刀手2", "宋朝长刀手3"):
                target = next((row for row in client.observe()["targets"] if row["name"] == name and npc_attackable(row)), None)
                if target:
                    fight(client, target, allow_melee_fallback=False)
            go(client, 67, 134, script="临安城/trap8.txt", combat=False)
            verify(client, FromFengChi=18)
        target = next((row for row in client.observe()["targets"] if row["name"] == "赵节" and npc_attackable(row)), None)
        if target:
            go(client, 53, 109)
            target = next((row for row in client.observe()["targets"] if row["id"] == target["id"] and npc_attackable(row)), None)
            if target:
                if not target["visibleFromPlayer"]:
                    raise AutomationError("Linan city Zhao is behind an obstacle")
                fight(client, target, allow_melee_fallback=False)
        late_records(output, "trace.jsonl", completed_scripts=("script/map/临安城/赵节die.txt",))
        go(client, 32, 130, destination="临安地下迷宫.map")
    state = checkpoint(client, output, "linan-underground-before-white-grant-slot1")
    if state["map"] != "临安地下迷宫.map":
        raise AutomationError("White's additions require the normal underground arrival")
    client.save_or_load(1)
    client.assign_magic(item(state, "player-magie-凤凰展翅.ini", "magic")["slot"], 0)
    resource = Path(json.loads((output / "run.json").read_text(encoding="utf-8"))["command"][2]) / RESOURCE_DIRECTORY
    path = reachable_trap(resource, state["map"], 0, state["player"]["position"], with_path=True, destination=(18, 147))
    for point in (*path[5:-1:6], path[-1]):
        go(client, *point)
    talk(client, "白煞")
    learned = checkpoint(client, output, "linan-white-granted-two-magics")
    for filename in ("player-magie-飞云决.ini", "惊涛拍浪.ini"):
        item(learned, filename, "magic")
    if any(row["name"] == "白煞" for row in learned["targets"]):
        raise AutomationError("White did not leave after his normal grants")
    late_records(output, "trace.jsonl", completed_scripts=("script/common/临安地下迷宫白煞.txt",))
    client.save_or_load(2)
    write_json(output / "linan-white-additions-proof.json", dict(status="grant-passed", normalWhiteLeft=True,
               grantedFiles=["player-magie-飞云决.ini", "惊涛拍浪.ini"], combatVerified=False, saveBytesEdited=False))


def linan_white_combat(client, output):
    before = idle(client)
    if before["map"] != "临安地下迷宫.map":
        raise AutomationError("White magic combat requires its normal underground source")
    resource = Path(json.loads((output / "run.json").read_text(encoding="utf-8"))["command"][2]) / RESOURCE_DIRECTORY
    boss = max((row for row in before["targets"] if row["name"] == "赵节" and npc_attackable(row)), key=lambda row: row["life"])
    proof = output / "linan-feyun-kill-proof.json"
    if proof.exists():
        target_id = json.loads(proof.read_text(encoding="utf-8"))["targetId"]
    else:
        assist_magic(client, output, "player-magie-飞云决.ini")
        state = idle(client)
        candidates = []
        for row in state["targets"]:
            if npc_attackable(row) and 0 < row["life"] <= 1200:
                occupied = {(actor["position"]["x"], actor["position"]["y"]) for actor in state["targets"]
                            if actor["kind"] == "npc" and actor["id"] != row["id"] and actor.get("action") not in (11, 255)}
                try:
                    path = reachable_trap(resource, state["map"], 0, state["player"]["position"], with_path=True,
                                          destination=(row["position"]["x"], row["position"]["y"]), avoid=occupied)
                except AutomationError as error:
                    if str(error).startswith("No connected trap"):
                        continue
                    raise
                candidates.append((len(path), row["id"], path))
        _, target_id, path = min(candidates)
        for point in (*path[5:-2:6], path[-2]):
            go(client, *point, combat=False)
        target = next(row for row in client.observe()["targets"] if row["id"] == target_id and npc_attackable(row))
        if not target["visibleFromPlayer"]:
            raise AutomationError("Feyun's normal target is behind an obstacle")
        fight(client, target, allow_melee_fallback=False)
        checkpoint(client, output, "linan-feyun-pure-skill-normal-kill")
        write_json(output / "linan-feyun-kill-proof.json", dict(status="passed", targetId=target_id, actualMagicFile="player-magie-飞云决.ini"))
    assist_magic(client, output, "惊涛拍浪.ini")
    state = idle(client)
    path = reachable_trap(resource, state["map"], 0, state["player"]["position"], with_path=True, destination=(35, 34))
    for point in (*path[5:-1:6], path[-1]):
        current = client.observe()
        visible = next((row for row in current["targets"] if row["id"] == boss["id"] and npc_attackable(row)), None)
        if visible and visible["visibleFromPlayer"]:
            break
        go(client, *point, combat=False)
    target = next(row for row in client.observe()["targets"] if row["id"] == boss["id"] and npc_attackable(row))
    if not target["visibleFromPlayer"]:
        raise AutomationError("Underground Zhao is behind an obstacle")
    crowded = checkpoint(client, output, "linan-jingtao-before-native-area-cast")
    client.act("CastSkill", generation=crowded["generation"], slot=0, targetId=target["id"])
    cleared = idle(client)
    defeated = [row for row in crowded["targets"] if npc_attackable(row) and not any(
                other["id"] == row["id"] and npc_attackable(other) for other in cleared["targets"])]
    write_json(output / "linan-jingtao-native-area-cast-proof.json", dict(normalCastSkill=True,
               originalTarget=target, normallyDefeatedNearbyActors=defeated))
    if cleared["player"]["position"] == dict(x=35, y=38):
        client.act("JumpTo", generation=cleared["generation"], x=35, y=35, timeoutMs=25000, timeout=30)
        checkpoint(client, output, "linan-native-jump-past-crowded-front")
    fight(client, target, allow_melee_fallback=False)
    verify(client, FromFengChi=19, ToZhongDu=1)
    late_records(output, "trace.jsonl", completed_scripts=("script/map/临安地下迷宫/赵节die.txt",))
    client.save_or_load(3)
    checkpoint(client, output, "linan-white-two-magics-and-zhao-complete-slot3")
    write_json(output / "linan-white-combat-proof.json", dict(status="passed", feyunNormalKill=target_id,
               jingtaoNormalBossKill=boss, completedBoundZhaoDeath=True, saveBytesEdited=False))


def meng_sparring(client, output, **_options):
    state = idle(client)
    if state["map"] == "汉阳.map" and (output / "side-meng-sparring-won.json").is_file():
        late_records(output, "trace.jsonl", completed_scripts=("汉阳小屋/die.txt",))
        return state
    if state["map"] == "汉阳.map":
        talk(client, "孟廷威", (66, 130))
        state = idle(client)
    if state["map"] != "汉阳小屋.map":
        raise AutomationError("Meng challenge requires its normal room")
    # This MOD has six same-name actors; NPC000 alone binds the victory script.
    resource = Path(json.loads((output / "run.json").read_text(encoding="utf-8"))["command"][2]) / RESOURCE_DIRECTORY
    binding = configparser.ConfigParser(interpolation=None)
    binding.read(resource / "ini/save/hanyanghouse.npc", encoding="utf-8-sig")
    actor = binding["NPC000"]
    if actor["Name"] != "孟廷威" or actor["DeathScript"] != "die.txt":
        raise AutomationError("Meng victory binding changed")
    position = dict(x=int(actor["MapX"]), y=int(actor["MapY"]))
    targets = [row for row in state["targets"] if row["name"] == actor["Name"]
               and row["position"] == position and npc_attackable(row)]
    if len(targets) != 1:
        raise AutomationError(f"Bound Meng actor is ambiguous: {targets}")
    client.save_or_load(0)
    checkpoint(client, output, "side-meng-bound-actor-before-normal-slot0")
    fight(client, targets[0], allow_melee_fallback=False)
    state = idle(client)
    if state["map"] != "汉阳.map" or state["player"]["position"] != dict(x=65, y=128) or state["player"]["life"] <= 0:
        raise AutomationError("Meng victory did not return alive to Hanyang")
    late_records(output, "trace.jsonl", completed_scripts=("汉阳小屋/die.txt",))
    write_json(output / "meng-bound-victory-proof.json", dict(status="passed", actualTarget=targets[0],
               templateSection="NPC000", completedDeathScript="汉阳小屋/die.txt", saveBytesEdited=False))
    return checkpoint(client, output, "side-meng-sparring-won")


def linan_return(client, output):
    client.assign_magic(item(idle(client), "惊涛拍浪.ini", "magic")["slot"], 0)
    previous = {name: getattr(mainline, name) for name in ("go", "fight", "checkpoint", "late_checkpoint", "restock", "meditate", "meng_sparring")}
    mainline.go, mainline.fight = go, fight
    mainline.meng_sparring = meng_sparring
    mainline.checkpoint = mainline.late_checkpoint = checkpoint
    if client.observe().get("cheatInvincibilityEnabled"):
        mainline.restock = lambda *_args, **_options: None
        mainline.meditate = lambda *_args, **_options: None
    try:
        state = idle(client)
        if state["map"] == "汉阳小屋.map":
            meng_sparring(client, output)
            mainline.hanyang_to_zhongdu(client, output)
        elif state["map"] in ("汉阳.map", "金兵营寨.map"):
            mainline.hanyang_to_zhongdu(client, output)
        else:
            mainline.linan_after_zhao(client, output)
    finally:
        for name, value in previous.items():
            setattr(mainline, name, value)
    client.save_or_load(2)
    checkpoint(client, output, "linan-returned-zhongdu-normal-slot2")


def tianren_entry(client, output):
    state = idle(client)
    if state["map"] != "中都.map":
        raise AutomationError("Tianren entry needs the normal return to Zhongdu")
    client.assign_magic(item(state, "player-magie-凤凰展翅.ini", "magic")["slot"], 0)
    previous_go, previous_floor, previous_checkpoint = mainline.go, mainline.tianren_dungeon_floors, mainline.checkpoint

    def entry_go(c, *coordinates, **options):
        value = go(c, *coordinates, **options)
        if options.get("destination") == "天忍教.map":
            c.save_or_load(0)
            checkpoint(c, output, "tianren-ground-arrival-normal-slot0")
        return value

    mainline.go, mainline.checkpoint = entry_go, checkpoint
    mainline.tianren_dungeon_floors = lambda *_args, **_options: None
    try:
        mainline.tianren_dungeons(client, output)
    finally:
        mainline.go, mainline.tianren_dungeon_floors, mainline.checkpoint = previous_go, previous_floor, previous_checkpoint
    client.save_or_load(1)
    checkpoint(client, output, "tianren-first-floor-arrival-normal-slot1")


def tianren_ground(client, output):
    before = checkpoint(client, output, "tianren-ground-bound-reward-before")
    if before["map"] != "天忍教.map":
        raise AutomationError("Ground reward requires its normal arrival source")
    client.assign_magic(item(before, "惊涛拍浪.ini", "magic")["slot"], 0)
    quantity = sum(row["quantity"] for row in before["inventory"] if row["file"] == "圣云玛瑙.ini")
    target = mainline.late_target(client, "剑客B", (41, 71))
    go(client, 41, 73, combat=False)
    target = next(row for row in client.observe()["targets"] if row["id"] == target["id"] and npc_attackable(row))
    if not target["visibleFromPlayer"]:
        raise AutomationError("Ground reward actor is behind an obstacle")
    fight(client, target, allow_melee_fallback=False)
    after = idle(client)
    if item(after, "圣云玛瑙.ini")["quantity"] != quantity + 1 or after["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Bound ground reward differs from one agate without a money change")
    late_records(output, "trace.jsonl", completed_scripts=("script/common/圣云玛瑙.txt",))
    client.save_or_load(2)
    saved = checkpoint(client, output, "tianren-ground-agate-normal-slot2")
    load_checkpoint(client, 2)
    reloaded = checkpoint(client, output, "tianren-ground-agate-reloaded")
    for key in ("map", "variables", "inventory", "magic"):
        if saved[key] != reloaded[key]:
            raise AutomationError(f"Ground reward save/reload differs: {key}")
    write_json(output / "tianren-ground-reward-proof.json", dict(status="passed", target=target,
               originalQuantity=quantity, newQuantity=quantity + 1, saveReloadMatched=True, saveBytesEdited=False))


def tianren_first(client, output):
    state = idle(client)
    if state["map"] != "天忍教-地下迷宫1.map":
        raise AutomationError("First floor needs its normal arrival source")
    client.assign_magic(item(state, "惊涛拍浪.ini", "magic")["slot"], 0)
    if not (output / "tianren-first-before-hammer-normal-slot0.json").exists():
        client.save_or_load(0)
        checkpoint(client, output, "tianren-first-before-hammer-normal-slot0")
    stage = int(client.observe(("TrjDxmgshijiang",))["variables"].get("TrjDxmgshijiang") or 0)
    if stage == 0:
        go(client, 75, 93)
        talk(client, "铁匠", (76, 93))
        verify(client, TrjDxmgshijiang=1)
        talk(client, "铁匠", (76, 93))
        verify(client, TrjDxmgshijiang=1)
        checkpoint(client, output, "tianren-smith-request-and-repeat")
    if stage != 2:
        target = mainline.late_target(client, "天忍教双斧教众1", (59, 33))
        go(client, 55, 44)
        if int(client.observe(("TrjDxmgshijiang",))["variables"].get("TrjDxmgshijiang") or 0) != 2:
            target = next(row for row in client.observe()["targets"] if row["id"] == target["id"] and npc_attackable(row))
            fight(client, target, allow_melee_fallback=False)
    verify(client, TrjDxmgshijiang=2)
    item(idle(client), "goods-sj-11-金刚锤.ini")
    late_records(output, "trace.jsonl", completed_scripts=("天忍教-地下迷宫1/蓝旗旗主.txt",))
    client.save_or_load(1)
    checkpoint(client, output, "tianren-hammer-granted-normal-slot1")
    go(client, 75, 93)
    before = idle(client)
    doors = [row for row in before["targets"] if row["kind"] == "object" and row["name"] == "大铁门1"]
    talk(client, "铁匠", (76, 93))
    after = idle(client)
    if (not doors or any(row["name"] == "大铁门1" for row in after["targets"])
            or any(row["file"] == "goods-sj-11-金刚锤.ini" for row in after["inventory"])):
        raise AutomationError("Smith did not consume the hammer and open the actual door")
    late_records(output, "trace.jsonl", completed_scripts=("天忍教-地下迷宫1/石匠.txt",))
    client.save_or_load(2)
    checkpoint(client, output, "tianren-smith-opened-door-normal-slot2")
    write_json(output / "tianren-first-hammer-proof.json", dict(status="passed", requestedAndRepeated=True,
               rewardGranted=True, hammerConsumed=True, originalDoors=doors, doorRemoved=True, saveBytesEdited=False))
    go(client, 75, 30, destination="天忍教-地下迷宫2.map")
    client.save_or_load(3)
    checkpoint(client, output, "tianren-second-arrival-normal-slot3")


def early_magic_combat(client, output):
    state = idle(client)
    if state["map"] != "主角家-狂沙镇.map":
        raise AutomationError("Early magic combat needs the normal village source")
    assist_battle(client, output, 60)
    resource = Path(json.loads((output / "run.json").read_text(encoding="utf-8"))["command"][2]) / RESOURCE_DIRECTORY
    results = []
    for filename in ("magic-孔雀翎.ini", "player-magic-怒雷指.ini", "player-magic-破雪狂攻.ini"):
        assist_magic(client, output, filename)
        state = idle(client)
        point = state["player"]["position"]
        targets = [row for row in state["targets"] if row["name"] == "灰狼" and npc_attackable(row)]
        target = min(targets, key=lambda row: abs(row["position"]["x"]-point["x"])*2 + abs(row["position"]["y"]-point["y"]))
        if not target["visibleFromPlayer"]:
            path = reachable_trap(resource, state["map"], 0, point, with_path=True,
                                  destination=(target["position"]["x"], target["position"]["y"] + 2))
            for step in (*path[5:-1:6], path[-1]):
                go(client, *step, combat=False, stop_when=lambda value: any(
                   row["id"] == target["id"] and row["visibleFromPlayer"] for row in value["targets"]))
                current = next(row for row in client.observe()["targets"] if row["id"] == target["id"])
                if current["visibleFromPlayer"]:
                    break
        before = checkpoint(client, output, "early-magic-before-" + filename)
        fight(client, target, allow_melee_fallback=False)
        after = checkpoint(client, output, "early-magic-after-" + filename)
        results.append(dict(file=filename, target=target, pureSkillNormalKill=True, beforePlayer=before["player"], afterPlayer=after["player"]))
    client.save_or_load(2)
    write_json(output / "early-magic-combat-proof.json", dict(status="passed", results=results, saveBytesEdited=False))


def tianren_second(client, output):
    state = idle(client)
    if state["map"] != "天忍教-地下迷宫2.map":
        raise AutomationError("Second floor needs its normal arrival source")
    client.assign_magic(item(state, "惊涛拍浪.ini", "magic")["slot"], 0)
    boss_source = output / "tianren-second-bound-boss-source.json"
    if not boss_source.exists():
        checkpoint(client, output, "tianren-second-bound-boss-source")
    source = json.loads(boss_source.read_text(encoding="utf-8"))
    bosses = [row for row in source["targets"] if row["name"] == "金国狼牙棒兵1" and row["position"] == dict(x=15, y=45)]
    if len(bosses) != 1 or source["generation"] != state["generation"]:
        raise AutomationError("Second-floor boss identity changed")
    switches = []
    previous = {name: getattr(mainline, name) for name in ("go", "fight", "checkpoint", "late_checkpoint", "meditate", "late_object", "late_target")}

    def switch(c, x, y):
        before = idle(c)
        target = next(row for row in before["targets"] if row["kind"] == "object" and row["position"] == dict(x=x, y=y))
        after = previous["late_object"](c, x, y)
        reject(c, "Interact", "action_rejected", generation=after["generation"], targetId=target["id"])
        repeated = c.observe(("TrjDxmgKg",))
        if repeated["inventory"] != before["inventory"] or repeated["player"]["money"] != before["player"]["money"]:
            raise AutomationError("A switch changed goods or money")
        count = int(repeated["variables"].get("TrjDxmgKg") or 0)
        if count < 4 and not any(row["name"] == "铁门关1" for row in repeated["targets"]):
            raise AutomationError("Second-floor door opened before the fourth switch")
        if count == 4 and any(row["name"] == "铁门关1" for row in repeated["targets"]):
            raise AutomationError("Second-floor fourth switch did not remove the door")
        switches.append(dict(position=target["position"], actualId=target["id"], countAfter=count, repeatRejected=True))
        checkpoint(c, output, f"tianren-second-switch-{count}")
        write_json(output / "tianren-second-switches.json", switches)
        return after

    def target(c, name, position=None, **options):
        if name == "金国狼牙棒兵1" and position == (15, 45):
            return next(row for row in c.observe()["targets"] if row["id"] == bosses[0]["id"] and npc_attackable(row))
        return previous["late_target"](c, name, position, **options)

    mainline.go, mainline.fight = go, lambda c, actor, **_options: fight(c, actor, allow_melee_fallback=False)
    mainline.checkpoint = mainline.late_checkpoint = checkpoint
    mainline.late_object, mainline.late_target = switch, target
    if state.get("cheatInvincibilityEnabled"):
        mainline.meditate = lambda *_args, **_options: None
    try:
        mainline.tianren_second_floor(client, output)
    finally:
        for name, value in previous.items():
            setattr(mainline, name, value)
    late_records(output, "trace.jsonl", completed_scripts=("天忍教-地下迷宫2/地图陷阱3.txt", "天忍教-地下迷宫2/die.txt"))
    client.save_or_load(1)
    checkpoint(client, output, "tianren-third-arrival-normal-slot1")
    write_json(output / "tianren-second-proof.json", dict(status="passed", originalBoss=bosses[0],
               fourSwitchesCompleted=True, normalBossDeathCompleted=True, saveBytesEdited=False))


def tianren_third(client, output):
    state = idle(client)
    if state["map"] != "天忍教-地下迷宫3.map":
        raise AutomationError("Third-floor additions need their normal arrival source")
    client.assign_magic(item(state, "惊涛拍浪.ini", "magic")["slot"], 0)
    source_path = output / "tianren-third-additions-source-normal-slot0.json"
    if not source_path.exists():
        client.save_or_load(0)
        checkpoint(client, output, "tianren-third-additions-source-normal-slot0")
    source = json.loads(source_path.read_text(encoding="utf-8"))
    if source["generation"] != state["generation"]:
        raise AutomationError("Third-floor actor identities changed")
    # This actor walks toward the arrival point before menus close; its authored life is unique.
    fake = next(row for row in source["targets"] if row["name"] == "剑客B" and row["life"] == 1730000)
    go(client, 56, 129)
    target = next((row for row in client.observe()["targets"] if row["id"] == fake["id"] and npc_attackable(row)), None)
    if target:
        if not target["visibleFromPlayer"]:
            raise AutomationError("The fake companion is behind an obstacle")
        fight(client, target, allow_melee_fallback=False)
    late_records(output, "trace.jsonl", completed_scripts=("script/common/天忍教假的伙伴.txt",))
    checkpoint(client, output, "tianren-fake-companion-death-dialogue-complete")
    go(client, 67, 128)
    before = idle(client)
    original_quantity = sum(row["quantity"] for row in before["inventory"] if row["file"] == "情丝斩.ini")
    mainline.late_object(client, 67, 126)
    after = idle(client)
    if item(after, "情丝斩.ini")["quantity"] != original_quantity + 1 or after["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Changed chest did not grant one Qingsi crystal")
    box = next(row for row in after["targets"] if row["kind"] == "object" and row["position"] == dict(x=67, y=126))
    reject(client, "Interact", "action_rejected", generation=after["generation"], targetId=box["id"])
    late_records(output, "trace.jsonl", completed_scripts=("script/common/情丝斩箱子.txt",))
    client.save_or_load(1)
    checkpoint(client, output, "tianren-fake-and-changed-chest-normal-slot1")
    go(client, 68, 131)
    before_shop = idle(client)
    merchant = mainline.late_target(client, "摊贩摆卖1", (68, 129))
    client.interact(merchant["id"])
    shop = client.wait_until(lambda value: "shop" in value, timeout=120, description="Tianren's bound special shop")
    checkpoint(client, output, "tianren-special-shop-59-items")
    if len(shop["shop"]) != 59:
        raise AutomationError("Tianren shop did not expose its 59 declared items")
    purchases = []
    for filename, cost in (("铁血丹心秘籍.ini", 10000), ("逍遥江湖情之书.ini", 3000),
                           ("回城之书.ini", 1800), ("永恒.ini", 1350)):
        before_buy = client.observe()
        quantity = sum(row["quantity"] for row in before_buy["inventory"] if row["file"] == filename)
        client.buy(item(before_buy, filename, "shop")["slot"])
        after_buy = client.observe()
        if item(after_buy, filename)["quantity"] != quantity + 1 or after_buy["player"]["money"] != before_buy["player"]["money"] - cost:
            raise AutomationError(f"Special shop purchase differs: {filename}")
        purchases.append(dict(file=filename, cost=cost, beforeMoney=before_buy["player"]["money"], afterMoney=after_buy["player"]["money"]))
    client.ui("Cancel")
    sale = client.wait_until(lambda value: "shop" in value, timeout=10, description="following normal sale pane")
    checkpoint(client, output, "tianren-special-following-sale")
    spare = next(row for row in sale["inventory"] if row["file"] == "goods-yaowu-1-凝神丹.ini")
    client.sell(spare["slot"])
    sold = client.observe()
    if (sum(row["quantity"] for row in sold["inventory"] if row["file"] == spare["file"]) != spare["quantity"] - 1
            or sold["player"]["money"] != sale["player"]["money"] + 70):
        raise AutomationError("The following sale did not consume one medicine and pay money")
    client.ui("Cancel")
    state = idle(client)
    late_records(output, "trace.jsonl", completed_scripts=("script/common/天忍教和尚.txt",))
    client.act("UseItem", generation=state["generation"], slot=item(state, "铁血丹心秘籍.ini")["slot"])
    item(idle(client), "player-magic-铁血丹心.ini", "magic")
    late_records(output, "trace.jsonl", completed_scripts=("script/common/习得铁血丹心.txt",))
    client.save_or_load(2)
    saved = checkpoint(client, output, "tianren-shop-and-wangling-learned-normal-slot2")
    load_checkpoint(client, 2)
    reloaded = checkpoint(client, output, "tianren-third-additions-reloaded")
    for key in ("map", "variables", "inventory", "magic"):
        if saved[key] != reloaded[key]:
            raise AutomationError(f"Third-floor additions save/reload differs: {key}")
    write_json(output / "tianren-third-additions-proof.json", dict(status="passed", fakeCompanion=fake,
               qingsiCrystalQuantityBefore=original_quantity, qingsiCrystalQuantityAfter=original_quantity+1,
               purchases=purchases, saleMoneyBefore=sale["player"]["money"], saleMoneyAfter=sold["player"]["money"],
               normalSaveReloadMatched=True, saveBytesEdited=False))


def wangling_combat(client, output):
    state = idle(client)
    if state["map"] != "主角家-狂沙镇.map":
        raise AutomationError("Wangling combat needs the normally refreshed village source")
    assist_magic(client, output, "player-magic-铁血丹心.ini")
    state = idle(client)
    resource = Path(json.loads((output / "run.json").read_text(encoding="utf-8"))["command"][2]) / RESOURCE_DIRECTORY
    paths = []
    for target in state["targets"]:
        if target["name"] != "灰狼" or not npc_attackable(target):
            continue
        for offset in ((0, 2), (1, 1), (-1, 1)):
            try:
                path = reachable_trap(resource, state["map"], 0, state["player"]["position"], with_path=True,
                                      destination=(target["position"]["x"] + offset[0], target["position"]["y"] + offset[1]))
                paths.append((len(path), target["id"], path))
            except AutomationError:
                pass
    if not paths:
        raise AutomationError("There is no connected normal Wangling target")
    _, actor_id, path = min(paths)
    def close_visible(value):
        point = value["player"]["position"]
        return any(row["id"] == actor_id and npc_attackable(row) and row["visibleFromPlayer"]
                   and abs(row["position"]["x"] - point["x"]) * 2 + abs(row["position"]["y"] - point["y"]) <= 12
                   for row in value["targets"])
    for point in (*path[5:-1:6], path[-1]):
        if close_visible(client.observe()):
            break
        go(client, *point, combat=False, stop_when=close_visible)
    target = next(row for row in client.observe()["targets"] if row["id"] == actor_id and npc_attackable(row))
    if not close_visible(client.observe()):
        raise AutomationError("Wangling's current target is distant or behind an obstacle")
    before = checkpoint(client, output, "wangling-pure-skill-before")
    fight(client, target, allow_melee_fallback=False)
    client.wait_until(lambda value: value["player"]["action"] in (0, 1, 20), timeout=10,
                      description="normal spell animation completion before saving")
    # The spiral projectiles and death animations can still award experience after the requested kill.
    previous = client.observe(VARIABLES)
    quiet_since = time.monotonic()
    deadline = quiet_since + 60
    while time.monotonic() < deadline:
        time.sleep(0.2)
        current = client.observe(VARIABLES)
        if any(current[key] != previous[key] for key in ("magic", "inventory", "variables")):
            quiet_since = time.monotonic()
        if time.monotonic() - quiet_since >= 4:
            break
        previous = current
    else:
        raise TimeoutError("Normal combat rewards did not settle before saving")
    after = checkpoint(client, output, "wangling-pure-skill-normal-kill")
    client.save_or_load(2)
    saved = checkpoint(client, output, "wangling-kill-normal-save-complete")
    load_checkpoint(client, 2)
    reloaded = checkpoint(client, output, "wangling-kill-reloaded")
    for key in ("map", "variables", "inventory", "magic"):
        if saved[key] != reloaded[key]:
            raise AutomationError(f"Wangling save/reload differs: {key}")
    write_json(output / "wangling-combat-proof.json", dict(status="passed", target=target,
               beforePlayer=before["player"], afterPlayer=after["player"], pureSkillNormalKill=True,
               normalSaveReloadMatched=True, saveBytesEdited=False))


def manjianghong_self(client, output):
    state = idle(client)
    if any(row["kind"] == "npc" for row in state["targets"]):
        raise AutomationError("The self-magic cost test requires the normal empty return-item map")
    assist_battle(client, output, state["player"]["level"], invincible=False)
    assist_magic(client, output, "player-magic-满江红.ini")
    client.assign_magic(item(client.observe(), "player-magic-杀意.ini", "magic")["slot"], 1)
    before_cost = checkpoint(client, output, "manjianghong-before-natural-mana-cost")
    client.act("CastSkill", generation=before_cost["generation"], slot=1)
    client.wait_until(lambda value: value["player"]["mana"] < before_cost["player"]["mana"], description="normal self-spell mana cost")
    before = checkpoint(client, output, "manjianghong-before-self-cast")
    client.act("CastSkill", generation=before["generation"], slot=0)
    client.wait_until(lambda value: value["player"]["life"] == before["player"]["life"] - 1500,
                      timeout=10, description="Manjianghong's actual self-life cost")
    after = checkpoint(client, output, "manjianghong-after-self-cast")
    if after["player"]["mana"] != min(before["player"]["manaMax"], before["player"]["mana"] + 240):
        raise AutomationError("Manjianghong's original negative mana cost did not restore mana")
    if before["variables"] != after["variables"] or before["inventory"] != after["inventory"] or before["player"]["money"] != after["player"]["money"]:
        raise AutomationError("Self magic changed plot, inventory or money")
    client.save_or_load(2)
    write_json(output / "manjianghong-self-proof.json", dict(status="passed", beforePlayer=before["player"],
               afterPlayer=after["player"], normalSelfLifeCost=1500, manaRestoreUpTo240=True,
               invincibilityDisabled=True, saveBytesEdited=False))


def tianren_circle_comparison(client, output):
    state = idle(client)
    if state["map"] != "天忍教-地下迷宫3.map":
        raise AutomationError("Circle comparison needs the normal final battle source")
    go(client, 21, 56, combat=False)
    client.save_or_load(0)
    checkpoint(client, output, "circle-comparison-normal-source-slot0")
    results = []
    for filename in ("惊涛拍浪.ini", "player-magie-凤凰展翅.ini"):
        load_checkpoint(client, 0)
        client.assign_magic(item(client.observe(), filename, "magic")["slot"], 0)
        before = checkpoint(client, output, "circle-comparison-before-" + filename)
        boss = next(row for row in before["targets"] if row["name"] == "完颜宏烈" and row["life"] == 12999999)
        started = time.monotonic()
        action = client.submit("StartCombat", generation=before["generation"], targetId=boss["id"],
                               radius=20, kills=1, skills=[0], allowMeleeFallback=False, timeoutMs=90000)
        while time.monotonic() - started < 30:
            status = client.request("GetActionStatus", actionId=action)
            if status["status"] != "running":
                raise AutomationError(f"Efficiency sample stopped early: {status}")
            time.sleep(0.2)
        client.request("CancelAction", actionId=action)
        after = checkpoint(client, output, "circle-comparison-after-" + filename)
        remaining = next(row["life"] for row in after["targets"] if row["id"] == boss["id"])
        damage = boss["life"] - remaining
        if damage <= 0:
            raise AutomationError(f"Efficiency sample did not damage the real boss: {filename}")
        others = {row["id"]: row.get("life", 0) for row in before["targets"] if row["kind"] == "npc" and row["id"] != boss["id"]}
        after_life = {row["id"]: row.get("life", 0) for row in after["targets"] if row["kind"] == "npc"}
        results.append(dict(file=filename, originalBoss=boss, seconds=time.monotonic()-started,
                            bossLifeAfter=remaining, actualBossDamage=damage,
                            otherActorsDamaged=sum(after_life.get(actor, 0) < life for actor, life in others.items()),
                            otherActorsKilled=sum(life > 0 and after_life.get(actor, 0) <= 0 for actor, life in others.items())))
        write_json(output / "circle-comparison-results.json", results)
    write_json(output / "circle-comparison-proof.json", dict(status="passed", results=results,
               sameNormalSaveForBoth=True, saveBytesEdited=False))


def return_items(client, output):
    destinations = (
        ("逍遥江湖情之书.ini", 3000, "式样的.txt", "主角家.map", (16, 18)),
        ("轮回寒玉.ini", 1950, "式样沙洲.txt", "主角家-狂沙镇.map", (110, 91)),
        ("蓝晶.ini", 1900, "式样沙洲边缘.txt", "主角家-狂沙镇.map", (78, 263)),
        ("回城之书.ini", 1800, "式样狂沙镇.txt", "狂沙镇.map", (147, 278)),
        ("永恒.ini", 1350, "式样龙门客栈.txt", "龙门客栈.map", (16, 15)),
        ("长安珍.ini", 1250, "式样长安.txt", "长安.map", (35, 72)),
        ("紫水晶.ini", 1000, "式样别离村.txt", "别离村.map", (44, 51)),
        ("汉阳玛瑙.ini", 400, "式样汉阳.txt", "汉阳.map", (45, 14)),
        ("中都血石.ini", 600, "式样中都.txt", "中都.map", (75, 169)),
    )
    state = idle(client)
    if state["map"] != "天忍教-地下迷宫3.map":
        raise AutomationError("Return items need the normal third-floor shop source")
    missing = [(filename, cost) for filename, cost, *_ in destinations
               if not any(row["file"] == filename for row in state["inventory"])]
    purchases = []
    if missing:
        merchant = mainline.late_target(client, "摊贩摆卖1", (68, 129))
        client.interact(merchant["id"])
        client.wait_until(lambda value: "shop" in value, description="return-item shop")
        for filename, cost in missing:
            before = client.observe()
            client.buy(item(before, filename, "shop")["slot"])
            after = client.observe()
            if item(after, filename)["quantity"] != 1 or after["player"]["money"] != before["player"]["money"] - cost:
                raise AutomationError(f"Return-item purchase differs: {filename}")
            purchases.append(dict(file=filename, cost=cost, beforeMoney=before["player"]["money"], afterMoney=after["player"]["money"]))
        client.ui("Cancel")
        client.wait_until(lambda value: "shop" in value, description="following sale pane")
        client.ui("Cancel")
    idle(client)
    client.save_or_load(0)
    checkpoint(client, output, "return-items-normal-branch-source-slot0")
    results = []
    for filename, _, script, map_name, position in destinations:
        load_checkpoint(client, 0)
        before = checkpoint(client, output, "return-item-before-" + filename)
        action = client.submit("UseItem", generation=before["generation"], slot=item(before, filename)["slot"])
        videos = []
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            state = client.observe()
            if state.get("video"):
                videos.append(state["video"])
                checkpoint(client, output, "return-item-video-" + filename)
                client.ui("Cancel")
            status = client.request("GetActionStatus", actionId=action)
            if status["status"] in ("failed", "cancelled"):
                raise AutomationError(f"Return-item action failed: {status}")
            if status["status"] == "succeeded" and state["map"] == map_name and state.get("worldInput") and not state.get("script"):
                break
            time.sleep(0.1)
        else:
            raise TimeoutError(f"Return-item continuation did not finish: {filename}")
        arrived = checkpoint(client, output, "return-item-arrived-" + filename)
        if (arrived["player"]["position"] != dict(zip(("x", "y"), position))
                or item(arrived, filename)["quantity"] != item(before, filename)["quantity"]
                or arrived["player"]["money"] != before["player"]["money"]):
            raise AutomationError(f"Return-item destination, quantity or money differs: {filename}")
        late_records(output, "trace.jsonl", completed_scripts=("script/common/" + script,))
        continuation = (44, 15) if map_name == "汉阳.map" else (position[0], position[1] + 2)
        go(client, *continuation, combat=False)
        client.save_or_load(3)
        saved = checkpoint(client, output, "return-item-continued-" + filename)
        load_checkpoint(client, 3)
        reloaded = checkpoint(client, output, "return-item-reloaded-" + filename)
        for key in ("map", "variables", "inventory", "magic"):
            if saved[key] != reloaded[key]:
                raise AutomationError(f"Return-item save/reload differs: {filename} / {key}")
        refreshed_count = None
        if filename == "逍遥江湖情之书.ini":
            go(client, 14, 87, destination="主角家-狂沙镇.map", script="主角家/trap-2.txt", combat=False)
            refreshed = checkpoint(client, output, "return-home-normal-exit-restores-village-actors")
            refreshed_count = sum(row["kind"] == "npc" for row in refreshed["targets"])
            if refreshed_count <= 0:
                raise AutomationError("Normal home exit did not restore the saved village actors")
            client.save_or_load(4)
        results.append(dict(file=filename, boundScript=script, actualMap=map_name, actualPosition=position,
                            actualVideos=videos, normalMovementContinued=True, normalSaveReloadMatched=True,
                            actorCount=sum(row["kind"] == "npc" for row in arrived["targets"]),
                            normalHomeExitRefreshedActorCount=refreshed_count))
        write_json(output / "return-items-results.json", results)
    write_json(output / "return-items-proof.json", dict(status="passed", purchases=purchases,
               results=results, saveBytesEdited=False))


def tianren_final(client, output):
    state = idle(client)
    if state["map"] != "天忍教-地下迷宫3.map":
        raise AutomationError("Finale needs the normal third-floor source")
    client.assign_magic(item(state, "player-magie-凤凰展翅.ini", "magic")["slot"], 0)
    bosses = [row for row in state["targets"] if row["name"] == "完颜宏烈" and row["position"] == dict(x=21, y=53)]
    if len(bosses) != 1:
        raise AutomationError("Final bound actor is ambiguous")
    boss = bosses[0]
    client.save_or_load(0)
    checkpoint(client, output, "tianren-final-before-intro-normal-slot0")
    go(client, 31, 75, script="天忍教-地下迷宫3/地图陷阱3.txt", combat=False)
    client.save_or_load(1)
    checkpoint(client, output, "tianren-final-after-intro-normal-slot1")
    go(client, 21, 56, combat=False)
    videos, attempts, special_actors = set(), [], []
    action = None
    deadline = time.monotonic() + 900
    while time.monotonic() < deadline:
        state = client.observe()
        if state["scene"] == "Title":
            raise AutomationError("Xinyue's bound victory unexpectedly returned to title")
        if state.get("video"):
            filename = state["video"].replace("\\", "/").split("/")[-1].lower()
            videos.add(filename)
            checkpoint(client, output, "tianren-final-video-" + filename)
            client.ui("Cancel")
        if "大结局" in state.get("script", ""):
            actors = [row for row in state.get("targets", []) if row["kind"] == "npc" and row["name"] == "完颜宏烈"]
            special = [row for row in actors if row["action"] == 12]
            if special:
                if len(actors) != 1 or special[0]["position"] != dict(x=21, y=52):
                    raise AutomationError("Final special animation selected a residual duplicate actor")
                if not special_actors:
                    checkpoint(client, output, "tianren-final-unique-dying-actor-special-animation")
                special_actors.append(special[0])
        if action is not None:
            status = client.request("GetActionStatus", actionId=action)
            if status["status"] != "running":
                attempts[-1]["result"] = status
                target = next((row for row in state.get("targets", []) if row["id"] == boss["id"]), None)
                attempts[-1]["lifeAfter"] = target["life"] if target else 0
                write_json(output / "tianren-final-combat-attempts.json", attempts)
                if status["status"] == "succeeded" and status.get("kills") == 1:
                    action = None
                elif (status.get("reason") == "action_timeout" and target and npc_attackable(target)
                      and target["life"] < attempts[-1]["lifeBefore"] and len(attempts) < 4):
                    action = None
                else:
                    raise AutomationError(f"Final combat stopped without a normal kill or progress: {status}")
        target = next((row for row in state.get("targets", []) if row["id"] == boss["id"] and npc_attackable(row)), None)
        if action is None and target and state.get("worldInput"):
            action = client.submit("StartCombat", generation=state["generation"], targetId=boss["id"],
                                   radius=20, kills=1, skills=[0], allowMeleeFallback=False, timeoutMs=240000)
            attempts.append(dict(actionId=action, targetId=boss["id"], lifeBefore=target["life"], magic="player-magie-凤凰展翅.ini", level=10))
        if state.get("map") == "主角家-狂沙镇.map" and state.get("worldInput") and not state.get("script"):
            break
        time.sleep(0.1)
    else:
        raise TimeoutError("Final victory and its post-movie continuation did not finish")
    if videos != {"happyend.avi", "begin.avi"} or not any(row.get("result", {}).get("kills") == 1 for row in attempts):
        raise AutomationError(f"Finale lacks the actual normal death or both videos: {videos}")
    if not special_actors:
        raise AutomationError("Finale lacks observation of its unique dying actor's special animation")
    records = late_records(output, "trace.jsonl", completed_scripts=("天忍教-地下迷宫3/大结局.txt",))
    start = mainline.late_completed_script(records, "天忍教-地下迷宫3/大结局.txt")
    calls = [row for row in records if row.get("executionId") == start["executionId"] and row.get("eventType") == "api.call"]
    names = [row["apiName"] for row in calls]
    movies = [index for index, name in enumerate(names) if name == "playmovie"]
    if (len(movies) != 2 or not movies[1] < names.index("setlevelfile") < names.index("loadmap") < names.index("enablefight")
            or "returntotitle" in names or state["player"]["levelFile"] != "level-hard.ini"
            or state["player"]["position"] != dict(x=140, y=55) or not state["player"]["canFight"]):
        raise AutomationError("Post-movie API order, hard levels or playable village differs")
    client.save_or_load(2)
    checkpoint(client, output, "tianren-final-village-after-movies-normal-slot2")
    go(client, 140, 57, combat=False)
    continued = idle(client)
    if continued["player"]["position"] == state["player"]["position"]:
        raise AutomationError("The post-movie village did not accept normal movement")
    client.save_or_load(3)
    saved = checkpoint(client, output, "tianren-final-continued-normal-slot3")
    load_checkpoint(client, 3)
    reloaded = checkpoint(client, output, "tianren-final-continuation-reloaded")
    for key in ("map", "variables", "inventory", "magic"):
        if saved[key] != reloaded[key]:
            raise AutomationError(f"Final continuation save/reload differs: {key}")
    if reloaded["player"]["levelFile"] != "level-hard.ini" or not reloaded["player"]["canFight"]:
        raise AutomationError("Final continuation lost hard levels or fighting after reload")
    write_json(output / "tianren-final-proof.json", dict(status="passed", originalBoss=boss, actualVideos=sorted(videos),
               uniqueDyingActorSpecialAnimation=special_actors[0], specialAnimationSamples=len(special_actors),
               completedBoundEnding=True, postMovieMap=state["map"], postMoviePosition=state["player"]["position"],
               postMovieHardLevelFile=True, normalMovementContinued=True, normalSaveReloadMatched=True,
               endingCalls=calls, saveBytesEdited=False))


def tianren_final_loss(client, output):
    before = idle(client)
    if before["map"] != "天忍教-地下迷宫3.map":
        raise AutomationError("Final loss requires its normal battle source")
    assist_battle(client, output, before["player"]["level"], invincible=False)
    videos = []
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        state = client.observe()
        if state.get("video"):
            videos.append(state["video"])
            checkpoint(client, output, "tianren-final-loss-video")
            client.ui("Cancel")
        if state["scene"] == "Title":
            break
        time.sleep(0.1)
    else:
        raise TimeoutError("Natural final defeat did not reach title")
    late_records(output, "trace.jsonl", completed_scripts=("script/common/主角死亡.txt",))
    checkpoint(client, output, "tianren-final-natural-death-title")
    write_json(output / "tianren-final-loss-proof.json", dict(status="passed", beforePlayer=before["player"],
               naturalEnemyAttacks=True, actualVideos=videos, actualTitleReached=True, saveBytesEdited=False))


def assist_magic(client, output, filename, level=10):
    before = checkpoint(client, output, "native-magic-before")
    learned = item(before, filename, "magic")
    client.save_or_load(6)
    checkpoint(client, output, "native-magic-source-slot6")
    client.assign_practice(learned["slot"])
    client.open_menu("System")
    client.activate("options")
    client.activate("cheat-settings")
    if not client.observe().get("cheatModeEnabled"):
        client.activate("cheat-mode")
    while item(client.observe(), filename, "magic")["level"] < level:
        previous = item(client.observe(), filename, "magic")["level"]
        client.activate("increase-magic-level")
        if item(client.observe(), filename, "magic")["level"] <= previous:
            raise AutomationError("Native magic upgrade made no progress")
    for _ in range(5):
        if client.observe().get("worldInput"):
            break
        client.ui("Cancel")
    client.assign_magic(item(client.observe(), filename, "magic")["slot"], 0)
    after = client.observe(VARIABLES)
    if (after["variables"] != before["variables"] or after["inventory"] != before["inventory"]
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Native magic upgrade changed story variables, inventory or money")
    identity = json.loads((output / "run.json").read_text(encoding="utf-8"))
    identity["cheatAssisted"] = True
    identity["assistanceRecords"].append(dict(source="native-options-menu", kind="learned-magic-upgrade",
            file=filename, beforeLevel=learned["level"], afterLevel=level, quickSlot=0,
            normalSourceSlot=6, startedAtMap=before["map"]))
    write_json(output / "run.json", identity)
    after = checkpoint(client, output, "native-magic-after")
    write_json(output / f"native-magic-proof-{int(time.time())}.json", dict(status="passed",
            beforeMagic=before["magic"], afterMagic=after["magic"], plotVariablesUnchanged=True,
            inventoryUnchanged=True, moneyUnchanged=True, saveBytesEdited=False))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--assets", type=Path, default=Path("assets"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--route", choices=("inventory", "opening", "home-gate", "home-extras", "easy", "hard", "wolves", "desert-skills", "town", "rescue", "longmen", "changan-entry", "changan-story", "fengxue", "bieli-additions", "tangmen", "tangmen-loss", "cuiyan-second", "tianwang", "tianwang-wood", "to-zhongdu", "zhongdu-additions", "zhongdu-books", "zhongdu-shaolin", "zhongdu-night", "zhongdu-night-rewards", "to-linan", "linan-additions", "fengchi-additions", "pilitang", "pilitang-restored-swords", "tournament-entry", "tournament", "tournament-loss", "fengchi-night", "prison-approach", "prison-companions", "linan-revenge-additions", "anran-magic", "linan-white-combat", "linan-return", "tianren-entry", "tianren-ground", "tianren-first", "early-magic-combat", "tianren-second", "tianren-third", "return-items", "wangling-combat", "manjianghong-self", "tianren-circle", "tianren-final", "tianren-final-loss", "observe", "exit"), default="opening")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--source-run", type=Path)
    parser.add_argument("--load-slot", type=int, choices=range(7))
    parser.add_argument("--assist-level", type=int)
    parser.add_argument("--expected-deaths", choices=("none", "all", "zhao", "qiu", "cai", "tang"))
    args = parser.parse_args()
    if args.expected_deaths is not None and args.route != "prison-companions":
        parser.error("Expected companion deaths only apply to the isolated prison outcome route")
    if args.assist_level is not None and (args.resume or args.route not in ("wolves", "desert-skills", "fengxue", "cuiyan-second")
                                         or not args.source_run or not 9 <= args.assist_level <= 80):
        parser.error("Assistance needs a fresh isolated battle source and level9..80")
    sys.stdout.reconfigure(encoding="utf-8")
    output, assets = args.output.resolve(), args.assets.resolve()
    resource = assets / RESOURCE_DIRECTORY
    if args.route == "inventory":
        output.mkdir(parents=True, exist_ok=False)
        catalog = source_candidates(assets)
        write_json(output / "source-candidates.json", catalog)
        print(json.dumps(catalog["counts"], ensure_ascii=False))
        return
    if args.resume:
        identity = json.loads((output / "run.json").read_text(encoding="utf-8"))
        if identity["resourceId"] != RESOURCE_ID:
            parser.error("Resume requires a Xinyue run")
    else:
        if not args.exe or args.source_run and args.load_slot is None:
            parser.error("Fresh run requires --exe; source run requires --load-slot")
        output.mkdir(parents=True, exist_ok=False)
        parent_identity = {}
        if args.source_run:
            parent = args.source_run.resolve()
            parent_identity = json.loads((parent / "run.json").read_text(encoding="utf-8"))
            if parent_identity["resourceId"] != RESOURCE_ID:
                parser.error("Source must be a Xinyue save")
            relative_slot = Path(SAVE_NAMESPACE) / f"rpg{args.load_slot + 1}"
            source_slot = parent / "user-data/save" / relative_slot
            source_before = save_hashes(source_slot)
            if "game.ini" not in source_before:
                parser.error("Selected normal manual slot is absent")
            shutil.copytree(parent / "user-data/save", output / "user-data/save")
            unchanged = source_before == save_hashes(output / "user-data/save" / relative_slot) == save_hashes(source_slot)
            write_json(output / "normal-save-clone.json", dict(parentRun=str(parent), sourceSlot=args.load_slot,
                       fileHashes=source_before, sourceBeforeEqualsCloneEqualsSourceAfter=unchanged, saveBytesEdited=False))
            if not unchanged:
                parser.error("Source changed or clone bytes differ")
        executable = args.exe.resolve()
        session = "xinyue-" + str(uuid.uuid4())
        command = [str(executable), "--assets", str(assets), "--resource-id", RESOURCE_ID,
                   "--skip-startup-video", "--enable-automation-hooks", "--automation-pipe", session,
                   "--user-data-root", str(output / "user-data"), "--log-file", str(output / "game.log")]
        with (output / "stdout.log").open("wb") as stdout, (output / "stderr.log").open("wb") as stderr:
            process = subprocess.Popen(command, cwd=executable.parent, stdout=stdout, stderr=stderr)
        identity = dict(resourceId=RESOURCE_ID, session=session, pid=process.pid, command=command,
                        engineSha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                        resourceProfileSha256=hashlib.sha256((resource / "game_profile.ini").read_bytes()).hexdigest(),
                        cheatAssisted=bool(args.assist_level or parent_identity.get("cheatAssisted", False)),
                        assistanceRecords=parent_identity.get("assistanceRecords", []), started=time.time())
        if parent_identity:
            identity.update(parentRun=str(args.source_run.resolve()), parentSlot=args.load_slot)
        write_json(output / "run.json", identity)
    started = time.time()
    snapshot = output / f"route-source-{int(started * 1000000)}"
    snapshot.mkdir()
    for filename in ("run_xinyue_gameplay.py", "run_chenghe_gameplay.py", "run_jxqy2_mainline.py",
                     "run_jxqy2_gameplay_smoke.py", "gameplay_automation.py", "run_yycs_gameplay.py",
                     "jxqy2_sidequests.py", "jxqy2_mainline_supplies.py"):
        shutil.copy2(Path(__file__).with_name(filename), snapshot / filename)
    result = dict(route=args.route, status="running", started=started, resourceId=RESOURCE_ID,
                  fullCoverage=False, fullPlaythrough=False, cheatAssisted=identity["cheatAssisted"],
                  resumed=args.resume, loadSlot=args.load_slot, sourceSnapshot=str(snapshot))
    with Client(identity["session"], timeout=25, transcript=output / "commands.jsonl") as client:
        try:
            state = client.observe()
            if any(row["name"] == "return-to-title" for row in state.get("ui", [])):
                client.ui("Cancel")
            client.act("SetAutoDialogue", enabled=True)
            if args.load_slot is not None:
                load_checkpoint(client, args.load_slot)
            if args.assist_level is not None:
                assist_battle(client, output, args.assist_level)
            elif not args.resume and any(record.get("source") == "native-options-menu"
                                        for record in identity["assistanceRecords"]):
                assist_battle(client, output, client.observe()["player"]["level"])
            if args.route == "opening":
                opening(client, output)
            elif args.route == "home-gate":
                home_gate(client, output)
            elif args.route == "home-extras":
                home_extras(client, output)
            elif args.route in ("easy", "hard"):
                departure(client, output, resource, args.route)
            elif args.route == "wolves":
                wolves(client, output)
            elif args.route == "desert-skills":
                desert_skills(client, output)
            elif args.route == "town":
                town(client, output, resource)
            elif args.route == "rescue":
                rescue(client, output, resource)
            elif args.route == "longmen":
                longmen(client, output, resource)
            elif args.route == "changan-entry":
                changan_entry(client, output)
            elif args.route == "changan-story":
                changan_story(client, output)
            elif args.route == "fengxue":
                fengxue(client, output, resource)
            elif args.route == "bieli-additions":
                bieli_additions(client, output)
            elif args.route in ("tangmen", "tangmen-loss"):
                tangmen(client, output, resource, lose=args.route == "tangmen-loss")
            elif args.route == "cuiyan-second":
                cuiyan_second(client, output)
            elif args.route == "tianwang":
                hanyang_tianwang(client, output)
            elif args.route == "tianwang-wood":
                tianwang_wood(client, output)
            elif args.route == "to-zhongdu":
                to_zhongdu(client, output)
            elif args.route == "zhongdu-additions":
                zhongdu_additions(client, output)
            elif args.route == "zhongdu-books":
                zhongdu_shop(client, output, purchase=True)
            elif args.route == "zhongdu-shaolin":
                zhongdu_shaolin(client, output)
            elif args.route == "zhongdu-night":
                zhongdu_night(client, output)
            elif args.route == "zhongdu-night-rewards":
                zhongdu_night_rewards(client, output)
            elif args.route == "to-linan":
                to_linan(client, output)
            elif args.route == "linan-additions":
                linan_additions(client, output)
            elif args.route == "fengchi-additions":
                fengchi_additions(client, output)
            elif args.route == "pilitang":
                pilitang(client, output)
            elif args.route == "pilitang-restored-swords":
                pilitang_restored_swords(client, output)
            elif args.route == "tournament-entry":
                tournament_entry(client, output)
            elif args.route == "tournament":
                tournament(client, output)
            elif args.route == "tournament-loss":
                tournament_loss(client, output)
            elif args.route == "fengchi-night":
                fengchi_night(client, output)
            elif args.route == "prison-approach":
                prison_approach(client, output)
            elif args.route == "prison-companions":
                prison_companions(client, output, args.expected_deaths or "none")
            elif args.route == "linan-revenge-additions":
                linan_revenge_additions(client, output)
            elif args.route == "anran-magic":
                anran_magic(client, output)
            elif args.route == "linan-white-combat":
                linan_white_combat(client, output)
            elif args.route == "linan-return":
                linan_return(client, output)
            elif args.route == "tianren-entry":
                tianren_entry(client, output)
            elif args.route == "tianren-ground":
                tianren_ground(client, output)
            elif args.route == "tianren-first":
                tianren_first(client, output)
            elif args.route == "early-magic-combat":
                early_magic_combat(client, output)
            elif args.route == "tianren-second":
                tianren_second(client, output)
            elif args.route == "tianren-third":
                tianren_third(client, output)
            elif args.route == "return-items":
                return_items(client, output)
            elif args.route == "wangling-combat":
                wangling_combat(client, output)
            elif args.route == "manjianghong-self":
                manjianghong_self(client, output)
            elif args.route == "tianren-circle":
                tianren_circle_comparison(client, output)
            elif args.route == "tianren-final":
                tianren_final(client, output)
            elif args.route == "tianren-final-loss":
                tianren_final_loss(client, output)
            elif args.route == "exit":
                client.exit_game()
            if args.route != "exit":
                state = checkpoint(client, output, f"route-{args.route}-complete")
                result.update(map=state.get("map"), player=state.get("player"))
            result["status"] = "passed"
        except Exception as error:
            result.update(status="failed", errorType=type(error).__name__, error=str(error))
            try:
                checkpoint(client, output, f"failure-{args.route}-{int(time.time())}")
            except Exception as evidence_error:
                result["evidenceError"] = str(evidence_error)
        finally:
            result.update(finished=time.time(), elapsedSeconds=time.time() - started)
            write_json(output / f"result-{args.route}-{int(time.time())}.json", result)
            print(json.dumps(result, ensure_ascii=False), flush=True)
    raise SystemExit(0 if result["status"] == "passed" else 1)


if __name__ == "__main__":
    main()
