"""Build and execute the JXQY2 main story through normal gameplay controls."""
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
from jxqy2_mainline_supplies import meditate, restock, select_medicine
from run_jxqy2_gameplay_smoke import (
    opening, checkpoint, item, LIFE_ITEM, MANA_ITEM,
)

STORY_VARIABLES = ("ksOcunzhang", "ChaiSongTalk", "KsOQieHuan", "TMZOQieHuan",
                   "BHGOTalkToYRX", "BaiHuaGuOQieHuan", "BHGOFire", "LMKZOQieHuan",
                   "ChangAnYanRuoXueXiaoShi", "HappyEnding")


def story_state(client):
    state = client.observe(STORY_VARIABLES)
    if state.get("choices"):
        raise AutomationError(f"Unmapped choice: {state.get('choiceMessage')}: {state['choices']}")
    if state.get("scene") == "Title":
        raise AutomationError("Unexpected return to title")
    return state


def idle(client, timeout=180):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        state = story_state(client)
        if "video" in state:
            client.ui("Cancel")
        elif state["worldInput"]:
            return state
        time.sleep(0.5)
    raise TimeoutError(f"Story did not settle: {state.get('map')} / {state.get('script')}")


def verify(client, **variables):
    state = client.observe(tuple(variables))
    assert all((state["variables"].get(key) or "0") == str(value) for key, value in variables.items()), state["variables"]
    return state


def talk(client, name, position=None):
    state = idle(client)
    generation = state["generation"]
    print(f"Talk: {state['map']} / {name}", flush=True)
    # The prison exit has three normal approach traps that stop queued input.
    for attempt in range(4):
        state = idle(client)
        matches = [target for target in state["targets"] if target["name"] == name
                   and (target.get("kind") != "npc" or target.get("interactive", False))
                   and (position is None or target["position"] == dict(x=position[0], y=position[1]))]
        if state["generation"] != generation or len(matches) != 1:
            raise AutomationError(f"Ambiguous interaction {name}: {matches}")
        try:
            client.act("Interact", timeout=185, generation=state["generation"],
                       targetId=matches[0]["id"], running=False, timeoutMs=180000)
            break
        except AutomationError as error:
            if "interaction_not_observed" not in str(error) or attempt == 3:
                raise
            print(f"Retry normal interaction: {name}", flush=True)
            time.sleep(0.3)
    return idle(client)


def continue_practice(client):
    state = idle(client)
    practicing = next((spell for spell in state["magic"]
                       if spell["slot"] == state["layout"]["practiceSlot"]), None)
    if practicing and practicing["level"] < 10:
        return state
    candidates = [spell for spell in state["magic"]
                  if spell["slot"] < state["layout"]["magicQuickBegin"] and spell["level"] < 10]
    if candidates:
        selected = max(candidates, key=lambda spell: (spell["level"], spell["slot"]))
        client.assign_practice(selected["slot"])
        state = idle(client)
        if item(state, selected["file"], "magic")["slot"] != state["layout"]["practiceSlot"]:
            raise AutomationError("Normal practice transfer did not assign the selected magic")
        print(f"Practice: {selected['file']} / level {selected['level']}", flush=True)
    return state


def fight(client, target, skills=None, *, prefer_melee=False):
    state = continue_practice(client)
    # Native ranged retreat requires both its radius and an actual walk action.
    ranged_required = (target.get("attackRadius", 1) >= 5
                       and target.get("hasWalkAction", True))
    if prefer_melee:
        skills = ()
    # Preserve mana for healing between ordinary fights and for larger enemies.
    if skills is None:
        skills = (0,) if target["life"] > 1000 else ()
        clustered = sum(enemy.get("hostile", False) and npc_attackable(enemy)
                        and abs(enemy["position"]["x"] - target["position"]["x"]) * 2
                        + abs(enemy["position"]["y"] - target["position"]["y"]) <= 8
                        for enemy in state.get("targets", []) if "position" in enemy and "position" in target) >= 2
        if clustered:
            area = [spell["slot"] - state["layout"]["magicQuickBegin"]
                    for filename in ("player-magic-洗髓经.ini",)
                    for spell in state["magic"] if spell["file"] == filename and spell["level"] >= 4
                    and state["layout"]["magicQuickBegin"] <= spell["slot"] < state["layout"]["practiceSlot"]]
            if area:
                skills = (*area, 0)
    if ranged_required and not skills:
        skills = (0,)
    if (ranged_required and state["player"]["mana"] * 2 < state["player"]["manaMax"]
            and not select_medicine(state, "mana", require_ready=False)):
        state = meditate(client, allow_combat=True)
    healing = next((entry for entry in state["magic"] if entry["file"] in (
                    "player-magic-白虹贯日.ini", "0player-magic-白虹贯日.ini")
                    and entry["slot"] >= state["layout"]["magicQuickBegin"]), None)
    healing_costs = (30, 42, 54, 66, 78, 86, 90, 102, 118, 120)
    healing_cost = (healing["level"] if healing["file"].startswith("0")
                    else healing_costs[healing["level"] - 1]) if healing else 0
    if (healing and healing.get("cooldownMs", 0) == 0
            and state["player"]["mana"] >= healing_cost
            and state["player"]["lifeMax"] - state["player"]["life"] >= 500):
        try:
            client.act("CastSkill", generation=state["generation"],
                       slot=healing["slot"] - state["layout"]["magicQuickBegin"])
        except AutomationError as error:
            after = client.observe()
            if ("action_not_executed" not in str(error) or after["generation"] != state["generation"]
                    or after["player"]["life"] >= state["player"]["life"]):
                raise
            print("Healing interrupted while taking damage; continue combat", flush=True)
        state = idle(client)
    print(f"Combat: {state['map']} / {target['name']} / {target['life']}", flush=True)
    life_medicine = select_medicine(state, "life", require_ready=False)
    supplies = dict(lifeItem=life_medicine["file"] if life_medicine else LIFE_ITEM, lifePercent=45)
    if skills:
        mana_medicine = select_medicine(state, "mana", require_ready=False)
        if mana_medicine:
            supplies.update(manaItem=mana_medicine["file"], manaPercent=20)
    combat_timeout = 60 if prefer_melee else 240
    deadline = time.monotonic() + combat_timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Combat and normal recovery exceeded their shared timeout")
        try:
            result = client.act("StartCombat", timeout=remaining + 5, generation=state["generation"],
                                targetId=target["id"], radius=20, kills=1, skills=list(skills),
                                allowMeleeFallback=not ranged_required, timeoutMs=max(1000, int(remaining * 1000)), **supplies)
            break
        except AutomationError as error:
            after = idle(client)
            depleted = supplies.get("manaItem")
            mana_depleted = (depleted is not None and str(error) == f"StartCombat: item_depleted: {depleted}"
                             and not any(entry["file"] == depleted and entry["quantity"] > 0
                                         for entry in after["inventory"]))
            replacement = select_medicine(after, "mana", require_ready=False)
            needs_recovery = ("skill_resources_unavailable" in str(error) and ranged_required
                              and after["player"]["mana"] * 2 < after["player"]["manaMax"]
                              and not replacement)
            if (not (mana_depleted or needs_recovery)
                    or after["generation"] != state["generation"]
                    or after["player"]["life"] <= 0
                    or not any(actor["id"] == target["id"] and npc_attackable(actor)
                               for actor in after["targets"])):
                raise
            state = after
            if mana_depleted:
                if replacement:
                    supplies["manaItem"] = replacement["file"]
                else:
                    supplies.pop("manaItem")
                    supplies.pop("manaPercent")
            if not replacement and after["player"]["mana"] * 2 < after["player"]["manaMax"]:
                print(f"Restore mana and continue the same target: {target['name']}", flush=True)
                state = meditate(client, allow_combat=True, timeout=max(1, deadline - time.monotonic()))
    assert result["kills"] == 1, result
    return idle(client)


def _move_script_evidence(client, script):
    trace_path = Path(client.transcript.name).parent / "user-data/automation/trace.jsonl"
    with trace_path.open("rb") as trace:
        trace.seek(0, 2)
        discard_partial = False
        if trace.tell():
            trace.seek(-1, 2)
            discard_partial = trace.read(1) != b"\n"
        pending, executions, completed = b"", set(), False
        while True:
            lines = (pending + trace.read()).split(b"\n")
            pending = lines.pop()
            if discard_partial and lines:
                lines.pop(0)
                discard_partial = False
            for line in lines:
                record = json.loads(line)
                if record.get("eventType") == "script.start" and record.get("virtualPath", "").endswith(script):
                    executions.add(record["executionId"])
                elif record.get("eventType") == "script.finish" and record.get("executionId") in executions:
                    if record.get("status") != "completed":
                        raise AutomationError(f"Move script failed: {script}: {record.get('status')}")
                    completed = True
            yield bool(executions), completed


def go(client, x, y, *, destination=None, script=None, timeout=180, combat=True, running=False, combat_handler=None, stop_when=None):
    state = idle(client)
    generation = state["generation"]
    evidence = _move_script_evidence(client, script) if script else None
    try:
        # Prime the reader before submission so earlier executions cannot satisfy this move.
        if evidence:
            next(evidence)
        print(f"Move: {state['map']} -> {x},{y}" + (f" -> {destination}" if destination else ""), flush=True)
        action = client.submit("MoveTo", generation=generation, x=x, y=y, running=running, timeoutMs=timeout * 1000)
        deadline = time.monotonic() + timeout + 10
        trace_deadline = None
        cancelled_for_script = False
        unexpected_script = None
        occupied_retries = 0
        deferred_targets = set()
        while time.monotonic() < deadline:
            state = story_state(client)
            status = client.request("GetActionStatus", actionId=action)
            if stop_when and state["generation"] == generation and state.get("worldInput") and stop_when(state):
                if status["status"] == "running":
                    client.request("CancelAction", actionId=action)
                return idle(client)
            if status.get("reason") == "world_changed":
                state = story_state(client)
            started, completed = next(evidence) if evidence else (False, False)
            if script and not state.get("outputHealthy", True):
                raise AutomationError(f"Required script trace unavailable: {script}")
            if started and status["status"] == "running" and not cancelled_for_script:
                client.request("CancelAction", actionId=action)
                cancelled_for_script = True
            if (state.get("script") and not state.get("worldInput")
                    and status["status"] == "running" and not cancelled_for_script):
                client.request("CancelAction", actionId=action)
                cancelled_for_script = True
                if not script and not destination:
                    unexpected_script = state["script"]
            changed = state["generation"] != generation or status.get("reason") == "world_changed"
            if changed or status["status"] == "succeeded" or completed or cancelled_for_script:
                state = idle(client, timeout=timeout)
                if unexpected_script:
                    raise AutomationError(f"Unexpected script during movement: {unexpected_script}")
                if evidence:
                    started, completed = next(evidence)
                if destination and state.get("map") != destination:
                    raise AutomationError(f"Unexpected map: {state.get('map')}; expected {destination}")
                if script and not completed:
                    # The batch writer flushes asynchronously. A completed move is
                    # not proof that its requested trap script actually ran.
                    if trace_deadline is None:
                        trace_deadline = time.monotonic() + 3
                    if time.monotonic() >= trace_deadline:
                        raise AutomationError(f"Move did not complete requested script: {script}")
                    time.sleep(0.05)
                    continue
                if (changed or state["generation"] != generation) and not destination and not completed:
                    raise AutomationError(f"Unexpected map change while moving to {x},{y}: {state.get('map')}")
                return state
            if (combat and state["worldInput"] and state["player"].get("canFight", True) and (status["status"] == "running"
                    or status.get("reason") == "blocked_destination")):
                point = state["player"]["position"]
                distance = lambda target: abs(target["position"]["x"] - point["x"]) * 2 + abs(target["position"]["y"] - point["y"])
                threats = [target for target in state["targets"] if target.get("hostile")
                           and target.get("id") not in deferred_targets
                           and npc_attackable(target) and target.get("visibleFromPlayer", True)
                           and distance(target) <= 12]
                if threats:
                    if status["status"] == "running":
                        client.request("CancelAction", actionId=action)
                    target = min(threats, key=distance)
                    try:
                        if combat_handler:
                            combat_handler(client, target)
                        else:
                            fight(client, target)
                    except AutomationError as error:
                        target_finished = False
                        if str(error) in ("StartCombat: invalid_enemy", "StartCombat: stale_target"):
                            after = idle(client)
                            target_finished = (after["generation"] == generation and not any(
                                actor["id"] == target["id"] and npc_attackable(actor)
                                for actor in after["targets"]))
                        if (str(error) in ("StartCombat: target_unreachable", "StartCombat: no_progress")
                                or target_finished):
                            deferred_targets.add(target["id"])
                            print(f"Defer optional combat: {target['name']} / {error}", flush=True)
                            action = client.submit("MoveTo", generation=generation, x=x, y=y,
                                                   running=running, timeoutMs=timeout * 1000)
                            continue
                        if str(error) != "StartCombat: world_changed" or not destination:
                            raise
                        after = idle(client)
                        if after["generation"] != generation and after.get("map") == destination:
                            # Chasing an enemy can cross the requested normal exit.
                            # Reuse the map and completed-script checks above.
                            continue
                        raise
                    # Continue this leg so deferred optional targets stay deferred
                    # after another fight, and retain this move's script evidence.
                    action = client.submit("MoveTo", generation=generation, x=x, y=y,
                                           running=running, timeoutMs=timeout * 1000)
                    deadline = time.monotonic() + timeout + 10
                    continue
            if status["status"] != "running":
                if (status.get("reason") in ("blocked_destination", "no_progress") and state["worldInput"]
                        and occupied_retries < 10 and any(
                             target.get("kind") == "npc" and target.get("hasWalkAction", False)
                             and (status.get("reason") == "blocked_destination" or not target.get("hostile", False))
                            # Moving NPCs also reserve their next tile before arriving.
                            and abs(target["position"]["x"] - x) * 2
                            + abs(target["position"]["y"] - y) <= 3 for target in state["targets"])):
                    occupied_retries += 1
                    time.sleep(0.5)
                    action = client.submit("MoveTo", generation=generation, x=x, y=y,
                                           running=running, timeoutMs=timeout * 1000)
                    continue
                raise AutomationError(f"Move to {x},{y}: {status}")
            if (state["worldInput"] and state["player"]["life"] * 100 < state["player"]["lifeMax"] * 45
                    and any(target.get("hostile") and npc_attackable(target) for target in state["targets"])):
                medicine = select_medicine(state, "life")
                if medicine:
                    client.act("UseItem", generation=state["generation"], slot=medicine["slot"])
            time.sleep(0.1)
        client.request("CancelAction", actionId=action)
        raise TimeoutError(f"Move timed out: {x},{y}")
    finally:
        if evidence:
            evidence.close()


def village_and_town(client, output):
    state = story_state(client)
    assert state["map"] == "主角家-狂沙镇.map"
    for filename in ("player-magic-无影神针.ini", "player-magic-白虹贯日.ini", "player-magic-天意剑诀.ini"):
        state = client.observe()
        spell = item(state, filename, "magic")
        if spell["slot"] < state["layout"]["magicQuickBegin"]:
            client.assign_magic(spell["slot"])
    talk(client, "掌柜2")
    verify(client, ksOcunzhang=1)
    go(client, 97, 171)
    state = story_state(client)
    if state["variables"].get("ksOcunzhang") != "2":
        target = next(target for target in state["targets"] if target["name"] == "火狼" and npc_attackable(target))
        fight(client, target, skills=[0])
    verify(client, ksOcunzhang=2)
    talk(client, "掌柜2")
    state = verify(client, ksOcunzhang=3)
    client.equip(item(state, "goods-jian-2-桃花剑.ini")["slot"])
    client.save_or_load(1)
    from jxqy2_sidequests import early_desert_skill
    early_desert_skill(client, output)
    go(client, 41, 302, destination="狂沙镇.map")
    go(client, 88, 185, script="狂沙镇/柴嵩交谈.txt")
    verify(client, ChaiSongTalk=1)
    talk(client, "老板2")
    verify(client, KsOQieHuan=1)
    client.save_or_load(2)


def begin_story(client, output):
    state = client.observe(("NanGongCaiHong", "ZhangRuMeng", "HeiSha", "BaiSha"))
    if state.get("map") == "主角家.map":
        verify(client, NanGongCaiHong=2, ZhangRuMeng=1, HeiSha=1, BaiSha=1)
    else:
        opening(client, output)
    state = idle(client)
    if not any(entry["file"] == "goods-jian-1-桃木剑.ini" for entry in state["inventory"]):
        talk(client, "宝箱", (16, 15))
    if not any(entry["file"] == LIFE_ITEM for entry in client.observe()["inventory"]):
        talk(client, "宝箱", (37, 57))
    if client.observe()["player"]["money"] == 400:
        talk(client, "宝箱", (28, 31))
    client.assign_magic(item(client.observe(), "player-magic-寒霜掌.ini", "magic")["slot"])
    client.assign_goods(item(client.observe(), LIFE_ITEM)["slot"], 0)
    go(client, 15, 67, script="主角家/trap-3.txt")
    for count in range(4):
        go(client, 14, 87, script="主角家/trap-2.txt")
        verify(client, HomeChoice=min(count + 1, 3))
    state = idle(client)
    assert state["player"]["lifeMax"] == 1791 and state["player"]["manaMax"] == 248, state["player"]
    for filename in ("goods-jian-1-桃木剑.ini", "goods-pifeng-1-白布披风.ini",
                     "goods-cloth-1-书生服.ini", "goods-huwan-2-月勾.ini"):
        client.equip(item(client.observe(), filename)["slot"])
    for filename in ("player-magic-无影神针.ini", "player-magic-白虹贯日.ini", "player-magic-天意剑诀.ini"):
        client.assign_magic(item(client.observe(), filename, "magic")["slot"])
    client.assign_practice(item(client.observe(), "player-magic-天意剑诀.ini", "magic")["slot"])
    checkpoint(client, output, "mainline-equipment-and-practice")
    client.save_or_load(1)


def rescue_yan_ruoxue(client, output):
    state = idle(client)
    if state["map"] == "狂沙镇夜.map":
        assert state["map"] == "狂沙镇夜.map", state.get("map")
        verify(client, ChaiSongTalk=1, KsOQieHuan=1)

        go(client, 129, 57, destination="狂沙镇-铁门寨.map")
        go(client, 12, 10, destination="铁门寨.map")
        go(client, 24, 89, script="铁门寨/进入山寨.txt")
        go(client, 22, 49, script="铁门寨/进入山寨大厅.txt")
        verify(client, TMZOQieHuan=1)
        checkpoint(client, output, "mainline-iron-village")

        go(client, 65, 13, destination="铁门寨-百花谷.map")
        verify(client, TmzBhgOQieHuan=1)
        client.save_or_load(3)
    else:
        assert state["map"] == "铁门寨-百花谷.map", state.get("map")
        verify(client, TMZOQieHuan=1, TmzBhgOQieHuan=1)
    # Four ordinary tigers occupy this passage. Let go clear its approach,
    # instead of pursuing a same-named target through intervening walls.
    go(client, 54, 68, destination="百花谷.map", timeout=300)
    verify(client, BhgOfirstenter=1)
    go(client, 15, 21, script="百花谷/地图陷阱3.txt")
    go(client, 35, 10, destination="百花谷小屋.map")
    verify(client, BaiHuaGuOQieHuan=1)
    go(client, 8, 25, destination="百花谷.map",
       script="百花谷小屋/交谈.txt", timeout=300)
    state = verify(client, BHGOTalkToYRX=1, BaiHuaGuOQieHuan=2, BHGOFire=1)
    assert state["map"] == "百花谷.map", state.get("map")
    checkpoint(client, output, "mainline-rescued-and-signalled")

    go(client, 35, 10, destination="百花谷小屋.map")
    # She has walked away from her original bound position during the scene.
    # There is exactly one Yan Ruoxue on this map; talk rejects ambiguity.
    talk(client, "燕若雪")
    state = verify(client, KsOQieHuan=2)
    assert state["map"] == "狂沙镇.map", state.get("map")
    client.save_or_load(4)
    checkpoint(client, output, "mainline-returned-before-inn-slot4")
    go(client, 97, 187, script="狂沙镇/投店.txt", timeout=300)
    verify(client, KsOQieHuan=3)
    checkpoint(client, output, "mainline-returned-to-town")
    client.save_or_load(3)


def longmen_to_changan(client, output):
    state = idle(client)
    assert state["map"] == "狂沙镇.map", state.get("map")
    verify(client, KsOQieHuan=3)
    go(client, 147, 258, destination="狂沙镇-龙门客栈.map")
    go(client, 19, 25, script="狂沙镇-龙门客栈/对话.txt")
    go(client, 27, 45, destination="龙门客栈.map")
    go(client, 25, 57, script="龙门客栈/柴嵩.txt")
    state = verify(client, LMKZOQieHuan=1)
    state = client.observe(("LMKZOZhanMaGang",))
    assert state["variables"]["LMKZOZhanMaGang"] in ("", "0"), state["variables"]
    checkpoint(client, output, "mainline-longmen-conversation")
    from jxqy2_sidequests import longmen_sidequests
    longmen_sidequests(client, output)
    go(client, 54, 98, destination="龙门客栈-长安.map")
    go(client, 8, 33, script="龙门客栈-长安/聊天.txt")
    go(client, 14, 107, destination="长安.map")
    checkpoint(client, output, "mainline-entered-changan")
    client.save_or_load(4)


def changan_to_fengxue(client, output):
    state = idle(client)
    assert state["map"] in ("长安.map", "长安迷宫.map"), state.get("map")
    if state["map"] == "长安.map" and int(client.observe(("ChangAnCaiSong",))["variables"]["ChangAnCaiSong"] or 0) >= 1:
        return finish_changan_inn(client, output)
    if state["map"] == "长安.map":
        restock(client, life_count=4, mana_count=3, reserve_money=300, max_spend=700)
        state = client.observe(("ChangAnLaoban", "ChangAnXiaofan", "ChangAnYangQiYe", "ChangAnYaYi",
                                "ChangAnYanRuoXueXiaoShi"))
        assert state["variables"]["ChangAnYanRuoXueXiaoShi"] in ("", "0"), state["variables"]
        if state["variables"]["ChangAnLaoban"] in ("", "0"):
            talk(client, "老板", (62, 104))
        verify(client, ChangAnLaoban=1)
        if state["variables"]["ChangAnXiaofan"] in ("", "0"):
            talk(client, "小贩2", (78, 150))
        verify(client, ChangAnXiaofan=1)
        if state["variables"]["ChangAnYangQiYe"] in ("", "0"):
            talk(client, "杨七爷", (61, 184))
        verify(client, ChangAnYangQiYe=1)
        if state["variables"]["ChangAnYaYi"] in ("", "0"):
            talk(client, "衙役", (125, 143))
        verify(client, ChangAnYaYi=1)

        go(client, 120, 124)
        state = idle(client)
        assert state["player"]["canJump"], "Normal inn scene did not enable jumping"
        client.act("JumpTo", generation=state["generation"], x=122, y=124)
        state = idle(client)
        assert state["player"]["position"] == dict(x=122, y=124), state["player"]
        go(client, 130, 124)
        go(client, 150, 109, destination="长安迷宫.map")
    verify(client, ChanAnMGFirstEnter=1)
    checkpoint(client, output, "mainline-official-seal-dungeon")
    client.save_or_load(5)

    # Terrain-checked waypoints through the normal passage. Nearby enemies
    # are handled by go; no total-kill condition is imposed by the seal script.
    for x, y in ((20, 118), (33, 117), (47, 120), (54, 104),
                 (64, 95), (69, 74), (76, 59), (77, 30), (90, 26)):
        go(client, x, y, timeout=300)
    state = idle(client)
    boxes = [target for target in state["targets"]
             if target["kind"] == "object" and target["name"] == "宝箱"
             and target["position"] == dict(x=90, y=25)]
    assert len(boxes) == 1, boxes
    client.interact(boxes[0]["id"], timeout=180)
    idle(client)
    state = verify(client, ChangAnGuanYin=1)
    assert item(state, "goods-sj-4-官印.ini")["quantity"] > 0
    checkpoint(client, output, "mainline-official-seal-obtained")

    for x, y in ((77, 30), (76, 59), (69, 74), (64, 95),
                 (54, 104), (47, 120), (33, 117), (20, 118), (15, 138)):
        go(client, x, y, timeout=300)
    go(client, 12, 143, destination="长安.map")
    go(client, 130, 124)
    go(client, 122, 124)
    state = idle(client)
    client.act("JumpTo", generation=state["generation"], x=120, y=124)
    state = idle(client)
    assert state["player"]["position"] == dict(x=120, y=124), state["player"]

    talk(client, "上官大管家", (146, 216))
    state = verify(client, ChangAnShangGuanGuanJia=1, ChangAnYuGuiFinish=1)
    assert item(state, "goods-sj-5-玉圭.ini")["quantity"] > 0
    talk(client, "杨七爷", (61, 184))
    state = verify(client, ChangAnYangQiYe=2)
    assert item(state, "goods-sj-6-夜明珠.ini")["quantity"] > 0

    finish_changan_inn(client, output)


def finish_changan_inn(client, output):
    state = client.observe(("ChangAnCaiSong", "ChangAnYanRuoXueXiaoShi"))
    phase = int(state["variables"]["ChangAnCaiSong"] or 0)
    if phase == 0:
        go(client, 89, 99, script="长安/trap-5.txt")
        talk(client, "柴嵩", (100, 241))
        verify(client, ChangAnCaiSong=1, ChangAnYanRuoXue=2)
        phase = 1
    if phase == 1:
        # Leave the drinking table and follow normal connected streets in short
        # legs; a single cross-city path from the table failed to make progress.
        for x, y in ((99, 245), (97, 242), (94, 235), (89, 223), (82, 207),
                     (81, 185), (73, 158), (73, 120), (72, 101), (79, 86), (83, 86)):
            go(client, x, y)
        go(client, 89, 99, script="长安/trap-5.txt")
    verify(client, ChangAnCaiSong=2)
    if state["variables"]["ChangAnYanRuoXueXiaoShi"] in ("", "0"):
        talk(client, "柴嵩", (100, 245))
    verify(client, ChangAnYanRuoXueXiaoShi=1)
    checkpoint(client, output, "mainline-searching-for-yan")
    client.save_or_load(5)

    go(client, 26, 281, destination="长安西郊.map")
    go(client, 0, 46, destination="风雪山庄.map")
    verify(client, ChangAnYanRuoXueXiaoShi=1)
    state = client.observe(("FengXueShanZhuanFinish",))
    assert state["variables"]["FengXueShanZhuanFinish"] in ("", "0"), state["variables"]
    checkpoint(client, output, "mainline-entered-fengxue")
    client.save_or_load(5)


EARLY_CHAPTERS = {
    "rescue": rescue_yan_ruoxue,
    "longmen": longmen_to_changan,
    "changan": changan_to_fengxue,
}



def fengxue_to_bieli(client, output, *, wind_magic_file="player-magic-风雪狂刀.ini",
                     family_guard_count=13, official_count=18):
    state = idle(client)
    assert state["map"] == "风雪山庄.map", state.get("map")
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
    client.assign_magic(item(state, wind_magic_file, "magic")["slot"], quick_slot=3)
    talk(client, "柴嵩", (88, 95))
    talk(client, "赵无双", (89, 93))
    checkpoint(client, output, "middle-fengxue-skill-learned")
    checkpoint(client, output, "middle-fengxue-release")
    go(client, 93, 13, destination="长安西郊.map")
    go(client, 19, 9, destination="长安.map")
    client.save_or_load(3)
    checkpoint(client, output, "middle-changan-fight-source-slot3")
    from jxqy2_sidequests import changan_sidequests
    changan_sidequests(client, output, family_guard_count=family_guard_count, official_count=official_count)
    go(client, 153, 309, destination="别离村.map")
    client.save_or_load(2)


def cuiyan_first_visit(client, output, *, dingshen_magic_file="player-magic-定身法.ini",
                       scroll_reward_variable=None):
    state = idle(client)
    if state["map"] not in ("别离村.map", "翠烟门.map", "别离村-翠烟门.map"):
        raise AutomationError("First Cuiyan visit requires Bieli or the first Cuiyan entrance")
    state = client.observe(("CuiYanMen2Finish", "CuiYanMen2CunLan"))
    if (state["variables"].get("CuiYanMen2Finish") or "0") != "0":
        raise AutomationError("First Cuiyan visit is unavailable after the Tang Ying escort starts")
    if state["map"] == "别离村-翠烟门.map":
        if (state["variables"].get("CuiYanMen2CunLan") != "1"
                or not any(entry["file"] == "goods-sj-7-唐门宝箱.ini" and entry["quantity"] > 0
                           for entry in state["inventory"])):
            raise AutomationError("Resuming the Cuiyan return requires Chunlan's delivery and its box")
    else:
        if state["map"] == "别离村.map":
            from jxqy2_sidequests import bieli_sidequests
            bieli_sidequests(client, output, dingshen_magic_file=dingshen_magic_file,
                             scroll_reward_variable=scroll_reward_variable)
            go(client, 20, 163, destination="别离村-翠烟门.map")
            go(client, 3, 155, destination="翠烟门.map")
        # Chunlan's delivery scene brings Qiu outside the still-closed gate.
        talk(client, "春兰", (127, 95))
        state = verify(client, CuiYanMen2CunLan=1)
        assert item(state, "goods-sj-7-唐门宝箱.ini")["quantity"] > 0
        talk(client, "秋依水", (128, 96))
        checkpoint(client, output, "middle-cuiyan-delivery")
        go(client, 146, 61, destination="别离村-翠烟门.map")
    go(client, 67, 24, destination="别离村.map")
    from jxqy2_mainline_supplies import meditate
    go(client, 60, 60, combat=False)
    meditate(client)
    go(client, 82, 95, destination="别离村-唐门.map")
    client.save_or_load(2)


def tangmen_capture(client, output, *, rain_magic_file="player-magic-漫天花雨手法.ini",
                    healing_magic_file="player-magic-白虹贯日.ini",
                    healing_costs=(30, 42, 54, 66, 78, 86, 90, 102, 118, 120),
                    capture_timeout=300):
    state = idle(client)
    if state["map"] == "唐门.map":
        state = client.observe(("TanMenFighting", "CuiYanMen2Finish"))
        if (state["variables"].get("TanMenFighting") != "0"
                or state["variables"].get("CuiYanMen2Finish") != "1"):
            raise AutomationError("Tangmen recovery requires the completed capture scene")
    else:
        if state["map"] != "别离村-唐门.map":
            raise AutomationError("Tangmen capture requires its approach map or the completed capture")
        go(client, 12, 13, destination="唐门.map", combat=False)
        state = verify(client, TanMenFighting=1)
        # Use a single explicit target: its death script starts the capture.
        bosses = [target for target in state["targets"]
                  if target["name"] == "唐萧" and target.get("hostile") and npc_attackable(target)]
        if len(bosses) != 1:
            raise AutomationError(f"Expected one initial Tang Xiao: {bosses}")
        fight(client, bosses[0], skills=())
        state = client.observe(("TanMenFighting", "CuiYanMen2Finish"))
        if (state.get("worldAction") or {}).get("status") == "running":
            client.request("CancelAction", actionId=state["worldAction"]["actionId"])
        # Let the normal enemy attacks cause the scripted capture.
        # Killing Tang Li instead is a game-over branch.
        deadline = time.monotonic() + capture_timeout
        while time.monotonic() < deadline:
            state = client.observe(("TanMenFighting", "CuiYanMen2Finish"))
            if state.get("scene") == "Title":
                raise AutomationError("Tangmen returned to title instead of scripted capture")
            if state.get("choices"):
                raise AutomationError(f"Unexpected Tangmen choice: {state['choices']}")
            if (state.get("worldInput") and state.get("variables", {}).get("TanMenFighting") == "0"
                    and state["variables"].get("CuiYanMen2Finish") == "1"):
                break
            time.sleep(0.1)
        else:
            raise TimeoutError("Tangmen scripted capture did not complete")
    state = verify(client, TanMenFighting=0, CuiYanMen2Finish=1)
    checkpoint(client, output, "middle-tangmen-scripted-defeat")
    if state["player"]["life"] == 0:
        medicine = select_medicine(state, "life")
        if medicine is None:
            raise AutomationError("Tangmen capture left zero life with no ready healing medicine")
        quantity = sum(entry["quantity"] for entry in state["inventory"]
                       if entry["file"] == medicine["file"])
        generation = state["generation"]
        client.act("UseItem", generation=generation, slot=medicine["slot"])
        state = idle(client)
        remaining = sum(entry["quantity"] for entry in state["inventory"]
                        if entry["file"] == medicine["file"])
        if (state["map"] != "唐门.map" or state["generation"] != generation
                or state["player"]["life"] <= 0 or remaining != quantity - 1):
            raise AutomationError("Tangmen medicine did not consume one dose and restore life")
        print(json.dumps(dict(event="tangmen.capture-recovery", medicine=medicine["file"],
                              quantityBefore=quantity, quantityAfter=remaining,
                              lifeBefore=0, lifeAfter=state["player"]["life"]),
                         ensure_ascii=False), flush=True)
    state = client.observe(("NaDaoMiJiMusic",))
    if state["variables"].get("NaDaoMiJiMusic") != "1":
        talk(client, "唐影", (109, 171))
    state = verify(client, NaDaoMiJiMusic=1)
    assert item(state, "book-唐门秘笈.ini")["quantity"] > 0
    talk(client, "唐离", (82, 123))
    # Keep the book unconsumed: the exit script explicitly requires it.
    go(client, 47, 296, destination="别离村-唐门.map")
    state = idle(client)
    client.act("UseItem", generation=state["generation"],
               slot=item(state, "book-唐门秘笈.ini")["slot"])
    state = idle(client)
    assert item(state, rain_magic_file, "magic")["level"] >= 1
    go(client, 1, 53, destination="别离村.map")
    go(client, 60, 60, combat=False)
    state = meditate(client)
    generation = state["generation"]
    for _ in range(8):
        if state["player"]["life"] >= state["player"]["lifeMax"]:
            break
        healing = next((spell for spell in state["magic"]
                        if spell["file"] == healing_magic_file
                        and spell["slot"] >= state["layout"]["magicQuickBegin"]), None)
        if healing is None or not 1 <= healing["level"] <= len(healing_costs):
            raise AutomationError("Tangmen recovery requires Baihong in a quick slot")
        cost = healing_costs[healing["level"] - 1]
        if state["player"]["mana"] < cost:
            state = meditate(client)
        state = client.wait_until(
            lambda snapshot: snapshot["generation"] != generation or (
                snapshot.get("worldInput") and any(
                    spell["file"] == healing["file"] and spell["slot"] == healing["slot"]
                    and spell.get("cooldownMs", 0) == 0 for spell in snapshot["magic"])),
            timeout=10, description="Baihong recovery cooldown")
        if (state["map"] != "别离村.map" or state["generation"] != generation
                or not state.get("worldInput") or state["player"]["mana"] < cost):
            raise AutomationError("Tangmen recovery lost its safe village casting state")
        before = dict(state["player"])
        client.act("CastSkill", generation=generation,
                   slot=healing["slot"] - state["layout"]["magicQuickBegin"])
        state = idle(client)
        if (state["generation"] != generation or state["map"] != "别离村.map"
                or state["player"]["life"] <= before["life"]):
            raise AutomationError("Tangmen Baihong recovery did not restore life")
        print(json.dumps(dict(event="tangmen.baihong-recovery", lifeBefore=before["life"],
                              lifeAfter=state["player"]["life"], manaBefore=before["mana"],
                              manaAfter=state["player"]["mana"]), ensure_ascii=False), flush=True)
    if state["player"]["life"] < state["player"]["lifeMax"]:
        raise AutomationError("Tangmen recovery did not reach full life within eight casts")
    meditate(client)
    checkpoint(client, output, "middle-tangmen-recovered")
    go(client, 20, 163, destination="别离村-翠烟门.map")
    client.save_or_load(2)


def cuiyan_second_visit(client, output, *, flower_magic_file="player-magic-花飞蝶舞剑.ini"):
    state = idle(client)
    if state["map"] == "翠烟门.map":
        state = client.observe(("CuiYanMen2Finish", "CuiYanMen2Fighting"))
        if (state["variables"].get("CuiYanMen2Finish") != "2"
                or state["variables"].get("CuiYanMen2Fighting") != "2"):
            raise AutomationError("Cuiyan recovery requires the active investigation")
    else:
        if state["map"] != "别离村-翠烟门.map":
            raise AutomationError("Cuiyan investigation requires its approach or courtyard")
        go(client, 3, 155, destination="翠烟门.map", combat=False)
        state = verify(client, CuiYanMen2Finish=2, CuiYanMen2Fighting=1)
        bosses = [target for target in state["targets"]
                  if target["name"] == "高级女剑客1"
                  and target["position"] == {"x": 110, "y": 134} and target.get("hostile")]
        if len(bosses) != 1:
            # The guard may already have moved toward the player after the cutscene.
            bosses = [target for target in state["targets"]
                      if target["name"] == "高级女剑客1" and target.get("hostile")]
        if len(bosses) != 1:
            raise AutomationError(f"Cannot identify Cuiyan gatekeeper: {bosses}")
        fight(client, bosses[0], skills=())
    verify(client, CuiYanMen2Fighting=2)
    count = int(client.observe(("CuiYanMen2DiZi",))["variables"].get("CuiYanMen2DiZi") or 0)
    completed = set()
    if count:
        records = late_records(output, "trace.jsonl", completed_scripts=("翠烟门/春兰2.txt",))
        baseline = late_completed_script(records, "翠烟门/春兰2.txt")
        records = records[records.index(baseline):]
        if any(record.get("eventType") in ("map.change", "session.start") for record in records):
            raise AutomationError("Cuiyan investigation evidence predates a map change or load")
        starts = {record["executionId"]: record.get("virtualPath", "") for record in records
                  if record.get("eventType") == "script.start"}
        completed = {starts.get(record["executionId"]) for record in records
                     if record.get("eventType") == "script.finish" and record.get("status") == "completed"}
        completed &= {f"script/map/翠烟门/弟子{index}.txt" for index in range(1, 7)}
        if len(completed) != count:
            raise AutomationError("Cuiyan witness count lacks distinct completed script evidence")
    # These scripts have no repeat guard. Resume only the distinct witnesses
    # still missing from this investigation's trace; disciple 6 is stationary.
    for name, position, index in (("小姐", (81, 138), 1), ("小姐", (69, 170), 2),
                                  ("女子抚琴", (93, 172), 5), ("小姐1", (100, 202), 4),
                                  ("普通女剑客2", (90, 237), 6)):
        if f"script/map/翠烟门/弟子{index}.txt" in completed:
            continue
        talk(client, name, position)
        count += 1
        verify(client, CuiYanMen2DiZi=count)
    state = client.observe(("CuiYanMen2DiZi",))
    assert int(state["variables"]["CuiYanMen2DiZi"]) >= 5
    talk(client, "秋依水", (91, 120))
    verify(client, CuiYanMen2AllFinish=1, CuiYanMen2Fighting=3)
    talk(client, "唐影", (91, 119))
    checkpoint(client, output, "middle-cuiyan-investigation")
    go(client, 146, 61, destination="别离村-翠烟门.map")
    verify(client, CuiYanMen2AllFinish=2)
    state = idle(client)
    client.act("UseItem", generation=state["generation"],
               slot=item(state, "book-翠烟门秘笈.ini")["slot"])
    state = idle(client)
    assert item(state, flower_magic_file, "magic")["level"] >= 1
    go(client, 67, 24, destination="别离村.map")
    verify(client, CuiYanMen2AllFinish=3)
    talk(client, "船夫", (77, 169))
    state = verify(client, HanYanFirstEnter=1)
    assert state["map"] == "汉阳.map"
    client.save_or_load(2)


def hanyang_tianwang(client, output, *, magic_file_prefix="player-magic-", yifeng_chest_required=True,
                    jump_to_guard=False):
    state = idle(client)
    if state["map"] == "段家庄.map":
        duan_family_side_story(client, output, magic_file_prefix=magic_file_prefix)
        client.save_or_load(2)
        return
    if state["map"] == "汉阳.map":
        state = client.observe(("HanYanCTMSZL", "TianWangPiEr"))
        if state["variables"].get("HanYanCTMSZL") == "3" or state["variables"].get("TianWangPiEr") == "4":
            if (state["variables"].get("HanYanCTMSZL") != "3"
                    or state["variables"].get("TianWangPiEr") != "4"
                    or not any(spell["file"] == magic_file_prefix + "大力金刚掌.ini" and spell["level"] >= 1
                               for spell in state["magic"])):
                raise AutomationError("Hanyang recovery requires delivered letter and learned Dali skill")
            duan_family_side_story(client, output, magic_file_prefix=magic_file_prefix)
            client.save_or_load(2)
            return
    if state["map"] == "天王岛-地下迷宫.map":
        state = client.observe(("TianWangPiEr",))
        if state["variables"].get("TianWangPiEr") != "2":
            raise AutomationError("Tianwang maze recovery requires the unopened quest chest stage")
    else:
        assert state["map"] == "汉阳.map", state.get("map")
        if state["player"]["position"] == dict(x=42, y=29):
            assert state["player"]["canJump"], "Cannot leave the Hanyang arrival boat"
            client.act("JumpTo", timeout=30, generation=state["generation"],
                       x=42, y=35, timeoutMs=25000)
            state = idle(client)
            if state["player"]["position"] != dict(x=42, y=35):
                raise AutomationError(f"Hanyang boat jump landed unexpectedly: {state['player']['position']}")
        if yifeng_chest_required and not any(spell["file"] == magic_file_prefix + "依风剑法.ini" for spell in state["magic"]):
            # This chest is on a separate bank, reached through the normal jump input.
            assert client.observe()["player"]["canJump"], "Cannot reach the Yifeng chest bank"
            late_jump(client, (53, 13), (45, 13))
            go(client, 38, 10)
            talk(client, "宝箱", (37, 9))
            assert item(idle(client), magic_file_prefix + "依风剑法.ini", "magic")["level"] >= 1
            late_jump(client, (45, 13), (53, 13))
            checkpoint(client, output, "side-yifeng-learned")
        restock(client)
        meditate(client)
        talk(client, "趟子手2", (76, 116))
        talk(client, "史忠良", (89, 84))
        verify(client, HanYanCTMSZL=1)
        talk(client, "楚天盟弟子乙", (87, 75))
        verify(client, HanYanCTMHouMenDiZi=2)
        talk(client, "天王帮弟子", (85, 71))
        talk(client, "天王帮弟子", (41, 37))
        state = verify(client, HanYanTWBDiZi=1)
        assert state["map"] == "天王岛.map"
        talk(client, "苹儿", (57, 77))
        verify(client, TianWangPiEr=1)
        talk(client, "苹儿", (74, 69))
        verify(client, TianWangPiEr=2)
        # The second conversation gives this ladder its usable normal script.
        late_jump(client, (21, 115), (29, 115))
        talk(client, "梯子2", (36, 112))
    state = idle(client)
    assert state["map"] == "天王岛-地下迷宫.map"
    go(client, 7, 43, combat=False)
    quantity = sum(entry["quantity"] for entry in idle(client)["inventory"])
    talk(client, "宝箱", (7, 42))
    state = verify(client, TianWangPiEr=3)
    assert sum(entry["quantity"] for entry in state["inventory"]) == quantity + 1
    checkpoint(client, output, "middle-tianwang-maze-chest")
    go(client, 61, 26, combat=False)
    talk(client, "梯子", (61, 25))
    late_jump(client, (29, 115), (21, 115))
    talk(client, "苹儿", (45, 75))
    state = verify(client, TianWangPiEr=4, TianWanShouMenDiZi=1, HanYanCTMSZL=2)
    assert item(state, "goods-sj-8-天王岛密函.ini")["quantity"] > 0
    client.assign_magic(item(state, magic_file_prefix + "大力金刚掌.ini", "magic")["slot"], quick_slot=3)
    # Preserve the island NPC positions after the maze through the normal save menu.
    client.save_or_load(2)
    if jump_to_guard:
        # The courtyard doorway is occupied; use normal jumps over the NPC and water.
        late_jump(client, (70, 41), (71, 37))
        for point in ((71, 29), (76, 24), (81, 24)):
            go(client, *point, combat=False)
        late_jump(client, (82, 21), (84, 21))
    talk(client, "守门弟子", (85, 21))
    talk(client, "史忠良", (89, 84))
    verify(client, HanYanCTMSZL=3)
    checkpoint(client, output, "middle-hanyang-letter-delivered")
    duan_family_side_story(client, output, magic_file_prefix=magic_file_prefix)
    client.save_or_load(2)


def prepare_duan_magic(client, output, *, magic_file_prefix="player-magic-"):
    state = idle(client)
    files = tuple(magic_file_prefix + name + ".ini" for name in ("天意剑诀", "寒霜掌", "风雪狂刀"))
    spells = {spell["file"]: spell for spell in state["magic"] if spell["file"] in files}
    if (len(spells) != 3 or spells[files[0]]["level"] < 5 or not 1 <= spells[files[2]]["level"] < 10
            or spells[files[0]]["slot"] != state["layout"]["practiceSlot"]
            or spells[files[1]]["slot"] != state["layout"]["magicQuickBegin"]):
        return state
    checkpoint(client, output, "side-duan-magic-before")
    client.assign_magic(spells[files[0]]["slot"], quick_slot=0)
    state = client.observe()
    client.assign_practice(item(state, files[2], "magic")["slot"])
    state = idle(client)
    after = [spell for spell in state["magic"] if spell["file"] in files]
    if (len(after) != 3 or {spell["file"] for spell in after} != set(files)
            or item(state, files[0], "magic")["slot"] != state["layout"]["magicQuickBegin"]
            or item(state, files[2], "magic")["slot"] != state["layout"]["practiceSlot"]):
        raise AutomationError("Duan magic preparation did not preserve skills and assign Tianyi/Fengxue")
    checkpoint(client, output, "side-duan-magic-prepared")
    return state


def duan_family_side_story(client, output, *, magic_file_prefix="player-magic-"):
    state = idle(client)
    if state["map"] not in ("汉阳.map", "段家庄.map"):
        raise AutomationError("Duan family route requires Hanyang or its active courtyard")
    state = client.observe(("HanYanCTMSZL", "DuanJiaZhuangClose", "DuanHuanShan"))
    assert state["variables"].get("HanYanCTMSZL") == "3", state["variables"]
    if state["variables"].get("DuanJiaZhuangClose") == "1":
        raise AutomationError("Duan family route closed after the prison escape")
    if state["map"] == "段家庄.map" and (state["variables"].get("DuanHuanShan") or "0") != "0":
        raise AutomationError("Duan courtyard recovery requires its undefeated stage")
    if any(spell["file"] == magic_file_prefix + "大梦心法.ini" for spell in state["magic"]):
        if state["map"] != "汉阳.map":
            raise AutomationError("Completed Duan family route must resume in Hanyang")
        return
    if state["variables"].get("DuanHuanShan") != "1":
        prepare_duan_magic(client, output, magic_file_prefix=magic_file_prefix)
        if state["map"] == "汉阳.map":
            restock(client)
            meditate(client)
            go(client, 1, 153, destination="汉阳-段家庄.map")
            go(client, 6, 53, destination="段家庄.map")
        # These waypoints follow the courtyard passage; the direct line crosses walls.
        approach = ((84, 42), (78, 60), (76, 82), (75, 110), (73, 137),
                    (64, 148), (49, 148), (42, 131), (43, 102), (53, 93))
        for point in approach:
            go(client, *point, timeout=300, combat=False)
        state = client.observe(("DuanHuanShan",))
        if state["variables"].get("DuanHuanShan") != "1":
            fight(client, late_target(client, "段环山", (56, 91)))
        verify(client, DuanHuanShan=1)
        checkpoint(client, output, "side-duan-family-defeated")
        for point in reversed(approach[:-1]):
            go(client, *point, timeout=300, combat=False)
        go(client, 92, 23, destination="汉阳-段家庄.map", combat=False)
        go(client, 29, 14, destination="汉阳.map", combat=False)
    talk(client, "史忠良", (89, 84))
    state = verify(client, DuanHuanShan=0, HanYanCTMSZL=3)
    assert item(state, magic_file_prefix + "大梦心法.ini", "magic")["level"] >= 1
    checkpoint(client, output, "side-dameng-learned")


def meng_sparring(client, output, *, magic_file_prefix="player-magic-"):
    state = verify(client, HanYanCTMSZL=3, DuanHuanShan=0)
    if (state["map"] != "汉阳.map"
            or not any(spell["file"] == magic_file_prefix + "大梦心法.ini" for spell in state["magic"])):
        raise AutomationError("Meng sparring requires the completed Tianwang and Duan family quests")
    checkpoint(client, output, "side-meng-before-sparring")
    evidence = _move_script_evidence(client, "汉阳小屋/die.txt")
    next(evidence)
    try:
        talk(client, "孟廷威", (66, 130))
        state = verify(client, HanYanBXBMTW=1)
        if state["map"] != "汉阳小屋.map":
            raise AutomationError("Meng sparring did not enter the normal challenge room")
        targets = [target for target in state["targets"]
                   if target["kind"] == "npc" and target["name"] == "孟廷威"
                   and target.get("hostile") and npc_attackable(target)]
        if len(targets) != 1:
            raise AutomationError(f"Expected one living Meng sparring opponent: {targets}")
        # fight verifies StartCombat kills=1 and selects range from the observed NPC.
        state = fight(client, targets[0])
        if (state["map"] != "汉阳.map" or state["player"]["life"] <= 0
                or state["player"]["position"] != dict(x=65, y=128)):
            raise AutomationError("Meng sparring did not return alive through its normal victory scene")
        if not state.get("outputHealthy", True):
            raise AutomationError("Meng sparring victory trace is unavailable")
        deadline = time.monotonic() + 3
        while not next(evidence)[1]:
            if time.monotonic() >= deadline:
                raise AutomationError("Missing completed Meng victory script; the start flag is not a win")
            time.sleep(0.05)
    finally:
        evidence.close()
    return checkpoint(client, output, "side-meng-sparring-won")


def hanyang_to_zhongdu(client, output, *, magic_file_prefix="player-magic-", camp_source_slot=None):
    state = verify(client, HanYanCTMSZL=3)
    if state["map"] == "金兵营寨.map":
        if (not any(spell["file"] == magic_file_prefix + "大梦心法.ini" and spell["level"] >= 1
                    for spell in state["magic"])
                or not (output / "side-meng-sparring-won.json").is_file()):
            raise AutomationError("Jin camp recovery requires Dameng and the completed Meng sparring checkpoint")
        late_records(output, "trace.jsonl", completed_scripts=("汉阳小屋/die.txt",))
    else:
        assert state["map"] == "汉阳.map", state.get("map")
        restock(client, mana_count=6)
        meditate(client)
        meng_sparring(client, output, magic_file_prefix=magic_file_prefix)
        meditate(client)
        go(client, 79, 61, destination="汉阳-金兵营寨.map")
        go(client, 32, 20, destination="金兵营寨.map", combat=False)
    checkpoint(client, output, "middle-jin-camp-entry")
    if camp_source_slot is not None:
        client.save_or_load(camp_source_slot)
        checkpoint(client, output, "middle-jin-camp-normal-branch-source")
    go(client, 97, 6, destination="汉阳-中都1.map", combat=False)
    if any(target["name"] == "老人" for target in idle(client)["targets"]):
        talk(client, "老人", (4, 12))
    late_jump(client, (10, 47), (18, 47))
    go(client, 24, 37, destination="汉阳-中都2.map")
    go(client, 16, 24, destination="中都.map")
    checkpoint(client, output, "middle-zhongdu-arrival")
    client.save_or_load(2)


MIDDLE_CHAPTERS = {
    "fengxue": fengxue_to_bieli,
    "cuiyan-first": cuiyan_first_visit,
    "tangmen": tangmen_capture,
    "cuiyan-second": cuiyan_second_visit,
    "tianwang": hanyang_tianwang,
    "to-zhongdu": hanyang_to_zhongdu,
}




LATE_VARIABLES = (
    "ZhongDuLYS", "ZhongDuTroublesRoom", "ZhongDuZhuangYuanDoor",
    "ZhongDuHouHuaYuan", "LinAnJieEr", "LinAnYanRuoXue", "LinAnChaiSong",
    "FromFengChi", "FengChiFight", "FengChiKill", "FCBW", "HappyEnding", "LinAnDaLao11", "ShaoJiFeng",
    "LinAnDie", "AnZang", "TanHua", "LinAnMianJu", "ToZhongDu",
    "LongYinShiLiaoRan", "JieTouXiaoFan", "TrjDxmg", "TrjDxmgshijiang",
    "TrjDxmgKg",
)


def late_target(client, name, position=None, *, kind="npc"):
    state = idle(client)
    candidates = [target for target in state["targets"]
                  if target["name"] == name and target["kind"] == kind
                  and (kind != "npc" or target.get("action") not in (11, 255))]
    if position is not None:
        exact = [target for target in candidates
                 if target["position"] == dict(x=position[0], y=position[1])]
        if exact:
            candidates = exact
        # A unique named actor may have walked from its resource position.
        # Duplicate names must still resolve at their known initial coordinate.
    if len(candidates) != 1:
        raise AutomationError(f"Cannot identify {name} at {position}: {candidates}")
    return candidates[0]


def late_object(client, x, y):
    state = idle(client)
    matches = [target for target in state["targets"]
               if target["kind"] == "object" and target["position"] == dict(x=x, y=y)]
    if len(matches) != 1:
        raise AutomationError(f"Expected one object at {x},{y}: {matches}")
    client.act("Interact", timeout=185, generation=state["generation"],
               targetId=matches[0]["id"], running=False, timeoutMs=180000)
    return idle(client)


def late_jump(client, start, destination):
    go(client, *start)
    state = idle(client)
    client.act("JumpTo", timeout=30, generation=state["generation"],
               x=destination[0], y=destination[1], timeoutMs=25000)
    state = idle(client)
    if state["player"]["position"] != dict(x=destination[0], y=destination[1]):
        raise AutomationError(f"Night jump landed unexpectedly: {state['player']['position']}")


def late_enter_night_garden(client):
    # Static candidates: all crossed tiles and cardinal side tiles allow jump;
    # no scripted gate/exit is crossed. Real NPC/object collisions remain active.
    late_jump(client, (114, 335), (122, 335))
    late_jump(client, (130, 323), (138, 323))


def late_leave_night_garden(client):
    late_jump(client, (138, 323), (130, 323))
    late_jump(client, (122, 335), (114, 335))


def zhongdu_night_main_gate(client, output):
    for point in ((138, 303), (148, 303), (158, 303), (167, 301), (168, 282),
                  (168, 262), (172, 250), (176, 239), (174, 230), (172, 222),
                  (165, 229), (161, 240), (155, 248), (151, 261), (141, 261)):
        go(client, *point, timeout=300)
    go(client, 140, 266, script="中都夜/trap-10.txt")
    checkpoint(client, output, "side-zhongdu-main-gate-opened")


def late_checkpoint(client, output, name):
    state = client.observe(LATE_VARIABLES)
    (output / f"{name}-variables.json").write_text(
        json.dumps(state, ensure_ascii=False, indent=2), encoding="utf-8")
    checkpoint(client, output, name)
    client.save_or_load(5)


def zhongdu_shaolin_side_story(client, output, *, magic_file_prefix="player-magic-",
                               second_boss_name=None, captive_money_range=(9000, 20000)):
    state = idle(client)
    assert state["map"] in ("中都.map", "中都地下迷宫.map")
    resumed = state["map"] == "中都地下迷宫.map"
    stage = int(client.observe(("ZDBiWu",))["variables"].get("ZDBiWu") or 0)
    if stage == 0:
        go(client, 117, 184, script="中都/地图陷阱11.txt", combat=False)
        verify(client, ZDBiWu=1)
        stage = 1
    if stage == 1:
        go(client, 124, 175, script="中都/trap-14.txt", combat=False)
        verify(client, ZDBiWu=2)
        stage = 2
    if stage == 2:
        # Resolve after the scripted jump and challenge; the actor was neutral before it.
        fight(client, late_target(client, "玄慈", (132, 162)))
    state = verify(client, ZDBiWu=3)
    assert item(state, magic_file_prefix + "金刚不坏神功.ini", "magic")["level"] >= 1
    checkpoint(client, output, "side-jingang-learned")
    if state["map"] == "中都.map":
        go(client, 150, 86, destination="中都地下迷宫.map")
    approach = ((32, 112), (43, 120), (50, 136), (60, 146),
                (71, 140), (77, 158), (87, 168))
    if not resumed:
        for point in approach[:-2]:
            go(client, *point, timeout=300, combat=point != (71, 140))
    state = idle(client)
    blockers = [target for target in state["targets"] if target["kind"] == "npc"
                and target["name"] == "打手" and target.get("hostile") and npc_attackable(target)
                and target["position"] == dict(x=75, y=142)]
    if len(blockers) > 1:
        raise AutomationError(f"Ambiguous underground doorway blocker: {blockers}")
    # The occupants move from their original spawn positions. Remember the
    # current doorway actor before approaching, not the nearer man behind him.
    introduction = ("中都地下迷宫/欧阳桐.txt" if second_boss_name is not None
                    and client.observe(("linjian",))["variables"].get("linjian") in (None, "", "0") else None)
    go(client, 74, 141, combat=False, script=introduction)
    if blockers:
        current = idle(client)
        matches = [target for target in current["targets"] if target["id"] == blockers[0]["id"]
                   and target.get("hostile") and npc_attackable(target)]
        if current["generation"] != state["generation"] or len(matches) != 1:
            raise AutomationError("Underground doorway blocker changed before combat")
        fight(client, matches[0])
    for point in approach[-2:]:
        go(client, *point, timeout=300, combat=False)
    state = client.observe(("ZDBiWu",))
    if (state["variables"].get("ZDBiWu") != "4"
            and (second_boss_name is None
                 or client.observe(("OuYangDie",))["variables"].get("OuYangDie") != "1")):
        fight(client, late_target(client, "欧阳桐", (91, 167)))
    if second_boss_name is not None and client.observe(("ZDBiWu",))["variables"].get("ZDBiWu") != "4":
        verify(client, ZDBiWu=3, OuYangDie=1)
        checkpoint(client, output, "side-ouyang-first-form-defeated")
        fight(client, late_target(client, second_boss_name, (91, 167)))
    verify(client, ZDBiWu=4, OuYangMusic=1)
    # His death script opens the cell. Each rescued woman has a separate one-shot reward.
    go(client, 80, 159)
    money = client.observe()["player"]["money"]
    talk(client, "女子1", (80, 135))
    assert captive_money_range[0] <= client.observe()["player"]["money"] - money <= captive_money_range[1]
    quantity = sum(entry["quantity"] for entry in client.observe()["inventory"])
    talk(client, "女子2", (78, 137))
    state = idle(client)
    assert sum(entry["quantity"] for entry in state["inventory"]) == quantity + 1
    assert not any(target["name"] in ("女子1", "女子2") for target in state["targets"])
    checkpoint(client, output, "side-zhongdu-captives-rescued")
    go(client, 80, 159)
    for point in reversed(approach[:-1]):
        go(client, *point, timeout=300)
    go(client, 22, 131, destination="中都.map")


def tianwang_family_revisit(client, output, *, magic_file_prefix="player-magic-"):
    state = verify(client, FromFengChi=20, HanYanTWBDiZi=1, TianWangPiEr=4)
    assert state["map"] == "汉阳.map", state.get("map")
    talk(client, "天王帮弟子", (41, 37))
    state = verify(client, FromFengChi=20, TianWangPiEr=5)
    assert state["map"] == "天王岛.map", state.get("map")
    talk(client, "杨瑛", (45, 76))
    state = idle(client)
    assert item(state, magic_file_prefix + "洗髓经.ini", "magic")["level"] >= 1
    talk(client, "苹儿")
    checkpoint(client, output, "side-xisui-and-family-revisit")
    talk(client, "守门弟子", (85, 21))
    assert idle(client)["map"] == "汉阳.map"


def zhongdu_mine_side_story(client, output, *, magic_file_prefix="player-magic-", craft_sword=True,
                            ore_miner_position=(64, 23), jump_to_ore_miner=False, collect_ore=True):
    state = verify(client, ZhongDuLYS=1)
    resumed = state["map"] == "矿山.map"
    officers_defeated = returning_with_ore = False
    if resumed:
        state = client.observe(("1JiangLing", "2JiangLing", "3JiangLing", "Jian", "KuangGongZouLe"))
        officers_defeated = all(state["variables"].get(f"{index}JiangLing") == str(index)
                                for index in (1, 2, 3))
        returning_with_ore = state["variables"].get("Jian") == "1"
        assert not returning_with_ore or officers_defeated
        state = verify(client, KuangShan=2, Jian=int(returning_with_ore),
                       KuangGongZouLe=int(returning_with_ore),
                       **{"1JiangLing": 1, "2JiangLing": 2 if officers_defeated else 0,
                          "3JiangLing": 3 if officers_defeated else 0})
        assert item(state, magic_file_prefix + "弯刀冷光.ini", "magic")["level"] >= 1
        if returning_with_ore:
            assert state["player"]["position"] in (dict(x=14, y=76), dict(x=14, y=75))
            assert item(state, "goods-奇矿石.ini")["quantity"] >= 1
        elif not officers_defeated:
            assert state["player"]["position"] in (dict(x=9, y=127), dict(x=9, y=125))
        # Re-resolve only surviving officers after loading; IDs are session-local.
        officer_positions = () if officers_defeated else ((2, (7, 105)), (3, (52, 85)))
    else:
        assert state["map"] == "中都.map", state.get("map")
        talk(client, "寺庙老妪", (54, 42))
        verify(client, **{"1KuangShan": 1})
        go(client, 29, 92, destination="中都-矿山.map", script="中都/地图陷阱10.txt", combat=False)
        verify(client, KuangShan=2)
        for point in ((29, 44), (38, 22), (41, 22)):
            go(client, *point)
        talk(client, "宝箱", (41, 20))
        assert item(idle(client), magic_file_prefix + "弯刀冷光.ini", "magic")["level"] >= 1
        checkpoint(client, output, "side-xueyidao-learned")
        state = go(client, 16, 7, destination="矿山.map", script="中都-矿山/地图陷阱2.txt", combat=False)
        # Entry disables NPC AI, so the first visit can use exact initial positions.
        officer_positions = tuple(enumerate(((90, 131), (7, 108), (52, 81)), 1))
    officers = {}
    generation = state["generation"]
    for index, position in officer_positions:
        matches = [target for target in state["targets"] if target["kind"] == "npc"
                   and target["name"] == "金国将领" and target.get("action") not in (11, 255)
                   and (not resumed or npc_attackable(target))
                   and abs(target["position"]["x"] - position[0]) <= (2 if resumed else 0)
                   and abs(target["position"]["y"] - position[1]) <= (2 if resumed else 0)]
        if len(matches) != 1:
            raise AutomationError(f"Expected mine officer {index} at {position}: {matches}")
        officers[index] = matches[0]["id"]
    if not resumed:
        go(client, 76, 142, combat=False)
        go(client, 84, 140, script="矿山/地图陷阱2.txt", combat=False)
    for index, points in (
            (1, ((90, 133),)),
            (2, ((82, 124), (80, 119), (68, 135), (56, 151), (45, 168), (25, 168),
                 (12, 154), (9, 125), (7, 110))),
            (3, ((7, 71), (19, 55), (31, 38), (46, 41), (47, 79), (52, 83)))):
        if officers_defeated or (resumed and index == 1):
            continue
        if resumed and index == 2:
            points = ((9, 125), (7, 110))
        for point in points:
            go(client, *point, timeout=300)
        counter = f"{index}JiangLing"
        state = client.observe((counter,))
        assert state["map"] == "矿山.map" and state["generation"] == generation
        if state["variables"].get(counter) != str(index):
            matches = [target for target in state["targets"]
                       if target["id"] == officers[index] and npc_attackable(target)]
            if len(matches) != 1:
                raise AutomationError(f"Mine officer vanished without his death flag: {index}")
            fight(client, matches[0])
        verify(client, **{counter: index})
    state = idle(client)
    practice_slot = state["layout"]["practiceSlot"]
    practicing = [entry for entry in state["magic"] if entry["slot"] == practice_slot]
    assert len(practicing) <= 1, practicing
    if (practicing and practicing[0]["file"] == magic_file_prefix + "风雪狂刀.ini"
            and practicing[0]["level"] == 10):
        item(state, magic_file_prefix + "风雪狂刀.ini", "magic")
        source_slot = item(state, magic_file_prefix + "弯刀冷光.ini", "magic")["slot"]
        client.assign_practice(source_slot)
        state = idle(client)
        assert item(state, magic_file_prefix + "弯刀冷光.ini", "magic")["slot"] == practice_slot
        assert item(state, magic_file_prefix + "风雪狂刀.ini", "magic")["slot"] == source_slot
        checkpoint(client, output, "side-mine-practice-switched")
    if not returning_with_ore:
        for point in (((56, 51), (63, 28), (64, 24)) if collect_ore else ((53, 37),)):
            go(client, *point)
        if collect_ore and jump_to_ore_miner:
            late_jump(client, (64, 26), (62, 24))
        # Every other miner's dialogue deletes all miners without awarding the ore.
        if collect_ore:
            talk(client, "矿工1", ore_miner_position)
        else:
            state = idle(client)
            position = state["player"]["position"]
            miners = [actor for actor in state["targets"] if actor["name"] == "矿工1"
                      and actor["position"] != dict(x=64, y=20)]
            assert miners, "No ordinary escape miner remains"
            miner = min(miners, key=lambda actor: abs(actor["position"]["x"] - position["x"]) * 2
                        + abs(actor["position"]["y"] - position["y"]))
            client.act("Interact", timeout=185, generation=state["generation"],
                       targetId=miner["id"], running=False, timeoutMs=180000)
            idle(client)
        state = verify(client, Jian=int(collect_ore), KuangGongZouLe=1)
        if collect_ore:
            assert item(state, "goods-奇矿石.ini")["quantity"] >= 1
        else:
            assert not any(entry["file"] == "goods-奇矿石.ini" for entry in state["inventory"])
        checkpoint(client, output, "side-mine-workers-rescued" if collect_ore else "side-mine-workers-left-without-ore")
        if collect_ore and jump_to_ore_miner:
            late_jump(client, (62, 24), (64, 26))
        for point in ((53, 37), (39, 44), (28, 62)):
            go(client, *point, timeout=300, combat=False)
    for point in ((14, 75), (7, 100), (9, 129), (14, 154), (27, 168), (39, 152),
                  (50, 135), (63, 121), (81, 116), (83, 145), (71, 161)):
        go(client, *point, timeout=300, combat=False)
    go(client, 66, 170, destination="中都-矿山.map", script="矿山/地图陷阱1.txt", combat=False)
    go(client, 28, 86, destination="中都.map", script="中都-矿山/地图陷阱1.txt", combat=False)
    verify(client, **{"4JiangLing": 4})
    if not craft_sword:
        go(client, 41, 88, combat=False)
        go(client, 40, 69, script="中都/龙音寺门口地图陷阱5.txt", combat=False)
    go(client, 52, 53, script="中都/地图陷阱17.txt", combat=False)
    if not craft_sword:
        checkpoint(client, output, "side-mine-ore-held-for-later-forging" if collect_ore else "side-mine-return-without-ore")
        return
    # The family scene ends inside the temple room; leave before the long city walk.
    go(client, 52, 53, combat=False)
    go(client, 47, 93, combat=False)
    meditate(client)
    # The smith takes all remaining cash; refill only missing supplies first.
    restock(client, mana_count=6, reserve_money=0)
    # Both the temple gate and medicine counter reach this first waypoint.
    for point in ((68, 118), (106, 109), (151, 80), (171, 125), (167, 118)):
        go(client, *point, combat=False)
    before = client.observe()["player"]["money"]
    talk(client, "掌柜1", (167, 119))
    state = verify(client, Jian=0)
    assert state["player"]["money"] == 0
    assert item(state, "goods-jian-14-剑中之剑.ini")["quantity"] >= 1
    assert not any(entry["file"] == "goods-奇矿石.ini" for entry in state["inventory"])
    # The actual sword trades 3000 maximum life for 3000 attack; retain the
    # equipped weapon and record the normal crafting cost before later rewards.
    (output / "side-mine-crafting.json").write_text(json.dumps(
        dict(moneyBefore=before, moneyAfter=0, inventory=state["inventory"]),
        ensure_ascii=False, indent=2), encoding="utf-8")
    checkpoint(client, output, "side-mine-sword-crafted")


def zhongdu_first_visit(client, output, *, magic_file_prefix="player-magic-", craft_sword=True,
                        ore_miner_position=(64, 23), jump_to_ore_miner=False,
                        second_boss_name=None, captive_money_range=(9000, 20000),
                        leave_via_main_gate=False):
    state = idle(client)
    assert state["map"] in ("中都.map", "矿山.map"), state.get("map")
    if state["map"] == "中都.map":
        meditate(client)
        restock(client, mana_count=6)
        go(client, 47, 93, script="中都/龙音寺门口地图陷阱5.txt")
        go(client, 48, 92, script="中都/龙音寺门口地图陷阱6.txt")
        verify(client, ZhongDuLYS=1)
    zhongdu_mine_side_story(client, output, magic_file_prefix=magic_file_prefix, craft_sword=craft_sword,
                            ore_miner_position=ore_miner_position, jump_to_ore_miner=jump_to_ore_miner)
    zhongdu_town_story(client, output, magic_file_prefix=magic_file_prefix, craft_sword=craft_sword,
                       second_boss_name=second_boss_name, captive_money_range=captive_money_range,
                       leave_via_main_gate=leave_via_main_gate)


def zhongdu_town_story(client, output, *, magic_file_prefix="player-magic-", craft_sword=True,
                        treasury_chest_opened=False, second_boss_name=None, captive_money_range=(9000, 20000),
                        leave_via_main_gate=False):
    if not treasury_chest_opened:
        # The long route around the eastern buildings exceeds one normal path query.
        for point in (((171, 125), (151, 80), (106, 109), (68, 118), (47, 93), (83, 62))
                      if craft_sword else ((83, 62),)):
            go(client, *point, combat=False)
        money = client.observe()["player"]["money"]
        talk(client, "宝箱", (83, 61))
        assert 1000 <= client.observe()["player"]["money"] - money <= 10000
        checkpoint(client, output, "side-zhongdu-treasury-opened")
        restock(client, mana_count=6)
    if not craft_sword and idle(client)["map"] == "中都.map":
        go(client, 84, 103, script=None if treasury_chest_opened else "中都/龙音寺门口地图陷阱6.txt", combat=False)
        for point in ((106, 109), (106, 147)):
            go(client, *point, combat=False)
    zhongdu_shaolin_side_story(client, output, magic_file_prefix=magic_file_prefix,
                               second_boss_name=second_boss_name, captive_money_range=captive_money_range)
    for point in ((84, 103), (75, 20), (79, 30)):
        go(client, *point, combat=False)
    talk(client, "柴嵩")
    verify(client, ZhongDuTroublesRoom=1)
    for point in ((84, 103), (97, 223), (111, 217)):
        go(client, *point, combat=False)
    talk(client, "老板", (110, 217))
    verify(client, ZhongDuTroublesRoom=2)
    for point in ((97, 223), (84, 103), (75, 20), (79, 30)):
        go(client, *point, combat=False)
    talk(client, "柴嵩")
    verify(client, ZhongDuTroublesRoom=3)
    for point in ((84, 103), (97, 223), (138, 263)):
        go(client, *point, combat=False)
    talk(client, "燕府家丁", (139, 263))
    talk(client, "燕府家丁", (139, 263))
    assert idle(client)["map"] == "中都夜.map"
    verify(client, ZhongDuZhuangYuanDoor=2)
    for point in ((84, 103), (97, 223)):
        go(client, *point, combat=False)
    late_enter_night_garden(client)
    for stage in range(1, 5):
        talk(client, "燕若雪")
        verify(client, ZhongDuHouHuaYuan=stage)
    late_leave_night_garden(client)
    for point in ((97, 223), (84, 103), (75, 20), (79, 30)):
        go(client, *point, combat=False)
    talk(client, "柴嵩")
    verify(client, ZhongDuTroublesRoom=4)
    # This normal script relocates the party inside the garden and disables jump.
    talk(client, "燕若雪")
    verify(client, ZhongDuHouHuaYuan=5)
    if leave_via_main_gate:
        zhongdu_night_main_gate(client, output)
    else:
        late_leave_night_garden(client)
    zhongdu_return_to_linan(client, output, magic_file_prefix=magic_file_prefix)


def zhongdu_return_to_linan(client, output, *, magic_file_prefix="player-magic-"):
    go(client, 70, 334)
    go(client, 20, 332, destination="汉阳-中都2.map")
    go(client, 2, 53, destination="汉阳-中都1.map")
    late_jump(client, (18, 47), (10, 47))
    go(client, 1, 13, destination="金兵营寨.map", combat=False)
    go(client, 17, 184, destination="汉阳-金兵营寨.map")
    go(client, 5, 72, destination="汉阳.map", combat=False)
    go(client, 77, 178, destination="稻香村.map")
    daoxiang_to_linan(client, output, magic_file_prefix=magic_file_prefix)
    late_checkpoint(client, output, "late-01-linan-arrival")


def daoxiang_to_linan(client, output, *, magic_file_prefix="player-magic-"):
    state = idle(client)
    assert state["map"] == "稻香村.map"
    # Hanyang's entry script starts this challenge even though Daoxiang's
    # separate trap4 has no map tiles. Resolve only its actual hostile actors.
    verify(client, DaoXiangFirstEnter=1)
    for name in ("王重阳", "洪七", "欧阳锋"):
        state = idle(client)
        matches = [target for target in state["targets"] if target["name"] == name
                   and target.get("hostile") and npc_attackable(target)]
        if len(matches) > 1:
            raise AutomationError(f"Ambiguous Daoxiang opponent {name}: {matches}")
        if matches:
            fight(client, matches[0])
    state = verify(client, DaoXiangFight=3)
    if not any(entry["file"] == magic_file_prefix + "天师符法.ini" for entry in state["magic"]):
        client.act("UseItem", generation=state["generation"],
                   slot=item(state, "book-天师符法秘笈.ini")["slot"])
        state = idle(client)
        assert item(state, magic_file_prefix + "天师符法.ini", "magic")["level"] >= 1
        checkpoint(client, output, "side-daoxiang-tianshi-learned")
    # One city-length path exceeds the native search limit; use ordinary legs.
    for point in ((44, 276), (57, 223), (77, 183), (97, 143), (117, 103)):
        go(client, *point)
    return go(client, 123, 87, destination="临安城.map")


def late_return_to_linan(client):
    go(client, 53, 252, destination="临安-凤池山庄.map")
    return go(client, 1, 3, destination="临安城.map")


def late_enter_fengchi(client):
    # Cross the city in ordinary bounded walks; both the western entrance and
    # Chai Song's house exceed the native search limit when targeting the gate.
    point = idle(client)["player"]["position"]
    if point["x"] < 100:
        linan_approach(client, 60, 310)
        go(client, 100, 302)
        go(client, 153, 309)
    elif point["y"] < 220:
        go(client, 153, 220)
    go(client, 173, 343, destination="临安-凤池山庄.map")
    return go(client, 12, 13, destination="凤池山庄.map")


def late_leave_linan(client):
    point = idle(client)["player"]["position"]
    if point["x"] > 100:
        if point["y"] < 220:
            go(client, 153, 220)
        go(client, 100, 302)
    go(client, 60, 310)
    # The normal one-shot entrance dialogue may precede the western exit.
    evidence = _move_script_evidence(client, "临安城/first.txt")
    try:
        next(evidence)
        try:
            return go(client, 35, 325, destination="稻香村.map")
        except AutomationError as error:
            if str(error) != "Unexpected map: 临安城.map; expected 稻香村.map":
                raise
            deadline = time.monotonic() + 3
            while not next(evidence)[1]:
                if time.monotonic() >= deadline:
                    raise error
                time.sleep(0.05)
            return go(client, 35, 325, destination="稻香村.map")
    finally:
        evidence.close()


def linan_approach(client, x, y, **arguments):
    evidence = _move_script_evidence(client, "临安城/first.txt")
    next(evidence)
    try:
        try:
            return go(client, x, y, **arguments)
        except AutomationError as error:
            expected = arguments.get("script")
            if str(error) not in ("Unexpected script during movement: script/map/临安城/first.txt",
                                  f"Move did not complete requested script: {expected}"):
                raise
            deadline = time.monotonic() + 3
            while not next(evidence)[1]:
                if time.monotonic() >= deadline:
                    raise error
                time.sleep(0.05)
            return go(client, x, y, **arguments)
    finally:
        evidence.close()


def linan_and_fengchi(client, output, *, magic_file_prefix="player-magic-"):
    assert idle(client)["map"] == "临安城.map"
    if client.observe(("FromFengChi",))["variables"].get("FromFengChi") != "1":
        # Jie'er occupies the only entrance to the courtyard containing the chest.
        talk(client, "婕儿")
        go(client, 142, 108, script="临安城/trap4.txt")
        verify(client, LinAnYanRuoXue=1)
        money = client.observe()["player"]["money"]
        talk(client, "宝箱", (147, 76))
        assert 1000 <= client.observe()["player"]["money"] - money <= 10000
        restock(client)
        talk(client, "柴嵩")
        verify(client, LinAnYanRuoXue=2)
        talk(client, "燕若雪")
        assert idle(client)["map"] == "凤池山庄.map"
        # His normal dialogue moves the gatekeeper off the only exit passage.
        talk(client, "守门家丁")
        verify(client, FenCiDaMenJiaDin=1)
        late_return_to_linan(client)
    verify(client, FromFengChi=1)
    linan_approach(client, 141, 139, script="临安城/trap9.txt")
    talk(client, "柴嵩")
    state = verify(client, FromFengChi=2)
    # The newly learned level-one Mengdie is weaker against the upcoming bosses.
    # Keep Tianyi attacking while Mengdie receives normal practice experience.
    dream_slot = item(state, magic_file_prefix + "梦蝶神功.ini", "magic")["slot"]
    previous_practice = next((entry["file"] for entry in state["magic"]
                              if entry["slot"] == state["layout"]["practiceSlot"]), None)
    if (item(state, magic_file_prefix + "天意剑诀.ini", "magic")["slot"] != state["layout"]["magicQuickBegin"]
            or previous_practice not in (magic_file_prefix + "风雪狂刀.ini", magic_file_prefix + "弯刀冷光.ini")
            or dream_slot >= state["layout"]["magicQuickBegin"]):
        raise AutomationError("Unexpected Tianyi/practice/Mengdie arrangement after Chai Song's lesson")
    client.assign_practice(dream_slot)
    state = idle(client)
    if (item(state, magic_file_prefix + "天意剑诀.ini", "magic")["slot"] != state["layout"]["magicQuickBegin"]
            or item(state, magic_file_prefix + "梦蝶神功.ini", "magic")["slot"] != state["layout"]["practiceSlot"]
            or item(state, previous_practice, "magic")["slot"] != dream_slot):
        raise AutomationError("Mengdie practice assignment did not preserve Tianyi and return the previous practice magic to the bag")
    checkpoint(client, output, "late-02-mengdie-practice")
    late_enter_fengchi(client)
    talk(client, "后门家丁")
    go(client, 85, 136, script="凤池山庄/maptrap4.txt")
    verify(client, FromFengChi=4)
    talk(client, "后门家丁")
    late_return_to_linan(client)
    verify(client, FromFengChi=5)
    talk(client, "婕儿")
    go(client, 155, 132, script="临安城/trap10.txt")
    talk(client, "燕若雪")
    verify(client, FromFengChi=6)
    late_enter_fengchi(client)
    talk(client, "后门家丁")
    # The enabled hall trap lies on every walking path to the bedroom at this stage.
    go(client, 74, 197, script="凤池山庄/datingtalk.txt")
    verify(client, FromFengChi=7)
    late_return_to_linan(client)
    restock(client, life_count=8, mana_count=8, reserve_money=1000, max_spend=6500)
    meditate(client)
    late_leave_linan(client)
    for point in ((97, 143), (77, 183), (79, 242)):
        go(client, *point)
    go(client, 100, 312, destination="霹雳堂.map")
    late_checkpoint(client, output, "late-02-pilitang-arrival")


def pilitang_and_tournament(client, output, *, magic_file_prefix="player-magic-",
                            sword_file="goods-jian-14-剑中之剑.ini", resource_directory="jxqy2",
                            tournament_source_slot=None):
    assert idle(client)["map"] == "霹雳堂.map"
    go(client, 20, 153, script="霹雳堂/trap1.txt", combat=False)
    # Enter the hall through its walking corridor before selecting the boss;
    # firing from the entrance-side wall wastes mana on blocked projectiles.
    go(client, 60, 56, combat=False)
    go(client, 66, 44, combat=False)
    boss = late_target(client, "雷同", (70, 40))
    # Lei retreats behind the hall obstacles. Immobilize him normally, then
    # walk into weapon range rather than repeatedly firing into those tiles.
    state = idle(client)
    client.assign_magic(item(state, magic_file_prefix + "定身法.ini", "magic")["slot"], quick_slot=1)
    state = idle(client)
    continue_practice(client)
    try:
        client.act("CastSkill", generation=state["generation"], slot=1, targetId=boss["id"])
    except AutomationError as error:
        if "action_not_executed" not in str(error):
            raise
        print("Immobilization interrupted; approach through normal melee movement", flush=True)
    state = idle(client)
    medicine = select_medicine(state, "life", require_ready=False)
    result = client.act("StartCombat", timeout=245, generation=state["generation"],
                        targetId=boss["id"], radius=20, kills=1, skills=[],
                        allowMeleeFallback=True, timeoutMs=240000,
                        lifeItem=medicine["file"] if medicine else LIFE_ITEM, lifePercent=45)
    assert result["kills"] == 1, result
    idle(client)
    verify(client, FromFengChi=8)
    # The death scene replaces the boss with z40-雷同 at 65,45. His
    # normal corpse owns 捡到书.txt; resolve it after the death animation.
    state = client.wait_until(
        lambda state: any(target["kind"] == "object" and target["name"] == "雷同"
                          and target["position"] == dict(x=65, y=45)
                          for target in state["targets"]),
        timeout=15, description="Lei Tong's book-bearing corpse")
    corpses = [target for target in state["targets"]
               if target["kind"] == "object" and target["name"] == "雷同"
               and target["position"] == dict(x=65, y=45)]
    assert len(corpses) == 1, corpses
    client.act("Interact", timeout=185, generation=state["generation"],
               targetId=corpses[0]["id"], running=False, timeoutMs=180000)
    state = idle(client)
    client.act("UseItem", generation=state["generation"],
               slot=item(state, "book-霹雳手法.ini")["slot"])
    state = idle(client)
    assert item(state, magic_file_prefix + "霹雳烈焰手法.ini", "magic")["level"] >= 1
    go(client, 8, 181, destination="稻香村.map")
    daoxiang_to_linan(client, output, magic_file_prefix=magic_file_prefix)
    # Lei Tong's completed death scene advances both story variables.
    verify(client, LinAnYanRuoXue=9)
    late_enter_fengchi(client)
    verify(client, FromFengChi=9)
    if tournament_source_slot is not None:
        client.save_or_load(3)
        late_checkpoint(client, output, "fengchi-shao-before-duel-normal-slot3")
    go(client, 74, 197, script="凤池山庄/datingtalk.txt")
    late_return_to_linan(client)
    before_supplies = idle(client)
    limits = dict(life_count=12, mana_count=12, reserve_money=1000, max_spend=12500)
    after_supplies = restock(client, **limits)
    meditate(client)
    (output / "tournament-supplies.json").write_text(json.dumps(
        dict(limits=limits, moneyBefore=before_supplies["player"]["money"],
             moneyAfter=after_supplies["player"]["money"],
             inventoryBefore=before_supplies["inventory"], inventoryAfter=after_supplies["inventory"]),
        ensure_ascii=False, indent=2), encoding="utf-8")
    client.equip(item(idle(client), sword_file)["slot"])
    talk(client, "燕若雪")
    verify(client, LinAnChaiSong=2)
    if tournament_source_slot is not None:
        client.save_or_load(tournament_source_slot)
        late_checkpoint(client, output, "tournament-before-entry-normal-source")
    talk(client, "柴嵩")
    state = verify(client, FCBW=1)
    assert state["map"] == "凤池山庄-比武场.map"
    checkpoint(client, output, "late-03-tournament-before")
    compete_in_tournament(client, output, resource_directory=resource_directory,
                          magic_file_prefix=magic_file_prefix,
                          round_source_slot=0 if tournament_source_slot is not None else None)


def tournament_resources(output, *, resource_directory="jxqy2", magic_file_prefix="player-magic-"):
    command = json.loads((output / "run.json").read_text(encoding="utf-8"))["command"]
    resource = Path(command[command.index("--assets") + 1]) / resource_directory
    table = configparser.ConfigParser(interpolation=None, strict=False)
    table.read_string((resource / "ini/save/fengchibw.npc").read_text(encoding="utf-8-sig"))
    defenses = {row["name"]: row.getint("defend") for row in table.values() if "name" in row}
    magic = {}
    for filename in (magic_file_prefix + "天意剑诀.ini", magic_file_prefix + "梦蝶神功.ini"):
        table = configparser.ConfigParser(interpolation=None)
        table.read_string((resource / "ini/magic" / filename).read_text(encoding="utf-8-sig"))
        levels = {section.casefold(): table[section] for section in table.sections()}
        magic[filename] = {level: dict(effect=levels[f"level{level}"].getint("effect"),
                                      manaCost=levels[f"level{level}"].getint("manacost"),
                                      thewCost=levels[f"level{level}"].getint("thewcost", fallback=0))
                           for level in range(1, 11)}
    return defenses, magic


def compete_in_tournament(client, output, *, start_stage=1, resource_directory="jxqy2",
                          magic_file_prefix="player-magic-", round_source_slot=None,
                          round_source_callback=None, stop_before_stage=None):
    client.act("SetAutoDialogue", enabled=True)
    opponents = ("赵无双", "秋依水", "唐影", "孟廷威", "柴嵩", "杨干",
                 "邵骑风", "史忠良", "唐离", "赵升权", "独孤剑")
    prior_executions = {record.get("executionId") for record in late_records(output, "trace.jsonl")
                        if record.get("eventType") == "script.start"}
    defenses, magic_rules = tournament_resources(output, resource_directory=resource_directory,
                                                magic_file_prefix=magic_file_prefix)
    rounds = []
    state = verify(client, FCBW=start_stage)
    while state.get("map") == "凤池山庄-比武场.map":
        stage = int(state["variables"].get("FCBW") or 0)
        if not state.get("worldInput") or not 1 <= stage <= len(opponents):
            raise AutomationError(f"Tournament round is not ready: {state.get('variables')}")
        if round_source_callback is not None:
            round_source_callback(client, output, state)
        if stage == stop_before_stage:
            return state
        name = opponents[stage - 1]
        targets = [target for target in state["targets"] if target["kind"] == "npc"
                   and target["name"] == name and target.get("hostile") and npc_attackable(target)]
        if len(targets) != 1:
            raise AutomationError(f"Expected one active tournament opponent {name}: {targets}")
        target = targets[0]
        if round_source_slot is not None and stage in (1, 11):
            client.save_or_load(round_source_slot if stage == 1 else round_source_slot + 1)
            late_checkpoint(client, output, f"tournament-round-{stage}-normal-source")
            state = verify(client, FCBW=stage)
        life_medicine = select_medicine(state, "life", require_ready=False)
        mana_medicine = select_medicine(state, "mana", require_ready=False)
        candidates = []
        quick_visible = any(entry["name"].startswith("bottom-magic-quick-") for entry in state.get("ui", []))
        for spell in state["magic"]:
            if spell["file"] not in magic_rules:
                continue
            if not quick_visible and spell["slot"] != state["layout"]["magicQuickBegin"]:
                continue
            rule = magic_rules[spell["file"]][spell["level"]]
            damage = max(10, rule["effect"] - defenses[name])
            if (damage >= max(100, rule["manaCost"] * 2)
                    and state["player"]["manaMax"] >= rule["manaCost"]
                    and state["player"]["thew"] >= rule["thewCost"]):
                candidates.append(dict(spell=spell, nominalDamage=damage, **rule))
        selected = max(candidates, key=lambda option: option["nominalDamage"] / option["manaCost"],
                       default=None) if mana_medicine else None
        skills = [0] if selected else []
        if selected and selected["spell"]["slot"] != state["layout"]["magicQuickBegin"]:
            client.assign_magic(selected["spell"]["slot"], quick_slot=0)
            state = client.observe(("FCBW", "HappyEnding"))
        continue_practice(client)
        state = client.observe(("FCBW", "HappyEnding"))
        strategy = dict(defenseFromResource=defenses[name], selected=selected,
                        reason="efficient_owned_skill" if selected else
                        "normal_attack_without_suitable_skill_or_mana_medicine")
        supplies = dict(lifeItem=life_medicine["file"] if life_medicine else LIFE_ITEM, lifePercent=45)
        if skills:
            supplies.update(manaItem=mana_medicine["file"], manaPercent=20)
        started = time.monotonic()
        combat_deadline = started + 600
        round_record = dict(stage=stage, opponent=target, skills=skills, strategy=strategy,
                            supplies=dict(supplies), attempts=[])
        rounds.append(round_record)
        exhausted_medicines = set()
        while True:
            remaining = combat_deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"Tournament action timed out against {name}")
            action = client.submit("StartCombat", generation=state["generation"], targetId=target["id"],
                                   radius=20, kills=1, skills=skills, allowMeleeFallback=not bool(skills),
                                   timeoutMs=max(1, int(remaining * 1000)), **supplies)
            attempt = dict(actionId=action, supplies=dict(supplies), startedSeconds=time.monotonic() - started)
            round_record["attempts"].append(attempt)
            (output / "tournament-rounds.json").write_text(
                json.dumps(rounds, ensure_ascii=False, indent=2), encoding="utf-8")
            while time.monotonic() < combat_deadline:
                result = client.request("GetActionStatus", actionId=action)
                attempt["result"] = result
                if result["status"] != "running":
                    break
                time.sleep(0.2)
            else:
                attempt.update(timedOut=True, cancelResult=client.request("CancelAction", actionId=action))
                (output / "tournament-rounds.json").write_text(
                    json.dumps(rounds, ensure_ascii=False, indent=2), encoding="utf-8")
                raise TimeoutError(f"Tournament action timed out against {name}")
            round_record.update(actionId=action, result=result, supplies=dict(supplies),
                                seconds=time.monotonic() - started)
            (output / "tournament-rounds.json").write_text(
                json.dumps(rounds, ensure_ascii=False, indent=2), encoding="utf-8")
            depleted = result.get("reason", "").removeprefix("item_depleted: ")
            attribute = next((key for key in ("life", "mana") if supplies.get(key + "Item") == depleted), None)
            if (result["status"] == "failed" and result.get("kills", 0) == 0 and attribute
                    and result.get("reason", "").startswith("item_depleted: ")):
                current = client.observe(("FCBW", "HappyEnding"))
                same_target = [entry for entry in current.get("targets", [])
                               if entry["kind"] == "npc" and entry["id"] == target["id"]
                               and entry.get("hostile") and npc_attackable(entry)]
                if (current.get("map") == state["map"] and current.get("generation") == state["generation"]
                        and current.get("variables", {}).get("FCBW") == str(stage)
                        and current.get("worldInput") and current["player"]["life"] > 0
                        and len(same_target) == 1):
                    exhausted_medicines.add(depleted)
                    available = dict(current, inventory=[entry for entry in current["inventory"]
                                                         if entry["file"] not in exhausted_medicines])
                    replacement = select_medicine(available, attribute, require_ready=False)
                    if replacement and replacement["quantity"] > 0:
                        attempt["replacement"] = dict(attribute=attribute, file=replacement["file"])
                        supplies[attribute + "Item"] = replacement["file"]
                        state = current
                        (output / "tournament-rounds.json").write_text(
                            json.dumps(rounds, ensure_ascii=False, indent=2), encoding="utf-8")
                        continue
            break
        defeated = result["status"] == "succeeded" and result.get("kills") == 1
        if not defeated and result.get("reason") not in ("player_dead", "world_changed"):
            raise AutomationError(f"Tournament control failed against {name}: {result}")
        # A death script may load the courtyard before its action result arrives.
        # Wait through replacement actors and the normal spectator sequence.
        deadline = time.monotonic() + 600
        while time.monotonic() < deadline:
            state = client.observe(("FCBW", "HappyEnding"))
            if state.get("choices") or state.get("scene") == "Title":
                raise AutomationError("Unexpected state during tournament transition")
            after_stage = int(state.get("variables", {}).get("FCBW") or 0)
            if state.get("worldInput"):
                if state.get("map") == "凤池山庄.map":
                    break
                if state.get("map") == "凤池山庄-比武场.map" and after_stage != stage and after_stage != 12:
                    break
            time.sleep(0.1)
        else:
            raise TimeoutError(f"Tournament story did not finish round {stage}")
        happy = int(state["variables"].get("HappyEnding") or 0)
        won = (state["map"] == "凤池山庄-比武场.map" and after_stage == stage + 1
               or state["map"] == "凤池山庄.map" and stage == 11 and happy == 1)
        lost = state["map"] == "凤池山庄.map" and happy == 0
        if not won and not lost or defeated and not won:
            raise AutomationError(f"Tournament outcome disagrees with action: {result} / {state['variables']}")
        round_record.update(outcome="won" if won else "lost", nextStage=after_stage,
                            generationAfter=state["generation"], happyEnding=happy,
                            script=f"凤池山庄-比武场/{name}败.txt" if won else "凤池山庄-比武场/主角死亡.txt")
        (output / "tournament-rounds.json").write_text(
            json.dumps(rounds, ensure_ascii=False, indent=2), encoding="utf-8")
    required = tuple(record["script"] for record in rounds) + ("凤池山庄-比武场/比武结束.txt",)
    trace = late_records(output, "trace.jsonl", completed_scripts=required)
    current_trace = [record for record in trace if record.get("executionId") not in prior_executions]
    for script in required:
        late_completed_script(current_trace, script)
    branch = int(state["variables"].get("HappyEnding") or 0)
    (output / "tournament-result.json").write_text(json.dumps(
        dict(intent="win", happyEnding=branch, outcome="won" if branch == 1 else "lost", rounds=rounds),
        ensure_ascii=False, indent=2), encoding="utf-8")
    if branch != 1:
        raise AutomationError("Tournament must defeat Dugu Jian and reach HappyEnding=1")
    late_checkpoint(client, output, "late-04-tournament-result")


def clear_fengchi_guards(client):
    # The normal 22nd guard death resets Ao's initially invulnerable life.
    # Fighting him before that callback wastes skills against one million HP.
    while True:
        state = story_state(client)
        if int(state["variables"].get("FengChiKill") or 0) >= 22:
            return state
        point = state["player"]["position"]
        guards = [target for target in state["targets"] if target.get("hostile")
                  and target["name"] in ("凤池家丁", "凤池家丁1") and npc_attackable(target)]
        if not guards:
            raise AutomationError("Fengchi night battle is missing its required guard deaths")
        target = min(guards, key=lambda actor: abs(actor["position"]["x"] - point["x"]) * 2
                     + abs(actor["position"]["y"] - point["y"]))
        fight(client, target, skills=(), prefer_melee=True)


def fengchi_and_prison(client, output, *, prison_magic_file="player-magic-江翻海沸.ini"):
    state = idle(client)
    if state["map"] != "临安大牢.map":
        if state["map"] == "凤池山庄.map":
            go(client, 102, 154, destination="凤池山庄夜战.map", timeout=300, combat=False)
        else:
            assert state["map"] == "凤池山庄夜战.map"
        verify(client, FromFengChi=12)
        clear_fengchi_guards(client)
        meditate(client, allow_combat=True)
        boss = late_target(client, "敖管家", (117, 101))
        fight(client, boss)
        go(client, 107, 170, destination="临安大牢.map", combat=False)
    state = client.observe(("LinAnDaLao11",))
    if (state["variables"].get("LinAnDaLao11") or "0") != "1":
        late_object(client, 42, 147)
    state = verify(client, LinAnDaLao11=1, FromFengChi=13)
    spell = item(state, prison_magic_file, "magic")
    if spell["slot"] != state["layout"]["magicQuickBegin"] + 4:
        client.assign_magic(spell["slot"], quick_slot=4)
    state = client.observe()
    weapon = next((entry for entry in state["inventory"]
                   if entry["slot"] == state["layout"]["equipmentBegin"] + 4), None)
    if weapon is None or weapon["file"] in ("goods-jian-1-桃木剑.ini", "goods-dao-1-九环刀.ini",
                                          "goods-jian-2-桃花剑.ini"):
        client.equip(item(state, "goods-jian-5-龙泉剑.ini")["slot"])
    # The central trap tile can be occupied by an archer; its adjacent tile
    # runs the same normal exit script without targeting the occupied square.
    go(client, 79, 78, destination="临安大牢-3.map")
    # The direct southern route crosses the active exit back to the previous floor.
    go(client, 40, 26, combat=False)
    go(client, 64, 158, script="临安大牢-3/trap3.txt", combat=False)
    boss = late_target(client, "邵骑风", (78, 171))
    fight(client, boss)
    verify(client, FromFengChi=14)
    go(client, 92, 174, destination="临安大牢第1层.map")
    client.save_or_load(1)
    checkpoint(client, output, "prison-before-companion-battle-normal-slot1")
    go(client, 15, 151, script="临安大牢第1层/trap-3.txt", combat=False)
    boss = late_target(client, "邵骑风", (16, 110))
    fight(client, boss)
    verify(client, ShaoJiFeng=2)
    checkpoint(client, output, "late-05-prison-companion-outcome")
    prison_exit(client, output)


def prison_exit(client, output):
    state = verify(client, ShaoJiFeng=2)
    if state["map"] != "临安大牢第1层.map":
        raise AutomationError("Prison exit requires the completed first-floor Shao battle")
    go(client, 33, 71)
    go(client, 52, 115, destination="大牢出口.map")
    go(client, 10, 19, script="大牢出口/trap3-安葬.txt")
    state = idle(client)
    if state["map"] == "大牢出口.map":
        state = client.observe(("Zhao", "Cai", "Tang", "Qiu", "LinAnDie"))
        death_flags = {"赵无双": "Zhao", "柴嵩": "Cai", "唐影": "Tang", "秋依水": "Qiu"}
        survivors = [target for target in state["targets"] if target["kind"] == "npc"
                     and target["name"] in death_flags
                     and state["variables"].get(death_flags[target["name"]]) != "1"
                     and target.get("interactive") and target.get("action") not in (11, 255)]
        if not survivors:
            raise AutomationError("Prison exit has no expected funeral or survivor interaction")
        talk(client, survivors[0]["name"])
    assert idle(client)["map"] == "大牢出口1.map"
    go(client, 27, 36, destination="临安-凤池山庄.map")
    go(client, 1, 3, destination="临安城.map")
    late_checkpoint(client, output, "late-06-prison-exit")


def linan_revenge(client, output, *, magic_file_prefix="player-magic-"):
    state = idle(client)
    assert state["map"] == "临安城.map"
    if state["player"]["position"] == dict(x=153, y=309):
        go(client, 148, 299, script="临安城/first.txt", combat=False)
    restock(client, mana_count=6, max_spend=6000)
    meditate(client)
    go(client, 141, 139, script="临安城/trap9.txt")
    verify(client, FromFengChi=15)
    go(client, 86, 165, script="临安城/trap12.txt", combat=False)
    verify(client, FromFengChi=16)
    target = late_target(client, "杂货摊贩", (133, 244))
    client.interact(target["id"])
    state = client.wait_until(lambda state: "shop" in state, description="mask merchant")
    client.buy(item(state, "goods-sj-3-面具.ini", "shop")["slot"])
    client.ui("Cancel")
    state = idle(client)
    client.act("UseItem", generation=state["generation"], slot=item(state, "goods-sj-3-面具.ini")["slot"])
    verify(client, LinAnMianJu=1)
    go(client, 86, 165, script="临安城/trap12.txt", combat=False)
    verify(client, FromFengChi=17)
    for name, position in (("宋朝长刀手2", (68, 134)), ("宋朝长刀手3", (67, 136))):
        guard = late_target(client, name, position)
        fight(client, guard, skills=())
    go(client, 67, 134, script="临安城/trap8.txt", combat=False)
    verify(client, FromFengChi=18)
    boss = late_target(client, "赵节", (52, 102))
    fight(client, boss)
    go(client, 32, 130, destination="临安地下迷宫.map")
    boss = late_target(client, "赵节", (34, 31))
    fight(client, boss)
    verify(client, FromFengChi=19, ToZhongDu=1)
    linan_after_zhao(client, output, magic_file_prefix=magic_file_prefix)


def linan_after_zhao(client, output, *, magic_file_prefix="player-magic-"):
    state = verify(client, FromFengChi=19, ToZhongDu=1)
    if state["map"] != "临安地下迷宫.map":
        raise AutomationError("Linan return requires the completed underground Zhao battle")
    go(client, 73, 135)
    late_object(client, 74, 137)
    go(client, 67, 134, script="临安城/trap8.txt")
    verify(client, FromFengChi=20)
    late_leave_linan(client)
    for point in ((97, 143), (77, 183), (57, 223), (44, 276)):
        go(client, *point)
    go(client, 30, 357, destination="汉阳.map")
    tianwang_family_revisit(client, output, magic_file_prefix=magic_file_prefix)
    hanyang_to_zhongdu(client, output, magic_file_prefix=magic_file_prefix)
    late_checkpoint(client, output, "late-07-zhongdu-return")


def tianren_dungeons(client, output, *, branch_source_slot=None):
    assert idle(client)["map"] == "中都.map"
    for point in ((70, 334), (97, 223), (138, 263)):
        go(client, *point)
    go(client, 140, 264, script="中都/trap-7.txt")
    verify(client, ZhongDuHouHuaYuan=6)
    # Leaving the manor crosses its rearmed farewell dialogue once.
    go(client, 138, 263, script="中都/trap-7.txt")
    for point in ((97, 223), (84, 103)):
        go(client, *point)
    go(client, 47, 93, script="中都/龙音寺门口地图陷阱5.txt")
    go(client, 48, 92, script="中都/龙音寺门口地图陷阱6.txt")
    go(client, 60, 35)
    talk(client, "大和尚", (60, 34))
    verify(client, LongYinShiLiaoRan=1)
    go(client, 48, 92, script="中都/龙音寺门口地图陷阱6.txt")
    go(client, 130, 77, script="中都/地图陷阱13.txt")
    verify(client, JieTouXiaoFan=1)
    state = idle(client)
    if any(entry["file"] == "goods-jian-14-剑中之剑.ini"
           and entry["slot"] == state["layout"]["equipmentBegin"] + 4 for entry in state["inventory"]):
        client.equip(item(state, "goods-jian-5-龙泉剑.ini")["slot"])
    go(client, 177, 120, destination="中都-天忍教.map")
    go(client, 18, 41, destination="天忍教.map")
    go(client, 52, 114, destination="天忍教-地下迷宫1.map")
    tianren_dungeon_floors(client, output, branch_source_slot=branch_source_slot)


def tianren_dungeon_floors(client, output, *, branch_source_slot=None):
    assert idle(client)["map"] == "天忍教-地下迷宫1.map"
    go(client, 55, 44)
    state = client.observe(("TrjDxmgshijiang",))
    if int(state["variables"].get("TrjDxmgshijiang") or 0) != 2:
        boss = late_target(client, "天忍教双斧教众1", (59, 33))
        fight(client, boss)
    verify(client, TrjDxmgshijiang=2)
    go(client, 75, 93)
    talk(client, "铁匠", (76, 93))
    if branch_source_slot is not None:
        client.save_or_load(branch_source_slot)
        checkpoint(client, output, f"tianren-before-second-floor-normal-slot{branch_source_slot}")
    go(client, 75, 30, destination="天忍教-地下迷宫2.map")
    tianren_second_floor(client, output)


def tianren_second_floor(client, output):
    state = idle(client)
    assert state["map"] == "天忍教-地下迷宫2.map"
    if int(client.observe(("TrjDxmg",))["variables"].get("TrjDxmg") or 0) == 3:
        late_records(output, "trace.jsonl", completed_scripts=(
            "天忍教-地下迷宫2/地图陷阱3.txt", "天忍教-地下迷宫2/die.txt"))
        go(client, 45, 106, destination="天忍教-地下迷宫3.map")
        late_checkpoint(client, output, "late-08-final-map")
        return
    opened = int(client.observe(("TrjDxmgKg",))["variables"].get("TrjDxmgKg") or 0)
    if not 0 <= opened <= 4:
        raise AutomationError(f"Unexpected second-floor switch count: {opened}")
    if opened == 0:
        for point in ((42, 119), (44, 135), (47, 149), (41, 156), (33, 152), (28, 142)):
            go(client, *point)
    for index, position in enumerate(((27, 132), (22, 142), (55, 76), (59, 67)), 1):
        if index <= opened:
            continue
        go(client, position[0], position[1] + 2)
        late_object(client, *position)
        verify(client, TrjDxmgKg=index)
    if opened != 4:
        for point in ((35, 93), (29, 100), (28, 92), (33, 82), (38, 67), (31, 75)):
            go(client, *point)
    tianren_second_floor_boss(client, output)


def tianren_second_floor_boss(client, output):
    state = verify(client, TrjDxmgKg=4)
    assert state["map"] == "天忍教-地下迷宫2.map"
    client.save_or_load(0)
    checkpoint(client, output, "tianren-second-floor-before-boss-normal-slot0")
    meditate(client, allow_combat=True)
    go(client, 31, 75)
    script = "天忍教-地下迷宫2/地图陷阱3.txt"
    try:
        go(client, 31, 74, script=script)
    except AutomationError as error:
        state = idle(client)
        if (str(error) != f"Move did not complete requested script: {script}"
                or state["map"] != "天忍教-地下迷宫2.map"
                or state["player"]["position"] != dict(x=31, y=74)):
            raise
        # Ordinary combat can finish on the trap tile without a walking arrival.
        # Another guard can occupy the entrance while the previous fight ends.
        go(client, 31, 75)
        go(client, 31, 74, script=script)
    boss = late_target(client, "金国狼牙棒兵1", (15, 45))
    fight(client, boss, skills=(0,))
    verify(client, TrjDxmg=3)
    go(client, 45, 106, destination="天忍教-地下迷宫3.map")
    late_checkpoint(client, output, "late-08-final-map")


def late_records(output, filename, *, completed_scripts=()):
    path = output / "user-data" / "automation" / filename
    deadline = time.monotonic() + 3
    while True:
        # The background writer can still be appending a batch, including UTF-8.
        records = [json.loads(line) for line in path.read_bytes().split(b"\n")[:-1]]
        try:
            for suffix in completed_scripts:
                late_completed_script(records, suffix)
            return records
        except AutomationError:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise
            time.sleep(min(0.05, remaining))


def late_completed_script(records, suffix):
    starts = [record for record in records if record.get("eventType") == "script.start"
              and record.get("virtualPath", "").endswith(suffix)]
    if not starts:
        raise AutomationError(f"Missing script trace: {suffix}")
    start = starts[-1]
    if not any(record.get("eventType") == "script.finish"
               and record.get("executionId") == start["executionId"]
               and record.get("status") == "completed" for record in records):
        raise AutomationError(f"Script did not complete normally: {suffix}")
    return start


def tianren_finale(client, output, *, tianyi_file="player-magic-天意剑诀.ini",
                   ending_movie_lines=(55, 137), ending_names=("ordinary", "happy")):
    state = idle(client)
    assert state["map"] == "天忍教-地下迷宫3.map"
    practiced = next((entry for entry in state["magic"]
                      if entry["file"] == tianyi_file), None)
    use_skill = practiced is not None and practiced["level"] >= 8
    life_medicine = select_medicine(state, "life", require_ready=False)
    mana_medicine = select_medicine(state, "mana", require_ready=False)
    player = state["player"]
    life_ready = bool(life_medicine) or (player.get("lifeMax", 0) > 0 and player["life"] * 10 >= player["lifeMax"] * 9)
    mana_ready = not use_skill or bool(mana_medicine) or (player.get("manaMax", 0) > 0 and player["mana"] * 10 >= player["manaMax"] * 9)
    strategy = dict(skills=[0] if use_skill else [], tianyiLevel=practiced["level"] if practiced else None,
                    reason="天意剑诀达到八级，使用快捷武功" if use_skill else "天意剑诀不足八级，保留普通攻击",
                    lifeMedicine=life_medicine, manaMedicine=mana_medicine,
                    lifeReady=life_ready, manaReady=mana_ready, playerBeforeCombat=player)
    (output / "late-09-combat-plan.json").write_text(
        json.dumps(strategy, ensure_ascii=False, indent=2), encoding="utf-8")
    if not life_ready or not mana_ready:
        checkpoint(client, output, "late-09-insufficient-supplies")
        raise AutomationError("Final combat supplies insufficient: "
                              + ("life requires recovery or medicine" if not life_ready else "mana requires recovery or medicine for Tianyi"))
    if use_skill:
        client.assign_magic(practiced["slot"], quick_slot=0)
    go(client, 31, 75, script="天忍教-地下迷宫3/地图陷阱3.txt", combat=False)
    boss = late_target(client, "完颜宏烈", (21, 53))
    state = client.observe(("HappyEnding",))
    branch = int(state["variables"].get("HappyEnding") or 0)
    if branch not in (0, 1):
        raise AutomationError(f"Unknown HappyEnding value: {branch}")
    checkpoint(client, output, "late-09-final-boss-before")
    supplies = dict(lifeItem=life_medicine["file"], lifePercent=60) if life_medicine else {}
    if use_skill and mana_medicine:
        supplies.update(manaItem=mana_medicine["file"], manaPercent=30)
    action = client.submit("StartCombat", generation=state["generation"], targetId=boss["id"],
                           radius=20, kills=1, skills=strategy["skills"], timeoutMs=240000, **supplies)
    combat_started = time.monotonic()
    while True:
        status = client.request("GetActionStatus", actionId=action)
        if status["status"] != "running" or time.monotonic() - combat_started >= 30:
            break
        time.sleep(0.2)
    sample = client.observe()
    sampled_boss = next((target for target in sample.get("targets", []) if target["id"] == boss["id"]), None)
    remaining_life = sampled_boss["life"] if sampled_boss else (
        0 if status["status"] == "succeeded" and status.get("kills") == 1 else None)
    progress = dict(actionId=action, elapsedSeconds=time.monotonic() - combat_started,
                    lifeBefore=boss["life"], lifeAfter=remaining_life,
                    lifeLost=boss["life"] - remaining_life if remaining_life is not None else None,
                    actionStatus=status, player=sample.get("player"), inventory=sample.get("inventory"))
    (output / "late-09-combat-progress.json").write_text(
        json.dumps(progress, ensure_ascii=False, indent=2), encoding="utf-8")
    result = client.wait_action(action, timeout=max(1, 245 - (time.monotonic() - combat_started)))
    if result.get("kills") != 1:
        raise AutomationError(f"Final boss was not defeated: {result}")
    videos = set()
    deadline = time.monotonic() + 300
    while time.monotonic() < deadline:
        state = client.observe()
        if state.get("choices"):
            raise AutomationError("Unexpected choice in final ending")
        if state.get("video"):
            videos.add(state["video"].replace("\\", "/").split("/")[-1].lower())
            client.ui("Cancel")  # Existing movie skip input, after observing it.
        if state.get("scene") == "Title":
            break
        time.sleep(0.05)
    else:
        raise TimeoutError("Final ending did not return normally to title")
    trace = late_records(output, "trace.jsonl", completed_scripts=("天忍教-地下迷宫3/大结局.txt",))
    start = late_completed_script(trace, "天忍教-地下迷宫3/大结局.txt")
    execution = [record for record in trace if record.get("executionId") == start["executionId"]]
    if not any(record.get("eventType") == "api.call" and record.get("apiName") == "returntotitle"
               for record in execution):
        raise AutomationError("Ending trace has no normal returntotitle call")
    # The caller supplies movie lines from the exact resource being tested.
    # Captured video additionally confirms the actual branch.
    lines = {record.get("line") for record in execution if record.get("eventType") == "source.line"}
    movie_line, other_line = ending_movie_lines[branch], ending_movie_lines[1 - branch]
    movie, other_movie = ("happyend.avi", "end.avi") if branch == 1 else ("end.avi", "happyend.avi")
    if movie_line not in lines or other_line in lines or movie not in videos or other_movie in videos:
        raise AutomationError(f"Ending branch {branch} lacks matching evidence: videos={videos}")
    events = late_records(output, "events.jsonl")
    killed = [record for record in events if record.get("event") == "action.finish"
              and record["data"].get("actionId") == action
              and record["data"].get("status") == "succeeded" and record["data"].get("kills") == 1]
    if len(killed) != 1:
        raise AutomationError("Final boss kill is missing from actual action trace")
    # The outer runner must also verify every earlier chapter and the uninterrupted
    # normal-new-game lineage before promoting this local ending result to a full run.
    proof = dict(fullPlaythrough=False, endingCompleted=True,
                 ending=ending_names[branch], happyEnding=branch,
                 finalBoss=boss, combatActionId=action, combatResult=killed[0],
                 combatStrategy=strategy, combatProgress=progress,
                 endingScript=start, observedVideos=sorted(videos), scene=state["scene"])
    (output / "mainline-completion.json").write_text(
        json.dumps(proof, ensure_ascii=False, indent=2), encoding="utf-8")
    checkpoint(client, output, "late-10-ending-title")
    return proof


LATE_CHAPTERS = {
    "zhongdu-first": zhongdu_first_visit,
    "linan-fengchi": linan_and_fengchi,
    "pilitang-tournament": pilitang_and_tournament,
    "fengchi-prison": fengchi_and_prison,
    "linan-revenge": linan_revenge,
    "tianren-dungeons": tianren_dungeons,
    "tianren-finale": tianren_finale,
}


CHAPTERS = {"opening": begin_story, "village": village_and_town}
CHAPTERS.update(EARLY_CHAPTERS)
CHAPTERS.update(MIDDLE_CHAPTERS)
CHAPTERS.update(LATE_CHAPTERS)
STORY_VARIABLES += LATE_VARIABLES


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--assets", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--chapter", choices=("all", *CHAPTERS), required=True)
    parser.add_argument("--start-at", choices=CHAPTERS, default="opening", help="First chapter when running all")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--restart", action="store_true", help="Relaunch a closed game using the same independent saves")
    parser.add_argument("--load-slot", type=int, choices=range(7))
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    output = args.output.resolve()
    if args.resume:
        identity = json.loads((output / "run.json").read_text(encoding="utf-8"))
        if args.restart:
            archive = output / f"launch-evidence-{int(time.time())}"
            archive.mkdir()
            for name in ("events.jsonl", "trace.jsonl"):
                source = output / "user-data" / "automation" / name
                if source.exists():
                    shutil.copy2(source, archive / name)
            shutil.copy2(output / "run.json", archive / "run.json")
            command = identity["command"]
            session = str(uuid.uuid4())
            command[command.index("--automation-pipe") + 1] = session
            executable = Path(command[0])
            with (output / "stdout.log").open("ab") as stdout, (output / "stderr.log").open("ab") as stderr:
                process = subprocess.Popen(command, cwd=executable.parent, stdout=stdout, stderr=stderr)
            identity.update(session=session, pid=process.pid,
                            engineSha256=hashlib.sha256(executable.read_bytes()).hexdigest())
            (output / "run.json").write_text(json.dumps(identity, ensure_ascii=False, indent=2), encoding="utf-8")
    else:
        if not args.exe or not args.assets:
            parser.error("A fresh run requires --exe and --assets")
        output.mkdir(parents=True, exist_ok=False)
        executable, assets = args.exe.resolve(), args.assets.resolve()
        session = str(uuid.uuid4())
        command = [str(executable), "--assets", str(assets), "--resource-id", "JXQY2",
                   "--skip-startup-video", "--enable-automation-hooks", "--automation-pipe", session,
                   "--user-data-root", str(output / "user-data"), "--log-file", str(output / "game.log")]
        with (output / "stdout.log").open("wb") as stdout, (output / "stderr.log").open("wb") as stderr:
            process = subprocess.Popen(command, cwd=executable.parent, stdout=stdout, stderr=stderr)
        identity = dict(session=session, pid=process.pid, command=command,
                        engineSha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                        resourceProfileSha256=hashlib.sha256((assets / "jxqy2/game_profile.ini").read_bytes()).hexdigest())
        (output / "run.json").write_text(json.dumps(identity, ensure_ascii=False, indent=2), encoding="utf-8")
    result = dict(chapter=args.chapter, status="running", fullPlaythrough=False,
                  cheatAssisted=identity.get("cheatAssisted", False),
                  routeSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  manualInterventions=None if args.resume else 0,
                  interventionRecords=[str(path) for path in sorted(output.glob("intervention-*.json"))],
                  resumed=args.resume, startAt=args.start_at,
                  routeDevelopmentResume=args.resume, started=time.time(), session=identity["session"],
                  commandsByteBegin=(output / "commands.jsonl").stat().st_size if (output / "commands.jsonl").exists() else 0)
    route_snapshot = output / f"route-{int(result['started'] * 1000000)}"
    route_snapshot.mkdir()
    for filename in ("run_jxqy2_mainline.py", "gameplay_automation.py",
                     "jxqy2_mainline_supplies.py", "jxqy2_sidequests.py", "run_jxqy2_gameplay_smoke.py"):
        shutil.copy2(Path(__file__).with_name(filename), route_snapshot / filename)
    result["routeSnapshot"] = str(route_snapshot)
    with Client(identity["session"], timeout=20, transcript=output / "commands.jsonl") as client:
        try:
            if args.resume:
                state = client.observe()
                if any(entry["name"] == "return-to-title" for entry in state.get("ui", [])):
                    client.ui("Cancel")
                if args.load_slot is not None:
                    if state["scene"] == "Title":
                        client.activate("load-game")
                        state = client.wait_until(lambda value: "saveSlot" in value)
                        while state["saveSlot"] != args.load_slot:
                            client.focus("load")
                            client.ui("Down" if state["saveSlot"] < args.load_slot else "Up")
                            state = client.observe()
                        client.activate("load")
                        client.wait_until(lambda value: value["worldInput"], timeout=60)
                    else:
                        client.save_or_load(args.load_slot, load=True)
                    result["loadedCheckpoint"] = args.load_slot
                client.act("SetAutoDialogue", enabled=True)
            chapters = list(CHAPTERS)
            selected = chapters[chapters.index(args.start_at):] if args.chapter == "all" else (args.chapter,)
            for name in selected:
                result["activeChapter"] = name
                CHAPTERS[name](client, output)
                state = checkpoint(client, output, f"chapter-{name}")
                result.setdefault("completedChapters", []).append(name)
                (output / "progress.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
            result.update(status="passed", map=state.get("map"), position=state.get("player", {}).get("position"))
            if args.chapter == "all" and not args.resume:
                proof = json.loads((output / "mainline-completion.json").read_text(encoding="utf-8"))
                result["fullPlaythrough"] = proof.get("endingCompleted", False) and result["completedChapters"] == list(CHAPTERS)
        except Exception as error:
            result.update(status="failed", error=str(error))
            try:
                checkpoint(client, output, f"failure-{args.chapter}-{int(time.time())}")
            except Exception as evidence_error:
                result["evidenceError"] = str(evidence_error)
        finally:
            result["finished"] = time.time()
            result["commandsByteEnd"] = (output / "commands.jsonl").stat().st_size
            (output / f"result-{args.chapter}-{int(time.time())}.json").write_text(
                json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
            print(json.dumps(result, ensure_ascii=False), flush=True)
    raise SystemExit(0 if result["status"] == "passed" else 1)


if __name__ == "__main__":
    main()
