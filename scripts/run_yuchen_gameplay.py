"""Test Yuchen through existing player controls and isolated normal saves."""
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
import run_yycs_gameplay as yycs
import run_yuemeier_gameplay as yuemeier

RESOURCE_ID = "JIANGHU_YUCHEN_1_03"
RESOURCE_DIRECTORY = "江湖余尘"
SAVE_NAMESPACE = RESOURCE_ID.lower()
PIPE_PREFIX = "yuchen-"
ADDITIONAL_VARIABLES = ()
ADDITIONAL_ROUTE_SOURCES = ()
CUSTOM_ROUTES = {}
yuemeier.RESOURCE_ID = RESOURCE_ID
yuemeier.RESOURCE_DIRECTORY = RESOURCE_DIRECTORY
yuemeier.SAVE_NAMESPACE = SAVE_NAMESPACE
checkpoint = yuemeier.checkpoint
write_json = yuemeier.write_json


def opening(client, output, resource, difficulty, gender):
    state = client.observe()
    if state["scene"] == "Title":
        checkpoint(client, output, "01-title")
        client.activate("new-game")
    client.wait_until(lambda state: bool(state.get("choices")), timeout=120,
                      description="normal difficulty choice")
    yycs.choose_site(client, output, resource, "script/map/map_020_樱花谷/begin.txt:6",
                     0 if difficulty == "hard" else 1,
                     remaining=(("script/map/map_020_樱花谷/begin.txt:7", gender),))
    state = checkpoint(client, output, "02-normal-opening")
    if (state["map"] != "map_020_樱花谷.map" or state["variables"].get("event") != "100"
            or state["variables"].get("jtjyhd") != "10" or state["variables"].get("cqyhd") != "10"
            or state["variables"].get("XZ") != str(gender)
            or state["player"]["levelFile"] != f"level-{difficulty}.ini"):
        raise AutomationError("Normal Yuchen opening differs from its source")
    client.save_or_load(0)
    before = checkpoint(client, output, "03-opening-slot0")
    yycs.load_checkpoint(client, 0)
    after = checkpoint(client, output, "04-opening-reloaded")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if before[key] != after[key]:
            raise AutomationError(f"Normal opening save/load changed {key}")


def departure(client, output, resource):
    before = yycs.idle(client)
    map_root = resource if (resource / "map" / before["map"]).exists() else resource.parent / "yycs"
    point = yycs.reachable_trap(map_root, before["map"], 1, before["player"]["position"],
                                {(row["position"]["x"], row["position"]["y"]) for row in before["targets"]})
    client.move(*point, running=True, timeout=120)
    blocked = checkpoint(client, output, "sakura-exit-disabled")
    if blocked["map"] != before["map"] or blocked["generation"] != before["generation"]:
        raise AutomationError("Sakura exit opened before accepting the task")
    path = "script/map/map_020_樱花谷/九头蛟对话.txt"
    sites = [site for site in yycs.inventory(resource)["choices"] if site["path"] == path]
    yycs.interact_named(client, output, resource, "九头蛟", choice=tuple((site["id"], 0) for site in sites))
    accepted = checkpoint(client, output, "jiutoujiao-task-accepted")
    client.save_or_load(1)
    saved = configparser.ConfigParser(interpolation=None, strict=False)
    saved.read(output / "user-data/save" / SAVE_NAMESPACE / "rpg2/map020.npc", encoding="utf-8-sig")
    bound = [saved.get(section, "ScriptFile", fallback="") for section in saved.sections()
             if saved.get(section, "Name", fallback="") == "九头蛟"]
    if bound != ["九头蛟找药.txt"] or accepted["player"]["money"] != before["player"]["money"]:
        raise AutomationError(f"Accepted task binding or money differs: {bound}")
    yycs.interact_named(client, output, resource, "九头蛟")
    yycs.transition(client, map_root, "map_019_寒波谷.map", 1, running=True)
    client.save_or_load(2)
    checkpoint(client, output, "hanbo-arrival-slot2")


def hanbo_books(client, output, resource):
    mentor = "script/map/map_019_寒波谷/孟冬林对话.txt"
    sites = [site for site in yycs.inventory(resource)["choices"] if site["path"] == mentor]
    before = checkpoint(client, output, "hanbo-before-mentor")
    yycs.interact_named(client, output, resource, "孟冬林", choice=((sites[0]["id"], 1), (sites[-1]["id"], 0)))
    granted = checkpoint(client, output, "miao-shou-book-granted")
    book = next(row for row in granted["inventory"] if row["file"] == "妙手空空.ini")
    if granted["variables"].get("mdl") != "1" or granted["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Meng Donglin's book grant or fee differs")
    yycs.interact_named(client, output, resource, "孟冬林")
    repeated = checkpoint(client, output, "mentor-repeated-no-reward")
    if repeated["inventory"] != granted["inventory"]:
        raise AutomationError("Mentor repeated his book reward")
    client.act("UseItem", generation=repeated["generation"], slot=book["slot"])
    learned = checkpoint(client, output, "native-stealing-book-used")
    if (learned["variables"].get("touqie") != "1"
            or any(row["file"] == "妙手空空.ini" for row in learned["inventory"])):
        raise AutomationError("Native stealing book did not consume itself and add one skill")
    box_path = "script/map/map_019_寒波谷/宝箱1.txt"
    site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == box_path)
    yycs.interact_named(client, output, resource, "宝箱", position=(24, 3), choice=(site["id"], 0))
    opened = checkpoint(client, output, "hanbo-miao-yu-box-opened")
    book = next(row for row in opened["inventory"] if row["file"] == "妙语连珠.ini")
    client.act("UseItem", generation=opened["generation"], slot=book["slot"])
    used = yycs.idle(client)
    if (used["variables"].get("koucai") != "1"
            or any(row["file"] == "妙语连珠.ini" for row in used["inventory"])):
        raise AutomationError("Miao Yu Lian Zhu did not consume itself and increase koucai")
    client.save_or_load(3)
    before_load = checkpoint(client, output, "hanbo-books-slot3")
    yycs.load_checkpoint(client, 3)
    after_load = checkpoint(client, output, "hanbo-books-reloaded")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if before_load[key] != after_load[key]:
            raise AutomationError(f"Book progress save/load changed {key}")


def alternate_named(client, output, resource, name, choice, target_id=None):
    for attempt in range(4):
        state = yycs.idle(client)
        target = next(row for row in state["targets"] if row["name"] == name and row.get("interactive")
                      and (target_id is None or row["id"] == target_id))
        try:
            client.interact(target["id"], side="alternate", timeout=180)
            break
        except AutomationError as error:
            if str(error) != "Interact: interaction_not_observed" or attempt == 3:
                raise
    return yycs.idle(client, choice=choice, output=output, resource=resource)


def check_used_object(client, output, name, position):
    before = checkpoint(client, output, name + "-before-repeat")
    target = next(row for row in before["targets"] if row["kind"] == "object"
                  and row["position"] == dict(x=position[0], y=position[1]))
    try:
        client.interact(target["id"], timeout=15)
    except AutomationError as error:
        if str(error) not in ("Interact: target_not_interactive", "Interact: interaction_not_observed", "Interact: action_rejected"):
            raise
    after = checkpoint(client, output, name + "-after-repeat")
    if after["inventory"] != before["inventory"] or after["player"]["money"] != before["player"]["money"]:
        raise AutomationError(f"Used object repeated its reward: {name}")


def verify_script(output, resource, path):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        starts = [row for row in yycs.trace_records(output)
                  if row.get("eventType") == "script.start" and row.get("virtualPath") == path]
        if starts:
            if starts[-1]["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
                raise AutomationError(f"Executed source differs: {path}")
            return yycs.completed_script(output, starts[-1])
        time.sleep(0.05)
    raise AutomationError(f"Native script start is missing: {path}")


def inventory_with_stock(inventory, resource, filename):
    expected = {row["file"]: row["quantity"] for row in inventory}
    stock = configparser.ConfigParser(interpolation=None)
    stock.read(resource / "ini/buy" / filename, encoding="utf-8-sig")
    for section in stock.sections():
        if stock.has_option(section, "IniFile"):
            key = stock.get(section, "IniFile")
            expected[key] = expected.get(key, 0) + stock.getint(section, "Number")
    return expected


def hanbo_persuasion(client, output, resource):
    check_used_object(client, output, "miao-yu-box", (24, 3))
    path = "script/map/map_019_寒波谷/洪朝对话.txt"
    sites = [site for site in yycs.inventory(resource)["choices"] if site["path"] == path]
    before = checkpoint(client, output, "hongchao-before-persuasion")
    yycs.interact_named(client, output, resource, "洪朝", choice=tuple((site["id"], 0) for site in sites))
    after = checkpoint(client, output, "hongchao-allowed-entry")
    guard = next(row for row in after["targets"] if row["name"] == "洪朝")
    if (guard["position"] != dict(x=27, y=83) or guard["hostile"]
            or before["variables"].get("koucai") != "1" or after["variables"].get("koucai") != "1"):
        raise AutomationError("Koucai persuasion did not move the guard or preserve the ability")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_019_寒波谷/宝箱2.txt")
    yycs.interact_named(client, output, resource, "宝箱", position=(31, 82), choice=(site["id"], 0))
    reward = checkpoint(client, output, "hanbo-second-box-money")
    if not 50 <= reward["player"]["money"] - after["player"]["money"] <= 500:
        raise AutomationError("Hanbo second box reward is outside its source range")
    check_used_object(client, output, "hanbo-money-box", (31, 82))
    client.save_or_load(4)
    checkpoint(client, output, "hanbo-persuasion-boxes-slot4")


def trade_graves(client, output, resource):
    before = checkpoint(client, output, "grave-before-shovel")
    yycs.interact_named(client, output, resource, "坟墓", position=(45, 24))
    rejected = checkpoint(client, output, "grave-needs-shovel")
    if rejected["inventory"] != before["inventory"] or rejected["variables"]["event"] != before["variables"]["event"]:
        raise AutomationError("Grave granted rewards before obtaining a shovel")
    state = yycs.idle(client)
    merchant = next(row for row in state["targets"] if row["name"] == "孟冬林")
    client.interact(merchant["id"], side="alternate", timeout=180)
    choice = client.wait_until(lambda value: bool(value.get("choices")), description="native merchant actions")
    path = "script/map/map_019_寒波谷/右键孟冬林.txt"
    site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == path)
    if not yycs.choice_matches_source(site, choice, 0):
        raise AutomationError("Merchant action choices do not match source")
    checkpoint(client, output, "shovel-merchant-choice")
    client.act("Choose", context=choice["context"], options=[0])
    client.wait_until(lambda value: "shop" in value, description="native shovel shop")
    shop = checkpoint(client, output, "shovel-shop-before")
    offered = next(row for row in shop["shop"] if row["file"] == "铁锹.ini")
    client.buy(offered["slot"])
    bought = checkpoint(client, output, "shovel-purchased")
    if (bought["player"]["money"] != shop["player"]["money"] - 200
            or sum(row["quantity"] for row in bought["inventory"] if row["file"] == "铁锹.ini") != 1
            or any(row["file"] == "铁锹.ini" and row["quantity"] > 0 for row in bought["shop"])):
        raise AutomationError("Native shovel purchase differs from its 200-tael price or finite stock")
    client.ui("Cancel")
    yycs.idle(client)
    start = next(row for row in reversed(yycs.trace_records(output))
                 if row.get("eventType") == "script.start" and row.get("virtualPath") == path)
    if start["contentSha256"] != site["sourceSha256"]:
        raise AutomationError("Merchant source changed")
    yycs.completed_script(output, start)
    client.save_or_load(5)
    for grave_map, position, name in (("map_019_寒波谷", (45, 24), "hanbo-grave"),
                                     ("map_020_樱花谷", (14, 37), "sakura-grave")):
        state = yycs.idle(client)
        if state["map"] != grave_map + ".map":
            yycs.transition(client, resource.parent / "yycs", grave_map + ".map", 2, running=True)
        before = checkpoint(client, output, name + "-before")
        path = f"script/map/{grave_map}/坟墓.txt"
        site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == path)
        yycs.interact_named(client, output, resource, "坟墓", position=position, choice=(site["id"], 0))
        after = checkpoint(client, output, name + "-reward")
        if (int(after["variables"]["event"]) != int(before["variables"]["event"]) - 1
                or sum(row["quantity"] for row in after["inventory"] if row["file"] == "铁锹.ini") != 1):
            raise AutomationError("Grave crime counter or retained shovel differs")
        if grave_map == "map_019_寒波谷":
            if sum(row["quantity"] for row in after["inventory"]) != sum(row["quantity"] for row in before["inventory"]) + 1:
                raise AutomationError("Hanbo grave did not grant one random third-level weapon")
        elif not 3000 <= after["player"]["money"] - before["player"]["money"] <= 4000:
            raise AutomationError("Sakura grave money is outside its source range")
        check_used_object(client, output, name, position)
    client.save_or_load(6)
    saved = checkpoint(client, output, "grave-progress-slot6")
    yycs.load_checkpoint(client, 6)
    loaded = checkpoint(client, output, "grave-progress-reloaded")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if saved[key] != loaded[key]:
            raise AutomationError(f"Grave progress save/load changed {key}")


def changqi_hostility(client, output, resource):
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_019_寒波谷/右键昌齐.txt")
    before = checkpoint(client, output, "changqi-steal-before")
    alternate_named(client, output, resource, "昌齐", (site["id"], 1))
    stolen = checkpoint(client, output, "changqi-steal-succeeded")
    if (stolen["variables"].get("toucq") != "1"
            or not 10 <= stolen["player"]["money"] - before["player"]["money"] <= 50):
        raise AutomationError("Chang Qi stealing reward differs from source")
    alternate_named(client, output, resource, "昌齐", (site["id"], 1))
    repeated = checkpoint(client, output, "changqi-steal-exhausted")
    if repeated["player"]["money"] != stolen["player"]["money"] or repeated["inventory"] != stolen["inventory"]:
        raise AutomationError("Chang Qi stealing repeated its reward")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_019_寒波谷/昌齐对话.txt")
    for count in range(9):
        yycs.interact_named(client, output, resource, "昌齐", choice=(site["id"], 2))
        state = checkpoint(client, output, f"changqi-insult-{count + 1}")
        target = next(row for row in state["targets"] if row["name"] == "昌齐")
        if state["variables"].get("cqyhd") != str(9 - count) or target["hostile"] != (count == 8):
            raise AutomationError("Chang Qi hostility did not follow the source friendship threshold")
    client.save_or_load(0)
    yycs.fight_named(client, output, resource, "昌齐")
    verify_script(output, resource, "script/map/map_019_寒波谷/昌齐死亡.txt")
    client.save_or_load(1)
    checkpoint(client, output, "changqi-normal-death-slot1")


def hu_entry(client, output, resource):
    yycs.transition(client, resource.parent / "yycs", "map_018_连接地图.map", 1, running=True)
    client.save_or_load(0)
    checkpoint(client, output, "hu-dedi-before-choice-slot0")


def hu_choice(client, output, resource, option):
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_018_连接地图/胡德第对话.txt")
    expected = site["path"] if option == 0 else None
    state = yycs.idle(client)
    target = next(row for row in state["targets"] if row["name"] == "胡德第")
    client.interact(target["id"], timeout=180)
    after = yycs.choose_site(client, output, resource, site["id"], option, expected_terminal=expected)
    checkpoint(client, output, f"hu-dedi-option-{option}-result")
    if option == 0:
        if after["scene"] != "Title":
            raise AutomationError("Hu Dedi's accepted execution did not finish at the title")
        return
    if not any(row["name"] == "胡德第" and row["hostile"] for row in after["targets"]):
        raise AutomationError("Hu Dedi's confrontation did not start the source battle")
    client.save_or_load(1)
    yycs.fight_named(client, output, resource, "胡德第")
    verify_script(output, resource, "script/map/map_018_连接地图/胡德第死亡.txt")
    client.save_or_load(2)
    checkpoint(client, output, "hu-dedi-defeated-slot2")


def yih_entry(client, output, resource):
    yycs.transition(client, resource.parent / "yycs", "map_017_连接地图.map", 1, running=True)
    client.save_or_load(3)
    checkpoint(client, output, "yihe-before-training-slot3")


def yih_choice(client, output, resource, option):
    def attributes(slot):
        saved = configparser.ConfigParser(interpolation=None, strict=False)
        saved.read(output / "user-data/save" / SAVE_NAMESPACE / f"rpg{slot + 1}/player0.ini", encoding="utf-8-sig")
        return {name: saved.getint("init", name) for name in ("Attack", "LifeMax", "ManaMax", "ThewMax")}
    before = attributes(3)
    money = client.observe()["player"]["money"]
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_017_连接地图/议和对话.txt"]
    yycs.interact_named(client, output, resource, "议和", choice=((sites[0]["id"], 1), (sites[1]["id"], option)))
    client.save_or_load(4)
    after = checkpoint(client, output, f"yihe-training-option-{option}-slot4")
    expected = {key: value + (5 if key == "Attack" else 10) for key, value in before.items()} if option == 0 else before
    if (attributes(4) != expected or after["variables"].get("yh") != ("1" if option == 0 else "2")
            or after["player"]["money"] != money):
        raise AutomationError("Yihe training/refusal reward, flag or money differs from source")
    yycs.interact_named(client, output, resource, "议和")
    client.save_or_load(5)
    if attributes(5) != expected:
        raise AutomationError("Yihe training reward repeated after the result was settled")
    checkpoint(client, output, "yihe-result-cannot-repeat-slot5")


def huian_entry(client, output, resource):
    for destination, trap in (("map_014_连接地图.map", 1), ("map_012_惠安镇.map", 1)):
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    client.save_or_load(6)
    checkpoint(client, output, "huian-initial-arrival-slot6")


def qingping_entry(client, output, resource):
    for destination, trap in (("map_014_连接地图.map", 2), ("map_017_连接地图.map", 3),
                              ("map_023_连接地图.map", 3), ("map_024_倚天山.map", 1),
                              ("map_021_油菜花地.map", 1), ("map_022_清平乡.map", 2)):
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
        checkpoint(client, output, "arrival-" + destination.split("_")[1])
    client.save_or_load(0)
    checkpoint(client, output, "qingping-initial-arrival-slot0")


def town_report(client, output, resource):
    before = checkpoint(client, output, "town-report-before")
    path = "script/map/map_012_惠安镇/战渺渺对话.txt"
    sites = [site for site in yycs.inventory(resource)["choices"] if site["path"] == path]
    if not before.get("script", "").endswith("李捕头举报.txt"):
        yycs.interact_named(client, output, resource, "战渺渺", choice=((sites[0]["id"], 0), (sites[1]["id"], 1)))
        refused = checkpoint(client, output, "zhan-miaomiao-refused")
        if refused["variables"].get("jbzmm") != "1" or refused["inventory"] != before["inventory"]:
            raise AutomationError("Zhan Miaomiao refusal changed items or missed the report condition")
        yycs.interact_named(client, output, resource, "李捕头")
    path = "script/map/map_012_惠安镇/李捕头举报.txt"
    site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == path)
    if client.observe().get("choices"):
        yycs.choose_site(client, output, resource, site["id"], 2)
    else:
        yycs.interact_named(client, output, resource, "李捕头", choice=(site["id"], 2))
    reported = checkpoint(client, output, "zhan-miaomiao-reported")
    if (reported["variables"].get("jbzmm") != "0" or reported["variables"].get("event") != "100"
            or any(row["name"] == "战渺渺" for row in reported["targets"])
            or sum(row["quantity"] for row in reported["inventory"] if row["file"] == "技能书.ini") != 1
            or reported["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Town report did not remove its NPC and grant one skill book")
    yycs.interact_named(client, output, resource, "李捕头", choice=(site["id"], 5))
    repeated = checkpoint(client, output, "town-report-cannot-repeat")
    if repeated["inventory"] != reported["inventory"]:
        raise AutomationError("Town report repeated its reward")
    client.save_or_load(1)
    yycs.load_checkpoint(client, 1)
    loaded = checkpoint(client, output, "town-report-reloaded-slot1")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if loaded[key] != repeated[key]:
            raise AutomationError(f"Town report save/load changed {key}")


def qingping_books(client, output, resource):
    path = "script/map/map_022_清平乡/宝箱3.txt"
    site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == path)
    before = checkpoint(client, output, "qingping-third-box-before-skill")
    yycs.interact_named(client, output, resource, "宝箱", position=(43, 10), choice=(site["id"], 0))
    locked = checkpoint(client, output, "qingping-third-box-needs-stealing-two")
    if locked["inventory"] != before["inventory"] or locked["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Qingping third box rewarded insufficient stealing")
    for number, position, book, variable in ((1, (64, 23), "妙手空空.ini", "touqie"),
                                             (2, (60, 71), "妙语连珠.ini", "koucai")):
        path = f"script/map/map_022_清平乡/宝箱{number}.txt"
        site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == path)
        yycs.interact_named(client, output, resource, "宝箱", position=position, choice=(site["id"], 0))
        granted = checkpoint(client, output, f"qingping-book-{number}-granted")
        item = next(row for row in granted["inventory"] if row["file"] == book)
        client.act("UseItem", generation=granted["generation"], slot=item["slot"])
        used = checkpoint(client, output, f"qingping-book-{number}-used")
        if used["variables"].get(variable) != "2" or any(row["file"] == book for row in used["inventory"]):
            raise AutomationError(f"Qingping book consumption or skill increase differs: {book}")
        check_used_object(client, output, f"qingping-book-{number}-box", position)
    before = checkpoint(client, output, "qingping-third-box-after-skill")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_022_清平乡/宝箱3.txt")
    yycs.interact_named(client, output, resource, "宝箱", position=(43, 10), choice=(site["id"], 0))
    opened = checkpoint(client, output, "qingping-third-box-reward")
    if not 10 <= opened["player"]["money"] - before["player"]["money"] <= 40:
        raise AutomationError("Qingping third box money is outside its source range")
    check_used_object(client, output, "qingping-third-box", (43, 10))
    client.save_or_load(1)
    checkpoint(client, output, "qingping-books-slot1")


def medical_lesson(client, output, resource):
    before = checkpoint(client, output, "medical-lesson-before")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_022_清平乡/甘新对话.txt"]
    yycs.interact_named(client, output, resource, "甘新", choice=((sites[0]["id"], 1), (sites[1]["id"], 0)))
    after = checkpoint(client, output, "medical-lesson-after")
    client.save_or_load(2)
    money = before["player"]["money"]
    old_level = int(before["variables"].get("yiliao") or 0)
    expected_money = money - 100 if money >= 100 else money
    expected_level = old_level + 1 if money >= 100 else old_level
    if after["player"]["money"] != expected_money or int(after["variables"].get("yiliao") or 0) != expected_level:
        raise AutomationError(f"Medical lesson violates its stated 100-tael fee: {money} -> {after['player']['money']}, medical {old_level} -> {after['variables'].get('yiliao')}")


def medical_full(client, output, resource):
    initial = checkpoint(client, output, "medical-training-initial")
    if initial["variables"].get("yiliao") or initial["player"]["money"] < 1010:
        raise AutomationError("Full medical training requires its normal untrained source and tuition")
    for number in range(10):
        medical_lesson(client, output, resource)
        state = checkpoint(client, output, f"medical-training-{number + 1}")
        if state["variables"].get("yiliao") != str(number + 1):
            raise AutomationError("Medical lessons did not advance exactly once per payment")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_022_清平乡/甘新对话.txt"]
    yycs.interact_named(client, output, resource, "甘新", choice=(sites[0]["id"], 1))
    rewarded = checkpoint(client, output, "medical-training-completed-refund")
    spell = next((row for row in rewarded["magic"] if row["file"] == "player-magic-清心咒.ini"), None)
    if (rewarded["variables"].get("ganxin") != "1" or rewarded["variables"].get("yiliao") != "10"
            or rewarded["player"]["money"] != initial["player"]["money"] or not spell
            or len(rewarded["magic"]) != len(initial["magic"]) + 1):
        raise AutomationError("Full medical training did not refund tuition and grant its learned healing magic")
    yycs.interact_named(client, output, resource, "甘新", choice=(sites[0]["id"], 1))
    repeated = checkpoint(client, output, "medical-graduation-cannot-repeat")
    if repeated["magic"] != rewarded["magic"] or repeated["player"]["money"] != rewarded["player"]["money"]:
        raise AutomationError("Medical graduation repeated its rewards")
    assigned = client.assign_magic(spell["slot"], 0)
    client.act("CastSkill", generation=assigned["generation"], slot=0)
    healed = client.wait_until(lambda value: value["player"]["life"] > assigned["player"]["life"],
                               timeout=15, description="learned healing magic effect")
    if healed["player"]["mana"] != assigned["player"]["mana"] - 6:
        raise AutomationError("Level-one healing magic did not use its defined six mana")
    checkpoint(client, output, "learned-qingxinzhou-healed-player")
    yycs.interact_named(client, output, resource, "甘新", choice=((sites[0]["id"], 3), (sites[2]["id"], 0)))
    treated = checkpoint(client, output, "native-treatment-ten-taels")
    if (treated["player"]["life"] != treated["player"]["lifeMax"]
            or treated["player"]["mana"] != treated["player"]["manaMax"]
            or treated["player"]["money"] != initial["player"]["money"] - 10):
        raise AutomationError("Native treatment did not restore life/mana for ten taels")
    client.save_or_load(3)
    saved = checkpoint(client, output, "medical-graduation-slot3")
    yycs.load_checkpoint(client, 3)
    loaded = checkpoint(client, output, "medical-graduation-reloaded")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if saved[key] != loaded[key]:
            raise AutomationError(f"Medical graduation save/load changed {key}")


def su_recruit(client, output, resource):
    yycs.transition(client, resource.parent / "yycs", "map_021_油菜花地.map", 1, running=True)
    before = checkpoint(client, output, "su-yingying-recruit-before")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_021_油菜花地/苏莹莹对话.txt"]
    yycs.interact_named(client, output, resource, "苏莹莹", choice=((sites[0]["id"], 0), (sites[1]["id"], 0)))
    after = checkpoint(client, output, "su-yingying-recruited")
    partner = next(row for row in after["targets"] if row["name"] == "苏莹莹")
    if partner["hostile"] or after["player"]["money"] != before["player"]["money"] - 500:
        raise AutomationError("Su Yingying recruitment hostility or fee differs")
    client.save_or_load(2)
    checkpoint(client, output, "su-yingying-recruited-slot2")


def su_management(client, output, resource):
    def saved_partner(slot):
        saved = configparser.ConfigParser(interpolation=None, strict=False)
        saved.read(output / "user-data/save" / SAVE_NAMESPACE / f"rpg{slot + 1}/partner0.ini", encoding="utf-8-sig")
        sections = [section for section in saved.sections() if saved.get(section, "name", fallback="") == "苏莹莹"]
        if len(sections) != 1:
            raise AutomationError("Su Yingying normal partner save is missing or duplicated")
        return dict(saved[sections[0]])
    original = saved_partner(2)
    if (original.get("kind") != "3" or original.get("relation") != "0" or original.get("deathscript")
            or original.get("scriptfile") != "右键苏莹莹队友.txt"):
        raise AutomationError("Su Yingying recruited binding differs from source")
    before = checkpoint(client, output, "su-equipment-before")
    sword = next(row for row in before["inventory"] if row["file"] == "goods-w12-桃木剑.ini")
    client.activate("partner-head-0")
    client.wait_until(lambda value: any(row["name"].startswith("partner-equipment-item-") for row in value["ui"]),
                      description="native partner equipment menu")
    client.focus_slot("partner-player-bag-item-", sword["slot"])
    client.ui("Confirm")
    checkpoint(client, output, "su-equipment-native-menu")
    for _ in range(3):
        if client.observe().get("worldInput"):
            break
        client.ui("Cancel")
    client.save_or_load(3)
    equipped = saved_partner(3)
    after = checkpoint(client, output, "su-sword-equipped-slot3")
    if (equipped.get("handequip") != sword["file"]
            or equipped["attack"] != original["attack"]
            or any(row["file"] == sword["file"] for row in after["inventory"])
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Native partner equipment did not transfer the sword while preserving base attack and money")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/common/右键苏莹莹队友.txt")
    yycs.interact_named(client, output, resource, "苏莹莹", choice=(site["id"], 2))
    left = checkpoint(client, output, "su-left-party")
    if any(row["name"] == "partner-head-0" for row in left["ui"]):
        raise AutomationError("Su Yingying remained in the partner menu after leaving")
    yycs.interact_named(client, output, resource, "苏莹莹", choice=(site["id"], 1))
    joined = checkpoint(client, output, "su-rejoined-party")
    if not any(row["name"] == "partner-head-0" for row in joined["ui"]):
        raise AutomationError("Su Yingying rejoining did not restore her partner menu")
    yycs.transition(client, resource.parent / "yycs", "map_022_清平乡.map", 2, running=True)
    arrived = checkpoint(client, output, "su-followed-qingping-transition")
    if len([row for row in arrived["targets"] if row["name"] == "苏莹莹"]) != 1:
        raise AutomationError("Su Yingying did not follow the normal map transition exactly once")
    player = configparser.ConfigParser(interpolation=None, strict=False)
    player.read(output / "user-data/save" / SAVE_NAMESPACE / "rpg4/player0.ini", encoding="utf-8-sig")
    maximum_thew = player.getint("init", "ThewMax")
    client.wait_until(lambda value: value["player"]["thew"] == maximum_thew,
                      timeout=60, description="normal standing recovery before stable save/load comparison")
    client.save_or_load(4)
    saved = checkpoint(client, output, "su-follow-equipment-slot4")
    persisted = saved_partner(4)
    yycs.load_checkpoint(client, 4)
    loaded = checkpoint(client, output, "su-follow-equipment-reloaded")
    if persisted.get("handequip") != sword["file"] or persisted.get("kind") != "3":
        raise AutomationError("Partner equipment or party membership was not saved")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if saved[key] != loaded[key]:
            raise AutomationError(f"Partner normal save/load changed {key}")
    if len([row for row in loaded["targets"] if row["name"] == "苏莹莹"]) != 1:
        raise AutomationError("Partner normal reload lost or duplicated Su Yingying")


def tong_medical(client, output, resource):
    for destination, trap in (("map_021_油菜花地.map", 1), ("map_024_倚天山.map", 1),
                              ("map_023_连接地图.map", 3), ("map_017_连接地图.map", 2),
                              ("map_014_连接地图.map", 1), ("map_012_惠安镇.map", 1),
                              ("map_027_连接地图.map", 3)):
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    client.save_or_load(4)
    before = checkpoint(client, output, "tong-gui-before-medical-slot4")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_027_连接地图/童贵对话.txt"]
    yycs.interact_named(client, output, resource, "童贵", choice=((sites[0]["id"], 0), (sites[1]["id"], 1)))
    after = checkpoint(client, output, "tong-gui-medical-ginseng")
    if (before["variables"].get("yiliao") != "10" or after["variables"].get("tgqnrs") != "1"
            or sum(row["quantity"] for row in after["inventory"] if row["file"] == "千年人参.ini") != 1
            or after["player"]["money"] != before["player"]["money"]
            or after["variables"].get("event") != before["variables"].get("event")):
        raise AutomationError("Tong Gui medical branch did not grant one ginseng without charging or crime")
    yycs.interact_named(client, output, resource, "童贵")
    repeated = checkpoint(client, output, "tong-gui-ginseng-cannot-repeat")
    if repeated["inventory"] != after["inventory"]:
        raise AutomationError("Tong Gui medical reward repeated")
    client.save_or_load(5)
    checkpoint(client, output, "ginseng-obtained-slot5")


def return_ginseng(client, output, resource):
    for destination, trap in (("map_012_惠安镇.map", 2), ("map_014_连接地图.map", 2),
                              ("map_017_连接地图.map", 3), ("map_018_连接地图.map", 2),
                              ("map_019_寒波谷.map", 2), ("map_020_樱花谷.map", 2)):
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    client.save_or_load(6)
    saved = configparser.ConfigParser(interpolation=None, strict=False)
    saved.read(output / "user-data/save" / SAVE_NAMESPACE / "rpg7/map020.npc", encoding="utf-8-sig")
    bound = [saved.get(section, "ScriptFile", fallback="") for section in saved.sections()
             if saved.get(section, "Name", fallback="") == "九头蛟"]
    if bound != ["九头蛟结局.txt"]:
        raise AutomationError("Ginseng acquisition did not bind the remote final conversation")
    checkpoint(client, output, "ginseng-return-before-final-slot6")


def jiutou_offer(client, output, resource, option):
    before = checkpoint(client, output, "jiutou-final-before-offer")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_020_樱花谷/九头蛟结局.txt"]
    yycs.interact_named(client, output, resource, "九头蛟", choice=((sites[0]["id"], 0), (sites[1]["id"], option)))
    after = checkpoint(client, output, f"jiutou-final-offer-option-{option}")
    old_books = sum(row["quantity"] for row in before["inventory"] if row["file"] == "技能书.ini")
    new_books = sum(row["quantity"] for row in after["inventory"] if row["file"] == "技能书.ini")
    if (any(row["file"] == "千年人参.ini" for row in after["inventory"])
            or new_books != old_books + (option == 2)
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Jiutoujiao final offering consumption, reward or money differs")
    hostile = any(row["name"] == "九头蛟" and row["hostile"] for row in after["targets"])
    if hostile != (option == 1) or option != 1 and after["variables"].get("lk") != "1":
        raise AutomationError("Jiutoujiao final offering branch did not set its actual result")
    client.save_or_load(0)
    checkpoint(client, output, f"jiutou-offer-option-{option}-slot0")
    if option == 1:
        jiutou_ending(client, output, resource)


def jiutou_ending(client, output, resource):
    yycs.fight_named(client, output, resource, "九头蛟", use_magic=True, single_target=True)
    verify_script(output, resource, "script/map/map_020_樱花谷/九头蛟结局死亡.txt")
    completed = checkpoint(client, output, "jiutou-credits-completed-still-playing")
    if completed["scene"] == "Title" or completed["map"] != "map_020_樱花谷.map" or not completed["worldInput"]:
        raise AutomationError("Final credits did not return to the actual playable map")
    client.save_or_load(1)
    jiutou_postgame(client, output, resource)


def jiutou_gift(client, output, resource, option):
    folder = "script/map/map_020_樱花谷/"
    if option == 4:
        yycs.transition(client, resource.parent / "yycs", "map_020_樱花谷.map", 2, running=True)
        path = folder + "九头蛟找人参.txt"
        site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == path)
        for index in range(4):
            yycs.interact_named(client, output, resource, "九头蛟", choice=(site["id"], 0))
            verify_script(output, resource, path)
            after = checkpoint(client, output, f"jiutou-four-requests-{index + 1}")
            if after["variables"].get("jtjbnf") != str(index + 1):
                raise AutomationError("Normal persuasion did not advance Jiutoujiao's request counter")
        yycs.interact_named(client, output, resource, "九头蛟")
        client.save_or_load(0)
        saved = configparser.ConfigParser(interpolation=None, strict=False)
        saved.read(output / "user-data/save" / SAVE_NAMESPACE / "rpg1/map020.npc", encoding="utf-8-sig")
        if not any(saved.get(section, "Name", fallback="") == "九头蛟"
                   and saved.get(section, "ScriptFile", fallback="") == "九头蛟送礼.txt" for section in saved):
            raise AutomationError("Four requests did not bind the actual supplies conversation")
        checkpoint(client, output, "jiutou-gift-normal-source-slot0")
        return
    path = folder + "九头蛟送礼.txt"
    site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == path)
    before = checkpoint(client, output, "jiutou-gift-before")
    yycs.interact_named(client, output, resource, "九头蛟", choice=(site["id"], option))
    verify_script(output, resource, path)
    after = checkpoint(client, output, "jiutou-gift-result")
    expected = {row["file"]: row["quantity"] for row in before["inventory"]}
    if option == 0:
        for filename in ("goods-m00-金花.ini", "goods-m01-银花.ini", "goods-m14-连翘.ini"):
            expected[filename] = expected.get(filename, 0) + 10
    if ({row["file"]: row["quantity"] for row in after["inventory"]} != expected
            or after["player"]["money"] != before["player"]["money"]
            or after["variables"]["event"] != before["variables"]["event"]):
        raise AutomationError("Jiutoujiao's actual supplies or refusal result differs")
    if option == 0:
        path = folder + "九头蛟疯.txt"
        site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == path)
        yycs.interact_named(client, output, resource, "九头蛟", choice=(site["id"], 1))
        greeting = checkpoint(client, output, "jiutou-gift-greeting-no-repeat")
        if greeting["inventory"] != after["inventory"]:
            raise AutomationError("Jiutoujiao repeated his supplies")
        yycs.interact_named(client, output, resource, "九头蛟", choice=(site["id"], 0))
        verify_script(output, resource, path)
    if option != 2:
        hostile = checkpoint(client, output, "jiutou-gift-native-hostility")
        if not any(row["name"] == "九头蛟" and row["hostile"] for row in hostile["targets"]):
            raise AutomationError("Jiutoujiao's attack branch did not become hostile")
        yuemeier.assist_battle(client, output, 80)
        yycs.fight_named(client, output, resource, "九头蛟", use_magic=True, single_target=True)
        verify_script(output, resource, folder + "九头蛟死亡.txt")
        if client.observe(("jtjsw",))["variables"].get("jtjsw") != "1":
            raise AutomationError("Early supplies-branch death did not open Silver Mask")
    client.save_or_load(1)
    checkpoint(client, output, "jiutou-gift-normal-result-slot1")


def jiutou_friendliness(client, output, resource):
    path = "script/map/map_020_樱花谷/九头蛟结局.txt"
    site = [site for site in yycs.inventory(resource)["choices"] if site["path"] == path][-1]
    before = checkpoint(client, output, "jiutou-leave-promise-before")
    for index in range(9):
        yycs.interact_named(client, output, resource, "九头蛟", choice=(site["id"], 1))
        after = checkpoint(client, output, f"jiutou-leave-promise-{index + 1}")
        if (after["variables"].get("jtjyhd") != str(9 - index)
                or after["inventory"] != before["inventory"]
                or after["player"]["money"] != before["player"]["money"]
                or any(row["name"] == "九头蛟" and row["hostile"] for row in after["targets"]) != (index == 8)):
            raise AutomationError("Jiutoujiao's actual final friendliness threshold differs")
    client.save_or_load(0)
    jiutou_ending(client, output, resource)


def jiutou_postgame(client, output, resource):
    verify_script(output, resource, "script/map/map_020_樱花谷/九头蛟结局死亡.txt")
    if yycs.idle(client)["map"] == "map_020_樱花谷.map":
        yycs.transition(client, resource.parent / "yycs", "map_019_寒波谷.map", 1, running=True)
    state = yycs.idle(client)
    mentor = next(row for row in state["targets"] if row["name"] == "孟冬林")
    yuemeier.approach_target(client, output, resource, (mentor["position"]["x"], mentor["position"]["y"]))
    yycs.interact_named(client, output, resource, "孟冬林")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_019_寒波谷/右键昌齐.txt")
    alternate_named(client, output, resource, "昌齐", (site["id"], 2))
    yycs.fight_named(client, output, resource, "昌齐", use_magic=True, single_target=True)
    verify_script(output, resource, "script/map/map_019_寒波谷/昌齐死亡.txt")
    player = configparser.ConfigParser(interpolation=None, strict=False)
    player.read(output / "user-data/save" / SAVE_NAMESPACE / "rpg2/player0.ini", encoding="utf-8-sig")
    client.wait_until(lambda value: value["player"]["thew"] == player.getint("init", "ThewMax"),
                      timeout=90, description="normal postgame recovery before save/load")
    client.save_or_load(2)
    saved = checkpoint(client, output, "postgame-movement-interaction-combat-slot2")
    yycs.load_checkpoint(client, 2)
    loaded = checkpoint(client, output, "postgame-reloaded")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if saved[key] != loaded[key]:
            raise AutomationError(f"Postgame normal save/load changed {key}")
    write_json(output / "jiutou-ending-acceptance.json", dict(ending="九头蛟结局死亡.txt", creditsScriptCompleted=True,
               stillPlayable=True, normalMovementAndTransition=True, normalInteraction=True, normalBattleAndDeath=True,
               normalSaveLoad=True, cheatAssisted=saved["cheatAssisted"], overallFullCoverage=False))


def jiutou_refuse(client, output, resource):
    before = checkpoint(client, output, "jiutou-final-refuse-before")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_020_樱花谷/九头蛟结局.txt")
    yycs.interact_named(client, output, resource, "九头蛟", choice=(site["id"], 1))
    after = checkpoint(client, output, "jiutou-final-refuse-keeps-ginseng")
    if (after["inventory"] != before["inventory"] or after["player"]["money"] != before["player"]["money"]
            or not any(row["name"] == "九头蛟" and row["hostile"] for row in after["targets"])):
        raise AutomationError("Refusing the final offering did not retain inventory and start battle")
    client.save_or_load(0)
    jiutou_ending(client, output, resource)


def qingping_help(client, output, resource):
    before = checkpoint(client, output, "qingping-help-before")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_022_清平乡/元义方对话.txt"]
    yycs.interact_named(client, output, resource, "元义方", choice=((sites[0]["id"], 1), (sites[1]["id"], 0)))
    farmer = checkpoint(client, output, "yuan-yifang-rotation-reward")
    if (any(row["name"] == "元义方" for row in farmer["targets"])
            or sum(row["quantity"] for row in farmer["inventory"] if row["file"] == "技能书.ini")
            != sum(row["quantity"] for row in before["inventory"] if row["file"] == "技能书.ini") + 1
            or farmer["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Crop rotation did not grant one book and remove its farmer")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_022_清平乡/敖广对话.txt")
    yycs.interact_named(client, output, resource, "敖广", choice=(site["id"], 0))
    healed = checkpoint(client, output, "ao-guang-medical-reward")
    if (healed["variables"].get("aoguang") != "1" or int(before["variables"].get("yiliao") or "0") < 1
            or sum(row["quantity"] for row in healed["inventory"] if row["file"] == "技能书.ini")
            != sum(row["quantity"] for row in farmer["inventory"] if row["file"] == "技能书.ini") + 1
            or healed["player"]["money"] != farmer["player"]["money"]):
        raise AutomationError("Ao Guang's native medical branch reward differs")
    yycs.interact_named(client, output, resource, "敖广")
    if checkpoint(client, output, "ao-guang-repeated-no-reward")["inventory"] != healed["inventory"]:
        raise AutomationError("Ao Guang repeated his reward")
    client.save_or_load(0)
    checkpoint(client, output, "qingping-help-slot0")


def debt_start(client, output, resource):
    before = checkpoint(client, output, "debt-before-acceptance")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_022_清平乡/林灵素对话.txt"]
    yycs.interact_named(client, output, resource, "林灵素", choice=((sites[0]["id"], 0), (sites[1]["id"], 0)))
    accepted = checkpoint(client, output, "debt-accepted")
    if accepted["variables"].get("llsan") != "1" or accepted["inventory"] != before["inventory"]:
        raise AutomationError("Lin Lingsu debt acceptance differs")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_022_清平乡/阿牛对话.txt"]
    yycs.interact_named(client, output, resource, "阿牛", choice=tuple((sites[index]["id"], option)
                       for index, option in enumerate((0, 1, 1, 0))))
    collected = checkpoint(client, output, "debt-anu-borrowed-and-returned-100")
    if (collected["variables"].get("llsan") != "2" or collected["inventory"] != before["inventory"]
            or collected["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Anu's first loan did not return 100 and bind debt completion")
    client.save_or_load(0)
    checkpoint(client, output, "debt-before-settlement-slot0")


def debt_settle(client, output, resource, option):
    if option not in (0, 1):
        raise AutomationError("Debt settlement requires the actual lie or repayment option")
    before = checkpoint(client, output, "debt-before-settlement")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_022_清平乡/林灵素对话.txt"]
    yycs.interact_named(client, output, resource, "林灵素", choice=(sites[-1]["id"], option))
    after = checkpoint(client, output, f"debt-settled-option-{option}")
    if (after["player"]["money"] != before["player"]["money"] - (50 if option == 1 else 0)
            or int(after["variables"]["event"]) != int(before["variables"]["event"]) + (1 if option == 1 else -1)
            or after["inventory"] != before["inventory"]):
        raise AutomationError("Debt settlement charge/reward or moral result differs")
    if option == 1:
        yycs.interact_named(client, output, resource, "林灵素")
    repeated = checkpoint(client, output, "debt-cannot-repeat-reward")
    if repeated["player"]["money"] != after["player"]["money"] or repeated["inventory"] != after["inventory"]:
        raise AutomationError("Debt settlement repeated its reward")
    client.save_or_load(1)
    saved = configparser.ConfigParser(interpolation=None, strict=False)
    folder = output / "user-data/save" / SAVE_NAMESPACE / "rpg2"
    game = configparser.ConfigParser(interpolation=None, strict=False)
    game.read(folder / "game.ini", encoding="utf-8-sig")
    saved.read(folder / game.get("state", "npc"), encoding="utf-8-sig")
    bound = [saved.get(section, "ScriptFile", fallback="") for section in saved.sections()
             if saved.get(section, "Name", fallback="") == "林灵素"]
    if bound != ["林灵素感谢.txt" if option == 1 else ""]:
        raise AutomationError("Debt settlement did not change its normal saved primary binding")
    checkpoint(client, output, "debt-settled-slot1")


def anu_second_loan(client, output, resource, option=0):
    before = checkpoint(client, output, "anu-before-second-loan")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_022_清平乡/阿牛再借钱.txt"]
    yycs.interact_named(client, output, resource, "阿牛", choice=((sites[0]["id"], 0), (sites[1]["id"], option)))
    after = checkpoint(client, output, "anu-second-loan-100")
    if option == 1:
        if (after["player"]["money"] != before["player"]["money"]
                or not any(row["name"] == "阿牛" and row["hostile"] for row in after["targets"])):
            raise AutomationError("Attacking instead of a second loan did not start the intended battle")
        yycs.fight_named(client, output, resource, "阿牛", use_magic=True, single_target=True)
        verify_script(output, resource, "script/map/map_022_清平乡/阿牛死亡.txt")
        client.save_or_load(2)
        checkpoint(client, output, "anu-second-loan-attack-result-slot2")
        return
    if after["variables"].get("anfc") != "1" or after["player"]["money"] != before["player"]["money"] - 100:
        raise AutomationError("Anu's second loan charge or state differs")
    client.save_or_load(2)
    checkpoint(client, output, "anu-before-final-return-slot2")


def anu_return(client, output, resource, option):
    before = checkpoint(client, output, "anu-before-final-return")
    site = [site for site in yycs.inventory(resource)["choices"]
            if site["path"] == "script/map/map_022_清平乡/阿牛再借钱.txt"][-1]
    yycs.interact_named(client, output, resource, "阿牛", choice=(site["id"], option))
    after = checkpoint(client, output, f"anu-final-return-option-{option}")
    if option == 0:
        if after["player"]["money"] != before["player"]["money"] + 5000 or any(row["name"] == "阿牛" for row in after["targets"]):
            raise AutomationError("Anu's success reward or departure differs")
    elif option == 1:
        if (after["player"]["money"] != before["player"]["money"]
                or int(after["variables"]["event"]) != int(before["variables"]["event"]) - 1
                or not any(row["name"] == "阿牛" and row["hostile"] for row in after["targets"])):
            raise AutomationError("Extorting Anu did not cause the actual criminal battle")
        yycs.fight_named(client, output, resource, "阿牛", use_magic=True, single_target=True)
        verify_script(output, resource, "script/map/map_022_清平乡/阿牛死亡.txt")
    else:
        raise AutomationError("Anu's final choice has only two actual options")
    client.save_or_load(3)
    checkpoint(client, output, "anu-final-result-slot3")


def early_silver(client, output, resource, steal=False):
    for destination, trap in (("map_014_连接地图.map", 2), ("map_017_连接地图.map", 3),
                              ("map_018_连接地图.map", 2), ("map_019_寒波谷.map", 2),
                              ("map_020_樱花谷.map", 2)):
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_020_樱花谷/右键九头蛟.txt")
    alternate_named(client, output, resource, "九头蛟", (site["id"], 2))
    yycs.fight_named(client, output, resource, "九头蛟", use_magic=True, single_target=True)
    verify_script(output, resource, "script/map/map_020_樱花谷/九头蛟死亡.txt")
    killed = checkpoint(client, output, "early-jiutou-death-opens-silver")
    if killed["variables"].get("jtjsw") != "1":
        raise AutomationError("Early Jiutoujiao death did not expose the Silver Mask branch")
    client.save_or_load(0)
    for destination, trap in (("map_019_寒波谷.map", 1), ("map_018_连接地图.map", 1),
                              ("map_017_连接地图.map", 1), ("map_014_连接地图.map", 1),
                              ("map_012_惠安镇.map", 1), ("map_027_连接地图.map", 3)):
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    client.save_or_load(4)
    before = checkpoint(client, output, "silver-visible-before-assistance-slot4")
    target = next(row for row in before["targets"] if row["name"] == "银面")
    saved = configparser.ConfigParser(interpolation=None, strict=False)
    saved.read(output / "user-data/save" / SAVE_NAMESPACE / "rpg5/map027.npc", encoding="utf-8-sig")
    stats = [{key: saved.get(section, key, fallback="") for key in ("Name", "Level", "Life", "LifeMax", "Attack", "Defend", "ScriptFile", "DeathScript")}
             for section in saved.sections() if saved.get(section, "Name", fallback="") == "银面"]
    write_json(output / "silver-actual-battle-definition.json", dict(target=target, savedStats=stats,
               assistanceReason="实际银面60级、3710生命，对应正常4级玩家，使用已授权原生80级和无敌辅助"))
    if len(stats) != 1 or stats[0]["Level"] != "60" or target["life"] != 3710:
        raise AutomationError("Silver Mask's actual saved level or observed life differs")
    yuemeier.assist_battle(client, output, 80)
    if steal:
        steal_map(client, output, resource, 3)
    yycs.interact_named(client, output, resource, "银面")
    yycs.fight_named(client, output, resource, "银面", use_magic=True, single_target=True)
    verify_script(output, resource, "script/map/map_027_连接地图/银面死亡.txt")
    completed = checkpoint(client, output, "silver-credits-completed-still-playing")
    if not completed["worldInput"] or completed["map"] != "map_027_连接地图.map":
        raise AutomationError("Silver Mask credits did not return to actual playable map")
    client.save_or_load(1)
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_027_连接地图/龙在田对话.txt"]
    yycs.interact_named(client, output, resource, "龙在田", choice=(sites[0]["id"], 2))
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_027_连接地图/右键龙在田.txt")
    alternate_named(client, output, resource, "龙在田", (site["id"], 2))
    yycs.fight_named(client, output, resource, "龙在田", use_magic=True, single_target=True)
    verify_script(output, resource, "script/map/map_027_连接地图/龙在田死亡.txt")
    yycs.transition(client, resource.parent / "yycs", "map_012_惠安镇.map", 2, running=True)
    yycs.recover(client, output, resource)
    client.save_or_load(2)
    stored = checkpoint(client, output, "silver-postgame-move-interact-battle-slot2")
    yycs.load_checkpoint(client, 2)
    loaded = checkpoint(client, output, "silver-postgame-reloaded")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if stored[key] != loaded[key]:
            raise AutomationError(f"Silver postgame save/load changed {key}")
    write_json(output / "silver-ending-acceptance.json", dict(ending="银面死亡.txt", creditsScriptCompleted=True,
               stillPlayable=True, normalMovementAndTransition=True, normalInteraction=True, normalBattleAndDeath=True,
               normalSaveLoad=True, cheatAssisted=True, overallFullCoverage=False))


def dog_recruit(client, output, resource):
    before = checkpoint(client, output, "dog-chickens-before-purchase")
    if before["player"]["money"] < 1000:
        raise AutomationError("The ten actual 100-tael chickens require 1000 taels")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_027_连接地图/龙在田对话.txt"]
    target = next(row for row in before["targets"] if row["name"] == "龙在田")
    yuemeier.approach_target(client, output, resource, (target["position"]["x"], target["position"]["y"]))
    client.interact(target["id"], timeout=180)
    choice = client.wait_until(lambda value: bool(value.get("choices")), description="actual chicken shop choice")
    if not yycs.choice_matches_source(sites[0], choice, 0):
        raise AutomationError("Chicken shop choices do not match their actual source")
    checkpoint(client, output, "dog-chicken-shop-native-choice")
    client.act("Choose", context=choice["context"], options=[0])
    shop = client.wait_until(lambda value: "shop" in value, description="actual chicken shop")
    offered = next(row for row in shop["shop"] if row["file"] == "烧鸡.ini")
    if offered["quantity"] != 10:
        raise AutomationError("Actual chicken shop did not offer ten finite chickens")
    for number in range(1, 11):
        client.buy(offered["slot"])
        bought = checkpoint(client, output, f"dog-chicken-purchase-{number}")
        if (bought["player"]["money"] != before["player"]["money"] - 100 * number
                or sum(row["quantity"] for row in bought["inventory"] if row["file"] == "烧鸡.ini") != number):
            raise AutomationError("Chicken purchase price or quantity differs")
    client.ui("Cancel")
    yycs.idle(client)
    verify_script(output, resource, sites[0]["path"])
    client.save_or_load(0)
    yycs.transition(client, resource.parent / "yycs", "map_012_惠安镇.map", 2, running=True)
    site = [site for site in yycs.inventory(resource)["choices"]
            if site["path"] == "script/map/map_012_惠安镇/狗肉对话.txt"][-1]
    for number in range(1, 11):
        if number == 10:
            client.save_or_load(1)
            checkpoint(client, output, "dog-before-tenth-chicken-slot1")
        yycs.interact_named(client, output, resource, "狗肉", choice=(site["id"], 0))
        fed = checkpoint(client, output, f"dog-fed-chicken-{number}")
        if (sum(row["quantity"] for row in fed["inventory"] if row["file"] == "烧鸡.ini") != 10 - number
                or int(fed["variables"].get("gourou") or "0") != min(number, 9)):
            raise AutomationError("Dog friendship or chicken consumption differs")
    if not any(row["name"] == "partner-head-0" for row in fed["ui"]):
        raise AutomationError("The tenth chicken did not recruit the dog")
    client.save_or_load(2)
    folder = output / "user-data/save" / SAVE_NAMESPACE / "rpg3"
    saved = configparser.ConfigParser(interpolation=None, strict=False)
    saved.read(folder / "partner0.ini", encoding="utf-8-sig")
    partner = [section for section in saved.sections() if saved.get(section, "Name", fallback="") == "狗肉"]
    if (len(partner) != 1 or saved.get(partner[0], "Kind") != "3"
            or saved.get(partner[0], "Relation") != "0" or saved.get(partner[0], "DeathScript") != ""
            or saved.get(partner[0], "ScriptFile") != "右键狗肉队友.txt"):
        raise AutomationError("Dog's actual saved partner binding differs")
    yycs.interact_named(client, output, resource, "王飞")
    verify_script(output, resource, "script/map/map_012_惠安镇/王飞狗肉.txt")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/common/右键狗肉队友.txt")
    yycs.interact_named(client, output, resource, "狗肉", choice=(site["id"], 1))
    left = checkpoint(client, output, "dog-left-party")
    if any(row["name"] == "partner-head-0" for row in left["ui"]):
        raise AutomationError("Dog's leave choice did not remove the party head")
    yycs.interact_named(client, output, resource, "狗肉", choice=(site["id"], 0))
    yycs.interact_named(client, output, resource, "狗肉", choice=(site["id"], 2))
    repeated = checkpoint(client, output, "dog-rejoined-and-nonhuman-steal-no-reward")
    if repeated["inventory"] != fed["inventory"] or repeated["player"]["money"] != fed["player"]["money"]:
        raise AutomationError("Dog management or nonhuman stealing awarded items")
    yycs.transition(client, resource.parent / "yycs", "map_014_连接地图.map", 2, running=True)
    followers = [row for row in yycs.idle(client)["targets"] if row["name"] == "狗肉"]
    if len(followers) != 1 or not any(row["name"] == "partner-head-0" for row in client.observe()["ui"]):
        raise AutomationError("Recruited dog did not follow across the normal map exit")
    player = configparser.ConfigParser(interpolation=None, strict=False)
    player.read(folder / "player0.ini", encoding="utf-8-sig")
    client.wait_until(lambda value: value["player"]["thew"] == player.getint("init", "ThewMax"),
                      timeout=90, description="normal dog route recovery before save/load")
    client.save_or_load(3)
    stored = checkpoint(client, output, "dog-following-slot3")
    yycs.load_checkpoint(client, 3)
    loaded = checkpoint(client, output, "dog-following-reloaded")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if stored[key] != loaded[key]:
            raise AutomationError(f"Dog route save/load changed {key}")


def town_books(client, output, resource):
    if yycs.idle(client)["map"] == "map_027_连接地图.map":
        yycs.transition(client, resource.parent / "yycs", "map_012_惠安镇.map", 2, running=True)
    before = checkpoint(client, output, "town-books-stealing-two-before")
    if before["variables"].get("touqie") != "2" or before["variables"].get("koucai") != "2":
        raise AutomationError("Town book threshold route requires actual stealing and persuasion two")
    client.save_or_load(0)
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_012_惠安镇/宝箱3.txt")
    yycs.interact_named(client, output, resource, "宝箱", position=(102, 68), choice=(site["id"], 0))
    failed = checkpoint(client, output, "town-third-box-rejects-stealing-two")
    if failed["inventory"] != before["inventory"] or failed["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Town third box rewarded insufficient stealing")
    for number, position, book, variable, level in ((1, (4, 30), "妙手空空.ini", "touqie", 3),
                                                   (2, (8, 21), "妙语连珠.ini", "koucai", 3),
                                                   (3, (102, 68), "妙手空空.ini", "touqie", 4),
                                                   (4, (118, 104), "妙语连珠.ini", "koucai", 4)):
        site = next(site for site in yycs.inventory(resource)["choices"]
                    if site["path"] == f"script/map/map_012_惠安镇/宝箱{number}.txt")
        yycs.interact_named(client, output, resource, "宝箱", position=position, choice=(site["id"], 0))
        opened = checkpoint(client, output, f"town-book-box-{number}-reward")
        item = next(row for row in opened["inventory"] if row["file"] == book)
        client.act("UseItem", generation=opened["generation"], slot=item["slot"])
        learned = checkpoint(client, output, f"town-book-box-{number}-used-level-{level}")
        if learned["variables"].get(variable) != str(level) or any(row["file"] == book for row in learned["inventory"]):
            raise AutomationError("Town book did not increase its exact native ability and consume itself")
        check_used_object(client, output, f"town-book-box-{number}", position)
    after = checkpoint(client, output, "town-four-books-used")
    if after["inventory"] != before["inventory"] or after["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Town book route changed unrelated items or money")
    client.save_or_load(1)
    stored = checkpoint(client, output, "town-books-level-four-slot1")
    yycs.load_checkpoint(client, 1)
    loaded = checkpoint(client, output, "town-books-level-four-reloaded")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if stored[key] != loaded[key]:
            raise AutomationError(f"Town book save/load changed {key}")


def ao_herb_start(client, output, resource):
    yycs.transition(client, resource.parent / "yycs", "map_021_油菜花地.map", 1, running=True)
    before = checkpoint(client, output, "ao-herb-before-native-pickup")
    yycs.interact_named(client, output, resource, "罂粟", position=(4, 26))
    after = checkpoint(client, output, "ao-herb-picked-from-bound-object")
    if (sum(row["quantity"] for row in after["inventory"] if row["file"] == "goods-e16-罂粟.ini") != 1
            or after["player"]["money"] != before["player"]["money"]
            or any(row["name"] == "罂粟" and row["position"] == dict(x=4, y=26) for row in after["targets"])):
        raise AutomationError("Native bound herb pickup did not grant one item and remove its object")
    verify_script(output, resource, "script/map/map_021_油菜花地/支线草药.txt")
    client.save_or_load(0)
    checkpoint(client, output, "ao-herb-and-ma-bao-before-choices-slot0")


def ao_herb(client, output, resource, option):
    yycs.transition(client, resource.parent / "yycs", "map_022_清平乡.map", 2, running=True)
    before = checkpoint(client, output, "ao-herb-before-treatment")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_022_清平乡/敖广对话.txt"]
    yycs.interact_named(client, output, resource, "敖广", choice=(sites[1]["id"], option))
    after = checkpoint(client, output, f"ao-herb-treatment-option-{option}")
    if (option not in (0, 1) or after["variables"].get("aoguang") != "1"
            or sum(row["quantity"] for row in after["inventory"] if row["file"] == "goods-e16-罂粟.ini") != option
            or sum(row["quantity"] for row in after["inventory"] if row["file"] == "技能书.ini")
            != sum(row["quantity"] for row in before["inventory"] if row["file"] == "技能书.ini") + 1
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Ao Guang herb consumption or medical reward differs")
    yycs.interact_named(client, output, resource, "敖广")
    if checkpoint(client, output, "ao-herb-repeated-no-reward")["inventory"] != after["inventory"]:
        raise AutomationError("Ao Guang's herb treatment reward repeated")
    client.save_or_load(1)
    checkpoint(client, output, "ao-herb-result-slot1")


def rescue_start(client, output, resource):
    yycs.transition(client, resource.parent / "yycs", "map_021_油菜花地.map", 1, running=True)
    yycs.transition(client, resource.parent / "yycs", "map_024_倚天山.map", 1, running=True)
    enemies = [row for row in yycs.idle(client)["targets"] if row.get("hostile") and yycs.npc_attackable(row)]
    for target in enemies:
        current = next((row for row in yycs.idle(client)["targets"] if row["id"] == target["id"] and yycs.npc_attackable(row)), None)
        if current is None:
            continue
        yuemeier.approach_target(client, output, resource, (current["position"]["x"], current["position"]["y"]),
                                magic_file="player-magic-烈火情天.ini")
        if any(row["id"] == target["id"] and yycs.npc_attackable(row) for row in client.observe()["targets"]):
            yycs.fight_named(client, output, resource, target["name"], target_id=target["id"],
                             use_magic=True, single_target=True)
    yuemeier.blocked_exit(client, output, resource, 2, "zhaixing-gate-closed-before-father-task",
                          magic_file="player-magic-烈火情天.ini")
    verify_script(output, resource, "script/map/map_024_倚天山/trap02.txt")
    yycs.transition(client, resource.parent / "yycs", "map_021_油菜花地.map", 1, running=True)
    yycs.transition(client, resource.parent / "yycs", "map_022_清平乡.map", 2, running=True)
    before = checkpoint(client, output, "rescue-before-father-task")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_022_清平乡/庄允城对话.txt")
    yuemeier.approach_target(client, output, resource, (20, 40), magic_file="player-magic-烈火情天.ini")
    yycs.interact_named(client, output, resource, "庄允城", choice=(site["id"], 0))
    accepted = checkpoint(client, output, "rescue-father-task-accepted")
    if (accepted["variables"].get("zhaixinglou") != "1" or accepted["variables"].get("yulin") != "2"
            or accepted["inventory"] != before["inventory"] or accepted["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Zhaixing rescue acceptance or charge differs")
    for destination, trap in (("map_021_油菜花地.map", 1), ("map_024_倚天山.map", 1), ("map_025_摘星楼.map", 2)):
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_025_摘星楼/强盗对话5.txt"]
    yuemeier.approach_target(client, output, resource, (18, 43), magic_file="player-magic-烈火情天.ini")
    yycs.interact_named(client, output, resource, "强盗小头目", choice=(sites[0]["id"], 0))
    guard = checkpoint(client, output, "rescue-persuasion-guard-moved")
    target = next(row for row in guard["targets"] if row["name"] == "强盗小头目")
    if target["hostile"] or target["position"] != dict(x=17, y=40):
        raise AutomationError("Zhaixing guard persuasion did not clear its actual doorway")
    client.save_or_load(0)
    checkpoint(client, output, "rescue-before-yulin-choices-slot0")


def rescue_yulin(client, output, resource, option):
    before = checkpoint(client, output, "rescue-yulin-before-price-choice")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_025_摘星楼/玉林对话.txt"]
    yuemeier.approach_target(client, output, resource, (20, 63), magic_file="player-magic-烈火情天.ini")
    yycs.interact_named(client, output, resource, "玉林", choice=((sites[0]["id"], 0), (sites[1]["id"], 0), (sites[2]["id"], option)))
    freed = checkpoint(client, output, f"rescue-yulin-freed-option-{option}")
    if (option not in (0, 1) or freed["variables"].get("zwl") != "2"
            or any(row["name"] == "庄文璐" for row in freed["targets"])
            or freed["player"]["money"] != before["player"]["money"] - (1000 if option == 0 else 0)
            or freed["inventory"] != before["inventory"]):
        raise AutomationError("Yulin's actual ransom or persuasion release differs")
    rescue_reward(client, output, resource, before, freed)


def rescue_reward(client, output, resource, before, freed):
    client.save_or_load(1)
    for destination, trap in (("map_024_倚天山.map", 1), ("map_021_油菜花地.map", 1), ("map_022_清平乡.map", 2)):
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    yuemeier.approach_target(client, output, resource, (20, 40), magic_file="player-magic-烈火情天.ini")
    yycs.interact_named(client, output, resource, "庄允城")
    reward = checkpoint(client, output, "rescue-father-reward")
    if (reward["variables"].get("zwl") != "3"
            or sum(row["quantity"] for row in reward["inventory"] if row["file"] == "技能书.ini")
            != sum(row["quantity"] for row in before["inventory"] if row["file"] == "技能书.ini") + 1
            or reward["player"]["money"] != freed["player"]["money"]):
        raise AutomationError("Father rescue completion reward differs")
    yycs.interact_named(client, output, resource, "庄允城")
    if checkpoint(client, output, "rescue-father-no-repeat-reward")["inventory"] != reward["inventory"]:
        raise AutomationError("Father repeated his rescue reward")
    client.save_or_load(2)
    checkpoint(client, output, "rescue-completed-slot2")


def rescue_duel(client, output, resource):
    before = checkpoint(client, output, "rescue-duel-before")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_025_摘星楼/玉林对话.txt"]
    yuemeier.approach_target(client, output, resource, (20, 63), magic_file="player-magic-烈火情天.ini")
    yycs.interact_named(client, output, resource, "玉林", choice=((sites[0]["id"], 0), (sites[1]["id"], 2)))
    battle = checkpoint(client, output, "rescue-duel-one-native-enemy")
    opponents = [row for row in battle["targets"] if row["kind"] == "npc"]
    if battle["generation"] == before["generation"] or len(opponents) != 1 or opponents[0]["name"] != "玉林" or not opponents[0]["hostile"]:
        raise AutomationError("Native duel did not load its separate one-enemy binding")
    client.save_or_load(3)
    yycs.fight_named(client, output, resource, "玉林", use_magic=True, single_target=True)
    verify_script(output, resource, "script/map/map_025_摘星楼/玉林单挑.txt")
    freed = checkpoint(client, output, "rescue-duel-daughter-freed")
    if freed["variables"].get("zwl") != "2" or freed["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Duel release or fee differs from its current death binding")
    rescue_reward(client, output, resource, before, freed)


def rescue_fight(client, output, resource, option):
    before = checkpoint(client, output, "rescue-fight-before")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_025_摘星楼/玉林对话.txt"]
    yuemeier.approach_target(client, output, resource, (20, 63), magic_file="player-magic-烈火情天.ini")
    if option == 1:
        if int(before["variables"].get("koucai") or 0) >= 3:
            raise AutomationError("Failed persuasion route needs its original koucai below three")
        yycs.interact_named(client, output, resource, "玉林",
                            choice=((sites[0]["id"], 0), (sites[1]["id"], 0), (sites[2]["id"], 1)))
    elif option == 0:
        if before["player"]["money"] >= 1000:
            raise AutomationError("Short ransom route needs its original money below one thousand")
        yycs.interact_named(client, output, resource, "玉林",
                            choice=((sites[0]["id"], 0), (sites[1]["id"], 0), (sites[2]["id"], 0)))
    else:
        for remaining in (1, 0):
            yycs.interact_named(client, output, resource, "玉林", choice=(sites[0]["id"], 1))
            state = checkpoint(client, output, f"rescue-yulin-friendliness-{remaining}")
            if state["variables"].get("yulin") != str(remaining):
                raise AutomationError("Yulin friendliness threshold differs")
    hostile = checkpoint(client, output, "rescue-yulin-and-gang-hostile")
    if not any(row["name"] == "玉林" and row["hostile"] for row in hostile["targets"]) or hostile["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Refusal did not start its native battle or charged an invalid ransom")
    client.save_or_load(3)
    yycs.fight_named(client, output, resource, "玉林", use_magic=True, single_target=True)
    verify_script(output, resource, "script/map/map_025_摘星楼/玉林死亡.txt")
    enemies = [row for row in yycs.idle(client)["targets"] if row.get("hostile") and yycs.npc_attackable(row)]
    for target in enemies:
        current = next((row for row in yycs.idle(client)["targets"] if row["id"] == target["id"] and yycs.npc_attackable(row)), None)
        if current:
            yuemeier.approach_target(client, output, resource, (current["position"]["x"], current["position"]["y"]),
                                    magic_file="player-magic-烈火情天.ini")
            if any(row["id"] == target["id"] and yycs.npc_attackable(row) for row in client.observe()["targets"]):
                yycs.fight_named(client, output, resource, target["name"], target_id=target["id"], use_magic=True, single_target=True)
    rescue_survivor(client, output, resource)


def rescue_survivor(client, output, resource):
    client.wait_until(lambda state: not any(row["kind"] == "npc" and row.get("action") == 11 for row in state["targets"]),
                      timeout=15, description="gang native death animations finish")
    before = checkpoint(client, output, "rescue-after-gang-battle-before-daughter")
    if not any(row["name"] == "庄文璐" for row in before["targets"]):
        raise AutomationError("Daughter did not survive the actual gang battle")
    yuemeier.approach_target(client, output, resource, (20, 23), magic_file="player-magic-烈火情天.ini")
    client.save_or_load(4)
    daughter_site = next(site for site in yycs.inventory(resource)["choices"]
                         if site["path"] == "script/map/map_025_摘星楼/庄文璐对话.txt")
    yycs.interact_named(client, output, resource, "庄文璐", choice=(daughter_site["id"], 0))
    freed = checkpoint(client, output, "rescue-battle-daughter-freed")
    if freed["variables"].get("zwl") != "2" or any(row["name"] == "庄文璐" for row in freed["targets"]):
        raise AutomationError("Battle survivor did not complete normal rescue")
    rescue_reward(client, output, resource, before, freed)


def rescue_daughter_death(client, output, resource):
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_025_摘星楼/庄文璐对话.txt")
    before = checkpoint(client, output, "rescue-daughter-death-before")
    yycs.interact_named(client, output, resource, "庄文璐", choice=(site["id"], 1))
    yycs.fight_named(client, output, resource, "庄文璐", use_magic=True, single_target=True)
    verify_script(output, resource, "script/map/map_025_摘星楼/庄文璐死亡.txt")
    dead = checkpoint(client, output, "rescue-daughter-actually-dead")
    target = next((row for row in dead["targets"] if row["name"] == "庄文璐"), None)
    if target is not None and (target.get("action") != 11 or yycs.npc_attackable(target)):
        raise AutomationError("Daughter remains alive after native death completion")
    expected_inventory = inventory_with_stock(before["inventory"], resource, "商店庄文璐.ini")
    if {row["file"]: row["quantity"] for row in dead["inventory"]} != expected_inventory:
        raise AutomationError("Daughter's native death belongings differ from her exact inventory binding")
    for destination, trap in (("map_024_倚天山.map", 1), ("map_021_油菜花地.map", 1), ("map_022_清平乡.map", 2)):
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    yuemeier.approach_target(client, output, resource, (20, 40), magic_file="player-magic-烈火情天.ini")
    client.save_or_load(0)
    checkpoint(client, output, "rescue-father-before-death-response-slot0")


def rescue_father_death(client, output, resource, option):
    if option not in (0, 1):
        raise AutomationError("Father death response has two actual choices")
    before = checkpoint(client, output, "rescue-father-death-response-before")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_022_清平乡/庄允城女死.txt")
    yycs.interact_named(client, output, resource, "庄允城", choice=(site["id"], option))
    after = checkpoint(client, output, "rescue-father-death-response")
    if after["inventory"] != before["inventory"] or after["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Daughter death response gave a rescue reward or charged money")
    if option == 0:
        opponents = [row for row in after["targets"] if row["name"] in ("庄允城", "保镖")]
        if len(opponents) < 2 or not all(row["hostile"] for row in opponents):
            raise AutomationError("Father and bodyguard did not start their actual death response battle")
        yycs.fight_named(client, output, resource, "庄允城", use_magic=True, single_target=True)
    else:
        yycs.interact_named(client, output, resource, "庄允城")
        verify_script(output, resource, "script/map/map_022_清平乡/庄允城哭泣.txt")
    client.save_or_load(1)
    checkpoint(client, output, "rescue-father-death-response-slot1")


def attribute_book(client, output, resource, option):
    changes = (("attack", 5), ("lifemax", 10), ("manamax", 20), ("evade", 2), ("thewmax", 10), ("defend", 4))
    if option not in range(7):
        raise AutomationError("Attribute book has seven actual choices")
    def attributes(slot):
        saved = configparser.ConfigParser(interpolation=None)
        saved.read(output / "user-data/save" / SAVE_NAMESPACE / f"rpg{slot + 1}/player0.ini", encoding="utf-8-sig")
        return {key: saved.getint("init", key) for key, _ in changes}
    client.save_or_load(0)
    before = checkpoint(client, output, "attribute-book-before")
    original = attributes(0)
    book = next(row for row in before["inventory"] if row["file"] == "技能书.ini")
    sites = [site for site in yycs.inventory(resource)["choices"] if site["path"] == "script/goods/技能书.txt"]
    action = client.submit("UseItem", generation=before["generation"], slot=book["slot"])
    client.wait_until(lambda state: bool(state.get("choices")), description="attribute book choice")
    yycs.choose_site(client, output, resource, sites[0]["id"], option,
                     remaining=((sites[1]["id"], 6),) if book["quantity"] > 1 and option != 6 else ())
    client.wait_action(action, timeout=3)
    client.save_or_load(1)
    after = checkpoint(client, output, "attribute-book-after-slot1")
    expected = original.copy()
    if option != 6:
        key, increase = changes[option]
        expected[key] += increase
    expected_inventory = {row["file"]: row["quantity"] for row in before["inventory"]}
    if option != 6:
        expected_inventory["技能书.ini"] -= 1
        if not expected_inventory["技能书.ini"]:
            del expected_inventory["技能书.ini"]
    if (attributes(1) != expected or {row["file"]: row["quantity"] for row in after["inventory"]} != expected_inventory
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Attribute book effect, consumption or cancel differs from source")
    write_json(output / "attribute-book-proof.json", dict(option=option, before=original, after=attributes(1),
               expected=expected, status="passed", cheatAssisted=after["cheatAssisted"], fullPlaythrough=False))


def mayongcheng(client, output, resource):
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_012_惠安镇/马永成对话.txt"]
    yuemeier.approach_target(client, output, resource, (6, 201), magic_file="player-magic-烈火情天.ini")
    yycs.interact_named(client, output, resource, "马永成", choice=((sites[0]["id"], 0), (sites[1]["id"], 0)))
    before = checkpoint(client, output, "mayongcheng-task-accepted")
    progress = next(site for site in yycs.inventory(resource)["choices"]
                    if site["path"] == "script/map/map_012_惠安镇/马永成对话2.txt")
    yycs.interact_named(client, output, resource, "马永成", choice=(progress["id"], 1))
    pending = checkpoint(client, output, "mayongcheng-no-reward-before-victory")
    if pending["inventory"] != before["inventory"] or pending["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Unfinished house task gave a reward")
    client.save_or_load(0)
    yycs.transition(client, resource.parent / "yycs", "map_014_连接地图.map", 2, running=True)
    yuemeier.approach_target(client, output, resource, (31, 105), magic_file="player-magic-烈火情天.ini")
    foe = next(site for site in yycs.inventory(resource)["choices"]
               if site["path"] == "script/map/map_014_连接地图/仇友三对话.txt")
    yycs.interact_named(client, output, resource, "仇友三", choice=(foe["id"], 0))
    yycs.fight_named(client, output, resource, "仇友三", use_magic=True, single_target=True)
    verify_script(output, resource, "script/map/map_014_连接地图/仇友三死亡.txt")
    defeated = checkpoint(client, output, "qiuyousan-defeated-house-reclaimed")
    if defeated["variables"].get("myc") != "2":
        raise AutomationError("Qiu Yousan death did not complete the house task")
    client.save_or_load(1)
    yycs.transition(client, resource.parent / "yycs", "map_012_惠安镇.map", 1, running=True)
    yuemeier.approach_target(client, output, resource, (6, 201), magic_file="player-magic-烈火情天.ini")
    before = checkpoint(client, output, "mayongcheng-before-reward")
    yycs.interact_named(client, output, resource, "马永成", choice=(progress["id"], 0))
    reward = checkpoint(client, output, "mayongcheng-one-book-reward")
    if (reward["variables"].get("myc") != "1" or reward["player"]["money"] != before["player"]["money"]
            or sum(row["quantity"] for row in reward["inventory"] if row["file"] == "技能书.ini")
            != sum(row["quantity"] for row in before["inventory"] if row["file"] == "技能书.ini") + 1):
        raise AutomationError("Native house task reward differs")
    yycs.interact_named(client, output, resource, "马永成")
    if checkpoint(client, output, "mayongcheng-no-repeat-reward")["inventory"] != reward["inventory"]:
        raise AutomationError("House task reward repeated")
    client.save_or_load(2)
    saved = checkpoint(client, output, "mayongcheng-completed-slot2")
    yycs.load_checkpoint(client, 2)
    loaded = checkpoint(client, output, "mayongcheng-completed-reloaded")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if saved[key] != loaded[key]:
            raise AutomationError(f"House task save/load changed {key}")


def forest_lotus(client, output, resource):
    for destination, trap in (("map_011_连接地图.map", 1), ("map_008_野树林.map", 1)):
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    for trap in (2, 3):
        yuemeier.blocked_exit(client, output, resource, trap, f"forest-unused-exit-{trap}-closed",
                              magic_file="player-magic-烈火情天.ini", excluded_traps=(1, 4, 5))
    yuemeier.approach_target(client, output, resource, (8, 25), magic_file="player-magic-烈火情天.ini")
    before = checkpoint(client, output, "forest-caravan-before-investigation")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_008_野树林/雪莲王.txt")
    yycs.interact_named(client, output, resource, "商队队员", choice=(site["id"], 0))
    found = checkpoint(client, output, "forest-caravan-lotus-found")
    if (found["variables"].get("xlw") != "1"
            or sum(row["quantity"] for row in found["inventory"] if row["file"] == "雪莲王.ini")
            != sum(row["quantity"] for row in before["inventory"] if row["file"] == "雪莲王.ini") + 1
            or found["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Caravan investigation reward differs")
    check_used_object(client, output, "caravan-lotus", (8, 25))
    client.save_or_load(0)
    checkpoint(client, output, "forest-lotus-branch-source-slot0")
    forest_return(client, output, resource)


def forest_return(client, output, resource):
    if yycs.idle(client)["map"] == "map_008_野树林.map":
        yuemeier.approach_exit(client, output, resource, 4, magic_file="player-magic-烈火情天.ini", excluded_traps=(1, 5))
        yycs.transition(client, resource.parent / "yycs", "map_011_连接地图.map", 4, running=True)
    if yycs.idle(client)["map"] != "map_011_连接地图.map":
        raise AutomationError("Lotus return requires its normal forest or connecting map")
    yuemeier.approach_exit(client, output, resource, 2, magic_file="player-magic-烈火情天.ini")
    yycs.transition(client, resource.parent / "yycs", "map_012_惠安镇.map", 2, running=True)
    client.save_or_load(1)
    checkpoint(client, output, "forest-lotus-returned-town-slot1")


def hostage_start(client, output, resource):
    yuemeier.approach_target(client, output, resource, (36, 92), magic_file="player-magic-烈火情天.ini")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_012_惠安镇/老鱼对话.txt")
    yycs.interact_named(client, output, resource, "老鱼", choice=(site["id"], 1))
    state = checkpoint(client, output, "hostage-task-accepted-oldyu-moved")
    if state["variables"].get("jbtb") != "1" or not any(row["name"] == "老鱼" and row["position"] == dict(x=37, y=90) for row in state["targets"]):
        raise AutomationError("Inn hostage acceptance did not move its actual door actor")
    yuemeier.approach_target(client, output, resource, (47, 66), magic_file="player-magic-烈火情天.ini")
    client.save_or_load(0)
    checkpoint(client, output, "hostage-before-negotiation-slot0")


def hostage_pay(client, output, resource):
    before = checkpoint(client, output, "hostage-before-persuasion-and-payment")
    for script in ("铁拔对话.txt", "铁拔对话2.txt"):
        sites = [site for site in yycs.inventory(resource)["choices"]
                 if site["path"] == "script/map/map_012_惠安镇/" + script]
        yycs.interact_named(client, output, resource, "铁拔", choice=tuple((site["id"], 1) for site in sites))
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_012_惠安镇/铁拔对话3.txt"]
    yycs.interact_named(client, output, resource, "铁拔", choice=((sites[0]["id"], 1), (sites[1]["id"], 0), (sites[2]["id"], 0)))
    freed = checkpoint(client, output, "hostage-paid-one-hundred-and-survived")
    if (before["player"]["money"] < 100 or freed["player"]["money"] != before["player"]["money"] - 100
            or freed["variables"].get("jbtb") != "0" or any(row["name"] == "铁拔" for row in freed["targets"])
            or not any(row["name"] == "朱媚" and row.get("action") != 11 for row in freed["targets"])
            or freed["inventory"] != before["inventory"]):
        raise AutomationError("Hostage ransom, survivor or inventory differs")
    client.save_or_load(1)
    checkpoint(client, output, "hostage-before-survivor-thanks-slot1")


def hostage_thanks(client, output, resource, option):
    if option not in (0, 1):
        raise AutomationError("Hostage survivor thanks has two actual choices")
    before = checkpoint(client, output, "hostage-survivor-before-response")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_012_惠安镇/朱媚得救.txt")
    yycs.interact_named(client, output, resource, "朱媚", choice=(site["id"], option))
    after = checkpoint(client, output, "hostage-survivor-response")
    if (after["variables"].get("zhumei") != str(option + 1)
            or int(after["variables"]["event"]) != int(before["variables"]["event"]) - (1 if option == 0 else 0)
            or sum(row["quantity"] for row in after["inventory"] if row["file"] == "技能书.ini")
            != sum(row["quantity"] for row in before["inventory"] if row["file"] == "技能书.ini") + option
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Hostage thanks moral value or reward differs")
    yycs.interact_named(client, output, resource, "朱媚")
    repeated = checkpoint(client, output, "hostage-survivor-no-repeat-reward")
    if repeated["inventory"] != after["inventory"] or repeated["variables"]["event"] != after["variables"]["event"]:
        raise AutomationError("Hostage thanks repeated its reward or moral penalty")
    yuemeier.approach_target(client, output, resource, (37, 90), magic_file="player-magic-烈火情天.ini")
    yycs.interact_named(client, output, resource, "老鱼")
    hotel = checkpoint(client, output, "hostage-free-inn-flag")
    if hotel["variables"].get("lyzd") != "1":
        raise AutomationError("Old Yu did not grant free accommodation")
    if hotel["cheatInvincibilityEnabled"]:
        client.open_menu("System")
        client.activate("options")
        client.activate("cheat-settings")
        client.activate("invincibility")
        for _ in range(5):
            if client.observe().get("worldInput"):
                break
            client.ui("Cancel")
        if client.observe()["cheatInvincibilityEnabled"]:
            raise AutomationError("Native invincibility did not turn off for inn recovery evidence")
    state = client.observe()
    spell = next(row for row in state["magic"] if row["file"] == "player-magic-清心咒.ini")
    client.assign_magic(spell["slot"], 0)
    client.act("CastSkill", generation=state["generation"], slot=0)
    before_rest = checkpoint(client, output, "hostage-inn-real-mana-deficit")
    if before_rest["player"]["mana"] >= before_rest["player"]["manaMax"]:
        raise AutomationError("Inn test did not create a real native mana deficit")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_012_惠安镇/老鱼感谢.txt")
    yycs.interact_named(client, output, resource, "老鱼", choice=(site["id"], 0))
    rested = checkpoint(client, output, "hostage-inn-free-native-recovery")
    if (rested["player"]["position"] != dict(x=32, y=72)
            or rested["player"]["mana"] != rested["player"]["manaMax"]
            or rested["player"]["life"] != rested["player"]["lifeMax"]
            or rested["player"]["money"] != before_rest["player"]["money"]):
        raise AutomationError("Free inn did not restore its real native deficit without a charge")
    client.save_or_load(2)
    checkpoint(client, output, "hostage-survivor-and-free-inn-slot2")


def hostage_failure(client, output, resource, option):
    before = checkpoint(client, output, "hostage-before-failure-choice")
    if option == 1:
        if before["player"]["money"] >= 100:
            raise AutomationError("Short hostage ransom requires less than one hundred")
        for script in ("铁拔对话.txt", "铁拔对话2.txt"):
            sites = [site for site in yycs.inventory(resource)["choices"]
                     if site["path"] == "script/map/map_012_惠安镇/" + script]
            yycs.interact_named(client, output, resource, "铁拔", choice=tuple((site["id"], 1) for site in sites))
        sites = [site for site in yycs.inventory(resource)["choices"]
                 if site["path"] == "script/map/map_012_惠安镇/铁拔对话3.txt"]
        yycs.interact_named(client, output, resource, "铁拔", choice=((sites[0]["id"], 1), (sites[1]["id"], 0), (sites[2]["id"], 0)))
    else:
        site = next(site for site in yycs.inventory(resource)["choices"]
                    if site["path"] == "script/map/map_012_惠安镇/铁拔对话.txt")
        yycs.interact_named(client, output, resource, "铁拔", choice=(site["id"], 0))
    client.wait_until(lambda state: not any(row["name"] == "朱媚" and row.get("action") != 11 for row in state["targets"]),
                      timeout=15, description="hostage native death action")
    client.wait_until(lambda state: not any(row["name"] == "朱媚" for row in state["targets"])
                      and any(row["kind"] == "object" and row["position"] == dict(x=47, y=65)
                              for row in state["targets"]), timeout=15, description="hostage native corpse creation")
    records = verify_script(output, resource, "script/map/map_012_惠安镇/铁拔对话3.txt" if option == 1 else
                            "script/map/map_012_惠安镇/铁拔对话.txt")
    if not any(row.get("apiName") == "setnpcaction" for row in records):
        raise AutomationError("Hostage native death action lacks its completed plot source")
    dead = checkpoint(client, output, "hostage-killed-by-tieba-native-corpse")
    if (dead["player"]["money"] != before["player"]["money"]
            or dead["variables"]["event"] != before["variables"]["event"]
            or not any(row["name"] == "铁拔" and row["hostile"] for row in dead["targets"])
            or any(row["name"] in ("李捕头", "捕快") and row["hostile"] for row in dead["targets"])):
        raise AutomationError("Hostage plot death charged money, changed player morality or missed its antagonist")
    client.save_or_load(1)
    yycs.fight_named(client, output, resource, "铁拔", use_magic=True, single_target=True)
    verify_script(output, resource, "script/map/map_012_惠安镇/铁拔死亡.txt")
    killed = checkpoint(client, output, "hostage-tieba-death-police-enemies")
    guards = [row for row in killed["targets"] if row["name"] in ("捕快", "李捕头")]
    if len(guards) != 8 or not all(row["hostile"] for row in guards):
        raise AutomationError("Tieba death did not make every actual police actor hostile")
    client.save_or_load(2)
    checkpoint(client, output, "hostage-antagonist-death-slot2")


def hostage_report(client, output, resource):
    before = checkpoint(client, output, "hostage-before-report")
    target = next(row for row in before["targets"] if row["name"] == "李捕头")
    yuemeier.approach_target(client, output, resource, (target["position"]["x"], target["position"]["y"]),
                            magic_file="player-magic-烈火情天.ini")
    yycs.interact_named(client, output, resource, "李捕头")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_012_惠安镇/李捕头举报.txt")
    yycs.interact_named(client, output, resource, "李捕头", choice=(site["id"], 3))
    after = checkpoint(client, output, "hostage-police-report-result")
    if (after["variables"].get("jbtb") != "0"
            or any(row["name"] in ("老鱼", "铁拔", "朱媚") for row in after["targets"])
            or sum(row["quantity"] for row in after["inventory"] if row["file"] == "技能书.ini")
            != sum(row["quantity"] for row in before["inventory"] if row["file"] == "技能书.ini") + 1
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Hostage report actors or reward differ from its actual source")
    yycs.interact_named(client, output, resource, "李捕头", choice=(site["id"], 5))
    if checkpoint(client, output, "hostage-report-no-repeat")["inventory"] != after["inventory"]:
        raise AutomationError("Hostage report repeated its reward")
    client.save_or_load(1)
    saved = checkpoint(client, output, "hostage-report-slot1")
    yycs.load_checkpoint(client, 1)
    loaded = checkpoint(client, output, "hostage-report-loaded-slot1")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if saved[key] != loaded[key]:
            raise AutomationError(f"Hostage report save/load changed {key}")


def caravan(client, output, resource, option):
    yuemeier.approach_target(client, output, resource, (77, 73), magic_file="player-magic-烈火情天.ini")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_012_惠安镇/唐三千对话.txt"]
    yycs.interact_named(client, output, resource, "唐三千", choice=tuple((site["id"], 0) for site in sites))
    client.save_or_load(0)
    before = checkpoint(client, output, "caravan-task-before-handover-slot0")
    lotus = any(row["file"] == "雪莲王.ini" for row in before["inventory"])
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_012_惠安镇/唐三千商队.txt")
    yycs.interact_named(client, output, resource, "唐三千", choice=(site["id"], option) if lotus else None)
    after = checkpoint(client, output, "caravan-handover-result")
    if not lotus or option == 2:
        if after["inventory"] != before["inventory"] or after["player"]["money"] != before["player"]["money"]:
            raise AutomationError("Unfinished caravan task granted a reward")
    elif option == 0:
        expected = {row["file"]: row["quantity"] for row in before["inventory"]}
        del expected["雪莲王.ini"]
        if ({row["file"]: row["quantity"] for row in after["inventory"]} != expected
                or after["variables"].get("xlw") != "0" or after["player"]["money"] != before["player"]["money"] + 10):
            raise AutomationError("Caravan delivery consumption or ten-tael reward differs")
        site = next(site for site in yycs.inventory(resource)["choices"]
                    if site["path"] == "script/map/map_012_惠安镇/唐三千十两.txt")
        yycs.interact_named(client, output, resource, "唐三千", choice=(site["id"], 1))
        repeated = checkpoint(client, output, "caravan-no-repeat-reward")
        if repeated["inventory"] != after["inventory"] or repeated["player"]["money"] != after["player"]["money"]:
            raise AutomationError("Caravan delivery reward repeated")
    elif option == 1:
        if (after["inventory"] != before["inventory"] or after["player"]["money"] != before["player"]["money"]
                or not any(row["name"] == "唐三千" and row["hostile"] for row in after["targets"])):
            raise AutomationError("Caravan refusal lost its lotus or missed the native battle")
        yycs.fight_named(client, output, resource, "唐三千", use_magic=True, single_target=True)
        verify_script(output, resource, "script/map/map_012_惠安镇/唐三千死亡.txt")
        dead = checkpoint(client, output, "caravan-tang-death-and-faction")
        if not all(row["hostile"] for row in dead["targets"] if row["name"] in ("唐四公子", "李捕头", "捕快")):
            raise AutomationError("Tang death did not set its actual family and police hostility")
    client.save_or_load(1)
    checkpoint(client, output, "caravan-result-slot1")


def tong_lotus(client, output, resource):
    yuemeier.approach_exit(client, output, resource, 3, magic_file="player-magic-烈火情天.ini")
    yycs.transition(client, resource.parent / "yycs", "map_027_连接地图.map", 3, running=True)
    target = next(row for row in yycs.idle(client)["targets"] if row["name"] == "童贵")
    yuemeier.approach_target(client, output, resource, (target["position"]["x"], target["position"]["y"]),
                            magic_file="player-magic-烈火情天.ini")
    client.save_or_load(0)
    before = checkpoint(client, output, "tong-before-lotus-exchange-slot0")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_027_连接地图/童贵对话.txt"]
    yycs.interact_named(client, output, resource, "童贵", choice=tuple((site["id"], 0) for site in sites))
    after = checkpoint(client, output, "tong-lotus-exchanged-for-ginseng")
    expected = {row["file"]: row["quantity"] for row in before["inventory"]}
    del expected["雪莲王.ini"]
    expected["千年人参.ini"] = expected.get("千年人参.ini", 0) + 1
    if ({row["file"]: row["quantity"] for row in after["inventory"]} != expected
            or after["variables"].get("tgqnrs") != "1" or after["variables"]["event"] != before["variables"]["event"]
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Tong lotus exchange consumption, ginseng or crime differs")
    yycs.interact_named(client, output, resource, "童贵")
    if checkpoint(client, output, "tong-lotus-exchange-no-repeat")["inventory"] != after["inventory"]:
        raise AutomationError("Tong lotus exchange repeated its reward")
    client.save_or_load(1)
    saved = checkpoint(client, output, "tong-lotus-exchange-slot1")
    yycs.load_checkpoint(client, 1)
    loaded = checkpoint(client, output, "tong-lotus-exchange-loaded-slot1")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if saved[key] != loaded[key]:
            raise AutomationError(f"Tong lotus exchange save/load changed {key}")


def tong_death(client, output, resource, option):
    before = checkpoint(client, output, "tong-before-death-branch")
    if option == 0:
        sites = [site for site in yycs.inventory(resource)["choices"]
                 if site["path"] == "script/map/map_027_连接地图/童贵对话.txt"]
        yycs.interact_named(client, output, resource, "童贵", choice=((sites[0]["id"], 0), (sites[1]["id"], 2)))
    else:
        site = next(site for site in yycs.inventory(resource)["choices"]
                    if site["path"] == "script/map/map_027_连接地图/右键童贵.txt")
        alternate_named(client, output, resource, "童贵", (site["id"], 2))
    yycs.fight_named(client, output, resource, "童贵", use_magic=True, single_target=True)
    verify_script(output, resource, "script/map/map_027_连接地图/童贵死亡.txt")
    after = checkpoint(client, output, "tong-native-death-result")
    original = {row["file"]: row["quantity"] for row in before["inventory"]}
    expected = inventory_with_stock(before["inventory"], resource, "商店童贵.ini")
    if option == 0 or not original.get("千年人参.ini"):
        expected["千年人参.ini"] = expected.get("千年人参.ini", 0) + 1
    if ({row["file"]: row["quantity"] for row in after["inventory"]} != expected
            or int(after["variables"]["event"]) != int(before["variables"]["event"]) - 1
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Tong death reward, moral penalty or money differs")
    client.save_or_load(2)
    checkpoint(client, output, "tong-death-result-slot2")


def report_town_task(client, output, resource, option, flag, removed):
    before = checkpoint(client, output, f"town-report-{flag}-before")
    target = next(row for row in before["targets"] if row["name"] == "李捕头")
    yuemeier.approach_target(client, output, resource, (target["position"]["x"], target["position"]["y"]),
                            magic_file="player-magic-烈火情天.ini")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_012_惠安镇/李捕头举报.txt")
    for _ in range(2):
        yycs.interact_named(client, output, resource, "李捕头", choice=(site["id"], option))
        if client.observe((flag,))["variables"].get(flag) == "0":
            break
    after = checkpoint(client, output, f"town-report-{flag}-result")
    if (after["variables"].get(flag) != "0" or any(row["name"] == removed for row in after["targets"])
            or sum(row["quantity"] for row in after["inventory"] if row["file"] == "技能书.ini")
            != sum(row["quantity"] for row in before["inventory"] if row["file"] == "技能书.ini") + 1
            or after["variables"]["event"] != before["variables"]["event"]
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Town task report actor, book, morality or fee differs")
    yycs.interact_named(client, output, resource, "李捕头", choice=(site["id"], 5))
    if checkpoint(client, output, f"town-report-{flag}-no-repeat")["inventory"] != after["inventory"]:
        raise AutomationError("Town task report repeated its reward")
    client.save_or_load(2)
    checkpoint(client, output, f"town-report-{flag}-slot2")


def stone_report(client, output, resource):
    yuemeier.approach_target(client, output, resource, (117, 186), magic_file="player-magic-烈火情天.ini")
    before = checkpoint(client, output, "stone-persuasion-before")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_012_惠安镇/石南虫对话.txt")
    yycs.interact_named(client, output, resource, "石南虫", choice=(site["id"], 3))
    after = checkpoint(client, output, "stone-persuasion-report-condition")
    if (after["variables"].get("jbsnc") != "1" or after["inventory"] != before["inventory"]
            or any(row["name"] == "石南虫" and row["hostile"] for row in after["targets"])):
        raise AutomationError("Stone persuasion missed its report condition or started combat")
    client.save_or_load(0)
    report_town_task(client, output, resource, 0, "jbsnc", "石南虫")


def gang_start(client, output, resource):
    yuemeier.approach_target(client, output, resource, (58, 21), magic_file="player-magic-烈火情天.ini")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_012_惠安镇/曾谁雄对话.txt"]
    yycs.interact_named(client, output, resource, "曾谁雄", choice=((sites[0]["id"], 2), (sites[1]["id"], 0), (sites[2]["id"], 0)))
    before = checkpoint(client, output, "gang-task-before-relic")
    yycs.interact_named(client, output, resource, "曾谁雄")
    if checkpoint(client, output, "gang-no-reward-before-relic")["inventory"] != before["inventory"]:
        raise AutomationError("Gang task granted a reward before obtaining the relic")
    client.save_or_load(0)
    yuemeier.approach_target(client, output, resource, (59, 150), magic_file="player-magic-烈火情天.ini")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_012_惠安镇/发钗.txt")
    yycs.interact_named(client, output, resource, "发钗宝箱", position=(59, 150), choice=(site["id"], 1))
    stolen = checkpoint(client, output, "gang-relic-native-steal")
    if (stolen["variables"].get("fachai") != "1" or not any(row["file"] == "goods-e09-发钗.ini" for row in stolen["inventory"])
            or any(row["name"] == "老点子" and row["hostile"] for row in stolen["targets"])):
        raise AutomationError("Stealing-four relic route did not obtain its item peacefully")
    check_used_object(client, output, "gang-relic-box-no-repeat", (59, 150))
    client.save_or_load(1)
    checkpoint(client, output, "gang-relic-before-resolution-slot1")


def gang_resolve(client, output, resource, option):
    if option == 2:
        yuemeier.approach_target(client, output, resource, (57, 152), magic_file="player-magic-烈火情天.ini")
        sites = [site for site in yycs.inventory(resource)["choices"]
                 if site["path"] == "script/map/map_012_惠安镇/老点子对话.txt"]
        before = checkpoint(client, output, "gang-relic-return-before")
        yycs.interact_named(client, output, resource, "老点子", choice=((sites[0]["id"], 3), (sites[1]["id"], 4)))
        after = checkpoint(client, output, "gang-relic-return-current-definition")
        if (after["variables"].get("fachai") != "0" or after["inventory"] != before["inventory"]
                or after["player"]["money"] != before["player"]["money"]):
            raise AutomationError("Relic return differs from current item-preserving source")
        client.save_or_load(2)
        return
    yuemeier.approach_target(client, output, resource, (58, 21), magic_file="player-magic-烈火情天.ini")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_012_惠安镇/曾谁雄偷老点子.txt")
    yycs.interact_named(client, output, resource, "曾谁雄", choice=(site["id"], 1 if option == 0 else 0))
    after = checkpoint(client, output, "gang-relic-resolution")
    if option == 0:
        if after["variables"].get("jbzsx") != "1":
            raise AutomationError("Gang persuasion missed its report condition")
        report_town_task(client, output, resource, 1, "jbzsx", "曾谁雄")
    else:
        opponents = [row for row in after["targets"] if row["name"] in ("曾谁雄", "骷髅帮弟子")]
        if len(opponents) != 3 or not all(row["hostile"] for row in opponents):
            raise AutomationError("Gang refusal did not make every actual gang actor hostile")
        for target in opponents:
            current = next((row for row in client.observe()["targets"] if row["id"] == target["id"] and yycs.npc_attackable(row)), None)
            if current:
                yuemeier.approach_target(client, output, resource, (current["position"]["x"], current["position"]["y"]),
                                        magic_file="player-magic-烈火情天.ini")
                yycs.fight_named(client, output, resource, target["name"], target_id=target["id"], use_magic=True, single_target=True)
        verify_script(output, resource, "script/map/map_012_惠安镇/曾谁雄死亡.txt")
        client.save_or_load(2)
        checkpoint(client, output, "gang-native-deaths-slot2")


def partner_drug(client, output, resource):
    before = checkpoint(client, output, "partner-drug-normal-equipped-source")
    target = next(row for row in before["targets"] if row["name"] == "甘新")
    yuemeier.approach_target(client, output, resource, (target["position"]["x"], target["position"]["y"]),
                            magic_file="player-magic-烈火情天.ini")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_022_清平乡/右键甘新.txt")
    client.interact(target["id"], side="alternate")
    choice = client.wait_until(lambda state: bool(state.get("choices")), description="normal medicine merchant actions")
    if not yycs.choice_matches_source(site, choice, 0):
        raise AutomationError("Medicine merchant actions do not match source")
    client.act("Choose", context=choice["context"], options=[0])
    shop = client.wait_until(lambda state: "shop" in state, description="normal medicine shop")
    offered = next(row for row in shop["shop"] if row["file"] == "goods-m00-金花.ini")
    client.buy(offered["slot"])
    client.ui("Cancel")
    yycs.idle(client)
    verify_script(output, resource, site["path"])
    ready = checkpoint(client, output, "partner-drug-purchased-normal-medicine")
    if (sum(row["quantity"] for row in ready["inventory"] if row["file"] == "goods-m00-金花.ini")
            != sum(row["quantity"] for row in before["inventory"] if row["file"] == "goods-m00-金花.ini") + 1
            or ready["player"]["money"] != before["player"]["money"] - 140):
        raise AutomationError("Medicine purchase did not add exactly one item")
    client.save_or_load(0)
    partner_injury(client, output, resource)


def partner_injury(client, output, resource):
    before = checkpoint(client, output, "partner-drug-before-actual-battle")
    partner = next(row for row in before["targets"] if row["name"] == "苏莹莹")
    yuemeier.approach_target(client, output, resource, (20, 40), magic_file="player-magic-烈火情天.ini")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_022_清平乡/右键庄允城.txt")
    alternate_named(client, output, resource, "庄允城", (site["id"], 2))
    client.move(26, 50, running=True, timeout=30)
    injured = client.wait_until(lambda state: any(row["name"] == "苏莹莹" and 0 < row["life"] < partner["life"]
                                                for row in state["targets"]), timeout=45,
                                description="actual enemy damage to recruited companion")
    checkpoint(client, output, "partner-drug-real-battle-injury")
    for name in ("保镖", "庄允城", "庄文璐"):
        target = next((row for row in client.observe()["targets"] if row["name"] == name and row.get("hostile")
                       and yycs.npc_attackable(row)), None)
        if target:
            if not target["visibleFromPlayer"]:
                yuemeier.approach_target(client, output, resource, (target["position"]["x"], target["position"]["y"]),
                                        magic_file="player-magic-烈火情天.ini")
            if any(row["id"] == target["id"] and yycs.npc_attackable(row) for row in client.observe()["targets"]):
                yycs.fight_named(client, output, resource, name, use_magic=True, single_target=True)
    state = checkpoint(client, output, "partner-drug-before-use-after-battle")
    actor = next(row for row in state["targets"] if row["name"] == "苏莹莹")
    if not 0 < actor["life"] < partner["life"]:
        raise AutomationError("Companion did not retain an actual life deficit")
    drug = next(row for row in state["inventory"] if row["file"] == "goods-m00-金花.ini")
    client.act("UseItem", generation=state["generation"], slot=drug["slot"])
    after = checkpoint(client, output, "partner-drug-native-healed-companion")
    healed = next(row for row in after["targets"] if row["name"] == "苏莹莹")
    if (healed["life"] != min(partner["life"], actor["life"] + 70)
            or sum(row["quantity"] for row in after["inventory"] if row["file"] == "goods-m00-金花.ini") != drug["quantity"] - 1):
        raise AutomationError("Medicine did not consume one item and restore the companion's defined seventy life")
    client.save_or_load(1)
    write_json(output / "partner-drug-proof.json", dict(status="passed", companion="苏莹莹", initialLife=partner["life"],
               actualBattleInjury=next(row["life"] for row in injured["targets"] if row["name"] == "苏莹莹"),
               beforeDrug=actor["life"], afterDrug=healed["life"], drug="goods-m00-金花.ini", nativeEffect=70,
               saveBytesEdited=False, cheatAssisted=after["cheatAssisted"]))


def wanted(client, output, resource):
    before = checkpoint(client, output, "wanted-morality-source")
    if int(before["variables"]["event"]) >= 100:
        raise AutomationError("Wanted route requires its actual below-one-hundred morality")
    yuemeier.approach_target(client, output, resource, (57, 152), magic_file="player-magic-烈火情天.ini")
    yycs.interact_named(client, output, resource, "老点子")
    refused = checkpoint(client, output, "wanted-merchant-refused")
    if ("shop" in refused or refused["inventory"] != before["inventory"]
            or refused["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Wanted merchant opened a shop or changed money/items")
    yuemeier.approach_target(client, output, resource, (19, 133), magic_file="player-magic-烈火情天.ini")
    yycs.interact_named(client, output, resource, "李捕头")
    state = checkpoint(client, output, "wanted-police-native-hostility")
    guards = [row for row in state["targets"] if row["name"] in ("李捕头", "捕快")]
    if len(guards) != 8 or not all(row["hostile"] for row in guards):
        raise AutomationError("Wanted encounter did not set all actual police actors hostile")
    client.save_or_load(0)
    yycs.load_checkpoint(client, 0)
    loaded = checkpoint(client, output, "wanted-saved-and-loaded-faction")
    if (loaded["variables"]["event"] != before["variables"]["event"]
            or not all(row["hostile"] for row in loaded["targets"] if row["name"] in ("李捕头", "捕快"))
            or loaded["player"]["money"] != before["player"]["money"] or loaded["inventory"] != before["inventory"]):
        raise AutomationError("Wanted state did not persist through normal save/load")


def stone_fight(client, output, resource, option):
    yuemeier.approach_target(client, output, resource, (117, 186), magic_file="player-magic-烈火情天.ini")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_012_惠安镇/石南虫对话.txt")
    yycs.interact_named(client, output, resource, "石南虫", choice=(site["id"], 2))
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_012_惠安镇/石南虫死亡.txt"]
    decisions = ((sites[0]["id"], 0), (sites[1]["id"], option)) if option in (0, 1) else ((sites[0]["id"], 1),)
    yycs.fight_named(client, output, resource, "石南虫", use_magic=True, single_target=True, choice=decisions)
    verify_script(output, resource, sites[0]["path"])
    state = checkpoint(client, output, "stone-death-response")
    guards = [row for row in state["targets"] if row["name"] in ("李捕头", "捕快")]
    if (any(row["hostile"] for row in guards) != (option != 1)
            or option in (0, 1) and state["variables"].get("renrou") != "1"):
        raise AutomationError("Stone interrogation or police faction outcome differs")
    client.save_or_load(1)
    checkpoint(client, output, "stone-death-response-slot1")


def long_blackmail(client, output, resource, option):
    if client.observe()["map"] == "map_012_惠安镇.map":
        yuemeier.approach_exit(client, output, resource, 3, magic_file="player-magic-烈火情天.ini")
        yycs.transition(client, resource.parent / "yycs", "map_027_连接地图.map", 3, running=True)
    target = next(row for row in yycs.idle(client)["targets"] if row["name"] == "龙在田")
    yuemeier.approach_target(client, output, resource, (target["position"]["x"], target["position"]["y"]),
                            magic_file="player-magic-烈火情天.ini")
    client.save_or_load(0)
    before = checkpoint(client, output, "long-before-blackmail-slot0")
    if before["variables"].get("renrou") != "1":
        raise AutomationError("Long blackmail requires the actual Stone confession")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_027_连接地图/龙在田对话.txt"]
    decisions = ((sites[0]["id"], 1), (sites[1]["id"], 0), (sites[2]["id"], 0),
                 (sites[3]["id"], option if option < 3 else 1))
    if option >= 3:
        if int(before["variables"].get("koucai") or 0) >= 1:
            raise AutomationError("Long's refused thousand requires an actual zero persuasion skill")
        decisions += ((sites[4]["id"], 1 if option == 3 else 0),)
    yycs.interact_named(client, output, resource, "龙在田", choice=decisions)
    after = checkpoint(client, output, "long-blackmail-result")
    if (after["player"]["money"] != before["player"]["money"] + (500 if option == 0 else 1000 if option == 1 else 0)
            or after["inventory"] != before["inventory"] or after["variables"]["event"] != before["variables"]["event"]):
        raise AutomationError("Long blackmail fee, goods or morality differs")
    if option < 2:
        try:
            client.interact(target["id"], timeout=15)
        except AutomationError as error:
            if str(error) not in ("Interact: target_not_interactive", "Interact: action_rejected"):
                raise
        actions = next(site for site in yycs.inventory(resource)["choices"]
                       if site["path"] == "script/map/map_027_连接地图/右键龙在田.txt")
        yycs.idle(client, choice=(actions["id"], 3), output=output, resource=resource)
    elif option == 2:
        yycs.interact_named(client, output, resource, "龙在田", choice=(sites[0]["id"], 2))
    elif option == 3:
        if after["variables"].get("lztsq") != "1" or next(row for row in after["targets"] if row["name"] == "龙在田")["hostile"]:
            raise AutomationError("Refused thousand did not retain the merchant and set his refusal binding")
        yycs.interact_named(client, output, resource, "龙在田")
    else:
        if not next(row for row in after["targets"] if row["name"] == "龙在田")["hostile"]:
            raise AutomationError("Refused thousand did not start the selected fight")
    repeated = checkpoint(client, output, "long-no-repeat-blackmail")
    if repeated["player"]["money"] != after["player"]["money"] or repeated["inventory"] != after["inventory"]:
        raise AutomationError("Long blackmail repeated its payment")
    if option == 4:
        yycs.fight_named(client, output, resource, "龙在田", use_magic=True, single_target=True,
                         magic_file="player-magic-烈火情天.ini")
        verify_script(output, resource, "script/map/map_027_连接地图/龙在田死亡.txt")
    client.wait_until(lambda state: state["player"]["action"] == 0, timeout=15,
                      description="standing before normal blackmail save")
    client.save_or_load(1)
    saved = checkpoint(client, output, "long-blackmail-slot1")
    yycs.load_checkpoint(client, 1)
    loaded = checkpoint(client, output, "long-blackmail-loaded-slot1")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if saved[key] != loaded[key]:
            raise AutomationError(f"Long blackmail save/load changed {key}")


def jade_start(client, output, resource):
    yuemeier.approach_target(client, output, resource, (84, 44), magic_file="player-magic-烈火情天.ini")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_012_惠安镇/战渺渺对话.txt"]
    yycs.interact_named(client, output, resource, "战渺渺", choice=tuple((site["id"], 0) for site in sites))
    before = checkpoint(client, output, "jade-task-accepted")
    yycs.interact_named(client, output, resource, "战渺渺")
    pending = checkpoint(client, output, "jade-task-without-item")
    if pending["inventory"] != before["inventory"] or pending["variables"].get("jbzmm") != "1":
        raise AutomationError("Jade task granted a reward without the item")
    client.save_or_load(0)
    yuemeier.approach_target(client, output, resource, (77, 70), magic_file="player-magic-烈火情天.ini")
    site = next(site for site in yycs.inventory(resource)["choices"]
                if site["path"] == "script/map/map_012_惠安镇/玉佩宝箱.txt")
    yycs.interact_named(client, output, resource, "玉佩宝箱", position=(77, 70), choice=(site["id"], 1))
    after = checkpoint(client, output, "jade-native-stolen-item")
    expected = {row["file"]: row["quantity"] for row in before["inventory"]}
    expected["玉佩.ini"] = expected.get("玉佩.ini", 0) + 1
    if ({row["file"]: row["quantity"] for row in after["inventory"]} != expected
            or any(row["name"] == "唐三千" and row["hostile"] for row in after["targets"])):
        raise AutomationError("Stealing-three jade box did not grant its item peacefully")
    check_used_object(client, output, "jade-box-no-repeat", (77, 70))
    client.save_or_load(1)
    checkpoint(client, output, "jade-before-handover-slot1")


def jade_resolve(client, output, resource, option):
    if option == 2:
        report_town_task(client, output, resource, 2, "jbzmm", "战渺渺")
        return
    yuemeier.approach_target(client, output, resource, (84, 44), magic_file="player-magic-烈火情天.ini")
    before = checkpoint(client, output, "jade-handover-before")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_012_惠安镇/战渺渺偷玉.txt"]
    yycs.interact_named(client, output, resource, "战渺渺", choice=(sites[0]["id"], option))
    after = checkpoint(client, output, "jade-handover-result")
    expected = {row["file"]: row["quantity"] for row in before["inventory"]}
    if option == 0:
        expected["玉佩.ini"] -= 1
        if not expected["玉佩.ini"]:
            del expected["玉佩.ini"]
        expected["铁锹.ini"] = expected.get("铁锹.ini", 0) + 1
        if after["variables"].get("yupei") != "1":
            raise AutomationError("Jade handover did not settle its native flag")
        yycs.interact_named(client, output, resource, "战渺渺", choice=(sites[1]["id"], 1))
    else:
        if not any(row["name"] == "战渺渺" and row["hostile"] for row in after["targets"]):
            raise AutomationError("Jade refusal did not start its native battle")
    if ({row["file"]: row["quantity"] for row in after["inventory"]} != expected
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Jade handover inventory or money differs")
    if option == 1:
        yycs.fight_named(client, output, resource, "战渺渺", use_magic=True, single_target=True)
        verify_script(output, resource, "script/map/map_012_惠安镇/战渺渺死亡.txt")
    else:
        repeated = checkpoint(client, output, "jade-handover-no-repeat")
        if repeated["inventory"] != after["inventory"] or repeated["player"]["money"] != after["player"]["money"]:
            raise AutomationError("Jade handover repeated its reward")
    client.save_or_load(2)
    checkpoint(client, output, "jade-resolution-slot2")


def cave_boundary(client, output, resource):
    yuemeier.approach_exit(client, output, resource, 1, magic_file="player-magic-烈火情天.ini", excluded_traps=(4, 5))
    yycs.transition(client, resource.parent / "yycs", "map_009_山洞内部.map", 1, running=True)
    for index, (x, y, script) in enumerate(((25, 43, "捡钱.txt"), (25, 48, "捡钱.txt"), (4, 11, "捡东西1.txt"),
                                          (24, 40, "捡药品低级.txt"), (4, 8, "捡药品低级.txt"))):
        yuemeier.approach_target(client, output, resource, (x, y), magic_file="player-magic-烈火情天.ini")
        before = checkpoint(client, output, f"cave-nine-box-{index}-before")
        yycs.interact_named(client, output, resource, "宝箱", position=(x, y))
        verify_script(output, resource, "script/common/" + script)
        after = checkpoint(client, output, f"cave-nine-box-{index}-reward")
        if script == "捡钱.txt":
            if not 50 <= after["player"]["money"] - before["player"]["money"] <= 500 or after["inventory"] != before["inventory"]:
                raise AutomationError("Cave money box reward differs from its fifty-to-five-hundred range")
        elif (sum(row["quantity"] for row in after["inventory"]) != sum(row["quantity"] for row in before["inventory"]) + 1
              or after["player"]["money"] != before["player"]["money"]):
            raise AutomationError("Cave item box did not grant exactly one item")
        check_used_object(client, output, f"cave-nine-box-{index}", (x, y))
    client.save_or_load(0)
    checkpoint(client, output, "cave-nine-boxes-slot0")
    yuemeier.approach_exit(client, output, resource, 2, magic_file="player-magic-烈火情天.ini")
    yycs.transition(client, resource.parent / "yycs", "map_010_山洞内部.map", 2, running=True)
    for index, (x, y, script) in enumerate(((23, 56, "捡钱.txt"), (23, 57, "捡钱.txt"), (16, 41, "捡药品中级.txt"),
                                          (16, 42, "捡药品低级.txt"), (15, 42, "捡钱.txt"))):
        yuemeier.approach_target(client, output, resource, (x, y), magic_file="player-magic-烈火情天.ini")
        before = checkpoint(client, output, f"cave-ten-box-{index}-before")
        yycs.interact_named(client, output, resource, "宝箱", position=(x, y))
        verify_script(output, resource, "script/common/" + script)
        after = checkpoint(client, output, f"cave-ten-box-{index}-reward")
        if script == "捡钱.txt":
            if not 50 <= after["player"]["money"] - before["player"]["money"] <= 500 or after["inventory"] != before["inventory"]:
                raise AutomationError("Cave money box reward differs from its fifty-to-five-hundred range")
        elif (sum(row["quantity"] for row in after["inventory"]) != sum(row["quantity"] for row in before["inventory"]) + 1
              or after["player"]["money"] != before["player"]["money"]):
            raise AutomationError("Cave item box did not grant exactly one item")
        check_used_object(client, output, f"cave-ten-box-{index}", (x, y))
    yuemeier.blocked_exit(client, output, resource, 2, "cave-ten-exit2-closed", magic_file="player-magic-烈火情天.ini", excluded_traps=(1,))
    client.save_or_load(1)
    checkpoint(client, output, "cave-ten-boxes-boundary-slot1")


def steal_map(client, output, resource, option):
    if option == 1:
        qingping_entry(client, output, resource)
    elif option == 4:
        for destination, trap in (("map_014_连接地图.map", 2), ("map_017_连接地图.map", 3),
                                  ("map_018_连接地图.map", 2), ("map_019_寒波谷.map", 2)):
            yuemeier.approach_exit(client, output, resource, trap, magic_file="player-magic-烈火情天.ini")
            yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    elif option == 5:
        yycs.transition(client, resource.parent / "yycs", "map_020_樱花谷.map", 2, running=True)
    elif option == 6:
        for destination, trap in (("map_024_倚天山.map", 1), ("map_021_油菜花地.map", 1),
                                  ("map_022_清平乡.map", 2)):
            yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    state = yycs.idle(client)
    if state["map"] == "map_012_惠安镇.map" and any(row["name"] == "老鱼" and row["position"] == dict(x=36, y=92) for row in state["targets"]):
        yuemeier.approach_target(client, output, resource, (36, 92), magic_file="player-magic-烈火情天.ini")
        site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == "script/map/map_012_惠安镇/老鱼对话.txt")
        yycs.interact_named(client, output, resource, "老鱼", choice=(site["id"], 1))
    client.save_or_load(0)
    folder = output / "user-data/save" / SAVE_NAMESPACE / "rpg1"
    game = configparser.ConfigParser(interpolation=None, strict=False)
    game.read(folder / "game.ini", encoding="utf-8-sig")
    saved = configparser.ConfigParser(interpolation=None, strict=False)
    saved.read(folder / game.get("state", "npc"), encoding="utf-8-sig")
    state = checkpoint(client, output, "native-steal-map-before-slot0")
    candidates = []
    for section in saved.sections():
        filename = saved.get(section, "ScriptFileRight", fallback="")
        path = "script/map/" + state["map"].rsplit(".", 1)[0] + "/" + filename
        source = resource / path
        if not filename or not source.exists():
            continue
        text = source.read_text(encoding="utf-8-sig")
        flag = re.search(r'assign\("(tou[^\"]*)",1\)', text)
        if not flag:
            continue
        target = next((row for row in state["targets"] if row["kind"] == "npc"
                       and row["name"] == saved.get(section, "Name")
                       and row["position"] == dict(x=saved.getint(section, "MapX"), y=saved.getint(section, "MapY"))), None)
        if target is None:
            matches = [row for row in state["targets"] if row["kind"] == "npc"
                       and row["name"] == saved.get(section, "Name")]
            if len(matches) == 1:
                target = matches[0]
        if target:
            candidates.append((target["id"], path, text, flag[1]))
    if state["map"] == "map_025_摘星楼.map":
        candidates.sort(key=lambda row: row[1].endswith("/右键庄文璐.txt"))
    proofs = []
    previous_proofs = json.loads((output / "native-steal-proofs.json").read_text(encoding="utf-8")) if (output / "native-steal-proofs.json").exists() else []
    for target_id, path, source, flag in candidates:
        known = next((row for row in previous_proofs if row["flag"] == flag and row["source"] == path), None)
        if known and known["status"] == "passed" and client.observe((flag,))["variables"].get(flag) == "1":
            proofs.append(known)
            continue
        state = yycs.idle(client)
        target = next(row for row in state["targets"] if row["id"] == target_id)
        try:
            yuemeier.approach_target(client, output, resource, (target["position"]["x"], target["position"]["y"]),
                                    magic_file="player-magic-烈火情天.ini")
        except AutomationError as error:
            if str(error).startswith("No connected trap 0"):
                pass
            elif target["name"] == "朱媚" and str(error).startswith("No connected normal interaction position"):
                hostage_pay(client, output, resource)
                yuemeier.approach_target(client, output, resource, (target["position"]["x"], target["position"]["y"]),
                                        magic_file="player-magic-烈火情天.ini")
            elif state["map"] == "map_025_摘星楼.map" and target["name"] == "庄文璐" and str(error).startswith("No connected normal interaction position"):
                proofs.append(dict(actor=target["name"], targetId=target_id, source=path, flag=flag,
                                   status="unverified", reason=str(error)))
                write_json(output / "native-steal-proofs.json", proofs)
                continue
            else:
                raise
        before = checkpoint(client, output, f"native-steal-{flag}-before")
        original_flag = client.observe((flag,))["variables"].get(flag)
        site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == path)
        steal_option = next(row["index"] for row in site["options"] if "偷窃" in row["text"])
        alternate_named(client, output, resource, target["name"], (site["id"], steal_option), target_id=target_id)
        verify_script(output, resource, path)
        after = checkpoint(client, output, f"native-steal-{flag}-reward")
        original = {row["file"]: row["quantity"] for row in before["inventory"]}
        actual = {row["file"]: row["quantity"] for row in after["inventory"]}
        money = re.search(r'addrandmoney\((\d+),(\d+)\)', source)
        goods = re.search(r'addrandgoods\("([^\"]+)"\)', source)
        if original_flag == "1":
            if actual != original or before["player"]["money"] != after["player"]["money"]:
                raise AutomationError("Previously used NPC steal repeated its reward")
        elif money:
            if actual != original or not int(money[1]) <= after["player"]["money"] - before["player"]["money"] <= int(money[2]):
                raise AutomationError("NPC steal money is outside its actual source range")
        elif goods:
            stock = configparser.ConfigParser(interpolation=None, strict=False)
            stock.read(resource / "ini/buy" / goods[1], encoding="utf-8-sig")
            allowed = {stock.get(section, "IniFile") for section in stock.sections() if stock.has_option(section, "IniFile")}
            changed = {key: value - original.get(key, 0) for key, value in actual.items() if value != original.get(key, 0)}
            if (len(changed) != 1 or next(iter(changed.values())) != 1 or not set(changed) <= allowed
                    or any(key not in actual for key in original) or before["player"]["money"] != after["player"]["money"]):
                raise AutomationError("NPC steal item differs from its actual reward table")
        else:
            raise AutomationError("Steal reward source needs an explicit route")
        if (client.observe((flag,))["variables"].get(flag) != "1" or after["variables"]["event"] != before["variables"]["event"]
                or any(row["id"] == target_id and row["hostile"] for row in after["targets"])):
            raise AutomationError("Successful NPC stealing changed morality or hostility or missed its flag")
        alternate_named(client, output, resource, target["name"], (site["id"], steal_option), target_id=target_id)
        repeated = checkpoint(client, output, f"native-steal-{flag}-no-repeat")
        if repeated["inventory"] != after["inventory"] or repeated["player"]["money"] != after["player"]["money"]:
            raise AutomationError("Native NPC steal repeated its reward")
        proofs.append(dict(actor=target["name"], targetId=target_id, source=path, flag=flag, status="passed"))
        write_json(output / "native-steal-proofs.json", proofs)
    if not proofs:
        raise AutomationError("No bound NPC steal was actually tested")
    client.save_or_load(1)
    checkpoint(client, output, "native-steal-map-complete-slot1")


def map_boundary(client, output, resource, option):
    if option == 0:
        yuemeier.approach_exit(client, output, resource, 2, magic_file="player-magic-烈火情天.ini")
        yycs.transition(client, resource.parent / "yycs", "map_014_连接地图.map", 2, running=True)
        for trap in (2, 4):
            yuemeier.blocked_exit(client, output, resource, trap, f"south-unused-exit-{trap}-closed",
                                  magic_file="player-magic-烈火情天.ini", excluded_traps=(1, 3))
    elif option == 1:
        yuemeier.blocked_exit(client, output, resource, 2, "zhaixing-unused-exit2-closed",
                              magic_file="player-magic-烈火情天.ini", excluded_traps=(1,))
    elif option == 2:
        yuemeier.blocked_exit(client, output, resource, 1, "east-unused-exit1-closed",
                              magic_file="player-magic-烈火情天.ini", excluded_traps=(2,))
    elif option == 3:
        yuemeier.blocked_exit(client, output, resource, 4, "east-unused-exit4-closed",
                              magic_file="player-magic-烈火情天.ini", excluded_traps=(2,))
    elif option == 4:
        yycs.transition(client, resource.parent / "yycs", "map_012_惠安镇.map", 2, running=True)
        yycs.transition(client, resource.parent / "yycs", "map_027_连接地图.map", 3, running=True)
        for trap, filename in ((3, "事件40.txt"), (5, "事件61.txt")):
            before = client.observe(("Event", "event"))
            yuemeier.blocked_exit(client, output, resource, trap, f"east-legacy-event-trap-{trap}-inactive",
                                  magic_file="player-magic-烈火情天.ini", excluded_traps=(2,))
            verify_script(output, resource.parent / "yycs", "script/map/map_027_连接地图/" + filename)
            after = client.observe(("Event", "event"))
            if after["variables"] != before["variables"]:
                raise AutomationError("Inactive legacy map event changed its actual variables")
    client.save_or_load(0)
    checkpoint(client, output, "normal-map-boundaries-slot0")


def paid_inn(client, output, resource):
    target = next(row for row in yycs.idle(client)["targets"] if row["name"] == "唐四公子")
    yuemeier.approach_target(client, output, resource, (target["position"]["x"], target["position"]["y"]),
                            magic_file="player-magic-烈火情天.ini")
    state = client.observe()
    if state["cheatInvincibilityEnabled"]:
        raise AutomationError("Paid inn uses a normal source without native invincibility")
    spell = next(row for row in state["magic"] if row["file"] == "player-magic-清心咒.ini")
    client.assign_magic(spell["slot"], 0)
    client.act("CastSkill", generation=state["generation"], slot=0)
    before = checkpoint(client, output, "paid-inn-real-deficit")
    if before["player"]["mana"] >= before["player"]["manaMax"]:
        raise AutomationError("Paid inn did not start with a real mana deficit")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_012_惠安镇/唐四公子对话.txt"]
    decisions = [(sites[0]["id"], 0), (sites[1]["id"], 0)]
    if before["player"]["money"] < 20:
        decisions.append((sites[2]["id"], 1))
    yycs.interact_named(client, output, resource, "唐四公子", choice=tuple(decisions))
    after = checkpoint(client, output, "paid-inn-result")
    if before["player"]["money"] >= 20:
        if (after["player"]["money"] != before["player"]["money"] - 20
                or after["player"]["position"] != dict(x=88, y=136)
                or after["player"]["mana"] != after["player"]["manaMax"]):
            raise AutomationError("Paid inn did not charge twenty and restore the actual deficit")
    elif after["player"]["money"] != before["player"]["money"] or after["player"]["mana"] > before["player"]["mana"]:
        raise AutomationError("Short paid-inn source was charged or restored")
    if after["inventory"] != before["inventory"]:
        raise AutomationError("Paid inn changed inventory")
    client.save_or_load(0)


def movement_symbol(client, output, resource):
    before = checkpoint(client, output, "movement-symbol-before")
    item = next(row for row in before["inventory"] if row["file"] == "神行太保.ini")
    site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == "script/goods/神行太保.txt")
    proofs = []
    for option, action_type in ((0, 3), (1, 2)):
        state = client.observe()
        request = client.submit("UseItem", generation=state["generation"], slot=item["slot"])
        client.wait_until(lambda value: bool(value.get("choices")), description="normal movement symbol choice")
        yycs.choose_site(client, output, resource, site["id"], option)
        client.wait_action(request, timeout=3)
        client.save_or_load(option + 1)
        saved = configparser.ConfigParser(interpolation=None)
        saved.read(output / "user-data/save" / SAVE_NAMESPACE / f"rpg{option + 2}/player0.ini", encoding="utf-8-sig")
        if saved.getint("init", "WalkIsRun") != (1 if option == 0 else 0):
            raise AutomationError("Movement symbol did not set its native persistent state")
        state = client.observe()
        position = state["player"]["position"]
        occupied = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]}
        path = yycs.reachable_trap(resource.parent / "yycs", state["map"], 3, position, occupied,
                                  avoid=occupied, with_path=True)
        if len(path) < 8:
            raise AutomationError("No long enough normal path for the movement symbol test")
        destination = path[min(12, len(path) - 2)]
        request = client.submit("MoveTo", generation=state["generation"], x=destination[0], y=destination[1],
                                running=False, timeoutMs=30000)
        moving = client.wait_until(lambda value: value["player"]["action"] == action_type, timeout=10,
                                   description="actual native running or walking after symbol use")
        checkpoint(client, output, f"movement-symbol-choice-{option}-actual-action")
        client.wait_action(request, timeout=32)
        after = checkpoint(client, output, f"movement-symbol-choice-{option}-complete")
        if after["inventory"] != before["inventory"] or after["player"]["money"] != before["player"]["money"]:
            raise AutomationError("Movement symbol consumed inventory or money")
        proofs.append(dict(option=option, nativeWalkIsRun=saved.getint("init", "WalkIsRun"), actualAction=moving["player"]["action"]))
    client.save_or_load(3)
    write_json(output / "movement-symbol-proof.json", dict(status="passed", results=proofs))


def grave_reward(client, output, resource, option):
    destination, crossings = (
        ("map_012_惠安镇.map", ()),
        ("map_008_野树林.map", (("map_011_连接地图.map", 1), ("map_008_野树林.map", 1))),
        ("map_027_连接地图.map", (("map_027_连接地图.map", 3),)),
        ("map_014_连接地图.map", (("map_014_连接地图.map", 2),)),
        ("map_014_连接地图.map", (("map_014_连接地图.map", 2),)),
    )[option]
    for map_name, trap in crossings:
        if yycs.idle(client)["map"] != map_name:
            yuemeier.approach_exit(client, output, resource, trap, magic_file="player-magic-烈火情天.ini")
            yycs.transition(client, resource.parent / "yycs", map_name, trap, running=True)
    client.save_or_load(0)
    if option == 4:
        guard = next(row for row in client.observe()["targets"] if row["name"] == "李沉舟")
        yuemeier.approach_target(client, output, resource,
                                (guard["position"]["x"], guard["position"]["y"]), magic_file="player-magic-烈火情天.ini")
        site = next(site for site in yycs.inventory(resource)["choices"]
                    if site["path"] == "script/map/map_014_连接地图/右键李沉舟.txt")
        alternate_named(client, output, resource, "李沉舟", (site["id"], 2))
        yycs.fight_named(client, output, resource, "李沉舟", use_magic=True, single_target=True,
                         magic_file="player-magic-烈火情天.ini")
        verify_script(output, resource, "script/map/map_014_连接地图/李沉舟死亡.txt")
    target = next(row for row in client.observe()["targets"] if row["name"] == "坟墓")
    point = (target["position"]["x"], target["position"]["y"])
    yuemeier.approach_target(client, output, resource, point, magic_file="player-magic-烈火情天.ini")
    before = checkpoint(client, output, "grave-before-actual-digging")
    if not any(row["file"] == "铁锹.ini" for row in before["inventory"]):
        raise AutomationError("Grave route requires a normally obtained shovel")
    path = "script/map/" + destination.rsplit(".", 1)[0] + "/" + ("坟墓安全.txt" if option == 4 else "坟墓.txt")
    site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == path)
    yycs.interact_named(client, output, resource, "坟墓", choice=(site["id"], 0))
    verify_script(output, resource, path)
    after = checkpoint(client, output, "grave-actual-reward-and-morality")
    table = re.search(r'addrandgoods\("([^\"]+)"\)', (resource / path).read_text(encoding="utf-8"))[1]
    stock = configparser.ConfigParser(interpolation=None, strict=False)
    stock.read(resource / "ini/buy" / table, encoding="utf-8-sig")
    allowed = {stock.get(section, "IniFile") for section in stock.sections() if stock.has_option(section, "IniFile")}
    original = {row["file"]: row["quantity"] for row in before["inventory"]}
    actual = {row["file"]: row["quantity"] for row in after["inventory"]}
    changed = {key: value - original.get(key, 0) for key, value in actual.items() if value != original.get(key, 0)}
    if (len(changed) != 1 or next(iter(changed.values())) != 1 or not set(changed) <= allowed
            or after["player"]["money"] != before["player"]["money"]
            or int(after["variables"]["event"]) != int(before["variables"]["event"]) - 1):
        raise AutomationError("Grave reward, shovel retention, money or morality differs")
    check_used_object(client, output, "grave-reward-no-repeat", point)
    if option == 3:
        if not next(row for row in client.observe()["targets"] if row["name"] == "李沉舟")["hostile"]:
            raise AutomationError("Guarded digging did not turn Li Chen Zhou hostile")
        yycs.fight_named(client, output, resource, "李沉舟", use_magic=True, single_target=True,
                         magic_file="player-magic-烈火情天.ini")
        verify_script(output, resource, "script/map/map_014_连接地图/李沉舟死亡.txt")
        check_used_object(client, output, "grave-after-guard-death-no-repeat", point)
    client.save_or_load(1)
    write_json(output / "grave-reward-proof.json", dict(status="passed", map=destination, position=point,
               source=path, actualTable=table, inventoryChange=changed, shovelRetained=True,
               moralityDecrease=1, repeatedRewardRejected=True, nativeGuardBattle=option >= 3))


def ma_bao(client, output, resource, option):
    if yycs.idle(client)["map"] == "map_022_清平乡.map":
        yycs.transition(client, resource.parent / "yycs", "map_021_油菜花地.map", 1, running=True)
    target = next(row for row in client.observe()["targets"] if row["name"] == "马宝")
    yuemeier.approach_target(client, output, resource,
                            (target["position"]["x"], target["position"]["y"]), magic_file="player-magic-烈火情天.ini")
    client.save_or_load(0)
    before = checkpoint(client, output, "ma-bao-normal-branch-source")
    if option == 2:
        path = "script/map/map_021_油菜花地/右键马宝.txt"
        site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == path)
        alternate_named(client, output, resource, "马宝", (site["id"], 1))
        stolen = checkpoint(client, output, "ma-bao-stolen-money")
        if (not 50 <= stolen["player"]["money"] - before["player"]["money"] <= 100
                or client.observe(("toumb",))["variables"]["toumb"] != "1"
                or stolen["inventory"] != before["inventory"]):
            raise AutomationError("Ma Bao stealing reward or actual flag differs")
        alternate_named(client, output, resource, "马宝", (site["id"], 1))
        after = checkpoint(client, output, "ma-bao-stealing-no-repeat")
        if after["player"]["money"] != stolen["player"]["money"]:
            raise AutomationError("Ma Bao repeated his stolen money")
    else:
        path = "script/map/map_021_油菜花地/马宝对话.txt"
        sites = [site for site in yycs.inventory(resource)["choices"] if site["path"] == path]
        yycs.interact_named(client, output, resource, "马宝", choice=((sites[0]["id"], 0), (sites[1]["id"], option)))
        after = checkpoint(client, output, "ma-bao-empathy-or-confrontation")
        target = next((row for row in after["targets"] if row["name"] == "马宝"), None)
        if (option == 0 and target is not None or option == 1 and (target is None or not target["hostile"])
                or after["inventory"] != before["inventory"] or after["player"]["money"] != before["player"]["money"]):
            raise AutomationError("Ma Bao empathy/confrontation outcome differs")
        if option == 1:
            yycs.fight_named(client, output, resource, "马宝", use_magic=True, single_target=True,
                             magic_file="player-magic-烈火情天.ini")
            verify_script(output, resource, "script/map/map_021_油菜花地/马宝死亡.txt")
            after = checkpoint(client, output, "ma-bao-death-ao-biao-hostile")
            if (int(after["variables"]["event"]) != int(before["variables"]["event"]) - 1
                    or not next(row for row in after["targets"] if row["name"] == "敖彪")["hostile"]):
                raise AutomationError("Ma Bao death did not apply the actual Ao Biao faction and morality effects")
    verify_script(output, resource, path)
    client.save_or_load(1)
    checkpoint(client, output, "ma-bao-result-slot1")


def village_branch(client, output, resource, option):
    client.save_or_load(0)
    before = checkpoint(client, output, "village-branch-normal-source-slot0")
    name = "元义方" if option < 2 else "保镖" if option < 4 else "章大爷"
    target = next(row for row in before["targets"] if row["name"] == name)
    try:
        yuemeier.approach_target(client, output, resource,
                                (target["position"]["x"], target["position"]["y"]),
                                magic_file="player-magic-烈火情天.ini")
    except AutomationError as error:
        if option != 4 or not str(error).startswith("No connected normal interaction position"):
            raise
        try:
            client.interact(target["id"], timeout=15)
        except (AutomationError, TimeoutError) as native_error:
            after = checkpoint(client, output, "chapter-poison-native-access-unverified")
            if after["inventory"] != before["inventory"] or after["variables"].get("SubEvent15") != before["variables"].get("SubEvent15"):
                raise AutomationError("Inaccessible poison candidate changed the player's quest or inventory")
            write_json(output / "chapter-poison-proof.json", dict(triggerPassed=False, cureStatus="unverified",
                       position=target["position"], nativeAccessError=str(native_error), staticOnly=False))
            return
        raise AutomationError("Native poison interaction reached the candidate; its dialogue must be verified")
    if option < 2:
        path = "script/map/map_022_清平乡/元义方对话.txt"
        sites = [site for site in yycs.inventory(resource)["choices"] if site["path"] == path]
        yycs.interact_named(client, output, resource, name, choice=((sites[0]["id"], 1), (sites[1]["id"], 1)))
        client.save_or_load(1)
        changed = configparser.ConfigParser(interpolation=None, strict=False)
        changed.read(output / "user-data/save" / SAVE_NAMESPACE / "rpg2/qingpingxiang.npc", encoding="utf-8-sig")
        if [changed.get(section, "ScriptFile", fallback="") for section in changed.sections()
                if changed.get(section, "Name", fallback="") == name] != ["元义方生气.txt"]:
            raise AutomationError("Yuan's insult did not change his actual dialogue binding")
        path = "script/map/map_022_清平乡/元义方生气.txt"
        site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == path)
        yycs.interact_named(client, output, resource, name, choice=(site["id"], option))
    elif option < 4:
        path = "script/map/map_022_清平乡/保镖对话.txt"
        site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == path)
        yycs.interact_named(client, output, resource, name, choice=(site["id"], 0))
        allowed = checkpoint(client, output, "bodyguard-normal-persuasion-doorway")
        if next(row for row in allowed["targets"] if row["name"] == name)["position"] != dict(x=17, y=28):
            raise AutomationError("Bodyguard did not move to his actual allowed doorway position")
        client.save_or_load(1)
        path = "script/map/map_022_清平乡/保镖被骗.txt"
        site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == path)
        yycs.interact_named(client, output, resource, name, choice=(site["id"], 1 if option == 2 else 0))
    else:
        yycs.interact_named(client, output, resource, name)
        path = "script/map/map_022_清平乡/中毒者对话.txt"
        verify_script(output, resource.parent / "yycs", path)
        started = checkpoint(client, output, "chapter-poison-quest-triggered")
        if (started["variables"].get("SubEvent15") != "2"
                or int(started["variables"].get("EvilVal") or 0) != int(before["variables"].get("EvilVal") or 0) - 20
                or started["inventory"] != before["inventory"] or started["player"]["money"] != before["player"]["money"]):
            raise AutomationError("Actual poison binding did not start its dependency quest without a reward")
        yycs.interact_named(client, output, resource, name)
        repeated = checkpoint(client, output, "chapter-poison-pending-no-repeat")
        if repeated["variables"].get("EvilVal") != started["variables"].get("EvilVal"):
            raise AutomationError("Pending poison dialogue repeated its initial morality effect")
        client.save_or_load(1)
        write_json(output / "chapter-poison-proof.json", dict(triggerPassed=True, cureStatus="unverified",
                   dependencyScript=path, nativeSubEvent15=2, doctorReachabilityNeedsReview=True))
        return
    verify_script(output, resource, path)
    after = checkpoint(client, output, "village-branch-result")
    target = next(row for row in after["targets"] if row["name"] == name)
    if (target["hostile"] != (option in (1, 3)) or after["inventory"] != before["inventory"]
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Village choice hostility, inventory or fee differs from its actual source")
    if option in (1, 3):
        yycs.fight_named(client, output, resource, name, use_magic=True, single_target=True,
                         magic_file="player-magic-烈火情天.ini")
        verify_script(output, resource, "script/map/map_022_清平乡/" + name + "死亡.txt")
    client.save_or_load(2)
    checkpoint(client, output, "village-branch-result-slot2")


def town_box_access(client, output, resource):
    client.save_or_load(2)
    checkpoint(client, output, "town-boxes-hostile-doorway-source-slot2")
    while True:
        state = yycs.idle(client)
        guards = [row for row in state["targets"] if row["name"] in ("李捕头", "捕快")
                  and row["hostile"] and yycs.npc_attackable(row)]
        if not guards:
            break
        position = state["player"]["position"]
        target = min(guards, key=lambda row: abs(row["position"]["x"] - position["x"]) * 2
                     + abs(row["position"]["y"] - position["y"]))
        for attempt in range(4):
            target = next((row for row in client.observe()["targets"] if row["id"] == target["id"]), None)
            if target is None or not yycs.npc_attackable(target):
                break
            try:
                yuemeier.approach_target(client, output, resource,
                                        (target["position"]["x"], target["position"]["y"]),
                                        magic_file="player-magic-烈火情天.ini")
                break
            except AutomationError as error:
                if not str(error).startswith("No connected trap 0") or attempt == 3:
                    raise
                time.sleep(0.4)
        if target is None:
            continue
        if any(row["id"] == target["id"] and yycs.npc_attackable(row) for row in client.observe()["targets"]):
            yycs.fight_named(client, output, resource, target["name"], target_id=target["id"],
                             use_magic=True, single_target=True, magic_file="player-magic-烈火情天.ini")
    state = checkpoint(client, output, "town-boxes-hostile-doorways-cleared")
    if any(row["name"] == "铁拔" for row in state["targets"]):
        if state["variables"].get("jbtb") != "1":
            hostage_start(client, output, resource)
        hostage_pay(client, output, resource)
    target = next((row for row in client.observe()["targets"] if row["name"] == "朱媚"), None)
    if target:
        yuemeier.approach_target(client, output, resource,
                                (target["position"]["x"], target["position"]["y"]),
                                magic_file="player-magic-烈火情天.ini")
        site = next(site for site in yycs.inventory(resource)["choices"]
                    if site["path"] == "script/map/map_012_惠安镇/右键朱媚.txt")
        alternate_named(client, output, resource, "朱媚", (site["id"], 2))
        before = checkpoint(client, output, "zhumei-player-combat-before-death")
        yycs.fight_named(client, output, resource, "朱媚", use_magic=True, single_target=True,
                         magic_file="player-magic-烈火情天.ini")
        verify_script(output, resource, "script/map/map_012_惠安镇/朱媚死亡.txt")
        after = checkpoint(client, output, "zhumei-player-combat-death")
        if int(after["variables"]["event"]) != int(before["variables"]["event"]) - 1:
            raise AutomationError("Player killing Zhu Mei did not apply its actual morality consequence")
    reward_boxes(client, output, resource)


def guarded_box(client, output, resource, option):
    name, point, guard, filename, threshold = (
        ("玉佩宝箱", (77, 70), "唐三千", "玉佩宝箱.txt", 3),
        ("发钗宝箱", (59, 150), "老点子", "发钗.txt", 4),
        ("宝箱梁大中", (72, 160), "梁大中", "宝箱梁大中.txt", 4),
        ("宝箱韦空幛", (71, 113), "韦空幛", "宝箱韦空幛.txt", 4),
    )[option]
    yuemeier.approach_target(client, output, resource, point, magic_file="player-magic-烈火情天.ini")
    client.save_or_load(0)
    before = checkpoint(client, output, "guarded-box-before-failed-steal")
    if int(before["variables"].get("touqie") or 0) >= threshold:
        raise AutomationError("Guarded failure requires an actual below-threshold stealing skill")
    path = "script/map/map_012_惠安镇/" + filename
    site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == path)
    yycs.interact_named(client, output, resource, name, position=point, choice=(site["id"], 1))
    verify_script(output, resource, path)
    failed = checkpoint(client, output, "guarded-box-failed-steal-hostility")
    target = next(row for row in failed["targets"] if row["name"] == guard)
    if (not target["hostile"] or failed["inventory"] != before["inventory"]
            or failed["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Failed stealing did not preserve rewards and turn its actual guard hostile")
    if option == 2 and not all(row["hostile"] for row in failed["targets"] if row["name"] in ("李捕头", "捕快")):
        raise AutomationError("Liang's guarded box did not alert the police")
    client.save_or_load(1)
    yuemeier.approach_target(client, output, resource,
                            (target["position"]["x"], target["position"]["y"]), magic_file="player-magic-烈火情天.ini")
    if any(row["name"] == guard and yycs.npc_attackable(row) for row in client.observe()["targets"]):
        yycs.fight_named(client, output, resource, guard, use_magic=True, single_target=True,
                         magic_file="player-magic-烈火情天.ini")
    verify_script(output, resource, "script/map/map_012_惠安镇/" + guard + "死亡.txt")
    checkpoint(client, output, "guarded-box-guard-native-death")
    reward_boxes(client, output, resource, positions=(point,))
    boxes = json.loads((output / "reward-box-proofs.json").read_text(encoding="utf-8"))
    if len(boxes) != 1 or boxes[0]["status"] != "passed":
        raise AutomationError("Guarded box reward remains unverified after its native fight")
    write_json(output / "guarded-box-proof.json", dict(status="passed", name=name, guard=guard, position=point,
               actualStealingSkill=before["variables"].get("touqie"), threshold=threshold,
               failureNoReward=True, guardActuallyHostile=True, nativeBattle=True, saveBytesEdited=False))


def reward_boxes(client, output, resource, positions=(), magic_file="player-magic-烈火情天.ini"):
    state = yycs.idle(client)
    if not positions and state["map"] == "map_012_惠安镇.map" and any(row["name"] == "老鱼" and row["position"] == dict(x=36, y=92) for row in state["targets"]):
        yuemeier.approach_target(client, output, resource, (36, 92), magic_file=magic_file)
        site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == "script/map/map_012_惠安镇/老鱼对话.txt")
        yycs.interact_named(client, output, resource, "老鱼", choice=(site["id"], 1))
    client.save_or_load(0)
    folder = output / "user-data/save" / SAVE_NAMESPACE / "rpg1"
    game = configparser.ConfigParser(interpolation=None, strict=False)
    game.read(folder / "game.ini", encoding="utf-8-sig")
    objects = configparser.ConfigParser(interpolation=None, strict=False)
    objects.read(folder / game.get("state", "obj"), encoding="utf-8-sig")
    state = client.observe()
    proofs = []
    write_json(output / "reward-box-proofs.json", proofs)
    for section in objects.sections():
        name = objects.get(section, "ObjName", fallback="")
        filename = objects.get(section, "ScriptFile", fallback="")
        if not any(label in name for label in ("宝箱", "宝盒")) or not filename:
            continue
        point = (objects.getint(section, "MapX"), objects.getint(section, "MapY"))
        if positions and point not in positions:
            continue
        sources = [(resource, "script/map/" + state["map"].rsplit(".", 1)[0] + "/" + filename),
                   (resource, "script/common/" + filename),
                   (resource.parent / "yycs", "script/map/" + state["map"].rsplit(".", 1)[0] + "/" + filename),
                   (resource.parent / "yycs", "script/common/" + filename)]
        source_root, path = next((root, path) for root, path in sources if (root / path).exists())
        source = (source_root / path).read_text(encoding="utf-8-sig")
        sites = [site for site in yycs.inventory(source_root)["choices"] if site["path"] == path]
        choice = None
        if sites:
            option = next(row["index"] for row in sites[0]["options"] if "偷窃" in row["text"] or "开锁" in row["text"])
            choice = (sites[0]["id"], option)
        try:
            yuemeier.approach_target(client, output, resource, point, magic_file=magic_file)
        except AutomationError as error:
            if not str(error).startswith("No connected normal interaction position"):
                raise
            if state["map"] == "map_008_野树林.map" and point == (18, 24):
                yuemeier.approach_exit(client, output, resource, 0, destination=(10, 24),
                                      magic_file=magic_file)
                for destination in ((12, 19), (15, 19)):
                    before_jump = checkpoint(client, output, f"forest-box-jump-{destination[0]}-{destination[1]}-before")
                    client.act("JumpTo", generation=before_jump["generation"], x=destination[0], y=destination[1])
                    checkpoint(client, output, f"forest-box-jump-{destination[0]}-{destination[1]}-after")
                yuemeier.approach_target(client, output, resource, point, magic_file=magic_file)
            else:
                unavailable = checkpoint(client, output, f"reward-box-{point[0]}-{point[1]}-no-walk-path")
                target = next(row for row in unavailable["targets"] if row["kind"] == "object" and row["position"] == dict(x=point[0], y=point[1]))
                try:
                    client.interact(target["id"], timeout=15)
                except (AutomationError, TimeoutError) as native_error:
                    after_attempt = checkpoint(client, output, f"reward-box-{point[0]}-{point[1]}-native-access-failed")
                    proofs.append(dict(name=name, position=point, source=path, status="unverified",
                                       reason=str(native_error), beforeInventory=unavailable["inventory"], afterInventory=after_attempt["inventory"]))
                    write_json(output / "reward-box-proofs.json", proofs)
                    continue
                raise AutomationError("Native box interaction reached a target excluded by static planning; add its actual access route")
        before = checkpoint(client, output, f"reward-box-{point[0]}-{point[1]}-before")
        yycs.interact_named(client, output, source_root, name, position=point, choice=choice)
        verify_script(output, source_root, path)
        after = checkpoint(client, output, f"reward-box-{point[0]}-{point[1]}-after")
        original = {row["file"]: row["quantity"] for row in before["inventory"]}
        actual = {row["file"]: row["quantity"] for row in after["inventory"]}
        money = re.search(r'addrandmoney\((\d+),(\d+)\)', source)
        random_goods = re.search(r'addrandgoods\("([^\"]+)"\)', source)
        fixed_goods = re.search(r'addgoods\("([^\"]+)"\)', source)
        if money:
            if actual != original or not int(money[1]) <= after["player"]["money"] - before["player"]["money"] <= int(money[2]):
                raise AutomationError("Box money differs from its actual source range")
        else:
            expected = original.copy()
            if fixed_goods:
                expected[fixed_goods[1]] = expected.get(fixed_goods[1], 0) + 1
            elif random_goods:
                stock = configparser.ConfigParser(interpolation=None, strict=False)
                stock.read(resource / "ini/buy" / random_goods[1], encoding="utf-8-sig")
                allowed = {stock.get(section, "IniFile") for section in stock.sections() if stock.has_option(section, "IniFile")}
                changed = {key: value - original.get(key, 0) for key, value in actual.items() if value != original.get(key, 0)}
                if len(changed) != 1 or next(iter(changed.values())) != 1 or not set(changed) <= allowed:
                    raise AutomationError("Box item differs from its actual reward table")
                expected = actual
            if actual != expected or after["player"]["money"] != before["player"]["money"]:
                raise AutomationError("Box item, empty result or fee differs")
        if after["variables"]["event"] != before["variables"]["event"]:
            raise AutomationError("Successful box opening unexpectedly changed morality")
        check_used_object(client, output, f"reward-box-{point[0]}-{point[1]}", point)
        proofs.append(dict(name=name, position=point, source=path, status="passed"))
        write_json(output / "reward-box-proofs.json", proofs)
        if state["map"] == "map_008_野树林.map" and point == (18, 24):
            yuemeier.approach_exit(client, output, resource, 0, destination=(14, 16),
                                  magic_file=magic_file)
            for destination in ((12, 19), (10, 24)):
                for attempt in range(4):
                    landing = checkpoint(client, output, f"forest-return-landing-{destination[0]}-{attempt}")
                    blockers = [actor for actor in landing["targets"] if actor["kind"] == "npc"
                                and actor.get("hostile") and actor["visibleFromPlayer"] and yycs.npc_attackable(actor)
                                and abs(actor["position"]["x"] - 10) * 2 + abs(actor["position"]["y"] - 24) <= 6]
                    if not blockers:
                        break
                    actor = blockers[0]
                    yycs.fight_named(client, output, resource, actor["name"], use_magic=True,
                                     single_target=True, target_id=actor["id"])
                    client.wait_until(lambda value: not any(row["id"] == actor["id"] and row["action"] != 255
                                      for row in value["targets"]), timeout=15,
                                      description="normal death animation releases forest landing")
                else:
                    raise AutomationError("Forest landing remains threatened after normal visible combat")
                jump = checkpoint(client, output, f"forest-box-return-{destination[0]}-{destination[1]}-before")
                client.act("JumpTo", generation=jump["generation"], x=destination[0], y=destination[1])
                returned = checkpoint(client, output, f"forest-box-return-{destination[0]}-{destination[1]}-after")
                if returned["player"]["position"] != dict(x=destination[0], y=destination[1]):
                    raise AutomationError("Normal forest return did not reach its confirmed landing")
    client.save_or_load(1)
    checkpoint(client, output, "reward-boxes-complete-slot1")


def blade_effects(client, output, resource):
    reward_boxes(client, output, resource, positions=((72, 160),))
    before = checkpoint(client, output, "blade-before-normal-equipment")
    blade = next(row for row in before["inventory"] if row["file"] == "goods-w11-悲魔之刃.ini")
    client.equip(blade["slot"])
    equipped = checkpoint(client, output, "blade-normally-equipped")
    if equipped["player"]["lifeMax"] != before["player"]["lifeMax"] - 500:
        raise AutomationError("Blade did not apply its actual minus-five-hundred maximum life")
    client.open_menu("Equip")
    checkpoint(client, output, "blade-real-equipment-panel")
    client.ui("Cancel")
    target = next(row for row in client.observe()["targets"] if row["name"] == "乞丐")
    yuemeier.approach_target(client, output, resource,
                            (target["position"]["x"], target["position"]["y"]),
                            magic_file="player-magic-烈火情天.ini")
    path = "script/map/map_012_惠安镇/右键乞丐.txt"
    site = next(site for site in yycs.inventory(resource)["choices"] if site["path"] == path)
    alternate_named(client, output, resource, "乞丐", (site["id"], 2), target_id=target["id"])
    verify_script(output, resource, path)
    proof = None
    for attempt in range(8):
        state = checkpoint(client, output, f"blade-native-attack-{attempt}-before")
        actor = next(row for row in state["targets"] if row["id"] == target["id"])
        if not yycs.npc_attackable(actor):
            raise AutomationError("Blade target died before its live poison state was saved")
        client.act("Attack", generation=state["generation"], targetId=actor["id"])
        client.save_or_load(2)
        folder = output / "user-data/save" / SAVE_NAMESPACE / "rpg3"
        game = configparser.ConfigParser(interpolation=None, strict=False)
        game.read(folder / "game.ini", encoding="utf-8-sig")
        saved = configparser.ConfigParser(interpolation=None, strict=False)
        saved.read(folder / game.get("state", "npc"), encoding="utf-8-sig")
        section = next(section for section in saved.sections() if saved.get(section, "Name", fallback="") == "乞丐")
        poisoned = saved.getfloat(section, "PoisonSeconds", fallback=0)
        after = checkpoint(client, output, f"blade-native-attack-{attempt}-after-slot2")
        remaining = next(row for row in after["targets"] if row["id"] == actor["id"])
        if poisoned > 0 and remaining["life"] < actor["life"]:
            proof = dict(status="passed", weapon=blade["file"], attackAttempt=attempt,
                         actualLifeMax=equipped["player"]["lifeMax"], poisonSeconds=poisoned,
                         poisonSource=saved.get(section, "PoisonByCharacterName", fallback=""),
                         targetLifeBefore=actor["life"], targetLifeAfter=remaining["life"],
                         normalManualSlot=2)
            break
    if proof is None:
        raise AutomationError("Blade attacks did not demonstrate their actual poison effect")
    write_json(output / "blade-effects-proof.json", proof)


def connector_rewards(client, output, resource):
    for destination, trap in (("map_024_倚天山.map", 1), ("map_023_连接地图.map", 3)):
        yuemeier.approach_exit(client, output, resource, trap, magic_file="player-magic-烈火情天.ini")
        yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    reward_boxes(client, output, resource)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--assets", type=Path, default=Path("assets"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--route", choices=("inventory", "opening", "departure", "hanbo-books", "hanbo-persuasion", "trade-graves", "changqi-hostility", "changqi-death", "hu-entry", "hu-choice", "yih-entry", "yih-choice", "huian-entry", "qingping-entry", "qingping-books", "qingping-help", "debt-start", "debt-settle", "anu-second-loan", "anu-return", "early-silver", "silver-steal", "dog-recruit", "town-books", "ao-herb-start", "ao-herb", "rescue-start", "rescue-yulin", "rescue-duel", "rescue-fight", "rescue-survivor", "rescue-daughter-death", "rescue-father-death", "attribute-book", "mayongcheng", "forest-lotus", "forest-return", "hostage-start", "hostage-pay", "hostage-thanks", "hostage-failure", "hostage-report", "caravan", "tong-lotus", "tong-death", "stone-report", "gang-start", "gang-resolve", "partner-drug", "partner-injury", "wanted", "stone-fight", "long-blackmail", "jade-start", "jade-resolve", "cave-boundary", "steal-map", "map-boundary", "paid-inn", "movement-symbol", "reward-boxes", "guarded-box", "town-box-access", "village-branch", "ma-bao", "grave-reward", "blade-effects", "connector-rewards", "medical-lesson", "medical-full", "su-recruit", "su-management", "tong-medical", "return-ginseng", "jiutou-offer", "jiutou-refuse", "jiutou-ending", "jiutou-gift", "jiutou-friendliness", "jiutou-postgame", "town-report", "observe", "exit") + tuple(CUSTOM_ROUTES), default="opening")
    parser.add_argument("--difficulty", choices=("easy", "hard"), default="easy")
    parser.add_argument("--gender", type=int, choices=(0, 1), default=0)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--source-run", type=Path)
    parser.add_argument("--load-slot", type=int, choices=range(7))
    parser.add_argument("--assist-level", type=int, choices=range(1, 81))
    parser.add_argument("--assist-money", type=int)
    parser.add_argument("--option", type=int, choices=range(7), default=1)
    args = parser.parse_args()
    if args.assist_money is not None and (args.resume or not args.source_run or not 0 <= args.assist_money <= 99999999):
        parser.error("Money assistance requires a fresh independent normal-save clone and a valid amount")
    sys.stdout.reconfigure(encoding="utf-8")
    output, assets = args.output.resolve(), args.assets.resolve()
    resource = assets / RESOURCE_DIRECTORY
    yycs.VARIABLES = ("event", "Level", "XZ", "xx", "XX", "xuanze", "XUANZE", "xz", "XuanZhe",
                      "koucai", "touqie", "yl", "cqyhd", "jtjyhd", "jtjsw", "jtjbnf", "toujtj", "mdl",
                      "KoucaiTalentLevel", "LeechcraftDifference", "SelValue", "toucq", "touhdd", "yh", "GoodsNum", "yiliao",
                      "jbzmm", "myc", "tgqnrs", "qrs", "jbsnc", "jbtb", "ganxin", "MoneyNum", "aoguang", "xlw")
    yycs.VARIABLES += ("lk", "jinengshu")
    yycs.VARIABLES += ("XUAN", "llsan", "XuanZh", "anfc", "gourou", "renrou", "lztsq", "zhaixinglou", "yulin")
    yycs.VARIABLES += ("zwl", "XE", "zhumei", "lyzd", "jbzsx", "fachai", "touqdw", "x", "xu", "xuan", "yupei", "SubEvent15", "EvilVal")
    yycs.VARIABLES += ADDITIONAL_VARIABLES
    if args.route == "inventory":
        output.mkdir(parents=True, exist_ok=False)
        yuemeier.source_candidates(assets, output)
        return
    if args.resume:
        identity = json.loads((output / "run.json").read_text(encoding="utf-8"))
        if identity["resourceId"] != RESOURCE_ID:
            parser.error("Resume requires a Yuchen run")
    else:
        if not args.exe or args.source_run and args.load_slot is None:
            parser.error("Fresh run requires --exe; cloning requires --load-slot")
        output.mkdir(parents=True, exist_ok=False)
        parent_identity = {}
        if args.source_run:
            parent = args.source_run.resolve()
            parent_identity = json.loads((parent / "run.json").read_text(encoding="utf-8"))
            if parent_identity["resourceId"] != RESOURCE_ID:
                parser.error("Source requires a Yuchen run")
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
        money_assistance = []
        if args.assist_money is not None:
            from run_xjxqy_gameplay import assist_saved_branch_numbers
            folder = output / "user-data/save" / SAVE_NAMESPACE / f"rpg{args.load_slot + 1}"
            saved_game = configparser.ConfigParser(interpolation=None)
            saved_game.read(folder / "game.ini", encoding="utf-8-sig")
            character_index = saved_game.getint("state", "chr")
            filename = f"player{character_index}.ini"
            path = folder / filename
            original = path.read_bytes()
            original_file = f"assist-money-player{character_index}-before.ini"
            (output / original_file).write_bytes(original)
            corrections = assist_saved_branch_numbers(folder, money=args.assist_money, money_character_index=character_index)
            assisted = path.read_bytes()
            pattern = rb'(?im)^(money[ \t]*=[ \t]*)[0-9]+([ \t\r]*)$'
            expected, count = re.subn(pattern, lambda match: match[1] + str(args.assist_money).encode("ascii") + match[2], original)
            changed = [name for name, value in save_hashes(output / "user-data/save").items() if before[name] != value]
            permitted = [f"{SAVE_NAMESPACE}/rpg{args.load_slot + 1}/{filename}"] if original != assisted else []
            if count != 1 or assisted != expected or changed != permitted or save_hashes(source_save) != before:
                raise AutomationError("Money assistance changed other saved bytes or its source")
            record = dict(source="independent-save-money", corrections=corrections, allOtherBytesAndFilesUnchanged=True,
                          originalFile=original_file, parentRun=str(parent), parentSlot=args.load_slot)
            write_json(output / "assist-money.json", record)
            money_assistance.append(record)
        executable = args.exe.resolve()
        session = PIPE_PREFIX + str(uuid.uuid4())
        command = [str(executable), "--assets", str(assets), "--resource-id", RESOURCE_ID,
                   "--skip-startup-video", "--enable-automation-hooks", "--automation-pipe", session,
                   "--user-data-root", str(output / "user-data"), "--log-file", str(output / "game.log")]
        with (output / "stdout.log").open("wb") as stdout, (output / "stderr.log").open("wb") as stderr:
            process = subprocess.Popen(command, cwd=executable.parent, stdout=stdout, stderr=stderr)
        identity = dict(resourceId=RESOURCE_ID, session=session, pid=process.pid, command=command,
                        engineSha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                        resourceProfileSha256=hashlib.sha256((resource / "game_profile.ini").read_bytes()).hexdigest(),
                        cheatAssisted=parent_identity.get("cheatAssisted", False) or bool(money_assistance),
                        assistanceRecords=parent_identity.get("assistanceRecords", []) + money_assistance, started=time.time())
        if parent_identity:
            identity.update(parentRun=str(args.source_run.resolve()), parentSlot=args.load_slot)
        write_json(output / "run.json", identity)
    started = time.time()
    snapshot = output / f"route-source-{time.time_ns()}"
    snapshot.mkdir()
    for filename in ("run_yuchen_gameplay.py", "run_yuemeier_gameplay.py", "run_yycs_gameplay.py",
                     "run_chenghe_gameplay.py", "gameplay_automation.py") + ADDITIONAL_ROUTE_SOURCES:
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
                yuemeier.assist_battle(client, output, args.assist_level)
            elif not args.resume and any(record.get("source") == "native-options-menu"
                                        for record in identity["assistanceRecords"]):
                yuemeier.assist_battle(client, output, client.observe()["player"]["level"])
            if args.route in CUSTOM_ROUTES:
                CUSTOM_ROUTES[args.route](client, output, resource, args)
            elif args.route == "opening":
                opening(client, output, resource, args.difficulty, args.gender)
            elif args.route == "departure":
                departure(client, output, resource)
            elif args.route == "hanbo-books":
                hanbo_books(client, output, resource)
            elif args.route == "hanbo-persuasion":
                hanbo_persuasion(client, output, resource)
            elif args.route == "trade-graves":
                trade_graves(client, output, resource)
            elif args.route == "changqi-hostility":
                changqi_hostility(client, output, resource)
            elif args.route == "changqi-death":
                verify_script(output, resource, "script/map/map_019_寒波谷/昌齐死亡.txt")
                client.save_or_load(1)
                checkpoint(client, output, "changqi-normal-death-slot1")
            elif args.route == "hu-entry":
                hu_entry(client, output, resource)
            elif args.route == "hu-choice":
                hu_choice(client, output, resource, args.option)
            elif args.route == "yih-entry":
                yih_entry(client, output, resource)
            elif args.route == "yih-choice":
                yih_choice(client, output, resource, args.option)
            elif args.route == "huian-entry":
                huian_entry(client, output, resource)
            elif args.route == "qingping-entry":
                qingping_entry(client, output, resource)
            elif args.route == "town-report":
                town_report(client, output, resource)
            elif args.route == "qingping-books":
                qingping_books(client, output, resource)
            elif args.route == "qingping-help":
                qingping_help(client, output, resource)
            elif args.route == "debt-start":
                debt_start(client, output, resource)
            elif args.route == "debt-settle":
                debt_settle(client, output, resource, args.option)
            elif args.route == "anu-second-loan":
                anu_second_loan(client, output, resource, args.option)
            elif args.route == "anu-return":
                anu_return(client, output, resource, args.option)
            elif args.route == "early-silver":
                early_silver(client, output, resource)
            elif args.route == "silver-steal":
                early_silver(client, output, resource, steal=True)
            elif args.route == "dog-recruit":
                dog_recruit(client, output, resource)
            elif args.route == "town-books":
                town_books(client, output, resource)
            elif args.route == "ao-herb-start":
                ao_herb_start(client, output, resource)
            elif args.route == "ao-herb":
                ao_herb(client, output, resource, args.option)
            elif args.route == "rescue-start":
                rescue_start(client, output, resource)
            elif args.route == "rescue-yulin":
                rescue_yulin(client, output, resource, args.option)
            elif args.route == "rescue-duel":
                rescue_duel(client, output, resource)
            elif args.route == "rescue-fight":
                rescue_fight(client, output, resource, args.option)
            elif args.route == "rescue-survivor":
                rescue_survivor(client, output, resource)
            elif args.route == "rescue-daughter-death":
                rescue_daughter_death(client, output, resource)
            elif args.route == "rescue-father-death":
                rescue_father_death(client, output, resource, args.option)
            elif args.route == "attribute-book":
                attribute_book(client, output, resource, args.option)
            elif args.route == "mayongcheng":
                mayongcheng(client, output, resource)
            elif args.route == "forest-lotus":
                forest_lotus(client, output, resource)
            elif args.route == "forest-return":
                forest_return(client, output, resource)
            elif args.route == "hostage-start":
                hostage_start(client, output, resource)
            elif args.route == "hostage-pay":
                hostage_pay(client, output, resource)
            elif args.route == "hostage-thanks":
                hostage_thanks(client, output, resource, args.option)
            elif args.route == "hostage-failure":
                hostage_failure(client, output, resource, args.option)
            elif args.route == "hostage-report":
                hostage_report(client, output, resource)
            elif args.route == "caravan":
                caravan(client, output, resource, args.option)
            elif args.route == "tong-lotus":
                tong_lotus(client, output, resource)
            elif args.route == "tong-death":
                tong_death(client, output, resource, args.option)
            elif args.route == "stone-report":
                stone_report(client, output, resource)
            elif args.route == "gang-start":
                gang_start(client, output, resource)
            elif args.route == "gang-resolve":
                gang_resolve(client, output, resource, args.option)
            elif args.route == "partner-drug":
                partner_drug(client, output, resource)
            elif args.route == "partner-injury":
                partner_injury(client, output, resource)
            elif args.route == "wanted":
                wanted(client, output, resource)
            elif args.route == "stone-fight":
                stone_fight(client, output, resource, args.option)
            elif args.route == "long-blackmail":
                long_blackmail(client, output, resource, args.option)
            elif args.route == "jade-start":
                jade_start(client, output, resource)
            elif args.route == "jade-resolve":
                jade_resolve(client, output, resource, args.option)
            elif args.route == "cave-boundary":
                cave_boundary(client, output, resource)
            elif args.route == "steal-map":
                steal_map(client, output, resource, args.option)
            elif args.route == "map-boundary":
                map_boundary(client, output, resource, args.option)
            elif args.route == "paid-inn":
                paid_inn(client, output, resource)
            elif args.route == "movement-symbol":
                movement_symbol(client, output, resource)
            elif args.route == "reward-boxes":
                reward_boxes(client, output, resource)
            elif args.route == "guarded-box":
                guarded_box(client, output, resource, args.option)
            elif args.route == "town-box-access":
                town_box_access(client, output, resource)
            elif args.route == "village-branch":
                village_branch(client, output, resource, args.option)
            elif args.route == "ma-bao":
                ma_bao(client, output, resource, args.option)
            elif args.route == "grave-reward":
                grave_reward(client, output, resource, args.option)
            elif args.route == "blade-effects":
                blade_effects(client, output, resource)
            elif args.route == "connector-rewards":
                connector_rewards(client, output, resource)
            elif args.route == "medical-lesson":
                medical_lesson(client, output, resource)
            elif args.route == "medical-full":
                medical_full(client, output, resource)
            elif args.route == "su-recruit":
                su_recruit(client, output, resource)
            elif args.route == "su-management":
                su_management(client, output, resource)
            elif args.route == "tong-medical":
                tong_medical(client, output, resource)
            elif args.route == "return-ginseng":
                return_ginseng(client, output, resource)
            elif args.route == "jiutou-offer":
                jiutou_offer(client, output, resource, args.option)
            elif args.route == "jiutou-ending":
                jiutou_ending(client, output, resource)
            elif args.route == "jiutou-gift":
                jiutou_gift(client, output, resource, args.option)
            elif args.route == "jiutou-friendliness":
                jiutou_friendliness(client, output, resource)
            elif args.route == "jiutou-postgame":
                jiutou_postgame(client, output, resource)
            elif args.route == "jiutou-refuse":
                jiutou_refuse(client, output, resource)
            elif args.route == "exit":
                client.exit_game()
            if args.route != "exit":
                state = checkpoint(client, output, f"route-{args.route}-complete")
                result.update(map=state.get("map"), player=state.get("player"))
            result["status"] = "passed"
            if args.route == "steal-map":
                steals = json.loads((output / "native-steal-proofs.json").read_text(encoding="utf-8"))
                result.update(passedSteals=sum(row["status"] == "passed" for row in steals),
                              unverifiedSteals=[row for row in steals if row["status"] != "passed"])
                if result["unverifiedSteals"]:
                    result["status"] = "partial"
            if args.route == "village-branch" and args.option == 4:
                result["poisonCandidate"] = json.loads((output / "chapter-poison-proof.json").read_text(encoding="utf-8"))
                result["status"] = "partial"
            if args.route in ("reward-boxes", "town-box-access", "connector-rewards"):
                boxes = json.loads((output / "reward-box-proofs.json").read_text(encoding="utf-8"))
                result.update(passedBoxes=sum(row["status"] == "passed" for row in boxes),
                              unverifiedBoxes=[row for row in boxes if row["status"] != "passed"])
                if result["unverifiedBoxes"]:
                    result["status"] = "partial"
                elif not boxes:
                    result["status"] = "no-pending-boxes"
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
    raise SystemExit(0 if result["status"] in ("passed", "partial") else 1)


if __name__ == "__main__":
    main()
