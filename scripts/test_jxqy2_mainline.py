"""Isolated mainline helper checks (no game or pipe required)."""
import ast
import configparser
import contextlib
import copy
import io
import json
import struct
from pathlib import Path
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

from jxqy2_mainline_supplies import select_medicine
from gameplay_automation import npc_attackable


class AutomationError(RuntimeError):
    pass


class Clock:
    def __init__(self):
        self.now = 0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


def start(identity=1):
    return {"eventType": "script.start", "executionId": identity,
            "virtualPath": "script/map/test/trap.txt"}


def finish(identity=1, status="completed"):
    return {"eventType": "script.finish", "executionId": identity, "status": status}


def line(record):
    return (json.dumps(record) + "\n").encode("utf-8")


class FakeClient:
    def __init__(self, folder):
        self.transcript = SimpleNamespace(name=str(folder / "commands.jsonl"))
        self.trace = folder / "user-data/automation/trace.jsonl"
        self.trace.parent.mkdir(parents=True)
        self.trace.write_bytes(b"")
        self.state = {"generation": 1, "map": "old.map", "worldInput": True,
                      "outputHealthy": True, "targets": [], "inventory": [],
                      "player": {"position": {"x": 0, "y": 0}, "life": 100, "lifeMax": 100}}
        self.statuses = [{"status": "succeeded", "reason": "arrived"}]
        self.on_submit = lambda: None
        self.callbacks = {}
        self.status_count = self.submissions = self.cancelled = self.fights = 0
        self.fight_skills = []

    def append(self, data):
        with self.trace.open("ab") as trace:
            trace.write(data)

    def submit(self, command, **arguments):
        assert command == "MoveTo"
        self.submissions += 1
        self.on_submit()
        return self.submissions

    def request(self, command, **arguments):
        if command == "CancelAction":
            self.cancelled += 1
            return {}
        assert command == "GetActionStatus"
        self.status_count += 1
        self.callbacks.get(self.status_count, lambda: None)()
        return self.statuses[min(self.status_count - 1, len(self.statuses) - 1)]


def check_error(call, text):
    try:
        call()
    except AutomationError as error:
        assert text in str(error), str(error)
    else:
        raise AssertionError("Expected AutomationError")


def check_practice_completion():
    from gameplay_automation import Client

    for slot, behavior, expected_cancels in ((4, "normal", 3), (36, "normal", 2),
                                               (4, "already_closed", 0),
                                               (4, "unrelated_window", 1), (4, "stuck", 3)):
        client = SimpleNamespace(menus=[], transfer=False, cancelled=0, context=1,
                                 magic=[dict(slot=slot, file="source.ini")])

        def observe():
            return dict(context=client.context, worldInput=not client.menus,
                        layout=dict(magicQuickBegin=36, practiceSlot=41),
                        magic=copy.deepcopy(client.magic),
                        ui=[dict(name={"Magic": "magic-item-0", "Practice": "practice-magic-0",
                                       "Other": "choice-option-0"}[menu]) for menu in client.menus])

        def ui(action):
            if action == "Secondary":
                if not client.transfer:
                    client.transfer = True
                else:
                    client.magic[0]["slot"] = 41
                    if behavior == "already_closed":
                        client.menus.clear()
                        client.transfer = False
            else:
                assert action == ("PanelPrevious" if slot < 36 else "PanelNext")
                client.menus.append("Practice")
            client.context += 1

        def act(command, **arguments):
            assert command == "SendUIAction" and arguments == dict(context=client.context, action="Cancel")
            assert client.menus and "Other" not in client.menus, "Cancel leaked outside owned windows"
            client.cancelled += 1
            if behavior == "unrelated_window":
                client.menus = ["Other"]
            elif behavior != "stuck":
                if client.transfer:
                    client.transfer = False
                else:
                    client.menus.pop(0)
            client.context += 1

        def wait_until(predicate, **unused):
            state = observe()
            if not predicate(state):
                raise TimeoutError("Remaining window blocks gameplay")
            return state

        client.observe, client.ui, client.act, client.wait_until = observe, ui, act, wait_until
        client.open_menu = lambda name: client.menus.append(name)
        client.focus_slot = lambda *arguments: None
        if behavior in ("unrelated_window", "stuck"):
            try:
                Client.assign_practice(client, slot)
            except TimeoutError:
                pass
            else:
                raise AssertionError("An unclosed window must not report completion")
        else:
            assert Client.assign_practice(client, slot)["worldInput"]
        assert client.cancelled == expected_cancels, (slot, behavior, client.cancelled)
        assert client.magic == [dict(slot=41, file="source.ini")]
    return 5


def main():
    source = Path(__file__).resolve().parents[1] / "scripts/run_jxqy2_mainline.py"
    tree = ast.parse(source.read_text(encoding="utf-8"))
    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                 and node.name in ("go", "_move_script_evidence", "talk")]
    assert len(functions) == 3
    clock = Clock()
    state = lambda client, **unused: copy.deepcopy(client.state)

    def fight(client, target, skills=None):
        client.fights += 1
        client.fight_skills.append(skills)
        client.state["targets"] = []

    namespace = dict(Path=Path, json=json, time=clock, AutomationError=AutomationError,
                     npc_attackable=npc_attackable,
                     idle=state, story_state=state, fight=fight, LIFE_ITEM="medicine")
    exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), "exec"), namespace)
    go = namespace["go"]
    checks = check_practice_completion()
    calls = []
    first = dict(id=492, kind="npc", name="柴嵩", interactive=True, position=dict(x=79, y=31))
    second = dict(id=493, kind="npc", name="柴嵩", interactive=False, position=dict(x=41, y=105))
    interaction = SimpleNamespace(
        state=dict(map="中都.map", generation=8, targets=[first, second]),
        act=lambda command, **arguments: calls.append((command, arguments)))
    namespace["talk"](interaction, "柴嵩")
    assert len(calls) == 1 and calls[0][0] == "Interact" and calls[0][1]["targetId"] == 492
    checks += 1
    calls.clear()
    second["interactive"] = True
    check_error(lambda: namespace["talk"](interaction, "柴嵩"), "Ambiguous interaction")
    assert not calls
    checks += 1
    interaction.state["targets"] = [dict(id=600, kind="object", name="宝箱", position=dict(x=1, y=2))]
    namespace["talk"](interaction, "宝箱", (1, 2))
    assert len(calls) == 1 and calls[0][0] == "Interact" and calls[0][1]["targetId"] == 600
    checks += 1

    calls.clear()
    interaction.state["targets"] = [dict(id=7, kind="npc", name="唐影", interactive=True,
                                          position=dict(x=23, y=6))]
    def interrupted_interaction(command, **arguments):
        calls.append(arguments["targetId"])
        if len(calls) <= 3:
            interaction.state["targets"][0]["id"] = 8
            raise AutomationError("Interact: interaction_not_observed")
    interaction.act = interrupted_interaction
    namespace["talk"](interaction, "唐影")
    assert calls == [7, 8, 8, 8], "Refresh identity after the three normal approach scripts"
    checks += 1
    with tempfile.TemporaryDirectory(prefix="jxqy-go-") as temporary:
        root = Path(temporary)

        def client():
            nonlocal checks
            checks += 1
            return FakeClient(root / str(checks))

        c = client()
        c.on_submit = lambda: c.append(line(start()) + line(finish()))
        assert go(c, 1, 2, script="test/trap.txt")["map"] == "old.map"

        c = client()
        check_error(lambda: go(c, 1, 2, script="test/trap.txt"), "did not complete requested script")

        c = client()
        c.append(line(start()) + line(finish()))
        check_error(lambda: go(c, 1, 2, script="test/trap.txt"), "did not complete requested script")

        c = client()
        tail = line(finish())
        c.on_submit = lambda: c.append(line(start()) + tail[:17])
        c.callbacks[2] = lambda: c.append(tail[17:])
        assert go(c, 1, 2, script="test/trap.txt")["map"] == "old.map"

        c = client()
        c.append(line(start())[:-1])
        c.on_submit = lambda: c.append(b"\n" + line(finish()))
        check_error(lambda: go(c, 1, 2, script="test/trap.txt"), "did not complete requested script")

        c = client()
        c.on_submit = lambda: c.append(line(start()) + line(finish(status="runtime-error")))
        check_error(lambda: go(c, 1, 2, script="test/trap.txt"), "Move script failed")

        c = client()
        c.statuses = [{"status": "cancelled", "reason": "world_changed"}]
        c.callbacks[1] = lambda: c.state.update(generation=2, map="new.map")
        assert go(c, 1, 2, destination="new.map")["map"] == "new.map"

        c = client()
        c.statuses = [{"status": "cancelled", "reason": "world_changed"}]
        c.callbacks[1] = lambda: c.state.update(generation=2, map="new.map")
        check_error(lambda: go(c, 1, 2), "Unexpected map change")

        for new_map, completed in (("new.map", True), ("other.map", True), ("new.map", False)):
            c = client()
            c.statuses = [dict(status="running")]
            c.state["targets"] = [dict(id=1, kind="npc", hostile=True, life=100,
                                       action=0, position=dict(x=1, y=1))]
            def crossing_fight(client, target, skills=None):
                client.state.update(generation=2, map=new_map, targets=[])
                if completed:
                    client.append(line(start()) + line(finish()))
                raise AutomationError("StartCombat: world_changed")
            with patch.dict(namespace, fight=crossing_fight):
                if new_map == "new.map" and completed:
                    assert go(c, 1, 2, destination="new.map", script="test/trap.txt")["map"] == "new.map"
                else:
                    check_error(lambda: go(c, 1, 2, destination="new.map", script="test/trap.txt"),
                                "world_changed" if new_map != "new.map" else "did not complete requested script")

        c = client()
        c.statuses = [dict(status="running")]
        c.on_submit = lambda: c.state.update(script="script/map/test/trap7.txt", worldInput=False)
        check_error(lambda: go(c, 1, 2, combat=False), "Unexpected script during movement")
        assert c.submissions == 1 and c.cancelled == 1, "A resetting trap must not repeat the move"

        for walking, clears in ((True, True), (True, False), (False, False)):
            c = client()
            c.state["targets"] = [dict(kind="npc", hasWalkAction=walking, position=dict(x=1, y=2))]
            c.statuses = [dict(status="failed", reason="blocked_destination")]
            if clears:
                c.statuses.append(dict(status="succeeded", reason="arrived"))
                go(c, 1, 2)
                assert c.submissions == 2
            else:
                check_error(lambda: go(c, 1, 2), "blocked_destination")
                assert c.submissions == (11 if walking else 1), "Only transient NPC occupancy has bounded retries"

        c = client()
        c.state["targets"] = [dict(kind="npc", hasWalkAction=True, position=dict(x=1, y=1))]
        c.statuses = [dict(status="failed", reason="blocked_destination"), dict(status="succeeded")]
        go(c, 1, 2)
        assert c.submissions == 2, "A moving NPC can reserve its next tile before reaching it"

        c = client()
        c.state["targets"] = [dict(kind="npc", hasWalkAction=True, hostile=False, position=dict(x=1, y=1))]
        c.statuses = [dict(status="failed", reason="no_progress"), dict(status="succeeded")]
        go(c, 1, 2)
        assert c.submissions == 2 and c.fights == 0, "Wait for a neutral doorway NPC without attacking it"

        for combat, reason in ((True, "blocked_destination"), (False, "blocked_destination"),
                               (True, "no_progress")):
            c = client()
            c.state["targets"] = [dict(id=7, kind="npc", hasWalkAction=True, hostile=True,
                                       life=500, action=0, position=dict(x=1, y=2))]
            c.statuses = [dict(status="failed", reason=reason)]
            if combat and reason == "blocked_destination":
                c.statuses.append(dict(status="succeeded"))
                go(c, 1, 2)
                assert c.fights == 1 and c.submissions == 2 and c.cancelled == 0
            else:
                check_error(lambda: go(c, 1, 2, combat=combat), reason)
                assert c.fights == 0, "Only ordinary hostile occupancy permits combat recovery"

        c = client()
        c.on_submit = lambda: c.append(line(start()) + line(finish()))
        c.statuses = [{"status": "cancelled", "reason": "world_changed"}]
        c.callbacks[1] = lambda: c.state.update(generation=2, map="new.map")
        assert go(c, 1, 2, script="test/trap.txt")["map"] == "new.map"

        c = client()
        c.state["targets"] = [{"name": "灰狼", "hostile": True, "life": 100, "position": {"x": 1, "y": 0}}]
        c.statuses = [{"status": "running"}, {"status": "succeeded"}]
        go(c, 1, 2, combat=False)
        assert c.fights == c.cancelled == 0 and c.submissions == 1

        c = client()
        c.state["player"]["canFight"] = False
        c.state["targets"] = [dict(id=7, name="enemy", kind="npc", hostile=True,
                                   life=500, position=dict(x=1, y=0))]
        c.statuses = [dict(status="running"), dict(status="succeeded")]
        go(c, 1, 2)
        assert c.fights == c.cancelled == 0, "Normal travel must obey the player's combat permission"

        for error in ("target_unreachable", "no_progress", "fight_disabled"):
            c = client()
            c.state["targets"] = [dict(id=7, kind="npc", name="optional enemy", hostile=True,
                                       life=500, position=dict(x=1, y=0))]
            c.statuses = [dict(status="running"), dict(status="running"), dict(status="succeeded")]
            fought = []
            def failed_optional_fight(client, target):
                fought.append(target["id"])
                raise AutomationError("StartCombat: " + error)
            with patch.dict(namespace, fight=failed_optional_fight):
                if error in ("target_unreachable", "no_progress"):
                    go(c, 1, 2)
                    assert fought == [7] and c.submissions == 2
                else:
                    check_error(lambda: go(c, 1, 2), error)
                    assert c.submissions == 1

        c = client()
        c.state["targets"] = [dict(id=identity, kind="npc", name="optional enemy", hostile=True,
                                   life=500, position=dict(x=identity - 6, y=0)) for identity in (7, 8)]
        c.statuses = [dict(status="running")] * 3 + [dict(status="succeeded")]
        fought = []
        def mixed_optional_fight(client, target):
            fought.append(target["id"])
            if target["id"] == 7:
                raise AutomationError("StartCombat: no_progress")
            client.state["targets"] = [actor for actor in client.state["targets"] if actor["id"] != 8]
        with patch.dict(namespace, fight=mixed_optional_fight):
            go(c, 1, 2)
        assert fought == [7, 8] and c.submissions == 3, "A successful fight must not retry deferred targets"
        checks += 1

        for finished, changed_world, reason in (
                (True, False, "invalid_enemy"), (False, False, "invalid_enemy"), (True, True, "invalid_enemy"),
                (True, False, "stale_target"), (False, False, "stale_target"), (True, True, "stale_target")):
            c = client()
            c.state["targets"] = [dict(id=7, name="optional enemy", kind="npc", hostile=True,
                                       life=500, position=dict(x=1, y=0))]
            c.statuses = [dict(status="running"), dict(status="running"), dict(status="succeeded")]
            def finished_optional_fight(client, target):
                if finished:
                    client.state["targets"] = []
                if changed_world:
                    client.state["generation"] += 1
                raise AutomationError("StartCombat: " + reason)
            with patch.dict(namespace, fight=finished_optional_fight):
                if finished and not changed_world:
                    go(c, 1, 2)
                    assert c.submissions == 2
                else:
                    check_error(lambda: go(c, 1, 2), reason)
                    assert c.submissions == 1

        for hostile in (False, True):
            c = client()
            c.state["player"]["life"] = 20
            c.state["targets"] = [{"hostile": hostile, "life": 100}]
            c.statuses = [{"status": "running"}, {"status": "succeeded"}]
            used = []
            c.act = lambda command, **arguments: used.append(command)
            namespace["select_medicine"] = lambda snapshot, attribute: {"slot": 0}
            go(c, 1, 2, combat=False)
            assert used == (["UseItem"] if hostile else []), "Preserve medicine during safe town travel"

        for name, life, eligibility, should_fight in (
                ("灰狼", 100, {}, True), ("金国弓箭兵1", 100, {}, True),
                ("高级枪手", 0, {"attackable": True}, True),
                ("高级枪手", 100, {"attackable": False}, False),
                ("灰狼", 100, {"visibleFromPlayer": True}, True),
                ("灰狼", 100, {"visibleFromPlayer": False}, False),
                ("灰狼", 0, {}, False)):
            c = client()
            c.state["targets"] = [{"name": name, "hostile": True, "life": life, **eligibility,
                                   "position": {"x": 1, "y": 0}}]
            c.statuses = [{"status": "running"}, {"status": "succeeded"}]
            go(c, 1, 2, combat=True)
            assert c.fights == c.cancelled == int(should_fight) and c.submissions == 1 + int(should_fight)
            assert c.fight_skills == ([None] if should_fight else []), (name, c.fight_skills)

        c = client()
        c.state["targets"] = [dict(name="灰狼", hostile=True, life=100,
                                   position=dict(x=1, y=0))]
        c.statuses = [{"status": "running"}, {"status": "succeeded"}]
        submitted = []
        normal_submit = c.submit
        def record_run(command, **arguments):
            submitted.append(arguments)
            return normal_submit(command, **arguments)
        c.submit = record_run
        go(c, 1, 2, running=True)
        assert c.fights == 1 and len(submitted) == 2
        assert all(arguments["running"] for arguments in submitted)
        checks += 1

        c = client()
        c.on_submit = lambda: c.append(line(start()) + line(finish()))
        c.callbacks[1] = lambda: c.state.update(generation=2, map="wrong.map")
        check_error(lambda: go(c, 1, 2, script="test/trap.txt", destination="expected.map"), "Unexpected map")

    target_function = next(node for node in tree.body
                           if isinstance(node, ast.FunctionDef) and node.name == "late_target")
    exec(compile(ast.Module(body=[target_function], type_ignores=[]), str(source), "exec"), namespace)
    for target in (dict(life=0, action=0, attackable=False, interactive=True),
                   dict(life=0, action=5, attackable=True, interactive=False)):
        c = SimpleNamespace(state={"targets": [dict(id=1, kind="npc", name="target", **target)]})
        assert namespace["late_target"](c, "target")["id"] == 1
        checks += 1
    for action in (11, 255):
        c = SimpleNamespace(state={"targets": [dict(id=1, kind="npc", name="target", life=100,
                                                   action=action, attackable=False, interactive=True)]})
        check_error(lambda: namespace["late_target"](c, "target"), "Cannot identify")
        checks += 1

    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                 and node.name in ("fight", "continue_practice")]
    assert len(functions) == 2
    namespace.update(select_medicine=select_medicine, MANA_ITEM="mana-medicine")
    exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), "exec"), namespace)
    for level, nearby, attackable, explicit, expected in (
            (1, True, True, None, [0]), (10, True, True, None, [1, 0]),
            (10, False, True, None, [0]), (10, True, False, None, [0]),
            (10, True, True, (0,), [0])):
        target = dict(id=7, name="enemy", life=2000, hostile=True, attackable=True,
                      position=dict(x=11, y=10), attackRadius=8)
        c = SimpleNamespace(state={
            "generation": 1, "map": "test.map", "inventory": [],
            "layout": {"magicQuickBegin": 36, "practiceSlot": 41},
            "magic": [dict(file="player-magic-江翻海沸.ini", slot=40, level=level),
                      dict(file="player-magic-洗髓经.ini", slot=37, level=level),
                      dict(file="player-magic-弯刀冷光.ini", slot=41, level=7)],
            "targets": [target, dict(id=8, name="other", life=1000, hostile=True, attackable=attackable,
                                     position=dict(x=12 if nearby else 30, y=10))],
            "player": {"life": 1000, "lifeMax": 1000, "mana": 1000, "manaMax": 1000}})
        c.observe = lambda: copy.deepcopy(c.state)
        def area_combat(command, **arguments):
            assert command == "StartCombat" and arguments["targetId"] == 7
            assert arguments["skills"] == expected, arguments
            return dict(kills=1)
        c.act = area_combat
        namespace["fight"](c, target, skills=explicit)
        checks += 1
    for filename, mana, cooldown, damage, expected in (
            ("player-magic-白虹贯日.ini", 30, 0, None, ["StartCombat"]),
            ("player-magic-白虹贯日.ini", 42, 0, None, ["CastSkill", "StartCombat"]),
            ("player-magic-白虹贯日.ini", 42, 100, None, ["StartCombat"]),
            ("player-magic-白虹贯日.ini", 42, 0, 100, ["CastSkill", "StartCombat"]),
            ("player-magic-白虹贯日.ini", 42, 0, 0, ["CastSkill"]),
            ("0player-magic-白虹贯日.ini", 1, 0, None, ["StartCombat"]),
            ("0player-magic-白虹贯日.ini", 2, 0, None, ["CastSkill", "StartCombat"]),
            ("0player-magic-白虹贯日.ini", 2, 100, None, ["StartCombat"])):
        commands = []
        c = SimpleNamespace(state={
            "generation": 1, "map": "test.map", "inventory": [],
            "layout": {"magicQuickBegin": 36, "practiceSlot": 41},
            "magic": [{"file": filename, "slot": 38,
                       "level": 2, "cooldownMs": cooldown}],
            "player": {"life": 400, "lifeMax": 1000, "mana": mana, "manaMax": 100}})

        def act(command, **arguments):
            commands.append(command)
            if command == "CastSkill":
                assert mana >= (2 if filename.startswith("0") else 42) and cooldown == 0, "Healing must obey level cost and cooldown"
                assert arguments["slot"] == 2
                if damage is not None:
                    c.state["player"]["life"] -= damage
                    raise AutomationError("CastSkill failed: action_not_executed")
            else:
                assert command == "StartCombat" and arguments["targetId"] == 7
                return {"kills": 1}

        c.act = act
        c.observe = lambda: copy.deepcopy(c.state)
        run_fight = lambda: namespace["fight"](c, {"id": 7, "name": "enemy", "life": 100}, skills=())
        if damage == 0:
            check_error(run_fight, "action_not_executed")
        else:
            run_fight()
        assert commands == expected, (mana, cooldown, damage, commands)
        checks += 1

    for name, radius, has_walk, fallback in (("高级弓箭兵", 10, None, False), ("陈三", 5, None, False),
                                            ("唐萧", 10, None, False), ("唐门弟子1", 10, None, False),
                                            ("灰狼", 1, None, True), ("近程敌人", 4, None, True),
                                            ("木人1", 11, False, True), ("可移动远程敌人", 11, True, False)):
        commands = []
        c.state["player"]["life"] = c.state["player"]["lifeMax"]
        c.state["player"]["mana"] = c.state["player"]["manaMax"]

        def combat(command, **arguments):
            assert command == "StartCombat"
            assert arguments["allowMeleeFallback"] is fallback
            assert arguments["skills"] == ([] if fallback else [0])
            assert "manaItem" not in arguments and "manaPercent" not in arguments
            commands.append(command)
            return {"kills": 1}

        c.act = combat
        target = dict(id=7, name=name, life=100, attackRadius=radius)
        if has_walk is not None:
            target["hasWalkAction"] = has_walk
        namespace["fight"](c, target, skills=())
        assert commands == ["StartCombat"]
        checks += 1
        # Night guard melee preference must not bypass native ranged retreat.
        namespace["fight"](c, target, skills=(), prefer_melee=True)
        assert commands == ["StartCombat", "StartCombat"]
        checks += 1

    for blue_stock in (False, True):
        commands = []
        c.state["player"].update(mana=10, manaMax=100)
        c.state["inventory"] = ([dict(file="goods-yaowu-1-凝神丹.ini", slot=1,
                                       quantity=1, cooldownMs=0)] if blue_stock else [])
        def recover(client, *, allow_combat):
            assert allow_combat
            commands.append("meditate")
            client.state["player"]["mana"] = client.state["player"]["manaMax"]
            return copy.deepcopy(client.state)
        def ranged_combat(command, **arguments):
            assert command == "StartCombat" and arguments["skills"] == [0]
            assert not arguments["allowMeleeFallback"] and arguments["targetId"] == 7
            assert ("manaItem" in arguments) is blue_stock
            commands.append(command)
            return dict(kills=1)
        namespace["meditate"] = recover
        c.act = ranged_combat
        namespace["fight"](c, dict(id=7, name="凤池家丁1", life=100, attackRadius=11),
                           skills=(), prefer_melee=True)
        assert commands == (["StartCombat"] if blue_stock else ["meditate", "StartCombat"])
        checks += 1

    c.state["inventory"] = []
    c.state["player"]["mana"] = 0
    commands.clear()
    def interrupted_recovery(client, **arguments):
        raise AutomationError("Meditation interrupted by world/input change")
    namespace["meditate"] = interrupted_recovery
    check_error(lambda: namespace["fight"](c, dict(id=7, name="弓箭手", life=100, attackRadius=11)),
                "world/input change")
    assert not commands, "Failed recovery must not fall back to archer pursuit"
    checks += 1

    for interruption in ("same_target", "world_changed", "target_gone", "other_error"):
        clock.now = 0
        commands = []
        c.state["generation"] = 1
        c.state["player"]["mana"] = c.state["player"]["manaMax"]
        enemy = dict(id=7, name="邵骑风", life=2000, attackRadius=11)
        c.state["targets"] = [enemy]
        def recovery(client, *, allow_combat, timeout):
            assert allow_combat and timeout <= 240
            commands.append(("meditate",))
            clock.sleep(10)
            client.state["player"]["mana"] = client.state["player"]["manaMax"]
            return copy.deepcopy(client.state)
        def ongoing_combat(command, **arguments):
            assert command == "StartCombat" and arguments["targetId"] == 7
            assert not arguments["allowMeleeFallback"]
            commands.append((command, arguments["timeoutMs"]))
            if len(commands) == 1:
                c.state["player"]["mana"] = 0
                if interruption == "world_changed": c.state["generation"] = 2
                if interruption == "target_gone": c.state["targets"] = []
                raise AutomationError("StartCombat: " + ("no_progress" if interruption == "other_error"
                                                          else "skill_resources_unavailable"))
            return dict(kills=1)
        namespace["meditate"] = recovery
        c.act = ongoing_combat
        if interruption == "same_target":
            namespace["fight"](c, enemy)
            assert [row[0] for row in commands] == ["StartCombat", "meditate", "StartCombat"]
            assert commands[-1][1] < commands[0][1], "Recovery shares the original combat deadline"
        else:
            check_error(lambda: namespace["fight"](c, enemy), "no_progress" if interruption == "other_error"
                        else "skill_resources_unavailable")
            assert len(commands) == 1, "A changed target/world or unrelated failure must not be retried"
        checks += 1

    for depletion in ("no_blue", "replacement", "still_in_stock", "life_item", "world_changed", "target_gone"):
        commands = []
        blue = "goods-yaowu-3-七巧补心丹.ini"
        alternate = "goods-yaowu-2-五花玉露丹.ini"
        c.state["generation"] = 1
        c.state["player"].update(life=1000, lifeMax=1000, mana=100, manaMax=100)
        c.state["inventory"] = [dict(file=blue, quantity=1, slot=0)]
        enemy = dict(id=7, name="赵节", life=12000, attackRadius=11)
        c.state["targets"] = [enemy]
        def depleted_combat(command, **arguments):
            assert command == "StartCombat" and arguments["targetId"] == 7
            assert not arguments["allowMeleeFallback"]
            commands.append((command, arguments))
            if len(commands) == 1:
                assert arguments["manaItem"] == blue
                c.state["player"]["mana"] = 0
                if depletion != "still_in_stock": c.state["inventory"] = []
                if depletion == "replacement":
                    c.state["inventory"] = [dict(file=alternate, quantity=1, slot=1)]
                if depletion == "world_changed": c.state["generation"] = 2
                if depletion == "target_gone": c.state["targets"] = []
                raise AutomationError("StartCombat: item_depleted: " +
                                      ("life-medicine" if depletion == "life_item" else blue))
            assert arguments.get("manaItem") == (alternate if depletion == "replacement" else None)
            return dict(kills=1)
        namespace["meditate"] = recovery
        c.act = depleted_combat
        if depletion in ("no_blue", "replacement"):
            namespace["fight"](c, enemy)
            assert [row[0] for row in commands] == (["StartCombat", "meditate", "StartCombat"]
                                                  if depletion == "no_blue" else ["StartCombat", "StartCombat"])
        else:
            check_error(lambda: namespace["fight"](c, enemy), "item_depleted")
            assert len(commands) == 1, "Only actual mana-item depletion with the same live target is recoverable"
        checks += 1

    from gameplay_automation import Client
    combat_clock = Clock()
    observations = []
    combat_client = SimpleNamespace(
        request=lambda command, **arguments: dict(command="StartCombat", status=(
            "running" if combat_clock.now < 2.1 else "succeeded")),
        observe=lambda: observations.append(combat_clock.now))
    with patch("gameplay_automation.time.monotonic", combat_clock.monotonic), \
            patch("gameplay_automation.time.sleep", combat_clock.sleep):
        assert Client.wait_action(combat_client, 7)["status"] == "succeeded"
    assert len(observations) == 3 and all(right - left >= 1
                                        for left, right in zip(observations, observations[1:]))
    checks += 1

    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                 and node.name == "cuiyan_first_visit"]
    assert len(functions) == 1
    for start_map, phase, delivery, box in (
            ("别离村.map", "", "0", False), ("翠烟门.map", "0", "0", False),
            ("翠烟门.map", "1", "0", False), ("别离村.map", "2", "0", False),
            ("别离村-翠烟门.map", "0", "1", True),
            ("别离村-翠烟门.map", "0", "0", True),
            ("别离村-翠烟门.map", "0", "1", False)):
        commands = []
        c = SimpleNamespace(state={"map": start_map,
                                  "variables": {"CuiYanMen2Finish": phase, "CuiYanMen2CunLan": delivery},
                                  "inventory": [dict(file="goods-sj-7-唐门宝箱.ini", quantity=1)] if box else []})
        c.observe = lambda variables=(): copy.deepcopy(c.state)
        c.save_or_load = lambda slot: commands.append(("save", slot))

        def visit_go(client, x, y, *, destination=None, combat=True):
            if destination == "别离村.map":
                assert (x, y) == (67, 24), "Use the reachable main exit"
            if destination:
                commands.append(("go", destination))
                client.state["map"] = destination
            else:
                assert (x, y) == (60, 60) and combat is False
                commands.append(("go", x, y))

        def visit_talk(client, name, position):
            commands.append(("talk", name, position))
            if name == "春兰":
                assert position == (127, 95)
                client.state["variables"]["CuiYanMen2CunLan"] = "1"
                client.state["inventory"] = [dict(file="goods-sj-7-唐门宝箱.ini", quantity=1)]
            else:
                assert name == "秋依水" and position == (128, 96)
                assert client.state["variables"]["CuiYanMen2CunLan"] == "1"
                assert client.state["inventory"][0]["quantity"] == 1

        def visit_verify(client, **variables):
            assert all(client.state["variables"].get(key) == str(value) for key, value in variables.items())
            commands.append(("verify",))
            return client.observe()

        namespace.update(idle=state, go=visit_go, talk=visit_talk, verify=visit_verify,
                         item=lambda snapshot, filename: next(row for row in snapshot["inventory"]
                                                              if row["file"] == filename),
                         checkpoint=lambda *arguments: None)
        exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), "exec"), namespace)
        def visit_bieli(*arguments, **options):
            assert options == dict(dingshen_magic_file="player-magic-定身法.ini", scroll_reward_variable=None)
            commands.append(("bieli",))
        with patch("jxqy2_sidequests.bieli_sidequests", visit_bieli), \
                patch("jxqy2_mainline_supplies.meditate", lambda *arguments: commands.append(("meditate",))):
            if phase in ("1", "2"):
                check_error(lambda: namespace["cuiyan_first_visit"](c, Path("unused")), "unavailable")
                assert not commands, "Later story stages must be rejected before movement"
            elif start_map == "别离村-翠烟门.map" and (delivery != "1" or not box):
                check_error(lambda: namespace["cuiyan_first_visit"](c, Path("unused")), "requires Chunlan")
                assert not commands, "Return recovery needs both delivery flag and held box"
            else:
                namespace["cuiyan_first_visit"](c, Path("unused"))
                visits = [command for command in commands if command[0] in ("talk", "verify")]
                assert visits == ([] if start_map == "别离村-翠烟门.map" else [
                    ("talk", "春兰", (127, 95)), ("verify",), ("talk", "秋依水", (128, 96))])
                assert (("bieli",) in commands) == (start_map == "别离村.map")
                assert sum(command == ("go", "翠烟门.map") for command in commands) == (start_map == "别离村.map")
                assert commands[-5:] == [("go", "别离村.map"), ("go", 60, 60),
                                         ("meditate",), ("go", "别离村-唐门.map"), ("save", 2)]
        checks += 1

    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                 and node.name == "tangmen_capture"]
    assert len(functions) == 1
    for start_map, phase, learned, life, medicine_count in (
            ("别离村-唐门.map", "0", False, 500, 0),
            ("唐门.map", "1", False, 500, 0), ("唐门.map", "1", True, 500, 0),
            ("唐门.map", "2", False, 500, 0),
            ("唐门.map", "1", False, 0, 2), ("唐门.map", "1", False, 0, 0)):
        commands = []
        book = dict(file="book-唐门秘笈.ini", quantity=1, slot=4)
        medicine = dict(file="goods-yaowu-5-大补散.ini", quantity=medicine_count, slot=8)
        c = SimpleNamespace(state={"map": start_map, "generation": 1, "worldInput": True,
                                  "worldAction": None,
                                  "layout": dict(magicQuickBegin=20),
                                  "magic": [dict(file="player-magic-白虹贯日.ini", slot=22,
                                                 level=5, cooldownMs=0)],
                                  "player": dict(life=life, lifeMax=3800, mana=3, manaMax=156),
                                  "variables": {"TanMenFighting": "0", "CuiYanMen2Finish": phase,
                                                "NaDaoMiJiMusic": "1" if learned else "0"},
                                  "inventory": ([book] if learned else []) + ([medicine] if medicine_count else []),
                                  "targets": [dict(name="唐萧", hostile=True, life=100, id=7)]})
        c.observe = lambda variables=(): copy.deepcopy(c.state)
        c.save_or_load = lambda slot: commands.append(("save", slot))

        def tang_go(client, x, y, *, destination=None, combat=True):
            commands.append(("go", destination))
            if destination is None:
                assert client.state["map"] == "别离村.map" and (x, y) == (60, 60)
                assert combat is False
                return
            if destination == "唐门.map":
                assert (x, y) == (12, 13) and combat is False
                client.state["variables"]["TanMenFighting"] = "1"
            if destination == "别离村-唐门.map":
                assert (x, y) == (47, 296) and book in client.state["inventory"]
            client.state["map"] = destination

        def tang_fight(client, target, skills):
            assert target["name"] == "唐萧" and skills == ()
            commands.append(("fight",))
            client.state["variables"].update(TanMenFighting="0", CuiYanMen2Finish="1")

        def tang_talk(client, name, position):
            assert client.state["player"]["life"] > 0, "Recover from zero life before world interactions"
            commands.append(("talk", name, position))
            if name == "唐影":
                assert position == (109, 171) and not learned
                client.state["variables"]["NaDaoMiJiMusic"] = "1"
                client.state["inventory"].append(book)
            else:
                assert name == "唐离" and position == (82, 123)
                assert book in client.state["inventory"]

        def tang_use(command, **arguments):
            if command == "CastSkill":
                assert c.state["map"] == "别离村.map" and arguments["slot"] == 2
                player = c.state["player"]
                assert player["mana"] >= 78 and c.state["magic"][0]["cooldownMs"] == 0
                player["life"] = min(player["lifeMax"], player["life"] + 900)
                player["mana"] -= 78
                c.state["magic"][0]["cooldownMs"] = 800
                commands.append(("heal",))
                return
            if arguments["slot"] == 8:
                assert command == "UseItem" and c.state["map"] == "唐门.map"
                assert c.state["player"]["life"] == 0 and medicine["quantity"] == 2
                medicine["quantity"] -= 1
                c.state["player"]["life"] = 600
                commands.append(("medicine",))
                return
            assert command == "UseItem" and c.state["map"] == "别离村-唐门.map"
            assert arguments["slot"] == 4
            commands.append(("use-book",))
            c.state["inventory"].remove(book)
            c.state["magic"].append(dict(file="player-magic-漫天花雨手法.ini", level=1))

        def tang_meditate(client):
            assert client.state["map"] == "别离村.map"
            commands.append(("meditate",))
            client.state["player"]["mana"] = client.state["player"]["manaMax"]
            return client.observe()

        def tang_wait(predicate, timeout, description):
            assert timeout == 10 and description == "Baihong recovery cooldown"
            if c.state["magic"][0]["cooldownMs"]:
                assert not predicate(c.observe()), "Cooling magic must not be cast"
                c.state["magic"][0]["cooldownMs"] = 0
                commands.append(("cooldown",))
            assert predicate(c.observe())
            return c.observe()

        c.act = tang_use
        c.wait_until = tang_wait
        namespace.update(idle=state, go=tang_go, talk=tang_talk, fight=tang_fight, verify=visit_verify,
                         meditate=tang_meditate,
                         item=lambda snapshot, filename, collection="inventory": next(
                             row for row in snapshot[collection] if row["file"] == filename))
        exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), "exec"), namespace)
        if phase == "2":
            check_error(lambda: namespace["tangmen_capture"](c, Path("unused")), "completed capture")
            assert not commands
        elif life == 0 and medicine_count == 0:
            check_error(lambda: namespace["tangmen_capture"](c, Path("unused")), "no ready healing medicine")
            assert not any(command[0] in ("talk", "go", "medicine") for command in commands)
        else:
            namespace["tangmen_capture"](c, Path("unused"))
            assert (("fight",) in commands) == (start_map == "别离村-唐门.map")
            assert (("talk", "唐影", (109, 171)) in commands) is not learned
            assert ("talk", "唐离", (82, 123)) in commands
            assert (("medicine",) in commands) == (life == 0)
            if life == 0:
                assert medicine["quantity"] == 1
            assert c.state["player"]["life"] == 3800 and c.state["player"]["mana"] == 156
            assert commands.count(("heal",)) == 4 and commands.count(("meditate",)) == 3
            assert commands.count(("cooldown",)) == 3
            assert commands[-3] == ("meditate",)
            assert commands[-1] == ("save", 2)
        checks += 1

    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                 and node.name in ("cuiyan_second_visit", "late_records", "late_completed_script")]
    assert len(functions) == 3
    for scenario in ("new", "four", "five", "missing-baseline", "stale-load", "duplicate", "wrong-stage"):
        commands = []
        resumed = scenario != "new"
        count = 5 if scenario == "five" else 4 if resumed else 0
        c = SimpleNamespace(state={
            "map": "翠烟门.map" if resumed else "别离村-翠烟门.map",
            "variables": dict(CuiYanMen2Finish="2", CuiYanMen2Fighting="3" if scenario == "wrong-stage" else "2",
                              CuiYanMen2DiZi=str(count)),
            "targets": [dict(name="高级女剑客1", hostile=True, position=dict(x=110, y=134))]})
        c.observe = lambda variables=(): copy.deepcopy(c.state)

        def investigation_go(client, x, y, *, destination, combat):
            assert (x, y) == (3, 155) and destination == "翠烟门.map" and combat is False
            commands.append(("gate",))
            client.state["map"] = destination
            client.state["variables"]["CuiYanMen2Fighting"] = "1"

        def investigation_fight(client, target, skills):
            assert target["name"] == "高级女剑客1" and skills == ()
            commands.append(("fight",))
            client.state["variables"]["CuiYanMen2Fighting"] = "2"

        def investigation_talk(client, name, position):
            commands.append(("talk", name, position))
            if name == "秋依水":
                assert client.state["variables"]["CuiYanMen2DiZi"] == "5"
                raise StopIteration("Investigation complete")
            assert (name, position) in (("小姐", (81, 138)), ("小姐", (69, 170)),
                                        ("女子抚琴", (93, 172)), ("小姐1", (100, 202)),
                                        ("普通女剑客2", (90, 237)))
            client.state["variables"]["CuiYanMen2DiZi"] = str(int(client.state["variables"]["CuiYanMen2DiZi"]) + 1)

        namespace.update(idle=state, go=investigation_go, fight=investigation_fight,
                         talk=investigation_talk, verify=visit_verify)
        exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), "exec"), namespace)
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            trace = output / "user-data/automation/trace.jsonl"
            trace.parent.mkdir(parents=True)
            records = []
            if scenario != "missing-baseline":
                records = [dict(eventType="script.start", executionId=1, virtualPath="script/map/翠烟门/春兰2.txt"),
                           dict(eventType="script.finish", executionId=1, status="completed")]
            for identity, index in enumerate((1, 2, 5, 4, 6)[:count], 2):
                records.extend((dict(eventType="script.start", executionId=identity,
                                     virtualPath=f"script/map/翠烟门/弟子{index}.txt"),
                                dict(eventType="script.finish", executionId=identity, status="completed")))
            if scenario == "stale-load":
                records.append(dict(eventType="map.change", target="翠烟门.map"))
            if scenario == "duplicate":
                records[-2]["virtualPath"] = "script/map/翠烟门/弟子1.txt"
            trace.write_bytes(b"".join(line(record) for record in records) + b'{"unfinished":')
            failures = {"missing-baseline": "Missing script trace", "stale-load": "predates",
                        "duplicate": "distinct completed", "wrong-stage": "active investigation"}
            if scenario in failures:
                check_error(lambda: namespace["cuiyan_second_visit"](c, output), failures[scenario])
                assert all(command == ("verify",) for command in commands)
            else:
                try:
                    namespace["cuiyan_second_visit"](c, output)
                except StopIteration as error:
                    assert str(error) == "Investigation complete"
                else:
                    raise AssertionError("Investigation must reach Qiu only after five distinct witnesses")
                assert (("fight",) in commands) is not resumed
                witnesses = [command for command in commands if command[0] == "talk" and command[1] != "秋依水"]
                assert len(witnesses) == (5 if not resumed else 5 - count)
                if scenario != "five":
                    assert witnesses[-1] == ("talk", "普通女剑客2", (90, 237))
        checks += 1

    npc = configparser.ConfigParser(interpolation=None)
    npc.read(source.parent.parent / "assets/jxqy2/ini/save/cuiym3.npc", encoding="utf-8")
    assert dict(npc["npc027"])["name"] == "普通女剑客2"
    assert (npc.getint("npc027", "mapx"), npc.getint("npc027", "mapy"), npc.getint("npc027", "action")) == (90, 237, 0)
    assert npc["npc027"]["scriptfile"] == "弟子6.txt"
    assert 'add("CuiYanMen2DiZi",1)' in (source.parent.parent / "assets/jxqy2/script/map/翠烟门/弟子6.txt").read_text(encoding="utf-8")
    checks += 1

    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                 and node.name == "hanyang_tianwang"]
    assert len(functions) == 1
    for delivered, learned in (("3", True), ("3", False), ("2", True)):
        commands = []
        c = SimpleNamespace(state={"map": "汉阳.map",
                                  "variables": dict(HanYanCTMSZL=delivered, TianWangPiEr="4"),
                                  "magic": [dict(file="player-magic-大力金刚掌.ini", level=1)] if learned else []})
        c.observe = lambda variables=(): copy.deepcopy(c.state)
        c.save_or_load = lambda slot: commands.append(("save", slot))
        namespace.update(idle=state, duan_family_side_story=lambda client, output, **kwargs: commands.append(("duan",)))
        exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), "exec"), namespace)
        if delivered == "3" and learned:
            namespace["hanyang_tianwang"](c, Path("unused"))
            assert commands == [("duan",), ("save", 2)]
        else:
            check_error(lambda: namespace["hanyang_tianwang"](c, Path("unused")), "delivered letter")
            assert not commands
        checks += 1

    commands = []
    c.state = dict(map="段家庄.map")
    namespace["hanyang_tianwang"](c, Path("unused"))
    assert commands == [("duan",), ("save", 2)]
    checks += 1

    for position, magic_prefix, learned in (
            (dict(x=42, y=29), "player-magic-", True),
            (dict(x=42, y=35), "player-magic-", True),
            (dict(x=42, y=29), "0player-magic-", True),
            (dict(x=42, y=29), "0player-magic-", False)):
        commands = []
        c = SimpleNamespace(state={"map": "汉阳.map", "generation": 9,
                                  "variables": {},
                                  "player": dict(position=position, canJump=True),
                                  "magic": [dict(file=magic_prefix + "依风剑法.ini")] if learned else []})
        c.observe = lambda variables=(): copy.deepcopy(c.state)

        def boat_jump(command, **arguments):
            assert command == "JumpTo" and arguments == dict(timeout=30, generation=9, x=42, y=35, timeoutMs=25000)
            assert c.state["player"]["position"] == dict(x=42, y=29)
            commands.append(("jump",))
            c.state["player"]["position"] = dict(x=42, y=35)

        def boat_restock(client):
            assert client.state["player"]["position"] == dict(x=42, y=35)
            raise StopIteration("Standing on the Hanyang bank")

        c.act = boat_jump
        namespace.update(idle=state, restock=boat_restock)
        exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), "exec"), namespace)
        try:
            namespace["hanyang_tianwang"](c, Path("unused"), magic_file_prefix=magic_prefix,
                                          yifeng_chest_required=learned)
        except StopIteration as error:
            assert str(error) == "Standing on the Hanyang bank"
        else:
            raise AssertionError("Hanyang arrival must continue from the bank")
        assert bool(commands) == (position == dict(x=42, y=29))
        checks += 1

    commands = []
    c = SimpleNamespace(state={"map": "汉阳.map", "variables": {}, "inventory": [],
                              "magic": [dict(file="player-magic-依风剑法.ini")],
                              "player": dict(position=dict(x=42, y=34))})
    c.observe = lambda variables=(): copy.deepcopy(c.state)

    def island_jump(client, origin, destination):
        assert client.state["map"] == "天王岛.map"
        commands.append(("jump", origin, destination))
        client.state["player"]["position"] = dict(x=destination[0], y=destination[1])

    def island_go(client, x, y, *, combat):
        assert client.state["map"] == "天王岛-地下迷宫.map"
        assert (x, y) in ((7, 43), (61, 26))
        assert combat is False, "The quest chest route must not clear every nearby wooden fighter"
        commands.append(("go", x, y))

    def island_talk(client, name, position):
        commands.append(("talk", name, position))
        variables = client.state["variables"]
        if name == "史忠良":
            variables["HanYanCTMSZL"] = "1"
        elif name == "楚天盟弟子乙":
            variables["HanYanCTMHouMenDiZi"] = "2"
        elif name == "天王帮弟子" and position == (41, 37):
            variables["HanYanTWBDiZi"] = "1"
            client.state["map"] = "天王岛.map"
        elif name == "苹儿":
            if position == (45, 75):
                assert commands[-2] == ("jump", (29, 115), (21, 115))
                raise StopIteration("Verified ordinary return to Ping'er")
            variables["TianWangPiEr"] = "1" if position == (57, 77) else "2"
        elif name == "梯子2":
            assert position == (36, 112) and commands[-2] == ("jump", (21, 115), (29, 115))
            client.state["map"] = "天王岛-地下迷宫.map"
        elif name == "宝箱":
            assert position == (7, 42) and commands[-2] == ("go", 7, 43)
            variables["TianWangPiEr"] = "3"
            client.state["inventory"].append(dict(file="grade-five-reward", quantity=1))
        elif name == "梯子":
            assert position == (61, 25)
            client.state["map"] = "天王岛.map"

    namespace.update(idle=state, talk=island_talk, go=island_go, late_jump=island_jump,
                     restock=lambda client: commands.append(("restock",)),
                     meditate=lambda client: commands.append(("meditate",)), verify=visit_verify)
    exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), "exec"), namespace)
    try:
        namespace["hanyang_tianwang"](c, Path("unused"))
    except StopIteration as error:
        assert str(error) == "Verified ordinary return to Ping'er"
    else:
        raise AssertionError("The route must return from the maze through the island ladder")
    assert commands[:2] == [("restock",), ("meditate",)]
    assert [command for command in commands if command[0] == "jump"] == [
        ("jump", (21, 115), (29, 115)), ("jump", (29, 115), (21, 115))]
    checks += 1

    for phase in ("2", "3", "1"):
        commands = []
        c = SimpleNamespace(state={"map": "天王岛-地下迷宫.map", "variables": dict(TianWangPiEr=phase),
                                  "inventory": [], "player": dict(position=dict(x=13, y=47))})
        c.observe = lambda variables=(): copy.deepcopy(c.state)
        if phase != "2":
            check_error(lambda: namespace["hanyang_tianwang"](c, Path("unused")), "unopened quest chest")
            assert not commands
        else:
            try:
                namespace["hanyang_tianwang"](c, Path("unused"))
            except StopIteration as error:
                assert str(error) == "Verified ordinary return to Ping'er"
            else:
                raise AssertionError("The resumed maze route must still collect and verify its reward")
            assert commands[0] == ("go", 7, 43)
            assert c.state["variables"]["TianWangPiEr"] == "3"
            assert c.state["inventory"] == [dict(file="grade-five-reward", quantity=1)]
            assert ("go", 61, 26) in commands
            assert [command for command in commands if command[0] == "jump"] == [
                ("jump", (29, 115), (21, 115))]
        checks += 1

    assets = source.parents[1] / "assets/jxqy2"
    tiles = (assets / "map/天王岛.map").read_bytes()
    _, width, _, info_size, _ = struct.unpack_from("<5i", tiles, 64)
    header, _, _, count = struct.unpack_from("<4i", tiles, 84)
    offset = header + count * info_size
    for x in range(21, 30):
        for y in (114, 115, 116):
            index = offset + (y * width + x) * 10
            assert tiles[index + 6] == 0 or tiles[index + 6] & 0x20
            assert tiles[index + 7] == 0, "The pond jump must not cross a story trap"
    objects = configparser.ConfigParser(interpolation=None)
    objects.read(assets / "ini/save/twddxmg.obj", encoding="utf-8-sig")
    selected = [row for row in objects.values() if row.get("mapx") == "7" and row.get("mapy") == "42"]
    assert len(selected) == 1 and selected[0]["scriptfile"] == "迷宫宝箱.txt"
    reward = (assets / "script/map/天王岛-地下迷宫/迷宫宝箱.txt").read_text(encoding="utf-8")
    assert 'addrandgoods("5级物品.ini")' in reward and 'assign("TianWangPiEr",3)' in reward
    checks += 1

    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                 and node.name == "prepare_duan_magic"]
    assert len(functions) == 1
    for tianyi_level, fengxue_level, practice, experience_gain in (
            (5, 4, True, 0), (5, 4, True, 10), (4, 4, True, 0),
            (5, 0, True, 0), (5, 10, True, 0), (5, 4, False, 0)):
        commands = []
        tianyi, cold, fengxue = ("player-magic-天意剑诀.ini", "player-magic-寒霜掌.ini", "player-magic-风雪狂刀.ini")
        c = SimpleNamespace(state={"magic": [dict(file=tianyi, slot=99 if practice else 1, level=tianyi_level, exp=30),
                                            dict(file=cold, slot=20, level=9, exp=99),
                                            dict(file=fengxue, slot=23, level=fengxue_level, exp=80)],
                                  "layout": dict(practiceSlot=99, magicQuickBegin=20)})
        c.observe = lambda variables=(): copy.deepcopy(c.state)

        def duan_assign_magic(slot, quick_slot):
            assert (slot, quick_slot) == (99, 0)
            commands.append(("quick",))
            c.state["magic"][0]["slot"], c.state["magic"][1]["slot"] = 20, 99
            c.state["magic"][0]["exp"] += experience_gain

        def duan_assign_practice(slot):
            assert slot == c.state["magic"][2]["slot"] and commands[-1] == ("quick",)
            commands.append(("practice",))
            c.state["magic"][2]["slot"], c.state["magic"][1]["slot"] = 99, slot

        c.assign_magic, c.assign_practice = duan_assign_magic, duan_assign_practice
        namespace.update(idle=state, checkpoint=lambda client, output, name: commands.append(("checkpoint", name)),
                         item=lambda snapshot, filename, collection: next(row for row in snapshot[collection]
                                                                         if row["file"] == filename))
        exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), "exec"), namespace)
        namespace["prepare_duan_magic"](c, Path("unused"))
        if tianyi_level == 5 and 1 <= fengxue_level < 10 and practice:
            assert commands == [("checkpoint", "side-duan-magic-before"), ("quick",), ("practice",),
                                ("checkpoint", "side-duan-magic-prepared")]
            assert {spell["file"]: spell["slot"] for spell in c.state["magic"]} == {
                tianyi: 20, cold: 23, fengxue: 99}
            assert c.state["magic"][0]["exp"] == 30 + experience_gain
        else:
            assert not commands
        checks += 1
    namespace["checkpoint"] = lambda *arguments: None

    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                 and node.name == "duan_family_side_story"]
    assert len(functions) == 1
    for phase, closed in (("0", "0"), ("", "0"), ("1", "0"), ("0", "1")):
        commands = []
        c = SimpleNamespace(state={"map": "段家庄.map", "magic": [],
                                  "variables": dict(HanYanCTMSZL="3", DuanHuanShan=phase,
                                                    DuanJiaZhuangClose=closed)})
        c.observe = lambda variables=(): copy.deepcopy(c.state)

        def duan_go(client, x, y, *, destination=None, timeout=0, combat=True):
            assert combat is False, "The Duan quest approach and return must not clear every guard"
            commands.append(("go", x, y, destination))
            if destination:
                assert client.state["variables"]["DuanHuanShan"] == "1"
                client.state["map"] = destination

        def duan_fight(client, target):
            assert client.state["map"] == "段家庄.map" and target["name"] == "段环山"
            commands.append(("fight",))
            client.state["variables"]["DuanHuanShan"] = "1"

        def duan_talk(client, name, position):
            assert client.state["map"] == "汉阳.map" and (name, position) == ("史忠良", (89, 84))
            commands.append(("reward",))
            client.state["variables"]["DuanHuanShan"] = "0"
            client.state["magic"] = [dict(file="player-magic-大梦心法.ini", level=1)]

        namespace.update(idle=state, go=duan_go, fight=duan_fight, talk=duan_talk,
                         verify=visit_verify, late_target=lambda client, name, position: dict(name=name),
                         restock=lambda client: commands.append(("unexpected-restock",)),
                         meditate=lambda client: commands.append(("unexpected-meditate",)),
                         item=lambda snapshot, filename, collection: next(row for row in snapshot[collection]
                                                                         if row["file"] == filename))
        exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), "exec"), namespace)
        if closed == "1" or (phase or "0") != "0":
            check_error(lambda: namespace["duan_family_side_story"](c, Path("unused")),
                        "closed" if closed == "1" else "undefeated stage")
            assert not commands
        else:
            namespace["duan_family_side_story"](c, Path("unused"))
            assert commands[0] == ("go", 84, 42, None), "A temporary occupant does not change the route waypoint"
            assert commands.count(("fight",)) == 1 and commands[-2] == ("reward",)
            assert not any(command[0].startswith("unexpected-") for command in commands)
            assert [command[3] for command in commands if command[0] == "go" and command[3]] == [
                "汉阳-段家庄.map", "汉阳.map"]
            assert c.state["magic"] == [dict(file="player-magic-大梦心法.ini", level=1)]
        checks += 1

    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                 and node.name == "hanyang_to_zhongdu"]
    assert len(functions) == 1
    for scenario in ("ready", "missing-dameng", "missing-checkpoint", "missing-trace", "failed-trace"):
        commands = []
        c = SimpleNamespace(state={"map": "金兵营寨.map", "variables": dict(HanYanCTMSZL="3"),
                                  "targets": [], "magic": [] if scenario == "missing-dameng" else
                                  [dict(file="player-magic-大梦心法.ini", level=1)]})
        c.observe = lambda variables=(): copy.deepcopy(c.state)

        def camp_go(client, x, y, *, destination, combat):
            assert client.state["map"] == "金兵营寨.map"
            assert (x, y, destination, combat) == (97, 6, "汉阳-中都1.map", False)
            commands.append(("camp-exit",))
            client.state["map"] = destination

        def camp_jump(client, origin, destination):
            assert (origin, destination) == ((10, 47), (18, 47))
            raise StopIteration("Continued from Jin camp")

        namespace.update(idle=state, go=camp_go, verify=visit_verify, late_jump=camp_jump,
                         restock=lambda *args, **kwargs: commands.append(("unexpected-restock",)),
                         meditate=lambda client: commands.append(("unexpected-meditate",)),
                         meng_sparring=lambda client, output: commands.append(("unexpected-sparring",)))
        exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), "exec"), namespace)
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            if scenario != "missing-checkpoint":
                (output / "side-meng-sparring-won.json").write_text("{}", encoding="utf-8")
            trace = output / "user-data/automation/trace.jsonl"
            trace.parent.mkdir(parents=True)
            records = [] if scenario == "missing-trace" else [
                dict(eventType="script.start", executionId=7, virtualPath="script/map/汉阳小屋/die.txt"),
                dict(eventType="script.finish", executionId=7,
                     status="failed" if scenario == "failed-trace" else "completed")]
            trace.write_bytes(b"".join(line(record) for record in records))
            if scenario == "ready":
                try:
                    namespace["hanyang_to_zhongdu"](c, output)
                except StopIteration as error:
                    assert str(error) == "Continued from Jin camp"
                else:
                    raise AssertionError("Jin camp recovery must continue the normal exit route")
                assert commands == [("verify",), ("camp-exit",)]
            else:
                message = "requires Dameng" if scenario in ("missing-dameng", "missing-checkpoint") else (
                    "Missing script trace" if scenario == "missing-trace" else "did not complete normally")
                check_error(lambda: namespace["hanyang_to_zhongdu"](c, output), message)
                assert commands == [("verify",)]
        checks += 1

    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                 and node.name == "meng_sparring"]
    assert len(functions) == 1
    for outcome in ("win", "started-before", "lost", "missing-trace", "wrong-return",
                    "ambiguous-target", "wrong-stage"):
        commands, closed = [], []
        c = SimpleNamespace(state={
            "map": "长安.map" if outcome == "wrong-stage" else "汉阳.map",
            "variables": dict(HanYanCTMSZL="3", DuanHuanShan="0",
                              HanYanBXBMTW="1" if outcome == "started-before" else "0"),
            "magic": [dict(file="player-magic-大梦心法.ini")],
            "player": dict(life=3000, position=dict(x=66, y=129)),
            "targets": [dict(id=9, kind="npc", name="孟廷威", hostile=True,
                             life=3120, attackRadius=6)]})
        c.observe = lambda variables=(): copy.deepcopy(c.state)

        def meng_talk(client, name, position):
            assert (name, position) == ("孟廷威", (66, 130))
            commands.append(("challenge",))
            client.state["map"] = "汉阳小屋.map"
            client.state["variables"]["HanYanBXBMTW"] = "1"
            if outcome == "ambiguous-target":
                client.state["targets"] *= 2

        def meng_fight(client, target):
            assert target["id"] == 9 and target["attackRadius"] == 6
            assert client.state["map"] == "汉阳小屋.map"
            commands.append(("fight",))
            if outcome == "lost":
                raise AutomationError("StartCombat: player_dead")
            if outcome != "wrong-return":
                client.state["map"] = "汉阳.map"
                client.state["player"]["position"] = dict(x=65, y=128)
            return client.observe()

        def meng_trace(client, script):
            assert script == "汉阳小屋/die.txt"
            try:
                yield False, False
                assert ("fight",) in commands
                while True:
                    yield True, outcome != "missing-trace"
            finally:
                closed.append(True)

        def meng_checkpoint(client, output, name):
            commands.append(("checkpoint", name))
            return client.observe()

        namespace.update(idle=state, talk=meng_talk, fight=meng_fight, verify=visit_verify,
                         _move_script_evidence=meng_trace, checkpoint=meng_checkpoint)
        exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), "exec"), namespace)
        errors = {"lost": "player_dead", "missing-trace": "start flag is not a win",
                  "wrong-return": "normal victory", "ambiguous-target": "one living",
                  "wrong-stage": "completed Tianwang"}
        if outcome in errors:
            check_error(lambda: namespace["meng_sparring"](c, Path("unused")), errors[outcome])
            assert ("checkpoint", "side-meng-sparring-won") not in commands
        else:
            namespace["meng_sparring"](c, Path("unused"))
            assert commands[-1] == ("checkpoint", "side-meng-sparring-won")
            assert ("challenge",) in commands and ("fight",) in commands
        assert closed == ([] if outcome == "wrong-stage" else [True])
        checks += 1
    return checks


if __name__ == "__main__":
    with contextlib.redirect_stdout(io.StringIO()):
        checks = main()
    print(f"Mainline helper checks: {checks} passed")
