"""Optional JXQY2 quests executed through ordinary gameplay controls."""
import json
import time

from gameplay_automation import AutomationError, npc_attackable


def early_desert_skill(client, output, *, swordsman_name="剑客b", magic_file="player-magic-怒雷指.ini"):
    """Learn Nulei before first entering Kuangsha; later return routes are closed."""
    from run_jxqy2_mainline import (
        _move_script_evidence, checkpoint, fight, go, idle, item, verify,
    )

    state = idle(client)
    if state["map"] != "主角家-狂沙镇.map":
        raise AutomationError("Nulei requires the early desert-village save, before entering Kuangsha")
    verify(client, ksOcunzhang=3)
    if any(entry["file"] == magic_file for entry in state["magic"]):
        return checkpoint(client, output, "side-desert-skill-complete")
    checkpoint(client, output, "side-desert-before-entry")
    go(client, 78, 160, destination="沙漠迷宫.map",
       script="主角家-狂沙镇/地图切换2.txt")
    state = idle(client)
    bosses = [target for target in state["targets"]
              if target["kind"] == "npc" and target["name"] == swordsman_name
              and target["position"] == dict(x=57, y=145) and npc_attackable(target)]
    if len(bosses) != 1:
        raise AutomationError(f"Expected the Nulei death-script owner at 57,145: {bosses}")
    boss_id = bosses[0]["id"]
    guards = [target["id"] for target in state["targets"]
              if target["kind"] == "npc" and target["name"] == "女弓箭手"
              and target["position"] in (dict(x=58, y=145), dict(x=57, y=147))]
    if len(guards) != 2:
        raise AutomationError("The two archers beside the Nulei swordsman are missing")
    evidence = _move_script_evidence(client, "沙漠迷宫/获得武功.txt")
    next(evidence)
    try:
        # Static map/solid-object corridor, avoiding a direct chase through the
        # maze walls. Existing go() clears threats as each ordinary leg advances.
        for point in ((18, 83), (29, 75), (39, 81), (49, 72), (56, 57), (66, 63),
                      (73, 79), (82, 91), (83, 120), (78, 140), (70, 155),
                      (63, 170), (53, 180), (43, 169), (36, 154), (28, 139),
                      (25, 116), (30, 96), (38, 110), (38, 140), (46, 144)):
            go(client, *point)
        checkpoint(client, output, "side-desert-before-swordsman")
        go(client, 51, 144, combat=False)
        for target_id in (*guards, boss_id):
            state = idle(client)
            target = next((target for target in state["targets"]
                           if target["id"] == target_id and npc_attackable(target)), None)
            if target is not None:
                fight(client, target, skills=(0,))
        state = idle(client)
        if item(state, magic_file, "magic")["level"] < 1:
            raise AutomationError("The swordsman's death did not grant Nulei")
        deadline = time.monotonic() + 3
        while not next(evidence)[1]:
            if time.monotonic() >= deadline:
                raise AutomationError("Missing completed Nulei death-script trace")
            time.sleep(0.05)
    finally:
        evidence.close()
    checkpoint(client, output, "side-desert-nulei-learned")
    go(client, 60, 152)
    state = idle(client)
    gates = [target for target in state["targets"]
             if target["kind"] == "object" and target["name"] == "时空门"
             and target["position"] == dict(x=61, y=152)]
    if len(gates) != 1:
        raise AutomationError(f"Expected the maze's normal return portal: {gates}")
    client.act("Interact", generation=state["generation"], targetId=gates[0]["id"],
               running=False, timeoutMs=60000, timeout=65)
    state = idle(client)
    if state["map"] != "沙漠迷宫.map" or state["player"]["position"] != dict(x=11, y=64):
        raise AutomationError("The normal maze portal did not return to its entrance")
    go(client, 11, 63, destination="主角家-狂沙镇.map",
       script="沙漠迷宫/地图切换.txt", combat=False)
    verify(client, ksOcunzhang=3)
    return checkpoint(client, output, "side-desert-skill-complete")


def changan_sidequests(client, output, *, family_guard_count=13, official_count=18,
                       npc_battle_wait_seconds=0):
    """Complete the Shangguan/officials fight after returning from Fengxue."""
    from run_jxqy2_mainline import checkpoint, fight, go, idle, talk, verify

    variables = ("ChangAnZhiFu", "CAFight", "SGDiaDing", "GuanBing", "ShangGuan")
    state = idle(client)
    if state["map"] != "长安.map":
        raise AutomationError("Shangguan quest requires normal arrival in Changan")
    state = client.observe(variables)
    if state["variables"].get("ChangAnZhiFu") != "1":
        raise AutomationError("Return from Changan's western outskirts to enable the Shangguan scene")
    if int(state["variables"].get("CAFight") or 0) == 0:
        # Approach the south gate without attacking the actors before the scene.
        for point in ((45, 297), (48, 279), (55, 269), (59, 253), (67, 245),
                      (79, 245), (88, 239), (97, 233), (105, 237), (117, 237),
                      (123, 235), (127, 235), (129, 237), (136, 228), (143, 219)):
            go(client, *point, combat=False)
        go(client, 146, 215, script="长安/maptrap7.txt", combat=False)
        verify(client, CAFight=1)
        checkpoint(client, output, "side-changan-fight-start")

    if npc_battle_wait_seconds:
        from jxqy2_mainline_supplies import select_medicine
        go(client, 169, 162, combat=False, running=True)
        deadline = time.monotonic() + npc_battle_wait_seconds
        previous = None
        while time.monotonic() < deadline:
            idle(client)
            state = client.observe(variables)
            progress = tuple(int(state["variables"].get(key) or 0) for key in ("SGDiaDing", "GuanBing"))
            if progress != previous:
                print(f"Native NPC battle: family={progress[0]}, officials={progress[1]}", flush=True)
                previous = progress
            if progress[0] >= family_guard_count or progress[1] >= official_count:
                break
            if state["player"]["life"] * 2 < state["player"]["lifeMax"]:
                medicine = select_medicine(state, "life")
                if medicine:
                    client.act("UseItem", generation=state["generation"], slot=medicine["slot"])
            time.sleep(0.5)

    # Officials initially help the player, then become hostile after the
    # family guards and Chen San are defeated. Counts follow the game's files.
    for counter, required, names in (
            ("SGDiaDing", family_guard_count, {"上官府家丁", "陈三"}),
            ("GuanBing", official_count, {"官兵", "枪手1", "枪手2", "枪手3", "枪手4", "枪手5"})):
        while True:
            idle(client)
            state = client.observe(variables)
            count = int(state["variables"].get(counter) or 0)
            if count == required:
                break
            if count > required or state["map"] != "长安.map":
                raise AutomationError(f"Unexpected Shangguan progress: {state['variables']}")
            candidates = [target for target in state["targets"]
                          if target["kind"] == "npc" and target["name"] in names
                          and target.get("hostile") and npc_attackable(target)]
            if not candidates:
                raise AutomationError(f"Missing Shangguan opponents: {counter}={count}/{required}")
            position = state["player"]["position"]
            target = min(candidates, key=lambda target:
                         abs(target["position"]["x"] - position["x"]) * 2
                         + abs(target["position"]["y"] - position["y"]))
            try:
                fight(client, target, skills=(0,) if target["name"] == "陈三" else None)
            except AutomationError as error:
                if str(error) not in ("StartCombat: invalid_enemy", "StartCombat: stale_target",
                                      "StartCombat: target_unavailable"):
                    raise
                after = client.observe(variables)
                after_count = int(after["variables"].get(counter) or 0)
                if (after["generation"] != state["generation"] or after["map"] != state["map"]
                        or after["variables"].get("CAFight") not in ("1", "2")
                        or not count < after_count <= required):
                    raise
                print(f"Shangguan NPC battle advanced {counter}: {count} -> {after_count}; "
                      "observe the remaining opponents", flush=True)
    state = verify(client, SGDiaDing=family_guard_count, GuanBing=official_count, CAFight=2)
    state = client.observe(("ShangGuan",))
    if state["variables"].get("ShangGuan") == "1":
        quantity = sum(entry["quantity"] for entry in state["inventory"])
        talk(client, "上官豹")
        state = verify(client, ShangGuan=2)
        if sum(entry["quantity"] for entry in state["inventory"]) != quantity + 1:
            raise AutomationError("Shangguan's grade-four item reward was not received")
    verify(client, ShangGuan=2)
    # This trap refuses exit while CAFight==1. Complete its normal cleanup and
    # stand outside before the main route continues towards Bieli village.
    go(client, 146, 215, script="长安/maptrap7.txt", combat=False)
    state = go(client, 144, 220, combat=False)
    if state["map"] != "长安.map":
        raise AutomationError("Unexpected map after leaving the Shangguan courtyard")
    checkpoint(client, output, "side-changan-complete")
    return state


def bieli_sidequests(client, output, *, need_full_life=False,
                     dingshen_magic_file="player-magic-定身法.ini", scroll_reward_variable=None):
    """Judge the case, escort the child, and open the key's three forest chests."""
    from run_jxqy2_mainline import checkpoint, go, idle, item, talk, verify
    from jxqy2_mainline_supplies import MEDICINES, meditate, restock

    def prepare_forest():
        state = idle(client)
        if need_full_life and state["player"]["life"] < state["player"]["lifeMax"]:
            if state["player"]["money"] < 40:
                raise AutomationError("Bieli inn requires 40 money before entering its lodging script")
            for point in ((60, 60), (55, 71), (49, 78), (39, 78), (29, 78),
                          (19, 78), (16, 64), (14, 62)):
                go(client, *point, combat=False)
            before = idle(client)
            # The original script has no affordability check; Player clamps
            # a negative balance to zero, so guard before normal interaction.
            if before["player"]["money"] < 40:
                raise AutomationError("Bieli inn money changed before lodging")
            talk(client, "老板", (14, 61))
            state = idle(client)
            player = state["player"]
            if (state["map"] != "别离村.map" or player["money"] != before["player"]["money"] - 40
                    or player["life"] != player["lifeMax"] or player["mana"] != player["manaMax"]
                    or player["thew"] < before["player"]["thew"] or player["thew"] <= 0):
                raise AutomationError("Bieli lodging did not deduct 40 and restore life/mana/thew")
            print(json.dumps(dict(event="sidequest.bieli.lodging", before=before["player"],
                                  after=player), ensure_ascii=False), flush=True)
            for point in ((13, 75), (18, 80), (27, 82), (36, 84), (46, 84), (51, 91)):
                go(client, *point, combat=False)
        else:
            go(client, 60, 60, combat=False)
            phase = int(client.observe(("BieLiMG",))["variables"].get("BieLiMG") or 0)
            checkpoint(client, output, f"side-bieli-meditation-{phase}-before")
            meditate(client)
            checkpoint(client, output, f"side-bieli-meditation-{phase}-after")
            for point in ((55, 71), (49, 78), (46, 84), (51, 91)):
                go(client, *point, combat=False)
        state = restock(client, life_count=2, mana_count=5, reserve_money=100, max_spend=700)
        stocks = [sum(entry["quantity"] for entry in state["inventory"]
                      if entry["file"] in MEDICINES and MEDICINES[entry["file"]][index] > 0)
                  for index in (0, 1)]
        print(json.dumps(dict(event="sidequest.bieli.supplies", lifeStock=stocks[0],
                              manaStock=stocks[1], lifeGoal=2, manaGoal=5,
                              budgetLimited=stocks[0] < 2 or stocks[1] < 5,
                              reserveMoney=100, money=state["player"]["money"]),
                         ensure_ascii=False), flush=True)
        for point in ((48, 78), (50, 62), (53, 48), (59, 42)):
            go(client, *point, combat=False)

    state = idle(client)
    if state["map"] != "别离村.map":
        raise AutomationError("Bieli side quests require normal arrival in the village")
    state = client.observe(("BieLiShengAn", "BieLiMG", "CuiYanMen2Finish"))
    learned = any(entry["file"] == dingshen_magic_file for entry in state["magic"])
    if state["variables"].get("CuiYanMen2Finish") == "1" and not learned:
        raise AutomationError("The forest is temporarily closed until the Tang Ying escort finishes")

    if int(state["variables"].get("BieLiShengAn") or 0) == 0:
        money = state["player"]["money"]
        for point in ((28, 54), (40, 54), (51, 55), (63, 55)):
            go(client, *point, combat=False)
        go(client, 73, 56, script="别离村/审案.txt", combat=False)
        state = verify(client, BieLiShengAn=1)
        if state["player"]["money"] != money + 200:
            raise AutomationError("Bieli court reward must be exactly 200 money")
        checkpoint(client, output, "side-bieli-court")

    state = client.observe(("BieLiMG",))
    phase = int(state["variables"].get("BieLiMG") or 0)
    if phase not in (0, 2):
        raise AutomationError(f"Load save slot 2 before restarting the unfinished child escort: {phase}")
    if learned:
        verify(client, BieLiShengAn=1, BieLiMG=2)
        return checkpoint(client, output, "side-bieli-complete")

    child_path = ((5, 123), (5, 93), (6, 64), (6, 35), (15, 22),
                  (30, 22), (45, 22), (59, 21), (71, 14), (77, 12))
    if phase == 0:
        prepare_forest()
        client.save_or_load(2)
        checkpoint(client, output, "side-bieli-before-child-save-2")
        print("Bieli child escort: saved slot 2; if interrupted, load slot 2 before restarting this chapter", flush=True)
        go(client, 57, 42, destination="别离村迷宫.map",
           script="别离村/trap4别离村至迷宫.txt", combat=False)
        for point in child_path:
            go(client, *point)
        talk(client, "男童-1", (77, 11))
        verify(client, BieLiMG=1)
        for point in reversed(child_path[:-1]):
            go(client, *point)
        go(client, 5, 153)
        go(client, 1, 151, destination="别离村.map",
           script="别离村迷宫/trap1至别离村.txt", combat=False)
        state = verify(client, BieLiMG=2)
        if item(state, "goods-sj-9-别离村钥匙.ini")["quantity"] < 1:
            raise AutomationError("The rescued child's key was not received")
        checkpoint(client, output, "side-bieli-child-rescued")

    item(client.observe(), "goods-sj-9-别离村钥匙.ini")
    prepare_forest()
    client.save_or_load(2)
    checkpoint(client, output, "side-bieli-before-chests-save-2")
    print("Bieli forest chests: saved slot 2; if interrupted, load slot 2 before restarting this chapter", flush=True)
    go(client, 57, 42, destination="别离村迷宫.map",
       script="别离村/trap4别离村至迷宫.txt", combat=False)
    chest_path = ((12, 136), (20, 122), (32, 117), (46, 115), (55, 111),
                  (57, 107), (62, 87), (64, 60), (55, 47))
    for point in chest_path:
        go(client, *point)
    # Collect the first two rewards before learning the spell.
    for position, approach in (((53, 47), (52, 47)), ((54, 45), (54, 46)),
                               ((55, 43), (55, 44))):
        state = go(client, *approach)
        matches = [target for target in state["targets"]
                   if target["kind"] == "object" and target["name"] == "宝箱"
                   and target["position"] == dict(x=position[0], y=position[1])]
        if len(matches) != 1:
            raise AutomationError(f"Expected the forest chest at {position}: {matches}")
        quantity = sum(entry["quantity"] for entry in state["inventory"])
        client.act("Interact", generation=state["generation"], targetId=matches[0]["id"],
                   running=False, timeoutMs=60000, timeout=65)
        state = idle(client)
        if position == (54, 45) and scroll_reward_variable:
            verify(client, **{scroll_reward_variable: 1})
            if sum(entry["quantity"] for entry in state["inventory"]) != quantity:
                raise AutomationError("The forest scroll dialogue unexpectedly changed goods")
        elif position != (55, 43) and sum(entry["quantity"] for entry in state["inventory"]) != quantity + 1:
            raise AutomationError(f"Forest chest reward was not received: {position}")
    state = verify(client, BieLiMG=2, BieLiShengAn=1)
    if item(state, dingshen_magic_file, "magic")["level"] < 1:
        raise AutomationError("Dingshen spell was not learned")
    checkpoint(client, output, "side-bieli-dingshen-learned")
    for point in reversed(chest_path):
        go(client, *point)
    go(client, 5, 153)
    go(client, 1, 151, destination="别离村.map",
       script="别离村迷宫/trap1至别离村.txt", combat=False)
    verify(client, BieLiMG=2, BieLiShengAn=1)
    return checkpoint(client, output, "side-bieli-complete")


def longmen_sidequests(client, output):
    """Accept and finish the five Zangmagang leaders before leaving Longmen."""
    from run_jxqy2_mainline import checkpoint, fight, go, idle, talk, verify

    required = {"1ZMG": 1, "2ZMG": 2, "3ZMG": 3, "4ZMG": 4, "5ZMG": 5}
    variables = (*required, "GotoZMG", "LMKZOZhanMaGang", "LMKZOQieHuan")
    state = idle(client)
    if state["map"] != "龙门客栈.map":
        raise AutomationError("Zangmagang requires the early Longmen visit")
    state = verify(client, LMKZOQieHuan=1)
    state = client.observe(variables)
    if (all(state["variables"].get(key) == str(value) for key, value in required.items())
            and state["variables"].get("LMKZOZhanMaGang") == "0"):
        return checkpoint(client, output, "side-longmen-complete")
    if int(state["variables"].get("GotoZMG") or 0) == 0:
        money = state["player"]["money"]
        if money < 100:
            raise AutomationError("The Longmen guide requires 100 money")
        talk(client, "无赖1", (44, 57))
        state = verify(client, GotoZMG=1, LMKZOZhanMaGang=1)
        if state["player"]["money"] != money - 100:
            raise AutomationError("The Longmen guide fee was not exactly 100 money")
    verify(client, GotoZMG=1, LMKZOZhanMaGang=1)
    checkpoint(client, output, "side-longmen-accepted")
    for point in ((37, 65), (31, 56), (25, 66), (24, 95)):
        go(client, *point, combat=False)
    go(client, 16, 102, destination="葬马岗.map",
       script="龙门客栈/地图切换4.txt", combat=False)
    go(client, 69, 44, combat=False)
    go(client, 60, 44, script="葬马岗/地图陷阱2.txt", combat=False)

    # The chest's southern neighbor (10,64) is blocked terrain; approach from
    # (10,65), following the connected northern corridor.
    for point in ((58, 68), (49, 74), (40, 81), (28, 81), (16, 81), (11, 67), (10, 65)):
        go(client, *point)
    state = idle(client)
    chests = [target for target in state["targets"]
              if target["kind"] == "object" and target["name"] == "宝箱"
              and target["position"] == dict(x=10, y=63)]
    if len(chests) != 1:
        raise AutomationError(f"Expected the Zangmagang grade-four chest: {chests}")
    quantity = sum(entry["quantity"] for entry in state["inventory"])
    client.act("Interact", generation=state["generation"], targetId=chests[0]["id"],
               running=False, timeoutMs=60000, timeout=65)
    state = idle(client)
    if sum(entry["quantity"] for entry in state["inventory"]) != quantity + 1:
        raise AutomationError("The Zangmagang grade-four chest reward was not received")
    checkpoint(client, output, "side-longmen-chest")

    # Approach each connected room before targeting its unique death-script
    # owner. A leader already defeated while clearing the route is verified by
    # its own variable; ordinary enemies do not satisfy this quest.
    for name, counter, points in (
            ("趟子手", "1ZMG", ((15, 79), (17, 99), (29, 99), (36, 99))),
            ("高级拳师", "5ZMG", ((36, 123), (29, 133), (26, 150), (20, 159), (20, 162))),
            ("刀客掌门", "2ZMG", ((22, 146), (26, 130), (34, 123), (46, 122),
                                  (58, 122), (68, 126), (72, 130))),
            ("剑客掌门1", "3ZMG", ((68, 116), (77, 122))),
            ("刀客掌门1", "4ZMG", ((72, 107), (81, 108), (83, 108)))):
        for point in points:
            go(client, *point)
        state = client.observe((counter,))
        if state["variables"].get(counter) != str(required[counter]):
            matches = [target for target in state["targets"]
                       if target["kind"] == "npc" and target["name"] == name
                       and target.get("hostile") and npc_attackable(target)]
            if len(matches) != 1:
                raise AutomationError(f"Expected the remaining Zangmagang leader {name}: {matches}")
            fight(client, matches[0], skills=(0,))
        verify(client, **{counter: required[counter]})
    verify(client, **required)
    checkpoint(client, output, "side-longmen-five-leaders")
    for point in ((74, 106), (62, 106), (53, 99), (52, 78), (51, 56), (57, 45),
                  (60, 26), (63, 8)):
        go(client, *point)
    go(client, 64, 5, destination="龙门客栈.map", script="葬马岗/地图切换1.txt", combat=False)
    for point in ((24, 95), (25, 66), (31, 56), (37, 65), (46, 55)):
        go(client, *point, combat=False)
    talk(client, "无赖1", (44, 57))
    state = idle(client)
    if any(target["name"] == "无赖1" for target in state["targets"]):
        raise AutomationError("The Longmen guide did not accept all five leader results")
    talk(client, "燕若雪", (46, 54))
    verify(client, **required, GotoZMG=1, LMKZOZhanMaGang=0, LMKZOQieHuan=1)
    return checkpoint(client, output, "side-longmen-complete")
