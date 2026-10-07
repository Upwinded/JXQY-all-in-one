"""Test Yuemeier through existing player controls and isolated normal saves."""
from __future__ import annotations

import argparse
import configparser
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import uuid

from gameplay_automation import Client, AutomationError
from run_chenghe_gameplay import save_hashes
from run_jxqy2_gameplay_smoke import reject
import run_yycs_gameplay as yycs

RESOURCE_ID = "YUEMEIER_WAIZHUAN_1_053"
RESOURCE_DIRECTORY = "月眉儿外传"
SAVE_NAMESPACE = RESOURCE_ID.lower()
ADDED_BOXES = {
    "map_039_飞龙堡.map": ((35, 18), (34, 17)),
    "map_022_清平乡.map": ((9, 30), (9, 31)),
    "map_029_码头.map": ((7, 49),),
    "map_009_山洞内部.map": ((4, 11), (24, 40), (4, 8)),
    "map_010_山洞内部.map": ((16, 41), (16, 42)),
    "map_004_武当山连接地图.map": ((18, 24), (37, 41)),
    "map_006_武当山山顶.map": ((2, 118), (0, 108), (3, 76), (33, 6), (58, 13), (18, 97), (32, 125)),
}


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def variables(resource):
    return tuple(sorted(set(yycs.VARIABLES) | {
        match for path in (resource / "script").rglob("*.txt")
        for match in re.findall(r'(?:getvar|assign|add)\("([^"]+)"', path.read_text(encoding="utf-8-sig"))}))


def checkpoint(client, output, name):
    state = yycs.checkpoint(client, output, name)
    if state.get("resourceId") != RESOURCE_ID:
        raise AutomationError("Wrong resource identity")
    identity = json.loads((output / "run.json").read_text(encoding="utf-8"))
    state.update(cheatAssisted=identity["cheatAssisted"], assistanceRecords=identity["assistanceRecords"])
    write_json(output / f"{name}.json", state)
    return state


def source_candidates(assets, output):
    resource = assets / RESOURCE_DIRECTORY
    catalog = yycs.inventory(resource)
    catalog["resourceId"] = RESOURCE_ID
    bindings = []
    for path in sorted((resource / "ini/save").iterdir()):
        if path.suffix not in (".npc", ".obj", ".ini"):
            continue
        config = configparser.ConfigParser(interpolation=None, strict=False)
        config.read(path, encoding="utf-8-sig")
        for section in config.sections():
            for key, value in config[section].items():
                if value and ("script" in key or section.startswith("map_")):
                    bindings.append(dict(path=path.relative_to(resource).as_posix(), section=section,
                                         key=key, script=value, map=config.get("Head", "Map", fallback=""),
                                         name=config.get(section, "Name", fallback=config.get(section, "ObjName", fallback="")),
                                         x=config.get(section, "MapX", fallback=""), y=config.get(section, "MapY", fallback="")))
    catalog["bindings"] = bindings
    write_json(output / "source-candidates.json", catalog)
    print(json.dumps(catalog["counts"], ensure_ascii=False))


def opening(client, output, resource, difficulty):
    client.wait_until(lambda state: state["scene"] == "Title", description="Yuemeier title")
    checkpoint(client, output, "01-title")
    client.activate("new-game")
    client.wait_until(lambda state: bool(state.get("choices")), timeout=120, description="normal difficulty choice")
    yycs.choose_site(client, output, resource,
                     "script/map/map_039_飞龙堡/开始游戏.txt:9", 0 if difficulty == "hard" else 1)
    state = checkpoint(client, output, "02-normal-opening")
    if (state["map"] != "map_039_飞龙堡.map" or state["player"]["position"] != dict(x=2, y=133)
            or state["player"]["level"] != 3 or len(state["magic"]) != 7
            or state["player"]["levelFile"] != f"level-{difficulty}.ini"
            or not any(row["file"] == "player-magic-百剑诀.ini" for row in state["magic"])):
        raise AutomationError("Normal MOD opening differs from its source")
    client.save_or_load(0)
    before = checkpoint(client, output, "03-opening-slot0")
    yycs.load_checkpoint(client, 0)
    after = checkpoint(client, output, "04-opening-reloaded")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if before[key] != after[key]:
            raise AutomationError(f"Normal opening save/load changed {key}")


def departure(client, output, resource):
    yycs.interact_named(client, output, resource, "飞龙堡门卫", position=(3, 125))
    checkpoint(client, output, "fortress-introduction")
    client.save_or_load(1)
    yycs.interact_named(client, output, resource, "飞龙堡头目")
    state = checkpoint(client, output, "fortress-proposal-refused")
    point = yycs.reachable_trap(resource.parent / "yycs", state["map"], 1, state["player"]["position"],
                                {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]})
    client.move(*point, running=True, timeout=120)
    blocked = checkpoint(client, output, "fortress-exit-disabled")
    if blocked["map"] != state["map"] or blocked["generation"] != state["generation"]:
        raise AutomationError("Fortress exit opened before the woman's warning")
    client.save_or_load(3)
    yycs.interact_named(client, output, resource, "妇人")
    checkpoint(client, output, "fortress-exit-enabled")
    yycs.transition(client, resource.parent / "yycs", "map_038_连接地图.map", 1, running=True)
    client.save_or_load(2)
    checkpoint(client, output, "fortress-departure-slot2")


def assist_battle(client, output, level):
    before = checkpoint(client, output, "native-assistance-before")
    client.open_menu("System")
    client.activate("options")
    client.activate("cheat-settings")
    if not client.observe().get("cheatModeEnabled"):
        client.activate("cheat-mode")
    if not client.observe().get("cheatInvincibilityEnabled"):
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
    if (after["variables"] != before["variables"] or after["inventory"] != before["inventory"]
            or after["player"]["money"] != before["player"]["money"]
            or not after.get("cheatInvincibilityEnabled")):
        raise AutomationError("Native assistance changed plot/inventory/money or failed")
    identity = json.loads((output / "run.json").read_text(encoding="utf-8"))
    identity["cheatAssisted"] = True
    identity["assistanceRecords"].append(dict(source="native-options-menu", requestedLevel=level,
            invincibility=True, startedAtMap=before["map"], beforePlayer=before["player"], afterPlayer=after["player"]))
    write_json(output / "run.json", identity)
    write_json(output / f"native-assistance-proof-{time.time_ns()}.json", dict(before=before, after=after,
               plotVariablesUnchanged=True, inventoryUnchanged=True, moneyUnchanged=True, saveBytesEdited=False))


def first_qinglong(client, output, resource):
    yycs.transition(client, resource.parent / "yycs", "map_036_连接地图.map", 1, running=True)
    client.save_or_load(0)
    yycs.interact_named(client, output, resource, "青龙")
    before = checkpoint(client, output, "first-qinglong-battle-started")
    yycs.fight_named(client, output, resource, "青龙", use_magic=True, magic_file="player-magic-百剑诀.ini")
    after = checkpoint(client, output, "first-qinglong-death")
    if after["variables"]["cyyyf"] != "1":
        raise AutomationError("First Qinglong death did not unlock the normal exit")
    write_json(output / "baijian-first-battle-proof.json", dict(before=before, after=after,
               learnedMagic="player-magic-百剑诀.ini", cheatAssisted=after["cheatAssisted"]))
    yycs.transition(client, resource.parent / "yycs", "map_028_连接地图.map", 1, running=True)
    state = checkpoint(client, output, "yang-yingfeng-first-rescue")
    if state["variables"]["cyyyf"] != "2":
        raise AutomationError("Normal hut rescue did not advance the MOD story")
    client.save_or_load(1)
    checkpoint(client, output, "first-rescue-slot1")


def huian(client, output, resource):
    yycs.interact_named(client, output, resource, "杨影枫")
    checkpoint(client, output, "yang-yingfeng-before-pendant-search")
    yycs.transition(client, resource.parent / "yycs", "map_027_连接地图.map", 3, running=True)
    yycs.transition(client, resource.parent / "yycs", "map_012_惠安镇.map", 2, running=True)
    client.save_or_load(2)
    checkpoint(client, output, "huian-entry-slot2")
    yycs.interact_named(client, output, resource, "蒙面人")
    for _ in range(2):
        yycs.fight_named(client, output, resource, "蒙面人", use_magic=True, magic_file="player-magic-百剑诀.ini")
    state = checkpoint(client, output, "huian-masked-men-complete")
    if state["variables"]["mmrswe"] != "2" or state["variables"]["mmrsw"] != "1":
        raise AutomationError("Masked-men normal deaths did not finish the pendant search trigger")
    client.save_or_load(3)
    checkpoint(client, output, "huian-mainline-slot3")


def huian_sidequests(client, output, resource):
    yycs.interact_named(client, output, resource, "杜恩")
    yycs.interact_named(client, output, resource, "祁连山")
    state = checkpoint(client, output, "zhou-mancang-task-started")
    if state["variables"]["deqls"] != "1":
        raise AutomationError("Zhou Mancang task did not start")
    yycs.interact_named(client, output, resource, "祁连山")
    yycs.interact_named(client, output, resource, "杜恩")
    state = checkpoint(client, output, "liu-condition-message")
    if state["variables"]["deqls"] != "2":
        raise AutomationError("Liu's condition did not advance the task")
    yycs.interact_named(client, output, resource, "杜恩")
    before = client.observe(yycs.VARIABLES)
    yycs.interact_named(client, output, resource, "祁连山")
    after = checkpoint(client, output, "zhou-task-reward")
    if (after["variables"]["deqls"] != "3"
            or sum(row["quantity"] for row in after["inventory"]) != sum(row["quantity"] for row in before["inventory"]) + 1
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Zhou task completion/reward differs from the source")
    yycs.interact_named(client, output, resource, "祁连山")
    yycs.interact_named(client, output, resource, "杜恩")
    repeated = checkpoint(client, output, "zhou-task-repeated")
    if repeated["inventory"] != after["inventory"] or repeated["variables"] != after["variables"]:
        raise AutomationError("Completed Zhou task repeated its reward")
    client.save_or_load(4)
    yycs.interact_named(client, output, resource, "吴承业")
    checkpoint(client, output, "wu-chengye-fight-started")
    yycs.fight_named(client, output, resource, "吴承业", use_magic=True, magic_file="player-magic-百剑诀.ini")
    checkpoint(client, output, "wu-chengye-death")
    client.save_or_load(5)
    checkpoint(client, output, "huian-sidequests-slot5")


def hut_return(client, output, resource):
    yycs.transition(client, resource.parent / "yycs", "map_027_连接地图.map", 3, running=True)
    yycs.transition(client, resource.parent / "yycs", "map_028_连接地图.map", 1, running=True)
    yycs.interact_named(client, output, resource, "杨影枫")
    state = checkpoint(client, output, "yang-yingfeng-pendant-search")
    if state["variables"]["goqpx"] != "1":
        raise AutomationError("Normal pendant conversation did not unlock Qingping")
    client.save_or_load(6)
    checkpoint(client, output, "hut-return-slot6")


def qingping(client, output, resource):
    if yycs.idle(client)["map"] != "map_022_清平乡.map":
        for destination, trap in (("map_027_连接地图.map", 3), ("map_012_惠安镇.map", 2),
                                  ("map_014_连接地图.map", 2), ("map_017_连接地图.map", 3),
                                  ("map_023_连接地图.map", 3), ("map_024_倚天山.map", 1),
                                  ("map_021_油菜花地.map", 1)):
            yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
        client.save_or_load(0)
        checkpoint(client, output, "suwubai-pre-fight-slot0")
        yycs.interact_named(client, output, resource, "苏木白")
        yycs.fight_named(client, output, resource, "苏木白", use_magic=True, magic_file="player-magic-百剑诀.ini")
        state = checkpoint(client, output, "suwubai-death-child-escaped")
        if any(row["name"] == "小娃娃" for row in state["targets"]):
            raise AutomationError("Suwubai death did not let the child escape")
        yycs.transition(client, resource.parent / "yycs", "map_022_清平乡.map", 2, running=True)
    client.save_or_load(5)
    checkpoint(client, output, "qingping-before-nurse-slot5")
    yycs.interact_named(client, output, resource, "江湖人", position=(30, 140))
    before = checkpoint(client, output, "qingping-before-nurse")
    if before["variables"]["jhr"]:
        raise AutomationError("Qingping fight started before the nurse's task")
    yycs.interact_named(client, output, resource, "乳娘")
    yycs.interact_named(client, output, resource, "江湖人", position=(30, 140))
    for _ in range(2):
        yycs.fight_named(client, output, resource, "江湖人", use_magic=True, magic_file="player-magic-百剑诀.ini")
    state = checkpoint(client, output, "qingping-occupiers-dead")
    if state["variables"]["jde"] != "2" or state["variables"]["jd"] != "1":
        raise AutomationError("Qingping occupation did not complete normally")
    client.save_or_load(1)
    yycs.transition(client, resource.parent / "yycs", "map_021_油菜花地.map", 1, running=True)
    yycs.interact_named(client, output, resource, "小冬瓜")
    state = checkpoint(client, output, "pendant-letter-delivered")
    if state["map"] != "map_022_清平乡.map" or state["variables"]["jd"] != "2":
        raise AutomationError("Child's pendant delivery did not reach the duel appointment")
    client.save_or_load(6)
    yycs.transition(client, resource.parent / "yycs", "map_021_油菜花地.map", 1, running=True)
    client.save_or_load(3)
    yycs.interact_named(client, output, resource, "杨影枫")
    yycs.fight_named(client, output, resource, "杨影枫", use_magic=True, magic_file="player-magic-百剑诀.ini")
    state = checkpoint(client, output, "yang-duel-book-reward")
    if (state["variables"]["gowyd"] != "1" or not any(row["file"] == "0武道德经上.ini" for row in state["inventory"])
            or any(row["name"] == "杨影枫" for row in state["targets"] if row["kind"] == "npc")):
        raise AutomationError("Yang duel did not finish its native book reward and departure condition")
    client.save_or_load(2)
    checkpoint(client, output, "qingping-duel-complete-slot2")


def added_boxes(client, output, resource):
    state = yycs.idle(client)
    points = ADDED_BOXES[state["map"]]
    opened = []
    unavailable = []
    for point in points:
        if state["map"] == "map_022_清平乡.map":
            client.move(8, point[1], running=True, timeout=120)
        elif (state["map"], point) in (("map_004_武当山连接地图.map", (37, 41)),
                                       ("map_006_武当山山顶.map", (33, 6))):
            stand, destination = ((33, 44), (36, 44)) if point == (37, 41) else ((34, 13), (34, 6))
            approach_exit(client, output, resource, 0, destination=stand)
            before = checkpoint(client, output, f"box-{point[0]}-{point[1]}-access-before")
            try:
                client.act("JumpTo", generation=before["generation"], x=destination[0], y=destination[1])
            except AutomationError as error:
                if str(error) != "JumpTo: no_progress":
                    raise
            else:
                raise AutomationError("Original player action resource unexpectedly allowed jump; recheck box reachability")
            after = checkpoint(client, output, f"box-{point[0]}-{point[1]}-access-after")
            if before["player"]["position"] != after["player"]["position"] or before["inventory"] != after["inventory"]:
                raise AutomationError("Unavailable box access changed position or inventory")
            unavailable.append(dict(position=point, before=before, after=after, reason="no walking path; native jump made no progress"))
            continue
        elif state["map"] in ("map_004_武当山连接地图.map", "map_006_武当山山顶.map"):
            approach_target(client, output, resource, point)
        before = client.observe()
        target = next(row for row in before["targets"] if row["kind"] == "object" and row["position"] == dict(x=point[0], y=point[1]))
        yycs.interact_named(client, output, resource, target["name"], position=point)
        after = checkpoint(client, output, f"added-box-{point[0]}-{point[1]}")
        if sum(row["quantity"] for row in after["inventory"]) != sum(row["quantity"] for row in before["inventory"]) + 1:
            raise AutomationError(f"Added box did not grant its one-item reward: {point}")
        target = next(row for row in after["targets"] if row["kind"] == "object" and row["position"] == dict(x=point[0], y=point[1]))
        reject(client, "Interact", "action_rejected", generation=after["generation"], targetId=target["id"])
        opened.append(dict(position=point, beforeInventory=before["inventory"], afterInventory=after["inventory"]))
    client.save_or_load(4)
    before = checkpoint(client, output, "added-boxes-slot4")
    yycs.load_checkpoint(client, 4)
    after = checkpoint(client, output, "added-boxes-reloaded")
    if before["inventory"] != after["inventory"] or before["player"]["money"] != after["player"]["money"]:
        raise AutomationError("Added box rewards did not survive normal save/load")
    for row in opened:
        point = row["position"]
        target = next(row for row in after["targets"] if row["kind"] == "object" and row["position"] == dict(x=point[0], y=point[1]))
        reject(client, "Interact", "action_rejected", generation=after["generation"], targetId=target["id"])
    write_json(output / f"added-boxes-{state['map']}.json", dict(map=state["map"], opened=opened, unavailable=unavailable,
               savedAndReloaded=True, repeatedRewardRejected=True, cheatAssisted=after["cheatAssisted"]))


def xiang_lao_han(client, output, resource):
    yycs.transition(client, resource.parent / "yycs", "map_011_连接地图.map", 1, running=True)
    yycs.interact_named(client, output, resource, "向老汉")
    yycs.interact_named(client, output, resource, "向老汉")
    state = checkpoint(client, output, "xiang-task-started")
    if state["variables"]["xlhxc"] != "1":
        raise AutomationError("Xiang's task did not start")
    yycs.transition(client, resource.parent / "yycs", "map_008_野树林.map", 3, running=True)
    client.save_or_load(0)
    yycs.interact_named(client, output, resource, "刘掌柜")
    yycs.fight_named(client, output, resource, "刘掌柜", use_magic=True, magic_file="player-magic-百剑诀.ini")
    state = checkpoint(client, output, "liu-zhanggui-death")
    if state["variables"]["xlhxc"] != "2":
        raise AutomationError("Liu's normal death did not finish Xiang's task")
    yycs.transition(client, resource.parent / "yycs", "map_011_连接地图.map", 5, running=True)
    before = client.observe()
    yycs.interact_named(client, output, resource, "向老汉")
    after = checkpoint(client, output, "xiang-task-reward-and-departure")
    if (any(row["name"] == "向老汉" for row in after["targets"] if row["kind"] == "npc")
            or sum(row["quantity"] for row in after["inventory"]) != sum(row["quantity"] for row in before["inventory"]) + 1):
        raise AutomationError("Xiang's task did not grant its reward and remove the giver")
    client.save_or_load(1)
    checkpoint(client, output, "xiang-task-complete-slot1")


def dock_entry(client, output, resource):
    for destination, trap in (("map_024_倚天山.map", 1), ("map_023_连接地图.map", 3),
                              ("map_017_连接地图.map", 2), ("map_014_连接地图.map", 1),
                              ("map_012_惠安镇.map", 1), ("map_027_连接地图.map", 3),
                              ("map_028_连接地图.map", 1), ("map_029_码头.map", 1)):
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    added_boxes(client, output, resource)
    client.save_or_load(0)
    checkpoint(client, output, "dock-choice-source-slot0")


def dock_choice(client, output, resource, option):
    before = checkpoint(client, output, f"dock-choice-{option}-before")
    site = "script/map/map_029_码头/捕头对话.txt:17"
    if before.get("choices"):
        yycs.choose_site(client, output, resource, site, option)
    else:
        yycs.interact_named(client, output, resource, "捕头", choice=(site, option))
    victim = "林晚" if option == 0 else "捕头"
    yycs.fight_named(client, output, resource, victim, use_magic=True, magic_file="player-magic-百剑诀.ini")
    after = checkpoint(client, output, f"dock-choice-{option}-complete")
    if (after["variables"]["XZ"] != str(option)
            or any(row["name"] in ("林晚", "捕头") and row["action"] not in (11, 255)
                   for row in after["targets"] if row["kind"] == "npc")):
        raise AutomationError("Dock choice did not finish its distinct native death/departure outcome")
    write_json(output / f"dock-outcome-{option}.json", dict(before=before, after=after,
               deadNpc=victim, survivingNpcLeftNormally="捕头" if option == 0 else "林晚",
               cheatAssisted=after["cheatAssisted"]))
    client.save_or_load(1)
    checkpoint(client, output, "dock-outcome-slot1")


def island_entry(client, output, resource):
    yycs.interact_named(client, output, resource, "渔夫")
    state = checkpoint(client, output, "island-ferry-arrival")
    if state["map"] != "map_052_码头.map" or state["variables"]["gowyd"] != "2":
        raise AutomationError("Native ferry did not arrive on the island")
    yycs.transition(client, resource.parent / "yycs", "map_050_忘忧岛.map", 1, running=True)
    client.save_or_load(2)
    checkpoint(client, output, "island-entry-slot2")
    yycs.interact_named(client, output, resource, "慧能")
    yycs.fight_named(client, output, resource, "慧能", use_magic=True, magic_file="player-magic-百剑诀.ini")
    checkpoint(client, output, "huineng-native-death")
    yycs.interact_named(client, output, resource, "王莽")
    yycs.interact_named(client, output, resource, "王莽")
    state = checkpoint(client, output, "wuyou-rescue-task-started")
    if state["variables"]["gowyj"] != "1":
        raise AutomationError("Native island conversation did not unlock Wuyou rescue")
    client.save_or_load(3)
    checkpoint(client, output, "island-mainline-slot3")


def cave_boxes(client, output, resource):
    yycs.transition(client, resource.parent / "yycs", "map_008_野树林.map", 3, running=True)
    yycs.transition(client, resource.parent / "yycs", "map_009_山洞内部.map", 1, running=True)
    added_boxes(client, output, resource)
    client.save_or_load(5)
    checkpoint(client, output, "cave-nine-boxes-slot5")
    yycs.transition(client, resource.parent / "yycs", "map_010_山洞内部.map", 2, running=True)
    added_boxes(client, output, resource)
    client.save_or_load(6)
    checkpoint(client, output, "cave-ten-boxes-slot6")


def approach_exit(client, output, resource, trap, destination=None, magic_file="player-magic-百剑诀.ini", excluded_traps=()):
    deadline = time.monotonic() + 480
    while time.monotonic() < deadline:
        state = yycs.idle(client)
        occupied = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]
                    if row.get("action") != 255}
        avoid = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]
                 if row.get("action") != 255 and not (row.get("hostile") and yycs.npc_attackable(row))}
        excluded = {point for other in excluded_traps
                    for point in yycs.trap_points(resource.parent / "yycs", state["map"], other)}
        avoid |= excluded
        try:
            path = yycs.reachable_trap(resource.parent / "yycs", state["map"], trap, state["player"]["position"],
                                      occupied, with_path=True, avoid=avoid, destination=destination)
        except AutomationError as error:
            if not str(error).startswith("No connected trap"):
                raise
            if any(row["kind"] == "npc" and row.get("action") == 11 for row in state["targets"]):
                client.wait_until(lambda value: not any(row["kind"] == "npc" and row.get("action") == 11
                                  for row in value["targets"]), timeout=15, description="native death animation releases path")
                continue
            # Observe does not expose object collision kinds; native movement checks them at each waypoint.
            occupied = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]
                        if row["kind"] == "npc" and row.get("action") != 255}
            avoid = (avoid & occupied) | excluded
            path = yycs.reachable_trap(resource.parent / "yycs", state["map"], trap, state["player"]["position"],
                                      occupied, with_path=True, avoid=avoid, destination=destination)
        try:
            if len(path) <= (2 if excluded_traps else 9):
                if destination is not None:
                    client.move(*destination, running=True, timeout=40)
                return
            client.move(*path[1 if excluded_traps else 8], running=True, timeout=40)
        except AutomationError as error:
            if str(error) not in ("MoveTo: no_progress", "MoveTo: blocked_destination"):
                raise
            state = yycs.idle(client)
            foes = [row for row in state["targets"] if row.get("hostile") and yycs.npc_attackable(row)
                    and row.get("visibleFromPlayer")]
            if not foes:
                if any(row["kind"] == "npc" and row.get("action") == 11 for row in state["targets"]):
                    client.wait_until(lambda value: not any(row["kind"] == "npc" and row.get("action") == 11
                                      for row in value["targets"]), timeout=15, description="native death animation releases doorway")
                    continue
                raise
            position = state["player"]["position"]
            target = min(foes, key=lambda row: abs(row["position"]["x"] - position["x"]) * 2
                         + abs(row["position"]["y"] - position["y"]))
            yycs.fight_named(client, output, resource, target["name"], use_magic=True,
                             target_id=target["id"], magic_file=magic_file)
    raise AutomationError("Normal waypoint traversal exceeded eight minutes")


def blocked_exit(client, output, resource, trap, name, magic_file="player-magic-百剑诀.ini", excluded_traps=()):
    approach_exit(client, output, resource, trap, magic_file=magic_file, excluded_traps=excluded_traps)
    before = yycs.idle(client)
    point = yycs.reachable_trap(resource.parent / "yycs", before["map"], trap, before["player"]["position"],
                               {(row["position"]["x"], row["position"]["y"]) for row in before["targets"]})
    action = client.submit("MoveTo", generation=before["generation"], x=point[0], y=point[1],
                           running=True, timeoutMs=120000)
    client.wait_until(lambda state: state.get("inEvent") or state["generation"] != before["generation"]
                      or client.request("GetActionStatus", actionId=action)["status"] != "running",
                      timeout=122, description="closed exit reached or native gate event")
    if client.request("GetActionStatus", actionId=action)["status"] == "running":
        client.request("CancelAction", actionId=action)
    else:
        client.wait_action(action, timeout=3)
    yycs.idle(client)
    after = checkpoint(client, output, name)
    if after["map"] != before["map"] or after["generation"] != before["generation"]:
        raise AutomationError(f"Story-gated exit opened early: {name}")


def approach_target(client, output, resource, position, magic_file="player-magic-百剑诀.ini"):
    state = yycs.idle(client)
    x, y = position
    neighbors = ((x, y + 2), (x + y % 2 - 1, y + 1), (x - 1, y),
                 (x + y % 2 - 1, y - 1), (x, y - 2),
                 (x + y % 2, y - 1), (x + 1, y), (x + y % 2, y + 1))
    occupied = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]
                if row.get("action") != 255}
    paths = []
    characters = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]
                  if row["kind"] == "npc" and row.get("action") != 255}
    for avoid in (occupied, characters):
        for point in neighbors:
            try:
                paths.append(yycs.reachable_trap(resource.parent / "yycs", state["map"], 0, state["player"]["position"],
                             avoid, with_path=True, avoid=avoid, destination=point))
            except AutomationError:
                pass
        if paths:
            break
    if not paths:
        raise AutomationError(f"No connected normal interaction position: {position}")
    approach_exit(client, output, resource, 0, destination=min(paths, key=len)[-1], magic_file=magic_file)


def island_rescue(client, output, resource):
    if yycs.idle(client)["map"] == "map_050_忘忧岛.map":
        blocked_exit(client, output, resource, 1, "seaside-before-book-blocked")
        for destination, trap in (("map_053_连接地图.map", 2), ("map_054_北山.map", 2)):
            yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
        blocked_exit(client, output, resource, 2, "north-cave-before-rescue-blocked")
    crossings = (("map_053_连接地图.map", 1), ("map_050_忘忧岛.map", 1),
                              ("map_057_连接地图.map", 4), ("map_058_禁地.map", 2),
                              ("map_059_禁地一层.map", 2), ("map_060_禁地二层.map", 2),
                              ("map_061_禁地三层.map", 2), ("map_062_禁地密室.map", 2))
    if yycs.idle(client)["map"] == "map_062_禁地密室.map":
        crossings = ()
    for destination, trap in crossings:
        approach_exit(client, output, resource, trap)
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    client.save_or_load(0)
    checkpoint(client, output, "rescue-chamber-source-slot0")
    while True:
        state = yycs.idle(client)
        foes = [row for row in state["targets"] if row.get("hostile") and yycs.npc_attackable(row)]
        if not foes:
            break
        position = state["player"]["position"]
        target = min(foes, key=lambda row: abs(row["position"]["x"] - position["x"]) * 2
                     + abs(row["position"]["y"] - position["y"]))
        yycs.fight_named(client, output, resource, target["name"], use_magic=True,
                         target_id=target["id"], magic_file="player-magic-百剑诀.ini")
    approach_exit(client, output, resource, 0, destination=(16, 15))
    before = client.observe()
    yycs.interact_named(client, output, resource, "宝盒", position=(16, 13))
    after = checkpoint(client, output, "beimo-blade-reward")
    if sum(row["quantity"] for row in after["inventory"] if row["file"] == "goods-w11-悲魔之刃.ini") != 1 + sum(
            row["quantity"] for row in before["inventory"] if row["file"] == "goods-w11-悲魔之刃.ini"):
        raise AutomationError("Chamber box did not grant the native blade reward")
    client.save_or_load(1)
    yycs.load_checkpoint(client, 1)
    target = next(row for row in client.observe()["targets"] if row["kind"] == "object" and row["position"] == dict(x=16, y=13))
    reject(client, "Interact", "action_rejected", generation=client.observe()["generation"], targetId=target["id"])
    yycs.interact_named(client, output, resource, "纳兰真被捆")
    after = checkpoint(client, output, "nalan-zhen-rescued")
    if (after["map"] != "map_058_禁地.map" or after["variables"]["gowyj"] != "3"
            or after["variables"]["golyssd"] != "1"
            or not any(row["name"] == "纳兰真" for row in after["targets"])):
        raise AutomationError("Normal rescue did not finish the sisters' reunion and cave task")
    client.save_or_load(5)
    checkpoint(client, output, "island-rescued-slot5")


def island_book(client, output, resource):
    crossings = (("map_057_连接地图.map", 1), ("map_050_忘忧岛.map", 1),
                 ("map_053_连接地图.map", 2), ("map_054_北山.map", 2), ("map_055_山洞.map", 2))
    current = yycs.idle(client)["map"]
    sources = ("map_058_禁地.map",) + tuple(row[0] for row in crossings)
    for destination, trap in crossings[sources.index(current):]:
        approach_exit(client, output, resource, trap)
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    client.save_or_load(0)
    checkpoint(client, output, "book-box-source-slot0")
    approach_target(client, output, resource, (75, 28))
    yycs.interact_named(client, output, resource, "宝盒", position=(75, 28))
    state = checkpoint(client, output, "book-letter-and-burning-complete")
    if (state["variables"]["gohb"] != "1" or any(row["file"] == "0武道德经上.ini" for row in state["inventory"])
            or not any(row["file"] == "0书信.ini" for row in state["inventory"])):
        raise AutomationError("Cave book/letter did not finish its native inventory and seaside gate effects")
    client.save_or_load(6)
    yycs.load_checkpoint(client, 6)
    target = next(row for row in client.observe()["targets"] if row["kind"] == "object" and row["position"] == dict(x=75, y=28))
    reject(client, "Interact", "action_rejected", generation=client.observe()["generation"], targetId=target["id"])
    checkpoint(client, output, "island-book-complete-slot6")


def seaside(client, output, resource):
    for destination, trap in (("map_054_北山.map", 1), ("map_053_连接地图.map", 1),
                              ("map_050_忘忧岛.map", 1), ("map_051_海边.map", 1)):
        approach_exit(client, output, resource, trap)
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    client.save_or_load(0)
    checkpoint(client, output, "seaside-battle-source-slot0")
    yycs.interact_named(client, output, resource, "纳兰真")
    blocked_exit(client, output, resource, 1, "seaside-battle-exit-disabled")
    yycs.fight_named(client, output, resource, "纳兰潜凛", use_magic=True, magic_file="player-magic-百剑诀.ini")
    state = checkpoint(client, output, "nalan-zhen-story-death-and-pendant")
    if (state["variables"]["gohb"] != "2" or state["variables"]["goflb"] != "1"
            or not any(row["file"] == "1半块玉佩.ini" for row in state["inventory"])
            or any(row["name"] == "纳兰真" for row in state["targets"])):
        raise AutomationError("Seaside battle did not complete Nalan Zhen's scripted death and fortress task")
    client.save_or_load(1)
    checkpoint(client, output, "seaside-complete-slot1")


def fortress_return(client, output, resource):
    for destination, trap in (("map_050_忘忧岛.map", 1), ("map_052_码头.map", 3)):
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    yycs.interact_named(client, output, resource, "渔夫")
    if yycs.idle(client)["map"] != "map_029_码头.map":
        raise AutomationError("Native return ferry did not reach the mainland")
    for destination, trap in (("map_028_连接地图.map", 1), ("map_036_连接地图.map", 2),
                              ("map_038_连接地图.map", 2), ("map_039_飞龙堡.map", 2)):
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    client.save_or_load(0)
    checkpoint(client, output, "fortress-return-qinglong-source-slot0")
    yycs.interact_named(client, output, resource, "青龙")
    blocked_exit(client, output, resource, 1, "fortress-return-battle-exit-disabled")
    yycs.fight_named(client, output, resource, "青龙", use_magic=True, magic_file="player-magic-百剑诀.ini")
    state = checkpoint(client, output, "fortress-qinglong-escape-complete")
    if state["variables"]["goflb"] != "2" or state["variables"]["gocjsz"] != "1" or any(
            row["name"] == "青龙" for row in state["targets"]):
        raise AutomationError("Fortress Qinglong's repaired death binding did not complete his escape and Zangjian task")
    client.save_or_load(2)
    yycs.load_checkpoint(client, 2)
    yycs.transition(client, resource.parent / "yycs", "map_038_连接地图.map", 1, running=True)
    client.save_or_load(3)
    checkpoint(client, output, "fortress-return-exit-restored-slot3")


def zangjian(client, output, resource):
    for destination, trap in (("map_036_连接地图.map", 1), ("map_028_连接地图.map", 1),
                              ("map_027_连接地图.map", 3), ("map_012_惠安镇.map", 2),
                              ("map_014_连接地图.map", 2), ("map_015_藏剑山庄.map", 2)):
        approach_exit(client, output, resource, trap)
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    client.save_or_load(0)
    checkpoint(client, output, "zangjian-source-slot0")
    approach_target(client, output, resource, (21, 71))
    yycs.interact_named(client, output, resource, "杨影枫")
    state = checkpoint(client, output, "zangjian-battle-triggered")
    if any(row["name"] == "杨影枫" and row["kind"] == "object" for row in state["targets"]):
        raise AutomationError("Zangjian object binding did not finish Yang's scene")
    for name in ("青龙", "卓非凡"):
        state = yycs.idle(client)
        if state["variables"]["zffsw"] == "2":
            break
        target = next(row for row in state["targets"] if row["name"] == name and yycs.npc_attackable(row))
        approach_target(client, output, resource, (target["position"]["x"], target["position"]["y"]))
        yycs.fight_named(client, output, resource, name, single_target=True)
        checkpoint(client, output, f"zangjian-{name}-normal-death")
        if yycs.idle(client)["variables"]["zffsw"] == "1":
            client.save_or_load(5)
            yycs.load_checkpoint(client, 5)
            checkpoint(client, output, "zangjian-first-death-reloaded")
    state = checkpoint(client, output, "zangjian-two-deaths-complete")
    if state["variables"]["zffsw"] != "2" or state["variables"]["gowds"] != "1" or state["variables"]["gocjsz"] != "2":
        raise AutomationError("Zangjian two native deaths did not unlock Wudang")
    client.save_or_load(6)
    checkpoint(client, output, "zangjian-complete-slot6")


def wudang_entry(client, output, resource):
    for destination, trap in (("map_014_连接地图.map", 1), ("map_012_惠安镇.map", 1),
                              ("map_011_连接地图.map", 1), ("map_008_野树林.map", 3),
                              ("map_004_武当山连接地图.map", 3)):
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    added_boxes(client, output, resource)
    yycs.transition(client, resource.parent / "yycs", "map_006_武当山山顶.map", 1, running=True)
    client.save_or_load(0)
    checkpoint(client, output, "wudang-gates-source-slot0")


def early_gates(client, output, resource):
    state = yycs.idle(client)
    if any(state["variables"][name] not in ("", "0") for name in ("gowds", "gocjsz")):
        raise AutomationError("Early-gate route requires a normal source before both story unlocks")
    for destination, trap in (("map_008_野树林.map", 3), ("map_004_武当山连接地图.map", 3)):
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    blocked_exit(client, output, resource, 1, "wudang-before-story-blocked")
    for destination, trap in (("map_008_野树林.map", 2), ("map_011_连接地图.map", 5),
                              ("map_012_惠安镇.map", 2), ("map_014_连接地图.map", 2)):
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    blocked_exit(client, output, resource, 2, "zangjian-before-story-blocked")


def wudang_guard(client, output, resource, name, option):
    yycs.interact_named(client, output, resource, name,
                        choice=(f"script/map/map_006_武当山山顶/{name}对话.txt:2", option))
    state = checkpoint(client, output, f"wudang-{name}-option-{option}")
    guards = [row for row in state["targets"] if row["name"] in ("胖道士", "瘦道士")]
    if option == 0 and guards or option == 1 and (len(guards) != 3 or any(not row.get("hostile") for row in guards)):
        raise AutomationError("Wudang guard choice did not produce its distinct native admission/hostile result")
    if option == 1:
        while True:
            state = yycs.idle(client)
            foes = [row for row in state["targets"] if row.get("hostile") and yycs.npc_attackable(row)]
            if not foes:
                break
            position = state["player"]["position"]
            target = min(foes, key=lambda row: abs(row["position"]["x"] - position["x"]) * 2
                         + abs(row["position"]["y"] - position["y"]))
            yycs.fight_named(client, output, resource, target["name"], use_magic=True,
                             target_id=target["id"], magic_file="player-magic-百剑诀.ini")
    client.save_or_load(1)
    checkpoint(client, output, "wudang-guard-complete-slot1")


def ending(client, output, resource):
    added_boxes(client, output, resource)
    client.save_or_load(2)
    checkpoint(client, output, "wudang-finale-source-slot2")
    yycs.interact_named(client, output, resource, "天星道长")
    state = checkpoint(client, output, "wudang-final-battle-triggered")
    terminal = "script/map/map_006_武当山山顶/天星道长死亡.txt"
    for name in ("天星道长", "孟知秋"):
        state = yycs.idle(client)
        target = next(row for row in state["targets"] if row["name"] == name and yycs.npc_attackable(row))
        approach_target(client, output, resource, (target["position"]["x"], target["position"]["y"]))
        yycs.fight_named(client, output, resource, name, single_target=True,
                         expected_terminal=terminal if name == "孟知秋" else None,
                         final_dialogue=("真儿，我来了", "活着到底是为了什么", "是心生所盼"))
        if name == "天星道长":
            state = checkpoint(client, output, "wudang-first-leader-death")
            if state["variables"]["swtm"] != "1":
                raise AutomationError("Wudang first death counter differs")
            client.save_or_load(3)
            yycs.load_checkpoint(client, 3)
    state = checkpoint(client, output, "actual-ending-title")
    if state["scene"] != "Title":
        raise AutomationError("Native ending did not complete its seaside epilogue and return to title")
    write_json(output / "actual-ending-proof.json", dict(after=state, terminal=terminal,
               normalEnding=True, seasideEpilogueCovered=True, actualTitleReached=True,
               cheatAssisted=state["cheatAssisted"], saveBytesEdited=False))


def new_magic(client, output, resource):
    yycs.idle(client)
    client.open_menu("System")
    client.activate("options")
    client.activate("cheat-settings")
    if client.observe().get("cheatInvincibilityEnabled"):
        client.activate("invincibility")
    for _ in range(5):
        if client.observe().get("worldInput"):
            break
        client.ui("Cancel")
    for name in ("百剑诀", "招魂幡", "慧剑长空", "冰心仙子", "杀意", "金蝉思雨"):
        if (output / f"new-magic-{name}-proof.json").exists():
            continue
        yycs.idle(client)
        yycs.load_checkpoint(client, 0)
        yycs.interact_named(client, output, resource, "青龙")
        if name == "杀意":
            client.move(24, 62, running=True, timeout=40)
        elif name != "金蝉思雨":
            client.move(26, 52, running=True, timeout=40)
        state = yycs.idle(client)
        filename = f"player-magic-{name}.ini"
        learned = next(row for row in state["magic"] if row["file"] == filename)
        client.assign_magic(learned["slot"], 0)
        before = checkpoint(client, output, f"magic-{name}-before")
        if before.get("cheatInvincibilityEnabled"):
            raise AutomationError("Magic fee test requires native invincibility disabled")
        target = next(row for row in before["targets"] if row["name"] == "青龙" and yycs.npc_attackable(row))
        arguments = {} if name in ("杀意", "金蝉思雨") else {"targetId": target["id"]}
        samples = []
        casts = 0
        for shot in range(3):
            mana_before = client.observe()["player"]["mana"]
            for attempt in range(3):
                try:
                    client.act("CastSkill", generation=before["generation"], slot=0, **arguments)
                    break
                except AutomationError as error:
                    if str(error) != "CastSkill: action_not_executed":
                        raise
                    if client.observe()["player"]["mana"] < mana_before:
                        break
                    if attempt == 2:
                        raise
                    time.sleep(0.3)
            casts += 1
            for _ in range(25):
                samples.append(client.observe(yycs.VARIABLES))
                time.sleep(0.1)
            if name in ("杀意", "金蝉思雨") or any(row["life"] < target["life"] for sample in samples
                                                     for row in sample["targets"] if row["id"] == target["id"]):
                break
        after = checkpoint(client, output, f"magic-{name}-after")
        config = configparser.ConfigParser(interpolation=None, strict=False)
        config.read(resource / "ini/magic" / filename, encoding="utf-8-sig")
        cost = config.getint(f"Level{learned['level']}", "ManaCost")
        if before["player"]["mana"] - min(row["player"]["mana"] for row in samples) != cost * casts:
            raise AutomationError(f"Native magic mana cost differs: {name}")
        damaged = any(row["life"] < target["life"] for sample in samples for row in sample["targets"] if row["id"] == target["id"])
        if name not in ("杀意", "金蝉思雨") and not damaged:
            raise AutomationError(f"Learned offensive magic did not damage the normal target: {name}")
        if name == "杀意" and max(row["player"]["thew"] for row in samples) <= before["player"]["thew"]:
            raise AutomationError("Sha Yi did not replenish native thew")
        write_json(output / f"new-magic-{name}-proof.json", dict(before=before, after=after, samples=samples,
                   actualManaCost=cost, casts=casts, targetDamaged=damaged, nativeInvincibilityDisabled=True,
                   cheatAssisted=after["cheatAssisted"], saveBytesEdited=False))
    yycs.idle(client)


def shield_and_pursuit(client, output, resource):
    yycs.load_checkpoint(client, 0)
    assist_battle(client, output, 20)
    approach_target(client, output, resource, (24, 48))
    while True:
        state = yycs.idle(client)
        foes = [row for row in state["targets"] if row.get("hostile") and yycs.npc_attackable(row)
                and row.get("visibleFromPlayer")]
        if not foes:
            break
        yycs.fight_named(client, output, resource, foes[0]["name"], use_magic=True,
                         target_id=foes[0]["id"], magic_file="player-magic-百剑诀.ini")
    client.save_or_load(6)
    checkpoint(client, output, "shield-and-pursuit-source-slot6")
    client.open_menu("System")
    client.activate("options")
    client.activate("cheat-settings")
    client.activate("invincibility")
    for _ in range(5):
        if client.observe().get("worldInput"):
            break
        client.ui("Cancel")
    trials = []
    for shield in (False, True):
        yycs.load_checkpoint(client, 6)
        if shield:
            magic = next(row for row in client.observe()["magic"] if row["file"] == "player-magic-金蝉思雨.ini")
            client.assign_magic(magic["slot"], 0)
        yycs.interact_named(client, output, resource, "青龙")
        before = checkpoint(client, output, f"shield-{shield}-before")
        if before.get("cheatInvincibilityEnabled"):
            raise AutomationError("Shield comparison requires native invincibility disabled")
        if shield:
            client.act("CastSkill", generation=before["generation"], slot=0)
        samples = []
        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            samples.append(client.observe(yycs.VARIABLES))
            time.sleep(0.1)
        after = checkpoint(client, output, f"shield-{shield}-after")
        trials.append(dict(shield=shield, before=before, after=after, samples=samples,
                           lostLife=before["player"]["life"] - after["player"]["life"]))
    if trials[0]["lostLife"] <= 0 or trials[1]["lostLife"] >= trials[0]["lostLife"] or (
            trials[1]["before"]["player"]["mana"] - trials[1]["after"]["player"]["mana"] != 6):
        raise AutomationError("Native shield comparison did not demonstrate damage absorption and its six-mana fee")
    write_json(output / "jinchan-shield-proof.json", dict(trials=trials, reducedActualDamage=True,
               nativeInvincibilityDisabled=True, cheatAssisted=True, saveBytesEdited=False))
    yycs.load_checkpoint(client, 6)
    assist_battle(client, output, 20)
    yycs.interact_named(client, output, resource, "青龙")
    client.move(24, 62, running=True, timeout=40)
    before = checkpoint(client, output, "native-pursuit-before")
    target = next(row for row in before["targets"] if row["name"] == "青龙" and yycs.npc_attackable(row))
    action = client.submit("Attack", generation=before["generation"], targetId=target["id"], timeoutMs=30000)
    samples = []
    deadline = time.monotonic() + 35
    while time.monotonic() < deadline:
        samples.append(client.observe(yycs.VARIABLES))
        status = client.request("GetActionStatus", actionId=action)
        if status["status"] != "running":
            break
        time.sleep(0.05)
    else:
        raise AutomationError("Native pursuit timed out")
    after = checkpoint(client, output, "native-pursuit-after")
    moved = any(row["player"]["position"] != before["player"]["position"] for row in samples)
    target_moved = any(row["position"] != target["position"] for sample in samples for row in sample["targets"] if row["id"] == target["id"])
    damaged = any(row["life"] < target["life"] for sample in samples for row in sample["targets"] if row["id"] == target["id"])
    if status["status"] != "succeeded" or not moved or not target_moved or not damaged:
        raise AutomationError("Native attack queue did not follow the moving target and hit it")
    write_json(output / "native-moving-target-pursuit-proof.json", dict(before=before, after=after,
               action=action, samples=samples, targetMoved=True, playerPursued=True, targetDamaged=True,
               cheatAssisted=True, saveBytesEdited=False))


def player_death(client, output, resource):
    if client.observe().get("cheatInvincibilityEnabled"):
        raise AutomationError("Normal player death requires native invincibility disabled")
    yycs.transition(client, resource.parent / "yycs", "map_036_连接地图.map", 1, running=True)
    client.save_or_load(0)
    yycs.interact_named(client, output, resource, "青龙")
    checkpoint(client, output, "normal-player-death-battle-started")
    terminal = "script/common/主角死亡.txt"
    state = yycs.idle(client, output=output, resource=resource, expected_terminal=terminal)
    checkpoint(client, output, "normal-player-death-title")
    write_json(output / "normal-player-death-proof.json", dict(after=state, terminal=terminal,
               actualTitleReached=True, normalEnemyDamage=True, cheatAssisted=False, saveBytesEdited=False))


def fortress_merchant(client, output, resource):
    yycs.transition(client, resource.parent / "yycs", "map_039_飞龙堡.map", 2, running=True)
    yycs.interact_named(client, output, resource, "妇人", shop=True)
    before = checkpoint(client, output, "fortress-woman-shop")
    filename = "goods-m00-金花.ini"
    offered = next(row for row in before["shop"] if row["file"] == filename)
    quantity = lambda state: sum(row["quantity"] for row in state["inventory"] if row["file"] == filename)
    client.buy(offered["slot"])
    after = checkpoint(client, output, "fortress-woman-purchase")
    # Native Goods::getRawCost uses Life*2 for this 70-life, zero-mana/thew medicine.
    if after["player"]["money"] != before["player"]["money"] - 140 or quantity(after) != quantity(before) + 1:
        raise AutomationError("Woman's actual medicine trade did not grant one item for the native 140-tael price")
    client.ui("Cancel")
    yycs.idle(client)
    client.save_or_load(5)
    saved = checkpoint(client, output, "fortress-woman-purchase-slot5")
    yycs.load_checkpoint(client, 5)
    loaded = checkpoint(client, output, "fortress-woman-purchase-reloaded")
    if saved["inventory"] != loaded["inventory"] or saved["player"]["money"] != loaded["player"]["money"]:
        raise AutomationError("Woman's trade did not survive normal save/load")
    write_json(output / "fortress-merchant-trade-proof.json", dict(before=before, after=after,
               file=filename, actualCost=140, savedAndReloaded=True, cheatAssisted=False, saveBytesEdited=False))


def sha_yi(client, output, resource):
    client.open_menu("System")
    client.activate("options")
    client.activate("cheat-settings")
    if client.observe().get("cheatInvincibilityEnabled"):
        client.activate("invincibility")
    for _ in range(5):
        if client.observe().get("worldInput"):
            break
        client.ui("Cancel")
    learned = next(row for row in client.observe()["magic"] if row["file"] == "player-magic-杀意.ini")
    client.assign_magic(learned["slot"], 0)
    client.move(24, 80, running=True)
    before = client.observe(yycs.VARIABLES)
    action = client.submit("CastSkill", generation=before["generation"], slot=0)
    samples = []
    deadline = time.monotonic() + 4
    while time.monotonic() < deadline:
        samples.append(client.observe(yycs.VARIABLES))
        time.sleep(0.01)
    client.wait_action(action)
    after = checkpoint(client, output, "sha-yi-animation-after")
    during = [state for state in samples if state["player"]["action"] == 8]
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.read(resource / "ini/level" / before["player"]["levelFile"], encoding="utf-8-sig")
    maximum = config.getint(f"Level{before['player']['level']}", "ThewMax")
    # Standing recovery runs at most once per frame, adding ceil(ThewMax*0.004).
    per_frame = (maximum * 4 + 999) // 1000
    gains = [dict(before=left, after=right, passiveUpperBound=(right["frame"] - left["frame"]) * per_frame)
             for left, right in zip(samples, samples[1:])
             if left["player"]["action"] == 8 and right["player"]["thew"] - left["player"]["thew"]
             > (right["frame"] - left["frame"]) * per_frame]
    write_json(output / "sha-yi-animation-proof.json", dict(before=before, after=after, samples=samples,
               magicAnimationThew=[state["player"]["thew"] for state in during],
               isolatedGains=gains,
               nativeInvincibilityDisabled=not before.get("cheatInvincibilityEnabled"),
               cheatAssisted=after["cheatAssisted"], saveBytesEdited=False))
    if not gains or before["player"]["mana"] - after["player"]["mana"] != 32:
        raise AutomationError("Sha Yi stamina gain has not been separated from passive recovery at its native release frame")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--assets", type=Path, default=Path("assets"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--route", choices=("inventory", "opening", "departure", "first-qinglong", "huian", "huian-sidequests", "hut-return", "qingping", "boxes", "xiang-laohan", "dock-entry", "dock-choice", "island-entry", "island-rescue", "island-book", "seaside", "fortress-return", "zangjian", "wudang-entry", "early-gates", "wudang-guard", "ending", "new-magic", "sha-yi", "shield-and-pursuit", "player-death", "fortress-merchant", "cave-boxes", "observe", "exit"), default="opening")
    parser.add_argument("--difficulty", choices=("easy", "hard"), default="easy")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--source-run", type=Path)
    parser.add_argument("--load-slot", type=int, choices=range(7))
    parser.add_argument("--assist-level", type=int, choices=range(3, 81))
    parser.add_argument("--option", type=int, choices=(0, 1), default=0)
    parser.add_argument("--guard", choices=("胖道士", "瘦道士"), default="胖道士")
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    output, assets = args.output.resolve(), args.assets.resolve()
    resource = assets / RESOURCE_DIRECTORY
    yycs.VARIABLES = variables(resource)
    if args.route == "inventory":
        output.mkdir(parents=True, exist_ok=False)
        source_candidates(assets, output)
        return
    if args.resume:
        identity = json.loads((output / "run.json").read_text(encoding="utf-8"))
        if identity["resourceId"] != RESOURCE_ID:
            parser.error("Resume requires a Yuemeier run")
    else:
        if not args.exe or args.source_run and args.load_slot is None:
            parser.error("Fresh run requires --exe; cloning requires --load-slot")
        output.mkdir(parents=True, exist_ok=False)
        parent_identity = {}
        if args.source_run:
            parent = args.source_run.resolve()
            parent_identity = json.loads((parent / "run.json").read_text(encoding="utf-8"))
            if parent_identity["resourceId"] != RESOURCE_ID:
                parser.error("Source requires a Yuemeier run")
            source_save = parent / "user-data/save"
            before = save_hashes(source_save)
            if f"{SAVE_NAMESPACE}/rpg{args.load_slot + 1}/game.ini" not in before:
                parser.error("Selected normal manual save is absent")
            shutil.copytree(source_save, output / "user-data/save")
            unchanged = before == save_hashes(output / "user-data/save") == save_hashes(source_save)
            write_json(output / "normal-save-clone.json", dict(parentRun=str(parent), sourceSlot=args.load_slot,
                       fileHashes=before, sourceBeforeEqualsCloneEqualsSourceAfter=unchanged, saveBytesEdited=False))
            if not unchanged:
                parser.error("Source changed or cloned save bytes differ")
        executable = args.exe.resolve()
        session = "yuemeier-" + str(uuid.uuid4())
        command = [str(executable), "--assets", str(assets), "--resource-id", RESOURCE_ID,
                   "--skip-startup-video", "--enable-automation-hooks", "--automation-pipe", session,
                   "--user-data-root", str(output / "user-data"), "--log-file", str(output / "game.log")]
        with (output / "stdout.log").open("wb") as stdout, (output / "stderr.log").open("wb") as stderr:
            process = subprocess.Popen(command, cwd=executable.parent, stdout=stdout, stderr=stderr)
        identity = dict(resourceId=RESOURCE_ID, session=session, pid=process.pid, command=command,
                        engineSha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                        resourceProfileSha256=hashlib.sha256((resource / "game_profile.ini").read_bytes()).hexdigest(),
                        playerNpcresSha256=hashlib.sha256((resource / "ini/npcres/npc016-月眉儿.ini").read_bytes()).hexdigest(),
                        cheatAssisted=parent_identity.get("cheatAssisted", False),
                        assistanceRecords=parent_identity.get("assistanceRecords", []), started=time.time())
        if parent_identity:
            identity.update(parentRun=str(args.source_run.resolve()), parentSlot=args.load_slot)
        write_json(output / "run.json", identity)
    started = time.time()
    snapshot = output / f"route-source-{time.time_ns()}"
    snapshot.mkdir()
    for filename in ("run_yuemeier_gameplay.py", "run_yycs_gameplay.py", "run_chenghe_gameplay.py", "gameplay_automation.py"):
        shutil.copy2(Path(__file__).with_name(filename), snapshot / filename)
    result = dict(route=args.route, status="running", started=started, resourceId=RESOURCE_ID,
                  fullCoverage=False, fullPlaythrough=False, cheatAssisted=identity["cheatAssisted"],
                  resumed=args.resume, loadSlot=args.load_slot, sourceSnapshot=str(snapshot))
    with Client(identity["session"], timeout=25, transcript=output / "commands.jsonl") as client:
        try:
            if any(row["name"] == "return-to-title" for row in client.observe().get("ui", [])):
                client.ui("Cancel")
            client.act("SetAutoDialogue", enabled=True)
            if args.load_slot is not None:
                yycs.load_checkpoint(client, args.load_slot)
            if args.assist_level is not None:
                assist_battle(client, output, args.assist_level)
            elif not args.resume and any(record.get("source") == "native-options-menu"
                                        for record in identity["assistanceRecords"]):
                assist_battle(client, output, client.observe()["player"]["level"])
            if args.route == "opening":
                opening(client, output, resource, args.difficulty)
            elif args.route == "departure":
                departure(client, output, resource)
            elif args.route == "first-qinglong":
                first_qinglong(client, output, resource)
            elif args.route == "huian":
                huian(client, output, resource)
            elif args.route == "huian-sidequests":
                huian_sidequests(client, output, resource)
            elif args.route == "hut-return":
                hut_return(client, output, resource)
            elif args.route == "qingping":
                qingping(client, output, resource)
            elif args.route == "boxes":
                added_boxes(client, output, resource)
            elif args.route == "xiang-laohan":
                xiang_lao_han(client, output, resource)
            elif args.route == "dock-entry":
                dock_entry(client, output, resource)
            elif args.route == "dock-choice":
                dock_choice(client, output, resource, args.option)
            elif args.route == "island-entry":
                island_entry(client, output, resource)
            elif args.route == "cave-boxes":
                cave_boxes(client, output, resource)
            elif args.route == "island-rescue":
                island_rescue(client, output, resource)
            elif args.route == "island-book":
                island_book(client, output, resource)
            elif args.route == "new-magic":
                new_magic(client, output, resource)
            elif args.route == "seaside":
                seaside(client, output, resource)
            elif args.route == "fortress-return":
                fortress_return(client, output, resource)
            elif args.route == "zangjian":
                zangjian(client, output, resource)
            elif args.route == "wudang-entry":
                wudang_entry(client, output, resource)
            elif args.route == "early-gates":
                early_gates(client, output, resource)
            elif args.route == "wudang-guard":
                wudang_guard(client, output, resource, args.guard, args.option)
            elif args.route == "ending":
                ending(client, output, resource)
            elif args.route == "shield-and-pursuit":
                shield_and_pursuit(client, output, resource)
            elif args.route == "player-death":
                player_death(client, output, resource)
            elif args.route == "fortress-merchant":
                fortress_merchant(client, output, resource)
            elif args.route == "sha-yi":
                sha_yi(client, output, resource)
            elif args.route == "exit":
                client.exit_game()
            if args.route != "exit":
                state = checkpoint(client, output, f"route-{args.route}-complete")
                result.update(map=state.get("map"), player=state.get("player"))
            result["status"] = "passed"
        except Exception as error:
            result.update(status="failed", errorType=type(error).__name__, error=str(error))
            try:
                checkpoint(client, output, f"failure-{args.route}-{time.time_ns()}")
            except Exception as evidence_error:
                result["evidenceError"] = str(evidence_error)
        finally:
            result.update(finished=time.time(), elapsedSeconds=time.time() - started,
                          cheatAssisted=json.loads((output / "run.json").read_text(encoding="utf-8"))["cheatAssisted"])
            write_json(output / f"result-{args.route}-{time.time_ns()}.json", result)
            print(json.dumps(result, ensure_ascii=False), flush=True)
    raise SystemExit(0 if result["status"] == "passed" else 1)


if __name__ == "__main__":
    main()
