"""Check real variable-length opening choices and referenced spell identities."""
from pathlib import Path
import configparser
import tempfile
from unittest.mock import Mock, patch

import run_xiaoxiang_gameplay as route
import run_jxqy2_mainline as movement
from gameplay_automation import AutomationError
from run_xiaoxiang_gameplay import RESOURCE_DIRECTORY, SPELLS
from run_yycs_gameplay import inventory, reachable_trap, trap_points


def main():
    resource = Path(__file__).resolve().parents[1] / "assets" / RESOURCE_DIRECTORY
    catalog = inventory(resource)
    choices = {row["message"]: row for row in catalog["choices"]}
    assert [row["text"] for row in choices["请选择游戏的难度："]["options"]] == ["普通", "困难", "专家"]
    assert [row["text"] for row in choices["请选择武功的类型："]["options"]] == ["远程", "肉搏", "群杀", "爆发"]
    assert len(choices["请选择要增加的属性："]["options"]) == 7
    assert choices["请选择游戏的难度："]["api"] == "chooseex"
    for spells in SPELLS:
        for filename in spells:
            assert (resource / "ini/magic" / filename).is_file(), filename
    map_name = "狂沙镇-铁门寨.map"
    isolated = reachable_trap(resource, map_name, 2, dict(x=8, y=84))
    connected = reachable_trap(resource, map_name, 2, dict(x=8, y=84),
                               avoid=trap_points(resource, map_name, 1))
    assert isolated == (4, 91) and connected[1] < 30
    assert reachable_trap(resource,"中都.map",None,dict(x=77,y=26),destination=(33,114)) == (33,114)
    guard_path = reachable_trap(resource,"临安城.map",None,dict(x=153,y=309),destination=(69,138),
                                with_path=True,avoid=trap_points(resource,"临安城.map",9))
    assert not set(guard_path) & set(trap_points(resource,"临安城.map",9))
    assert set(guard_path) & set(trap_points(resource,"临安城.map",1))
    assert not route.magic_ground_allows(resource, "中都.map", (12,185))
    assert route.magic_ground_allows(resource, "中都.map", (13,181))
    room_target = dict(id=8, name="房客男", attackable=True, position=dict(x=13,y=184))
    room_before = dict(generation=1, map="中都.map", player=dict(position=dict(x=12,y=185)), targets=[room_target])
    room_after = dict(room_before, player=dict(position=dict(x=13,y=185)))
    with patch.object(route,"idle",side_effect=[room_before,room_after]),patch.object(route,"walk_to") as moved:
        route.approach_target(Mock(),resource,room_target)
        assert route.magic_ground_allows(resource,"中都.map",moved.call_args.args[2])
    for map_name,start,goal in (("临安城.map",(154,109),(145,83)),("临安城.map",(154,109),(163,116)),
                                 ("凤池山庄.map",(54,248),(65,226))):
        assert reachable_trap(resource,map_name,None,dict(x=start[0],y=start[1]),destination=goal) == goal
    client = Mock()
    client.submit.return_value = 9
    client.request.return_value = dict(status="running", reason="")
    close = dict(generation=1, worldInput=True, map="test", player=dict(position=dict(x=4,y=5)))
    with patch.object(movement,"idle",return_value=close), patch.object(movement,"story_state",return_value=close):
        observed = movement.go(client,9,10,combat=False,stop_when=lambda s:s["player"]["position"]==dict(x=4,y=5))
    assert observed == close
    assert client.request.call_args_list[-1].args == ("CancelAction",)
    assert client.request.call_args_list[-1].kwargs == dict(actionId=9)
    adjacent = dict(generation=1,map="临安地下迷宫.map",player=dict(position=dict(x=77,y=134)),
                    targets=[dict(id=7,attackable=True,position=dict(x=76,y=135))])
    with patch.object(route,"idle",return_value=adjacent),patch.object(route,"walk_to") as walked:
        assert route.approach_target(Mock(),resource,adjacent["targets"][0]) == adjacent
        walked.assert_not_called()
    neutral = dict(adjacent,targets=[dict(adjacent["targets"][0],attackable=False,interactive=True)])
    with patch.object(route,"idle",return_value=neutral),patch.object(route,"walk_to") as walked:
        assert route.approach_target(Mock(),resource,neutral["targets"][0],attackable=False) == neutral
        walked.assert_not_called()
    obstructed = dict(adjacent,player=dict(position=dict(x=76,y=133)))
    with patch.object(route,"idle",side_effect=[obstructed,adjacent]),patch.object(route,"walk_to") as walked:
        route.approach_target(Mock(),resource,obstructed["targets"][0])
        assert walked.call_args.args[2] != (76,133)
    chest = dict(id=8,kind="object",name="宝箱",position=dict(x=9,y=0))
    distant = dict(generation=1,map="test",player=dict(position=dict(x=0,y=0)),targets=[chest])
    reached = dict(distant,player=dict(position=dict(x=8,y=0)))
    with patch.object(route,"idle",side_effect=[distant,reached]),patch.object(route,"walk_to") as walked, \
         patch.object(route,"reachable_trap",return_value=[(0,0),(8,0)]):
        route.approach_target(Mock(),resource,chest,attackable=False)
        assert walked.call_args.args[2] == (8,0)
    unreachable = dict(distant,targets=[dict(chest,attackable=True)])
    with patch.object(route,"idle",return_value=unreachable), patch.object(route,"magic_ground_allows",return_value=True), \
         patch.object(route,"reachable_trap",side_effect=AutomationError("No connected route")):
        try:
            route.approach_target(Mock(),resource,unreachable['targets'][0])
        except AutomationError as error:
            assert str(error).startswith("No reachable combat neighbor:")
        else:
            raise AssertionError("An unreachable target was accepted for combat")
    straight = dict(unreachable,player=dict(position=dict(x=6,y=0)))
    with patch.object(route,'idle',return_value=straight),patch.object(route,'walk_to') as moved, patch.object(route,'magic_ground_allows',return_value=True), \
         patch.object(route,'reachable_trap',return_value=[(9,0),(8,0),(7,0),(6,0)]):
        assert route.approach_target(Mock(),resource,straight['targets'][0]) == straight
        moved.assert_not_called()
    definitions = configparser.ConfigParser(interpolation=None,strict=False)
    definitions.read(resource / "ini/save/lamg.npc",encoding="utf-8-sig")
    assert [s for s in definitions.sections() if definitions[s].get("deathscript") == "火药炮.txt"] == [f"NPC{i:03}" for i in range(52,60)]
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        generated = root / "save" / route.SAVE_NAMESPACE / "game" / "贴身软甲Defend300LifeMax1000.ini"
        generated.parent.mkdir(parents=True)
        text = (resource / "ini/goods/贴身软甲.ini").read_text(encoding="utf-8-sig")
        text = text.replace("[Init]","[init]").replace("666>2000","1000").replace("166>500","300")
        generated.write_text(text,encoding="utf-8")
        assert route.table_reward_matches(resource,root,generated.name,{"贴身软甲.ini"})
        generated.write_text(text.replace("Defend=300","Defend=9999"),encoding="utf-8")
        assert not route.table_reward_matches(resource,root,generated.name,{"贴身软甲.ini"})
        sword = generated.with_name('goods-w15-莫邪剑Attack85Defend-2EffectType2.ini')
        text = (resource/'ini/goods/goods-w15-莫邪剑.ini').read_text(encoding='utf-8-sig')
        text = text.replace('[Init]','[init]').replace('50>150','85').replace('1>-50','-2').replace('1>3','2')
        sword.write_text(text,encoding='utf-8')
        assert route.table_reward_matches(resource,root,sword.name,{'goods-w15-莫邪剑.ini'})
        sword.write_text(text.replace('Defend=-2','Defend=-51'),encoding='utf-8')
        assert not route.table_reward_matches(resource,root,sword.name,{'goods-w15-莫邪剑.ini'})
    path = [(x,0) for x in range(18)]
    state = dict(map="test",player=dict(position=dict(x=0,y=0)),
                 targets=[dict(kind="object",position=dict(x=8,y=0))])
    with patch.object(route,"idle",return_value=state),patch.object(route,"reachable_trap",return_value=path) as planned, \
         patch.object(route,"go") as moved:
        route.walk_to(Mock(),resource,(17,0))
    assert (8,0) not in planned.call_args.kwargs["avoid"]
    assert all(call.args[1:3] != (8,0) for call in moved.call_args_list)
    assert moved.call_args_list[-1].args[1:3] == (17,0)
    with patch.object(route,"idle",return_value=state),patch.object(route,"reachable_trap",side_effect=[(17,0),path]), \
         patch.object(route,"go") as moved:
        route.transition(Mock(),resource,1,"next","next.txt",via_path=True)
    assert all(call.args[1:3] != (8,0) for call in moved.call_args_list)
    assert moved.call_args_list[-1].kwargs["script"] == "next.txt"
    title = dict(scene="Title")
    title_client = Mock()
    title_client.observe.return_value = title
    assert route.idle(title_client,allow_title=True) == title
    try:
        route.idle(title_client)
    except AutomationError as error:
        assert str(error) == "Unexpected return to title"
    else:
        raise AssertionError("An unexpected title was accepted as normal gameplay")
    target = dict(id=7, name="disciple", life=497)
    before = dict(generation=1, inventory=[], map="test")
    moving = dict(target,position=dict(x=1,y=0))
    for generation,reason,accepted in ((1,"blocked_destination",True),(2,"blocked_destination",False),(1,"manual_input",False)):
        updated = dict(moving,position=dict(x=2,y=0))
        after = dict(generation=generation,inventory=[],map="test",targets=[updated])
        client = Mock()
        client.act.return_value = dict(kills=1)
        with patch.object(route,"idle",side_effect=[before,after,after]), \
             patch.object(route,"approach_target",side_effect=[AutomationError(reason),after]) as approached, \
             patch.object(route,"write_json"):
            try:
                observed = route.fight(client,Path("unused"),resource,moving)
            except AutomationError:
                assert not accepted
            else:
                assert accepted and observed == after and approached.call_count == 2
    for generation, remaining, accepted in ((1, [], True), (1, [dict(id=7, attackable=True)], False), (2, [], False)):
        after = dict(generation=generation, targets=remaining)
        client = Mock()
        client.act.side_effect = AutomationError("StartCombat: invalid_enemy")
        with patch.object(route, "idle", side_effect=[before, after]), patch.object(route, "approach_target", return_value=before), patch.object(route, "write_json"):
            try:
                observed = route.fight(client, Path("unused"), resource, target)
            except AutomationError:
                assert not accepted
            else:
                assert accepted and observed == after
        assert client.act.call_args.kwargs["skills"] == [0]
        assert client.act.call_args.kwargs["allowMeleeFallback"] is False
    before = dict(variables={"deaths": "0"}, player=dict(position=dict(x=0, y=0)),
                  targets=[dict(target, attackable=True, hostile=True, position=dict(x=1, y=0))])
    for count, reason, accepted in ((17, "world_changed", True), (16, "world_changed", False),
                                    (17, "no_progress", False)):
        after = dict(variables={"deaths": str(count)})
        with patch.object(route, "idle", side_effect=[before, after, after]), \
             patch.object(route, "approach_target", return_value=before), \
             patch.object(route, "fight", side_effect=AutomationError(f"StartCombat: {reason}")), \
             patch.object(route, "write_json"):
            try:
                observed = route.kill_group(Mock(), Path("unused"), resource, "deaths", 17, ("disciple",))
            except AutomationError:
                assert not accepted
            else:
                assert accepted and observed == after
    candidates = dict(before,targets=[before['targets'][0],dict(before['targets'][0],id=8,position=dict(x=4,y=0))])
    finished = dict(variables={'deaths':'17'})
    with patch.object(route,'idle',side_effect=[candidates,finished]), \
         patch.object(route,'fight',side_effect=[AutomationError('No reachable combat neighbor: disciple'),finished]) as battled:
        assert route.kill_group(Mock(),Path('unused'),resource,'deaths',17,('disciple',)) == finished
        assert [call.args[3]['id'] for call in battled.call_args_list] == [7,8]
    ready = dict(generation=1,inventory=[],map='test',targets=[dict(target,attackable=True,position=dict(x=1,y=0))])
    client = Mock()
    client.act.side_effect = [AutomationError('StartCombat: configured_skill_unavailable'),dict(kills=1)]
    with patch.object(route,'idle',return_value=ready),patch.object(route,'approach_target',return_value=ready), \
         patch.object(route,'write_json'),patch.object(route.time,'sleep') as paused:
        assert route.fight(client,Path('unused'),resource,ready['targets'][0]) == ready
        assert client.act.call_count == 2
        paused.assert_called_once_with(0.5)
    client.act.reset_mock(side_effect=True)
    client.act.side_effect = AutomationError('StartCombat: configured_skill_unavailable')
    with patch.object(route,'idle',return_value=ready),patch.object(route,'approach_target',return_value=ready), \
         patch.object(route,'write_json'),patch.object(route.time,'sleep'):
        try:route.fight(client,Path('unused'),resource,ready['targets'][0],retry_unavailable=1)
        except AutomationError:assert client.act.call_count == 2
        else:raise AssertionError('Permanent magic unavailability was hidden')
    print("Xiaoxiang choice candidates and sixteen opening spell files passed")


if __name__ == "__main__":
    main()
