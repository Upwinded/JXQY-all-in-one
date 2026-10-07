"""Test Chenghe through the existing player controls and isolated normal saves."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid

from gameplay_automation import Client, AutomationError, npc_attackable
from run_jxqy2_gameplay_smoke import HOME_VARIABLES, LIFE_ITEM, MANA_ITEM, item, settle
from run_jxqy2_mainline import CHAPTERS, STORY_VARIABLES, fight, go, idle, talk, verify
from run_yycs_gameplay import inventory, load_checkpoint, reachable_trap, trap_points

RESOURCE_ID = "JIAN_ER_GAI_CHENGHE_1_041"
RESOURCE_DIRECTORY = "剑二改承合版"
VARIABLES = (*HOME_VARIABLES, *STORY_VARIABLES, "ZhenJieJu", "Zhenbeiju", "YiFengTalk", "YiFengOn")
VARIABLES += ("ZMG", "LMKZOZhanMaGang")
VARIABLES += ("ChangAnZhiFu", "SGDiaDing", "GuanBing", "CAFight", "GouGuan", "ShangGuan",
              "FengXueShanZhuanFinish", "XiYuanZhao", "WuDaoDeJing", "BieLiMG", "BieLiShengAn",
              "CuiYanMen2Finish")
VARIABLES += ("TanMenFighting", "NaDaoMiJiMusic", "CuiYanMen2AllFinish", "CuiYanMen2Fighting",
              "CuiYanMen2DiZi", "CuiYanMen2QiuYiShui", "HanYanFirstEnter", "HanYanCTMSZL",
              "HanYanCTMHouMenDiZi", "HanYanTWBDiZi", "TianWangPiEr", "TianWanShouMenDiZi",
              "DuanHuanShan", "DuanJiaZhuangClose", "HanYanBXBMTW", "XiaoYaoDie", "XiaoYaoDao")
VARIABLES += ("JiBaiMTW", "Jian", "JianShui", "KuangShan", "KuangGongZouLe", "ZDBiWu", "OuYangMusic",
              "1KuangShan", "1JiangLing", "2JiangLing", "3JiangLing", "4JiangLing", "DaoXiangFight")
VARIABLES += ("OuYangDie", "KuiHua", "SongQian", "linjian", "FCSFight", "ShenShao")
VARIABLES += ("FengChiFight", "FCKillBig", "ADie", "SQFDie", "FengChiKill", "JZZJ", "LinAnDie",
              "ShaoJiFeng", "Zhao", "Cai", "Tang", "Qiu", "TQ")


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def save_hashes(path):
    return {file.relative_to(path).as_posix(): hashlib.sha256(file.read_bytes()).hexdigest()
            for file in sorted(path.rglob("*")) if file.is_file()}


def checkpoint(client, output, name):
    state = client.observe(VARIABLES)
    if state.get("resourceId") != RESOURCE_ID or not state.get("outputHealthy"):
        raise AutomationError("Wrong game or missing required trace")
    write_json(output / f"{name}.json", state)
    shutil.copyfile(client.snapshot(), output / f"{name}.png")
    return state


def forge_materials(client, output):
    from jxqy2_mainline_supplies import meditate, restock
    from run_jxqy2_mainline import zhongdu_return_to_linan
    state = verify(client, Jian=1, ZhenJieJu=2, ZhongDuHouHuaYuan=5)
    if state["map"] != "中都.map":
        raise AutomationError("Forge materials require Zhongdu before the farewell closes Hanyang's south gate")
    for filename in ("无名之剑.ini", "goods-yaowu-8-皇家密药.ini"):
        item(state, filename)
    client.save_or_load(0)
    for point in ((70, 334), (97, 223), (168, 127)):
        go(client, *point, combat=False)
    go(client, 167, 118, combat=False)
    before = idle(client)
    talk(client, "掌柜1", (167, 119))
    state = verify(client, Jian=1, JianShui=0, ZhenJieJu=2)
    if state["inventory"] != before["inventory"] or state["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Incomplete forge requirements consumed money or goods")
    checkpoint(client, output, "forge-incomplete-materials-refused")
    for _ in range(16):
        state = idle(client)
        if state["player"]["life"] >= state["player"]["lifeMax"] - 100:
            break
        healing = item(state, "0player-magic-白虹贯日.ini", "magic")
        life = state["player"]["life"]
        client.act("CastSkill", generation=state["generation"],
                   slot=healing["slot"] - state["layout"]["magicQuickBegin"])
        client.wait_until(lambda value: value["player"]["life"] > life,
                          timeout=5, description="normal forge Baihong recovery")
    else:
        raise AutomationError("Forge Baihong recovery exceeded sixteen normal casts")
    meditate(client)
    go(client, 97, 223, combat=False)
    restock(client, life_count=12, mana_count=12, max_spend=8000)
    zhongdu_return_to_linan(client, output, magic_file_prefix="0player-magic-")
    forge_material_purchases(client, output)


def forge_material_purchases(client, output):
    from run_jxqy2_mainline import late_target, late_leave_linan, daoxiang_to_linan, linan_approach
    state = verify(client, Jian=1, ZhenJieJu=2, ZhongDuHouHuaYuan=5)
    jade = "goods-weijin-9-千年古玉.ini"
    has_jade = any(row["file"] == jade and row["quantity"] for row in state["inventory"])
    if state["map"] == "临安城.map" and not has_jade:
        linan_approach(client, 60, 310, combat=False)
        late_leave_linan(client)
    elif state["map"] not in ("临安城.map", "稻香村.map"):
        raise AutomationError("Forge purchases require normal arrival in Linan or Daoxiang")
    purchases_path = output / "forge-material-purchases.json"
    purchases = json.loads(purchases_path.read_text(encoding="utf-8")) if purchases_path.exists() else []
    for filename, price in ((jade, 2000), ("goods-weijin-11-碧水珠.ini", 3000)):
        if any(row["file"] == filename and row["quantity"] for row in client.observe()["inventory"]):
            if filename == jade and idle(client)["map"] == "稻香村.map":
                daoxiang_to_linan(client, output, magic_file_prefix="0player-magic-")
                linan_approach(client, 60, 310, combat=False)
                go(client, 100, 302, combat=False)
            continue
        position = (133, 244) if idle(client)["map"] == "临安城.map" else None
        target = late_target(client, "杂货摊贩", position)
        go(client, target["position"]["x"], target["position"]["y"] + 2, combat=False, running=True)
        target = late_target(client, "杂货摊贩", position)
        client.interact(target["id"])
        state = client.wait_until(lambda value: "shop" in value, description="forge material merchant")
        offered = item(state, filename, "shop")
        quantity = sum(row["quantity"] for row in state["inventory"] if row["file"] == filename)
        money = state["player"]["money"]
        client.buy(offered["slot"])
        state = client.observe()
        if item(state, filename)["quantity"] != quantity + 1 or state["player"]["money"] != money - price:
            raise AutomationError("Forge material purchase differs from its normal quantity and price")
        purchases.append(dict(file=filename, quantityBefore=quantity, quantityAfter=quantity + 1,
                              moneyBefore=money, moneyAfter=state["player"]["money"], price=price))
        write_json(purchases_path, purchases)
        client.ui("Cancel")
        idle(client)
        if filename == "goods-weijin-9-千年古玉.ini":
            client.save_or_load(1)
            checkpoint(client, output, "forge-jade-daoxiang-normal-slot1")
            daoxiang_to_linan(client, output, magic_file_prefix="0player-magic-")
            linan_approach(client, 60, 310, combat=False)
            go(client, 100, 302, combat=False)
    client.save_or_load(6)
    checkpoint(client, output, "forge-materials-linan-normal-slot6")


def source_candidates(assets):
    roots = (assets / RESOURCE_DIRECTORY, assets / "jxqy2")
    catalog = dict(resourceId=RESOURCE_ID, inventoryType="source-candidates", fullCoverage=False,
                   sources=[], choices=[], conditions=[], terminals=[])
    seen = set()
    for root in roots:
        candidates = inventory(root)
        included = {source["path"] for source in candidates["sources"]} - seen
        for collection in ("sources", "choices", "conditions", "terminals"):
            catalog[collection].extend(dict(site, sourceRoot=str(root.resolve()))
                                       for site in candidates[collection] if site["path"] in included)
        seen.update(included)
    catalog["counts"] = dict(scripts=len(catalog["sources"]), choiceSites=len(catalog["choices"]),
                           conditionalSites=len(catalog["conditions"]), mediaAndReturnSites=len(catalog["terminals"]))
    return catalog


def meng_branch(client, output, *, lose=False):
    from run_jxqy2_mainline import meng_sparring, _move_script_evidence
    state = verify(client, HanYanCTMSZL=3, DuanHuanShan=0, JiBaiMTW=0)
    if state["map"] != "汉阳.map":
        raise AutomationError("Meng branches require a normal source before the first challenge")
    client.save_or_load(4)
    checkpoint(client, output, "meng-branch-source-slot4")
    if lose:
        evidence = _move_script_evidence(client, "汉阳小屋/主角死亡.txt")
        next(evidence)
        try:
            talk(client, "孟廷威", (66, 130))
            state = verify(client, HanYanBXBMTW=1, JiBaiMTW=0)
            if state["map"] != "汉阳小屋.map":
                raise AutomationError("Meng loss challenge did not enter the normal room")
            # Remain within the opponent's ordinary attack range without attacking.
            client.submit("MoveTo", generation=state["generation"], x=12, y=24, timeoutMs=30000)
            client.wait_until(lambda value: value.get("map") == "汉阳.map" and value.get("worldInput"), timeout=600)
            deadline = time.monotonic() + 3
            while not next(evidence)[1]:
                if time.monotonic() >= deadline:
                    raise AutomationError("Meng loss has no completed native player-death script")
                time.sleep(0.05)
        finally:
            evidence.close()
        verify(client, JiBaiMTW=0)
        checkpoint(client, output, "meng-normal-loss-return")
    else:
        from jxqy2_mainline_supplies import meditate, restock
        for filename in ("goods-cloth-4-丝衣.ini", "goods-toukui-6-貂皮帽.ini", "goods-dao-3-香红刀.ini"):
            state = client.observe()
            if any(entry["file"] == filename for entry in state["inventory"]):
                client.equip(item(state, filename)["slot"])
        for count in range(1, 12):
            for _ in range(12):
                state = idle(client)
                if state["player"]["life"] == state["player"]["lifeMax"]:
                    break
                healing = item(state, "0player-magic-白虹贯日.ini", "magic")
                before = state["player"]["life"]
                client.act("CastSkill", generation=state["generation"],
                           slot=healing["slot"] - state["layout"]["magicQuickBegin"])
                if idle(client)["player"]["life"] <= before:
                    raise AutomationError("Normal Baihong did not heal before Meng sparring")
            else:
                raise AutomationError("Meng preparation did not reach full life within twelve casts")
            meditate(client)
            restock(client, life_count=5, mana_count=4)
            checkpoint(client, output, f"meng-win-{count}-normal-preparation")
            meng_sparring(client, output, magic_file_prefix="0player-magic-")
            state = verify(client, JiBaiMTW=count)
            reward = [entry for entry in state["magic"] if entry["file"] == "player-magic-断金斧.ini"]
            if len(reward) != (1 if count >= 10 else 0):
                raise AutomationError("Meng's tenth victory reward differs from its native counter")
            checkpoint(client, output, f"meng-win-{count}-reward-checked")
    client.save_or_load(6)
    checkpoint(client, output, "meng-branch-complete-slot6")


def linan_chai_fusion(client, output):
    state = client.observe(("LinAnChaiSong", "FromFengChi", "KuiHua"))
    if state["map"] != "临安城.map" or state["variables"].get("FromFengChi") not in (None, "", "0"):
        raise AutomationError("Chai fusion requires the normal first Linan visit")
    if state["variables"].get("LinAnChaiSong") in (None, "", "0"):
        talk(client, "婕儿")
        go(client, 142, 108, script="临安城/trap4.txt")
        verify(client, LinAnYanRuoXue=1)
        talk(client, "宝箱", (147, 76))
        talk(client, "柴嵩")
    state = verify(client, LinAnChaiSong=3)
    count = int(client.observe(("KuiHua",))["variables"].get("KuiHua") or 0)
    if count not in (0, 2) or state["player"]["money"] <= 100:
        raise AutomationError("Fusion requires a normal rich source before lecture one or three")
    previous = ("0player-magic-定身法.ini", "0player-magic-怒雷指.ini")
    for filename in previous:
        item(state, filename, "magic")
    for expected in range(count + 1, 4):
        if expected == 3:
            client.save_or_load(6)
            checkpoint(client, output, "chai-fusion-before-third-lecture-slot6")
        talk(client, "柴嵩")
        state = verify(client, KuiHua=expected)
        checkpoint(client, output, f"chai-fusion-lecture-{expected}")
    item(state, "player-magic-葵花点穴手.ini", "magic")
    remaining = [entry["file"] for entry in state["magic"] if entry["file"] in previous]
    if remaining:
        raise AutomationError(f"Chai fusion retained its two component skills: {remaining}")
    talk(client, "柴嵩")
    state = verify(client, KuiHua=4)
    if sum(entry["file"] == "player-magic-葵花点穴手.ini" for entry in state["magic"]) != 1:
        raise AutomationError("Chai fourth lecture duplicated the fusion reward")
    client.save_or_load(6)
    checkpoint(client, output, "chai-fusion-complete-slot6")


def camp_generals(client, output):
    from jxqy2_mainline_supplies import MEDICINES, plan_purchases
    state = verify(client, XiaoYaoDie=0, XiaoYaoDao=0)
    if state["map"] != "金兵营寨.map":
        raise AutomationError("Camp quest requires the fresh normal camp-entry source")
    generals = [target for target in state["targets"] if target["kind"] == "npc"
                and target["name"] == "金国将领" and target["life"] == 5000]
    if len(generals) != 2:
        raise AutomationError("Camp entry must contain both quest generals")
    general_ids = {target["id"] for target in generals}
    if "shop" not in state:
        for filename in ("goods-toukui-10-铁盔.ini", "goods-pifeng-4-云雨披风.ini", "goods-xie-5-鹿皮靴.ini"):
            state = idle(client)
            entry = next((entry for entry in state["inventory"] if entry["file"] == filename), None)
            if entry:
                client.equip(entry["slot"])
        state = idle(client)
    if "shop" not in state:
        # This normal item opens a modal shop; its script ends after Cancel.
        client.submit("UseItem", generation=state["generation"], slot=item(state, "心愿之印.ini")["slot"])
    state = client.wait_until(lambda value: "shop" in value, timeout=30, description="Chenghe portable shop")
    checkpoint(client, output, "camp-portable-shop")
    plan = plan_purchases(state, ("goods-yaowu-7-回天丹.ini", "goods-yaowu-1-凝神丹.ini"),
                          life_count=6, mana_count=6, max_spend=3500)
    purchases = []
    for filename in plan:
        before = client.observe()
        offered = [entry for entry in before["shop"] if entry["file"] == filename and entry["quantity"] > 0]
        if len(offered) != 1:
            raise AutomationError("Portable shop stock changed before purchase")
        quantity = sum(entry["quantity"] for entry in before["inventory"] if entry["file"] == filename)
        client.buy(offered[0]["slot"])
        state = client.observe()
        after_quantity = sum(entry["quantity"] for entry in state["inventory"] if entry["file"] == filename)
        if after_quantity != quantity + 1 or state["player"]["money"] != before["player"]["money"] - MEDICINES[filename][2]:
            raise AutomationError("Portable shop purchase differs from quantity/price")
        purchases.append(dict(file=filename, quantityBefore=quantity, quantityAfter=after_quantity,
                              moneyBefore=before["player"]["money"], moneyAfter=state["player"]["money"]))
    write_json(output / "camp-portable-purchases.json", purchases)
    client.ui("Cancel")
    idle(client)
    def prepare():
        for _ in range(12):
            state = idle(client)
            if state["player"]["life"] >= state["player"]["lifeMax"] - 100:
                return state
            healing = item(state, "0player-magic-白虹贯日.ini", "magic")
            before = state["player"]["life"]
            client.act("CastSkill", generation=state["generation"],
                       slot=healing["slot"] - state["layout"]["magicQuickBegin"])
            if idle(client)["player"]["life"] <= before:
                raise AutomationError("Camp Baihong recovery made no progress under normal enemy attacks")
        raise AutomationError("Camp recovery did not settle within twelve casts")
    def check_reward():
        state = client.observe(("XiaoYaoDie", "XiaoYaoDao"))
        count = int(state["variables"].get("XiaoYaoDie") or 0)
        reward = [entry for entry in state["magic"] if entry["file"] == "player-magic-逍遥刀法.ini"]
        if count not in (0, 1, 2) or len(reward) != (1 if count == 2 else 0):
            raise AutomationError("Camp reward disagrees with the normal general-death counter")
        if count:
            checkpoint(client, output, f"camp-general-{count}-reward-checked")
        return state
    prepare()
    client.save_or_load(4)
    checkpoint(client, output, "camp-normal-preparation-slot4")
    for point in ((24, 150), (30, 135), (36, 125), (43, 113)):
        go(client, *point)
        check_reward()
        prepare()
    for identity in sorted(general_ids):
        state = idle(client)
        target = next((entry for entry in state["targets"] if entry["id"] == identity and npc_attackable(entry)), None)
        if target:
            prepare()
            fight(client, target, skills=(0,))
            check_reward()
    verify(client, XiaoYaoDie=2, XiaoYaoDao=1)
    client.save_or_load(5)
    checkpoint(client, output, "camp-generals-complete-slot5")


def fengchi_shao_loss(client, output):
    from run_jxqy2_mainline import _move_script_evidence, late_return_to_linan
    state = verify(client, FromFengChi=9, FCSFight=0, ShenShao=0)
    if state["map"] != "凤池山庄.map":
        raise AutomationError("Shao loss requires the normal source before the added duel")
    before = checkpoint(client, output, "shao-loss-before")
    evidence = _move_script_evidence(client, "凤池山庄/主角死亡.txt")
    next(evidence)
    try:
        go(client, 74, 197, script="凤池山庄/datingtalk.txt")
        state = verify(client, FCSFight=1)
        if not state["player"]["canFight"]:
            raise AutomationError("The added Shao duel still prevents ordinary combat")
        checkpoint(client, output, "shao-loss-combat-permitted")
        # Remain beside the opponent and allow his normal attacks to win.
        client.submit("MoveTo", generation=state["generation"], x=79, y=196, timeoutMs=30000)
        deadline = time.monotonic() + 600
        while time.monotonic() < deadline:
            state = client.observe(("LinAnYanRuoXue", "ShenShao", "ZhenJieJu"))
            if state.get("scene") == "Title" or state.get("choices"):
                raise AutomationError("Shao loss did not reach its normal continuation")
            if next(evidence)[1] and state.get("worldInput"):
                break
            time.sleep(0.2)
        else:
            raise TimeoutError("Normal Shao defeat did not complete within ten minutes")
    finally:
        evidence.close()
    if (state["map"] != "凤池山庄.map" or state["player"]["canFight"]
            or state["variables"].get("ShenShao") not in (None, "", "0")
            or state["variables"].get("ZhenJieJu") != before["variables"].get("ZhenJieJu")
            or state["variables"].get("LinAnYanRuoXue") not in ("11", "12")):
        raise AutomationError("Shao defeat did not restore the ordinary continuation or preserve the victory reward")
    client.save_or_load(5)
    checkpoint(client, output, "shao-loss-complete-slot5")
    go(client, 81, 196, combat=False)
    checkpoint(client, output, "shao-loss-normal-movement")
    late_return_to_linan(client)
    checkpoint(client, output, "shao-loss-normal-linan-return")


def tournament_loss(client, output):
    from run_jxqy2_mainline import _move_script_evidence, late_records, late_completed_script
    state = client.observe(("FCBW", "HappyEnding", "Zhenbeiju"))
    stage = int(state["variables"].get("FCBW") or 0)
    if state["map"] != "凤池山庄-比武场.map" or not 1 <= stage <= 11:
        raise AutomationError("Tournament loss requires a normal source before an active round")
    target = next(actor for actor in state["targets"] if actor.get("hostile") and npc_attackable(actor))
    checkpoint(client, output, f"tournament-loss-round-{stage}-before")
    before = int(state["variables"].get("Zhenbeiju") or 0)
    # Take off owned equipment through its normal menu so weak opponents can win.
    worn = [entry for entry in state["inventory"] if entry["slot"] >= state["layout"]["equipmentBegin"]]
    client.open_menu("Equip")
    for entry in worn:
        client.focus_slot("equipment-item-", entry["slot"])
        client.ui("Confirm")
        after = client.observe()
        if (any(item["slot"] == entry["slot"] for item in after["inventory"])
                or sum(item["quantity"] for item in after["inventory"] if item["file"] == entry["file"])
                != sum(item["quantity"] for item in state["inventory"] if item["file"] == entry["file"])):
            raise AutomationError("Normal equipment removal did not preserve the owned item")
    client.ui("Cancel")
    checkpoint(client, output, f"tournament-loss-round-{stage}-normal-equipment-removal")
    evidence = _move_script_evidence(client, "凤池山庄-比武场/主角死亡.txt")
    next(evidence)
    try:
        client.act("MoveTo", generation=state["generation"],
                   x=target["position"]["x"], y=target["position"]["y"] + 2,
                   running=False, timeoutMs=30000)
        # Later normal sources can outlevel weak opponents; keep observing
        # natural damage rather than changing saved health or character stats.
        deadline = time.monotonic() + 3600
        while time.monotonic() < deadline:
            state = client.observe(("FCBW", "HappyEnding", "Zhenbeiju"))
            if state.get("scene") == "Title" or state.get("choices"):
                raise AutomationError("Tournament loss did not reach its normal continuation")
            if next(evidence)[1] and state.get("worldInput") and state.get("map") == "凤池山庄.map":
                break
            time.sleep(0.2)
        else:
            raise TimeoutError("Tournament defeat did not complete within one hour")
    finally:
        evidence.close()
    if ((state["variables"].get("HappyEnding") or "0") != "0"
            or int(state["variables"].get("Zhenbeiju") or 0) != before + (stage == 11)):
        raise AutomationError("Tournament loss did not preserve its normal ending markers")
    finished = "凤池山庄-比武场/比武结束.txt"
    late_completed_script(late_records(output, "trace.jsonl", completed_scripts=(finished,)), finished)
    write_json(output / "tournament-loss-result.json", dict(stage=stage, happyEnding=0,
               zhenbeiju=int(state["variables"].get("Zhenbeiju") or 0), naturalDefeat=True,
               cheatAssisted=False, fullPlaythrough=False))
    client.save_or_load(4)
    checkpoint(client, output, f"tournament-loss-round-{stage}-complete-slot4")


def fengchi_prison(client, output, *, reverse_boss_order=False):
    from run_jxqy2_mainline import late_records, late_completed_script
    state = idle(client)
    if state["map"] != "临安大牢.map":
        if state["map"] == "凤池山庄.map":
            go(client, 102, 154, destination="凤池山庄夜战.map", timeout=300, combat=False)
        verify(client, FromFengChi=12)
        # Entry itself starts combat. Guards have no death counter in this MOD.
        state = verify(client, FengChiFight=1)
        if any(actor["name"] in ("敖管家", "邵骑风") and actor.get("hostile")
               and npc_attackable(actor) for actor in state["targets"]):
            client.save_or_load(2)
            checkpoint(client, output, "fengchi-night-before-bosses-normal-slot2")
        bosses = ("邵骑风", "敖管家") if reverse_boss_order else ("敖管家", "邵骑风")
        for name in bosses:
            state = idle(client)
            targets = [actor for actor in state["targets"] if actor["name"] == name
                       and actor.get("hostile") and npc_attackable(actor)]
            if len(targets) == 1:
                fight(client, targets[0], skills=(0,))
            elif len(targets) > 1:
                raise AutomationError(f"Ambiguous Fengchi night boss {name}")
        required = ("凤池山庄夜战/aodie.txt", "凤池山庄夜战/邵骑风死.txt")
        trace = late_records(output, "trace.jsonl", completed_scripts=required)
        for script in required:
            late_completed_script(trace, script)
        checkpoint(client, output, "fengchi-night-both-bosses-complete")
        go(client, 107, 170, destination="临安大牢.map", combat=False)
    if (client.observe(("LinAnDaLao11",))["variables"].get("LinAnDaLao11") or "0") != "1":
        client.save_or_load(0)
        checkpoint(client, output, "prison-before-door-normal-slot0")
    CHAPTERS["fengchi-prison"](client, output, prison_magic_file="江翻海沸.ini")
    client.save_or_load(6)
    checkpoint(client, output, "fengchi-prison-complete-slot6")


def prison_companions_loss(client, output):
    from run_jxqy2_mainline import late_records, late_completed_script, prison_exit
    state = client.observe(VARIABLES)
    if state["map"] != "临安大牢第1层.map" or state["variables"].get("FromFengChi") != "14":
        raise AutomationError("Companion loss requires a normal first-floor source")
    flags = ("Zhao", "Cai", "Tang", "Qiu")
    deaths = lambda snapshot: sum(int(snapshot["variables"].get(name) or 0) for name in flags)
    initial_count = deaths(state)
    before = int(state["variables"].get("Zhenbeiju") or 0)
    if not int(state["variables"].get("ShaoJiFeng") or 0):
        go(client, 15, 151, script="临安大牢第1层/trap-3.txt", combat=False)
    state = client.observe(VARIABLES)
    try:
        client.act("JumpTo", generation=state["generation"], x=19, y=157,
                   running=False, timeoutMs=30000)
    except AutomationError as error:
        if str(error) not in ("JumpTo: no_progress", "JumpTo: blocked_destination"):
            raise
        checkpoint(client, output, "companion-retreat-unfinished")
    deadline, recorded = time.monotonic() + 600, initial_count
    while time.monotonic() < deadline:
        state = client.observe(VARIABLES)
        if state["map"] != "临安大牢第1层.map" or state.get("scene") == "Title":
            raise AutomationError("Companion observation left the normal battle")
        count = deaths(state)
        if count != recorded:
            idle(client)
            slot = {1: 0, 2: 2, 3: 3, 4: 4}[count]
            client.save_or_load(slot)
            checkpoint(client, output, f"prison-companions-{count}-lost-slot{slot}")
            recorded = count
        if count == 4:
            break
        if int(state["variables"].get("ShaoJiFeng") or 0) == 2:
            raise AutomationError(f"Companion battle ended after {count} natural deaths")
        time.sleep(0.2)
    else:
        raise TimeoutError("Four companion deaths did not occur within ten minutes")
    state = client.observe(VARIABLES)
    if (int(state["variables"].get("LinAnDie") or 0) != 4
            or int(state["variables"].get("Zhenbeiju") or 0) != before + 4 - initial_count):
        raise AutomationError("Natural companion deaths did not match their story counters")
    required = tuple("临安大牢第1层/" + name for name in
                     ("赵姐姐死亡.txt", "柴大哥死亡.txt", "唐大哥死亡.txt", "秋姐姐死亡.txt"))
    trace = late_records(output, "trace.jsonl", completed_scripts=required)
    for script in required:
        late_completed_script(trace, script)
    boss = next(actor for actor in state["targets"] if actor["name"] == "邵骑风"
                and actor.get("hostile") and npc_attackable(actor))
    fight(client, boss, skills=(0,))
    verify(client, ShaoJiFeng=2)
    prison_exit(client, output)
    client.save_or_load(6)
    checkpoint(client, output, "prison-all-companions-lost-linan-slot6")


def home_gate(client, output):
    before = checkpoint(client, output, "home-gate-before")
    if before.get("map") != "主角家.map" or any(before["variables"].get(name) not in (None, "", "0")
            for name in ("NanGongCaiHong", "ZhangRuMeng", "HeiSha", "BaiSha")):
        raise AutomationError("Gate refusal requires the normal home source before conversations")
    go(client, 15, 67, script="主角家/trap-3.txt")
    after = checkpoint(client, output, "home-gate-refused")
    if after["map"] != "主角家.map" or after["variables"] != before["variables"]:
        raise AutomationError("The unmet home prerequisites did not preserve the story state")


def opening(client, output):
    state = client.wait_until(lambda value: value["scene"] == "Title")
    if state["resourceId"] != RESOURCE_ID:
        raise AutomationError("The selected resource is not Chenghe")
    checkpoint(client, output, "01-title")
    client.act("SetAutoDialogue", enabled=True)
    client.activate("new-game")
    state = settle(client, map_name="沙漠之战")
    checkpoint(client, output, "02-desert")
    client.submit("MoveTo", generation=state["generation"], x=25, y=35,
                  running=True, timeoutMs=180000)
    settle(client, map_name="主角家", timeout=240)
    checkpoint(client, output, "03-home-introduction")
    # Keep the normal source before completing the departure prerequisites.
    client.save_or_load(0)
    for name in ("张如梦", "南宫彩虹", "南宫彩虹", "白煞", "黑煞"):
        talk(client, name)
    verify(client, NanGongCaiHong=2, ZhangRuMeng=1, HeiSha=1, BaiSha=1)
    for position in ((16, 15), (37, 57), (28, 31)):
        talk(client, "宝箱", position)
    state = client.observe()
    if item(state, LIFE_ITEM)["quantity"] != 5 or item(state, MANA_ITEM)["quantity"] != 5:
        raise AutomationError("Home medicine reward differs from its source")
    for filename in ("goods-jian-1-桃木剑.ini", "goods-pifeng-1-白布披风.ini",
                     "goods-cloth-1-书生服.ini", "goods-huwan-2-月勾.ini"):
        client.equip(item(client.observe(), filename)["slot"])
    home_ready(client, output)


def home_ready(client, output):
    verify(client, NanGongCaiHong=2, ZhangRuMeng=1, HeiSha=1, BaiSha=1)
    client.assign_magic(item(client.observe(), "0player-magic-寒霜掌.ini", "magic")["slot"], 0)
    client.assign_goods(item(client.observe(), LIFE_ITEM)["slot"], 0)
    client.save_or_load(1)
    checkpoint(client, output, "04-home-branch-source-slot1")
    load_checkpoint(client, 1)
    after = checkpoint(client, output, "05-home-reloaded")
    verify(client, NanGongCaiHong=2, ZhangRuMeng=1, HeiSha=1, BaiSha=1)
    if after["map"] != "主角家.map":
        raise AutomationError("Normal home save did not reload")


def yifeng_unlock(client, output):
    state = checkpoint(client, output, "yifeng-before")
    if state["map"] != "主角家.map" or state["variables"].get("YiFengTalk") != "2":
        raise AutomationError("Yifeng requires the normal second-conversation source")
    for count in range(3, 11):
        talk(client, "南宫彩虹")
        verify(client, YiFengTalk=11 if count == 10 else count)
        if count < 10:
            verify(client, YiFengOn=0)
    verify(client, YiFengOn=1)
    client.save_or_load(3)
    checkpoint(client, output, "yifeng-unlocked-slot3")


def departure(client, output, resource, difficulty):
    state = idle(client)
    if state["map"] != "主角家.map":
        raise AutomationError("Departure requires the normal home checkpoint")
    go(client, 15, 67, script="主角家/trap-3.txt")
    if difficulty == "easy":
        for count in range(4):
            go(client, 14, 87, script="主角家/trap-2.txt")
            verify(client, HomeChoice=min(count + 1, 3))
    else:
        state = client.observe()
        point = reachable_trap(resource, state["map"], 1, state["player"]["position"],
                               avoid=trap_points(resource, state["map"], 2))
        go(client, *point, script="主角家/trap-1.txt")
    state = checkpoint(client, output, f"06-departure-{difficulty}")
    if state["map"] != "主角家-狂沙镇.map" or state["player"]["levelFile"] != f"level-{difficulty}.ini":
        raise AutomationError("Native difficulty exit did not apply")
    for filename in ("0player-magic-无影神针.ini", "0player-magic-白虹贯日.ini", "0player-magic-天意剑诀.ini"):
        client.assign_magic(item(client.observe(), filename, "magic")["slot"])
    client.assign_practice(item(client.observe(), "0player-magic-天意剑诀.ini", "magic")["slot"])
    client.save_or_load(2)
    checkpoint(client, output, f"07-{difficulty}-source-slot2")


def wolves(client, output):
    state = idle(client)
    if state["map"] != "主角家-狂沙镇.map":
        raise AutomationError("Wolves require the normal desert village arrival")
    talk(client, "掌柜2")
    verify(client, ksOcunzhang=1)
    talk(client, "掌柜2")
    verify(client, ksOcunzhang=1)
    client.save_or_load(3)
    checkpoint(client, output, "wolves-task-source-slot3")
    go(client, 97, 171)
    state = client.observe(("ksOcunzhang",))
    if state["variables"].get("ksOcunzhang") != "2":
        target = next(actor for actor in state["targets"] if actor["name"] == "火狼" and npc_attackable(actor))
        fight(client, target, skills=[0])
    verify(client, ksOcunzhang=2)
    client.save_or_load(4)
    checkpoint(client, output, "wolves-reward-source-slot4")
    before = client.observe()
    for point in ((102, 126), (130, 100), (133, 91)):
        go(client, *point)
    talk(client, "掌柜2")
    verify(client, ksOcunzhang=3)
    after = client.observe()
    if after["player"]["money"] != before["player"]["money"] + 500:
        raise AutomationError("Village reward did not grant 500 taels")
    client.equip(item(after, "goods-jian-2-桃花剑.ini")["slot"])
    before = client.observe()
    talk(client, "掌柜2")
    after = client.observe()
    if after["player"]["money"] != before["player"]["money"] or after["inventory"] != before["inventory"]:
        raise AutomationError("Village repeated conversation duplicated the reward")
    client.save_or_load(5)
    checkpoint(client, output, "wolves-complete-slot5")


def desert_skill(client, output):
    from jxqy2_sidequests import early_desert_skill
    early_desert_skill(client, output, swordsman_name="剑客B", magic_file="0player-magic-怒雷指.ini")
    client.save_or_load(6)
    checkpoint(client, output, "desert-skill-source-slot6")


def village(client, output):
    verify(client, ksOcunzhang=3)
    item(client.observe(), "0player-magic-怒雷指.ini", "magic")
    go(client, 41, 302, destination="狂沙镇.map")
    go(client, 88, 185, script="狂沙镇/柴嵩交谈.txt")
    verify(client, ChaiSongTalk=1)
    client.save_or_load(0)
    checkpoint(client, output, "kuangsha-inn-source-slot0")
    talk(client, "老板2")
    verify(client, KsOQieHuan=1)
    client.save_or_load(1)
    checkpoint(client, output, "kuangsha-night-source-slot1")


def kuangsha_gates(client, output):
    verify(client, ksOcunzhang=3)
    go(client, 41, 302, destination="狂沙镇.map", combat=False, running=True)
    verify(client, ChaiSongTalk=0, KsOQieHuan=0)
    client.save_or_load(0)
    checkpoint(client, output, "kuangsha-before-inn-slot0")
    for name, point, script in (("west", (147, 258), "地图切换1.txt"),
                                ("east", (129, 57), "地图切换2.txt")):
        go(client, *point, script="狂沙镇/" + script, combat=False, running=True)
        state = checkpoint(client, output, "kuangsha-" + name + "-refused-before-inn")
        verify(client, ChaiSongTalk=0, KsOQieHuan=0)
        if state["map"] != "狂沙镇.map":
            raise AutomationError("Kuangsha departure before the inn was not refused")
    go(client, 88, 185, script="狂沙镇/柴嵩交谈.txt")
    verify(client, ChaiSongTalk=1, KsOQieHuan=0)
    client.save_or_load(1)
    checkpoint(client, output, "kuangsha-after-chai-before-inn-slot1")


def kuangsha_night_gate(client, output):
    verify(client, ChaiSongTalk=1, KsOQieHuan=0)
    talk(client, "老板2")
    verify(client, KsOQieHuan=1)
    client.save_or_load(2)
    checkpoint(client, output, "kuangsha-night-gate-source-slot2")
    go(client, 147, 258, script="狂沙镇夜/地图切换1.txt", combat=False, running=True)
    state = checkpoint(client, output, "kuangsha-west-refused-at-night")
    verify(client, ChaiSongTalk=1, KsOQieHuan=1)
    if state["map"] != "狂沙镇夜.map":
        raise AutomationError("The western exit at night did not refuse departure")


def longmen_arrival(client, output):
    verify(client, KsOQieHuan=3)
    go(client, 147, 258, destination="狂沙镇-龙门客栈.map")
    go(client, 19, 25, script="狂沙镇-龙门客栈/对话.txt")
    go(client, 27, 45, destination="龙门客栈.map")
    go(client, 25, 57, script="龙门客栈/柴嵩.txt")
    verify(client, LMKZOQieHuan=1, LMKZOZhanMaGang=0, ZMG=0)
    client.save_or_load(0)
    checkpoint(client, output, "longmen-arrival-source-slot0")


def kuangsha_return_gates(client, output):
    verify(client, KsOQieHuan=2)
    for name, point, script in (("west", (147, 258), "地图切换1.txt"),
                                ("east", (129, 57), "地图切换2.txt")):
        go(client, *point, script="狂沙镇/" + script, combat=False, running=True)
        state = checkpoint(client, output, "kuangsha-" + name + "-refused-with-yan")
        verify(client, KsOQieHuan=2)
        if state["map"] != "狂沙镇.map":
            raise AutomationError("Kuangsha departure with Yan before lodging was not refused")


def longmen_departure(client, output):
    verify(client, LMKZOQieHuan=1, LMKZOZhanMaGang=0)
    go(client, 54, 98, destination="龙门客栈-长安.map")
    go(client, 8, 33, script="龙门客栈-长安/聊天.txt")
    go(client, 14, 107, destination="长安.map")
    client.save_or_load(0)
    checkpoint(client, output, "changan-arrival-source-slot0")


def fengxue_return_before_rescue(client, output):
    state = verify(client, FengXueShanZhuanFinish=0, ChangAnYanRuoXueXiaoShi=1)
    if state["map"] != "风雪山庄.map":
        raise AutomationError("An early return requires the normal Fengxue arrival")
    go(client, 93, 13, destination="长安西郊.map", script="风雪山庄/地图陷阱1.txt", combat=False)
    verify(client, FengXueShanZhuanFinish=0, ChangAnYanRuoXueXiaoShi=1)
    go(client, 19, 9, destination="长安.map", script="长安西郊/地图切换.txt", combat=False)
    verify(client, ChangAnZhiFu=1, FengXueShanZhuanFinish=0, ChangAnYanRuoXueXiaoShi=1)
    client.save_or_load(3)
    checkpoint(client, output, "fengxue-early-return-slot3")


def changan_gates(client, output, resource):
    before = checkpoint(client, output, "changan-gates-before")
    if before["map"] != "长安.map" or before["variables"].get("FengXueShanZhuanFinish") not in ("", "0"):
        raise AutomationError("Changan gate checks require the normal city before Fengxue rescue")
    if before["player"]["position"] == dict(x=33, y=69):
        for point in ((35, 81), (39, 89), (45, 93), (49, 101)):
            go(client, *point, combat=False, running=True)
        go(client, 49, 117, script="长安/trap-2.txt", combat=False, running=True)
        before = checkpoint(client, output, "changan-gates-after-city-introduction")
    gates = [("south", 4)]
    if before["variables"].get("ChangAnYanRuoXueXiaoShi") in ("", "0"):
        gates.insert(0, ("west", 3))
    for name, trap in gates:
        state = client.observe()
        occupied = {(target["position"]["x"], target["position"]["y"]) for target in state["targets"]}
        path = reachable_trap(resource, state["map"], trap, state["player"]["position"],
                              occupied=occupied, avoid=occupied, with_path=True)
        for point in path[8:-1:8]:
            go(client, *point, combat=False, running=True)
        go(client, *path[-1], script=f"长安/trap-{trap}.txt", combat=False, running=True)
        state = checkpoint(client, output, "changan-" + name + "-refused-before-prerequisite")
        if state["map"] != "长安.map" or state["variables"] != before["variables"]:
            raise AutomationError(f"The {name} gate did not preserve the unmet story state")


def zangmagang_task_gates(client, output, resource):
    verify(client, LMKZOQieHuan=1)
    before = client.observe(("ZMG",))
    progress = int(before["variables"].get("ZMG") or 0)
    if progress == 0:
        if before["player"]["money"] < 100:
            raise AutomationError("The native Longmen guide requires 100 taels")
        talk(client, "无赖1", (44, 57))
        if client.observe()["player"]["money"] != before["player"]["money"] - 100:
            raise AutomationError("Unexpected Longmen guide fee")
    elif progress != 1:
        raise AutomationError("Task gates require the normal pre-combat source")
    verify(client, ZMG=1, LMKZOZhanMaGang=1)
    client.save_or_load(0)
    checkpoint(client, output, "zmg-task-gates-source-slot0")
    zangmagang_progress_return(client, output, None)
    talk(client, "燕若雪", (46, 54))
    verify(client, ZMG=1, LMKZOZhanMaGang=1)
    go(client, 37, 65, script="龙门客栈/地图切换5.txt", combat=False)
    for name, trap, script in (("west", 1, "地图切换.txt"),
                              ("east", 2, "地图切换1.txt")):
        state = client.observe()
        point = reachable_trap(resource, state["map"], trap, state["player"]["position"])
        go(client, *point, script="龙门客栈/" + script, combat=False, running=True)
        state = checkpoint(client, output, "longmen-" + name + "-refused-during-zmg")
        verify(client, ZMG=1, LMKZOZhanMaGang=1)
        if state["map"] != "龙门客栈.map":
            raise AutomationError("An unfinished Zangmagang task did not prevent departure")


def zangmagang_progress_return(client, output, resource):
    before = client.observe(("ZMG", "LMKZOZhanMaGang"))
    progress = int(before["variables"].get("ZMG") or 0)
    if not 1 <= progress <= 7 or before["variables"].get("LMKZOZhanMaGang") != "1":
        raise AutomationError("A progress return requires a normal unfinished-task source")
    if before["map"] == "葬马岗.map":
        occupied = {(target["position"]["x"], target["position"]["y"])
                    for target in before["targets"]}
        path = reachable_trap(resource, before["map"], 1, before["player"]["position"],
                              occupied=occupied, with_path=True)
        for point in path[4:-1:4]:
            go(client, *point, combat=False, running=True)
        go(client, *path[-1], destination="龙门客栈.map", script="葬马岗/地图切换1.txt",
           combat=False, running=True)
        for point in ((24, 95), (25, 66), (31, 56)):
            go(client, *point, combat=False, running=True)
        go(client, 37, 65, combat=False, running=True)
        go(client, 46, 55, script="龙门客栈/地图切换5.txt", combat=False)
    elif before["map"] != "龙门客栈.map":
        raise AutomationError("Unexpected task-return map")
    verify(client, ZMG=progress, LMKZOZhanMaGang=1)
    money = client.observe()["player"]["money"]
    talk(client, "无赖1", (44, 57))
    state = checkpoint(client, output, f"zmg-return-progress-{progress}")
    if state["player"]["money"] != money:
        raise AutomationError("A repeated guide conversation charged another fee")
    verify(client, ZMG=progress)
    guide_present = any(target["name"] == "无赖1" for target in state["targets"])
    if guide_present != (progress < 6):
        raise AutomationError("The guide's acceptance does not match the source counter")
    if progress >= 6:
        talk(client, "燕若雪", (46, 54))
        verify(client, ZMG=progress, LMKZOZhanMaGang=0)
        client.save_or_load(6)
        checkpoint(client, output, f"zmg-return-progress-{progress}-finished-slot6")


def zangmagang(client, output, *, resume_south=False):
    verify(client, LMKZOQieHuan=1)
    before = client.observe(("ZMG", "LMKZOZhanMaGang"))
    progress = int(before["variables"].get("ZMG") or 0)
    if progress == 0:
        verify(client, LMKZOZhanMaGang=0)
        if before["player"]["money"] < 100:
            raise AutomationError("The native Longmen guide requires 100 taels")
        talk(client, "无赖1", (44, 57))
        if client.observe()["player"]["money"] != before["player"]["money"] - 100:
            raise AutomationError("The native Longmen guide did not charge 100 taels")
    elif progress not in ((3, 4) if resume_south else (1,)):
        raise AutomationError("Zangmagang requires a normal arrival or accepted-task source")
    verify(client, ZMG=progress if resume_south else 1, LMKZOZhanMaGang=1)
    state = client.observe()
    if state["map"] not in ("龙门客栈.map", "葬马岗.map"):
        raise AutomationError("Zangmagang requires the normal task maps")
    if resume_south:
        if state["map"] != "葬马岗.map" or not all((output / f"zmg-chest-{x}-{y}.json").exists()
                for x, y in ((10, 63), (11, 62), (11, 61))):
            raise AutomationError("South recovery requires the observed north-chest completion")
        checkpoint(client, output, "zmg-south-recovery-before")
        if progress == 3:
            go(client, 26, 150)
    else:
        client.save_or_load(0)
        checkpoint(client, output, "zmg-accepted-source-slot0")
    recorded = set(range(1, progress + 1)) if resume_south else {1}

    def open_box(position, reward):
        if resume_south and (output / f"zmg-chest-{position[0]}-{position[1]}.json").exists():
            return
        while True:
            state = idle(client)
            point = state["player"]["position"]
            threats = [target for target in state["targets"] if target.get("hostile")
                       and npc_attackable(target) and target.get("visibleFromPlayer", True)
                       and abs(target["position"]["x"] - point["x"]) * 2
                       + abs(target["position"]["y"] - point["y"]) <= 12]
            if not threats:
                break
            fight(client, threats[0])
            record_progress()
        before = client.observe()
        talk(client, "宝箱", position)
        after = client.observe()
        quantity = lambda state: sum(entry["quantity"] for entry in state["inventory"])
        money = after["player"]["money"] - before["player"]["money"]
        if reward == "item":
            valid = quantity(after) == quantity(before) + 1 and money == 0
        elif reward == "small-money":
            valid = before["inventory"] == after["inventory"] and 10 <= money <= 100
        elif reward == "medium-money":
            valid = before["inventory"] == after["inventory"] and 100 <= money <= 1000
        else:
            valid = before["inventory"] == after["inventory"] and money == 0
        if not valid:
            raise AutomationError(f"Unexpected Zangmagang chest reward at {position}: {money}")
        try:
            talk(client, "宝箱", position)
        except AutomationError as error:
            # The reward script clears this object's binding after opening.
            if str(error) != "Interact: action_rejected":
                raise
        repeated = client.observe()
        if repeated["inventory"] != after["inventory"] or repeated["player"]["money"] != after["player"]["money"]:
            raise AutomationError(f"Repeated Zangmagang chest reward at {position}")
        checkpoint(client, output, f"zmg-chest-{position[0]}-{position[1]}")

    def record_progress():
        state = client.observe(("ZMG",))
        progress = int(state["variables"].get("ZMG") or 0)
        if not 1 <= progress <= 7:
            raise AutomationError(f"Unexpected Chenghe leader progress: {progress}")
        if progress not in recorded:
            client.save_or_load(progress - 1)
            checkpoint(client, output, f"zmg-progress-{progress}-slot{progress - 1}")
            recorded.add(progress)
        return state
    if not resume_south and state["map"] == "龙门客栈.map":
        go(client, 37, 65, script="龙门客栈/地图切换5.txt", combat=False)
        for point in ((31, 56), (25, 66), (24, 95)):
            go(client, *point, combat=False)
        go(client, 16, 102, destination="葬马岗.map", script="龙门客栈/地图切换4.txt", combat=False)
        go(client, 69, 44, combat=False)
        go(client, 60, 44, script="葬马岗/地图陷阱2.txt", combat=False)
    if not resume_south:
        for point in ((58, 68), (49, 74), (40, 81), (28, 81), (16, 81), (11, 67), (10, 65)):
            go(client, *point)
            record_progress()
        for position, reward in (((10, 63), "item"), ((11, 62), "item"), ((11, 61), "medium-money")):
            open_box(position, reward)
    for name, points in (
            ("高级蒙面人1", ((10, 65),)),
            ("趟子手", ((15, 79), (17, 99), (29, 99), (36, 99))),
            ("高级拳师", ((36, 123), (29, 133), (26, 150), (20, 159), (20, 162))),
            ("刀客掌门", ((22, 146), (26, 130), (34, 123), (46, 122), (58, 122), (68, 126), (72, 130))),
            ("剑客掌门1", ((68, 116), (77, 122))),
            ("刀客掌门1", ((72, 107), (81, 108), (83, 108)))):
        if resume_south:
            if name in ("高级蒙面人1", "趟子手"):
                continue
            if name == "高级拳师":
                points = () if progress == 4 else ((20, 159), (20, 162))
        for point in points:
            go(client, *point)
            record_progress()
        state = client.observe()
        matches = [target for target in state["targets"] if target["name"] == name
                   and target.get("hostile") and npc_attackable(target)]
        if len(matches) > 1:
            raise AutomationError(f"Ambiguous native Zangmagang leader: {name}")
        if matches:
            fight(client, matches[0], skills=(0,))
            record_progress()
        if name == "高级拳师":
            for position, reward in (((9, 157), "empty"), ((12, 143), "small-money"), ((14, 142), "empty")):
                open_box(position, reward)
    verify(client, ZMG=7)
    checkpoint(client, output, "zmg-six-leaders-complete")
    for point in ((74, 106), (62, 106), (53, 99), (52, 78), (51, 56), (57, 45), (60, 26), (63, 8)):
        go(client, *point)
    go(client, 64, 5, destination="龙门客栈.map", script="葬马岗/地图切换1.txt", combat=False)
    for point in ((24, 95), (25, 66), (31, 56)):
        go(client, *point, combat=False)
    go(client, 37, 65, combat=False)
    go(client, 46, 55, script="龙门客栈/地图切换5.txt", combat=False)
    talk(client, "无赖1", (44, 57))
    if any(target["name"] == "无赖1" for target in client.observe()["targets"]):
        raise AutomationError("The guide did not accept the native six-leader result")
    talk(client, "燕若雪", (46, 54))
    verify(client, ZMG=7, LMKZOZhanMaGang=0)
    client.save_or_load(6)
    checkpoint(client, output, "zmg-return-complete-slot6")


def tianren_true_first_boss(client, output):
    from run_jxqy2_mainline import late_target, late_records, late_completed_script
    state = verify(client, HappyEnding=1, ZhenJieJu=4, TrjDxmg=3)
    if state['map'] != '天忍教-地下迷宫3.map':
        raise AutomationError('True ending requires its normal final-map source')
    go(client, 31, 75, script='天忍教-地下迷宫3/地图陷阱3.txt', combat=False)
    boss = late_target(client, '完颜宏烈', (21, 53))
    checkpoint(client, output, 'true-first-boss-before')
    fight(client, boss, skills=(0,))
    state = idle(client)
    extra = late_target(client, '完颜宏烈')
    if extra['life'] != 150000 or extra['id'] == boss['id']:
        raise AutomationError('First victory did not start the distinct true-ending boss')
    trace = late_records(output, 'trace.jsonl', completed_scripts=('天忍教-地下迷宫3/大结局.txt',))
    start = late_completed_script(trace, '天忍教-地下迷宫3/大结局.txt')
    execution = [r for r in trace if r.get('executionId') == start['executionId']]
    if any(r.get('apiName') == 'returntotitle' for r in execution):
        raise AutomationError('The extra battle unexpectedly returned to title')
    client.save_or_load(6)
    checkpoint(client, output, 'true-extra-boss-before-normal-slot6')
    write_json(output / 'true-extra-battle-source-proof.json', dict(status='passed', firstBoss=boss,
               extraBoss=extra, firstEndingScript=start, normalSlot=6, endingCompleted=False))


def tianren_true_ending(client, output, *, lose=False):
    from jxqy2_mainline_supplies import select_medicine
    from run_jxqy2_mainline import late_target, late_records, late_completed_script
    state = verify(client, HappyEnding=1, ZhenJieJu=4, TrjDxmg=3)
    if state['map'] != '天忍教-地下迷宫3.map':
        raise AutomationError('Extra battle requires its normal manual source')
    boss = late_target(client, '完颜宏烈')
    if boss['life'] != 150000:
        raise AutomationError('This is not the distinct extra boss')
    action = None
    if lose:
        if state['cheatModeEnabled'] or state['cheatInvincibilityEnabled']:
            raise AutomationError('Natural defeat requires battle cheats disabled')
        # No attack or player-stat modification: normal NPC AI causes this defeat.
        client.move(boss['position']['x'] + 2, boss['position']['y'] + 4, running=False)
    else:
        # Advance actual dialogue pages manually to retain the cutscene positions.
        client.act('SetAutoDialogue', enabled=False)
        supplies = {}
        for stat, threshold in (('life', 60), ('mana', 30)):
            medicine = select_medicine(state, stat, require_ready=False)
            if medicine:
                supplies[stat + 'Item'] = medicine['file']
                supplies[stat + 'Percent'] = threshold
        action = client.submit('StartCombat', generation=state['generation'], targetId=boss['id'],
                               radius=20, kills=1, skills=[0], timeoutMs=240000, **supplies)
    videos, dialogue_positions = set(), []
    deadline = time.monotonic() + 360
    while time.monotonic() < deadline:
        state = client.observe()
        if state.get('choices'):
            raise AutomationError('Unexpected choice in the true ending')
        if state.get('dialogue') and not lose:
            dialogue_positions.append(dict(frame=state['frame'], map=state['map'], script=state['script'],
                                           player=state['player'], dialogue=state['dialogue']))
            if ('谢谢柴大哥' in state['dialogue']['text']
                    and not (output / 'true-happyending5-position.json').exists()):
                checkpoint(client, output, 'true-happyending5-position')
            client.ui('Confirm')
        if state.get('video'):
            videos.add(state['video'].replace('\\', '/').split('/')[-1].lower())
            client.ui('Cancel')
        if state['scene'] == 'Title':
            break
        if action is not None:
            status = client.request('GetActionStatus', actionId=action)
            if status['status'] == 'failed':
                raise AutomationError(f'Extra battle failed: {status}')
        time.sleep(.08)
    else:
        raise TimeoutError('True ending did not return normally to title')
    suffix = '天忍教-地下迷宫3/' + ('飞云败.txt' if lose else '真结局.txt')
    trace = late_records(output, 'trace.jsonl', completed_scripts=(suffix,))
    start = late_completed_script(trace, suffix)
    execution = [r for r in trace if r.get('executionId') == start['executionId']]
    if 'happyend.avi' not in videos or not any(r.get('apiName') == 'returntotitle' for r in execution):
        raise AutomationError('True ending lacks its observed movie or normal title return')
    combat = client.request('GetActionStatus', actionId=action) if action is not None else None
    if combat is not None and (combat['status'] != 'succeeded' or combat.get('kills') != 1):
        raise AutomationError(f'Extra boss kill lacks successful native combat: {combat}')
    proof = dict(fullPlaythrough=False, endingCompleted=True, ending='true-loss' if lose else 'true-victory',
                 finalBoss=boss, combatActionId=action, combatResult=combat, endingScript=start,
                 observedVideos=sorted(videos), scene=state['scene'], naturalPlayerDefeat=lose)
    write_json(output / 'true-ending-dialogue-positions.json', dialogue_positions)
    write_json(output / 'mainline-completion.json', proof)
    checkpoint(client, output, 'true-ending-title')
    return proof


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--assets", type=Path, default=Path("assets"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--route", choices=("inventory", "home-gate", "home-ready", "yifeng-unlock", "opening", "easy", "hard", "wolves", "desert-skill", "kuangsha-gates", "kuangsha-night-gate", "kuangsha-return-gates", "longmen-arrival", "longmen-departure", "fengxue-return-before-rescue", "changan-gates", "changan-npc-battle", "changan-family-first", "zangmagang", "zangmagang-south", "zangmagang-task-gates", "zangmagang-progress-return", "meng-ten-wins", "meng-loss", "mine-no-ore", "linan-chai-fusion", "linan-helmet-reward", "camp-generals", "fengchi-shao-loss", "tournament-source", "tournament-loss", "fengchi-prison-reverse", "prison-exit", "prison-companions-source", "prison-companions-loss", "prison-companions-rescue", "linan-revenge-return", "linan-after-zhao-source", "tianren-true-source", "tianren-true-dungeons", "tianren-true-first-boss", "tianren-true-victory", "tianren-true-loss", "tianren-dungeon-floors", "tianren-second-floor", "tianren-forge-return", "forge-materials", "forge-purchases", "forge-funding-source", "forest-skill-source", "zhongdu-after-mine", "zhongdu-after-chest", "zhongdu-after-shaolin", "zhongdu-return", *[x for x in CHAPTERS if x != "opening"]), required=True)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--source-run", type=Path)
    parser.add_argument("--load-slot", type=int, choices=range(7))
    parser.add_argument("--assist-hard-battle", action="store_true",
                        help="Use the native invincibility/resources menu on an isolated camp or final-battle branch")
    args = parser.parse_args()
    if args.assist_hard_battle and args.route not in ("camp-generals", "tianren-second-floor", "tianren-finale", "tianren-true-dungeons", "tianren-true-first-boss", "tianren-true-victory"):
        parser.error("This assistance is limited to the camp and Tianren battle routes")
    if args.route == "zangmagang-south" and (not args.resume or args.load_slot is not None):
        parser.error("South recovery continues the same live source without loading another save")
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
            parser.error("Resume requires a Chenghe run")
        if args.assist_hard_battle and not identity["cheatAssisted"]:
            parser.error("Keep the unassisted evidence intact; assistance requires a fresh isolated run")
    else:
        if not args.exe or args.source_run and args.load_slot is None:
            parser.error("Fresh run requires --exe; a source run also requires --load-slot")
        output.mkdir(parents=True, exist_ok=False)
        parent_identity = None
        if args.source_run:
            parent = args.source_run.resolve()
            parent_identity = json.loads((parent / "run.json").read_text(encoding="utf-8"))
            if parent_identity["resourceId"] != RESOURCE_ID:
                parser.error("Source saves must be Chenghe")
            relative_slot = Path("jian_er_gai_chenghe_1_041") / f"rpg{args.load_slot + 1}"
            source_slot = parent / "user-data/save" / relative_slot
            source_before = save_hashes(source_slot)
            if "game.ini" not in source_before:
                parser.error("Source must contain the selected normal manual slot")
            shutil.copytree(parent / "user-data/save", output / "user-data/save")
            copied = save_hashes(output / "user-data/save" / relative_slot)
            unchanged = source_before == copied == save_hashes(source_slot)
            write_json(output / "normal-save-clone-hash-verification.json",
                       dict(sourceSlot=args.load_slot, fileHashes=source_before,
                            sourceBeforeEqualsCloneEqualsSourceAfter=unchanged, saveBytesEdited=False))
            if not unchanged:
                parser.error("Normal manual source changed or its copied bytes differ")
            write_json(output / "normal-save-clone.json", dict(parentRun=str(parent),
                       sourceSlot=args.load_slot, saveBytesEdited=False))
        executable = args.exe.resolve()
        session = "chenghe-" + str(uuid.uuid4())
        command = [str(executable), "--assets", str(assets), "--resource-id", RESOURCE_ID,
                   "--skip-startup-video", "--enable-automation-hooks", "--automation-pipe", session,
                   "--user-data-root", str(output / "user-data"), "--log-file", str(output / "game.log")]
        with (output / "stdout.log").open("wb") as stdout, (output / "stderr.log").open("wb") as stderr:
            process = subprocess.Popen(command, cwd=executable.parent, stdout=stdout, stderr=stderr)
        identity = dict(resourceId=RESOURCE_ID, session=session, pid=process.pid, command=command,
                        engineSha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                        resourceProfileSha256=hashlib.sha256((resource / "game_profile.ini").read_bytes()).hexdigest(),
                        campResourceTemplateSha256AtLaunch=hashlib.sha256(
                            (resource / "ini/save/jinbingdayin.npc").read_bytes()).hexdigest(),
                        cheatAssisted=bool(args.assist_hard_battle or parent_identity and parent_identity.get("cheatAssisted")), started=time.time())
        if parent_identity:
            identity.update(parentRun=str(args.source_run.resolve()), parentSlot=args.load_slot)
            if parent_identity.get('silverSaveEdited'):
                identity['silverSaveEdited'] = True
        identity['assistHardBattleRequested'] = bool(args.assist_hard_battle)
        write_json(output / "run.json", identity)
    started = time.time()
    snapshot = output / f"route-source-{int(started * 1000000)}"
    snapshot.mkdir()
    for filename in ("run_chenghe_gameplay.py", "run_jxqy2_mainline.py", "run_jxqy2_gameplay_smoke.py",
                     "gameplay_automation.py", "jxqy2_sidequests.py", "jxqy2_mainline_supplies.py"):
        shutil.copy2(Path(__file__).with_name(filename), snapshot / filename)
    result = dict(route=args.route, status="running", started=started, resourceId=RESOURCE_ID,
                  fullCoverage=False, fullPlaythrough=False, cheatAssisted=identity["cheatAssisted"],
                  resumed=args.resume, loadSlot=args.load_slot, sourceSnapshot=str(snapshot))
    with Client(identity["session"], timeout=25, transcript=output / "commands.jsonl") as client:
        try:
            state = client.observe()
            if any(row["name"] == "return-to-title" for row in state.get("ui", [])):
                client.ui("Cancel")
            client.act("SetAutoDialogue", enabled=True)
            if args.load_slot is not None:
                state = client.observe()
                if state.get("script") and not state.get("worldInput"):
                    client.wait_until(lambda value: value.get("scene") == "Title" or value.get("worldInput"),
                                      timeout=240, description="normal script completion before loading")
                load_checkpoint(client, args.load_slot)
            if args.assist_hard_battle:
                from run_xjxqy_gameplay import assist
                prefix = "camp" if args.route == "camp-generals" else args.route
                before = checkpoint(client, output, f"{prefix}-assistance-plot-before")
                assist(client, output, invincible=True)
                after = checkpoint(client, output, f"{prefix}-assistance-plot-after")
                if before["variables"] != after["variables"]:
                    raise AutomationError("Native battle assistance changed plot variables")
            checkpoint(client, output, f"route-{args.route}-source-{int(started * 1000000)}")
            if args.route == "opening":
                opening(client, output)
            elif args.route == "home-gate":
                home_gate(client, output)
            elif args.route == "home-ready":
                home_ready(client, output)
            elif args.route == "yifeng-unlock":
                yifeng_unlock(client, output)
            elif args.route in ("easy", "hard"):
                departure(client, output, resource, args.route)
            elif args.route == "wolves":
                wolves(client, output)
            elif args.route == "desert-skill":
                desert_skill(client, output)
            elif args.route == "village":
                village(client, output)
            elif args.route == "kuangsha-gates":
                kuangsha_gates(client, output)
            elif args.route == "kuangsha-night-gate":
                kuangsha_night_gate(client, output)
            elif args.route == "kuangsha-return-gates":
                kuangsha_return_gates(client, output)
            elif args.route == "longmen-arrival":
                longmen_arrival(client, output)
            elif args.route == "zangmagang":
                zangmagang(client, output)
            elif args.route == "zangmagang-south":
                zangmagang(client, output, resume_south=True)
            elif args.route == "zangmagang-task-gates":
                zangmagang_task_gates(client, output, resource)
            elif args.route == "zangmagang-progress-return":
                zangmagang_progress_return(client, output, resource)
            elif args.route == "longmen-departure":
                longmen_departure(client, output)
            elif args.route == "fengxue-return-before-rescue":
                fengxue_return_before_rescue(client, output)
            elif args.route == "changan-gates":
                changan_gates(client, output, resource)
            elif args.route in ("changan-npc-battle", "changan-family-first"):
                from jxqy2_sidequests import changan_sidequests
                changan_sidequests(client, output, family_guard_count=14, official_count=17,
                                   npc_battle_wait_seconds=180 if args.route == "changan-npc-battle" else 0)
                client.save_or_load(4)
            elif args.route == "fengxue":
                CHAPTERS[args.route](client, output, wind_magic_file="0player-magic-风雪狂刀.ini",
                                    family_guard_count=14, official_count=17)
            elif args.route == "forest-skill-source":
                state = client.observe()
                if state["map"] not in ("别离村.map", "别离村迷宫.map"):
                    raise AutomationError("Forest skill probing requires its normal Bieli source")
                for filename in ("0player-magic-寒霜掌.ini", "0player-magic-风雪狂刀.ini"):
                    item(state, filename, "magic")
            elif args.route == "cuiyan-first":
                state = client.observe()
                sword = item(state, "0player-magic-天意剑诀.ini", "magic")
                if sword["slot"] != state["layout"]["magicQuickBegin"]:
                    client.assign_magic(sword["slot"], quick_slot=0)
                checkpoint(client, output, "bieli-combat-loadout")
                CHAPTERS[args.route](client, output, dingshen_magic_file="0player-magic-定身法.ini",
                                    scroll_reward_variable="WuDaoDeJing")
            elif args.route == "tangmen":
                if client.observe()["map"] == "别离村-唐门.map":
                    client.save_or_load(3)
                    checkpoint(client, output, "tangmen-before-capture-slot3")
                CHAPTERS[args.route](client, output, rain_magic_file="0player-magic-漫天花雨手法.ini",
                                    healing_magic_file="0player-magic-白虹贯日.ini",
                                    healing_costs=tuple(range(1, 11)), capture_timeout=600)
            elif args.route == "cuiyan-second":
                CHAPTERS[args.route](client, output, flower_magic_file="0player-magic-花飞蝶舞剑.ini")
            elif args.route == "tianwang":
                state = client.observe(("YiFengOn", "HanYanCTMSZL"))
                if state["map"] == "汉阳.map" and state["variables"].get("HanYanCTMSZL") in (None, "", "0"):
                    client.save_or_load(3)
                    checkpoint(client, output, "hanyang-before-tianwang-slot3")
                first_chest = state["variables"].get("YiFengOn") == "1"
                if state["map"] == "汉阳.map" and state["variables"].get("HanYanCTMSZL") in (None, "", "0"):
                    chest = any(target["name"] == "宝箱" and target["position"] == dict(x=37, y=9)
                                for target in state["targets"])
                    if chest != first_chest:
                        raise AutomationError("Hanyang first-arrival chest differs from its native Yifeng branch")
                    checkpoint(client, output, "hanyang-first-chest-present" if chest else "hanyang-first-chest-absent")
                CHAPTERS[args.route](client, output, magic_file_prefix="0player-magic-",
                                    yifeng_chest_required=first_chest, jump_to_guard=True)
            elif args.route == "to-zhongdu":
                if client.observe()["map"] == "汉阳.map":
                    client.save_or_load(4)
                    checkpoint(client, output, "hanyang-before-meng-slot4")
                CHAPTERS[args.route](client, output, magic_file_prefix="0player-magic-", camp_source_slot=6)
            elif args.route in ("meng-ten-wins", "meng-loss"):
                meng_branch(client, output, lose=args.route == "meng-loss")
            elif args.route == "linan-fengchi":
                CHAPTERS[args.route](client, output, magic_file_prefix="0player-magic-")
            elif args.route == "linan-chai-fusion":
                linan_chai_fusion(client, output)
            elif args.route == "camp-generals":
                camp_generals(client, output)
            elif args.route == "fengchi-shao-loss":
                fengchi_shao_loss(client, output)
            elif args.route == "tournament-source":
                state = client.observe(('FCBW', 'HappyEnding', 'Zhenbeiju'))
                if (state['map'] != '凤池山庄-比武场.map'
                        or not 1 <= int(state['variables'].get('FCBW') or 0) <= 11):
                    raise AutomationError('Tournament source requires a normal active round')
            elif args.route == "tournament-loss":
                tournament_loss(client, output)
            elif args.route in ("fengchi-prison", "fengchi-prison-reverse"):
                fengchi_prison(client, output, reverse_boss_order=args.route.endswith("reverse"))
            elif args.route == "prison-companions-source":
                state = verify(client, FromFengChi=14, ShaoJiFeng=0, LinAnDie=0)
                if state['map'] != '临安大牢第1层.map':
                    raise AutomationError('Companion source requires the normal pre-challenge slot')
            elif args.route == "prison-companions-loss":
                prison_companions_loss(client, output)
            elif args.route == "prison-companions-rescue":
                from run_jxqy2_mainline import prison_exit
                state = verify(client, FromFengChi=14, ShaoJiFeng=1)
                if state["map"] != "临安大牢第1层.map":
                    raise AutomationError("Companion rescue requires its normal active battle source")
                boss = next(actor for actor in state["targets"] if actor["name"] == "邵骑风"
                            and actor.get("hostile") and npc_attackable(actor))
                fight(client, boss, skills=(0,))
                verify(client, ShaoJiFeng=2)
                checkpoint(client, output, "partial-companions-rescue-outcome")
                prison_exit(client, output)
                client.save_or_load(6)
                checkpoint(client, output, "partial-companions-rescue-linan-slot6")
            elif args.route == "prison-exit":
                from run_jxqy2_mainline import prison_exit
                prison_exit(client, output)
                client.save_or_load(6)
                checkpoint(client, output, "prison-exit-normal-slot6")
            elif args.route == "linan-after-zhao-source":
                state = verify(client, FromFengChi=19, ToZhongDu=1, TianWangPiEr=4)
                if state["map"] != "临安地下迷宫.map":
                    raise AutomationError("The book branch requires the normal source before the family revisit")
            elif args.route in ("linan-revenge-return", "linan-helmet-reward"):
                from run_jxqy2_mainline import linan_after_zhao
                helmet = "goods-toukui-15-天机神盔.ini"
                before_quantity = sum(entry["quantity"] for entry in client.observe()["inventory"]
                                      if entry["file"] == helmet)
                linan_after_zhao(client, output, magic_file_prefix="0player-magic-")
                if args.route == "linan-helmet-reward":
                    from run_jxqy2_mainline import late_records, late_completed_script
                    state = checkpoint(client, output, "linan-helmet-reward-observed")
                    if item(state, helmet)["quantity"] != before_quantity + 1:
                        raise AutomationError("General death did not grant exactly one original helmet")
                    records = late_records(output, "trace.jsonl",
                                           completed_scripts=("临安地下迷宫/将领死.txt",))
                    late_completed_script(records, "临安地下迷宫/将领死.txt")
                client.save_or_load(6)
                checkpoint(client, output, "revenge-zhongdu-normal-slot6")
            elif args.route == "linan-revenge":
                CHAPTERS[args.route](client, output, magic_file_prefix="0player-magic-")
                client.save_or_load(6)
                checkpoint(client, output, "revenge-zhongdu-normal-slot6")
            elif args.route == "tianren-finale":
                state = client.observe(("HappyEnding", "ZhenJieJu", "Zhenbeiju"))
                if (int(state["variables"].get("HappyEnding") or 0) == 1
                        and int(state["variables"].get("ZhenJieJu") or 0) >= 4):
                    raise AutomationError("This source requires the additional true-ending battle")
                bitter = int(state["variables"].get("Zhenbeiju") or 0) >= 5
                CHAPTERS[args.route](client, output, tianyi_file="0player-magic-天意剑诀.ini",
                                    ending_movie_lines=(215 if bitter else 57, 140),
                                    ending_names=("true-bitter" if bitter else "ordinary", "happy"))
            elif args.route == "tianren-dungeons":
                CHAPTERS[args.route](client, output, branch_source_slot=1)
                client.save_or_load(6)
                checkpoint(client, output, "tianren-before-finale-normal-slot6")
            elif args.route == "tianren-true-dungeons":
                from run_jxqy2_mainline import tianren_second_floor
                state = verify(client, HappyEnding=1, ZhenJieJu=3, TrjDxmg=1, TrjDxmgshijiang=2)
                if state['map'] != '天忍教-地下迷宫1.map':
                    raise AutomationError('True dungeons require the completed first-floor source')
                item(state, '剑中之剑.ini')
                go(client, 75, 30, destination='天忍教-地下迷宫2.map')
                tianren_second_floor(client, output)
                verify(client, HappyEnding=1, ZhenJieJu=4, TrjDxmg=3, LinAnDie=0)
                client.save_or_load(6)
                checkpoint(client, output, 'true-ending-before-first-final-boss-normal-slot6')
            elif args.route == "tianren-true-first-boss":
                tianren_true_first_boss(client, output)
            elif args.route in ("tianren-true-victory", "tianren-true-loss"):
                tianren_true_ending(client, output, lose=args.route.endswith("loss"))
            elif args.route == "tianren-true-source":
                state = verify(client, HappyEnding=1, FromFengChi=20, Jian=0, JianShui=0)
                if (int(client.observe(("ZhenJieJu",))["variables"].get("ZhenJieJu") or 0) < 3
                        or state["map"] not in ("中都.map", "天忍教-地下迷宫1.map",
                                                "天忍教-地下迷宫2.map", "天忍教-地下迷宫3.map")):
                    raise AutomationError("True ending requires the normal forged-sword source")
                item(state, "剑中之剑.ini")
            elif args.route == "tianren-dungeon-floors":
                from run_jxqy2_mainline import tianren_dungeon_floors
                tianren_dungeon_floors(client, output, branch_source_slot=1)
                client.save_or_load(6)
                checkpoint(client, output, "tianren-before-finale-normal-slot6")
            elif args.route == "tianren-second-floor":
                from run_jxqy2_mainline import tianren_second_floor
                tianren_second_floor(client, output)
                client.save_or_load(6)
                checkpoint(client, output, "tianren-before-finale-normal-slot6")
            elif args.route == "tianren-forge-return":
                state = verify(client, Jian=1, ZhenJieJu=2, TrjDxmg=1, TrjDxmgshijiang=2)
                if state["map"] != "天忍教-地下迷宫1.map":
                    raise AutomationError("Forge return requires the normal first-floor hammer source")
                item(state, "goods-sj-11-金刚锤.ini")
                for destination in ("天忍教.map", "中都-天忍教.map", "中都.map"):
                    state = idle(client)
                    occupied = {(actor["position"]["x"], actor["position"]["y"])
                                for actor in state["targets"]}
                    point = reachable_trap(resource, state["map"], 1,
                                           state["player"]["position"], occupied)
                    go(client, *point, destination=destination)
                verify(client, Jian=1, ZhenJieJu=2, TrjDxmg=1)
                client.save_or_load(6)
                checkpoint(client, output, "forge-return-zhongdu-normal-slot6")
            elif args.route == "forge-materials":
                forge_materials(client, output)
            elif args.route == "forge-purchases":
                forge_material_purchases(client, output)
            elif args.route == "forge-funding-source":
                state = verify(client, Jian=1, ZhenJieJu=2, FromFengChi=20)
                if state["map"] not in ("临安地下迷宫.map", "临安城.map", "中都.map", "中都地下迷宫.map", "天忍教-地下迷宫1.map"):
                    raise AutomationError("Forge funding requires its normal late-game source")
                for filename in ("无名之剑.ini", "goods-yaowu-8-皇家密药.ini",
                                 "goods-weijin-9-千年古玉.ini", "goods-weijin-11-碧水珠.ini"):
                    item(state, filename)
            elif args.route == "pilitang-tournament":
                CHAPTERS[args.route](client, output, magic_file_prefix="0player-magic-",
                                    sword_file="goods-jian-8-霸陵剑.ini", resource_directory=RESOURCE_DIRECTORY,
                                    tournament_source_slot=6)
            elif args.route == "zhongdu-return":
                from run_jxqy2_mainline import late_jump, zhongdu_night_main_gate, zhongdu_return_to_linan
                state = verify(client, ZhongDuTroublesRoom=4, ZhongDuHouHuaYuan=5,
                               OuYangDie=2, ZDBiWu=4, OuYangMusic=1)
                if state["map"] != "中都夜.map":
                    raise AutomationError("Return recovery requires the normal fifth-meeting night scene")
                path = reachable_trap(resource, state["map"], None, state["player"]["position"],
                                      destination=(130, 323), with_path=True,
                                      avoid=trap_points(resource, state["map"], 9))
                for point in path[3::3]:
                    go(client, *point)
                late_jump(client, (130, 323), (138, 323))
                checkpoint(client, output, "zhongdu-night-main-gate-approach")
                zhongdu_night_main_gate(client, output)
                zhongdu_return_to_linan(client, output, magic_file_prefix="0player-magic-")
            elif args.route == "mine-no-ore":
                from run_jxqy2_mainline import zhongdu_mine_side_story
                state = verify(client, Jian=0, KuangGongZouLe=0,
                               **{"1JiangLing": 1, "2JiangLing": 2, "3JiangLing": 3})
                if state["map"] != "矿山.map":
                    raise AutomationError("Miner escape requires the normal source before collecting ore")
                zhongdu_mine_side_story(client, output, magic_file_prefix="0player-magic-",
                                       craft_sword=False, collect_ore=False)
                client.save_or_load(6)
                checkpoint(client, output, "mine-no-ore-complete-slot6")
            elif args.route == "zhongdu-first":
                state = client.observe(("ZhongDuLYS",))
                if state["map"] == "中都.map":
                    client.save_or_load(3)
                    checkpoint(client, output, "zhongdu-before-mine-slot3")
                    if state["player"]["position"] == dict(x=25, y=317):
                        go(client, 33, 288, script="中都/地图陷阱1.txt", combat=False)
                    if state["variables"].get("ZhongDuLYS") in (None, "", "0"):
                        state = client.observe()
                        path = reachable_trap(resource, state["map"], None, state["player"]["position"],
                                              destination=(45, 98), with_path=True)
                        for point in path[12::12]:
                            go(client, *point, combat=False, running=True)
                        go(client, 45, 98, combat=False, running=True)
                CHAPTERS[args.route](client, output, magic_file_prefix="0player-magic-", craft_sword=False,
                                     ore_miner_position=(64, 20), jump_to_ore_miner=True,
                                     second_boss_name="欧阳桐乙", captive_money_range=(10000, 99999),
                                     leave_via_main_gate=True)
            elif args.route in ("zhongdu-after-mine", "zhongdu-after-chest", "zhongdu-after-shaolin"):
                from run_jxqy2_mainline import zhongdu_town_story, late_records, late_completed_script
                state = verify(client, Jian=1, KuangGongZouLe=1, ZhongDuLYS=1,
                               **{"1JiangLing": 1, "2JiangLing": 2, "3JiangLing": 3, "4JiangLing": 4})
                expected_map = "中都地下迷宫.map" if args.route == "zhongdu-after-shaolin" else "中都.map"
                if state["map"] != expected_map:
                    raise AutomationError("Post-mine route requires the normal Zhongdu return")
                item(state, "goods-奇矿石.ini")
                opened = args.route in ("zhongdu-after-chest", "zhongdu-after-shaolin")
                if opened:
                    records = late_records(output, "trace.jsonl",
                                          completed_scripts=("中都/地图陷阱17.txt", "script/common/大量银子.txt"))
                    family = late_completed_script(records, "中都/地图陷阱17.txt")
                    treasury = late_completed_script(records, "script/common/大量银子.txt")
                    if records.index(treasury) <= records.index(family):
                        raise AutomationError("Treasury recovery requires its completed reward after the mine return")
                zhongdu_town_story(client, output, magic_file_prefix="0player-magic-", craft_sword=False,
                                   treasury_chest_opened=opened, second_boss_name="欧阳桐乙",
                                   captive_money_range=(10000, 99999),
                                   leave_via_main_gate=True)
            else:
                CHAPTERS[args.route](client, output)
            state = checkpoint(client, output, f"route-{args.route}-complete")
            result.update(status="passed", map=state.get("map"), player=state.get("player"))
        except Exception as error:
            result.update(status="failed", error=str(error))
            try:
                checkpoint(client, output, f"failure-{args.route}-{int(time.time())}")
            except Exception as evidence_error:
                result["evidenceError"] = str(evidence_error)
        finally:
            result.update(finished=time.time(), elapsedSeconds=time.time() - started)
            write_json(output / f"result-{args.route}-{int(time.time())}.json", result)
            print(json.dumps(result, ensure_ascii=False), flush=True)
    raise SystemExit(0 if result["status"] == "passed" else 1)


if __name__ == "__main__":
    main()
