"""Inventory XJXQY obligations and run routes using normal player actions."""
from __future__ import annotations

import argparse
import configparser
import datetime
import hashlib
import itertools
import json
import re
from pathlib import Path
import shutil
import subprocess
import time
import uuid

from gameplay_automation import Client, AutomationError
from run_yycs_gameplay import inventory, reachable_trap, trap_points, process_running, completed_script, choice_variable_changes

VARIABLES = ("Event", "Clue", "Result", "End", "Love", "Level", "Rain", "StopRain",
             "NoEnd", "Zhujiarenshen", "fight001", "Talkluqin", "Talkquxia",
             "Talkjiangxu", "Talkmatao", "Talkmap005luren", "Talkguarder", "Say",
             "Sub", "Choose", "Choosekezhan", "Bixue", "Talkzuilangzhong", "Talkchenpeng", "map017Enemy", "JiuLvLaoBan",
             "Select", "Selectmore", "Selectlast", "Talklvwencai60", "map027lwc", "map027lwcmore", "map027Enemy",
             "TalkLang80", "TalkLang90", "TalkYufu", "TalkLaow", "Suba",
             "Eventqiuyu", "Chooseqiuyu", "TalkDashou", "TalkJia", "Talkchenp", "PlayerLevel", "GoodsNum", "map039Enemy", "Choosemarry", "190", "Dodo", "Ask", "Talkzhangfu", "fight011", "fight008", "TalkJiguan", "Jiguan", "fight016", "fight029", "Talkmap029", "Talkjiangbo",
             "Talklaowang270", "Talk280", "Talkdaye280", "Talkxiaolin280", "Talkheshu280", "Talkheshen280",
             "Talkhuashan", "Talk048290", "gantan", "Talkxiaolin", "Talkheshu290", "Talkheshen",
             "Value", "Talkmaicai", "Talkyinshua", "Talklejianqiu", "Talkchangsangu", "Talklaoban", "Map050jiuke1", "Map050jiuke2", "fight051", "yamen", "fight062", "Knock", "Talkshijingliang",
             "Talkmap064", "Talkzhikeseng", "Talkwuzhi", "Talkyuanzhen", "Talkyuantong", "Chooseyuantong", "Talkheshang",
             "Talkdiaoyu", "Talkmap070", "Talkyueyun", "Talkliang", "Talkliuyun", "Talkhan", "Kufang", "zhang", "MoneyNum",
             "die", "Chooseyao", "Selyao", "Xuelao", "Choosewho", "Chooseman", "Choosedistance", "Choosestay",
             "Talkliehu", "Talkzhaoda", "Talktufei", "fight091", "Talkzhuhai", "Talkzhufuren", "qiang", "qiangmore",
             "Talkzhanggui", "Talkgongkai", "Saychake", "Talkcaocao", "Talkwlw", "Openbox2", "Openbox3", "Choose1", "Choose2", "Eventzhaoqi", "Openboxzhao")


def write_json(path, value):
    identity = path.parent / "run.json"
    if (path != identity and isinstance(value, dict) and "cheatAssisted" in value and identity.exists()
            and json.loads(identity.read_text(encoding="utf-8")).get("cheatAssisted") is True):
        value["cheatAssisted"] = True
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def catalog_for(resource):
    catalog = inventory(resource)
    catalog["resourceId"] = "XJXQY"
    return catalog


def enable_saved_partner_combat(path):
    before = path.read_bytes()
    pattern = rb'(?im)^(partnercombat\s*=\s*)[01](\s*)$'
    after, count = re.subn(pattern, rb'\g<1>1\g<2>', before)
    if count != 1:
        raise AutomationError("Expected one explicit PartnerCombat field in the cloned source slot")
    path.write_bytes(after)
    return dict(path=str(path), beforeSha256=hashlib.sha256(before).hexdigest(),
                afterSha256=hashlib.sha256(after).hexdigest(), field="PartnerCombat", value=1,
                changed=before != after)



def assist_saved_branch_numbers(folder, money=None, ruby_quantity=None, npc_vulnerable=None, player_life=None, ginseng_quantity=None,
                                player_life_max=None, player_evade=None, player_level=None, npc_life=None, character_level=None,
                                money_character_index=0):
    corrections = []
    if money is not None and money_character_index not in (0, 1, 2, 3):
        raise AutomationError("Money assistance requires an existing character index 0 to 3")
    fields = [(money, f"player{money_character_index}.ini", "money", None), (player_life_max, "player0.ini", "lifemax", None),
              (player_life, "player0.ini", "life", None), (player_evade, "player0.ini", "evade", None),
              (player_level, "player0.ini", "level", None),
              (ruby_quantity, "goods0.ini", "number", ("inifile", "goods402_红玉.ini")),
               (ginseng_quantity, "goods1.ini", "number", ("inifile", "goods216_双头人参.ini"))]
    if character_level is not None:
        index, level = character_level
        if index not in (0, 1, 2, 3):
            raise AutomationError("Character level assistance requires an existing character index0 to3")
        fields.append((level, f"player{index}.ini", "level", None))
    if npc_vulnerable is not None or npc_life is not None:
        game = configparser.ConfigParser(interpolation=None)
        game.read(folder / "game.ini", encoding="utf-8-sig")
        filename = game["state"]["npc"]
        if Path(filename).name != filename:
            raise AutomationError("Saved NPC numeric assistance requires a plain current-map filename")
        if npc_vulnerable is not None:
            fields.append((0, filename, "invincible", ("name", npc_vulnerable)))
        if npc_life is not None:
            fields.append((npc_life[1], filename, "life", ("name", npc_life[0])))
    for value, filename, field, selector in fields:
        if value is None:
            continue
        path = folder / filename
        before = path.read_bytes()
        ini = configparser.ConfigParser(interpolation=None)
        ini.read_string(before.decode("utf-8-sig"))
        sections = [name for name in ini.sections() if field in ini[name]
                    and (selector is None or ini[name].get(selector[0]) == selector[1])]
        if len(sections) != 1:
            raise AutomationError("Numeric assistance needs one exact saved numeric field")
        section = sections[0]
        if field == "life" and not 1 <= value <= ini.getint(section, "lifemax"):
            raise AutomationError("Assisted life must be positive and within the saved maximum")
        if field == "lifemax" and not 1 <= value <= 99999999:
            raise AutomationError("Assisted life maximum must be positive and within the numeric limit")
        if field == "evade" and not 0 <= value <= 99999999:
            raise AutomationError("Assisted evade must be nonnegative and within the numeric limit")
        if field == "level" and not 1 <= value <= 80:
            raise AutomationError("Assisted player level must be between1 and80")
        block = re.search(rb'(?ms)^(?:\xef\xbb\xbf)?\[' + re.escape(section.encode("utf-8")) + rb'\][^\n]*\n(.*?)(?=^\[|\Z)', before)
        if block is None:
            raise AutomationError("Saved numeric field section was not found")
        content, count = re.subn(rb'(?im)^(' + field.encode("ascii") + rb'[ \t]*=[ \t]*)[0-9]+([ \t\r]*)$',
                                 lambda match: match[1] + str(value).encode("ascii") + match[2], block[1])
        if count != 1:
            raise AutomationError("Numeric assistance needs one saved numeric field")
        after = before[:block.start(1)] + content + before[block.end(1):]
        path.write_bytes(after)
        corrections.append(dict(path=str(path), section=section, field=field, beforeValue=ini.getint(section, field),
                                afterValue=value, beforeSha256=hashlib.sha256(before).hexdigest(),
                                afterSha256=hashlib.sha256(after).hexdigest(), purpose="Explicitly assisted branch boundary"))
    return corrections


def protect_saved_partners(path):
    before = path.read_bytes()
    ini = configparser.ConfigParser()
    ini.read_string(before.decode("utf-8-sig"))
    partners = [name for name in ini.sections() if name.lower() != "head"]
    if not partners or any(ini.getint(name, "kind") != 3 for name in partners):
        raise AutomationError("Partner protection needs a normal partner-only save file")
    after, count = re.subn(rb'(?im)^(invincible\s*=\s*)\d+(\s*)$', rb'\g<1>1\g<2>', before)
    if count != len(partners):
        raise AutomationError("Partner protection cannot account for every saved partner")
    path.write_bytes(after)
    return dict(path=str(path), beforeSha256=hashlib.sha256(before).hexdigest(),
                afterSha256=hashlib.sha256(after).hexdigest(), field="Invincible", value=1,
                partners=partners, changed=before != after)


def title_visible(state):
    return {"new-game", "load-game", "exit"} <= {item["name"] for item in state.get("ui", [])}


def load_checkpoint(client, slot):
    state = client.wait_until(lambda value: not value.get("loading") and (title_visible(value)
                              or "map" in value and not value.get("inEvent")),
                              description="title or gameplay before load")
    if not title_visible(state):
        client.save_or_load(slot, load=True)
        return client.observe(VARIABLES)
    client.activate("load-game")
    state = client.wait_until(lambda value: "saveSlot" in value)
    while state["saveSlot"] != slot:
        client.focus("load")
        client.ui("Down" if state["saveSlot"] < slot else "Up")
        state = client.observe()
    client.activate("load")
    return client.wait_until(lambda value: value["worldInput"], variables=VARIABLES,
                             description="native load completion")


def checkpoint(client, output, name, variables=()):
    requested = tuple(dict.fromkeys((*variables, *VARIABLES)))
    if len(set(variables)) > 128:
        raise AutomationError("Checkpoint explicitly requested more than128 variables")
    # The native observation limit is128; keep this checkpoint's explicit fields first.
    state = client.observe(requested[:128])
    write_json(output / f"{name}.json", state)
    if not state.get("outputHealthy"):
        raise AutomationError("Required trace output is unavailable")
    shutil.copyfile(client.snapshot(), output / f"{name}.png")
    return state


def records(output):
    path = output / "user-data/automation/trace.jsonl"
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]


def script_proof(output, resource, relative, after_sequence=0):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        trace = records(output)
        starts = [row for row in trace if row.get("eventType") == "script.start"
                  and row.get("virtualPath", "").casefold() == relative.casefold()
                  and row.get("sequence", 0) > after_sequence]
        if starts:
            start = starts[-1]
            if start["contentSha256"] != hashlib.sha256((resource / relative).read_bytes()).hexdigest():
                raise AutomationError(f"Script content changed: {relative}")
            execution = [row for row in trace if row.get("executionId") == start["executionId"]]
            finished = [row for row in execution if row.get("eventType") == "script.finish"]
            if finished:
                if len(finished) != 1 or finished[0].get("status") != "completed":
                    raise AutomationError(f"Script failed: {relative}: {finished}")
                return dict(path=relative, sourceSha256=start["contentSha256"],
                            executionId=start["executionId"], start=start, finish=finished[0],
                            executedLines=sorted({row["line"] for row in execution if "line" in row}))
        time.sleep(0.1)
    raise AutomationError(f"Current completed script evidence missing: {relative}")


def choose_site(client, output, resource, site_id, option, remaining=(), gamble_round=False, shop_cancel=False,
                expected_terminal=None, final_dialogue=None):
    site = next(row for row in catalog_for(resource)["choices"] if row["id"] == site_id)
    state = client.observe((*VARIABLES, site["variable"]))
    if (state.get("choiceMessage") != site["message"]
            or [row["text"] for row in state.get("choices", [])] != [row["text"] for row in site["options"]]):
        raise AutomationError(f"Choice does not match {site_id}")
    trace = records(output)
    start = next(row for row in reversed(trace) if row.get("eventType") == "script.start" and row.get("virtualPath") == site["path"])
    active_lines = [row for row in trace if row.get("executionId") == start["executionId"] and row.get("eventType") == "source.line"]
    if start["contentSha256"] != site["sourceSha256"] or not active_lines or active_lines[-1]["line"] != site["line"]:
        raise AutomationError(f"Choice has no matching current source line: {site_id}")
    name = f"choice-{site['coverageId']}-{option}-{time.time_ns()}"
    checkpoint(client, output, name + "-before", (site["variable"],))
    if expected_terminal:
        client.act("SetAutoDialogue", enabled=False)
    action = client.act("Choose", context=state["context"], options=[option])
    after = idle(client, choices=remaining, output=output, resource=resource, gamble_round=gamble_round,
                 shop_cancel=shop_cancel, expected_terminal=expected_terminal, final_dialogue=final_dialogue)
    execution = completed_script(output, start)
    changes = choice_variable_changes(execution, active_lines[-1]["sequence"], site["variable"])
    if (changes and changes[0].get("afterValue") != str(option)
            or not changes and state["variables"].get(site["variable"]) != str(option)):
        raise AutomationError(f"Choice lacks native assignment: {site_id}")
    checkpoint(client, output, name + "-after", (site["variable"],))
    write_json(output / f"choice-proof-{name}.json", dict(status="passed", choiceSite=site_id,
               choiceIndex=option, scriptStart=start, action=action, cheatAssisted=False,
               beforeFile=str(output / f"{name}-before.json"), afterFile=str(output / f"{name}-after.json"),
               expectedTerminal=expected_terminal))
    print(f"Choice verified: {site_id} / {option}", flush=True)
    return after


def idle(client, timeout=180, choices=(), output=None, resource=None, gamble_round=False, shop_cancel=False,
         expected_terminal=None, final_dialogue=None, capture_videos=False):
    if expected_terminal:
        timeout = max(timeout, 600)
    deadline = time.monotonic() + timeout
    captured_videos = set()
    while time.monotonic() < deadline:
        state = client.observe(VARIABLES)
        if state.get("choices"):
            if choices:
                return choose_site(client, output, resource, *choices[0], remaining=choices[1:],
                                   gamble_round=gamble_round, shop_cancel=shop_cancel,
                                   expected_terminal=expected_terminal, final_dialogue=final_dialogue)
            raise AutomationError(f"Unmapped choice: {state.get('script')} / {state.get('choiceMessage')}")
        if (expected_terminal or capture_videos) and state.get("video"):
            if state["context"] not in captured_videos:
                rendered = client.wait_until(lambda value: value.get("video") and value.get("context") == state["context"]
                                              and value.get("frame", 0) >= state["frame"] + 30,
                                              timeout=10, description="presented native ending video")
                checkpoint(client, output, f"{'ending' if expected_terminal else 'native'}-video-{rendered['context']}")
                captured_videos.add(rendered["context"])
            time.sleep(0.2)
            continue
        if expected_terminal and not state.get("autoDialogue") and state.get("dialogue", {}).get("complete"):
            if final_dialogue and final_dialogue in state["dialogue"]["text"]:
                client.wait_until(lambda value: value.get("context") == state["context"]
                                  and value.get("frame", 0) >= state["frame"] + 30, timeout=5,
                                  description="presented complete ending text")
                checkpoint(client, output, f"ending-dialogue-{state['context']}")
            client.ui("Confirm")
            continue
        if "shop" in state:
            if not shop_cancel:
                raise AutomationError("Unmapped native shop menu")
            if not state["shop"]:
                raise AutomationError("Native shop has no goods")
            if callable(shop_cancel):
                shop_cancel(state)
            name = f"native-shop-{time.time_ns()}"
            before = checkpoint(client, output, name + "-before")
            client.ui("Cancel")
            after = client.wait_until(lambda value: "shop" not in value, description="native shop cancellation")
            if after["player"]["money"] != before["player"]["money"]:
                raise AutomationError("Cancelling the shop changed money")
            checkpoint(client, output, name + "-after")
            continue
        if any(row["name"] == "gamble-primary" for row in state.get("ui", [])):
            if not gamble_round:
                raise AutomationError("Unmapped native gambling menu")
            play_gamble_round(client, output, stake=gamble_round if type(gamble_round) is int else 80)
            gamble_round = False
        if state.get("worldInput") and not state.get("inEvent") and not expected_terminal:
            return state
        if title_visible(state):
            if expected_terminal:
                proof = script_proof(output, resource, expected_terminal)
                execution = [row for row in records(output) if row.get("executionId") == proof["executionId"]]
                if not any(row.get("apiName") == "returntotitle" for row in execution):
                    raise AutomationError("Ending source lacks its native return to title")
                return state
            raise AutomationError("Route returned to the title without an expected terminal")
        time.sleep(0.2)
    raise TimeoutError(f"Route did not settle: {state.get('map')} / {state.get('script')}")


def transition(client, resource, destination, trap, output, name, choices=(), expected_terminal=None, final_dialogue=None):
    deadline = time.monotonic() + 180
    for attempt in range(5):
        state = idle(client)
        bindings = configparser.ConfigParser(interpolation=None)
        bindings.read(resource / "ini/save/traps.ini", encoding="utf-8-sig")
        source = f"script/map/{Path(state['map']).stem}/{bindings[Path(state['map']).stem][str(trap)]}"
        before = records(output)[-1]["sequence"]
        occupied = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]}
        avoided = occupied
        try:
            path = reachable_trap(resource, state["map"], trap, state["player"]["position"], occupied, with_path=True, avoid=avoided)
        except AutomationError as route_error:
            if "No connected trap" not in str(route_error):
                raise
            # A following NPC can leave a narrow route while the player approaches.
            party = configparser.ConfigParser(interpolation=None)
            party.read(output / "user-data/save/xjxqy/game/partner0.ini", encoding="utf-8-sig")
            followers = {party.get(section, "name", fallback="") for section in party.sections()}
            avoided = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"] if row["name"] not in followers}
            path = reachable_trap(resource, state["map"], trap, state["player"]["position"], occupied, with_path=True, avoid=avoided)
        x, y = path[-1]
        action = client.submit("MoveTo", generation=state["generation"], x=x, y=y,
                               running=True, timeoutMs=180000)
        while time.monotonic() < deadline:
            current = client.observe(VARIABLES)
            status = client.request("GetActionStatus", actionId=action)
            if current.get("inEvent") or current["generation"] != state["generation"]:
                if status["status"] == "running":
                    client.request("CancelAction", actionId=action)
                current = idle(client, choices=choices, output=output, resource=resource,
                               expected_terminal=expected_terminal, final_dialogue=final_dialogue)
                starts = [row for row in records(output) if row.get("eventType") == "script.start" and row["sequence"] > before]
                destination_reached = (current.get("map") == destination
                                       or destination == "Title" and title_visible(current) and expected_terminal)
                if destination_reached and any(row["virtualPath"] == source for row in starts):
                    write_json(output / f"{name}-script-proof.json", script_proof(output, resource, source, before))
                    checkpoint(client, output, name)
                    return current
                if current.get("map") != state["map"]:
                    raise AutomationError(f"Unexpected map: {current.get('map')} (expected {destination})")
                checkpoint(client, output, f"{name}-on-route-{attempt}")
                break
            if status["status"] != "running":
                # A short refusal can finish between observations. Its native
                # completed source, rather than a sampled inEvent, proves it.
                if status["status"] == "succeeded" and status["reason"] == "arrived" and current.get("map") == destination:
                    proof = script_proof(output, resource, source, before)
                    write_json(output / f"{name}-script-proof.json", proof)
                    checkpoint(client, output, name)
                    return current
                if status["status"] == "failed" and status["reason"] in ("no_progress", "blocked_destination"):
                    current_occupied = {(row["position"]["x"], row["position"]["y"]) for row in current["targets"]}
                    if (current["map"] == state["map"] and current["variables"] == state["variables"]
                            and current_occupied != occupied):
                        checkpoint(client, output, f"{name}-changed-occupancy-{attempt}")
                        break
                    points = [point for point in path[1:3] if point not in occupied]
                    if points:
                        try:
                            client.move(*points[-1])
                        except AutomationError as move_error:
                            if not any(reason in str(move_error) for reason in ("blocked_destination", "no_progress")):
                                raise
                        refreshed = checkpoint(client, output, f"{name}-occupied-step-{attempt}")
                        refreshed_occupied = {(row["position"]["x"], row["position"]["y"]) for row in refreshed["targets"]}
                        if (refreshed["map"] == state["map"] and refreshed["variables"] == state["variables"]
                                and (refreshed["player"]["position"] != state["player"]["position"]
                                     or refreshed_occupied != occupied)):
                            break
                raise AutomationError(f"Trap was not entered: {status}")
            time.sleep(0.2)
        else:
            client.request("CancelAction", actionId=action)
            raise TimeoutError(f"Trap {trap} did not settle")
    raise AutomationError(f"Exit remained closed after five native events: {destination}")


def opening(client, output, resource, difficulty):
    state = client.wait_until(title_visible, description="XJXQY title menu")
    if state["resourceId"] != "XJXQY":
        raise AutomationError(f"Wrong resource: {state['resourceId']}")
    checkpoint(client, output, "01-title")
    client.act("SetAutoDialogue", enabled=True)
    client.activate("new-game")
    videos = []
    deadline = time.monotonic() + 240
    while time.monotonic() < deadline:
        state = client.observe(VARIABLES)
        if state.get("video"):
            videos.append(state["video"])
            client.ui("Cancel")
        elif state.get("choices"):
            if (state.get("choiceMessage") != "请选择游戏的难度："
                    or [item["text"] for item in state["choices"]] != ["普通", "困难"]):
                raise AutomationError(f"Unexpected difficulty choice: {state.get('choices')}")
            before = checkpoint(client, output, "02-difficulty-choice")
            index = int(difficulty == "hard")
            choice_action = client.act("Choose", context=state["context"], options=[index])
            break
        time.sleep(0.2)
    else:
        raise TimeoutError("Opening difficulty choice was not reached")
    state = idle(client)
    expected = dict(Event="10", End="1", Level=str(index))
    if state["map"] != "map001_衡山.map" or any(state["variables"].get(k) != v for k, v in expected.items()):
        raise AutomationError(f"Opening state mismatch: {state['map']} / {state['variables']}")
    if state["player"]["levelFile"] != f"level-{'hard' if index else 'easy'}.ini":
        raise AutomationError("Difficulty did not apply its native level file")
    checkpoint(client, output, "03-opening-complete")
    client.save_or_load(0)
    saved = checkpoint(client, output, "04-opening-saved-slot0")
    loaded = load_checkpoint(client, 0)
    if (loaded["generation"] <= saved["generation"]
            or any(loaded["variables"].get(k) != v for k, v in expected.items())
            or loaded["player"]["levelFile"] != saved["player"]["levelFile"]):
        raise AutomationError("Native save/load did not preserve the opening state")
    checkpoint(client, output, "05-opening-loaded-slot0")
    proof = script_proof(output, resource, "script/map/map001_衡山/begin.txt")
    write_json(output / "opening-proof.json", dict(status="passed", difficulty=difficulty,
               choiceSite="script/map/map001_衡山/begin.txt:97", choiceIndex=index,
               choiceContext=before["context"], action=choice_action, script=proof,
               variables=saved["variables"], player=saved["player"], sourceSlot=0,
               observedVideos=videos, saveLoadVerified=True, cheatAssisted=False, fullPlaythrough=False))
    print(f"Native {difficulty} opening passed; saved slot 0", flush=True)


def departure(client, output, resource):
    state = idle(client)
    if state["map"] != "map001_衡山.map" or state["variables"].get("Event") != "10":
        raise AutomationError("Departure requires the native opening slot 0")
    client.act("SetAutoDialogue", enabled=True)
    transition(client, resource, "map003_衡山派大厅.map", 1, output, "10-hall-entry")
    # The bedroom trigger stays on this map and advances Event through 15 to 20.
    state = transition(client, resource, "map003_衡山派大厅.map", 1, output, "11-master-last-wishes")
    if state["variables"].get("Event") != "20" or state["variables"].get("Result") != "0":
        raise AutomationError("Native last-wishes script did not finish at Event 20")
    proof = script_proof(output, resource, "script/map/map003_衡山派大厅/trap01.txt")
    client.save_or_load(1)
    checkpoint(client, output, "12-master-complete-slot1")
    transition(client, resource, "map001_衡山.map", 2, output, "13-hall-departure")
    transition(client, resource, "map004_衡山脚下.map", 3, output, "14-foothill-arrival")
    client.save_or_load(2)
    checkpoint(client, output, "15-letter-source-slot2")
    write_json(output / "departure-proof.json", dict(status="passed", script=proof,
               finalMap="map004_衡山脚下.map", Event="20", sourceSlots=[0, 1, 2], cheatAssisted=False))
    print("Native last wishes and foothill departure passed; letter source saved slot 2", flush=True)


def quantity(state, filename):
    return sum(row["quantity"] for row in state.get("inventory", []) if row["file"] == filename)


def interact_at(client, output, resource, name, source, prefix, position=None, choices=(), gamble_round=False, shop_cancel=False,
                capture_videos=False, target_id=None):
    for attempt in range(20):
        state = idle(client)
        targets = [row for row in state["targets"] if row["name"] == name
                   and row.get("interactive", True)
                   and (target_id is None or row["id"] == target_id)
                   and (position is None or row["position"] == dict(x=position[0], y=position[1]))]
        if len(targets) != 1:
            raise AutomationError(f"Expected one target {name} at {position}, got {targets}")
        before = records(output)[-1]["sequence"]
        try:
            client.interact(targets[0]["id"])
            break
        except AutomationError as error:
            if "interaction_not_observed" not in str(error):
                raise
            current = idle(client)
            starts = [row for row in records(output) if row.get("eventType") == "script.start" and row["sequence"] > before]
            if (current["map"] != state["map"]
                    or current["variables"].get("Event") != state["variables"].get("Event")
                    or any(row["virtualPath"] == source for row in starts)):
                raise
            if not starts and current["player"]["position"] == state["player"]["position"]:
                destination = targets[0]["position"]["x"], targets[0]["position"]["y"]
                occupied = {(row["position"]["x"], row["position"]["y"]) for row in current["targets"]}
                try:
                    path = reachable_trap(resource, current["map"], None, current["player"]["position"],
                                          with_path=True, destination=destination, avoid=occupied - {destination})
                except AutomationError as path_error:
                    if "No connected trap" not in str(path_error):
                        raise
                    # A follower can vacate a later narrow tile as we approach.
                    # Only request a nearby free point; native collision still decides.
                    try:
                        path = reachable_trap(resource, current["map"], None, current["player"]["position"],
                                              with_path=True, destination=destination)
                    except AutomationError as blocked_target:
                        if "No connected trap" not in str(blocked_target):
                            raise
                        x, y = destination
                        adjacent = ((x, y + 2), (x + y % 2 - 1, y + 1), (x - 1, y),
                                    (x + y % 2 - 1, y - 1), (x, y - 2),
                                    (x + y % 2, y - 1), (x + 1, y), (x + y % 2, y + 1))
                        paths = []
                        for point in adjacent:
                            if point in occupied:
                                continue
                            try:
                                paths.append(reachable_trap(resource, current["map"], None,
                                    current["player"]["position"], with_path=True, destination=point))
                            except AutomationError as adjacent_error:
                                if "No connected trap" not in str(adjacent_error):
                                    raise
                        if not paths:
                            raise blocked_target
                        path = min(paths, key=len)
                points = [point for point in path[1:3] if point not in occupied]
                if not points:
                    raise
                try:
                    client.move(*points[-1])
                except AutomationError as move_error:
                    if not any(reason in str(move_error) for reason in ("no_progress", "blocked_destination")):
                        raise
                    refreshed = checkpoint(client, output, f"{prefix}-occupied-step-{attempt}")
                    if refreshed["map"] != current["map"] or refreshed["variables"] != current["variables"]:
                        raise
                    if ("blocked_destination" in str(move_error)
                            or refreshed["player"]["position"] != current["player"]["position"]
                            or any(row["position"] == dict(x=points[-1][0], y=points[-1][1])
                                   for row in refreshed["targets"])):
                        continue
                    raise
            # A trap can clear the original click even without a bound script.
            # Reclick after verified movement or completed on-route scripts.
            for row in starts:
                script_proof(output, resource, row["virtualPath"], before)
            checkpoint(client, output, f"{prefix}-on-route-{attempt}")
    else:
        raise AutomationError(f"Interaction repeatedly interrupted on the route: {name}")
    if callable(choices):
        choices = choices()
    state = idle(client, choices=choices, output=output, resource=resource, gamble_round=gamble_round, shop_cancel=shop_cancel,
                 capture_videos=capture_videos)
    proof = script_proof(output, resource, source, before)
    checkpoint(client, output, prefix)
    write_json(output / f"{prefix}-script-proof.json", proof)
    return state


def early_detours(client, output, resource):
    state = idle(client)
    if state["map"] != "map001_衡山.map" or state["variables"].get("Event") != "10":
        raise AutomationError("Early detours require the opening slot 0")
    client.act("SetAutoDialogue", enabled=True)
    interact_at(client, output, resource, "卢青", "script/map/map001_衡山/卢青对话.txt", "20-luqing-reminder")
    transition(client, resource, "map001_衡山.map", 3, output, "21-early-departure-refused")
    if client.observe(VARIABLES)["variables"].get("Event") != "10":
        raise AutomationError("Refused early departure changed the story")
    script_proof(output, resource, "script/map/map001_衡山/trap03.txt")
    transition(client, resource, "map002_烟庐.map", 2, output, "22-smoke-hut-detour")
    transition(client, resource, "map001_衡山.map", 1, output, "23-smoke-hut-return")
    client.save_or_load(0)
    checkpoint(client, output, "24-detours-complete-slot0")
    write_json(output / "early-detours-proof.json", dict(status="passed", Event="10", cheatAssisted=False,
               earlyDepartureRefused=True, hutRoundTrip=True, sourceSlot=0))


def hengshan_dialogues(client, output, resource):
    state = idle(client)
    if state["map"] != "map003_衡山派大厅.map" or state["variables"].get("Event") != "20":
        raise AutomationError("Hall dialogues require the native last-wishes slot 1")
    client.act("SetAutoDialogue", enabled=True)
    routes = (("卢青", None, "卢青对话.txt", "Talkluqin", 2),
              ("曲霞", None, "曲霞对话.txt", "Talkquxia", 3),
              ("衡山派弟子", (6, 36), "江旭对话.txt", "Talkjiangxu", 2),
              ("衡山派弟子", (8, 38), "马涛对话.txt", "Talkmatao", 3))
    for name, position, filename, variable, maximum in routes:
        for number in range(1, maximum + 2):
            state = interact_at(client, output, resource, name, f"script/map/map003_衡山派大厅/{filename}",
                                f"30-hall-{variable}-{number}", position)
            if state["variables"].get(variable) != str(min(number, maximum)):
                raise AutomationError(f"Native repeated dialogue counter mismatch: {variable}")
    before = client.observe(VARIABLES)
    state = interact_at(client, output, resource, "宝箱", "script/map/map003_衡山派大厅/宝箱.txt", "31-hall-chest")
    if any(quantity(state, file) != quantity(before, file) + 1
           for file in ("goods214_灵芝草.ini", "goods031_布衣.ini")):
        raise AutomationError("Hall chest did not provide exactly its native goods")
    chest = next(row for row in state["targets"] if row["name"] == "宝箱")
    last = records(output)[-1]["sequence"]
    action = client.submit("Interact", generation=state["generation"], targetId=chest["id"], running=True)
    status = client.request("GetActionStatus", actionId=action)
    if status["status"] != "failed" or status["reason"] != "action_rejected":
        raise AutomationError(f"Empty chest did not reject repeated activation: {status}")
    state = checkpoint(client, output, "32-hall-chest-repeat")
    if (any(quantity(state, file) != quantity(before, file) + 1
            for file in ("goods214_灵芝草.ini", "goods031_布衣.ini"))
            or any(row.get("eventType") == "script.start" and row["sequence"] > last for row in records(output))):
        raise AutomationError("Empty chest repeated its script or reward")
    client.equip(next(row["slot"] for row in state["inventory"] if row["file"] == "goods031_布衣.ini"))
    client.save_or_load(3)
    checkpoint(client, output, "33-hall-prepared-slot3")
    write_json(output / "hengshan-dialogues-proof.json", dict(status="passed", repeatedCounters=True,
               chestRewardOnce=True, chestRepeat=status, sourceSlot=3, cheatAssisted=False))


def blood_letter(client, output, resource):
    state = idle(client)
    if state["map"] != "map003_衡山派大厅.map" or state["variables"].get("Event") != "20":
        raise AutomationError("Blood-letter route requires the prepared hall slot 3")
    client.act("SetAutoDialogue", enabled=True)
    sword = next(row for row in state["inventory"] if row["file"] == "goods047_铁剑.ini")
    client.equip(sword["slot"])
    state = client.observe(VARIABLES)
    skill = next(row for row in state["magic"] if row["file"] == "magic001_衡山有雪.ini")
    client.assign_magic(skill["slot"], 0)
    transition(client, resource, "map001_衡山.map", 2, output, "40-hall-departure")
    transition(client, resource, "map004_衡山脚下.map", 3, output, "41-foothill-entry")
    # Both exits must preserve the unfulfilled letter objective.
    transition(client, resource, "map004_衡山脚下.map", 2, output, "42-missing-letter-exit-refused")
    client.save_or_load(2)
    checkpoint(client, output, "43-letter-prepared-source-slot2")
    before = client.observe(VARIABLES)
    state = interact_at(client, output, resource, "石头", "script/map/map004_衡山脚下/捡到血书.txt", "44-blood-letter-found")
    if state["variables"].get("Event") != "25" or quantity(state, "goods401_血书.ini") != quantity(before, "goods401_血书.ini") + 1:
        raise AutomationError("Native letter interaction did not start the ambush with one letter")
    client.save_or_load(4)
    checkpoint(client, output, "45-ambush-source-slot4")
    deadline = time.monotonic() + 900
    while time.monotonic() < deadline:
        state = idle(client)
        if state["variables"].get("Event") == "30":
            break
        if state["variables"].get("Event") != "25":
            raise AutomationError(f"Unexpected ambush progress: {state['variables']}")
        client.act("StartCombat", timeout=125, generation=state["generation"], radius=64, kills=1,
                   skills=[0], lifeItem="goods214_灵芝草.ini", lifePercent=65, timeoutMs=120000)
        state = checkpoint(client, output, f"46-ambush-kills-{client.observe(VARIABLES)['variables'].get('fight001')}")
        print(f"Letter ambush: fight001={state['variables'].get('fight001')}, life={state['player']['life']}", flush=True)
    else:
        raise TimeoutError("Letter ambush did not complete")
    if state["variables"].get("fight001") != "11" or any(row.get("hostile") and row.get("attackable") for row in state["targets"]):
        raise AutomationError("Native letter ambush did not finish all eleven enemies")
    proof = script_proof(output, resource, "script/map/map004_衡山脚下/杀手死亡.txt")
    client.save_or_load(5)
    checkpoint(client, output, "47-ambush-complete-slot5")
    transition(client, resource, "map005_林间小道.map", 2, output, "48-wuyi-road-entry")
    client.save_or_load(6)
    checkpoint(client, output, "49-wuyi-road-source-slot6")
    write_json(output / "blood-letter-proof.json", dict(status="passed", nativeKills=11, Event="30",
               sourceSlots=[2, 4, 5, 6], lastDeathScript=proof, cheatAssisted=False))


def assist(client, output, level=None, invincible=False):
    before = checkpoint(client, output, "assistance-before")
    if not any(row["name"] == "save-load" for row in before.get("ui", [])):
        client.open_menu("System")
    client.activate("options")
    client.activate("cheat-settings")
    if not client.observe().get("cheatModeEnabled"):
        client.activate("cheat-mode")
    if invincible and not client.observe().get("cheatInvincibilityEnabled"):
        client.activate("invincibility")
    while level is not None and client.observe()["player"]["level"] < level:
        old = client.observe()["player"]["level"]
        client.activate("increase-player-level")
        if client.observe()["player"]["level"] <= old:
            raise AutomationError("Native cheat level action made no progress")
    client.activate("restore-resources")
    for _ in range(5):
        if client.observe().get("worldInput"):
            break
        client.ui("Cancel")
    after = checkpoint(client, output, "assistance-after")
    if (not after.get("worldInput") or not after.get("cheatModeEnabled")
            or invincible and not after.get("cheatInvincibilityEnabled")):
        raise AutomationError("Native cheat menu did not apply the requested assistance")
    if before["variables"] != after["variables"]:
        raise AutomationError("Native assistance unexpectedly changed observed plot variables")
    if after["player"]["lifeMax"] <= 0 or after["player"]["life"] <= 0:
        raise AutomationError("Native assistance left a nonpositive player life or life maximum")
    write_json(output / "assistance-proof.json", dict(status="passed", cheatAssisted=True,
               source="native-options-menu", beforePlayer=before["player"], afterPlayer=after["player"],
               requestedLevel=level, invincibility=after.get("cheatInvincibilityEnabled"),
               plotVariablesUnchanged=before["variables"] == after["variables"]))


def clear_enemies(client, output, prefix, ranged=False, skills=(0,), stop_variable=None, choices=(), resource=None):
    path_retries = 0
    for count in range(100):
        state = idle(client, choices=choices, output=output, resource=resource)
        if stop_variable is not None and stop_variable[0] not in state["variables"]:
            state = client.observe((*VARIABLES, stop_variable[0]))
        if (stop_variable is not None and state["variables"].get(stop_variable[0]) == stop_variable[1]) or not any(row.get("hostile") and row.get("attackable") for row in state["targets"]):
            return state
        try:
            client.act("StartCombat", timeout=125, generation=state["generation"], radius=64,
                       kills=1, skills=list(skills), allowMeleeFallback=not ranged, timeoutMs=120000)
        except AutomationError as error:
            if "no_progress" in str(error) and path_retries < 3:
                current = checkpoint(client, output, f"{prefix}-path-interruption-{count}")
                if current["player"]["position"] != state["player"]["position"]:
                    path_retries += 1
                    continue
            if "no_enemy_in_range" not in str(error):
                raise
            state = checkpoint(client, output, prefix + "-remaining-distant-enemies")
            write_json(output / f"{prefix}-combat-scope.json", dict(status="nearby-only",
                       remaining=[row for row in state["targets"] if row.get("hostile")], cheatAssisted=False))
            return state
        path_retries = 0
        checkpoint(client, output, f"{prefix}-{count}")
    raise AutomationError("Enemy clearing exceeded the map's test limit")


def walk_required_battle(client, output, resource, target_value, prefix, progress_variable="Event"):
    blocked_points = set()
    for attempt in range(120):
        try:
            clear_enemies(client, output, f"{prefix}-combat-{attempt}", skills=(),
                          stop_variable=(progress_variable, target_value) if progress_variable is not None else None)
        except AutomationError as error:
            if not any(reason in str(error) for reason in ("no_progress", "target_unreachable")):
                raise
            checkpoint(client, output, f"{prefix}-blocked-enemy-{attempt}")
        state = idle(client)
        if progress_variable is not None and progress_variable not in state["variables"]:
            state = client.observe((*VARIABLES, progress_variable))
        if progress_variable is not None and state["variables"].get(progress_variable) == target_value:
            return state
        enemies = [row for row in state["targets"] if row.get("hostile") and row.get("attackable")]
        if not enemies:
            if progress_variable is None:
                return state
            raise AutomationError("Required battle ended before its native story callback")
        visible = [row for row in enemies if row.get("visibleFromPlayer")]
        if visible:
            try:
                client.act("StartCombat", timeout=125, generation=state["generation"], targetId=visible[0]["id"],
                           radius=64, kills=1, skills=[], timeoutMs=120000)
                checkpoint(client, output, f"{prefix}-native-visible-target-{attempt}")
                continue
            except AutomationError as error:
                if not any(reason in str(error) for reason in ("no_progress", "target_unreachable")):
                    raise
                state = checkpoint(client, output, f"{prefix}-visible-target-blocked-{attempt}")
        position = state["player"]["position"]
        occupied = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]}
        for target in sorted(enemies, key=lambda row: abs(row["position"]["x"] - position["x"]) * 2
                             + abs(row["position"]["y"] - position["y"])):
            destination = (target["position"]["x"], target["position"]["y"])
            try:
                path = reachable_trap(resource, state["map"], None, position, with_path=True,
                                      destination=destination, avoid=blocked_points | (occupied - {destination, (position["x"], position["y"])}))
                break
            except AutomationError as error:
                if "No connected trap" not in str(error):
                    raise
        else:
            # Observe targets include passable bodies and decorative objects.
            # When they close every candidate path, try the static map and let
            # the native MoveTo collision check accept or reject the next step.
            occupied = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]
                        if row.get("kind") != "object" and row.get("life", 1) > 0}
            for target in enemies:
                destination = (target["position"]["x"], target["position"]["y"])
                try:
                    path = reachable_trap(resource, state["map"], None, position, with_path=True,
                                          destination=destination, avoid=blocked_points | (occupied - {destination}))
                    break
                except AutomationError as error:
                    if "No connected trap" not in str(error):
                        raise
            else:
                raise AutomationError("Required battle has no connected enemy approach")
        points = [point for point in path[1:min(3, len(path) - 1)] if point not in occupied]
        if not points:
            raise AutomationError("Required battle has no normal walking approach")
        try:
            client.move(*points[-1])
        except AutomationError as error:
            if not any(reason in str(error) for reason in ("blocked_destination", "no_progress")):
                raise
            blocked_points.add(points[-1])
            checkpoint(client, output, f"{prefix}-occupied-step-{attempt}")
            continue
        checkpoint(client, output, f"{prefix}-native-approach-{attempt}")
    else:
        raise AutomationError("Required battle exceeded its native walking limit")


def wuyi_first_visit(client, output, resource):
    state = idle(client)
    if state["variables"].get("Event") != "30":
        raise AutomationError("First Wuyi visit requires the completed letter ambush slot 6")
    client.act("SetAutoDialogue", enabled=True)
    if state["map"] in ("map007_武夷山九猴洞.map", "map011_武夷山顶.map") and (output / "premature-gate-proof.json").exists():
        return wuyi_from_cave(client, output, resource)
    if state["map"] != "map005_林间小道.map":
        raise AutomationError("First Wuyi visit requires its native road or verified cave checkpoint")
    for number in range(1, 4):
        state = interact_at(client, output, resource, "路人", "script/map/map005_林间小道/路人对话.txt", f"50-road-directions-{number}")
        if state["variables"].get("Talkmap005luren") != str(min(number, 2)):
            raise AutomationError("Native road directions did not preserve the repeated-talk limit")
    transition(client, resource, "map015_临安城南.map", 3, output, "51-premature-linan-arrival")
    # The approach crosses native trap tiles, including unbound traps that
    # cancel a long interaction click. Walk beside the guard, then click.
    state = client.observe(VARIABLES)
    client.act("MoveTo", generation=state["generation"], x=22, y=30,
               running=True, timeoutMs=45000, timeout=50)
    checkpoint(client, output, "51-premature-guard-approach")
    guard_responses = set()
    for number in range(16):
        state = interact_at(client, output, resource, "宋兵", "script/map/map015_临安城南/临安城南门士兵对话.txt",
                            f"52-premature-guard-response-{number}", (22, 29))
        guard_responses.add(state["variables"].get("Talkguarder"))
        if guard_responses == {"0", "1"}:
            break
    else:
        raise AutomationError("Both native random guard responses were not observed")
    occupied = [(row["position"]["x"], row["position"]["y"]) for row in state["targets"]
                if row.get("kind") == "npc" and row.get("interactive") and not row.get("hostile")]
    try:
        reachable_trap(resource, state["map"], 1, state["player"]["position"], avoid=occupied)
    except AutomationError as error:
        if "No connected trap" not in str(error):
            raise
    else:
        raise AutomationError("The closed guard formation unexpectedly allows a gate route")
    x, y = reachable_trap(resource, state["map"], 1, state["player"]["position"])
    action = client.submit("MoveTo", generation=state["generation"], x=x, y=y, running=True, timeoutMs=15000)
    try:
        client.wait_action(action, timeout=20)
    except AutomationError:
        blocked = client.request("GetActionStatus", actionId=action)
        if blocked["status"] != "failed" or blocked["reason"] != "no_progress":
            raise
    else:
        raise AutomationError("Movement through the closed guard formation unexpectedly succeeded")
    state = checkpoint(client, output, "52-premature-linan-gate-closed")
    if state["map"] != "map015_临安城南.map" or state["variables"].get("Event") != "30":
        raise AutomationError("Premature Linan gate changed the story")
    write_json(output / "premature-gate-proof.json", dict(status="passed", guardResponses=sorted(guard_responses),
               movementResult=blocked, gateStillClosed=True, Event="30", cheatAssisted=False))
    transition(client, resource, "map005_林间小道.map", 2, output, "53-premature-linan-return")
    transition(client, resource, "map006_武夷山脚.map", 2, output, "54-wuyi-foothill")
    transition(client, resource, "map007_武夷山九猴洞.map", 2, output, "55-nine-monkeys-cave")
    client.save_or_load(0)
    checkpoint(client, output, "56-nine-monkeys-source-slot0")
    return wuyi_from_cave(client, output, resource)


def wuyi_from_cave(client, output, resource):
    if client.observe(VARIABLES)["map"] == "map007_武夷山九猴洞.map":
        clear_enemies(client, output, "57-nine-monkeys-combat")
        transition(client, resource, "map011_武夷山顶.map", 2, output, "58-wuyi-summit")
    transition(client, resource, "map012_武夷山大厅.map", 2, output, "59-wuyi-hall")
    state = interact_at(client, output, resource, "张林", "script/map/map012_武夷山大厅/武夷派大弟子张林对话.txt", "60-liu-linan-clue")
    if state["variables"].get("Event") != "40" or quantity(state, "goods401_血书.ini") != 1:
        raise AutomationError("Native first Zhang Lin dialogue did not establish the Linan clue")
    interact_at(client, output, resource, "张林", "script/map/map012_武夷山大厅/武夷派大弟子张林对话.txt", "61-zhanglin-repeat")
    client.save_or_load(1)
    checkpoint(client, output, "62-wuyi-clue-slot1")
    transition(client, resource, "map011_武夷山顶.map", 1, output, "63-wuyi-hall-return")
    transition(client, resource, "map007_武夷山九猴洞.map", 1, output, "64-nine-monkeys-reentry")
    clear_enemies(client, output, "65-nine-monkeys-return-combat")
    transition(client, resource, "map006_武夷山脚.map", 1, output, "66-wuyi-descent")
    transition(client, resource, "map005_林间小道.map", 1, output, "67-wuyi-road-return")
    transition(client, resource, "map015_临安城南.map", 3, output, "68-linan-arrival")
    transition(client, resource, "map016_临安城.map", 1, output, "69-linan-gate-open")
    client.save_or_load(6)
    checkpoint(client, output, "70-linan-source-slot6")
    write_json(output / "wuyi-first-visit-proof.json", dict(status="passed", Event="40", letterRetained=True,
               prematureGateClosed=True, gateOpenAfterClue=True, sourceSlots=[0, 1, 6], cheatAssisted=False))


def linan_inn(client, output, resource):
    state = idle(client)
    if state["map"] != "map016_临安城.map" or state["variables"].get("Event") != "40":
        raise AutomationError("Inn tests require the native Linan entry slot 6")
    client.act("SetAutoDialogue", enabled=True)
    transition(client, resource, "map020_临安城客栈一楼.map", 6, output, "80-inn-arrival")
    client.save_or_load(0)
    source = checkpoint(client, output, "81-inn-choice-source-slot0")
    relative = "script/map/map020_临安城客栈一楼/客栈掌柜对话.txt"
    site = relative + ":14"
    state = interact_at(client, output, resource, "客栈掌柜", relative, "82-inn-refused", choices=[(site, 1)])
    if state["player"]["money"] != source["player"]["money"] or state["variables"].get("Event") != "40":
        raise AutomationError("Refusing the inn changed money or main story")
    load_checkpoint(client, 0)
    state = interact_at(client, output, resource, "客栈掌柜", relative, "83-inn-stayed", choices=[(site, 0)])
    if (state["player"]["money"] != source["player"]["money"] - 10
            or state["player"]["life"] != state["player"]["lifeMax"]
            or state["player"]["mana"] != state["player"]["manaMax"]):
        raise AutomationError("The inn did not charge ten and restore native resources")
    if source["player"]["money"] != 100:
        raise AutomationError("Native money-boundary route requires its original hundred silver")
    for number in range(9):
        before = state["player"]["money"]
        state = interact_at(client, output, resource, "客栈掌柜", relative, f"84-inn-repeat-{number}", choices=[(site, 0)])
        if state["player"]["money"] != before - 10:
            raise AutomationError("Repeated inn stay did not apply its fee exactly once")
    state = interact_at(client, output, resource, "客栈掌柜", relative, "85-inn-zero-silver-refused", choices=[(site, 0)])
    if state["player"]["money"] != 0 or state["variables"].get("Event") != "40":
        raise AutomationError("Insufficient silver changed money or main story")
    # Reuse the pre-choice save so the main route retains its native budget.
    load_checkpoint(client, 0)
    transition(client, resource, "map016_临安城.map", 1, output, "86-inn-return")
    client.save_or_load(6)
    checkpoint(client, output, "87-inn-complete-linan-slot6")
    write_json(output / "linan-inn-proof.json", dict(status="passed", stay=True, refusal=True,
               nativeTenSilverBoundary=True, nativeZeroSilverRefusal=True, saveReused=True,
               sourceSlot=0, mainRouteMoney=100, cheatAssisted=False))


def hemei_inn_choices(client, output, resource):
    state = idle(client)
    if state["map"] != "map016_临安城.map" or state["variables"].get("Event") != "180":
        raise AutomationError("Hemei inn choices require the native rescue continuation")
    client.act("SetAutoDialogue", enabled=True)
    transition(client, resource, "map020_临安城客栈一楼.map", 6, output, "440-hemei-inn-arrival")
    client.save_or_load(0)
    source = checkpoint(client, output, "441-hemei-inn-source-slot0")
    relative = "script/map/map020_临安城客栈一楼/客栈掌柜对话.txt"
    for option in (0, 1):
        load_checkpoint(client, 0)
        state = interact_at(client, output, resource, "客栈掌柜", relative,
                            f"442-hemei-inn-option-{option}", choices=[(relative + ":46", option)])
        if (state["variables"].get("Event") != "180"
                or state["player"]["money"] != source["player"]["money"]):
            raise AutomationError("Hemei inn branch unexpectedly changed money or main story")
        client.save_or_load(option + 1)
    load_checkpoint(client, 0)
    transition(client, resource, "map016_临安城.map", 1, output, "443-hemei-inn-return")
    client.save_or_load(6)
    write_json(output / "hemei-inn-choices-proof.json", dict(status="passed", Event="180", options=[0, 1],
               sameSource=True, moneyUnchanged=True, sourceSlots=[0, 1, 2, 6], cheatAssisted=False))


def tavern_inquiry_repair(client, output, resource):
    state = idle(client)
    if state["map"] != "map016_临安城.map" or state["variables"].get("Event") != "40":
        raise AutomationError("Tavern inquiry regression requires the native first Linan entry")
    client.act("SetAutoDialogue", enabled=True)
    transition(client, resource, "map017_临安城酒楼.map", 4, output, "450-tavern-repair-entry")
    relative = "script/map/map017_临安城酒楼/酒楼老板对话.txt"
    state = interact_at(client, output, resource, "酒店掌柜", relative, "451-tavern-restored-dialogue")
    if state["variables"].get("JiuLvLaoBan") != "1":
        raise AutomationError("The tavern inquiry dialogue did not run after SetNpcClickScript")
    interact_at(client, output, resource, "酒店掌柜", relative, "452-tavern-restored-repeat")
    client.save_or_load(6)
    write_json(output / "tavern-inquiry-repair-proof.json", dict(status="passed", Event="40", JiuLvLaoBan="1",
               dialogue=script_proof(output, resource, relative), movementScript=script_proof(output, resource,
               "script/map/map017_临安城酒楼/酒馆掌柜点击脚本.txt"), sourceSlot=6, cheatAssisted=False))


def linan_tavern(client, output, resource):
    state = idle(client)
    if state["map"] == "map016_临安城.map" and state["variables"].get("Event") in ("50", "60") and (output / "101-tavern-rescue-source-slot2.json").exists():
        client.act("SetAutoDialogue", enabled=True)
        return linan_tavern_clue(client, output, resource)
    if state["map"] == "map017_临安城酒楼.map" and state["variables"].get("Event") == "45" and (output / "99-tavern-fight-exit-refused-script-proof.json").exists():
        client.act("SetAutoDialogue", enabled=True)
        return linan_tavern_battle(client, output, resource)
    if state["map"] != "map016_临安城.map" or state["variables"].get("Event") != "40":
        raise AutomationError("Tavern route requires the native Linan entry slot 6")
    client.act("SetAutoDialogue", enabled=True)
    transition(client, resource, "map016_临安城.map", 1, output, "90-linan-search-exit-refused")
    script_proof(output, resource, "script/map/map016_临安城/trap01.txt")
    transition(client, resource, "map017_临安城酒楼.map", 4, output, "91-tavern-entry")
    state = client.observe(VARIABLES)
    client.move(13, 45)
    dialogue_source = "script/map/map017_临安城酒楼/酒楼老板对话.txt"
    state = interact_at(client, output, resource, "酒店掌柜", dialogue_source, "92-tavern-counter-dialogue")
    if state["variables"].get("JiuLvLaoBan") != "1":
        raise AutomationError("The tavern counter did not preserve its NPC dialogue")
    interact_at(client, output, resource, "酒店掌柜", dialogue_source, "93-tavern-counter-repeat")
    write_json(output / "tavern-inquiry-proof.json", dict(status="passed", object="酒店掌柜",
               JiuLvLaoBan="1", source=script_proof(output, resource, dialogue_source), cheatAssisted=False))
    state = interact_at(client, output, resource, "醉郎中", "script/map/map017_临安城酒楼/醉郎中.txt", "94-prescription-sidequest-start")
    if state["variables"].get("Sub") != "120" or state["variables"].get("Talkzuilangzhong") != "1":
        raise AutomationError("Native drunk physician did not open the prescription sidequest")
    interact_at(client, output, resource, "醉郎中", "script/map/map017_临安城酒楼/醉郎中.txt", "95-prescription-not-found-reminder")
    client.save_or_load(0)
    checkpoint(client, output, "96-tavern-source-slot0")
    state = transition(client, resource, "map017_临安城酒楼.map", 2, output, "97-tavern-battle-start")
    if state["variables"].get("Event") != "45":
        raise AutomationError("Native tavern stairs did not open the four-enemy fight")
    client.save_or_load(1)
    checkpoint(client, output, "98-tavern-fight-source-slot1")
    transition(client, resource, "map017_临安城酒楼.map", 1, output, "99-tavern-fight-exit-refused")
    script_proof(output, resource, "script/map/map017_临安城酒楼/trap01.txt")
    return linan_tavern_battle(client, output, resource)


def linan_tavern_battle(client, output, resource):
    load_checkpoint(client, 1)
    checkpoint(client, output, "99-tavern-fight-source-reloaded")
    state = clear_enemies(client, output, "100-tavern-battle", skills=[])
    if state["variables"].get("Event") != "50" or state["variables"].get("map017Enemy") != "4":
        raise AutomationError("Native tavern fight did not complete all four death scripts")
    proof = script_proof(output, resource, "script/map/map017_临安城酒楼/路达及其跟班死亡.txt")
    client.save_or_load(2)
    checkpoint(client, output, "101-tavern-rescue-source-slot2")
    transition(client, resource, "map016_临安城.map", 1, output, "102-tavern-return-to-linan")
    return linan_tavern_clue(client, output, resource)


def linan_tavern_clue(client, output, resource):
    state = client.observe(VARIABLES)
    if state["variables"].get("Event") == "50":
        state = interact_at(client, output, resource, "小雷", "script/map/map016_临安城/临安城居民7之小雷对话.txt", "103-xiaolei-clue")
    if state["variables"].get("Event") != "60":
        raise AutomationError("Native beggar clue did not advance Event to 60")
    interact_at(client, output, resource, "小雷", "script/map/map016_临安城/临安城居民7之小雷对话.txt", "104-xiaolei-reminder")
    client.save_or_load(6)
    checkpoint(client, output, "105-rescue-clue-slot6")
    inquiry_passed = (output / "tavern-inquiry-proof.json").is_file()
    chapter_status = "passed" if inquiry_passed else "partial"
    write_json(output / "linan-tavern-proof.json", dict(status=chapter_status, Event="60", nativeKills=4,
               searchExitRefused=True, fightExitRefused=True, prescriptionSidequest="120",
               tavernInquiry="passed" if inquiry_passed else "pending", deathScript=script_proof(output, resource, "script/map/map017_临安城酒楼/路达及其跟班死亡.txt"),
               sourceSlots=[0, 1, 2, 6], cheatAssisted=False))
    return chapter_status


def play_gamble_round(client, output, stake=80):
    prefix = f"gamble-{time.time_ns()}"
    before = checkpoint(client, output, prefix + "-before")
    client.activate("gamble-big")
    client.activate("gamble-increase")
    for _ in range(stake - 1):
        client.ui("Confirm")
    checkpoint(client, output, prefix + f"-stake{stake}")
    client.activate("gamble-primary")
    rolled = checkpoint(client, output, prefix + "-rolled")
    delta = rolled["player"]["money"] - before["player"]["money"]
    if delta not in (-stake, stake):
        raise AutomationError(f"Native {stake}-silver wager did not settle: {delta}")
    client.activate("gamble-exit")
    write_json(output / f"{prefix}-proof.json", dict(status="passed", playerActions=f"押大/下注{stake}/开盅/离开",
               moneyDelta=delta, beforeFile=str(output / f"{prefix}-before.json"),
               rolledFile=str(output / f"{prefix}-rolled.json"), cheatAssisted=False))


def linan_residents(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if state["map"] == "map020_临安城客栈一楼.map":
        state = transition(client, resource, "map016_临安城.map", 1, output, "resident-linan-city")
    if state["map"] != "map016_临安城.map" or state["variables"].get("Event") != "40":
        raise AutomationError("Resident dialogue audit requires the ordinary first Linan visit")
    proofs, unavailable = template_dialogues(client, output, resource, "map016.npc")
    client.save_or_load(6)
    write_json(output / "linan-residents-proof.json", dict(status="passed", sourceEvent=state["variables"].get("Event"),
               nativeDialogues=proofs, unavailable=unavailable, unavailableCountedAsPassed=False,
               ordinarySourceSlot=6))


def template_dialogues(client, output, resource, template_name, choices_by_name=None, sources=None):
    state = idle(client)
    source_map, source_event = state["map"], state["variables"].get("Event")
    template = configparser.ConfigParser(interpolation=None)
    template.read(resource / "ini/save" / template_name, encoding="utf-8-sig")
    expected, unavailable, proofs = [], [], []
    assigned_targets = set()
    for section in template.sections():
        row = template[section]
        if not row.get("scriptfile"):
            continue
        name = row["name"]
        source = "script/map/" + Path(source_map).stem + "/" + row["scriptfile"]
        if sources is not None and source not in sources:
            continue
        position = (int(row["mapx"]), int(row["mapy"]))
        expected.append(dict(name=name, source=source, position=position))
    while expected:
        state = idle(client)
        player = state["player"]["position"]
        row = min(expected, key=lambda v: abs(v["position"][0] - player["x"]) + abs(v["position"][1] - player["y"]))
        expected.remove(row)
        matches = [t for t in state["targets"] if t["name"] == row["name"] and t.get("interactive")
                   and t["id"] not in assigned_targets]
        if not matches:
            unavailable.append(row)
            continue
        target = min(matches, key=lambda t: abs(t["position"]["x"] - row["position"][0])
                     + abs(t["position"]["y"] - row["position"][1]))
        assigned_targets.add(target["id"])
        randoms = [(index, *match.groups()) for index, line in enumerate(
            (resource / row["source"]).read_text(encoding="utf-8-sig").splitlines(), 1)
            if (match := re.search(r'getrandnum\("([^"]+)",\s*(\d+),\s*(\d+)\)', line, re.I))]
        required, seen = {}, {}
        for repeat in range(24 if randoms else 4):
            state = interact_at(client, output, resource, row["name"], row["source"],
                                f"dialogue-{Path(source_map).stem}-{len(proofs)}-{repeat}", target_id=target["id"],
                                shop_cancel=True, choices=(choices_by_name or {}).get(row["name"], ()))
            if state["map"] != source_map or state["variables"].get("Event") != source_event:
                raise AutomationError("Resident dialogue unexpectedly advanced the main story")
            proof = script_proof(output, resource, row["source"])
            for line, name, low, high in randoms:
                if line in proof["executedLines"]:
                    required[name] = set(range(int(low), int(high) + 1))
                    seen.setdefault(name, set())
            for event in records(output):
                if event.get("executionId") == proof["executionId"] and event.get("eventType") == "variable.change":
                    if event.get("variableName") in seen:
                        seen[event["variableName"]].add(int(event["afterValue"]))
            proofs.append(dict(name=row["name"], repeat=repeat, execution=proof))
            if repeat >= 3 and all(required[name] <= values for name, values in seen.items()):
                break
        else:
            raise AutomationError(f"Native random dialogue outcomes remain unobserved: {row['source']} / {seen}")
    return proofs, unavailable


def remaining_map_traps(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    initial = checkpoint(client, output, "remaining-trap-before")
    if initial["map"] == "map012_武夷山大厅.map" and initial["variables"].get("Event") == "40":
        for destination, trap in (("map011_武夷山顶.map", 1), ("map007_武夷山九猴洞.map", 1),
                                  ("map006_武夷山脚.map", 1), ("map005_林间小道.map", 1)):
            transition(client, resource, destination, trap, output, "remaining-trap-descent-" + Path(destination).stem)
        initial = checkpoint(client, output, "remaining-trap-before")
    if initial["map"] == "map015_临安城南.map" and initial["variables"].get("Event") in ("40", "210"):
        transition(client, resource, "map005_林间小道.map", 2, output, "remaining-trap-forest-entry")
        initial = checkpoint(client, output, "remaining-trap-before")
    number = initial["map"].split("_")[0]
    routes = {
        "map004": (1, "map001_衡山.map"), "map005": (1, "map004_衡山脚下.map"),
        "map020": (3, "map021_临安城客栈二楼.map"), "map021": (2, "map020_临安城客栈一楼.map"),
        "map034": (3, "map034_海边小树林.map"), "map048": (3, "map062_华山栈道3.map"),
        "map056": (1, "map049_长安城.map"), "map070": (1, "map064_少室山下.map"),
        "map084": (1, "map082_金兵大营.map"), "map113": (1, "map112_五剑堂.map"),
        "map119": (1, "map118_泰山.map")}
    if number not in routes:
        raise AutomationError("No reviewed remaining trap route for this ordinary map")
    trap, destination = routes[number]
    event = initial["variables"].get("Event")
    if (number == "map004" and event in ("25", "30")
            or number == "map005" and event in ("40", "210", "240", "250")
            or number == "map034" and int(event or 0) < 270
            or number == "map048" and event == "360"
            or number == "map084" and event == "470"
            or number == "map113" and initial["variables"].get("Clue") in ("110", "120")):
        destination = initial["map"]
    before = records(output)[-1]["sequence"]
    transition(client, resource, destination, trap, output, "remaining-trap-after")
    bindings = configparser.ConfigParser(interpolation=None)
    bindings.read(output / "user-data/save/xjxqy/game/traps.ini", encoding="utf-8-sig")
    source = "script/map/" + Path(initial["map"]).stem + "/" + bindings[Path(initial["map"]).stem][str(trap)]
    proof = script_proof(output, resource, source, before)
    configured = checkpoint(client, output, "remaining-trap-before-save")
    client.save_or_load(6)
    restored = load_checkpoint(client, 6)
    if (restored["map"] != configured["map"] or restored["variables"] != configured["variables"]
            or restored["player"]["money"] != configured["player"]["money"]
            or observed_character_configuration(restored) != observed_character_configuration(configured)):
        raise AutomationError("Return trap state did not survive normal save/load")
    write_json(output / "remaining-map-traps-proof.json", dict(status="passed", execution=proof,
               trap=trap, fromMap=initial["map"], toMap=configured["map"], nativeMovementOnly=True,
               normalSaveReload=True, sourceSlot=6))


def zhaoqi_matchmaking(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    initial = checkpoint(client, output, "matchmaking-before")
    city, house = "map016_临安城.map", "map032_赵七娘家.map"
    if (initial["map"] != city or initial["variables"].get("Eventzhaoqi") not in ("", "0")
            or initial["variables"].get("Event") != "40"):
        raise AutomationError("Matchmaking requires the ordinary first Linan arrival before this side quest")
    client.save_or_load(0)
    for index in range(2):
        interact_at(client, output, resource, "周木匠", "script/map/map016_临安城/周木匠对话.txt", f"matchmaking-carpenter-introduction-{index}")
    transition(client, resource, house, 13, output, "matchmaking-first-house")
    state = transition(client, resource, house, 2, output, "matchmaking-first-conversation")
    if state["variables"].get("Eventzhaoqi") != "1":
        raise AutomationError("Zhao did not establish her ordinary matchmaking request")
    client.save_or_load(1)
    interact_at(client, output, resource, "赵七娘", "script/map/map032_赵七娘家/赵七娘对话.txt", "matchmaking-request-repeat")
    before = checkpoint(client, output, "matchmaking-chest-before")
    chest_source = "script/map/map032_赵七娘家/宝箱.txt"
    first = interact_at(client, output, resource, "宝箱", chest_source, "matchmaking-chest-first", (14, 30))
    if (first["variables"].get("Openboxzhao") != "1"
            or observed_character_configuration(first) != observed_character_configuration(before)
            or first["player"]["money"] != before["player"]["money"]):
        raise AutomationError("First chest search did not remain empty")
    client.save_or_load(2)
    load_checkpoint(client, 2)
    second = interact_at(client, output, resource, "宝箱", chest_source, "matchmaking-chest-second", (14, 30))
    if (quantity(second, "goods079_游龙披风.ini") != quantity(before, "goods079_游龙披风.ini") + 1
            or second["player"]["money"] != before["player"]["money"] + 50):
        raise AutomationError("Second chest search lost its ordinary saved reward")
    chest = next(row for row in second["targets"] if row["name"] == "宝箱")
    last = records(output)[-1]["sequence"]
    action = client.submit("Interact", generation=second["generation"], targetId=chest["id"], running=True)
    rejected = client.request("GetActionStatus", actionId=action)
    if (rejected["status"] != "failed" or rejected["reason"] != "action_rejected"
            or any(row.get("eventType") == "script.start" and row["sequence"] > last for row in records(output))):
        raise AutomationError("Zhao's collected chest repeated its script")
    transition(client, resource, city, 1, output, "matchmaking-carpenter-return")
    state = interact_at(client, output, resource, "周木匠", "script/map/map016_临安城/周木匠对话.txt", "matchmaking-carpenter-accepted")
    if state["variables"].get("Eventzhaoqi") != "2" or any(row["name"] == "周木匠" for row in state["targets"]):
        raise AutomationError("The carpenter did not depart normally for Zhao's house")
    client.save_or_load(3)
    load_checkpoint(client, 3)
    transition(client, resource, house, 13, output, "matchmaking-couple-house")
    before_couple = checkpoint(client, output, "matchmaking-couple-before")
    state = transition(client, resource, house, 3, output, "matchmaking-couple-reward")
    if (state["variables"].get("Eventzhaoqi") != "3"
            or quantity(state, "goods207_十年佳酿.ini") != quantity(before_couple, "goods207_十年佳酿.ini") + 1):
        raise AutomationError("The native couple did not complete their side quest reward")
    for name, filename in (("赵七娘", "赵七娘对话.txt"), ("周木匠", "周木匠对话.txt")):
        interact_at(client, output, resource, name, "script/map/map032_赵七娘家/" + filename, "matchmaking-complete-" + Path(filename).stem)
    configured = checkpoint(client, output, "matchmaking-before-save")
    client.save_or_load(6)
    restored = load_checkpoint(client, 6)
    if (restored["variables"] != configured["variables"]
            or observed_character_configuration(restored) != observed_character_configuration(configured)
            or restored["player"]["money"] != configured["player"]["money"]
            or restored["variables"].get("Event") != initial["variables"].get("Event")):
        raise AutomationError("The matchmaking reward or main story changed after ordinary reload")
    write_json(output / "zhaoqi-matchmaking-proof.json", dict(status="passed", stages=[0, 1, 2, 3],
               nativePlayerInteractions=True, normalSaveReload=True, chestFirstSearchEmpty=True,
               chestSecondSearchSaved=True, capeQuantity=1, chestMoney=50, coupleWineQuantity=1,
               chestRepeat=rejected, mainEventUnchanged=True, sourceSlots=[0, 1, 2, 3, 6]))


def native_loss_prompt(client, output, resource):
    initial = checkpoint(client, output, "native-loss-before")
    routes = {
        "map118_泰山.map": ("罗天强", "比武失败.txt", "读取进度再打一次吧", "player"),
        "map084_金兵主帅营.map": ("南宫灭", "南宫灭死亡.txt", "你这个骗子", "npc")}
    if initial.get("map") not in routes or not initial.get("worldInput"):
        raise AutomationError("Native loss prompt needs a reviewed ordinary battle source")
    enemy_name, filename, marker, actor = routes[initial["map"]]
    enemy = next(row for row in initial["targets"] if row["name"] == enemy_name)
    if not enemy.get("hostile") or not enemy.get("attackable") or enemy["life"] <= 0:
        raise AutomationError("Native loss prompt needs its live hostile battle target")
    if actor == "player" and initial["player"]["life"] != 1 or actor == "npc" and enemy["life"] != 1:
        raise AutomationError("Native loss prompt requires the recorded positive-life numeric assistance")
    source = "script/map/" + Path(initial["map"]).stem + "/" + filename
    client.act("SetAutoDialogue", enabled=False)
    client.save_or_load(0)
    sequence = records(output)[-1]["sequence"]
    if actor == "player" and client.observe().get("cheatInvincibilityEnabled"):
        client.open_menu("System")
        client.activate("options")
        client.activate("cheat-settings")
        client.activate("invincibility")
        while not client.observe().get("worldInput"):
            client.ui("Cancel")
    state = client.observe(VARIABLES)
    if actor == "player" and state.get("cheatInvincibilityEnabled"):
        raise AutomationError("Native player loss still has invincibility enabled")
    action = client.submit("StartCombat", generation=state["generation"], targetId=enemy["id"],
                           radius=64, kills=1, skills=[], timeoutMs=60000)
    deadline = time.monotonic() + 65
    while time.monotonic() < deadline:
        state = client.observe(VARIABLES)
        status = client.request("GetActionStatus", actionId=action)
        if status["status"] in ("failed", "cancelled") and status["reason"] not in ("player_dead", "world_changed"):
            raise AutomationError(f"Native loss combat: {status}")
        if state.get("script") == source or title_visible(state):
            break
        time.sleep(0.1)
    else:
        raise TimeoutError("Normal combat did not start the reviewed native death callback")
    idle(client, output=output, resource=resource, expected_terminal=source, final_dialogue=marker)
    proof = script_proof(output, resource, source, sequence)
    dialogues = [path for path in output.glob("ending-dialogue-*.json")
                 if marker in json.loads(path.read_text(encoding="utf-8")).get("dialogue", {}).get("text", "")]
    title = checkpoint(client, output, "native-loss-title")
    if not dialogues or not title_visible(title):
        raise AutomationError("Native loss prompt lacks its full original dialogue and title")
    write_json(output / "native-loss-prompt-proof.json", dict(status="passed", script=proof, defeatedActor=actor,
               targetName=enemy_name, combatAction=action, combatStatus=status, sourceSlot=0,
               initialPlayerLife=initial["player"]["life"], initialEnemyLife=enemy["life"],
               finalDialogueFile=str(dialogues[-1]), titleFile=str(output / "native-loss-title.json"),
               nativeDeathCallback=True, storyEnding=False, cheatAssisted=True))


def saved_npc_dialogues(client, output, resource, partners=False, sources=None):
    client.act("SetAutoDialogue", enabled=True)
    initial = idle(client)
    client.save_or_load(0)
    game = output / "user-data/save/xjxqy/game"
    saved = configparser.ConfigParser(interpolation=None)
    saved.read(game / "game.ini", encoding="utf-8-sig")
    npc_file = game / ("partner0.ini" if partners else saved.get("state", "npc"))
    pending = sources if sources is not None else {row["path"] for row in json.loads(Path("tmp/xjxqy-coverage-47/unexecuted-scripts.json").read_text(encoding="utf-8"))["scripts"]}
    proofs, unavailable = template_dialogues(client, output, resource, str(npc_file), sources=pending)
    if not proofs and not unavailable:
        raise AutomationError("This normal saved map has no pending bound NPC dialogue")
    client.save_or_load(6)
    configured = checkpoint(client, output, "saved-npc-dialogues-before-load")
    restored = load_checkpoint(client, 6)
    if (restored["variables"].get("Event") != initial["variables"].get("Event")
            or restored["player"]["money"] != configured["player"]["money"]
            or observed_character_configuration(restored) != observed_character_configuration(configured)):
        raise AutomationError("Pending NPC dialogue state did not survive normal save/load")
    write_json(output / ("saved-partner-dialogues-proof.json" if partners else "saved-npc-dialogues-proof.json"), dict(status="passed-for-presented-targets", map=initial["map"],
               nativeNPCDialogues=proofs, unavailableAtCurrentStage=unavailable, normalSaveReload=True,
               **{"sourcePartnerFile" if partners else "sourceNPCFile": npc_file.name}, sourceSlots=[0, 6], cheatAssisted=True))


def partner_dialogue_tour(client, output, resource, recheck=False):
    client.act("SetAutoDialogue", enabled=True)
    initial = idle(client)
    source_map, event = initial["map"], initial["variables"].get("Event")
    pending = ({row["path"] for row in catalog_for(resource)["sources"]} if recheck else
               {row["path"] for row in json.loads(Path("tmp/xjxqy-coverage-47/unexecuted-scripts.json").read_text(encoding="utf-8"))["scripts"]})
    rooms = []

    def dialogue():
        state = idle(client)
        client.save_or_load(0)
        party = configparser.ConfigParser(interpolation=None)
        party.read(output / "user-data/save/xjxqy/game/partner0.ini", encoding="utf-8-sig")
        if not any("script/map/" + Path(state["map"]).stem + "/" + party.get(section, "scriptfile", fallback="") in pending
                   for section in party.sections()):
            return
        saved_npc_dialogues(client, output, resource, partners=True, sources=pending)
        proof = json.loads((output / "saved-partner-dialogues-proof.json").read_text(encoding="utf-8"))
        rooms.append(proof)
        write_json(output / ("partner-tour-" + Path(state["map"]).stem + "-proof.json"), proof)

    def move(destination, trap):
        transition(client, resource, destination, trap, output, "partner-tour-entry-" + Path(destination).stem)
        if idle(client)["variables"].get("Event") != event:
            raise AutomationError("Partner dialogue tour unexpectedly advanced the main story")

    def room(destination, trap):
        move(destination, trap)
        dialogue()
        if destination in ("map020_临安城客栈一楼.map", "map055_长安城客栈一楼.map"):
            upstairs = "map021_临安城客栈二楼.map" if destination.startswith("map020_") else "map055_1_长安客栈二楼.map"
            move(upstairs, 2)
            dialogue()
            move(destination, 1)
        move(source_map, 1)

    if source_map == "map016_临安城.map" and event in ("120", "180"):
        for destination, trap in (("map017_临安城酒楼.map", 4), ("map020_临安城客栈一楼.map", 6),
                ("map022_临安城杂货店.map", 10), ("map025_临安城铁匠铺.map", 8),
                ("map027_临安城赌坊.map", 7), ("map028_临安城药店.map", 9),
                ("map032_1_临安城民居.map", 14), ("map032_赵七娘家.map", 13)):
            room(destination, trap)
        if event == "120":
            room("map023_临安城妓院.map", 5)
            room("map026_临安城何员外家.map", 12)
        move("map035_临安城东.map", 2)
        dialogue()
        move("map034_1_海边小渔村.map", 2)
        dialogue()
        move("map036_渔夫家.map", 2)
        dialogue()
    elif source_map == "map049_长安城.map" and event == "300":
        room("map055_长安城客栈一楼.map", 3)
        room("map057_长安城铁匠铺.map", 7)
    elif source_map == "map051_长安妓院一楼.map" and event == "307":
        move("map049_长安城.map", 1)
        source_map = "map049_长安城.map"
        dialogue()
        for destination, trap in (("map055_长安城客栈一楼.map", 3),
                                 ("map056_长安城赌坊.map", 5),
                                 ("map057_长安城铁匠铺.map", 7),
                                 ("map058_长安城杂货店.map", 6)):
            room(destination, trap)
        move("map050_长安城酒店一楼.map", 2)
        dialogue()
        move("map050_1_长安酒店二楼.map", 2)
        dialogue()
        move("map050_长安城酒店一楼.map", 1)
        move(source_map, 1)
        move(source_map, 10)
        room("map052_长安城衙门.map", 8)
    elif source_map == "map044_石塘镇.map" and event == "320":
        room("map046_石塘镇酒楼.map", 3)
    elif source_map == "map065_少林寺.map" and event == "370":
        room("map066_大雄宝殿.map", 2)
    elif source_map == "map100_天王岛.map" and event == "610":
        room("map101_天王帮大殿.map", 2)
    elif source_map == "map121_洞庭湖底.map" and event == "610":
        move("map099_洞庭湖畔.map", 1)
        dialogue()
    elif source_map == "map097_湖口村茶馆.map" and event == "550":
        move("map096_湖口村.map", 1)
        dialogue()
    elif source_map == "map106_成都客栈一楼.map" and event == "620" and initial["variables"].get("Clue") == "10":
        dialogue()
        move("map106_1成都客栈二楼.map", 2)
        dialogue()
        move(source_map, 1)
        if idle(client)["variables"].get("Clue") != "10":
            raise AutomationError("The short Clue10 inn tour unexpectedly advanced the clue")
    elif source_map == "map014_武夷山禁地外洞.map" and event == "240":
        move("map011_武夷山顶.map", 1)
        move("map007_武夷山九猴洞.map", 1)
        dialogue()
        move("map006_武夷山脚.map", 1)
        dialogue()
    else:
        raise AutomationError("No reviewed ordinary source stage for the partner dialogue tour")
    if not rooms:
        raise AutomationError("Partner tour reached no pending dialogue")
    client.save_or_load(6)
    write_json(output / "partner-dialogue-tour-proof.json", dict(status="passed-for-presented-targets", initialMap=initial["map"],
               rooms=rooms, nativeMovementOnly=True, normalSaveReload=True, sourceSlot=6, cheatAssisted=True))


def interior_dialogues(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    initial_map = state["map"]
    holiday = bool(json.loads((output / "run.json").read_text(encoding="utf-8")).get("calendarAssistance"))
    proofs, unavailable = [], []

    def room(template_name):
        current_map = idle(client)["map"]
        choices = {}
        for name, filename, option in (("客栈掌柜", "客栈掌柜对话.txt", 1),
                                       ("铁匠铺学徒", "铁匠学徒对话.txt", 0),
                                       ("妓女3", "妓女秋雨对话.txt", 1),
                                       ("世景良", "世景良.txt", 1)):
            source = "script/map/" + Path(current_map).stem + "/" + filename
            sites = [row for row in catalog_for(resource)["choices"] if row["path"] == source]
            if sites:
                site = next((row for row in sites if holiday and row["label"] == "YuanDan"), min(sites, key=lambda v: v["line"]))
                if filename == "铁匠学徒对话.txt" and holiday:
                    site = next(row for row in sites if row["label"] == "Bixue0YD")
                choices[name] = [(site["id"], option)]
        completed, missing = template_dialogues(client, output, resource, template_name, choices)
        proofs.extend(completed)
        unavailable.extend(missing)

    if state["map"] == "map020_临安城客栈一楼.map" and state["variables"].get("Event") == "40":
        room("map020.npc")
        transition(client, resource, "map021_临安城客栈二楼.map", 2, output, "interior-linan-inn-up")
        room("map021.npc")
        transition(client, resource, "map020_临安城客栈一楼.map", 1, output, "interior-linan-inn-down")
        transition(client, resource, "map016_临安城.map", 1, output, "interior-linan-city")
        for destination, trap, template in (("map023_临安城妓院.map", 5, "map023.npc"),
                ("map026_临安城何员外家.map", 12, "map026.npc"),
                ("map032_1_临安城民居.map", 14, "map032_1.npc")):
            transition(client, resource, destination, trap, output, "interior-entry-" + Path(destination).stem)
            room(template)
            transition(client, resource, "map016_临安城.map", 1, output, "interior-return-" + Path(destination).stem)
    elif state["map"] == "map049_长安城.map" and state["variables"].get("Event") == "300":
        transition(client, resource, "map055_长安城客栈一楼.map", 3, output, "interior-changan-inn")
        room("map055.npc")
        transition(client, resource, "map055_1_长安客栈二楼.map", 2, output, "interior-changan-inn-up")
        room("map055_1.npc")
        transition(client, resource, "map055_长安城客栈一楼.map", 1, output, "interior-changan-inn-down")
        transition(client, resource, "map049_长安城.map", 1, output, "interior-changan-inn-return")
        for destination, trap, template in (("map057_长安城铁匠铺.map", 7, "map057.npc"),
                                            ("map058_长安城杂货店.map", 6, "map058.npc")):
            transition(client, resource, destination, trap, output, "interior-entry-" + Path(destination).stem)
            room(template)
            transition(client, resource, "map049_长安城.map", 1, output, "interior-return-" + Path(destination).stem)
    else:
        raise AutomationError("Room dialogue audit requires a normally reached first Linan or Changan source")
    client.save_or_load(6)
    write_json(output / "interior-dialogues-proof.json", dict(status="passed", initialMap=initial_map,
               nativeDialogues=proofs, unavailable=unavailable, unavailableCountedAsPassed=False, sourceSlot=6))


def saved_character_configuration(folder, character):
    expected = {}
    for kind, fields in (("goods", ("number",)), ("magic", ("level", "exp"))):
        data = configparser.ConfigParser(interpolation=None)
        data.read(folder / f"{kind}{character}.ini", encoding="utf-8-sig")
        expected[kind] = sorted([dict(slot=int(section) - 1, file=data[section]["inifile"],
                                     **{field: data.getint(section, field, fallback=0) for field in fields})
                                for section in data.sections() if section.isdigit() and data.get(section, "inifile", fallback="")],
                               key=lambda row: row["slot"])
    return expected


def character_configuration(client, output, resource):
    state = idle(client)
    game = output / "user-data/save/xjxqy/game"
    saved = configparser.ConfigParser(interpolation=None)
    saved.read(game / "game.ini", encoding="utf-8-sig")
    character = saved.getint("state", "chr")
    if character not in (0, 1, 2):
        raise AutomationError("Ordinary configuration menus require a normally controllable protagonist")
    original_other_roles = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                            for p in game.glob("*.ini")
                            if p.name in {f"{kind}{index}.ini" for index in range(4) if index != character
                                          for kind in ("player", "magic", "goods")}}
    expected = saved_character_configuration(game, character)
    inventory = lambda v: [dict(slot=row["slot"], file=row["file"], number=row["quantity"]) for row in v["inventory"]]
    magic = lambda v: [{field: row[field] for field in ("slot", "file", "level", "exp")} for row in v["magic"]]
    if inventory(state) != expected["goods"] or magic(state) != expected["magic"]:
        raise AutomationError("The active protagonist did not load the selected save's own goods and magic")
    initial = checkpoint(client, output, "character-config-initial")
    client.open_menu("Equip")
    checkpoint(client, output, "character-config-equipment-menu")
    client.ui("Cancel")
    equipped = []
    for row in initial["inventory"]:
        if row["slot"] >= initial["layout"]["equipmentBegin"]:
            continue
        item = configparser.ConfigParser(interpolation=None)
        item.read(resource / "ini/goods" / row["file"], encoding="utf-8-sig")
        if item.getint("Init", "Kind", fallback=0) == 1:
            client.equip(row["slot"])
            current = idle(client)
            if not any(value["file"] == row["file"] and initial["layout"]["equipmentBegin"] <= value["slot"]
                       < initial["layout"]["goodsQuickBegin"] for value in current["inventory"]):
                raise AutomationError("The protagonist could not equip an item from the native starting configuration")
            equipped.append(row["file"])
    for index, row in enumerate(initial["magic"][:5]):
        current = idle(client)
        slot = next(value["slot"] for value in current["magic"] if value["file"] == row["file"])
        client.assign_magic(slot, index)
    configured = checkpoint(client, output, "character-config-before-save")
    if any(not any(value["file"] == row["file"] and value["slot"] == configured["layout"]["magicQuickBegin"] + index
                   for value in configured["magic"]) for index, row in enumerate(initial["magic"][:5])):
        raise AutomationError("The selected protagonist magic did not reach its intended shortcut slot")
    client.save_or_load(5)
    restored = load_checkpoint(client, 5)
    if inventory(restored) != inventory(configured) or magic(restored) != magic(configured):
        raise AutomationError("Normal save/load changed protagonist equipment or magic shortcut slots")
    if {name: hashlib.sha256((game / name).read_bytes()).hexdigest() for name in original_other_roles} != original_other_roles:
        raise AutomationError("Changing this protagonist's configuration altered another protagonist's stored configuration")
    checkpoint(client, output, "character-config-restored")
    client.save_or_load(6)
    write_json(output / "character-configuration-proof.json", dict(status="passed", characterIndex=character,
               initialConfiguration=expected, equippedThroughMenu=equipped, assignedMagic=len(initial["magic"][:5]),
               normalSaveReload=True, otherProtagonistsUnchanged=True, sourceSlot=6))


def character_yang_ending(client, output, resource):
    game = output / "user-data/save/xjxqy/game"
    expected = saved_character_configuration(game, 3)
    hashes = {name: hashlib.sha256((game / name).read_bytes()).hexdigest()
              for name in ("player3.ini", "magic3.ini", "goods3.ini")}
    end3_finish(client, output, resource)
    snapshot = next(path for path in output.glob("ending-dialogue-*.json")
                    if json.loads(path.read_text(encoding="utf-8")).get("map") == "map102_后花园和小树林.map")
    state = json.loads(snapshot.read_text(encoding="utf-8"))
    inventory = [dict(slot=row["slot"], file=row["file"], number=row["quantity"]) for row in state["inventory"]]
    magic = [{field: row[field] for field in ("slot", "file", "level", "exp")} for row in state["magic"]]
    execution = script_proof(output, resource, "script/map/map120_风波亭/秦桧死亡.txt")
    if inventory != expected["goods"] or magic != expected["magic"] or 90 not in execution["executedLines"]:
        raise AutomationError("The native Yang Ying cutscene did not load her own equipment and magic")
    write_json(output / "character-yang-configuration-proof.json", dict(status="passed", characterIndex=3,
               initialConfiguration=expected, sourceHashes=hashes, execution=execution,
               snapshotFile=str(snapshot),
               ordinaryWorldInput=state["worldInput"], configurationObservedDuringNativeEnding=True,
               battleAndMenuTestsAvailable=False, endingFullyCompleted=True))


def observed_character_configuration(state):
    return dict(goods=[dict(slot=row["slot"], file=row["file"], number=row["quantity"])
                       for row in state["inventory"]],
                magic=[{field: row[field] for field in ("slot", "file", "level", "exp")}
                       for row in state["magic"]])


def character_combat_configuration(client, output, resource):
    game = output / "user-data/save/xjxqy/game"
    saved = configparser.ConfigParser(interpolation=None)
    saved.read(game / "game.ini", encoding="utf-8-sig")
    character = saved.getint("state", "chr")
    initial = idle(client)
    if character not in (0, 1, 2) or observed_character_configuration(initial) != saved_character_configuration(game, character):
        raise AutomationError("Combat configuration requires the protagonist's own ordinary saved configuration")
    client.act("SetAutoDialogue", enabled=True)
    assist(client, output, invincible=True)
    if character == 2 and initial["variables"].get("Event") == "304":
        for destination, trap in (("map050_长安城酒店一楼.map", 1), ("map049_长安城.map", 1),
                                  ("map047_长安城东.map", 1), ("map047_长安城东.map", 3),
                                  ("map049_长安城.map", 2), ("map051_长安妓院一楼.map", 4)):
            transition(client, resource, destination, trap, output, "character-battle-entry-" + Path(destination).stem)
    elif character == 2 and initial["variables"].get("Event") == "690":
        transition(client, resource, "map116_剑门关.map", 1, output, "character-native-jianmen-start")
    prepared = checkpoint(client, output, "character-combat-source")
    if not any(row.get("hostile") and row.get("attackable") for row in prepared["targets"]):
        raise AutomationError("Combat configuration needs an ordinary hostile encounter")
    if prepared["player"]["level"] != initial["player"]["level"]:
        raise AutomationError("Configuration assistance unexpectedly changed the protagonist's level")
    other_roles = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in game.glob("*.ini")
                   if p.name in {f"{kind}{index}.ini" for index in range(4) if index != character
                                 for kind in ("player", "magic", "goods")}}
    client.save_or_load(0)
    casts = []
    for index, learned in enumerate(prepared["magic"]):
        state = load_checkpoint(client, 0)
        slot = next(row["slot"] for row in state["magic"] if row["file"] == learned["file"])
        client.assign_magic(slot, index % 5)
        before = checkpoint(client, output, f"character-cast-{index}-before")
        target = min((row for row in before["targets"] if row.get("hostile") and row.get("attackable")),
                     key=lambda row: (row["position"]["x"] - before["player"]["position"]["x"]) ** 2
                     + ((row["position"]["y"] - before["player"]["position"]["y"]) / 2) ** 2)
        action = client.act("CastSkill", timeout=25, generation=before["generation"], targetId=target["id"],
                            slot=index % 5, timeoutMs=20000)
        after = checkpoint(client, output, f"character-cast-{index}-after")
        if action["status"] != "succeeded" or action["reason"] != "action_finished" or after["map"] != prepared["map"]:
            raise AutomationError("A configured protagonist skill did not execute in its ordinary encounter")
        casts.append(dict(file=learned["file"], level=learned["level"], quickSlot=index % 5, action=action,
                          target=target, beforeMana=before["player"]["mana"], afterMana=after["player"]["mana"]))
        print(f"Character {character}: cast {index + 1}/{len(prepared['magic'])}", flush=True)
    load_checkpoint(client, 0)
    before = checkpoint(client, output, "character-battle-equipment-before")
    equipment = [row for row in before["inventory"]
                 if before["layout"]["equipmentBegin"] <= row["slot"] < before["layout"]["goodsQuickBegin"]]
    for index, row in enumerate(equipment):
        client.open_menu("Equip")
        client.focus_slot("equipment-item-", row["slot"])
        client.ui("Confirm")
        client.ui("Cancel")
        state = checkpoint(client, output, f"character-equipment-{index}-removed")
        moved = next(value for value in state["inventory"] if value["file"] == row["file"])
        if moved["slot"] >= before["layout"]["equipmentBegin"] or moved["quantity"] != row["quantity"]:
            raise AutomationError("Ordinary equipment removal did not return the item to the protagonist's bag")
        client.equip(moved["slot"])
        restored = checkpoint(client, output, f"character-equipment-{index}-restored")
        if observed_character_configuration(restored) != observed_character_configuration(before):
            raise AutomationError("Ordinary re-equipping changed the protagonist's saved configuration")
    # Keep a distinct quick-bar order for the later native PlayerChange check.
    for index, learned in enumerate(prepared["magic"]):
        state = idle(client)
        slot = next(row["slot"] for row in state["magic"] if row["file"] == learned["file"])
        client.assign_magic(slot, (index + 1) % 5)
    configured = checkpoint(client, output, "character-combat-configured")
    client.save_or_load(5)
    restored = load_checkpoint(client, 5)
    if observed_character_configuration(restored) != observed_character_configuration(configured):
        raise AutomationError("Combat save/load changed protagonist equipment or learned magic")
    if {name: hashlib.sha256((game / name).read_bytes()).hexdigest() for name in other_roles} != other_roles:
        raise AutomationError("Combat configuration changed another protagonist's stored configuration")
    client.save_or_load(6)
    write_json(output / "character-combat-configuration-proof.json", dict(status="passed", characterIndex=character,
               nativeLevel=prepared["player"]["level"], casts=casts, equipmentRemovedAndRestored=equipment,
               sourceMap=prepared["map"], sourceEvent=prepared["variables"].get("Event"),
               normalSaveReload=True, otherProtagonistsUnchanged=True, sourceSlot=6,
               assistance="Native invincibility; ordinary mana costs and damage balance not assessed", cheatAssisted=True))


def character_configuration_return(client, output, resource):
    game = output / "user-data/save/xjxqy/game"
    saved = configparser.ConfigParser(interpolation=None)
    saved.read(game / "game.ini", encoding="utf-8-sig")
    character = saved.getint("state", "chr")
    if character not in (1, 2):
        raise AutomationError("Configuration return requires an ordinary Linxin or Rumeng checkpoint")
    incoming = saved_character_configuration(game, 0)
    client.act("SetAutoDialogue", enabled=True)
    # Cheat settings belong to the process, so a cloned save needs fresh native admission.
    assist(client, output, invincible=True)
    state = idle(client)
    npc_proof = None
    if character == 1 and state["map"] == "map086_天山.map" and state["variables"].get("Event") == "480":
        assist(client, output, level=60, invincible=True)
        clear_enemies(client, output, "character-return-snow-leopards", skills=())
        transition(client, resource, "map087_天山山洞.map", 2, output, "character-return-snow-cave")
        source = "script/map/map087_天山山洞/trap02.txt"
        sites = sorted((row for row in catalog_for(resource)["choices"] if row["path"] == source), key=lambda row: row["line"])
        if len(sites) != 4:
            raise AutomationError("The native snow grandma questions changed")
        transition(client, resource, "map087_天山山洞.map", 2, output, "character-return-snow-questions",
                   choices=[(site["id"], option) for site, option in zip(sites, (1, 0, 1, 0))])
        clear_enemies(client, output, "character-return-snow-grandma", skills=())
        transition(client, resource, "map086_天山.map", 1, output, "character-return-snow-medicine")
        walk_required_battle(client, output, resource, None, "character-return-snow-road", progress_variable=None)
        before = checkpoint(client, output, "character-switch-before")
        outgoing = observed_character_configuration(before)
        outgoing["goods"] = [row for row in outgoing["goods"] if row["file"] != "goods215_雪莲王.ini"]
        source = "script/map/map086_天山/trap01.txt"
        transition(client, resource, "map085_朱仙镇客栈.map", 1, output, "character-return-native-linxin")
        expected_event, expected_line = "530", 99
    elif character == 2 and state["map"] == "map051_长安妓院一楼.map" and state["variables"].get("Event") == "306":
        before = checkpoint(client, output, "character-switch-before")
        outgoing = observed_character_configuration(before)
        source = "script/map/map051_长安妓院一楼/姚公子及跟班死亡.txt"
        walk_required_battle(client, output, resource, "307", "character-return-yao", progress_variable="Event")
        expected_event, expected_line = "307", 72
    elif character == 2 and state["map"] == "map116_剑门关.map" and state["variables"].get("Event") == "695":
        npc = configparser.ConfigParser(interpolation=None)
        npc.read(game / "map116.npc", encoding="utf-8-sig")
        actor = next(npc[section] for section in npc.sections() if npc.get(section, "name", fallback="") == "南宫彩虹")
        expected = dict(level="37", attacklevel="3", flyini="magic023_雨后彩虹.ini", lifemax="1500",
                        attack="1800", defend="910", evade="176", npcini="npcres005_南宫彩虹.ini")
        if any(actor.get(key) != value for key, value in expected.items()):
            raise AutomationError("Caihong's native duel configuration differs from the formal NPC source")
        client.wait_until(lambda value: any(row["name"] == "南宫彩虹" and row["action"] in (5, 6, 7, 8)
                                            for row in value["targets"]), timeout=12, description="native Caihong attack")
        checkpoint(client, output, "character-caihong-native-attack")
        npc_proof = dict(configuration=expected, ordinaryAIActionObserved=True,
                         snapshot="character-caihong-native-attack.json", source="ini/save/map116.npc",
                         sourceSha256=hashlib.sha256((resource / "ini/save/map116.npc").read_bytes()).hexdigest(),
                         playerMenusAvailable=False, equipmentDefinedByNPCStats=True)
        before = checkpoint(client, output, "character-switch-before")
        outgoing = observed_character_configuration(before)
        source = "script/map/map116_剑门关/剑门关之战.txt"
        client.wait_until(lambda value: value.get("script") == source or value.get("map") != "map116_剑门关.map",
                          timeout=70, description="ordinary duel timeout and protagonist return")
        idle(client, timeout=240)
        expected_event, expected_line = "695", 69
    else:
        raise AutomationError("Unsupported ordinary character-return checkpoint")
    after = checkpoint(client, output, "character-switch-after")
    execution = script_proof(output, resource, source)
    if (expected_line not in execution["executedLines"] or after["variables"].get("Event") != expected_event
            or observed_character_configuration(after) != incoming):
        raise AutomationError("Native PlayerChange did not restore Dugu's own equipment and magic configuration")
    stored_outgoing = saved_character_configuration(game, character)
    # Native kills may add magic experience; equipment and slot/skill/level ownership must remain exact.
    magic_slots = lambda rows: [{field: row[field] for field in ("slot", "file", "level")} for row in rows]
    if stored_outgoing["goods"] != outgoing["goods"] or magic_slots(stored_outgoing["magic"]) != magic_slots(outgoing["magic"]):
        raise AutomationError("Native PlayerChange did not preserve the departing protagonist's configuration")
    client.save_or_load(6)
    saved.read(output / "user-data/save/xjxqy/rpg7/game.ini", encoding="utf-8-sig")
    if saved.getint("state", "chr") != 0:
        raise AutomationError("The returned ordinary save does not select Dugu")
    restored = load_checkpoint(client, 6)
    if observed_character_configuration(restored) != incoming:
        raise AutomationError("The returned protagonist configuration did not survive normal save/load")
    write_json(output / "character-configuration-return-proof.json", dict(status="passed", previousCharacter=character,
               currentCharacter=0, incomingConfiguration=incoming, storedOutgoing=stored_outgoing,
               nativePlayerChange=execution, ownEquipmentAndQuickBarRestored=True,
               departingConfigurationPreserved=True, normalSaveReload=True, npcCombatConfiguration=npc_proof,
               sourceSlot=6, cheatAssisted=True))


def character_equipment_limits(client, output, resource):
    game = output / "user-data/save/xjxqy/game"
    data = configparser.ConfigParser(interpolation=None)
    data.read(game / "game.ini", encoding="utf-8-sig")
    character = data.getint("state", "chr")
    player = configparser.ConfigParser(interpolation=None)
    player.read(game / f"player{character}.ini", encoding="utf-8-sig")
    sex = player.getint("init", "sex", fallback=0)
    equipment_sex = sex or 1
    client.act("SetAutoDialogue", enabled=True)
    initial = checkpoint(client, output, "equipment-limits-source")
    equipment = []
    for row in initial["inventory"]:
        item = configparser.ConfigParser(interpolation=None)
        item.read(resource / "ini/goods" / row["file"], encoding="utf-8-sig")
        if item.getint("Init", "Kind", fallback=0) != 1:
            continue
        if any(item.get("Init", key, fallback="") for key in ("User", "MinUserLevel", "NoNeedToEquip")):
            raise AutomationError("This gender matrix does not classify additional equipment restrictions")
        part = ("head", "neck", "body", "back", "hand", "wrist", "foot").index(item.get("Init", "Part").lower())
        equipment.append(dict(file=row["file"], part=part, requiredSex=item.getint("Init", "Sex", fallback=0)))
    owned = lambda state: {row["file"]: quantity(state, row["file"]) for row in state["inventory"]}
    results = []
    for index, item in enumerate(equipment):
        before = idle(client)
        row = next(row for row in before["inventory"] if row["file"] == item["file"])
        if row["slot"] >= initial["layout"]["equipmentBegin"]:
            client.open_menu("Equip")
            client.focus_slot("equipment-item-", row["slot"])
            client.ui("Confirm")
            client.ui("Cancel")
            row = next(row for row in idle(client)["inventory"] if row["file"] == item["file"])
        before = idle(client)
        client.equip(row["slot"])
        after = checkpoint(client, output, f"equipment-limits-{index}-result")
        rejected = bool(item["requiredSex"] and item["requiredSex"] != equipment_sex)
        if (owned(after) != owned(initial)
                or rejected and observed_character_configuration(after) != observed_character_configuration(before)
                or not rejected and not any(value["file"] == item["file"]
                       and value["slot"] == initial["layout"]["equipmentBegin"] + item["part"] for value in after["inventory"])):
            raise AutomationError("Ordinary equipment activation did not respect the protagonist's item/gender configuration")
        results.append(dict(**item, expectedRejected=rejected, inventoryQuantitiesPreserved=True,
                            sourceSha256=hashlib.sha256((resource / "ini/goods" / item["file"]).read_bytes()).hexdigest()))
    configured = checkpoint(client, output, "equipment-limits-configured")
    client.save_or_load(6)
    restored = load_checkpoint(client, 6)
    if observed_character_configuration(restored) != observed_character_configuration(configured):
        raise AutomationError("Mixed equipment changes did not survive normal save/load")
    write_json(output / "character-equipment-limits-proof.json", dict(status="passed", characterIndex=character,
               protagonistSex=sex, effectiveEquipmentSex=equipment_sex, nativeEquipmentCases=results,
               normalSaveReload=True, sourceSlot=6, cheatAssisted=True))


def pickup_reward(resource, source):
    text = (resource / source).read_text(encoding="utf-8-sig")
    calls = re.findall(r'\b(addgoods|addrandgoods|addrandmoney)\s*\(([^)]*)\)', text, re.I)
    if len(calls) != 1 or "delcurobj" not in text.casefold():
        raise AutomationError("This pickup audit requires one reward and native object removal: " + source)
    command, arguments = calls[0]
    if command.casefold() == "addrandmoney":
        low, high = map(int, arguments.split(","))
        return dict(kind="money", minimum=min(low, high), maximum=max(low, high))
    match = re.fullmatch(r'\s*"([^"]+)"\s*(?:,\s*(\d+))?\s*', arguments)
    if not match:
        raise AutomationError("Unsupported native pickup reward arguments: " + source)
    filename, count = match.groups()
    if command.casefold() == "addgoods":
        return dict(kind="goods", files=[filename], quantity=max(1, int(count or 1)), emptyOutcomeAllowed=False)
    stock_path = resource / "ini/buy" / filename
    stock = configparser.ConfigParser(interpolation=None, comment_prefixes=(";", "#", "//"))
    stock.read(stock_path, encoding="utf-8-sig")
    files = [stock.get(str(index), "IniFile", fallback="")
             for index in range(1, stock.getint("Header", "Count") + 1)]
    return dict(kind="random-goods", files=sorted(set(filter(None, files))), quantity=1,
                emptyOutcomeAllowed="" in files, stock=filename,
                stockSha256=hashlib.sha256(stock_path.read_bytes()).hexdigest())


def saved_object_pickups(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    initial = idle(client)
    game = output / "user-data/save/xjxqy/game"
    state_ini = configparser.ConfigParser(interpolation=None)
    state_ini.read(game / "game.ini", encoding="utf-8-sig")
    object_file = game / state_ini.get("state", "obj")
    objects = configparser.ConfigParser(interpolation=None)
    objects.read(object_file, encoding="utf-8-sig")
    pending = {row["path"] for row in json.loads((Path("tmp/xjxqy-coverage-40/unexecuted-scripts.json")).read_text(encoding="utf-8"))["scripts"]}
    cases = {}
    for section in objects.sections():
        row = objects[section]
        source = "script/common/" + row.get("scriptfile", "")
        if source not in pending or int(row.get("scriptfilejusttouch", "0")) > 0:
            continue
        position = (int(row["mapx"]), int(row["mapy"]))
        if source not in cases:
            cases[source] = dict(section=section, name=row["objname"], position=position,
                                 reward=pickup_reward(resource, source))
    if not cases:
        raise AutomationError("The ordinary source has no pending interactive common pickup")
    source_hash = hashlib.sha256(object_file.read_bytes()).hexdigest()
    client.save_or_load(0)
    proofs = []
    while cases:
        before = idle(client)
        player = before["player"]["position"]
        source = min(cases, key=lambda key: abs(cases[key]["position"][0] - player["x"])
                     + abs(cases[key]["position"][1] - player["y"]))
        case = cases.pop(source)
        target = next(row for row in before["targets"] if row["kind"] == "object" and row["name"] == case["name"]
                      and row["position"] == dict(x=case["position"][0], y=case["position"][1]))
        prefix = "pickup-" + str(len(proofs))
        before = checkpoint(client, output, prefix + "-before")
        after = interact_at(client, output, resource, case["name"], source, prefix + "-after",
                            case["position"], target_id=target["id"])
        files = {row["file"] for state in (before, after) for row in state["inventory"]}
        delta = {file: quantity(after, file) - quantity(before, file) for file in files
                 if quantity(after, file) != quantity(before, file)}
        money = after["player"]["money"] - before["player"]["money"]
        reward = case["reward"]
        same_object = lambda row: row["kind"] == "object" and row["name"] == target["name"] and row["position"] == target["position"]
        count_before = sum(same_object(row) for row in before["targets"])
        count_after = sum(same_object(row) for row in after["targets"])
        valid = (not delta and reward["minimum"] <= money <= reward["maximum"]) if reward["kind"] == "money" else (
            money == 0 and (not delta and reward["emptyOutcomeAllowed"]
                           or len(delta) == 1 and next(iter(delta)) in reward["files"]
                           and next(iter(delta.values())) == reward["quantity"]))
        if (not valid or any(row["id"] == target["id"] for row in after["targets"]) or count_after != count_before - 1
                or after["map"] != initial["map"]
                or any(after["variables"].get(key) != before["variables"].get(key)
                       for key in ("Event", "End", "Love", "NoEnd", "Clue"))):
            raise AutomationError("Native pickup reward, removal or story mismatch: " + source)
        proofs.append(dict(**case, execution=script_proof(output, resource, source),
                           goodsDelta=delta, moneyDelta=money, originalTargetId=target["id"],
                           objectRemoved=True, positionCountBefore=count_before, positionCountAfter=count_after))
    configured = checkpoint(client, output, "pickups-before-save")
    client.save_or_load(6)
    restored = load_checkpoint(client, 6)
    if (observed_character_configuration(restored) != observed_character_configuration(configured)
            or restored["player"]["money"] != configured["player"]["money"]
            or any(sum(row["kind"] == "object" and row["name"] == proof["name"]
                       and row["position"] == dict(x=proof["position"][0], y=proof["position"][1])
                       for row in restored["targets"]) != proof["positionCountAfter"] for proof in proofs)):
        raise AutomationError("Collected objects or their rewards changed after normal save/load")
    write_json(output / "saved-object-pickups-proof.json", dict(status="passed", map=initial["map"],
               sourceObjectFile=object_file.name, sourceObjectSha256=source_hash, nativePickups=proofs,
               normalSaveReload=True, collectedObjectOccurrencesStayRemoved=True, sourceSlots=[0, 6],
               allPossibleRandomGoodsVerified=False, cheatAssisted=True))


def dynamic_drop_search(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    initial = idle(client)
    game = configparser.ConfigParser(interpolation=None)
    game_path = output / "user-data/save/xjxqy/game/game.ini"
    game.read(game_path, encoding="utf-8-sig")
    npc_name = game.get("state", "npc")
    npcs = configparser.ConfigParser(interpolation=None)
    npcs.read(game_path.parent / npc_name, encoding="utf-8-sig")
    enemies = [dict(npcs[section]) for section in npcs.sections()
               if npcs.get(section, "relation", fallback="") == "1"]
    if (not enemies or any(row.get("deathscript") for row in enemies)
            or any(int(row.get("level", "0")) >= 12 for row in enemies)):
        raise AutomationError("Tier-one drop search requires a callback-free low-level ordinary source")
    client.save_or_load(4)
    source_npc = output / "user-data/save/xjxqy/rpg5" / npc_name
    source_hash = hashlib.sha256(source_npc.read_bytes()).hexdigest()
    rounds = []
    for repeat in range(5):
        if repeat:
            load_checkpoint(client, 4)
            assist(client, output, invincible=True)
        walk_required_battle(client, output, resource, None,
                             f"drop-search-{repeat}", progress_variable=None)
        state = checkpoint(client, output, f"drop-search-{repeat}-defeated")
        if state["map"] != initial["map"] or state["variables"] != initial["variables"]:
            raise AutomationError("Callback-free drop search changed a plot condition")
        client.save_or_load(5)
        game.read(game_path, encoding="utf-8-sig")
        object_name = game.get("state", "obj")
        objects = configparser.ConfigParser(interpolation=None)
        object_path = game_path.parent / object_name
        objects.read(object_path, encoding="utf-8-sig")
        dropped = [dict(section=section, name=objects.get(section, "objname"),
                        script=objects.get(section, "scriptfile"),
                        position=[objects.getint(section, "mapx"), objects.getint(section, "mapy")])
                   for section in objects.sections() if objects.get(section, "kind", fallback="") == "7"]
        rounds.append(dict(repeat=repeat, nativeDrops=dropped,
                           objectSnapshot=str(output / "user-data/save/xjxqy/rpg6" / object_name),
                           objectSha256=hashlib.sha256(object_path.read_bytes()).hexdigest()))
        write_json(output / "dynamic-drop-search-rounds.json", rounds)
        if any(row["script"] == "1级武器.txt" for row in dropped):
            saved_object_pickups(client, output, resource)
            proof = json.loads((output / "saved-object-pickups-proof.json").read_text(encoding="utf-8"))
            if not any(row["execution"]["path"] == "script/common/1级武器.txt"
                       for row in proof["nativePickups"]):
                raise AutomationError("Generated tier-one weapon was not picked through its native script")
            write_json(output / "dynamic-drop-search-proof.json", dict(status="passed",
                       sourceNPCFile=str(source_npc), sourceNPCSha256=source_hash,
                       sourceNPCCount=len(enemies), callbackFree=True, nativeCombatOnly=True,
                       npcNumbersEdited=False, plotVariablesUnchanged=True,
                       ordinaryBranchSlot=4, rounds=rounds, normalPickupSaveReload=True))
            return
    raise AutomationError("Tier-one weapon did not occur in five native randomized battle repetitions")


def interior_chests(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    initial = idle(client)
    proofs = []

    def room(cases):
        opened = []
        for filename, position, goods, money, locked in cases:
            before = idle(client)
            source = "script/map/" + Path(before["map"]).stem + "/" + filename
            state = interact_at(client, output, resource, "宝箱", source,
                                "chest-" + Path(before["map"]).stem + "-" + Path(filename).stem, position)
            expected = {row["file"]: quantity(before, row["file"]) for row in before["inventory"]}
            for name, number in goods.items():
                expected[name] = expected.get(name, 0) + number
            actual = {row["file"]: quantity(state, row["file"]) for row in state["inventory"]}
            if (actual != expected or state["player"]["money"] != before["player"]["money"] + money
                    or state["variables"].get("Event") != before["variables"].get("Event")):
                raise AutomationError("Native chest reward or main-story mismatch: " + source)
            if before["map"] == "map025_临安城铁匠铺.map":
                magic = next(row for row in state["magic"] if row["file"] == "magic056_烟火霹雳弹.ini")
                if magic["level"] != 9:
                    raise AutomationError("The fireworks chest did not grant its level-nine native skill")
            proofs.append(dict(execution=script_proof(output, resource, source), position=position,
                               expectedGoods=goods, expectedMoney=money, locked=locked))
            if not locked:
                opened.append(position)
        client.save_or_load(5)
        state = load_checkpoint(client, 5)
        before = checkpoint(client, output, "chest-reloaded-" + Path(state["map"]).stem)
        last = records(output)[-1]["sequence"]
        for position in opened:
            target = next(row for row in state["targets"] if row["kind"] == "object" and row["name"] == "宝箱"
                          and row["position"] == dict(x=position[0], y=position[1]))
            action = client.submit("Interact", generation=state["generation"], targetId=target["id"], running=True)
            status = client.request("GetActionStatus", actionId=action)
            if status["status"] != "failed" or status["reason"] != "action_rejected":
                raise AutomationError("A collected chest allowed another activation after normal save/load")
        after = checkpoint(client, output, "chest-repeat-rejected-" + Path(state["map"]).stem)
        if (after["inventory"] != before["inventory"] or after["player"]["money"] != before["player"]["money"]
                or any(row.get("eventType") == "script.start" and row["sequence"] > last for row in records(output))):
            raise AutomationError("Rejected chest interaction repeated a reward or script")

    he_cases = [("宝箱.txt", (9, 48), {} if initial["variables"].get("Event") == "40" else
                 {"goods009_玫瑰玉簪.ini": 1, "goods209_玄参方.ini": 1}, 0,
                 initial["variables"].get("Event") == "40"),
                ("宝箱1.txt", (5, 40), {"goods007_翡翠簪.ini": 1}, 100, False)]
    if initial["map"] == "map002_烟庐.map":
        room([("宝箱.txt", (7, 10), {}, 50, False),
              ("宝箱2.txt", (5, 10), {}, 50, False)])
    elif initial["map"] == "map063_华山派大厅.map":
        room([("宝箱.txt", (9, 15), {"goods035_镜湖玉甲.ini": 1, "goods003_通天冠.ini": 1}, 0, False)])
    elif initial["map"] == "map092_土匪窝.map":
        room([("宝箱.txt", (3, 11), {}, 1000, False),
              ("宝箱1.txt", (6, 11), {"goods030_芙蓉金丝镯.ini": 1, "goods076_飞燕靴.ini": 1}, 0, False)])
    elif initial["map"] == "map106_1成都客栈二楼.map":
        if initial["variables"].get("Openbox2") != "1":
            room([("宝箱1.txt", (3, 29), {}, 0, True)])
            if idle(client)["variables"].get("Openbox2") != "1":
                raise AutomationError("The empty chest did not retain its first opening after ordinary reload")
        room([("宝箱.txt", (11, 12), {"goods210_药王金方.ini": 1}, 0, False),
              ("宝箱1.txt", (3, 29), {"goods060_三因剑.ini": 1}, 0, False),
              ("宝箱2.txt", (10, 42), {"goods211_千金藤.ini": 2, "goods213_仙人茶.ini": 2,
                                      "goods214_灵芝草.ini": 2}, 0, False)])
    elif initial["map"] == "map020_临安城客栈一楼.map" and initial["variables"].get("Event") == "40":
        room([("宝箱.txt", (4, 31), {}, 0, False),
              ("宝箱1.txt", (9, 23), {"goods033_兽皮软甲.ini": 1}, 30, False)])
        transition(client, resource, "map021_临安城客栈二楼.map", 2, output, "chest-inn-up")
        room([("宝箱.txt", (15, 25), {"goods011_迦蓝挂链.ini": 1}, 0, False),
              ("宝箱1.txt", (2, 31), {"goods017_冰珠挂链.ini": 1, "goods066_流云履.ini": 1}, 0, False)])
        transition(client, resource, "map020_临安城客栈一楼.map", 1, output, "chest-inn-down")
        transition(client, resource, "map016_临安城.map", 1, output, "chest-linan-city")
        transition(client, resource, "map025_临安城铁匠铺.map", 8, output, "chest-linan-blacksmith")
        room([("宝箱.txt", (8, 18), {"goods061_烟火霹雳弹.ini": 5}, 0, False)])
        transition(client, resource, "map016_临安城.map", 1, output, "chest-blacksmith-return")
        transition(client, resource, "map026_临安城何员外家.map", 12, output, "chest-he-house")
        room(he_cases)
    elif initial["map"] == "map026_临安城何员外家.map" and initial["variables"].get("Event") == "190":
        room(he_cases)
    elif initial["map"] == "map049_长安城.map" and initial["variables"].get("Event") == "300":
        transition(client, resource, "map055_长安城客栈一楼.map", 3, output, "chest-changan-inn")
        room([("宝箱.txt", (18, 34), {}, 100, False),
              ("宝箱1.txt", (9, 15), {"goods074_凌波靴.ini": 1, "goods013_赤霞珠链.ini": 1}, 0, False)])
        transition(client, resource, "map055_1_长安客栈二楼.map", 2, output, "chest-changan-up")
        room([("宝箱.txt", (5, 22), {"goods209_玄参方.ini": 1}, 200, False),
              ("宝箱1.txt", (17, 33), {"goods067_无影靴.ini": 1, "goods084_绮罗玉披.ini": 1}, 0, False)])
    elif initial["map"] == "map012_武夷山大厅.map":
        if any(row.get("name") == "宝箱" and row["position"] == dict(x=17, y=40) for row in initial["targets"]):
            room([("宝箱.txt", (17, 40), {"goods003_通天冠.ini": 1}, 0, False)])
        else:
            room([("宝箱1.txt", (17, 41), {"goods021_银铁护腕.ini": 1}, 0, False)])
    elif initial["map"] == "map084_金兵主帅营.map":
        room([("宝箱.txt", (8, 14), {"goods070_逍遥足.ini": 1}, 0, False)])
    elif initial["map"] == "map106_成都客栈一楼.map":
        room([("宝箱.txt", (12, 14), {"goods209_玄参方.ini": 4}, 0, False),
              ("宝箱1.txt", (3, 29), {}, 400, False),
              ("宝箱2.txt", (8, 22), {}, 0, False)])
    else:
        raise AutomationError("Chest audit requires an ordinary saved chest entry")
    client.save_or_load(6)
    write_json(output / "interior-chests-proof.json", dict(status="passed", nativeChests=proofs,
               normalSaveReload=True, collectedChestsRejectRepeat=True, sourceSlot=6))


def remaining_interior_tour(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    initial = idle(client)
    map_name, event = initial["map"], initial["variables"].get("Event")
    if map_name == "map001_衡山.map" and event == "10":
        transition(client, resource, "map002_烟庐.map", 2, output, "remaining-smoke-entry")
        saved_npc_dialogues(client, output, resource, partners=True)
        interior_chests(client, output, resource)
        state = transition(client, resource, map_name, 1, output, "remaining-smoke-return")
    elif map_name == "map007_武夷山九猴洞.map" and event == "30":
        transition(client, resource, "map011_武夷山顶.map", 2, output, "remaining-wuyi-entry")
        saved_npc_dialogues(client, output, resource)
        state = transition(client, resource, map_name, 1, output, "remaining-wuyi-return")
    elif map_name == "map106_成都客栈一楼.map" and event == "620" and initial["variables"].get("Clue") == "0":
        transition(client, resource, "map106_1成都客栈二楼.map", 2, output, "remaining-chengdu-up")
        interior_chests(client, output, resource)
        state = transition(client, resource, map_name, 1, output, "remaining-chengdu-down")
    elif map_name == "map062_华山栈道3.map" and event == "370":
        transition(client, resource, "map063_华山派大厅.map", 3, output, "remaining-huashan-entry")
        saved_npc_dialogues(client, output, resource, partners=True)
        interior_chests(client, output, resource)
        state = transition(client, resource, map_name, 1, output, "remaining-huashan-return")
    elif map_name == "map091_长白山北.map" and initial["variables"].get("Talktufei") == "2":
        transition(client, resource, "map092_土匪窝.map", 2, output, "remaining-bandit-entry")
        interior_chests(client, output, resource)
        state = transition(client, resource, map_name, 1, output, "remaining-bandit-return")
    else:
        raise AutomationError("Remaining interior tour requires a reviewed ordinary entry stage")
    if state["map"] != map_name or state["variables"].get("Event") != event:
        raise AutomationError("Remaining interior tour changed the main story")
    client.save_or_load(6)
    configured = checkpoint(client, output, "remaining-interior-before-load")
    restored = load_checkpoint(client, 6)
    if (restored["map"] != map_name or restored["variables"] != configured["variables"]
            or observed_character_configuration(restored) != observed_character_configuration(configured)
            or restored["player"]["money"] != configured["player"]["money"]):
        raise AutomationError("Remaining interior tour rewards changed after ordinary reload")
    write_json(output / "remaining-interior-tour-proof.json", dict(status="passed", sourceMap=map_name,
               Event=event, nativeMovementOnly=True, normalSaveReload=True, mainEventUnchanged=True))


def linan_shop_tiers(client, output, resource, city="linan"):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if state["map"] == "map020_临安城客栈一楼.map":
        state = transition(client, resource, "map016_临安城.map", 1, output, "tier-linan-city")
    chengdu = city == "chengdu"
    if chengdu:
        if state["map"] != "map105_成都.map" or state["variables"].get("Event") != "620" or state["variables"].get("Clue") != "0":
            raise AutomationError("Chengdu shop tier audit requires the ordinary first city source")
    elif state["map"] != "map016_临安城.map" or state["variables"].get("Event") != "40":
        raise AutomationError("Shop tier audit requires the ordinary first Linan source")
    source_map = state["map"]
    level = state["player"]["level"]
    tier = 1 + sum(level >= boundary for boundary in (10, 16, 22, 28, 34, 40))
    holiday = bool(json.loads((output / "run.json").read_text(encoding="utf-8")).get("calendarAssistance"))
    medicine = "低级药品.ini" if level < 16 else "中级药品.ini" if level < 31 else "高级药品.ini"
    proofs = []
    routes = (
        ("map022_临安城杂货店.map", 10, "杂货店老板", "杂货店老板对话.txt", f"buy{tier}级防具.ini"),
        ("map025_临安城铁匠铺.map", 8, "铁匠铺学徒", "铁匠学徒对话.txt", f"buy{'过年' if holiday else ''}{tier}级武器.ini"),
        ("map028_临安城药店.map", 9, "药店老板", "药店老板对话.txt", medicine))
    if chengdu:
        routes = (("map107_成都杂货铺.map", 2, "杂货铺老板", "杂货铺老板.txt", f"buy{tier}级防具.ini"),
                  ("map108_成都铁匠铺.map", 3, "铁匠铺老板", "铁匠.txt", f"buy{'过年' if holiday else ''}{tier}级武器.ini"),
                  ("map109_成都药店.map", 4, "药店老板", "药店掌柜.txt", medicine))
    for destination, trap, name, filename, stock_name in routes:
        state = transition(client, resource, destination, trap, output, "tier-entry-" + Path(destination).stem)
        before = checkpoint(client, output, "tier-source-" + Path(destination).stem)
        old_shops = set(output.glob("native-shop-*-before.json"))
        source = "script/map/" + Path(destination).stem + "/" + filename
        choices = [(source + (":120" if holiday else ":50"), 0)] if name == "铁匠铺学徒" else ()
        state = interact_at(client, output, resource, name, source, "tier-dialogue-" + Path(destination).stem,
                            shop_cancel=True, choices=choices)
        captured = set(output.glob("native-shop-*-before.json")) - old_shops
        if len(captured) != 1:
            raise AutomationError("Shop tier needs one actual presented shop inventory")
        snapshot_file = captured.pop()
        snapshot = json.loads(snapshot_file.read_text(encoding="utf-8"))
        stock_path = resource / "ini/buy" / stock_name
        stock = configparser.ConfigParser(interpolation=None)
        stock.read(stock_path, encoding="utf-8-sig")
        expected = [dict(file=stock[str(index)]["inifile"], quantity=max(1, stock.getint(str(index), "number", fallback=1)))
                    for index in range(1, stock.getint("Header", "count") + 1)
                    if stock.get(str(index), "inifile", fallback="")]
        actual = [dict(file=row["file"], quantity=row["quantity"]) for row in snapshot["shop"]]
        execution = script_proof(output, resource, source)
        lines = (resource / source).read_text(encoding="utf-8-sig").splitlines()
        if (actual != expected or snapshot["player"]["level"] != level
                or state["player"]["money"] != before["player"]["money"] or state["inventory"] != before["inventory"]
                or state["variables"].get("Value") != str(int(holiday))
                or not any(stock_name in lines[line - 1] and "sellgoods" in lines[line - 1].casefold()
                           for line in execution["executedLines"])):
            raise AutomationError(f"Native stock or cancellation mismatch: level{level} / {stock_name}")
        proofs.append(dict(stock=stock_name, stockSha256=hashlib.sha256(stock_path.read_bytes()).hexdigest(),
                           level=level, tier=tier, snapshotFile=str(snapshot_file), actual=actual, execution=execution,
                           blankStockSlots=[index for index in range(1, stock.getint("Header", "count") + 1)
                                            if not stock.get(str(index), "inifile", fallback="")]))
        transition(client, resource, source_map, 1, output, "tier-return-" + Path(destination).stem)
    client.save_or_load(6)
    if chengdu:
        configured = checkpoint(client, output, "chengdu-tiers-before-load")
        restored = load_checkpoint(client, 6)
        if (restored["map"] != source_map or restored["variables"] != configured["variables"] or restored["player"]["level"] != level
                or restored["player"]["money"] != configured["player"]["money"]
                or observed_character_configuration(restored) != observed_character_configuration(configured)):
            raise AutomationError("Chengdu shop tier state did not survive normal save/load")
    write_json(output / ("chengdu-shop-tiers-proof.json" if chengdu else "linan-shop-tiers-proof.json"), dict(status="passed", heroLevel=level, tier=tier,
               holiday=holiday, nativeShopInventories=proofs, moneyAndInventoryUnchanged=True, normalSaveReload=chengdu, sourceSlot=6))


def goods_prices(resource, filename, buy_percent=100, recycle_percent=100):
    item = configparser.ConfigParser(interpolation=None)
    item.read(resource / "ini/goods" / filename, encoding="utf-8-sig")
    number = lambda key: int(item.get("Init", key, fallback="0") or "0")
    raw = number("Cost")
    if raw <= 0:
        if number("Kind") == 0:
            raw = number("Thew") * 4 + number("Life") * 2 + number("Mana") * 2
        elif number("Kind") == 1 and not number("NoNeedToEquip"):
            raw = (sum(number(key) for key in ("Attack", "Attack2", "Attack3", "Defend", "Defend2", "Defend3")) * 20
                   + number("Evade") * 40 + number("LifeMax") * 2 + number("ThewMax") * 3 + number("ManaMax") * 2)
        else:
            raw = 0
        raw *= 2 if number("EffectType") else 1
    return max(0, raw * buy_percent // 100), max(0, (number("SellPrice") or raw // 2) * recycle_percent // 100)


def linan_shop_transactions(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if state["map"] == "map020_临安城客栈一楼.map":
        state = transition(client, resource, "map016_临安城.map", 1, output, "trade-linan-city")
    if state["map"] != "map016_临安城.map" or state["variables"].get("Event") != "40":
        raise AutomationError("Shop transactions require the ordinary first Linan source")
    holiday = bool(json.loads((output / "run.json").read_text(encoding="utf-8")).get("calendarAssistance"))
    level = state["player"]["level"]
    tier = 1 + sum(level >= boundary for boundary in (10, 16, 22, 28, 34, 40))
    medicine = "低级药品.ini" if level < 16 else "中级药品.ini" if level < 31 else "高级药品.ini"
    transactions = []
    for destination, trap, name, filename, stock_name in (
        ("map022_临安城杂货店.map", 10, "杂货店老板", "杂货店老板对话.txt", f"buy{tier}级防具.ini"),
        ("map025_临安城铁匠铺.map", 8, "铁匠铺学徒", "铁匠学徒对话.txt", f"buy{'过年' if holiday else ''}{tier}级武器.ini"),
        ("map028_临安城药店.map", 9, "药店老板", "药店老板对话.txt", medicine)):
        transition(client, resource, destination, trap, output, "trade-entry-" + Path(destination).stem)
        stock = configparser.ConfigParser(interpolation=None)
        stock.read(resource / "ini/buy" / stock_name, encoding="utf-8-sig")
        buy_percent = stock.getint("Header", "BuyPercent", fallback=100)
        recycle_percent = stock.getint("Header", "RecyclePercent", fallback=100)
        if stock.getint("Header", "NumberValid", fallback=0):
            raise AutomationError("This shop transaction route requires an ordinary unlimited merchant")

        def trade(presented):
            candidates = [(row, goods_prices(resource, row["file"], buy_percent, recycle_percent))
                          for row in presented["shop"] if quantity(presented, row["file"]) == 0]
            item, (buy_price, sell_price) = next((row, prices) for row, prices in candidates if prices[0] > 0 and prices[1] > 0)
            before = checkpoint(client, output, f"trade-{len(transactions)}-before")
            client.buy(item["slot"])
            bought = checkpoint(client, output, f"trade-{len(transactions)}-bought")
            sufficient = before["player"]["money"] >= buy_price
            if (bought["player"]["money"] != before["player"]["money"] - buy_price * sufficient
                    or quantity(bought, item["file"]) != int(sufficient)
                    or bought["shop"] != before["shop"]):
                raise AutomationError("Actual shop price or insufficient-money result differs from its formal item data")
            if sufficient:
                owned = next(row for row in bought["inventory"] if row["file"] == item["file"])
                if owned["slot"] >= bought["layout"]["equipmentBegin"]:
                    raise AutomationError("Shop purchase did not arrive in the ordinary bag")
                client.sell(owned["slot"])
                after = checkpoint(client, output, f"trade-{len(transactions)}-sold")
                if after["player"]["money"] != bought["player"]["money"] + sell_price:
                    raise AutomationError("Selling the purchased item did not return its formal resale price")
            else:
                unsellable = next(row for row in bought["inventory"] if row["file"] == "goods401_血书.ini")
                client.sell(unsellable["slot"])
                after = checkpoint(client, output, f"trade-{len(transactions)}-unsellable")
                if after["player"]["money"] != bought["player"]["money"]:
                    raise AutomationError("A zero-price story item produced sale money")
            expected_stock = [dict(row, quantity=row["quantity"] + int(sufficient and row["file"] == item["file"]))
                              for row in before["shop"]]
            if (observed_character_configuration(after) != observed_character_configuration(before)
                    or after["shop"] != expected_stock):
                raise AutomationError("Buy/sell or rejected transaction changed retained inventory or stock")
            transactions.append(dict(stock=stock_name, file=item["file"], buyPrice=buy_price, sellPrice=sell_price,
                                     purchaseSucceeded=sufficient, storyItemSaleRejected=not sufficient,
                                     moneyBefore=before["player"]["money"], moneyAfter=after["player"]["money"],
                                     unlimitedPurchaseStockUnchanged=True, resaleStockIncrement=int(sufficient),
                                     itemSha256=hashlib.sha256((resource / "ini/goods" / item["file"]).read_bytes()).hexdigest(),
                                     stockSha256=hashlib.sha256((resource / "ini/buy" / stock_name).read_bytes()).hexdigest()))
        source = "script/map/" + Path(destination).stem + "/" + filename
        choices = [(source + (":120" if holiday else ":50"), 0)] if name == "铁匠铺学徒" else ()
        interact_at(client, output, resource, name, source, "trade-native-" + Path(destination).stem,
                    choices=choices, shop_cancel=trade)
        transactions[-1]["execution"] = script_proof(output, resource, source)
        transition(client, resource, "map016_临安城.map", 1, output, "trade-return-" + Path(destination).stem)
    configured = checkpoint(client, output, "trade-before-save")
    client.save_or_load(6)
    restored = load_checkpoint(client, 6)
    if (restored["player"]["money"] != configured["player"]["money"]
            or observed_character_configuration(restored) != observed_character_configuration(configured)):
        raise AutomationError("Normal save/load did not preserve actual shop results")
    write_json(output / "linan-shop-transactions-proof.json", dict(status="passed", heroLevel=level, tier=tier,
               holiday=holiday, nativeTransactions=transactions, normalSaveReload=True, sourceSlot=6, cheatAssisted=True))


def festival_choices(client, output, resource):
    identity = json.loads((output / "run.json").read_text(encoding="utf-8"))
    if identity.get("calendarAssistance", {}).get("localDate") != "2026-01-01":
        raise AutomationError("Festival routes require the explicitly authorized process date")
    client.act("SetAutoDialogue", enabled=True)
    initial = idle(client)
    proofs = []

    def both(name, filename, line, costs=(0, 0), variable=None, expected=None, shop=False):
        source = "script/map/" + Path(idle(client)["map"]).stem + "/" + filename
        before = checkpoint(client, output, f"festival-{len(proofs)}-source", (variable,) if variable else ())
        client.save_or_load(0)
        for option in (0, 1):
            load_checkpoint(client, 0)
            state = interact_at(client, output, resource, name, source, f"festival-{len(proofs)}-option{option}",
                                choices=[(source + f":{line}", option)], shop_cancel=shop)
            after = checkpoint(client, output, f"festival-{len(proofs)}-result{option}", (variable,) if variable else ())
            if (after["variables"].get("Value") != "1"
                    or after["player"]["money"] != before["player"]["money"] - costs[option]
                    or expected and str(after["variables"].get(variable) or "0") != str(expected[option])):
                raise AutomationError(f"Native festival result differs: {source} option{option}")
            proofs.append(dict(source=source, line=line, option=option, nativeCheckYear=1,
                               moneyBefore=before["player"]["money"], moneyAfter=after["player"]["money"],
                               execution=script_proof(output, resource, source)))
        # Keep the first choice's normal result for the next destination.
        load_checkpoint(client, 0)
        interact_at(client, output, resource, name, source, f"festival-{len(proofs)}-retained-option0",
                    choices=[(source + f":{line}", 0)], shop_cancel=shop)

    if initial["map"] == "map020_临安城客栈一楼.map":
        late = initial["variables"].get("Event") == "180"
        money = initial["player"]["money"]
        both("客栈掌柜", "客栈掌柜对话.txt", 121 if late else 89,
             (0 if late or money < 10 else 10, 0))
        if not late:
            transition(client, resource, "map016_临安城.map", 1, output, "festival-linan-city")
            for name, filename, line, cost in (
                ("鹃儿", "临安城居民11之鹃儿对话.txt", 33, 5),
                ("小武", "临安城居民20之小武对话.txt", 16, 5),
                ("小雷", "临安城居民7之小雷对话.txt", 54, 20)):
                money = idle(client)["player"]["money"]
                both(name, filename, line, (cost if money >= cost else 0, 0))
            transition(client, resource, "map025_临安城铁匠铺.map", 8, output, "festival-linan-blacksmith")
            money = idle(client)["player"]["money"]
            # The holiday apprentice's second option buys the rare blade.
            both("铁匠铺学徒", "铁匠学徒对话.txt", 120, (0, 2500 if money >= 2500 else 0),
                 "Bixue", (0, int(money >= 2500)), shop=True)
            # Retain the purchase through the ordinary UI so the casino greeting
            # can be covered without repeating the already accepted wager test.
            if money >= 2500:
                load_checkpoint(client, 0)
                interact_at(client, output, resource, "铁匠铺学徒",
                            "script/map/map025_临安城铁匠铺/铁匠学徒对话.txt", "festival-retain-rare-purchase",
                            choices=[("script/map/map025_临安城铁匠铺/铁匠学徒对话.txt:120", 1)], shop_cancel=True)
            transition(client, resource, "map016_临安城.map", 1, output, "festival-blacksmith-return")
            if idle(client)["player"]["money"] >= 80:
                raise AutomationError("Festival greeting route needs a native insufficient-funds casino continuation")
            transition(client, resource, "map027_临安城赌坊.map", 7, output, "festival-linan-casino")
            both("赌坊老板", "赌场老板对话.txt", 40)
    elif initial["map"] == "map028_临安城药店.map" and initial["variables"].get("Sub") == "130":
        both("药店老板", "药店老板对话.txt", 123, variable="Sub", expected=(130, 140), shop=True)
    elif initial["map"] == "map049_长安城.map":
        for name, filename, line, cost in (
            ("浅浅", "长安城居民6之浅浅对话.txt", 40, 5),
            ("乞丐", "长安城居民9之乞丐对话.txt", 18, 10)):
            money = idle(client)["player"]["money"]
            both(name, filename, line, (cost if money >= cost else 0, 0))
        if idle(client)["player"]["money"] >= 100:
            raise AutomationError("Festival greeting route requires the ordinary casino insufficient-funds branch")
        transition(client, resource, "map056_长安城赌坊.map", 5, output, "festival-changan-casino")
        both("赌坊老板", "赌场老板对话.txt", 47)
    else:
        raise AutomationError("Unsupported ordinary festival source")
    client.save_or_load(6)
    write_json(output / "festival-choices-proof.json", dict(status="passed", calendarAssistance=identity["calendarAssistance"],
               sourceMap=initial["map"], sourceEvent=initial["variables"].get("Event"), choices=proofs,
               branchSaveReused=True, directCallbacks=False, ordinarySourceSlots=[0, 6]))


def casino_choices(client, output, resource):
    state = idle(client)
    client.act("SetAutoDialogue", enabled=True)
    if state["map"] == "map020_临安城客栈一楼.map":
        state = transition(client, resource, "map016_临安城.map", 1, output, "4199-casino-inn-departure")
    destinations = {"map016_临安城.map": ("map027_临安城赌坊.map", 7, 80),
                    "map049_长安城.map": ("map056_长安城赌坊.map", 5, 100)}
    if state["map"] in destinations:
        destination, trap, cost = destinations[state["map"]]
        state = transition(client, resource, destination, trap, output, "4200-casino-entry")
    elif state["map"] in ("map027_临安城赌坊.map", "map056_长安城赌坊.map"):
        cost = 80 if state["map"] == "map027_临安城赌坊.map" else 100
    else:
        raise AutomationError("Casino choices require a normal Linan or Changan source")
    if state["variables"].get("Event") in ("304", "305"):
        raise AutomationError("Rumeng's native casino refusal is outside wager choices")
    source = "script/map/" + Path(state["map"]).stem + "/赌场老板对话.txt"
    site = min((row for row in catalog_for(resource)["choices"] if row["path"] == source), key=lambda row: row["line"])
    client.save_or_load(0)
    before = checkpoint(client, output, "4201-casino-source")
    state = interact_at(client, output, resource, "赌坊老板", source, "4202-casino-refusal", choices=[(site["id"], 1)])
    if state["player"]["money"] != before["player"]["money"] or state["inventory"] != before["inventory"]:
        raise AutomationError("Refusing a native wager changed money or inventory")
    outcomes = set()
    for attempt in range(12):
        load_checkpoint(client, 0)
        state = interact_at(client, output, resource, "赌坊老板", source, f"4203-casino-wager-{attempt}",
                            choices=[(site["id"], 0)], gamble_round=cost)
        delta = state["player"]["money"] - before["player"]["money"]
        expected = (0,) if before["player"]["money"] < cost else (-cost, cost)
        if (delta not in expected or state["inventory"] != before["inventory"]
                or state["variables"].get("Event") != before["variables"].get("Event")):
            raise AutomationError("Native casino settlement changed the story or has an unexpected payout")
        outcomes.add("insufficient" if delta == 0 else "win" if delta > 0 else "loss")
        if outcomes == {"insufficient"} or outcomes == {"win", "loss"}:
            break
    else:
        raise AutomationError("Both native casino outcomes were not observed after twelve ordinary wagers")
    client.save_or_load(6)
    write_json(output / "casino-choices-proof.json", dict(status="passed", source=source, admissionCost=cost,
               nativeRefusal=True, outcomes=sorted(outcomes), wagerStake=cost if "insufficient" not in outcomes else 0,
               nativeStoryAndInventoryUnchanged=True, sourceSlots=[0, 6], cheatAssisted=False))


def linan_rare_weapon(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if state["map"] == "map016_临安城.map":
        state = transition(client, resource, "map025_临安城铁匠铺.map", 8, output, "4300-rare-weapon-shop")
    if state["map"] != "map025_临安城铁匠铺.map":
        raise AutomationError("Rare weapon requires a normally reached Linan blacksmith source")
    before = checkpoint(client, output, "4301-rare-weapon-source", ("Bixue",))
    if int(before["variables"].get("Bixue") or 0) != 0:
        raise AutomationError("Rare weapon already purchased in this ordinary source")
    client.save_or_load(0)
    source = "script/map/map025_临安城铁匠铺/铁匠学徒对话.txt"
    state = interact_at(client, output, resource, "铁匠铺学徒", source, "4302-rare-weapon-refusal",
                        choices=[(source + ":50", 0)], shop_cancel=True)
    if state["inventory"] != before["inventory"] or state["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Rare weapon refusal changed inventory or money")
    load_checkpoint(client, 0)
    interact_at(client, output, resource, "铁匠铺学徒", source, "4303-rare-weapon-purchase",
                choices=[(source + ":50", 1)], shop_cancel=True)
    state = checkpoint(client, output, "4304-rare-weapon-result", ("Bixue",))
    bought = int(before["player"]["money"] >= 2500)
    if (state["player"]["money"] != before["player"]["money"] - 2500 * bought
            or quantity(state, "goods051_碧潮宝刃.ini") != quantity(before, "goods051_碧潮宝刃.ini") + bought
            or int(state["variables"].get("Bixue") or 0) != bought):
        raise AutomationError("Rare weapon price, quantity or completion flag mismatch")
    if bought:
        repeated = interact_at(client, output, resource, "铁匠铺学徒", source, "4305-rare-weapon-repeat", shop_cancel=True)
        if repeated["inventory"] != state["inventory"] or repeated["player"]["money"] != state["player"]["money"]:
            raise AutomationError("Rare weapon reward repeated after purchase")
    client.save_or_load(6)
    write_json(output / "linan-rare-weapon-proof.json", dict(status="passed", nativeRefusal=True,
               nativePurchased=bool(bought), nativeSilverConsumed=2500 * bought,
               nativeRepeatNoReward=bool(bought), insufficientSilver=not bought, sourceSlots=[0, 6], cheatAssisted=False))


def linan_token(client, output, resource):
    state = idle(client)
    if state["map"] != "map016_临安城.map" or state["variables"].get("Event") != "60":
        raise AutomationError("Linan token branches require the native beggar clue slot 6")
    client.act("SetAutoDialogue", enabled=True)
    transition(client, resource, "map027_临安城赌坊.map", 7, output, "110-casino-arrival")
    client.save_or_load(0)
    source = checkpoint(client, output, "111-token-choice-source-slot0")
    relative = "script/map/map027_临安城赌坊/吕文才对话.txt"
    state = interact_at(client, output, resource, "吕文才", relative, "112-token-first-fight", choices=[(relative + ":28", 1)])
    if state["variables"].get("Event") != "65":
        raise AutomationError("First token fight did not start at native Event 65")
    state = clear_enemies(client, output, "113-token-first-fight-combat")
    if state["variables"].get("Event") != "70" or quantity(state, "goods403_临安城东门令牌.ini") != 1:
        raise AutomationError("Native token fight did not yield one east-gate token")
    client.save_or_load(5)
    checkpoint(client, output, "114-token-first-fight-won-slot5")
    outcomes = set()
    for number in range(12):
        load_checkpoint(client, 0)
        state = interact_at(client, output, resource, "吕文才", relative, f"115-token-gamble-{number}",
                            choices=[(relative + ":28", 0)], gamble_round=True)
        delta = state["player"]["money"] - source["player"]["money"]
        outcome = "win" if delta == 80 else "loss" if delta == -80 else None
        if outcome is None:
            raise AutomationError("Native token wager has an unexpected money delta")
        outcomes.add(outcome)
        if outcome == "win":
            if state["variables"].get("Event") != "70" or quantity(state, "goods403_临安城东门令牌.ini") != 1:
                raise AutomationError("Winning the native wager did not provide its token")
            client.save_or_load(4)
            checkpoint(client, output, "116-token-gamble-win-slot4")
        else:
            if state["variables"].get("Event") != "60" or quantity(state, "goods403_临安城东门令牌.ini") != 0:
                raise AutomationError("Losing the native wager changed the token objective")
            client.save_or_load(3)
            checkpoint(client, output, "117-token-gamble-loss-slot3")
        if outcomes == {"win", "loss"}:
            break
    else:
        raise AutomationError("Both native random wager outcomes were not observed")
    load_checkpoint(client, 3)
    before = client.observe(VARIABLES)
    state = interact_at(client, output, resource, "吕文才", relative, "118-token-repeat-insufficient-silver",
                        choices=[(relative + ":94", 0)])
    if state["variables"].get("Event") != "60" or state["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Repeated wager with insufficient silver changed the story or money")
    state = interact_at(client, output, resource, "吕文才", relative, "119-token-repeat-fight", choices=[(relative + ":94", 1)])
    state = clear_enemies(client, output, "120-token-repeat-fight-combat")
    if state["variables"].get("Event") != "70" or quantity(state, "goods403_临安城东门令牌.ini") != 1:
        raise AutomationError("Repeated token fight did not complete normally")
    load_checkpoint(client, 5)
    transition(client, resource, "map016_临安城.map", 1, output, "121-casino-return")
    client.save_or_load(6)
    checkpoint(client, output, "122-native-token-linan-slot6")
    write_json(output / "linan-token-proof.json", dict(status="passed", Event="70", tokenCount=1,
               firstFight=True, repeatedFight=True, nativeGambleWin=True, nativeGambleLoss=True,
               repeatedInsufficientSilver=True, sourceSlots=[0, 3, 4, 5, 6], cheatAssisted=False))


def coastal_clue(client, output, resource):
    state = idle(client)
    if state["variables"].get("Event") != "70" or quantity(state, "goods403_临安城东门令牌.ini") != 1:
        raise AutomationError("Coastal clue requires a native east-gate token source")
    client.act("SetAutoDialogue", enabled=True)
    if state["map"] == "map027_临安城赌坊.map":
        interact_at(client, output, resource, "吕文才", "script/map/map027_临安城赌坊/吕文才对话.txt", "130-token-holder-reminder")
        transition(client, resource, "map016_临安城.map", 1, output, "131-token-casino-return")
    state = interact_at(client, output, resource, "东门卫兵1", "script/map/map016_临安城/城东卫兵对话.txt", "132-token-presented")
    if state["variables"].get("Event") != "80":
        raise AutomationError("Presenting the native token did not open the east gate")
    transition(client, resource, "map035_临安城东.map", 2, output, "133-linan-east-outskirts")
    interact_at(client, output, resource, "卖鱼人", "script/map/map035_临安城东/卖鱼人对话.txt", "134-fishseller-clue")
    interact_at(client, output, resource, "卖鱼人", "script/map/map035_临安城东/卖鱼人对话.txt", "135-fishseller-reminder")
    transition(client, resource, "map034_1_海边小渔村.map", 2, output, "136-coastal-village")
    client.save_or_load(0)
    source = checkpoint(client, output, "137-coastal-payment-source-slot0")
    relative = "script/map/map034_1_海边小渔村/渔民浪里白挑对话.txt"
    state = interact_at(client, output, resource, "浪里白挑", relative, "138-coastal-first-refusal", choices=[(relative + ":21", 1)])
    if state["player"]["money"] != source["player"]["money"] or state["variables"].get("TalkLang90") != "0":
        raise AutomationError("Refusing the first coastal payment changed money or its clue branch")
    client.save_or_load(2)
    checkpoint(client, output, "139-coastal-repeat-source-slot2")
    state = interact_at(client, output, resource, "浪里白挑", relative, "140-coastal-repeat-refusal", choices=[(relative + ":56", 1)])
    if state["player"]["money"] != source["player"]["money"]:
        raise AutomationError("Repeated refusal charged silver")
    load_checkpoint(client, 2)
    state = interact_at(client, output, resource, "浪里白挑", relative, "141-coastal-repeat-paid", choices=[(relative + ":56", 0)])
    if state["player"]["money"] != source["player"]["money"] - 10 or state["variables"].get("TalkLang90") != "1":
        raise AutomationError("Repeated coastal payment did not cost ten and set its later clue")
    load_checkpoint(client, 0)
    state = interact_at(client, output, resource, "浪里白挑", relative, "142-coastal-first-paid", choices=[(relative + ":21", 0)])
    if state["player"]["money"] != source["player"]["money"] - 10 or state["variables"].get("TalkLang90") != "1":
        raise AutomationError("First coastal payment did not cost ten and set its later clue")
    interact_at(client, output, resource, "渔夫", "script/map/map034_1_海边小渔村/渔夫对话.txt", "143-fisherman-clue")
    interact_at(client, output, resource, "渔夫", "script/map/map034_1_海边小渔村/渔夫对话.txt", "144-fisherman-reminder")
    transition(client, resource, "map034_1_海边小渔村.map", 1, output, "145-coastal-rescue-exit-refused")
    transition(client, resource, "map036_渔夫家.map", 2, output, "146-old-wang-house")
    state = interact_at(client, output, resource, "渔夫老王", "script/map/map036_渔夫家/渔夫老王对话.txt", "147-old-wang-refuses-second-trip")
    if state["variables"].get("Event") != "90":
        raise AutomationError("Old Wang's first refusal did not advance Event to 90")
    interact_at(client, output, resource, "渔夫老王", "script/map/map036_渔夫家/渔夫老王对话.txt", "148-old-wang-repeat-refusal")
    client.save_or_load(1)
    checkpoint(client, output, "149-old-wang-clue-source-slot1")
    transition(client, resource, "map034_1_海边小渔村.map", 1, output, "150-old-wang-house-return")
    state = interact_at(client, output, resource, "浪里白挑", relative, "151-coastal-paid-jia-clue")
    if state["variables"].get("Event") != "95":
        raise AutomationError("Paid coastal information did not advance Event to 95")
    interact_at(client, output, resource, "浪里白挑", relative, "152-coastal-paid-reminder")
    transition(client, resource, "map035_临安城东.map", 1, output, "153-coastal-clue-return-to-outskirts")
    transition(client, resource, "map016_临安城.map", 1, output, "154-coastal-clue-return-to-linan")
    client.save_or_load(6)
    checkpoint(client, output, "155-native-jia-clue-slot6")
    write_json(output / "coastal-clue-proof.json", dict(status="passed", Event="95", nativeTokenGate=True,
               firstPaymentOptions=True, repeatedPaymentOptions=True, tenSilverCharged=True,
               paidJiaClue=True, oldWangRefusal=True, sourceSlots=[0, 1, 2, 6], cheatAssisted=False))


def coastal_unpaid_clue(client, output, resource):
    state = idle(client)
    if (state["map"] != "map034_1_海边小渔村.map" or state["variables"].get("Event") != "80"
            or state["variables"].get("TalkLang90") != "0"):
        raise AutomationError("Unpaid clue requires the native first-refusal slot 2")
    money = state["player"]["money"]
    client.act("SetAutoDialogue", enabled=True)
    interact_at(client, output, resource, "渔夫", "script/map/map034_1_海边小渔村/渔夫对话.txt", "160-unpaid-fisherman-clue")
    transition(client, resource, "map036_渔夫家.map", 2, output, "161-unpaid-old-wang-house")
    state = interact_at(client, output, resource, "渔夫老王", "script/map/map036_渔夫家/渔夫老王对话.txt", "162-unpaid-old-wang-refusal")
    if state["variables"].get("Event") != "90":
        raise AutomationError("Unpaid branch did not reach the native second-trip refusal")
    transition(client, resource, "map034_1_海边小渔村.map", 1, output, "163-unpaid-house-return")
    state = interact_at(client, output, resource, "浪里白挑", "script/map/map034_1_海边小渔村/渔民浪里白挑对话.txt", "164-unpaid-jia-clue")
    if (state["variables"].get("Event") != "95" or state["variables"].get("TalkLang90") != "0"
            or state["player"]["money"] != money):
        raise AutomationError("Unpaid information changed its payment flag or money")
    interact_at(client, output, resource, "浪里白挑", "script/map/map034_1_海边小渔村/渔民浪里白挑对话.txt", "165-unpaid-reminder")
    client.save_or_load(6)
    checkpoint(client, output, "166-unpaid-clue-source-slot6")
    write_json(output / "coastal-unpaid-clue-proof.json", dict(status="passed", Event="95", TalkLang90="0",
               moneyUnchanged=True, sourceSlot=2, cheatAssisted=False))


def jia_and_qiuyu(client, output, resource):
    state = idle(client)
    if state["map"] != "map016_临安城.map" or state["variables"].get("Event") != "95":
        raise AutomationError("Jia and Qiuyu require the native city clue slot 6")
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(5)
    checkpoint(client, output, "170-jia-clue-source-slot5")
    jia = "script/map/map020_临安城客栈一楼/贾老实对话.txt"
    guard = "script/map/map023_临安城妓院/妓院打手对话.txt"
    qiuyu = "script/map/map023_临安城妓院/妓女秋雨对话.txt"
    transition(client, resource, "map020_临安城客栈一楼.map", 6, output, "171-jia-first-inn")
    state = interact_at(client, output, resource, "贾老实", jia, "172-jia-tears-request")
    if state["variables"].get("Event") != "100":
        raise AutomationError("Jia's request did not advance Event to 100")
    interact_at(client, output, resource, "贾老实", jia, "173-jia-request-reminder")
    client.save_or_load(3)
    checkpoint(client, output, "174-jia-request-source-slot3")
    transition(client, resource, "map016_临安城.map", 1, output, "175-jia-first-inn-exit")
    transition(client, resource, "map023_临安城妓院.map", 5, output, "176-brothel-first-visit")
    state = interact_at(client, output, resource, "妓院打手", guard, "177-guard-first-at-event100")
    if state["variables"].get("Event") != "105":
        raise AutomationError("First-time brothel guard did not refuse entry at Event 100")
    load_checkpoint(client, 5)
    transition(client, resource, "map023_临安城妓院.map", 5, output, "178-brothel-early-visit")
    money = idle(client)["player"]["money"]
    state = interact_at(client, output, resource, "妓院打手", guard, "179-guard-early-first")
    if state["variables"].get("TalkDashou") != "1" or state["player"]["money"] != money:
        raise AutomationError("Early guard dialogue altered money or failed its native flag")
    interact_at(client, output, resource, "妓院打手", guard, "180-guard-early-repeat")
    transition(client, resource, "map016_临安城.map", 1, output, "181-early-brothel-exit")
    transition(client, resource, "map020_临安城客栈一楼.map", 6, output, "182-jia-second-inn")
    interact_at(client, output, resource, "贾老实", jia, "183-jia-second-request")
    transition(client, resource, "map016_临安城.map", 1, output, "184-jia-second-inn-exit")
    transition(client, resource, "map023_临安城妓院.map", 5, output, "185-qiuyu-entry")
    source = checkpoint(client, output, "186-qiuyu-choice-source")
    ruby = quantity(source, "goods402_红玉.ini")
    client.save_or_load(0)
    state = interact_at(client, output, resource, "妓女3", qiuyu, "187-qiuyu-refusal", choices=[(qiuyu + ":14", 1)])
    if state["variables"].get("Eventqiuyu") != "2" or quantity(state, "goods402_红玉.ini") != ruby:
        raise AutomationError("Qiuyu refusal did not preserve inventory or set its native flag")
    interact_at(client, output, resource, "妓女3", qiuyu, "188-qiuyu-refusal-repeat")
    client.save_or_load(1)
    checkpoint(client, output, "189-qiuyu-refusal-source-slot1")
    load_checkpoint(client, 0)
    state = interact_at(client, output, resource, "妓女3", qiuyu, "190-qiuyu-accepted", choices=[(qiuyu + ":14", 0)])
    if state["variables"].get("Eventqiuyu") != "1" or quantity(state, "goods402_红玉.ini") != ruby + 1:
        raise AutomationError("Qiuyu acceptance did not grant exactly one ruby")
    state = interact_at(client, output, resource, "妓女3", qiuyu, "191-qiuyu-accepted-repeat")
    if quantity(state, "goods402_红玉.ini") != ruby + 1:
        raise AutomationError("Qiuyu granted a repeated ruby")
    client.save_or_load(2)
    checkpoint(client, output, "192-qiuyu-accepted-source-slot2")
    state = interact_at(client, output, resource, "妓院打手", guard, "193-guard-repeat-at-event100")
    if state["variables"].get("Event") != "105":
        raise AutomationError("Previously visited guard did not advance Event to 105")
    interact_at(client, output, resource, "妓院打手", guard, "194-guard-after-refusal")
    transition(client, resource, "map016_临安城.map", 1, output, "195-brothel-refusal-exit")
    client.save_or_load(4)
    checkpoint(client, output, "196-nangong-acrobat-source-slot4")
    state = transition(client, resource, "map016_临安城.map", 3, output, "197-nangong-native-acrobat")
    write_json(output / "197-nangong-nested-script-proof.json", script_proof(output, resource,
               "script/map/map023_临安城妓院/南宫彩虹对话.txt"))
    if state["variables"].get("Event") != "110" or quantity(state, "goods404_一杯清水.ini") != 1:
        raise AutomationError("Native Nangong scene did not return with one cup and Event 110")
    transition(client, resource, "map020_临安城客栈一楼.map", 6, output, "198-jia-delivery-entry")
    state = interact_at(client, output, resource, "贾老实", jia, "199-jia-joins")
    if state["variables"].get("Event") != "120" or quantity(state, "goods404_一杯清水.ini") != 0:
        raise AutomationError("Jia did not consume the cup and join at Event 120")
    interact_at(client, output, resource, "贾老实", jia, "200-jia-companion-reminder")
    transition(client, resource, "map016_临安城.map", 1, output, "201-jia-companion-city")
    client.save_or_load(6)
    checkpoint(client, output, "202-jia-native-companion-source-slot6")
    write_json(output / "jia-and-qiuyu-proof.json", dict(status="passed", Event="120",
               qiuyuBothOptions=True, rubyGrantedOnce=True, guardFirstAndRepeatedAtEvent100=True,
               nativeAcrobat=True, cupConsumed=True, sourceSlots=[0, 1, 2, 3, 4, 5, 6], cheatAssisted=False))


def prescription_request(client, output, resource):
    state = idle(client)
    if state["map"] != "map016_临安城.map" or state["variables"].get("Sub") != "120":
        raise AutomationError("Prescription request requires the native drunken-doctor clue")
    client.act("SetAutoDialogue", enabled=True)
    chen = "script/map/map016_临安城/临安城居民25之陈鹏对话.txt"
    drug = "script/map/map028_临安城药店/药店老板对话.txt"
    state = interact_at(client, output, resource, "陈鹏", chen, "210-chen-prescription-clue")
    if state["variables"].get("Sub") != "130":
        raise AutomationError("Chen's clue did not advance Sub to 130")
    seen = set()
    for attempt in range(12):
        state = interact_at(client, output, resource, "陈鹏", chen, f"211-chen-repeat-{attempt}")
        seen.add(state["variables"].get("Talkchenp"))
        if seen == {"0", "1"}:
            break
    if seen != {"0", "1"}:
        raise AutomationError("Chen's native random repeat variants remain incomplete")
    transition(client, resource, "map028_临安城药店.map", 9, output, "212-drugstore-entry")
    client.save_or_load(0)
    checkpoint(client, output, "213-drugstore-choice-source-slot0")
    state = interact_at(client, output, resource, "药店老板", drug, "214-drugstore-buy-menu",
                        choices=[(drug + ":22", 0)], shop_cancel=True)
    if state["variables"].get("Sub") != "130":
        raise AutomationError("Buying branch changed the prescription quest")
    load_checkpoint(client, 0)
    state = interact_at(client, output, resource, "药店老板", drug, "215-drugstore-prescription-request",
                        choices=[(drug + ":22", 1)])
    if state["variables"].get("Sub") != "140" or quantity(state, "goods211_千金藤.ini") != 0:
        raise AutomationError("Request without vine did not start its native retrieval quest")
    interact_at(client, output, resource, "药店老板", drug, "216-drugstore-no-vine-repeat", shop_cancel=True)
    transition(client, resource, "map016_临安城.map", 1, output, "217-drugstore-return")
    client.save_or_load(6)
    checkpoint(client, output, "218-prescription-vine-source-slot6")
    write_json(output / "prescription-request-proof.json", dict(status="passed", Sub="140",
               chenRandomVariants=sorted(seen), bothDrugstoreOptions=True, nativeShopCancellation=True,
               sourceSlots=[0, 6], cheatAssisted=False))


def bixia_first_rescue(client, output, resource):
    state = idle(client)
    on_island = (state["map"] == "map037_碧霞岛.map" and state["variables"].get("Event") == "130"
                 and (output / "225-native-bixia-boat-script-proof.json").exists())
    if not on_island and (state["map"] != "map016_临安城.map" or state["variables"].get("Event") != "120"):
        raise AutomationError("Bixia rescue requires the native Jia companion slot 6")
    client.act("SetAutoDialogue", enabled=True)
    if not on_island:
        transition(client, resource, "map035_临安城东.map", 2, output, "220-jia-east-outskirts")
        transition(client, resource, "map034_1_海边小渔村.map", 2, output, "221-jia-coastal-arrival")
        transition(client, resource, "map036_渔夫家.map", 2, output, "222-jia-old-wang-house")
        state = transition(client, resource, "map036_渔夫家.map", 2, output, "223-jia-secures-boat")
        if state["variables"].get("Event") != "125":
            raise AutomationError("Jia's native boat request did not advance Event to 125")
        transition(client, resource, "map034_1_海边小渔村.map", 1, output, "224-jia-dock-return")
        state = interact_at(client, output, resource, "渔夫老王",
                            "script/map/map034_1_海边小渔村/渔夫老王对话.txt", "225-native-bixia-boat")
        if state["map"] != "map037_碧霞岛.map" or state["variables"].get("Event") != "130":
            raise AutomationError("Old Wang's native boat did not reach Bixia at Event 130")
        client.save_or_load(0)
        checkpoint(client, output, "226-bixia-island-source-slot0")
    clear_enemies(client, output, "227-bixia-island-nearby-combat")
    transition(client, resource, "map038_碧霞岛山洞.map", 1, output, "228-bixia-cave-entry")
    client.save_or_load(1)
    checkpoint(client, output, "229-bixia-cave-source-slot1")
    transition(client, resource, "map038_碧霞岛山洞.map", 1, output, "230-bixia-cave-early-exit-refused")
    state = transition(client, resource, "map038_碧霞岛山洞.map", 4, output, "231-bixia-abductor-battle")
    if state["variables"].get("Event") != "140":
        raise AutomationError("Native abductor scene did not start Event 140 battle")
    client.save_or_load(2)
    checkpoint(client, output, "232-abductor-battle-source-slot2")
    transition(client, resource, "map038_碧霞岛山洞.map", 1, output, "233-abductor-battle-exit-refused")
    load_checkpoint(client, 2)
    state = clear_enemies(client, output, "234-native-abductor-combat")
    state = idle(client)
    if state["variables"].get("Event") != "150":
        raise AutomationError("Native abductor death did not finish at Event 150")
    write_json(output / "234-abductor-death-script-proof.json", script_proof(output, resource,
               "script/map/map038_碧霞岛山洞/采花贼死亡.txt"))
    client.save_or_load(3)
    checkpoint(client, output, "235-abductor-rescue-source-slot3")
    transition(client, resource, "map037_碧霞岛.map", 1, output, "236-bixia-night-island")
    before = checkpoint(client, output, "237-vine-before-pickup")
    vines = [row for row in before["targets"] if row["name"] == "千金藤"]
    if not vines:
        raise AutomationError("Native night island has no prescription vine")
    target = min(vines, key=lambda row: abs(row["position"]["x"] - before["player"]["position"]["x"]) * 2
                 + abs(row["position"]["y"] - before["player"]["position"]["y"]))
    state = interact_at(client, output, resource, "千金藤", "script/common/千金藤.txt", "238-vine-native-pickup",
                        (target["position"]["x"], target["position"]["y"]))
    if (quantity(state, "goods211_千金藤.ini") != quantity(before, "goods211_千金藤.ini") + 1
            or any(row["id"] == target["id"] for row in state["targets"])):
        raise AutomationError("Vine pickup did not grant one item and remove its object")
    client.save_or_load(4)
    checkpoint(client, output, "239-vine-and-night-source-slot4")
    state = interact_at(client, output, resource, "渔夫老王", "script/map/map037_碧霞岛/渔夫老王对话.txt", "240-native-island-overnight")
    if state["map"] != "map038_碧霞岛山洞.map" or state["variables"].get("Event") != "160":
        raise AutomationError("Native overnight did not awaken Zhang Linxin at Event 160")
    write_json(output / "240-overnight-script-proof.json", script_proof(output, resource,
               "script/map/map037_碧霞岛/过夜.txt"))
    interact_at(client, output, resource, "张琳心", "script/map/map038_碧霞岛山洞/张琳心对话.txt", "241-zhang-linxin-search-clue")
    transition(client, resource, "map038_碧霞岛山洞.map", 1, output, "242-linxin-rescue-exit-refused")
    client.save_or_load(6)
    checkpoint(client, output, "243-linxin-companion-source-slot6")
    write_json(output / "bixia-first-rescue-proof.json", dict(status="passed", Event="160",
               nativeBoat=True, abductorDeath=True, earlyAndBattleAndRescueExitsRefused=True,
               vinePickedOnce=True, nativeOvernight=True, sourceSlots=[0, 1, 2, 3, 4, 6], cheatAssisted=False))


def hemei_rescue(client, output, resource):
    state = idle(client)
    in_secret = (state["map"] == "map039_碧霞岛山洞秘道.map" and state["variables"].get("Event") == "160"
                 and (output / "252-secret-rescue-exit-refused-script-proof.json").exists())
    if not in_secret and (state["map"] != "map038_碧霞岛山洞.map" or state["variables"].get("Event") != "160"):
        raise AutomationError("Hemei rescue requires the native overnight companion slot 6")
    client.act("SetAutoDialogue", enabled=True)
    if not in_secret:
        transition(client, resource, "map039_碧霞岛山洞秘道.map", 2, output, "250-native-secret-passage")
        client.save_or_load(0)
        checkpoint(client, output, "251-secret-battle-source-slot0")
        transition(client, resource, "map039_碧霞岛山洞秘道.map", 1, output, "252-secret-rescue-exit-refused")
        load_checkpoint(client, 0)
    for step in range(50):
        state = idle(client)
        if state["map"] == "map034_1_海边小渔村.map" and state["variables"].get("Event") == "180":
            break
        if state["player"]["position"] == dict(x=3, y=42):
            break
        if any(row.get("hostile") and row.get("attackable") and row.get("visibleFromPlayer") for row in state["targets"]):
            try:
                clear_enemies(client, output, f"secret-route-combat-{step}-{time.time_ns()}", ranged=True)
            except AutomationError as error:
                if not any(reason in str(error) for reason in ("no_progress", "target_unreachable")):
                    raise
                checkpoint(client, output, f"secret-route-blocked-enemy-{step}-{time.time_ns()}")
            state = idle(client)
            if state["map"] != "map039_碧霞岛山洞秘道.map":
                break
        occupied = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]}
        path = reachable_trap(resource, state["map"], None, state["player"]["position"], occupied,
                              with_path=True, destination=(3, 42))
        write_json(output / f"secret-waypoint-path-{step}-{time.time_ns()}.json", dict(map=state["map"], path=path))
        point = next(point for point in reversed(path[1:3]) if point not in occupied)
        try:
            client.move(*point)
        except AutomationError as error:
            if not any(reason in str(error) for reason in ("blocked_destination", "no_progress")):
                raise
            checkpoint(client, output, f"secret-route-blocked-walk-{step}-{time.time_ns()}")
            continue
        checkpoint(client, output, f"secret-native-waypoint-{step}")
        if any(row.get("hostile") and row.get("attackable") and row.get("visibleFromPlayer") for row in client.observe()["targets"]):
            try:
                clear_enemies(client, output, f"secret-route-combat-{step}", ranged=True)
            except AutomationError as error:
                if not any(reason in str(error) for reason in ("no_progress", "target_unreachable")):
                    raise
                state = checkpoint(client, output, f"secret-route-blocked-enemy-{step}")
                write_json(output / f"secret-route-blocked-enemy-proof-{step}.json", dict(status="pending",
                           error=str(error), remaining=[row for row in state["targets"] if row.get("hostile")],
                           cheatAssisted=False))
    else:
        raise AutomationError("Secret passage walking exceeded its recorded route limit")
    state = clear_enemies(client, output, "253-secret-native-combat")
    state = idle(client)
    if (state["map"] != "map034_1_海边小渔村.map" or state["variables"].get("Event") != "180"
            or state["variables"].get("map039Enemy") != "4"):
        raise AutomationError("Four native killer deaths did not finish Hemei's return at Event 180")
    write_json(output / "253-secret-rescue-script-proof.json", script_proof(output, resource,
               "script/map/map039_碧霞岛山洞秘道/杀手死亡.txt"))
    client.save_or_load(1)
    checkpoint(client, output, "254-hemei-coastal-source-slot1")
    interact_at(client, output, resource, "渔夫老王", "script/map/map034_1_海边小渔村/渔夫老王对话.txt", "255-old-wang-rescue-reminder")
    transition(client, resource, "map035_临安城东.map", 1, output, "256-hemei-outskirts-return")
    transition(client, resource, "map016_临安城.map", 1, output, "257-hemei-linan-return")
    client.save_or_load(6)
    checkpoint(client, output, "258-hemei-native-city-source-slot6")
    write_json(output / "hemei-rescue-proof.json", dict(status="passed", Event="180", map039Enemy="4",
               nativeSecretPassage=True, rescueExitRefused=True, nativeReturn=True,
               sourceSlots=[0, 1, 6], cheatAssisted=False))


def hemei_marriage(client, output, resource):
    source = "script/map/map026_临安城何员外家/trap02.txt"
    state = client.observe(VARIABLES)
    if (state.get("map") == "map026_临安城何员外家.map" and state.get("inEvent")
            and state.get("variables", {}).get("Event") == "180"
            and (output / "276-hemei-continuing-story-source-slot6.json").exists()):
        state = idle(client, choices=[(source + ":44", 0)], output=output, resource=resource,
                     expected_terminal=source, final_dialogue="独孤剑与何梅结为夫妻以后")
        write_json(output / "277-hemei-marriage-title-script-proof.json", script_proof(output, resource, source))
        checkpoint(client, output, "277-hemei-marriage-title")
        return hemei_marriage_finish(client, output, resource, state, source)
    state = idle(client)
    if state["map"] != "map016_临安城.map" or state["variables"].get("Event") != "180":
        raise AutomationError("Hemei marriage requires the native rescue city slot 6")
    client.act("SetAutoDialogue", enabled=True)
    transition(client, resource, "map026_临安城何员外家.map", 12, output, "270-hemei-home-entry")
    client.save_or_load(0)
    checkpoint(client, output, "271-hemei-proposal-source-slot0")
    transition(client, resource, "map026_临安城何员外家.map", 1, output, "272-hemei-home-early-exit-refused")
    load_checkpoint(client, 0)
    state = transition(client, resource, "map026_临安城何员外家.map", 2, output, "273-hemei-marriage-refused",
                       choices=[(source + ":44", 1)])
    if state["variables"].get("Event") != "190":
        raise AutomationError("Marriage refusal did not advance the continuing story to Event 190")
    client.save_or_load(1)
    checkpoint(client, output, "274-hemei-refusal-source-slot1")
    transition(client, resource, "map016_临安城.map", 1, output, "275-hemei-refusal-city-return")
    client.save_or_load(6)
    checkpoint(client, output, "276-hemei-continuing-story-source-slot6")
    load_checkpoint(client, 0)
    state = transition(client, resource, "Title", 2, output, "277-hemei-marriage-title",
                       choices=[(source + ":44", 0)], expected_terminal=source,
                       final_dialogue="独孤剑与何梅结为夫妻以后")
    return hemei_marriage_finish(client, output, resource, state, source)


def hemei_marriage_finish(client, output, resource, state, source):
    proof = script_proof(output, resource, source)
    execution = [row for row in records(output) if row.get("executionId") == proof["executionId"]]
    dialogues = [p for p in output.glob("ending-dialogue-*.json")
                 if "独孤剑与何梅结为夫妻以后" in json.loads(p.read_text(encoding="utf-8")).get("dialogue", {}).get("text", "")]
    videos = [p for p in output.glob("ending-video-*.json")
              if Path(json.loads(p.read_text(encoding="utf-8")).get("video", "")).name.casefold() == "over.wmv"]
    if (not title_visible(state) or not dialogues or not videos
            or not any(row.get("apiName") == "playmovie" for row in execution)):
        raise AutomationError("Marriage ending lacks its complete text, native video, or title")
    write_json(output / "ending-proof-E01.json", dict(status="passed", endingId="E01", storyEnding=True,
               sourceSlot=0, expectedTerminal=source, script=proof, finalDialogueFile=str(dialogues[-1]),
               videoFile=str(videos[-1]), titleFile=str(output / "277-hemei-marriage-title.json"),
               videoSkipped=False, cheatAssisted=False))
    load_checkpoint(client, 6)
    write_json(output / "hemei-marriage-proof.json", dict(status="passed", bothProposalOptions=True,
               marriageEnding="E01", continuingEvent="190", sourceSlots=[0, 1, 6], cheatAssisted=False))


def prescription_complete(client, output, resource):
    state = idle(client)
    if (state["map"] != "map016_临安城.map" or state["variables"].get("Sub") != "140"
            or quantity(state, "goods211_千金藤.ini") < 1):
        raise AutomationError("Prescription completion requires the native Bixia vine")
    client.act("SetAutoDialogue", enabled=True)
    transition(client, resource, "map028_临安城药店.map", 9, output, "280-prescription-vine-return")
    client.save_or_load(0)
    before = checkpoint(client, output, "281-vine-exchange-source-slot0")
    state = interact_at(client, output, resource, "药店老板", "script/map/map028_临安城药店/药店老板对话.txt", "282-native-prescription-exchange")
    if (state["variables"].get("Sub") != "150"
            or quantity(state, "goods211_千金藤.ini") != quantity(before, "goods211_千金藤.ini") - 1
            or quantity(state, "goods411_一张药方.ini") != quantity(before, "goods411_一张药方.ini") + 1):
        raise AutomationError("Native vine exchange did not give exactly one prescription")
    state = interact_at(client, output, resource, "药店老板", "script/map/map028_临安城药店/药店老板对话.txt", "283-prescription-exchange-repeat", shop_cancel=True)
    if quantity(state, "goods411_一张药方.ini") != quantity(before, "goods411_一张药方.ini") + 1:
        raise AutomationError("Drugstore repeated its quest reward")
    client.save_or_load(1)
    checkpoint(client, output, "284-prescription-obtained-source-slot1")
    transition(client, resource, "map016_临安城.map", 1, output, "285-drugstore-prescription-exit")
    transition(client, resource, "map017_临安城酒楼.map", 4, output, "286-prescription-doctor-entry")
    client.save_or_load(2)
    before = checkpoint(client, output, "287-doctor-choice-source-slot2")
    source = "script/map/map017_临安城酒楼/醉郎中.txt"
    state = interact_at(client, output, resource, "醉郎中", source, "288-prescription-kept", choices=[(source + ":32", 1)])
    if (quantity(state, "goods411_一张药方.ini") != quantity(before, "goods411_一张药方.ini")
            or state["variables"].get("Talkzuilangzhong") != "1" or state["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Keeping the prescription changed its reward or inventory")
    client.save_or_load(3)
    checkpoint(client, output, "289-prescription-kept-source-slot3")
    load_checkpoint(client, 2)
    state = interact_at(client, output, resource, "醉郎中", source, "290-prescription-returned", choices=[(source + ":32", 0)])
    if (quantity(state, "goods411_一张药方.ini") != quantity(before, "goods411_一张药方.ini") - 1
            or state["variables"].get("Talkzuilangzhong") != "2"
            or state["player"]["money"] != before["player"]["money"] + 2000):
        raise AutomationError("Returning the prescription did not grant its native two-thousand reward")
    state = interact_at(client, output, resource, "醉郎中", source, "291-prescription-reward-repeat")
    if state["player"]["money"] != before["player"]["money"] + 2000:
        raise AutomationError("Doctor repeated his silver reward")
    client.save_or_load(4)
    checkpoint(client, output, "292-prescription-returned-source-slot4")
    transition(client, resource, "map016_临安城.map", 1, output, "293-prescription-tavern-exit")
    client.save_or_load(6)
    checkpoint(client, output, "294-native-prescription-complete-slot6")
    write_json(output / "prescription-complete-proof.json", dict(status="passed", Sub="150",
               vineCollectedNaturally=True, vineExchangedOnce=True, doctorBothOptions=True,
               silverReward=2000, repeatedRewardPrevented=True, sourceSlots=[0, 1, 2, 3, 4, 6], cheatAssisted=False))


def rumeng_and_linxin(client, output, resource):
    state = idle(client)
    if state["map"] != "map016_临安城.map" or state["variables"].get("Event") != "190":
        raise AutomationError("Rumeng duel requires the native marriage-refusal city checkpoint")
    client.act("SetAutoDialogue", enabled=True)
    guard = "script/map/map016_临安城/张府卫兵对话.txt"
    interact_at(client, output, resource, "宋兵", guard, "300-zhang-home-event190", (40, 81))
    transition(client, resource, "map015_临安城南.map", 1, output, "301-native-rumeng-south-entry")
    client.save_or_load(0)
    checkpoint(client, output, "302-rumeng-duel-source-slot0")
    state = transition(client, resource, "map015_临安城南.map", 3, output, "303-native-rumeng-duel")
    if state["variables"].get("Event") != "195":
        raise AutomationError("Rumeng encounter did not start its native duel")
    client.save_or_load(1)
    checkpoint(client, output, "304-rumeng-battle-source-slot1")
    transition(client, resource, "map015_临安城南.map", 1, output, "305-rumeng-city-exit-refused")
    load_checkpoint(client, 1)
    transition(client, resource, "map015_临安城南.map", 2, output, "306-rumeng-road-exit-refused")
    load_checkpoint(client, 1)
    state = clear_enemies(client, output, "307-rumeng-native-combat")
    state = idle(client)
    if (state["map"] != "map021_临安城客栈二楼.map" or state["variables"].get("Event") != "198"
            or quantity(state, "goods401_血书.ini") != 0):
        raise AutomationError("Native Rumeng victory did not finish the scripted stolen-letter aftermath")
    write_json(output / "307-rumeng-aftermath-script-proof.json", script_proof(output, resource,
               "script/map/map015_临安城南/张如梦打败.txt"))
    client.save_or_load(2)
    checkpoint(client, output, "308-stolen-letter-source-slot2")
    transition(client, resource, "map020_临安城客栈一楼.map", 1, output, "309-inn-aftermath-downstairs")
    transition(client, resource, "map020_临安城客栈一楼.map", 1, output, "310-inn-aftermath-exit-refused")
    inn = "script/map/map020_临安城客栈一楼/客栈掌柜对话.txt"
    state = interact_at(client, output, resource, "客栈掌柜", inn, "311-inn-rescuer-clue")
    if state["variables"].get("Event") != "200":
        raise AutomationError("Inn rescuer clue did not advance Event to 200")
    interact_at(client, output, resource, "客栈掌柜", inn, "312-inn-rescuer-repeat")
    transition(client, resource, "map016_临安城.map", 1, output, "313-inn-rescuer-city")
    state = interact_at(client, output, resource, "宋兵", guard, "314-zhang-home-event200", (40, 81))
    if state["variables"].get("Ask") != "1":
        raise AutomationError("Zhang residence inquiry did not set its native clue flag")
    interact_at(client, output, resource, "宋兵", guard, "315-zhang-home-repeat", (40, 81))
    client.save_or_load(4)
    checkpoint(client, output, "316-linxin-search-source-slot4")
    transition(client, resource, "map015_临安城南.map", 1, output, "317-linxin-south-reentry")
    state = transition(client, resource, "map015_临安城南.map", 4, output, "318-linxin-native-reunion")
    if state["variables"].get("Event") != "210":
        raise AutomationError("Native Linxin reunion did not advance Event to 210")
    interact_at(client, output, resource, "张琳心", "script/map/map015_临安城南/张琳心对话.txt", "319-linxin-family-reminder")
    transition(client, resource, "map015_临安城南.map", 1, output, "320-wuyi-departure-city-return-refused")
    client.save_or_load(6)
    checkpoint(client, output, "321-native-wuyi-second-visit-source-slot6")
    write_json(output / "rumeng-and-linxin-proof.json", dict(status="passed", Event="210",
               nativeDuelVictory=True, duelBothExitsRefused=True, letterStolenByStory=True,
               rescuerClue=True, zhangHomeFirstAndRepeat=True, nativeLinxinReunion=True,
               sourceSlots=[0, 1, 2, 4, 6], cheatAssisted=False))


def wuyi_second_visit(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    resuming_battle = (state["map"] == "map011_武夷山顶.map" and state["variables"].get("Event") in ("215", "220")
                       and (output / "334-wuyi-thirty-killers-source-slot0.json").exists())
    client.act("SetAutoDialogue", enabled=True)
    if not resuming_battle:
        if state["map"] != "map015_临安城南.map" or state["variables"].get("Event") != "210":
            raise AutomationError("Second Wuyi visit requires the native Linxin reunion checkpoint")
        transition(client, resource, "map005_林间小道.map", 2, output, "330-linxin-road-departure")
        transition(client, resource, "map006_武夷山脚.map", 2, output, "331-second-wuyi-foothill")
        transition(client, resource, "map007_武夷山九猴洞.map", 2, output, "332-second-nine-monkeys-entry")
        transition(client, resource, "map011_武夷山顶.map", 2, output, "333-native-wuyi-killer-ambush")
        state = idle(client)
        if state["variables"].get("Event") != "215":
            raise AutomationError("Native Wuyi ambush did not start Event 215")
        client.save_or_load(0)
        checkpoint(client, output, "334-wuyi-thirty-killers-source-slot0")
        transition(client, resource, "map011_武夷山顶.map", 1, output, "335-wuyi-ambush-descent-refused")
        load_checkpoint(client, 0)
    state = walk_required_battle(client, output, resource, "220", "336-wuyi")
    if state["variables"].get("fight011") != "30":
        raise AutomationError("Wuyi battle did not complete all thirty native death callbacks")
    write_json(output / "336-wuyi-final-death-script-proof.json", script_proof(output, resource,
               "script/map/map011_武夷山顶/杀手死亡.txt"))
    client.save_or_load(1)
    checkpoint(client, output, "337-wuyi-ambush-won-source-slot1")
    transition(client, resource, "map011_武夷山顶.map", 1, output, "338-wuyi-before-hall-descent-refused")
    transition(client, resource, "map011_武夷山顶.map", 3, output, "339-wuyi-before-hall-forbidden-cave-refused")
    transition(client, resource, "map012_武夷山大厅.map", 2, output, "340-wuyi-wounded-hall")
    state = interact_at(client, output, resource, "张林", "script/map/map012_武夷山大厅/武夷派大弟子张林对话.txt", "341-zhanglin-last-clue")
    if state["variables"].get("Event") != "230":
        raise AutomationError("Zhang Lin's last clue did not advance Event to 230")
    client.save_or_load(2)
    checkpoint(client, output, "342-zhanglin-last-clue-source-slot2")
    transition(client, resource, "map011_武夷山顶.map", 1, output, "343-wuyi-last-clue-hall-return")
    transition(client, resource, "map011_武夷山顶.map", 1, output, "344-wuyi-rescue-descent-refused")
    transition(client, resource, "map014_武夷山禁地外洞.map", 3, output, "345-native-wuyi-forbidden-cave")
    client.save_or_load(6)
    checkpoint(client, output, "346-native-stone-door-source-slot6")
    write_json(output / "wuyi-second-visit-proof.json", dict(status="passed", Event="230", fight011="30",
               thirtyDeathsVerified=True, earlyDescentAndCaveRefused=True, zhanglinLastClue=True,
               nativeForbiddenCave=True, sourceSlots=[0, 1, 2, 6], cheatAssisted=False))


def stone_door_and_liuzhongyuan(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if (state["map"] == "map008_武夷山禁地内洞1.map" and state["variables"].get("Event") == "235"
            and (output / "367-liu-chief-source-slot5.json").exists()):
        return finish_liuzhongyuan(client, output, resource)
    if state["map"] != "map014_武夷山禁地外洞.map" or state["variables"].get("Event") != "230":
        raise AutomationError("Stone-door route requires the native forbidden-cave checkpoint")
    client.save_or_load(0)
    checkpoint(client, output, "350-stone-door-source-slot0")
    transition(client, resource, "map014_武夷山禁地外洞.map", 1, output, "351-liu-rescue-outer-exit-refused")
    stone = "script/map/map014_武夷山禁地外洞/密洞石门.txt"
    site = stone + ":7"
    state = interact_at(client, output, resource, "武夷密洞石门", stone, "352-stone-door-refused", choices=((site, 1),))
    if state["map"] != "map014_武夷山禁地外洞.map" or state["variables"].get("Jiguan") not in ("", "0"):
        raise AutomationError("Refusing the stone door unexpectedly opened the cave")
    client.save_or_load(1)
    checkpoint(client, output, "353-stone-refusal-source-slot1")
    load_checkpoint(client, 0)
    state = interact_at(client, output, resource, "武夷密洞石门", stone, "354-stone-door-opened", choices=((site, 0),))
    if state["map"] != "map009_武夷山禁地内洞2.map" or state["variables"].get("Jiguan") != "1":
        raise AutomationError("Pushing the stone door did not enter its native cave")
    client.save_or_load(2)
    checkpoint(client, output, "355-stone-inner-source-slot2")
    interact_at(client, output, resource, "张琳心", "script/map/map009_武夷山禁地内洞2/张琳心对话.txt", "356-stone-linxin-cave-dialogue")
    interact_at(client, output, resource, "武夷密洞书信", "script/map/map009_武夷山禁地内洞2/trap02.txt", "357-stone-letter-to-ash")
    state = idle(client)
    if any(row["name"] == "武夷密洞书信" for row in state["targets"]):
        raise AutomationError("Native stone-cave letter was not removed")
    before = state
    state = interact_at(client, output, resource, "宝箱", "script/map/map009_武夷山禁地内洞2/宝箱.txt", "358-stone-cave-chest")
    goods = ("goods012_绿松石挂链.ini", "goods018_珊瑚挂链.ini")
    if state["player"]["money"] != before["player"]["money"] + 100 or any(quantity(state, file) != quantity(before, file) + 1 for file in goods):
        raise AutomationError("Stone-cave chest rewards differ from its native source")
    chest = next(row for row in state["targets"] if row["name"] == "宝箱")
    action = client.submit("Interact", generation=state["generation"], targetId=chest["id"], running=True)
    rejected = client.request("GetActionStatus", actionId=action)
    if rejected["status"] != "failed" or rejected["reason"] != "action_rejected":
        raise AutomationError("Empty stone-cave chest accepted a repeated interaction")
    repeated = checkpoint(client, output, "359-stone-cave-empty-chest")
    if repeated["player"]["money"] != state["player"]["money"] or any(quantity(repeated, file) != quantity(state, file) for file in goods):
        raise AutomationError("Repeated stone-cave chest interaction changed rewards")
    state = transition(client, resource, "map009_武夷山禁地内洞2.map", 3, output, "360-native-stone-wall-magic")
    if not any(row["file"] == "magic009_风雷九洲.ini" and row["level"] == 2 for row in state["magic"]):
        raise AutomationError("Native wall inscription did not teach level-two Fenglei Jiuzhou")
    client.save_or_load(3)
    checkpoint(client, output, "361-native-stone-wall-magic-source-slot3")
    transition(client, resource, "map014_武夷山禁地外洞.map", 1, output, "362-stone-cave-return")
    transition(client, resource, "map008_武夷山禁地内洞1.map", 2, output, "363-native-liu-rescue-cave")
    client.save_or_load(4)
    checkpoint(client, output, "364-liu-twenty-eight-killers-source-slot4")
    transition(client, resource, "map008_武夷山禁地内洞1.map", 1, output, "365-liu-killers-exit-refused")
    state = walk_required_battle(client, output, resource, "235", "366-liu")
    if state["variables"].get("fight008") != "28":
        raise AutomationError("Liu rescue did not complete its twenty-eight native killer deaths")
    write_json(output / "366-liu-last-killer-script-proof.json", script_proof(output, resource, "script/map/map008_武夷山禁地内洞1/杀手死亡.txt"))
    client.save_or_load(5)
    checkpoint(client, output, "367-liu-chief-source-slot5")
    transition(client, resource, "map008_武夷山禁地内洞1.map", 1, output, "368-liu-chief-exit-refused")
    finish_liuzhongyuan(client, output, resource)


def finish_liuzhongyuan(client, output, resource, complete_chapter=True):
    state = walk_required_battle(client, output, resource, "240", "369-liu-chief")
    write_json(output / "369-liu-chief-death-script-proof.json", script_proof(output, resource, "script/map/map008_武夷山禁地内洞1/杀手头目死亡.txt"))
    interact_at(client, output, resource, "张琳心", "script/map/map008_武夷山禁地内洞1/张琳心对话.txt", "370-liu-last-words-companion")
    transition(client, resource, "map014_武夷山禁地外洞.map", 1, output, "371-native-liu-rescue-return")
    client.save_or_load(6)
    checkpoint(client, output, "372-native-zhangfeng-return-source-slot6")
    if complete_chapter:
        write_json(output / "stone-door-and-liuzhongyuan-proof.json", dict(status="passed", Event="240", fight008="28",
                   stoneBothOptions=True, nativeLetterRemoved=True, chestRewardOnce=True, wallMagicLevel=2,
                   nativeChiefDeath=True, liuLastWords=True, sourceSlots=list(range(7)), cheatAssisted=False))


def linan_blacksmith_aftermath(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if (state["map"] == "map016_临安城.map" and state["variables"].get("Event") == "255"
            and (output / "389-second-lvwencai-source-slot2.json").exists()):
        return finish_blacksmith_aftermath(client, output, resource)
    if state["map"] != "map014_武夷山禁地外洞.map" or state["variables"].get("Event") != "240":
        raise AutomationError("Blacksmith aftermath requires the native Liu Zhongyuan checkpoint")
    transition(client, resource, "map011_武夷山顶.map", 1, output, "380-liu-rescue-summit-return")
    transition(client, resource, "map007_武夷山九猴洞.map", 1, output, "381-liu-rescue-nine-monkeys-return")
    transition(client, resource, "map006_武夷山脚.map", 1, output, "382-liu-rescue-foothill-return")
    transition(client, resource, "map005_林间小道.map", 1, output, "383-zhangfeng-road-return")
    client.save_or_load(0)
    checkpoint(client, output, "384-zhangfeng-road-source-slot0")
    state = transition(client, resource, "map005_林间小道.map", 4, output, "385-native-zhangfeng-returns-letter")
    if state["variables"].get("Event") != "250" or quantity(state, "goods401_血书.ini") != 1:
        raise AutomationError("Native Zhang Feng road encounter did not return the letter")
    client.save_or_load(1)
    checkpoint(client, output, "386-native-returned-letter-source-slot1")
    transition(client, resource, "map015_临安城南.map", 3, output, "387-blacksmith-south-entry")
    state = transition(client, resource, "map016_临安城.map", 1, output, "388-native-blacksmith-murder")
    if state["variables"].get("Event") != "255":
        raise AutomationError("Native blacksmith murder did not start the second Lvwencai battle")
    write_json(output / "388-blacksmith-murder-script-proof.json", script_proof(output, resource, "script/map/map016_临安城/段铁匠遇害.txt"))
    client.save_or_load(2)
    checkpoint(client, output, "389-second-lvwencai-source-slot2")
    transition(client, resource, "map016_临安城.map", 11, output, "390-zhang-home-before-lvwencai-refused")
    load_checkpoint(client, 2)
    finish_blacksmith_aftermath(client, output, resource)

def finish_blacksmith_aftermath(client, output, resource):
    state = walk_required_battle(client, output, resource, "23", "391-lvwencai", progress_variable="fight016")
    if any(row.get("hostile") and row.get("attackable") for row in state["targets"]):
        raise AutomationError("Lvwencai victory left required enemies alive")
    write_json(output / "391-lvwencai-final-death-script-proof.json", script_proof(output, resource, "script/map/map016_临安城/吕文才死亡.txt"))
    client.save_or_load(3)
    checkpoint(client, output, "392-blacksmith-last-words-source-slot3")
    before = state
    state = interact_at(client, output, resource, "段铁匠", "script/map/map016_临安城/段铁匠尸体脚本.txt", "393-native-blacksmith-last-words")
    rewards = {"goods061_烟火霹雳弹.ini": 3, "goods014_兽骨项圈.ini": 1, "goods207_十年佳酿.ini": 5}
    if (state["variables"].get("Event") != "256" or any(quantity(state, file) != quantity(before, file) + count for file, count in rewards.items())
            or not any(row["file"] == "magic056_烟火霹雳弹.ini" for row in state["magic"])):
        raise AutomationError("Blacksmith's last words did not apply the native rewards and Event 256")
    corpse = next(row for row in state["targets"] if row["name"] == "段铁匠")
    action = client.submit("Interact", generation=state["generation"], targetId=corpse["id"], running=True)
    rejected = client.request("GetActionStatus", actionId=action)
    if rejected["status"] != "failed" or rejected["reason"] != "action_rejected":
        raise AutomationError("Blacksmith corpse accepted a repeated reward interaction")
    repeated = checkpoint(client, output, "394-blacksmith-corpse-repeat")
    if any(quantity(repeated, file) != quantity(state, file) for file in rewards):
        raise AutomationError("Blacksmith corpse duplicated its rewards")
    client.save_or_load(6)
    checkpoint(client, output, "395-native-zhang-home-source-slot6")
    write_json(output / "linan-blacksmith-aftermath-proof.json", dict(status="passed", Event="256", fight016="23",
               nativeReturnedLetter=True, nativeBlacksmithMurder=True, secondLvwencaiVictory=True,
               lastWordsRewardsOnce=True, sourceSlots=[0, 1, 2, 3, 6], cheatAssisted=False))


def zhang_home_and_nangong(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if (state["map"] == "map029_临安城张府.map" and state["variables"].get("Event") == "256"
            and (output / "401-zhang-home-battle-source-slot0.json").exists()):
        if not (output / "403-zhang-home-battle-secret-exit-refused-script-proof.json").exists():
            load_checkpoint(client, 0)
            zhang_home_secret_refusal(client, output, resource)
            load_checkpoint(client, 0)
        return finish_zhang_home(client, output, resource)
    if state["map"] != "map016_临安城.map" or state["variables"].get("Event") != "256":
        raise AutomationError("Zhang residence requires the native blacksmith aftermath checkpoint")
    transition(client, resource, "map029_临安城张府.map", 11, output, "400-native-zhang-home-battle")
    client.save_or_load(0)
    checkpoint(client, output, "401-zhang-home-battle-source-slot0")
    transition(client, resource, "map029_临安城张府.map", 1, output, "402-zhang-home-battle-front-exit-refused")
    load_checkpoint(client, 0)
    zhang_home_secret_refusal(client, output, resource)
    load_checkpoint(client, 0)
    finish_zhang_home(client, output, resource)

def zhang_home_secret_refusal(client, output, resource):
    from run_yycs_gameplay import trap_points
    before = records(output)[-1]["sequence"]
    blocked_points = set()
    for attempt in range(50):
        state = idle(client)
        if state["map"] != "map029_临安城张府.map" or state["variables"].get("Event") != "256":
            raise AutomationError("Zhang residence battle changed before the secret-door refusal")
        position = state["player"]["position"]
        if (position["x"], position["y"]) in trap_points(resource, state["map"], 2):
            break
        try:
            path = reachable_trap(resource, state["map"], 2, position, with_path=True, avoid=blocked_points)
        except AutomationError as error:
            if "No connected trap" not in str(error) or not blocked_points:
                raise
            # Soldiers move and disappear after death; earlier occupied steps
            # cannot be treated as permanent map obstacles.
            blocked_points.clear()
            continue
        next_point = path[min(2, len(path) - 1)]
        try:
            client.move(*next_point, running=False, timeout=12)
        except AutomationError as error:
            if not any(reason in str(error) for reason in ("no_progress", "action_timeout", "blocked_destination")):
                raise
            state = checkpoint(client, output, f"403-secret-blocked-step-{attempt}")
            blocked_points.add(next_point)
            visible = [row for row in state["targets"] if row.get("hostile") and row.get("attackable") and row.get("visibleFromPlayer")]
            if not visible:
                continue
            position = state["player"]["position"]
            target = min(visible, key=lambda row: abs(row["position"]["x"] - position["x"]) * 2 + abs(row["position"]["y"] - position["y"]))
            client.act("StartCombat", timeout=40, generation=state["generation"], targetId=target["id"], kills=1, skills=[], timeoutMs=35000)
            blocked_points.clear()
        checkpoint(client, output, f"403-secret-native-step-{attempt}")
    else:
        raise AutomationError("Zhang residence secret-door approach exceeded its walking limit")
    source = "script/map/map029_临安城张府/trap02.txt"
    write_json(output / "403-zhang-home-battle-secret-exit-refused-script-proof.json", script_proof(output, resource, source, before))
    checkpoint(client, output, "403-zhang-home-battle-secret-exit-refused")


def finish_zhang_home(client, output, resource):
    state = walk_required_battle(client, output, resource, "260", "404-zhang-home")
    if state["variables"].get("fight029") != "10":
        raise AutomationError("Zhang residence did not complete its ten native soldier deaths")
    write_json(output / "404-zhang-home-final-death-script-proof.json", script_proof(output, resource, "script/map/map029_临安城张府/宋兵死亡.txt"))
    client.save_or_load(1)
    checkpoint(client, output, "405-zhang-home-won-source-slot1")
    for number, value in enumerate((1, 3, 4, 4)):
        state = interact_at(client, output, resource, "张琳心", "script/map/map029_临安城张府/张琳心对话.txt", f"406-zhang-home-linxin-{number}")
        if state["variables"].get("Talkmap029") != str(value):
            raise AutomationError("Native Zhang residence companion dialogue counter differs from the source")
    for number, value in enumerate((1, 2, 2, 2)):
        state = interact_at(client, output, resource, "蒋伯", "script/map/map029_临安城张府/蒋伯对话.txt", f"407-zhang-home-jiangbo-{number}")
        if state["variables"].get("Talkjiangbo") != str(value):
            raise AutomationError("Native Jiang Bo repeat counter differs from the source")
    for number, (position, filename, goods) in enumerate((((9, 15), "宝箱1.txt", ("goods044_霞影纱衣.ini",)),
                                                         ((2, 23), "宝箱.txt", ("goods023_金刚护手.ini", "goods029_茉莉花扣.ini")))):
        before = idle(client)
        state = interact_at(client, output, resource, "宝箱", "script/map/map029_临安城张府/" + filename, f"408-zhang-home-chest-{number}", position)
        if any(quantity(state, file) != quantity(before, file) + 1 for file in goods):
            raise AutomationError("Zhang residence chest did not provide its exact native goods")
        chest = next(row for row in state["targets"] if row["name"] == "宝箱" and row["position"] == dict(x=position[0], y=position[1]))
        action = client.submit("Interact", generation=state["generation"], targetId=chest["id"], running=True)
        rejected = client.request("GetActionStatus", actionId=action)
        if rejected["status"] != "failed" or rejected["reason"] != "action_rejected":
            raise AutomationError("Empty Zhang residence chest accepted a repeated activation")
        repeated = checkpoint(client, output, f"409-zhang-home-empty-chest-{number}")
        if any(quantity(repeated, file) != quantity(state, file) for file in goods):
            raise AutomationError("Zhang residence chest repeated its rewards")
    client.save_or_load(2)
    checkpoint(client, output, "410-zhang-home-secret-route-source-slot2")
    transition(client, resource, "map029_临安城张府.map", 1, output, "411-zhang-home-front-exit-refused")
    transition(client, resource, "map033_秘道.map", 2, output, "412-native-zhang-home-secret-route")
    client.save_or_load(3)
    checkpoint(client, output, "413-secret-route-source-slot3")
    transition(client, resource, "map033_秘道.map", 1, output, "414-secret-route-home-return-refused")
    clear_enemies(client, output, "415-secret-route-nearby-enemies", skills=())
    transition(client, resource, "map034_海边小树林.map", 2, output, "416-native-nangong-woods")
    client.save_or_load(4)
    checkpoint(client, output, "417-nangong-woods-source-slot4")
    state = transition(client, resource, "map034_海边小树林.map", 3, output, "418-native-zhangfeng-nangong-duel")
    if state["variables"].get("Event") != "265":
        raise AutomationError("Native Zhang Feng and Nangong duel did not start Event 265")
    client.save_or_load(6)
    checkpoint(client, output, "419-native-kill-zhangfeng-source-slot6")
    write_json(output / "zhang-home-and-nangong-proof.json", dict(status="passed", Event="265", fight029="10",
               nativeSoldierDeaths=True, jiangboRepeatedCounter=2, linxinRepeatCounter=4,
               chestsRewardOnce=True, nativeSecretRoute=True, nativeNangongDuel=True,
               sourceSlots=[0, 1, 2, 3, 4, 6], cheatAssisted=False))


def defeat_nangong(client, output, resource, option):
    source = "script/map/map034_海边小树林/打败南宫灭.txt"
    for attempt in range(12):
        state = idle(client, choices=((source + ":53", option),), output=output, resource=resource)
        if state["variables"].get("Event") == "270":
            return state
        boss = next(row for row in state["targets"] if row["name"] == "南宫灭" and row.get("hostile") and row.get("attackable"))
        try:
            client.act("StartCombat", timeout=65, generation=state["generation"], targetId=boss["id"],
                       kills=1, skills=[0], allowMeleeFallback=False, timeoutMs=60000)
        except AutomationError as error:
            if not any(reason in str(error) for reason in ("no_progress", "configured_skill_unavailable")):
                raise
            state = checkpoint(client, output, f"433-nangong-option-{option}-stalled-{attempt}")
            point = boss["position"]
            offsets = ((3, 4), (-3, 4), (3, -4), (-3, -4))
            moved = False
            for dx, dy in offsets[attempt % 4:] + offsets[:attempt % 4]:
                destination = (point["x"] + dx, point["y"] + dy)
                try:
                    reachable_trap(resource, state["map"], None, state["player"]["position"], destination=destination)
                    client.move(*destination, timeout=15)
                    current = checkpoint(client, output, f"433-nangong-option-{option}-native-flank-{attempt}")
                    if current["player"]["position"] != state["player"]["position"]:
                        moved = True
                        break
                except AutomationError as movement_error:
                    if not any(reason in str(movement_error) for reason in ("No connected trap", "no_progress", "blocked_destination", "action_timeout")):
                        raise
                    checkpoint(client, output, f"433-nangong-option-{option}-blocked-flank-{attempt}-{dx}-{dy}")
            if not moved:
                raise AutomationError("Nangong battle has no verified normal flanking movement")
    raise AutomationError("Nangong battle exceeded its normal flanking limit")


def kill_zhangfeng_choice(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if state["map"] != "map034_海边小树林.map" or state["variables"].get("Event") != "265":
        raise AutomationError("Zhang Feng choice requires the native Nangong battle checkpoint")
    source_end = int(state["variables"]["End"])
    client.save_or_load(0)
    checkpoint(client, output, "430-nangong-common-source-slot0")
    transition(client, resource, "map034_海边小树林.map", 1, output, "431-nangong-secret-exit-refused")
    load_checkpoint(client, 0)
    transition(client, resource, "map034_海边小树林.map", 2, output, "432-nangong-village-exit-refused")
    source = "script/map/map034_海边小树林/打败南宫灭.txt"
    for option, increment in ((0, 10), (1, 20)):
        load_checkpoint(client, 0)
        state = defeat_nangong(client, output, resource, option)
        if (state["map"] != "map034_海边小树林.map" or state["variables"].get("Event") != "270"
                or int(state["variables"].get("End", "0")) != source_end + increment):
            raise AutomationError("Native Zhang Feng choice did not apply its exact ending increment")
        write_json(output / f"433-zhangfeng-option-{option}-script-proof.json", script_proof(output, resource, source))
        client.save_or_load(option + 1)
        checkpoint(client, output, f"434-zhangfeng-option-{option}-source-slot{option + 1}")
    load_checkpoint(client, 2)
    transition(client, resource, "map034_海边小树林.map", 1, output, "435-zhangfeng-aftermath-secret-exit-refused")
    client.save_or_load(6)
    checkpoint(client, output, "436-native-mercy-main-route-source-slot6")
    write_json(output / "kill-zhangfeng-choice-proof.json", dict(status="passed", Event="270", sourceEnd=source_end,
               firstOptionEnd=source_end + 10, secondOptionEnd=source_end + 20, nativeNangongVictories=2,
               bothChoices=True, sourceSlots=[0, 1, 2, 6], cheatAssisted=False))


def furong_and_shitang(client, output, resource):
    state = idle(client)
    if state["map"] != "map034_海边小树林.map" or state["variables"].get("Event") != "270":
        raise AutomationError("Furong route requires the normal post-Nangong checkpoint")
    source_end, source_money = state["variables"]["End"], state["player"]["money"]
    sword_before = quantity(state, "goods057_越女剑.ini")
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    checkpoint(client, output, "440-furong-common-source-slot0")
    interact_at(client, output, resource, "张琳心", "script/map/map034_海边小树林/张琳心对话.txt", "441-forest-linxin")
    transition(client, resource, "map034_1_海边小渔村.map", 2, output, "442-village-entry")
    transition(client, resource, "map034_1_海边小渔村.map", 1, output, "443-linan-return-refused")
    for attempt in range(2):
        interact_at(client, output, resource, "张琳心", "script/map/map034_1_海边小渔村/张琳心对话.txt", f"444-village-linxin-{attempt}")
        state = interact_at(client, output, resource, "渔夫老王", "script/map/map034_1_海边小渔村/渔夫老王对话.txt", f"445-laowang-{attempt}")
        if state["variables"].get("Talklaowang270") != "1" or state["variables"].get("Event") != "270":
            raise AutomationError("Village Laowang inquiry did not finish without advancing the quest")
    client.save_or_load(1)
    checkpoint(client, output, "446-furong-house-source-slot1")
    transition(client, resource, "map036_渔夫家.map", 2, output, "447-furong-house-entry")
    client.save_or_load(2)
    checkpoint(client, output, "448-furong-meeting-source-slot2")
    state = transition(client, resource, "map044_石塘镇.map", 2, output, "449-furong-boat-arrival")
    meeting = script_proof(output, resource, "script/map/map036_渔夫家/段芙蓉对话.txt")
    if (state["variables"].get("Event") != "280" or state["variables"].get("End") != source_end
            or state["player"]["money"] != source_money or quantity(state, "goods057_越女剑.ini") != sword_before + 1):
        raise AutomationError("Furong voyage did not give exactly one sword and preserve End and money")
    client.save_or_load(3)
    checkpoint(client, output, "450-shitang-arrival-source-slot3")
    for name in ("小雷", "段芙蓉", "张琳心"):
        for attempt in range(2):
            interact_at(client, output, resource, name, f"script/map/map044_石塘镇/{name}对话.txt", f"451-arrival-{name}-{attempt}")
    if client.observe(VARIABLES)["variables"].get("Talk280") != "1":
        raise AutomationError("Shitang partner first and repeat dialogue did not retain its counter")
    random_results = {}
    for name, filename, variable in (
            ("小灵", "石塘镇居民1之小灵对话.txt", "Talkxiaolin280"),
            ("何大叔", "石塘镇居民3之何大叔对话.txt", "Talkheshu280"),
            ("何大婶", "石塘镇居民4之何大婶对话.txt", "Talkheshen280")):
        seen = set()
        for attempt in range(16):
            state = interact_at(client, output, resource, name, f"script/map/map044_石塘镇/{filename}", f"452-resident-{name}-{attempt}")
            seen.add(state["variables"].get(variable))
            if seen == {"0", "1"}:
                break
        if seen != {"0", "1"}:
            raise AutomationError(f"Native random dialogue still missing one side: {name}: {seen}")
        random_results[name] = sorted(seen)
    for name, filename in (("潘子龙", "石塘镇居民2之潘子龙对话.txt"), ("钟大爷", "石塘镇居民5之钟大爷对话.txt")):
        for attempt in range(2):
            interact_at(client, output, resource, name, f"script/map/map044_石塘镇/{filename}", f"453-resident-{name}-{attempt}")
    state = checkpoint(client, output, "454-town-dialogues-complete")
    if (state["variables"].get("Talkdaye280") != "1" or state["variables"].get("Event") != "280"
            or quantity(state, "goods057_越女剑.ini") != sword_before + 1
            or any(row["name"] in ("小雷", "段芙蓉") for row in state["targets"])):
        raise AutomationError("Town farewell did not remove departing actors or repeated the sword reward")
    client.save_or_load(6)
    for destination, trap, label in (("map045_石塘镇民房.map", 4, "house"),
                                     ("map046_石塘镇酒楼.map", 3, "tavern"),
                                     ("map047_长安城东.map", 2, "changan-east"),
                                     ("map048_华山脚下.map", 1, "huashan")):
        load_checkpoint(client, 3)
        state = transition(client, resource, destination, trap, output, f"455-{label}-entry")
        if any(row["name"] in ("小雷", "段芙蓉") for row in state["targets"]):
            raise AutomationError(f"Departing actors survived the Event280 map transition: {destination}")
        for attempt in range(2):
            interact_at(client, output, resource, "张琳心", f"script/map/{Path(destination).stem}/张琳心对话.txt", f"456-{label}-linxin-{attempt}")
        if label == "changan-east":
            transition(client, resource, destination, 2, output, "457-changan-city-before-huashan-refused")
    load_checkpoint(client, 6)
    client.save_or_load(6)
    final = checkpoint(client, output, "458-shitang-main-route-source-slot6")
    write_json(output / "furong-and-shitang-proof.json", dict(status="passed", Event="280", End=source_end,
               swordAwardCount=1, randomDialogueSides=random_results, nativeArrival=meeting,
               fourEvent280Destinations=True, sourceSlots=[0, 1, 2, 3, 6], cheatAssisted=False))
    print(f"Furong voyage and Shitang dialogues passed: Event={final['variables']['Event']}, End={source_end}", flush=True)


def huashan_inquiry(client, output, resource):
    state = idle(client)
    if state["map"] != "map044_石塘镇.map" or state["variables"].get("Event") != "280":
        raise AutomationError("Huashan inquiry requires the native Shitang arrival checkpoint")
    source_end, source_money = state["variables"]["End"], state["player"]["money"]
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    transition(client, resource, "map046_石塘镇酒楼.map", 3, output, "460-tavern-event280-entry")
    for name, filename in (("酒店老板", "酒店老板对话.txt"), ("酒客", "酒客对话.txt"), ("酒客4", "酒客2对话.txt")):
        for attempt in range(2):
            interact_at(client, output, resource, name, f"script/map/map046_石塘镇酒楼/{filename}", f"461-event280-{name}-{attempt}")
    transition(client, resource, "map044_石塘镇.map", 1, output, "462-tavern-return")
    transition(client, resource, "map048_华山脚下.map", 1, output, "463-huashan-first-entry")
    client.save_or_load(1)
    checkpoint(client, output, "464-huashan-event280-source-slot1")
    state = client.observe(VARIABLES)
    guards = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"] if row["name"] == "华山派弟子"}
    try:
        reachable_trap(resource, state["map"], 3, state["player"]["position"], avoid=guards)
    except AutomationError as error:
        if "No connected trap" not in str(error):
            raise
        write_json(output / "465-main-road-guard-occupancy.json", dict(map=state["map"], Event="280",
                   guardPositions=sorted(guards), noConnectedWalkingPath=True, observationOnly=True))
    else:
        raise AutomationError("Huashan main road has a path around its guarding NPCs")
    for attempt in range(2):
        if attempt:
            transition(client, resource, "map044_石塘镇.map", 1, output, "468-scenery-normal-town-return")
            transition(client, resource, "map048_华山脚下.map", 1, output, "468-scenery-normal-revisit")
        state = transition(client, resource, "map048_华山脚下.map", 4, output, f"468-huashan-scenery-{attempt}")
        if state["variables"].get("gantan") != "1":
            raise AutomationError("Huashan scenic dialogue did not retain its one-time marker")
    transition(client, resource, "map048_华山脚下.map", 5, output, "469-hidden-path-event280-refused")
    state = client.observe(VARIABLES)
    try:
        reachable_trap(resource, state["map"], 2, state["player"]["position"],
                       avoid=trap_points(resource, state["map"], 5))
    except AutomationError as error:
        if "No connected trap" not in str(error):
            raise
        write_json(output / "470-hidden-path-gate-proof.json", dict(map=state["map"], Event="280",
                   exitTrap=2, blockingTrap=5, noPathAvoidingBarrier=True, observationOnly=True))
    else:
        raise AutomationError("The hidden entrance has a walkable route around its refusal trigger")
    for attempt, position in enumerate(((16, 16), (16, 17))):
        state = interact_at(client, output, resource, "华山派弟子", "script/map/map048_华山脚下/华山派弟子对话.txt",
                            f"471-huashan-guard-{attempt}", position=position)
        if state["variables"].get("Event") != "290" or state["variables"].get("Talkhuashan") != "1":
            raise AutomationError("Huashan guard first/repeated dialogue did not settle at Event290")
    for attempt in range(2):
        state = interact_at(client, output, resource, "张琳心", "script/map/map048_华山脚下/张琳心对话.txt", f"472-huashan-linxin-{attempt}")
        if state["variables"].get("Talk048290") != "1":
            raise AutomationError("Huashan partner dialogue counter did not persist")
    transition(client, resource, "map048_华山脚下.map", 5, output, "473-hidden-path-event290-refused")
    transition(client, resource, "map044_石塘镇.map", 1, output, "475-inquiry-town-return")
    client.save_or_load(2)
    transition(client, resource, "map044_石塘镇.map", 2, output, "476-changan-before-inquiry-refused")
    interact_at(client, output, resource, "张琳心", "script/map/map044_石塘镇/张琳心对话.txt", "477-town-event290-linxin")
    random_results = {}
    for name, filename, variable in (("小灵", "石塘镇居民1之小灵对话.txt", "Talkxiaolin"),
                                     ("何大叔", "石塘镇居民3之何大叔对话.txt", "Talkheshu290"),
                                     ("何大婶", "石塘镇居民4之何大婶对话.txt", "Talkheshen")):
        seen = set()
        for attempt in range(16):
            state = interact_at(client, output, resource, name, f"script/map/map044_石塘镇/{filename}", f"478-event290-{name}-{attempt}")
            seen.add(state["variables"].get(variable))
            if seen == {"0", "1"}:
                break
        if seen != {"0", "1"}:
            raise AutomationError(f"Event290 resident random dialogue missing one side: {name}: {seen}")
        random_results[name] = sorted(seen)
    transition(client, resource, "map045_石塘镇民房.map", 4, output, "479-house-event290-entry")
    interact_at(client, output, resource, "张琳心", "script/map/map045_石塘镇民房/张琳心对话.txt", "480-house-event290-linxin")
    transition(client, resource, "map044_石塘镇.map", 1, output, "481-house-event290-return")
    transition(client, resource, "map046_石塘镇酒楼.map", 3, output, "482-tavern-event290-entry")
    for name, filename in (("张琳心", "张琳心对话.txt"), ("酒店老板", "酒店老板对话.txt"), ("酒客", "酒客对话.txt"), ("酒客4", "酒客2对话.txt")):
        interact_at(client, output, resource, name, f"script/map/map046_石塘镇酒楼/{filename}", f"483-event290-{name}")
    transition(client, resource, "map044_石塘镇.map", 1, output, "484-tavern-event290-return")
    client.save_or_load(3)
    for attempt in range(2):
        state = interact_at(client, output, resource, "钟大爷", "script/map/map044_石塘镇/石塘镇居民5之钟大爷对话.txt", f"485-zhong-changan-inquiry-{attempt}")
        if state["variables"].get("Event") != "300":
            raise AutomationError("Zhong inquiry failed to unlock Chang'an at Event300")
    transition(client, resource, "map044_石塘镇.map", 1, output, "486-huashan-before-lisan-refused")
    state = checkpoint(client, output, "487-huashan-inquiry-complete")
    if state["variables"].get("End") != source_end or state["player"]["money"] != source_money:
        raise AutomationError("Huashan inquiry changed End or money")
    client.save_or_load(6)
    write_json(output / "huashan-inquiry-proof.json", dict(status="passed", Event="300", End=source_end,
               randomDialogueSides=random_results, mainRoadBlockedByGuards=True, hiddenPathBeforeGuideRefused=True,
               guardFirstAndRepeat=True, scenicFirstAndRepeat=True, sourceSlots=[0, 1, 2, 3, 6], cheatAssisted=False))
    print("Huashan inquiry passed; Event=300", flush=True)


def changan_inquiry(client, output, resource):
    state = idle(client)
    if state["map"] != "map044_石塘镇.map" or state["variables"].get("Event") != "300":
        raise AutomationError("Chang'an inquiry requires Zhong's native guide information")
    source_end, source_money = state["variables"]["End"], state["player"]["money"]
    incoming = configparser.ConfigParser()
    incoming.read(output / "user-data/save/xjxqy/game/player2.ini", encoding="utf-8-sig")
    incoming_section = next(section for section in incoming.sections() if section.lower() == "init")
    rumeng_money = incoming.getint(incoming_section, "money")
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    transition(client, resource, "map047_长安城东.map", 2, output, "490-changan-east-entry")
    interact_at(client, output, resource, "张琳心", "script/map/map047_长安城东/张琳心对话.txt", "491-east-event300-linxin")
    transition(client, resource, "map049_长安城.map", 2, output, "492-changan-first-entry")
    client.save_or_load(1)
    transition(client, resource, "map049_长安城.map", 1, output, "493-city-event300-exit-refused")
    transition(client, resource, "map049_长安城.map", 4, output, "494-brothel-event300-refused")
    interact_at(client, output, resource, "张琳心", "script/map/map049_长安城/张琳心对话.txt", "495-city-event300-linxin")
    for name, filename in (("乞丐", "长安城居民9之乞丐对话.txt"), ("云老爷", "长安城居民4之云老爷对话.txt"),
                           ("云彪", "长安城居民5之云彪对话.txt"), ("浅浅", "长安城居民6之浅浅对话.txt"),
                           ("金兵1", "长安城居民7之金兵1对话.txt"), ("金兵2", "长安城居民8之金兵2对话.txt"),
                           ("金兵3", "长安城居民10之金兵3对话.txt"), ("金兵4", "长安城居民11之金兵4对话.txt"),
                           ("许大娘", "长安城居民13之许大娘对话.txt"), ("赶马人", "长安城居民14之赶马人对话.txt")):
        interact_at(client, output, resource, name, f"script/map/map049_长安城/{filename}", f"496-city-{name}")
    interact_at(client, output, resource, "卖药人", "script/map/map049_长安城/长安城居民2之卖药人对话.txt", "497-city-medicine-shop", shop_cancel=True)
    random_results = {}
    for name, filename, variable in (("乐剑秋", "长安城居民3之乐剑秋对话.txt", "Talklejianqiu"),
                                     ("常三姑", "长安城居民12之常三姑对话.txt", "Talkchangsangu")):
        seen = set()
        for attempt in range(16):
            state = interact_at(client, output, resource, name, f"script/map/map049_长安城/{filename}", f"498-city-{name}-{attempt}")
            seen.add(state["variables"].get(variable))
            if seen == {"0", "1"}:
                break
        if seen != {"0", "1"}:
            raise AutomationError(f"Chang'an resident random dialogue missing one side: {name}: {seen}")
        random_results[name] = sorted(seen)
    for attempt in range(3):
        state = interact_at(client, output, resource, "活字印刷", "script/map/map049_长安城/长安城居民15之活字印刷对话.txt", f"499-printing-{attempt}")
        if state["variables"].get("Talkyinshua") != str(min(attempt + 1, 2)):
            raise AutomationError("Printing dialogue did not advance and retain its native counter")
    transition(client, resource, "map050_长安城酒店一楼.map", 2, output, "500-tavern-event300-entry")
    for name, filename in (("张琳心", "张琳心对话.txt"), ("酒店店小二", "酒店店小二对话.txt")):
        interact_at(client, output, resource, name, f"script/map/map050_长安城酒店一楼/{filename}", f"501-tavern-{name}")
    seen = set()
    for attempt in range(24):
        state = interact_at(client, output, resource, "酒店掌柜", "script/map/map050_长安城酒店一楼/酒店老板对话.txt", f"502-tavern-owner-{attempt}")
        seen.add(state["variables"].get("Talklaoban"))
        if seen == {"0", "1", "2"}:
            break
    if seen != {"0", "1", "2"}:
        raise AutomationError(f"Tavern owner random dialogue missing a side: {seen}")
    for attempt in range(5):
        state = interact_at(client, output, resource, "酒客6", "script/map/map050_长安城酒店一楼/酒客组1对话.txt", f"503-tavern-group1-{attempt}")
        if state["variables"].get("Map050jiuke1") != str(min(attempt + 1, 4)):
            raise AutomationError("Tavern group1 dialogue counter mismatch")
    group2_counter = state["variables"].get("Map050jiuke2")
    for attempt in range(2):
        state = interact_at(client, output, resource, "酒客2", "script/map/map050_长安城酒店一楼/酒客组2对话.txt", f"504-tavern-group2-{attempt}")
        if state["variables"].get("Map050jiuke2") != group2_counter or state["variables"].get("Map050jiuke1") != "1":
            raise AutomationError("Tavern group2 no longer matches its current counter-write diagnostic")
    write_json(output / "tavern-group2-counter-diagnostic.json", dict(observed=True, requestedCounter="Map050jiuke2",
               actualAssignedCounter="Map050jiuke1", repeatedFirstDialogue=True, resourceChangesApplied=False))
    transition(client, resource, "map050_1_长安酒店二楼.map", 2, output, "505-upstairs-before-clue")
    for name, filename in (("张琳心", "张琳心对话.txt"), ("酒客5", "酒客组3对话.txt")):
        interact_at(client, output, resource, name, f"script/map/map050_1_长安酒店二楼/{filename}", f"506-upstairs-{name}")
    transition(client, resource, "map050_长安城酒店一楼.map", 1, output, "507-upstairs-before-clue-return")
    transition(client, resource, "map049_长安城.map", 1, output, "508-tavern-before-clue-return")
    client.save_or_load(2)
    for attempt in range(3):
        state = interact_at(client, output, resource, "卖菜翁", "script/map/map049_长安城/长安城居民1之卖菜翁对话.txt", f"509-lisan-clue-{attempt}")
        if state["variables"].get("Talkmaicai") != str(min(attempt + 1, 2)) or state["variables"].get("Event") != ("300" if attempt == 0 else "301"):
            raise AutomationError("Native Lisan clue did not advance once and preserve repeated dialogue")
    client.save_or_load(3)
    transition(client, resource, "map049_长安城.map", 1, output, "510-city-event301-exit-refused")
    transition(client, resource, "map049_长安城.map", 4, output, "511-brothel-event301-refused")
    transition(client, resource, "map049_长安城.map", 10, output, "512-official-gate-event301-refused")
    interact_at(client, output, resource, "张琳心", "script/map/map049_长安城/张琳心对话.txt", "513-city-event301-linxin")
    transition(client, resource, "map050_长安城酒店一楼.map", 2, output, "514-tavern-event301-entry")
    entry_frame = client.observe()["frame"]
    client.wait_until(lambda value: value.get("worldInput") and value["player"]["action"] == 0
                      and value["frame"] >= entry_frame + 120, description="native tavern entry walk completion")
    for name, filename in (("张琳心", "张琳心对话.txt"), ("酒店掌柜", "酒店老板对话.txt")):
        interact_at(client, output, resource, name, f"script/map/map050_长安城酒店一楼/{filename}", f"515-event301-{name}")
    state = checkpoint(client, output, "515-before-character-transfer")
    if state["variables"].get("End") != source_end or state["player"]["money"] != source_money:
        raise AutomationError("Chang'an investigation changed End or Dugu's money")
    state = transition(client, resource, "map050_1_长安酒店二楼.map", 2, output, "516-rumeng-native-control")
    if state["variables"].get("Event") != "304":
        raise AutomationError("Native tavern reunion did not transfer to Rumeng at Event304")
    for name, filename in (("独孤剑", "独孤剑对话.txt"), ("张琳心", "张琳心对话.txt")):
        interact_at(client, output, resource, name, f"script/map/map050_1_长安酒店二楼/{filename}", f"517-rumeng-{name}")
    state = client.observe(VARIABLES)
    if state["variables"].get("End") != source_end or state["player"]["money"] != rumeng_money:
        raise AutomationError("Native character transfer changed End or Rumeng's saved money")
    client.save_or_load(6)
    saved = configparser.ConfigParser()
    saved.read(output / "user-data/save/xjxqy/rpg7/game.ini", encoding="utf-8-sig")
    if saved.getint("state", "chr") != 2:
        raise AutomationError("Native saved character is not Rumeng after PlayerChange(2)")
    write_json(output / "changan-inquiry-proof.json", dict(status="passed", Event="304", End=source_end,
               randomDialogueSides=random_results, ownerDialogueSides=sorted(seen), currentCharacter=2,
               lisanFirstAndRepeat=True, tavernGroup2CounterDiagnostic=True, sourceSlots=[0, 1, 2, 3, 6], cheatAssisted=False))
    print("Chang'an investigation and native Rumeng control passed; Event=304", flush=True)


def changan_rescue(client, output, resource):
    state = idle(client)
    if state["map"] != "map050_1_长安酒店二楼.map" or state["variables"].get("Event") != "304":
        raise AutomationError("Chang'an rescue requires the native Rumeng control checkpoint")
    source_end = state["variables"]["End"]
    dugu = configparser.ConfigParser()
    dugu.read(output / "user-data/save/xjxqy/game/player0.ini", encoding="utf-8-sig")
    dugu_section = next(section for section in dugu.sections() if section.lower() == "init")
    dugu_money = dugu.getint(dugu_section, "money")
    token = "goods406_天王令.ini"
    token_before = sum(row["quantity"] for row in state["inventory"] if row["file"] == token)
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    transition(client, resource, "map050_长安城酒店一楼.map", 1, output, "520-rumeng-downstairs")
    transition(client, resource, "map049_长安城.map", 1, output, "521-rumeng-leaves-tavern")
    transition(client, resource, "map049_长安城.map", 4, output, "522-brothel-event304-refused")
    transition(client, resource, "map047_长安城东.map", 1, output, "523-caihong-east-entry")
    client.save_or_load(1)
    transition(client, resource, "map047_长安城东.map", 2, output, "524-east-event304-city-refused")
    state = transition(client, resource, "map047_长安城东.map", 3, output, "525-caihong-native-departure")
    if state["variables"].get("Event") != "305" or any(row["name"] in ("南宫彩虹", "黑衣杀手") for row in state["targets"]):
        raise AutomationError("Native Caihong encounter did not remove both departing actors at Event305")
    transition(client, resource, "map047_长安城东.map", 1, output, "526-rumeng-wrong-road-refused")
    transition(client, resource, "map049_长安城.map", 2, output, "527-rumeng-city-return")
    transition(client, resource, "map049_长安城.map", 1, output, "528-rumeng-leave-city-refused")
    client.save_or_load(2)
    assist(client, output, invincible=True)
    state = transition(client, resource, "map051_长安妓院一楼.map", 4, output, "529-yao-native-battle-entry")
    if state["variables"].get("Event") != "306":
        raise AutomationError("Native Yao encounter did not start its battle at Event306")
    client.save_or_load(3)
    for trap in (1, 2):
        transition(client, resource, "map051_长安妓院一楼.map", trap, output, f"530-battle-exit-{trap}-refused")
    state = walk_required_battle(client, output, resource, "307", "531-yao", progress_variable="Event")
    write_json(output / "531-yao-death-script-proof.json", script_proof(output, resource,
               "script/map/map051_长安妓院一楼/姚公子及跟班死亡.txt"))
    if state["variables"].get("fight051") != "3" or state["player"]["money"] != dugu_money:
        raise AutomationError("Yao's three deaths did not restore Dugu's native wallet")
    client.save_or_load(4)
    transition(client, resource, "map051_长安妓院一楼.map", 2, output, "532-upstairs-event307-refused")
    for name, filename in (("张如梦", "张如梦对话.txt"), ("张琳心", "张琳心对话.txt"), ("姚公子", "姚公子对话.txt")):
        interact_at(client, output, resource, name, f"script/map/map051_长安妓院一楼/{filename}", f"533-battle-aftermath-{name}")
    transition(client, resource, "map049_长安城.map", 1, output, "534-yao-city-return")
    transition(client, resource, "map049_长安城.map", 1, output, "535-city-event307-exit-refused")
    transition(client, resource, "map049_长安城.map", 4, output, "536-brothel-event307-refused")
    state = transition(client, resource, "map049_长安城.map", 10, output, "537-yao-official-gate")
    if state["variables"].get("yamen") != "1":
        raise AutomationError("Native official gate did not admit Yao's party")
    # Yao stands in the only walking passage after the gate dialogue.
    # Ordinary movement lets the following partner vacate that tile.
    client.move(9, 90)
    client.move(3, 78)
    for name, filename in (("大牢卫兵", "大牢门口士兵对话.txt"), ("衙门卫兵1", "衙门门口士兵对话.txt"),
                           ("衙门卫兵2", "衙门门口士兵对话.txt")):
        interact_at(client, output, resource, name, f"script/map/map049_长安城/{filename}", f"538-official-guard-{name}")
    transition(client, resource, "map052_长安城衙门.map", 8, output, "539-yamen-entry")
    transition(client, resource, "map049_长安城.map", 1, output, "540-yamen-return")
    client.move(7, 71)
    transition(client, resource, "map053_长安城大牢木屋.map", 9, output, "541-jail-house-entry")
    transition(client, resource, "map053_长安城大牢木屋.map", 1, output, "542-jail-house-before-rescue-refused")
    for name, filename in (("张琳心", "张琳心对话.txt"), ("姚公子", "姚公子对话.txt")):
        interact_at(client, output, resource, name, f"script/map/map053_长安城大牢木屋/{filename}", f"543-jail-house-{name}")
    transition(client, resource, "map054_长安城大牢.map", 2, output, "544-jail-native-entry")
    client.save_or_load(5)
    transition(client, resource, "map054_长安城大牢.map", 1, output, "545-jail-before-rescue-refused")
    for name, filename in (("张琳心", "张琳心对话.txt"), ("姚公子", "姚公子对话.txt")):
        interact_at(client, output, resource, name, f"script/map/map054_长安城大牢/{filename}", f"546-jail-{name}")
    state = transition(client, resource, "map047_长安城东.map", 2, output, "547-lisan-and-yanghu-native-rescue")
    if state["variables"].get("Event") != "310":
        raise AutomationError("Native jail rescue and farewell did not finish at Event310")
    if sum(row["quantity"] for row in state["inventory"] if row["file"] == token) != token_before + 1:
        raise AutomationError("Yanghu did not award exactly one native Tianwang token")
    transition(client, resource, "map047_长安城东.map", 2, output, "548-east-event310-city-refused")
    interact_at(client, output, resource, "张琳心", "script/map/map047_长安城东/张琳心对话.txt", "549-east-event310-linxin")
    transition(client, resource, "map044_石塘镇.map", 1, output, "550-rescue-shitang-return")
    transition(client, resource, "map045_石塘镇民房.map", 4, output, "551-lisan-house-native-entry")
    for attempt in range(2):
        state = interact_at(client, output, resource, "李三", "script/map/map045_石塘镇民房/李三对话.txt", f"552-lisan-guide-{attempt}")
        if state["variables"].get("Event") != "320":
            raise AutomationError("Lisan's first and repeated guide dialogue did not settle at Event320")
    transition(client, resource, "map044_石塘镇.map", 1, output, "553-guide-town-return")
    state = checkpoint(client, output, "554-changan-rescue-complete")
    if state["variables"].get("End") != source_end or state["player"]["money"] != dugu_money:
        raise AutomationError("Native Chang'an rescue unexpectedly changed End or Dugu's money")
    client.save_or_load(6)
    saved = configparser.ConfigParser()
    saved.read(output / "user-data/save/xjxqy/rpg7/game.ini", encoding="utf-8-sig")
    if saved.getint("state", "chr") != 0:
        raise AutomationError("Native rescue save did not restore Dugu control")
    write_json(output / "changan-rescue-proof.json", dict(status="passed", Event="320", End=source_end,
               fight051="3", yamen="1", currentCharacter=0, nativeCaihongDeparture=True,
               nativeLisanAndYanghuRescue=True, tianwangTokenAward=1, lisanFirstAndRepeat=True,
               sourceSlots=list(range(7)), cheatAssisted=True))
    print("Chang'an rescue passed; Event=320, native Dugu control restored", flush=True)


def stone_wall_late(client, output, resource):
    state = idle(client)
    if state["map"] != "map009_武夷山禁地内洞2.map" or state["variables"].get("Event") != "230":
        raise AutomationError("Late wall learning requires the native cave checkpoint before learning")
    source_end = state["variables"]["End"]
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    transition(client, resource, "map014_武夷山禁地外洞.map", 1, output, "560-wall-learning-deferred")
    transition(client, resource, "map008_武夷山禁地内洞1.map", 2, output, "561-late-wall-native-rescue-entry")
    state = walk_required_battle(client, output, resource, "235", "562-late-wall-killers")
    if state["variables"].get("fight008") != "28":
        raise AutomationError("Late wall route did not finish all twenty-eight native deaths")
    finish_liuzhongyuan(client, output, resource, complete_chapter=False)
    transition(client, resource, "map009_武夷山禁地内洞2.map", 3, output, "563-late-wall-cave-return")
    before = records(output)[-1]["sequence"]
    state = transition(client, resource, "map009_武夷山禁地内洞2.map", 3, output, "564-late-wall-native-learning")
    source = "script/map/map009_武夷山禁地内洞2/trap03.txt"
    proof = script_proof(output, resource, source, before)
    lines = (resource / source).read_text(encoding="utf-8-sig").splitlines()
    late_line = next(i + 1 for i, line in enumerate(lines) if 'say("独孤剑：想不到我们竟有这番奇遇' in line)
    if (state["variables"].get("Event") != "240" or state["variables"].get("End") != source_end
            or late_line not in proof["executedLines"]
            or not any(row["file"] == "magic009_风雷九洲.ini" and row["level"] == 2 for row in state["magic"])):
        raise AutomationError("The naturally delayed wall visit did not execute its Event>230 learning branch")
    client.save_or_load(6)
    write_json(output / "stone-wall-late-proof.json", dict(status="passed", Event="240", End=source_end,
               deferredBeforeLearning=True, nativeTwentyEightDeaths=True, nativeChiefDeath=True,
               wallMagicLevel=2, lateSourceLine=late_line, sourceSlots=[0, 6], cheatAssisted=True))
    print("Naturally delayed wall learning passed; Event=240", flush=True)


def huashan_hidden_path(client, output, resource):
    state = idle(client)
    if state["map"] != "map044_石塘镇.map" or state["variables"].get("Event") != "320":
        raise AutomationError("Huashan hidden path requires Lisan's native guide checkpoint")
    source_end, source_money = state["variables"]["End"], state["player"]["money"]
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    transition(client, resource, "map048_华山脚下.map", 1, output, "570-huashan-native-guide-entry")
    client.save_or_load(1)
    transition(client, resource, "map048_华山脚下.map", 5, output, "571-huashan-hidden-gate-open")
    state = transition(client, resource, "map061_华山栈道2.map", 2, output, "572-native-lisan-hidden-path")
    if state["variables"].get("Event") != "320" or state["variables"].get("End") != source_end or state["player"]["money"] != source_money:
        raise AutomationError("Native Huashan hidden entry unexpectedly changed story state or money")
    client.save_or_load(6)
    write_json(output / "huashan-hidden-path-proof.json", dict(status="passed", Event="320", End=source_end,
               hiddenGateOpenedNaturally=True, nativeGuideDialogue=True, sourceSlots=[0, 1, 6], cheatAssisted=True))
    print("Native Huashan hidden path entry passed; Event=320", flush=True)


def huashan_bridge(client, output, resource):
    state = idle(client)
    if state["map"] != "map061_华山栈道2.map" or state["variables"].get("Event") != "320":
        raise AutomationError("Huashan bridge requires the native hidden-path entry")
    source_end, source_money = state["variables"]["End"], state["player"]["money"]
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    transition(client, resource, "map061_华山栈道2.map", 1, output, "580-bridge-before-guide-exit-refused")
    state = transition(client, resource, "map061_华山栈道2.map", 3, output, "581-lisan-native-farewell")
    if state["variables"].get("Event") != "330" or any(row["name"] == "李三" for row in state["targets"]):
        raise AutomationError("Lisan's bridge farewell did not advance Event330 and remove the guide")
    client.save_or_load(1)
    transition(client, resource, "map062_华山栈道3.map", 2, output, "582-native-huashan-summit-entry")
    transition(client, resource, "map062_华山栈道3.map", 2, output, "583-summit-event330-return-refused")
    state = transition(client, resource, "map062_华山栈道3.map", 5, output, "584-native-lindui-discovery")
    if state["variables"].get("Event") != "340" or state["variables"].get("End") != source_end:
        raise AutomationError("Native Lindui encounter did not finish at Event340 with unchanged End")
    if state["player"]["money"] != source_money:
        raise AutomationError("Native mountain route unexpectedly charged money")
    client.save_or_load(6)
    write_json(output / "huashan-bridge-proof.json", dict(status="passed", Event="340", End=source_end,
               nativeLisanFarewell=True, nativeLinduiDiscovery=True, sourceSlots=[0, 1, 6], cheatAssisted=True))
    print("Native Huashan bridge and Lindui encounter passed; Event=340", flush=True)


def changan_poem(client, output, resource):
    state = idle(client)
    if state["map"] != "map049_长安城.map" or state["variables"].get("Event") != "300" or state["variables"].get("Sub") != "150":
        raise AutomationError("Chang'an poem requires the native city entry with completed prescription")
    source_end, source_money = state["variables"]["End"], state["player"]["money"]
    client.act("SetAutoDialogue", enabled=True)
    transition(client, resource, "map058_长安城杂货店.map", 6, output, "590-poem-shop-entry")
    interact_at(client, output, resource, "杂货店老板", "script/map/map058_长安城杂货店/杂货铺老板.txt", "591-poem-native-shop-cancel", shop_cancel=True)
    client.save_or_load(0)
    source = "script/map/map058_长安城杂货店/世景良.txt"
    site = source + ":26"
    state = interact_at(client, output, resource, "世景良", source, "592-poem-refused", choices=[(site, 1)])
    if state["variables"].get("Sub") != "150":
        raise AutomationError("Ignoring the poem unexpectedly advanced the side quest")
    client.save_or_load(1)
    load_checkpoint(client, 0)
    state = interact_at(client, output, resource, "世景良", source, "593-poem-answered", choices=[(site, 0)])
    if state["variables"].get("Sub") != "160":
        raise AutomationError("Answering the poem did not advance its native clue")
    interact_at(client, output, resource, "世景良", source, "594-poem-repeated")
    state = transition(client, resource, "map049_长安城.map", 1, output, "595-poem-city-return")
    if state["variables"].get("End") != source_end or state["player"]["money"] != source_money:
        raise AutomationError("Native poem or cancelled shop changed End or money")
    client.save_or_load(6)
    write_json(output / "changan-poem-proof.json", dict(status="passed", Event="300", Sub="160", End=source_end,
               bothPoemOptions=True, nativeRepeat=True, nativeShopCancellation=True, sourceSlots=[0, 1, 6], cheatAssisted=True))
    print("Chang'an poem both options passed; Sub=160", flush=True)


def huashan_confrontation(client, output, resource):
    state = idle(client)
    if state["map"] != "map062_华山栈道3.map" or state["variables"].get("Event") != "340":
        raise AutomationError("Huashan confrontation requires the native Lindui discovery")
    source_end, source_money = state["variables"]["End"], state["player"]["money"]
    client.save_or_load(0)
    client.act("SetAutoDialogue", enabled=False)
    before = records(output)[-1]["sequence"]
    x, y = reachable_trap(resource, state["map"], 3, state["player"]["position"])
    action = client.submit("MoveTo", generation=state["generation"], x=x, y=y, running=True, timeoutMs=60000)
    state = client.wait_until(lambda value: value.get("map") == "map063_华山派大厅.map" and value.get("dialogue", {}).get("complete"),
                              variables=VARIABLES, description="native Huashan hall dialogue")
    if client.request("GetActionStatus", actionId=action)["status"] == "running":
        client.request("CancelAction", actionId=action)
    if not {"林海", "方勉", "罗天强", "李啸", "左冷心", "孙不三"} <= {row["name"] for row in state["targets"]}:
        raise AutomationError("Huashan hall is missing its native story actors")
    checkpoint(client, output, "600-native-huashan-hall-presented")
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if state["map"] != "map048_华山脚下.map" or state["variables"].get("Event") != "360":
        raise AutomationError("Native Huashan hall confrontation did not return to the mountain foot at Event360")
    write_json(output / "601-hall-complete-script-proof.json", script_proof(output, resource, "script/map/map062_华山栈道3/trap03.txt", before))
    checkpoint(client, output, "601-huashan-hall-complete")
    client.save_or_load(1)
    transition(client, resource, "map048_华山脚下.map", 2, output, "602-hidden-route-event360-refused")
    interact_at(client, output, resource, "张琳心", "script/map/map048_华山脚下/张琳心对话.txt", "603-huashan-linxin-event360")
    transition(client, resource, "map044_石塘镇.map", 1, output, "604-huashan-inquiry-town-return")
    transition(client, resource, "map047_长安城东.map", 2, output, "605-lindui-east-return")
    transition(client, resource, "map049_长安城.map", 2, output, "606-lindui-city-return")
    transition(client, resource, "map051_长安妓院一楼.map", 4, output, "607-linxin-native-waits-outside")
    if any(row["name"] == "张琳心" for row in client.observe()["targets"]):
        raise AutomationError("Linxin did not stay outside during the native brothel inquiry")
    for name, filename in (("妓院鸨母", "鸨母对话.txt"), ("妓女可儿", "妓女可儿对话.txt"),
                           ("妓女靓儿", "妓女靓儿对话.txt"), ("妓女喜儿", "妓女喜儿对话.txt"),
                           ("嫖客苏宇", "嫖客苏宇对话.txt"), ("嫖客侯研", "嫖客侯研对话.txt")):
        interact_at(client, output, resource, name, f"script/map/map051_长安妓院一楼/{filename}", f"608-brothel-{name}")
    transition(client, resource, "map051_1_长安妓院二楼.map", 2, output, "609-lindui-upstairs-entry")
    for name, filename in (("洋妓女黛丝", "洋妓女黛丝对话.txt"), ("嫖客贾啬", "嫖客贾啬对话.txt"),
                           ("嫖客吴此", "嫖客吴此对话.txt"), ("妓女月儿", "妓女月儿对话.txt"), ("妓女星儿", "妓女星儿对话.txt")):
        interact_at(client, output, resource, name, f"script/map/map051_1_长安妓院二楼/{filename}", f"610-brothel-upstairs-{name}")
    client.save_or_load(2)
    state = interact_at(client, output, resource, "林对儿和如花", "script/map/map051_1_长安妓院二楼/林对儿和如花对话.txt", "611-lindui-native-battle")
    if state["variables"].get("Event") != "365":
        raise AutomationError("Native Lindui dialogue did not start its fight")
    client.save_or_load(3)
    transition(client, resource, "map051_1_长安妓院二楼.map", 1, output, "612-lindui-battle-exit-refused")
    state = walk_required_battle(client, output, resource, "368", "613-lindui")
    write_json(output / "613-lindui-death-script-proof.json", script_proof(output, resource, "script/map/map051_1_长安妓院二楼/打败林对儿.txt"))
    if state["map"] != "map062_华山栈道3.map" or state["variables"].get("End") != source_end or state["player"]["money"] != source_money:
        raise AutomationError("Native Lindui victory did not enter the Linhai battle with unchanged End and money")
    client.save_or_load(6)
    checkpoint(client, output, "614-linhai-native-battle-source-slot6")
    write_json(output / "huashan-confrontation-proof.json", dict(status="passed", Event="368", End=source_end,
               nativeHallActorsVerified=True, nativeHallDialogue=True, nativeLinduiVictory=True,
               nativeLinhaiBattleStarted=True, sourceSlots=[0, 1, 2, 3, 6], cheatAssisted=True))
    print("Native Huashan confrontation and Lindui victory passed; Event=368", flush=True)


def changan_to_guide(client, output, resource):
    state = idle(client)
    if state["map"] != "map049_长安城.map" or state["variables"].get("Event") != "300" or state["variables"].get("Sub") != "160":
        raise AutomationError("The poem route requires its native Chang'an clue checkpoint")
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    for attempt in range(2):
        interact_at(client, output, resource, "卖菜翁", "script/map/map049_长安城/长安城居民1之卖菜翁对话.txt", f"620-poem-lisan-clue-{attempt}")
    transition(client, resource, "map050_长安城酒店一楼.map", 2, output, "621-poem-tavern-entry")
    frame = client.observe()["frame"]
    client.wait_until(lambda value: value.get("worldInput") and value["player"]["action"] == 0 and value["frame"] >= frame + 120,
                      description="native poem-route tavern entry walk")
    transition(client, resource, "map050_1_长安酒店二楼.map", 2, output, "622-poem-rumeng-native-control")
    changan_rescue(client, output, resource)
    huashan_hidden_path(client, output, resource)
    state = checkpoint(client, output, "623-poem-native-huashan-source")
    if state["variables"].get("Sub") != "160" or state["variables"].get("Event") != "320":
        raise AutomationError("Native poem route lost its clue or Lisan guide progress")
    write_json(output / "changan-to-guide-proof.json", dict(status="passed", Event="320", Sub="160",
               nativePoemCluePreserved=True, nativeRescueAndMountainRoute=True, cheatAssisted=True))
    print("Poem clue preserved through native Chang'an rescue and Huashan entry", flush=True)


def knock_huashan_stone(client, output, resource, prefix, reward, cleared=False):
    state = idle(client)
    if state["map"] != "map061_华山栈道2.map" or state["variables"].get("Knock") not in ("", "0"):
        raise AutomationError("Stone test requires an untouched native stone checkpoint")
    goods = ("goods005_霁云盔.ini", "goods037_霁云铠.ini", "goods069_霁云铛.ini",
             "goods052_霁云枪.ini", "goods024_霁云之腕.ini", "goods080_霁云之护.ini")
    before = {name: quantity(state, name) for name in goods}
    source = "script/map/map061_华山栈道2/石碑.txt"
    for number in range(1, 11):
        try:
            state = interact_at(client, output, resource, "石碑", source, f"{prefix}-knock-{number}")
        except AutomationError as error:
            if number != 1 or not any(reason in str(error) for reason in ("interaction_not_observed", "no_progress")):
                raise
            clear_enemies(client, output, f"{prefix}-native-path-clear", skills=())
            state = interact_at(client, output, resource, "石碑", source, f"{prefix}-knock-{number}")
        if state["variables"].get("Knock") != str(min(number, 9)):
            raise AutomationError("Native stone knock counter changed unexpectedly")
        expected = int(reward and number == 10)
        if any(quantity(state, name) != before[name] + expected for name in goods):
            raise AutomationError("Native stone reward differs from its six-item source")
    if reward or cleared:
        if reward and state["variables"].get("Sub") != "170":
            raise AutomationError("Early stone treasure did not advance Sub170")
        sequence = records(output)[-1]["sequence"]
        target = next(row for row in state["targets"] if row["name"] == "石碑")
        try:
            client.interact(target["id"])
        except AutomationError as error:
            if "action_rejected" not in str(error):
                raise
            rejection = str(error)
        else:
            raise AutomationError("Cleared stone script accepted a repeated reward interaction")
        state = checkpoint(client, output, f"{prefix}-repeat-rejected")
        if any(quantity(state, name) != before[name] + int(reward) for name in goods):
            raise AutomationError("Repeated stone click changed the reward")
        if any(row.get("eventType") == "script.start" and row["sequence"] > sequence for row in records(output)):
            raise AutomationError("Cleared stone click unexpectedly executed a script")
        write_json(output / f"{prefix}-repeat-proof.json", dict(nativeRejection=rejection, rewardUnchanged=True))
    else:
        state = interact_at(client, output, resource, "石碑", source, f"{prefix}-knock-11-no-clue")
        if state["variables"].get("Knock") != "9" or any(quantity(state, name) != before[name] for name in goods):
            raise AutomationError("Stone without its poem clue unexpectedly awarded treasure")
    return state


def linhai_and_shaolin(client, output, resource, late_treasure=False, doctor_followup=False):
    state = idle(client)
    if state["map"] != "map062_华山栈道3.map" or state["variables"].get("Event") != "368":
        raise AutomationError("Linhai battle requires its native post-confrontation checkpoint")
    if late_treasure and (state["variables"].get("Sub") != "160" or quantity(state, "goods411_一张药方.ini") != 0):
        raise AutomationError("Late treasure requires the native poem and returned-prescription route")
    if doctor_followup and state["variables"].get("Sub") != "170":
        raise AutomationError("Doctor followup requires the naturally claimed early treasure")
    source_end = state["variables"]["End"]
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    battle_sequence = records(output)[-1]["sequence"]
    for trap in (1, 2, 3, 4):
        prefix = f"630-linhai-battle-gate-{trap}"
        if (output / f"{prefix}-script-proof.json").exists():
            script_proof(output, resource, f"script/map/map062_华山栈道3/trap0{trap}.txt")
            continue
        for attempt in range(6):
            try:
                state = transition(client, resource, "map062_华山栈道3.map", trap, output, prefix)
                break
            except AutomationError as error:
                if "no_progress" not in str(error):
                    raise
                state = idle(client)
                enemies = [row for row in state["targets"] if row.get("hostile") and row.get("attackable")]
                if len(enemies) <= 1:
                    raise
                target = min(enemies, key=lambda row: abs(row["position"]["x"] - state["player"]["position"]["x"]) * 2
                             + abs(row["position"]["y"] - state["player"]["position"]["y"]))
                client.act("StartCombat", timeout=125, generation=state["generation"], targetId=target["id"],
                           radius=64, kills=1, skills=[], timeoutMs=120000)
                checkpoint(client, output, f"{prefix}-native-blocker-fight-{attempt}")
        else:
            raise AutomationError("Linhai battle gate remained blocked after native combat")
        if state["variables"].get("Event") != "368":
            raise AutomationError("Linhai fight finished before all battle gates were checked")
    state = walk_required_battle(client, output, resource, "370", "631-linhai")
    source = "script/map/map062_华山栈道3/林海与华山弟子死亡.txt"
    proof = script_proof(output, resource, source)
    deaths = [row for row in records(output) if row.get("eventType") == "script.start" and row.get("virtualPath") == source
              and row["sequence"] > battle_sequence]
    if state["variables"].get("fight062") != "11" or len(deaths) != 11:
        raise AutomationError("Linhai battle lacks all eleven native death callbacks")
    write_json(output / "631-linhai-death-script-proof.json", proof)
    client.save_or_load(1)
    for name in ("方勉", "罗天强", "李啸", "左冷心", "孙不三", "张琳心"):
        interact_at(client, output, resource, name, f"script/map/map062_华山栈道3/{name}对话.txt", f"632-linhai-after-{name}")
    transition(client, resource, "map061_华山栈道2.map", 2, output, "633-native-stone-no-poem-return")
    client.save_or_load(2)
    if doctor_followup:
        goods = "goods016_霁云之胆.ini"
        before = quantity(state, goods)
        try:
            state = interact_at(client, output, resource, "醉郎中", "script/map/map061_华山栈道2/醉郎中对话.txt", "750-native-doctor-treasure-confrontation")
        except AutomationError as error:
            if "interaction_not_observed" not in str(error):
                raise
            clear_enemies(client, output, "750-native-doctor-path-clear", skills=())
            state = interact_at(client, output, resource, "醉郎中", "script/map/map061_华山栈道2/醉郎中对话.txt", "750-native-doctor-treasure-confrontation")
        if state["variables"].get("Sub") != "171" or quantity(state, goods) != before:
            raise AutomationError("Native doctor confrontation did not start Sub171 without reward")
        client.save_or_load(3)
        state = walk_required_battle(client, output, resource, "180", "751-doctor", progress_variable="Sub")
        source = "script/map/map061_华山栈道2/醉郎中死亡.txt"
        proof = script_proof(output, resource, source)
        deaths = [row for row in records(output) if row.get("eventType") == "script.start" and row.get("virtualPath") == source]
        if quantity(state, goods) != before + 1 or len(deaths) != 1 or state["variables"].get("Event") != "370":
            raise AutomationError("Native doctor death did not award its one extra treasure item once")
        write_json(output / "751-doctor-death-script-proof.json", proof)
        client.save_or_load(4)
    else:
        prefix = "634-stone-late" if late_treasure else "634-stone-no-poem"
        state = knock_huashan_stone(client, output, resource, prefix, False, cleared=late_treasure)
        if state["variables"].get("Sub") != ("160" if late_treasure else "150"):
            raise AutomationError("Native unawarded stone changed the side quest")
    transition(client, resource, "map062_华山栈道3.map", 2, output, "635-native-huashan-after-stone")
    state = transition(client, resource, "map064_少室山下.map", 4, output, "636-native-shaolin-arrival")
    if state["variables"].get("End") != source_end or state["variables"].get("Event") != "370":
        raise AutomationError("Native Shaolin arrival changed End or lost Event370")
    client.save_or_load(6)
    proof_file = "doctor-treasure-followup-proof.json" if doctor_followup else "stone-treasure-late-proof.json" if late_treasure else "linhai-and-shaolin-proof.json"
    write_json(output / proof_file, dict(status="passed", Event="370", End=source_end,
               nativeDeathCallbacks=11, battleGateRefusals=4, postBattleDialogues=6,
               noPoemStoneKnocks=0 if late_treasure or doctor_followup else 11, lateEmptyStoneKnocks=10 if late_treasure else 0,
               Sub="180" if doctor_followup else "160" if late_treasure else "150", nativeDoctorDeath=doctor_followup,
               doctorRewardItems=int(doctor_followup), nativeShaolinArrival=True,
               sourceSlots=[0, 1, 2, 3, 4, 6] if doctor_followup else [0, 1, 2, 6], cheatAssisted=True))
    print("Native Linhai battle and Shaolin arrival passed; Event=370", flush=True)


def stone_treasure_early(client, output, resource):
    state = idle(client)
    if state["map"] != "map061_华山栈道2.map" or state["variables"].get("Event") not in ("320", "330") or state["variables"].get("Sub") != "160":
        raise AutomationError("Early treasure requires its native poem and Lisan checkpoint")
    if quantity(state, "goods411_一张药方.ini") != 0:
        raise AutomationError("Early treasure test requires the native returned-prescription route")
    client.act("SetAutoDialogue", enabled=True)
    if state["variables"].get("Event") == "320":
        transition(client, resource, "map061_华山栈道2.map", 3, output, "640-poem-lisan-native-farewell")
    state = idle(client)
    if state["variables"].get("Event") != "330":
        raise AutomationError("Native guide farewell did not reach the treasure time boundary")
    client.save_or_load(0)
    state = knock_huashan_stone(client, output, resource, "641-stone-early", True)
    client.save_or_load(6)
    write_json(output / "stone-treasure-early-proof.json", dict(status="passed", Event=state["variables"]["Event"], Sub="170",
               nativeKnocks=10, nativeRewardItems=6, repeatedRewardRejected=True, sourceSlots=[0, 6], cheatAssisted=True))
    print("Native early stone treasure passed; six items, Sub=170", flush=True)


def shaolin_first_inquiry(client, output, resource):
    state = idle(client)
    if state["map"] != "map064_少室山下.map" or state["variables"].get("Event") != "370":
        raise AutomationError("Shaolin inquiry requires its native mountain arrival")
    source_end = state["variables"]["End"]
    client.act("SetAutoDialogue", enabled=True)
    interact_at(client, output, resource, "张琳心", "script/map/map064_少室山下/张琳心对话.txt", "650-shaolin-before-woodcutter")
    for number in range(1, 5):
        state = interact_at(client, output, resource, "樵夫", "script/map/map064_少室山下/樵夫对话.txt", f"651-woodcutter-{number}")
        if state["variables"].get("Talkmap064") != str(min(number, 3)):
            raise AutomationError("Native woodcutter dialogue counter did not stop at three")
    interact_at(client, output, resource, "张琳心", "script/map/map064_少室山下/张琳心对话.txt", "652-shaolin-after-woodcutter")
    transition(client, resource, "map064_少室山下.map", 2, output, "653-premature-zhuxian-refused")
    transition(client, resource, "map065_少林寺.map", 1, output, "654-native-shaolin-entry")
    transition(client, resource, "map065_少林寺.map", 1, output, "655-shaolin-event370-exit-refused")
    for number in range(2):
        state = interact_at(client, output, resource, "知客僧", "script/map/map065_少林寺/知客僧对话.txt", f"656-host-monk-{number}")
        if state["variables"].get("Talkzhikeseng") != "1":
            raise AutomationError("Native host monk did not retain its repeated-talk state")
        interact_at(client, output, resource, "无智", "script/map/map065_少林寺/无智对话.txt", f"657-wuzhi-{number}")
    interact_at(client, output, resource, "少林二代弟子", "script/map/map065_少林寺/元通对话.txt", "658-yuantong-before-request", position=(12, 14))
    interact_at(client, output, resource, "守塔林弟子", "script/map/map065_少林寺/守塔林弟子对话.txt", "659-pagoda-guard")
    for position in ((15, 40), (13, 33)):
        interact_at(client, output, resource, "少林二代弟子", "script/map/map065_少林寺/普通弟子对话.txt", f"660-shaolin-disciple-{position[0]}", position=position)
    for number in range(2):
        interact_at(client, output, resource, "元真", "script/map/map065_少林寺/元真对话.txt", f"661-yuanzhen-request-{number}")
    client.save_or_load(0)
    source = "script/map/map065_少林寺/元通对话.txt"
    site = next(row["id"] for row in catalog_for(resource)["choices"] if row["path"] == source)
    for option, slot in ((1, 1), (0, 2)):
        load_checkpoint(client, 0)
        state = interact_at(client, output, resource, "少林二代弟子", source, f"662-yuantong-option-{option}", position=(12, 14), choices=[(site, option)])
        if quantity(state, "goods407_泥人.ini") != 1 or state["variables"].get("Talkyuantong") != "1":
            raise AutomationError("Yuantong choice did not award exactly one native clay figurine")
        state = interact_at(client, output, resource, "少林二代弟子", source, f"663-yuantong-repeat-{option}", position=(12, 14))
        if quantity(state, "goods407_泥人.ini") != 1:
            raise AutomationError("Yuantong repeated its figurine reward")
        client.save_or_load(slot)
    state = interact_at(client, output, resource, "元真", "script/map/map065_少林寺/元真对话.txt", "664-native-figurine-return")
    if state["variables"].get("Event") != "380" or state["variables"].get("Talkyuanzhen") != "2" or quantity(state, "goods407_泥人.ini") != 0:
        raise AutomationError("Native figurine return did not consume the item and reveal the abbot")
    interact_at(client, output, resource, "元真", "script/map/map065_少林寺/元真对话.txt", "665-yuanzhen-completed-repeat")
    transition(client, resource, "map066_大雄宝殿.map", 2, output, "666-native-main-hall")
    interact_at(client, output, resource, "无相和尚", "script/map/map066_大雄宝殿/无相对话.txt", "667-wuxiang-before-exposure")
    interact_at(client, output, resource, "少林二代弟子", "script/map/map066_大雄宝殿/普通弟子对话.txt", "668-hall-disciple", position=(9, 31))
    transition(client, resource, "map065_少林寺.map", 1, output, "669-main-hall-return")
    transition(client, resource, "map067_藏经阁.map", 3, output, "670-native-library-before-lesson")
    interact_at(client, output, resource, "无穷", "script/map/map067_藏经阁/无穷对话.txt", "671-wuqiong-before-lesson")
    interact_at(client, output, resource, "张琳心", "script/map/map067_藏经阁/张琳心对话.txt", "672-library-event380")
    transition(client, resource, "map065_少林寺.map", 1, output, "673-library-return")
    transition(client, resource, "map068_达摩堂.map", 4, output, "674-native-damo-entry")
    interact_at(client, output, resource, "张琳心", "script/map/map068_达摩堂/张琳心对话.txt", "675-damo-poem-clue")
    before = quantity(idle(client), "goods210_药王金方.ini")
    state = interact_at(client, output, resource, "宝箱", "script/map/map068_达摩堂/宝箱.txt", "676-damo-chest")
    if quantity(state, "goods210_药王金方.ini") != before + 1:
        raise AutomationError("Damo chest did not award its medicine once")
    target = next(row for row in state["targets"] if row["name"] == "宝箱")
    action = client.submit("Interact", generation=state["generation"], targetId=target["id"], side="primary", running=True)
    rejected = client.request("GetActionStatus", actionId=action)
    if rejected["status"] != "failed" or rejected["reason"] != "action_rejected":
        raise AutomationError("Damo chest accepted a repeated reward interaction")
    client.save_or_load(3)
    state = transition(client, resource, "map068_达摩堂.map", 2, output, "677-native-abbot-poem")
    if state["variables"].get("Event") != "390" or not any(row["name"] == "无虚大师" for row in state["targets"]):
        raise AutomationError("Native poem did not open the abbot's chamber and advance Event390")
    client.save_or_load(4)
    interact_at(client, output, resource, "无虚大师", "script/map/map068_达摩堂/无虚大师对话.txt", "678-abbot-rest-instruction")
    state = transition(client, resource, "map065_少林寺.map", 1, output, "679-native-pagoda-guard-cleared")
    guard = next(row for row in state["targets"] if row["name"] == "守塔林弟子")
    if guard["position"] != dict(x=0, y=0) or state["variables"].get("End") != source_end:
        raise AutomationError("Native abbot departure did not move the guard or preserved End incorrectly")
    client.save_or_load(6)
    write_json(output / "shaolin-first-inquiry-proof.json", dict(status="passed", Event="390", End=source_end,
               bothFigurineOptions=True, nativeFigurineReturned=True, nativeAbbotRevealed=True,
               nativeGuardMoved=True, chestRewardOnce=True, sourceSlots=[0, 1, 2, 3, 4, 6], cheatAssisted=True))
    print("Native Shaolin inquiry both choices and abbot reveal passed; Event=390", flush=True)


def poem_late_confrontation(client, output, resource, claimed=False):
    state = idle(client)
    expected_sub, expected_knock = ("170", ("9",)) if claimed else ("160", ("", "0"))
    completed = (claimed and state["map"] == "map062_华山栈道3.map" and state["variables"].get("Event") == "368"
                 and (output / "huashan-confrontation-proof.json").exists())
    if not completed:
        if (state["map"] != "map061_华山栈道2.map" or state["variables"].get("Event") != "330"
                or state["variables"].get("Sub") != expected_sub or state["variables"].get("Knock") not in expected_knock):
            raise AutomationError("Poem confrontation requires its native claimed or untouched treasure source")
        client.act("SetAutoDialogue", enabled=True)
        transition(client, resource, "map062_华山栈道3.map", 2, output, "680-late-native-summit-entry")
        transition(client, resource, "map062_华山栈道3.map", 5, output, "681-late-native-lindui-discovery")
        huashan_confrontation(client, output, resource)
    state = checkpoint(client, output, "682-late-poem-linhai-source")
    entry_proof = script_proof(output, resource, "script/map/map061_华山栈道2/trap02.txt")
    if claimed and not any(row.get("executionId") == entry_proof["executionId"] and row.get("eventType") == "variable.change"
                           and row.get("variableName") == "Knock" and row.get("beforeValue") == "9" and row.get("afterValue") == "0"
                           for row in records(output)):
        raise AutomationError("Native treasure departure lacks the source-defined knock counter reset")
    if state["variables"].get("Sub") != expected_sub or state["variables"].get("Knock") not in ("", "0"):
        raise AutomationError("Native confrontation changed the poem treasure state")
    proof_file = "treasure-followup-confrontation-proof.json" if claimed else "poem-late-confrontation-proof.json"
    write_json(output / proof_file, dict(status="passed", Event="368", Sub=expected_sub, nativeConfrontation=True,
               stoneUntouched=not claimed, earlyTreasurePreserved=claimed, nativeKnockCounterReset=claimed,
               nativeLinhaiBattleSource=True, cheatAssisted=True))
    print(f"Native poem treasure confrontation passed; Event=368, Sub={expected_sub}", flush=True)


def shaolin_traitor_and_lessons(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if state["map"] == "map069_少林寺塔林.map" and state["variables"].get("Event") in ("405", "410") and (output / "701-native-traitor-revealed-script-proof.json").exists():
        script_proof(output, resource, "script/map/map068_达摩堂/无虚大师对话.txt")
        medicine_delta = json.loads((output / "700-damo-revisit-chest-proof.json").read_text(encoding="utf-8"))["rewardDelta"]
        return finish_shaolin_traitor_and_lessons(client, output, resource, state["variables"]["End"], medicine_delta)
    if state["map"] != "map065_少林寺.map" or state["variables"].get("Event") != "390":
        raise AutomationError("Shaolin traitor investigation requires the native abbot checkpoint")
    source_end = state["variables"]["End"]
    transition(client, resource, "map069_少林寺塔林.map", 5, output, "690-native-pagoda-entry")
    interact_at(client, output, resource, "张琳心", "script/map/map069_少林寺塔林/张琳心对话.txt", "691-pagoda-event390")
    state = interact_at(client, output, resource, "假和尚", "script/map/map069_少林寺塔林/假和尚对话.txt", "692-native-false-monk-battle")
    if state["variables"].get("Event") != "395":
        raise AutomationError("Native false monk did not start its fight")
    client.save_or_load(0)
    transition(client, resource, "map069_少林寺塔林.map", 1, output, "693-false-monk-exit-refused")
    state = walk_required_battle(client, output, resource, "400", "694-false-monk")
    write_json(output / "694-false-monk-death-script-proof.json", script_proof(output, resource, "script/map/map069_少林寺塔林/假和尚死亡.txt"))
    client.save_or_load(1)
    interact_at(client, output, resource, "假和尚", "script/map/map069_少林寺塔林/假和尚对话.txt", "695-false-monk-captive-repeat")
    interact_at(client, output, resource, "张琳心", "script/map/map069_少林寺塔林/张琳心对话.txt", "696-pagoda-event400")
    transition(client, resource, "map065_少林寺.map", 1, output, "697-native-captive-pagoda-exit")
    transition(client, resource, "map065_少林寺.map", 1, output, "698-shaolin-event400-exit-refused")
    transition(client, resource, "map068_达摩堂.map", 4, output, "699-native-abbot-report-entry")
    state = idle(client)
    medicine_before = quantity(state, "goods210_药王金方.ini")
    target = next(row for row in state["targets"] if row["name"] == "宝箱")
    sequence = records(output)[-1]["sequence"]
    action = client.submit("Interact", generation=state["generation"], targetId=target["id"], side="primary", running=True)
    status = client.request("GetActionStatus", actionId=action)
    if status["status"] == "failed":
        if status["reason"] != "action_rejected":
            raise AutomationError(f"Damo revisit interaction failed: {status}")
    else:
        client.wait_action(action, 62)
        idle(client)
        write_json(output / "700-damo-revisit-chest-script-proof.json", script_proof(output, resource, "script/map/map068_达摩堂/宝箱.txt", sequence))
    state = checkpoint(client, output, "700-damo-revisit-chest")
    medicine_delta = quantity(state, "goods210_药王金方.ini") - medicine_before
    if medicine_delta not in (0, 1):
        raise AutomationError("Damo revisit chest changed medicine unexpectedly")
    write_json(output / "700-damo-revisit-chest-proof.json", dict(nativeInteraction=status, rewardDelta=medicine_delta,
               alternateObjectFile="map068_2.obj", intentRequiresOriginalReview=medicine_delta == 1, cheatAssisted=True))
    state = interact_at(client, output, resource, "无虚大师", "script/map/map068_达摩堂/无虚大师对话.txt", "701-native-traitor-revealed")
    if state["map"] != "map069_少林寺塔林.map" or state["variables"].get("Event") != "405":
        raise AutomationError("Native abbot report did not reveal Wuxiang and start Event405")
    client.save_or_load(2)
    transition(client, resource, "map069_少林寺塔林.map", 1, output, "702-wuxiang-battle-exit-refused")
    return finish_shaolin_traitor_and_lessons(client, output, resource, source_end, medicine_delta)


def finish_shaolin_traitor_and_lessons(client, output, resource, source_end, medicine_delta):
    state = walk_required_battle(client, output, resource, "410", "703-wuxiang")
    write_json(output / "703-wuxiang-death-script-proof.json", script_proof(output, resource, "script/map/map069_少林寺塔林/无相和尚死亡.txt"))
    if not any(row["file"] == "magic010_龙爪虎抓.ini" and row["level"] == 1 for row in state["magic"]):
        raise AutomationError("Native abbot lesson did not teach Dragon Claw level one")
    client.save_or_load(3)
    partner_magic = configparser.ConfigParser(interpolation=None)
    partner_magic.read(output / "user-data/save/xjxqy/rpg4/magic1.ini", encoding="utf-8-sig")
    if not any(partner_magic.get(section, "inifile", fallback="") == "magic022_梅花弄影.ini" for section in partner_magic.sections()):
        raise AutomationError("Native partner lesson is absent from Linxin's saved magic list")
    interact_at(client, output, resource, "张琳心", "script/map/map069_少林寺塔林/张琳心对话.txt", "704-pagoda-event410")
    transition(client, resource, "map065_少林寺.map", 1, output, "705-native-pagoda-story-exit")
    interact_at(client, output, resource, "无智", "script/map/map065_少林寺/无智对话.txt", "706-wuzhi-after-traitor")
    interact_at(client, output, resource, "少林二代弟子", "script/map/map065_少林寺/元通对话.txt", "707-yuantong-after-traitor", position=(12, 14))
    seen = set()
    for attempt in range(12):
        state = interact_at(client, output, resource, "少林二代弟子", "script/map/map065_少林寺/普通弟子对话.txt", f"708-shaolin-after-traitor-{attempt}", position=(15, 40))
        seen.add(state["variables"].get("Talkheshang"))
        if seen == {"0", "1"}:
            break
    else:
        raise AutomationError("Shaolin disciples did not reach both native random replies")
    transition(client, resource, "map065_少林寺.map", 1, output, "709-shaolin-event410-exit-refused")
    transition(client, resource, "map067_藏经阁.map", 3, output, "710-native-library-lesson-entry")
    client.save_or_load(4)
    state = interact_at(client, output, resource, "无穷", "script/map/map067_藏经阁/无穷对话.txt", "711-native-demon-disintegration-lesson")
    if state["variables"].get("Event") != "420":
        raise AutomationError("Native Wuqiong lesson did not advance Event420")
    interact_at(client, output, resource, "无穷", "script/map/map067_藏经阁/无穷对话.txt", "712-wuqiong-completed-repeat")
    interact_at(client, output, resource, "张琳心", "script/map/map067_藏经阁/张琳心对话.txt", "713-library-event420")
    transition(client, resource, "map065_少林寺.map", 1, output, "714-native-library-return")
    transition(client, resource, "map064_少室山下.map", 1, output, "715-native-shaolin-complete-exit")
    interact_at(client, output, resource, "樵夫", "script/map/map064_少室山下/樵夫对话.txt", "716-woodcutter-event420")
    interact_at(client, output, resource, "张琳心", "script/map/map064_少室山下/张琳心对话.txt", "717-mountain-event420")
    state = transition(client, resource, "map070_朱仙镇西北.map", 2, output, "718-native-zhuxian-arrival")
    if state["variables"].get("End") != source_end or state["variables"].get("Event") != "420":
        raise AutomationError("Native Zhu Xian arrival changed End or story progress")
    client.save_or_load(6)
    write_json(output / "shaolin-traitor-and-lessons-proof.json", dict(status="passed", Event="420", End=source_end,
               nativeFalseMonkVictory=True, nativeWuxiangVictory=True, nativePlayerMagic=True,
               nativePartnerMagic=True, nativeDemonLesson=True, nativeZhuXianArrival=True,
               damoRevisitRewardDelta=medicine_delta, sourceSlots=[0, 1, 2, 3, 4, 6], cheatAssisted=True))
    print("Native Shaolin traitor battles and lessons passed; Event=420", flush=True)


def han_camp_first_visit(client, output, resource, yue_first):
    state = idle(client)
    if state["map"] == "map073_韩世忠大营.map" and (output / "765-han-warehouse-before-permission-script-proof.json").exists():
        script_proof(output, resource, "script/map/map073_韩世忠大营/trap04.txt")
    else:
        transition(client, resource, "map073_韩世忠大营.map", 2, output, "760-native-han-camp")
        transition(client, resource, "map072_朱仙镇东北.map", 1, output, "761-han-before-gate-free-return")
        transition(client, resource, "map073_韩世忠大营.map", 2, output, "762-han-camp-reentry")
        state = transition(client, resource, "map073_韩世忠大营.map", 4, output, "763-native-han-guard-admission")
        if state["variables"].get("Talkliang") != "1":
            raise AutomationError("Native Han guards did not admit the companion at Talkliang1")
        transition(client, resource, "map073_韩世忠大营.map", 1, output, "764-han-wife-not-met-exit-refused")
        transition(client, resource, "map073_韩世忠大营.map", 3, output, "765-han-warehouse-before-permission")
    for number, position in ((1, (12, 36)), (2, (15, 35)), (3, (16, 34)), (6, (20, 34)),
                             (7, (27, 28)), (8, (26, 42)), (4, (18, 18)), (5, (16, 15))):
        prefix, source = f"766-han-soldier-{number}", f"script/map/map073_韩世忠大营/宋兵对话{number}.txt"
        if (output / f"{prefix}-script-proof.json").exists():
            script_proof(output, resource, source)
            continue
        try:
            interact_at(client, output, resource, "守卫1" if number == 1 else "宋兵", source, prefix, position=position)
        except AutomationError as error:
            if number != 8 or not any(reason in str(error) for reason in ("interaction_not_observed", "blocked_destination")):
                raise
            for x, y in ((27, 29), (26, 27), (23, 33)):
                state = idle(client)
                client.act("MoveTo", generation=state["generation"], x=x, y=y, running=True, timeoutMs=6000)
                frame = client.observe()["frame"]
                client.wait_until(lambda value: value["frame"] >= frame + 120, timeout=5, description="native partner follow after corridor detour")
            checkpoint(client, output, prefix + "-native-corridor-detour")
            interact_at(client, output, resource, "宋兵", source, prefix, position=position)
    interact_at(client, output, resource, "张琳心", "script/map/map073_韩世忠大营/张琳心对话.txt", "767-han-camp-lin-before-wife")
    transition(client, resource, "map074_韩世忠主营帐.map", 2, output, "768-native-han-wife-tent")
    transition(client, resource, "map074_韩世忠主营帐.map", 1, output, "769-wife-not-met-tent-exit-refused")
    interact_at(client, output, resource, "丫环1", "script/map/map074_韩世忠主营帐/侍女对话.txt", "770-han-maid", position=(7, 17))
    interact_at(client, output, resource, "张琳心", "script/map/map074_韩世忠主营帐/张琳心对话.txt", "771-han-wife-tent-lin-before")
    state = interact_at(client, output, resource, "梁红玉", "script/map/map074_韩世忠主营帐/梁红玉对话.txt", "772-native-han-wife-warning")
    if state["variables"].get("Talkliang") != "2":
        raise AutomationError("Native Han warning did not leave Lin with the wife")
    client.save_or_load(2)
    partner_save = configparser.ConfigParser()
    if not partner_save.read(output / "user-data/save/xjxqy/rpg3/partner0.ini", encoding="utf-8-sig") or any(name.lower() != "head" for name in partner_save.sections()):
        raise AutomationError("Native saved partner list still contains Lin after she stays with the wife")
    interact_at(client, output, resource, "梁红玉", "script/map/map074_韩世忠主营帐/梁红玉对话.txt", "773-han-wife-warning-repeat")
    interact_at(client, output, resource, "张琳心", "script/map/map074_韩世忠主营帐/张琳心对话.txt", "774-lin-waits-with-wife")
    state = transition(client, resource, "map073_韩世忠大营.map", 1, output, "775-native-han-stroll-event421")
    if state["variables"].get("Event") != "421":
        raise AutomationError("Native first Han tent departure did not reach Event421")
    transition(client, resource, "map073_韩世忠大营.map", 1, output, "776-han-companion-left-behind-exit-refused")
    transition(client, resource, "map074_韩世忠主营帐.map", 2, output, "777-native-lin-pickup")
    client.save_or_load(3)
    for name, slot in (("梁红玉", 4), ("张琳心", 5)):
        if slot == 5:
            load_checkpoint(client, 3)
        state = interact_at(client, output, resource, name, f"script/map/map074_韩世忠主营帐/{name}对话.txt", f"778-native-adoption-{name}")
        if (state["variables"].get("Event") != ("430" if yue_first else "421")
                or state["variables"].get("Talkliang") != ("2" if yue_first else "3")):
            raise AutomationError("Native adoption did not produce its camp-order-specific state and partner return")
        interact_at(client, output, resource, "梁红玉", "script/map/map074_韩世忠主营帐/梁红玉对话.txt", f"779-adoption-wife-repeat-{slot}")
        interact_at(client, output, resource, "张琳心", "script/map/map074_韩世忠主营帐/张琳心对话.txt", f"780-adoption-lin-repeat-{slot}")
        client.save_or_load(slot)
        partner_save = configparser.ConfigParser()
        partner_save.read(output / "user-data/save/xjxqy" / f"rpg{slot + 1}/partner0.ini", encoding="utf-8-sig")
        partners = [name for name in partner_save.sections() if name.lower() != "head"]
        if len(partners) != 1 or partner_save[partners[0]].get("name") != "张琳心" or partner_save.getint(partners[0], "kind") != 3:
            raise AutomationError("Native saved partner list did not restore Lin after adoption")
    transition(client, resource, "map073_韩世忠大营.map", 1, output, "781-native-adopted-companion-departure")
    interact_at(client, output, resource, "张琳心", "script/map/map073_韩世忠大营/张琳心对话.txt", "782-han-camp-lin-after-adoption")
    return transition(client, resource, "map072_朱仙镇东北.map", 1, output, "783-native-han-camp-departure")


def yue_camp_first_visit(client, output, resource, yue_first, save_tent=False):
    transition(client, resource, "map076_岳飞大营.map", 3, output, "790-native-yue-camp")
    transition(client, resource, "map076_岳飞大营.map", 1, output, "791-yue-before-warning-exit-refused")
    transition(client, resource, "map076_岳飞大营.map", 4, output, "792-native-yue-guard-admission")
    interact_at(client, output, resource, "张琳心", "script/map/map076_岳飞大营/张琳心对话.txt", "793-yue-camp-lin-before-warning")
    for number, name, position in ((1, "守卫1", (22, 64)), (2, "守卫3", (15, 24)), (3, "宋兵", (20, 38)),
                                    (4, "宋兵", (20, 39)), (5, "宋兵", (16, 41))):
        interact_at(client, output, resource, name, f"script/map/map076_岳飞大营/宋兵对话{number}.txt", f"794-yue-soldier-{number}", position=position)
    interact_at(client, output, resource, "牛皋", "script/map/map076_岳飞大营/牛皋对话.txt", "795-niugao-before-warning")
    transition(client, resource, "map077_岳飞主帐营.map", 2, output, "796-native-yue-main-tent")
    if save_tent:
        before = checkpoint(client, output, "796-native-yue-tent-before-save")
        client.save_or_load(0)
        load_checkpoint(client, 0)
        after = checkpoint(client, output, "796-native-yue-tent-after-load")
        if after["map"] != before["map"] or after["player"]["position"] != before["player"]["position"]:
            raise AutomationError("Native Yue tent save/load did not retain map and position")
    interact_at(client, output, resource, "张琳心", "script/map/map077_岳飞主帐营/张琳心对话.txt", "797-yue-tent-lin-before-warning")
    before = quantity(idle(client), "goods401_血书.ini")
    if before != 1:
        raise AutomationError("Native first Yue visit requires the original single blood letter")
    state = interact_at(client, output, resource, "岳云", "script/map/map077_岳飞主帐营/岳云对话.txt", "798-native-blood-letter-warning")
    if (quantity(state, "goods401_血书.ini") != 0 or state["variables"].get("Talkyueyun") != "1"
            or state["variables"].get("Event") != ("420" if yue_first else "430")):
        raise AutomationError("Native Yue warning did not consume the letter and produce its order-specific event")
    interact_at(client, output, resource, "岳云", "script/map/map077_岳飞主帐营/岳云对话.txt", "799-yue-warning-repeat")
    interact_at(client, output, resource, "张琳心", "script/map/map077_岳飞主帐营/张琳心对话.txt", "800-yue-tent-lin-after-warning")
    transition(client, resource, "map076_岳飞大营.map", 1, output, "801-native-yue-tent-departure")
    interact_at(client, output, resource, "张琳心", "script/map/map076_岳飞大营/张琳心对话.txt", "802-yue-camp-lin-after-warning")
    return transition(client, resource, "map072_朱仙镇东北.map", 1, output, "803-native-yue-camp-departure")


def camps_first_visit(client, output, resource, yue_first=False):
    state = idle(client)
    resuming_han = (state["map"] == "map073_韩世忠大营.map" and state["variables"].get("Talkliang") == "1"
                    and (output / "765-han-warehouse-before-permission-script-proof.json").exists())
    if (state["variables"].get("Event") != "420" or state["variables"].get("Talkyueyun") not in (("1",) if resuming_han and yue_first else ("", "0"))
            or not resuming_han and (state["map"] != "map072_朱仙镇东北.map" or state["variables"].get("Talkliang") not in ("", "0"))):
        raise AutomationError("Camp order testing requires the untouched native fork source")
    source_end, source_money = state["variables"]["End"], state["player"]["money"]
    client.act("SetAutoDialogue", enabled=True)
    if not resuming_han:
        client.save_or_load(0)
    for visit in ((yue_camp_first_visit, han_camp_first_visit) if yue_first else (han_camp_first_visit, yue_camp_first_visit)):
        if resuming_han and yue_first and visit == yue_camp_first_visit:
            script_proof(output, resource, "script/map/map077_岳飞主帐营/岳云对话.txt")
            script_proof(output, resource, "script/map/map076_岳飞大营/trap01.txt")
            continue
        visit(client, output, resource, yue_first)
        interact_at(client, output, resource, "张琳心", "script/map/map072_朱仙镇东北/张琳心对话.txt", f"804-fork-after-{visit.__name__}")
        client.save_or_load(1 if visit == (yue_camp_first_visit if yue_first else han_camp_first_visit) else 2)
    state = idle(client)
    if state["variables"].get("Event") != "430" or state["variables"].get("End") != source_end or state["player"]["money"] != source_money:
        raise AutomationError("Both native camp visits did not converge at Event430 with unchanged End and money")
    transition(client, resource, "map072_朱仙镇东北.map", 2, output, "805-both-camps-han-revisit-refused")
    transition(client, resource, "map072_朱仙镇东北.map", 3, output, "806-both-camps-yue-revisit-refused")
    state = transition(client, resource, "map071_朱仙镇.map", 1, output, "807-native-both-camps-town-return")
    interact_at(client, output, resource, "张琳心", "script/map/map071_朱仙镇/张琳心对话.txt", "808-town-after-both-camps")
    client.save_or_load(6)
    order = "yue-first" if yue_first else "han-first"
    write_json(output / f"camps-{order}-proof.json", dict(status="passed", Event="430", End=source_end,
               order=order, originalLetterConsumedOnce=True, nativeBothAdoptionActors=True, partnerReturned=True,
               bothRevisitsRefused=True, nativeTownReturn=True, sourceSlots=[0, 1, 2, 3, 4, 5, 6], cheatAssisted=True))
    print(f"Native camp order {order} passed; Event=430", flush=True)


def zhuxian_first_inquiry(client, output, resource):
    state = idle(client)
    if state["map"] != "map070_朱仙镇西北.map" or state["variables"].get("Event") != "420":
        raise AutomationError("Zhuxian inquiry requires its native post-Shaolin arrival")
    source_end, source_money = state["variables"]["End"], state["player"]["money"]
    client.act("SetAutoDialogue", enabled=True)
    for number in (1, 2):
        state = interact_at(client, output, resource, "钓鱼翁", "script/map/map070_朱仙镇西北/钓鱼翁对话.txt", f"720-fisherman-{number}")
        if state["variables"].get("Talkdiaoyu") != "1":
            raise AutomationError("Native fisherman counter did not stay at one")
        state = interact_at(client, output, resource, "张琳心", "script/map/map070_朱仙镇西北/张琳心对话.txt", f"721-northwest-lin-{number}")
        if state["variables"].get("Talkmap070") != "1":
            raise AutomationError("Native northwest companion counter did not stay at one")
    transition(client, resource, "map071_朱仙镇.map", 2, output, "722-native-zhuxian-town")
    client.save_or_load(0)
    transition(client, resource, "map071_朱仙镇.map", 1, output, "723-town-premature-exit-refused")
    interact_at(client, output, resource, "张琳心", "script/map/map071_朱仙镇/张琳心对话.txt", "724-town-lin")
    residents = (("陈原", "朱仙镇居民3之陈原对话.txt"), ("润娘", "朱仙镇居民5之润娘对话.txt"),
                 ("店小二", "朱仙镇居民7之店小二对话.txt"), ("牛牛", "朱仙镇居民4之牛牛对话.txt"),
                 ("乔奇", "朱仙镇居民2之乔奇对话.txt"), ("包怀民", "朱仙镇居民8之包怀民对话.txt"),
                 ("葛老太", "朱仙镇居民6之葛老太对话.txt"), ("丁雅心", "朱仙镇居民9之丁雅心对话.txt"),
                 ("夫妻", "朱仙镇居民夫妻对话.txt"))
    for name, filename in residents:
        for number in (1, 2):
            interact_at(client, output, resource, name, f"script/map/map071_朱仙镇/{filename}", f"725-town-{name}-{number}")
    shops_before = len(list(output.glob("native-shop-*-before.json")))
    state = interact_at(client, output, resource, "买卖人", "script/map/map071_朱仙镇/朱仙镇买卖人对话.txt", "726-town-three-shops", shop_cancel=True)
    if len(list(output.glob("native-shop-*-before.json"))) != shops_before + 3 or state["player"]["money"] != source_money:
        raise AutomationError("Native town merchant did not open and cancel all three shops without charge")
    movie_source = "script/map/map071_朱仙镇/朱仙镇居民1之说书人对话.txt"
    state = interact_at(client, output, resource, "说书人", movie_source, "727-native-eight-hammers-story", capture_videos=True)
    movies = [json.loads(path.read_text(encoding="utf-8")) for path in output.glob("native-video-*.json")]
    if len(movies) != 1 or Path(movies[0].get("video", "")).name.casefold() != "shuoshu-ba.wmv":
        raise AutomationError("Native storyteller movie lacks its presented video evidence")
    commands = [json.loads(line) for line in (output / "commands.jsonl").read_text(encoding="utf-8").splitlines()]
    if any(row["request"]["command"] == "SendUIAction"
           and row["request"]["arguments"].get("context") == movies[0]["context"]
           and row["request"]["arguments"].get("action") == "Cancel" for row in commands):
        raise AutomationError("Native storyteller movie was skipped")
    transition(client, resource, "map200_朱仙镇民居.map", 5, output, "728-town-house")
    interact_at(client, output, resource, "张琳心", "script/map/map200_朱仙镇民居/张琳心对话.txt", "729-house-lin")
    transition(client, resource, "map071_朱仙镇.map", 1, output, "730-house-return")
    transition(client, resource, "map079_朱仙镇茶馆.map", 3, output, "731-teahouse-before-luqing")
    interact_at(client, output, resource, "茶馆老板", "script/map/map079_朱仙镇茶馆/茶馆老板对话.txt", "732-teahouse-owner")
    interact_at(client, output, resource, "张琳心", "script/map/map079_朱仙镇茶馆/张琳心对话.txt", "733-teahouse-lin")
    transition(client, resource, "map071_朱仙镇.map", 1, output, "734-teahouse-return")
    transition(client, resource, "map085_朱仙镇客栈.map", 4, output, "735-inn-before-camps")
    interact_at(client, output, resource, "客栈掌柜", "script/map/map085_朱仙镇客栈/客栈掌柜对话.txt", "736-inn-owner")
    interact_at(client, output, resource, "张琳心", "script/map/map085_朱仙镇客栈/张琳心对话.txt", "737-inn-lin")
    goods = ("goods209_玄参方.ini", "goods042_蝉翼纱衣.ini")
    before = {name: quantity(idle(client), name) for name in goods}
    state = interact_at(client, output, resource, "宝箱", "script/map/map085_朱仙镇客栈/宝箱.txt", "738-inn-chest")
    if any(quantity(state, name) != before[name] + 1 for name in goods):
        raise AutomationError("Native inn chest did not award both items once")
    target = next(row for row in state["targets"] if row["name"] == "宝箱")
    try:
        client.interact(target["id"])
    except AutomationError as error:
        if "action_rejected" not in str(error):
            raise
    else:
        raise AutomationError("Native inn chest accepted a repeated reward interaction")
    transition(client, resource, "map071_朱仙镇.map", 1, output, "739-inn-return")
    transition(client, resource, "map072_朱仙镇东北.map", 2, output, "740-native-camp-fork")
    interact_at(client, output, resource, "张琳心", "script/map/map072_朱仙镇东北/张琳心对话.txt", "741-camp-fork-lin")
    state = transition(client, resource, "map072_朱仙镇东北.map", 1, output, "742-camp-fork-town-return-refused")
    if (state["variables"].get("End") != source_end or state["variables"].get("Event") != "420"
            or state["player"]["money"] != source_money):
        raise AutomationError("Native town inquiry changed its main story source or money")
    client.save_or_load(6)
    write_json(output / "zhuxian-first-inquiry-proof.json", dict(status="passed", Event="420", End=source_end,
               residentDialogues=18, allThreeShopCancellations=True, storytellerVideo="shuoshu-ba.wmv", videoSkipped=False,
               innChestItems=2, repeatChestRejected=True, nativeCampFork=True, sourceSlots=[0, 6], cheatAssisted=True))
    print("Native Zhuxian inquiry and storyteller video passed; both camp orders source saved", flush=True)



def zhuxian_overnight(client, output, resource):
    state = idle(client)
    if state["map"] != "map071_朱仙镇.map" or state["variables"].get("Event") != "430":
        raise AutomationError("Overnight chapter requires native Event430 town source")
    source_end, source_money = state["variables"]["End"], state["player"]["money"]
    sword, ruby = "goods048_丹心剑.ini", "goods402_红玉.ini"
    source_swords, source_ruby = quantity(state, sword), quantity(state, ruby)
    client.act("SetAutoDialogue", enabled=True)
    transition(client, resource, "map071_朱仙镇.map", 1, output, "810-town-event430-west-refused")
    transition(client, resource, "map085_朱仙镇客栈.map", 4, output, "811-inn-before-overnight")
    interact_at(client, output, resource, "客栈掌柜", "script/map/map085_朱仙镇客栈/客栈掌柜对话.txt", "812-inn-owner-event430")
    interact_at(client, output, resource, "张琳心", "script/map/map085_朱仙镇客栈/张琳心对话.txt", "813-inn-lin-event430")
    transition(client, resource, "map071_朱仙镇.map", 1, output, "814-native-tea-request-return")
    transition(client, resource, "map079_朱仙镇茶馆.map", 3, output, "815-native-luqing-arrival")
    interact_at(client, output, resource, "茶馆老板", "script/map/map079_朱仙镇茶馆/茶馆老板对话.txt", "816-tea-owner-event430")
    interact_at(client, output, resource, "张琳心", "script/map/map079_朱仙镇茶馆/张琳心对话.txt", "817-tea-lin-event430")
    client.save_or_load(0)
    state = interact_at(client, output, resource, "卢青", "script/map/map079_朱仙镇茶馆/卢青对话.txt", "818-native-sword-and-overnight")
    expected_money = max(0, source_money - 10)
    if (state["map"] != "map085_朱仙镇客栈.map" or state["variables"].get("Event") != "440"
            or state["variables"].get("End") != source_end or state["player"]["money"] != expected_money
            or quantity(state, sword) != source_swords + 1 or quantity(state, ruby) != source_ruby):
        raise AutomationError("Native overnight changed its sword, money, ruby or story state incorrectly")
    client.save_or_load(1)
    party = configparser.ConfigParser()
    party.read(output / "user-data/save/xjxqy/rpg2/partner0.ini", encoding="utf-8-sig")
    if any(name.lower() != "head" for name in party.sections()):
        raise AutomationError("Lin still saved as partner while awaiting her inn dialogue")
    transition(client, resource, "map085_朱仙镇客栈.map", 1, output, "819-inn-companion-not-called-refused")
    for repeat in (1, 2):
        interact_at(client, output, resource, "客栈掌柜", "script/map/map085_朱仙镇客栈/客栈掌柜对话.txt", f"820-inn-owner-event440-{repeat}")
    state = interact_at(client, output, resource, "张琳心", "script/map/map085_朱仙镇客栈/张琳心对话.txt", "821-native-inn-partner-return")
    if state["variables"].get("zhang") != "1":
        raise AutomationError("Native inn companion did not enable the departure")
    interact_at(client, output, resource, "张琳心", "script/map/map085_朱仙镇客栈/张琳心对话.txt", "822-inn-partner-repeat")
    client.save_or_load(2)
    party.read(output / "user-data/save/xjxqy/rpg3/partner0.ini", encoding="utf-8-sig")
    partners = [name for name in party.sections() if name.lower() != "head"]
    if len(partners) != 1 or party[partners[0]].get("name") != "张琳心" or party.getint(partners[0], "kind") != 3:
        raise AutomationError("Native inn partner return was not saved")
    transition(client, resource, "map071_朱仙镇.map", 1, output, "823-native-inn-departure")
    transition(client, resource, "map071_朱仙镇.map", 1, output, "824-town-event440-west-refused")
    transition(client, resource, "map079_朱仙镇茶馆.map", 3, output, "825-tea-event440-revisit")
    state = idle(client)
    if any(target["name"] == "卢青" for target in state["targets"]) or quantity(state, sword) != source_swords + 1:
        raise AutomationError("Native teahouse revisit recreated Lu Qing or repeated his reward")
    interact_at(client, output, resource, "张琳心", "script/map/map079_朱仙镇茶馆/张琳心对话.txt", "826-tea-lin-event440")
    transition(client, resource, "map071_朱仙镇.map", 1, output, "827-tea-event440-return")
    transition(client, resource, "map072_朱仙镇东北.map", 2, output, "828-native-event440-fork")
    state = transition(client, resource, "map073_韩世忠大营.map", 2, output, "829-native-before-liuyun-source")
    if state["variables"].get("Talkliuyun") not in ("", "0") or state["variables"].get("End") != source_end:
        raise AutomationError("Native pre-Liu Yun source already consumed the branch")
    client.save_or_load(6)
    write_json(output / "zhuxian-overnight-proof.json", dict(status="passed", Event="440", End=source_end,
               sourceMoney=source_money, finalMoney=expected_money, swordDelta=1, sourceRuby=source_ruby,
               nativePartnerReturn=True, prematureInnExitRefused=True, swordRepeatRefused=True,
               sourceSlots=[0, 1, 2, 6], cheatAssisted=True))
    print("Native Lu Qing sword and overnight passed; Event440 guard branch source saved", flush=True)


def han_red_jade_visit(client, output, resource, has_ruby):
    state = idle(client)
    ruby = "goods402_红玉.ini"
    if (state["map"] != "map073_韩世忠大营.map" or state["variables"].get("Event") != "440"
            or state["variables"].get("Talkliuyun") not in ("", "0") or quantity(state, ruby) != int(has_ruby)):
        raise AutomationError("Han jade chapter requires its native pre-guard Event440 source")
    source_end, source_money = int(state["variables"]["End"]), state["player"]["money"]
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    state = transition(client, resource, "map073_韩世忠大营.map", 4, output, "830-native-liuyun-jade-gate")
    if (state["variables"].get("Talkliuyun") != "1" or state["variables"].get("GoodsNum") != str(int(has_ruby))
            or int(state["variables"]["End"]) != source_end + int(has_ruby) or quantity(state, ruby) != 0):
        raise AutomationError("Native Liu Yun gate did not consume the single jade and End increment correctly")
    client.save_or_load(1)
    triggered = configparser.ConfigParser(interpolation=None)
    triggered.read(output / "user-data/save/xjxqy/rpg2/trapindexignore.ini", encoding="utf-8-sig")
    if "4" not in triggered["init"].values():
        raise AutomationError("Native gate save did not preserve its triggered index")
    before = records(output)[-1]["sequence"]
    for trap in (0, 4):
        state = idle(client)
        occupied = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]}
        client.move(*reachable_trap(resource, state["map"], trap, state["player"]["position"], occupied))
    state = checkpoint(client, output, "831-liuyun-gate-repeat-suppressed")
    if (any(row.get("eventType") == "script.start" and row["sequence"] > before for row in records(output))
            or int(state["variables"]["End"]) != source_end + int(has_ruby) or quantity(state, ruby) != 0):
        raise AutomationError("A triggered gate repeated its script or changed its reward state")
    write_json(output / "831-liuyun-gate-repeat-proof.json", dict(status="passed", nativeTriggeredIndex=4,
               nativeWalkOutAndBack=True, newScriptExecution=False, EndUnchanged=True, rubyUnchanged=True,
               cheatAssisted=True))
    for repeat in (1, 2):
        state = interact_at(client, output, resource, "刘云", "script/map/map073_韩世忠大营/刘云对话.txt", f"832-liuyun-repeat-{repeat}")
    if int(state["variables"]["End"]) != source_end + int(has_ruby):
        raise AutomationError("Liu Yun repeat increased End again")
    if has_ruby:
        transition(client, resource, "map073_韩世忠大营.map", 1, output, "833-han-commander-not-met-exit-refused")
        transition(client, resource, "map073_韩世忠大营.map", 3, output, "834-han-warehouse-permission-refused")
        transition(client, resource, "map074_韩世忠主营帐.map", 2, output, "835-native-han-commander-entry")
        transition(client, resource, "map074_韩世忠主营帐.map", 1, output, "836-han-commander-not-met-tent-exit-refused")
        for repeat in (1, 2):
            interact_at(client, output, resource, "梁红玉", "script/map/map074_韩世忠主营帐/梁红玉对话.txt", f"837-wife-event440-{repeat}")
            state = interact_at(client, output, resource, "韩世忠", "script/map/map074_韩世忠主营帐/韩世忠对话.txt", f"838-native-han-permission-{repeat}")
        if state["variables"].get("Talkhan") != "1":
            raise AutomationError("Native Han commander did not grant the warehouse permission")
        client.save_or_load(2)
        transition(client, resource, "map073_韩世忠大营.map", 1, output, "839-native-han-permitted-return")
        transition(client, resource, "map073_韩世忠大营.map", 1, output, "840-han-warehouse-not-visited-exit-refused")
        transition(client, resource, "map075_韩世忠库房.map", 3, output, "841-native-han-warehouse-entry")
        before = idle(client)
        goods = ("goods050_云腾剑.ini", "goods059_柔云剑.ini", "goods063_蚊须针.ini")
        quantities = [quantity(before, name) for name in goods]
        for filename, position in (("宝箱.txt", (5, 13)), ("宝箱1.txt", (6, 15)), ("宝箱2.txt", (7, 17))):
            interact_at(client, output, resource, "宝箱", f"script/map/map075_韩世忠库房/{filename}", f"842-native-warehouse-{filename}", position=position)
            target = next(row for row in idle(client)["targets"] if row["name"] == "宝箱" and row["position"] == dict(x=position[0], y=position[1]))
            try:
                client.interact(target["id"])
            except AutomationError as error:
                if "action_rejected" not in str(error):
                    raise
            else:
                raise AutomationError("Han warehouse chest accepted a repeated reward interaction")
        state = interact_at(client, output, resource, "张琳心", "script/map/map075_韩世忠库房/张琳心对话.txt", "843-warehouse-lin")
        if state["player"]["money"] != source_money + 500 or [quantity(state, name) - number for name, number in zip(goods, quantities)] != [1, 1, 6]:
            raise AutomationError("Native Han warehouse did not award exactly500 and its three goods quantities")
        client.save_or_load(3)
        state = transition(client, resource, "map073_韩世忠大营.map", 1, output, "844-native-warehouse-return")
        if state["variables"].get("Kufang") != "1":
            raise AutomationError("Native warehouse departure did not open the final camp exit")
    state = transition(client, resource, "map072_朱仙镇东北.map", 1, output, "845-native-han-event440-departure")
    if state["variables"].get("Event") != "440" or int(state["variables"]["End"]) != source_end + int(has_ruby):
        raise AutomationError("Native Han jade branch departure changed its final story state")
    client.save_or_load(6)
    name = "han-red-jade" if has_ruby else "han-no-red-jade"
    write_json(output / f"{name}-proof.json", dict(status="passed", Event="440", End=state["variables"]["End"],
               sourceRuby=int(has_ruby), returnedRuby=has_ruby, nativeEndDelta=int(has_ruby), repeatEndDelta=0,
               warehouseVisited=has_ruby, warehouseMoneyDelta=500 if has_ruby else 0,
               sourceSlots=[0, 1, 2, 3, 6] if has_ruby else [0, 1, 6], cheatAssisted=True))
    print("Native Han jade branch passed:", name, flush=True)


def han_no_red_jade_bypass(client, output, resource):
    state = idle(client)
    if (state["map"] != "map073_韩世忠大营.map" or state["variables"].get("Event") != "440"
            or state["variables"].get("Talkliuyun") != "1" or state["variables"].get("GoodsNum") != "0"
            or quantity(state, "goods402_红玉.ini") != 0):
        raise AutomationError("Jade refusal walking check needs the native post-refusal source")
    source_end, position = state["variables"]["End"], state["player"]["position"]
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    blocked = []
    for trap in (2, 3):
        state = idle(client)
        occupied = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]}
        occupied.discard((position["x"], position["y"]))
        try:
            reachable_trap(resource, state["map"], trap, position, occupied, avoid=occupied)
        except AutomationError as error:
            if "No connected trap" not in str(error):
                raise
        else:
            raise AutomationError("Refused Han entry has a connected unoccupied walking path")
        destination = reachable_trap(resource, state["map"], trap, position, occupied)
        before = records(output)[-1]["sequence"]
        try:
            client.move(*destination, timeout=30)
        except AutomationError as error:
            if "no_progress" not in str(error):
                raise
        else:
            raise AutomationError("Native walking bypassed the no-jade guard")
        state = checkpoint(client, output, f"850-no-jade-trap{trap}-walking-blocked")
        if (state["player"]["position"] != position or state["variables"]["End"] != source_end
                or any(row.get("eventType") == "script.start" and row["sequence"] > before for row in records(output))):
            raise AutomationError("Blocked walking changed its map entry or story state")
        blocked.append(dict(trap=trap, destination=destination, reason="no_progress", positionUnchanged=True))
    state = transition(client, resource, "map072_朱仙镇东北.map", 1, output, "851-no-jade-blocked-return")
    if state["variables"]["End"] != source_end or quantity(state, "goods402_红玉.ini") != 0:
        raise AutomationError("No-jade departure changed its End or jade state")
    client.save_or_load(6)
    write_json(output / "han-no-red-jade-bypass-proof.json", dict(status="passed", nativeWalkOnly=True,
               refusalBypassed=False, occupiedWalkingPathsDisconnected=True, nativeBlockedMoves=blocked,
               EndUnchanged=True, rubyUnchanged=True, sourceSlots=[0, 6], cheatAssisted=True))
    print("Native walking cannot bypass the no-jade guard; normal departure passed", flush=True)


def yue_gold_clothes(client, output, resource):
    state = idle(client)
    if state["map"] != "map072_朱仙镇东北.map" or state["variables"].get("Event") != "440":
        raise AutomationError("Yue gold clothes chapter requires native Event440 at the camp fork")
    source_end, source_money = state["variables"]["End"], state["player"]["money"]
    gold, helmet = "goods408_金服.ini", "goods006_凌云盔.ini"
    gold_count, helmet_count = quantity(state, gold), quantity(state, helmet)
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    transition(client, resource, "map076_岳飞大营.map", 3, output, "860-yue-event440-entry")
    transition(client, resource, "map076_岳飞大营.map", 1, output, "861-yue-before-commander-exit-refused")
    interact_at(client, output, resource, "张琳心", "script/map/map076_岳飞大营/张琳心对话.txt", "862-yue-camp-lin440")
    transition(client, resource, "map078_议事厅.map", 5, output, "863-yue-meeting-hall440")
    for name in ("张宪", "汤怀", "张琳心"):
        interact_at(client, output, resource, name, f"script/map/map078_议事厅/{name}对话.txt", f"864-hall440-{name}")
    for filename, position in (("宝箱1.txt", (4, 12)), ("宝箱.txt", (7, 24))):
        interact_at(client, output, resource, "宝箱", f"script/map/map078_议事厅/{filename}", f"865-hall-{filename}", position=position)
        target = next(row for row in idle(client)["targets"] if row["name"] == "宝箱" and row["position"] == dict(x=position[0], y=position[1]))
        try:
            client.interact(target["id"])
        except AutomationError as error:
            if "action_rejected" not in str(error):
                raise
        else:
            raise AutomationError("Meeting hall chest allowed a repeated reward")
    transition(client, resource, "map076_岳飞大营.map", 1, output, "866-hall440-return")
    transition(client, resource, "map077_岳飞主帐营.map", 2, output, "867-yue-commander440-entry")
    transition(client, resource, "map077_岳飞主帐营.map", 1, output, "868-yue-tent-before-talk-exit-refused")
    for name in ("岳云", "张琳心"):
        interact_at(client, output, resource, name, f"script/map/map077_岳飞主帐营/{name}对话.txt", f"869-tent440-{name}")
    client.save_or_load(1)
    state = interact_at(client, output, resource, "岳飞", "script/map/map077_岳飞主帐营/岳飞对话.txt", "870-native-yue-gold-clothes")
    if state["variables"].get("Event") != "450" or quantity(state, gold) != gold_count + 2:
        raise AutomationError("Yue commander did not advance Event450 and give two gold clothes")
    for repeat in (1, 2):
        state = interact_at(client, output, resource, "岳飞", "script/map/map077_岳飞主帐营/岳飞对话.txt", f"871-yue-repeat450-{repeat}")
    for name in ("岳云", "张琳心"):
        interact_at(client, output, resource, name, f"script/map/map077_岳飞主帐营/{name}对话.txt", f"872-tent450-{name}")
    client.save_or_load(2)
    transition(client, resource, "map076_岳飞大营.map", 1, output, "873-yue450-tent-departure")
    transition(client, resource, "map076_岳飞大营.map", 1, output, "874-yue450-wrong-direction-refused")
    interact_at(client, output, resource, "张琳心", "script/map/map076_岳飞大营/张琳心对话.txt", "875-yue-camp-lin450")
    transition(client, resource, "map078_议事厅.map", 5, output, "876-yue-meeting-hall450")
    for name in ("张宪", "汤怀", "张琳心"):
        interact_at(client, output, resource, name, f"script/map/map078_议事厅/{name}对话.txt", f"877-hall450-{name}")
    state = idle(client)
    if (state["variables"]["End"] != source_end or quantity(state, gold) != gold_count + 2
            or quantity(state, helmet) != helmet_count + 1 or state["player"]["money"] != source_money + 500):
        raise AutomationError("Repeated Yue visit changed clothes, End, or meeting hall reward quantities")
    transition(client, resource, "map076_岳飞大营.map", 1, output, "878-hall450-return")
    state = transition(client, resource, "map081_凤凰山.map", 3, output, "879-native-phoenix-entry")
    client.save_or_load(6)
    write_json(output / "yue-gold-clothes-proof.json", dict(status="passed", Event="450", End=source_end,
               nativeGoldClothesDelta=2, repeatGoldClothesDelta=0, meetingHallMoneyDelta=500,
               meetingHallHelmetDelta=1, nativePhoenixEntry=True, sourceSlots=[0, 1, 2, 6], cheatAssisted=True))
    print("Native Yue commander and gold clothes passed; Phoenix Event450 source saved", flush=True)


def jin_infiltration(client, output, resource):
    state = idle(client)
    continuing = state["map"] == "map082_金兵大营.map" and state["variables"].get("Event") == "460"
    if not continuing and (state["map"] != "map081_凤凰山.map" or state["variables"].get("Event") != "450"):
        raise AutomationError("Jin infiltration needs its Phoenix source or current native camp-jump continuation")
    source_end = state["variables"]["End"]
    gold, sword, armor = "goods408_金服.ini", "goods048_丹心剑.ini", "goods039_子义之痕.ini"
    gold_count, sword_count, armor_count = (quantity(state, name) for name in (gold, sword, armor))
    if gold_count != (0 if continuing else 2) or sword_count < 1:
        raise AutomationError("Infiltration source needs two native gold clothes and the Danxin sword")
    client.act("SetAutoDialogue", enabled=True)
    if continuing:
        load_checkpoint(client, 2)
        script_proof(output, resource, "script/map/map081_凤凰山/trap02.txt")
        script_proof(output, resource, "script/map/map082_金兵大营/trap06.txt")
    else:
        client.save_or_load(0)
        for repeat in (1, 2):
            interact_at(client, output, resource, "张琳心", "script/map/map081_凤凰山/张琳心对话.txt", f"880-phoenix-lin-{repeat}")
        transition(client, resource, "map081_凤凰山.map", 1, output, "881-phoenix-backtrack-refused")
        # Restore the native branch save after the narrow backtrack, where the
        # following companion can occupy the only forward walking tile.
        load_checkpoint(client, 0)
        transition(client, resource, "map081_凤凰山.map", 4, output, "882-phoenix-summit-refused")
        state = transition(client, resource, "map082_金兵大营.map", 2, output, "883-native-disguise-and-jin-entry")
        if state["variables"]["Event"] != "450" or quantity(state, gold) != 0:
            raise AutomationError("Native disguise did not consume exactly two clothes before the camp jump")
        client.save_or_load(1)
        interact_at(client, output, resource, "张琳心", "script/map/map082_金兵大营/张琳心对话.txt", "884-jin-lin450")
        state = transition(client, resource, "map082_金兵大营.map", 6, output, "885-native-camp-jump")
        if state["variables"].get("Event") != "460":
            raise AutomationError("Native camp jump did not advance Event460")
        client.save_or_load(2)
    transition(client, resource, "map082_金兵大营.map", 3, output, "886-jin-commander-before-wenlong-refused")
    interact_at(client, output, resource, "张琳心", "script/map/map082_金兵大营/张琳心对话.txt", "887-jin-lin460")
    npcs = configparser.ConfigParser(interpolation=None)
    npcs.read(resource / "ini/save/map082.npc", encoding="utf-8-sig")
    for number in range(1, 13):
        filename = f"金兵对话{number}.txt"
        actors = [npcs[name] for name in npcs if name.startswith("NPC") and npcs[name].get("scriptfile") == filename]
        actor = actors[0]
        position = actor.getint("mapx"), actor.getint("mapy")
        for repeat in (1, 2):
            interact_at(client, output, resource, "金兵", f"script/map/map082_金兵大营/{filename}",
                        f"888-jin-soldier{number}-{repeat}", position=position)
    client.save_or_load(3)
    state = transition(client, resource, "map083_陆文龙营帐.map", 2, output, "889-native-wenlong-sword-and-origin")
    if (state["variables"].get("Event") != "470" or quantity(state, sword) != sword_count - 1
            or any(row.get("name") in ("陆文龙", "乳娘", "王佐") for row in state["targets"])):
        raise AutomationError("Native Wenlong story did not return the sword and depart with Wang Zuo and nurse")
    for repeat in (1, 2):
        interact_at(client, output, resource, "张琳心", "script/map/map083_陆文龙营帐/张琳心对话.txt", f"890-wenlong-lin470-{repeat}")
    state = interact_at(client, output, resource, "宝箱", "script/map/map083_陆文龙营帐/宝箱.txt", "891-wenlong-chest")
    if quantity(state, armor) != armor_count + 1:
        raise AutomationError("Wenlong chest did not give exactly one Ziyi armor")
    target = next(row for row in state["targets"] if row.get("name") == "宝箱")
    try:
        client.interact(target["id"])
    except AutomationError as error:
        if "action_rejected" not in str(error):
            raise
    else:
        raise AutomationError("Wenlong chest accepted a repeated reward")
    if state["variables"]["End"] != source_end or quantity(state, gold) != 0:
        raise AutomationError("Infiltration changed the source End or repeated its clothes")
    client.save_or_load(6)
    write_json(output / "jin-infiltration-proof.json", dict(status="passed", Event="470", End=source_end,
               nativeGoldClothesConsumed=2, nativeDanxinSwordReturned=1, nativeZiyiArmorDelta=1,
               soldierDialogueSources=12, repeatedSoldierDialogues=12, nativeWenlongDeparture=True,
               sourceSlots=[0, 1, 2, 3, 6], cheatAssisted=True))
    print("Native disguise, camp jump, twelve soldier scripts and Wenlong Event470 passed", flush=True)


def yue_frontline_death(client, output, resource):
    state = idle(client)
    if state["map"] != "map077_岳飞主帐营.map" or state["variables"].get("Event") != "450":
        raise AutomationError("Frontline refusal requires the native Yue Event450 tent source")
    client.act("SetAutoDialogue", enabled=True)
    transition(client, resource, "map076_岳飞大营.map", 1, output, "895-frontline-camp-source")
    client.save_or_load(0)
    client.act("SetAutoDialogue", enabled=False)
    source = "script/map/map076_岳飞大营/trap06.txt"
    transition(client, resource, "Title", 6, output, "896-native-frontline-arrows-and-title",
               expected_terminal=source, final_dialogue="你挂了")
    write_json(output / "yue-frontline-death-proof.json", dict(status="passed", nativeWalkingOnly=True,
               nativeScriptedDeath=True, nativeReturnToTitle=True, storyEnding=False,
               sourceSlot=0, cheatAssisted=True))
    print("Native frontline scripted death and return to title passed; no story ending counted", flush=True)


def nangong_showdown_source(client, output, resource):
    state = idle(client)
    if state["map"] != "map083_陆文龙营帐.map" or state["variables"].get("Event") != "470":
        raise AutomationError("Nangong showdown preparation requires the native Wenlong Event470 source")
    client.act("SetAutoDialogue", enabled=True)
    assist(client, output, 60, True)
    client.save_or_load(0)
    transition(client, resource, "map082_金兵大营.map", 1, output, "900-native-jin-battle-entry")
    client.save_or_load(1)
    army = idle(client)
    army_count = sum(bool(row.get("hostile") and row.get("attackable")) for row in army["targets"])
    army_after = clear_enemies(client, output, "900-jin-native-combat")
    remaining_army = sum(bool(row.get("hostile") and row.get("attackable")) for row in army_after["targets"])
    state = transition(client, resource, "map084_金兵主帅营.map", 3, output, "901-native-nangong-showdown")
    enemy = next(row for row in state["targets"] if row.get("name") == "南宫灭")
    if not enemy.get("hostile") or not enemy.get("attackable") or state["variables"].get("Event") != "470":
        raise AutomationError("Nangong showdown did not activate its native battle")
    client.save_or_load(2)
    write_json(output / "nangong-showdown-source-proof.json", dict(status="passed", nativeArmyFightEntry=True,
               nativeNangongBattle=True, enemyLife=enemy["life"], nativeArmyEnemiesCleared=army_count - remaining_army,
               nativeArmyRemaining=remaining_army, nativeArmyClearScope="nearby" if remaining_army else "all",
               assistedPlayerLevel=60, playerInvincibilityForPreparation=True,
               inheritedPartnerInvincibility=True, sourceSlots=[0, 1, 2], cheatAssisted=True))
    print("Native Nangong battle source saved before required defeat", flush=True)


def nangong_medicine_departure(client, output, resource, partner_defeat, option):
    client.act("SetAutoDialogue", enabled=True)
    initial = client.observe(VARIABLES)
    if initial.get("map") != "map084_金兵主帅营.map" and not initial.get("inEvent"):
        raise AutomationError("Medicine departure requires the native Nangong battle checkpoint")
    if not partner_defeat and initial.get("cheatInvincibilityEnabled"):
        client.open_menu("System")
        client.activate("options")
        client.activate("cheat-settings")
        client.activate("invincibility")
        while not client.observe().get("worldInput"):
            client.ui("Cancel")
        initial = checkpoint(client, output, "901-required-player-defeat-invincibility-disabled")
        if initial.get("cheatInvincibilityEnabled"):
            raise AutomationError("Required player defeat still has native invincibility enabled")
    combat = None
    if not partner_defeat and initial.get("map") == "map084_金兵主帅营.map" and not initial.get("inEvent"):
        enemy = next(row for row in initial["targets"] if row.get("name") == "南宫灭")
        position = initial["player"]["position"]
        occupied = {(row["position"]["x"], row["position"]["y"]) for row in initial["targets"]}
        destination = (enemy["position"]["x"] + 1, enemy["position"]["y"])
        path = reachable_trap(resource, initial["map"], None, position, with_path=True,
                              destination=destination, avoid=occupied - {(position["x"], position["y"])})
        combat = client.submit("MoveTo", generation=initial["generation"], x=path[-1][0], y=path[-1][1],
                               running=True, timeoutMs=150000)
    source = "script/map/map084_金兵主帅营/" + ("张琳心死亡.txt" if partner_defeat else "天魔解体死亡.txt")
    site = next(row["id"] for row in catalog_for(resource)["choices"] if row["path"] == source)
    captured = set()
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        state = client.observe(VARIABLES)
        if combat is not None:
            status = client.request("GetActionStatus", actionId=combat)
            if state.get("inEvent") and status["status"] == "running":
                client.request("CancelAction", actionId=combat)
                combat = None
            elif status["status"] == "succeeded" and status["command"] == "MoveTo":
                combat = client.submit("StartCombat", generation=state["generation"], targetId=enemy["id"],
                                       radius=64, kills=1, skills=[], timeoutMs=150000)
            elif status["status"] != "running":
                if status["reason"] not in ("player_dead", "world_changed"):
                    raise AutomationError(f"Required Nangong defeat: {status}")
                combat = None
        if state.get("video") and state["context"] not in captured:
            frame = state["frame"]
            client.wait_until(lambda row: row.get("video") and row.get("frame", 0) >= frame + 30,
                              timeout=10, description="presented native Tianmo movie")
            checkpoint(client, output, f"native-tianmo-video-{state['context']}")
            captured.add(state["context"])
        if state.get("choices"):
            checkpoint(client, output, "902-native-medicine-choice")
            state = choose_site(client, output, resource, site, option)
            break
        if title_visible(state):
            raise AutomationError("Required Nangong defeat unexpectedly returned to title")
        time.sleep(0.2)
    else:
        raise TimeoutError("Nangong battle did not reach the native medicine choice")
    expected_map = "map088_长白山.map" if option == 0 else "map086_天山.map"
    if (state["map"] != expected_map or state["variables"].get("Event") != "480"
            or state["variables"].get("Selyao") != str(option) or state["variables"].get("die") != "1"
            or state["player"]["level"] < 35):
        raise AutomationError("Native medicine choice did not select its map, Selyao, and minimum character level")
    if not captured:
        raise AutomationError("Native Tianmo movie was not observed")
    proof = script_proof(output, resource, source)
    if not any(row.get("apiName") == "playerchange" for row in records(output) if row.get("executionId") == proof["executionId"]):
        raise AutomationError("Medicine departure has no native player-change call")
    checkpoint(client, output, "903-native-linxin-medicine-departure")
    assist(client, output, None, True)
    client.save_or_load(6)
    game = configparser.ConfigParser(interpolation=None)
    game.read(output / "user-data/save/xjxqy/rpg7/game.ini", encoding="utf-8-sig")
    character = configparser.ConfigParser(interpolation=None)
    character.read(output / "user-data/save/xjxqy/rpg7/player1.ini", encoding="utf-8-sig")
    if game["state"]["chr"] != "1" or not any(row.get("name") == "张琳心" for row in character.values()):
        raise AutomationError("Native character-one save did not preserve Linxin as the current player")
    write_json(output / "nangong-medicine-departure-proof.json", dict(status="passed", source=source,
               defeatedCharacter="张琳心" if partner_defeat else "独孤剑", choiceIndex=option,
               Event="480", Selyao=str(option), nativeCharacterIndex=1, nativeCharacterLevel=state["player"]["level"],
               nativeMovieObserved=True, movieSkipped=False, nativeSourceProof=proof,
               invincibilityEnabledAfterRequiredDefeat=True, sourceSlot=6, cheatAssisted=True))
    print("Native Nangong defeat and medicine departure passed:", expected_map, flush=True)


def tianshan_snow_lotus(client, output, resource):
    state = idle(client)
    if state["map"] == "map086_天山.map" and state["variables"].get("Event") == "520":
        client.act("SetAutoDialogue", enabled=True)
        trials = json.loads((output / "tianshan-combinations-progress.json").read_text(encoding="utf-8"))["trials"]
        return tianshan_medicine_return(client, output, resource, trials)
    if state["map"] != "map086_天山.map" or state["variables"].get("Event") != "480":
        raise AutomationError("Snow lotus requires the native Tianshan medicine source")
    client.act("SetAutoDialogue", enabled=True)
    assist(client, output, 60, True)
    transition(client, resource, state["map"], 1, output, "910-no-lotus-return-refused")
    clear_enemies(client, output, "911-native-snow-leopards", skills=())
    transition(client, resource, "map087_天山山洞.map", 2, output, "912-native-snow-cave")
    transition(client, resource, "map087_天山山洞.map", 1, output, "913-cave-before-fight-refused")
    state = idle(client)
    if state["variables"].get("Xuelao") not in ("", "0"):
        raise AutomationError("Snow grandma choices require an untouched native score")
    client.save_or_load(0)
    source = "script/map/map087_天山山洞/trap02.txt"
    sites = sorted((row for row in catalog_for(resource)["choices"] if row["path"] == source), key=lambda row: row["line"])
    if len(sites) != 4:
        raise AutomationError("Snow grandma must expose its four current-source questions")
    scores = lambda options: sum(choice == correct for choice, correct in zip(options, (0, 1, 0, 1)))
    combinations = sorted(itertools.product((0, 1), repeat=4), key=scores)
    trials = []
    lotus = "goods215_雪莲王.ini"
    for index, options in enumerate(combinations):
        if index:
            load_checkpoint(client, 0)
        state = transition(client, resource, "map087_天山山洞.map", 2, output, f"914-questions-{index}",
                           choices=tuple((site["id"], option) for site, option in zip(sites, options)))
        score = scores(options)
        enemy = next(row for row in state["targets"] if row.get("name") == "雪莲姥姥")
        if state["variables"].get("Xuelao", "0") not in (str(score), "" if score == 0 else str(score)) or not enemy.get("hostile"):
            raise AutomationError("Snow grandma native answers selected the wrong score or battle")
        expected_life = (1000, 80000, 60000, 32000, 15000)[score]
        if enemy["life"] != expected_life:
            raise AutomationError("Snow grandma battle does not match its current resource variant")
        before_lotus = quantity(state, lotus)
        clear_enemies(client, output, f"915-native-snow-grandma-{index}", skills=())
        state = idle(client)
        if state["variables"].get("Event") != "520" or quantity(state, lotus) != before_lotus + 1:
            raise AutomationError("Snow grandma native defeat did not grant exactly one lotus")
        proof = script_proof(output, resource, "script/map/map087_天山山洞/雪莲姥姥死亡.txt")
        trials.append(dict(options=options, nativeScore=score, enemyLifeBeforeBattle=expected_life,
                           Event="520", snowLotusDelta=1, nativeDeathProof=proof))
        write_json(output / "tianshan-combinations-progress.json", dict(trials=trials, total=16, cheatAssisted=True))
    client.save_or_load(1)
    for repeat in range(2):
        interact_at(client, output, resource, "雪莲姥姥", "script/map/map087_天山山洞/雪莲姥姥对话.txt",
                    f"916-grandma-after-defeat-{repeat}")
    transition(client, resource, "map086_天山.map", 1, output, "917-native-cave-return")
    transition(client, resource, "map086_天山.map", 2, output, "918-no-cave-revisit-after-lotus")
    client.save_or_load(2)
    return tianshan_medicine_return(client, output, resource, trials)


def tianshan_medicine_return(client, output, resource, trials):
    if len(trials) != 16:
        raise AutomationError("Snow lotus return requires all sixteen completed native combinations")
    walk_required_battle(client, output, resource, None, "918-native-leopards-on-return", progress_variable=None)
    lotus = "goods215_雪莲王.ini"
    state = transition(client, resource, "map085_朱仙镇客栈.map", 1, output, "919-native-lotus-restoration")
    if state["variables"].get("Event") != "530" or quantity(state, lotus) != 0:
        raise AutomationError("Native lotus restoration did not consume its medicine and reach Event530")
    client.save_or_load(6)
    game = configparser.ConfigParser(interpolation=None)
    game.read(output / "user-data/save/xjxqy/rpg7/game.ini", encoding="utf-8-sig")
    if game["state"]["chr"] != "0":
        raise AutomationError("Native lotus restoration did not return control to Dugu")
    goods = configparser.ConfigParser(interpolation=None)
    goods.read(output / "user-data/save/xjxqy/rpg7/goods1.ini", encoding="utf-8-sig")
    linxin_lotus = sum(int(row.get("number", 0)) for row in goods.values() if row.get("inifile") == lotus)
    if linxin_lotus != 0:
        raise AutomationError("Native lotus return left its medicine in Linxin's saved inventory")
    write_json(output / "tianshan-snow-lotus-proof.json", dict(status="passed", combinations=trials,
               nativeScoresCovered=list(range(5)), nativeAllAnswerCombinations=True, nativeCharacterReturn=0,
               snowLotusConsumed=1, linxinMedicineAfter=linxin_lotus, Event="530", sourceSlots=[0, 1, 2, 6], cheatAssisted=True))
    print("Native Tianshan sixteen answer combinations and lotus restoration passed", flush=True)


def changbai_bandits_first(client, output, resource):
    state = idle(client)
    if (state["map"] != "map088_长白山.map" or state["variables"].get("Event") != "480"
            or state["variables"].get("Talkzhaoda") not in ("", "0")):
        raise AutomationError("Bandits-first medicine requires an unasked native Changbai source")
    client.act("SetAutoDialogue", enabled=True)
    assist(client, output, 60, True)
    client.save_or_load(0)
    transition(client, resource, "map091_长白山北.map", 3, output, "965-bandits-before-zhao")
    client.save_or_load(1)
    transition(client, resource, "map091_长白山北.map", 2, output, "966-unasked-seven-bandits")
    client.save_or_load(2)
    state = transition(client, resource, "map091_长白山北.map", 1, output, "967-unasked-battle-return-refused")
    if state["variables"].get("Talkzhaoda") not in ("", "0"):
        raise AutomationError("Bandits-first battle unexpectedly asked Zhao")
    client.save_or_load(3)
    changbai_compass_finish(client, output, resource)
    write_json(output / "changbai-bandits-first-proof.json", dict(status="passed", nativeZhaoUnaskedBeforeBattle=True,
               nativeCompassAlreadyHeldAtFirstEnquiry=True, nativeFirstEnquiryGrantedGinseng=True,
               Event="530", sourceSlots=list(range(7)), cheatAssisted=True))


def changbai_compass_medicine(client, output, resource):
    state = idle(client)
    if state["map"] == "map090_窝棚.map" and state["variables"].get("Event") == "520":
        client.act("SetAutoDialogue", enabled=True)
        return changbai_medicine_return(client, output, resource)
    if state["map"] == "map091_长白山北.map" and state["variables"].get("Talktufei") in ("1", "2"):
        client.act("SetAutoDialogue", enabled=True)
        return changbai_compass_finish(client, output, resource)
    second_medicine = (state["variables"].get("Event") == "520"
                       and state["variables"].get("Zhujiarenshen") == "1"
                       and quantity(state, "goods216_双头人参.ini") == 1
                       and state["variables"].get("Talkzhaoda") in ("", "0"))
    if state["map"] != "map088_长白山.map" or (state["variables"].get("Event") != "480" and not second_medicine):
        raise AutomationError("Changbai medicine requires the native character-one departure")
    client.act("SetAutoDialogue", enabled=True)
    assist(client, output, 60, True)
    client.save_or_load(0)
    if not second_medicine:
        transition(client, resource, state["map"], 1, output, "920-no-ginseng-return-refused")
    for repeat in range(2):
        state = interact_at(client, output, resource, "猎人", "script/map/map088_长白山/猎人对话.txt",
                            f"921-native-hunter-{repeat}")
    if not second_medicine and state["variables"].get("Talkliehu") != "1":
        raise AutomationError("Hunter did not preserve its native enquiry flag")
    transition(client, resource, "map090_窝棚.map", 2, output, "922-native-ginseng-hut")
    client.save_or_load(1)
    npc = configparser.ConfigParser(interpolation=None)
    npc.read(resource / "ini/save/map090.npc", encoding="utf-8-sig")
    for row in npc.values():
        if row.get("name") != "参客":
            continue
        for repeat in range(2):
            interact_at(client, output, resource, "参客", "script/map/map090_窝棚/" + row["scriptfile"],
                        f"923-ginseng-worker-{row['scriptfile']}-{repeat}",
                        position=(int(row["mapx"]), int(row["mapy"])), shop_cancel=True)
    for repeat in range(2):
        state = interact_at(client, output, resource, "赵大帽子", "script/map/map090_窝棚/赵大帽子对话.txt",
                            f"924-native-compass-request-{repeat}")
    if state["variables"].get("Talkzhaoda") != "1":
        raise AutomationError("Native compass request did not retain its pending state")
    client.save_or_load(2)
    transition(client, resource, "map088_长白山.map", 1, output, "925-hut-return")
    transition(client, resource, "map091_长白山北.map", 3, output, "926-native-bandit-approach")
    state = transition(client, resource, "map091_长白山北.map", 2, output, "927-native-seven-bandits")
    if state["variables"].get("Talktufei") != "1":
        raise AutomationError("Bandit entrance did not activate the native outdoor fight")
    client.save_or_load(3)
    transition(client, resource, state["map"], 1, output, "928-bandit-fight-exit-1-refused")
    return changbai_compass_finish(client, output, resource)


def changbai_compass_finish(client, output, resource):
    state = idle(client)
    if not state.get("cheatInvincibilityEnabled"):
        assist(client, output, None, True)
    if state["variables"].get("Talktufei") == "1":
        state = walk_required_battle(client, output, resource, "6", "928-six-bandits", progress_variable="fight091")
        transition(client, resource, state["map"], 2, output, "928-bandit-fight-exit-2-refused")
    state = walk_required_battle(client, output, resource, "2", "929-seven-bandits", progress_variable="Talktufei")
    compass, ginseng = "goods410_指南针.ini", "goods216_双头人参.ini"
    if state["variables"].get("fight091") != "7" or quantity(state, compass) != 1:
        raise AutomationError("Seven native bandit callbacks did not grant one compass")
    client.save_or_load(4)
    transition(client, resource, "map088_长白山.map", 1, output, "930-native-bandit-return")
    transition(client, resource, "map090_窝棚.map", 2, output, "931-native-compass-delivery")
    before_ginseng = quantity(idle(client), ginseng)
    state = interact_at(client, output, resource, "赵大帽子", "script/map/map090_窝棚/赵大帽子对话.txt",
                        "932-native-ginseng-exchange")
    if (state["variables"].get("Event") != "520" or state["variables"].get("Talkzhaoda") != "2"
            or quantity(state, compass) != 0 or quantity(state, ginseng) != before_ginseng + 1):
        raise AutomationError("Native compass exchange did not grant exactly one ginseng")
    for repeat in range(2):
        state = interact_at(client, output, resource, "赵大帽子", "script/map/map090_窝棚/赵大帽子对话.txt",
                            f"933-ginseng-exchange-repeat-{repeat}")
    if quantity(state, ginseng) != before_ginseng + 1:
        raise AutomationError("Repeated Zhao enquiry duplicated its ginseng")
    client.save_or_load(5)
    changbai_medicine_return(client, output, resource)
    write_json(output / "changbai-compass-medicine-proof.json", dict(status="passed", nativeSevenBandits=7,
               nativeCompassConsumed=1, nativeGinsengGranted=1, nativeGinsengConsumed=1, nativeZhaoRepeatNoReward=True,
               Event="530", sourceSlots=list(range(7)), cheatAssisted=True))
    print("Native Changbai compass exchange and medicine restoration passed", flush=True)


def changbai_medicine_return(client, output, resource):
    state = idle(client)
    ginseng = "goods216_双头人参.ini"
    before = quantity(state, ginseng)
    if before not in (1, 2):
        raise AutomationError("Medicine return requires one or two native/assisted ginseng items")
    transition(client, resource, "map088_长白山.map", 1, output, "934-hut-medicine-return")
    interact_at(client, output, resource, "猎人", "script/map/map088_长白山/猎人对话.txt", "935-hunter-after-medicine")
    state = transition(client, resource, "map085_朱仙镇客栈.map", 1, output, "936-native-ginseng-restoration")
    if state["variables"].get("Event") != "530" or quantity(state, ginseng) != 0:
        raise AutomationError("Native ginseng restoration did not consume its medicine and reach Event530")
    client.save_or_load(6)
    goods = configparser.ConfigParser(interpolation=None)
    goods.read(output / "user-data/save/xjxqy/rpg7/goods1.ini", encoding="utf-8-sig")
    after = sum(int(row.get("number", 0)) for row in goods.values() if row.get("inifile") == ginseng)
    if after != before - 1:
        raise AutomationError("Native return failed to consume one item from Linxin's saved inventory")
    write_json(output / "changbai-medicine-return-proof.json", dict(status="passed", linxinMedicineBefore=before,
               linxinMedicineAfter=after, nativeMedicineConsumed=1, activeHeroMedicineAfter=quantity(state, ginseng),
               Event="530", sourceSlot=6, cheatAssisted=True))


def changbai_zhu_medicine(client, output, resource, wife):
    state = idle(client)
    if state["map"] == "map094_朱家大堂.map":
        # Resume the native pre-dialogue slot; the failed chest probe is retained.
        state = load_checkpoint(client, 0)
        state = idle(client)
        house_source = True
    else:
        house_source = False
    if (state["map"] not in ("map088_长白山.map", "map094_朱家大堂.map")
            or state["variables"].get("Event") != "480"
            or any(state["variables"].get(key) not in ("", "0")
                   for key in ("Talkzhuhai", "Talkzhufuren", "Zhujiarenshen"))):
        raise AutomationError("Zhu medicine choices require the untouched Changbai departure")
    client.act("SetAutoDialogue", enabled=True)
    assist(client, output, None, True)
    if not house_source:
        state = transition(client, resource, "map093_长白山东.map", 4, output, "940-native-zhu-east")
        write_json(output / "zhu-child-before-presence.json", dict(Event=state["variables"]["Event"],
                   npcNames=[row["name"] for row in state["targets"] if row.get("kind") == "npc"],
                   initialNpcFile="map093.npc", unboundChildDialogueNotCounted=True, cheatAssisted=True))
        transition(client, resource, "map094_朱家大堂.map", 2, output, "942-native-zhu-house")
        for position in ((10, 50), (9, 51)):
            interact_at(client, output, resource, "朱府家丁", "script/map/map094_朱家大堂/朱府家丁对话.txt",
                        f"943-zhu-servant-{position[0]}-{position[1]}", position=position)
        client.save_or_load(0)
    ginseng = "goods216_双头人参.ini"
    if not wife:
        before = idle(client)
        direct_access = True
        for step in range(30):
            current = idle(client)
            if current["player"]["position"] == dict(x=4, y=34):
                break
            occupied = {(row["position"]["x"], row["position"]["y"]) for row in current["targets"]}
            try:
                path = reachable_trap(resource, current["map"], None, current["player"]["position"],
                                      with_path=True, destination=(4, 34), avoid=occupied)
            except AutomationError as error:
                if "No connected trap" not in str(error):
                    raise
                direct_access = False
                write_json(output / "zhu-direct-chest-access-pending.json", dict(status="pending",
                           walkingRouteBlocked=True,
                           noReachabilityConclusion=True, chestPosition=[3, 33], cheatAssisted=True))
                break
            client.move(*path[min(2, len(path) - 1)])
        else:
            raise AutomationError("Direct ginseng chest approach exceeded its walking limit")
        if direct_access:
            state = interact_at(client, output, resource, "宝箱", "script/map/map094_朱家大堂/宝箱2.txt",
                                "944-native-direct-ginseng-chest", position=(3, 33))
            if (quantity(state, ginseng) != quantity(before, ginseng) + 1 or state["variables"].get("Event") != "520"
                    or state["variables"].get("Zhujiarenshen") != "1" or any(row.get("hostile") for row in state["targets"])
                    or state["variables"].get("Talkzhuhai") not in ("", "0")
                    or state["variables"].get("Talkzhufuren") not in ("", "0")):
                raise AutomationError("Direct native chest access did not preserve the unasked friendly house")
            write_json(output / "zhu-direct-chest-proof.json", dict(status="passed", nativeDirectAccess=True,
                       noDialogueBeforeMedicine=True, nativeBattleNotRequired=True, nativeGinsengDelta=1,
                       Event="520", Zhujiarenshen="1", cheatAssisted=True))
        load_checkpoint(client, 0)
    actor = "朱夫人" if wife else "朱海"
    flag = "Talkzhufuren" if wife else "Talkzhuhai"
    source = f"script/map/map094_朱家大堂/{actor}对话.txt"
    sites = sorted((row for row in catalog_for(resource)["choices"] if row["path"] == source), key=lambda row: row["line"])
    if len(sites) != 2:
        raise AutomationError("Zhu dialogue requires current first and repeated medicine choices")
    for index in (0, 1):
        state = interact_at(client, output, resource, actor, source, f"945-{actor}-refuse-{index}",
                            choices=((sites[index]["id"], 1),))
        if state["variables"].get(flag) != "1" or any(row.get("hostile") for row in state["targets"]):
            raise AutomationError("Native refusal did not preserve a peaceful repeated enquiry")
    client.save_or_load(1)
    state = interact_at(client, output, resource, actor, source, f"946-{actor}-repeat-rob",
                        choices=((sites[1]["id"], 0),))
    if sum(bool(row.get("hostile") and row.get("attackable")) for row in state["targets"]) != 4:
        raise AutomationError("Repeated robbery did not activate all four native house defenders")
    client.save_or_load(2)
    walk_required_battle(client, output, resource, None, f"947-{actor}-repeat-battle", progress_variable=None)
    state = interact_at(client, output, resource, "宝箱", "script/map/map094_朱家大堂/宝箱2.txt",
                        f"948-{actor}-repeat-battle-ginseng", position=(3, 33))
    if quantity(state, ginseng) != 1 or state["variables"].get("Zhujiarenshen") != "1":
        raise AutomationError("Repeated robbery did not grant its native medicine flag")
    client.save_or_load(3)
    load_checkpoint(client, 0)
    state = interact_at(client, output, resource, actor, source, f"949-{actor}-first-rob",
                        choices=((sites[0]["id"], 0),))
    if sum(bool(row.get("hostile") and row.get("attackable")) for row in state["targets"]) != 4:
        raise AutomationError("First robbery did not activate all four native house defenders")
    client.save_or_load(4)
    walk_required_battle(client, output, resource, None, f"950-{actor}-first-battle", progress_variable=None)
    for position, file, goods in (((5, 46), "宝箱.txt", "goods045_岚枫雪衣.ini"),
                                  ((5, 26), "宝箱1.txt", "goods210_药王金方.ini"),
                                  ((3, 33), "宝箱2.txt", ginseng)):
        before = idle(client)
        state = interact_at(client, output, resource, "宝箱", "script/map/map094_朱家大堂/" + file,
                            f"951-{actor}-{file}", position=position)
        if quantity(state, goods) != quantity(before, goods) + 1:
            raise AutomationError("Native Zhu chest did not grant exactly one item")
        target = next(row for row in state["targets"] if row.get("name") == "宝箱" and row["position"] == dict(x=position[0], y=position[1]))
        try:
            client.interact(target["id"])
        except AutomationError as error:
            if "action_rejected" not in str(error):
                raise
        else:
            raise AutomationError("Native Zhu chest repeated its reward")
    client.save_or_load(5)
    transition(client, resource, "map093_长白山东.map", 1, output, "952-native-zhu-robbery-leave")
    for repeat in range(2):
        interact_at(client, output, resource, "朱盈", "script/map/map093_长白山东/小孩对话.txt",
                    f"953-zhu-child-after-{repeat}")
    state = transition(client, resource, "map088_长白山.map", 1, output, "954-native-zhu-medicine-return")
    before = quantity(state, ginseng)
    state = transition(client, resource, "map085_朱仙镇客栈.map", 1, output, "955-native-zhu-restoration")
    client.save_or_load(6)
    goods = configparser.ConfigParser(interpolation=None)
    goods.read(output / "user-data/save/xjxqy/rpg7/goods1.ini", encoding="utf-8-sig")
    after = sum(int(row.get("number", 0)) for row in goods.values() if row.get("inifile") == ginseng)
    if state["variables"].get("Event") != "530" or state["variables"].get("Zhujiarenshen") != "1" or before != 1 or after != 0:
        raise AutomationError("Native Zhu medicine restoration did not preserve robbery and consume its medicine")
    write_json(output / "changbai-zhu-medicine-proof.json", dict(status="passed", actor=actor,
               nativeFirstAndRepeatedChoiceOptions=4, nativeDefendersPerBattle=4, nativeTwoRobberyBattles=True,
               nativeGinsengConsumed=1, linxinMedicineAfter=after, Event="530", Zhujiarenshen="1",
               sourceSlots=list(range(7)), cheatAssisted=True))
    print("Native Zhu medicine first/repeated choices and restoration passed:", actor, flush=True)


def tianwang_password_and_hall(client, output, resource):
    state = idle(client)
    if state["map"] != "map100_天王岛.map" or state["variables"].get("Event") != "560":
        raise AutomationError("Tianwang passwords require the native island Event560 source")
    client.save_or_load(0)
    source = "script/map/map100_天王岛/trap03.txt"
    sites = [row["id"] for row in catalog_for(resource)["choices"] if row["path"] == source]
    if len(sites) != 2:
        raise AutomationError("Expected the two current native password choices")
    for trial, options in enumerate(((0,), (1, 0), (1, 1))):
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        terminal = source if trial < 2 else None
        state = transition(client, resource, "Title" if terminal else "map100_天王岛.map", 3,
                           output, f"1000-password-{trial}", choices=tuple(zip(sites, options)),
                           expected_terminal=terminal, final_dialogue="你挂了，但愿你已经存盘了！")
        if terminal:
            if not title_visible(state):
                raise AutomationError("Wrong native password did not return to title")
        elif state["variables"].get("Event") != "570":
            raise AutomationError("Correct native passwords did not advance Event570")
    client.save_or_load(1)
    for actor in ("史初", "候忠", "周旱", "草草"):
        for repeat in range(2):
            state = interact_at(client, output, resource, actor, f"script/map/map100_天王岛/{actor}对话.txt",
                                f"1001-island-{actor}-{repeat}")
            if actor == "草草" and state["variables"].get("Talkcaocao") == "2":
                if any(row["name"] == actor for row in state["targets"]):
                    raise AutomationError("Grass did not leave after the native full life story")
                break
    for number, position in enumerate(((15, 33), (15, 34), (11, 42), (10, 43)), 1):
        for repeat in range(2):
            interact_at(client, output, resource, "天王帮众", f"script/map/map100_天王岛/天王帮众{number}对话.txt",
                        f"1002-island-member-{number}-{repeat}", position=position)
    for trial, trap in enumerate((2, 4)):
        load_checkpoint(client, 1)
        client.act("SetAutoDialogue", enabled=True)
        state = transition(client, resource, "map101_天王帮大殿.map", trap, output, f"1003-hall-entrance-{trap}")
        if state["variables"].get("Event") != "580":
            raise AutomationError("Native hall introduction did not reach Event580")
        client.save_or_load(2 + trial)
    for actor, filename in (("封玉书", "封玉书对话.txt"), ("杨湖", "杨湖对话.txt"), ("路云远", "路云远对话.txt"),
                            ("焦俊", "天王帮众对话1.txt"), ("凌正", "天王帮众对话2.txt"),
                            ("裴良", "天王帮众对话3.txt"), ("崔信", "天王帮众对话4.txt")):
        for repeat in range(2):
            interact_at(client, output, resource, actor, f"script/map/map101_天王帮大殿/{filename}",
                        f"1004-hall-{actor}-{repeat}")
    client.save_or_load(3)
    write_json(output / "tianwang-password-and-hall-proof.json", dict(status="passed", nativeWrongPasswordTitles=2,
               nativePasswordOptions=4, nativeHallEntrances=[2, 4], Event="580", sourceSlots=[0, 1, 2, 3],
               deathTitlesAreStoryEndings=False, cheatAssisted=True))


def tianwang_garden_and_betrayal(client, output, resource, stop_before_battle=False):
    state = idle(client)
    initial_stage = state["variables"].get("Event")
    client.act("SetAutoDialogue", enabled=True)
    if state["map"] == "map101_天王帮大殿.map" and state["variables"].get("Event") == "580":
        state = transition(client, resource, "map102_后花园和小树林.map", 2, output, "1030-garden-first-entry")
        transition(client, resource, state["map"], 1, output, "1031-garden-hall-return-refused-580")
        transition(client, resource, "map103_杨瑛寝宫.map", 2, output, "1032-bedroom-before-ambush")
        for actor in ("萍儿", "莺儿"):
            for repeat in range(2):
                interact_at(client, output, resource, actor, f"script/map/map103_杨瑛寝宫/{actor}对话.txt",
                            f"1033-maid-580-{actor}-{repeat}")
        for position, filename, rewards in (
                ((16, 23), "宝箱2.txt", ("goods086_飞天霞影.ini",)),
                ((18, 30), "宝箱1.txt", ("goods210_药王金方.ini",)),
                ((10, 16), "宝箱.txt", ("goods082_圣天王袍.ini", "goods004_冲虚冠.ini"))):
            before = idle(client)
            after = interact_at(client, output, resource, "宝箱", f"script/map/map103_杨瑛寝宫/{filename}",
                                f"1034-bedroom-{filename}", position=position)
            if any(quantity(after, reward) - quantity(before, reward) != 1 for reward in rewards):
                raise AutomationError("Native bedroom chest reward mismatch")
            target = next(row for row in after["targets"] if row["name"] == "宝箱" and row["position"] == dict(x=position[0], y=position[1]))
            try:
                client.interact(target["id"])
            except AutomationError as error:
                if "action_rejected" not in str(error):
                    raise
            else:
                raise AutomationError("Opened bedroom chest repeated its native reward")
        state = transition(client, resource, "map102_后花园和小树林.map", 1, output, "1035-bedroom-garden-return")
        state = interact_at(client, output, resource, "杨瑛", "script/map/map102_后花园和小树林/杨瑛的同伴对话.txt",
                            "1036-native-garden-ambush")
        if state["variables"].get("Event") != "600":
            raise AutomationError("Native garden ambush did not advance Event600")
        client.save_or_load(4)
    if state["map"] == "map102_后花园和小树林.map" and state["variables"].get("Event") == "600":
        transition(client, resource, state["map"], 1, output, "1037-garden-hall-return-refused-600")
        transition(client, resource, "map103_杨瑛寝宫.map", 2, output, "1038-bedroom-after-ambush")
        for actor in ("萍儿", "莺儿"):
            for repeat in range(2):
                interact_at(client, output, resource, actor, f"script/map/map103_杨瑛寝宫/{actor}对话.txt",
                            f"1039-maid-600-{actor}-{repeat}")
        transition(client, resource, "map102_后花园和小树林.map", 1, output, "1040-bedroom-lake-return")
        state = interact_at(client, output, resource, "杨瑛", "script/map/map102_后花园和小树林/杨瑛的同伴对话.txt",
                            "1041-native-yang-map-promise")
        if state["variables"].get("Event") != "605":
            raise AutomationError("Native Yang map promise did not advance Event605")
        interact_at(client, output, resource, "杨瑛", "script/map/map102_后花园和小树林/杨瑛的同伴对话.txt", "1042-yang-map-promise-repeat")
    if state["map"] == "map102_后花园和小树林.map" and state["variables"].get("Event") == "605":
        transition(client, resource, state["map"], 1, output, "1043-garden-hall-return-refused-605")
        state = transition(client, resource, "map103_杨瑛寝宫.map", 2, output, "1044-native-feng-bedroom-entry")
    if state["map"] == "map103_杨瑛寝宫.map" and state["variables"].get("Event") == "605":
        client.save_or_load(5)
        if stop_before_battle:
            write_json(output / "tianwang-garden-battle-source-proof.json", dict(status="passed", Event="605",
                       nativeGardenAmbush=initial_stage == "580", nativeYangMapPromise=True,
                       nativeBedroomChests=3 if initial_stage == "580" else 0, sourceSlot=5,
                       NoEnd=state["variables"].get("NoEnd"), Saychake=state["variables"].get("Saychake"),
                       Talkcaocao=state["variables"].get("Talkcaocao"), cheatAssisted=True))
            return
        transition(client, resource, state["map"], 1, output, "1045-feng-bedroom-exit-before-fight")
        state = transition(client, resource, state["map"], 2, output, "1046-native-feng-betrayal")
        if not any(row.get("hostile") and row["name"] == "封玉书" for row in state["targets"]):
            raise AutomationError("Native Feng betrayal did not enable the required battle")
        walk_required_battle(client, output, resource, "610", "1047-native-feng-battle")
        script_proof(output, resource, "script/map/map103_杨瑛寝宫/封玉书死亡.txt")
        for actor in ("杨瑛", "杨湖", "路云远"):
            for repeat in range(2):
                interact_at(client, output, resource, actor, f"script/map/map103_杨瑛寝宫/{actor + '的同伴' if actor == '杨瑛' else actor}对话.txt",
                            f"1048-bedroom-610-{actor}-{repeat}")
        transition(client, resource, "map102_后花园和小树林.map", 1, output, "1049-native-bedroom-departure")
        interact_at(client, output, resource, "杨瑛", "script/map/map102_后花园和小树林/杨瑛的同伴对话.txt", "1050-yang-garden-610")
        state = transition(client, resource, "map101_天王帮大殿.map", 1, output, "1051-hall-after-betrayal")
    if state["map"] == "map101_天王帮大殿.map" and state["variables"].get("Event") == "610":
        checkpoint(client, output, "1052-hall-610-present-npcs")
        for number, actor in enumerate(("焦俊", "凌正", "裴良", "崔信"), 1):
            interact_at(client, output, resource, actor, f"script/map/map101_天王帮大殿/天王帮众对话{number}.txt",
                        f"1052-hall-610-{actor}")
        state = transition(client, resource, "map100_天王岛.map", 1, output, "1053-native-island-departure-source")
    if state["map"] != "map100_天王岛.map" or state["variables"].get("Event") != "610":
        raise AutomationError("Tianwang betrayal did not settle at native island Event610")
    client.save_or_load(6)
    write_json(output / "tianwang-garden-and-betrayal-proof.json", dict(status="passed", Event="610",
               nativeGardenAmbush=initial_stage == "580", nativeYangMapPromise=initial_stage in ("580", "600"),
               nativeFengDeath=True, nativeBedroomChests=3 if initial_stage == "580" else 0,
               nativeMaidsAt580And600=initial_stage == "580", initialSourceEvent=initial_stage,
               sourceSlots=[3, 4, 5, 6], cheatAssisted=True))


def tianwang_return_and_letter(client, output, resource):
    state = idle(client)
    if state["map"] != "map100_天王岛.map" or state["variables"].get("Event") != "610":
        raise AutomationError("Tianwang return requires the native island Event610 source")
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    for actor in ("史初", "候忠", "周旱", "草草"):
        if actor == "草草" and state["variables"].get("Talkcaocao") == "2" and not any(row["name"] == actor for row in state["targets"]):
            continue
        for repeat in range(2):
            interact_at(client, output, resource, actor, f"script/map/map100_天王岛/{actor}对话.txt",
                        f"1060-island-610-{actor}-{repeat}")
    for number, position in enumerate(((15, 33), (15, 34), (11, 42), (10, 43)), 1):
        interact_at(client, output, resource, "天王帮众", f"script/map/map100_天王岛/天王帮众{number}对话.txt",
                    f"1061-island-610-member-{number}", position=position)
    transition(client, resource, state["map"], 3, output, "1062-native-yang-swimming-race")
    state = transition(client, resource, "map121_洞庭湖底.map", 1, output, "1063-native-paired-dive")
    if sum(row.get("hostile", False) for row in state["targets"]) != 7:
        raise AutomationError("Expected seven native return-route lake enemies")
    walk_required_battle(client, output, resource, None, "1064-native-return-lake-battle", progress_variable=None)
    client.save_or_load(1)
    transition(client, resource, "map121_洞庭湖底.map", 2, output, "1065-lake-island-return-refused-610")
    transition(client, resource, "map099_洞庭湖畔.map", 1, output, "1066-native-paired-lake-emergence")
    transition(client, resource, "map099_洞庭湖畔.map", 2, output, "1067-shore-island-return-refused-610")
    state = transition(client, resource, "map098_天王帮秘道.map", 1, output, "1068-native-return-secret-tunnel")
    client.save_or_load(2)
    state = transition(client, resource, "map097_湖口村茶馆.map", 1, output, "1069-native-gong-murder-discovery")
    client.save_or_load(3)
    for repeat in range(2):
        interact_at(client, output, resource, "杨瑛", "script/map/map097_湖口村茶馆/杨瑛的同伴对话.txt",
                    f"1070-yang-letter-search-{repeat}")
    transition(client, resource, "map096_湖口村.map", 1, output, "1071-village-return-610")
    client.save_or_load(4)
    for actor in ("温谦", "纪小妹", "紫荆", "莲姑"):
        interact_at(client, output, resource, actor, f"script/map/map096_湖口村/{actor}对话.txt", f"1072-village-610-{actor}")
    state = idle(client)
    if state["variables"].get("Saychake") == "3" and state["variables"].get("Talkcaocao") in ("1", "2"):
        before = checkpoint(client, output, "1072-wang-turnin-before")
        expected_silver = -50 if state["variables"]["Talkcaocao"] == "1" else 3000
        state = interact_at(client, output, resource, "王老五", "script/map/map096_湖口村/王老五对话.txt", "1072-native-wang-turnin")
        if (state["variables"].get("NoEnd") != "0" or state["variables"].get("Talkwlw") != "1"
                or state["player"]["money"] - before["player"]["money"] != expected_silver):
            raise AutomationError("Native Wang turn-in did not clear the restriction with the correct silver")
        for repeat in range(2):
            repeated = interact_at(client, output, resource, "王老五", "script/map/map096_湖口村/王老五对话.txt", f"1072-wang-turnin-repeat-{repeat}")
            if repeated["inventory"] != state["inventory"] or repeated["player"]["money"] != state["player"]["money"]:
                raise AutomationError("Native completed Wang turn-in repeated its reward")
        client.save_or_load(4)
        write_json(output / "wang-property-turnin-proof.json", dict(status="passed", nativeTurnIn=True,
                   silverDelta=expected_silver, NoEnd="0", Talkwlw="1", Talkcaocao=state["variables"]["Talkcaocao"],
                   nativeRepeatNoReward=True, sourceSlot=4, cheatAssisted=True))
    state = transition(client, resource, "map097_湖口村茶馆.map", 1, output, "1073-native-letter-search-return")
    client.save_or_load(5)
    before = quantity(state, "goods405_书信.ini")
    state = interact_at(client, output, resource, "茶馆留书", "script/map/map097_湖口村茶馆/桌上的书信.txt", "1074-native-rumeng-letter-voyage")
    if (state["map"] != "map105_成都.map" or state["variables"].get("Event") != "620"
            or state["variables"].get("Clue") != "0" or quantity(state, "goods405_书信.ini") - before != 1):
        raise AutomationError("Native Rumeng letter voyage did not reach Chengdu with one letter")
    client.save_or_load(6)
    write_json(output / "tianwang-return-and-letter-proof.json", dict(status="passed", Event="620", Clue="0",
               nativePairedDive=True, nativeReturnLakeEnemies=7, nativeGongMurderDiscovery=True,
               nativeRumengLetterGranted=1, nativeChengduArrival=True, sourceSlots=list(range(7)), cheatAssisted=True))


def hukou_unreturned_letter(client, output, resource):
    state = idle(client)
    if (state["map"] != "map097_湖口村茶馆.map" or state["variables"].get("Event") != "610"
            or state["variables"].get("NoEnd") != "1"):
        raise AutomationError("Unreturned property route requires the native tea-house NoEnd1 source")
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    before = quantity(state, "goods405_书信.ini")
    state = interact_at(client, output, resource, "茶馆留书", "script/map/map097_湖口村茶馆/桌上的书信.txt",
                        "1074-native-unreturned-letter-voyage")
    if (state["map"] != "map105_成都.map" or state["variables"].get("Event") != "620"
            or state["variables"].get("Clue") != "0" or state["variables"].get("NoEnd") != "1"
            or quantity(state, "goods405_书信.ini") - before != 1):
        raise AutomationError("Native letter route did not retain the unreturned-property restriction")
    client.save_or_load(6)
    load_checkpoint(client, 6)
    state = checkpoint(client, output, "1075-native-unreturned-chengdu-reload")
    if state["variables"].get("NoEnd") != "1" or state["variables"].get("Event") != "620":
        raise AutomationError("Normal reload did not retain NoEnd1 at Chengdu")
    write_json(output / "hukou-unreturned-letter-proof.json", dict(status="passed", NoEnd="1", Event="620",
               nativeLetterVoyage=True, nativeSaveReload=True, sourceSlot=6, cheatAssisted=True))


def hukou_wang_property(client, output, resource):
    state = idle(client)
    if (state["map"] != "map096_湖口村.map" or state["variables"].get("Event") != "540"
            or state["variables"].get("Saychake") not in ("", "0")):
        raise AutomationError("Wang property requires the untouched native Hukou Event540 source")
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    wang = "script/map/map096_湖口村/王老五对话.txt"
    rumour = "script/map/map097_湖口村茶馆/茶客1肖头对话.txt"
    interact_at(client, output, resource, "王老五", wang, "1010-wang-before-rumour")
    transition(client, resource, "map097_湖口村茶馆.map", 1, output, "1011-wang-tea-rumour-entry")
    for repeat in range(2):
        state = interact_at(client, output, resource, "肖头", rumour, f"1012-wang-tea-rumour-{repeat}")
    if state["variables"].get("Saychake") != "1":
        raise AutomationError("Native tea rumour did not initialize the Wang enquiry")
    transition(client, resource, "map096_湖口村.map", 1, output, "1013-wang-village-return")
    before = checkpoint(client, output, "1014-wang-property-before")
    state = interact_at(client, output, resource, "王老五", wang, "1015-wang-property-received")
    wrist = "goods413_另一只护腕.ini"
    if (state["player"]["money"] - before["player"]["money"] != 1000
            or quantity(state, wrist) - quantity(before, wrist) != 1
            or state["variables"].get("NoEnd") != "1" or state["variables"].get("Saychake") != "2"):
        raise AutomationError("Native Wang property reward or quest restriction mismatch")
    for repeat in range(2):
        repeated = interact_at(client, output, resource, "王老五", wang, f"1016-wang-property-repeat-{repeat}")
        if repeated["inventory"] != state["inventory"] or repeated["player"]["money"] != state["player"]["money"]:
            raise AutomationError("Native Wang reward repeated without a tea revisit")
    client.save_or_load(1)
    transition(client, resource, "map097_湖口村茶馆.map", 1, output, "1017-wang-tea-revisit")
    for repeat in range(2):
        after = interact_at(client, output, resource, "肖头", rumour, f"1018-wang-rumour-after-reward-{repeat}")
        if (after["variables"].get("Saychake") != "2" or after["inventory"] != state["inventory"]
                or after["player"]["money"] != state["player"]["money"]):
            raise AutomationError("Tea revisit regressed the native Wang quest stage or reward")
    client.save_or_load(2)
    transition(client, resource, "map096_湖口村.map", 1, output, "1019-wang-repeat-village-return")
    repeated = interact_at(client, output, resource, "王老五", wang, "1020-wang-revisit-no-reward")
    if repeated["inventory"] != state["inventory"] or repeated["player"]["money"] != state["player"]["money"]:
        raise AutomationError("Wang rewarded the fixed tea revisit again")
    client.save_or_load(3)
    load_checkpoint(client, 1)
    client.act("SetAutoDialogue", enabled=True)
    checkpoint(client, output, "1021-wang-clean-property-source")
    write_json(output / "hukou-wang-property-proof.json", dict(status="passed", nativeSilverGranted=1000,
               nativeWristGranted=1, nativeUnresetRepeatNoReward=True, NoEnd="1", Saychake="2", Event="540",
               sourceSlots=[0, 1, 2, 3], nativeTeaRevisitPreservesStage=True, cheatAssisted=True))


def hukou_wang_to_feng_source(client, output, resource):
    state = idle(client)
    first_wrist = quantity(state, "goods412_一只护腕.ini") == 1
    client.act("SetAutoDialogue", enabled=True)
    if (state["map"] == "map096_湖口村.map" and state["variables"].get("Event") == "540"
            and state["variables"].get("Saychake") == "2" and state["variables"].get("NoEnd") == "1"
            and quantity(state, "goods413_另一只护腕.ini") == 1):
        transition(client, resource, "map097_湖口村茶馆.map", 1, output, "1080-wang-clean-tea-entry")
        for count in range(4):
            interact_at(client, output, resource, "龚楷", "script/map/map097_湖口村茶馆/龚楷对话.txt", f"1081-wang-gong-tea-{count}")
        for repeat in range(2):
            state = interact_at(client, output, resource, "张如梦", "script/map/map097_湖口村茶馆/张如梦对话.txt", f"1082-wang-rumeng-reunion-{repeat}")
        if state["variables"].get("Event") != "550":
            raise AutomationError("Wang route reunion did not reach Event550")
        state = interact_at(client, output, resource, "龚楷", "script/map/map097_湖口村茶馆/龚楷对话.txt", "1083-wang-native-secret-entrance")
        if state["variables"].get("Event") != "560":
            raise AutomationError("Wang route secret entrance did not reach Event560")
        hukou_inquiry_and_entrance(client, output, resource)
        tianwang_password_and_hall(client, output, resource)
        state = idle(client)
    if state["map"] != "map101_天王帮大殿.map" or state["variables"].get("Event") != "580":
        raise AutomationError("Wang handoff requires its native hall Event580 checkpoint")
    # The two hall trials reload the password checkpoint. Complete the side
    # handoff on the final retained branch after those independent trials.
    transition(client, resource, "map100_天王岛.map", 1, output, "1084-wang-final-handoff-island")
    for actor in ("周旱", "草草"):
        before = idle(client)
        for repeat in range(2):
            state = interact_at(client, output, resource, actor, f"script/map/map100_天王岛/{actor}对话.txt",
                                f"1085-wang-final-handoff-{actor}-{repeat}")
            if actor == "草草" and state["variables"].get("Talkcaocao") == "2":
                if (any(row["name"] == actor for row in state["targets"])
                        or state["player"]["money"] != before["player"]["money"] - 1000):
                    raise AutomationError("Grass life story did not remove the actor and consume1000 silver")
                break
    if (state["variables"].get("Saychake") != "3" or state["variables"].get("Talkcaocao") != ("2" if first_wrist else "1")
            or state["variables"].get("NoEnd") != "1" or quantity(state, "goods413_另一只护腕.ini") != 0
            or quantity(state, "goods412_一只护腕.ini") != 0):
        raise AutomationError("Native Zhou and Grass wrist handoff mismatch")
    transition(client, resource, "map101_天王帮大殿.map", 4, output, "1086-wang-handoff-hall-return")
    client.save_or_load(3)
    write_json(output / f"wang-zhou-grass-{'first-wrist' if first_wrist else 'no-first-wrist'}-proof.json", dict(status="passed", nativeZhouHandOff=True,
               nativeGrassHandOff=True, nativeSecondWristConsumed=1, nativeFirstWristConsumed=int(first_wrist),
               nativeFullLifeStory=first_wrist, nativeSilverConsumed=1000 if first_wrist else 0, Saychake="3",
               Talkcaocao="2" if first_wrist else "1", NoEnd="1", Event="580", cheatAssisted=True))
    tianwang_garden_and_betrayal(client, output, resource, stop_before_battle=True)


def hukou_inquiry_and_entrance(client, output, resource):
    state = idle(client)
    client.act("SetAutoDialogue", enabled=True)
    if state["map"] == "map085_朱仙镇客栈.map" and state["variables"].get("Event") == "530":
        assist(client, output, None, True)
        for repeat in range(2):
            interact_at(client, output, resource, "客栈掌柜", "script/map/map085_朱仙镇客栈/客栈掌柜对话.txt",
                        f"970-restored-inn-owner-{repeat}")
        interact_at(client, output, resource, "张琳心", "script/map/map085_朱仙镇客栈/张琳心对话.txt", "971-restored-linxin")
        transition(client, resource, "map071_朱仙镇.map", 1, output, "972-native-wang-dock")
        state = interact_at(client, output, resource, "王佐", "script/map/map071_朱仙镇/王佐对话.txt", "973-native-wang-voyage")
        if state["map"] != "map096_湖口村.map" or state["variables"].get("Event") != "540":
            raise AutomationError("Native Wang voyage did not reach Hukou Event540")
        client.save_or_load(0)
        interact_at(client, output, resource, "张琳心", "script/map/map096_湖口村/张琳心对话.txt", "974-hukou-linxin")
        for actor in ("温谦", "纪小妹", "王老五", "紫荆", "莲姑"):
            for repeat in range(2):
                interact_at(client, output, resource, actor, f"script/map/map096_湖口村/{actor}对话.txt",
                            f"975-hukou-{actor}-{repeat}")
        transition(client, resource, "map097_湖口村茶馆.map", 1, output, "976-hukou-teahouse")
        for repeat in range(2):
            interact_at(client, output, resource, "张琳心", "script/map/map097_湖口村茶馆/张琳心对话.txt", f"977-teahouse-linxin-{repeat}")
        for count in range(4):
            state = interact_at(client, output, resource, "龚楷", "script/map/map097_湖口村茶馆/龚楷对话.txt", f"978-gong-tea-{count}")
            if state["variables"].get("Talkgongkai") != str(min(count + 1, 3)):
                raise AutomationError("Native Gong tea enquiry counter did not advance and repeat")
        for repeat in range(2):
            interact_at(client, output, resource, "肖头", "script/map/map097_湖口村茶馆/茶客1肖头对话.txt", f"979-tea-rumour-{repeat}")
        client.save_or_load(1)
        for repeat in range(2):
            state = interact_at(client, output, resource, "张如梦", "script/map/map097_湖口村茶馆/张如梦对话.txt", f"980-rumeng-reunion-{repeat}")
        if state["variables"].get("Event") != "550":
            raise AutomationError("Native Rumeng reunion did not advance to Event550")
        interact_at(client, output, resource, "张琳心", "script/map/map097_湖口村茶馆/张琳心对话.txt", "981-linxin-after-reunion")
        client.save_or_load(2)
        state = interact_at(client, output, resource, "龚楷", "script/map/map097_湖口村茶馆/龚楷对话.txt", "982-native-token-entrance")
        if state["variables"].get("Event") != "560":
            raise AutomationError("Native token entrance did not advance to Event560")
    if state["map"] in ("map097_湖口村茶馆.map", "map098_天王帮秘道.map") and state["variables"].get("Event") == "560":
        if state["map"] == "map097_湖口村茶馆.map":
            # Gong leaves the player on this trap during his event; leave and
            # re-enter normally so the next visit can activate the secret door.
            client.move(7, 10)
            state = transition(client, resource, "map098_天王帮秘道.map", 2, output, "983-native-secret-tunnel")
        else:
            script_proof(output, resource, "script/map/map097_湖口村茶馆/trap02.txt")
        client.save_or_load(3)
    if state["map"] == "map098_天王帮秘道.map" and state["variables"].get("Event") == "560":
        transition(client, resource, state["map"], 1, output, "984-secret-return-refused")
        transition(client, resource, "map099_洞庭湖畔.map", 2, output, "985-native-lake-shore")
        transition(client, resource, "map098_天王帮秘道.map", 1, output, "986-native-shore-revisit")
        transition(client, resource, "map099_洞庭湖畔.map", 2, output, "987-native-shore-departure")
        state = transition(client, resource, "map121_洞庭湖底.map", 2, output, "988-native-dive")
        client.save_or_load(4)
    if state["map"] != "map121_洞庭湖底.map" or state["variables"].get("Event") != "560":
        raise AutomationError("Hukou entrance requires its native initial lake-bed state")
    walk_required_battle(client, output, resource, None, "989-lake-bed-enemies", progress_variable=None)
    before = idle(client)
    for repeat in range(2):
        state = interact_at(client, output, resource, "宝箱", "script/map/map121_洞庭湖底/宝箱.txt", f"990-broken-lock-{repeat}", position=(7, 40))
    if state["inventory"] != before["inventory"] or state["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Native permanently broken lake-bed chest granted a reward")
    for count in range(5):
        before = idle(client)
        state = interact_at(client, output, resource, "宝箱", "script/map/map121_洞庭湖底/宝箱1.txt", f"991-rusted-lock-{count}", position=(16, 49))
        if state["variables"].get("Openbox3") != str(min(count + 1, 4)):
            raise AutomationError("Native rusty lock did not retain its four-attempt counter")
        if quantity(state, "goods026_盘龙之臂.ini") != quantity(before, "goods026_盘龙之臂.ini") + int(count == 4):
            raise AutomationError("Native fifth rusty-lock attempt did not grant exactly one armguard")
    for position, file, amount in (((9, 12), "宝箱2.txt", 1000), ((12, 28), "宝箱3.txt", 500),
                                    ((7, 41), "宝箱3.txt", 500), ((9, 17), "宝箱4.txt", 1000)):
        before = idle(client)
        state = interact_at(client, output, resource, "宝箱", f"script/map/map121_洞庭湖底/{file}",
                            f"992-lake-silver-{position[0]}-{position[1]}", position=position)
        if state["player"]["money"] != before["player"]["money"] + amount:
            raise AutomationError("Native lake-bed chest silver reward mismatched")
    client.save_or_load(5)
    state = transition(client, resource, "map100_天王岛.map", 2, output, "993-native-tianwang-island")
    client.save_or_load(6)
    write_json(output / "hukou-inquiry-and-entrance-proof.json", dict(status="passed", Event="560",
               nativeWangVoyage=True, nativeRumengReunion=True, nativeGongTeaStates=4,
               nativeSecretTunnelAndDive=True, nativeRustyLockAttempts=5, nativeSilverReward=3000,
               nativeIslandArrival=state["map"], sourceSlots=list(range(7)), cheatAssisted=True))
    print("Native Hukou enquiries, secret entrance and lake-bed treasure passed", flush=True)


def chengdu_first_inquiry(client, output, resource, low_money=False):
    state = idle(client)
    if state["variables"].get("Clue") != "0" or state["map"] not in ("map105_成都.map", "map106_成都客栈一楼.map"):
        raise AutomationError("Chengdu first inquiry requires its normal Clue0 source")
    client.act("SetAutoDialogue", enabled=True)
    city = "script/map/map105_成都/"
    inn = "script/map/map106_成都客栈一楼/"
    house = "script/map/map110_成都民居/"
    if state["map"] == "map105_成都.map":
        npcs = configparser.ConfigParser(interpolation=None)
        npcs.read(resource / "ini/save/map105.npc", encoding="utf-8-sig")
        for section in npcs.sections():
            row = npcs[section]
            if not row.get("scriptfile") or row.get("name") == "董铁嘴":
                continue
            for repeat in range(2):
                if (output / f"1600-city-{section}-{repeat}-script-proof.json").exists():
                    continue
                interact_at(client, output, resource, row["name"], city + row["scriptfile"],
                            f"1600-city-{section}-{repeat}",
                            position=(int(row["mapx"]), int(row["mapy"])) if row["name"] in ("船夫", "妇人") else None)
        interact_at(client, output, resource, "杨瑛", city + "杨瑛的同伴对话.txt", "1601-yang-clue0")
        transition(client, resource, "map110_成都民居.map", 5, output, "1602-he-before-password")
        interact_at(client, output, resource, "贺老四", house + "贺老四.txt", "1603-he-clue0-refusal")
        transition(client, resource, "map105_成都.map", 1, output, "1604-he-early-return")
        transition(client, resource, "map106_成都客栈一楼.map", 1, output, "1605-chengdu-inn")
    client.save_or_load(0)
    source = checkpoint(client, output, "1606-inn-before-inquiry-slot0", ("Talklv",))
    if low_money:
        state = interact_at(client, output, resource, "店小二", inn + "店小二.txt", "1607-poor-waiter")
        if state["variables"].get("Clue") != "10" or state["player"]["money"] != source["player"]["money"]:
            raise AutomationError("Insufficient waiter gratuity did not reach Clue10 without charging")
        interact_at(client, output, resource, "店小二", inn + "店小二.txt", "1608-poor-waiter-repeat")
        transition(client, resource, "map106_成都客栈一楼.map", 1, output, "1609-owner-unasked-exit-refusal")
        interact_at(client, output, resource, "吕掌柜", inn + "吕掌柜.txt", "1610-owner-after-waiter-first")
        state = interact_at(client, output, resource, "吕掌柜", inn + "吕掌柜.txt", "1611-poor-owner-stay",
                            choices=[(inn + "吕掌柜.txt:113", 1)])
        if state["variables"].get("Clue") != "10" or state["player"]["money"] != source["player"]["money"]:
            raise AutomationError("Insufficient ten-silver room charged or advanced the route")
        state = transition(client, resource, "map105_成都.map", 1, output, "1612-poor-inquiry-native-clue15")
        if state["variables"].get("Clue") != "15":
            raise AutomationError("Waiter-first native inquiry did not reach Clue15")
        client.save_or_load(6)
        write_json(output / "chengdu-poor-inquiry-proof.json", dict(status="passed", waiterFirst=True,
                   insufficientWaiterAndRoom=True, ownerUnaskedExitRefusal=True, finalClue="15",
                   sourceSlots=[0, 6], cheatAssisted=True))
        return
    for actor, script in (("店小三", "店小三.txt"), ("路人", "路人的对话.txt"),
                          ("房客男", "房客男.txt"), ("房客女", "房客女.txt")):
        for repeat in range(2):
            interact_at(client, output, resource, actor, inn + script, f"1613-inn-clue0-{actor}-{repeat}")
    interact_at(client, output, resource, "杨瑛", inn + "杨瑛的同伴对话.txt", "1614-inn-yang-clue0")
    interact_at(client, output, resource, "吕掌柜", inn + "吕掌柜.txt", "1615-owner-first")
    client.save_or_load(1)
    source = checkpoint(client, output, "1616-owner-clue0-repeat-source-slot1", ("Talklv",))
    for option in (0, 1):
        load_checkpoint(client, 1)
        state = interact_at(client, output, resource, "吕掌柜", inn + "吕掌柜.txt", f"1617-owner-clue0-{option}",
                            choices=[(inn + "吕掌柜.txt:59", option)])
        if state["variables"].get("Clue") != "0" or state["player"]["money"] != source["player"]["money"] - 10 * option:
            raise AutomationError("Clue0 inn refusal/rest changed its native price or plot stage")
    load_checkpoint(client, 1)
    before = checkpoint(client, output, "1618-waiter-paid-before")
    state = interact_at(client, output, resource, "店小二", inn + "店小二.txt", "1619-waiter-paid")
    if state["variables"].get("Clue") != "10" or state["player"]["money"] != before["player"]["money"] - 5:
        raise AutomationError("Paid waiter inquiry did not cost five and reach Clue10")
    interact_at(client, output, resource, "店小二", inn + "店小二.txt", "1620-waiter-paid-repeat")
    interact_at(client, output, resource, "杨瑛", inn + "杨瑛的同伴对话.txt", "1621-inn-yang-clue10")
    client.save_or_load(2)
    source = checkpoint(client, output, "1622-owner-clue10-source-slot2")
    for option in (0, 1):
        load_checkpoint(client, 2)
        state = interact_at(client, output, resource, "吕掌柜", inn + "吕掌柜.txt", f"1623-owner-clue10-{option}",
                            choices=[(inn + "吕掌柜.txt:113", option)])
        if state["variables"].get("Clue") != "10" or state["player"]["money"] != source["player"]["money"] - 10 * option:
            raise AutomationError("Clue10 inn refusal/rest changed its native price or plot stage")
    load_checkpoint(client, 2)
    state = transition(client, resource, "map105_成都.map", 1, output, "1624-native-clue15")
    if state["variables"].get("Clue") != "15":
        raise AutomationError("Normal owner-first inquiry did not reach Clue15")
    interact_at(client, output, resource, "杨瑛", city + "杨瑛的同伴对话.txt", "1625-city-yang-clue15")
    transition(client, resource, "map110_成都民居.map", 5, output, "1626-he-password-source")
    client.save_or_load(3)
    checkpoint(client, output, "1627-he-clue15-source-slot3")
    state = interact_at(client, output, resource, "贺老四", house + "贺老四.txt", "1628-he-password-and-clue")
    if state["variables"].get("Clue") != "20":
        raise AutomationError("Native He password inquiry did not reach Clue20")
    interact_at(client, output, resource, "贺老四", house + "贺老四.txt", "1629-he-clue20-repeat")
    interact_at(client, output, resource, "杨瑛", house + "杨瑛的同伴对话.txt", "1630-house-yang-clue20")
    client.save_or_load(4)
    checkpoint(client, output, "1631-he-exit-choice-source-slot4")
    for option in (1, 0):
        load_checkpoint(client, 4)
        state = transition(client, resource, "map105_成都.map", 1, output, f"1632-he-exit-choice-{option}",
                           choices=[(house + "trap01.txt:31", option)])
        if state["variables"].get("Clue") != "25":
            raise AutomationError("He exit choice did not reach Clue25")
    client.save_or_load(5)
    checkpoint(client, output, "1633-city-clue25-source-slot5")
    for option in (0, 1):
        interact_at(client, output, resource, "杨瑛", city + "杨瑛的同伴对话.txt", f"1634-city-clue25-choice-{option}",
                    choices=[(city + "杨瑛的同伴对话.txt:39", option)])
    transition(client, resource, "map106_成都客栈一楼.map", 1, output, "1635-dead-waiter-inn")
    transition(client, resource, "map106_成都客栈一楼.map", 1, output, "1636-clue25-search-exit-refusal")
    for actor, script in (("店小三", "店小三.txt"), ("路人", "路人的对话.txt"),
                          ("房客男", "房客男.txt"), ("房客女", "房客女.txt"), ("吕掌柜", "吕掌柜.txt")):
        for repeat in range(2):
            interact_at(client, output, resource, actor, inn + script, f"1637-inn-clue25-{actor}-{repeat}")
    transition(client, resource, "map106_1成都客栈二楼.map", 2, output, "1638-upstairs-letter-search")
    upper = "script/map/map106_1成都客栈二楼/"
    for index in (1, 2, 3):
        interact_at(client, output, resource, f"房客{index}", upper + f"房客{index}的对话.txt", f"1639-upstairs-guest-{index}")
    chengdu_letter_source(client, output, resource)
    write_json(output / "chengdu-first-inquiry-proof.json", dict(status="passed", nativeClues=[0, 10, 15, 20, 25, 30],
               ownerFirst=True, paidWaiter=True, nativeFakeLetter=True, initialCityActors=True,
               roomChoicesBeforeLetter=True, sourceSlots=list(range(7)), finalClue="30", cheatAssisted=False))


def chengdu_letter_source(client, output, resource):
    state = idle(client)
    if state["map"] != "map106_1成都客栈二楼.map" or state["variables"].get("Clue") != "25":
        raise AutomationError("Letter source requires the normally entered Clue25 upper floor")
    client.act("SetAutoDialogue", enabled=True)
    upper = "script/map/map106_1成都客栈二楼/"
    inn = "script/map/map106_成都客栈一楼/"
    before = checkpoint(client, output, "1640-letter-before")
    state = interact_at(client, output, resource, "map106-1书信.ini", upper + "留书.txt", "1641-false-letter")
    if state["variables"].get("Clue") != "30" or quantity(state, "goods405_书信.ini") != quantity(before, "goods405_书信.ini") + 1:
        raise AutomationError("Normal upstairs letter did not grant the clue and one letter")
    transition(client, resource, "map106_成都客栈一楼.map", 1, output, "1642-inn-return-clue30")
    for actor, script in (("店小三", "店小三.txt"), ("路人", "路人的对话.txt"),
                          ("房客男", "房客男.txt"), ("房客女", "房客女.txt")):
        interact_at(client, output, resource, actor, inn + script, f"1643-inn-clue30-{actor}")
    client.save_or_load(6)
    source = checkpoint(client, output, "1644-overnight-source-slot6", ("Talklv", "Talkxiaosan"))
    state = interact_at(client, output, resource, "吕掌柜", inn + "吕掌柜.txt", "1645-clue30-no-room",
                        choices=[(inn + "吕掌柜.txt:180", 0)])
    if state["variables"].get("Clue") != "30" or state["player"]["money"] != source["player"]["money"]:
        raise AutomationError("Refusing Chengdu overnight changed money or clue")
    write_json(output / "chengdu-letter-source-proof.json", dict(status="passed", nativeFakeLetter=True,
               nativeClues=[25, 30], overnightRefused=True, sourceSlot=6, cheatAssisted=False))


def chengdu_room_boundary(client, output, resource):
    state = idle(client)
    stage = state["variables"].get("Clue")
    inn = "script/map/map106_成都客栈一楼/"
    if state["map"] != "map106_成都客栈一楼.map" or stage not in ("0", "30") or state["player"]["money"] != 9:
        raise AutomationError("Room boundary requires the normal pre-room source with explicitly assisted nine silver")
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    line = 59 if stage == "0" else 180
    after = interact_at(client, output, resource, "吕掌柜", inn + "吕掌柜.txt", "1700-nine-silver-room-refusal",
                        choices=[(inn + f"吕掌柜.txt:{line}", 1)])
    if after["variables"].get("Clue") != stage or after["player"]["money"] != 9:
        raise AutomationError("Nine-silver room refusal unexpectedly charged or advanced")
    if stage == "0":
        state = interact_at(client, output, resource, "店小二", inn + "店小二.txt", "1701-nine-silver-waiter-paid")
        if state["variables"].get("Clue") != "10" or state["player"]["money"] != 4:
            raise AutomationError("Nine-silver waiter did not leave four silver at Clue10")
        state = interact_at(client, output, resource, "吕掌柜", inn + "吕掌柜.txt", "1702-four-silver-clue10-room",
                            choices=[(inn + "吕掌柜.txt:113", 1)])
        if state["variables"].get("Clue") != "10" or state["player"]["money"] != 4:
            raise AutomationError("Four-silver Clue10 room charged or advanced")
        transition(client, resource, "map105_成都.map", 1, output, "1703-boundary-clue15-city")
        transition(client, resource, "map106_成都客栈一楼.map", 1, output, "1704-generic-room-clue15")
        for option in (0, 1):
            state = interact_at(client, output, resource, "吕掌柜", inn + "吕掌柜.txt", f"1705-generic-poor-room-{option}",
                                choices=[(inn + "吕掌柜.txt:11", option)])
            if state["variables"].get("Clue") != "15" or state["player"]["money"] != 4:
                raise AutomationError("Generic poor room altered money or its native Clue15")
    client.save_or_load(6)
    write_json(output / "chengdu-room-boundary-proof.json", dict(status="passed", sourceClue=stage,
               nineSilverRefusal=True, genericAndClue10PoorRooms=stage == "0", sourceSlots=[0, 6], cheatAssisted=True))


def chengdu_north_and_traitor(client, output, resource):
    state = idle(client)
    if state["map"] != "map106_成都客栈一楼.map" or state["variables"].get("Clue") != "30":
        raise AutomationError("North ambush requires the native Chengdu overnight source")
    if not state.get("cheatInvincibilityEnabled"):
        raise AutomationError("This assisted north battle requires observed player invincibility")
    client.act("SetAutoDialogue", enabled=True)
    inn = "script/map/map106_成都客栈一楼/"
    city = "script/map/map105_成都/"
    north = "script/map/map111_成都北郊/"
    house = "script/map/map110_成都民居/"
    client.save_or_load(0)
    before = checkpoint(client, output, "1800-overnight-before-slot0")
    state = interact_at(client, output, resource, "吕掌柜", inn + "吕掌柜.txt", "1801-native-chengdu-overnight",
                        choices=[(inn + "吕掌柜.txt:180", 1)])
    if state["variables"].get("Clue") != "31" or state["player"]["money"] != before["player"]["money"] - 10:
        raise AutomationError("Chengdu overnight did not cost ten at native Clue31")
    interact_at(client, output, resource, "吕掌柜", inn + "吕掌柜.txt", "1802-owner-clue31")
    interact_at(client, output, resource, "杨瑛", inn + "杨瑛的同伴对话.txt", "1803-inn-yang-clue31")
    state = transition(client, resource, "map105_成都.map", 1, output, "1804-suo-messenger-clue35")
    if state["variables"].get("Clue") != "35":
        raise AutomationError("Native Suo messenger did not advance Clue31 to35")
    client.save_or_load(1)
    checkpoint(client, output, "1805-city-clue35-source-slot1")
    for actor, script in (("窦俊男", "窦俊男的对话.txt"), ("菱珠", "菱珠的对话.txt")):
        load_checkpoint(client, 1)
        for repeat in range(2):
            interact_at(client, output, resource, actor, city + script, f"1806-city-next-day-{actor}-{repeat}")
    for actor, script in (("窦娇娘", "窦娇娘的对话.txt"), ("窦豆儿", "窦豆的对话.txt"),
                          ("苏宇", "苏宇的对话.txt"), ("春姨", "春姨的对话.txt"), ("杨瑛", "杨瑛的同伴对话.txt")):
        for repeat in range(2):
            interact_at(client, output, resource, actor, city + script, f"1807-city-clue35-{actor}-{repeat}")
    transition(client, resource, "map111_成都北郊.map", 6, output, "1808-north-meeting")
    client.save_or_load(2)
    checkpoint(client, output, "1809-ambush-source-slot2")
    state = transition(client, resource, "map111_成都北郊.map", 3, output, "1810-native-north-ambush")
    if state["variables"].get("Clue") != "36":
        raise AutomationError("Native north ambush did not reach Clue36")
    initial_enemies = sum(row.get("hostile", False) and row.get("attackable", False) for row in state["targets"])
    for trap in (1, 2):
        if idle(client)["variables"].get("Clue") == "36":
            transition(client, resource, "map111_成都北郊.map", trap, output, f"1811-north-fight-exit-refusal-{trap}")
    walk_required_battle(client, output, resource, "40", "1812-north-required-battle", progress_variable="Clue")
    state = checkpoint(client, output, "1813-north-cleared", ("fight111",))
    if state["variables"].get("fight111") != "9" or any(row.get("hostile") and row.get("attackable") for row in state["targets"]):
        raise AutomationError("Native north battle did not finish all nine death callbacks")
    client.save_or_load(3)
    checkpoint(client, output, "1814-north-return-choice-source-slot3")
    for first, second in ((1, 0), (1, 1), (0, 1), (0, 0)):
        load_checkpoint(client, 3)
        state = transition(client, resource, "map105_成都.map", 1, output, f"1815-north-suspect-{first}-{second}",
                           choices=[(north + "trap01.txt:43", first), (north + f"trap01.txt:{49 if first == 0 else 70}", second)])
        expected = "45" if (first, second) == (0, 0) else "41"
        if state["variables"].get("Clue") != expected:
            raise AutomationError("North return suspicion did not preserve its native outcome")
        if (first, second) == (1, 0):
            client.save_or_load(4)
            checkpoint(client, output, "1816-clue41-source-slot4")
    client.save_or_load(5)
    checkpoint(client, output, "1817-city-clue45-source-slot5")
    interact_at(client, output, resource, "杨瑛", city + "杨瑛的同伴对话.txt", "1818-yang-clue45")
    transition(client, resource, "map110_成都民居.map", 5, output, "1819-he-expose")
    client.save_or_load(6)
    checkpoint(client, output, "1820-he-fight-source-slot6")
    state = interact_at(client, output, resource, "贺老四", house + "贺老四.txt", "1821-he-native-betrayal")
    if state["variables"].get("Clue") not in ("46", "50"):
        raise AutomationError("Native He betrayal did not enable its required battle")
    walk_required_battle(client, output, resource, "50", "1822-he-required-battle", progress_variable="Clue")
    script_proof(output, resource, house + "贺老四死亡.txt")
    interact_at(client, output, resource, "杨瑛", house + "杨瑛的同伴对话.txt", "1823-house-yang-clue50")
    client.save_or_load(0)
    checkpoint(client, output, "1824-he-body-choice-source-slot0")
    for choices in (((39, 0),), ((39, 1), (54, 0)), ((39, 1), (54, 1))):
        load_checkpoint(client, 0)
        state = transition(client, resource, "map110_成都民居.map", 2, output, "1825-he-body-" + "-".join(str(v) for _, v in choices),
                           choices=[(house + f"trap02.txt:{line}", option) for line, option in choices])
        if state["variables"].get("Clue") != "60":
            raise AutomationError("Native true-He body clue did not reach Clue60")
    client.save_or_load(6)
    checkpoint(client, output, "1826-he-body-complete-slot6")
    write_json(output / "chengdu-north-and-traitor-proof.json", dict(status="passed", nativeClues=[30, 31, 35, 36, 40, 41, 45, 46, 50, 60],
               initialObservedEnemies=initial_enemies, nativeNorthDeathCallbacks=9, nativeFalseHeDeath=True,
               northReturnFourOutcomes=True, trueHeBodyThreeOutcomes=True, sourceSlots=list(range(7)), cheatAssisted=True))


def rumeng_jianmen_source(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if state["map"] != "map111_成都北郊.map" or state["variables"].get("Event") != "660":
        raise AutomationError("Rumeng pursuit requires the native three-boss rescue checkpoint")
    source_end, source_love = state["variables"]["End"], state["variables"]["Love"]
    client.save_or_load(0)
    transition(client, resource, "map111_成都北郊.map", 1, output, "2600-rescue-city-exit-refused")
    transition(client, resource, "map111_成都北郊.map", 2, output, "2601-rescue-estate-exit-refused")
    state = transition(client, resource, "map116_剑门关.map", 4, output, "2602-native-rumeng-pursuit")
    if (state["variables"].get("Event") != "690" or state["variables"].get("End") != source_end
            or state["variables"].get("Love") != source_love or state["player"]["level"] != 37):
        raise AutomationError("Native pursuit did not transfer to level37 Rumeng while preserving ending flags")
    client.save_or_load(6)
    saved = configparser.ConfigParser()
    saved.read(output / "user-data/save/xjxqy/rpg7/game.ini", encoding="utf-8-sig")
    if saved.getint("state", "chr") != 2:
        raise AutomationError("Native Jianmen checkpoint did not save Rumeng as the current character")
    checkpoint(client, output, "2603-native-jianmen-source-slot6")
    write_json(output / "rumeng-jianmen-source-proof.json", dict(status="passed", Event=690, End=source_end,
               Love=source_love, currentCharacter=2, nativeLevel=37, bothOldRoadsRefused=True,
               sourceSlots=[0, 6], cheatAssisted=True))


def jianmen_deaths(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if (state["map"] != "map116_剑门关.map" or state["variables"].get("Event") != "695"
            or not state["timer"]["started"] or not state["timer"]["hidden"]):
        raise AutomationError("Jianmen death branches require the normal timed duel save")
    source_end = int(state["variables"]["End"])
    client.save_or_load(0)
    outcomes = []
    for actor, slot in (("南宫彩虹", 2), ("张如梦", 3)):
        load_checkpoint(client, 0)
        sequence = records(output)[-1]["sequence"]
        source = f"script/map/map116_剑门关/{actor}死亡.txt"
        if actor == "南宫彩虹":
            assist(client, output, level=60, invincible=True)
            clear_enemies(client, output, "2800-native-caihong-defeat", skills=())
        else:
            client.open_menu("System")
            client.activate("options")
            client.activate("cheat-settings")
            if client.observe().get("cheatInvincibilityEnabled"):
                client.activate("invincibility")
            while not client.observe().get("worldInput"):
                client.ui("Cancel")
            before = checkpoint(client, output, "2801-native-rumeng-loss-before", ("diejian",))
            if before["cheatInvincibilityEnabled"] or before["player"]["level"] != 37:
                raise AutomationError("Rumeng loss requires his native level37 and invincibility disabled")
            client.wait_until(lambda value: value.get("script") == source or value.get("map") != "map116_剑门关.map",
                              timeout=75, description="native Rumeng death callback")
        idle(client, timeout=240)
        state = checkpoint(client, output, f"2802-native-{actor}-death-complete", ("diejian",))
        proof = script_proof(output, resource, source, sequence)
        if (state["map"] != "map071_朱仙镇.map" or state["variables"].get("diejian") != "1"
                or int(state["variables"].get("End") or 0) != source_end + 100 or state["timer"]["started"]):
            raise AutomationError("Native Jianmen death did not add100 once and stop the duel timer")
        client.save_or_load(slot)
        write_json(output / f"2802-{actor}-death-script-proof.json", proof)
        outcomes.append(dict(actor=actor, nativeDeathCallback=proof, End=source_end + 100, sourceSlot=slot))
    client.save_or_load(6)
    write_json(output / "jianmen-deaths-proof.json", dict(status="passed", outcomes=outcomes,
               bothNativeDeaths=True, sourceSlots=[0, 2, 3, 6], cheatAssisted=True))


def yue_rescue_source(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if state["map"] != "map071_朱仙镇.map" or state["variables"].get("Event") != "695":
        raise AutomationError("Yue rescue preparation requires the normal post-Jianmen Zhuxian save")
    source_end = state["variables"]["End"]
    transition(client, resource, "map071_朱仙镇.map", 1, output, "2900-zhuxian-old-road-refused")
    transition(client, resource, "map072_朱仙镇东北.map", 2, output, "2901-zhuxian-northeast-entry")
    transition(client, resource, "map072_朱仙镇东北.map", 2, output, "2902-han-camp-event695-refused")
    transition(client, resource, "map076_岳飞大营.map", 3, output, "2903-native-yue-camp-return")
    transition(client, resource, "map076_岳飞大营.map", 1, output, "2904-yue-camp-event695-exit-refused")
    transition(client, resource, "map076_岳飞大营.map", 3, output, "2905-phoenix-before-warning-refused")
    for attempt in range(2):
        interact_at(client, output, resource, "陆文龙", "script/map/map076_岳飞大营/陆文龙.txt",
                    f"2906-wenlong-return-{attempt}")
    transition(client, resource, "map077_岳飞主帐营.map", 2, output, "2907-native-yueyun-tent-entry")
    client.save_or_load(0)
    state = interact_at(client, output, resource, "岳云", "script/map/map077_岳飞主帐营/岳云对话.txt", "2908-native-yue-warning-and-timer")
    timer = state["timer"]
    if (state["variables"].get("Event") != "700" or state["variables"].get("End") != source_end
            or not timer["started"] or timer["hidden"] or not timer["callbackSet"]
            or timer["script"] != "岳飞死亡.txt" or not 55 <= timer["remainingSeconds"] <= 60):
        raise AutomationError("Native Yueyun warning did not start the original visible sixty-second rescue timer")
    client.save_or_load(1)
    client.save_or_load(6)
    checkpoint(client, output, "2909-native-yue-rescue-source-slot6")
    write_json(output / "yue-rescue-source-proof.json", dict(status="passed", Event=700, End=source_end,
               nativeWarning=True, originalSeconds=60, timerVisible=True, sourceSlots=[0, 1, 6], cheatAssisted=True))


def yue_rescue(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if (state["map"] != "map077_岳飞主帐营.map" or state["variables"].get("Event") != "700"
            or not state["timer"]["started"]):
        raise AutomationError("Yue rescue requires the normal Yueyun timer source")
    source_end = state["variables"]["End"]
    client.save_or_load(0)
    checkpoint(client, output, "3100-native-rescue-timer-before")
    transition(client, resource, "map076_岳飞大营.map", 1, output, "3101-timed-rescue-leaves-tent")
    transition(client, resource, "map076_岳飞大营.map", 1, output, "3102-timed-old-road-refused")
    interact_at(client, output, resource, "陆文龙", "script/map/map076_岳飞大营/陆文龙.txt", "3103-wenlong-native-rescue-warning")
    transition(client, resource, "map081_凤凰山.map", 3, output, "3104-native-phoenix-timed-entry")
    state = transition(client, resource, "map081_凤凰山.map", 4, output, "3105-native-yue-saved-before-timeout")
    if (state["variables"].get("Event") != "700" or state["variables"].get("End") != source_end
            or state["timer"]["started"] or state["timer"]["callbackSet"]
            or any(row["name"] in ("凤凰山杀手", "宋兵", "岳飞", "牛皋", "张宪", "汤怀") for row in state["targets"])
            or not any(row["name"] == "方勉" and row.get("hostile") and row.get("attackable") for row in state["targets"])):
        raise AutomationError("Native rescue did not stop the timer and start the first Fang battle")
    client.save_or_load(6)
    write_json(output / "yue-rescue-proof.json", dict(status="passed", Event=700, End=source_end,
               originalTimerStoppedByNativeRescue=True, nativePhoenixKillersBypassed=True,
               firstFangBattle=True, sourceSlots=[0, 6], cheatAssisted=True))


def fang_first_battle(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if state["map"] != "map081_凤凰山.map" or state["variables"].get("Event") != "700" or state["timer"]["started"]:
        raise AutomationError("First Fang battle requires the normal timely rescue save")
    client.save_or_load(0)
    sequence = records(output)[-1]["sequence"]
    target = next(row for row in state["targets"] if row["name"] == "方勉" and row.get("hostile") and row.get("attackable"))
    client.act("StartCombat", timeout=125, generation=state["generation"], targetId=target["id"],
               radius=64, kills=1, skills=[], timeoutMs=120000)
    state = idle(client, timeout=240)
    proof = script_proof(output, resource, "script/map/map081_凤凰山/方勉第一次死亡.txt", sequence)
    if (state["variables"].get("Event") != "700" or any(row["name"] == "张琳心" for row in state["targets"])
            or not any(row["name"] == "杨瑛" for row in state["targets"])
            or not any(row["name"] == "方勉" and row.get("hostile") and row.get("attackable") for row in state["targets"])):
        raise AutomationError("Native first Fang death did not produce Linxin's fall and the second battle")
    client.save_or_load(6)
    checkpoint(client, output, "3200-native-second-fang-source-slot6")
    write_json(output / "3200-first-fang-death-script-proof.json", proof)
    write_json(output / "fang-first-battle-proof.json", dict(status="passed", nativeFirstFangDeath=proof,
               nativeLinxinFall=True, nativeYangCompanion=True, secondFangBattle=True, sourceSlots=[0, 6], cheatAssisted=True))


def fang_second_branches(client, output, resource, accept_only=False):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if (state["map"] != "map081_凤凰山.map" or state["variables"].get("Event") != "700"
            or not accept_only and (state["variables"].get("End") != "222" or int(state["variables"].get("NoEnd") or 0) != 0)):
        raise AutomationError("Second Fang forks require the normal End222 source without an outstanding debt")
    source = "script/map/map081_凤凰山/方勉第二次死亡.txt"
    site = next(row for row in catalog_for(resource)["choices"] if row["path"] == source)
    client.save_or_load(0)
    sword = "goods054_巨阙剑.ini"
    before_swords = quantity(state, sword)
    source_end = state["variables"].get("End")
    outcomes = []
    for option in ((0,) if accept_only else (0, 1)):
        load_checkpoint(client, 0)
        state = client.observe(VARIABLES)
        sequence = records(output)[-1]["sequence"]
        target = next(row for row in state["targets"] if row["name"] == "方勉" and row.get("hostile") and row.get("attackable"))
        client.act("StartCombat", timeout=125, generation=state["generation"], targetId=target["id"],
                   radius=64, kills=1, skills=[], timeoutMs=120000)
        state = idle(client, timeout=600, choices=[(site["id"], option)], output=output, resource=resource, capture_videos=True)
        proof = script_proof(output, resource, source, sequence)
        expected_map = "map118_泰山.map" if option == 0 else "map029_临安城张府.map"
        if (state["map"] != expected_map or state["variables"].get("Event") != "800"
                or state["variables"].get("End") != source_end or quantity(state, sword) != before_swords + 1
                or option == 1 and state["variables"].get("Result") != "3"):
            raise AutomationError("Native Fang aftermath did not reach its selected summit or End3 battle source")
        client.save_or_load(option + 1)
        checkpoint(client, output, f"3300-native-fang-fork-{option}")
        write_json(output / f"3300-native-fang-fork-{option}-script-proof.json", proof)
        outcomes.append(dict(option=option, map=state["map"], sourceSlot=option + 1, nativeGiantSwordReward=1))
    client.save_or_load(6)
    write_json(output / "fang-second-branches-proof.json", dict(status="passed", outcomes=outcomes,
               End=source_end, nativeSaveReuse=True, sourceSlots=[0, 1, 6] if accept_only else [0, 1, 2, 6], cheatAssisted=True))


def taishan_tournament(client, output, resource, reuse_forks=True):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    resuming_champion = (state["variables"].get("Event") == "820"
                         and (output / "3706-taishan-champion-0-script-proof.json").exists())
    if state["map"] != "map118_泰山.map" or state["variables"].get("Event") != "800" and not resuming_champion:
        raise AutomationError("Taishan tournament requires the normal Fang acceptance save")
    source_end, source_love = state["variables"].get("End"), state["variables"].get("Love")
    if not resuming_champion:
        client.save_or_load(0)
    folder = "script/map/map118_泰山/"
    bindings = configparser.ConfigParser()
    bindings.read(resource / "ini/save/map118.npc", encoding="utf-8-sig")
    actors = [(bindings.get(section, "Name"), bindings.get(section, "ScriptFile"),
               (bindings.getint(section, "MapX"), bindings.getint(section, "MapY")))
              for section in bindings.sections() if section != "Head"
              and bindings.get(section, "ScriptFile", fallback="")]
    unavailable_after_victory = []
    for stage in ((820,) if resuming_champion else (800, 820)):
        for index, (name, script, position) in enumerate(actors):
            if stage == 800 and name == "无虚大师":
                continue
            duplicate = sum(actor[0] == name for actor in actors) > 1
            state = client.observe(VARIABLES)
            bound = [row for row in state["targets"] if row["name"] == name and
                     (not duplicate or row["position"] == dict(x=position[0], y=position[1]))]
            if stage == 820 and len(bound) == 1 and not bound[0].get("interactive"):
                unavailable_after_victory.append(dict(name=name, script=script, nativeTarget=bound[0],
                                                      reason="Native actor has no available interaction after victory"))
                continue
            interact_at(client, output, resource, name, folder + script, f"3700-taishan-{stage}-npc-{index}",
                        position=position if duplicate else None)
            if stage == 800 and name == "卢青":
                interact_at(client, output, resource, name, folder + script, "3701-taishan-luqing-repeat")
        if stage == 820:
            break
        transition(client, resource, "map118_泰山.map", 1, output, "3702-taishan-before-tournament-summit-refused")
        interact_at(client, output, resource, "无虚大师", folder + "无虚大师.txt", "3703-taishan-first-battle")
        for slot, name, callback in ((1, "罗天强", "罗天强打败.txt"), (2, "李啸", "李啸打败.txt")):
            if reuse_forks:
                client.save_or_load(slot)
            state = client.observe(VARIABLES)
            sequence = records(output)[-1]["sequence"]
            target = next(row for row in state["targets"] if row["name"] == name and row.get("hostile") and row.get("attackable"))
            client.act("StartCombat", timeout=125, generation=state["generation"], targetId=target["id"],
                       radius=64, kills=1, skills=[], timeoutMs=120000)
            idle(client, timeout=240)
            write_json(output / f"3704-taishan-battle-{slot}-script-proof.json", script_proof(output, resource, folder + callback, sequence))
        if reuse_forks:
            client.save_or_load(3)
        sites = {name: next(row for row in catalog_for(resource)["choices"] if row["path"] == folder + name)
                 for name in ("方雷打败.txt", "无虚大师.txt")}
        for option in ((1, 0) if reuse_forks else (1,)):
            if reuse_forks:
                load_checkpoint(client, 3)
            state = client.observe(VARIABLES)
            target = next(row for row in state["targets"] if row["name"] == "小雷" and row.get("hostile") and row.get("attackable"))
            client.act("StartCombat", timeout=125, generation=state["generation"], targetId=target["id"],
                       radius=64, kills=1, skills=[], timeoutMs=120000)
            state = idle(client, timeout=240, choices=[(sites["方雷打败.txt"]["id"], option)], output=output, resource=resource)
            if state["variables"].get("Event") != "810":
                raise AutomationError("Native Xiaolei defeat did not reach Event810")
            if option == 1:
                if reuse_forks:
                    client.save_or_load(4)
                for readiness in (1, 0):
                    interact_at(client, output, resource, "无虚大师", folder + "无虚大师.txt",
                                f"3705-taishan-master-readiness-{readiness}", choices=[(sites["无虚大师.txt"]["id"], readiness)])
            state = client.observe(VARIABLES)
            sequence = records(output)[-1]["sequence"]
            target = next(row for row in state["targets"] if row["name"] == "无虚大师" and row.get("hostile") and row.get("attackable"))
            client.act("StartCombat", timeout=125, generation=state["generation"], targetId=target["id"],
                       radius=64, kills=1, skills=[], timeoutMs=120000)
            state = idle(client, timeout=240)
            if state["map"] != "map118_泰山.map" or state["variables"].get("Event") != "820":
                raise AutomationError("Native fourth tournament victory did not crown the player at Event820")
            write_json(output / f"3706-taishan-champion-{option}-script-proof.json", script_proof(output, resource, folder + "无虚大师打败.txt", sequence))
            interact_at(client, output, resource, "小雷", folder + "方雷.txt",
                        f"3706-taishan-champion-{option}-xiaolei-dialogue")
            checkpoint(client, output, f"3706-taishan-champion-{option}")
            if reuse_forks:
                client.save_or_load(5 if option == 1 else 6)
    state = checkpoint(client, output, "3707-taishan-champion-dialogues-complete")
    if state["variables"].get("End") != source_end or state["variables"].get("Love") != source_love:
        raise AutomationError("Tournament unexpectedly changed the ending source conditions")
    client.save_or_load(6)
    write_json(output / "taishan-tournament-proof.json", dict(status="passed", nativeFourVictories=True,
               nativeThirdBattleChoices=[1, 0] if reuse_forks else [1], nativeMasterInteractionChoices=[1, 0],
               nativeAvailableNpcDialoguesBeforeAndAfter=True, midBattleSavesReused=reuse_forks,
               unavailableAfterVictory=unavailable_after_victory,
               Event=820, End=source_end, Love=source_love, sourceSlots=[0, 1, 2, 3, 4, 5, 6] if reuse_forks else [0, 6], cheatAssisted=True))


def summit_end5_source(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if (state["map"] != "map118_泰山.map" or state["variables"].get("Event") != "820"
            or state["variables"].get("Love") != "1" or state["variables"].get("End") != "222"
            or int(state["variables"].get("NoEnd") or 0) != 0 or int(state["variables"].get("Zhujiarenshen") or 0) != 0):
        raise AutomationError("End5 summit requires the normal qualifying tournament save")
    client.save_or_load(0)
    transition(client, resource, "map119_玉皇峰.map", 1, output, "3800-native-summit-arrival")
    client.save_or_load(1)
    state = transition(client, resource, "map120_风波亭.map", 2, output, "3801-native-end5-windbo-source")
    if state["variables"].get("Result") != "5" or state["variables"].get("Event") != "900":
        raise AutomationError("Native summit reunion did not start the End5 rescue battle")
    client.save_or_load(6)
    write_json(output / "summit-end5-source-proof.json", dict(status="passed", nativeLinxinReunion=True,
               normalEnd222=True, debtCleared=True, zhuMedicineNotStolen=True,
               Result=5, Event=900, sourceSlots=[0, 1, 6], cheatAssisted=True))


def summit_finish(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    variables = state["variables"]
    if state["map"] != "map118_泰山.map" or variables.get("Event") != "820":
        raise AutomationError("Summit ending requires a normal completed tournament save")
    if variables.get("Love") == "0":
        ending, marker = "E04", "从此，一提起独孤家"
    elif variables.get("Love") == "1" and (variables.get("End") != "222"
            or variables.get("NoEnd") == "1" or variables.get("Zhujiarenshen") == "1"):
        ending, marker = "E06", "老夫观今日泰山景致"
    else:
        raise AutomationError("This ordinary summit source leads to the End5 battle rather than a summit ending")
    transition(client, resource, "map119_玉皇峰.map", 1, output, "3900-native-ending-summit-arrival")
    client.save_or_load(0)
    source = "script/map/map119_玉皇峰/trap02.txt"
    before = output / "3901-native-summit-ending-before.json"
    checkpoint(client, output, before.stem)
    client.act("SetAutoDialogue", enabled=False)
    transition(client, resource, "Title", 2, output, "3902-native-summit-ending-title",
               expected_terminal=source, final_dialogue=marker)
    proof = script_proof(output, resource, source)
    dialogues = [path for path in output.glob("ending-dialogue-*.json")
                 if marker in json.loads(path.read_text(encoding="utf-8")).get("dialogue", {}).get("text", "")]
    videos = [path for path in output.glob("ending-video-*.json")
              if Path(json.loads(path.read_text(encoding="utf-8")).get("video", "")).name.casefold() == "over.wmv"]
    state = checkpoint(client, output, "3902-native-summit-ending-title")
    if not dialogues or not videos or not title_visible(state):
        raise AutomationError("Summit ending lacks its full original dialogue, credits and title")
    write_json(output / f"ending-proof-{ending}.json", dict(status="passed", endingId=ending, storyEnding=True,
               expectedTerminal=source, script=proof, beforeFile=str(before), sourceSlot=0,
               finalDialogueFile=str(dialogues[-1]), videoFile=str(videos[-1]),
               titleFile=str(output / "3902-native-summit-ending-title.json"), videoSkipped=False, cheatAssisted=True))


def end5_qin_source(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if state["map"] != "map120_风波亭.map" or state["variables"].get("Result") != "5":
        raise AutomationError("End5 guards require the ordinary summit rescue source")
    client.save_or_load(0)
    interact_at(client, output, resource, "张琳心", "script/map/map120_风波亭/张琳心对话.txt", "3999-end5-before-rescue-linxin")
    transition(client, resource, "map120_风波亭.map", 3, output, "3999-native-end5-rescue-battle")
    walk_required_battle(client, output, resource, "13", "4000-native-end5-windbo-guards", progress_variable="Fight120")
    state = checkpoint(client, output, "4001-native-end5-qin-source", ("Fight120", "qinhuidie"))
    if (state["variables"].get("Fight120") != "13" or int(state["variables"].get("qinhuidie") or 0) != 0
            or not any(row["name"] == "秦桧" and row.get("hostile") and row.get("attackable") for row in state["targets"])):
        raise AutomationError("Thirteen native guard deaths did not produce Qin's first End5 battle")
    write_json(output / "4001-end5-guards-script-proof.json", script_proof(output, resource, "script/map/map120_风波亭/万俟亵死亡.txt"))
    client.save_or_load(6)
    write_json(output / "end5-qin-source-proof.json", dict(status="passed", Result=5,
               nativeWindboDeaths=13, sourceSlots=[0, 6], cheatAssisted=True))


def end5_finish(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = client.observe((*VARIABLES, "qinhuidie"))
    if (state["map"] != "map120_风波亭.map" or state["variables"].get("Result") != "5"
            or int(state["variables"].get("qinhuidie") or 0) not in (0, 1)):
        raise AutomationError("End5 finish requires Qin's first ordinary battle")
    if int(state["variables"].get("qinhuidie") or 0) == 0:
        checkpoint(client, output, "4100-native-end5-first-qin-before", ("qinhuidie",))
        client.save_or_load(0)
        walk_required_battle(client, output, resource, "1", "4100-native-end5-first-qin-battle", progress_variable="qinhuidie")
    idle(client, timeout=300)
    first_death = script_proof(output, resource, "script/map/map120_风波亭/秦桧死亡.txt")
    write_json(output / "4101-native-end5-first-qin-death-script-proof.json", first_death)
    before = output / "4102-native-end5-final-battle-before.json"
    state = checkpoint(client, output, before.stem, ("qinhuidie",))
    if state["variables"].get("qinhuidie") != "1":
        raise AutomationError("Native first Qin defeat did not produce the Manjianghong battle")
    client.save_or_load(1)
    client.act("SetAutoDialogue", enabled=False)
    source = "script/map/map120_风波亭/秦桧死亡2.txt"
    marker = "太好了，就叫“南宫飞云”"
    target = next(row for row in state["targets"] if row["name"] == "秦桧" and row.get("hostile") and row.get("attackable"))
    client.act("StartCombat", timeout=125, generation=state["generation"], targetId=target["id"],
               radius=64, kills=1, skills=[2], allowMeleeFallback=False, timeoutMs=120000)
    idle(client, timeout=600, output=output, resource=resource, expected_terminal=source, final_dialogue=marker)
    proof = script_proof(output, resource, source)
    dialogues = [path for path in output.glob("ending-dialogue-*.json")
                 if marker in json.loads(path.read_text(encoding="utf-8")).get("dialogue", {}).get("text", "")]
    videos = [path for path in output.glob("ending-video-*.json")
              if Path(json.loads(path.read_text(encoding="utf-8")).get("video", "")).name.casefold() == "over.wmv"]
    state = checkpoint(client, output, "4103-native-end5-title")
    if not title_visible(state) or not dialogues or not videos:
        raise AutomationError("End5 lacks the completed Feiyun dialogue, credits and title")
    write_json(output / "ending-proof-E05.json", dict(status="passed", endingId="E05", storyEnding=True,
               expectedTerminal=source, script=proof, nativeFirstQinDeath=first_death, beforeFile=str(before), sourceSlot=1,
               finalDialogueFile=str(dialogues[-1]), videoFile=str(videos[-1]),
               titleFile=str(output / "4103-native-end5-title.json"), videoSkipped=False, cheatAssisted=True))


def fang_refusal_end2(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if (state["map"] != "map081_凤凰山.map" or state["variables"].get("Event") != "700"
            or state["variables"].get("End") == "222" and int(state["variables"].get("NoEnd") or 0) != 1):
        raise AutomationError("End2 refusal requires a normal second Fang source outside the End3 condition")
    client.save_or_load(0)
    before_path = output / "3600-native-end2-before.json"
    checkpoint(client, output, before_path.stem)
    source = "script/map/map081_凤凰山/方勉第二次死亡.txt"
    site = next(row for row in catalog_for(resource)["choices"] if row["path"] == source)
    target = next(row for row in state["targets"] if row["name"] == "方勉" and row.get("hostile") and row.get("attackable"))
    client.act("StartCombat", timeout=125, generation=state["generation"], targetId=target["id"],
               radius=64, kills=1, skills=[], timeoutMs=120000)
    idle(client, timeout=600, choices=[(site["id"], 1)], output=output, resource=resource,
         expected_terminal=source, final_dialogue="独自离去，从此不再过问江湖中事")
    state = checkpoint(client, output, "3601-native-end2-title")
    proof = script_proof(output, resource, source)
    dialogues = [p for p in output.glob("ending-dialogue-*.json")
                 if "独自离去，从此不再过问江湖中事" in json.loads(p.read_text(encoding="utf-8")).get("dialogue", {}).get("text", "")]
    videos = [p for p in output.glob("ending-video-*.json")
              if Path(json.loads(p.read_text(encoding="utf-8")).get("video", "")).name.casefold() == "over.wmv"]
    if not title_visible(state) or not dialogues or not videos:
        raise AutomationError("End2 lacks its completed native refusal text, credits or title")
    write_json(output / "ending-proof-E02.json", dict(status="passed", endingId="E02", storyEnding=True,
               sourceSlot=0, expectedTerminal=source, script=proof, beforeFile=str(before_path),
               finalDialogueFile=str(dialogues[-1]), videoFile=str(videos[-1]),
               titleFile=str(output / "3601-native-end2-title.json"), videoSkipped=False, cheatAssisted=True))


def end3_qin_source(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if state["map"] != "map029_临安城张府.map" or state["variables"].get("Result") != "3":
        raise AutomationError("End3 guards require the normal Fang refusal branch")
    client.save_or_load(0)
    walk_required_battle(client, output, resource, "7", "3400-native-seven-qin-house-guards", progress_variable="fight029huwei")
    state = checkpoint(client, output, "3401-native-end3-windbo-entry", ("fight029huwei", "Fight120"))
    if state["map"] != "map120_风波亭.map" or state["variables"].get("fight029huwei") != "7":
        raise AutomationError("Seven native house guard deaths did not load the End3 Windbo battle")
    write_json(output / "3401-house-guard-script-proof.json", script_proof(output, resource, "script/map/map029_临安城张府/护卫死亡.txt"))
    client.save_or_load(1)
    transition(client, resource, "map120_风波亭.map", 2, output, "3402-native-end3-moqi-battle")
    walk_required_battle(client, output, resource, "29", "3403-native-windbo-guards", progress_variable="Fight120")
    state = checkpoint(client, output, "3404-native-end3-qin-source", ("Fight120",))
    if (state["variables"].get("Result") != "3" or state["variables"].get("Fight120") != "29"
            or not any(row["name"] == "秦桧" and row.get("hostile") and row.get("attackable") for row in state["targets"])):
        raise AutomationError("Native29 Windbo deaths did not produce Qin's final End3 battle")
    write_json(output / "3404-windbo-guards-script-proof.json", script_proof(output, resource, "script/map/map120_风波亭/万俟亵死亡.txt"))
    client.save_or_load(6)
    write_json(output / "end3-qin-source-proof.json", dict(status="passed", Result=3,
               nativeHouseGuardDeaths=7, nativeWindboDeaths=29, sourceSlots=[0, 1, 6], cheatAssisted=True))


def end3_finish(client, output, resource):
    client.act("SetAutoDialogue", enabled=False)
    state = client.observe(VARIABLES)
    if state["map"] != "map120_风波亭.map" or state["variables"].get("Result") != "3":
        raise AutomationError("End3 finish requires the native Qin battle source")
    client.save_or_load(0)
    source = "script/map/map120_风波亭/秦桧死亡.txt"
    checkpoint(client, output, "3500-native-end3-before", ("Fight120",))
    target = next(row for row in state["targets"] if row["name"] == "秦桧" and row.get("hostile") and row.get("attackable"))
    client.act("StartCombat", timeout=125, generation=state["generation"], targetId=target["id"],
               radius=64, kills=1, skills=[], timeoutMs=120000)
    idle(client, timeout=600, output=output, resource=resource, expected_terminal=source,
         final_dialogue="独孤剑死后，杨瑛黯然回到天王帮")
    state = checkpoint(client, output, "3500-native-end3-title")
    proof = script_proof(output, resource, source)
    dialogues = [p for p in output.glob("ending-dialogue-*.json")
                 if "独孤剑死后，杨瑛黯然回到天王帮" in json.loads(p.read_text(encoding="utf-8")).get("dialogue", {}).get("text", "")]
    videos = [p for p in output.glob("ending-video-*.json")
              if Path(json.loads(p.read_text(encoding="utf-8")).get("video", "")).name.casefold() == "over.wmv"]
    if not title_visible(state) or not dialogues or not videos or 7 not in proof["executedLines"]:
        raise AutomationError("End3 lacks its actual source branch, completed text, credits and native title")
    write_json(output / "ending-proof-E03.json", dict(status="passed", endingId="E03", storyEnding=True,
               sourceSlot=0, expectedTerminal=source, script=proof, finalDialogueFile=str(dialogues[-1]),
               beforeFile=str(output / "3500-native-end3-before.json"),
               videoFile=str(videos[-1]), titleFile=str(output / "3500-native-end3-title.json"),
               videoSkipped=False, cheatAssisted=True))


def yue_timeout(client, output, resource):
    client.act("SetAutoDialogue", enabled=False)
    state = client.observe(VARIABLES)
    if (state.get("map") != "map077_岳飞主帐营.map" or state["variables"].get("Event") != "700"
            or not state["timer"]["started"] or state["timer"]["hidden"]):
        raise AutomationError("Yue timeout requires the normal visible rescue timer save")
    client.save_or_load(0)
    checkpoint(client, output, "3000-native-yue-timeout-before")
    source = "script/common/岳飞死亡.txt"
    idle(client, timeout=240, output=output, resource=resource, expected_terminal=source,
         final_dialogue="英雄，大错既已铸成")
    state = checkpoint(client, output, "3001-native-yue-timeout-title")
    if not title_visible(state):
        raise AutomationError("Native Yue timeout did not return to title")
    write_json(output / "3001-native-yue-timeout-script-proof.json", script_proof(output, resource, source))
    write_json(output / "yue-timeout-proof.json", dict(status="passed", originalSeconds=60,
               timerExpiredNaturally=True, nativeReturnToTitle=True, storyEnding=False, sourceSlots=[0], cheatAssisted=True))


def jianmen_timeout(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if state["map"] != "map116_剑门关.map" or state["variables"].get("Event") != "690":
        raise AutomationError("Jianmen timeout requires the native Rumeng arrival before the duel")
    if not state.get("cheatInvincibilityEnabled"):
        raise AutomationError("Waiting through the original duel requires observed player invincibility")
    source_end = int(state["variables"]["End"])
    client.save_or_load(0)
    state = transition(client, resource, "map116_剑门关.map", 1, output, "2700-native-jianmen-vows-and-duel")
    timer = state["timer"]
    if (state["variables"].get("Event") != "695" or not timer["started"] or not timer["hidden"]
            or not timer["callbackSet"] or timer["triggerSeconds"] != 0
            or timer["script"] != "剑门关之战.txt" or not 55 <= timer["remainingSeconds"] <= 60):
        raise AutomationError("Native duel did not start the original hidden sixty-second timer")
    client.save_or_load(1)
    before = checkpoint(client, output, "2701-native-hidden-duel-source-slot1", ("diejian",))
    loaded = load_checkpoint(client, 1)
    if (not loaded["timer"]["hidden"] or not loaded["timer"]["started"]
            or loaded["timer"]["remainingSeconds"] != before["timer"]["remainingSeconds"]):
        raise AutomationError("Normal duel save/load did not preserve the hidden timer state")
    checkpoint(client, output, "2702-hidden-duel-normally-reloaded", ("diejian",))
    sequence = records(output)[-1]["sequence"]
    client.wait_until(lambda value: value.get("inEvent") or value["timer"]["remainingSeconds"] <= 30,
                      timeout=45, description="native Jianmen timer reaching thirty seconds")
    checkpoint(client, output, "2703-hidden-duel-halfway", ("diejian",))
    client.wait_until(lambda value: value.get("inEvent") or value["timer"]["remainingSeconds"] <= 1,
                      timeout=45, description="native Jianmen timer approaching zero")
    checkpoint(client, output, "2704-hidden-duel-before-zero", ("diejian",))
    client.wait_until(lambda value: value.get("script") == "script/map/map116_剑门关/剑门关之战.txt"
                      or value.get("map") != "map116_剑门关.map",
                      timeout=10, description="original zero-second callback")
    idle(client, timeout=240)
    state = checkpoint(client, output, "2705-native-jianmen-timeout-complete", ("diejian",))
    proof = script_proof(output, resource, "script/map/map116_剑门关/剑门关之战.txt", sequence)
    if (state["map"] != "map071_朱仙镇.map" or int(state["variables"].get("End") or 0) != source_end + 200
            or state["timer"]["started"] or state["timer"]["callbackSet"]):
        raise AutomationError("Native timeout did not add200 and return to Dugu in Zhuxian")
    client.save_or_load(6)
    write_json(output / "jianmen-timeout-script-proof.json", proof)
    write_json(output / "jianmen-timeout-proof.json", dict(status="passed", originalSeconds=60, hiddenTimer=True,
               nativeSaveReload=True, timerExpiredNaturally=True, beforeEnd=source_end, End=source_end + 200,
               nativeCallback=proof, sourceSlots=[0, 1, 6], cheatAssisted=True))


def chengdu_three_bosses(client, output, resource, direct=False):
    client.act("SetAutoDialogue", enabled=True)
    state = checkpoint(client, output, "2500-three-bosses-before", ("fight113", "huihuidan", "zhui", "zhuiyang"))
    medicine = "goods218_回回丹.ini"
    if (state["map"] != "map113_五剑堂正厅.map" or state["variables"].get("Clue") != "130"
            or int(state["variables"].get("fight113") or 0) != 0 or quantity(state, medicine) != 1):
        raise AutomationError("Three bosses require the native uncompleted battle with one Huihui pill")
    if not state.get("cheatInvincibilityEnabled"):
        raise AutomationError("Three-boss assisted battle requires observed player invincibility")
    source_end = state["variables"]["End"]
    client.save_or_load(0)
    outcomes = []
    source = "script/map/map113_五剑堂正厅/敌人死亡.txt"
    site = next(row for row in catalog_for(resource)["choices"] if row["path"] == source)
    for retained in ((True,) if direct else (True, False)):
        for option in ((1,) if direct else (0, 1)):
            load_checkpoint(client, 0)
            key = f"{'retained' if retained else 'used'}-{option}"
            if not retained:
                before = checkpoint(client, output, f"2501-antidote-use-{option}-before")
                item = next(row for row in before["inventory"] if row["file"] == medicine and row["quantity"] == 1)
                sequence = records(output)[-1]["sequence"]
                client.act("UseItem", generation=before["generation"], slot=item["slot"])
                state = idle(client)
                if quantity(state, medicine) != 0 or state["variables"].get("Clue") != "130":
                    raise AutomationError("Native antidote use did not consume one without changing the quest")
                write_json(output / f"2501-antidote-use-{option}-script-proof.json",
                           script_proof(output, resource, "script/goods/回回丹.txt", sequence))
            battle_sequence = records(output)[-1]["sequence"]
            clear_enemies(client, output, f"2502-three-boss-{key}", skills=(), stop_variable=("Event", "660"),
                          choices=[(site["id"], option)], resource=resource)
            state = checkpoint(client, output, f"2503-three-boss-result-{key}", ("fight113", "huihuidan", "zhui", "zhuiyang"))
            variables = state["variables"]
            if (state["map"] != "map111_成都北郊.map" or variables.get("Event") != "660"
                    or variables.get("fight113") != "3" or variables.get("End") != source_end
                    or variables.get("huihuidan") != str(int(retained)) or variables.get("Love") != str(option)
                    or variables.get("zhui") != str(option) or int(variables.get("zhuiyang") or 0) != option
                    or quantity(state, medicine) != 0 or quantity(state, "goods217_百辟丹.ini") != 0):
                raise AutomationError("Native rescue, medicine or pursuit outcome does not match its branch")
            deaths = [row for row in records(output) if row.get("eventType") == "script.start"
                      and row.get("virtualPath") == source and row["sequence"] > battle_sequence]
            if len(deaths) != 3:
                raise AutomationError("Three-boss branch does not have three native completed death callbacks")
            for death in deaths:
                completed_script(output, death)
            write_json(output / f"2503-three-boss-{key}-script-proof.json", script_proof(output, resource, source))
            slot = 1 + (0 if retained else 2) + option
            client.save_or_load(slot)
            outcomes.append(dict(retainedAntidote=retained, pursuitOption=option, huihuidan=int(retained),
                                 Love=option, nativeDeaths=3, Event=660, sourceSlot=slot))
            print(f"Native three-boss rescue: antidote={int(retained)} pursuit={option}", flush=True)
    load_checkpoint(client, 2)
    client.save_or_load(6)
    checkpoint(client, output, "2504-rescue-before-rumeng-source-slot6", ("fight113", "huihuidan", "zhui", "zhuiyang"))
    write_json(output / "chengdu-three-bosses-proof.json", dict(status="passed", outcomes=outcomes,
               nativeItemUseForMissingAntidote=not direct, bothAntidotesAndPursuitChoices=not direct,
               sourceSlots=[0, 2, 6] if direct else [0, 1, 2, 3, 4, 6], cheatAssisted=True))


def chengdu_secret_door(client, output, resource, direct=False):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if state["map"] != "map105_成都.map" or state["variables"].get("Clue") != "110":
        raise AutomationError("Secret door route requires Suo's native medicine checkpoint")
    transition(client, resource, "map111_成都北郊.map", 6, output, "2400-medicine-return-north")
    transition(client, resource, "map112_五剑堂.map", 2, output, "2401-estate-second-killers")
    walk_required_battle(client, output, resource, None, "2402-second-estate-clear", progress_variable=None)
    state = checkpoint(client, output, "2403-second-estate-cleared", ("fight112",))
    if state["variables"].get("fight112") != "40":
        raise AutomationError("Second estate visit did not accumulate another twenty native death callbacks")
    transition(client, resource, "map113_五剑堂正厅-1.map", 2, output, "2404-native-missing-yang-hall")
    transition(client, resource, "map113_五剑堂正厅-1.map", 1, output, "2405-hall-before-door-exit-refused")
    client.save_or_load(0)
    checkpoint(client, output, "2406-five-swords-source-slot0")
    source = "script/map/map113_五剑堂正厅-1/trap02.txt"
    sites = sorted((row for row in catalog_for(resource)["choices"] if row["path"] == source), key=lambda row: row["line"])
    if len(sites) != 5:
        raise AutomationError("Five-sword source does not contain five native binary choices")
    combinations = []
    for options in (((1, 0, 0, 0, 1),) if direct else itertools.product(range(2), repeat=5)):
        load_checkpoint(client, 0)
        key = "".join(map(str, options))
        transition(client, resource, "map113_五剑堂正厅.map", 2, output, "2407-five-swords-" + key,
                   choices=[(site["id"], option) for site, option in zip(sites, options)])
        state = checkpoint(client, output, "2408-five-swords-value-" + key, ("Mimen",))
        expected = (1 if options[0] else 2) + sum((2 if option else 1) * (10 ** index)
                   for index, option in enumerate(options[1:4], 1)) + (10000 if options[4] else 20000)
        if state["variables"].get("Clue") != "120" or state["variables"].get("Mimen") != str(expected):
            raise AutomationError("Native five-sword combination did not preserve its arithmetic and Clue120")
        combinations.append(dict(options=list(options), Mimen=expected))
        if options == (0, 0, 0, 0, 0) or expected == 11111:
            slot = 2 if expected == 11111 else 1
            client.save_or_load(slot)
            checkpoint(client, output, f"2409-five-swords-{'correct' if slot == 2 else 'wrong'}-source-slot{slot}", ("Mimen",))
        print(f"Five swords: {key} -> {expected}", flush=True)
    for slot, correct in (((2, True),) if direct else ((1, False), (2, True))):
        load_checkpoint(client, slot)
        for trap in (4, 5, 6):
            prefix = f"2410-door-trap-{'correct' if correct else 'wrong'}-{trap}"
            transition(client, resource, "map113_五剑堂正厅.map", trap, output, prefix)
            proof = script_proof(output, resource, f"script/map/map113_五剑堂正厅/trap0{trap}.txt")
            execution = [row for row in records(output) if row.get("executionId") == proof["executionId"]]
            changes = [row.get("apiName") for row in execution if row.get("apiName") in ("changelife", "changethew", "changemana")]
            if (bool(changes) == correct or not correct and set(changes) != {"changelife", "changethew", "changemana"}):
                raise AutomationError("Native secret-room trap did not follow the actual sword combination")
    state = checkpoint(client, output, "2411-native-three-boss-source-slot6", ("Mimen",))
    if state["variables"].get("Clue") != "130" or state["variables"].get("Mimen") != "11111":
        raise AutomationError("Correct-door route did not naturally start the three-boss encounter")
    client.save_or_load(6)
    write_json(output / "chengdu-secret-door-proof.json", dict(status="passed", combinations=combinations,
               exactCombinations=len(combinations), correctOptions=[1, 0, 0, 0, 1], secondEstateDeathCallbacks=20,
               nativeThreeBossStart=True, Clue="130", Mimen="11111", allThreeTrapsBothBranches=not direct,
               trapDamageScope="Native script calls verified; invincibility means ordinary damage amounts remain unverified",
               sourceSlots=[0, 2, 6] if direct else [0, 1, 2, 6], cheatAssisted=True))


def chengdu_city_medicine(client, output, resource):
    state = idle(client)
    if state["map"] != "map105_成都.map" or state["variables"].get("Clue") != "90":
        raise AutomationError("City medicine route requires the native poisoned-Yang return")
    if not state.get("cheatInvincibilityEnabled"):
        raise AutomationError("Assisted city battle requires observed player invincibility")
    client.act("SetAutoDialogue", enabled=True)
    city = "script/map/map105_成都/"
    client.save_or_load(5)
    checkpoint(client, output, "2300-city-clue90-source-slot5")
    state = transition(client, resource, "map105_成都.map", 8, output, "2301-native-city-six-killer-ambush")
    if state["variables"].get("Clue") != "91":
        raise AutomationError("City ambush did not start native Clue91")
    client.save_or_load(0)
    checkpoint(client, output, "2302-city-battle-source-slot0", ("fight105",))
    for trap in range(1, 7):
        transition(client, resource, "map105_成都.map", trap, output, f"2303-city-battle-refusal-{trap}")
    for option in (0, 1):
        load_checkpoint(client, 0)
        for kill in range(7):
            state = idle(client, choices=[(city + "杀手死光.txt:23", option)], output=output, resource=resource)
            if state["variables"].get("Clue") == "105":
                break
            if state["variables"].get("Clue") != "91":
                raise AutomationError("City battle reached an unexpected native clue")
            client.act("StartCombat", timeout=125, generation=state["generation"], radius=64,
                       kills=1, skills=[], timeoutMs=120000)
            checkpoint(client, output, f"2304-city-battle-choice-{option}-kill-{kill}", ("fight105",))
        else:
            raise AutomationError("City battle did not finish six native deaths")
        state = checkpoint(client, output, f"2305-city-death-choice-{option}", ("fight105",))
        if state["variables"].get("fight105") != "6" or any(row.get("hostile") and row.get("attackable") for row in state["targets"]):
            raise AutomationError("City battle did not finish all six native callbacks")
        write_json(output / f"2305-city-death-choice-{option}-script-proof.json", script_proof(output, resource, city + "杀手死光.txt"))
        client.save_or_load(option + 1)
    for option in (0, 1):
        load_checkpoint(client, 2)
        state = idle(client)
        bodies = [row for row in state["targets"] if row["name"] == "成都黑衣杀手尸体"]
        body = next(row for row in bodies if sum(other["position"] == row["position"] for other in bodies) == 1)
        state = interact_at(client, output, resource, body["name"], city + "杀手的尸体.txt",
                            f"2306-city-body-smell-{option}", position=(body["position"]["x"], body["position"]["y"]),
                            choices=[(city + "杀手的尸体.txt:34", option)])
        if state["variables"].get("Clue") != "105":
            raise AutomationError("Repeated city body choice changed Clue105")
    transition(client, resource, "map105_成都.map", 6, output, "2307-medicine-before-north-refused")
    transition(client, resource, "map108_成都铁匠铺.map", 3, output, "2308-native-smell-blacksmith")
    before = checkpoint(client, output, "2309-suo-medicine-source-slot3", ("Inroom",))
    if before["variables"].get("Inroom") != "2":
        raise AutomationError("Native blacksmith smell did not advance Inroom2")
    client.save_or_load(3)
    state = transition(client, resource, "map108_成都铁匠铺.map", 2, output, "2310-native-suo-last-words")
    if state["variables"].get("Clue") != "110" or quantity(state, "goods218_回回丹.ini") != quantity(before, "goods218_回回丹.ini") + 1:
        raise AutomationError("Suo last words did not award one native medicine at Clue110")
    transition(client, resource, "map105_成都.map", 1, output, "2311-native-suo-return-city")
    client.save_or_load(6)
    checkpoint(client, output, "2312-native-secret-door-route-source-slot6")
    write_json(output / "chengdu-city-medicine-proof.json", dict(status="passed", nativeClues=[90, 91, 100, 105, 110],
               sixDeathCallbacksEachOption=True, bothDeathChoices=True, bothRepeatedBodyChoices=True,
               sixCityBattleExitRefusals=True, nativeSuoLastWords=True, nativeMedicineAward=1,
               sourceSlots=[0, 1, 2, 3, 5, 6], cheatAssisted=True))


def chengdu_estate_poison(client, output, resource):
    client.act("SetAutoDialogue", enabled=True)
    state = idle(client)
    if (state["map"] == "map113_五剑堂正厅-1.map" and state["variables"].get("Clue") == "90"
            and (output / "2209-native-yang-poison-script-proof.json").exists()):
        return finish_chengdu_estate_poison(client, output, resource)
    if state["map"] != "map105_成都.map" or state["variables"].get("Clue") != "75":
        raise AutomationError("Estate poison route requires the normally discovered Fang estate")
    if not state.get("cheatInvincibilityEnabled"):
        raise AutomationError("Assisted estate battle requires observed player invincibility")
    transition(client, resource, "map111_成都北郊.map", 6, output, "2200-estate-north")
    transition(client, resource, "map112_五剑堂.map", 2, output, "2201-estate-entry")
    state = transition(client, resource, "map112_五剑堂.map", 3, output, "2202-native-estate-guard-battle")
    if state["variables"].get("Clue") != "80":
        raise AutomationError("Native estate guard did not start Clue80")
    client.save_or_load(0)
    checkpoint(client, output, "2203-estate-battle-source-slot0", ("fight112", "shashousw"))
    for trap in (1, 2):
        transition(client, resource, "map112_五剑堂.map", trap, output, f"2204-estate-before-battle-refusal-{trap}")
    walk_required_battle(client, output, resource, None, "2205-estate-twenty-killers", progress_variable=None)
    state = checkpoint(client, output, "2206-estate-cleared", ("fight112", "shashousw"))
    if state["variables"].get("fight112") != "20" or state["variables"].get("shashousw") != "1":
        raise AutomationError("Estate battle did not complete twenty native death callbacks")
    script_proof(output, resource, "script/map/map112_五剑堂/杀手死亡.txt")
    client.save_or_load(1)
    checkpoint(client, output, "2207-estate-cleared-source-slot1")
    transition(client, resource, "map112_五剑堂.map", 1, output, "2208-estate-after-battle-return-refusal")
    state = transition(client, resource, "map113_五剑堂正厅-1.map", 2, output, "2209-native-yang-poison")
    if state["variables"].get("Clue") != "90" or not any(row["name"] == "杨瑛" for row in state["targets"]):
        raise AutomationError("Native hall entry did not poison Yang at Clue90")
    finish_chengdu_estate_poison(client, output, resource)


def finish_chengdu_estate_poison(client, output, resource):
    state = checkpoint(client, output, "2210-poisoned-yang-interaction-review")
    yang = [row for row in state["targets"] if row["name"] == "杨瑛"]
    if len(yang) != 1 or yang[0].get("interactive") is not False:
        raise AutomationError("Native poisoned Yang should have no available click interaction")
    write_json(output / "poisoned-yang-unbound-dialogue.json", dict(nativeTarget=yang[0],
               rawCandidate="script/map/map113_五剑堂正厅-1/杨瑛的同伴对话.txt",
               classification="NPC has no native click interaction at Clue90; candidate remains unexecuted"))
    client.save_or_load(2)
    checkpoint(client, output, "2211-poisoned-hall-source-slot2")
    transition(client, resource, "map112_五剑堂.map", 1, output, "2212-poisoned-hall-return-estate")
    transition(client, resource, "map112_五剑堂.map", 2, output, "2213-medicine-before-hall-refused")
    transition(client, resource, "map111_成都北郊.map", 1, output, "2214-medicine-north")
    transition(client, resource, "map111_成都北郊.map", 2, output, "2215-medicine-before-estate-refused")
    transition(client, resource, "map105_成都.map", 1, output, "2216-native-empty-city")
    client.save_or_load(6)
    checkpoint(client, output, "2217-native-city-ambush-source-slot6")
    write_json(output / "chengdu-estate-poison-proof.json", dict(status="passed", nativeClues=[75, 80, 90],
               estateDeathCallbacks=20, nativeYangPoison=True, hallVariant="map113_五剑堂正厅-1.map",
               beforeBattleExitAndHallRefused=True, medicineReturnRefused=True,
               sourceSlots=[0, 1, 2, 6], cheatAssisted=True))


def chengdu_lv_body_and_estate(client, output, resource):
    state = idle(client)
    if state["map"] != "map110_成都民居.map" or state["variables"].get("Clue") != "60":
        raise AutomationError("Lv body route requires the normally retained true-He body clue")
    client.act("SetAutoDialogue", enabled=True)
    city = "script/map/map105_成都/"
    inn = "script/map/map106_成都客栈一楼/"
    estate = "script/map/map112_五剑堂/"
    transition(client, resource, "map105_成都.map", 1, output, "2100-lv-body-city-entry")
    state = transition(client, resource, "map106_成都客栈一楼.map", 1, output, "2101-missing-innkeeper")
    if any(row["name"] == "吕掌柜" for row in state["targets"]):
        raise AutomationError("Native Clue60 inn entry did not remove Lv")
    for repeat in range(2):
        interact_at(client, output, resource, "店小三", inn + "店小三.txt", f"2102-waiter-missing-lv-{repeat}")
    state = checkpoint(client, output, "2103-waiter-clue60", ("Talkxiaosan",))
    if state["variables"].get("Talkxiaosan") != "2":
        raise AutomationError("Native missing-innkeeper conversation did not set Talkxiaosan2")
    state = transition(client, resource, "map105_成都.map", 1, output, "2104-missing-lv-clue65")
    if state["variables"].get("Clue") != "65":
        raise AutomationError("Native inn exit did not reach Clue65")
    interact_at(client, output, resource, "杨瑛", city + "杨瑛的同伴对话.txt", "2105-yang-clue65")
    client.save_or_load(0)
    checkpoint(client, output, "2106-lv-body-without-estate-source-slot0", ("Talkfangzhang",))
    state = transition(client, resource, "map105_成都.map", 7, output, "2107-body-map-interpretation",
                       choices=[(city + "trap07.txt:51", 0)])
    if state["variables"].get("Clue") != "70":
        raise AutomationError("Native map interpretation did not reach Clue70")
    client.save_or_load(1)
    checkpoint(client, output, "2108-clue70-unknown-estate-source-slot1", ("Talkfangzhang",))
    choice = next(row for row in catalog_for(resource)["choices"]
                  if row["path"] == city + "杨瑛的同伴对话.txt" and row["message"] == "独孤剑：那“口”指的是")
    for option in (0, 1):
        load_checkpoint(client, 1)
        state = interact_at(client, output, resource, "杨瑛", choice["path"], f"2109-yang-unknown-estate-{option}",
                            choices=[(choice["id"], option)])
        if state["variables"].get("Clue") != ("70" if option == 0 else "71"):
            raise AutomationError("Unknown estate companion choice changed its native outcome")
    client.save_or_load(2)
    checkpoint(client, output, "2110-clue71-companion-source-slot2")
    load_checkpoint(client, 0)
    state = transition(client, resource, "map105_成都.map", 7, output, "2111-body-unknown-sound",
                       choices=[(city + "trap07.txt:51", 1)])
    if state["variables"].get("Clue") != "71":
        raise AutomationError("Body sound interpretation without estate knowledge did not reach71")
    client.save_or_load(3)
    checkpoint(client, output, "2112-body-clue71-source-slot3")
    interact_at(client, output, resource, "杨瑛", city + "杨瑛的同伴对话.txt", "2113-yang-clue71")
    transition(client, resource, "map111_成都北郊.map", 6, output, "2114-unknown-estate-north")
    transition(client, resource, "map112_五剑堂.map", 2, output, "2115-unknown-estate-entry")
    state = transition(client, resource, "map112_五剑堂.map", 3, output, "2116-native-guard71-to75")
    if state["variables"].get("Clue") != "75":
        raise AutomationError("Native guard71 did not identify the Fang estate")
    load_checkpoint(client, 0)
    transition(client, resource, "map111_成都北郊.map", 6, output, "2117-early-estate-north")
    transition(client, resource, "map112_五剑堂.map", 2, output, "2118-early-estate-entry")
    transition(client, resource, "map112_五剑堂.map", 3, output, "2119-native-early-guard-inquiry")
    state = checkpoint(client, output, "2120-estate-name-learned", ("Talkfangzhang",))
    if state["variables"].get("Talkfangzhang") != "1" or state["variables"].get("Clue") != "65":
        raise AutomationError("Native early guard inquiry did not preserve65 and learn the estate")
    transition(client, resource, "map111_成都北郊.map", 1, output, "2121-early-estate-return-north")
    transition(client, resource, "map105_成都.map", 1, output, "2122-early-estate-return-city")
    client.save_or_load(4)
    checkpoint(client, output, "2123-lv-body-known-estate-source-slot4", ("Talkfangzhang",))
    state = transition(client, resource, "map105_成都.map", 7, output, "2124-body-known-map",
                       choices=[(city + "trap07.txt:51", 0)])
    if state["variables"].get("Clue") != "70":
        raise AutomationError("Known estate map interpretation did not reach70")
    client.save_or_load(5)
    checkpoint(client, output, "2125-clue70-known-estate-source-slot5", ("Talkfangzhang",))
    for option in (0, 1):
        load_checkpoint(client, 5)
        state = interact_at(client, output, resource, "杨瑛", choice["path"], f"2126-yang-known-estate-{option}",
                            choices=[(choice["id"], option)])
        if state["variables"].get("Clue") != ("70" if option == 0 else "75"):
            raise AutomationError("Known estate companion choice changed its native outcome")
    load_checkpoint(client, 4)
    state = transition(client, resource, "map105_成都.map", 7, output, "2127-body-known-sound",
                       choices=[(city + "trap07.txt:51", 1)])
    if state["variables"].get("Clue") != "75":
        raise AutomationError("Body sound interpretation with estate knowledge did not reach75")
    client.save_or_load(6)
    checkpoint(client, output, "2128-native-estate-battle-source-slot6", ("Talkfangzhang",))
    write_json(output / "chengdu-lv-body-and-estate-proof.json", dict(status="passed", nativeClues=[60, 65, 70, 71, 75],
               nativeWaiterRepeat=True, bodyChoicesWithAndWithoutEstateKnowledge=True,
               companionChoicesWithAndWithoutEstateKnowledge=True, nativeGuard71To75=True,
               sourceSlots=list(range(7)), cheatAssisted=False))


def chengdu_fortune(client, output, resource):
    state = idle(client)
    if state["map"] != "map105_成都.map" or state["variables"].get("Clue") != "25":
        raise AutomationError("Fortune branches require the normally retained Clue25 city source")
    client.act("SetAutoDialogue", enabled=True)
    client.move(16, 32)
    seen = set()
    source = "script/map/map105_成都/董铁嘴的对话.txt"
    sites = {"0": 12, "1": 28, "2": 44}
    selected = []
    def current_question():
        state = client.wait_until(lambda value: bool(value.get("choices")), variables=(*VARIABLES, "Talksuanminxs"),
                                  description="native randomly selected fortune question")
        site = source + ":" + str(sites[state["variables"]["Talksuanminxs"]])
        selected[:] = [(site, 0 if (site, 0) not in seen else 1)]
        return selected
    for attempt in range(40):
        client.save_or_load(0)
        state = idle(client)
        before = state["player"]["money"]
        state = interact_at(client, output, resource, "董铁嘴", source, f"1900-fortune-{attempt}", choices=current_question)
        if state["variables"].get("Clue") != "25" or state["player"]["money"] != before:
            raise AutomationError("Native fortune altered money or its retained story stage")
        seen.add(selected[0])
        if len(seen) == 6:
            client.save_or_load(6)
            write_json(output / "chengdu-fortune-proof.json", dict(status="passed", naturalRandomQuestions=3,
                       allSixOptions=sorted(seen), attempts=attempt + 1, sourceSlots=[0, 6], cheatAssisted=False))
            return
    raise AutomationError("Natural fortune sampling still has pending options after forty attempts")


def chengdu_companion_choices(client, output, resource):
    state = idle(client)
    stage = state["variables"].get("Clue")
    if stage not in ("0", "15", "25", "30", "41", "45", "50", "60", "65", "70", "71", "75") or (
            state["map"] not in ("map105_成都.map", "map110_成都民居.map")
            and not (stage in ("0", "30", "65") and state["map"] == "map106_成都客栈一楼.map")):
        raise AutomationError("Companion tour requires an ordinary retained source at a reviewed Chengdu clue stage")
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(0)
    completed = []
    catalog = catalog_for(resource)
    for destination, trap in (("map105_成都.map", None), ("map106_成都客栈一楼.map", 1),
                              ("map106_1成都客栈二楼.map", 1), ("map107_成都杂货铺.map", 2),
                              ("map108_成都铁匠铺.map", 3), ("map109_成都药店.map", 4),
                              ("map110_成都民居.map", 5), ("map111_成都北郊.map", 6), ("map112_五剑堂.map", 6)):
        load_checkpoint(client, 0)
        if idle(client)["map"] != "map105_成都.map":
            transition(client, resource, "map105_成都.map", 1, output, "2000-companion-city-" + Path(destination).stem)
        if destination != "map105_成都.map":
            first_map = "map106_成都客栈一楼.map" if destination == "map106_1成都客栈二楼.map" else destination
            if destination == "map112_五剑堂.map":
                first_map = "map111_成都北郊.map"
            transition(client, resource, first_map, trap, output, "2001-companion-map-" + Path(destination).stem)
        if destination == "map106_1成都客栈二楼.map":
            transition(client, resource, destination, 2, output, "2002-companion-upstairs")
        if destination == "map112_五剑堂.map":
            transition(client, resource, destination, 2, output, "2002-companion-estate")
        source = "script/map/" + Path(destination).stem + "/杨瑛的同伴对话.txt"
        choices = [row for row in catalog["choices"] if row["path"] == source]
        if stage == "25":
            first = next((row for row in choices if row["label"] == "Clue25"), None)
            nested = []
            paths = ((0,), (1,)) if first else ((),)
        elif stage == "41":
            first = next(row for row in choices if row["message"] == "独孤剑：我怀疑……")
            nested = sorted((row for row in choices if row["message"] == "独孤剑：他的……"), key=lambda row: row["line"])
            paths = ((1, 0), (1, 1), (0, 1), (0, 0))
        elif stage == "60":
            first = next(row for row in choices if row["message"] == "独孤剑：这可能指的是……")
            nested = [next(row for row in choices if row["message"] == "独孤剑：我想应该是")]
            paths = ((0,), (1, 0), (1, 1))
        elif stage == "70":
            first = next(row for row in choices if row["message"] == "独孤剑：那“口”指的是")
            nested = []
            paths = ((0,), (1,))
        else:
            first, nested, paths = None, [], ((),)
        client.save_or_load(1)
        for options in paths:
            load_checkpoint(client, 1)
            known_estate = client.observe((*VARIABLES, "Talkfangzhang"))["variables"].get("Talkfangzhang") == "1"
            plan = [(first["id"], options[0])] if options else []
            if len(options) == 2:
                plan.append((nested[options[0] if stage == "41" else 0]["id"], options[1]))
            state = interact_at(client, output, resource, "杨瑛", source,
                                "2003-companion-" + Path(destination).stem + "-" + "-".join(map(str, options)), choices=plan)
            expected = "45" if stage == "41" and options == (0, 0) else stage
            if stage == "70" and options == (1,):
                expected = "75" if known_estate else "71"
            if state["variables"].get("Clue") != expected:
                raise AutomationError("Map-local companion choice changed its native clue outcome")
        completed.append(source)
    client.save_or_load(6)
    write_json(output / "chengdu-companion-choices-proof.json", dict(status="passed", sourceClue=stage,
               nativeBoundMaps=completed, allLocalChoiceOutcomes=True, sourceSlots=[0, 1, 6], cheatAssisted=False))


def yun_wrist(client, output, resource, low_money=False):
    state = idle(client)
    stage = state["variables"].get("Suba", "0") or "0"
    if stage not in ("0", "201", "220") or state["map"] not in ("map016_临安城.map", "map035_临安城东.map"):
        raise AutomationError("Cloud wrist route requires a normal Linan source before its next handoff")
    client.act("SetAutoDialogue", enabled=True)
    if state["map"] == "map016_临安城.map":
        transition(client, resource, "map035_临安城东.map", 2, output, "1500-yun-outskirts")
    client.save_or_load(0)
    source = checkpoint(client, output, "1501-yun-source-slot0")
    cloud = "script/map/map035_临安城东/云笑风对话.txt"
    tea = "script/map/map035_临安城东/茶馆老板对话.txt"
    if stage != "220":
        for actor, script in (("茶馆老板", "茶馆老板对话.txt"), ("妇人", "妇人对话.txt"),
                              ("酒客", "酒客对话.txt"), ("卖鱼人", "卖鱼人对话.txt")):
            interact_at(client, output, resource, actor, "script/map/map035_临安城东/" + script,
                        "1502-yun-initial-" + actor)
        before = checkpoint(client, output, "1503-yun-introduction-before")
        state = interact_at(client, output, resource, "云笑风", cloud, "1504-yun-introduction")
        if low_money:
            if state["variables"].get("Suba") != "201" or state["player"]["money"] != before["player"]["money"]:
                raise AutomationError("Insufficient two-silver introduction did not preserve money at Suba201")
            interact_at(client, output, resource, "茶馆老板", tea, "1505-yun-poor-tea")
            state = interact_at(client, output, resource, "云笑风", cloud, "1506-yun-poor-repeat")
            if state["variables"].get("Suba") != "201" or state["player"]["money"] != before["player"]["money"]:
                raise AutomationError("Repeated poor introduction unexpectedly advanced or charged money")
            client.save_or_load(6)
            write_json(output / "yun-poor-introduction-proof.json", dict(status="passed", sourceStage=stage,
                       finalStage="201", moneyUnchanged=True, nativeRepeatedRefusal=True, sourceSlots=[0, 6], cheatAssisted=True))
            return
        if state["variables"].get("Suba") != "210" or state["player"]["money"] != before["player"]["money"] - 2:
            raise AutomationError("Cloud introduction did not charge exactly two at native Suba210")
        write_json(output / "yun-two-silver-observation.json", dict(sourceStage=stage,
                   beforeMoney=before["player"]["money"], afterMoney=state["player"]["money"],
                   moneyDelta=state["player"]["money"] - before["player"]["money"],
                   originalNarrativeTwoSilver=True, source=script_proof(output, resource, cloud),
                   classification="verified exact two-silver payment"))
        interact_at(client, output, resource, "云笑风", cloud, "1507-yun-reminder")
        for index in range(2):
            interact_at(client, output, resource, "茶馆老板", tea, f"1508-yun-tea-{index}")
        client.save_or_load(1)
        checkpoint(client, output, "1509-yun-followup-source-slot1", ("Talkchalaoban",))
        transition(client, resource, "map016_临安城.map", 1, output, "1510-yun-city")
        transition(client, resource, "map027_临安城赌坊.map", 7, output, "1511-yun-casino")
        before = checkpoint(client, output, "1512-wulai-before")
        state = interact_at(client, output, resource, "吴来", "script/map/map027_临安城赌坊/吴来.txt", "1513-wulai-return-money")
        if state["variables"].get("Suba") != "220" or state["player"]["money"] != before["player"]["money"] + 100:
            raise AutomationError("Wu Lai did not return exactly one hundred at Suba220")
        client.save_or_load(2)
        checkpoint(client, output, "1514-wulai-completed-slot2")
        transition(client, resource, "map016_临安城.map", 1, output, "1515-wulai-city-return")
        transition(client, resource, "map035_临安城东.map", 2, output, "1516-yun-final-visit")
        client.save_or_load(3)
        checkpoint(client, output, "1517-yun-turnin-source-slot3")
    before = checkpoint(client, output, "1518-yun-turnin-before")
    state = interact_at(client, output, resource, "云笑风", cloud, "1519-yun-first-wrist")
    enough = before["player"]["money"] >= 100
    if (quantity(state, "goods412_一只护腕.ini") != quantity(before, "goods412_一只护腕.ini") + 1
            or state["player"]["money"] != before["player"]["money"] - (100 if enough else 0)
            or any(row["name"] == "云笑风" for row in state["targets"])
            or not enough and state["variables"].get("NoEnd") != "1"):
        raise AutomationError("Cloud wrist handoff did not preserve its native repayment boundary")
    interact_at(client, output, resource, "茶馆老板", tea, "1520-yun-tea-after-departure")
    transition(client, resource, "map016_临安城.map", 1, output, "1521-yun-city-with-wrist")
    client.save_or_load(6)
    write_json(output / "yun-wrist-proof.json", dict(status="passed", sourceStage=stage,
               nativeFirstWrist=True, nativeCloudDeparture=True, repayment=enough,
               Event=state["variables"].get("Event"), NoEnd=state["variables"].get("NoEnd", "0") or "0",
               sourceSlots=[0, 6] if stage == "220" else [0, 1, 2, 3, 6], cheatAssisted=False))


def yun_linxin_reunion_source(client, output, resource):
    state = idle(client)
    if state["map"] not in ("map016_临安城.map", "map015_临安城南.map") or state["variables"].get("Event") != "200" or quantity(state, "goods412_一只护腕.ini") != 1:
        raise AutomationError("Wrist continuation requires the normal Cloud completion at Event200")
    client.act("SetAutoDialogue", enabled=True)
    if state["map"] == "map016_临安城.map":
        transition(client, resource, "map015_临安城南.map", 1, output, "2100-wrist-linxin-south")
    state = transition(client, resource, "map015_临安城南.map", 4, output, "2100-wrist-linxin-reunion")
    if state["variables"].get("Event") != "210" or quantity(state, "goods412_一只护腕.ini") != 1:
        raise AutomationError("Normal Linxin reunion did not retain the first wrist at Event210")
    client.save_or_load(6)
    checkpoint(client, output, "2101-wrist-wuyi-source-slot6")
    write_json(output / "yun-linxin-reunion-source-proof.json", dict(status="passed", nativeReunion=True,
               firstWristRetained=True, Event="210", NoEnd=state["variables"].get("NoEnd", "0") or "0",
               sourceSlot=6, cheatAssisted=False))


def save_preview_layout(client, output, resource):
    idle(client)
    client.open_menu("System")
    client.activate("save-load")
    state = client.wait_until(lambda value: "saveSlot" in value, description="native save preview menu")
    previews = []
    for slot in (0, 1, 6):
        while state["saveSlot"] != slot:
            client.focus("load")
            client.ui("Down" if state["saveSlot"] < slot else "Up")
            state = client.observe()
        frame = state["frame"]
        client.wait_until(lambda value: value.get("frame", 0) >= frame + 2,
                          description="presented selected save preview")
        checkpoint(client, output, f"save-preview-slot{slot}")
        previews.append(dict(slot=slot, image=f"save-preview-slot{slot}.png"))
    for _ in range(3):
        if client.observe().get("worldInput"):
            break
        client.ui("Cancel")
    if not client.observe().get("worldInput"):
        raise AutomationError("Save preview menu did not close normally")
    write_json(output / "save-preview-layout-proof.json", dict(status="passed", nativeMenu=True,
               selectedSlots=previews, noSaveWritten=True))


def normal_exit(client, identity, output):
    state = client.observe()
    if not title_visible(state):
        if not any(item["name"] == "return-to-title" for item in state.get("ui", [])):
            client.open_menu("System")
        client.activate("return-to-title")
        client.wait_until(title_visible, description="normal return to title")
    client.focus("exit")
    state = client.observe()
    try:
        client.submit("SendUIAction", context=state["context"], action="Confirm")
    except AutomationError as error:
        if not any(reason in str(error) for reason in ("Pipe closed", "Pipe I/O failed: 109", "Pipe I/O failed: 233")):
            raise
    deadline = time.monotonic() + 15
    transient_errors = []
    while True:
        try:
            if not process_running(identity["pid"], identity["command"][0]):
                break
        except PermissionError as error:
            if error.winerror != 5:
                raise
            transient_errors.append(str(error))
        if time.monotonic() >= deadline:
            raise TimeoutError("Normal menu exit did not stop the recorded game process")
        time.sleep(0.1)
    write_json(output / "normal-exit-proof.json", dict(menuExit=True, processStopped=True, pid=identity["pid"],
               transientProcessQueryErrors=transient_errors))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--inventory-only", action="store_true")
    parser.add_argument("--difficulty", choices=("normal", "hard"), default="normal")
    parser.add_argument("--chapter", choices=("opening", "early-detours", "departure", "hengshan-dialogues", "blood-letter", "wuyi-first-visit", "linan-inn", "hemei-inn-choices", "tavern-inquiry-repair", "linan-tavern", "linan-token", "casino-choices", "festival-choices", "linan-residents", "interior-dialogues", "saved-npc-dialogues", "saved-partner-dialogues", "partner-dialogue-tour", "save-preview-layout", "remaining-map-traps", "zhaoqi-matchmaking", "native-loss-prompt", "character-configuration", "character-combat-configuration", "character-configuration-return", "character-equipment-limits", "character-yang-ending", "interior-chests", "remaining-interior-tour", "saved-object-pickups", "dynamic-drop-search", "linan-shop-tiers", "chengdu-shop-tiers", "linan-shop-transactions", "linan-rare-weapon", "coastal-clue", "coastal-unpaid-clue", "jia-and-qiuyu", "prescription-request", "bixia-first-rescue", "hemei-rescue", "hemei-marriage", "prescription-complete", "rumeng-and-linxin", "wuyi-second-visit", "stone-door-and-liuzhongyuan", "linan-blacksmith-aftermath", "zhang-home-and-nangong", "kill-zhangfeng-choice", "furong-and-shitang", "huashan-inquiry", "changan-inquiry", "changan-rescue", "stone-wall-late", "huashan-hidden-path", "huashan-bridge", "changan-poem", "huashan-confrontation", "changan-to-guide", "linhai-and-shaolin", "stone-treasure-early", "shaolin-first-inquiry", "poem-late-confrontation", "shaolin-traitor-and-lessons", "stone-treasure-late", "zhuxian-first-inquiry", "treasure-followup-confrontation", "doctor-treasure-followup", "camps-han-first", "camps-yue-first", "yue-tent-background", "zhuxian-overnight", "han-red-jade", "han-no-red-jade", "han-no-red-jade-bypass", "yue-gold-clothes", "jin-infiltration", "yue-frontline-death", "nangong-showdown-source", "nangong-player-defeat-changbai", "nangong-partner-defeat-tianshan", "nangong-player-defeat-tianshan", "nangong-partner-defeat-changbai", "tianshan-snow-lotus", "changbai-compass-medicine", "changbai-zhu-hai", "changbai-zhu-wife", "changbai-zhu-two-medicines", "hukou-inquiry-and-entrance", "changbai-bandits-first", "lake-opened-chests-repeat", "tianwang-password-and-hall", "hukou-wang-property", "tianwang-garden-and-betrayal", "tianwang-return-and-letter", "hukou-unreturned-letter", "hukou-wang-to-feng-source", "yun-wrist", "yun-wrist-low-money", "chengdu-first-inquiry", "chengdu-low-money-first-inquiry", "chengdu-letter-source", "chengdu-room-boundary", "chengdu-north-and-traitor", "chengdu-fortune", "chengdu-companion-choices", "yun-linxin-reunion-source", "chengdu-lv-body-and-estate", "chengdu-estate-poison", "chengdu-city-medicine", "chengdu-secret-door", "chengdu-secret-door-direct", "chengdu-three-bosses", "chengdu-three-bosses-direct", "rumeng-jianmen-source", "jianmen-timeout", "jianmen-deaths", "yue-rescue-source", "yue-timeout", "yue-rescue", "fang-first-battle", "fang-second-branches", "fang-acceptance", "taishan-tournament", "taishan-uninterrupted", "summit-end5-source", "summit-finish", "end5-qin-source", "end5-finish", "end3-qin-source", "end3-finish", "fang-refusal-end2"), default="opening")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--recheck-bound-dialogues", action="store_true",
                        help="Replay already-covered partner bindings through the ordinary dialogue tour")
    parser.add_argument("--parent", type=Path)
    parser.add_argument("--load-slot", type=int, choices=range(7))
    parser.add_argument("--cheat-assisted", action="store_true", help="Label this run and explicitly admit assisted parents")
    parser.add_argument("--enable-partner-combat", action="store_true",
                        help="Enable PartnerCombat in the cloned normal source slot, preserving the parent")
    parser.add_argument("--assist-partner-invincible", action="store_true",
                        help="Protect companions in a cloned source slot for explicitly assisted hard battles")
    parser.add_argument("--assist-money", type=int, help="Set player money only in a cloned branch source")
    parser.add_argument("--assist-local-date", help="Authorized YYYY-MM-DD date for this isolated test process")
    parser.add_argument("--assist-ruby-quantity", type=int, choices=(0, 1), help="Set existing red jade quantity only in a cloned branch source")
    parser.add_argument("--assist-npc-vulnerable", help="Remove invincibility from one saved current-map NPC in a cloned source")
    parser.add_argument("--assist-npc-life", nargs=2, metavar=("NAME", "LIFE"), help="Set one named NPC's positive life only in a cloned branch source")
    parser.add_argument("--assist-ginseng-quantity", type=int, choices=(1, 2), help="Set Linxin medicine quantity only in a cloned branch source")
    parser.add_argument("--assist-player-life-max", type=int, help="Set hero life maximum in a cloned numeric-assistance source")
    parser.add_argument("--assist-player-life", type=int, help="Set positive hero life only in a cloned branch source")
    parser.add_argument("--assist-player-level", type=int, choices=range(1, 81), help="Set the existing hero level only in a cloned numeric-assistance source")
    parser.add_argument("--assist-character-level", nargs=2, type=int, metavar=("INDEX", "LEVEL"), help="Set an existing saved character level in a cloned assisted source")
    parser.add_argument("--assist-player-evade", type=int, help="Set nonnegative hero evade only in a cloned branch source")
    parser.add_argument("--assist-invincible", action="store_true", help="Enable invincibility through the native options menu")
    parser.add_argument("--assist-level", type=int, choices=range(1, 81), help="Raise the player level through the native options menu")
    parser.add_argument("--exit", action="store_true", help="Exit normally after the chapter")
    args = parser.parse_args()
    if args.recheck_bound_dialogues and args.chapter != "partner-dialogue-tour":
        parser.error("Bound dialogue replay requires --chapter partner-dialogue-tour")
    if args.assist_npc_life is not None:
        try:
            npc_name, npc_life = args.assist_npc_life
            args.assist_npc_life = (npc_name, int(npc_life))
        except ValueError:
            parser.error("Assisted NPC life must be a positive integer")
        if not npc_name or args.assist_npc_life[1] <= 0:
            parser.error("Assisted NPC life requires a name and positive integer")
    if args.assist_local_date:
        try:
            parsed_date = datetime.date.fromisoformat(args.assist_local_date)
            if parsed_date.isoformat() != args.assist_local_date:
                raise ValueError("noncanonical date")
        except ValueError:
            parser.error("Process date must be a valid YYYY-MM-DD date")
    if args.assist_local_date and (not args.cheat_assisted or args.resume):
        parser.error("Process date assistance requires a new --cheat-assisted run; resume keeps its original date")
    if args.assist_money is not None and not 0 <= args.assist_money <= 99999999:
        parser.error("Assisted money must be between0 and99999999")
    if (args.assist_money is not None or args.assist_ruby_quantity is not None or args.assist_npc_vulnerable is not None or args.assist_npc_life is not None or args.assist_player_life is not None or args.assist_ginseng_quantity is not None or args.assist_player_life_max is not None or args.assist_player_evade is not None or args.assist_player_level is not None or args.assist_character_level is not None) and (not args.parent or not args.cheat_assisted):
        parser.error("Saved number assistance requires a new --parent child and --cheat-assisted")
    if args.enable_partner_combat and not args.parent:
        parser.error("Partner-combat save correction requires a new child from --parent")
    if args.assist_partner_invincible and (not args.parent or not args.cheat_assisted):
        parser.error("Companion protection requires a new --parent child and --cheat-assisted")
    output, assets = args.output.resolve(), args.assets.resolve()
    resource = assets / "xjxqy"
    if (args.assist_invincible or args.assist_level is not None) and not args.cheat_assisted:
        parser.error("Native cheat actions require --cheat-assisted")
    if args.inventory_only:
        output.mkdir(parents=True, exist_ok=False)
        catalog = catalog_for(resource)
        write_json(output / "branch-catalog.json", catalog)
        print(json.dumps(catalog["counts"]))
        return
    if args.parent and (args.resume or args.load_slot is None or args.chapter == "opening"):
        parser.error("A saved-game child needs a later chapter and --load-slot, without --resume")
    if args.resume:
        identity = json.loads((output / "run.json").read_text(encoding="utf-8"))
        if (identity["resourceId"] != "XJXQY" or identity["difficulty"] != args.difficulty
                or identity.get("cheatAssisted") and not args.cheat_assisted
                or not identity["session"].startswith("xjxqy-")):
            raise AutomationError("Resume identity does not match an isolated normal XJXQY run")
        if not process_running(identity["pid"], identity["command"][0]):
            raise AutomationError("Recorded process has stopped; create a child from its normal save")
        if args.cheat_assisted:
            identity["cheatAssisted"] = True
            write_json(output / "run.json", identity)
    else:
        if not args.exe:
            parser.error("A new run requires --exe")
        output.mkdir(parents=True, exist_ok=False)
        if args.parent:
            parent = args.parent.resolve()
            parent_identity = json.loads((parent / "run.json").read_text(encoding="utf-8"))
            if (parent_identity["resourceId"] != "XJXQY"
                    or parent_identity.get("cheatAssisted") and not args.cheat_assisted):
                raise AutomationError("Parent is not XJXQY or needs explicit admission of assistance")
            if process_running(parent_identity["pid"], parent_identity["command"][0]):
                with Client(parent_identity["session"], transcript=parent / "commands.jsonl") as client:
                    normal_exit(client, parent_identity, parent)
            if not (parent / "normal-exit-proof.json").is_file():
                raise AutomationError("Parent has no normal-exit evidence")
            shutil.copytree(parent / "user-data/save", output / "user-data/save")
            hashes = lambda root: {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                                   for p in root.rglob("*") if p.is_file()}
            original, copied = hashes(parent / "user-data/save"), hashes(output / "user-data/save")
            if original != copied:
                raise AutomationError("Copied normal save bytes differ")
            write_json(output / "normal-save-clone.json", dict(parentRun=str(parent), sourceSlot=args.load_slot,
                       hashes=original, normalExitVerified=True, cheatAssisted=parent_identity["cheatAssisted"]))
            if args.assist_money is not None or args.assist_ruby_quantity is not None or args.assist_npc_vulnerable is not None or args.assist_npc_life is not None or args.assist_player_life is not None or args.assist_ginseng_quantity is not None or args.assist_player_life_max is not None or args.assist_player_evade is not None or args.assist_player_level is not None or args.assist_character_level is not None:
                correction = assist_saved_branch_numbers(output / "user-data/save/xjxqy" / f"rpg{args.load_slot + 1}", args.assist_money, args.assist_ruby_quantity, args.assist_npc_vulnerable, args.assist_player_life, args.assist_ginseng_quantity, args.assist_player_life_max, args.assist_player_evade, args.assist_player_level, args.assist_npc_life, args.assist_character_level)
                write_json(output / "saved-number-assistance.json", dict(corrections=correction, sourceSlot=args.load_slot, parentUnchanged=True, cheatAssisted=True))
            if args.enable_partner_combat:
                correction = enable_saved_partner_combat(output / "user-data/save/xjxqy" / f"rpg{args.load_slot + 1}" / "game.ini")
                write_json(output / "partner-combat-save-correction.json", dict(correction=correction,
                           sourceSlot=args.load_slot, parentUnchanged=True, cheatAssisted=parent_identity["cheatAssisted"]))
            if args.assist_partner_invincible:
                correction = protect_saved_partners(output / "user-data/save/xjxqy" / f"rpg{args.load_slot + 1}" / "partner0.ini")
                write_json(output / "partner-invincibility-assistance.json", dict(correction=correction,
                           sourceSlot=args.load_slot, parentUnchanged=True, purpose="Explicitly assisted companion battle",
                           mustRemoveBeforeRequiredPartnerDefeat=True, cheatAssisted=True))
            args.difficulty = parent_identity["difficulty"]
        executable, session = args.exe.resolve(), "xjxqy-" + uuid.uuid4().hex
        command = [str(executable), "--assets", str(assets), "--resource-id", "XJXQY", "--skip-startup-video",
                   "--enable-automation-hooks", "--automation-pipe", session,
                   "--user-data-root", str(output / "user-data"), "--log-file", str(output / "game.log")]
        if args.assist_local_date:
            command += ["--automation-local-date", args.assist_local_date]
        with (output / "stdout.log").open("wb") as stdout, (output / "stderr.log").open("wb") as stderr:
            process = subprocess.Popen(command, cwd=executable.parent, stdout=stdout, stderr=stderr)
        identity = dict(resourceId="XJXQY", session=session, pid=process.pid, command=command,
                        difficulty=args.difficulty, cheatAssisted=args.cheat_assisted, started=time.time(),
                        engineSha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                        resourceProfileSha256=hashlib.sha256((resource / "game_profile.ini").read_bytes()).hexdigest())
        if args.assist_local_date:
            identity["calendarAssistance"] = dict(localDate=args.assist_local_date, processOnly=True,
                authorization="User explicitly permitted isolated process date simulation on 2026-10-04")
        write_json(output / "run.json", identity)
        write_json(output / "branch-catalog.json", catalog_for(resource))
    command = identity["command"]
    if Path(command[command.index("--assets") + 1]).resolve() != assets:
        raise AutomationError("Recorded assets do not match")
    shutil.copyfile(Path(__file__), output / f"route-{time.time_ns()}.py")
    result = dict(status="running", chapter=args.chapter, difficulty=identity["difficulty"],
                  started=time.time(), cheatAssisted=False, fullCoverage=False, fullPlaythrough=False)
    if identity.get("calendarAssistance"):
        result["calendarAssistance"] = identity["calendarAssistance"]
    result_path = output / f"result-{time.time_ns()}.json"
    with Client(identity["session"], timeout=30, transcript=output / "commands.jsonl") as client:
        try:
            chapter_status = "passed"
            if args.load_slot is not None:
                if args.resume and client.observe().get("inEvent"):
                    client.act("SetAutoDialogue", enabled=True)
                    idle(client, output=output, resource=resource)
                load_checkpoint(client, args.load_slot)
                if args.enable_partner_combat and client.observe().get("partnerCombatEnabled") is not True:
                    raise AutomationError("Normal load did not enable the corrected partner-combat switch")
            elif args.resume:
                state = client.observe(VARIABLES)
                if state.get("map") and not state.get("inEvent") and any(row["name"] == "save-load" for row in state.get("ui", [])):
                    client.ui("Cancel")
            if args.assist_invincible or args.assist_level is not None:
                assist(client, output, args.assist_level, args.assist_invincible)
            if args.chapter == "opening":
                opening(client, output, resource, args.difficulty)
            elif args.chapter == "departure":
                departure(client, output, resource)
            elif args.chapter == "early-detours":
                early_detours(client, output, resource)
            elif args.chapter == "hengshan-dialogues":
                hengshan_dialogues(client, output, resource)
            elif args.chapter == "blood-letter":
                blood_letter(client, output, resource)
            elif args.chapter == "wuyi-first-visit":
                wuyi_first_visit(client, output, resource)
            elif args.chapter == "linan-inn":
                linan_inn(client, output, resource)
            elif args.chapter == "hemei-inn-choices":
                hemei_inn_choices(client, output, resource)
            elif args.chapter == "tavern-inquiry-repair":
                tavern_inquiry_repair(client, output, resource)
            elif args.chapter == "linan-tavern":
                chapter_status = linan_tavern(client, output, resource)
            elif args.chapter == "linan-token":
                linan_token(client, output, resource)
            elif args.chapter == "casino-choices":
                casino_choices(client, output, resource)
            elif args.chapter == "festival-choices":
                festival_choices(client, output, resource)
            elif args.chapter == "linan-residents":
                linan_residents(client, output, resource)
            elif args.chapter == "interior-dialogues":
                interior_dialogues(client, output, resource)
            elif args.chapter == "saved-npc-dialogues":
                saved_npc_dialogues(client, output, resource)
            elif args.chapter == "saved-partner-dialogues":
                saved_npc_dialogues(client, output, resource, partners=True)
            elif args.chapter == "partner-dialogue-tour":
                partner_dialogue_tour(client, output, resource, recheck=args.recheck_bound_dialogues)
            elif args.chapter == "save-preview-layout":
                save_preview_layout(client, output, resource)
            elif args.chapter == "remaining-map-traps":
                remaining_map_traps(client, output, resource)
            elif args.chapter == "zhaoqi-matchmaking":
                zhaoqi_matchmaking(client, output, resource)
            elif args.chapter == "native-loss-prompt":
                native_loss_prompt(client, output, resource)
            elif args.chapter == "character-configuration":
                character_configuration(client, output, resource)
            elif args.chapter == "character-combat-configuration":
                character_combat_configuration(client, output, resource)
            elif args.chapter == "character-configuration-return":
                character_configuration_return(client, output, resource)
            elif args.chapter == "character-equipment-limits":
                character_equipment_limits(client, output, resource)
            elif args.chapter == "character-yang-ending":
                character_yang_ending(client, output, resource)
            elif args.chapter == "interior-chests":
                interior_chests(client, output, resource)
            elif args.chapter == "remaining-interior-tour":
                remaining_interior_tour(client, output, resource)
            elif args.chapter == "saved-object-pickups":
                saved_object_pickups(client, output, resource)
            elif args.chapter == "dynamic-drop-search":
                dynamic_drop_search(client, output, resource)
            elif args.chapter == "linan-shop-tiers":
                linan_shop_tiers(client, output, resource)
            elif args.chapter == "chengdu-shop-tiers":
                linan_shop_tiers(client, output, resource, city="chengdu")
            elif args.chapter == "linan-shop-transactions":
                linan_shop_transactions(client, output, resource)
            elif args.chapter == "linan-rare-weapon":
                linan_rare_weapon(client, output, resource)
            elif args.chapter == "coastal-clue":
                coastal_clue(client, output, resource)
            elif args.chapter == "coastal-unpaid-clue":
                coastal_unpaid_clue(client, output, resource)
            elif args.chapter == "jia-and-qiuyu":
                jia_and_qiuyu(client, output, resource)
            elif args.chapter == "prescription-request":
                prescription_request(client, output, resource)
            elif args.chapter == "bixia-first-rescue":
                bixia_first_rescue(client, output, resource)
            elif args.chapter == "hemei-rescue":
                hemei_rescue(client, output, resource)
            elif args.chapter == "hemei-marriage":
                hemei_marriage(client, output, resource)
            elif args.chapter == "prescription-complete":
                prescription_complete(client, output, resource)
            elif args.chapter == "rumeng-and-linxin":
                rumeng_and_linxin(client, output, resource)
            elif args.chapter == "wuyi-second-visit":
                wuyi_second_visit(client, output, resource)
            elif args.chapter == "stone-door-and-liuzhongyuan":
                stone_door_and_liuzhongyuan(client, output, resource)
            elif args.chapter == "linan-blacksmith-aftermath":
                linan_blacksmith_aftermath(client, output, resource)
            elif args.chapter == "zhang-home-and-nangong":
                zhang_home_and_nangong(client, output, resource)
            elif args.chapter == "kill-zhangfeng-choice":
                kill_zhangfeng_choice(client, output, resource)
            elif args.chapter == "furong-and-shitang":
                furong_and_shitang(client, output, resource)
            elif args.chapter == "huashan-inquiry":
                huashan_inquiry(client, output, resource)
            elif args.chapter == "changan-inquiry":
                changan_inquiry(client, output, resource)
            elif args.chapter == "changan-rescue":
                changan_rescue(client, output, resource)
            elif args.chapter == "stone-wall-late":
                stone_wall_late(client, output, resource)
            elif args.chapter == "huashan-hidden-path":
                huashan_hidden_path(client, output, resource)
            elif args.chapter == "huashan-bridge":
                huashan_bridge(client, output, resource)
            elif args.chapter == "changan-poem":
                changan_poem(client, output, resource)
            elif args.chapter == "huashan-confrontation":
                huashan_confrontation(client, output, resource)
            elif args.chapter == "changan-to-guide":
                changan_to_guide(client, output, resource)
            elif args.chapter == "linhai-and-shaolin":
                linhai_and_shaolin(client, output, resource)
            elif args.chapter == "stone-treasure-early":
                stone_treasure_early(client, output, resource)
            elif args.chapter == "shaolin-first-inquiry":
                shaolin_first_inquiry(client, output, resource)
            elif args.chapter == "poem-late-confrontation":
                poem_late_confrontation(client, output, resource)
            elif args.chapter == "shaolin-traitor-and-lessons":
                shaolin_traitor_and_lessons(client, output, resource)
            elif args.chapter == "stone-treasure-late":
                linhai_and_shaolin(client, output, resource, late_treasure=True)
            elif args.chapter == "zhuxian-first-inquiry":
                zhuxian_first_inquiry(client, output, resource)
            elif args.chapter == "treasure-followup-confrontation":
                poem_late_confrontation(client, output, resource, claimed=True)
            elif args.chapter == "doctor-treasure-followup":
                linhai_and_shaolin(client, output, resource, doctor_followup=True)
            elif args.chapter in ("camps-han-first", "camps-yue-first"):
                camps_first_visit(client, output, resource, yue_first=args.chapter == "camps-yue-first")
            elif args.chapter == "zhuxian-overnight":
                zhuxian_overnight(client, output, resource)
            elif args.chapter in ("han-red-jade", "han-no-red-jade"):
                han_red_jade_visit(client, output, resource, has_ruby=args.chapter == "han-red-jade")
            elif args.chapter == "han-no-red-jade-bypass":
                han_no_red_jade_bypass(client, output, resource)
            elif args.chapter == "yue-gold-clothes":
                yue_gold_clothes(client, output, resource)
            elif args.chapter == "jin-infiltration":
                jin_infiltration(client, output, resource)
            elif args.chapter == "yue-frontline-death":
                yue_frontline_death(client, output, resource)
            elif args.chapter in ("changbai-zhu-hai", "changbai-zhu-wife"):
                changbai_zhu_medicine(client, output, resource, wife=args.chapter.endswith("-wife"))
            elif args.chapter == "lake-opened-chests-repeat":
                state = idle(client)
                if state["map"] != "map121_洞庭湖底.map" or state["variables"].get("Event") != "560":
                    raise AutomationError("Opened lake chests require the native collected-treasure slot")
                before = checkpoint(client, output, "994-opened-lake-chests-before")
                for position in ((16, 49), (9, 12), (12, 28), (7, 41), (9, 17)):
                    target = next(row for row in state["targets"] if row["name"] == "宝箱"
                                  and row["position"] == dict(x=position[0], y=position[1]))
                    try:
                        client.interact(target["id"])
                    except AutomationError as error:
                        if "action_rejected" not in str(error):
                            raise
                    else:
                        raise AutomationError("Collected lake chest repeated its reward")
                after = checkpoint(client, output, "995-opened-lake-chests-after")
                if after["inventory"] != before["inventory"] or after["player"]["money"] != before["player"]["money"]:
                    raise AutomationError("Rejected lake chest interaction changed inventory or silver")
                write_json(output / "lake-opened-chests-repeat-proof.json", dict(status="passed", nativeSaveReload=True,
                           nativeFiveOpenedChestsRejected=True, inventoryAndMoneyUnchanged=True, cheatAssisted=True))
            elif args.chapter == "changbai-bandits-first":
                changbai_bandits_first(client, output, resource)
            elif args.chapter == "tianwang-password-and-hall":
                tianwang_password_and_hall(client, output, resource)
            elif args.chapter == "tianwang-garden-and-betrayal":
                tianwang_garden_and_betrayal(client, output, resource)
            elif args.chapter == "tianwang-return-and-letter":
                tianwang_return_and_letter(client, output, resource)
            elif args.chapter == "hukou-unreturned-letter":
                hukou_unreturned_letter(client, output, resource)
            elif args.chapter == "hukou-wang-to-feng-source":
                hukou_wang_to_feng_source(client, output, resource)
            elif args.chapter == "yun-linxin-reunion-source":
                yun_linxin_reunion_source(client, output, resource)
            elif args.chapter in ("chengdu-secret-door", "chengdu-secret-door-direct"):
                chengdu_secret_door(client, output, resource, direct=args.chapter.endswith("-direct"))
            elif args.chapter in ("chengdu-three-bosses", "chengdu-three-bosses-direct"):
                chengdu_three_bosses(client, output, resource, direct=args.chapter.endswith("-direct"))
            elif args.chapter == "rumeng-jianmen-source":
                rumeng_jianmen_source(client, output, resource)
            elif args.chapter == "jianmen-deaths":
                jianmen_deaths(client, output, resource)
            elif args.chapter == "yue-rescue-source":
                yue_rescue_source(client, output, resource)
            elif args.chapter == "yue-rescue":
                yue_rescue(client, output, resource)
            elif args.chapter in ("fang-second-branches", "fang-acceptance"):
                fang_second_branches(client, output, resource, accept_only=args.chapter == "fang-acceptance")
            elif args.chapter in ("taishan-tournament", "taishan-uninterrupted"):
                taishan_tournament(client, output, resource, reuse_forks=args.chapter == "taishan-tournament")
            elif args.chapter == "summit-end5-source":
                summit_end5_source(client, output, resource)
            elif args.chapter == "summit-finish":
                summit_finish(client, output, resource)
            elif args.chapter == "end5-qin-source":
                end5_qin_source(client, output, resource)
            elif args.chapter == "end5-finish":
                end5_finish(client, output, resource)
            elif args.chapter == "fang-refusal-end2":
                fang_refusal_end2(client, output, resource)
            elif args.chapter == "end3-qin-source":
                end3_qin_source(client, output, resource)
            elif args.chapter == "end3-finish":
                end3_finish(client, output, resource)
            elif args.chapter == "fang-first-battle":
                fang_first_battle(client, output, resource)
            elif args.chapter == "yue-timeout":
                yue_timeout(client, output, resource)
            elif args.chapter == "jianmen-timeout":
                jianmen_timeout(client, output, resource)
            elif args.chapter == "chengdu-city-medicine":
                chengdu_city_medicine(client, output, resource)
            elif args.chapter == "chengdu-estate-poison":
                chengdu_estate_poison(client, output, resource)
            elif args.chapter == "chengdu-lv-body-and-estate":
                chengdu_lv_body_and_estate(client, output, resource)
            elif args.chapter == "chengdu-companion-choices":
                chengdu_companion_choices(client, output, resource)
            elif args.chapter == "chengdu-fortune":
                chengdu_fortune(client, output, resource)
            elif args.chapter == "chengdu-room-boundary":
                chengdu_room_boundary(client, output, resource)
            elif args.chapter == "chengdu-north-and-traitor":
                chengdu_north_and_traitor(client, output, resource)
            elif args.chapter == "chengdu-letter-source":
                chengdu_letter_source(client, output, resource)
            elif args.chapter in ("chengdu-first-inquiry", "chengdu-low-money-first-inquiry"):
                chengdu_first_inquiry(client, output, resource, low_money=args.chapter == "chengdu-low-money-first-inquiry")
            elif args.chapter in ("yun-wrist", "yun-wrist-low-money"):
                yun_wrist(client, output, resource, low_money=args.chapter.endswith("-low-money"))
            elif args.chapter == "hukou-wang-property":
                hukou_wang_property(client, output, resource)
            elif args.chapter == "hukou-inquiry-and-entrance":
                hukou_inquiry_and_entrance(client, output, resource)
            elif args.chapter == "changbai-zhu-two-medicines":
                client.act("SetAutoDialogue", enabled=True)
                state = idle(client)
                if state["map"] == "map094_朱家大堂.map":
                    transition(client, resource, "map093_长白山东.map", 1, output, "960-two-medicines-leave-zhu")
                    transition(client, resource, "map088_长白山.map", 1, output, "961-two-medicines-changbai")
                changbai_compass_medicine(client, output, resource)
            elif args.chapter == "changbai-compass-medicine":
                changbai_compass_medicine(client, output, resource)
            elif args.chapter == "tianshan-snow-lotus":
                tianshan_snow_lotus(client, output, resource)
            elif args.chapter == "nangong-showdown-source":
                nangong_showdown_source(client, output, resource)
            elif args.chapter in ("nangong-player-defeat-changbai", "nangong-partner-defeat-tianshan", "nangong-player-defeat-tianshan", "nangong-partner-defeat-changbai"):
                nangong_medicine_departure(client, output, resource,
                    partner_defeat=args.chapter.startswith("nangong-partner-defeat-"),
                    option=int(args.chapter.endswith("-tianshan")))
            elif args.chapter == "yue-tent-background":
                client.act("SetAutoDialogue", enabled=True)
                yue_camp_first_visit(client, output, resource, yue_first=True, save_tent=True)
                client.save_or_load(6)
                write_json(output / "yue-tent-background-proof.json", dict(status="passed", nativeTentEntry=True, nativeSaveLoad=True, nativeWarningCompleted=True, cheatAssisted=True))
            client.act("SetAutoDialogue", enabled=False)
            result.update(status=chapter_status, finalState=checkpoint(client, output, f"{args.chapter}-final"))
            if args.exit:
                normal_exit(client, identity, output)
        except Exception as error:
            result.update(status="failed", error=str(error))
            try:
                client.act("SetAutoDialogue", enabled=False)
                checkpoint(client, output, f"failure-{time.time_ns()}")
            except Exception as capture_error:
                result["captureError"] = str(capture_error)
            raise
        finally:
            result["finished"] = time.time()
            result["elapsedSeconds"] = result["finished"] - result["started"]
            write_json(result_path, result)


if __name__ == "__main__":
    main()
