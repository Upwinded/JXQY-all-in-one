"""Small checks for branch inventory completeness and evidence rejection."""
from pathlib import Path
import ast
import hashlib
import json
import re
import tempfile
import sys
from unittest.mock import Mock, patch
import run_yycs_gameplay as route

sys.stdout.reconfigure(encoding="utf-8")

from run_yycs_gameplay import inventory, opening_evidence, save_inventory, source_line, trap_points, reachable_trap, choice_evidence, medicine

root = Path(__file__).resolve().parents[1]
resource = root / "assets/yycs"
kernel = Mock()
kernel.OpenProcess.return_value = 1
kernel.WaitForSingleObject.return_value = 258
def process_image(handle, flags, image, length):
    image.value = "C:/Windows/System32/conhost.exe"
    return True
kernel.QueryFullProcessImageNameW.side_effect = process_image
with patch.object(route.ctypes, "WinDLL", return_value=kernel):
    assert route.process_running(1, "C:/Windows/System32/conhost.exe")
    assert not route.process_running(1, root / "jxqy-all-in-one.exe")
assert kernel.CloseHandle.call_count == 2

bag_client = Mock()
world = dict(scene="MainScene", worldInput=True)
bag_client.observe.side_effect = [dict(scene="MainScene", inEvent=False, worldInput=False, ui=[dict(name="goods-item-0")]), world]
assert route.idle(bag_client) == world
bag_client.ui.assert_called_once_with("Cancel")

kernel.QueryFullProcessImageNameW.side_effect = None
kernel.QueryFullProcessImageNameW.return_value = False
for exited in (True, False):
    kernel.WaitForSingleObject.side_effect = [258, 0 if exited else 258]
    with patch.object(route.ctypes, "WinDLL", return_value=kernel), patch.object(route.ctypes, "get_last_error", return_value=5):
        try:
            assert not route.process_running(1, root / "jxqy-all-in-one.exe")
        except OSError as error:
            assert not exited and error.winerror == 5
        else:
            assert exited, "An inaccessible live process was treated as exited"

full_state = dict(player=dict(life=100, lifeMax=100, mana=50, manaMax=50),
                  targets=[dict(hostile=True, attackable=True)])
with patch.object(route, "idle", return_value=full_state):
    assert route.recover(Mock(), root / "tmp", resource) is full_state
with patch.object(route, "idle", return_value=dict(full_state, player=dict(full_state["player"], mana=49))):
    try:
        route.recover(Mock(), root / "tmp", resource)
    except route.AutomationError as error:
        assert "safe checkpoint" in str(error)
    else:
        raise AssertionError("Actual recovery ignored active enemies")

sit_client = Mock()
sit_client.act.side_effect = route.AutomationError("ToggleSit: action_rejected")
sit_client.observe.return_value = dict(generation=1, player=dict(action=9))
assert not route.tower_sit(sit_client, dict(generation=1))
for state in (dict(generation=1, player=dict(action=0)), dict(generation=2, player=dict(action=9))):
    sit_client.observe.return_value = state
    try:
        route.tower_sit(sit_client, dict(generation=1))
    except route.AutomationError:
        pass
    else:
        raise AssertionError("A sit rejection without the same-world hurt race was hidden")

with tempfile.TemporaryDirectory() as temporary:
    output = Path(temporary)
    (output / "331-ending-one-masked-challenge-source-slot1.json").write_text("{}", encoding="utf-8")
    state = dict(map="map_028_连接地图.map", variables=dict(Result="1", Event="585"), player=dict(life=1), targets=[], inventory=[])
    path = "script/map/map_028_连接地图/杨影枫死亡.txt"
    start = dict(eventType="script.start", virtualPath=path,
                 contentSha256=hashlib.sha256((resource / path).read_bytes()).hexdigest())
    for ambush_inventory in ([], [dict(file="goods-w20-独孤剑.ini", slot=204)]):
        ambush_client = Mock()
        with patch.object(route, "idle", return_value=dict(state, inventory=ambush_inventory)), \
                patch.object(route, "fight_hostiles") as combat, patch.object(route, "fight_named") as melee, \
                patch.object(route, "trace_records", return_value=[start]), patch.object(route, "completed_script", return_value=[]):
            try:
                route.ending_one_aftermath(ambush_client, output, resource, win=True)
            except route.AutomationError as error:
                assert "紫衫蒙面人死亡.txt" in str(error)
            else:
                raise AssertionError("The ordinary player defeat was accepted as a purple killer victory")
        assert combat.call_count + melee.call_count == 1
        ambush_client.move.assert_not_called()

    for owned_magic in ([], [dict(file="player-magic-孤烟逐云.ini")]):
        source = dict(map="map_033_落叶谷.map", variables=dict(Event="555", SenseVal="1315"), player=dict(life=1), magic=owned_magic)
        battle = dict(source, variables=dict(source["variables"], Event="558"))
        after = dict(source, variables=dict(source["variables"], Event="560"))
        meng_client = Mock()
        with patch.object(route, "idle", side_effect=[source, after]), patch.object(route, "transition", return_value=battle), \
                patch.object(route, "checkpoint", return_value=source), patch.object(route, "fight_named") as combat, \
                patch.object(route, "trace_records", return_value=[]):
            try:
                route.meng_challenge_defeat(meng_client, output, resource, win=True)
            except route.AutomationError as error:
                assert "孟知秋死亡.txt" in str(error)
            else:
                raise AssertionError("An ordinary recovery without Meng's death was accepted as a victory")
        combat.assert_called_once()
        assert combat.call_args.kwargs["magic_file"] == ("player-magic-孤烟逐云.ini" if owned_magic else "player-magic-烈火情天.ini")
        meng_client.submit.assert_not_called()

    source = dict(map="map_025_摘星楼.map", variables=dict(Result="1", Event="3005"))
    after = dict(source, variables=dict(source["variables"], Event="3010", EvilValue="100"))
    captive_client = Mock()
    captive_client.observe.side_effect = [source, dict(variables=dict(KillQW1="1"))]
    with patch.object(route, "idle", side_effect=[source, after]), patch.object(route, "load_checkpoint"), \
            patch.object(route, "checkpoint"), patch.object(route, "fight_hostiles") as combat, \
            patch.object(route, "trace_records", return_value=[]):
        try:
            route.ending_one_captive_choices(captive_client, output, resource, win=True)
        except route.AutomationError as error:
            assert "enemy-count completion" in str(error)
        else:
            raise AssertionError("The surrender aftermath was accepted without native enemy-count completion")
    combat.assert_called_once()
    captive_client.submit.assert_not_called()

# The reunion's configured trap 6 has no physical tiles; its real trigger is 11.
assert not trap_points(resource, "map_019_寒波谷.map", 6)
assert reachable_trap(resource, "map_019_寒波谷.map", 11, dict(x=56, y=22)) in ((26, 51), (26, 52))
assert not trap_points(resource, "map_033_落叶谷.map", 2)
assert reachable_trap(resource, "map_033_落叶谷.map", 5, dict(x=2, y=139)) in trap_points(resource, "map_033_落叶谷.map", 5)
assert reachable_trap(resource, "map_019_寒波谷.map", 19, dict(x=44, y=20)) == (44, 20)
assert reachable_trap(resource, "map_019_寒波谷.map", 18, dict(x=37, y=20)) == (37, 20)
# The upper stream permits jumping (0x20); the lower 0x40 water does not.
stream = (resource / "map/map_019_寒波谷.map").read_bytes()
_, stream_width, _, image_size, _ = route.struct.unpack_from("<5i", stream, 64)
header, _, _, image_count = route.struct.unpack_from("<4i", stream, 84)
tile_offset = header + image_count * image_size
for x in range(38, 44):
    for y in (19, 20, 21):
        obstacle = stream[tile_offset + (y * stream_width + x) * 10 + 6]
        assert obstacle == 0 or obstacle & 0x20
assert source_line('choose("--题目", "是", "否", "Answer"); -- ignored') == 'choose("--题目", "是", "否", "Answer"); '
assert source_line('-- choose("ignored", "yes", "no", "Answer");') == ""

try:
    save_inventory(resource, root / "tmp", (root / "tmp/yycs-missing-evidence-directory",))
except ValueError as error:
    assert "Evidence directory is missing" in str(error)
else:
    raise AssertionError("Missing evidence was silently omitted from the coverage audit")

with tempfile.TemporaryDirectory() as temporary:
    fixture = Path(temporary)
    (fixture / "script").mkdir()
    (fixture / "script/common").mkdir()
    (fixture / "script/common/talkindex.txt").write_text('[1,0]题目\n[2,0]等待\n[3,0]硬闯\n', encoding="utf-8")
    (fixture / "script/choice.txt").write_text(
        '-- choose("comment", "yes", "no", "A");\n'
        '::Stage2::\nchoose("--题目", "是", "否", "Answer");\n'
        'if getvar("Answer") == 0 then goto Yes end\n'
        'select(1,2,3,"DoorAnswer");\n', encoding="utf-8")
    catalog = inventory(fixture)
    assert len(catalog["choices"]) == 2
    site = catalog["choices"][0]
    assert site["line"] == 3 and site["label"] == "Stage2"
    assert site["message"] == "--题目" and [item["index"] for item in site["options"]] == [0, 1]
    assert all(item["status"] == "pending" for item in site["options"])
    selected = catalog["choices"][1]
    assert selected["api"] == "select" and selected["message"] == "题目"
    assert [item["text"] for item in selected["options"]] == ["等待", "硬闯"]
    assert not selected["missingTalkIds"]
    assert len(catalog["conditions"]) == 1 and not catalog["fullCoverage"]
    saved = save_inventory(fixture, fixture)
    assert json.loads((fixture / "branch-catalog.json").read_text(encoding="utf-8")) == saved
    (fixture / "run.json").write_text(json.dumps(dict(resourceId="YYCS", difficulty="easy", cheatAssisted=True)), encoding="utf-8")
    (fixture / "03-opening-complete.json").write_text("{}", encoding="utf-8")
    (fixture / "02-difficulty-choice.json").write_text("{}", encoding="utf-8")
    try:
        opening_evidence(fixture)
    except ValueError:
        pass
    else:
        raise AssertionError("Cheat-assisted summary was accepted as normal opening evidence")
    (fixture / "run.json").write_text(json.dumps(dict(resourceId="YYCS", cheatAssisted=False)), encoding="utf-8")
    before = dict(context=10, choiceMessage=site["message"], choices=site["options"])
    after = dict(worldInput=True, variables={site["variable"]: "0"})
    for name, value in (("before.json", before), ("after.json", after)):
        (fixture / name).write_text(json.dumps(value), encoding="utf-8")
    start = dict(eventType="script.start", executionId=1, sequence=1,
                 virtualPath=site["path"], contentSha256=site["sourceSha256"])
    changed = dict(eventType="variable.change", executionId=1, sequence=4,
                   variableName=site["variable"], afterValue="0")
    records = [start, dict(eventType="source.line", executionId=1, sequence=2, line=site["line"]),
               dict(eventType="api.call", executionId=1, sequence=3, apiName="choose"), changed,
               dict(eventType="script.finish", executionId=1, sequence=5, status="completed")]
    (fixture / "user-data/automation").mkdir(parents=True)
    (fixture / "user-data/automation/trace.jsonl").write_text("".join(json.dumps(row) + "\n" for row in records), encoding="utf-8")
    commands = [dict(request=dict(command="Choose", arguments=dict(context=10, options=[0])),
                     response=dict(ok=True, data=dict(actionId=1))),
                dict(request=dict(command="GetActionStatus"), response=dict(data=dict(actionId=1, status="succeeded")))]
    (fixture / "commands.jsonl").write_text("".join(json.dumps(row) + "\n" for row in commands), encoding="utf-8")
    proof = dict(status="passed", cheatAssisted=False, sourceSha256=site["sourceSha256"], scriptCompleted=True,
                 scriptStart=start, variableChange=changed, action=dict(actionId=1), choiceIndex=0,
                 beforeFile=str(fixture / "before.json"), afterFile=str(fixture / "after.json"))
    proof_path = fixture / "choice-proof.json"
    proof_path.write_text(json.dumps(proof), encoding="utf-8")
    assert choice_evidence(fixture, proof_path, site)["choiceIndex"] == 0
    assert choice_evidence(fixture, proof_path, site, trace=records, commands=commands)["choiceIndex"] == 0
    (fixture / "run.json").write_text(json.dumps(dict(resourceId="YYCS", cheatAssisted=True)), encoding="utf-8")
    route.write_json(proof_path, dict(proof))
    assert json.loads(proof_path.read_text(encoding="utf-8"))["cheatAssisted"] is True
    try:
        choice_evidence(fixture, proof_path, site, trace=records, commands=commands)
    except ValueError:
        pass
    else:
        raise AssertionError("Assisted evidence was accepted without explicit admission")
    assert choice_evidence(fixture, proof_path, site, trace=records, commands=commands, allow_cheats=True)["cheatAssisted"] is True
    proof_path.write_text(json.dumps(proof), encoding="utf-8")
    try:
        choice_evidence(fixture, proof_path, site, trace=records, commands=commands, allow_cheats=True)
    except ValueError:
        pass
    else:
        raise AssertionError("An assisted run accepted a proof labelled as normal")
    (fixture / "run.json").write_text(json.dumps(dict(resourceId="YYCS", cheatAssisted=False)), encoding="utf-8")
    start["virtualPath"] = site["path"].upper()
    proof_path.write_text(json.dumps(proof), encoding="utf-8")
    assert choice_evidence(fixture, proof_path, site, trace=records, commands=commands)["choiceIndex"] == 0
    start["contentSha256"] = "different-native-source"
    proof_path.write_text(json.dumps(proof), encoding="utf-8")
    try:
        choice_evidence(fixture, proof_path, site, trace=records, commands=commands)
    except ValueError:
        pass
    else:
        raise AssertionError("Case matching accepted a different native source hash")
    start.update(virtualPath=site["path"], contentSha256=site["sourceSha256"])
    proof_path.write_text(json.dumps(proof), encoding="utf-8")
    (fixture / "after.json").write_text(json.dumps(dict(scene="Title", worldInput=False)), encoding="utf-8")
    try:
        choice_evidence(fixture, proof_path, site, trace=records, commands=commands)
    except ValueError:
        pass
    else:
        raise AssertionError("An unexpected title return was counted as a completed story choice")
    (fixture / "after.json").write_text(json.dumps(after), encoding="utf-8")
    # The script may reuse its choice variable for a later native random draw.
    later = dict(eventType="source.line", executionId=1, sequence=4, line=site["line"] + 1)
    randomized = dict(eventType="variable.change", executionId=1, sequence=5,
                      variableName=site["variable"], afterValue="1")
    reused = records[:3] + [later, randomized, dict(eventType="script.finish", executionId=1, sequence=6, status="completed")]
    before["variables"] = {site["variable"]: "0"}
    after["variables"][site["variable"]] = "1"
    for name, value in (("before.json", before), ("after.json", after)):
        (fixture / name).write_text(json.dumps(value), encoding="utf-8")
    proof.update(variableChange=None, variableUnchanged=True)
    proof_path.write_text(json.dumps(proof), encoding="utf-8")
    assert choice_evidence(fixture, proof_path, site, trace=reused, commands=commands)["choiceIndex"] == 0
    proof.update(variableChange=changed, variableUnchanged=False)
    after["variables"][site["variable"]] = "0"
    (fixture / "after.json").write_text(json.dumps(after), encoding="utf-8")
    proof["choiceIndex"] = 1
    proof_path.write_text(json.dumps(proof), encoding="utf-8")
    try:
        choice_evidence(fixture, proof_path, site)
    except ValueError:
        pass
    else:
        raise AssertionError("An option absent from the native command and trace was counted")
    # Repeating an option does not emit variable.change when the value already matches.
    before["variables"] = {site["variable"]: "0"}
    (fixture / "before.json").write_text(json.dumps(before), encoding="utf-8")
    (fixture / "user-data/automation/trace.jsonl").write_text(
        "".join(json.dumps(row) + "\n" for row in records if row != changed), encoding="utf-8")
    proof.update(choiceIndex=0, variableChange=None, variableUnchanged=True)
    proof_path.write_text(json.dumps(proof), encoding="utf-8")
    assert choice_evidence(fixture, proof_path, site)["choiceIndex"] == 0
    before["variables"][site["variable"]] = "1"
    (fixture / "before.json").write_text(json.dumps(before), encoding="utf-8")
    try:
        choice_evidence(fixture, proof_path, site)
    except ValueError:
        pass
    else:
        raise AssertionError("A missing native change was accepted despite a different initial value")
    (fixture / "ini/goods").mkdir(parents=True)
    (fixture / "ini/goods/drug.ini").write_text('[Init]\nKind=0\nLife=70\nMana=\n', encoding="utf-8")
    (fixture / "ini/goods/weapon.ini").write_text('[Init]\nKind=1\nLife=1000\n', encoding="utf-8")
    state = dict(inventory=[dict(file="weapon.ini", quantity=1, slot=204), dict(file="drug.ini", quantity=2, slot=0)],
                 player=dict(life=30, lifeMax=100, mana=40, manaMax=100))
    assert medicine(state, fixture, "Life")["file"] == "drug.ini"
    assert medicine(state, fixture, "Mana") is None

catalog = inventory(resource)
sites = catalog["choices"]
assert len({site["id"] for site in sites}) == len(sites)
assert catalog["counts"]["choiceSites"] == 76 and catalog["counts"]["choiceOptions"] == 152
assert catalog["counts"]["chooseSites"] == 51 and catalog["counts"]["selectSites"] == 25
assert all((resource / site["path"]).is_file() for site in sites)
assert all(len(site["options"]) == 2 for site in sites)
found = {f"{path.relative_to(resource).as_posix()}:{number}"
         for path in (resource / "script").rglob("*.txt")
         for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1)
         if re.search(r"\b(?:choose(?:ex|plus|multiple)?|select)\s*\(", source_line(line), re.I)}
assert found == {site["id"] for site in sites}, "An inline or additional choice API escaped inventory"
assert not any('assign("Result",3)' in source_line(line).replace(" ", "")
               for path in (resource / "script").rglob("*.txt")
               for line in path.read_text(encoding="utf-8-sig").splitlines())
assert trap_points(resource, "map_002_凌绝峰峰顶.map", 1)
assert trap_points(resource, "map_001_凌绝峰连接地图.map", 2)
assert reachable_trap(resource, "map_003_武当山下.map", 2, dict(x=8, y=93)) == (35, 4)
path = reachable_trap(resource, "map_003_武当山下.map", 2, dict(x=8, y=93), with_path=True)
assert path[0] == (8, 93) and path[-1] == (35, 4)
assert all(abs(a[0] - b[0]) <= 1 and abs(a[1] - b[1]) <= 2 and a != b for a, b in zip(path, path[1:]))
seventh_floor = "map_047_通天塔第七层.map"
entrance = trap_points(resource, seventh_floor, 1)
safe_path = reachable_trap(resource, seventh_floor, 8, dict(x=8, y=84), with_path=True, avoid=entrance)
assert safe_path[1] == (9, 84) and not set(safe_path[1:]).intersection(entrance)
assert reachable_trap(resource, "map_005_洗剑池.map", 3, dict(x=6, y=77)) == (13, 52)
assert reachable_trap(resource, "map_007_连接地图.map", 2, dict(x=4, y=13), {(17, 96)}) != (17, 96)

# A conversation on the path must not count as entering the requested map.
origin = dict(map="map_023_连接地图.map", generation=1, targets=[],
              player=dict(position=dict(x=26, y=23)), worldInput=True, script="")
destination = dict(origin, map="map_024_倚天山.map", generation=2)


class TravelClient:
    def __init__(self, observations):
        self.observations = iter(observations)
        self.moves = 0

    def observe(self, variables):
        return next(self.observations)

    def submit(self, command, **arguments):
        assert command == "MoveTo"
        self.moves += 1
        return self.moves

    def request(self, command, **arguments):
        return dict(status="running")


client = TravelClient([dict(origin, inEvent=True), destination])
with patch.object(route, "idle", side_effect=[origin, origin, origin, destination]):
    assert route.transition(client, resource, destination["map"], 1) == destination
assert client.moves == 2
client = TravelClient([dict(origin, inEvent=True)] * 5)
with patch.object(route, "idle", return_value=origin):
    try:
        route.transition(client, resource, destination["map"], 1)
    except route.AutomationError:
        pass
    else:
        raise AssertionError("A permanently closed exit was accepted as a map transition")
assert client.moves == 5
# An interaction can start a movie before its world-changing action completes.
interaction_origin = dict(map="map_016_剑气峰.map", generation=1, player=dict(position=dict(x=1, y=1)),
                          targets=[dict(name="Actor", kind="npc", interactive=True, action=11, id=6, position=dict(x=1, y=1)),
                                   dict(name="Actor", kind="npc", interactive=True, life=0, action=0, id=7, position=dict(x=2, y=1))])


class MovieInteractionClient:
    def __init__(self):
        self.cancelled_video = False
        self.polls = 0

    def submit(self, command, **arguments):
        assert command == "Interact" and arguments["targetId"] == 7
        return 1

    def observe(self, variables):
        return dict(interaction_origin, video="yyf-fall.wmv", context=2) if self.polls == 0 else interaction_origin

    def ui(self, action):
        assert action == "Cancel"
        self.cancelled_video = True

    def request(self, command, **arguments):
        assert command == "GetActionStatus"
        self.polls += 1
        return dict(status="running" if self.polls == 1 else "succeeded")


movie_client = MovieInteractionClient()
with patch.object(route, "idle", return_value=interaction_origin), patch.object(route, "checkpoint") as capture:
    assert route.interact_named(movie_client, root / "tmp", resource, "Actor") == interaction_origin
    assert movie_client.cancelled_video and movie_client.polls == 2 and capture.call_count == 1

foe = dict(id=7, name="Foe", kind="npc", hostile=True, attackable=True, action=0, life=10,
           position=dict(x=1, y=1))
combat_state = dict(player=dict(mana=0, position=dict(x=0, y=0)), targets=[foe],
                    magic=[dict(file="player-magic-烈火情天.ini", level=9)])
with patch.object(route, "fight_named") as fight:
    combat_client = type("CombatClient", (), {"observe": lambda self, variables=(): next(self.states)})()
    combat_client.states = iter([combat_state, dict(combat_state, targets=[])])
    route.fight_hostiles(combat_client, root / "tmp", resource)
    assert fight.call_args.kwargs["use_magic"] is False and fight.call_args.kwargs["single_target"] is True
with patch.object(route, "fight_named", side_effect=route.AutomationError("Expected an attackable Foe")) as fight:
    combat_client.states = iter([combat_state, dict(combat_state, targets=[]), dict(combat_state, targets=[])])
    route.fight_hostiles(combat_client, root / "tmp", resource)
    assert fight.call_count == 1
with patch.object(route, "fight_named", side_effect=[route.AutomationError("StartCombat: item_depleted: drug.ini"), None]) as fight:
    combat_client.states = iter([combat_state, dict(combat_state, inventory=[]), combat_state, dict(combat_state, targets=[])])
    route.fight_hostiles(combat_client, root / "tmp", resource)
    assert fight.call_count == 2

# A different enemy can move into melee range while the selected target stalls.
with patch.object(route, "fight_named", side_effect=[route.AutomationError("StartCombat: target_unreachable"), None]) as fight:
    combat_client.states = iter([combat_state, dict(combat_state, targets=[])])
    route.fight_hostiles(combat_client, root / "tmp", resource, choice=("last-kill", 0))
    assert [call.kwargs["target_id"] for call in fight.call_args_list] == [7, 7]
    assert fight.call_args.kwargs["use_magic"] is False and fight.call_args.kwargs["single_target"] is True
    assert fight.call_args.kwargs["choice"] == ("last-kill", 0)
crowded_hostiles = dict(combat_state, targets=[dict(foe, position=dict(x=4, y=1)),
                                              dict(foe, id=8, position=dict(x=1, y=0))])
for reason in ("no_progress", "action_timeout"):
    with patch.object(route, "checkpoint", return_value=crowded_hostiles), \
            patch.object(route, "fight_named", side_effect=[route.AutomationError(f"StartCombat: {reason}"), None]) as fight:
        combat_client.states = iter([combat_state, dict(combat_state, targets=[])])
        route.fight_hostiles(combat_client, root / "tmp", resource, choice=("last-kill", 1))
        assert [call.kwargs["target_id"] for call in fight.call_args_list] == [7, 8]
        assert fight.call_args.kwargs["single_target"] is True and fight.call_args.kwargs["choice"] == ("last-kill", 1)

with patch.object(route, "checkpoint", return_value=combat_state), \
        patch.object(route, "fight_named", side_effect=route.AutomationError("StartCombat: no_progress")) as fight:
    combat_client.states = iter([combat_state])
    try:
        route.fight_hostiles(combat_client, root / "tmp", resource)
    except route.AutomationError as error:
        assert str(error) == "StartCombat: no_progress" and fight.call_count == 1
    else:
        raise AssertionError("An unchanged stalled target was silently retried")
with patch.object(route, "checkpoint", return_value=crowded_hostiles), \
        patch.object(route, "fight_named", side_effect=[route.AutomationError("StartCombat: target_unreachable"),
                                                        route.AutomationError("StartCombat: no_progress"), None]) as fight:
    combat_client.states = iter([combat_state, dict(combat_state, targets=[])])
    route.fight_hostiles(combat_client, root / "tmp", resource)
    assert [call.kwargs["target_id"] for call in fight.call_args_list] == [7, 7, 8]

# A crowded firing target can stall while a different enemy moves next to the player.
tower_state = dict(combat_state, map="map_046_通天塔第六层.map", generation=1,
                   player=dict(mana=500, position=dict(x=0, y=0)),
                   magic=[dict(file="player-magic-魂牵梦绕.ini", level=1)],
                   targets=[dict(foe, position=dict(x=4, y=1))])
crowded = dict(tower_state, targets=[*tower_state["targets"], dict(foe, id=8, position=dict(x=1, y=0))])
with patch.object(route, "idle", side_effect=[tower_state, dict(tower_state, targets=[])]), \
        patch.object(route, "checkpoint", return_value=crowded), \
        patch.object(route, "fight_named", side_effect=[route.AutomationError("StartCombat: no_progress"), None]) as fight, \
        patch.object(route, "trap_points", return_value=[]), \
        patch.object(route, "reachable_trap", return_value=[(0, 0)]), \
        patch.object(route, "transition", return_value=tower_state):
    route.tower_transition(combat_client, root / "tmp", resource, tower_state["map"], 9)
    assert [call.kwargs["target_id"] for call in fight.call_args_list] == [7, 8]
    assert fight.call_args.kwargs["single_target"] is True

ranged_state = dict(tower_state, player=dict(mana=0, manaMax=200, life=100, lifeMax=100,
                    sitting=False, thew=20, action=0, position=dict(x=0, y=0)),
                    targets=[dict(foe, attackRadius=8)])
rested_state = dict(ranged_state, player=dict(ranged_state["player"], mana=200, sitting=True))
rest_client = type("RestClient", (), {
    "observe": lambda self, variables=(): rested_state,
    "act": lambda self, command, **arguments: self.actions.append(command)})()
rest_client.actions = []
with patch.object(route, "idle", side_effect=[ranged_state, dict(rested_state, targets=[])]), \
        patch.object(route, "checkpoint"), patch.object(route, "trap_points", return_value=[]), \
        patch.object(route, "reachable_trap", return_value=[(0, 0)]), \
        patch.object(route, "transition", return_value=rested_state):
    route.tower_transition(rest_client, root / "tmp", resource, ranged_state["map"], 9)
assert rest_client.actions == ["ToggleSit", "ToggleSit"] and ranged_state["player"]["mana"] == 0

with patch.object(route, "idle", return_value=dict(tower_state, targets=[])), \
        patch.object(route, "trap_points", return_value=[]), \
        patch.object(route, "reachable_trap", return_value=[(n, 0) for n in range(17)]), \
        patch.object(route, "transition", return_value=tower_state) as travel:
    route.tower_transition(combat_client, root / "tmp", resource, tower_state["map"], 8)
    assert travel.call_count == 1  # Sixteen steps can land on the choice trap itself.

neutral_guard = dict(foe, hostile=False, attackable=False, position=dict(x=1, y=0))
with patch.object(route, "idle", return_value=dict(tower_state, targets=[neutral_guard])), \
        patch.object(route, "trap_points", return_value=[]), \
        patch.object(route, "reachable_trap", return_value=[(0, 0), (0, 1)]) as path, \
        patch.object(Path, "is_file", return_value=False), patch.object(route, "transition", return_value=tower_state):
    route.tower_transition(combat_client, root / "tmp", resource, tower_state["map"], 8)
    assert (1, 0) in path.call_args.kwargs["avoid"], "The tower route walks into a neutral guard"

backtrack_source = "script/map/map_044_通天塔四层/trap01.txt"
backtrack_start = dict(eventType="script.start", executionId=27, virtualPath=backtrack_source,
                      contentSha256=hashlib.sha256((resource / backtrack_source).read_bytes()).hexdigest())
fourth_floor = dict(tower_state, map="map_044_通天塔四层.map", variables=dict(Event="3182"))
third_floor = dict(fourth_floor, map="map_043_通天塔第三层.map", generation=2, targets=[])
for earlier in ([], [backtrack_start]):
    with patch.object(route, "idle", side_effect=[fourth_floor, third_floor, dict(fourth_floor, targets=[])]), \
            patch.object(route, "trap_points", return_value=[]), patch.object(route, "reachable_trap", return_value=[(0, 0)]), \
            patch.object(Path, "is_file", return_value=True), \
            patch.object(route, "trace_records", side_effect=[earlier, [backtrack_start]]), \
            patch.object(route, "completed_script") as complete, patch.object(route, "checkpoint"), \
            patch.object(route, "fight_named", side_effect=route.AutomationError("StartCombat: world_changed")), \
            patch.object(route, "transition", return_value=fourth_floor) as travel:
        try:
            route.tower_transition(combat_client, root / "tmp", resource, fourth_floor["map"], 8)
        except route.AutomationError:
            assert earlier, "A fresh native tower backtrack was rejected"
        else:
            assert not earlier, "An old execution was accepted as a new backtrack"
            assert [call.args[3] for call in travel.call_args_list] == [2, 8]
            complete.assert_called_once_with(root / "tmp", backtrack_start)

fast_source = "script/map/map_046_通天塔第六层/trap09.txt"
fast_start = dict(eventType="script.start", executionId=9, virtualPath=fast_source,
                  contentSha256=hashlib.sha256((resource / fast_source).read_bytes()).hexdigest())
fast_client = type("FastClient", (), {"observe": lambda self, variables=(): tower_state})()
for earlier in ([], [fast_start]):
    with patch.object(route, "idle", return_value=dict(tower_state, targets=[])), \
            patch.object(route, "trap_points", return_value=[]), patch.object(route, "reachable_trap", return_value=[(0, 0)]), \
            patch.object(Path, "is_file", return_value=True), \
            patch.object(route, "trace_records", side_effect=[earlier, [fast_start]]), \
            patch.object(route, "completed_script"), patch.object(route, "checkpoint", return_value=tower_state), \
            patch.object(route, "transition", side_effect=route.AutomationError("Trap was not entered: arrived")):
        try:
            route.tower_transition(fast_client, root / "tmp", resource, tower_state["map"], 9)
        except route.AutomationError:
            assert earlier, "A fresh completed fast trap was rejected"
        else:
            assert not earlier, "An old script execution was accepted as a new trap"

combat_records = []
with patch.object(route, "idle", side_effect=[tower_state, dict(tower_state, targets=[])]), \
        patch.object(route, "trap_points", return_value=[]), patch.object(route, "reachable_trap", return_value=[(0, 0)]), \
        patch.object(Path, "is_file", return_value=True), \
        patch.object(route, "trace_records", side_effect=lambda output: list(combat_records)), \
        patch.object(route, "fight_named", side_effect=lambda *args, **kwargs: combat_records.append(fast_start)), \
        patch.object(route, "completed_script"), patch.object(route, "checkpoint", return_value=tower_state), \
        patch.object(route, "transition", side_effect=route.AutomationError("Trap was not entered: arrived")):
    route.tower_transition(fast_client, root / "tmp", resource, tower_state["map"], 9)
assert combat_records == [fast_start]  # Combat can cross the trigger before the final move.

terminal = "script/common/主角死亡.txt"
terminal_start = dict(eventType="script.start", executionId=1, virtualPath=terminal,
                      contentSha256=hashlib.sha256((resource / terminal).read_bytes()).hexdigest())
dialogue = dict(scene="对白", context=7, frame=1, dialogue=dict(text="………… 剧终 …………", complete=False))


def wait_for_rendered_dialogue(self, predicate, **arguments):
    ready = dict(dialogue, frame=1 + 30 * self.render_waits, dialogue=dict(dialogue["dialogue"], complete=True))
    assert predicate(ready) and self.actions[-1] == ("SetAutoDialogue", dict(enabled=False))
    self.render_waits += 1
    return ready


ending_client = type("EndingClient", (), {
    "observe": lambda self, variables=(): next(self.states),
    "wait_until": wait_for_rendered_dialogue,
    "act": lambda self, command, **arguments: self.actions.append((command, arguments))})()
ending_client.states = iter([dialogue, dialogue, dict(scene="Title")])
ending_client.actions = []
ending_client.render_waits = 0
with patch.object(route, "trace_records", return_value=[terminal_start]), patch.object(route, "completed_script", return_value=[dict(apiName="returntotitle")]), patch.object(route, "checkpoint", return_value=dialogue) as capture:
    assert route.idle(ending_client, output=root / "tmp", resource=resource, expected_terminal=terminal)["scene"] == "Title"
    assert capture.call_count == 1
    assert ending_client.render_waits == 2
assert ending_client.actions == [("SetAutoDialogue", dict(enabled=False)), ("SetAutoDialogue", dict(enabled=True, intervalMs=1000))]

ending_client.states = iter([dict(dialogue, dialogue=dict(text="武当山跳涯自杀", complete=True)), dict(scene="Title")])
with patch.object(route, "trace_records", return_value=[terminal_start]), patch.object(route, "completed_script", return_value=[dict(apiName="returntotitle")]), patch.object(route, "checkpoint") as capture:
    route.idle(ending_client, output=root / "tmp", resource=resource, expected_terminal=terminal, final_dialogue="跳涯自杀")
    assert capture.call_count == 1

ending_client.states = iter([dict(scene="Title")])
with patch.object(route, "trace_records", return_value=[terminal_start]), patch.object(route, "completed_script", return_value=[dict(apiName="returntotitle")]), patch.object(route, "checkpoint") as capture:
    ending_client.states = iter([dialogue, dict(scene="Title")])
    route.idle(ending_client, output=root / "tmp", resource=resource, expected_terminal=terminal,
               final_dialogue=("different epilogue", "剧终"))
    assert capture.call_count == 1

ending_client.states = iter([dict(scene="Title")])
with patch.object(route.time, "monotonic", side_effect=[0, 181]), \
        patch.object(route, "trace_records", return_value=[terminal_start]), \
        patch.object(route, "completed_script", return_value=[dict(apiName="returntotitle")]):
    assert route.idle(ending_client, output=root / "tmp", resource=resource, expected_terminal=terminal)["scene"] == "Title"

with tempfile.TemporaryDirectory() as temporary:
    output = Path(temporary)
    (output / "530-ending-one-finale-source-slot0.json").write_text(json.dumps(dict(variables=dict(EvilValue="100"))), encoding="utf-8")
    (output / "ending-dialogue-1.json").write_text(json.dumps(dict(dialogue=dict(text="剧终", complete=True))), encoding="utf-8")
    (output / "story-video-1.json").write_text(json.dumps(dict(video="end2.wmv")), encoding="utf-8")
    battle = dict(tower_state, map="map_030_悲魔山庄.map", scene="MainScene", inEvent=False,
                  variables=dict(Event="3210", NpcCount="2"), inventory=[])
    finale_client = Mock()
    finale_client.observe.side_effect = [battle, battle, battle, dict(scene="Title")]
    death_path = "script/map/map_030_悲魔山庄/死亡.txt"
    start = dict(eventType="script.start", virtualPath=death_path,
                 contentSha256=hashlib.sha256((resource / death_path).read_bytes()).hexdigest())
    final_line = next(n for n, line in enumerate((resource / death_path).read_text(encoding="utf-8-sig").splitlines(), 1) if "剧终" in line)
    records = [start, dict(eventType="source.line", line=final_line), dict(apiName="returntotitle")]
    with patch.object(route, "idle", return_value=battle), patch.object(route, "checkpoint", return_value=dict(scene="Title")), \
            patch.object(route, "fight_named", side_effect=route.AutomationError("StartCombat: item_depleted: drug.ini")) as fight, \
            patch.object(route, "trace_records", return_value=[start]), patch.object(route, "completed_script", return_value=records):
        route.ending_one_finale(finale_client, output, resource)
        assert fight.call_count == 1  # Reobserve after the last medicine instead of ending the route.

    closer = dict(battle, targets=[dict(foe, id=8, position=dict(x=0, y=1))])
    for error in ("StartCombat: no_progress", "StartCombat: action_timeout"):
        finale_client.observe.side_effect = [battle, battle, closer, dict(scene="Title")]
        with patch.object(route, "idle", return_value=battle), \
                patch.object(route, "checkpoint", side_effect=[closer, dict(scene="Title")]), \
                patch.object(route, "fight_named", side_effect=[route.AutomationError(error), None]) as fight, \
                patch.object(route, "trace_records", return_value=[start]), patch.object(route, "completed_script", return_value=records):
            route.ending_one_finale(finale_client, output, resource)
            assert [call.kwargs["target_id"] for call in fight.call_args_list] == [7, 8]
    finale_client.observe.side_effect = [battle, battle]
    with patch.object(route, "idle", return_value=battle), patch.object(route, "checkpoint", return_value=battle), \
            patch.object(route, "fight_named", side_effect=route.AutomationError("StartCombat: no_progress")):
        try:
            route.ending_one_finale(finale_client, output, resource)
        except route.AutomationError as error:
            assert str(error) == "StartCombat: no_progress"
        else:
            raise AssertionError("The finale accepted a stall against the same closest target")

for event, meeting, items in (("146", "0", []), ("110", "1", []),
                             ("110", "0", [dict(file="goods-n13-紫霞玉佩.ini", quantity=1)])):
    client = Mock()
    client.observe.return_value = dict(map="map_012_惠安镇.map", variables=dict(Event=event, Result="0", Lany=meeting), inventory=items)
    try:
        route.hermit_amulet_early(client, root / "tmp", resource)
    except route.AutomationError as error:
        assert "first-morning town source" in str(error)
    else:
        raise AssertionError("Early amulet preparation accepted an advanced or already purchased source")
    client.move.assert_not_called()

for event, result, meeting, own_before in (("232", "0", "0", None), ("234", "0", "0", None),
                                           ("185", "0", "1", None), ("600", "1", "0", 10), ("185", "0", "0", 11),
                                           ("1809", "2", "0", 10)):
    client = Mock()
    client.observe.return_value = dict(map="map_019_寒波谷.map", variables=dict(Event=event, Result=result, Lany=meeting), inventory=[])
    try:
        route.hanbo_hermit(client, root / "tmp", resource, own_before=own_before)
    except route.AutomationError as error:
        assert "before any meeting" in str(error)
    else:
        raise AssertionError("Hermit gift route accepted an unsupported or previously visited source")

for map_name, event, result, quest in (("map_014_连接地图.map", "500", "0", "0"),
                                      ("map_012_惠安镇.map", "1806", "2", "0"),
                                      ("map_012_惠安镇.map", "570", "1", "10")):
    client = Mock()
    client.observe.return_value = dict(map=map_name, variables=dict(Event=event, Result=result, SubEvent14=quest))
    try:
        route.huian_fishing_hook(client, root / "tmp", resource)
    except route.AutomationError as error:
        assert "before accepting the quest" in str(error)
    else:
        raise AssertionError("Fishing hook accepted an unsupported or already completed source")

for quest in ("5", "10", "20"):
    client = Mock()
    client.observe.return_value = dict(map="map_022_清平乡.map", variables=dict(Event="170", Result="0", SubEvent08=quest, HaveTalk="0"))
    try:
        route.qingping_missing_boy(client, root / "tmp", resource)
    except route.AutomationError as error:
        assert "without a prior quest" in str(error)
    else:
        raise AssertionError("Missing-boy branches accepted an already started quest")

for knife_inventory, quest in (([dict(file="goods-w19-土龙刀.ini")], "0"), ([], "5")):
    client = Mock()
    client.observe.return_value = dict(map="map_012_惠安镇.map", inventory=knife_inventory,
                                      variables=dict(Result="1", Event="570", SubEvent19=quest))
    try:
        route.huian_knife_missing(client, root / "tmp", resource)
    except route.AutomationError as error:
        assert "without the quest item" in str(error)
    else:
        raise AssertionError("Missing-knife dialogue accepted a fulfilled or item-bearing source")

for phase in (("0", "185"), ("1", "680"), ("2", "1821")):
    source = dict(map="map_012_惠安镇.map", variables=dict(zip(("Result", "Event"), phase)))
    with patch.object(route, "idle", return_value=source):
        try:
            route.huian_flowers_insufficient(Mock(), root / "tmp", resource)
        except route.AutomationError as error:
            assert "normal funded" in str(error)
        else:
            raise AssertionError("Flower depletion accepted an unsupported story source")

flower_source = dict(map="map_016_剑气峰.map", variables=dict(Event="500", Result="0", SenseVal="965", EvilVal="1010"),
                     player=dict(money=570), inventory=[])
wrong_price = dict(flower_source, player=dict(money=550), inventory=[dict(file="goods-e21-玫瑰花.ini", quantity=1)])
with patch.object(route, "idle", return_value=flower_source), patch.object(route, "checkpoint"), \
        patch.object(route, "transition"), patch.object(route, "interact_named", return_value=wrong_price):
    try:
        route.pre_letter_flowers(Mock(), root / "tmp", resource)
    except route.AutomationError as error:
        assert "payment" in str(error)
    else:
        raise AssertionError("Boundary preparation accepted a flower purchased at the wrong native price")

unfunded = dict(flower_source, player=dict(money=248))
funding_client = Mock()
with patch.object(route, "idle", return_value=unfunded), patch.object(route, "checkpoint", return_value=unfunded), \
        patch.object(route, "transition"), patch.object(route, "interact_named") as interact:
    try:
        route.pre_letter_flowers(funding_client, root / "tmp", resource, roses=3)
    except route.AutomationError as error:
        assert "treasure" in str(error) and interact.call_args.kwargs["position"] == (4, 30)
    else:
        raise AssertionError("An unfunded boundary route proceeded to flower purchases")
funding_client.move.assert_called_once_with(90, 100, running=True)

with patch.object(route, "idle", return_value=flower_source):
    weapon_client = Mock()
    try:
        route.petrify_weapon_preparation(weapon_client, root / "tmp", resource)
    except route.AutomationError as error:
        assert "pre-marriage" in str(error)
    else:
        raise AssertionError("Weapon preparation accepted the story-locked pre-letter phase")
    assert weapon_client.method_calls == []

poison_client = Mock()
poison_client.observe.return_value = dict(map="map_033_落叶谷.map", player=dict(life=0))
try:
    route.fight_with_poison(poison_client, root / "tmp", resource, "孟知秋")
except route.AutomationError as error:
    assert "player_dead" in str(error)
else:
    raise AssertionError("A defeated player continued the poison victory attempt")
poison_client.act.assert_not_called()

for chapter, source_map, event in ((route.ending_two_island_search, "map_012_惠安镇.map", "1821"),
                                   (route.ending_two_ambush, "map_062_禁地密室.map", "1823"),
                                   (route.ending_two_meng_help, "map_058_禁地.map", "2000"),
                                   (route.ending_two_tower_offers, "map_032_天山.map", "2002"),
                                   (route.ending_two_zixuan_rescue, "map_025_摘星楼.map", "2004"),
                                   (route.ending_two_fortress_rescue, "map_019_寒波谷.map", "2007"),
                                   (route.ending_two_fortress_rescue, "map_018_连接地图.map", "2007")):
    for wrong in (dict(map=source_map, variables=dict(Event=event, Result="1")),
                  dict(map=source_map, variables=dict(Event="1818", Result="2"))):
        guarded_client = Mock()
        with patch.object(route, "idle", return_value=wrong):
            try:
                chapter(guarded_client, root / "tmp", resource)
            except route.AutomationError:
                pass
            else:
                raise AssertionError("The sisters' route accepted a different native story source")
        assert guarded_client.method_calls == []

ambush_source = dict(map="map_062_禁地密室.map", variables=dict(Event="1823", Result="2", SenseVal="1250", EvilVal="1000"))
captured = dict(map="map_058_禁地.map", player=dict(life=100), variables=dict(ambush_source["variables"], Event="2000"))
capture_path = "script/map/map_058_禁地/同伴死亡.txt"
capture_start = dict(eventType="script.start", executionId=100, virtualPath=capture_path,
                     contentSha256=hashlib.sha256((resource / capture_path).read_bytes()).hexdigest())
ambush_death_path = "script/map/map_058_禁地/无忧教死亡.txt"
ambush_death_start = dict(eventType="script.start", executionId=99, virtualPath=ambush_death_path,
                         contentSha256=hashlib.sha256((resource / ambush_death_path).read_bytes()).hexdigest())
for previous, digest, finish_records, all_clear in (([capture_start], capture_start["contentSha256"], [dict(variableName="Event", afterValue="2000")], False),
                                                   ([], "stale", [dict(variableName="Event", afterValue="2000")], False),
                                                   ([], capture_start["contentSha256"], [], False),
                                                   ([], capture_start["contentSha256"], [dict(variableName="Event", afterValue="2000")], False),
                                                   ([], capture_start["contentSha256"], [dict(variableName="Event", afterValue="2000")], True)):
    run_client = Mock()
    def finish_ambush(output, start):
        return [dict(variableName="NpcCount", afterValue="0")] if start["executionId"] == 99 else finish_records
    with tempfile.TemporaryDirectory() as temporary:
        with patch.object(route, "idle", side_effect=[ambush_source, captured]), \
                patch.object(route, "trace_records", side_effect=[previous, [dict(capture_start, contentSha256=digest)], [ambush_death_start] if all_clear else []]), \
                patch.object(route, "transition"), patch.object(route, "checkpoint", return_value=dict(captured, variables=ambush_source["variables"])), \
                patch.object(route, "fight_hostiles", side_effect=route.AutomationError("StartCombat: world_changed" if all_clear else "StartCombat: target_unavailable")), \
                patch.object(route, "completed_script", side_effect=finish_ambush):
            try:
                route.ending_two_ambush(run_client, Path(temporary), resource)
            except route.AutomationError:
                assert previous or digest == "stale" or not finish_records
            else:
                assert not previous and digest != "stale" and finish_records
                proof = json.loads((Path(temporary) / "ending-two-ambush-proof.json").read_text(encoding="utf-8"))
                assert proof["route"] == ("all-enemies-cleared" if all_clear else "companion-loss")
                assert len(proof["allClearExecutions"]) == int(all_clear)

with patch.object(route, "idle", side_effect=[ambush_source, dict(captured, variables=ambush_source["variables"])]), \
        patch.object(route, "trace_records", return_value=[]), patch.object(route, "transition"), \
        patch.object(route, "checkpoint", return_value=ambush_source), \
        patch.object(route, "fight_hostiles", side_effect=route.AutomationError("StartCombat: target_unavailable")):
    try:
        route.ending_two_ambush(Mock(), root / "tmp", resource)
    except route.AutomationError as error:
        assert "capture and Meng referral" in str(error)
    else:
        raise AssertionError("A vanished enemy was accepted without the native capture outcome")

herb_client = Mock()
with patch.object(route, "idle", return_value=dict(map="map_003_武当山下.map",
        variables=dict(Event="46", SubEvent01="10", SubEvent02="20"))):
    try:
        route.wudang_herb_dialogues(herb_client, root / "tmp", resource)
    except route.AutomationError:
        pass
    else:
        raise AssertionError("Random herb dialogue accepted an already completed quest")
assert herb_client.method_calls == []

shoes_client = Mock()
with patch.object(route, "idle", return_value=dict(map="map_022_清平乡.map", variables=dict(Event="232"), player=dict(money=550))):
    try:
        route.cangjian_shoes_insufficient(shoes_client, root / "tmp", resource)
    except route.AutomationError:
        pass
    else:
        raise AssertionError("Free-shoes spending accepted a source outside its checked budget")
assert shoes_client.method_calls == []

tree = ast.parse(Path(route.__file__).read_text(encoding="utf-8"))
client_calls = {node.func.attr for node in ast.walk(tree) if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Attribute) and isinstance(node.func.value, ast.Name)
                and node.func.value.id == "client"}
assert client_calls <= set(dir(route.Client)), "A route calls an unavailable Client method"
chapter_argument = next(node for node in ast.walk(tree) if isinstance(node, ast.Call)
                        and isinstance(node.func, ast.Attribute) and node.func.attr == "add_argument"
                        and node.args and isinstance(node.args[0], ast.Constant) and node.args[0].value == "--chapter")
chapters = set(ast.literal_eval(next(item.value for item in chapter_argument.keywords if item.arg == "choices")))
dispatched = set()
for node in ast.walk(tree):
    if isinstance(node, ast.Compare) and ast.unparse(node.left) == "args.chapter":
        for value in node.comparators:
            if isinstance(value, ast.Constant) and isinstance(value.value, str):
                dispatched.add(value.value)
            elif isinstance(value, ast.Tuple):
                dispatched.update(ast.literal_eval(value))
assert chapters - {"opening"} <= dispatched, "A CLI chapter has no execution branch"

site = dict(id="delayed-choice:17", path="delayed-choice", sourceSha256="source-digest", line=17,
            coverageId="delayed", message="Choose", variable="XZ", options=[dict(index=0, text="A"), dict(index=1, text="B")])
start = dict(eventType="script.start", virtualPath=site["path"], contentSha256=site["sourceSha256"], executionId=1)
line = dict(eventType="source.line", executionId=1, line=17)
choice_client = Mock()
choice_client.observe.return_value = dict(context=3, choiceMessage="Choose", choices=[dict(index=0, text="A"), dict(index=1, text="B")])
choice_client.act.side_effect = RuntimeError("verified-choice-dispatched")
with patch.object(route, "inventory", return_value=dict(choices=[site])), \
        patch.object(route, "trace_records", side_effect=[[], [start, dict(line, line=16)], [start, line]]), \
        patch.object(route.time, "sleep"), patch.object(route, "write_json"), patch.object(route.shutil, "copyfile"):
    try:
        route.choose_site(choice_client, root / "tmp", resource, site["id"], 0)
    except RuntimeError as error:
        assert str(error) == "verified-choice-dispatched"
choice_client.act.assert_called_once_with("Choose", context=3, options=[0])
for trace, clock, message in (([dict(start, contentSha256="wrong"), line], [0], "source is missing or changed"),
                              ([], [0, 0.1, 3.1], "matching active source line")):
    choice_client.act.reset_mock()
    with patch.object(route, "inventory", return_value=dict(choices=[site])), \
            patch.object(route, "trace_records", return_value=trace), \
            patch.object(route.time, "monotonic", side_effect=clock), patch.object(route.time, "sleep"):
        try:
            route.choose_site(choice_client, root / "tmp", resource, site["id"], 0)
        except route.AutomationError as error:
            assert message in str(error)
        else:
            raise AssertionError("Invalid or absent native choice source was accepted")
    choice_client.act.assert_not_called()

print("YYCS inventory, evidence rejection, delayed choice trace, chapter dispatch, native map traps, and depleted-mana combat passed")
