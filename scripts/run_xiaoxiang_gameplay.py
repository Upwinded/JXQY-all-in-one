"""Play Xiaoxiang through existing player controls and isolated manual saves."""
from __future__ import annotations

import argparse
import configparser
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import uuid

from gameplay_automation import Client, AutomationError, npc_attackable
from run_chenghe_gameplay import save_hashes
from run_yycs_gameplay import inventory, load_checkpoint, reachable_trap, medicine, trap_points
from run_jxqy2_mainline import go as native_go, talk, late_records

RESOURCE_ID = "XIAOXIANGXING_1_022"
RESOURCE_DIRECTORY = "潇湘行"
SAVE_NAMESPACE = RESOURCE_ID.lower()
VARIABLES = ("XuanZhe", "expert", "xz", "XZ", "gotmz", "tmz", "event", "lmkz", "fy")
VARIABLES += ("tmzylsw", "tmztrsw", "hksz", "kslmkzsw", "lmkzfy", "ca", "yangbo")
VARIABLES += ("cacglb", "yxsz", "cajdlb", "chaguanlaoban", "wmszjdsw", "wmszjdswe",
              "wmszzsw", "wmszny", "yssze", "fxsz", "gozmg")
VARIABLES += ("fxszzws", "fxszzsq", "fxszzjsw", "fxszfy", "fxszjd", "fxszyb", "gozd", "cadz")
VARIABLES += ("zdyh", "zdwxj", "zdlq", "yjj", "zdxc", "gz", "goks", "swksdr", "zddz", "zdtj",
              "zdjd", "zdfy", "zdjkd", "zdwyffy", "zdrx", "zdzdjd", "zdjzfy", "zdhh", "trj")
VARIABLES += ("zdjdsw", "lafy", "laytz")
VARIABLES += ("fcsz", "fcszszl", "laly", "bwcly", "dxc", "lazx", "laytzx", "lafyx")
VARIABLES += ("dxcss", "dxcsse", "dxcmr", "dxcsssw", "dxcml", "ylpl")
VARIABLES += ("fcszdrsw", "lacsj", "lawb", "ladxmg", "hyp")
VARIABLES += ("trjdzsw", "trjylpl", "swzmg")
SPELLS = (
    ("001治疗术.ini", "001杀意.ini", "001梦蝶神功.ini", "001春城何处不飞花.ini"),
    ("001江逐月天.ini", "001怒涛劲.ini", "001阳春白雪.ini", "001银烛清韵.ini"),
    ("001江山如画.ini", "001怒涛卷霜.ini", "001夜引风岚.ini", "001燕归.ini"),
    ("001达摩真经.ini", "001浮生长恨.ini", "001寒日无言.ini", "001霹雳雷珠.ini"),
)


def go(client, *coordinates, **options):
    if options.get("combat", True) and "combat_handler" not in options:
        output = Path(client.transcript.name).parent
        identity = json.loads((output / "run.json").read_text(encoding="utf-8"))
        command = identity["command"]
        resource = Path(command[command.index("--assets")+1]) / RESOURCE_DIRECTORY
        options["combat_handler"] = lambda c,t: fight(c,output,resource,t)
    return native_go(client,*coordinates,**options)


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def quantity(state, filename):
    return sum(row["quantity"] for row in state["inventory"] if row["file"] == filename)


def checkpoint(client, output, name):
    state = client.observe(VARIABLES)
    if state.get("resourceId") != RESOURCE_ID or not state.get("outputHealthy"):
        raise AutomationError("Wrong game or missing required trace")
    identity = json.loads((output / "run.json").read_text(encoding="utf-8"))
    state["cheatAssisted"] = identity["cheatAssisted"]
    state["assistanceRecords"] = identity.get("assistanceRecords", [])
    write_json(output / f"{name}.json", state)
    shutil.copyfile(client.snapshot(), output / f"{name}.png")
    return state


def source_candidates(assets):
    catalog = dict(resourceId=RESOURCE_ID, inventoryType="source-candidates", fullCoverage=False,
                   sources=[], choices=[], conditions=[], terminals=[])
    seen = set()
    for root in (assets / RESOURCE_DIRECTORY, assets / "jxqy2", assets / "yycs"):
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


def idle(client, timeout=180, *, allow_title=False):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        state = client.observe(VARIABLES)
        if state.get("choices"):
            raise AutomationError(f"Unmapped choice: {state.get('choiceMessage')}")
        if state.get("scene") == "Title":
            if allow_title:
                return state
            raise AutomationError("Unexpected return to title")
        if state.get("video"):
            client.ui("Cancel")
        elif state.get("worldInput"):
            return state
        time.sleep(0.3)
    raise TimeoutError(f"Story did not settle: {state.get('map')} / {state.get('script')}")


def opening(client, output, difficulty, magic):
    client.wait_until(lambda state: state["scene"] == "Title", description="Xiaoxiang title")
    checkpoint(client, output, "01-title")
    client.activate("new-game")
    for number, (message, options, selection) in enumerate((
            ("请选择游戏的难度：", ["普通", "困难", "专家"], difficulty),
            ("请选择武功的类型：", ["远程", "肉搏", "群杀", "爆发"], magic)), 2):
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            state = client.observe(VARIABLES)
            if state.get("video"):
                client.ui("Cancel")
            elif state.get("choices"):
                if state.get("choiceMessage") != message or [row["text"] for row in state["choices"]] != options:
                    raise AutomationError(f"Unexpected opening choice: {state.get('choiceMessage')}")
                checkpoint(client, output, f"0{number}-choice")
                client.act("Choose", context=state["context"], options=[selection])
                break
            time.sleep(0.2)
        else:
            raise TimeoutError(f"Opening choice missing: {message}")
    state = idle(client)
    known = {row["file"] for row in state["magic"]}
    expected = {"XuanZhe": str(difficulty), "xz": str(magic), "expert": "1" if difficulty == 2 else "0"}
    if (state["map"] != "狂沙镇.map" or state["player"]["level"] != 4
            or state["player"]["levelFile"] != ("level-easy.ini" if difficulty == 0 else "level-hard.ini")
            or known != set(SPELLS[magic]) | {"wugong普通攻击.ini"}
            or any((state["variables"].get(key) or "0") != value for key, value in expected.items())):
        raise AutomationError(f"Opening state mismatch: {state}")
    if not any(row["file"] == "神行太保.ini" and row["quantity"] == 1 for row in state["inventory"]):
        raise AutomationError("Opening travel item missing")
    checkpoint(client, output, "04-opening-complete")
    client.save_or_load(0)
    client.move(80, 185)
    client.save_or_load(0, load=True)
    loaded = idle(client)
    for key in ("map", "player", "variables", "inventory", "magic"):
        if loaded[key] != state[key]:
            raise AutomationError(f"Opening manual save/load mismatch: {key}")
    checkpoint(client, output, "05-opening-slot0-loaded")


def rest_choice(client, output, selection):
    state = idle(client)
    go(client, 100, 186, combat=False)
    state = idle(client)
    target = next(row for row in state["targets"] if row["name"] == "客栈老板")
    client.act("Interact", timeout=185, generation=state["generation"], targetId=target["id"],
               running=False, timeoutMs=180000)
    state = client.wait_until(lambda value: value.get("choices"), description="Xiaoxiang inn rest choice")
    if ([row["text"] for row in state["choices"]] != ["立刻休息。", "四处转转。"]
            or state["choiceMessage"] != "柴嵩：我要不要回客房小憩一下？"):
        raise AutomationError("Unexpected inn choice")
    checkpoint(client, output, f"inn-choice-{selection}")
    client.act("Choose", context=state["context"], options=[selection])
    return idle(client)


def town_gates(client, output, resource, *, gates=True):
    state = idle(client)
    if state["map"] != "狂沙镇.map" or (state["variables"].get("gotmz") or "0") != "0":
        raise AutomationError("Town gates require the normal pre-rest source")
    before_money = state["player"]["money"]
    for trap, script in (((1, "地图切换.txt"), (2, "地图切换1.txt"), (3, "地图切换2.txt")) if gates else ()):
        state = idle(client)
        point = reachable_trap(resource, state["map"], trap, state["player"]["position"])
        go(client, *point, script="狂沙镇/" + script, combat=False)
        state = checkpoint(client, output, f"town-gate-{trap}-refused")
        if state["map"] != "狂沙镇.map" or (state["variables"].get("gotmz") or "0") != "0":
            raise AutomationError("Early gate changed the map or rest prerequisite")
    state = rest_choice(client, output, 1)
    if (state["variables"].get("gotmz") or "0") != "0" or state["player"]["money"] != before_money:
        raise AutomationError("Declining rest changed its prerequisite or charged money")
    checkpoint(client, output, "inn-rest-declined")
    client.save_or_load(1)
    state = rest_choice(client, output, 0)
    if (state["variables"].get("gotmz") != "1" or state["player"]["money"] != before_money
            or state["player"]["life"] != state["player"]["lifeMax"]
            or state["player"]["mana"] != state["player"]["manaMax"]):
        raise AutomationError("Rest failed its normal recovery or unexpectedly charged money")
    checkpoint(client, output, "inn-rest-accepted")
    talk(client, "客栈老板")
    state = idle(client)
    if state["player"]["money"] != before_money or state["variables"].get("gotmz") != "1":
        raise AutomationError("Post-rest inn interaction changed money or prerequisite")
    client.save_or_load(2)
    checkpoint(client, output, "inn-night-normal-slot2")


def transition(client, resource, trap, destination, script, *, avoid_traps=(), combat=False, via_path=False):
    state = idle(client)
    occupied = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]
                if row.get("kind") == "npc"}
    avoid = {point for other in avoid_traps for point in trap_points(resource, state["map"], other)}
    point = reachable_trap(resource, state["map"], trap, state["player"]["position"], occupied, avoid=avoid)
    if via_path:
        path = reachable_trap(resource, state["map"], trap, state["player"]["position"],
                              occupied, avoid=avoid, with_path=True)
        objects = {(row["position"]["x"],row["position"]["y"]) for row in state["targets"] if row.get("kind") == "object"}
        for step in (step for step in path[8:-1:8] if step not in objects):
            go(client, *step, combat=combat, running=bool(state.get("cheatInvincibilityEnabled")))
    return go(client, *point, destination=destination, script=script, combat=combat,
              running=bool(state.get("cheatInvincibilityEnabled")))


def expert_death(client, output, resource, *, expert=True):
    state = idle(client)
    if (state["variables"].get("expert") or "0") != ("1" if expert else "0") or state.get("cheatModeEnabled"):
        raise AutomationError("Natural death requires its matching unassisted difficulty source")
    rest_choice(client, output, 0)
    transition(client, resource, 3, "狂沙镇-铁门寨.map", "狂沙镇/地图切换2.txt")
    before = checkpoint(client, output, "expert-before-natural-death")
    saved = output / "user-data/save" / SAVE_NAMESPACE
    before_hashes = save_hashes(saved / "rpg1")
    target = min((row for row in before["targets"] if row.get("hostile") and npc_attackable(row)),
                 key=lambda row: abs(row["position"]["x"] - before["player"]["position"]["x"]) * 2
                                 + abs(row["position"]["y"] - before["player"]["position"]["y"]))
    client.submit("MoveTo", generation=before["generation"], x=target["position"]["x"],
                  y=target["position"]["y"] + 2, running=False, timeoutMs=180000)
    client.wait_until(lambda value: value.get("scene") == "Title", timeout=240,
                      description="expert natural death and return to title")
    after_hashes = save_hashes(saved / "rpg1")
    if not before_hashes or (bool(after_hashes) if expert else after_hashes != before_hashes):
        raise AutomationError("Natural death did not apply the expected manual-save retention")
    parent = Path(json.loads((output / "run.json").read_text(encoding="utf-8"))["parentRun"])
    clone = json.loads((output / "normal-save-clone.json").read_text(encoding="utf-8"))
    original_unchanged = save_hashes(parent / "user-data/save" / SAVE_NAMESPACE / "rpg1") == clone["fileHashes"]
    if not original_unchanged:
        raise AutomationError("Expert death changed the preserved parent save")
    checkpoint(client, output, "expert-natural-death-title")
    write_json(output / ("expert-death-proof.json" if expert else "normal-death-proof.json"), dict(status="passed", cheatAssisted=False,
               beforeManualSlotHashes=before_hashes, afterManualSlotHashes=after_hashes,
               sourceRun=str(parent), parentSaveUnchanged=True))
    if not expert:
        load_checkpoint(client, 0)
        checkpoint(client, output, "normal-death-original-save-reloaded")


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
    if (not after.get("worldInput") or not after.get("cheatInvincibilityEnabled")
            or after["variables"] != before["variables"]):
        raise AutomationError("Native battle assistance failed or changed observed story variables")
    write_json(output / "native-assistance-proof.json", dict(status="passed", cheatAssisted=True,
               source="native-options-menu", beforePlayer=before["player"], afterPlayer=after["player"],
               beforeInventory=before["inventory"], afterInventory=after["inventory"],
               requestedLevel=level, invincibility=True, plotVariablesUnchanged=True))


def fight(client, output, resource, target, *, skills=(0,), allow_title=False, retry_unavailable=3):
    state = idle(client)
    if skills:
        for attempt in range(3):
            try:
                state = approach_target(client, resource, target)
                break
            except AutomationError as error:
                after = idle(client)
                updated = next((row for row in after["targets"] if row["id"] == target["id"] and npc_attackable(row)),None)
                if (attempt == 2 or after["generation"] != state["generation"] or not updated
                        or updated["position"] == target["position"] and "target moved before" not in str(error)
                        or not any(reason in str(error) for reason in ("blocked_destination","no_progress"))):
                    raise
                write_json(output / f"combat-approach-replan-{time.time_ns()}.json",
                           dict(status="failed",actionError=str(error),targetBefore=target,targetAfter=updated))
                target = updated
    supplies = {}
    for attribute, parameter in (("Life", "lifeItem"), ("Mana", "manaItem")):
        selected = medicine(state, resource, attribute)
        if selected:
            supplies[parameter] = selected["file"]
    print(f"Combat: {state['map']} / {target['name']} / {target['life']}", flush=True)
    try:
        result = client.act("StartCombat", timeout=245, generation=state["generation"], targetId=target["id"],
                            radius=20, kills=1, skills=list(skills), allowMeleeFallback=not skills, timeoutMs=240000, **supplies)
    except AutomationError as error:
        after = idle(client)
        updated = next((row for row in after['targets'] if row['id'] == target['id'] and npc_attackable(row)),None)
        if (str(error) == 'StartCombat: configured_skill_unavailable' and retry_unavailable
                and after['generation'] == state['generation'] and updated):
            write_json(output / f'combat-unavailable-{time.time_ns()}.json',dict(status='failed',
                       actionError=str(error),before=state,after=after,retryRemaining=retry_unavailable))
            time.sleep(0.5)
            return fight(client,output,resource,updated,skills=skills,allow_title=allow_title,
                         retry_unavailable=retry_unavailable-1)
        if (str(error) not in ("StartCombat: invalid_enemy", "StartCombat: stale_target")
                or after["generation"] != state["generation"]
                or any(row["id"] == target["id"] and npc_attackable(row) for row in after["targets"])):
            raise
        write_json(output / f"combat-{time.time_ns()}.json", dict(target=target, after=after,
                   status="target-finished-before-submission", actionError=str(error)))
        return after
    if result.get("kills") != 1:
        raise AutomationError("Native combat did not complete one death")
    after = idle(client,allow_title=allow_title)
    write_json(output / f"combat-{time.time_ns()}.json", dict(target=target, result=result, after=after,
               strategy="native-learned-magic" if skills else "native-melee"))
    return after


def tiemenzhai(client, output, resource, assist_level=None):
    state = idle(client)
    if state["map"] not in ("狂沙镇.map", "狂沙镇-铁门寨.map", "铁门寨.map") or state["variables"].get("gotmz") != "1":
        raise AutomationError("Tiemenzhai requires the normal post-rest source")
    if assist_level:
        assist_battle(client, output, assist_level)
    state = client.observe()
    spring = next(row for row in state["magic"] if row["file"] == SPELLS[0][3])
    client.assign_magic(spring["slot"], 0)
    if state["map"] == "狂沙镇.map":
        state = transition(client, resource, 3, "狂沙镇-铁门寨.map", "狂沙镇/地图切换2.txt")
        client.save_or_load(3)
        checkpoint(client, output, "tiemenzhai-road-normal-slot3")
    if state["map"] != "铁门寨.map":
        transition(client, resource, 2, "铁门寨.map", "狂沙镇-铁门寨/地图切换1.txt", avoid_traps=(1,), combat=True)
        client.save_or_load(4)
        checkpoint(client, output, "tiemenzhai-before-battle-slot4")
    state = idle(client)
    if not any(row.get("hostile") for row in state["targets"]) and state["variables"].get("tmzylsw") != "11":
        point = reachable_trap(resource, state["map"], 4, state["player"]["position"])
        go(client, *point, script="铁门寨/进入山寨大厅.txt", combat=False)
    for phase, variable in (("yelv", "tmzylsw"), ("disciples", "tmztrsw")):
        for _ in range(11):
            state = idle(client)
            if state["variables"].get(variable) == "11":
                break
            targets = [row for row in state["targets"] if row.get("hostile") and npc_attackable(row)]
            if not targets:
                raise AutomationError(f"Tiemenzhai {phase} ended before its death counter")
            position = state["player"]["position"]
            target = min(targets, key=lambda row: abs(row["position"]["x"] - position["x"]) * 2
                                                   + abs(row["position"]["y"] - position["y"]))
            fight(client, output, resource, target)
        state = client.observe(VARIABLES)
        if state["variables"].get(variable) != "11":
            raise AutomationError(f"Tiemenzhai {phase} native death counter mismatch")
        checkpoint(client, output, f"tiemenzhai-{phase}-complete")
    talk(client, "山寨头领")
    state = client.observe(VARIABLES)
    if state["variables"].get("tmz") != "1" or state["variables"].get("hksz") != "1":
        raise AutomationError("Tiemenzhai leader did not open the pursuit")
    client.save_or_load(5)
    checkpoint(client, output, "tiemenzhai-pursuit-slot5")


def tiemenzhai_gate(client, output, resource):
    before = idle(client)
    if before["map"] != "铁门寨.map" or (before["variables"].get("tmz") or "0") != "0":
        raise AutomationError("Tiemenzhai gate requires the pre-completion source")
    point = reachable_trap(resource, before["map"], 1, before["player"]["position"])
    go(client, *point, script="铁门寨/地图切换.txt", combat=False)
    after = checkpoint(client, output, "tiemenzhai-unfinished-departure-refused")
    if (after["map"] != "铁门寨.map" or after["variables"] != before["variables"]
            or after["player"]["position"] != dict(x=4, y=117)):
        raise AutomationError("Unfinished Tiemenzhai gate did not return to its normal entrance")


def longmen(client, output, resource):
    state = idle(client)
    if state["map"] == "铁门寨.map" and state["variables"].get("tmz") == "1":
        transition(client, resource, 1, "狂沙镇-铁门寨.map", "铁门寨/地图切换.txt")
        transition(client, resource, 1, "狂沙镇.map", "狂沙镇-铁门寨/地图切换.txt", combat=True)
        checkpoint(client, output, "pursuit-kuangsha-return")
        transition(client, resource, 2, "狂沙镇-龙门客栈.map", "狂沙镇/地图切换1.txt")
        client.save_or_load(0)
        checkpoint(client, output, "six-assassins-before-battle-slot0")
    elif state["map"] != "狂沙镇-龙门客栈.map" or state["variables"].get("tmz") not in ("1", "2"):
        raise AutomationError("Longmen pursuit requires the completed normal Tiemenzhai source")
    for _ in range(6):
        state = idle(client)
        if state["variables"].get("kslmkzsw") == "6":
            break
        position = state["player"]["position"]
        targets = [row for row in state["targets"] if row["name"] == "杀手"
                   and row.get("hostile") and npc_attackable(row)]
        if not targets:
            raise AutomationError("Assassins disappeared before their native six deaths")
        target = min(targets, key=lambda row: abs(row["position"]["x"] - position["x"]) * 2
                                               + abs(row["position"]["y"] - position["y"]))
        fight(client, output, resource, target)
    state = idle(client)
    if state["variables"].get("kslmkzsw") != "6" or state["variables"].get("tmz") != "2":
        raise AutomationError("Assassin battle did not open the two normal exits")
    client.save_or_load(1)
    checkpoint(client, output, "six-assassins-complete-slot1")
    transition(client, resource, 2, "龙门客栈.map", "狂沙镇-龙门客栈/地图切换1.txt", avoid_traps=(1,))
    client.save_or_load(2)
    checkpoint(client, output, "longmen-arrival-slot2")
    state = idle(client)
    point = reachable_trap(resource, state["map"], 2, state["player"]["position"],
                            avoid=trap_points(resource, state["map"], 1))
    go(client, *point, script="龙门客栈/地图切换1.txt", combat=False)
    if idle(client)["map"] != "龙门客栈.map":
        raise AutomationError("Longmen failed to refuse early departure")
    checkpoint(client, output, "longmen-departure-before-rest-refused")
    talk(client, "老板")
    if client.observe(VARIABLES)["variables"].get("lmkzfy") != "1":
        raise AutomationError("First Longmen rest did not enable the meal encounter")
    checkpoint(client, output, "longmen-first-rest")
    talk(client, "老板")
    state = client.observe(VARIABLES)
    if state["variables"].get("ca") != "1":
        raise AutomationError("Longmen Feiyun meeting did not enable Chang'an")
    client.save_or_load(3)
    checkpoint(client, output, "longmen-feiyun-meeting-slot3")
    transition(client, resource, 2, "长安.map", "龙门客栈/地图切换1.txt", avoid_traps=(1,))
    state = client.observe(VARIABLES)
    if state["variables"].get("yangbo") != "1":
        raise AutomationError("Chang'an arrival did not enable Yang Bo")
    client.save_or_load(4)
    checkpoint(client, output, "changan-arrival-slot4")


def kill_group(client, output, resource, variable, total, names, *, skills=(0,)):
    for _ in range(total):
        state = idle(client)
        if int(state["variables"].get(variable) or 0) == total:
            return state
        position = state["player"]["position"]
        targets = [row for row in state["targets"] if row["name"] in names
                   and row.get("hostile") and npc_attackable(row)]
        if not targets:
            raise AutomationError(f"No enemy before {variable} reaches {total}")
        for target in sorted(targets, key=lambda row: abs(row["position"]["x"] - position["x"]) * 2
                                                    + abs(row["position"]["y"] - position["y"])):
            try:
                fight(client, output, resource, target, skills=skills)
                break
            except AutomationError as error:
                if str(error).startswith("No reachable combat neighbor:"):
                    continue
                after = idle(client)
                if (str(error) != "StartCombat: world_changed"
                        or int(after["variables"].get(variable) or 0) != total):
                    raise
                write_json(output / f"combat-world-change-{time.time_ns()}.json",
                           dict(target=target, after=after, status="native-counter-completed",
                                actionError=str(error), counter=variable, expected=total))
                break
        else:
            raise AutomationError(f"No reachable enemy before {variable} reaches {total}")
    state = idle(client)
    if int(state["variables"].get(variable) or 0) != total:
        raise AutomationError(f"Native death counter {variable} did not reach {total}")
    return state


def walk_to(client, resource, point, *, combat=False, stop_when=None, avoid_traps=()):
    state = idle(client)
    occupied = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]
                if row.get("kind") == "npc" and row.get("life",0)>0
                and (not combat or not row.get("hostile") or not npc_attackable(row))}
    avoid = {position for trap in avoid_traps for position in trap_points(resource,state["map"],trap)}
    path = reachable_trap(resource, state["map"], None, state["player"]["position"],
                          with_path=True, destination=point, avoid=(occupied - {point}) | avoid)
    objects = {(row["position"]["x"],row["position"]["y"]) for row in state["targets"] if row.get("kind") == "object"}
    # Keep objects in the path; do not choose one as an intermediate MoveTo destination.
    for step in [*(step for step in path[8:-1:8] if step not in objects), path[-1]]:
        handler = lambda c,t: fight(c,Path(c.transcript.name).parent,resource,t)
        after = go(client, *step, combat=combat, running=bool(state.get("cheatInvincibilityEnabled")),
                   stop_when=stop_when,combat_handler=handler if combat else None)
        if stop_when and stop_when(after):
            return after


def magic_ground_allows(resource, map_name, point):
    data = (resource / "map" / map_name).read_bytes()
    _, width, height, image_size, _ = struct.unpack_from("<5i", data, 64)
    header, _, _, image_count = struct.unpack_from("<4i", data, 84)
    offset = header + image_count * image_size
    if width <= 0 or height <= 0 or offset + width * height * 10 > len(data):
        raise ValueError(f"Invalid map dimensions: {map_name}")
    x, y = point
    if not (0 <= x < width and 0 <= y < height):
        return False
    obstacle = data[offset + (y * width + x) * 10 + 6]
    return obstacle == 0 or bool(obstacle & 0x40)


def approach_target(client, resource, target, *, attackable=True):
    state = idle(client)
    target = next((row for row in state['targets'] if row['id'] == target['id']),target)
    point = state["player"]["position"]
    x, y = target["position"]["x"], target["position"]["y"]
    def near_target(observed):
        actor = next((row for row in observed["targets"] if row["id"] == target["id"]
                      and (npc_attackable(row) if attackable else row.get("interactive") or row.get("kind") == "object")), None)
        player = observed["player"]["position"]
        if actor is None:
            return True
        if attackable and not magic_ground_allows(resource, observed["map"], (player["x"], player["y"])):
            return False
        if abs(actor["position"]["x"]-player["x"])*2 + abs(actor["position"]["y"]-player["y"]) > (8 if attackable else 3):
            return False
        try:
            path = reachable_trap(resource,observed["map"],None,actor["position"],with_path=True,
                                  destination=(player["x"],player["y"]))
            if attackable and (not actor.get("visibleFromPlayer", True)
                               or not all(magic_ground_allows(resource, observed["map"], tile) for tile in path)):
                return False
            if len(path) <= 2:
                return True
            # A short, straight walkable line lets magic hit a retreating ranged enemy.
            if attackable and actor.get('visibleFromPlayer',True) and len(path) <= 4:
                ox,oy = 2*path[0][0]+path[0][1]%2,path[0][1]
                dx,dy = 2*path[-1][0]+path[-1][1]%2-ox,path[-1][1]-oy
                return all((2*px+py%2-ox)*dy == (py-oy)*dx for px,py in path)
            return False
        except AutomationError:
            return False
    if near_target(state):
        return state
    occupied = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]
                if row.get("kind") == "npc" and row.get("life",0)>0}
    paths = []
    for goal in ((x,y+2),(x+y%2-1,y+1),(x-1,y),(x+y%2-1,y-1),
                 (x,y-2),(x+y%2,y-1),(x+1,y),(x+y%2,y+1)):
        if attackable and not magic_ground_allows(resource, state["map"], goal):
            continue
        try:
            if len(reachable_trap(resource,state["map"],None,target["position"],with_path=True,
                                   destination=goal,avoid=occupied)) > 2:
                continue
            paths.append(reachable_trap(resource,state["map"],None,point,with_path=True,
                                        destination=goal,avoid=occupied))
        except AutomationError:
            continue
    if not paths:
        raise AutomationError(f"No reachable {'combat' if attackable else 'dialogue'} neighbor: {target['name']}")
    walk_to(client,resource,min(paths,key=len)[-1],stop_when=near_target)
    after = idle(client)
    if after["generation"] != state["generation"]:
        raise AutomationError("Unexpected world change while approaching the target")
    if not near_target(after):
        raise AutomationError("no_progress: target moved before reaching its neighbor")
    return after


def changan(client, output, resource, assist_level=None):
    state = idle(client)
    if state["map"] != "长安.map" or state["variables"].get("yangbo") != "1":
        raise AutomationError("Chang'an tea investigation requires the normal arrival source")
    if assist_level:
        assist_battle(client, output, assist_level)
    client.save_or_load(0)
    checkpoint(client, output, "changan-before-tea-slot0")
    walk_to(client, resource, (117, 252))
    talk(client, "茶馆老板")
    if idle(client)["variables"].get("cacglb") != "1":
        raise AutomationError("Tea owner did not admit the normal jade-token bearer")
    walk_to(client, resource, (115, 247))
    talk(client, "杨博")
    state = checkpoint(client, output, "changan-yangbo-investigation")
    if state["variables"].get("yxsz") != "1" or state["variables"].get("yangbo") != "0":
        raise AutomationError("Yang Bo did not open Yixin investigation")
    client.save_or_load(1)
    transition(client, resource, 7, "别离村-翠烟门.map", "长安/trap-7.txt")
    transition(client, resource, 1, "无名山庄.map", "别离村-翠烟门/trap1至别离村.txt", avoid_traps=(2,), combat=True)
    client.save_or_load(2)
    checkpoint(client, output, "yixin-first-entry-slot2")
    kill_group(client, output, resource, "wmszjdsw", 17, ("家丁",))
    state = checkpoint(client, output, "yixin-first-investigation-complete")
    if state["map"] != "别离村-翠烟门.map" or state["variables"].get("cajdlb") != "1":
        raise AutomationError("First Yixin battle did not return naturally to the east road")
    transition(client, resource, 2, "长安.map", "别离村-翠烟门/trap2至翠烟门.txt", avoid_traps=(1,), combat=True)
    walk_to(client, resource, (103, 246))
    client.save_or_load(3)
    checkpoint(client, output, "changan-before-feiyun-meal-slot3")
    talk(client, "酒店老板")
    talk(client, "南宫飞云")
    talk(client, "酒店老板")
    state = checkpoint(client, output, "changan-feiyun-departure")
    if state["variables"].get("yssze") != "1" or state["variables"].get("cajdlb") != "3":
        raise AutomationError("Feiyun meal did not open the second Yixin visit")
    client.save_or_load(4)
    checkpoint(client, output, "yixin-second-visit-source-slot4")


def yixin(client, output, resource):
    state = idle(client)
    if state["map"] not in ("长安.map", "无名山庄.map", "别离村-翠烟门.map") or state["variables"].get("yssze") not in ("1", "2"):
        raise AutomationError("Second Yixin investigation requires the normal Feiyun departure source")
    if state["variables"].get("yssze") == "1":
        if state["map"] == "长安.map":
            transition(client, resource, 7, "别离村-翠烟门.map", "长安/trap-7.txt")
        if idle(client)["map"] == "别离村-翠烟门.map":
            transition(client, resource, 1, "无名山庄.map", "别离村-翠烟门/一心山庄.txt", avoid_traps=(2,), combat=True)
            client.save_or_load(5)
            checkpoint(client, output, "yixin-second-entry-slot5")
        kill_group(client, output, resource, "wmszjdswe", 10, ("家丁", "守门家丁甲", "守门家丁乙"))
        checkpoint(client, output, "yixin-second-gate-guards-complete")
        current = idle(client)
        if not any(row["name"] in ("庄主", "魔天卫") and row.get("hostile") and npc_attackable(row)
                   for row in current["targets"]) and not int(current["variables"].get("wmszzsw") or 0):
            walk_to(client, resource, (58, 81), combat=True)
            talk(client, "庄主")
        kill_group(client, output, resource, "wmszzsw", 5, ("庄主", "魔天卫"))
        state = checkpoint(client, output, "yixin-leader-battle-complete")
        if state["variables"].get("wmszny") != "1":
            raise AutomationError("Yixin leader battle did not open the inner court")
        transition(client, resource, 3, "无名山庄.map", "无名山庄/内院.txt", avoid_traps=(2,), combat=True)
        state = checkpoint(client, output, "yixin-inner-court-investigation")
        if state["variables"].get("yssze") != "2" or state["variables"].get("cajdlb") != "4":
            raise AutomationError("Inner court did not complete the natural inquiry")
    if idle(client)["map"] == "无名山庄.map":
        transition(client, resource, 2, "别离村-翠烟门.map", "无名山庄/去长安.txt", avoid_traps=(1,), combat=True, via_path=True)
    if idle(client)["map"] == "别离村-翠烟门.map":
        transition(client, resource, 2, "长安.map", "别离村-翠烟门/trap2至翠烟门.txt", avoid_traps=(1,), combat=True)
    walk_to(client, resource, (103, 246))
    talk(client, "酒店老板")
    state = checkpoint(client, output, "changan-wind-snow-task")
    if state["variables"].get("fxsz") != "1" or state["variables"].get("cajdlb") != "5":
        raise AutomationError("Hotel owner did not open Fengxue")
    client.save_or_load(6)
    checkpoint(client, output, "fengxue-task-source-slot6")


def fengxue(client, output, resource, assist_level=None):
    state = idle(client)
    if state["map"] not in ("长安.map", "长安西郊.map", "风雪山庄.map") or state["variables"].get("fxsz") != "1":
        raise AutomationError("Fengxue requires the normal hotel task source")
    if assist_level:
        assist_battle(client, output, assist_level)
    if state["map"] == "长安.map":
        transition(client, resource, 3, "长安西郊.map", "长安/trap-3.txt", via_path=True)
        client.save_or_load(0)
        checkpoint(client, output, "fengxue-west-road-slot0")
    if idle(client)["map"] == "长安西郊.map":
        transition(client, resource, 2, "风雪山庄.map", "长安西郊/地图切换1.txt", avoid_traps=(1,), combat=True, via_path=True)
        client.save_or_load(1)
        checkpoint(client, output, "fengxue-arrival-slot1")
    state = idle(client)
    if (state["variables"].get("fxszzws") or "0") == "0":
        transition(client, resource, 3, "风雪山庄.map", "风雪山庄/地图陷阱3.txt", avoid_traps=(1,4,5), via_path=True)
        state = checkpoint(client, output, "fengxue-gate-overheard")
        if state["player"]["position"] != dict(x=74,y=117):
            raise AutomationError("Fengxue gate did not move the player to the normal side entrance")
        client.save_or_load(2)
        checkpoint(client, output, "fengxue-before-wushuang-slot2")
        transition(client, resource, 4, "风雪山庄.map", "风雪山庄/地图陷阱4.txt", avoid_traps=(1,5), via_path=True)
        kill_group(client, output, resource, "fxszzws", 1, ("赵无双",))
        state = checkpoint(client, output, "fengxue-wushuang-defeated")
        if state["player"]["position"] != dict(x=22,y=97):
            raise AutomationError("Wushuang sparring did not finish the normal Feiyun scene")
    if (state["variables"].get("fxszzsq") or "0") != "0":
        raise AutomationError("Fengxue father branch has already completed")
    client.save_or_load(3)
    checkpoint(client, output, "fengxue-father-branch-source-slot3")


def fengxue_father(client, output, resource, *, victory, assist_level=None):
    before = idle(client)
    if (before["map"] != "风雪山庄.map" or before["variables"].get("fxszzws") != "1"
            or int(before["variables"].get("fxszzsq") or 0)):
        raise AutomationError("Father sparring requires the normal Wushuang completion source")
    if assist_level:
        assist_battle(client, output, assist_level)
    if not victory and client.observe().get("cheatInvincibilityEnabled"):
        raise AutomationError("Natural father defeat requires invincibility disabled")
    event = int(before["variables"].get("event") or 0)
    client.save_or_load(0)
    checkpoint(client, output, "father-sparring-before-slot0")
    talk(client, "赵升权")
    started = checkpoint(client, output, "father-sparring-started")
    if started["variables"].get("fxszzjsw") != "1":
        raise AutomationError("Father did not start the normal sparring death branch")
    if victory:
        state = client.observe()
        spring = next(row for row in state["magic"] if row["file"] == SPELLS[0][3])
        client.assign_magic(spring["slot"], 0)
        kill_group(client, output, resource, "fxszzsq", 1, ("赵升权",), skills=(0,))
        samples = []
    else:
        samples, deadline = [], time.monotonic()+300
        while time.monotonic()<deadline:
            state = client.observe(VARIABLES)
            samples.append(dict(frame=state["frame"],life=state["player"]["life"],action=state["player"]["action"],
                                inEvent=state["inEvent"],variables=state["variables"]))
            if state["scene"] == "Title":
                raise AutomationError("Father defeat incorrectly returned to title")
            if state["variables"].get("fxszzsq") == "1" and state.get("worldInput"):
                break
            time.sleep(0.05)
        else:
            raise TimeoutError("Father natural defeat did not complete")
        write_json(output/"father-natural-defeat-samples.json",samples)
        if min(row["life"] for row in samples)>0:
            raise AutomationError("Father defeat completed without an observed zero-life sample")
    after = checkpoint(client, output, "father-victory" if victory else "father-natural-defeat")
    if (after["variables"].get("fxszzsq") != "1" or int(after["variables"].get("event") or 0) != event+int(victory)
            or after["player"]["life"]<=0):
        raise AutomationError("Father branch reward or recovery did not match the actual outcome")
    client.save_or_load(1)
    checkpoint(client,output,"father-outcome-slot1")
    talk(client,"新赵无双")
    state = checkpoint(client,output,"fengxue-zhongdu-task")
    if state["variables"].get("gozd") != "1" or state["variables"].get("cadz") != "1":
        raise AutomationError("Wushuang did not open the normal Zhongdu task")
    client.save_or_load(2)
    checkpoint(client,output,"zhongdu-task-source-slot2")


def fengxue_gate(client, output, resource):
    before = idle(client)
    if before["map"] != "风雪山庄.map" or int(before["variables"].get("fxszzws") or 0):
        raise AutomationError("Fengxue gate requires the normal before-Wushuang source")
    transition(client, resource, 5, "风雪山庄.map", "风雪山庄/地图陷阱5.txt", avoid_traps=(1,4), via_path=True)
    after = checkpoint(client,output,"fengxue-front-court-refused")
    if after["variables"] != before["variables"]:
        raise AutomationError("Fengxue refusal changed the observed task state")


def zhongdu_arrival(client, output, resource, assist_level=None):
    state = idle(client)
    if state["variables"].get("gozd") != "1" or state["map"] != "风雪山庄.map":
        raise AutomationError("Zhongdu departure requires the normal completed Fengxue source")
    if assist_level:
        assist_battle(client,output,assist_level)
    transition(client, resource, 1, "长安西郊.map", "风雪山庄/地图陷阱1.txt", via_path=True)
    transition(client, resource, 1, "长安.map", "长安西郊/地图切换.txt", avoid_traps=(2,), via_path=True, combat=True)
    client.save_or_load(3)
    checkpoint(client,output,"zhongdu-departure-changan-slot3")
    transition(client, resource, 7, "别离村-翠烟门.map", "长安/trap-7.txt", via_path=True)
    transition(client, resource, 1, "无名山庄.map", "别离村-翠烟门/一心山庄.txt", avoid_traps=(2,), via_path=True, combat=True)
    client.save_or_load(4)
    checkpoint(client,output,"zhongdu-departure-yixin-slot4")
    transition(client, resource, 1, "中都.map", "无名山庄/去中都.txt", avoid_traps=(2,), via_path=True)
    state = checkpoint(client,output,"zhongdu-arrival")
    if state["variables"].get("gozd") != "2":
        raise AutomationError("Zhongdu arrival did not complete the normal entrance scene")
    client.save_or_load(5)
    checkpoint(client,output,"zhongdu-arrival-source-slot5")


def zhongdu_letter(client, output, resource):
    before = idle(client)
    if before["map"] != "中都.map" or int(before["variables"].get("zdyh") or 0):
        raise AutomationError("Letter task requires the normal unstarted Zhongdu source")
    client.save_or_load(0)
    checkpoint(client,output,"letter-before-task-slot0")
    if before["player"]["position"] == dict(x=25,y=317):
        transition(client,resource,1,"中都.map","中都/地图陷阱1.txt",via_path=True)
        checkpoint(client,output,"zhongdu-changge-contact-signal")
    walk_to(client,resource,(53,308))
    talk(client,"丫鬟",position=(54,306))
    if idle(client)["variables"].get("zdyh") != "1":
        raise AutomationError("Wang maid did not introduce the marriage task")
    walk_to(client,resource,(51,303))
    talk(client,"王小姐")
    talk(client,"王小姐")
    if idle(client)["variables"].get("zdyh") != "2":
        raise AutomationError("Miss Wang did not request the letter")
    talk(client,"丫鬟",position=(54,306))
    letter = checkpoint(client,output,"letter-received")
    if (quantity(letter,"书信.ini") != quantity(before,"书信.ini")+1
            or letter["variables"].get("zdlq") != "1"):
        raise AutomationError("Maid did not award exactly one letter")
    client.save_or_load(1)
    checkpoint(client,output,"letter-delivery-source-slot1")
    walk_to(client,resource,(73,239))
    talk(client,"柳青")
    after = checkpoint(client,output,"letter-delivered-yijinjing-reward")
    if (after["variables"].get("yjj") != "1" or after["variables"].get("zdlq") != "2"
            or quantity(after,"书信.ini") != quantity(before,"书信.ini")
            or quantity(after,"易筋经.ini") != quantity(before,"易筋经.ini")+1
            or after["variables"].get("event") != before["variables"].get("event")):
        raise AutomationError("Letter delivery item exchange or task state mismatch")
    talk(client,"柳青")
    repeated = checkpoint(client,output,"letter-repeated-delivery-no-duplicate-reward")
    if repeated["inventory"] != after["inventory"] or repeated["variables"] != after["variables"]:
        raise AutomationError("Completed letter task repeated its reward")
    client.save_or_load(2)
    checkpoint(client,output,"letter-complete-source-slot2")


def zhongdu_xuanci(client, output, resource, assist_level=None):
    before = idle(client)
    underground = before["map"] == "中都地下迷宫.map" and before["variables"].get("zdxc") == "1"
    if underground:
        before = json.loads((output / "xuanci-before-task-slot0.json").read_text(encoding="utf-8"))
    elif before["map"] != "中都.map" or int(before["variables"].get("zdxc") or 0):
        raise AutomationError("Xuanci requires the normal unstarted Zhongdu source")
    if assist_level:
        assist_battle(client,output,assist_level)
    event = int(before["variables"].get("event") or 0)
    has_book = before["variables"].get("yjj") == "1"
    if not underground:
        client.save_or_load(0)
        checkpoint(client,output,"xuanci-before-task-slot0")
        if before["player"]["position"] == dict(x=25,y=317):
            transition(client,resource,1,"中都.map","中都/地图陷阱1.txt",via_path=True)
        transition(client,resource,16,"中都.map","中都/欧阳家.txt",via_path=True)
        checkpoint(client,output,"xuanci-ouyang-house-clue")
        walk_to(client,resource,(125,140))
        talk(client,"玄慈")
    started = checkpoint(client,output,"xuanci-underground-battle-start")
    if (started["map"] != "中都地下迷宫.map" or started["variables"].get("zdxc") != "1"
            or int(started["variables"].get("event") or 0) != event+1
            or not any(row["file"] == "001金刚不坏神功.ini" for row in started["magic"])):
        raise AutomationError("Xuanci did not teach the ending prerequisite through dialogue")
    if not underground:
        client.save_or_load(1)
        checkpoint(client,output,"xuanci-before-battle-slot1")
    spring = next(row for row in started["magic"] if row["file"] == SPELLS[0][3])
    client.assign_magic(spring["slot"], 0)
    if not underground:
        state = idle(client)
        targets = [row for row in state["targets"] if row.get("hostile") and npc_attackable(row) and row["name"] == "和尚"]
        if targets:
            point = state["player"]["position"]
            target = min(targets,key=lambda row:abs(row["position"]["x"]-point["x"])*2+abs(row["position"]["y"]-point["y"]))
            fight(client,output,resource,target)
    checkpoint(client,output,"xuanci-disciples-encountered")
    state = idle(client)
    target = next(row for row in state["targets"] if row["name"] == "玄慈" and row.get("interactive"))
    walk_to(client,resource,(target["position"]["x"]+1,target["position"]["y"]),combat=True)
    talk(client,"玄慈")
    state = idle(client)
    boss = next(row for row in state["targets"] if row["name"] == "欧阳桐" and row.get("hostile") and npc_attackable(row))
    fight(client,output,resource,boss)
    after = checkpoint(client,output,"xuanci-ouyang-defeated-book-return" if has_book else "xuanci-ouyang-defeated-without-book")
    if any(row["name"] == "玄慈" for row in after["targets"]):
        raise AutomationError("Xuanci did not leave after the normal Ouyang conclusion")
    if (quantity(after,"易筋经.ini") != quantity(before,"易筋经.ini")-int(has_book)
            or quantity(after,"如何偏爱来潇湘.ini") != quantity(before,"如何偏爱来潇湘.ini")+int(has_book)):
        raise AutomationError("Xuanci book exchange and optional reward mismatch")
    client.save_or_load(2)
    checkpoint(client,output,"xuanci-complete-source-slot2")
    transition(client,resource,1,"中都.map","中都地下迷宫/trap1to中都.txt",via_path=True)
    client.save_or_load(3)
    checkpoint(client,output,"xuanci-zhongdu-return-source-slot3")


def zhongdu_mine(client, output, resource, assist_level=None):
    state = idle(client)
    mining = state["map"] == "矿山.map" and state["variables"].get("gz") == "2"
    returning = state["map"] in ("矿山.map","中都-矿山.map","中都.map") and state["variables"].get("gz") in ("3","4")
    before = state
    if mining or returning:
        before = json.loads((output / "mine-before-departure-slot0.json").read_text(encoding="utf-8"))
    elif before["map"] != "中都.map" or int(before["variables"].get("gz") or 0):
        raise AutomationError("Mining task requires the normal unstarted Zhongdu source")
    if assist_level:
        assist_battle(client,output,assist_level)
    if not mining and not returning:
        walk_to(client,resource,(74,152))
        talk(client,"玉徽堂弟子")
        walk_to(client,resource,(72,135))
        talk(client,"耿芝")
        talk(client,"耿芝")
        started = checkpoint(client,output,"mine-task-received")
        if started["variables"].get("gz") != "2" or started["variables"].get("goks") != "1":
            raise AutomationError("Geng Zhi did not start the mining rescue task")
        client.save_or_load(0)
        checkpoint(client,output,"mine-before-departure-slot0")
        transition(client,resource,10,"中都-矿山.map","中都/地图陷阱10.txt",via_path=True)
        transition(client,resource,2,"矿山.map","中都-矿山/地图陷阱2.txt",avoid_traps=(1,),via_path=True,combat=True)
        client.save_or_load(1)
        checkpoint(client,output,"mine-before-battle-source-slot1")
    if not returning:
        kill_group(client,output,resource,"swksdr",25,("金国矿山兵",))
    rescued = checkpoint(client,output,"mine-rescue-complete")
    if (rescued["variables"].get("goks") != "2" or rescued["variables"].get("gz") not in ("3","4")
            or quantity(rescued,"奇矿石.ini") != quantity(before,"奇矿石.ini")+1):
        raise AutomationError("Mining deaths did not finish the rescue and ore reward")
    if not returning:
        client.save_or_load(2)
        checkpoint(client,output,"mine-rescue-source-slot2")
    if rescued["map"] == "矿山.map":
        transition(client,resource,1,"中都-矿山.map","矿山/地图陷阱1.txt",avoid_traps=(2,),via_path=True)
    if idle(client)["map"] == "中都-矿山.map":
        transition(client,resource,1,"中都.map","中都-矿山/地图陷阱1.txt",avoid_traps=(2,),via_path=True,combat=True)
    if idle(client)["variables"].get("gz") == "3":
        walk_to(client,resource,(72,135))
        talk(client,"耿芝")
    if idle(client)["variables"].get("gz") != "4":
        raise AutomationError("Geng Zhi did not accept the completed rescue")
    state = idle(client)
    smith = next(row for row in state["targets"] if row["name"] == "铁匠" and row.get("interactive"))
    walk_to(client,resource,(smith["position"]["x"]+1,smith["position"]["y"]))
    talk(client,"铁匠")
    after = checkpoint(client,output,"mine-ore-investigation-complete")
    if (after["variables"].get("zdtj") != "1" or quantity(after,"奇矿石.ini") != quantity(rescued,"奇矿石.ini")
            or after["variables"].get("event") != before["variables"].get("event")):
        raise AutomationError("Smith did not preserve the ore and open its later task")
    client.save_or_load(3)
    checkpoint(client,output,"mine-zhongdu-complete-source-slot3")


def zhongdu_rent(client, output, resource):
    before = idle(client)
    if before["map"] != "中都.map" or int(before["variables"].get("zdjd") or 0):
        raise AutomationError("Zhongdu rental requires its normal unstarted source")
    avoid = () if before["variables"].get("zdxc") == "1" else (16,)
    client.save_or_load(0)
    checkpoint(client,output,"rent-before-main-slot0")
    if before["player"]["position"] == dict(x=25,y=317):
        transition(client,resource,1,"中都.map","中都/地图陷阱1.txt",via_path=True)
    walk_to(client,resource,(139,264),avoid_traps=avoid)
    talk(client,"家丁甲",position=(140,264))
    introduced = checkpoint(client,output,"rent-family-clue")
    if introduced["variables"].get("zdjd") != "1":
        raise AutomationError("Family servant did not introduce the rental task")
    talk(client,"家丁甲",position=(140,264))
    repeated = checkpoint(client,output,"rent-family-clue-repeated")
    if repeated["variables"] != introduced["variables"]:
        raise AutomationError("Repeated family clue changed task state")
    walk_to(client,resource,(82,21),avoid_traps=avoid)
    client.save_or_load(1)
    room_before = checkpoint(client,output,"rent-before-room-slot1")
    talk(client,"老人")
    room_after = checkpoint(client,output,"rent-room-and-wine-clue")
    if (room_after["variables"].get("zdfy") != "1" or room_after["variables"].get("zdjkd") != "1"
            or any(row["name"] == "老人" for row in room_after["targets"])):
        raise AutomationError("Rental did not complete the room and wine scene")
    write_json(output / "rent-money-observation.json",dict(before=room_before["player"]["money"],
               after=room_after["player"]["money"],dialogueAmount=50,source="native-rental-dialogue"))
    client.save_or_load(2)
    checkpoint(client,output,"rent-room-complete-slot2")
    walk_to(client,resource,(47,98),avoid_traps=avoid)
    talk(client,"飞云")
    checkpoint(client,output,"rent-feiyun-first-meeting")
    zhongdu_feiyun(client,output,resource)


def zhongdu_feiyun(client, output, resource):
    before = idle(client)
    street = any(row["name"] == "王二" and row.get("interactive") for row in before["targets"])
    inn = any(row["name"] == "客栈老板" and row.get("interactive") for row in before["targets"])
    if (before["map"] != "中都.map" or before["variables"].get("zdjd") != "1"
            or not (street or inn)):
        raise AutomationError("Feiyun continuation requires the normal street-clue or inn-rest source")
    avoid = () if before["variables"].get("zdxc") == "1" else (16,)
    if street:
        walk_to(client,resource,(35,128),avoid_traps=avoid)
        talk(client,"王二")
        checkpoint(client,output,"rent-street-clue")
        talk(client,"飞云")
        checkpoint(client,output,"rent-feiyun-identity")
    walk_to(client,resource,(33,114),avoid_traps=avoid)
    talk(client,"客栈老板")
    night = checkpoint(client,output,"rent-night-feiyun-missing")
    if night["variables"].get("zdjd") != "2" or night["variables"].get("zdfy") != "0":
        raise AutomationError("Inn rest did not open the Feiyun search")
    walk_to(client,resource,(139,264),avoid_traps=(*avoid,8))
    talk(client,"家丁甲",position=(140,264))
    if idle(client)["variables"].get("zdjd") != "3":
        raise AutomationError("Family servant did not enable the garden entrance")
    transition(client,resource,8,"中都.map","中都/院墙.txt",avoid_traps=avoid,via_path=True)
    ready = checkpoint(client,output,"rent-garden-feiyun-elopement-ready")
    if (ready["variables"].get("zdfy") != "1" or ready["variables"].get("zdrx") != "1"
            or ready["variables"].get("event") != before["variables"].get("event")):
        raise AutomationError("Garden scene did not open elopement or changed ending points")
    client.save_or_load(4)
    checkpoint(client,output,"rent-elopement-source-slot4")


def zhongdu_elopement(client, output, resource, assist_level=None):
    before = idle(client)
    if before["map"] != "中都.map" or before["variables"].get("zdjd") != "3":
        raise AutomationError("Elopement requires the normal garden-complete source")
    if assist_level:
        assist_battle(client,output,assist_level)
    if before["variables"].get("zdzdjd") != "1":
        talk(client,"飞云")
        started = checkpoint(client,output,"elopement-family-battle-start")
        if started["variables"].get("zdzdjd") != "1":
            raise AutomationError("Feiyun did not start the family battle")
        client.save_or_load(5)
        checkpoint(client,output,"elopement-family-battle-source-slot5")
    spring = next(row for row in idle(client)["magic"] if row["file"] == SPELLS[0][3])
    client.assign_magic(spring["slot"],0)
    kill_group(client,output,resource,"zdjdsw",8,("家丁",))
    after = checkpoint(client,output,"elopement-linan-arrival")
    if (after["map"] != "临安城.map" or after["variables"].get("laytz") != "0"
            or after["variables"].get("event") != before["variables"].get("event")
            or quantity(after,"奇矿石.ini") != quantity(before,"奇矿石.ini")):
        raise AutomationError("Family deaths did not complete the normal Linan arrival")
    client.save_or_load(6)
    checkpoint(client,output,"linan-arrival-source-slot6")


def linan_gate(client, output, resource):
    before = idle(client)
    if before["map"] != "临安城.map" or int(before["variables"].get("fcsz") or 0):
        raise AutomationError("Linan gate requires the normal before-visit source")
    transition(client,resource,3,"临安城.map","临安城/临安-凤池山庄.txt",avoid_traps=(2,8),via_path=True)
    after = checkpoint(client,output,"linan-fengchi-departure-refused")
    if after["variables"] != before["variables"]:
        raise AutomationError("Linan departure refusal changed task variables")


def linan_visit(client, output, resource):
    before = idle(client)
    if before["map"] != "临安城.map" or int(before["variables"].get("fcsz") or 0):
        raise AutomationError("Linan visit requires its normal unstarted source")
    client.save_or_load(0)
    checkpoint(client,output,"linan-before-ruoxue-slot0")
    walk_to(client,resource,(145,83),avoid_traps=(3,8))
    talk(client,"若雪")
    ruoxue = checkpoint(client,output,"linan-feiyun-arrived")
    if ruoxue["variables"].get("lafy") != "1":
        raise AutomationError("Ruoxue did not open Feiyun arrival")
    walk_to(client,resource,(163,116),avoid_traps=(3,8))
    talk(client,"飞云")
    ready = checkpoint(client,output,"linan-fengchi-visit-task")
    if ready["variables"].get("fcsz") != "1":
        raise AutomationError("Feiyun did not open the first Fengchi visit")
    client.save_or_load(1)
    checkpoint(client,output,"linan-fengchi-departure-slot1")
    transition(client,resource,3,"临安-凤池山庄.map","临安城/临安-凤池山庄.txt",avoid_traps=(2,8),via_path=True)
    transition(client,resource,2,"凤池山庄.map","临安-凤池山庄/trap2至凤池.txt",avoid_traps=(1,),via_path=True,combat=True)
    client.save_or_load(2)
    checkpoint(client,output,"fengchi-first-entrance-slot2")
    fengchi_visit(client,output,resource)


def fengchi_visit(client, output, resource):
    before = idle(client)
    if before["map"] != "凤池山庄.map" or before["variables"].get("fcsz") != "1":
        raise AutomationError("Fengchi first visit requires the normal entrance source")
    transition(client,resource,2,"凤池山庄.map","凤池山庄/前门.txt",avoid_traps=(1,),via_path=True)
    refused = checkpoint(client,output,"fengchi-front-door-refused")
    if refused["variables"] != before["variables"]:
        raise AutomationError("Fengchi front door changed the first-visit variables")
    walk_to(client,resource,(51,234))
    talk(client,"杨长老")
    checkpoint(client,output,"fengchi-elder-opened-door")
    client.save_or_load(4)
    checkpoint(client,output,"fengchi-before-hall-slot4")
    walk_to(client,resource,(65,226))
    talk(client,"邵骑风")
    checkpoint(client,output,"fengchi-shao-welcome")
    walk_to(client,resource,(79,189))
    talk(client,"独孤剑")
    dgu = checkpoint(client,output,"fengchi-dugu-identity-tested")
    if dgu["variables"].get("fcszszl") != "1":
        raise AutomationError("Dugu meeting did not open Shi Zhongliang farewell")
    walk_to(client,resource,(59,232))
    talk(client,"史忠良")
    after = checkpoint(client,output,"fengchi-first-visit-linan-return")
    if (after["map"] != "临安城.map" or after["variables"].get("fcsz") != "1"
            or after["variables"].get("event") != before["variables"].get("event")
            or quantity(after,"奇矿石.ini") != quantity(before,"奇矿石.ini")):
        raise AutomationError("First Fengchi visit did not preserve the Linan return prerequisites")
    client.save_or_load(5)
    checkpoint(client,output,"fengchi-first-return-source-slot5")


def fengchi_reunion(client, output, resource):
    before = idle(client)
    reunited = before["map"] == "凤池山庄.map" and before["variables"].get("fcsz") == "2"
    if reunited:
        before = json.loads((output / "fengchi-first-return-source-slot5.json").read_text(encoding="utf-8"))
    elif (before["map"] != "临安城.map" or before["variables"].get("fcsz") != "1"
            or before["variables"].get("fcszszl") != "1"):
        raise AutomationError("Feiyun reunion requires the normal first-visit return source")
    if not reunited:
        walk_to(client,resource,(163,116),avoid_traps=(2,3,8))
        talk(client,"飞云")
        ready = checkpoint(client,output,"fengchi-feiyun-reunion-road")
        if ready["map"] != "临安-凤池山庄.map" or ready["variables"].get("fcsz") != "2":
            raise AutomationError("Feiyun did not leave for the Dugu reunion")
        client.save_or_load(1)
        checkpoint(client,output,"fengchi-feiyun-reunion-road-slot1")
        transition(client,resource,2,"凤池山庄.map","临安-凤池山庄/trap2至凤池.txt",avoid_traps=(1,),via_path=True,combat=True)
        client.save_or_load(2)
        checkpoint(client,output,"fengchi-before-dugu-reunion-slot2")
    if any(row["name"] == "下人" and row.get("interactive") for row in idle(client)["targets"]):
        walk_to(client,resource,(65,226))
        talk(client,"下人")
        checkpoint(client,output,"fengchi-reunion-servant-opened-door")
    walk_to(client,resource,(80,191))
    talk(client,"独孤剑")
    after = checkpoint(client,output,"fengchi-feiyun-stayed-linan-return")
    if (after["map"] != "临安城.map" or after["variables"].get("event") != before["variables"].get("event")
            or quantity(after,"奇矿石.ini") != quantity(before,"奇矿石.ini")):
        raise AutomationError("Dugu reunion changed the normal Linan return prerequisites")
    if any(row["name"] == "飞云" for row in after["targets"]):
        raise AutomationError("Feiyun stayed at Fengchi but his old Linan actor remains visible")
    client.save_or_load(4)
    checkpoint(client,output,"fengchi-reunion-linan-source-slot4")


def linan_messages(client, output, resource):
    before = idle(client)
    if (before["map"] != "临安城.map" or before["variables"].get("fcsz") != "2"
            or int(before["variables"].get("dxc") or 0)):
        raise AutomationError("Linan messages require the normal after-reunion source")
    client.save_or_load(0)
    checkpoint(client,output,"linan-before-messages-slot0")
    walk_to(client,resource,(145,83),avoid_traps=(2,3,8))
    talk(client,"若雪")
    checkpoint(client,output,"linan-ruoxue-informed-feiyun-stay")
    walk_to(client,resource,(147,109),avoid_traps=(2,3,8))
    talk(client,"家丁")
    if idle(client)["variables"].get("lazx") != "1":
        raise AutomationError("Family servant did not open Zhao Xing report")
    talk(client,"赵兴")
    if idle(client)["variables"].get("laytzx") != "1":
        raise AutomationError("Zhao Xing did not open the new hallmaster report")
    checkpoint(client,output,"linan-zhao-xing-report")
    talk(client,"新杨堂主")
    if idle(client)["variables"].get("lafyx") != "1":
        raise AutomationError("Hallmaster did not open new Feiyun meeting")
    talk(client,"新飞云")
    after = checkpoint(client,output,"linan-daoxiang-and-luyou-task")
    if (after["variables"].get("dxc") != "1" or after["variables"].get("laly") != "1"
            or after["variables"].get("lafyx") != "0"
            or after["variables"].get("event") != before["variables"].get("event")
            or quantity(after,"奇矿石.ini") != quantity(before,"奇矿石.ini")):
        raise AutomationError("Linan reports did not open Daoxiang and Lu You with prerequisites intact")
    client.save_or_load(4)
    checkpoint(client,output,"linan-daoxiang-task-source-slot4")


def linan_luyou(client, output, resource, assist_level=None):
    before = idle(client)
    in_arena = before["map"] == "比武场.map" and not int(before["variables"].get("bwcly") or 0)
    if in_arena:
        before = json.loads((output / "luyou-before-task-slot1.json").read_text(encoding="utf-8"))
    elif (before["map"] != "临安城.map" or before["variables"].get("laly") != "1"
            or int(before["variables"].get("bwcly") or 0)):
        raise AutomationError("Lu You requires the normal newly-opened task source")
    if assist_level:
        assist_battle(client,output,assist_level)
    has_ore = before["variables"].get("zdtj") == "1"
    if not in_arena:
        client.save_or_load(1)
        checkpoint(client,output,"luyou-before-task-slot1")
        spring = next(row for row in before["magic"] if row["file"] == SPELLS[0][3])
        client.assign_magic(spring["slot"],0)
        target = next(row for row in idle(client)["targets"] if row["name"] == "陆游" and row.get("interactive"))
        approach_target(client,resource,target,attackable=False)
        talk(client,"陆游")
    first = checkpoint(client,output,"luyou-first-arena")
    if first["map"] != "比武场.map":
        raise AutomationError("Lu You did not open the first normal sparring round")
    kill_group(client,output,resource,"bwcly",1,("陆游",))
    won = checkpoint(client,output,"luyou-first-sparring-won")
    if won["map"] != "临安城.map" or won["variables"].get("bwcly") != "1":
        raise AutomationError("First sparring victory did not restore Lu You in Linan")
    talk(client,"陆游")
    second = checkpoint(client,output,"luyou-second-arena")
    if second["map"] != "比武场.map":
        raise AutomationError("Lu You did not open the second normal sparring round")
    target = next(row for row in second["targets"] if row["name"] == "陆游" and row.get("hostile") and npc_attackable(row))
    fight(client,output,resource,target)
    reward = checkpoint(client,output,"luyou-spring-rain-magic-reward")
    learned = [row for row in reward["magic"] if row["file"] == "001小楼一夜听春雨.ini"]
    if (reward["map"] != "临安城.map" or len(learned) != 1 or learned[0]["level"] != 9
            or reward["variables"].get("laly") != ("1" if has_ore else "0")
            or reward["variables"].get("bwcly") != ("2" if has_ore else "1")):
        raise AutomationError("Second sparring reward or ore-dependent branch mismatch")
    if has_ore:
        talk(client,"陆游")
    after = checkpoint(client,output,"luyou-ore-commissioned" if has_ore else "luyou-without-ore-complete")
    if (quantity(after,"奇矿石.ini") != quantity(before,"奇矿石.ini")-int(has_ore)
            or after["variables"].get("laly") != "0"
            or after["variables"].get("event") != before["variables"].get("event")
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Lu You ore exchange or ending points changed unexpectedly")
    client.save_or_load(2)
    checkpoint(client,output,"luyou-complete-source-slot2")


def daoxiang_investigate(client, output, resource, assist_level=None):
    before = idle(client)
    village = before["map"] == "稻香村.map" and before["variables"].get("dxc") == "1" and not int(before["variables"].get("dxcsse") or 0)
    if village:
        before = json.loads((output / "daoxiang-before-departure-slot0.json").read_text(encoding="utf-8"))
    elif before["map"] != "临安城.map" or before["variables"].get("dxc") != "1" or before["variables"].get("laly") != "0":
        raise AutomationError("Daoxiang requires the normal Lu You-complete source")
    if assist_level:
        assist_battle(client,output,assist_level)
    if not village:
        client.save_or_load(0)
        checkpoint(client,output,"daoxiang-before-departure-slot0")
        transition(client,resource,2,"稻香村.map","临安城/临安-稻香村.txt",avoid_traps=(3,8),via_path=True)
        client.save_or_load(1)
        checkpoint(client,output,"daoxiang-first-entrance-slot1")
    disciple = next((row for row in idle(client)["targets"] if row["name"] == "弟子" and row.get("interactive")),None)
    if disciple:
        approach_target(client,resource,disciple,attackable=False)
        talk(client,"弟子")
        checkpoint(client,output,"daoxiang-changge-password-clue")
    if idle(client)["variables"].get("dxcss") != "1":
        target = next(row for row in idle(client)["targets"] if row["name"] == "天忍教弟子" and row.get("interactive"))
        if not target.get("hostile"):
            approach_target(client,resource,target,attackable=False)
            talk(client,"天忍教弟子")
        spring = next(row for row in idle(client)["magic"] if row["file"] == SPELLS[0][3])
        selected = spring if spring["level"] == 10 else next(row for row in idle(client)["magic"] if row["file"] == "001小楼一夜听春雨.ini")
        client.assign_magic(selected["slot"],0)
        client.save_or_load(3)
        checkpoint(client,output,"daoxiang-lone-disciple-battle-slot3")
        kill_group(client,output,resource,"dxcss",1,("天忍教弟子",))
        checkpoint(client,output,"daoxiang-lone-disciple-mask-reward")
    if idle(client)["variables"].get("dxcml") != "1":
        target = next(row for row in idle(client)["targets"] if row["name"] == "天忍教引路人" and row.get("interactive"))
        approach_target(client,resource,target,attackable=False)
        talk(client,"天忍教引路人")
    guide = checkpoint(client,output,"daoxiang-guide-opened-forest")
    if guide["variables"].get("dxcml") != "1":
        raise AutomationError("The masked guide did not open the forest investigation")
    transition(client,resource,2,"别离村迷宫.map","稻香村/稻香村-霹雳堂.txt",avoid_traps=(1,3),via_path=True)
    after = checkpoint(client,output,"daoxiang-neutral-forest-entrance")
    if (after["variables"].get("event") != before["variables"].get("event")
            or quantity(after,"奇矿石.ini") != quantity(before,"奇矿石.ini")
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Daoxiang infiltration changed ending points, ore or money")
    client.save_or_load(2)
    checkpoint(client,output,"daoxiang-forest-before-report-slot2")


def daoxiang_report(client, output, resource):
    before = idle(client)
    forest = before["map"] == "别离村迷宫.map" and before["variables"].get("dxcml") == "1"
    village = before["map"] == "稻香村.map" and before["variables"].get("dxcsse") == "1"
    if not forest and not village:
        raise AutomationError("Daoxiang report requires the normal infiltration or village-return source")
    spring = next(row for row in before["magic"] if row["file"] == SPELLS[0][3])
    client.assign_magic(spring["slot"],0)
    if forest:
        target = next(row for row in before["targets"] if row["name"] == "天忍教杀手" and row.get("interactive"))
        approach_target(client,resource,target,attackable=False)
        talk(client,"天忍教杀手")
    returned = checkpoint(client,output,"daoxiang-forest-intelligence-village-return")
    if returned["map"] != "稻香村.map" or returned["variables"].get("dxcsse") != "1":
        raise AutomationError("Forest intelligence did not open the village assassins")
    client.save_or_load(4)
    checkpoint(client,output,"daoxiang-three-assassins-before-battle-slot4")
    if int(returned["variables"].get("dxcsssw") or 0) < 3:
        targets = [row for row in returned["targets"] if row["name"] == "天忍教杀手" and row.get("interactive") and not row.get("hostile")]
        if targets:
            point = returned["player"]["position"]
            target = min(targets,key=lambda row:abs(row["position"]["x"]-point["x"])*2+abs(row["position"]["y"]-point["y"]))
            approach_target(client,resource,target,attackable=False)
            talk(client,"天忍教杀手",position=(target["position"]["x"],target["position"]["y"]))
        kill_group(client,output,resource,"dxcsssw",3,("天忍教杀手",))
    killed = checkpoint(client,output,"daoxiang-three-assassins-dead")
    if killed["variables"].get("dxcmr") != "1":
        raise AutomationError("Three real assassin deaths did not reveal the Changge contact")
    client.save_or_load(5)
    checkpoint(client,output,"daoxiang-contact-before-report-slot5")
    target = next(row for row in killed["targets"] if row["name"] == "门人" and row.get("interactive"))
    approach_target(client,resource,target,attackable=False)
    talk(client,"门人")
    after = checkpoint(client,output,"daoxiang-report-fengchi-convention-ready")
    if (after["map"] != "临安城.map" or after["variables"].get("fcsz") != "3"
            or quantity(after,"玉笔令.ini") != quantity(before,"玉笔令.ini")-1
            or after["variables"].get("event") != before["variables"].get("event")
            or after["variables"].get("bwcly") != before["variables"].get("bwcly")
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Changge report did not open the convention or preserve its prerequisites")
    client.save_or_load(6)
    checkpoint(client,output,"fengchi-convention-departure-source-slot6")


def fengchi_convention(client, output, resource, assist_level=None):
    before = idle(client)
    arena = before["map"] == "凤池山庄-比武场.map"
    if arena:
        before = json.loads((output / "fengchi-convention-before-departure-slot0.json").read_text(encoding="utf-8"))
    elif before["map"] != "临安城.map" or before["variables"].get("fcsz") != "3" or before["variables"].get("dxcml") == "2":
        raise AutomationError("Fengchi convention requires the normal Changge report-complete source")
    if assist_level:
        assist_battle(client,output,assist_level)
    if not arena:
        client.save_or_load(0)
        checkpoint(client,output,"fengchi-convention-before-departure-slot0")
        transition(client,resource,3,"临安-凤池山庄.map","临安城/临安-凤池山庄.txt",avoid_traps=(2,8),via_path=True)
        transition(client,resource,2,"凤池山庄.map","临安-凤池山庄/trap2至凤池.txt",avoid_traps=(1,),via_path=True,combat=True)
        checkpoint(client,output,"fengchi-convention-friends-arrived")
        target = next(row for row in idle(client)["targets"] if row["name"] == "赵" and row.get("interactive"))
        approach_target(client,resource,target,attackable=False)
        talk(client,"赵")
    sparring = checkpoint(client,output,"fengchi-autonomous-sparring-player-turn")
    if sparring["map"] != "凤池山庄-比武场.map" or not sparring.get("worldInput"):
        raise AutomationError("Autonomous convention sparring did not reach the player turn")
    rain = next(row for row in sparring["magic"] if row["file"] == "001小楼一夜听春雨.ini")
    client.assign_magic(rain["slot"],0)
    target = next(row for row in sparring["targets"] if row["name"] == "飞云" and row.get("hostile") and npc_attackable(row))
    fight(client,output,resource,target)
    after = checkpoint(client,output,"fengchi-convention-finished-linan-return")
    if (after["map"] != "临安城.map" or after["variables"].get("dxcml") != "2"
            or after["variables"].get("event") != before["variables"].get("event")
            or after["variables"].get("bwcly") != before["variables"].get("bwcly")
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Convention did not open the pursuit with ending and ore prerequisites intact")
    client.save_or_load(1)
    checkpoint(client,output,"daoxiang-pursuit-departure-source-slot1")


def yelv_pursuit(client, output, resource):
    before = idle(client)
    initial = before["map"] == "临安城.map" and before["variables"].get("dxcml") == "2"
    if initial:
        client.save_or_load(2)
        checkpoint(client,output,"yelv-pursuit-before-departure-slot2")
        transition(client,resource,2,"稻香村.map","临安城/临安-稻香村.txt",avoid_traps=(3,8),via_path=True)
    else:
        before = json.loads((output / "yelv-pursuit-before-departure-slot2.json").read_text(encoding="utf-8"))
    state = idle(client)
    if state["map"] == "稻香村.map" and state["variables"].get("dxcml") == "2":
        transition(client,resource,2,"别离村迷宫.map","稻香村/稻香村-霹雳堂.txt",avoid_traps=(1,3),via_path=True)
        lead = checkpoint(client,output,"yelv-forest-confrontation-enemies-active")
        if lead["variables"].get("ylpl") != "1" or lead["variables"].get("dxc") != "2":
            raise AutomationError("Second forest visit did not open the native Yelv pursuit")
        client.save_or_load(3)
        checkpoint(client,output,"yelv-forest-escape-source-slot3")
    state = idle(client)
    if state["map"] == "别离村迷宫.map" and state["variables"].get("ylpl") == "1":
        transition(client,resource,1,"稻香村.map","别离村迷宫/trap1至别离村.txt",via_path=True,combat=True)
    state = idle(client)
    if state["map"] == "稻香村.map" and state["variables"].get("ylpl") == "1":
        transition(client,resource,1,"临安城.map","稻香村/稻香村-临安.txt",avoid_traps=(2,3),via_path=True,combat=True)
    state = idle(client)
    if state["map"] == "临安城.map" and state["variables"].get("ylpl") == "1":
        transition(client,resource,3,"临安-凤池山庄.map","临安城/临安-凤池山庄.txt",avoid_traps=(2,8),via_path=True)
    state = idle(client)
    if state["map"] != "临安-凤池山庄.map" or state["variables"].get("ylpl") != "1":
        raise AutomationError("Pursuit did not arrive at the native Yelv road encounter")
    client.save_or_load(4)
    checkpoint(client,output,"yelv-road-before-duel-slot4")
    target = next(row for row in state["targets"] if row["name"] == "耶律辟离" and row.get("interactive"))
    if not target.get("hostile"):
        approach_target(client,resource,target,attackable=False)
        talk(client,"耶律辟离")
    after = checkpoint(client,output,"yelv-road-duel-opened")
    if after["variables"].get("event") != before["variables"].get("event") or after["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Road pursuit changed ending points or money")
    client.save_or_load(5)
    checkpoint(client,output,"yelv-ending-before-battle-slot5")


def yelv_duel(client, output, resource):
    before = idle(client)
    if before["map"] != "临安-凤池山庄.map" or before["variables"].get("ylpl") != "1":
        raise AutomationError("Yelv duel requires the normal road battle source")
    target = next(row for row in before["targets"] if row["name"] == "耶律辟离" and row.get("hostile") and npc_attackable(row))
    rain = next(row for row in before["magic"] if row["file"] == "001小楼一夜听春雨.ini")
    client.assign_magic(rain["slot"],0)
    event = int(before["variables"].get("event") or 0)
    source = output / "user-data/save" / SAVE_NAMESPACE / "rpg6"
    before_hashes = save_hashes(source)
    fight(client,output,resource,target,allow_title=event==0)
    after = checkpoint(client,output,"yelv-injury-ending-title" if event==0 else "yelv-defeated-fengchi-rescue-opened")
    if event == 0:
        if after["scene"] != "Title" or before_hashes != save_hashes(source):
            raise AutomationError("Injury epilogue did not reach title or changed the normal battle source")
        write_json(output / "injury-ending-proof.json",dict(status="passed",cheatAssisted=True,
                   ending="柴嵩毒发身亡",sourceSnapshot="yelv-ending-before-battle-slot5.json",
                   sourceSlot=5,sourceManualSaveUnchanged=True,sourceManualSaveHashes=before_hashes,
                   beforeEndingPoints=before["variables"].get("event"),effectiveEndingPoints=event,
                   nativeTarget=target,endingScript="script/map/临安-凤池山庄/耶律辟离死亡.txt",
                   endingScriptSha256=hashlib.sha256((resource / "script/map/临安-凤池山庄/耶律辟离死亡.txt").read_bytes()).hexdigest(),
                   titleSnapshot="yelv-injury-ending-title.json",movieExpected=False))
    else:
        if (after["map"] != "临安-凤池山庄.map" or after["variables"].get("ylpl") != "0"
                or after["variables"].get("fcsz") != "4" or int(after["variables"].get("event") or 0) != event):
            raise AutomationError("Yelv defeat did not open the normal Fengchi rescue")
        client.save_or_load(6)
        checkpoint(client,output,"fengchi-rescue-departure-source-slot6")


def fengchi_rescue(client, output, resource):
    state = idle(client)
    if state["map"] == "临安-凤池山庄.map":
        if state["variables"].get("fcsz") != "4" or state["variables"].get("ylpl") != "0":
            raise AutomationError("Rescue requires the normal post-Yelv road source")
        client.save_or_load(0)
        before = checkpoint(client,output,"fengchi-rescue-before-departure-slot0")
        transition(client,resource,2,"凤池山庄.map","trap2至凤池.txt",avoid_traps=(1,3),via_path=True)
        client.save_or_load(1)
        checkpoint(client,output,"fengchi-rescue-before-gate-slot1")
    elif state["map"] in ("凤池山庄.map","临安大牢第1层.map"):
        before = json.loads((output / "fengchi-rescue-before-departure-slot0.json").read_text(encoding="utf-8"))
    else:
        raise AutomationError("Rescue requires the normal road or manor battle source")
    event = int(before["variables"].get("event") or 0)
    if event not in (1,2):
        raise AutomationError("Rescue requires an independently preserved ending source")
    source = output / "user-data/save" / SAVE_NAMESPACE / "rpg7"
    source_hashes = save_hashes(source)
    state = idle(client)
    if not any(row.get("hostile") for row in state["targets"]):
        guard = next(row for row in state["targets"] if row["name"] == "家丁" and row.get("interactive"))
        approach_target(client,resource,guard,attackable=False)
        talk(client,"家丁")
        client.save_or_load(2)
        checkpoint(client,output,"fengchi-rescue-hostile-source-slot2")
    rain = next(row for row in idle(client)["magic"] if row["file"] == "001小楼一夜听春雨.ini")
    client.assign_magic(rain["slot"],0)
    for _ in range(3):
        state = idle(client,allow_title=event==1)
        if state.get("scene") == "Title" or state.get("map") == "临安大牢第1层.map":
            break
        count = int(state["variables"].get("fcszdrsw") or 0)
        if count >= 2:
            raise AutomationError("Rescue deaths did not trigger the native story branch")
        targets = [row for row in state["targets"] if row["name"] in ("邵骑风","敖管家")
                   and row.get("hostile") and npc_attackable(row)]
        if not targets:
            raise AutomationError("Missing rescue boss before both native deaths")
        position = state["player"]["position"]
        target = min(targets,key=lambda row: abs(row["position"]["x"]-position["x"])*2
                                             +abs(row["position"]["y"]-position["y"]))
        try:
            fight(client,output,resource,target,allow_title=event==1)
        except AutomationError as error:
            after = idle(client,allow_title=event==1)
            if (str(error) != "StartCombat: world_changed" or not
                    (after.get("scene") == "Title" or after.get("map") == "临安大牢第1层.map")):
                raise
            write_json(output / f"combat-rescue-world-change-{time.time_ns()}.json",
                       dict(target=target,after=after,status="native-story-branch",actionError=str(error)))
    after = checkpoint(client,output,"fengchi-prison-ending-title" if event==1 else "fengchi-rescue-prison-battle-opened")
    records = late_records(output,"trace.jsonl",completed_scripts=("凤池山庄/敌人死亡.txt",))
    starts = [row for row in records if row.get("eventType") == "script.start"
              and row.get("virtualPath","").endswith("凤池山庄/敌人死亡.txt")]
    if len(starts) != 2 or any(not any(row.get("eventType") == "script.finish"
            and row.get("executionId") == start["executionId"] and row.get("status") == "completed"
            for row in records) for start in starts):
        raise AutomationError("Both rescue boss death scripts must complete")
    if source_hashes != save_hashes(source):
        raise AutomationError("Rescue changed the preserved normal departure save")
    if event == 1:
        if after["scene"] != "Title":
            raise AutomationError("Prison death epilogue did not return normally to title")
    else:
        if (after["map"] != "临安大牢第1层.map" or after["variables"].get("fcszdrsw") != "2"
                or int(after["variables"].get("event") or 0) != event
                or int(after["variables"].get("bwcly") or 0) != (int(before["variables"].get("bwcly") or 0)
                    if int(before["variables"].get("bwcly") or 0) >= 2 else 0)
                or after["player"]["money"] != before["player"]["money"]):
            raise AutomationError("Rescue prison battle state or prerequisites mismatch")
        client.save_or_load(3)
        checkpoint(client,output,"prison-counterattack-source-slot3")
    write_json(output / "fengchi-rescue-outcome-proof.json",dict(status="passed",cheatAssisted=True,
               endingPoints=event,result="prison-death-ending-title" if event==1 else "prison-counterattack-opened",
               sourceSlot=6,sourceManualSaveUnchanged=True,sourceManualSaveHashes=source_hashes,
               completedDeathScripts=starts,deathScript="script/map/凤池山庄/敌人死亡.txt",
               deathScriptSha256=hashlib.sha256((resource / "script/map/凤池山庄/敌人死亡.txt").read_bytes()).hexdigest(),
               movieExpected=False))


def prison_counterattack(client, output, resource, assist_level=None):
    before = idle(client)
    if before["map"] != "临安大牢第1层.map" or before["variables"].get("event") != "2":
        raise AutomationError("Counterattack requires the normally reached prison battle")
    if assist_level:
        assist_battle(client,output,assist_level)
        before = idle(client)
    target = next(row for row in before["targets"] if row["name"] == "邵骑风"
                  and row.get("hostile") and npc_attackable(row))
    spell = next(row for row in before["magic"] if row["file"] == "001春城何处不飞花.ini" and row["level"] >= 9)
    client.assign_magic(spell["slot"],0)
    fight(client,output,resource,target)
    after = checkpoint(client,output,"prison-shao-defeated-linan-return")
    if (after["map"] != "临安城.map" or after["variables"].get("lacsj") != "1"
            or after["variables"].get("event") != before["variables"].get("event")
            or after["variables"].get("bwcly") != before["variables"].get("bwcly")
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Prison escape or its ending and ore prerequisites mismatch")
    late_records(output,"trace.jsonl",completed_scripts=("临安大牢第1层/邵骑风死亡.txt",))
    client.save_or_load(4)
    checkpoint(client,output,"linan-after-prison-before-house-slot4")


def linan_revenge(client, output, resource):
    before = idle(client)
    if (before["map"] != "临安城.map" or before["variables"].get("lacsj") != "1"
            or before["variables"].get("lawb") == "1"):
        raise AutomationError("Revenge requires the normal post-prison source before investigating the house")
    checkpoint(client,output,"linan-revenge-before-house")
    walk_to(client,resource,(69,138),avoid_traps=(2,3,8,9,11))
    state = idle(client)
    guard = min((row for row in state["targets"] if row["name"] == "卫兵" and row.get("interactive")),
                key=lambda row:abs(row["position"]["x"]-69)*2+abs(row["position"]["y"]-138))
    approach_target(client,resource,guard,attackable=False)
    talk(client,"卫兵",position=(guard["position"]["x"],guard["position"]["y"]))
    refused = checkpoint(client,output,"linan-guard-before-revenge-refused")
    if (refused["variables"] != before["variables"] or refused["player"]["money"] != before["player"]["money"]
            or any(row.get("hostile") for row in refused["targets"] if row["name"] == "卫兵") ):
        raise AutomationError("Guards activated before the normal revenge trigger")
    transition(client,resource,9,"临安城.map","trap9.txt",avoid_traps=(1,2,3,8,11),via_path=True)
    after = checkpoint(client,output,"linan-house-investigated-revenge-opened")
    if (after["variables"].get("lawb") != "1"
            or after["variables"].get("event") != before["variables"].get("event")
            or after["variables"].get("bwcly") != before["variables"].get("bwcly")
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("House investigation failed to open revenge or changed prerequisites")
    client.save_or_load(5)
    checkpoint(client,output,"linan-zhaojie-before-guards-slot5")


def zhaojie_manor(client, output, resource):
    before = idle(client)
    if before["map"] != "临安城.map" or before["variables"].get("lawb") != "1":
        raise AutomationError("Zhaojie manor requires the normally opened revenge task")
    spell = next(row for row in before["magic"] if row["file"] == "001春城何处不飞花.ini" and row["level"] >= 9)
    client.assign_magic(spell["slot"],0)
    guards = [row for row in before["targets"] if row["name"] == "卫兵" and row.get("life",0)>0]
    if guards and not any(row.get("hostile") for row in guards):
        walk_to(client,resource,(69,138),avoid_traps=(2,3,8,9,11))
        state = idle(client)
        guard = min((row for row in state["targets"] if row["name"] == "卫兵" and row.get("interactive")),
                    key=lambda row:abs(row["position"]["x"]-69)*2+abs(row["position"]["y"]-138))
        approach_target(client,resource,guard,attackable=False)
        talk(client,"卫兵",position=(guard["position"]["x"],guard["position"]["y"]))
        checkpoint(client,output,"zhaojie-manor-guards-hostile")
    for _ in range(3):
        state = idle(client)
        guards = [row for row in state["targets"] if row["name"] == "卫兵" and row.get("hostile") and npc_attackable(row)]
        if not guards:
            break
        fight(client,output,resource,guards[0])
    state = checkpoint(client,output,"zhaojie-manor-guards-defeated")
    if any(row["name"] == "卫兵" and npc_attackable(row) for row in state["targets"]):
        raise AutomationError("Manor guards are still alive and hostile")
    if state["variables"].get("ladxmg") != "1":
        transition(client,resource,8,"临安城.map","trap8.txt",avoid_traps=(1,2,3,9,11),via_path=True)
        transition(client,resource,11,"临安城.map","trap11-地下迷宫.txt",avoid_traps=(1,2,3,9),via_path=True)
        refused = checkpoint(client,output,"zhaojie-basement-before-boss-refused")
        if refused["map"] != "临安城.map" or refused["variables"].get("ladxmg") == "1":
            raise AutomationError("Basement opened before Zhaojie's first defeat")
        target = next(row for row in refused["targets"] if row["name"] == "赵节" and row.get("interactive"))
        if not target.get("hostile"):
            approach_target(client,resource,target,attackable=False)
            talk(client,"赵节")
        state = idle(client)
        target = next(row for row in state["targets"] if row["name"] == "赵节" and row.get("hostile") and npc_attackable(row))
        fight(client,output,resource,target)
        after = checkpoint(client,output,"zhaojie-first-defeat-basement-opened")
        if after["map"] != "临安城.map" or after["variables"].get("ladxmg") != "1":
            raise AutomationError("Zhaojie's first native death did not open the basement")
        late_records(output,"trace.jsonl",completed_scripts=("临安城/赵节死亡.txt",))
        client.save_or_load(0)
        checkpoint(client,output,"zhaojie-first-defeat-source-slot0")
    transition(client,resource,11,"临安地下迷宫.map","trap11-地下迷宫.txt",avoid_traps=(1,2,3,9),via_path=True,combat=True)
    after = checkpoint(client,output,"zhaojie-basement-entered")
    if (after["variables"].get("event") != before["variables"].get("event")
            or after["variables"].get("bwcly") != before["variables"].get("bwcly")
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Manor route changed ending, ore or money prerequisites")
    client.save_or_load(6)
    checkpoint(client,output,"zhaojie-basement-source-slot6")


def table_reward_matches(resource, user_data, filename, allowed):
    if filename in allowed:
        return True
    for base in allowed:
        if not filename.lower().startswith(Path(base).stem.lower()):
            continue
        original = configparser.ConfigParser(interpolation=None,strict=False)
        original.read(resource / "ini/goods" / base,encoding="utf-8-sig")
        generated = configparser.ConfigParser(interpolation=None,strict=False)
        generated.read(user_data / "save" / SAVE_NAMESPACE / "game" / filename,encoding="utf-8-sig")
        if not original.has_section("Init") or not generated.has_section("init"):
            return False
        expected,actual = original["Init"],generated["init"]
        if any(expected.get(key,"") != actual.get(key,"") for key in ("name","icon","image","part")):
            return False
        ranges = {key:value for key,value in expected.items() if ">" in value}
        return bool(ranges) and all(min(map(int,value.split('>'))) <= int(actual[key]) <= max(map(int,value.split('>')))
                                    for key,value in ranges.items())
    return False


def tianren_arrival(client, output, resource, assist_level=None):
    before = idle(client)
    if before["map"] != "中都.map" or before["variables"].get("trj") != "1":
        raise AutomationError("Tianren arrival requires the normal final reunion source")
    if assist_level:
        assist_battle(client,output,assist_level)
    spell = next(row for row in before["magic"] if row["file"] == SPELLS[0][3])
    client.assign_magic(spell["slot"],0)
    client.save_or_load(0)
    checkpoint(client,output,"tianren-before-departure-slot0")
    transition(client,resource,12,"中都-天忍教.map","中都/trap-12.txt",avoid_traps=(13,16),via_path=True)
    transition(client,resource,2,"天忍教.map","中都-天忍教/地图切换右.txt",avoid_traps=(1,),via_path=True)
    client.save_or_load(1)
    checkpoint(client,output,"tianren-exterior-before-basement-slot1")
    transition(client,resource,2,"天忍教-地下迷宫1.map","天忍教/进入迷宫.txt",avoid_traps=(1,),via_path=True,combat=True)
    after = checkpoint(client,output,"tianren-first-floor-before-battle")
    if (after["variables"].get("event") != before["variables"].get("event")
            or after["variables"].get("bwcly") != before["variables"].get("bwcly")
            or int(after["variables"].get("trjdzsw") or 0)
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Tianren arrival changed the battle or story prerequisites")
    client.save_or_load(2)
    checkpoint(client,output,"tianren-first-floor-before-battle-slot2")


def tianren_first_gate(client, output, resource, assist_level=None):
    before = idle(client)
    if before["map"] != "天忍教-地下迷宫1.map" or not 0 <= int(before["variables"].get("trjdzsw") or 0) < 27:
        raise AutomationError("First-floor gates require the normal incomplete battle source")
    if assist_level:
        assist_battle(client,output,assist_level)
    transition(client,resource,1,before["map"],"天忍教-地下迷宫1/出口地图陷阱1.txt",avoid_traps=(2,),via_path=True)
    refused = checkpoint(client,output,"tianren-first-exit-refused")
    if refused["variables"] != before["variables"]:
        raise AutomationError("The first-floor return trap changed story prerequisites")
    go(client,76,132,combat=False)
    state = idle(client)
    spell = next(row for row in state['magic'] if row['file'] == '001小楼一夜听春雨.ini')
    client.assign_magic(spell['slot'],0)
    target = min((row for row in state['targets'] if row.get('hostile') and npc_attackable(row)),
                 key=lambda row:abs(row['position']['x']-76)*2+abs(row['position']['y']-132))
    if not int(state['variables'].get('trjdzsw') or 0):
        fight(client,output,resource,target)
    cleared = checkpoint(client,output,'tianren-entry-enemy-dead-before-early-passage')
    if not 0 < int(cleared['variables'].get('trjdzsw') or 0) < 27:
        raise AutomationError('Early-passage trial unexpectedly completed the first battle')
    go(client,76,94,combat=True,timeout=60)
    cleared = checkpoint(client,output,'early-passage-stone-side-before-native-movement')
    stone = next(row for row in cleared['targets'] if row['name'] == '石头' and row.get('life',0)>0)
    try:
        reachable_trap(resource,cleared['map'],2,cleared['player']['position'],
                       avoid={(stone['position']['x'],stone['position']['y'])})
    except AutomationError:
        pass
    else:
        raise AutomationError('The live stone does not block the early passage in this source')
    try:
        go(client,76,98,combat=False,timeout=30)
    except AutomationError as error:
        if not any(reason in str(error) for reason in ('no_progress','blocked_destination')):
            raise
        obstruction = str(error)
    else:
        raise AutomationError('Native movement passed the expected stone barrier')
    after = checkpoint(client,output,'tianren-early-passage-native-movement-blocked')
    if after['map'] != cleared['map'] or after['variables'] != cleared['variables']:
        raise AutomationError('The blocked passage changed story prerequisites')
    client.save_or_load(0)
    checkpoint(client,output,'tianren-early-passage-checked-source-slot0')
    write_json(output/'tianren-early-passage-proof.json',dict(status='passed',firstExitRefused=True,
               earlyPassageBlockedInTestedSource=True,nativeMovementError=obstruction,
               deathCounter=after['variables']['trjdzsw'],stone=stone,
               mapAndPlotVariablesUnchangedByMovement=True,cheatAssisted=True))


def tianren_first_battle(client, output, resource):
    before = idle(client)
    if before["map"] != "天忍教-地下迷宫1.map":
        raise AutomationError("Tianren battle requires its normally reached first floor")
    spell = next(row for row in before["magic"] if row["file"] == "001小楼一夜听春雨.ini")
    client.assign_magic(spell["slot"],0)
    kill_group(client,output,resource,"trjdzsw",27,("天忍教弟子",))
    after = checkpoint(client,output,"tianren-twenty-seven-deaths-ambush")
    if (after["variables"].get("trjdzsw") != "27" or after["map"] != before["map"]
            or after["variables"].get("event") != before["variables"].get("event")
            or after["player"]["money"] != before["player"]["money"]
            or not any(row["name"] == "天忍教教众" and row.get("hostile") for row in after["targets"])):
        raise AutomationError("Twenty-seven native deaths did not open the scripted ambush")
    late_records(output,"trace.jsonl",completed_scripts=("天忍教-地下迷宫1/天忍教弟子死亡.txt",))
    client.save_or_load(3)
    checkpoint(client,output,"tianren-first-floor-ambush-source-slot3")


def clear_enemies(client, output, resource, names):
    while True:
        state = idle(client)
        point = state['player']['position']
        enemies = [row for row in state['targets'] if row['name'] in names
                   and row.get('hostile') and npc_attackable(row)]
        if not enemies:
            return state
        for target in sorted(enemies,key=lambda row:abs(row['position']['x']-point['x'])*2
                                                  + abs(row['position']['y']-point['y'])):
            try:
                fight(client,output,resource,target)
                break
            except AutomationError as error:
                if not str(error).startswith('No reachable combat neighbor:'):
                    raise
        else:
            raise AutomationError('No reachable enemy in the remaining group')


def tianren_ambush(client, output, resource):
    before = idle(client)
    if before['map'] != '天忍教-地下迷宫1.map' or before['variables'].get('trjdzsw') != '27':
        raise AutomationError('Ambush requires the normally completed first battle')
    spell = next(row for row in before['magic'] if row['file'] == '001小楼一夜听春雨.ini')
    client.assign_magic(spell['slot'],0)
    clear_enemies(client,output,resource,('天忍教教众','天忍教弟子'))
    after = checkpoint(client,output,'tianren-ambush-enemies-cleared')
    if after['variables'] != before['variables'] or after['player']['money'] != before['player']['money']:
        raise AutomationError('Ambush changed task prerequisites or silver')
    client.save_or_load(3)
    checkpoint(client,output,'tianren-ambush-cleared-source-slot3')


def tianren_second_entry(client, output, resource):
    before = idle(client)
    if before['map'] != '天忍教-地下迷宫1.map' or before['variables'].get('trjdzsw') != '27':
        raise AutomationError('Second-floor entry requires the completed first battle')
    transition(client,resource,2,'天忍教-地下迷宫2.map',
               '天忍教-地下迷宫1/进入地下迷宫2的地图陷阱2.txt',avoid_traps=(1,),via_path=True)
    arrived = checkpoint(client,output,'tianren-second-floor-before-battle')
    if (int(arrived['variables'].get('trjylpl') or 0)
            or arrived['variables'] != before['variables']
            or any(row['name'] == '南宫飞云' for row in arrived['targets'])):
        raise AutomationError('Second-floor arrival changed prerequisites or retained the partner')
    client.save_or_load(0)
    for trap in (1,2):
        state = idle(client)
        point = reachable_trap(resource,state['map'],trap,state['player']['position'])
        walk_to(client,resource,point,combat=True)
        after = checkpoint(client,output,f'tianren-second-trap{trap}-inactive-before-boss')
        if after['map'] != arrived['map'] or after['variables'] != arrived['variables']:
            raise AutomationError('An inactive second-floor gate advanced the story')
    client.save_or_load(0)
    checkpoint(client,output,'tianren-second-floor-gates-checked-source-slot0')


def tianren_second_battle(client, output, resource):
    before = idle(client)
    if before['map'] != '天忍教-地下迷宫2.map':
        raise AutomationError('Second battle requires the normally reached second floor')
    spell = next(row for row in before['magic'] if row['file'] == '001小楼一夜听春雨.ini')
    client.assign_magic(spell['slot'],0)
    boss = next((row for row in before['targets'] if row['name'] == '耶律辟离' and row.get('life',0)>0),None)
    if boss and not boss.get('hostile'):
        approach_target(client,resource,boss,attackable=False)
        client.save_or_load(5)
        checkpoint(client,output,'tianren-yelv-before-dialogue-source-slot5')
        talk(client,'耶律辟离')
        checkpoint(client,output,'tianren-yelv-and-twelve-guards-hostile')
    kill_group(client,output,resource,'trjylpl',13,('耶律辟离','天忍教教众'))
    after = checkpoint(client,output,'tianren-yelv-thirteen-real-deaths-third-gate-open')
    if (after['variables'].get('event') != before['variables'].get('event')
            or after['player']['money'] != before['player']['money']):
        raise AutomationError('Final battle changed ending prerequisites or silver')
    client.save_or_load(5)
    checkpoint(client,output,'tianren-second-battle-complete-source-slot5')


def tianren_second_disciples(client, output, resource, assist_level=None):
    before = idle(client)
    if before['map'] != '天忍教-地下迷宫2.map' or int(before['variables'].get('trjylpl') or 0):
        raise AutomationError('Second-floor disciples require the normal pre-boss source')
    if assist_level:
        assist_battle(client,output,assist_level)
    spell = next(row for row in before['magic'] if row['file'] == '001小楼一夜听春雨.ini')
    client.assign_magic(spell['slot'],0)
    clear_enemies(client,output,resource,('天忍教弟子',))
    after = checkpoint(client,output,'tianren-second-disciples-cleared-before-boss')
    if after['variables'] != before['variables']:
        raise AutomationError('Second-floor disciples changed the boss death counter')
    client.save_or_load(0)
    checkpoint(client,output,'tianren-second-disciples-cleared-source-slot0')


def tianren_third_entry(client, output, resource):
    before = idle(client)
    if before['map'] != '天忍教-地下迷宫2.map' or before['variables'].get('trjylpl') != '13':
        raise AutomationError('Third-floor entry requires the thirteen real deaths')
    transition(client,resource,2,'天忍教-地下迷宫3.map','天忍教-地下迷宫2/进入第三层.txt',
               avoid_traps=(1,),via_path=True)
    after = checkpoint(client,output,'tianren-third-floor-feiyun-corpse-before-ending')
    if (after['variables'] != before['variables']
            or not any(row['name'] == '南宫飞云尸体' for row in after['targets'])):
        raise AutomationError('Third-floor aftermath missing its ending object')
    client.save_or_load(6)
    checkpoint(client,output,'tianren-gold-ending-source-slot6')


def tianren_finale(client, output, resource):
    before = idle(client)
    if before['map'] != '天忍教-地下迷宫3.map' or before['variables'].get('trjylpl') != '13':
        raise AutomationError('Finale requires the normally reached third-floor aftermath')
    target = next(row for row in before['targets'] if row['name'] == '南宫飞云尸体')
    approach_target(client,resource,target,attackable=False)
    state = idle(client)
    client.act('Interact',timeout=185,generation=state['generation'],targetId=target['id'],running=False,timeoutMs=180000)
    idle(client,timeout=240,allow_title=True)
    after = checkpoint(client,output,'xiaoxiang-gold-ending-title')
    if after['scene'] != 'Title':
        raise AutomationError('Final reunion did not return to title')
    late_records(output,'trace.jsonl',completed_scripts=('天忍教-地下迷宫3/飞云尸体.txt',))
    write_json(output/'gold-ending-player-outcome-proof.json',dict(status='passed',cheatAssisted=True,
               ending='飞云康复与无双团圆',nativeDeathsFirstFloor=27,nativeDeathsSecondFloor=13,
               event=before['variables'].get('event'),returnToTitle=True,movieExpected=False,
               beforeFile=str(output/'tianren-gold-ending-source-slot6.json'),
               afterFile=str(output/'xiaoxiang-gold-ending-title.json')))


def tianren_third_checks(client, output, resource):
    before = idle(client)
    if before['map'] != '天忍教-地下迷宫3.map' or before['variables'].get('trjylpl') != '13':
        raise AutomationError('Third-floor checks require the normal aftermath source')
    point = reachable_trap(resource,before['map'],3,before['player']['position'])
    walk_to(client,resource,point,combat=False)
    cleared = checkpoint(client,output,'tianren-third-inherited-trap-inactive')
    if cleared['variables'] != before['variables'] or cleared['player']['money'] != before['player']['money']:
        raise AutomationError('Inherited trap invoked a different story or changed silver')
    target = next(row for row in cleared['targets'] if row['name'] == '完颜宏烈尸体')
    approach_target(client,resource,target,attackable=False)
    talk(client,'完颜宏烈尸体')
    reward = checkpoint(client,output,'tianren-wanyan-corpse-silver-reward')
    increase = reward['player']['money']-cleared['player']['money']
    if not 10 <= increase <= 100 or reward['variables'] != cleared['variables']:
        raise AutomationError('Corpse reward is outside the native silver interval')
    talk(client,'完颜宏烈尸体')
    repeated = checkpoint(client,output,'tianren-wanyan-corpse-no-repeated-silver')
    if repeated['player']['money'] != reward['player']['money'] or repeated['variables'] != reward['variables']:
        raise AutomationError('Corpse repeated its reward or advanced the story')
    client.save_or_load(6)
    checkpoint(client,output,'tianren-gold-ending-source-slot6')
    write_json(output/'tianren-third-rewards-and-trap-proof.json',dict(status='passed',cheatAssisted=True,
               silverReward=increase,repeatedReward=False,legacyTrapInactive=True,plotVariablesUnchanged=True))


def zhongdu_final_reunion(client, output, resource):
    before = idle(client)
    if before["map"] != "中都.map" or before["variables"].get("zdjzfy") != "1" or int(before["variables"].get("trj") or 0):
        raise AutomationError("Final reunion requires its normal post-Zhaojie Zhongdu source")
    transition(client,resource,12,"中都.map","中都/trap-12.txt",avoid_traps=(13,16),via_path=True)
    refused = checkpoint(client,output,"tianren-exit-before-reunion-refused")
    if refused["variables"] != before["variables"] or refused["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Tianren gate changed the incomplete reunion prerequisites")
    target = next(row for row in refused["targets"] if row["name"] == "南宫飞云" and row.get("interactive"))
    approach_target(client,resource,target,attackable=False)
    talk(client,"南宫飞云")
    investigation = checkpoint(client,output,"final-reunion-tianren-investigation-opened")
    if investigation["variables"].get("zdhh") != "1" or investigation["variables"].get("zdjzfy") != "0":
        raise AutomationError("Final Feiyun reunion did not open the market investigation")
    transition(client,resource,13,"中都.map","中都/地图陷阱13.txt",avoid_traps=(12,16),via_path=True)
    discovered = checkpoint(client,output,"tianren-location-discovered")
    if discovered["variables"].get("zdhh") != "0" or discovered["variables"].get("zdjzfy") != "1":
        raise AutomationError("Market investigation did not restore the Feiyun meeting")
    target = next(row for row in discovered["targets"] if row["name"] == "南宫飞云" and row.get("interactive"))
    approach_target(client,resource,target,attackable=False)
    talk(client,"南宫飞云")
    after = checkpoint(client,output,"final-reunion-feiyun-follows-tianren-opened")
    if (after["variables"].get("trj") != "1" or after["variables"].get("event") != before["variables"].get("event")
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Feiyun did not open the native Tianren entrance")
    client.save_or_load(5)
    checkpoint(client,output,"tianren-ready-source-slot5")


def map_chests(client, output, resource, map_name, boxes, label, save_slot):
    before = idle(client)
    if before["map"] != map_name:
        raise AutomationError("Chest rewards require their normally reached map source")
    for number,position,table in boxes:
        definitions = configparser.ConfigParser(interpolation=None,strict=False)
        definitions.read(resource / "ini/buy" / table,encoding="utf-8-sig")
        allowed = {definitions[s]["inifile"] for s in definitions.sections() if s != "Header"}
        state = idle(client)
        target = next(row for row in state["targets"] if row.get("kind") == "object"
                      and row["name"] == "宝箱" and row["position"] == dict(x=position[0],y=position[1]))
        approach_target(client,resource,target,attackable=False)
        ready = checkpoint(client,output,f"{label}-box{number}-before")
        talk(client,"宝箱",position=position)
        reward = checkpoint(client,output,f"{label}-box{number}-reward")
        filenames = {row["file"] for row in ready["inventory"]+reward["inventory"]}
        changes = {name:quantity(reward,name)-quantity(ready,name) for name in filenames
                   if quantity(reward,name) != quantity(ready,name)}
        if (len(changes) != 1 or next(iter(changes.values())) != 1
                or not table_reward_matches(resource,output / "user-data",next(iter(changes)),allowed)):
            raise AutomationError(f"Box {number} did not give one native table reward: {changes}")
        talk(client,"宝箱",position=position)
        repeated = checkpoint(client,output,f"{label}-box{number}-repeat")
        if (repeated["inventory"] != reward["inventory"] or repeated["variables"] != ready["variables"]
                or repeated["player"]["money"] != ready["player"]["money"]):
            raise AutomationError(f"Box {number} repeated its reward or changed task prerequisites")
        write_json(output / f"{label}-box{number}-reward-proof.json",dict(status="passed",table=table,
                   reward=changes,repeatedReward=False,moneyUnchanged=True,plotVariablesUnchanged=True))
    client.save_or_load(save_slot)
    checkpoint(client,output,f"{label}-loot-complete-source-slot{save_slot}")



def zhaojie_loot(client, output, resource):
    map_chests(client,output,resource,"临安地下迷宫.map",
               ((1,(19,141),"中级药品.ini"),(2,(72,36),"5级防具.ini"),
                (3,(31,35),"5级武器.ini"),(4,(37,24),"中级药品.ini")),"basement",2)


def tianren_loot(client, output, resource):
    before = idle(client)
    first = before["map"] == "天忍教-地下迷宫1.map" and before["variables"].get("trjdzsw") == "27"
    second = before["map"] == "天忍教-地下迷宫2.map"
    if not first and not second:
        raise AutomationError("Tianren loot requires its normal first ambush or second floor source")
    boxes = ((1,(34,109),"高级药品.ini"),(2,(57,30),"6级防具.ini"),
             (3,(54,58),"6级武器.ini"),(4,(68,36),"高级药品.ini")) if first else (
             (1,(22,141),"特级药品.ini"),(2,(28,130),"7级防具.ini"),(3,(33,122),"7级武器.ini"),
             (4,(55,76),"特级药品.ini"),(5,(60,66),"特级药品.ini"),(6,(65,58),"特级药品.ini"))
    map_chests(client,output,resource,before["map"],boxes,"tianren-first" if first else "tianren-second",4 if first else 1)


def zhaojie_second_battle(client, output, resource):
    before = idle(client)
    if before["map"] != "临安地下迷宫.map":
        raise AutomationError("Second Zhaojie battle requires the normally reached basement")
    has_ore = before["variables"].get("bwcly") == "2"
    target = next(row for row in before["targets"] if row["name"] == "赵节" and npc_attackable(row))
    spell = next(row for row in before["magic"] if row["file"] == "001小楼一夜听春雨.ini")
    client.assign_magic(spell["slot"],0)
    try:
        fight(client,output,resource,target)
    except AutomationError as error:
        if str(error) != "StartCombat: world_changed":
            raise
    after = checkpoint(client,output,"zhaojie-second-death-ore-dependent-transition")
    if (after["map"] != ("临安城.map" if has_ore else "中都.map")
            or after["variables"].get("bwcly") != ("3" if has_ore else before["variables"].get("bwcly"))
            or after["variables"].get("event") != before["variables"].get("event")
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Second Zhaojie death did not take its native ore-dependent transition")
    late_records(output,"trace.jsonl",completed_scripts=("临安地下迷宫/赵节死亡.txt",))
    client.save_or_load(3)
    checkpoint(client,output,"zhaojie-second-death-source-slot3")
    if has_ore:
        target = next(row for row in after["targets"] if row["name"] == "陆游" and row.get("interactive"))
        approach_target(client,resource,target,attackable=False)
        talk(client,"陆游")
        reward = checkpoint(client,output,"ore-soft-armor-month-later-zhongdu")
        armors = {row["file"] for row in after["inventory"]+reward["inventory"]
                  if table_reward_matches(resource,output / "user-data",row["file"],{"贴身软甲.ini"})}
        if (reward["map"] != "中都.map" or reward["variables"].get("laly") != "0"
                or sum(quantity(reward,name)-quantity(after,name) for name in armors) != 1):
            raise AutomationError("Commissioned ore did not grant one soft armor and the month transition")
    else:
        reward = after
        if quantity(reward,"贴身软甲.ini") != quantity(before,"贴身软甲.ini"):
            raise AutomationError("The no-ore source unexpectedly received commissioned armor")
    if (reward["variables"].get("zdjzfy") != "1" or reward["player"]["money"] != before["player"]["money"]
            or reward["variables"].get("event") != before["variables"].get("event")):
        raise AutomationError("Final Zhongdu reunion prerequisites mismatch")
    client.save_or_load(4)
    checkpoint(client,output,"zhongdu-final-reunion-source-slot4")


def zhaojie_firearms(client, output, resource, assist_level=None):
    before = idle(client)
    if before["map"] != "临安地下迷宫.map":
        raise AutomationError("Firearms require the normally reached Zhaojie basement")
    if assist_level:
        assist_battle(client,output,assist_level)
    binding_path = output / "firearms-target-bindings.json"
    if binding_path.exists():
        bindings = json.loads(binding_path.read_text(encoding="utf-8"))
        if bindings["generation"] != before["generation"]:
            raise AutomationError("Firearm target identities changed with the native map generation")
    else:
        if int(before["variables"].get("hyp") or 0):
            raise AutomationError("Fresh firearm bindings require the normal zero-counter source")
        definitions = configparser.ConfigParser(interpolation=None,strict=False)
        definitions.read(resource / "ini/save/lamg.npc",encoding="utf-8-sig")
        sections = [s for s in definitions.sections() if s.lower().startswith("npc")]
        actors = [row for row in before["targets"] if row.get("kind") == "npc"]
        if [row["name"] for row in actors[:len(sections)]] != [definitions[s]["name"] for s in sections]:
            raise AutomationError("Native basement actor order differs from the initial resource")
        targets = [dict(section=s,targetId=actors[i]["id"],name=actors[i]["name"])
                   for i,s in enumerate(sections) if definitions[s].get("deathscript") == "火药炮.txt"]
        if len(targets) != 8:
            raise AutomationError("The basement must bind exactly eight firearm deaths")
        bindings = dict(generation=before["generation"],targets=targets,
                        sourceSha256=hashlib.sha256((resource / "ini/save/lamg.npc").read_bytes()).hexdigest())
        write_json(binding_path,bindings)
        checkpoint(client,output,"firearms-before-battle")
    spell = next(row for row in before["magic"] if row["file"] == "001春城何处不飞花.ini" and row["level"] >= 9)
    client.assign_magic(spell["slot"],0)
    identities = {row["targetId"] for row in bindings["targets"]}
    for _ in range(8):
        state = idle(client)
        count = int(state["variables"].get("hyp") or 0)
        if count == 8:
            break
        if count == 4:
            checkpoint(client,output,"firearms-four-deaths-discussion")
        targets = [row for row in state["targets"] if row["id"] in identities and npc_attackable(row)]
        if not targets:
            raise AutomationError("Firearm soldiers disappeared before all eight native death callbacks")
        position = state["player"]["position"]
        target = min(targets,key=lambda row:abs(row["position"]["x"]-position["x"])*2+abs(row["position"]["y"]-position["y"]))
        fight(client,output,resource,target)
    after = checkpoint(client,output,"firearms-eight-deaths-magic-learned")
    spells = [row for row in after["magic"] if row["file"] == "001火药炮.ini"]
    if (after["variables"].get("hyp") != "8" or len(spells) != 1 or spells[0]["level"] != 1
            or after["variables"].get("event") != before["variables"].get("event")
            or after["variables"].get("bwcly") != before["variables"].get("bwcly")
            or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Firearm reward or story prerequisites mismatch")
    late_records(output,"trace.jsonl",completed_scripts=("临安地下迷宫/火药炮.txt",))
    client.save_or_load(1)
    checkpoint(client,output,"firearms-learned-before-loot-boss-slot1")


def daoxiang_paralysis(client, output, resource):
    before = idle(client)
    if before["map"] != "别离村迷宫.map" or before["variables"].get("dxcml") != "1" or quantity(before,"钥匙.ini"):
        raise AutomationError("Paralysis reward requires the normal neutral forest source without its key")
    walk_to(client,resource,(36,54),avoid_traps=(1,))
    talk(client,"宝箱",position=(36,52))
    locked = checkpoint(client,output,"paralysis-box-without-key-refused")
    if locked["inventory"] != before["inventory"] or locked["magic"] != before["magic"]:
        raise AutomationError("Locked box changed inventory or granted magic without its key")
    walk_to(client,resource,(14,23),avoid_traps=(1,))
    talk(client,"尸体",position=(14,21))
    key = checkpoint(client,output,"paralysis-key-from-corpse")
    if quantity(key,"钥匙.ini") != quantity(before,"钥匙.ini")+1:
        raise AutomationError("The corpse did not award exactly one key")
    walk_to(client,resource,(36,54),avoid_traps=(1,))
    talk(client,"宝箱",position=(36,52))
    learned = checkpoint(client,output,"paralysis-magic-reward")
    spells = [row for row in learned["magic"] if row["file"] == "001定身法.ini"]
    if len(spells) != 1 or spells[0]["level"] != 1 or quantity(learned,"钥匙.ini") != quantity(key,"钥匙.ini"):
        raise AutomationError("Paralysis box magic or retained key mismatch")
    talk(client,"宝箱",position=(36,52))
    after = checkpoint(client,output,"paralysis-repeated-box-no-duplicate-reward")
    if (after["inventory"] != learned["inventory"] or after["magic"] != learned["magic"]
            or after["variables"] != before["variables"] or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Repeated paralysis box changed rewards or story prerequisites")
    client.save_or_load(3)
    checkpoint(client,output,"paralysis-complete-source-slot3")


def mine_gate(client, output, resource, assist_level=None):
    before = idle(client)
    if assist_level:
        assist_battle(client,output,assist_level)
    if before["map"] == "中都.map" and not int(before["variables"].get("goks") or 0):
        if before["player"]["position"] == dict(x=25,y=317):
            transition(client,resource,1,"中都.map","中都/地图陷阱1.txt",via_path=True)
        transition(client,resource,10,"中都-矿山.map","中都/地图陷阱10.txt",via_path=True)
        state = idle(client)
        transition(client,resource,2,state["map"],"中都-矿山/地图陷阱2.txt",avoid_traps=(1,),via_path=True,combat=True)
        after = checkpoint(client,output,"mine-without-task-refused")
        if after["variables"] != state["variables"]:
            raise AutomationError("Mine gate without its task changed the observed story state")
    elif before["map"] == "矿山.map" and int(before["variables"].get("swksdr") or 0)<25:
        transition(client,resource,1,before["map"],"矿山/地图陷阱1.txt",avoid_traps=(2,),via_path=True)
        after = checkpoint(client,output,"mine-unfinished-departure-refused")
        if after["variables"] != before["variables"]:
            raise AutomationError("Unfinished mine gate changed the observed story state")
    else:
        raise AutomationError("Mine gate requires a normal unstarted or unfinished task source")


def changan_gates(client, output, resource):
    before = idle(client)
    if before["map"] != "长安.map" or any(before["variables"].get(key) not in ("", "0")
                                         for key in ("yxsz", "fxsz", "gozmg")):
        raise AutomationError("Chang'an gates require the unstarted city investigation")
    for trap in (7, 3, 1, 4):
        state = idle(client)
        occupied = {(row["position"]["x"], row["position"]["y"]) for row in state["targets"]
                    if row.get("kind") == "npc"}
        path = reachable_trap(resource, state["map"], trap, state["player"]["position"],
                              avoid=occupied, with_path=True)
        for point in path[8:-1:8]:
            if any(row.get("kind") == "npc" and row["position"] == dict(x=point[0], y=point[1])
                   for row in idle(client)["targets"]):
                continue
            go(client, *point, combat=False, running=False)
        go(client, *path[-1], script=f"长安/trap-{trap}.txt", combat=False, running=False)
        after = checkpoint(client, output, f"changan-trap-{trap}-before-prerequisite-refused")
        if after["map"] != before["map"] or after["variables"] != before["variables"]:
            raise AutomationError(f"Chang'an trap {trap} changed the unmet task state")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--assets", type=Path, default=Path("assets"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--route", choices=("inventory", "opening", "observe", "town-gates", "town-rest", "expert-death", "normal-death", "tiemenzhai", "tiemenzhai-gate", "longmen", "changan", "changan-gates", "yixin", "fengxue", "fengxue-win", "fengxue-loss", "fengxue-gate", "zhongdu-arrival", "zhongdu-letter", "zhongdu-xuanci", "zhongdu-mine", "zhongdu-rent", "zhongdu-feiyun", "zhongdu-elopement", "linan-gate", "linan-visit", "fengchi-visit", "fengchi-reunion", "linan-messages", "linan-luyou", "daoxiang-investigate", "daoxiang-report", "daoxiang-paralysis", "fengchi-convention", "yelv-pursuit", "yelv-duel", "fengchi-rescue", "prison-counterattack", "linan-revenge", "zhaojie-manor", "zhaojie-firearms", "zhaojie-loot", "zhaojie-second-battle", "zhongdu-final-reunion", "tianren-arrival", "tianren-first-battle", "tianren-ambush", "tianren-first-gate", "tianren-loot", "tianren-second-entry", "tianren-second-disciples", "tianren-second-battle", "tianren-third-entry", "tianren-third-checks", "tianren-finale", "mine-gate", "exit"), default="opening")
    parser.add_argument("--assist-level", type=int)
    parser.add_argument("--difficulty", type=int, choices=range(3), default=0)
    parser.add_argument("--magic", type=int, choices=range(4), default=0)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--source-run", type=Path)
    parser.add_argument("--load-slot", type=int, choices=range(7))
    args = parser.parse_args()
    if args.assist_level is not None and (args.resume or args.route not in ("tiemenzhai", "changan", "fengxue", "fengxue-win", "zhongdu-arrival", "zhongdu-xuanci", "zhongdu-mine", "zhongdu-elopement", "linan-luyou", "daoxiang-investigate", "fengchi-convention", "prison-counterattack", "zhaojie-firearms", "tianren-arrival", "tianren-first-gate", "tianren-second-disciples", "mine-gate") or not args.source_run or not 4 <= args.assist_level <= 80):
        parser.error("Battle assistance requires a fresh isolated battle-route clone and level 4..80")
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
            parser.error("Resume requires a Xiaoxiang run")
    else:
        if not args.exe or args.source_run and args.load_slot is None:
            parser.error("Fresh run requires --exe; source run requires --load-slot")
        output.mkdir(parents=True, exist_ok=False)
        parent_identity = {}
        if args.source_run:
            parent = args.source_run.resolve()
            parent_identity = json.loads((parent / "run.json").read_text(encoding="utf-8"))
            if parent_identity["resourceId"] != RESOURCE_ID:
                parser.error("Source must be Xiaoxiang")
            relative_slot = Path(SAVE_NAMESPACE) / f"rpg{args.load_slot + 1}"
            original = parent / "user-data/save" / relative_slot
            before = save_hashes(original)
            if "game.ini" not in before:
                parser.error("Source must contain the selected manual save")
            shutil.copytree(original, output / "user-data/save" / relative_slot)
            copied = save_hashes(output / "user-data/save" / relative_slot)
            unchanged = before == copied == save_hashes(original)
            write_json(output / "normal-save-clone.json", dict(parentRun=str(parent), sourceSlot=args.load_slot,
                       fileHashes=before, sourceBeforeEqualsCloneEqualsSourceAfter=unchanged, saveBytesEdited=False))
            if not unchanged:
                parser.error("Source changed or clone bytes differ")
        executable = args.exe.resolve()
        session = "xiaoxiang-" + str(uuid.uuid4())
        command = [str(executable), "--assets", str(assets), "--resource-id", RESOURCE_ID,
                   "--skip-startup-video", "--enable-automation-hooks", "--automation-pipe", session,
                   "--user-data-root", str(output / "user-data"), "--log-file", str(output / "game.log")]
        with (output / "stdout.log").open("wb") as stdout, (output / "stderr.log").open("wb") as stderr:
            process = subprocess.Popen(command, cwd=executable.parent, stdout=stdout, stderr=stderr)
        identity = dict(resourceId=RESOURCE_ID, session=session, pid=process.pid, command=command,
                        engineSha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                        resourceProfileSha256=hashlib.sha256((resource / "game_profile.ini").read_bytes()).hexdigest(),
                        cheatAssisted=bool(args.assist_level or parent_identity.get("cheatAssisted")), started=time.time())
        if parent_identity:
            identity.update(parentRun=str(args.source_run.resolve()), parentSlot=args.load_slot)
            for key in ("silverSaveEdited", "npcLifeSaveEdited", "assistanceRecords"):
                if key in parent_identity:
                    identity[key] = parent_identity[key]
        if args.assist_level:
            identity["assistanceRecords"] = [*identity.get("assistanceRecords", []),
                                             str(output / "native-assistance-proof.json")]
        write_json(output / "run.json", identity)
    started = time.time()
    source_copy = output / f"route-source-{int(started * 1000000)}.py"
    shutil.copyfile(__file__, source_copy)
    dependencies = source_copy.with_suffix(".dependencies")
    dependencies.mkdir()
    for filename in ("gameplay_automation.py", "run_jxqy2_mainline.py", "run_yycs_gameplay.py",
                     "run_chenghe_gameplay.py", "jxqy2_mainline_supplies.py"):
        shutil.copyfile(Path(__file__).with_name(filename), dependencies / filename)
    result = dict(route=args.route, status="running", started=started, resourceId=RESOURCE_ID,
                  fullCoverage=False, fullPlaythrough=False, cheatAssisted=identity["cheatAssisted"],
                  resumed=args.resume, loadSlot=args.load_slot, sourceSnapshot=str(source_copy))
    with Client(identity["session"], timeout=25, transcript=output / "commands.jsonl") as client:
        try:
            if any(row["name"] == "return-to-title" for row in client.observe().get("ui", [])):
                client.ui("Cancel")
            client.act("SetAutoDialogue", enabled=True)
            if args.load_slot is not None:
                load_checkpoint(client, args.load_slot)
            if args.route == "opening":
                opening(client, output, args.difficulty, args.magic)
            elif args.route == "town-gates":
                town_gates(client, output, resource)
            elif args.route == "town-rest":
                town_gates(client, output, resource, gates=False)
            elif args.route == "expert-death":
                expert_death(client, output, resource)
            elif args.route == "normal-death":
                expert_death(client, output, resource, expert=False)
            elif args.route == "tiemenzhai":
                tiemenzhai(client, output, resource, args.assist_level)
            elif args.route == "tiemenzhai-gate":
                tiemenzhai_gate(client, output, resource)
            elif args.route == "longmen":
                longmen(client, output, resource)
            elif args.route == "changan":
                changan(client, output, resource, args.assist_level)
            elif args.route == "yixin":
                yixin(client, output, resource)
            elif args.route == "changan-gates":
                changan_gates(client, output, resource)
            elif args.route == "fengxue":
                fengxue(client, output, resource, args.assist_level)
            elif args.route in ("fengxue-win", "fengxue-loss"):
                fengxue_father(client,output,resource,victory=args.route=="fengxue-win",assist_level=args.assist_level)
            elif args.route == "fengxue-gate":
                fengxue_gate(client,output,resource)
            elif args.route == "zhongdu-arrival":
                zhongdu_arrival(client,output,resource,args.assist_level)
            elif args.route == "zhongdu-letter":
                zhongdu_letter(client,output,resource)
            elif args.route == "zhongdu-xuanci":
                zhongdu_xuanci(client,output,resource,args.assist_level)
            elif args.route == "zhongdu-mine":
                zhongdu_mine(client,output,resource,args.assist_level)
            elif args.route == "zhongdu-rent":
                zhongdu_rent(client,output,resource)
            elif args.route == "zhongdu-feiyun":
                zhongdu_feiyun(client,output,resource)
            elif args.route == "zhongdu-elopement":
                zhongdu_elopement(client,output,resource,args.assist_level)
            elif args.route == "linan-gate":
                linan_gate(client,output,resource)
            elif args.route == "linan-visit":
                linan_visit(client,output,resource)
            elif args.route == "fengchi-visit":
                fengchi_visit(client,output,resource)
            elif args.route == "fengchi-reunion":
                fengchi_reunion(client,output,resource)
            elif args.route == "linan-messages":
                linan_messages(client,output,resource)
            elif args.route == "linan-luyou":
                linan_luyou(client,output,resource,args.assist_level)
            elif args.route == "daoxiang-investigate":
                daoxiang_investigate(client,output,resource,args.assist_level)
            elif args.route == "daoxiang-report":
                daoxiang_report(client,output,resource)
            elif args.route == "daoxiang-paralysis":
                daoxiang_paralysis(client,output,resource)
            elif args.route == "fengchi-convention":
                fengchi_convention(client,output,resource,args.assist_level)
            elif args.route == "yelv-pursuit":
                yelv_pursuit(client,output,resource)
            elif args.route == "yelv-duel":
                yelv_duel(client,output,resource)
            elif args.route == "fengchi-rescue":
                fengchi_rescue(client,output,resource)
            elif args.route == "prison-counterattack":
                prison_counterattack(client,output,resource,args.assist_level)
            elif args.route == "linan-revenge":
                linan_revenge(client,output,resource)
            elif args.route == "zhaojie-manor":
                zhaojie_manor(client,output,resource)
            elif args.route == "zhaojie-firearms":
                zhaojie_firearms(client,output,resource,args.assist_level)
            elif args.route == "zhaojie-loot":
                zhaojie_loot(client,output,resource)
            elif args.route == "zhaojie-second-battle":
                zhaojie_second_battle(client,output,resource)
            elif args.route == "zhongdu-final-reunion":
                zhongdu_final_reunion(client,output,resource)
            elif args.route == "tianren-arrival":
                tianren_arrival(client,output,resource,args.assist_level)
            elif args.route == "tianren-first-battle":
                tianren_first_battle(client,output,resource)
            elif args.route == "tianren-ambush":
                tianren_ambush(client,output,resource)
            elif args.route == "tianren-loot":
                tianren_loot(client,output,resource)
            elif args.route == "tianren-second-entry":
                tianren_second_entry(client,output,resource)
            elif args.route == "tianren-second-battle":
                tianren_second_battle(client,output,resource)
            elif args.route == "tianren-second-disciples":
                tianren_second_disciples(client,output,resource,args.assist_level)
            elif args.route == "tianren-third-entry":
                tianren_third_entry(client,output,resource)
            elif args.route == "tianren-third-checks":
                tianren_third_checks(client,output,resource)
            elif args.route == "tianren-finale":
                tianren_finale(client,output,resource)
            elif args.route == "tianren-first-gate":
                tianren_first_gate(client,output,resource,args.assist_level)
            elif args.route == "mine-gate":
                mine_gate(client,output,resource,args.assist_level)
            elif args.route == "exit":
                client.exit_game()
            else:
                checkpoint(client, output, "observed")
            result.update(status="passed", elapsedSeconds=time.time() - started)
        except Exception as error:
            result.update(status="failed", error=str(error), elapsedSeconds=time.time() - started)
            try:
                checkpoint(client, output, f"failure-{int(started * 1000000)}")
            except Exception as evidence_error:
                result["evidenceError"] = str(evidence_error)
        write_json(output / f"result-{int(started * 1000000)}.json", result)
        print(json.dumps(result, ensure_ascii=False), flush=True)
        if result["status"] != "passed":
            raise SystemExit(1)


if __name__ == "__main__":
    main()
