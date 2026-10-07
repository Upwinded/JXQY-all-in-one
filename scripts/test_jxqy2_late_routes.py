"""Offline chapter checks. No game process or pipe is opened."""
import ast
import copy
import configparser
import json
from pathlib import Path
import struct
import tempfile
from gameplay_automation import npc_attackable


class AutomationError(RuntimeError):
    pass


class Clock:
    now = 0
    def monotonic(self):
        return self.now
    def sleep(self, seconds):
        self.now += seconds


root = Path(__file__).resolve().parents[1]
tree = ast.parse((root / "scripts/run_jxqy2_mainline.py").read_text(encoding="utf-8"))
names = {"prepare_duan_magic", "duan_family_side_story", "zhongdu_shaolin_side_story", "tianwang_family_revisit",
         "compete_in_tournament", "tournament_resources", "late_completed_script", "late_records"}
functions = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
opponents = ("赵无双", "秋依水", "唐影", "孟廷威", "柴嵩", "杨干", "邵骑风", "史忠良", "唐离", "赵升权", "独孤剑")


def item(state, filename, collection="inventory"):
    found = [entry for entry in state[collection] if entry["file"] == filename]
    assert len(found) == 1, (filename, found)
    return found[0]


def verify(client, **variables):
    state = client.observe()
    assert all((state["variables"].get(key) or "0") == str(value) for key, value in variables.items())
    return state


class DuelClient:
    def __init__(self, lose=None, reason=None, omit_trace=False, no_mana=False, tianyi_level=7, mana_max=1000,
                 hidden_quick=False):
        self.stage, self.lose, self.reason = 1, lose, reason
        self.trace, self.commands, self.omit_trace = [], [], omit_trace
        self.state = dict(map="凤池山庄-比武场.map", generation=1, worldInput=True,
                          variables={"FCBW": "1", "HappyEnding": "0"},
                          magic=[dict(file="player-magic-梦蝶神功.ini", slot=40, level=1),
                                 dict(file="player-magic-天意剑诀.ini", slot=41, level=tianyi_level)],
                          layout={"magicQuickBegin": 40},
                          ui=[] if hidden_quick else [dict(name="bottom-magic-quick-0")],
                          inventory=[dict(file="life.ini", attribute="life", quantity=50)],
                          player=dict(life=1000, manaMax=mana_max, thew=1000))
        self.assignments = []
        if hidden_quick:
            self.state["magic"][0]["slot"], self.state["magic"][1]["slot"] = 41, 40
        if not no_mana:
            self.state["inventory"].append(dict(file="mana.ini", attribute="mana", quantity=50))
        self.target()
    def assign_magic(self, slot, quick_slot):
        assert quick_slot == 0
        self.assignments.append((slot, quick_slot))
        for spell in self.state["magic"]:
            if spell["slot"] == slot:
                spell["slot"] = 40
            elif spell["slot"] == 40:
                spell["slot"] = slot
    def target(self):
        self.state["targets"] = [dict(kind="npc", name=opponents[self.stage - 1], hostile=True,
                                      id=700 + self.stage, life=9000)]
    def observe(self, variables=()):
        return copy.deepcopy(self.state)
    def act(self, command, **arguments):
        assert command == "SetAutoDialogue"
    def submit(self, command, **arguments):
        assert command == "StartCombat" and arguments["generation"] == self.state["generation"]
        assert arguments["targetId"] == 700 + self.stage
        self.commands.append(arguments)
        return self.stage
    def completed(self, suffix):
        if self.omit_trace:
            return
        identity = len(self.trace) + 100
        self.trace.extend([dict(eventType="script.start", executionId=identity,
                                virtualPath="script/map/凤池山庄-比武场/" + suffix),
                           dict(eventType="script.finish", executionId=identity, status="completed")])
    def request(self, command, **arguments):
        assert command == "GetActionStatus"
        old_stage = self.stage
        if self.reason == "item_depleted: life.ini":
            return dict(status="failed", reason=self.reason, kills=0)
        if self.lose == old_stage:
            self.completed("主角死亡.txt")
            self.completed("比武结束.txt")
            self.state.update(map="凤池山庄.map", generation=99)
            self.state["variables"]["FCBW"] = "11" if old_stage == 11 else "12"
            return dict(status="cancelled" if self.reason == "world_changed" else "failed",
                        reason=self.reason or "player_dead", kills=0)
        self.completed(opponents[old_stage - 1] + "败.txt")
        if old_stage == 11:
            self.completed("比武结束.txt")
            self.state.update(map="凤池山庄.map", generation=99)
            self.state["variables"]["HappyEnding"] = "1"
        else:
            self.stage += 1
            self.state["generation"] += 1
            self.state["variables"]["FCBW"] = str(self.stage)
            self.target()
        return dict(status="cancelled" if self.reason == "world_changed" else "succeeded",
                    reason=self.reason or "enemies_defeated", kills=0 if self.reason else 1)


namespace = dict(time=Clock(), json=json, Path=Path, configparser=configparser,
                 npc_attackable=npc_attackable,
                 AutomationError=AutomationError, item=item, verify=verify,
                 LIFE_ITEM="fallback-life.ini", idle=lambda client: client.observe(),
                 continue_practice=lambda client: client.observe(),
                 checkpoint=lambda *args: None, late_checkpoint=lambda *args: None,
                  select_medicine=lambda state, attribute, **kwargs: next(
                     (entry for entry in state["inventory"] if entry["attribute"] == attribute
                      and entry["quantity"] > 0), None))
exec(compile(ast.Module(body=functions, type_ignores=[]), "coverage", "exec"), namespace)
read_records = namespace["late_records"]

def prepare_output(folder):
    output = Path(folder)
    (output / "run.json").write_text(json.dumps({"command": ["game", "--assets", str(root / "assets")]}), encoding="utf-8")
    return output


checks = 0
for options in ({}, {"lose": 1}, {"lose": 11}, {"lose": 3, "reason": "world_changed"},
                {"reason": "world_changed"}, {"no_mana": True}, {"tianyi_level": 6},
                {"tianyi_level": 1}, {"tianyi_level": 8}, {"tianyi_level": 8, "mana_max": 10},
                {"hidden_quick": True}):
    client = DuelClient(**options)
    namespace["late_records"] = lambda *args, **kwargs: copy.deepcopy(client.trace)
    with tempfile.TemporaryDirectory() as folder:
        try:
            namespace["compete_in_tournament"](client, prepare_output(folder))
        except AutomationError as error:
            assert "lose" in options and "must defeat Dugu Jian" in str(error)
        else:
            assert "lose" not in options
        result = json.loads((Path(folder) / "tournament-result.json").read_text(encoding="utf-8"))
        assert len(result["rounds"]) == options.get("lose", 11)
        assert result["happyEnding"] == (0 if "lose" in options else 1)
        assert all(command["allowMeleeFallback"] == (not bool(command["skills"])) for command in client.commands)
        assert all(len(row["attempts"]) == 1 for row in result["rounds"])
        if options.get("no_mana") or options.get("mana_max", 1000) < 56:
            assert all(command["skills"] == [] for command in client.commands)
            assert not client.assignments
        elif options.get("hidden_quick"):
            assert not client.assignments and all(command["skills"] == [0] for command in client.commands)
        elif "lose" not in options and options.get("tianyi_level", 7) >= 6:
            assert client.assignments and client.commands[-1]["skills"] == [0]
            selected = result["rounds"][-1]["strategy"]["selected"]
            assert selected["spell"]["file"] == "player-magic-天意剑诀.ini"
            level = options.get("tianyi_level", 7)
            assert selected["spell"]["level"] == level
            expected_damage, expected_cost = {6: (490, 92), 7: (800, 166), 8: (1370, 180)}[level]
            assert (selected["nominalDamage"], selected["manaCost"]) == (expected_damage, expected_cost)
            if level == 6:
                first = result["rounds"][0]["strategy"]["selected"]
                assert first["spell"]["file"] == "player-magic-天意剑诀.ini"
                assert first["nominalDamage"] == 785 and first["manaCost"] == 92
        elif "lose" not in options:
            assert client.commands[-1]["skills"] == []  # Dream level 1 versus defense 1000.
            assert result["rounds"][-1]["strategy"]["selected"] is None
    checks += 1
for options in ({"omit_trace": True}, {"reason": "item_depleted: life.ini"}):
    client = DuelClient(**options)
    namespace["late_records"] = lambda *args, **kwargs: copy.deepcopy(client.trace)
    with tempfile.TemporaryDirectory() as folder:
        try:
            namespace["compete_in_tournament"](client, prepare_output(folder))
        except AutomationError:
            checks += 1
        else:
            raise AssertionError("Invalid tournament evidence accepted")


class RefillDuelClient(DuelClient):
    def __init__(self, attribute="mana", invalid=None):
        super().__init__()
        self.attribute, self.invalid, self.exhausted = attribute, invalid, False
        self.state["inventory"].append(dict(file="alternate.ini", attribute=attribute, quantity=2, cooldownMs=500))
        if invalid == "no_stock":
            self.state["inventory"][-1]["quantity"] = 0
        elif invalid == "wrong_medicine":
            self.state["inventory"][-1]["attribute"] = "life" if attribute == "mana" else "mana"
    def submit(self, command, **arguments):
        super().submit(command, **arguments)
        return len(self.commands)
    def request(self, command, **arguments):
        if command == "CancelAction":
            assert self.invalid == "timeout"
            return dict(status="cancelled", reason="cancelled_by_client")
        if not self.exhausted:
            self.exhausted = True
            filename = self.attribute + ".ini"
            for entry in self.state["inventory"]:
                if entry["file"] == filename:
                    entry["quantity"] = 0
            namespace["time"].sleep(599 if self.invalid == "timeout" else 120)
            if self.invalid == "map":
                self.state["map"] = "凤池山庄.map"
            elif self.invalid == "generation":
                self.state["generation"] += 1
            elif self.invalid == "stage":
                self.state["variables"]["FCBW"] = "2"
            elif self.invalid == "target":
                self.state["targets"][0]["id"] += 99
            elif self.invalid == "dead_target":
                self.state["targets"][0]["attackable"] = False
            elif self.invalid == "dead_player":
                self.state["player"]["life"] = 0
            elif self.invalid == "input_locked":
                self.state["worldInput"] = False
            return dict(status="failed", reason="no_progress" if self.invalid == "other_error" else
                        "item_depleted: " + filename, kills=1 if self.invalid == "already_killed" else 0)
        if self.invalid == "timeout":
            return dict(status="running", kills=0)
        return super().request(command, **arguments)


for attribute in ("life", "mana"):
    client = RefillDuelClient(attribute)
    namespace["time"] = Clock()
    namespace["late_records"] = lambda *args, **kwargs: copy.deepcopy(client.trace)
    with tempfile.TemporaryDirectory() as folder:
        namespace["compete_in_tournament"](client, prepare_output(folder))
        rounds = json.loads((Path(folder) / "tournament-rounds.json").read_text(encoding="utf-8"))
    attempts = rounds[0]["attempts"]
    assert len(attempts) == 2 and attempts[0]["result"]["reason"] == "item_depleted: " + attribute + ".ini"
    assert attempts[0]["replacement"] == dict(attribute=attribute, file="alternate.ini")
    assert attempts[1]["result"]["kills"] == 1 and rounds[0]["outcome"] == "won"
    assert attempts[0]["actionId"] != attempts[1]["actionId"]
    assert client.commands[0]["timeoutMs"] == 600000 and client.commands[1]["timeoutMs"] == 480000
    changed = {key for key in client.commands[0] if client.commands[0][key] != client.commands[1][key]}
    assert changed == {attribute + "Item", "timeoutMs"}
    assert attempts[0]["supplies"][attribute + "Item"] == attribute + ".ini"
    assert attempts[1]["supplies"][attribute + "Item"] == "alternate.ini"
    checks += 1

for invalid in ("no_stock", "wrong_medicine", "map", "generation", "stage", "target", "dead_target",
                "dead_player", "input_locked", "other_error", "already_killed", "timeout"):
    client = RefillDuelClient(invalid=invalid)
    namespace["time"] = Clock()
    namespace["late_records"] = lambda *args, **kwargs: copy.deepcopy(client.trace)
    with tempfile.TemporaryDirectory() as folder:
        try:
            namespace["compete_in_tournament"](client, prepare_output(folder))
        except (AutomationError, TimeoutError) as error:
            assert isinstance(error, TimeoutError) == (invalid == "timeout")
        else:
            raise AssertionError(f"Unsafe tournament refill accepted: {invalid}")
        rounds = json.loads((Path(folder) / "tournament-rounds.json").read_text(encoding="utf-8"))
    assert len(rounds) == 1
    assert len(rounds[0]["attempts"]) == (2 if invalid == "timeout" else 1)
    assert len(client.commands) == len(rounds[0]["attempts"])
    if invalid == "timeout":
        assert client.commands[1]["timeoutMs"] == 1000
        assert rounds[0]["attempts"][-1]["timedOut"]
        assert namespace["time"].monotonic() < 601
    checks += 1


class SideClient:
    def __init__(self, map_name, variables):
        self.calls = []
        self.state = dict(map=map_name, variables=variables, magic=[], inventory=[], targets=[],
                          player={"money": 100}, generation=1)
    def observe(self, variables=()):
        return copy.deepcopy(self.state)


def go(client, x, y, **arguments):
    client.calls.append(("go", (x, y), arguments))
    if "destination" in arguments:
        client.state["map"] = arguments["destination"]
        client.state["generation"] += 1
    script = arguments.get("script", "")
    if script == "中都/地图陷阱11.txt":
        assert arguments["combat"] is False
        client.state["variables"]["ZDBiWu"] = "1"
    if script == "中都/trap-14.txt":
        assert arguments["combat"] is False and client.state["variables"]["ZDBiWu"] == "1"
        client.state["variables"]["ZDBiWu"] = "2"


def fight(client, target):
    client.calls.append(("fight", target))
    variables = client.state["variables"]
    if target == "段环山":
        variables["DuanHuanShan"] = "1"
    elif target == "玄慈":
        assert variables["ZDBiWu"] == "2"
        variables["ZDBiWu"] = "3"
        client.state["magic"].append(dict(file="player-magic-金刚不坏神功.ini", level=1))
    elif target == "欧阳桐":
        assert variables["ZDBiWu"] == "3"
        variables.update(ZDBiWu="4", OuYangMusic="1")


def talk(client, name, position=None):
    client.calls.append(("talk", name, position))
    variables = client.state["variables"]
    if name == "史忠良":
        assert client.state["map"] == "汉阳.map" and variables["DuanHuanShan"] == "1"
        variables["DuanHuanShan"] = "0"
        client.state["magic"].append(dict(file="player-magic-大梦心法.ini", level=1))
    elif name == "女子1":
        assert variables["ZDBiWu"] == "4" and position == (80, 135)
        client.state["player"]["money"] += 9000
    elif name == "女子2":
        assert variables["ZDBiWu"] == "4" and position == (78, 137)
        client.state["inventory"].append(dict(file="reward.ini", quantity=1))
    elif name == "天王帮弟子":
        assert variables["FromFengChi"] == "20" and position == (41, 37)
        client.state["map"] = "天王岛.map"
        variables["TianWangPiEr"] = "5"
    elif name == "杨瑛":
        assert position == (45, 76)
        client.state["magic"].append(dict(file="player-magic-洗髓经.ini", level=1))
    elif name == "守门弟子":
        client.state["map"] = "汉阳.map"


namespace.update(go=go, talk=talk, fight=fight, restock=lambda client: None, meditate=lambda client: None,
                 late_target=lambda client, name, *args: name)
for function, map_name, variables, expected in [
    ("duan_family_side_story", "汉阳.map", {"HanYanCTMSZL": "3"}, "大梦心法"),
    ("zhongdu_shaolin_side_story", "中都.map", {}, "金刚不坏神功"),
    ("tianwang_family_revisit", "汉阳.map", {"FromFengChi": "20", "HanYanTWBDiZi": "1", "TianWangPiEr": "4"}, "洗髓经"),
]:
    client = SideClient(map_name, variables)
    namespace[function](client, Path("unused"))
    assert client.state["map"] == map_name
    item(client.state, f"player-magic-{expected}.ini", "magic")
    if function == "zhongdu_shaolin_side_story":
        boss_call = client.calls.index(("fight", "欧阳桐"))
        protected = [call for call in client.calls[:boss_call] if call[0] == "go"
                     and call[1] in ((71, 140), (74, 141), (77, 158), (87, 168))]
        assert [call[1] for call in protected] == [(71, 140), (74, 141), (77, 158), (87, 168)]
        assert all(call[2]["combat"] is False for call in protected)
    checks += 1

for obstruction in ("present", "absent", "duplicate", "vanished", "generation"):
    client = SideClient("中都.map", {"ZDBiWu": "3"})
    client.state["magic"].append(dict(file="player-magic-金刚不坏神功.ini", level=1))
    doorway_fights = []
    def doorway_go(client, x, y, **arguments):
        go(client, x, y, **arguments)
        if client.state["variables"]["ZDBiWu"] != "3":
            return
        if (x, y) == (71, 140):
            client.state["targets"] = [dict(kind="npc", name="打手", id=919, life=1520,
                                           hostile=True, attackable=True, position=dict(x=74, y=143))]
            if obstruction != "absent":
                client.state["targets"].append(dict(kind="npc", name="打手", id=918, life=1520,
                                                    hostile=True, attackable=True, position=dict(x=75, y=142)))
            if obstruction == "duplicate":
                duplicate = copy.deepcopy(client.state["targets"][-1])
                duplicate["id"] = 920
                client.state["targets"].append(duplicate)
        elif (x, y) == (74, 141):
            if obstruction == "generation":
                client.state["generation"] += 1
            for target in client.state["targets"]:
                if target["id"] == 918:
                    target["position"] = dict(x=75, y=144)
            if obstruction == "vanished":
                client.state["targets"] = [target for target in client.state["targets"] if target["id"] != 918]
    def doorway_fight(client, target):
        if isinstance(target, dict):
            assert target["id"] == 918 and client.calls[-1][1] == (74, 141)
            doorway_fights.append(target["id"])
            client.state["targets"] = [entry for entry in client.state["targets"] if entry["id"] != target["id"]]
        else:
            fight(client, target)
    namespace.update(go=doorway_go, fight=doorway_fight)
    try:
        namespace["zhongdu_shaolin_side_story"](client, Path("unused"))
    except AutomationError:
        assert obstruction in ("duplicate", "vanished", "generation") and not doorway_fights
        assert not any(call == ("fight", "欧阳桐") for call in client.calls)
    else:
        assert obstruction in ("present", "absent")
        assert doorway_fights == ([918] if obstruction == "present" else [])
        assert client.state["variables"]["ZDBiWu"] == "4"
    checks += 1
namespace.update(go=go, fight=fight)
client = SideClient("汉阳.map", {"HanYanCTMSZL": "3", "DuanJiaZhuangClose": "1"})
try:
    namespace["duan_family_side_story"](client, Path("unused"))
except AutomationError:
    assert not client.calls
    checks += 1
else:
    raise AssertionError("Closed Duan family route entered")

# The ending branch validator must accept each matching movie/line pair only.
finale = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "tianren_finale")
begin = next(i for i, node in enumerate(finale.body) if isinstance(node, ast.Assign) and "movie_line" in ast.unparse(node.targets))
end = begin + 3
validator = compile(ast.Module(body=finale.body[begin:end], type_ignores=[]), "ending-branch", "exec")
for branch, lines, videos, succeeds in [(0, {55}, {"end.avi"}, True), (1, {137}, {"happyend.avi"}, True),
                                       (1, {55}, {"end.avi"}, False), (0, {55, 137}, {"end.avi"}, False),
                                       (1, {137}, {"happyend.avi", "end.avi"}, False)]:
    try:
        exec(validator, dict(branch=branch, lines=lines, videos=videos,
                             ending_movie_lines=(55, 137), AutomationError=AutomationError))
    except AutomationError:
        assert not succeeds
    else:
        assert succeeds
    checks += 1


# The arrival boat and Yifeng chest use ordinary jumps and an exact chest target.
hanyang = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "hanyang_tianwang")
arrival = next(node.orelse for node in hanyang.body if isinstance(node, ast.If)
               and any(isinstance(child, ast.Expr) and isinstance(child.value, ast.Call)
                       and ast.unparse(child.value.func) == "restock" for child in node.orelse))
stop = next(i for i, node in enumerate(arrival) if isinstance(node, ast.Expr)
            and isinstance(node.value, ast.Call) and ast.unparse(node.value.func) == "restock")
hanyang_code = compile(ast.Module(body=[hanyang.body[0], *arrival[:stop]], type_ignores=[]), "hanyang-skill", "exec")
for learned, can_jump, expected_jumps in [(False, True, 3), (True, True, 1), (False, False, None)]:
    client = SideClient("汉阳.map", {})
    client.state["player"].update(position=dict(x=42, y=29), canJump=can_jump)
    if learned:
        client.state["magic"].append(dict(file="player-magic-依风剑法.ini", level=1))
    jumps = []
    def jump(client, start, destination):
        assert client.state["player"]["canJump"]
        jumps.append((start, destination))
        client.state["player"]["position"] = dict(x=destination[0], y=destination[1])
    def boat_jump(command, **arguments):
        assert command == "JumpTo" and (arguments["x"], arguments["y"]) == (42, 35)
        assert client.state["player"]["position"] == dict(x=42, y=29)
        jump(client, (42, 29), (42, 35))
    client.act = boat_jump
    def learn_yifeng(client, name, position):
        assert (name, position) == ("宝箱", (37, 9))
        client.state["magic"].append(dict(file="player-magic-依风剑法.ini", level=1))
    try:
        exec(hanyang_code, dict(client=client, output=Path("unused"), idle=lambda c: c.observe(),
                               late_jump=jump, go=lambda *args: None, talk=learn_yifeng,
                               item=item, checkpoint=lambda *args: None,
                               magic_file_prefix="player-magic-", yifeng_chest_required=True))
    except AssertionError:
        assert expected_jumps is None and not jumps
    else:
        assert len(jumps) == expected_jumps
        item(client.state, "player-magic-依风剑法.ini", "magic")
    checks += 1


mine_function = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "zhongdu_mine_side_story")
class MineClient(SideClient):
    def __init__(self, missing=False, vanished=False):
        super().__init__("中都.map", {"ZhongDuLYS": "1"})
        self.missing, self.vanished = missing, vanished
        self.officer_indices = {1: 1, 2: 2, 3: 3}
        self.state["player"]["money"] = 1234
        self.state["layout"] = dict(practiceSlot=41)
        self.state["magic"] = [dict(file="player-magic-风雪狂刀.ini", level=9, slot=41)]
        self.practice_fault = None
        self.restock_moves = False
    def assign_practice(self, slot):
        self.calls.append(("assign_practice", slot))
        if self.practice_fault == "no_swap":
            return
        for entry in self.state["magic"]:
            if entry["slot"] == slot:
                entry["slot"] = 41
            elif entry["slot"] == 41:
                entry["slot"] = slot
        if self.practice_fault == "duplicate":
            self.state["magic"].append(copy.deepcopy(self.state["magic"][-1]))
        elif self.practice_fault == "lost_previous":
            self.state["magic"] = [entry for entry in self.state["magic"] if entry["file"] != "player-magic-风雪狂刀.ini"]
    def enter_mine(self):
        self.state["targets"] = [dict(id=i, name="金国将领", kind="npc", life=100,
                                      position=dict(x=p[0], y=p[1]))
                                  for i, p in enumerate(((90, 131), (7, 108), (52, 81)), 1)]
        if self.missing:
            self.state["targets"].pop(0)


def mine_go(client, x, y, **arguments):
    if (x, y) == (68, 118):
        assert client.state["player"]["position"] in (dict(x=47, y=93), dict(x=67, y=187))
    client.state["player"]["position"] = dict(x=x, y=y)
    client.calls.append(("go", (x, y), arguments))
    if client.state["map"] == "矿山.map" and client.state["variables"].get("KuangGongZouLe") == "1":
        assert arguments.get("combat") is False, "Returning with the ore must not start incidental fights"
    script = arguments.get("script", "")
    if "destination" in arguments:
        client.state["map"] = arguments["destination"]
        client.state["generation"] += 1
    if client.state["map"] == "中都-矿山.map":
        client.state["variables"]["KuangShan"] = "2"
    if arguments.get("destination") == "矿山.map":
        client.enter_mine()
    if script == "矿山/地图陷阱2.txt":
        for target in client.state["targets"]:
            target["position"] = dict(x=50, y=50)  # Initial coordinates no longer match.
        if client.vanished:
            client.state["targets"].pop(0)
    if isinstance(client, ResumedMineClient) and (x, y) == (7, 110):
        for target in client.state["targets"]:
            target["position"] = dict(x=50, y=50)  # Saved positions can change after resuming.
        if client.change_generation:
            client.state["generation"] += 1
    if arguments.get("destination") == "中都.map":
        assert client.state["variables"]["KuangGongZouLe"] == "1"
        client.state["variables"]["4JiangLing"] = "4"
    if script == "中都/地图陷阱17.txt":
        client.state["player"]["position"] = dict(x=55, y=44)
    return client.observe()


def mine_talk(client, name, position):
    client.calls.append(("talk", name, position))
    if name == "寺庙老妪":
        assert position == (54, 42)
        client.state["variables"]["1KuangShan"] = "1"
    elif name == "宝箱":
        assert position == (41, 20) and client.state["map"] == "中都-矿山.map"
        client.state["magic"].append(dict(file="player-magic-弯刀冷光.ini", level=1, slot=6))
    elif name == "矿工1":
        assert position == (64, 23) and all(client.state["variables"][f"{i}JiangLing"] == str(i) for i in (1, 2, 3))
        client.state["variables"].update(Jian="1", KuangGongZouLe="1")
        client.state["inventory"].append(dict(file="goods-奇矿石.ini", quantity=1))
    elif name == "掌柜1":
        assert position == (167, 119) and client.state["variables"]["Jian"] == "1"
        approaches = client.calls[-6:-1]
        assert [call[1] for call in approaches] == [(68, 118), (106, 109), (151, 80), (171, 125), (167, 118)]
        assert all(call[0] == "go" and call[2].get("combat") is False for call in approaches)
        client.state["variables"]["Jian"] = "0"
        client.state["player"]["money"] = 0
        client.state["inventory"] = [dict(file="goods-jian-14-剑中之剑.ini", quantity=1)]
    else:
        raise AssertionError((name, position))


def mine_fight(client, target):
    client.calls.append(("fight", target["id"]))
    index = client.officer_indices[target["id"]]
    client.state["variables"][f"{index}JiangLing"] = str(index)
    client.state["targets"] = [entry for entry in client.state["targets"] if entry["id"] != target["id"]]


def mine_meditate(client):
    assert client.state["map"] == "中都.map"
    assert [call[1] for call in client.calls[-2:]] == [(52, 53), (47, 93)]
    client.calls.append(("meditate",))


def mine_restock(client, **limits):
    assert client.calls[-1] == ("meditate",)
    assert limits == dict(mana_count=6, reserve_money=0)
    client.calls.append(("restock", limits))
    client.state["player"]["money"] -= 260
    if client.restock_moves:
        client.state["player"]["position"] = dict(x=67, y=187)
    return client.observe()


mine_namespace = dict(go=mine_go, talk=mine_talk, fight=mine_fight, idle=lambda c: c.observe(),
                      meditate=mine_meditate, restock=mine_restock,
                      npc_attackable=npc_attackable,
                      verify=verify, item=item, checkpoint=lambda *args: None,
                      json=json, AutomationError=AutomationError)
exec(compile(ast.Module(body=[mine_function], type_ignores=[]), "mine", "exec"), mine_namespace)
class ResumedMineClient(MineClient):
    def __init__(self, position=(9, 127)):
        super().__init__()
        self.state.update(map="矿山.map", generation=104)
        self.state["player"]["position"] = dict(x=position[0], y=position[1])
        self.state["variables"].update(KuangShan="2", **{"1JiangLing": "1", "2JiangLing": "", "3JiangLing": "0"})
        self.state["magic"].append(dict(file="player-magic-弯刀冷光.ini", level=1, slot=6))
        self.state["targets"] = [dict(id=800 + index, name="金国将领", kind="npc", life=1239,
                                      hostile=True, attackable=True, action=0,
                                      position=dict(x=point[0], y=point[1]))
                                  for index, point in ((2, (7, 105)), (3, (52, 85)))]
        self.officer_indices = {802: 2, 803: 3}
        self.change_generation = False


for options in ({}, {"missing": True}, {"vanished": True}):
    client = MineClient(**options)
    with tempfile.TemporaryDirectory() as folder:
        try:
            mine_namespace["zhongdu_mine_side_story"](client, Path(folder))
        except AutomationError:
            assert options and client.state["player"]["money"] == 1234
        else:
            assert not options and client.state["map"] == "中都.map"
            proof = json.loads((Path(folder) / "side-mine-crafting.json").read_text(encoding="utf-8"))
            assert proof["moneyBefore"] == 974 and proof["moneyAfter"] == 0
            item(client.state, "goods-jian-14-剑中之剑.ini")
    checks += 1

for position in ((9, 127), (9, 125)):
    client = ResumedMineClient(position)
    # Normal AI may move an officer slightly between the save and Observe.
    client.state["targets"][0]["position"]["y"] += 1
    with tempfile.TemporaryDirectory() as folder:
        mine_namespace["zhongdu_mine_side_story"](client, Path(folder))
        proof = json.loads((Path(folder) / "side-mine-crafting.json").read_text(encoding="utf-8"))
        assert proof["moneyBefore"] == 974 and proof["moneyAfter"] == 0
    assert [call[1] for call in client.calls if call[0] == "fight"] == [802, 803]
    assert [call[1] for call in client.calls[:2]] == [(9, 125), (7, 110)]
    assert not any(call[0] == "go" and call[1] == (80, 119) for call in client.calls)
    assert not any(call[0] == "talk" and call[1] in ("寺庙老妪", "宝箱") for call in client.calls)
    checks += 1

for invalid in ("position", "temple", "mine_stage", "first_alive", "second_done", "third_done",
                "no_skill", "missing", "duplicate", "distant", "not_attackable", "generation"):
    client = ResumedMineClient()
    if invalid == "position":
        client.state["player"]["position"] = dict(x=9, y=126)
    elif invalid in ("temple", "mine_stage", "first_alive", "second_done", "third_done"):
        key, value = {"temple": ("ZhongDuLYS", "0"), "mine_stage": ("KuangShan", "1"),
                      "first_alive": ("1JiangLing", "0"), "second_done": ("2JiangLing", "2"),
                      "third_done": ("3JiangLing", "3")}[invalid]
        client.state["variables"][key] = value
    elif invalid == "no_skill":
        client.state["magic"] = []
    elif invalid == "missing":
        client.state["targets"].pop()
    elif invalid == "duplicate":
        duplicate = copy.deepcopy(client.state["targets"][0])
        duplicate["id"] = 999
        client.state["targets"].append(duplicate)
    elif invalid == "distant":
        client.state["targets"][0]["position"]["x"] += 3
    elif invalid == "not_attackable":
        client.state["targets"][0]["attackable"] = False
    else:
        client.change_generation = True
    with tempfile.TemporaryDirectory() as folder:
        try:
            mine_namespace["zhongdu_mine_side_story"](client, Path(folder))
        except (AssertionError, AutomationError):
            assert not any(call[0] == "fight" for call in client.calls)
            assert client.state["player"]["money"] == 1234
            if invalid != "generation":
                assert not client.calls
        else:
            raise AssertionError(f"Invalid mine resume accepted: {invalid}")
    checks += 1

# Fully defeated officers need no NPC IDs; the reward and return phases stay distinct.
for returning, position, wind_level, already_swapped in (
        (False, (52, 84), 9, False), (False, (56, 51), 10, False),
        (True, (14, 76), 10, False), (True, (14, 75), 10, True)):
    client = ResumedMineClient(position)
    client.state["variables"].update({"2JiangLing": "2", "3JiangLing": "3",
                                      "Jian": str(int(returning)), "KuangGongZouLe": str(int(returning))})
    client.restock_moves = returning
    client.state["targets"] = []
    item(client.state, "player-magic-风雪狂刀.ini", "magic")["level"] = wind_level
    if returning:
        client.state["inventory"] = [dict(file="goods-奇矿石.ini", quantity=1)]
    if already_swapped:
        client.assign_practice(6)
        client.calls.clear()
    with tempfile.TemporaryDirectory() as folder:
        mine_namespace["zhongdu_mine_side_story"](client, Path(folder))
    assert not any(call[0] == "fight" for call in client.calls)
    walked = [call[1] for call in client.calls if call[0] == "go"]
    assert walked[:3] == ([(14, 75), (7, 100), (9, 129)] if returning
                           else [(56, 51), (63, 28), (64, 24)])
    assert sum(call[0] == "talk" and call[1] == "矿工1" for call in client.calls) == int(not returning)
    expected_swap = wind_level == 10 and not already_swapped
    assert sum(call[0] == "assign_practice" for call in client.calls) == int(expected_swap)
    if wind_level == 10:
        assert item(client.state, "player-magic-弯刀冷光.ini", "magic")["slot"] == 41
        assert item(client.state, "player-magic-风雪狂刀.ini", "magic")["slot"] == 6
    checks += 1

for invalid in ("no_ore", "zero_ore", "wrong_position", "workers_not_rescued", "partial_officers",
                "no_swap", "lost_previous", "duplicate"):
    client = ResumedMineClient((14, 76))
    client.state["variables"].update({"2JiangLing": "2", "3JiangLing": "3", "Jian": "1", "KuangGongZouLe": "1"})
    client.state["targets"] = []
    client.state["inventory"] = [dict(file="goods-奇矿石.ini", quantity=1)]
    item(client.state, "player-magic-风雪狂刀.ini", "magic")["level"] = 10
    if invalid == "no_ore":
        client.state["inventory"] = []
    elif invalid == "zero_ore":
        client.state["inventory"][0]["quantity"] = 0
    elif invalid == "wrong_position":
        client.state["player"]["position"] = dict(x=14, y=77)
    elif invalid == "workers_not_rescued":
        client.state["variables"]["KuangGongZouLe"] = "0"
    elif invalid == "partial_officers":
        client.state["variables"]["3JiangLing"] = "0"
    else:
        client.practice_fault = invalid
    with tempfile.TemporaryDirectory() as folder:
        try:
            mine_namespace["zhongdu_mine_side_story"](client, Path(folder))
        except (AssertionError, AutomationError):
            assert all(call[0] == "assign_practice" for call in client.calls)
        else:
            raise AssertionError(f"Invalid mine return/swap accepted: {invalid}")
    checks += 1

# Source order is part of the money contract: craft before either later cash reward.
zhongdu = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "zhongdu_first_visit")
zhongdu_town = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "zhongdu_town_story")
steps = ast.unparse(zhongdu) + ast.unparse(zhongdu_town)
assert steps.index("ZhongDuLYS=1") < steps.index("zhongdu_mine_side_story") < steps.index("83, 61") < steps.index("zhongdu_shaolin_side_story")
assert steps.index("meditate(client)") < steps.index("restock(client, mana_count=6)") < steps.index("龙音寺门口地图陷阱5")
assert steps.index("restock(client, mana_count=6)", steps.index("83, 61")) < steps.index("zhongdu_shaolin_side_story")
checks += 1

mine_call = next(i for i, node in enumerate(zhongdu.body)
                 if isinstance(node, ast.Expr) and isinstance(node.value, ast.Call)
                 and isinstance(node.value.func, ast.Name) and node.value.func.id == "zhongdu_mine_side_story")
entry_code = compile(ast.Module(body=zhongdu.body[:mine_call + 1], type_ignores=[]), "zhongdu-entry", "exec")
for map_name in ("中都.map", "矿山.map", "中都-矿山.map"):
    client = SideClient(map_name, {"ZhongDuLYS": "1"})
    entered = []
    try:
        exec(entry_code, dict(client=client, output=Path("unused"), idle=lambda c: c.observe(),
                              meditate=lambda c: c.calls.append(("meditate",)),
                              restock=lambda c, **kwargs: c.calls.append(("restock",)),
                              go=go, verify=verify,
                              magic_file_prefix="player-magic-", craft_sword=True,
                              ore_miner_position=(64, 23), jump_to_ore_miner=False,
                              second_boss_name=None, captive_money_range=(9000, 20000),
                              zhongdu_mine_side_story=lambda c, out, **kwargs: entered.append(c.state["map"])))
    except AssertionError:
        assert map_name == "中都-矿山.map" and not client.calls and not entered
    else:
        assert entered == [map_name]
        assert len(client.calls) == (4 if map_name == "中都.map" else 0)
    checks += 1


# Cross-city conversations must begin beside the NPC after normal movement.
# The mine-return starting point reproduces the long path that exhausted A*.
city_client = SideClient("中都.map", {})
city_client.state["player"]["position"] = dict(x=167, y=118)


def city_go(client, x, y, **arguments):
    client.calls.append(("go", (x, y), arguments))
    client.state["player"]["position"] = dict(x=x, y=y)
    if "destination" in arguments:
        client.state["map"] = arguments["destination"]


def city_talk(client, name, position=None):
    client.calls.append(("talk", name, position))
    variables = client.state["variables"]
    point = client.state["player"]["position"]
    if name == "宝箱":
        assert position == (83, 61) and point == dict(x=83, y=62)
        client.state["player"]["money"] += 1000
    elif name == "老板":
        assert position == (110, 217) and point == dict(x=111, y=217)
        assert variables["ZhongDuTroublesRoom"] == "1"
        variables["ZhongDuTroublesRoom"] = "2"
    elif name == "柴嵩":
        assert point == dict(x=79, y=30)
        previous = variables.get("ZhongDuTroublesRoom", "0")
        assert previous in ("0", "2", "3")
        variables["ZhongDuTroublesRoom"] = {"0": "1", "2": "3", "3": "4"}[previous]
        if previous == "3":
            assert variables["ZhongDuHouHuaYuan"] == "4"
            client.state["player"]["position"] = dict(x=142, y=326)
    elif name == "燕府家丁":
        assert point == dict(x=138, y=263) and position == (139, 263)
        assert variables["ZhongDuTroublesRoom"] == "3"
        stage = int(variables.get("ZhongDuZhuangYuanDoor", "0")) + 1
        variables["ZhongDuZhuangYuanDoor"] = str(stage)
        if stage == 2:
            client.state["map"] = "中都夜.map"
            client.state["player"]["position"] = dict(x=79, y=29)
    elif name == "燕若雪":
        assert client.state["map"] == "中都夜.map" and point["x"] >= 138
        stage = int(variables.get("ZhongDuHouHuaYuan", "0")) + 1
        variables["ZhongDuHouHuaYuan"] = str(stage)
        assert stage <= 4 or variables["ZhongDuTroublesRoom"] == "4"
    else:
        raise AssertionError(name)


def city_jump(client, start, destination):
    city_go(client, *start)
    client.calls.append(("jump", start, destination))
    client.state["player"]["position"] = dict(x=destination[0], y=destination[1])


city_namespace = dict(client=city_client, output=Path("unused"),
                      magic_file_prefix="player-magic-", craft_sword=True, treasury_chest_opened=False,
                      second_boss_name=None, captive_money_range=(9000, 20000),
                      leave_via_main_gate=False,
                      idle=lambda client: client.observe(), verify=verify,
                      go=city_go, talk=city_talk, late_jump=city_jump,
                      restock=lambda *args, **kwargs: None,
                      late_checkpoint=lambda *args: None,
                      checkpoint=lambda *args: None,
                      daoxiang_to_linan=lambda client, output, **kwargs:
                          city_go(client, 123, 87, destination="临安城.map"),
                      zhongdu_shaolin_side_story=lambda client, output, **kwargs:
                          client.state["player"].update(position=dict(x=149, y=88)))
garden_functions = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                    and node.name in ("late_enter_night_garden", "late_leave_night_garden", "zhongdu_return_to_linan")]
exec(compile(ast.Module(body=garden_functions + zhongdu_town.body, type_ignores=[]),
             "zhongdu-city-approaches", "exec"), city_namespace)
assert city_client.state["variables"]["ZhongDuHouHuaYuan"] == "5"
assert city_client.state["map"] == "临安城.map"
city_walks = [call[1] for call in city_client.calls if call[0] == "go"]
assert city_walks[:6] == [(171, 125), (151, 80), (106, 109), (68, 118), (47, 93), (83, 62)]
assert city_walks.count((84, 103)) == 6 and city_walks.count((97, 223)) == 5
assert (70, 334) in city_walks
checks += 1


# Prison equipment must preserve the real equipped reward weapon, not a bag item.
prison = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "fengchi_and_prison")
prison_entry = next(index for index, node in enumerate(prison.body)
                    if isinstance(node, ast.Expr) and isinstance(node.value, ast.Call)
                    and any(keyword.arg == "destination" and isinstance(keyword.value, ast.Constant)
                            and keyword.value.value == "临安大牢-3.map" for keyword in node.value.keywords))
prison_walks = []
def prison_go(client, x, y, **arguments):
    if arguments.get("destination") == "临安大牢-3.map":
        client.state["map"] = "临安大牢-3.map"
    else:
        assert client.state["map"] == "临安大牢-3.map" and arguments.get("combat") is False
        if arguments.get("script"):
            assert prison_walks[-1] == (40, 26) and arguments["script"] == "临安大牢-3/trap3.txt"
    prison_walks.append((x, y))
client = SideClient("临安大牢.map", {})
exec(compile(ast.Module(body=prison.body[prison_entry:prison_entry + 3], type_ignores=[]),
             "prison-northern-passage", "exec"), dict(client=client, go=prison_go))
assert prison_walks == [(79, 78), (40, 26), (64, 158)]
prison_map = (root / "assets/jxqy2/map/临安大牢-3.map").read_bytes()
_, prison_width, _, prison_image_size, _ = struct.unpack_from("<5i", prison_map, 64)
prison_header, _, _, prison_image_count = struct.unpack_from("<4i", prison_map, 84)
prison_tiles = prison_header + prison_image_count * prison_image_size
for point, trap in (((40, 26), 0), ((64, 158), 3), ((12, 166), 1)):
    tile = prison_tiles + (point[1] * prison_width + point[0]) * 10
    assert not prison_map[tile + 6] & 0xc0 and prison_map[tile + 7] == trap
assert 'loadmap("临安大牢.map")' in (root / "assets/jxqy2/script/map/临安大牢-3/trap2-to4.txt").read_text(encoding="utf-8")
checks += 1

# A ranged prison fight can leave the hero south of the returning-floor exit.
prison_exit_helper = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                          and node.name == "prison_exit")
prison_exit = next(index for index, node in enumerate(prison_exit_helper.body)
                   if isinstance(node, ast.Expr) and isinstance(node.value, ast.Call)
                   and any(keyword.arg == "destination" and isinstance(keyword.value, ast.Constant)
                           and keyword.value.value == "大牢出口.map" for keyword in node.value.keywords))
prison_exit_walks = []
exec(compile(ast.Module(body=prison_exit_helper.body[prison_exit - 1:prison_exit + 1], type_ignores=[]),
             "prison-first-floor-exit", "exec"),
     dict(client=SideClient("临安大牢第1层.map", {}),
          go=lambda client, x, y, **arguments: prison_exit_walks.append((x, y, arguments))))
assert prison_exit_walks == [(33, 71, {}), (52, 115, {"destination": "大牢出口.map"})]
prison_first_floor = (root / "assets/jxqy2/map/临安大牢第1层.map").read_bytes()
_, prison_width, _, prison_image_size, _ = struct.unpack_from("<5i", prison_first_floor, 64)
prison_header, _, _, prison_image_count = struct.unpack_from("<4i", prison_first_floor, 84)
prison_tiles = prison_header + prison_image_count * prison_image_size
for point, trap in (((33, 71), 0), ((52, 115), 2), ((8, 169), 1)):
    tile = prison_tiles + (point[1] * prison_width + point[0]) * 10
    assert not prison_first_floor[tile + 6] & 0xc0 and prison_first_floor[tile + 7] == trap
checks += 1

# Lin'an's outer checkpoint asks for the mask before the manor's inner trap.
revenge = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "linan_revenge")
guard_fights = next(index for index, node in enumerate(revenge.body) if isinstance(node, ast.For))
class RevengeEntranceClient(SideClient):
    def __init__(self):
        super().__init__("临安城.map", {"FromFengChi": "14"})
        self.state.update(generation=1, inventory=[], player=dict(position=dict(x=145, y=95)))
    def interact(self, identity):
        assert identity == 100 and self.state["variables"]["FromFengChi"] == "16"
        self.state["shop"] = [dict(file="goods-sj-3-面具.ini", slot=3)]
    def wait_until(self, predicate, **arguments):
        assert predicate(self.state)
        return self.observe()
    def buy(self, slot):
        assert slot == 3 and "shop" in self.state
        self.state["inventory"] = [dict(file="goods-sj-3-面具.ini", slot=7)]
    def ui(self, action):
        assert action == "Cancel"
        del self.state["shop"]
    def act(self, action, **arguments):
        assert action == "UseItem" and arguments["slot"] == 7 and "shop" not in self.state
        assert item(self.state, "goods-sj-3-面具.ini")["slot"] == 7
        self.state["inventory"] = []
        self.state["variables"]["LinAnMianJu"] = "1"
def revenge_entrance_go(client, x, y, **arguments):
    client.calls.append(((x, y), arguments["script"]))
    if (x, y) == (141, 139):
        assert arguments["script"] == "临安城/trap9.txt"
        client.state["variables"]["FromFengChi"] = "15"
    else:
        assert (x, y) == (86, 165) and arguments["script"] == "临安城/trap12.txt"
        assert arguments["combat"] is False
        masked = client.state["variables"].get("LinAnMianJu") == "1"
        assert client.state["variables"]["FromFengChi"] == ("16" if masked else "15")
        client.state["variables"]["FromFengChi"] = "17" if masked else "16"
client = RevengeEntranceClient()
exec(compile(ast.Module(body=revenge.body[:guard_fights], type_ignores=[]), "revenge-mask-checkpoint", "exec"),
     dict(client=client, output=None, idle=lambda client: client.observe(), meditate=lambda client: None,
          restock=lambda client, **arguments: None,
          go=revenge_entrance_go, verify=verify, item=item,
          late_target=lambda client, name, position: dict(id=100) if
              name == "杂货摊贩" and position == (133, 244) else None))
assert client.state["variables"]["FromFengChi"] == "17"
assert client.calls == [((141, 139), "临安城/trap9.txt"), ((86, 165), "临安城/trap12.txt"),
                        ((86, 165), "临安城/trap12.txt")]
inner_gate = revenge.body[guard_fights + 1].value
assert [ast.literal_eval(argument) for argument in inner_gate.args[1:]] == [67, 134]
assert next(keyword.value.value for keyword in inner_gate.keywords if keyword.arg == "script") == "临安城/trap8.txt"
linan_map = (root / "assets/jxqy2/map/临安城.map").read_bytes()
_, linan_width, _, linan_image_size, _ = struct.unpack_from("<5i", linan_map, 64)
linan_header, _, _, linan_image_count = struct.unpack_from("<4i", linan_map, 84)
for point, trap in (((86, 165), 12), ((67, 134), 8)):
    tile = linan_header + linan_image_count * linan_image_size + (point[1] * linan_width + point[0]) * 10
    assert not linan_map[tile + 6] & 0xc0 and linan_map[tile + 7] == trap
checks += 1

weapon_index = next(index for index, node in enumerate(prison.body)
                    if isinstance(node, ast.Assign) and "weapon" in ast.unparse(node.targets))
weapon_code = compile(ast.Module(body=prison.body[weapon_index - 1:weapon_index + 2], type_ignores=[]), "prison-weapon", "exec")
class WeaponClient:
    def __init__(self, weapon):
        self.equipped = None
        self.state = dict(layout=dict(equipmentBegin=100), inventory=[
            dict(file="goods-jian-5-龙泉剑.ini", slot=3),
            dict(file="goods-jian-2-桃花剑.ini", slot=4)])
        if weapon:
            self.state["inventory"].append(dict(file=weapon, slot=104))
    def observe(self):
        return copy.deepcopy(self.state)
    def equip(self, slot):
        self.equipped = slot
for weapon, expected in [(None, 3), ("goods-jian-1-桃木剑.ini", 3), ("goods-dao-1-九环刀.ini", 3),
                         ("goods-jian-2-桃花剑.ini", 3), ("goods-jian-6-雌雄剑.ini", None),
                         ("unfamiliar-reward.ini", None)]:
    client = WeaponClient(weapon)
    exec(weapon_code, dict(client=client, item=item))
    assert client.equipped == expected
    checks += 1


# The first Lin'an chest is behind Jie'er; the first Fengchi exit is behind its gatekeeper.
linan = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "linan_and_fengchi")
prefix = []
for statement in linan.body:
    prefix.append(statement)
    if (isinstance(statement, ast.Expr) and isinstance(statement.value, ast.Call)
            and isinstance(statement.value.func, ast.Name) and statement.value.func.id == "verify"
            and any(keyword.arg == "FromFengChi" for keyword in statement.value.keywords)):
        break
client = SideClient("临安城.map", {})
gate = dict(courtyard=False, manor=False)
def first_visit_talk(client, name, position=None):
    client.calls.append(("talk", name))
    if name == "婕儿":
        gate["courtyard"] = True
    elif name == "宝箱":
        assert gate["courtyard"] and client.state["variables"].get("LinAnYanRuoXue") == "1"
        client.state["player"]["money"] += 1000
    elif name == "柴嵩":
        client.state["variables"]["LinAnYanRuoXue"] = "2"
    elif name == "燕若雪":
        client.state["map"] = "凤池山庄.map"
    elif name == "守门家丁":
        assert client.state["map"] == "凤池山庄.map"
        gate["manor"] = True
        client.state["variables"]["FenCiDaMenJiaDin"] = "1"
    else:
        raise AssertionError(name)
def first_visit_go(client, x, y, **arguments):
    assert gate["courtyard"] and (x, y) == (142, 108)
    assert "LinAnYanRuoXue" not in client.state["variables"]
    assert arguments["script"] == "临安城/trap4.txt"
    client.state["variables"]["LinAnYanRuoXue"] = "1"
def first_visit_return(client):
    assert gate["manor"], "The first exit remains blocked until the gatekeeper moves"
    client.state["map"] = "临安城.map"
    client.state["variables"]["FromFengChi"] = "1"
exec(compile(ast.Module(body=prefix, type_ignores=[]), "first-visit-gates", "exec"),
     dict(client=client, output=None, idle=lambda client: client.observe(),
          talk=first_visit_talk, go=first_visit_go, verify=verify,
          restock=lambda client: None, late_return_to_linan=first_visit_return))
assert client.calls == [("talk", name) for name in ("婕儿", "宝箱", "柴嵩", "燕若雪", "守门家丁")]
checks += 1

# Chai Song's new skill is practiced without replacing the stronger main attack.
practice_start = next(index for index, statement in enumerate(linan.body)
                      if isinstance(statement, ast.Assign) and isinstance(statement.value, ast.Call)
                      and isinstance(statement.value.func, ast.Name) and statement.value.func.id == "verify"
                      and any(keyword.arg == "FromFengChi" and keyword.value.value == 2
                              for keyword in statement.value.keywords))
practice_end = next(index for index in range(practice_start + 1, len(linan.body))
                   if isinstance(linan.body[index], ast.Expr)
                   and isinstance(linan.body[index].value, ast.Call)
                   and isinstance(linan.body[index].value.func, ast.Name)
                   and linan.body[index].value.func.id == "late_enter_fengchi")
practice_code = compile(ast.Module(body=linan.body[practice_start:practice_end], type_ignores=[]),
                        "mengdie-practice", "exec")
for previous_file, outcome in (
        ("player-magic-风雪狂刀.ini", "normal"), ("player-magic-弯刀冷光.ini", "normal"),
        ("player-magic-风雪狂刀.ini", "wrong-main"), ("player-magic-寒霜掌.ini", "unexpected-practice"),
        ("player-magic-弯刀冷光.ini", "lost-practice"), ("player-magic-风雪狂刀.ini", "duplicate-dream")):
    client = SideClient("临安城.map", {"FromFengChi": "2"})
    client.state.update(layout=dict(magicQuickBegin=36, practiceSlot=41), magic=[
        dict(file="player-magic-天意剑诀.ini", slot=0 if outcome == "wrong-main" else 36, level=6, exp=25875),
        dict(file="player-magic-梦蝶神功.ini", slot=6, level=1, exp=0),
        dict(file=previous_file, slot=41, level=4, exp=5742)])
    def practice(slot):
        assert slot == 6
        client.calls.append(("practice", slot))
        item(client.state, "player-magic-梦蝶神功.ini", "magic")["slot"] = 41
        item(client.state, previous_file, "magic")["slot"] = 6
        if outcome == "lost-practice":
            client.state["magic"].pop()
        elif outcome == "duplicate-dream":
            client.state["magic"].append(dict(file="player-magic-梦蝶神功.ini", slot=7))
    client.assign_practice = practice
    try:
        exec(practice_code, dict(client=client, output=None, item=item, idle=lambda c: c.observe(),
                                 magic_file_prefix="player-magic-",
                                 verify=verify, AutomationError=AutomationError,
                                 checkpoint=lambda c, out, name: c.calls.append(("checkpoint", name))))
    except (AutomationError, AssertionError):
        assert outcome != "normal"
        if outcome in ("wrong-main", "unexpected-practice"):
            assert client.calls == []
    else:
        assert outcome == "normal"
        assert client.calls == [("practice", 6), ("checkpoint", "late-02-mengdie-practice")]
        assert [(spell["slot"], spell["level"], spell["exp"]) for spell in client.state["magic"]] == [
            (36, 6, 25875), (41, 1, 0), (6, 4, 5742)]
    checks += 1

# From6's hall trap advances the story before any walking route reaches the bedroom.
def verifies_stage(statement, **expected):
    return (isinstance(statement, ast.Expr) and isinstance(statement.value, ast.Call)
            and isinstance(statement.value.func, ast.Name) and statement.value.func.id == "verify"
            and all(any(keyword.arg == key and isinstance(keyword.value, ast.Constant)
                        and keyword.value.value == value for keyword in statement.value.keywords)
                    for key, value in expected.items()))

start = next(index for index, statement in enumerate(linan.body) if verifies_stage(statement, FromFengChi=6))
client = SideClient("临安城.map", {"FromFengChi": "6"})
def return_visit_enter(client):
    client.state["map"] = "凤池山庄.map"
def return_visit_talk(client, name):
    assert name == "后门家丁" and client.state["map"] == "凤池山庄.map"
    client.calls.append(("gate", name))
def return_visit_go(client, x, y, **arguments):
    if "script" in arguments:
        assert client.calls == [("gate", "后门家丁")]
        assert (x, y) == (74, 197) and arguments["script"] == "凤池山庄/datingtalk.txt"
        client.state["variables"]["FromFengChi"] = "7"
    else:
        assert client.calls[-1] == ("meditate", "临安城.map")
        if "destination" in arguments:
            client.state["map"] = arguments["destination"]
def return_visit_return(client):
    assert client.state["variables"]["FromFengChi"] == "7"
    client.state["map"] = "临安城.map"
def fake_meditate(client):
    assert client.state["map"] == "临安城.map"
    client.calls.append(("meditate", client.state["map"]))
def pre_pilitang_restock(client, **limits):
    assert client.state["map"] == "临安城.map" and client.state["variables"]["FromFengChi"] == "7"
    assert limits == dict(life_count=8, mana_count=8, reserve_money=1000, max_spend=6500)
    client.calls.append(("restock", client.state["map"]))
exec(compile(ast.Module(body=linan.body[start:], type_ignores=[]), "return-visit-traps", "exec"),
     dict(client=client, output=None, verify=verify, late_enter_fengchi=return_visit_enter,
          talk=return_visit_talk, go=return_visit_go, late_return_to_linan=return_visit_return,
          late_leave_linan=lambda client: return_visit_go(client, 35, 325, destination="稻香村.map"),
          restock=pre_pilitang_restock, meditate=fake_meditate, late_checkpoint=lambda *args: None))
assert client.state["map"] == "霹雳堂.map"
assert client.calls[-2:] == [("restock", "临安城.map"), ("meditate", "临安城.map")]
checks += 1

# Lei Tong's death scene advances Yan Ruoxue before returning through Daoxiang.
pilitang = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "pilitang_and_tournament")
start = next(index for index, statement in enumerate(pilitang.body)
             if isinstance(statement, ast.Expr) and isinstance(statement.value, ast.Call)
             and isinstance(statement.value.func, ast.Name) and statement.value.func.id == "go")
start = next(index for index, statement in enumerate(pilitang.body)
             if isinstance(statement, ast.Expr) and isinstance(statement.value, ast.Call)
             and isinstance(statement.value.func, ast.Name) and statement.value.func.id == "go"
             and any(keyword.arg == "destination" and keyword.value.value == "稻香村.map"
                     for keyword in statement.value.keywords))
end = next(index for index, statement in enumerate(pilitang.body) if verifies_stage(statement, FromFengChi=9))
client = SideClient("霹雳堂.map", {"FromFengChi": "8", "LinAnYanRuoXue": "9"})
def post_lei_go(client, x, y, *, destination):
    client.state["map"] = destination
    client.calls.append(destination)
def post_lei_enter(client):
    verify(client, LinAnYanRuoXue=9)
    client.state["variables"]["FromFengChi"] = "9"
    client.state["map"] = "凤池山庄.map"
exec(compile(ast.Module(body=pilitang.body[start:end + 1], type_ignores=[]), "post-lei-road-return", "exec"),
     dict(client=client, output=Path("unused"), go=post_lei_go,
          magic_file_prefix="player-magic-",
          daoxiang_to_linan=lambda client, output, **kwargs: post_lei_go(client, 123, 87, destination="临安城.map"),
          late_enter_fengchi=post_lei_enter, verify=verify))
assert client.calls == ["稻香村.map", "临安城.map"]
checks += 1

# Both later rests use the existing normal meditation helper while still in Lin'an.
start = next(index for index, statement in enumerate(pilitang.body)
             if isinstance(statement, ast.Assign) and ast.unparse(statement.targets[0]) == "before_supplies")
client = SideClient("临安城.map", {})
def fake_restock(client, **limits):
    assert limits["reserve_money"] == 1000
    client.calls.append(("restock",))
    return client.observe()
exec(compile(ast.Module(body=pilitang.body[start:start + 4], type_ignores=[]), "tournament-rest", "exec"),
     dict(client=client, idle=lambda client: client.observe(), restock=fake_restock, meditate=fake_meditate))
assert client.calls == [("restock",), ("meditate", "临安城.map")]
revenge = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "linan_revenge")
client.calls.clear()
client.state["player"] = dict(position=dict(x=160, y=235))
revenge_rest = next(index for index, node in enumerate(revenge.body)
                    if isinstance(node, ast.Expr) and isinstance(node.value, ast.Call)
                    and ast.unparse(node.value.func) == "meditate")
def revenge_restock(client, **limits):
    assert limits == dict(mana_count=6, max_spend=6000)
    client.calls.append(("restock",))
exec(compile(ast.Module(body=revenge.body[:revenge_rest + 1], type_ignores=[]), "revenge-rest", "exec"),
     dict(client=client, idle=lambda client: client.observe(), meditate=fake_meditate,
          restock=revenge_restock))
assert client.calls == [("restock",), ("meditate", "临安城.map")]
checks += 2

# The broken bridge separates both exits; the elder leaves after the first visit.
outbound = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "hanyang_to_zhongdu")
inbound = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "zhongdu_return_to_linan")
def bridge_segment(function, first, last):
    statements = []
    for statement in function.body:
        if ast.unparse(statement).startswith(first) or statements:
            statements.append(statement)
        if statements and ast.unparse(statement).startswith(last):
            return ast.Module(body=statements, type_ignores=[])
    raise AssertionError("Bridge traversal segment not found")
outbound_code = compile(bridge_segment(outbound, "go(client, 97, 6,", "go(client, 16, 24,"), "bridge-outbound", "exec")
inbound_code = compile(bridge_segment(inbound, "go(client, 20, 332,", "go(client, 5, 72,"), "bridge-inbound", "exec")
for returning, elder_present in [(False, True), (False, False), (True, False)]:
    client = SideClient("中都.map" if returning else "金兵营寨.map", {})
    bank = None
    def bridge_go(client, x, y, *, destination, combat=True):
        global bank
        if returning and client.state["map"] == "金兵营寨.map":
            assert combat is True, "Returning through the camp must clear nearby blocking soldiers"
        elif client.state["map"] in ("金兵营寨.map", "汉阳-金兵营寨.map") or destination == "金兵营寨.map":
            assert combat is False, "Camp transit does not require killing its soldiers"
        if client.state["map"] == "汉阳-中都1.map":
            assert bank == ("west" if returning else "east"), "Walking cannot cross the broken bridge"
        if destination == "汉阳-中都1.map":
            bank = "east" if returning else "west"
            client.state["targets"] = [dict(name="老人")] if elder_present else []
        client.state["map"] = destination
    def bridge_talk(client, name, position):
        assert bank == "west" and elder_present and name == "老人" and position == (4, 12)
        client.calls.append(("talk", name))
        client.state["targets"] = []
    def bridge_jump(client, start, destination):
        global bank
        assert client.state["map"] == "汉阳-中都1.map"
        expected = ((18, 47), (10, 47)) if returning else ((10, 47), (18, 47))
        assert (start, destination) == expected
        bank = "west" if returning else "east"
        client.calls.append(("jump", start, destination))
    exec(inbound_code if returning else outbound_code,
         dict(client=client, go=bridge_go, talk=bridge_talk, late_jump=bridge_jump,
              idle=lambda client: client.observe()))
    assert len(client.calls) == (2 if elder_present else 1)
    assert client.state["map"] == ("汉阳.map" if returning else "中都.map")
    checks += 1

# Evidence can end mid-UTF-8 or reach disk after returning to the title.
with tempfile.TemporaryDirectory() as folder:
    output = Path(folder)
    trace_path = output / "user-data/automation/trace.jsonl"
    trace_path.parent.mkdir(parents=True)
    start = dict(eventType="script.start", executionId=1, virtualPath="script/map/ending.txt")
    finish = dict(eventType="script.finish", executionId=1, status="completed")
    chinese = dict(eventType="api.call", executionId=1, apiName="中文")
    encode = lambda record: (json.dumps(record, ensure_ascii=False) + "\n").encode("utf-8")
    partial = encode(chinese)
    split = partial.index("中".encode("utf-8")) + 1
    for initial, tails, succeeds in [
        (encode(start), [encode(finish)], True),
        (encode(start) + partial[:split], [partial[split:] + encode(finish)[:-1], b"\n"], True),
        (encode(start), [], False),
        (encode(start) + encode(dict(finish, status="aborted")), [], False),
    ]:
        trace_path.write_bytes(initial)
        clock = Clock()
        def sleep(seconds):
            clock.now += seconds
            if tails:
                with trace_path.open("ab") as stream:
                    stream.write(tails.pop(0))
        clock.sleep = sleep
        namespace["time"] = clock
        try:
            records = read_records(output, "trace.jsonl", completed_scripts=("map/ending.txt",))
        except AutomationError:
            assert not succeeds and 3 <= clock.now < 3.1
        else:
            assert succeeds and records[-1] == finish
        checks += 1
    trace_path.write_bytes(encode(start) + b"{invalid}\n")
    try:
        read_records(output, "trace.jsonl")
    except json.JSONDecodeError:
        checks += 1
    else:
        raise AssertionError("Malformed complete trace line accepted")

boss = {"id": 7, "name": "boss", "life": 30000, "position": {"x": 21, "y": 53}}
suffix = "\u5929\u5fcd\u6559-\u5730\u4e0b\u8ff7\u5bab3/\u5927\u7ed3\u5c40.txt"
trace = [{"eventType": "script.start", "executionId": 9, "virtualPath": "script/map/" + suffix},
         {"eventType": "script.finish", "executionId": 9, "status": "completed"},
         {"eventType": "api.call", "executionId": 9, "apiName": "returntotitle"},
         {"eventType": "source.line", "executionId": 9, "line": 55}]


def run_finale_case(level, mana=True, life=True, duration=60, branch=0, prepared=None,
                    tianyi_file="player-magic-天意剑诀.ini", ending_movie_lines=(55, 137),
                    ending_names=("ordinary", "happy")):
    function = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "tianren_finale")
    movie = "happyend.avi" if branch else "end.avi"
    branch_trace = copy.deepcopy(trace)
    branch_trace[-1]["line"] = ending_movie_lines[branch]
    clock = Clock()
    class FakeClient:
        def __init__(self):
            self.state = {"map": "\u5929\u5fcd\u6559-\u5730\u4e0b\u8ff7\u5bab3.map",
                          "scene": "GameManager", "generation": 1, "variables": {"HappyEnding": str(branch)},
                          "magic": [{"slot": 41, "file": tianyi_file, "level": level}],
                          "inventory": [], "player": {"life": 9000, "mana": 1000}, "targets": [copy.deepcopy(boss)]}
            if life:
                self.state["inventory"].append({"file": "owned-life.ini", "quantity": 20, "attribute": "life"})
            if mana:
                self.state["inventory"].append({"file": "owned-mana.ini", "quantity": 20, "attribute": "mana"})
            if prepared is not None:
                self.state["player"].update(life=9000 if prepared else 8000, lifeMax=9000, manaMax=1000)
            self.assignment = None
            self.combat = None
            self.finished = False
        def observe(self, variables=()):
            return copy.deepcopy(self.state)
        def assign_magic(self, slot, quick_slot):
            self.assignment = (slot, quick_slot)
        def submit(self, command, **arguments):
            assert command == "StartCombat"
            self.combat = arguments
            return 42
        def request(self, command, **arguments):
            assert command == "GetActionStatus" and arguments["actionId"] == 42
            done = clock.now >= duration
            self.state["targets"][0]["life"] = 0 if done else 20000
            return {"status": "succeeded" if done else "running", "kills": 1 if done else 0}
        def wait_action(self, action, timeout):
            assert action == 42 and 0 < timeout <= 245
            clock.now = max(clock.now, duration)
            self.state.update(scene="VideoPlayer", video=movie)
            return {"status": "succeeded", "kills": 1}
        def ui(self, action):
            assert action == "Cancel" and self.state.get("video") == movie
            self.state.pop("video")
            self.state["scene"] = "Title"
    client = FakeClient()
    checkpoints = []
    namespace = {"time": clock, "json": json, "AutomationError": AutomationError,
                 "idle": lambda c: c.observe(), "go": lambda *args, **kwargs: None,
                 "late_target": lambda *args: copy.deepcopy(boss),
                 "checkpoint": lambda c, out, name: checkpoints.append(name),
                 "select_medicine": lambda state, attribute, require_ready: next(
                     (item for item in state["inventory"] if item["attribute"] == attribute and item["quantity"] > 0), None),
                 "late_records": lambda out, filename, **kwargs: branch_trace if filename == "trace.jsonl" else [
                     {"event": "action.finish", "data": {"actionId": 42, "status": "succeeded", "kills": 1}}],
                 "late_completed_script": lambda records, suffix: records[0]}
    exec(compile(ast.Module(body=[function], type_ignores=[]), "finale", "exec"), namespace)
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory)
        if (not life and prepared is not True) or (level >= 8 and not mana and prepared is not True):
            try:
                namespace["tianren_finale"](client, output, tianyi_file=tianyi_file,
                                             ending_movie_lines=ending_movie_lines, ending_names=ending_names)
            except AutomationError as error:
                assert "supplies insufficient" in str(error)
            else:
                raise AssertionError("Missing supplies started combat")
            assert client.combat is None and client.assignment is None
            assert "late-09-insufficient-supplies" in checkpoints
            assert (output / "late-09-combat-plan.json").is_file()
            return
        proof = namespace["tianren_finale"](client, output, tianyi_file=tianyi_file,
                                             ending_movie_lines=ending_movie_lines, ending_names=ending_names)
        assert proof["endingCompleted"] and not proof["fullPlaythrough"]
        assert client.combat["skills"] == ([0] if level >= 8 else [])
        assert client.combat.get("lifeItem") == ("owned-life.ini" if life else None)
        assert client.assignment == ((41, 0) if level >= 8 else None)
        if level >= 8 and mana:
            assert client.combat["manaItem"] == "owned-mana.ini"
        else:
            assert "manaItem" not in client.combat
        progress = json.loads((output / "late-09-combat-progress.json").read_text(encoding="utf-8"))
        assert progress["lifeLost"] == (30000 if duration < 30 else 10000)
        assert 0 <= progress["elapsedSeconds"] < 30.3
        assert proof["observedVideos"] == [movie]
        assert proof["happyEnding"] == branch
        assert proof["ending"] == ending_names[branch]


for arguments in ({"level": 8}, {"level": 7}, {"level": 7, "mana": False},
                  {"level": 8, "mana": False}, {"level": 8, "life": False},
                  {"level": 10, "duration": 1}, {"level": 8, "branch": 1},
                  {"level": 10, "mana": False, "life": False, "prepared": True, "duration": 1},
                  {"level": 10, "mana": False, "life": True, "prepared": True},
                  {"level": 10, "mana": False, "life": False, "prepared": False}):
    run_finale_case(**arguments)
    checks += 1

for branch, movie_lines, names in ((0, (57, 140), ("ordinary", "happy")),
                                  (1, (57, 140), ("ordinary", "happy")),
                                  (0, (215, 140), ("true-bitter", "happy"))):
    run_finale_case(level=8, branch=branch, tianyi_file="0player-magic-天意剑诀.ini",
                    ending_movie_lines=movie_lines, ending_names=names)
    checks += 1

# The return to Zhongdu keeps the manor and temple scenes in their native order,
# with bounded city walks before interacting across the buildings.
tianren = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "tianren_dungeons")
temple_verified = next(index for index, node in enumerate(tianren.body)
                       if isinstance(node, ast.Expr) and isinstance(node.value, ast.Call)
                       and any(keyword.arg == "LongYinShiLiaoRan" for keyword in node.value.keywords))
return_client = SideClient("中都.map", {"ZhongDuHouHuaYuan": "5", "FromFengChi": "20"})
return_client.state["player"]["position"] = dict(x=25, y=317)


def return_city_go(client, x, y, **arguments):
    if (x, y) == (140, 264):
        assert client.state["player"]["position"] == dict(x=138, y=263)
        assert arguments["script"] == "中都/trap-7.txt"
        client.state["variables"]["ZhongDuHouHuaYuan"] = "6"
        client.state["player"]["position"] = dict(x=156, y=223)
    else:
        if (x, y) == (138, 263) and client.state["variables"]["ZhongDuHouHuaYuan"] == "6":
            assert arguments["script"] == "中都/trap-7.txt"
            client.state["player"]["position"] = dict(x=140, y=264)
            client.calls.append((x, y))
            return
        if (x, y) == (47, 93):
            assert client.state["variables"]["ZhongDuHouHuaYuan"] == "6"
            assert arguments["script"] == "中都/龙音寺门口地图陷阱5.txt"
            client.state["templeSceneCompleted"] = True
        if (x, y) == (48, 92):
            assert arguments["script"] == "中都/龙音寺门口地图陷阱6.txt"
            client.state.setdefault("templeGateBranches", []).append(
                int(client.state["variables"].get("LongYinShiLiaoRan") or 0))
        client.state["player"]["position"] = dict(x=x, y=y)
    client.calls.append((x, y))


def return_city_talk(client, name, position=None):
    assert name == "大和尚" and position == (60, 34)
    assert client.state["player"]["position"] == dict(x=60, y=35)
    assert client.state["templeSceneCompleted"]
    assert client.state["variables"]["ZhongDuHouHuaYuan"] == "6"
    client.state["variables"]["LongYinShiLiaoRan"] = "1"


exec(compile(ast.Module(body=tianren.body[:temple_verified + 2], type_ignores=[]),
             "tianren-return-city", "exec"),
     dict(client=return_client, idle=lambda client: client.observe(),
          go=return_city_go, talk=return_city_talk, verify=verify))
assert return_client.calls == [(70, 334), (97, 223), (138, 263), (140, 264),
                               (138, 263), (97, 223), (84, 103), (47, 93), (48, 92), (60, 35), (48, 92)]
assert return_client.state["templeGateBranches"] == [0, 1]
city_map = (root / "assets/jxqy2/map/中都.map").read_bytes()
_, city_width, _, city_image_size, _ = struct.unpack_from("<5i", city_map, 64)
city_header, _, _, city_image_count = struct.unpack_from("<4i", city_map, 84)
city_tiles = city_header + city_image_count * city_image_size
for point in return_client.calls:
    tile = city_tiles + (point[1] * city_width + point[0]) * 10
    assert not city_map[tile + 6] & 0xc0
    assert city_map[tile + 7] == {(140, 264): 7, (47, 93): 5, (48, 92): 6}.get(point, 0)
checks += 1

dungeon_floors = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                      and node.name == "tianren_second_floor")
switch_loop = next(node for node in dungeon_floors.body if isinstance(node, ast.For)
                   and "TrjDxmgKg" in ast.unparse(node))
switch_map = (root / "assets/jxqy2/map/天忍教-地下迷宫2.map").read_bytes()
_, switch_width, _, switch_image_size, _ = struct.unpack_from("<5i", switch_map, 64)
switch_header, _, _, switch_image_count = struct.unpack_from("<4i", switch_map, 84)
switch_tiles = switch_header + switch_image_count * switch_image_size
switch_npcs = configparser.ConfigParser(interpolation=None)
switch_npcs.read(root / "assets/jxqy2/ini/save/trj-dixiamigong-2.npc", encoding="utf-8-sig")
switch_client = SideClient("天忍教-地下迷宫2.map", {"TrjDxmgKg": "0"})

def switch_approach(client, x, y):
    tile = switch_tiles + (y * switch_width + x) * 10
    assert not switch_map[tile + 6] & 0xc0 and switch_map[tile + 7] == 0
    assert not any(int(fields.get("mapx", "-1")) == x and int(fields.get("mapy", "-1")) == y
                   for fields in (switch_npcs[section] for section in switch_npcs.sections()))
    client.state["player"]["position"] = dict(x=x, y=y)
    client.calls.append(("approach", x, y))

def switch_interact(client, x, y):
    assert client.state["player"]["position"] == dict(x=x, y=y + 2)
    client.state["variables"]["TrjDxmgKg"] = str(int(client.state["variables"]["TrjDxmgKg"]) + 1)
    client.calls.append(("interact", x, y))

maze_approach_loop = next(node for node in ast.walk(dungeon_floors) if isinstance(node, ast.For)
                          and "42, 119" in ast.unparse(node))
exec(compile(ast.Module(body=[maze_approach_loop, switch_loop], type_ignores=[]), "tianren-switch-approaches", "exec"),
     dict(client=switch_client, opened=0, go=switch_approach, late_object=switch_interact, verify=verify))
assert len(switch_client.calls) == 14 and switch_client.state["variables"]["TrjDxmgKg"] == "4"
checks += 1

gate_approach = next(node for node in ast.walk(dungeon_floors) if isinstance(node, ast.For)
                     and "35, 93" in ast.unparse(node))
gate_points = ast.literal_eval(gate_approach.iter)
assert gate_points[-1] == (31, 75)
for x, y in gate_points:
    tile = switch_tiles + (y * switch_width + x) * 10
    assert not switch_map[tile + 6] & 0xc0 and switch_map[tile + 7] == 0
assert all(keyword.arg != "combat" for keyword in gate_approach.body[0].value.keywords)
checks += 1

second_boss = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                   and node.name == "tianren_second_floor_boss")
gate_script = "天忍教-地下迷宫2/地图陷阱3.txt"
gate_assignment = next(node for node in second_boss.body if isinstance(node, ast.Assign)
                       and ast.unparse(node.targets[0]) == "script")
gate_attempt = next(node for node in second_boss.body if isinstance(node, ast.Try))
for error, position, expected_moves in (
        (None, dict(x=31, y=74), 1),
        (f"Move did not complete requested script: {gate_script}", dict(x=31, y=74), 3),
        ("Move script failed", dict(x=31, y=74), 1),
        (f"Move did not complete requested script: {gate_script}", dict(x=31, y=75), 1)):
    moves = []
    def gate_walk(client, x, y, **arguments):
        moves.append((x, y, arguments))
        if len(moves) == 1 and error:
            raise AutomationError(error)
    failed = False
    try:
        exec(compile(ast.Module(body=[gate_assignment, gate_attempt], type_ignores=[]),
                     "tianren-trap-arrival", "exec"),
             dict(client=object(), go=gate_walk, AutomationError=AutomationError,
                  idle=lambda client: dict(map="天忍教-地下迷宫2.map",
                                           player=dict(position=position))))
    except AutomationError:
        failed = True
    assert len(moves) == expected_moves
    assert failed == bool(error and expected_moves == 1)
    if expected_moves == 3:
        assert moves[1:] == [(31, 75, dict(combat=False)),
                             (31, 74, dict(script=gate_script, combat=False))]
    checks += 1

# Hanyang's normal entrance, rather than the orphan Daoxiang trap4, starts
# the three opponents. A partial challenge must finish before reading its book.
daoxiang = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                and node.name == "daoxiang_to_linan")


class DaoxiangClient(SideClient):
    def __init__(self, stage, learned=False):
        super().__init__("稻香村.map", {"DaoXiangFirstEnter": "1", "DaoXiangFight": str(stage)})
        self.state["targets"] = [dict(id=index + 1, name=name, hostile=True, attackable=True)
                                 for index, name in enumerate(("王重阳", "洪七", "欧阳锋")) if index >= stage]
        if learned:
            self.state["magic"] = [dict(file="player-magic-天师符法.ini", level=1)]
    def act(self, command, **arguments):
        assert command == "UseItem" and arguments == dict(generation=1, slot=5)
        assert self.state["variables"]["DaoXiangFight"] == "3"
        assert self.state["inventory"] == [dict(file="book-天师符法秘笈.ini", slot=5, quantity=1)]
        self.state["inventory"] = []
        self.state["magic"] = [dict(file="player-magic-天师符法.ini", level=1)]
        self.calls.append(("book",))


def daoxiang_fight(client, target):
    client.state["targets"] = [entry for entry in client.state["targets"] if entry["id"] != target["id"]]
    stage = int(client.state["variables"]["DaoXiangFight"]) + 1
    client.state["variables"]["DaoXiangFight"] = str(stage)
    if stage == 3:
        client.state["inventory"] = [dict(file="book-天师符法秘笈.ini", slot=5, quantity=1)]
    client.calls.append(("fight", target["name"]))


daoxiang_namespace = dict(idle=lambda client: client.observe(), verify=verify, item=item,
                         npc_attackable=npc_attackable, AutomationError=AutomationError,
                         fight=daoxiang_fight, go=go,
                         checkpoint=lambda client, output, name: client.calls.append(("checkpoint", name)))
exec(compile(ast.Module(body=[daoxiang], type_ignores=[]), "daoxiang-reward", "exec"), daoxiang_namespace)
for stage, learned in ((0, False), (2, False), (3, True)):
    client = DaoxiangClient(stage, learned)
    daoxiang_namespace["daoxiang_to_linan"](client, Path("unused"))
    assert client.state["map"] == "临安城.map"
    assert sum(call[0] == "fight" for call in client.calls) == 3 - stage
    assert sum(call[0] == "book" for call in client.calls) == int(not learned)
    assert [call[1] for call in client.calls if call[0] == "go"] == [
        (44, 276), (57, 223), (77, 183), (97, 143), (117, 103), (123, 87)]
    checks += 1
for fault in ("missing", "ambiguous"):
    client = DaoxiangClient(2)
    client.state["targets"] = [] if fault == "missing" else client.state["targets"] * 2
    try:
        daoxiang_namespace["daoxiang_to_linan"](client, Path("unused"))
    except (AssertionError, AutomationError):
        assert not client.calls
    else:
        raise AssertionError(f"Invalid Daoxiang challenge accepted: {fault}")
    checks += 1
entrance = (root / "assets/jxqy2/script/map/汉阳/地图切换3.txt").read_text(encoding="utf-8")
assert all(f'setnpcrelation("{name}",1)' in entrance for name in ("王重阳", "洪七", "欧阳锋"))
assert 'loadmap("稻香村.map")' in entrance and 'assign("DaoXiangFirstEnter",1)' in entrance
checks += 1
fengchi_entry = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "late_enter_fengchi")
for initial_position, expected in (((145, 95), [(153, 220), (173, 343), (12, 13)]),
                                  ((145, 309), [(173, 343), (12, 13)]),
                                  ((37, 322), [(60, 310), (100, 302), (153, 309), (173, 343), (12, 13)])):
    client = SideClient("临安城.map", {})
    client.state["player"]["position"] = dict(zip(("x", "y"), initial_position))
    walks = []
    def entry_go(client, x, y, **arguments):
        walks.append((x, y))
        if "destination" in arguments:
            client.state["map"] = arguments["destination"]
        return client.observe()
    namespace = dict(idle=lambda client: client.observe(), go=entry_go, linan_approach=entry_go)
    exec(compile(ast.Module(body=[fengchi_entry], type_ignores=[]), "linan-south-gate", "exec"), namespace)
    namespace["late_enter_fengchi"](client)
    assert walks == expected and client.state["map"] == "凤池山庄.map"
    checks += 1
tile = linan_header + linan_image_count * linan_image_size + (220 * linan_width + 153) * 10
assert not linan_map[tile + 6] & 0xc0 and linan_map[tile + 7] == 0

west_exit = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "late_leave_linan")
for position, stage, expected in (((153, 309), 7, [(100, 302), (60, 310), (35, 325)]),
                                 ((145, 95), 7, [(153, 220), (100, 302), (60, 310), (35, 325)]),
                                 ((67, 134), 7, [(60, 310), (35, 325)]),
                                 ((67, 134), 20, [(60, 310), (35, 325)])):
    client = SideClient("临安城.map", {"FromFengChi": str(stage)})
    client.state["player"]["position"] = dict(zip(("x", "y"), position))
    walks = []
    namespace = dict(idle=lambda client: client.observe(), go=entry_go,
                     _move_script_evidence=lambda client, script: iter((False, False) for _ in range(2)))
    exec(compile(ast.Module(body=[west_exit], type_ignores=[]), "linan-west-gate", "exec"), namespace)
    namespace["late_leave_linan"](client)
    assert walks == expected and client.state["map"] == "稻香村.map"
    checks += 1
for completed, message in ((True, "Unexpected map: 临安城.map; expected 稻香村.map"),
                           (False, "Unexpected map: 临安城.map; expected 稻香村.map"),
                           (True, "Move to 35,325: no_progress")):
    client = SideClient("临安城.map", {})
    client.state["player"]["position"] = {"x": 67, "y": 134}
    attempts = []
    def gate_evidence(client, script):
        assert script == "临安城/first.txt"
        yield False, False
        while True:
            yield True, completed
    def interrupted_exit(client, x, y, **arguments):
        if arguments.get("destination"):
            attempts.append((x, y))
            if len(attempts) == 1:
                raise AutomationError(message)
            client.state["map"] = arguments["destination"]
        return client.observe()
    namespace = dict(idle=lambda client: client.observe(), go=interrupted_exit,
                     _move_script_evidence=gate_evidence, time=Clock(), AutomationError=AutomationError)
    exec(compile(ast.Module(body=[west_exit], type_ignores=[]), "linan-entry-prelude", "exec"), namespace)
    if completed and message.startswith("Unexpected map:"):
        namespace["late_leave_linan"](client)
        assert len(attempts) == 2 and client.state["map"] == "稻香村.map"
    else:
        try:
            namespace["late_leave_linan"](client)
        except AutomationError as error:
            assert str(error) == message and len(attempts) == 1
        else:
            raise AssertionError("Only a newly completed normal entry dialogue permits another exit move")
    checks += 1
approach = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "linan_approach")
for completed, message, retry in (
        (True, "Unexpected script during movement: script/map/临安城/first.txt", True),
        (True, "Move did not complete requested script: 临安城/trap9.txt", True),
        (False, "Unexpected script during movement: script/map/临安城/first.txt", False),
        (True, "Move to 60,310: no_progress", False)):
    attempts = []
    def interrupted_approach(client, x, y, **arguments):
        attempts.append((x, y))
        if len(attempts) == 1:
            raise AutomationError(message)
        return client.observe()
    def approach_evidence(client, script):
        assert script == "临安城/first.txt"
        yield False, False
        while True:
            yield True, completed
    namespace = dict(go=interrupted_approach, _move_script_evidence=approach_evidence,
                     time=Clock(), AutomationError=AutomationError)
    exec(compile(ast.Module(body=[approach], type_ignores=[]), "linan-approach-recovery", "exec"), namespace)
    try:
        namespace["linan_approach"](client, 60, 310, script="临安城/trap9.txt")
    except AutomationError as error:
        assert not retry and str(error) == message and len(attempts) == 1
    else:
        assert retry and len(attempts) == 2
    checks += 1
for point in ((100, 302), (60, 310)):
    tile = linan_header + linan_image_count * linan_image_size + (point[1] * linan_width + point[0]) * 10
    assert not linan_map[tile + 6] & 0xc0 and linan_map[tile + 7] == 0
tile = linan_header + linan_image_count * linan_image_size + (315 * linan_width + 41) * 10
assert not linan_map[tile + 6] & 0xc0 and linan_map[tile + 7] == 1

hall_map = (root / "assets/jxqy2/map/霹雳堂.map").read_bytes()
_, hall_width, _, hall_image_size, _ = struct.unpack_from("<5i", hall_map, 64)
hall_header, _, _, hall_image_count = struct.unpack_from("<4i", hall_map, 84)
hall_tiles = hall_header + hall_image_count * hall_image_size
for point in ((60, 56), (66, 44)):
    tile = hall_tiles + (point[1] * hall_width + point[0]) * 10
    assert not hall_map[tile + 6] & 0xc0 and hall_map[tile + 7] == 0
checks += 1

# A normal quick/practice swap can put a capped attack into the practice slot.
# Continue training a bag skill without removing the attack or healing quick slots.
practice_helper = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                       and node.name == "continue_practice")
practice_namespace = dict(idle=lambda client: client.observe(), item=item, AutomationError=AutomationError)
exec(compile(ast.Module(body=[practice_helper], type_ignores=[]), "continue-practice", "exec"), practice_namespace)
for capped, bags, lost_transfer in ((False, True, False), (True, True, False),
                                     (True, False, False), (True, True, True)):
    client = SideClient("凤池山庄-比武场.map", {})
    client.state.update(layout=dict(magicQuickBegin=36, practiceSlot=41), magic=[
        dict(file="player-magic-梦蝶神功.ini", slot=36, level=5),
        dict(file="player-magic-白虹贯日.ini", slot=38, level=9),
        dict(file="player-magic-天意剑诀.ini", slot=41, level=10 if capped else 9)])
    if bags:
        client.state["magic"] += [dict(file="player-magic-寒霜掌.ini", slot=4, level=9),
                                  dict(file="player-magic-弯刀冷光.ini", slot=8, level=6)]
    def transfer(slot):
        client.calls.append(slot)
        if not lost_transfer:
            item(client.state, "player-magic-寒霜掌.ini", "magic")["slot"] = 41
            item(client.state, "player-magic-天意剑诀.ini", "magic")["slot"] = slot
    client.assign_practice = transfer
    try:
        practice_namespace["continue_practice"](client)
    except AutomationError:
        assert lost_transfer
    else:
        assert not lost_transfer
        assert client.calls == ([4] if capped and bags else [])
    assert item(client.state, "player-magic-梦蝶神功.ini", "magic")["slot"] == 36
    assert item(client.state, "player-magic-白虹贯日.ini", "magic")["slot"] == 38
    checks += 1

tile = linan_header + linan_image_count * linan_image_size + (235 * linan_width + 160) * 10
assert not linan_map[tile + 6] & 0xc0 and linan_map[tile + 7] == 0

# The hall boss may retreat behind its obstacles. Its route must target the
# normal immobilization and allow weapon pursuit, including an interrupted cast.
boss_end = next(index for index, statement in enumerate(pilitang.body)
                if verifies_stage(statement, FromFengChi=8))
for interrupted in (False, True):
    class HallClient:
        def __init__(self):
            self.calls = []
        def assign_magic(self, slot, quick_slot):
            assert slot == 4 and quick_slot == 1
        def act(self, command, **arguments):
            self.calls.append((command, arguments))
            assert arguments["generation"] == 5 and arguments["targetId"] == 77
            if command == "CastSkill" and interrupted:
                raise AutomationError("CastSkill: action_not_executed")
            return dict(kills=1)
    client = HallClient()
    state = dict(map="霹雳堂.map", generation=5,
                 magic=[dict(file="player-magic-定身法.ini", slot=4)])
    exec(compile(ast.Module(body=pilitang.body[:boss_end], type_ignores=[]), "hall-boss-route", "exec"),
         dict(client=client, magic_file_prefix="player-magic-", idle=lambda client: state, go=lambda *args, **kwargs: None,
              late_target=lambda *args: dict(id=77), item=item,
              continue_practice=lambda client: None, AutomationError=AutomationError,
              select_medicine=lambda *args, **kwargs: dict(file="healing.ini"), LIFE_ITEM="life.ini"))
    cast, combat = client.calls
    assert cast[0] == "CastSkill" and cast[1]["slot"] == 1
    assert combat[0] == "StartCombat" and combat[1]["kills"] == 1
    assert combat[1]["skills"] == [] and combat[1]["allowMeleeFallback"]
    checks += 1

# Ao is invulnerable until the normal 22nd guard-death callback resets his level.
guard_helper = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                    and node.name == "clear_fengchi_guards")
guard_code = compile(ast.Module(body=[guard_helper], type_ignores=[]), "fengchi-guards", "exec")
for initial_count in (19, 22):
    client = SideClient("凤池山庄夜战.map", {"FengChiKill": str(initial_count)})
    client.state.update(player=dict(position=dict(x=0, y=0)), targets=[
        dict(id=99, name="敖管家", hostile=True, life=1000000, position=dict(x=0, y=0)),
        *[dict(id=index, name="凤池家丁", hostile=True, life=100, position=dict(x=index, y=0))
          for index in (3, 2, 1)]])
    fought = []
    def guard_fight(client, target, *, skills, prefer_melee):
        assert skills == () and prefer_melee and target["name"] == "凤池家丁"
        fought.append(target["id"])
        client.state["targets"].remove(target)
        client.state["variables"]["FengChiKill"] = str(int(client.state["variables"]["FengChiKill"]) + 1)
    namespace = dict(story_state=lambda client: client.state, verify=verify, fight=guard_fight,
                     npc_attackable=lambda target: target["life"] > 0, AutomationError=AutomationError)
    exec(guard_code, namespace)
    namespace["clear_fengchi_guards"](client)
    assert fought == ([1, 2, 3] if initial_count < 22 else [])
    assert client.state["variables"]["FengChiKill"] == "22"
    checks += 1
client = SideClient("凤池山庄夜战.map", {"FengChiKill": "21"})
client.state.update(player=dict(position=dict(x=0, y=0)), targets=[])
try:
    namespace["clear_fengchi_guards"](client)
    raise AssertionError("missing guard deaths must stop the route")
except AutomationError as error:
    assert "required guard deaths" in str(error)
checks += 1
guard_death = (root / "assets/jxqy2/script/map/凤池山庄夜战/die.txt").read_text(encoding="utf-8")
assert 'getvar("FengChiKill") == 22' in guard_death and 'setnpclevel("敖管家",45)' in guard_death
checks += 1

prison_map = (root / "assets/jxqy2/map/临安大牢.map").read_bytes()
_, prison_width, _, prison_image_size, _ = struct.unpack_from("<5i", prison_map, 64)
prison_header, _, _, prison_image_count = struct.unpack_from("<4i", prison_map, 84)
for x, y in ((78, 77), (79, 78)):
    tile = prison_header + prison_image_count * prison_image_size + (y * prison_width + x) * 10
    assert not prison_map[tile + 6] & 0xc0 and prison_map[tile + 7] == 1
assert 'loadmap("临安大牢-3.map")' in (root / "assets/jxqy2/script/map/临安大牢/地图陷阱1.txt").read_text(encoding="utf-8")
checks += 1

# The exit initially loads all four zero-life NPC records. Death traps remove
# fallen companions during approach, so native story flags identify survivors.
survivor_branch = next(node for node in prison_exit_helper.body if isinstance(node, ast.If)
                       and ast.unparse(node.test) == "state['map'] == '大牢出口.map'")
for all_dead in (False, True):
    client = SideClient("大牢出口.map", dict(Zhao="1", Tang="1", Cai="1" if all_dead else "0", Qiu="0"))
    client.state["targets"] = [dict(kind="npc", name=name, life=0, interactive=True, action=0)
                               for name in ("唐影", "赵无双", "柴嵩")]
    conversations = []
    try:
        exec(compile(ast.Module(body=[survivor_branch], type_ignores=[]), "prison-survivors", "exec"),
             dict(client=client, state=client.state, talk=lambda client, name: conversations.append(name),
                  AutomationError=AutomationError))
    except AutomationError:
        assert all_dead and not conversations
    else:
        assert not all_dead and conversations == ["柴嵩"]
    checks += 1


print(f"{checks} late-route checks passed; no game or pipe opened.")
