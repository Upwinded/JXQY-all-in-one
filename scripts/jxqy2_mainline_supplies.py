"""Small, bounded JXQY2 restocks through the existing merchant UI."""
from __future__ import annotations

from collections import Counter
import json
import time

from gameplay_automation import AutomationError, npc_attackable

_RESOURCE_IDS = ("JXQY2", "JIAN_ER_GAI_CHENGHE_1_041")


# Actual ini/goods values, not the occasionally inconsistent display descriptions.
# Each tuple is (life restored, mana restored, purchase price at BuyPercent=100).
MEDICINES = {
    "goods-yaowu-5-大补散.ini": (600, 0, 130),
    "goods-yaowu-6-金石散.ini": (1200, 0, 350),
    "goods-yaowu-7-回天丹.ini": (2400, 0, 540),
    "goods-yaowu-1-凝神丹.ini": (0, 160, 140),
    "goods-yaowu-2-五花玉露丹.ini": (0, 320, 380),
    "goods-yaowu-3-七巧补心丹.ini": (0, 640, 500),
    "goods-yaowu-13-九转还魂丹.ini": (1800, 800, 1200),
    "goods-yaowu-14-龙延化毒丹.ini": (2000, 1000, 1400),
    "goods-yaowu-4-千年灵芝.ini": (2000, 800, 1500),
    "goods-yaowu-8-皇家密药.ini": (2400, 1000, 1600),
}

_LOW = ("goods-yaowu-5-大补散.ini", "goods-yaowu-1-凝神丹.ini",
        "goods-yaowu-6-金石散.ini", "goods-yaowu-2-五花玉露丹.ini")
_HIGH = ("goods-yaowu-7-回天丹.ini", "goods-yaowu-3-七巧补心丹.ini")
# These stationary NPC bindings are shared by the normal story map variants.
_SHOPS = {
    "长安.map": ("老板2", (127, 227), _LOW),
    "别离村.map": ("掌柜3", (51, 90), _LOW),
    "汉阳.map": ("掌柜3", (32, 36), _LOW + _HIGH),
    "中都.map": ("老板", (67, 188), _LOW + _HIGH),
    "临安城.map": ("老板2", (159, 232), _HIGH + tuple(MEDICINES)[6:]),
}

# Verified cost / 2 at the route's shops (RecyclePercent=100).
_OLD_WEAPON_PRICES = {
    "goods-jian-1-桃木剑.ini": 210,
    "goods-jian-2-桃花剑.ini": 250,
    "goods-jian-4-三指剑.ini": 400,
}


def _old_weapons(state):
    layout = state.get("layout", {})
    equipment = layout.get("equipmentBegin")
    if equipment is None or not any(
            entry["slot"] == equipment + 4 and entry["file"] == "goods-jian-6-雌雄剑.ini"
            and entry["quantity"] == 1 for entry in state["inventory"]):
        return []
    # San Zhi retains +5 defense/+2 evade versus Ci Xiong; the route explicitly
    # keeps Ci Xiong's +10 attack/+50 life instead. Unknown weapons stay untouched.
    return [entry for entry in state["inventory"]
            if entry["file"] in _OLD_WEAPON_PRICES and entry["quantity"] > 0
            and 0 <= entry["slot"] < layout["goodsQuickBegin"]]


def _equip_cloth_upgrade(client, state):
    layout = state.get("layout", {})
    equipment = layout.get("equipmentBegin")
    if equipment is None:
        return state
    old, new = "goods-cloth-1-书生服.ini", "goods-cloth-3-布袍.ini"
    source = next((entry for entry in state["inventory"] if entry["file"] == new
                   and entry["quantity"] > 0 and 0 <= entry["slot"] < layout["goodsQuickBegin"]), None)
    if source is None or not any(entry["slot"] == equipment + 2 and entry["file"] == old
                                 for entry in state["inventory"]):
        return state
    before = state
    client.equip(source["slot"])
    state = client.wait_until(lambda value: value["worldInput"], timeout=30, description="equip cloth robe")
    record = dict(event="supply.equipment", file=new, previousFile=old, sourceSlot=source["slot"],
                  equipmentSlot=equipment + 2, inventoryBefore=before["inventory"], inventoryAfter=state["inventory"],
                  moneyBefore=before["player"]["money"], moneyAfter=state["player"]["money"],
                  lifeMaxBefore=before["player"]["lifeMax"], lifeMaxAfter=state["player"]["lifeMax"])
    print(json.dumps(record, ensure_ascii=False), flush=True)
    if (state["map"] != before["map"] or state["generation"] != before["generation"]
            or _quantities(state) != _quantities(before) or record["moneyAfter"] != record["moneyBefore"]
            or record["lifeMaxAfter"] != record["lifeMaxBefore"] + 15
            or not any(entry["slot"] == equipment + 2 and entry["file"] == new for entry in state["inventory"])):
        raise AutomationError(f"Cloth upgrade did not match equipment/quantity/life maximum: {record}")
    return state


def meditate(client, *, timeout=90, allow_combat=False):
    """Restore mana with normal sitting and natural standing stamina recovery."""
    state = client.observe()
    towns = {"主角家.map", "狂沙镇.map", "龙门客栈.map", "长安.map", "别离村.map", "汉阳.map", "中都.map", "临安城.map"}
    courtyard_entry = (state.get("map") == "段家庄.map"
                       and state["player"].get("position") == dict(x=89, y=23))
    night_courtyard = False
    if state.get("map") == "凤池山庄夜战.map":
        state = client.observe(("FengChiKill",))
        night_courtyard = int(state.get("variables", {}).get("FengChiKill") or 0) >= 22
    if (state.get("resourceId") not in _RESOURCE_IDS
            or (not allow_combat and state.get("map") not in towns
                and not courtyard_entry and not night_courtyard)):
        raise AutomationError("Meditation requires a verified JXQY2 recovery point")
    generation, map_name = state["generation"], state["map"]
    before = dict(state["player"])
    deadline = time.monotonic() + timeout
    try:
        while True:
            player = state["player"]
            if (state.get("map") != map_name or state["generation"] != generation
                    or not state.get("worldInput") or player.get("controlled")
                    or (state.get("worldAction") or {}).get("status") == "running"):
                raise AutomationError("Meditation interrupted by world/input change")
            if player["life"] <= 0 or (not allow_combat and player["life"] < before["life"]):
                raise AutomationError("Meditation interrupted by damage")
            if allow_combat and player["life"] * 100 < player["lifeMax"] * 60:
                medicine = select_medicine(state, "life")
                if medicine:
                    client.act("UseItem", generation=generation, slot=medicine["slot"])
                    state = client.observe()
                    continue
                if not select_medicine(state, "life", require_ready=False):
                    raise AutomationError("Combat meditation has no life medicine")
            position = player["position"]
            if not allow_combat and any(target.get("kind") == "npc" and target.get("hostile") and npc_attackable(target)
                   and abs(target["position"]["x"] - position["x"]) * 2
                   + abs(target["position"]["y"] - position["y"]) <= 40
                   for target in state.get("targets", [])):
                raise AutomationError("Meditation refused near an active enemy")
            if "sitting" not in player:
                raise AutomationError("This engine does not expose the normal sitting state")
            if player["mana"] >= player["manaMax"]:
                if player["sitting"]:
                    client.act("ToggleSit", generation=generation)
                    state = client.observe()
                    if state["player"]["sitting"]:
                        raise AutomationError("Meditation could not return to standing")
                print(json.dumps(dict(event="supply.meditation", before=before,
                                      after=state["player"]), ensure_ascii=False), flush=True)
                return state
            if time.monotonic() >= deadline:
                raise TimeoutError("Normal meditation did not restore mana before its timeout")
            mana_per_tick = max(int(player["manaMax"] * 0.004 + 0.5), 5)
            remaining_ticks = (player["manaMax"] - player["mana"] + mana_per_tick - 1) // mana_per_tick
            ready_thew = min(100, remaining_ticks * 5)
            if (not player["sitting"] and player["thew"] >= ready_thew
                    and (not allow_combat or player.get("action") in (0, 20))):
                try:
                    client.act("ToggleSit", generation=generation)
                except AutomationError as error:
                    if not allow_combat or "action_rejected" not in str(error):
                        raise
            time.sleep(0.2)
            state = client.observe()
    except (AutomationError, TimeoutError):
        # End our ordinary sit action if the original world still accepts input.
        # Do not issue another world command after loading or changing control.
        if (state.get("map") == map_name and state.get("generation") == generation
                and state.get("worldInput") and state["player"].get("sitting")
                and not state["player"].get("controlled")):
            try:
                client.act("ToggleSit", generation=generation)
            except AutomationError:
                pass
        raise


def _quantities(state):
    counts = Counter()
    for entry in state.get("inventory", []):
        counts[entry["file"]] += max(0, entry["quantity"])
    return counts


def select_medicine(state, attribute, *, require_ready=True):
    """Return an existing, ready inventory row for 'life' or 'mana', or None.

    Prefer the smallest dose covering the current deficit, otherwise the largest
    available dose. At full health/mana this selects the smallest dose for a
    future StartCombat rule. The caller decides whether to use it and when.
    """
    if attribute not in ("life", "mana"):
        raise ValueError("Medicine attribute must be life or mana")
    index = 0 if attribute == "life" else 1
    player = state["player"]
    missing = max(1, player[attribute + "Max"] - player[attribute])
    candidates = [entry for entry in state.get("inventory", [])
                  if entry["file"] in MEDICINES and MEDICINES[entry["file"]][index] > 0
                  and entry["quantity"] > 0
                  and (not require_ready or entry.get("cooldownMs", 0) == 0)]

    def rank(entry):
        amount = MEDICINES[entry["file"]][index]
        return (max(0, missing - amount), max(0, amount - missing),
                MEDICINES[entry["file"]][2], entry["slot"])

    return min(candidates, key=rank) if candidates else None


def plan_purchases(state, available_files, *, life_count=4, mana_count=4,
                   reserve_money=300, max_spend=1200):
    """Plan individual purchases, balancing stocks and retaining a cash reserve.

    Existing higher-tier medicines count toward stock goals. Refill a held kind
    when affordable; otherwise use the cheapest offered medicine. Mixed drugs
    count toward both goals, so this is a modest reserve, not a combat guarantee.
    """
    values = (life_count, mana_count, reserve_money, max_spend)
    if any(type(value) is not int or value < 0 for value in values) or max(life_count, mana_count) > 20:
        raise ValueError("Supply limits must be nonnegative integers; stock goals cannot exceed 20")
    counts = _quantities(state)
    offered = set(available_files) & MEDICINES.keys()
    budget = min(max_spend, max(0, state["player"]["money"] - reserve_money))
    goals = (life_count, mana_count)
    result = []
    for _ in range(sum(goals)):
        stocks = [sum(counts[name] for name, amounts in MEDICINES.items() if amounts[index] > 0)
                  for index in (0, 1)]
        deficient = sorted((index for index in (0, 1) if stocks[index] < goals[index]),
                           key=lambda index: (stocks[index] / goals[index], index))
        chosen = None
        for index in deficient:
            candidates = [name for name in offered if MEDICINES[name][index] > 0
                          and MEDICINES[name][2] <= budget]
            if candidates:
                chosen = min(candidates, key=lambda name: (counts[name] == 0, MEDICINES[name][2], name))
                break
        if chosen is None:
            break
        result.append(chosen)
        counts[chosen] += 1
        budget -= MEDICINES[chosen][2]
    return result


def restock(client, *, life_count=4, mana_count=4, reserve_money=300, max_spend=1200):
    """Interact with one verified merchant, buy one item at a time, then cancel.

    Call only from an idle town checkpoint with automatic dialogue already on.
    No map travel, target-name fallback, or retry after an uncertain purchase.
    Records go to stdout; Client's normal transcript records every UI request.
    """
    state = client.observe()
    limits = dict(life_count=life_count, mana_count=mana_count,
                  reserve_money=reserve_money, max_spend=max_spend)
    if state.get("resourceId") not in _RESOURCE_IDS or state.get("map") not in _SHOPS:
        raise AutomationError("No verified JXQY2 supply shop on this map")
    if not state.get("worldInput") or (state.get("worldAction") or {}).get("status") == "running":
        raise AutomationError("Restock requires idle world input")
    name, position, catalog = _SHOPS[state["map"]]
    plan = plan_purchases(state, catalog, **limits)
    state = _equip_cloth_upgrade(client, state)
    sales = _old_weapons(state)
    funded = dict(state, player=dict(state["player"], money=state["player"]["money"] + sum(
        _OLD_WEAPON_PRICES[entry["file"]] * entry["quantity"] for entry in sales)))
    if len(plan_purchases(funded, catalog, **limits)) <= len(plan):
        sales = []
    if not plan and not sales:
        print(json.dumps(dict(event="supply.skipped", map=state["map"],
                              reason="stock_sufficient_or_budget_too_small"), ensure_ascii=False), flush=True)
        return state
    matches = [target for target in state.get("targets", [])
               if target.get("kind") == "npc" and target["name"] == name
               and target["position"] == dict(x=position[0], y=position[1])
               and target.get("interactive") and not target.get("hostile")]
    if len(matches) != 1:
        raise AutomationError(f"Expected one verified merchant {name} at {position}: {matches}")
    if not state.get("autoDialogue"):
        raise AutomationError("Restock requires the route's automatic dialogue policy")
    map_name, generation = state["map"], state["generation"]
    approach = {"长安.map": (124, 228), "汉阳.map": (31, 35),
                "中都.map": (67, 187), "临安城.map": (160, 235)}.get(map_name)
    if approach is not None:
        # Approach from the verified street side, away from counter barriers.
        client.act("MoveTo", timeout=125, generation=generation, x=approach[0], y=approach[1],
                   running=False, timeoutMs=120000)
        state = client.observe()
        if (state["map"] != map_name or state["generation"] != generation
                or state["player"]["position"] != dict(x=approach[0], y=approach[1])):
            raise AutomationError("Verified medicine counter approach changed unexpectedly")
    client.act("Interact", timeout=125, generation=generation, targetId=matches[0]["id"],
               running=False, timeoutMs=120000)
    state = client.wait_until(lambda value: "shop" in value, timeout=30, description="supply merchant")
    if state["map"] != map_name or state["generation"] != generation:
        raise AutomationError("World changed while approaching supply merchant")
    offered = {entry["file"] for entry in state["shop"]}
    if not set(catalog) <= offered:
        raise AutomationError("Supply merchant does not match the verified catalog")
    for entry in sales:
        filename, slot = entry["file"], entry["slot"]
        for _ in range(entry["quantity"]):
            before = client.observe()
            if (before["map"] != map_name or before["generation"] != generation or "shop" not in before
                    or not any(row["slot"] == slot and row["file"] == filename for row in _old_weapons(before))):
                raise AutomationError("Verified old weapon or supply shop changed before sale")
            client.sell(slot)
            state = client.observe()
            record = dict(event="supply.sale", file=filename, slot=slot, expectedPrice=_OLD_WEAPON_PRICES[filename],
                          quantityBefore=_quantities(before)[filename], quantityAfter=_quantities(state)[filename],
                          moneyBefore=before["player"]["money"], moneyAfter=state["player"]["money"])
            print(json.dumps(record, ensure_ascii=False), flush=True)
            if (record["quantityAfter"] != record["quantityBefore"] - 1
                    or record["moneyAfter"] != record["moneyBefore"] + record["expectedPrice"]):
                raise AutomationError(f"Supply sale did not match quantity/price: {record}")
    plan = plan_purchases(state, catalog, **limits)
    for filename in plan:
        before = client.observe()
        entries = [entry for entry in before.get("shop", []) if entry["file"] == filename]
        if before["map"] != map_name or before["generation"] != generation or len(entries) != 1:
            raise AutomationError("Supply shop changed before purchase")
        price = MEDICINES[filename][2]
        money = before["player"]["money"]
        quantity = _quantities(before)[filename]
        if money - price < reserve_money:
            raise AutomationError("Supply cash reserve changed before purchase")
        client.buy(entries[0]["slot"])
        state = client.observe()
        record = dict(event="supply.purchase", file=filename, expectedPrice=price,
                      quantityBefore=quantity, quantityAfter=_quantities(state)[filename],
                      moneyBefore=money, moneyAfter=state["player"]["money"])
        print(json.dumps(record, ensure_ascii=False), flush=True)
        if record["quantityAfter"] != quantity + 1 or record["moneyAfter"] != money - price:
            raise AutomationError(f"Supply purchase did not match quantity/price: {record}")
    client.ui("Cancel")
    state = client.wait_until(lambda value: value["worldInput"], timeout=30, description="leave supply merchant")
    print(json.dumps(dict(event="supply.finished", map=state["map"], purchases=len(plan),
                          money=state["player"]["money"]), ensure_ascii=False), flush=True)
    return state
