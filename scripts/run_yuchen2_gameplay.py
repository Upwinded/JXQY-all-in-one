"""Yuchen II routes using the existing isolated-save player runner."""
from __future__ import annotations

import argparse
import configparser
import sys
import time

from gameplay_automation import AutomationError
import run_yuchen_gameplay as yuchen
import run_yuemeier_gameplay as yuemeier
import run_yycs_gameplay as yycs

RESOURCE_ID = "JIANGHU_YUCHEN_2"
RESOURCE_DIRECTORY = "江湖余尘二"
SAVE_NAMESPACE = RESOURCE_ID.lower()


def combat_magic(client):
    learned = {row["file"] for row in client.observe()["magic"]}
    for filename in ("player-magic-魂牵梦绕.ini", "player-magic-烈火情天.ini", "player-magic-逆转心经.ini"):
        if filename in learned:
            return filename
    raise AutomationError("No checked offensive magic is actually learned")


def talk(client, output, resource, name, choice=None):
    state = yycs.idle(client)
    actors = [row for row in state["targets"] if row["name"] == name
              and (row.get("interactive") or row["kind"] == "object")]
    if len(actors) != 1:
        raise AutomationError(f"Expected one current actor: {name}")
    point = actors[0]["position"]
    yuemeier.approach_target(client, output, resource, (point["x"], point["y"]),
                             magic_file=combat_magic(client))
    return yycs.interact_named(client, output, resource, name, choice=choice)


def interact_choice(client, output, resource, name, filename, option):
    state = yycs.idle(client)
    path = f"script/map/{state['map'].rsplit('.', 1)[0]}/{filename}"
    sites = [site for site in yycs.inventory(resource)["choices"] if site["path"] == path]
    if len(sites) != 1:
        raise AutomationError(f"Expected one checked choice: {path}")
    return talk(client, output, resource, name, choice=(sites[0]["id"], option))


def early_tasks(client, output, resource, args):
    before = yuchen.checkpoint(client, output, "early-task-before")
    interact_choice(client, output, resource, "温名华", "温名华对话.txt", args.option)
    after = yuchen.checkpoint(client, output, "wen-task-choice")
    if after["variables"].get("wmh") != ("1" if args.option == 0 else before["variables"].get("wmh")):
        raise AutomationError("Wen Minghua acceptance/refusal differs")
    if after["inventory"] != before["inventory"] or after["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Early task unexpectedly changed inventory or money")
    if args.option == 0:
        yycs.interact_named(client, output, resource, "温名华")
        repeated = yuchen.checkpoint(client, output, "wen-task-waiting-repeat")
        if repeated["inventory"] != after["inventory"]:
            raise AutomationError("Wen task rewarded before obtaining grass")
    yycs.transition(client, resource.parent / "yycs", "map_019_寒波谷.map", 1, running=True)
    arrival = yuchen.checkpoint(client, output, "hanbo-arrival")
    interact_choice(client, output, resource, "苏莹莹", "苏莹莹对话.txt", 0)
    insufficient = yuchen.checkpoint(client, output, "su-insufficient-400")
    if (insufficient["player"]["money"] != arrival["player"]["money"]
            or insufficient["inventory"] != arrival["inventory"]):
        raise AutomationError("Insufficient recruitment charged money or changed inventory/partner")
    interact_choice(client, output, resource, "苏莹莹", "苏莹莹对话.txt", 1)
    refused = yuchen.checkpoint(client, output, "su-refused")
    if refused["player"]["money"] != arrival["player"]["money"]:
        raise AutomationError("Refused recruitment charged money")
    interact_choice(client, output, resource, "刘婆婆", "刘婆婆对话.txt", args.option)
    granny = yuchen.checkpoint(client, output, "granny-task-choice")
    if granny["variables"].get("lpp") != ("1" if args.option == 0 else arrival["variables"].get("lpp")):
        raise AutomationError("Granny acceptance/refusal differs")
    if granny["inventory"] != arrival["inventory"] or granny["player"]["money"] != arrival["player"]["money"]:
        raise AutomationError("Granny's initial task changed reward or money")
    client.save_or_load(1)
    saved = configparser.ConfigParser(interpolation=None, strict=False)
    saved.read(output / "user-data/save" / SAVE_NAMESPACE / "rpg2/map019.npc", encoding="utf-8-sig")
    su = [saved[section] for section in saved if saved[section].get("Name") == "苏莹莹"]
    if len(su) != 1 or su[0].get("Kind") != "0" or su[0].get("ScriptFile") != "苏莹莹对话.txt":
        raise AutomationError("Unfunded/refused Su recruitment changed native kind or binding")
    before_load = yuchen.checkpoint(client, output, "early-tasks-slot1")
    yycs.load_checkpoint(client, 1)
    loaded = yuchen.checkpoint(client, output, "early-tasks-reloaded")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if before_load[key] != loaded[key]:
            raise AutomationError(f"Early task save/load changed {key}")


def travel(client, output, resource, destination, trap):
    yuemeier.approach_exit(client, output, resource, trap, magic_file=combat_magic(client))
    yycs.transition(client, resource.parent / "yycs", destination, trap, running=True)
    yuchen.checkpoint(client, output, "arrival-" + destination.rsplit(".", 1)[0])


def bridge(client, output, resource):
    travel(client, output, resource, "map_018_连接地图.map", 1)
    travel(client, output, resource, "map_017_连接地图.map", 1)


def main_start(client, output, resource, args):
    if yycs.idle(client)["map"] == "map_019_寒波谷.map":
        bridge(client, output, resource)
    interact_choice(client, output, resource, "文公子", "文公子对话.txt", 1)
    refusal = yuchen.checkpoint(client, output, "wen-gongzi-refused")
    if refusal["variables"].get("wgz") or any(row["file"] == "0武林帖.ini" for row in refusal["inventory"]):
        raise AutomationError("Wen Gongzi refusal advanced the martial letter task")
    interact_choice(client, output, resource, "文公子", "文公子对话.txt", 0)
    accepted = yuchen.checkpoint(client, output, "wen-gongzi-accepted")
    if accepted["variables"].get("wgz") != "1" or accepted["player"]["money"] != refusal["player"]["money"]:
        raise AutomationError("Wen Gongzi acceptance granted premature money")
    client.save_or_load(2)
    travel(client, output, resource, "map_014_连接地图.map", 1)
    yycs.interact_named(client, output, resource, "赵蒙")
    blocked = yuchen.checkpoint(client, output, "zhao-before-martial-letter")
    if blocked["variables"].get("wlt"):
        raise AutomationError("Zhao advanced the task without the martial letter")
    travel(client, output, resource, "map_015_藏剑山庄.map", 2)
    manor_start(client, output, resource, args)


def manor_start(client, output, resource, args):
    for name in ("万天平", "许白"):
        talk(client, output, resource, name)
    premature = yuchen.checkpoint(client, output, "manor-before-task")
    if premature["variables"].get("wtp") or any(row["hostile"] for row in premature["targets"] if row["name"] == "许白"):
        raise AutomationError("Manor clue or Xu Bai battle started prematurely")
    interact_choice(client, output, resource, "周庄主", "周庄主对话.txt", 1)
    declined = yuchen.checkpoint(client, output, "zhou-refused")
    if declined["variables"].get("zzz"):
        raise AutomationError("Zhou refusal accepted the task")
    interact_choice(client, output, resource, "周庄主", "周庄主对话.txt", 0)
    accepted = yuchen.checkpoint(client, output, "zhou-accepted")
    if accepted["variables"].get("zzz") != "1":
        raise AutomationError("Zhou acceptance failed")
    client.save_or_load(3)
    yuchen.checkpoint(client, output, "manor-investigation-slot3")


def fight_actor(client, output, resource, name, expected_variable, expected_value):
    state = yycs.idle(client)
    target = next(row for row in state["targets"] if row["name"] == name and yycs.npc_attackable(row))
    point = target["position"]
    yuemeier.approach_target(client, output, resource, (point["x"], point["y"]),
                             magic_file=combat_magic(client))
    before = yuchen.checkpoint(client, output, name + "-battle-before")
    yycs.fight_named(client, output, resource, name, single_target=True, target_id=target["id"])
    after = yuchen.checkpoint(client, output, name + "-battle-completed")
    if after["variables"].get(expected_variable) != expected_value:
        raise AutomationError(f"Native {name} death did not set {expected_variable}={expected_value}")
    return before, after


def manor_battles(client, output, resource, args):
    fight_actor(client, output, resource, "神秘人", "zzz", "2")
    before = yuchen.checkpoint(client, output, "zhou-before-investigation-report")
    talk(client, output, resource, "周庄主")
    after = yuchen.checkpoint(client, output, "zhou-investigation-reported")
    if (after["variables"].get("zzz") != "3" or after["variables"].get("wtp") != "1"
            or after["inventory"] != before["inventory"] or after["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Zhou report differs from its explicit no-payment clue reward")
    talk(client, output, resource, "周庄主")
    repeated = yuchen.checkpoint(client, output, "zhou-report-repeated")
    if repeated["inventory"] != after["inventory"] or repeated["player"]["money"] != after["player"]["money"]:
        raise AutomationError("Zhou report repeated a reward")
    talk(client, output, resource, "万天平")
    clue = yuchen.checkpoint(client, output, "wan-sheepskin-clue")
    if clue["variables"].get("wtp") != "2":
        raise AutomationError("Wan's clue did not unlock Xu Bai")
    talk(client, output, resource, "许白")
    _, reward = fight_actor(client, output, resource, "许白", "wtp", "3")
    if reward["variables"].get("wgz") != "2" or sum(row["quantity"] for row in reward["inventory"] if row["file"] == "0羊皮.ini") != 1:
        raise AutomationError("Xu Bai death did not grant one sheepskin and unlock Wen")
    talk(client, output, resource, "万天平")
    client.save_or_load(4)
    yuchen.checkpoint(client, output, "manor-sheepskin-slot4")


def martial_letter(client, output, resource, args):
    travel(client, output, resource, "map_014_连接地图.map", 1)
    travel(client, output, resource, "map_017_连接地图.map", 3)
    before = yuchen.checkpoint(client, output, "wen-before-sheepskin-battle")
    talk(client, output, resource, "文公子")
    _, reward = fight_actor(client, output, resource, "文公子", "wlt", "1")
    if sum(row["quantity"] for row in reward["inventory"] if row["file"] == "0武林帖.ini") != 1:
        raise AutomationError("Wen's actual death script did not grant one martial letter")
    if reward["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Wen's death unexpectedly paid money absent from source")
    client.save_or_load(5)
    yuchen.checkpoint(client, output, "martial-letter-slot5")
    travel(client, output, resource, "map_014_连接地图.map", 1)
    client.save_or_load(6)
    yuchen.checkpoint(client, output, "zhao-letter-choice-source-slot6")


def zhao_choice(client, output, resource, args):
    before = yuchen.checkpoint(client, output, "zhao-letter-before-choice")
    interact_choice(client, output, resource, "赵蒙", "赵蒙对话.txt", args.option)
    after = yuchen.checkpoint(client, output, "zhao-letter-after-choice")
    if after["variables"].get("wlt") != ("2" if args.option == 0 else "1"):
        raise AutomationError("Zhao acceptance/refusal differs from its actual source")
    if after["inventory"] != before["inventory"] or after["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Zhao choice changed money or consumed an item absent from its source")
    talk(client, output, resource, "赵蒙", choice=None if args.option == 0 else
         (next(site["id"] for site in yycs.inventory(resource)["choices"]
               if site["path"].endswith("map_014_连接地图/赵蒙对话.txt")), 1))
    client.save_or_load(0)
    yuchen.checkpoint(client, output, "zhao-letter-choice-slot0")


def finale(client, output, resource, args):
    travel(client, output, resource, "map_015_藏剑山庄.map", 2)
    travel(client, output, resource, "map_016_剑气峰.map", 2)
    talk(client, output, resource, "鬼道人")
    before = yuchen.checkpoint(client, output, "ghost-before-final-battle")
    boss = next(row for row in before["targets"] if row["name"] == "鬼道人")
    if not boss["hostile"] or boss["life"] != 5999 or before["variables"].get("wlt") != "2":
        raise AutomationError("Ghost's final battle did not start from the actual accepted task")
    client.save_or_load(1)
    yycs.fight_named(client, output, resource, "鬼道人", single_target=True, target_id=boss["id"],
                     expected_terminal="script/map/map_016_剑气峰/鬼道人死亡.txt",
                     final_dialogue="恭喜您顺利通关")
    state = yuchen.checkpoint(client, output, "ghost-actual-ending-title")
    starts = [row for row in yycs.trace_records(output) if row.get("eventType") == "script.start"
              and row.get("virtualPath") == "script/map/map_016_剑气峰/鬼道人死亡.txt"]
    records = yycs.completed_script(output, starts[-1])
    if state["scene"] != "Title" or not any(row.get("apiName") == "freemap" for row in records):
        raise AutomationError("Ghost ending did not release the map and return to title")
    yuchen.write_json(output / "actual-ending-proof.json", dict(status="passed", finalBoss=boss,
        endingSource=starts[-1], actualReturnToTitle=True, actualFreeMap=True,
        videoCalled=any(row.get("apiName") == "playmovie" for row in records),
        normalFinalBattleSlot=1, cheatAssisted=True, saveBytesEdited=False,
        fullPlaythrough=False, fullCoverage=False))


def ghost_blocked(client, output, resource, args):
    travel(client, output, resource, "map_015_藏剑山庄.map", 2)
    travel(client, output, resource, "map_016_剑气峰.map", 2)
    talk(client, output, resource, "鬼道人")
    state = yuchen.checkpoint(client, output, "ghost-with-refused-zhao-task")
    boss = next(row for row in state["targets"] if row["name"] == "鬼道人")
    if boss["hostile"] or boss["life"] != 5999 or state["variables"].get("wlt") != "1":
        raise AutomationError("Ghost became hostile without accepting Zhao's task")
    client.save_or_load(1)


def manor_exit(client, output, resource, args):
    travel(client, output, resource, "map_014_连接地图.map", 1)
    state = yuchen.checkpoint(client, output, "manor-exit-object-regression")
    client.save_or_load(0)
    game = configparser.ConfigParser()
    game.read(output / "user-data/save" / SAVE_NAMESPACE / "rpg1/game.ini", encoding="utf-8-sig")
    if game["state"]["obj"] != "map014_obj.obj" or sum(row["kind"] == "object" for row in state["targets"]) != 37:
        raise AutomationError("Manor exit loaded the wrong or empty actual object table")
    yycs.load_checkpoint(client, 0)
    after = yuchen.checkpoint(client, output, "manor-exit-object-reloaded")
    if sum(row["kind"] == "object" for row in after["targets"]) != 37:
        raise AutomationError("Manor exit object table was lost after normal save/load")


def star_quest(client, output, resource, args):
    travel(client, output, resource, "map_024_倚天山.map", 1)
    travel(client, output, resource, "map_025_摘星楼.map", 2)
    talk(client, output, resource, "狗肉")
    interact_choice(client, output, resource, "刘玥", "刘玥对话.txt", 1)
    refused = yuchen.checkpoint(client, output, "liu-star-task-refused")
    if refused["variables"].get("fs"):
        raise AutomationError("Liu refusal accepted the investigation")
    interact_choice(client, output, resource, "刘玥", "刘玥对话.txt", 0)
    talk(client, output, resource, "刘玥")
    client.save_or_load(2)
    accepted = yuchen.checkpoint(client, output, "liu-star-task-accepted-slot2")
    if accepted["variables"].get("fs") != "1":
        raise AutomationError("Liu acceptance failed to bind the investigation")
    travel(client, output, resource, "map_026_摘星楼地下.map", 2)
    talk(client, output, resource, "缘落")
    prior = yuchen.checkpoint(client, output, "yuan-before-feng-testimony")
    if any(row["hostile"] for row in prior["targets"] if row["name"] == "缘落"):
        raise AutomationError("Yuan became hostile before Feng's testimony")
    talk(client, output, resource, "冯氏")
    testimony = yuchen.checkpoint(client, output, "feng-testimony")
    if testimony["variables"].get("fs") != "2":
        raise AutomationError("The actual Feng object did not grant testimony")
    talk(client, output, resource, "冯氏")
    talk(client, output, resource, "缘落")
    fight_actor(client, output, resource, "缘落", "swyl", "1")
    client.save_or_load(3)
    star_reward(client, output, resource, args)


def star_reward(client, output, resource, args):
    if yycs.idle(client)["map"] == "map_026_摘星楼地下.map":
        travel(client, output, resource, "map_025_摘星楼.map", 1)
    yuemeier.approach_exit(client, output, resource, 0, destination=(31, 103),
                          magic_file=combat_magic(client), excluded_traps=(2,))
    talk(client, output, resource, "刘玥")
    reward = yuchen.checkpoint(client, output, "liu-star-investigation-reward")
    if (reward["variables"].get("swyl") != "2"
            or not any(row["file"] == "player-magic-清心咒.ini" for row in reward["magic"])
            or any(row["kind"] == "object" and row["name"] == "尸体" for row in reward["targets"])):
        raise AutomationError("Liu did not teach Qingxin and remove the actual corpse")
    talk(client, output, resource, "刘玥")
    repeated = yuchen.checkpoint(client, output, "liu-star-reward-not-repeated")
    if repeated["magic"] != reward["magic"] or repeated["inventory"] != reward["inventory"]:
        raise AutomationError("Liu repeated the investigation reward")
    client.save_or_load(4)
    yycs.load_checkpoint(client, 4)
    yuchen.checkpoint(client, output, "star-quest-completed-reloaded")


def boxes(client, output, resource, args):
    yuchen.reward_boxes(client, output, resource, magic_file=combat_magic(client))
    state = client.observe()
    yuchen.write_json(output / f"reward-box-proofs-{state['map']}.json",
                      yuchen.json.loads((output / "reward-box-proofs.json").read_text(encoding="utf-8")))


def field_bridge_boxes(client, output, resource, args):
    boxes(client, output, resource, args)
    travel(client, output, resource, "map_024_倚天山.map", 1)
    travel(client, output, resource, "map_023_连接地图.map", 3)
    boxes(client, output, resource, args)


def su_recruit(client, output, resource, args):
    before = yuchen.checkpoint(client, output, "su-funded-before")
    interact_choice(client, output, resource, "苏莹莹", "苏莹莹对话.txt", 1)
    refused = yuchen.checkpoint(client, output, "su-funded-refused")
    if refused["player"]["money"] != before["player"]["money"]:
        raise AutomationError("Funded refusal charged Su's fee")
    interact_choice(client, output, resource, "苏莹莹", "苏莹莹对话.txt", 0)
    after = yuchen.checkpoint(client, output, "su-funded-recruited")
    if (after["player"]["money"] != before["player"]["money"] - 500
            or not any(row["name"].startswith("partner-head-") for row in after["ui"])):
        raise AutomationError("Su recruitment did not charge exactly 500 and add the partner")
    client.save_or_load(2)


def party_manage(client, output, resource, args):
    before = yuchen.checkpoint(client, output, "party-management-before")
    name = next(row["name"] for row in before["targets"] if row["name"] in ("苏莹莹", "狗肉"))
    filename = f"右键{name}队友.txt"
    site = next(row for row in yycs.inventory(resource)["choices"] if row["path"] == f"script/common/{filename}")
    sword = next(row for row in before["inventory"] if row["file"] == "goods-w12-桃木剑.ini")
    client.activate("partner-head-0")
    client.wait_until(lambda value: any(row["name"].startswith("partner-equipment-item-") for row in value["ui"]),
                      description="native partner equipment menu")
    client.focus_slot("partner-player-bag-item-", sword["slot"])
    client.ui("Confirm")
    yuchen.checkpoint(client, output, "party-sword-native-menu")
    for _ in range(3):
        if client.observe().get("worldInput"):
            break
        client.ui("Cancel")
    talk(client, output, resource, name, choice=(site["id"], 2))
    talk(client, output, resource, name, choice=(site["id"], 1))
    left = yuchen.checkpoint(client, output, "party-left")
    if any(row["name"].startswith("partner-head-") for row in left["ui"]):
        raise AutomationError("Partner remained in the party after leaving")
    talk(client, output, resource, name, choice=(site["id"], 0))
    joined = yuchen.checkpoint(client, output, "party-rejoined-without-fee")
    if (not any(row["name"].startswith("partner-head-") for row in joined["ui"])
            or joined["player"]["money"] != before["player"]["money"]
            or any(row["file"] == sword["file"] for row in joined["inventory"])):
        raise AutomationError("Partner rejoin, sword transfer or fee differs")
    destination, trap = (("map_018_连接地图.map", 1) if before["map"] == "map_019_寒波谷.map"
                         else ("map_024_倚天山.map", 1))
    travel(client, output, resource, destination, trap)
    client.save_or_load(3)
    yycs.load_checkpoint(client, 3)
    loaded = yuchen.checkpoint(client, output, "party-follow-equipment-reloaded")
    if len([row for row in loaded["targets"] if row["name"] == name]) != 1:
        raise AutomationError("Partner map travel or normal reload lost or duplicated the partner")
    folder = output / "user-data/save" / SAVE_NAMESPACE / "rpg4"
    game = configparser.ConfigParser(); game.read(folder / "game.ini", encoding="utf-8-sig")
    saved = configparser.ConfigParser(); saved.read(folder / f"partner{game['state']['chr']}.ini", encoding="utf-8-sig")
    actors = [saved[section] for section in saved if saved[section].get("name") == name]
    if (len(actors) != 1 or actors[0].get("kind") != "3" or actors[0].get("handequip") != sword["file"]
            or actors[0].get("scriptfile") != filename or actors[0].get("deathscript")):
        raise AutomationError("Partner membership, equipment or actual saved binding differs")


def trade(client, output, resource, args):
    state = yycs.idle(client)
    name = "于掌柜" if "map_019_" in state["map"] else "曹铁匠" if args.option == 2 else "梁掌柜"
    filename, cost = ("goods-w00-青铜剑.ini", 300) if name == "曹铁匠" else ("goods-m00-金花.ini", 140)
    actor = next(row for row in state["targets"] if row["name"] == name)
    point = actor["position"]
    yuemeier.approach_target(client, output, resource, (point["x"], point["y"]), magic_file=combat_magic(client))
    before = yuchen.checkpoint(client, output, "medicine-shop-before")
    def transaction(shop):
        offered = next(row for row in shop["shop"] if row["file"] == filename)
        client.buy(offered["slot"])
        bought = yuchen.checkpoint(client, output, "medicine-shop-bought")
        count_before = sum(row["quantity"] for row in before["inventory"] if row["file"] == offered["file"])
        if (bought["player"]["money"] != before["player"]["money"] - cost
                or sum(row["quantity"] for row in bought["inventory"] if row["file"] == offered["file"]) != count_before + 1):
            raise AutomationError("Actual purchase did not add one item for its defined price")
        item = next(row for row in bought["inventory"] if row["file"] == offered["file"])
        client.sell(item["slot"])
        sold = yuchen.checkpoint(client, output, "medicine-shop-sold")
        if (sold["player"]["money"] != bought["player"]["money"] + cost // 2
                or sum(row["quantity"] for row in sold["inventory"] if row["file"] == offered["file"]) != count_before):
            raise AutomationError("Actual sale did not remove one item for its defined half-price")
        client.buy(offered["slot"])
        yuchen.checkpoint(client, output, "medicine-shop-retained-one-for-drug-test")
    shop = yycs.interact_named(client, output, resource, name, shop=True)
    transaction(shop)
    client.ui("Cancel")
    yycs.idle(client)
    script = "清平乡铁匠对话.txt" if name == "曹铁匠" else "药店老板.txt"
    yuchen.verify_script(output, resource, f"script/map/{state['map'].removesuffix('.map')}/{script}")
    client.save_or_load(4)
    yuchen.checkpoint(client, output, "medicine-shop-closed-slot4")


def partner_drug(client, output, resource, args):
    before = yuchen.checkpoint(client, output, "su-drug-before-native-injury")
    partner = next(row for row in before["targets"] if row["name"] == "苏莹莹")
    if args.option == 0:
        drug = next(row for row in before["inventory"] if row["file"] == "goods-m00-金花.ini")
        client.act("UseItem", generation=before["generation"], slot=drug["slot"])
        after = yuchen.checkpoint(client, output, "su-drug-full-life-boundary")
        healed = next(row for row in after["targets"] if row["name"] == "苏莹莹")
        if (partner["life"] != 629 or healed["life"] != 629
                or sum(row["quantity"] for row in after["inventory"] if row["file"] == drug["file"]) != drug["quantity"] - 1):
            raise AutomationError("Native full-life Su medicine boundary or item consumption differs")
        yuchen.write_json(output / "partner-drug-proof.json", dict(status="passed", companion="苏莹莹",
            beforeLife=629, afterLife=629, fullLifeBoundary=True, definedEffect=70,
            nativeInvincibility=after.get("cheatInvincibilityEnabled"), actualBattleInjury=False, saveBytesEdited=False))
        client.save_or_load(5)
        return
    if before["map"] == "map_018_连接地图.map":
        travel(client, output, resource, "map_017_连接地图.map", 1)
        travel(client, output, resource, "map_023_连接地图.map", 3)
        travel(client, output, resource, "map_024_倚天山.map", 1)
    client.move(14, 36, running=True, timeout=30)
    client.move(22, 52, running=True, timeout=30)
    injured = client.wait_until(lambda state: any(row["name"] == "苏莹莹" and 0 < row["life"] < partner["life"]
                                                for row in state["targets"]), timeout=35,
                                description="actual level21 bat damage to recruited Su")
    yuchen.checkpoint(client, output, "su-drug-native-battle-injury")
    client.move(27, 22, running=True, timeout=30)
    state = yuchen.checkpoint(client, output, "su-drug-before-use")
    actor = next(row for row in state["targets"] if row["name"] == "苏莹莹")
    if not 0 < actor["life"] < partner["life"]:
        raise AutomationError("Su did not retain a real battle life deficit")
    drug = next(row for row in state["inventory"] if row["file"] == "goods-m00-金花.ini")
    client.act("UseItem", generation=state["generation"], slot=drug["slot"])
    after = yuchen.checkpoint(client, output, "su-drug-actual-healed")
    healed = next(row for row in after["targets"] if row["name"] == "苏莹莹")
    if (healed["life"] != min(partner["life"], actor["life"] + 70)
            or sum(row["quantity"] for row in after["inventory"] if row["file"] == drug["file"]) != drug["quantity"] - 1):
        raise AutomationError("Native drug did not consume one and apply its 70-life partner effect")
    yuchen.write_json(output / "partner-drug-proof.json", dict(status="passed", beforeLife=actor["life"],
        afterLife=healed["life"], definedEffect=70, actualBattleInjury=partner["life"] - next(
            row["life"] for row in injured["targets"] if row["name"] == "苏莹莹"),
        nativeInvincibility=after.get("cheatInvincibilityEnabled"), saveBytesEdited=False))
    client.save_or_load(5)


def dog_drug(client, output, resource, args):
    talk(client, output, resource, "金花")
    client.move(14, 36, running=True, timeout=30)
    client.wait_until(lambda state: any(row["name"] == "狗肉" and 0 < row["life"] < 1205
                                       for row in state["targets"]), timeout=35,
                      description="actual bat injury before partner medicine")
    before = yuchen.checkpoint(client, output, "dog-drug-before-use")
    actor = next(row for row in before["targets"] if row["name"] == "狗肉")
    drug = next(row for row in before["inventory"] if row["file"] == "goods-m00-金花.ini")
    client.act("UseItem", generation=before["generation"], slot=drug["slot"])
    after = yuchen.checkpoint(client, output, "dog-drug-after-use")
    healed = next(row for row in after["targets"] if row["name"] == "狗肉")
    if (healed["life"] != min(1205, actor["life"] + 70)
            or sum(row["quantity"] for row in after["inventory"] if row["file"] == drug["file"]) != drug["quantity"] - 1):
        raise AutomationError("Dog medicine failed its actual 70-life effect or one-item consumption")
    yuchen.write_json(output / "partner-drug-proof.json", dict(status="passed", companion="狗肉",
        beforeLife=actor["life"], afterLife=healed["life"], definedEffect=70,
        nativeInvincibility=after.get("cheatInvincibilityEnabled"), actualBattleInjury=True, saveBytesEdited=False))
    client.save_or_load(5)


def caps(client, output, resource, args):
    import run_xinyue_gameplay as xinyue
    xinyue.checkpoint = yuchen.checkpoint
    xinyue.VARIABLES = yycs.VARIABLES
    filename = "player-magic-魂牵梦绕.ini"
    xinyue.assist_magic(client, output, filename, 10)
    client.assign_practice(next(row["slot"] for row in client.observe()["magic"] if row["file"] == filename))
    client.open_menu("System")
    client.activate("options")
    client.activate("cheat-settings")
    client.activate("increase-player-level")
    client.activate("increase-magic-level")
    state = yuchen.checkpoint(client, output, "native-maximum-level-extra-upgrade")
    if state["player"]["level"] != 80 or next(row["level"] for row in state["magic"] if row["file"] == filename) != 10:
        raise AutomationError("Actual player or magic exceeded its defined level cap")
    for _ in range(5):
        if client.observe().get("worldInput"):
            break
        client.ui("Cancel")
    client.assign_magic(next(row["slot"] for row in client.observe()["magic"] if row["file"] == filename), 0)
    invincibility(client, False)
    actor = next(row for row in client.observe()["targets"] if row.get("hostile") and yycs.npc_attackable(row))
    point = actor["position"]
    yuemeier.approach_target(client, output, resource, (point["x"], point["y"]), magic_file=filename)
    before = yuchen.checkpoint(client, output, "maximum-magic-effective-battle-before")
    yycs.fight_named(client, output, resource, actor["name"], use_magic=True, single_target=True,
                     target_id=actor["id"], magic_file=filename)
    after = yuchen.checkpoint(client, output, "maximum-magic-effective-battle-after")
    if before["player"]["mana"] - after["player"]["mana"] < 60:
        raise AutomationError("Effective learned Soul magic did not consume its defined mana without invincibility")
    invincibility(client, True)


def invincibility(client, enabled):
    if not any(row["name"] == "options" for row in client.observe().get("ui", [])):
        client.open_menu("System")
    client.activate("options")
    client.activate("cheat-settings")
    if client.observe().get("cheatInvincibilityEnabled") != enabled:
        client.activate("invincibility")
    for _ in range(5):
        if client.observe().get("worldInput"):
            break
        client.ui("Cancel")
    if client.observe().get("cheatInvincibilityEnabled") != enabled:
        raise AutomationError("Native invincibility menu did not apply the requested process state")


def wind_effect(client, output, resource, args):
    invincibility(client, False)
    learned = next(row for row in client.observe()["magic"] if row["file"] == "001杀意.ini")
    client.assign_magic(learned["slot"], 0)
    client.move(11, 34, running=True, timeout=30)
    client.move(11, 24, running=True, timeout=30)
    before = yuchen.checkpoint(client, output, "wind-effect-before")
    client.act("CastSkill", generation=before["generation"], slot=0)
    after = yuchen.checkpoint(client, output, "wind-effect-after")
    if (before.get("cheatInvincibilityEnabled") or before["player"]["mana"] - after["player"]["mana"] != 32
            or after["player"]["thew"] <= before["player"]["thew"]
            or before["player"]["life"] != after["player"]["life"]):
        raise AutomationError("Actual Wind effect or its defined 32-mana and zero-life cost differs")
    yuchen.write_json(output / "wind-effect-proof.json", dict(status="passed", before=before["player"],
        after=after["player"], nativeInvincibility=False, definedEffect=107, definedManaCost=32,
        noDefinedLifeCost=True, introLifeCostIntentUnconfirmed=True, saveBytesEdited=False))
    invincibility(client, True)


def movement(client, output, resource, args):
    if client.observe()["player"]["position"] != dict(x=11, y=24):
        client.move(11, 24, running=True, timeout=30)
    before = yuchen.checkpoint(client, output, "movement-symbol-before")
    item = next(row for row in before["inventory"] if row["file"] == "0神行太保.ini")
    site = next(row for row in yycs.inventory(resource)["choices"] if row["path"] == "script/goods/神行太保.txt")
    for option, point, action in ((0, (11, 34), 3), (1, (11, 24), 2)):
        state = client.observe()
        request = client.submit("UseItem", generation=state["generation"], slot=item["slot"])
        client.wait_until(lambda value: bool(value.get("choices")), description="normal movement symbol choice")
        yycs.choose_site(client, output, resource, site["id"], option)
        client.wait_action(request, timeout=3)
        client.save_or_load(option)
        folder = output / "user-data/save" / SAVE_NAMESPACE / f"rpg{option + 1}"
        game = configparser.ConfigParser(); game.read(folder / "game.ini", encoding="utf-8-sig")
        player = configparser.ConfigParser(); player.read(folder / f"player{game['state']['chr']}.ini", encoding="utf-8-sig")
        if player.getint("init", "WalkIsRun") != 1 - option:
            raise AutomationError("Movement symbol did not save its actual run/walk state")
        state = client.observe()
        request = client.submit("MoveTo", generation=state["generation"], x=point[0], y=point[1], running=False, timeoutMs=30000)
        client.wait_until(lambda value: value["player"]["action"] in (action, 22 if action == 3 else 21), timeout=10,
                          description="native symbol-controlled running/walking")
        yuchen.checkpoint(client, output, f"movement-symbol-{option}-actual-action")
        client.wait_action(request, timeout=32)
        after = yuchen.checkpoint(client, output, f"movement-symbol-{option}-after")
        if after["inventory"] != before["inventory"] or after["player"]["money"] != before["player"]["money"]:
            raise AutomationError("Movement symbol consumed an item or money")


def poison_boundary(client, output, resource, args):
    before = yuchen.checkpoint(client, output, "poison-boundary-before")
    target = next(row for row in before["targets"] if row["name"] == "章大爷")
    try:
        client.interact(target["id"], timeout=15)
    except (AutomationError, TimeoutError) as error:
        after = yuchen.checkpoint(client, output, "poison-boundary-native-result")
        if after["variables"] != before["variables"] or after["inventory"] != before["inventory"]:
            raise AutomationError("Boundary candidate changed plot or goods despite the failed interaction")
        yuchen.write_json(output / "poison-boundary-proof.json", dict(position=target["position"],
            actualPlayerAttempt=True, nativeError=str(error), triggerPassed=False, definitionIntentUnconfirmed=True))
        return
    yycs.idle(client)
    raise AutomationError("Poison candidate actually reached; add its observed player route")


def shield_setup(client, output, resource, args):
    for destination, trap in (("map_021_油菜花地.map", 1), ("map_024_倚天山.map", 1),
                              ("map_023_连接地图.map", 3), ("map_017_连接地图.map", 2)):
        travel(client, output, resource, destination, trap)
    main_start(client, output, resource, args)
    manor_battles(client, output, resource, args)
    martial_letter(client, output, resource, args)
    args.option = 0
    zhao_choice(client, output, resource, args)
    travel(client, output, resource, "map_015_藏剑山庄.map", 2)
    travel(client, output, resource, "map_016_剑气峰.map", 2)
    talk(client, output, resource, "鬼道人")
    client.save_or_load(6)
    yuchen.checkpoint(client, output, "shield-final-boss-normal-source-slot6")


def shield_effect(client, output, resource, args):
    if client.observe()["map"] == "map_016_剑气峰.map":
        travel(client, output, resource, "map_015_藏剑山庄.map", 1)
    learned = next(row for row in client.observe()["magic"] if row["file"] == "player-magic-金钟罩.ini")
    client.assign_magic(learned["slot"], 0)
    invincibility(client, False)
    before = yuchen.checkpoint(client, output, "shield-effect-before")
    client.act("CastSkill", generation=before["generation"], slot=0)
    client.save_or_load(0)
    cast = yuchen.checkpoint(client, output, "shield-effect-cast-slot0")

    def saved_shields(slot):
        saved = configparser.ConfigParser(interpolation=None, strict=False)
        saved.read(output / "user-data/save" / SAVE_NAMESPACE / f"rpg{slot + 1}/proj.ini", encoding="utf-8-sig")
        return [dict(section=section, effect=saved.getint(section, "Damage"),
                     lifetime=saved.getint(section, "LifeTime"), wait=saved.getint(section, "WaitTime"),
                     doing=saved.getint(section, "Doing"))
                for section in saved.sections() if saved.get(section, "FileName", fallback="") == "player-magic-金钟罩.ini"
                and not saved.getint(section, "Vanishing", fallback=0)]

    shields = saved_shields(0)
    if (before.get("cheatInvincibilityEnabled") or before["player"]["mana"] - cast["player"]["mana"] != 6
            or not any(row["effect"] == 10 and row["lifetime"] + row["wait"] > 0 for row in shields)):
        raise AutomationError("Golden Bell did not create its ten-point damage reduction with six-mana cost")
    yycs.load_checkpoint(client, 0)
    client.save_or_load(1)
    restored = saved_shields(1)
    if not any(row["effect"] == 10 and row["lifetime"] + row["wait"] > 0 for row in restored):
        raise AutomationError("Golden Bell did not retain its real shield after normal save/load")
    loaded = yuchen.checkpoint(client, output, "shield-effect-reloaded")
    invincibility(client, True)
    yycs.load_checkpoint(client, 6)
    client.assign_magic(next(row["slot"] for row in client.observe()["magic"]
                             if row["file"] == "player-magic-金钟罩.ini"), 0)
    battle_before = yuchen.checkpoint(client, output, "shield-effect-battle-before")
    client.act("CastSkill", generation=battle_before["generation"], slot=0)
    client.save_or_load(2)
    active = saved_shields(2)
    protected_cast = yuchen.checkpoint(client, output, "shield-effect-protected-cast")
    if not active or protected_cast["player"]["mana"] != battle_before["player"]["mana"]:
        raise AutomationError("Native invincibility cast did not preserve mana and create the actual reduction effect")
    invincibility(client, False)
    cast_battle = yuchen.checkpoint(client, output, "shield-effect-battle-cast")
    player = configparser.ConfigParser(interpolation=None, strict=False)
    folder = output / "user-data/save" / SAVE_NAMESPACE / "rpg3"
    game = configparser.ConfigParser(); game.read(folder / "game.ini", encoding="utf-8-sig")
    player.read(folder / f"player{game['state']['chr']}.ini", encoding="utf-8-sig")
    if any(player.get("init", field, fallback="") for field in
           ("HandEquip", "BodyEquip", "HeadEquip", "FootEquip", "BackEquip", "WristEquip", "NeckEquip")):
        raise AutomationError("Shield damage comparison requires the checked unequipped player")
    unshielded = 3435 - player.getint("init", "Defend")
    damage = int((unshielded - 10) * 0.8 + 0.5)  # Player::addLife applies DAMAGE_RATE after reduction.
    if damage <= 5:
        raise AutomationError("The shield's reduction is masked by the native minimum damage")

    def sample_primary_hit(initial, expected, name):
        # Other boss skills can reach minimum damage; retain every sample and require a primary hit.
        previous = initial
        samples = []
        for _ in range(100):
            current = client.observe()
            loss = previous["player"]["life"] - current["player"]["life"]
            samples.append(dict(before=previous["player"]["life"], after=current["player"]["life"], loss=loss,
                                frame=current["frame"], nativeInvincibility=current["cheatInvincibilityEnabled"]))
            if loss >= expected and loss % expected == 0:
                yuchen.write_json(output / f"shield-effect-{name}-samples.json", samples)
                return previous, current, loss
            previous = current
            time.sleep(0.03)
        yuchen.write_json(output / f"shield-effect-{name}-samples.json", samples)
        raise AutomationError(f"Native {name} primary hit was not observed")

    hit_before, first_hit, observed = sample_primary_hit(cast_battle, damage, "shielded")
    client.open_menu("System")
    invincibility(client, True)
    yycs.load_checkpoint(client, 6)
    invincibility(client, False)
    baseline = yuchen.checkpoint(client, output, "shield-effect-unshielded-before")
    baseline_damage = int(unshielded * 0.8 + 0.5)
    baseline_before, baseline_hit, baseline_loss = sample_primary_hit(baseline, baseline_damage, "unshielded")
    client.open_menu("System")
    yuchen.write_json(output / "shield-effect-proof.json", dict(status="passed", nativeInvincibility=False,
        before=before["player"], cast=cast["player"], loaded=loaded["player"], hitBefore=hit_before["player"], firstHit=first_hit["player"],
        castShields=shields, restoredShields=restored, battleShields=active, reductionPerHit=10,
        expectedBossHit=damage, expectedUnshieldedBossHit=int(unshielded * 0.8 + 0.5),
        playerDamageRate=0.8, actualLifeLoss=observed, actualUnshieldedLifeLoss=baseline_loss,
        shieldedHitMultiple=observed // damage, unshieldedHitMultiple=baseline_loss // baseline_damage,
        baselineBefore=baseline_before["player"], baselineAfter=baseline_hit["player"], battleCastingInvincibility=True,
        battleCastingManaCost=0, safeCastingInvincibility=False, safeCastingManaCost=6, saveBytesEdited=False))
    invincibility(client, True)


def closed_exit(client, output, resource, args):
    trap = {"map_014_连接地图.map": 1, "map_022_清平乡.map": 2}[client.observe()["map"]]
    yuemeier.blocked_exit(client, output, resource, trap, "actual-empty-bound-exit",
                         magic_file=combat_magic(client))
    client.save_or_load(0)


def opening_magic(client, output, resource, args):
    import run_xinyue_gameplay as xinyue
    xinyue.checkpoint = yuchen.checkpoint
    xinyue.VARIABLES = yycs.VARIABLES
    filename = next(row["file"] for row in client.observe()["magic"]
                    if row["file"] in ("player-magic-烈火情天.ini", "player-magic-逆转心经.ini"))
    if args.option == 1:
        xinyue.assist_magic(client, output, filename, 10)
    client.assign_magic(next(row["slot"] for row in client.observe()["magic"] if row["file"] == filename), 0)
    yuemeier.approach_target(client, output, resource, (6, 46), magic_file=filename)
    invincibility(client, False)
    before = yuchen.checkpoint(client, output, "opening-magic-before")
    level = next(row["level"] for row in before["magic"] if row["file"] == filename)
    yycs.fight_named(client, output, resource, "红衣杀手lv1", single_target=True, use_magic=True, magic_file=filename)
    after = yuchen.checkpoint(client, output, "opening-magic-after")
    cost = before["player"]["mana"] - after["player"]["mana"]
    if cost < level or cost % level:
        raise AutomationError("Actual modified starting magic did not consume its one-mana-per-level fee")
    yuchen.write_json(output / "opening-magic-proof.json", dict(status="passed", file=filename, level=level,
        definedManaCost=level, actualManaCost=cost, before=before["player"], after=after["player"],
        nativeInvincibility=False, actualNormalBattle=True, saveBytesEdited=False))
    invincibility(client, True)


def corpse_read(client, output, resource, args):
    travel(client, output, resource, "map_025_摘星楼.map", 1)
    yuemeier.approach_exit(client, output, resource, 0, destination=(31, 103),
                          magic_file=combat_magic(client), excluded_traps=(2,))
    before = yuchen.checkpoint(client, output, "corpse-letter-before")
    interact_choice(client, output, resource, "尸体", "尸体.txt", 0)
    after = yuchen.checkpoint(client, output, "corpse-letter-read")
    if after["inventory"] != before["inventory"] or after["variables"].get("fs") != before["variables"].get("fs"):
        raise AutomationError("Optional corpse letter changed goods or the checked testimony condition")
    client.save_or_load(2)


def mayor_gate(client, output, resource, args):
    before = yuchen.checkpoint(client, output, "mayor-physical-gate-before")
    if before["variables"].get("wbt") != "1":
        raise AutomationError("Gate test requires the preserved one-deed source")
    result = dict(walkingReached=False, jumpingReached=False, plotVariablesEdited=False)
    try:
        client.move(10, 38, running=True, timeout=15)
        result["walkingReached"] = True
    except (AutomationError, TimeoutError) as error:
        result["walkingError"] = str(error)
    yuchen.checkpoint(client, output, "mayor-physical-gate-walk-result")
    if not result["walkingReached"]:
        state = client.observe()
        try:
            client.act("JumpTo", generation=state["generation"], x=15, y=31, timeout=15)
            yuchen.checkpoint(client, output, "mayor-physical-gate-jump-result")
            talk(client, output, resource, "郝村长", choice=(next(site["id"] for site in yycs.inventory(resource)["choices"]
                 if site["path"].endswith("map_022_清平乡/郝村长对话.txt")), 1))
            result["jumpingReached"] = True
        except (AutomationError, TimeoutError) as error:
            result["jumpingError"] = str(error)
    yuchen.write_json(output / "mayor-physical-gate-proof.json", result)
    client.save_or_load(0)


def qingxin_effect(client, output, resource, args):
    invincibility(client, False)
    learned = next(row for row in client.observe()["magic"] if row["file"] == "player-magic-清心咒.ini")
    client.assign_magic(learned["slot"], 0)
    client.move(14, 36, running=True, timeout=30)
    before = yuchen.checkpoint(client, output, "qingxin-effect-before")
    dog = next(row for row in before["targets"] if row["name"] == "狗肉")
    if not 0 < dog["life"] < 1205:
        raise AutomationError("Qingxin effect requires a living dog with actual battle injury")
    client.act("CastSkill", generation=before["generation"], slot=0)
    samples = []
    for _ in range(30):
        samples.append(client.observe())
        time.sleep(0.1)
    after = yuchen.checkpoint(client, output, "qingxin-effect-after")
    if (before.get("cheatInvincibilityEnabled")
            or before["player"]["mana"] - min(row["player"]["mana"] for row in samples) != 6
            or max(actor["life"] for row in samples for actor in row["targets"] if actor["name"] == "狗肉") <= dog["life"]):
        raise AutomationError("Learned Qingxin failed its actual healing or six-mana fee")
    yuchen.write_json(output / "qingxin-effect-proof.json", dict(status="passed", before=before["player"],
        after=after["player"], beforeDogLife=dog["life"], samples=samples, nativeInvincibility=False, definedRangeLife=50,
        definedManaCost=6, saveBytesEdited=False))
    invincibility(client, True)


def dog_recruit(client, output, resource, args):
    before = yuchen.checkpoint(client, output, "dog-with-chicken-before")
    chicken = sum(row["quantity"] for row in before["inventory"] if row["file"] == "0烧鸡.ini")
    if chicken < 1:
        raise AutomationError("Dog recruitment requires an actually obtained chicken")
    interact_choice(client, output, resource, "狗肉", "狗肉对话.txt", 1)
    refused = yuchen.checkpoint(client, output, "dog-chicken-refused")
    if refused["inventory"] != before["inventory"]:
        raise AutomationError("Refused dog recruitment consumed chicken")
    interact_choice(client, output, resource, "狗肉", "狗肉对话.txt", 0)
    recruited = yuchen.checkpoint(client, output, "dog-chicken-recruited")
    if (sum(row["quantity"] for row in recruited["inventory"] if row["file"] == "0烧鸡.ini") != chicken - 1
            or not any(row["name"].startswith("partner-head-") for row in recruited["ui"])):
        raise AutomationError("Dog recruitment did not consume one chicken and create a partner")
    client.save_or_load(2)


def village_start(client, output, resource, args):
    travel(client, output, resource, "map_022_清平乡.map", 2)
    talk(client, output, resource, "王捕头")
    before = yuchen.checkpoint(client, output, "village-before-good-deeds")
    interact_choice(client, output, resource, "孙敏", "孙敏对话.txt", 0)
    insufficient = yuchen.checkpoint(client, output, "sun-insufficient-400")
    if insufficient["variables"].get("sm") or insufficient["variables"].get("wbt") or insufficient["player"]["money"] != 400:
        raise AutomationError("Sun's insufficient money advanced a good deed or charged money")
    interact_choice(client, output, resource, "孙敏", "孙敏对话.txt", 1)
    interact_choice(client, output, resource, "毛头", "毛头对话.txt", 1)
    refused = yuchen.checkpoint(client, output, "maotou-refused")
    if refused["variables"].get("mtls"):
        raise AutomationError("Maotou refusal advanced reconciliation")
    interact_choice(client, output, resource, "毛头", "毛头对话.txt", 0)
    talk(client, output, resource, "刘氏")
    told = yuchen.checkpoint(client, output, "liushi-reconciliation-refused")
    if told["variables"].get("mtls") != "2":
        raise AutomationError("Liu's answer did not update Maotou's actual task")
    talk(client, output, resource, "毛头")
    first = yuchen.checkpoint(client, output, "maotou-first-good-deed")
    if first["variables"].get("mtls") != "3" or first["variables"].get("wbt") != "1":
        raise AutomationError("Maotou completion did not grant exactly one good deed")
    talk(client, output, resource, "毛头")
    talk(client, output, resource, "王捕头")
    blocked = yuchen.checkpoint(client, output, "village-one-good-deed-guard-blocked")
    guard = next(row for row in blocked["targets"] if row["name"] == "王捕头")
    if blocked["variables"].get("wbt") != "1" or guard["position"] != dict(x=16, y=33):
        raise AutomationError("Guard opened before two actual good deeds or reward repeated")
    client.save_or_load(3)
    yuchen.checkpoint(client, output, "village-one-good-deed-slot3")


def village_finish(client, output, resource, args):
    before = yuchen.checkpoint(client, output, "sun-funded-before")
    interact_choice(client, output, resource, "孙敏", "孙敏对话.txt", 0)
    donated = yuchen.checkpoint(client, output, "sun-funded-donation-refunded")
    if (donated["variables"].get("sm") != "1" or donated["variables"].get("wbt") != "2"
            or donated["player"]["money"] != before["player"]["money"]):
        raise AutomationError("Sun completion did not refund 500 or grant one additional deed")
    talk(client, output, resource, "孙敏")
    repeated = yuchen.checkpoint(client, output, "sun-funded-no-repeat")
    if repeated["variables"].get("wbt") != "2" or repeated["player"]["money"] != donated["player"]["money"]:
        raise AutomationError("Sun completion repeated its deed or refund")
    talk(client, output, resource, "王捕头")
    opened = yuchen.checkpoint(client, output, "village-two-deeds-guard-opened")
    if opened["variables"].get("wbt") != "3":
        raise AutomationError("Guard did not accept two completed good deeds")
    talk(client, output, resource, "飞天")
    prior = yuchen.checkpoint(client, output, "feitian-before-task")
    if any(row["hostile"] for row in prior["targets"] if row["name"] == "飞天"):
        raise AutomationError("Feitian became hostile before the mayor's task")
    interact_choice(client, output, resource, "郝村长", "郝村长对话.txt", 1)
    refused = yuchen.checkpoint(client, output, "mayor-refused")
    if refused["variables"].get("hcz"):
        raise AutomationError("Mayor refusal accepted the task")
    interact_choice(client, output, resource, "郝村长", "郝村长对话.txt", 0)
    talk(client, output, resource, "飞天")
    fight_actor(client, output, resource, "飞天", "hcz", "2")
    talk(client, output, resource, "郝村长")
    reward = yuchen.checkpoint(client, output, "mayor-golden-bell-reward")
    if reward["variables"].get("hcz") != "3" or not any(row["file"] == "player-magic-金钟罩.ini" for row in reward["magic"]):
        raise AutomationError("Mayor did not teach Golden Bell after Feitian's native death")
    talk(client, output, resource, "郝村长")
    repeat = yuchen.checkpoint(client, output, "mayor-golden-bell-no-repeat")
    if repeat["magic"] != reward["magic"] or repeat["player"]["money"] != reward["player"]["money"]:
        raise AutomationError("Mayor repeated a reward")
    client.save_or_load(4)
    yuchen.checkpoint(client, output, "village-completed-slot4")


def grass_message(client, output, resource, args):
    bridge(client, output, resource)
    travel(client, output, resource, "map_023_连接地图.map", 3)
    travel(client, output, resource, "map_024_倚天山.map", 1)
    travel(client, output, resource, "map_021_油菜花地.map", 1)
    talk(client, output, resource, "于富贵")
    state = yuchen.checkpoint(client, output, "yu-message-delivered")
    if state["variables"].get("lpp") != "2":
        raise AutomationError("Yu Fugui did not receive the accepted message")
    talk(client, output, resource, "于富贵")
    repeated = yuchen.checkpoint(client, output, "yu-message-repeated")
    if repeated["inventory"] != state["inventory"] or repeated["player"]["money"] != state["player"]["money"]:
        raise AutomationError("Yu Fugui repeated a reward")
    client.save_or_load(2)
    yuchen.checkpoint(client, output, "yu-message-slot2")


def grass_reward(client, output, resource, args):
    for destination, trap in (("map_024_倚天山.map", 1), ("map_023_连接地图.map", 3),
                              ("map_017_连接地图.map", 2), ("map_018_连接地图.map", 2),
                              ("map_019_寒波谷.map", 2)):
        travel(client, output, resource, destination, trap)
    before = yuchen.checkpoint(client, output, "granny-before-reward")
    talk(client, output, resource, "刘婆婆")
    reward = yuchen.checkpoint(client, output, "granny-grass-reward")
    if (reward["variables"].get("lpp") != "3" or reward["variables"].get("wmh") != "2"
            or sum(row["quantity"] for row in reward["inventory"] if row["file"] == "0银丝草.ini") != 1):
        raise AutomationError("Granny message completion did not grant one silver grass")
    talk(client, output, resource, "刘婆婆")
    repeated = yuchen.checkpoint(client, output, "granny-reward-not-repeated")
    if repeated["inventory"] != reward["inventory"] or repeated["player"]["money"] != reward["player"]["money"]:
        raise AutomationError("Granny completion repeated reward")
    travel(client, output, resource, "map_020_樱花谷.map", 2)
    talk(client, output, resource, "温名华")
    learned = yuchen.checkpoint(client, output, "wen-grass-delivered-magic-learned")
    if (learned["variables"].get("wmh") != "3"
            or any(row["file"] == "0银丝草.ini" for row in learned["inventory"])
            or not any(row["file"] == "001杀意.ini" for row in learned["magic"])):
        raise AutomationError("Silver grass delivery did not consume its item and teach the actual magic")
    talk(client, output, resource, "温名华")
    after = yuchen.checkpoint(client, output, "wen-completed-not-repeated")
    if after["inventory"] != learned["inventory"] or after["magic"] != learned["magic"]:
        raise AutomationError("Wen completion repeated its reward")
    client.save_or_load(3)
    saved = yuchen.checkpoint(client, output, "grass-completed-slot3")
    yycs.load_checkpoint(client, 3)
    loaded = yuchen.checkpoint(client, output, "grass-completed-reloaded")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if saved[key] != loaded[key]:
            raise AutomationError(f"Completed grass task save/load changed {key}")


def opening(client, output, resource, difficulty, gender):
    state = client.observe()
    if state["scene"] == "Title":
        yuchen.checkpoint(client, output, "01-title")
        client.activate("new-game")
    client.wait_until(lambda state: bool(state.get("choices")), timeout=120,
                      description="normal difficulty choice")
    sites = [site for site in yycs.inventory(resource)["choices"]
             if site["path"] == "script/map/map_020_樱花谷/begin.txt"]
    yycs.choose_site(client, output, resource, sites[0]["id"],
                     0 if difficulty == "hard" else 1,
                     remaining=((sites[1]["id"], gender),))
    state = yuchen.checkpoint(client, output, "02-normal-opening")
    if (state["map"] != "map_020_樱花谷.map" or state["variables"].get("XZ") != str(gender)
            or state["variables"].get("Level") != ("0" if difficulty == "hard" else "1")
            or not any(row["file"] == "0神行太保.ini" for row in state["inventory"])):
        raise AutomationError("Yuchen II normal opening identity, choices or reward differs")
    client.save_or_load(0)
    before = yuchen.checkpoint(client, output, "03-opening-slot0")
    yycs.load_checkpoint(client, 0)
    after = yuchen.checkpoint(client, output, "04-opening-reloaded")
    for key in ("map", "variables", "inventory", "magic", "player"):
        if before[key] != after[key]:
            raise AutomationError(f"Normal opening save/load changed {key}")
    yuchen.write_json(output / "opening-proof.json", dict(difficulty=difficulty, gender=gender,
        levelFile=state["player"]["levelFile"], player=state["player"], variables=state["variables"],
        expectedLevelFile=f"level-{difficulty}.ini", normalMenuSaveReload=True,
        cheatAssisted=False, saveBytesEdited=False))
    if state["player"]["levelFile"] != f"level-{difficulty}.ini":
        raise AutomationError("Gender template overwrote the chosen difficulty")


def main():
    for module in (yuchen, yuemeier):
        module.RESOURCE_ID = RESOURCE_ID
        module.RESOURCE_DIRECTORY = RESOURCE_DIRECTORY
        module.SAVE_NAMESPACE = SAVE_NAMESPACE
    yuchen.PIPE_PREFIX = "yuchen2-"
    yuchen.ADDITIONAL_ROUTE_SOURCES = ("run_yuchen2_gameplay.py", "run_xinyue_gameplay.py", "run_xjxqy_gameplay.py")
    yuchen.ADDITIONAL_VARIABLES = ("EvilVal", "Event", "Result", "SenseVal", "sny", "wlt", "wmh", "lpp",
                                   "wgz", "zzz", "wtp", "fs", "swyl", "sm", "wbt", "mtls", "hcz")
    yuchen.CUSTOM_ROUTES = {"early-tasks": early_tasks, "main-start": main_start,
                            "manor-start": manor_start, "manor-battles": manor_battles, "grass-message": grass_message,
                            "grass-reward": grass_reward, "martial-letter": martial_letter, "zhao-choice": zhao_choice,
                            "village-start": village_start, "village-finish": village_finish, "finale": finale,
                            "ghost-blocked": ghost_blocked, "manor-exit": manor_exit, "star-quest": star_quest,
                            "star-reward": star_reward, "boxes": boxes, "dog-recruit": dog_recruit,
                            "field-bridge-boxes": field_bridge_boxes, "su-recruit": su_recruit, "party-manage": party_manage,
                            "trade": trade, "partner-drug": partner_drug, "wind-effect": wind_effect,
                            "qingxin-effect": qingxin_effect, "dog-drug": dog_drug, "caps": caps,
                            "movement": movement, "poison-boundary": poison_boundary,
                            "shield-setup": shield_setup, "shield-effect": shield_effect,
                            "closed-exit": closed_exit, "opening-magic": opening_magic,
                            "corpse-read": corpse_read, "mayor-gate": mayor_gate}
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--route", choices=("opening", "inventory", "observe", "exit", *yuchen.CUSTOM_ROUTES), default="opening")
    parser.parse_known_args()
    yuchen.opening = opening
    yuchen.main()


if __name__ == "__main__":
    main()
