"""Repeat the representative JXQY2 route through normal gameplay and menus."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import shutil
import time
import uuid

from gameplay_automation import Client, AutomationError, npc_attackable

HOME_VARIABLES = ("NanGongCaiHong", "ZhangRuMeng", "HeiSha", "BaiSha", "HomeChoice")
LIFE_ITEM = "goods-yaowu-5-大补散.ini"
MANA_ITEM = "goods-yaowu-1-凝神丹.ini"
WEAPON = "goods-dao-1-九环刀.ini"
SKILL = "player-magic-寒霜掌.ini"


class MissingEvidence(AutomationError):
    pass


def checkpoint(client, output, name):
    state = client.observe(HOME_VARIABLES)
    if not state["outputHealthy"]:
        raise MissingEvidence("Game is running but required trace output is unavailable")
    try:
        (output / f"{name}.json").write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding="utf-8")
        shutil.copyfile(client.snapshot(), output / f"{name}.png")
    except (OSError, AutomationError) as error:
        raise MissingEvidence(f"Required screenshot unavailable: {error}") from error
    return state


def item(state, filename, collection="inventory"):
    matches = [entry for entry in state[collection] if entry["file"] == filename]
    assert len(matches) == 1, (filename, matches)
    return matches[0]


def reject(client, command, reason, **arguments):
    action = client.submit(command, **arguments)
    result = client.request("GetActionStatus", actionId=action)
    assert result["status"] == "failed" and reason in result["reason"], result


def settle(client, *, map_name=None, timeout=180, on_state=None):
    deadline = time.monotonic() + timeout
    reported = 0
    while time.monotonic() < deadline:
        state = client.observe(HOME_VARIABLES)
        if on_state:
            on_state(state)
        if "video" in state:
            client.ui("Cancel")
        elif state.get("choices"):
            raise AutomationError(f"Unmapped choice: {state['choiceMessage']}: {state['choices']}")
        elif state["worldInput"] and (map_name is None or map_name in state.get("map", "")):
            return state
        if time.monotonic() - reported > 15:
            print(json.dumps({key: state.get(key) for key in ("scene", "map", "script", "frame")}, ensure_ascii=False), flush=True)
            reported = time.monotonic()
        time.sleep(0.02 if on_state else 0.15)
    raise TimeoutError(f"Route did not settle on {map_name}; last state: {state.get('map')} / {state.get('script')}")


def target_named(state, name):
    targets = [item for item in state["targets"] if item["name"] == name and item.get("interactive", True)]
    if len(targets) != 1:
        raise AutomationError(f"Expected exactly one target {name}, got {targets}")
    return targets[0]


def talk_to(client, name):
    state = settle(client)
    client.interact(target_named(state, name)["id"])
    return settle(client)


def opening(client, output):
    state = client.wait_until(lambda s: s["scene"] == "Title", description="title")
    assert state["resourceId"] == "JXQY2"
    # Negative requests must not dispatch an input or change the scene.
    rejected = client.submit("SendUIAction", context=state["context"] - 1, action="Confirm")
    assert client.request("GetActionStatus", actionId=rejected)["status"] == "failed"
    rejected = client.submit("ExecuteScript", path="anything")
    assert client.request("GetActionStatus", actionId=rejected)["status"] == "failed"
    client.observe()
    client.sequence -= 1
    try:
        client.observe()
    except AutomationError as error:
        assert "duplicate_or_invalid_id" in str(error)
    else:
        raise AssertionError("Duplicate request was accepted")
    checkpoint(client, output, "01-title")
    client.act("SetAutoDialogue", enabled=True)
    client.activate("new-game")
    state = settle(client, map_name="沙漠之战")
    print("opening desert ready", flush=True)
    client.submit("MoveTo", generation=state["generation"], x=25, y=35,
                  running=True, timeoutMs=180000)
    observed_greeting = False
    def verify_home_greeting(state):
        nonlocal observed_greeting
        if state.get("dialogue", {}).get("text") == "黑煞：公子，夫人。":
            white = target_named(state, "白煞")
            assert white["position"] == dict(x=27, y=45)
            assert white["direction"] == 5 and white["action"] == 0 and not white["asyncMovement"], white
            observed_greeting = True
    state = settle(client, map_name="主角家", timeout=240, on_state=verify_home_greeting)
    if not observed_greeting:
        raise MissingEvidence("Opening greeting was not observed; choreography cannot be judged")
    assert target_named(state, "白煞")["direction"] == 5
    checkpoint(client, output, "02-home-introduction")
    print("home introduction completed", flush=True)
    for name in ("张如梦", "南宫彩虹", "南宫彩虹", "白煞", "黑煞"):
        state = talk_to(client, name)
        print(f"talked to {name}: {state['variables']}", flush=True)
    expected = dict(NanGongCaiHong="2", ZhangRuMeng="1", HeiSha="1", BaiSha="1")
    assert all(state["variables"].get(key) == value for key, value in expected.items()), state["variables"]
    (output / "home-state.json").write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding="utf-8")
    return state


def enter_trigger(client, x, y, script):
    state = client.observe()
    action = client.submit("MoveTo", generation=state["generation"], x=x, y=y,
                           running=True, timeoutMs=60000)
    deadline = time.monotonic() + 65
    while time.monotonic() < deadline:
        state = client.observe()
        if state.get("inEvent") and state["script"].endswith(script):
            # The normal trap script now owns movement. Cancel only our goal.
            client.request("CancelAction", actionId=action)
            return settle(client)
        status = client.request("GetActionStatus", actionId=action)
        if status["status"] in ("failed", "cancelled"):
            raise AutomationError(f"Did not enter {script}: {status}")
        time.sleep(0.15)
    raise TimeoutError(f"Trigger was not entered: {script}")


def prepare_and_leave_home(client, output):
    state = client.observe()
    reject(client, "MoveTo", "blocked_destination", generation=state["generation"], x=15, y=66)
    action = client.submit("MoveTo", generation=state["generation"], x=20, y=60)
    reject(client, "MoveTo", "world_action_busy", generation=state["generation"], x=21, y=60)
    assert client.request("CancelAction", actionId=action)["status"] == "cancelled"
    for x, y in ((37, 57), (28, 31)):
        state = settle(client)
        target = next(t for t in state["targets"] if t["kind"] == "object" and t["position"] == dict(x=x, y=y))
        client.interact(target["id"])
        settle(client)
    state = client.observe()
    assert item(state, LIFE_ITEM)["quantity"] == 5 and item(state, MANA_ITEM)["quantity"] == 5
    client.assign_magic(item(state, SKILL, "magic")["slot"])
    state = client.observe()
    assert item(state, SKILL, "magic")["slot"] == state["layout"]["magicQuickBegin"]
    client.assign_goods(item(state, LIFE_ITEM)["slot"], 0)
    state = client.observe()
    assert item(state, LIFE_ITEM)["slot"] == state["layout"]["goodsQuickBegin"]
    enter_trigger(client, 15, 67, "主角家/trap-3.txt")
    for count in range(4):
        state = enter_trigger(client, 14, 87, "主角家/trap-2.txt")
        assert state["variables"]["HomeChoice"] == str(min(count + 1, 3)), state["variables"]
    assert state["map"] == "主角家-狂沙镇.map"
    assert state["player"]["levelFile"] == "level-easy.ini"
    checkpoint(client, output, "03-native-easy")
    print("native easy exit passed", flush=True)


def shop_and_equipment(client, output):
    state = client.observe()
    target = next(t for t in state["targets"] if t["kind"] == "npc" and t["position"] == dict(x=132, y=61))
    client.interact(target["id"])
    state = client.wait_until(lambda s: "shop" in s, timeout=30, description="merchant")
    before_money = state["player"]["money"]
    client.buy(item(state, WEAPON, "shop")["slot"])
    state = client.observe()
    assert item(state, WEAPON)["quantity"] == 1 and state["player"]["money"] == before_money - 280
    before_money = state["player"]["money"]
    quantity = item(state, MANA_ITEM)["quantity"]
    client.buy(item(state, MANA_ITEM, "shop")["slot"])
    state = client.observe()
    assert item(state, MANA_ITEM)["quantity"] == quantity + 1 and state["player"]["money"] == before_money - 140
    client.sell(item(state, MANA_ITEM)["slot"])
    state = checkpoint(client, output, "04-shop")
    assert item(state, MANA_ITEM)["quantity"] == quantity and state["player"]["money"] == before_money - 70
    client.ui("Cancel")
    state = settle(client)
    client.equip(item(state, WEAPON)["slot"])
    state = client.observe()
    assert item(state, WEAPON)["slot"] >= state["layout"]["equipmentBegin"]
    print("shop, equipment and quick slots passed", flush=True)


def combat_and_save(client, output):
    client.move(100, 82)
    state = client.observe()
    client.act("JumpTo", generation=state["generation"], x=100, y=80)
    state = client.observe()
    assert state["player"]["position"] == dict(x=100, y=80)
    reject(client, "CastSkill", "action_rejected", generation=state["generation"], slot=4)
    reject(client, "UseItem", "item_unavailable", generation=state["generation"], slot=30)
    before_mana = state["player"]["mana"]
    client.act("CastSkill", generation=state["generation"], slot=0)
    state = client.observe()
    assert state["player"]["mana"] < before_mana, "Completed skill did not consume mana"
    drug = item(state, MANA_ITEM)
    before_mana = state["player"]["mana"]
    client.act("UseItem", generation=state["generation"], slot=drug["slot"])
    state = client.observe()
    assert item(state, MANA_ITEM)["quantity"] == drug["quantity"] - 1
    assert state["player"]["mana"] > before_mana
    client.move(100, 82)
    state = client.observe()
    wolves = [t for t in state["targets"] if t.get("hostile") and t["name"] == "灰狼" and npc_attackable(t)]
    assert wolves, "No normal gray wolf is available"
    target = min(wolves, key=lambda t: abs(t["position"]["x"] - 100) * 2 + abs(t["position"]["y"] - 82))
    assert abs(target["position"]["x"] - 100) < 20 and abs(target["position"]["y"] - 82) < 30, target
    checkpoint(client, output, "05-before-wolf")
    # Let the nearby wolf approach before testing one attack. A moving target can
    # invalidate a single normal approach; continuous pursuit belongs to StartCombat.
    state = client.wait_until(lambda s: any(t["id"] == target["id"]
        and abs(t["position"]["x"] - s["player"]["position"]["x"]) <= 1
        and abs(t["position"]["y"] - s["player"]["position"]["y"]) <= 1
        for t in s.get("targets", [])), timeout=20, description="wolf enters melee distance")
    client.act("Attack", generation=state["generation"], targetId=target["id"])
    state = client.observe()
    target_after_attack = next(t for t in state["targets"] if t["id"] == target["id"])
    assert npc_attackable(target_after_attack) and target_after_attack["life"] <= target["life"]  # Normal attacks can miss.
    result = client.act("StartCombat", timeout=125, generation=state["generation"],
                        targetId=target["id"], radius=20, kills=1, skills=[0],
                        lifeItem=LIFE_ITEM, manaItem=MANA_ITEM, timeoutMs=120000)
    assert result["kills"] == 1 and result["reason"] == "enemies_defeated", result
    state = client.wait_until(lambda s: s.get("player", {}).get("action") in (0, 20), timeout=10,
                              description="finish the current combat animation")
    remaining = next((t for t in state["targets"] if t["id"] == target["id"]), None)
    assert remaining is None or not npc_attackable(remaining), remaining
    before_mana = state["player"]["mana"]
    drug = item(state, MANA_ITEM)
    if before_mana < state["player"]["manaMax"]:
        client.act("UseItem", generation=state["generation"], slot=drug["slot"])
    else:
        events = [json.loads(line) for line in (output / "user-data/automation/events.jsonl").read_text(encoding="utf-8").splitlines()]
        assert any(r["event"] == "combat.item" and r["data"]["file"] == MANA_ITEM
                   and r["data"]["countAfter"] == r["data"]["countBefore"] - 1
                   and r["data"]["after"] > r["data"]["before"] for r in events), "Missing automatic medicine evidence"
    state = checkpoint(client, output, "06-after-wolf-and-medicine")
    if before_mana < state["player"]["manaMax"]:
        assert item(state, MANA_ITEM)["quantity"] == drug["quantity"] - 1
        assert state["player"]["mana"] > before_mana
    old_target_id = target["id"]
    before = checkpoint(client, output, "07-before-save")
    client.save_or_load(0)
    # Change position normally so load must restore it, not merely preserve state.
    client.move(100, 80)
    client.save_or_load(0, load=True)
    after = checkpoint(client, output, "08-after-load")
    assert after["generation"] > before["generation"]
    assert after["map"] == before["map"]
    assert after["player"]["position"] == before["player"]["position"]
    assert after["variables"] == before["variables"]
    inventory = lambda s: [(x["slot"], x["file"], x["quantity"]) for x in s["inventory"]]
    magic = lambda s: [(x["slot"], x["file"], x["level"]) for x in s["magic"]]
    assert inventory(after) == inventory(before) and magic(after) == magic(before)
    reject(client, "Interact", "stale_target", generation=after["generation"], targetId=old_target_id)
    reject(client, "MoveTo", "stale_world", generation=before["generation"], x=100, y=80)
    old_context = after["context"]
    client.open_menu("System")
    reject(client, "SendUIAction", "stale_ui_context", context=old_context, action="Confirm")
    client.ui("Cancel")
    print("wolf combat, medicine and save/load passed", flush=True)


def check_trace(output):
    directory = output / "user-data" / "automation"
    try:
        records = [json.loads(line) for line in (directory / "events.jsonl").read_text(encoding="utf-8").splitlines()]
        trace = (directory / "trace.jsonl").read_text(encoding="utf-8")
        traced = [json.loads(line) for line in trace.splitlines()]
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise MissingEvidence(f"Trace cannot be read: {error}") from error
    starts = [r["data"]["actionId"] for r in records if r["event"] == "action.start"]
    assert len(starts) == len(set(starts)), "A request executed twice across a nested run"
    dialogue = [r["data"] for r in records if r["event"] == "dialogue"]
    assert any("主角死亡.txt" in r["script"] for r in dialogue), "Opening scripted defeat was not observed"
    assert any("主角家/trap-2.txt" in r["script"] for r in dialogue), "Native difficulty script was not observed"
    for event in ("script.start", "script.finish", "map.change", "variable.change"):
        if not any(record.get("eventType") == event for record in traced):
            raise MissingEvidence(f"Missing normal-play trace event: {event}")
    started = {r["executionId"] for r in traced if r["eventType"] == "script.start"}
    finished = {r["executionId"] for r in traced if r["eventType"] == "script.finish" and r["status"] == "completed"}
    assert started == finished, "A normal-play script failed or did not finish"


def run_once(executable: Path, assets: Path, output: Path):
    output.mkdir(parents=True, exist_ok=False)
    session = str(uuid.uuid4())
    user_root = output / "user-data"
    command = [str(executable), "--assets", str(assets), "--user-data-root", str(user_root),
               "--resource-id", "JXQY2", "--skip-startup-video", "--enable-automation-hooks",
               "--automation-pipe", session, "--log-file", str(output / "game.log")]
    with (output / "stdout.log").open("wb") as stdout, (output / "stderr.log").open("wb") as stderr:
        process = subprocess.Popen(command, cwd=executable.parent, stdout=stdout, stderr=stderr)
    def digest(path):
        with path.open("rb") as source:
            return hashlib.file_digest(source, "sha256").hexdigest()
    identity = dict(session=session, pid=process.pid, command=command,
                    engineSha256=digest(executable), routeSha256=digest(Path(__file__)),
                    resourceProfileSha256=digest(assets / "jxqy2/game_profile.ini"))
    (output / "run.json").write_text(json.dumps(identity,
                                                  ensure_ascii=False, indent=2), encoding="utf-8")
    result = {"status": "running", "scope": "representative_flow", "fullPlaythrough": False,
              "manualInterventions": 0, "openingBattle": "expected_scripted_defeat"}
    try:
        with Client(session, timeout=20, transcript=output / "commands.jsonl") as client:
            try:
                opening(client, output)
                prepare_and_leave_home(client, output)
                shop_and_equipment(client, output)
                combat_and_save(client, output)
                check_trace(output)
                # Reconnect must find the normal pause menu, with automation stopped.
                client.close()
                time.sleep(0.3)
                with Client(session, transcript=output / "commands.jsonl") as resumed:
                    state = resumed.observe()
                    assert not state["autoDialogue"] and not state["worldInput"]
                    assert any(w["name"] == "return-to-title" for w in state["ui"])
                    checkpoint(resumed, output, "09-disconnected-pause")
                    resumed.exit_game()
                result["exitCode"] = process.wait(timeout=20)
                # The existing game returns ElementResult::erExit (2) on normal UI exit.
                assert result["exitCode"] in (0, 2), f"Unexpected game exit: {result['exitCode']}"
                result["status"] = "passed"
            except Exception:
                if client.handle:
                    try:
                        checkpoint(client, output, "failure-state")
                    except Exception as evidence_error:
                        result["evidenceError"] = str(evidence_error)
                raise
    except Exception as error:
        result.update(status="indeterminate" if isinstance(error, (MissingEvidence, OSError)) else "failed", error=str(error))
    finally:
        result["processAlive"] = process.poll() is None
        (output / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps(result, ensure_ascii=False), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=3)
    args = parser.parse_args()
    results = []
    for index in range(args.runs):
        result = run_once(args.exe.resolve(), args.assets.resolve(), args.output.resolve() / f"run-{index + 1}")
        results.append(result)
        if result["status"] != "passed":
            break  # Preserve the first unexpected state for analysis.
    summary = dict(requestedRuns=args.runs, results=results, fullPlaythrough=False)
    (args.output / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    raise SystemExit(0 if len(results) == args.runs and all(r["status"] == "passed" for r in results) else 1)


if __name__ == "__main__":
    main()
