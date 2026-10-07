"""Offline checks; never creates a game client or connects to a pipe."""
import configparser
import copy
import contextlib
import io
from pathlib import Path
from unittest.mock import patch

from gameplay_automation import AutomationError
from jxqy2_mainline_supplies import MEDICINES, _OLD_WEAPON_PRICES, meditate, plan_purchases, restock, select_medicine


RED = "goods-yaowu-5-大补散.ini"
BLUE = "goods-yaowu-1-凝神丹.ini"
BIG_RED = "goods-yaowu-7-回天丹.ini"
BIG_BLUE = "goods-yaowu-3-七巧补心丹.ini"


def inventory(filename, quantity, slot=0, cooldown=0):
    return dict(file=filename, quantity=quantity, slot=slot, cooldownMs=cooldown)


def snapshot():
    return dict(resourceId="JXQY2", map="长安.map", generation=1, worldInput=True, worldAction=None,
                autoDialogue=True, inventory=[], player=dict(money=1000, life=500,
                lifeMax=2000, mana=20, manaMax=500), targets=[dict(id=7, kind="npc",
                name="老板2", position=dict(x=127, y=227), interactive=True, hostile=False)])


class FakeClient:
    def __init__(self, state, price_error=0, sale_error=0, equip_error=False):
        self.state = copy.deepcopy(state)
        self.commands = []
        self.price_error = price_error
        self.sale_error, self.equip_error = sale_error, equip_error

    def observe(self):
        return copy.deepcopy(self.state)

    def act(self, command, **arguments):
        if command == "MoveTo":
            expected = {"长安.map": (124, 228), "汉阳.map": (31, 35),
                        "中都.map": (67, 187), "临安城.map": (160, 235)}[self.state["map"]]
            assert (arguments["x"], arguments["y"]) == expected
            self.commands.append(command)
            self.state["player"]["position"] = dict(x=expected[0], y=expected[1])
            return
        assert command == "Interact" and arguments["targetId"] == 7 and not arguments["running"]
        self.commands.append(command)
        self.state["worldInput"] = False
        self.state["shop"] = [dict(file=name, slot=index) for index, name in enumerate(
            (RED, BLUE, "goods-yaowu-6-金石散.ini", "goods-yaowu-2-五花玉露丹.ini"))]
        if self.state["map"] in ("汉阳.map", "中都.map"):
            expected = (31, 35) if self.state["map"] == "汉阳.map" else (67, 187)
            assert self.state["player"]["position"] == dict(x=expected[0], y=expected[1])
            self.state["shop"] += [dict(file=name, slot=index + 4)
                                   for index, name in enumerate((BIG_RED, BIG_BLUE))]
        elif self.state["map"] == "临安城.map":
            assert self.state["player"]["position"] == dict(x=160, y=235)
            self.state["shop"] = [dict(file=name, slot=index) for index, name in enumerate(
                (BIG_RED, BIG_BLUE, *tuple(MEDICINES)[6:]))]

    def wait_until(self, predicate, **unused):
        assert predicate(self.state)
        return self.observe()

    def buy(self, slot):
        filename = self.state["shop"][slot]["file"]
        self.commands.append(filename)
        row = next((item for item in self.state["inventory"] if item["file"] == filename), None)
        if row is None:
            row = inventory(filename, 0, len(self.state["inventory"]))
            self.state["inventory"].append(row)
        row["quantity"] += 1
        self.state["player"]["money"] -= MEDICINES[filename][2] + self.price_error

    def sell(self, slot):
        assert "shop" in self.state
        row = next(item for item in self.state["inventory"] if item["slot"] == slot)
        self.commands.append(("sell", row["file"]))
        self.state["player"]["money"] += _OLD_WEAPON_PRICES[row["file"]] + self.sale_error
        row["quantity"] -= 1
        if row["quantity"] == 0:
            self.state["inventory"].remove(row)

    def equip(self, slot):
        assert self.state["worldInput"] and "shop" not in self.state
        row = next(item for item in self.state["inventory"] if item["slot"] == slot)
        self.commands.append(("equip", row["file"]))
        if not self.equip_error:
            equipped = next(item for item in self.state["inventory"]
                            if item["slot"] == self.state["layout"]["equipmentBegin"] + 2)
            row["slot"], equipped["slot"] = equipped["slot"], row["slot"]
            self.state["player"]["lifeMax"] += 15

    def ui(self, action):
        assert action == "Cancel"
        self.commands.append(action)
        del self.state["shop"]
        self.state["worldInput"] = True


class MeditationClient:
    def __init__(self):
        self.state = snapshot()
        self.state["player"].update(position=dict(x=20, y=20), life=500, lifeMax=500,
                                    mana=0, manaMax=120, thew=100, sitting=False, controlled=False)
        self.commands, self.clock = [], 0

    def observe(self, variables=()):
        return copy.deepcopy(self.state)

    def act(self, command, **arguments):
        assert command == "ToggleSit" and arguments == dict(generation=1)
        self.state["player"]["sitting"] = not self.state["player"]["sitting"]
        self.commands.append(self.state["player"]["sitting"])

    def advance(self, seconds):
        assert seconds == 0.2
        self.clock += seconds
        player = self.state["player"]
        if player["sitting"]:
            player["mana"] = min(player["manaMax"], player["mana"] + 5)
            player["thew"] -= 5
            if player["thew"] == 0 and player["mana"] < player["manaMax"]:
                player["sitting"] = False  # Native exhaustion, not a client command.
        else:
            player["thew"] += 5


def check_meditation():
    client = MeditationClient()
    with patch("jxqy2_mainline_supplies.time.sleep", client.advance), \
            patch("jxqy2_mainline_supplies.time.monotonic", lambda: client.clock):
        state = meditate(client)
    assert state["player"]["mana"] == 120 and not state["player"]["sitting"]
    assert client.commands == [True, True, False], "Wait for natural stamina recovery, then sit again"
    assert state["player"]["money"] == 1000 and state["inventory"] == []

    for guard_deaths in (21, 22, 25):
        client = MeditationClient()
        client.state.update(map="凤池山庄夜战.map", variables=dict(FengChiKill=str(guard_deaths)))
        with patch("jxqy2_mainline_supplies.time.sleep", client.advance), \
                patch("jxqy2_mainline_supplies.time.monotonic", lambda: client.clock):
            if guard_deaths >= 22:
                assert meditate(client)["player"]["mana"] == 120
            else:
                try:
                    meditate(client)
                except AutomationError:
                    assert not client.commands
                else:
                    raise AssertionError("Night recovery requires the normal guard-death prerequisite")

    class CombatMeditationClient(MeditationClient):
        def __init__(self, stock=True):
            super().__init__()
            self.state.update(map="凤池山庄夜战.map", variables=dict(FengChiKill="22"))
            self.state["player"].update(action=0, life=200)
            self.state["targets"].append(dict(kind="npc", hostile=True, attackable=True,
                                              position=dict(x=21, y=20)))
            self.state["inventory"] = [inventory(RED, 1)] if stock else []
        def act(self, command, **arguments):
            if command == "UseItem":
                assert arguments == dict(generation=1, slot=0)
                self.state["inventory"][0]["quantity"] -= 1
                self.state["player"]["life"] = self.state["player"]["lifeMax"]
                self.commands.append("medicine")
            else:
                super().act(command, **arguments)
        def advance(self, seconds):
            super().advance(seconds)
            self.state["player"]["life"] -= 1

    for stock in (False, True):
        client = CombatMeditationClient(stock)
        with patch("jxqy2_mainline_supplies.time.sleep", client.advance), \
                patch("jxqy2_mainline_supplies.time.monotonic", lambda: client.clock):
            try:
                result = meditate(client, allow_combat=True)
            except AutomationError as error:
                assert not stock and "no life medicine" in str(error) and not client.commands
            else:
                assert stock and result["player"]["mana"] == 120
                assert not result["player"]["sitting"] and client.commands[0] == "medicine"
                assert result["inventory"][0]["quantity"] == 0
                assert 300 <= result["player"]["life"] < 500

    client = MeditationClient()
    client.state["resourceId"] = "YYCS"
    try:
        meditate(client, allow_combat=True)
    except AutomationError as error:
        assert "JXQY2 recovery" in str(error) and not client.commands
    else:
        raise AssertionError("Combat recovery must stay within the JXQY2 route")

    for map_name, point, allowed in (
            ("段家庄.map", dict(x=89, y=23), True), ("段家庄.map", dict(x=88, y=23), False),
            ("中都.map", dict(x=25, y=317), True), ("中都.map", dict(x=55, y=44), True)):
        client = MeditationClient()
        client.state["map"] = map_name
        client.state["player"]["position"] = point
        with patch("jxqy2_mainline_supplies.time.sleep", client.advance), \
                patch("jxqy2_mainline_supplies.time.monotonic", lambda: client.clock):
            if allowed:
                assert meditate(client)["player"]["mana"] == 120
            else:
                try:
                    meditate(client)
                except AutomationError:
                    assert not client.commands
                else:
                    raise AssertionError("Unverified courtyard recovery position must be refused")

    for blocker in ("enemy", "zero-life-enemy", "controlled", "locked"):
        client = MeditationClient()
        if blocker in ("enemy", "zero-life-enemy"):
            enemy = dict(kind="npc", hostile=True, life=1, position=dict(x=21, y=20))
            if blocker == "zero-life-enemy":
                enemy.update(life=0, attackable=True)
                client.state["map"] = "中都.map"
                client.state["player"]["position"] = dict(x=25, y=317)
                enemy["position"] = dict(x=26, y=317)
            client.state["targets"].append(enemy)
        elif blocker == "controlled":
            client.state["player"]["controlled"] = True
        else:
            client.state["worldInput"] = False
        try:
            meditate(client)
        except AutomationError:
            assert not client.commands
        else:
            raise AssertionError(f"Meditation must reject {blocker}")

    client = MeditationClient()
    client.state["targets"].append(dict(kind="npc", hostile=True, life=100, attackable=False,
                                        position=dict(x=21, y=20)))
    with patch("jxqy2_mainline_supplies.time.sleep", client.advance), \
            patch("jxqy2_mainline_supplies.time.monotonic", lambda: client.clock):
        assert meditate(client)["player"]["mana"] == 120

    for interruption in ("damage", "generation", "timeout"):
        client = MeditationClient()

        def advance(seconds):
            client.clock += seconds
            if interruption == "damage":
                client.state["player"]["life"] -= 1
            elif interruption == "generation":
                client.state["generation"] += 1

        with patch("jxqy2_mainline_supplies.time.sleep", advance), \
                patch("jxqy2_mainline_supplies.time.monotonic", lambda: client.clock):
            try:
                meditate(client, timeout=0.3)
            except (AutomationError, TimeoutError):
                assert client.commands == ([True] if interruption == "generation" else [True, False])
            else:
                raise AssertionError(f"Meditation must stop on {interruption}")


def check_equipment_sales():
    state = snapshot()
    state["layout"] = dict(equipmentBegin=84, goodsQuickBegin=81)
    state["player"]["money"] = 180
    state["inventory"] = [inventory(name, 1, index + 1) for index, name in enumerate(_OLD_WEAPON_PRICES)] + [
        inventory("goods-cloth-3-布袍.ini", 1, 7), inventory("book-唐门秘笈.ini", 1, 8),
        inventory("goods-sj-6-夜明珠.ini", 1, 9), inventory("unknown-weapon.ini", 1, 10),
        inventory("goods-cloth-1-书生服.ini", 1, 86), inventory("goods-jian-6-雌雄剑.ini", 1, 88)]
    client = FakeClient(state)
    after = restock(client)
    assert client.commands[:6] == [("equip", "goods-cloth-3-布袍.ini"), "MoveTo", "Interact"] + [
        ("sell", name) for name in _OLD_WEAPON_PRICES]
    assert client.commands[6:] == [RED, BLUE, RED, BLUE, RED, "Cancel"]
    assert after["player"]["money"] == 370  # 180 + 860 - 3*130 - 2*140.
    assert after["player"]["lifeMax"] == state["player"]["lifeMax"] + 15
    assert next(row for row in after["inventory"] if row["slot"] == 86)["file"] == "goods-cloth-3-布袍.ini"
    for name in ("book-唐门秘笈.ini", "goods-sj-6-夜明珠.ini", "unknown-weapon.ini", "goods-jian-6-雌雄剑.ini"):
        assert next(row for row in after["inventory"] if row["file"] == name)["quantity"] == 1

    for weapon in ("goods-jian-2-桃花剑.ini", "unknown-better-weapon.ini", "goods-jian-14-剑中之剑.ini"):
        guarded = copy.deepcopy(state)
        guarded["inventory"][-1]["file"] = weapon
        client = FakeClient(guarded)
        restock(client)
        assert client.commands == [("equip", "goods-cloth-3-布袍.ini")]
        assert client.state["player"]["money"] == 180

    # No sale when available cash already pays the bounded restock.
    affluent = copy.deepcopy(state)
    affluent["player"]["money"] = 5000
    client = FakeClient(affluent)
    restock(client)
    assert not any(isinstance(command, tuple) and command[0] == "sell" for command in client.commands)

    for options, fragment in [(dict(sale_error=1), "sale"), (dict(equip_error=True), "Cloth upgrade")]:
        client = FakeClient(state, **options)
        try:
            restock(client)
        except AutomationError as error:
            assert fragment in str(error)
            assert not any(command in (RED, BLUE) for command in client.commands)
            assert len([command for command in client.commands if isinstance(command, tuple)
                        and command[0] == "sell"]) <= 1
        else:
            raise AssertionError("Unverified equipment/sale effects must stop the restock")


def main():
    root = Path(__file__).resolve().parents[1] / "assets/jxqy2/ini/goods"
    for name, values in MEDICINES.items():
        data = configparser.ConfigParser()
        data.read(root / name, encoding="utf-8-sig")
        assert tuple(data.getint("init", key) for key in ("life", "mana", "cost")) == values, name
    for name, price in _OLD_WEAPON_PRICES.items():
        data = configparser.ConfigParser()
        data.read(root / name, encoding="utf-8-sig")
        assert data.getint("init", "cost") // 2 == price and data.getint("init", "sellprice", fallback=0) == 0
    clothes = []
    for name in ("goods-cloth-1-书生服.ini", "goods-cloth-3-布袍.ini"):
        data = configparser.ConfigParser()
        data.read(root / name, encoding="utf-8-sig")
        clothes.append(tuple(data.getint("init", key) for key in ("defend", "lifemax", "thewmax", "attack", "manamax", "evade")))
        assert data["init"]["part"] == "body" and data.getint("init", "kind") == 1
    assert tuple(new - old for old, new in zip(*clothes)) == (7, 15, 15, 0, 0, 0)

    state = snapshot()
    state["inventory"] = [inventory(RED, 2), inventory(BIG_RED, 1, 1), inventory(BLUE, 3, 2),
                          inventory(BIG_BLUE, 1, 3, cooldown=100)]
    assert select_medicine(state, "life")["file"] == BIG_RED
    assert select_medicine(state, "mana")["file"] == BLUE
    assert select_medicine(state, "mana", require_ready=False)["file"] == BIG_BLUE
    cooling = snapshot()
    cooling["inventory"] = [inventory(BIG_BLUE, 1, cooldown=100)]
    assert select_medicine(cooling, "mana") is None
    assert select_medicine(cooling, "mana", require_ready=False)["file"] == BIG_BLUE
    cooling["inventory"][0]["quantity"] = 0
    assert select_medicine(cooling, "mana", require_ready=False) is None
    state["player"]["life"] = 1800
    assert select_medicine(state, "life")["file"] == RED
    assert plan_purchases(state, (RED, BLUE, BIG_RED, BIG_BLUE)) == [RED]
    state["player"]["money"] = 300
    assert plan_purchases(state, (RED, BLUE)) == []
    assert select_medicine(snapshot(), "mana") is None

    client = FakeClient(snapshot())
    with contextlib.redirect_stdout(io.StringIO()) as records:
        after = restock(client, life_count=2, mana_count=2, max_spend=500)
    assert after["player"]["money"] == 600
    assert [item["quantity"] for item in after["inventory"]] == [2, 1]
    assert client.commands == ["MoveTo", "Interact", RED, BLUE, RED, "Cancel"]
    assert records.getvalue().count('"event": "supply.purchase"') == 3

    for resource_id in ("JIAN_ER_GAI_CHENGHE_1_041", "YYCS", "unknown"):
        state = snapshot()
        state["resourceId"] = resource_id
        client = FakeClient(state)
        if resource_id == "JIAN_ER_GAI_CHENGHE_1_041":
            with contextlib.redirect_stdout(io.StringIO()):
                after = restock(client, life_count=2, mana_count=2, max_spend=500)
            assert after["player"]["money"] == 600
            assert client.commands == ["MoveTo", "Interact", RED, BLUE, RED, "Cancel"]
        else:
            try:
                restock(client)
            except AutomationError:
                assert not client.commands
            else:
                raise AssertionError("Unverified resource identities must not shop")

    state = snapshot()
    state["map"] = "汉阳.map"
    state["player"]["position"] = dict(x=35, y=81)
    state["targets"][0].update(name="掌柜3", position=dict(x=32, y=36))
    client = FakeClient(state)
    with contextlib.redirect_stdout(io.StringIO()):
        restock(client, life_count=1, mana_count=0)
    assert client.commands == ["MoveTo", "Interact", RED, "Cancel"]

    state = snapshot()
    state.update(map="临安城.map")
    state["player"].update(money=3000, position=dict(x=149, y=298))
    state["targets"][0].update(position=dict(x=159, y=232))
    client = FakeClient(state)
    with contextlib.redirect_stdout(io.StringIO()):
        after = restock(client, life_count=1, mana_count=1)
    assert client.commands == ["MoveTo", "Interact", BIG_RED, BIG_BLUE, "Cancel"]
    assert after["player"]["money"] == 1960

    client = FakeClient(snapshot(), price_error=1)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            restock(client)
    except AutomationError as error:
        assert "quantity/price" in str(error)
        assert len(client.commands) == 3  # Never repeat an uncertain purchase.
    else:
        raise AssertionError("A price mismatch must stop shopping")

    state = snapshot()
    state.update(map="别离村.map", inventory=[inventory(RED, 2)])
    state["player"]["money"] = 840
    state["targets"][0].update(name="掌柜3", position=dict(x=51, y=90))
    client = FakeClient(state)
    with contextlib.redirect_stdout(io.StringIO()):
        after = restock(client, life_count=2, mana_count=5, reserve_money=100, max_spend=700)
        check_meditation()
        check_equipment_sales()
    assert client.commands == ["Interact"] + [BLUE] * 5 + ["Cancel"]
    assert after["player"]["money"] == 140
    assert next(item["quantity"] for item in after["inventory"] if item["file"] == BLUE) == 5

    # Preserve the six existing mana drugs; only missing life stock is purchased.
    state = snapshot()
    state.update(map="中都.map", inventory=[inventory(RED, 2), inventory(BLUE, 6, 1)])
    state["player"]["money"] = 1120
    state["targets"][0].update(name="老板", position=dict(x=67, y=188))
    client = FakeClient(state)
    with contextlib.redirect_stdout(io.StringIO()):
        after = restock(client, mana_count=6)
    assert client.commands == ["MoveTo", "Interact", RED, RED, "Cancel"]
    assert after["player"]["money"] == 860
    assert next(item["quantity"] for item in after["inventory"] if item["file"] == BLUE) == 6
    client.commands.clear()
    with contextlib.redirect_stdout(io.StringIO()):
        restock(client, mana_count=6)
    assert not client.commands
    print("Supply selection, prices, budget, purchases, verified old-weapon sales and cloth upgrade checks passed")


if __name__ == "__main__":
    main()
