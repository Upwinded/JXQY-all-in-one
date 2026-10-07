"""Offline side-quest behavior checks; never opens a pipe or starts the game."""
import copy
from pathlib import Path
from types import ModuleType
from unittest.mock import patch

from gameplay_automation import AutomationError
from jxqy2_sidequests import bieli_sidequests, changan_sidequests, early_desert_skill, longmen_sidequests


class FakeClient:
    def __init__(self, village):
        self.state = dict(map="别离村.map" if village else "长安.map", generation=1,
                          player=dict(position=dict(x=22 if village else 41, y=42 if village else 313),
                                      money=1000, life=500, lifeMax=500, mana=3, manaMax=120,
                                      thew=100, sitting=False, controlled=False),
                          variables=dict(ChangAnZhiFu="1"),
                          inventory=[], magic=[], targets=[])
        self.commands, self.checkpoints = [], []
        self.desert = [dict(id=index, name=name, kind="npc", hostile=True, life=1200,
                            position=dict(x=x, y=y)) for index, name, x, y in (
            (1000, "剑客b", 57, 145), (1001, "女弓箭手", 58, 145), (1002, "女弓箭手", 57, 147))]
        self.desert.append(dict(id=1003, name="时空门", kind="object", position=dict(x=61, y=152)))
        self.zangma = [dict(id=index, name=name, kind="npc", hostile=True, life=1400,
                            position=dict(x=x, y=y)) for index, name, x, y in (
            (2000, "趟子手", 36, 100), (2001, "刀客掌门", 72, 131), (2002, "剑客掌门1", 77, 123),
            (2003, "刀客掌门1", 83, 109), (2004, "高级拳师", 20, 163))]
        self.zangma.append(dict(id=2005, name="宝箱", kind="object", position=dict(x=10, y=63)))
        self.longmen = [dict(id=2006, name="无赖1", kind="npc", position=dict(x=44, y=57)),
                        dict(id=2007, name="燕若雪", kind="npc", position=dict(x=46, y=54))]
        self.forest = [dict(id=100, name="男童-1", kind="npc", position=dict(x=77, y=11))]
        for index, position in enumerate(((53, 47), (54, 45), (55, 43)), 101):
            self.forest.append(dict(id=index, name="宝箱", kind="object",
                                    position=dict(x=position[0], y=position[1])))
        if not village:
            names = ["上官府家丁"] * 12 + ["陈三"] + ["官兵"] * 13 + [f"枪手{i}" for i in range(1, 6)]
            self.state["targets"] = [dict(id=i, name=name, kind="npc", hostile=i < 13,
                                           life=100, position=dict(x=160, y=185 + i % 3))
                                      for i, name in enumerate(names)]
            self.state["targets"].append(dict(id=999, name="不属于支线的敌人", kind="npc",
                                               hostile=True, life=100, position=dict(x=160, y=185)))

    def observe(self, variables=()):
        return copy.deepcopy(self.state)

    def save_or_load(self, slot, *, load=False):
        assert slot == 2 and not load and self.state["map"] == "别离村.map"
        self.commands.append(("save", slot, int(self.state["variables"].get("BieLiMG", "0"))))
        return self.observe()

    def act(self, command, **arguments):
        assert command == "Interact" and arguments["running"] is False
        assert arguments["generation"] == self.state["generation"]
        target = next(target for target in self.state["targets"] if target["id"] == arguments["targetId"])
        if target["id"] == 2005:
            assert self.state["map"] == "葬马岗.map"
            self.state["inventory"].append(dict(file="zangma-grade-four", quantity=1))
            self.commands.append((command, target["id"]))
            return
        if target["name"] == "时空门":
            assert item(self.state, "player-magic-怒雷指.ini", "magic")["level"] == 1
            self.state["player"]["position"] = dict(x=11, y=64)
            self.commands.append((command, target["id"]))
            return
        assert target["kind"] == "object" and self.state["variables"]["BieLiMG"] == "2"
        self.commands.append((command, target["id"]))
        if target["position"] == dict(x=55, y=43):
            self.state["magic"].append(dict(file="player-magic-定身法.ini", level=1, slot=2))
        else:
            self.state["inventory"].append(dict(file=f"reward-{target['id']}", quantity=1))


def go(client, x, y, **arguments):
    state, variables = client.state, client.state["variables"]
    client.commands.append(("go", x, y, arguments))
    script = arguments.get("script", "")
    if script == "长安/maptrap7.txt":
        assert arguments["combat"] is False
        if int(variables.get("CAFight", 0)) == 0:
            variables["CAFight"] = "1"
        else:
            assert variables["CAFight"] == "2", "The route must not retreat during combat"
    elif script == "别离村/审案.txt":
        variables["BieLiShengAn"] = "1"
        state["player"]["money"] += 200
    if "destination" in arguments:
        assert state["map"] != arguments["destination"]
        state["map"] = arguments["destination"]
        state["generation"] += 1
        state["targets"] = copy.deepcopy(client.forest) if state["map"] == "别离村迷宫.map" else []
        if state["map"] == "沙漠迷宫.map":
            state["targets"] = copy.deepcopy(client.desert)
        if state["map"] == "葬马岗.map":
            state["targets"] = copy.deepcopy(client.zangma)
        if state["map"] == "龙门客栈.map":
            state["targets"] = copy.deepcopy(client.longmen)
        if state["map"] == "别离村.map" and variables.get("BieLiMG") == "1":
            variables["BieLiMG"] = "2"
            state["inventory"].append(dict(file="goods-sj-9-别离村钥匙.ini", quantity=1))
    state["player"]["position"] = dict(x=x, y=y)
    return client.observe()


def talk(client, name, position=None):
    client.commands.append(("talk", name, position))
    if name == "无赖1":
        assert position == (44, 57)
        if client.state["variables"].get("GotoZMG") != "1":
            client.state["variables"].update(GotoZMG="1", LMKZOZhanMaGang="1")
            client.state["player"]["money"] -= 100
        else:
            assert all(client.state["variables"].get(f"{number}ZMG") == str(number) for number in range(1, 6))
            client.state["targets"] = [target for target in client.state["targets"] if target["name"] != name]
    elif name == "燕若雪":
        assert position == (46, 54)
        assert not any(target["name"] == "无赖1" for target in client.state["targets"])
        client.state["variables"]["LMKZOZhanMaGang"] = "0"
    elif name == "男童-1":
        assert position == (77, 11) and client.state["map"] == "别离村迷宫.map"
        client.state["variables"]["BieLiMG"] = "1"
    elif name == "老板":
        assert position == (14, 61) and client.state["map"] == "别离村.map"
        player = client.state["player"]
        player.update(money=player["money"] - 40, life=player["lifeMax"],
                      mana=player["manaMax"], thew=500, position=dict(x=6, y=78))
    else:
        assert name == "上官豹" and client.state["variables"]["CAFight"] == "2"
        client.state["variables"]["ShangGuan"] = "2"
        client.state["inventory"].append(dict(file="grade-four-reward", quantity=1))
    return client.observe()


def fight(client, target, skills=None):
    assert target["id"] != 999, "A side quest must not sweep unrelated enemies"
    if target["name"] == "陈三":
        assert skills == (0,), "Chen San is an archer despite his ordinary NPC name"
    if 2000 <= target["id"] <= 2004:
        assert skills == (0,)
        client.commands.append(("fight", target["id"]))
        next(entry for entry in client.state["targets"] if entry["id"] == target["id"])["life"] = 0
        number = target["id"] - 1999
        client.state["variables"][f"{number}ZMG"] = str(number)
        return client.observe()
    if target["id"] >= 1000:
        assert skills == (0,)
        client.commands.append(("fight", target["id"]))
        next(entry for entry in client.state["targets"] if entry["id"] == target["id"])["life"] = 0
        if target["id"] == 1000:
            client.state["magic"].append(dict(file="player-magic-怒雷指.ini", level=1, slot=4))
        return client.observe()
    variables = client.state["variables"]
    counter = "SGDiaDing" if target["id"] < 13 else "GuanBing"
    if counter == "GuanBing":
        assert variables.get("SGDiaDing") == "13"
    client.commands.append(("fight", target["id"]))
    next(entry for entry in client.state["targets"] if entry["id"] == target["id"])["life"] = 0
    variables[counter] = str(int(variables.get(counter, "0")) + 1)
    if variables.get("SGDiaDing") == "13":
        variables["ShangGuan"] = "1"
        for entry in client.state["targets"]:
            if 13 <= entry["id"] < 31:
                entry["hostile"] = True
    if variables.get("GuanBing") == "18":
        variables["CAFight"] = "2"
    return client.observe()


def verify(client, **variables):
    assert all(client.state["variables"].get(key, "0") == str(value) for key, value in variables.items())
    return client.observe()


def checkpoint(client, output, name):
    client.checkpoints.append(name)
    return client.observe()


def item(state, filename, collection="inventory"):
    matches = [entry for entry in state[collection] if entry["file"] == filename]
    assert len(matches) == 1
    return matches[0]


def evidence(client, script):
    assert script == "沙漠迷宫/获得武功.txt"
    yield False, False
    assert item(client.state, "player-magic-怒雷指.ini", "magic")["level"] == 1
    yield True, True


def meditate(client):
    assert client.state["map"] == "别离村.map"
    assert client.state["player"]["position"] == dict(x=60, y=60)
    client.commands.append(("meditate",))
    client.state["player"]["mana"] = client.state["player"]["manaMax"]
    return client.observe()


def restock(client, **limits):
    assert limits == dict(life_count=2, mana_count=5, reserve_money=100, max_spend=700)
    assert client.state["player"]["mana"] == client.state["player"]["manaMax"]
    client.commands.append(("restock",))
    return client.observe()


def check_changan_combat_races(helpers):
    reasons = ("StartCombat: invalid_enemy", "StartCombat: stale_target",
               "StartCombat: target_unavailable")
    for reason in (*reasons, "StartCombat: no_progress"):
        for advanced in (False, True):
            client = FakeClient(village=False)
            calls = 0

            def competing_fight(client, target, skills=None):
                nonlocal calls
                calls += 1
                if calls == 1:
                    if advanced:
                        fight(client, target, skills)
                    raise AutomationError(reason)
                return fight(client, target, skills)

            with patch.object(helpers, "fight", competing_fight):
                try:
                    state = changan_sidequests(client, Path("unused"))
                except AutomationError as error:
                    assert str(error) == reason
                    assert not advanced or reason not in reasons
                    assert calls == 1, "Unproven or unrelated failure must not retry"
                    assert not any(command[0] == "talk" for command in client.commands)
                else:
                    assert advanced and reason in reasons
                    assert state["variables"]["ShangGuan"] == "2"
                    assert len([command for command in client.commands if command[0] == "fight"]) == 31


def main():
    helpers = ModuleType("run_jxqy2_mainline")
    for name in ("go", "talk", "fight", "verify", "checkpoint", "item"):
        setattr(helpers, name, globals()[name])
    helpers.idle = lambda client: client.observe()
    helpers._move_script_evidence = evidence
    with patch.dict("sys.modules", run_jxqy2_mainline=helpers), \
            patch("jxqy2_mainline_supplies.meditate", meditate), \
            patch("jxqy2_mainline_supplies.restock", restock):
        check_changan_combat_races(helpers)
        client = FakeClient(village=True)
        client.state["map"] = "龙门客栈.map"
        client.state["variables"]["LMKZOQieHuan"] = "1"
        state = longmen_sidequests(client, Path("unused"))
        assert state["map"] == "龙门客栈.map" and state["player"]["money"] == 900
        assert state["variables"]["LMKZOZhanMaGang"] == "0"
        assert [command[1] for command in client.commands if command[0] == "fight"] == [2000, 2004, 2001, 2002, 2003]
        assert ("Interact", 2005) in client.commands and item(state, "zangma-grade-four")["quantity"] == 1
        assert client.commands[-2:] == [("talk", "无赖1", (44, 57)), ("talk", "燕若雪", (46, 54))]
        count = len(client.commands)
        longmen_sidequests(client, Path("unused"))
        assert len(client.commands) == count

        client = FakeClient(village=True)
        client.state["map"] = "主角家-狂沙镇.map"
        client.state["variables"]["ksOcunzhang"] = "3"
        state = early_desert_skill(client, Path("unused"))
        assert state["map"] == "主角家-狂沙镇.map"
        assert [command[1] for command in client.commands if command[0] == "fight"] == [1001, 1002, 1000]
        assert ("Interact", 1003) in client.commands
        assert client.checkpoints[-1] == "side-desert-skill-complete"
        assert item(state, "player-magic-怒雷指.ini", "magic")["level"] == 1
        client = FakeClient(village=True)
        client.state["map"] = "主角家-狂沙镇.map"
        client.state["variables"]["ksOcunzhang"] = "3"
        def unfinished(client, script):
            yield False, False
            yield True, False
        with patch.object(helpers, "_move_script_evidence", unfinished), \
                patch("jxqy2_sidequests.time.monotonic", side_effect=(0, 4)):
            try:
                early_desert_skill(client, Path("unused"))
            except AutomationError as error:
                assert "Missing completed Nulei" in str(error)
                assert ("Interact", 1003) not in client.commands
            else:
                raise AssertionError("A learned spell alone is not completed death-script trace evidence")

        client = FakeClient(village=True)
        state = bieli_sidequests(client, Path("unused"))
        assert state["map"] == "别离村.map" and state["player"]["money"] == 1200
        assert state["variables"]["BieLiMG"] == "2" and len(state["magic"]) == 1
        assert [command[1] for command in client.commands if command[0] == "Interact"] == [101, 102, 103]
        assert sum(entry["quantity"] for entry in state["inventory"]) == 3
        moves = [command[3]["destination"] for command in client.commands
                 if command[0] == "go" and "destination" in command[3]]
        assert moves == ["别离村迷宫.map", "别离村.map"] * 2
        forest_entries = [command for command in client.commands
                          if command[0] == "save" or (command[0] == "go"
                          and command[3].get("destination") == "别离村迷宫.map")]
        assert [command[0] for command in forest_entries] == ["save", "go", "save", "go"]
        assert forest_entries[0] == ("save", 2, 0) and forest_entries[2] == ("save", 2, 2)
        preparation = [command[0] for command in client.commands
                       if command[0] in ("meditate", "restock", "save")]
        assert preparation == ["meditate", "restock", "save"] * 2
        meditation_checkpoints = [name for name in client.checkpoints if "meditation" in name]
        assert meditation_checkpoints == [f"side-bieli-meditation-{phase}-{moment}"
                                          for phase in (0, 2) for moment in ("before", "after")]
        assert not any(command[:2] == ("talk", "老板") for command in client.commands)
        assert "side-bieli-before-child-save-2" in client.checkpoints
        assert "side-bieli-before-chests-save-2" in client.checkpoints
        count = len(client.commands)
        bieli_sidequests(client, Path("unused"))
        assert len(client.commands) == count, "Do not repeat completed quest rewards"

        client = FakeClient(village=True)
        original_act = client.act
        def chenghe_chest(command, **arguments):
            if arguments["targetId"] == 102:
                client.commands.append((command, 102))
                client.state["variables"]["WuDaoDeJing"] = "1"
            else:
                original_act(command, **arguments)
                if arguments["targetId"] == 103:
                    client.state["magic"][0]["file"] = "0player-magic-定身法.ini"
        client.act = chenghe_chest
        state = bieli_sidequests(client, Path("unused"), dingshen_magic_file="0player-magic-定身法.ini",
                                scroll_reward_variable="WuDaoDeJing")
        assert state["variables"]["WuDaoDeJing"] == "1"
        assert sum(entry["quantity"] for entry in state["inventory"]) == 2
        assert item(state, "0player-magic-定身法.ini", "magic")["level"] == 1

        client = FakeClient(village=True)
        client.state["variables"]["BieLiShengAn"] = "1"
        client.state["player"].update(life=400, money=39)
        try:
            bieli_sidequests(client, Path("unused"), need_full_life=True)
        except AutomationError as error:
            assert "requires 40 money" in str(error) and not client.commands
        else:
            raise AssertionError("Insufficient money must not enter the unguarded inn script")

        client = FakeClient(village=True)
        client.state["variables"]["BieLiShengAn"] = "1"
        client.state["player"]["life"] = 400
        state = bieli_sidequests(client, Path("unused"), need_full_life=True)
        assert state["player"]["money"] == 960 and state["player"]["life"] == 500
        lodging = [command for command in client.commands if command[:2] == ("talk", "老板")]
        assert lodging == [("talk", "老板", (14, 61))]
        assert sum(command[0] == "meditate" for command in client.commands) == 1

        client = FakeClient(village=True)
        client.state["variables"]["CuiYanMen2Finish"] = "1"
        try:
            bieli_sidequests(client, Path("unused"))
        except AutomationError as error:
            assert "temporarily closed" in str(error) and not client.commands
        else:
            raise AssertionError("Closed forest must not be entered")

        client = FakeClient(village=False)
        state = changan_sidequests(client, Path("unused"))
        assert len([command for command in client.commands if command[0] == "fight"]) == 31
        assert state["variables"]["CAFight"] == "2" and state["variables"]["ShangGuan"] == "2"
        assert state["player"]["position"] == dict(x=144, y=220)
        assert next(entry for entry in state["targets"] if entry["id"] == 999)["life"] == 100

        client = FakeClient(village=False)
        client.state["variables"].update(SGDiaDing="14", GuanBing="17", CAFight="2", ShangGuan="1")
        client.state["targets"] = []
        state = changan_sidequests(client, Path("unused"), family_guard_count=14, official_count=17)
        assert state["variables"]["ShangGuan"] == "2"
        assert not any(command[0] == "fight" for command in client.commands)

        client = FakeClient(village=False)
        client.state["targets"] = [entry for entry in client.state["targets"] if entry["id"] != 30]
        try:
            changan_sidequests(client, Path("unused"))
        except AutomationError as error:
            assert "GuanBing=17/18" in str(error)
            assert not any(command[0] == "talk" for command in client.commands)
        else:
            raise AssertionError("Missing required guard must not be reported as quest completion")

        client = FakeClient(village=False)
        client.state["variables"]["ChangAnZhiFu"] = "0"
        try:
            changan_sidequests(client, Path("unused"))
        except AutomationError as error:
            assert "western outskirts" in str(error) and not client.commands
        else:
            raise AssertionError("The scene must be enabled by the normal map return")
    print("Side-quest flow, rewards, stage guards, required opponents and normal returns passed")


if __name__ == "__main__":
    main()
