"""Small executable check for the XJXQY catalog and title-menu recognition."""
import json
import struct
import subprocess
import sys
from pathlib import Path
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

from run_xjxqy_gameplay import catalog_for, title_visible, write_json, enable_saved_partner_combat, protect_saved_partners, assist_saved_branch_numbers, load_checkpoint, walk_required_battle, interact_at, normal_exit, clear_enemies, saved_character_configuration, observed_character_configuration, goods_prices, pickup_reward, dynamic_drop_search, remaining_map_traps, zhaoqi_matchmaking, native_loss_prompt, saved_npc_dialogues, partner_dialogue_tour, chengdu_companion_choices, linan_shop_tiers, transition, checkpoint, VARIABLES
from gameplay_automation import Client, AutomationError
from audit_xjxqy_gameplay import branch_destinations, traced_condition_values, check_choice, complete_session_trace
from run_yycs_gameplay import reachable_trap


def main():
    assert len(VARIABLES) <= 128 and len({name.casefold() for name in VARIABLES}) == len(VARIABLES)
    old_start = dict(executionId=1, virtualPath="test.txt", contentSha256="old-source")
    choice_site = dict(id="test.txt:10", path="test.txt", sourceSha256="current-source",
                       message="Medicine", options=[dict(text="A"), dict(text="B")], api="choose", line=10)
    choice_proof = dict(choiceIndex=1, action=dict(actionId=5), cheatAssisted=False, scriptStart=old_start)
    choice_before = dict(choiceMessage="Medicine", choices=[dict(text="A"), dict(text="B")])
    with patch("audit_xjxqy_gameplay.read", return_value=dict(resourceId="XJXQY", session="xjxqy-test", cheatAssisted=False)):
        assert check_choice(Path("."), Path("."), choice_site, choice_proof, choice_before, {}, [old_start], []) is None
        try:
            check_choice(Path("."), Path("."), choice_site, choice_proof, choice_before, {}, [], [])
        except ValueError:
            pass
        else:
            raise AssertionError("An untraced historical source proof was admitted")
    trace = []
    def condition_event(kind, **details):
        trace.append(dict(eventType=kind, sequence=len(trace) + 1, **details))
    condition_sites = [dict(path='test.txt', line=10, source='if getvar("Clue") == 15 then goto A end'),
                       dict(path='test.txt', line=11, source='if getvar("Missing") == 0 then goto A end')]
    condition_event('variable.change', variableName='Clue', afterValue='15')
    condition_event('script.start', executionId=1, virtualPath='test.txt')
    condition_event('source.line', executionId=1, line=10)
    condition_event('api.call', executionId=1, apiName='getvar')
    condition_event('source.line', executionId=1, line=10)
    condition_event('variable.change', variableName='Clue', afterValue='')
    condition_event('api.call', executionId=1, apiName='getvar')
    condition_event('source.line', executionId=1, line=11)
    condition_event('api.call', executionId=1, apiName='getvar')
    for value in ('0017', '2147483648'):
        condition_event('variable.change', variableName='Clue', afterValue=value)
        condition_event('source.line', executionId=1, line=10)
        condition_event('api.call', executionId=1, apiName='getvar')
    condition_event('variable.change', variableName='clue', afterValue='15')
    condition_event('source.line', executionId=1, line=10)
    condition_event('api.call', executionId=1, apiName='getvar')
    condition_event('variable.change', variableName='Clue', afterValue='15')
    condition_event('source.line', executionId=1, line=10)
    condition_event('api.call', executionId=1, apiName='say')
    condition_event('api.call', executionId=1, apiName='getvar')
    condition_event('script.finish', executionId=1, status='completed')
    evaluated = traced_condition_values(trace, condition_sites)[1]
    assert [(side, evidence['variableValue']) for _, side, evidence in evaluated] == [('taken', 15), ('fallthrough', 0)]
    assert traced_condition_values(trace, condition_sites, initial_unknown_is_zero=True) == traced_condition_values(trace, condition_sites)
    complete = [dict(eventType='session.start'), *trace, dict(eventType='session.finish', status='completed')]
    complete = [dict(event, sequence=index, sessionId='default-zero-test') for index, event in enumerate(complete, 1)]
    assert complete_session_trace(complete)
    inferred = traced_condition_values(complete, condition_sites, initial_unknown_is_zero=True)[1]
    assert [(side, evidence['variableValue']) for _, side, evidence in inferred] == [('taken', 15), ('fallthrough', 0), ('taken', 0)]
    assert inferred[-1][2]['variableChangeSequence'] is None
    assert inferred[-1][2]['inference'] == 'complete-reviewed-runtime-default-zero-at-getvar'
    assert not complete_session_trace(complete[1:]) and not complete_session_trace(complete[:-1])
    assert not complete_session_trace([complete[0], *complete[2:]])
    assert not complete_session_trace([*complete[:-1], dict(complete[-1], status='failed')])
    assert not complete_session_trace([dict(event, sessionId=None) for event in complete])
    assert title_visible({"scene": "TitleBackground", "ui": [dict(name=n) for n in ("new-game", "load-game", "exit")]})
    assert not title_visible({"scene": "Title", "ui": [dict(name="exit")]})
    for prefix in ("magic-item-", "integrated-magic-item-"):
        calls = []
        state = dict(context=6, ui=[dict(name="equipment-item-1", slot=1, id=10),
                                  dict(name=prefix + "1", slot=1, id=12)])
        client = SimpleNamespace(observe=lambda: state, act=lambda command, **args: calls.append((command, args)))
        Client.focus_slot(client, "magic-item-", 1)
        assert calls == [("FocusUI", dict(context=6, targetId=12))]
    calls, responses = [], iter((dict(saveSlot=0), dict(worldInput=True)))
    def wait(predicate, **_):
        state = next(responses)
        assert predicate(state)
        return state
    client = SimpleNamespace(observe=lambda: dict(ui=[dict(name="save-load")]),
                             activate=lambda name: calls.append(name), wait_until=wait)
    assert Client.save_or_load(client, 0, load=True)["worldInput"]
    assert calls == ["save-load", "load"]
    calls = []
    loaded = dict(map='test.map', generation=9, variables=dict(Event='10'))
    client = SimpleNamespace(wait_until=lambda *_args, **_kwargs: dict(map='test.map', inEvent=False),
                             save_or_load=lambda slot, **kwargs: calls.append((slot, kwargs)),
                             observe=lambda variables: loaded if tuple(variables) == VARIABLES else {})
    assert load_checkpoint(client, 0) == loaded
    assert calls == [(0, dict(load=True))]
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        (root / "script/common").mkdir(parents=True)
        (root / "ini/buy").mkdir(parents=True)
        pickup = root / "script/common/pickup.txt"
        pickup.write_text('addgoods("item.ini", 2);\ndelcurobj();\n', encoding="utf-8")
        assert pickup_reward(root, "script/common/pickup.txt") == dict(kind="goods", files=["item.ini"], quantity=2, emptyOutcomeAllowed=False)
        pickup.write_text('addrandmoney(50, 1);\ndelcurobj();\n', encoding="utf-8")
        assert pickup_reward(root, "script/common/pickup.txt") == dict(kind="money", minimum=1, maximum=50)
        (root / "ini/buy/stock.ini").write_text('[Header]\nCount=2\n[1]\nIniFile=item.ini\n//IniFile=old1.ini\n//IniFile=old2.ini\n[2]\nIniFile=\n', encoding="utf-8")
        pickup.write_text('addrandgoods("stock.ini");\ndelcurobj();\n', encoding="utf-8")
        reward = pickup_reward(root, "script/common/pickup.txt")
        assert reward["files"] == ["item.ini"] and reward["emptyOutcomeAllowed"] and reward["quantity"] == 1
        (root / "ini/goods").mkdir(parents=True)
        for contents, expected in (("Kind=0\nLife=50\nMana=20\nThew=10\n", (180, 90)),
                                   ("Kind=1\nAttack=5\nDefend=3\nEvade=2\nEffectType=1\n", (480, 240)),
                                   ("Kind=2\nLife=50\n", (0, 0)),
                                   ("Kind=1\nCost=101\nSellPrice=40\n", (101, 40))):
            (root / "ini/goods/price.ini").write_text("[Init]\n" + contents, encoding="utf-8")
            assert goods_prices(root, "price.ini") == expected
            assert goods_prices(root, "price.ini", 80, 70) == (expected[0] * 80 // 100, expected[1] * 70 // 100)
        (root / "goods3.ini").write_text('[Head]\nCount=2\n[201]\nIniFile=helmet.ini\nNumber=1\n[207]\nIniFile=boots.ini\nNumber=1\n', encoding="utf-8")
        (root / "magic3.ini").write_text('[Head]\nCount=1\n[1]\nIniFile=skill.ini\nLevel=3\nExp=1\n', encoding="utf-8")
        configuration = saved_character_configuration(root, 3)
        assert [row["slot"] for row in configuration["goods"]] == [200, 206]
        assert configuration["magic"] == [dict(slot=0, file="skill.ini", level=3, exp=1)]
        observed = dict(inventory=[dict(slot=200, file="helmet.ini", quantity=1, cooldownMs=100),
                                   dict(slot=206, file="boots.ini", quantity=1, cooldownMs=0)],
                        magic=[dict(slot=0, file="skill.ini", level=3, exp=1, cooldownMs=200)])
        assert observed_character_configuration(observed) == configuration
        for extra in (("--assist-local-date", "2026-02-29", "--cheat-assisted"),
                      ("--assist-local-date", "2026-01-01"),
                      ("--assist-local-date", "2026-01-01", "--cheat-assisted", "--resume")):
            output = root / "rejected-date"
            rejected = subprocess.run([sys.executable, str(Path(__file__).with_name("run_xjxqy_gameplay.py")),
                "--assets", str(root), "--output", str(output), "--inventory-only", *extra], capture_output=True)
            assert rejected.returncode == 2 and not output.exists()
        save = root / "game.ini"
        original = b'[Option]\r\nPartnerCombat=0\r\n[State]\r\nEvent=265\r\n'
        save.write_bytes(original)
        correction = enable_saved_partner_combat(save)
        assert save.read_bytes() == original.replace(b'PartnerCombat=0', b'PartnerCombat=1')
        assert correction['changed'] and correction['beforeSha256'] != correction['afterSha256']
        partner = root / 'partner0.ini'
        before = b'[head]\r\ncount=1\r\n[partner000]\r\nkind=3\r\nlife=1000\r\ninvincible=0\r\n'
        partner.write_bytes(before)
        protect_saved_partners(partner)
        assert partner.read_bytes() == before.replace(b'invincible=0', b'invincible=1')
        money = root / 'player0.ini'
        money.write_bytes(b'\xef\xbb\xbf[player]\r\nmoney=2265\r\nlevel=52\r\n')
        goods = root / 'goods0.ini'
        goods.write_text('[head]\ncount=1\n[item0]\ninifile=goods402_红玉.ini\nnumber=1\n', encoding='utf-8')
        changes = assist_saved_branch_numbers(root, money=10, ruby_quantity=0)
        assert len(changes) == 2 and money.read_bytes() == b'\xef\xbb\xbf[player]\r\nmoney=10\r\nlevel=52\r\n'
        assert goods.read_text(encoding='utf-8').endswith('number=0\n')
        before = money.read_bytes()
        assist_saved_branch_numbers(root, player_level=9)
        assert money.read_bytes() == before.replace(b'level=52', b'level=9')
        character = root / 'player1.ini'
        character_before = b'[init]\r\nlevel=12\r\nlifemax=535\r\n'
        character.write_bytes(character_before)
        hero_before = money.read_bytes()
        changes = assist_saved_branch_numbers(root, character_level=(1, 35))
        assert character.read_bytes() == character_before.replace(b'level=12', b'level=35')
        assert money.read_bytes() == hero_before and changes[0]['beforeValue'] == 12
        for index, level in ((4, 35), (1, 0), (1, 81)):
            before_character = character.read_bytes()
            try:
                assist_saved_branch_numbers(root, character_level=(index, level))
            except AutomationError:
                assert character.read_bytes() == before_character and money.read_bytes() == hero_before
            else:
                raise AssertionError('Character level assistance admitted an invalid index or level')
        requested = []
        observed = SimpleNamespace(observe=lambda names: (requested.append(names), dict(outputHealthy=True))[1],
                                   snapshot=lambda: money)
        checkpoint(observed, root, "variable-limit", ("Talklv", "Talkxiaosan", "Talklv"))
        assert len(requested[0]) == 128 and requested[0][:2] == ("Talklv", "Talkxiaosan")
        assert len(set(requested[0])) == 128 and {"Event", "Clue", "End", "NoEnd"} <= set(requested[0])
        checkpoint(observed, root, "battle-variable-limit", ("fight113", "huihuidan", "zhui", "zhuiyang"))
        assert len(requested[-1]) == 128 and {"Clue", "NoEnd", "fight113", "huihuidan", "zhui", "zhuiyang"} <= set(requested[-1])
        before = money.read_bytes()
        try:
            assist_saved_branch_numbers(root, player_level=0)
        except AutomationError:
            assert money.read_bytes() == before
        else:
            raise AssertionError('Level assistance admitted a zero level')
        save.write_text('[state]\nnpc=map084.npc\n', encoding='utf-8')
        npc = root / 'map084.npc'
        before = '[head]\ncount=2\n[npc000]\nname=张琳心\ninvincible=1\n[npc001]\nname=南宫灭\ninvincible=0\n'.encode('utf-8')
        npc.write_bytes(before)
        changes = assist_saved_branch_numbers(root, npc_vulnerable='张琳心')
        assert len(changes) == 1 and changes[0]['beforeValue'] == 1
        assert npc.read_bytes() == before.replace(b'invincible=1', b'invincible=0')
        before = '[head]\r\ncount=2\r\n[npc000]\r\nname=张琳心\r\nlife=100\r\nlifemax=100\r\n[npc001]\r\nname=南宫灭\r\nlife=10000000\r\nlifemax=10000000\r\n'.encode('utf-8')
        npc.write_bytes(before)
        changes = assist_saved_branch_numbers(root, npc_life=('南宫灭', 1))
        assert npc.read_bytes() == before.replace(b'life=10000000', b'life=1')
        assert changes[0]['beforeValue'] == 10000000 and changes[0]['afterValue'] == 1
        before = npc.read_bytes()
        for life in (0, 10000001):
            try:
                assist_saved_branch_numbers(root, npc_life=('南宫灭', life))
            except AutomationError:
                assert npc.read_bytes() == before
            else:
                raise AssertionError('NPC life assistance admitted an already-dead or excessive life')
        for function in (remaining_map_traps, zhaoqi_matchmaking, native_loss_prompt):
            client = SimpleNamespace(act=lambda *_args, **_kwargs: None)
            with patch('run_xjxqy_gameplay.checkpoint', return_value=dict(map='map999_unreviewed.map', variables={})):
                try:
                    function(client, root, root)
                except AutomationError:
                    pass
                else:
                    raise AssertionError('A new route admitted an unreviewed source map')
        for mapname, event, destination in (('map015_临安城南.map', '40', 'map005_林间小道.map'),
                                             ('map015_临安城南.map', '210', 'map005_林间小道.map'),
                                             ('map015_临安城南.map', '195', None),
                                             ('map012_武夷山大厅.map', '40', 'map011_武夷山顶.map')):
            state = dict(map=mapname, variables=dict(Event=event))
            with patch('run_xjxqy_gameplay.checkpoint', return_value=state), patch('run_xjxqy_gameplay.transition', side_effect=AutomationError('checked forest entry')) as move:
                try:
                    remaining_map_traps(client, root, root)
                except AutomationError:
                    pass
                assert move.called == bool(destination)
                if destination:
                    assert move.call_args.args[2:4] == (destination, 2 if mapname.startswith('map015_') else 1)
        for mapname, event, destination, trap in (('map016_临安城.map', '120', 'map017_临安城酒楼.map', 4),
                ('map016_临安城.map', '180', 'map017_临安城酒楼.map', 4),
                ('map100_天王岛.map', '610', 'map101_天王帮大殿.map', 2),
                ('map121_洞庭湖底.map', '610', 'map099_洞庭湖畔.map', 1),
                ('map097_湖口村茶馆.map', '550', 'map096_湖口村.map', 1),
                ('map097_湖口村茶馆.map', '540', None, None),
                ('map121_洞庭湖底.map', '560', None, None),
                ('map100_天王岛.map', '560', None, None)):
            state = dict(map=mapname, variables=dict(Event=event))
            with patch('run_xjxqy_gameplay.idle', return_value=state), patch.object(Path, 'read_text', return_value='{"scripts": []}'), patch('run_xjxqy_gameplay.transition', side_effect=AutomationError('checked tour entry')) as move:
                try:
                    partner_dialogue_tour(client, root, root)
                except AutomationError:
                    pass
                assert move.called == bool(destination)
                if destination:
                    assert move.call_args.args[2:4] == (destination, trap)
        for clue in ('0', '91'):
            state = dict(map='map105_成都.map', variables=dict(Event='620', Clue=clue), player=dict(level=15))
            with patch('run_xjxqy_gameplay.idle', return_value=state), patch.object(Path, 'read_text', return_value='{}'), patch('run_xjxqy_gameplay.transition', side_effect=AutomationError('checked shop entry')) as move:
                try:
                    linan_shop_tiers(client, root, root, city='chengdu')
                except AutomationError:
                    pass
                assert move.called == (clue == '0')
                if move.called:
                    assert move.call_args.args[2:4] == ('map107_成都杂货铺.map', 2)
        trapfile = root / 'ini/save/traps.ini'
        trapfile.parent.mkdir(parents=True, exist_ok=True)
        trapfile.write_text('[test]\n1=trap01.txt\n', encoding='utf-8')
        state = dict(map='test.map', generation=1, player=dict(position=dict(x=7, y=14)), targets=[dict(name='resident', position=dict(x=5, y=13))])
        client = SimpleNamespace(submit=lambda *_args, **_kwargs: (_ for _ in ()).throw(AutomationError('picked safe route')))
        for answers in [[[(7, 14), (6, 22)]], [AutomationError('No connected trap 1'), [(7, 14), (6, 22)]]]:
            with patch('run_xjxqy_gameplay.idle', return_value=state), patch('run_xjxqy_gameplay.records', return_value=[dict(sequence=0)]), patch('run_xjxqy_gameplay.reachable_trap', side_effect=answers) as route:
                try:
                    transition(client, root, 'other.map', 1, root, 'checked')
                except AutomationError as error:
                    assert str(error) == 'picked safe route'
                assert route.call_args_list[0].kwargs['avoid'] == {(5, 13)}
                assert route.call_count == len(answers)
                if len(answers) == 2:
                    assert route.call_args_list[1].kwargs['avoid'] == {(5, 13)}
        state = dict(map='test.map', generation=1, variables=dict(Event='620'), player=dict(position=dict(x=7, y=14)), targets=[])
        changed = dict(state, player=dict(position=dict(x=7, y=17)))
        moves = []
        submissions = iter([17, AutomationError('checked resumed exit')])
        def submit_exit(*_args, **_kwargs):
            value = next(submissions)
            if isinstance(value, Exception):
                raise value
            return value
        client = SimpleNamespace(submit=submit_exit, observe=lambda _variables: state, request=lambda *_args, **_kwargs: dict(status='failed', reason='no_progress'), move=lambda *point: moves.append(point))
        with patch('run_xjxqy_gameplay.idle', side_effect=[state, changed]), patch('run_xjxqy_gameplay.records', return_value=[dict(sequence=0)]), patch('run_xjxqy_gameplay.reachable_trap', return_value=[(7, 14), (7, 15), (7, 17), (6, 22)]), patch('run_xjxqy_gameplay.checkpoint', return_value=changed):
            try:
                transition(client, root, 'other.map', 1, root, 'checked')
            except AutomationError as error:
                assert str(error) == 'checked resumed exit'
            assert moves == [(7, 17)]
        occupied_after = dict(state, targets=[dict(name='follower', position=dict(x=7, y=17))])
        for reason in ('blocked_destination', 'manual_input'):
            submissions = iter([17, AutomationError('checked resumed exit')])
            def blocked_step(*_point):
                raise AutomationError('MoveTo: ' + reason)
            client.move = blocked_step
            with patch('run_xjxqy_gameplay.idle', side_effect=[state, occupied_after]), \
                    patch('run_xjxqy_gameplay.records', return_value=[dict(sequence=0)]), \
                    patch('run_xjxqy_gameplay.reachable_trap', return_value=[(7, 14), (7, 15), (7, 17), (6, 22)]), \
                    patch('run_xjxqy_gameplay.checkpoint', return_value=occupied_after):
                try:
                    transition(client, root, 'other.map', 1, root, 'changed-follower')
                except AutomationError as error:
                    assert str(error) == ('checked resumed exit' if reason == 'blocked_destination' else 'MoveTo: manual_input')
                else:
                    raise AssertionError('Blocked step did not replan or respect manual takeover')
        game = root / 'user-data/save/xjxqy/game'
        game.mkdir(parents=True)
        (game / 'game.ini').write_text('[state]\nnpc=ordinary-map.npc\n', encoding='utf-8')
        client = SimpleNamespace(act=lambda *_args, **_kwargs: None, save_or_load=lambda _slot: None)
        with patch('run_xjxqy_gameplay.idle', return_value=dict(map='test.map')), patch('run_xjxqy_gameplay.template_dialogues', side_effect=AutomationError('checked partner source')) as dialogue, patch.object(Path, 'read_text', return_value='{"scripts": []}'):
            try:
                saved_npc_dialogues(client, root, root, partners=True)
            except AutomationError as error:
                assert str(error) == 'checked partner source'
            assert Path(dialogue.call_args.args[3]) == game / 'partner0.ini'
        def refresh_saved_world(slot):
            assert slot == 0
            (game / 'game.ini').write_text('[state]\nnpc=current-map.npc\n', encoding='utf-8')
        client.save_or_load = refresh_saved_world
        with patch('run_xjxqy_gameplay.idle', return_value=dict(map='test.map')), \
                patch('run_xjxqy_gameplay.template_dialogues', side_effect=AutomationError('checked refreshed NPC source')) as dialogue, \
                patch.object(Path, 'read_text', return_value='{"scripts": []}'):
            try:
                saved_npc_dialogues(client, root, root)
            except AutomationError as error:
                assert str(error) == 'checked refreshed NPC source'
            assert Path(dialogue.call_args.args[3]) == game / 'current-map.npc'
        replay_source = 'script/map/map099_洞庭湖畔/杨瑛的同伴对话.txt'
        (game / 'partner0.ini').write_text('[NPC000]\nName=杨瑛\nScriptFile=杨瑛的同伴对话.txt\n', encoding='utf-8')
        replay_state = dict(map='map121_洞庭湖底.map', variables=dict(Event='610'))
        def enter_lake(*_args, **_kwargs):
            replay_state['map'] = 'map099_洞庭湖畔.map'
        client.save_or_load = lambda _slot: None
        for clue in ('0', '15', '30', '45', '50', '65', '71', '75'):
            source_stage_map = ('map106_成都客栈一楼.map' if clue in ('0', '30', '65') else
                                'map110_成都民居.map' if clue == '50' else 'map105_成都.map')
            stage_state = dict(map=source_stage_map, variables=dict(Clue=clue))
            stage_dialogues = []
            def enter_stage_map(_client, _resource, destination, *_args, **_kwargs):
                stage_state['map'] = destination
            def stage_dialogue(_client, _output, _resource, _name, source, *_args, **kwargs):
                assert kwargs['choices'] == []
                stage_dialogues.append(source)
                return stage_state
            client.observe = lambda *_: stage_state
            with patch('run_xjxqy_gameplay.idle', side_effect=lambda *_: stage_state), \
                    patch('run_xjxqy_gameplay.load_checkpoint', side_effect=lambda *_: stage_state.update(map=source_stage_map)), \
                    patch('run_xjxqy_gameplay.catalog_for', return_value=dict(choices=[])), \
                    patch('run_xjxqy_gameplay.transition', side_effect=enter_stage_map), \
                    patch('run_xjxqy_gameplay.interact_at', side_effect=stage_dialogue):
                chengdu_companion_choices(client, root, root)
            assert len(stage_dialogues) == len(set(stage_dialogues)) == 9
        with patch('run_xjxqy_gameplay.idle', side_effect=lambda *_: replay_state), \
                patch('run_xjxqy_gameplay.catalog_for', return_value=dict(sources=[dict(path=replay_source)])), \
                patch('run_xjxqy_gameplay.transition', side_effect=enter_lake), \
                patch('run_xjxqy_gameplay.saved_npc_dialogues', side_effect=AutomationError('checked replay bindings')) as dialogue:
            try:
                partner_dialogue_tour(client, root, root, recheck=True)
            except AutomationError as error:
                assert str(error) == 'checked replay bindings'
            else:
                raise AssertionError('Covered bindings were skipped during an explicit replay')
            assert dialogue.call_args.kwargs['sources'] == {replay_source}
        inn_maps = ('map106_成都客栈一楼.map', 'map106_1成都客栈二楼.map')
        replay_state.update(map=inn_maps[0], variables=dict(Event='620', Clue='10'))
        inn_sources = {'script/map/' + Path(name).stem + '/杨瑛的同伴对话.txt' for name in inn_maps}
        replayed = []
        def replay_inn_dialogue(*_args, **_kwargs):
            replayed.append(replay_state['map'])
            write_json(root / 'saved-partner-dialogues-proof.json', dict(map=replay_state['map']))
        with patch('run_xjxqy_gameplay.idle', side_effect=lambda *_: replay_state), \
                patch('run_xjxqy_gameplay.catalog_for', return_value=dict(sources=[dict(path=p) for p in inn_sources])), \
                patch('run_xjxqy_gameplay.transition', side_effect=enter_stage_map), \
                patch('run_xjxqy_gameplay.saved_npc_dialogues', side_effect=replay_inn_dialogue):
            stage_state = replay_state
            partner_dialogue_tour(client, root, root, recheck=True)
        assert replayed == list(inn_maps) and replay_state['map'] == inn_maps[0] and replay_state['variables']['Clue'] == '10'
        money.write_bytes(b'\xef\xbb\xbf[init]\r\nlife=5451\r\nlifemax=5451\r\nlevel=60\r\n')
        assist_saved_branch_numbers(root, player_life=1)
        assert money.read_bytes() == b'\xef\xbb\xbf[init]\r\nlife=1\r\nlifemax=5451\r\nlevel=60\r\n'
        try:
            assist_saved_branch_numbers(root, player_life=0)
        except AutomationError:
            pass
        else:
            raise AssertionError('Life assistance admitted an already-dead player')
        before = money.read_bytes()
        changes = assist_saved_branch_numbers(root, player_life_max=10000)
        assert money.read_bytes() == before.replace(b'lifemax=5451', b'lifemax=10000')
        assert changes[0]['field'] == 'lifemax' and changes[0]['beforeValue'] == 5451
        money.write_bytes(money.read_bytes() + b'evade=387\r\n')
        before = money.read_bytes()
        changes = assist_saved_branch_numbers(root, player_evade=0)
        assert money.read_bytes() == before.replace(b'evade=387', b'evade=0')
        assert changes[0]['field'] == 'evade' and changes[0]['beforeValue'] == 387
        before = money.read_bytes()
        try:
            assist_saved_branch_numbers(root, player_evade=-1)
        except AutomationError:
            assert money.read_bytes() == before
        else:
            raise AssertionError('Evade assistance admitted a negative value')
        before = money.read_bytes()
        try:
            assist_saved_branch_numbers(root, player_life_max=0)
        except AutomationError:
            assert money.read_bytes() == before
        else:
            raise AssertionError('Life maximum assistance admitted a nonpositive limit')
        medicine = root / 'goods1.ini'
        before = '[head]\ncount=1\n[item0]\ninifile=goods216_双头人参.ini\nnumber=1\n'.encode('utf-8')
        medicine.write_bytes(before)
        changes = assist_saved_branch_numbers(root, ginseng_quantity=2)
        assert len(changes) == 1 and medicine.read_bytes() == before.replace(b'number=1', b'number=2')
        script = root / "script/map/branch.txt"
        script.parent.mkdir(parents=True)
        script.write_text('-- choose("忽略","甲","乙","X");\nchoose("问题--原文","普通","困难","Level");\nif getvar("End") == 222 then goto Done end\nreturntotitle();\n::Done::\nreturn;\n', encoding="utf-8")
        catalog = catalog_for(root)
        assert catalog["resourceId"] == "XJXQY"
        assert catalog["counts"]["choiceSites"] == 1
        assert catalog["counts"]["choiceOptions"] == 2
        assert catalog["counts"]["conditionalSites"] == 1
        assert catalog["choices"][0]["line"] == 2
        assert catalog["choices"][0]["message"] == "问题--原文"
        assert all(x["status"] == "pending" for x in catalog["choices"][0]["options"])
        assert json.loads(json.dumps(catalog, ensure_ascii=False))["choices"] == catalog["choices"]
        assert branch_destinations(root, catalog["conditions"][0]) == dict(taken=6, fallthrough=4)
        script.write_text('if getvar("End") == 222 then goto Done end\n::Done::\nreturn;\n', encoding="utf-8")
        assert branch_destinations(root, dict(path="script/map/branch.txt", line=1)) is None
        write_json(root / "run.json", dict(cheatAssisted=True))
        write_json(root / "proof.json", dict(cheatAssisted=False))
        assert json.loads((root / "proof.json").read_text(encoding="utf-8"))["cheatAssisted"] is True
        grid = bytearray(100 + 5 * 5 * 10)
        struct.pack_into("<5i", grid, 64, 0, 5, 5, 0, 0)
        struct.pack_into("<4i", grid, 84, 100, 0, 0, 0)
        grid[100 + (2 * 5 + 2) * 10 + 6] = 0x80
        grid[100 + (2 * 5 + 3) * 10 + 7] = 1
        (root / "map").mkdir()
        (root / "map/test.map").write_bytes(grid)
        path = reachable_trap(root, "test.map", None, dict(x=1, y=2), with_path=True, destination=(3, 2))
        assert path[0] == (1, 2) and path[-1] == (3, 2) and (2, 2) not in path
        assert reachable_trap(root, "test.map", 1, dict(x=1, y=2), with_path=True) == path
        grid[100 + (2 * 5 + 2) * 10 + 6] = 0
        (root / "map/test.map").write_bytes(grid)
        state = dict(map="test.map", player=dict(position=dict(x=1, y=2)), variables=dict(Event="405"),
                     targets=[dict(id=1, name="enemy", hostile=True, attackable=True, visibleFromPlayer=False, position=dict(x=3, y=2)),
                              dict(id=2, name="companion", position=dict(x=2, y=2))])
        def move(x, y):
            assert (x, y) not in {(2, 2), (3, 2)}, "Battle approach must avoid the occupied first step"
            state["player"]["position"] = dict(x=x, y=y)
            state["variables"]["Event"] = "410"
        client = SimpleNamespace(observe=lambda *_: state, move=move)
        with patch("run_xjxqy_gameplay.idle", return_value=state), \
                patch("run_xjxqy_gameplay.clear_enemies", side_effect=AutomationError("no_progress")), \
                patch("run_xjxqy_gameplay.checkpoint", return_value=state):
            assert walk_required_battle(client, root, root, "410", "occupied-approach")["variables"]["Event"] == "410"
        empty_battle = dict(state, targets=[])
        with patch("run_xjxqy_gameplay.idle", return_value=empty_battle), \
                patch("run_xjxqy_gameplay.clear_enemies", return_value=empty_battle):
            assert walk_required_battle(client, root, root, None, "all-enemies", progress_variable=None) == empty_battle
        callback_state = dict(state, variables=dict(Event="700", Fight120="29"))
        queries = []
        callback_client = SimpleNamespace(observe=lambda names: (queries.append(names), callback_state)[1],
                                          act=lambda *args, **kwargs: (_ for _ in ()).throw(AssertionError("Do not attack the newly spawned final boss")))
        with patch("run_xjxqy_gameplay.idle", return_value=state):
            assert clear_enemies(callback_client, root, "native-death-count", stop_variable=("Fight120", "29")) is callback_state
            assert walk_required_battle(callback_client, root, root, "29", "native-death-count", progress_variable="Fight120") is callback_state
        assert len(queries) == 3 and all("Fight120" in names and len(names) <= 128 for names in queries)
        state["variables"]["Event"] = "405"
        state["player"]["position"] = dict(x=1, y=2)
        state["targets"].append(dict(id=3, name="reachable enemy", hostile=True, attackable=True,
                                     visibleFromPlayer=False, position=dict(x=0, y=4)))
        attempted_destinations = []
        def enemy_path(*args, **kwargs):
            attempted_destinations.append(kwargs["destination"])
            if kwargs["destination"] == (3, 2):
                raise AutomationError("No connected trap None: blocked enemy")
            return [(1, 2), (0, 2), (0, 3), (0, 4)]
        with patch("run_xjxqy_gameplay.idle", return_value=state), \
                patch("run_xjxqy_gameplay.clear_enemies", side_effect=AutomationError("no_progress")), \
                patch("run_xjxqy_gameplay.reachable_trap", side_effect=enemy_path), \
                patch("run_xjxqy_gameplay.checkpoint", return_value=state):
            assert walk_required_battle(client, root, root, "410", "blocked-nearest")["variables"]["Event"] == "410"
        assert attempted_destinations == [(3, 2), (0, 4)]
        state["targets"].pop()
        state["variables"]["Event"] = "405"
        state["player"]["position"] = dict(x=1, y=2)
        original_targets = state["targets"]
        state["targets"] = [original_targets[0], dict(id=9, kind="object", position=dict(x=2, y=2)),
                            dict(id=10, kind="npc", life=0, position=dict(x=2, y=2))]
        def corpse_move(x, y):
            assert (x, y) == (2, 2), "Passable body must not close the only approach"
            state["variables"]["Event"] = "410"
        client.move = corpse_move
        def corpse_path(*args, **kwargs):
            if (2, 2) in kwargs["avoid"]:
                raise AutomationError("No connected trap None: observed body")
            return [(1, 2), (2, 2), (3, 2)]
        with patch("run_xjxqy_gameplay.idle", return_value=state), \
                patch("run_xjxqy_gameplay.clear_enemies", side_effect=AutomationError("no_progress")), \
                patch("run_xjxqy_gameplay.reachable_trap", side_effect=corpse_path), \
                patch("run_xjxqy_gameplay.checkpoint", return_value=state):
            assert walk_required_battle(client, root, root, "410", "passable-body")["variables"]["Event"] == "410"
        state["targets"] = original_targets
        client.move = move
        state["player"]["position"] = dict(x=1, y=2)
        state["targets"][0]["name"] = "actor"
        attempts = []
        def interact(target_id):
            attempts.append(target_id)
            if len(attempts) == 1:
                raise AutomationError("Interact: interaction_not_observed")
        client.interact = interact
        with patch("run_xjxqy_gameplay.idle", return_value=state), \
                patch("run_xjxqy_gameplay.records", return_value=[dict(sequence=1)]), \
                patch("run_xjxqy_gameplay.script_proof", return_value=dict(status="passed")), \
                patch("run_xjxqy_gameplay.checkpoint", return_value=state):
            interact_at(client, root, root, "actor", "test.txt", "occupied-interaction")
        assert attempts == [1, 1] and state["player"]["position"] != dict(x=1, y=2)
        state["player"]["position"] = dict(x=1, y=2)
        attempts.clear()
        def occupied_move(x, y):
            state["player"]["position"] = dict(x=1, y=1)
            state["targets"][1]["position"] = dict(x=x, y=y)
            raise AutomationError("MoveTo: no_progress")
        client.move = occupied_move
        with patch("run_xjxqy_gameplay.idle", return_value=state), \
                patch("run_xjxqy_gameplay.records", return_value=[dict(sequence=1)]), \
                patch("run_xjxqy_gameplay.script_proof", return_value=dict(status="passed")), \
                patch("run_xjxqy_gameplay.checkpoint", return_value=state):
            interact_at(client, root, root, "actor", "test.txt", "moving-companion")
        assert attempts == [1, 1] and state["player"]["position"] == dict(x=1, y=1)
        attempts.clear()
        client.move = lambda *_: (_ for _ in ()).throw(AutomationError("MoveTo: blocked_destination"))
        with patch("run_xjxqy_gameplay.idle", return_value=state), \
                patch("run_xjxqy_gameplay.records", return_value=[dict(sequence=1)]), \
                patch("run_xjxqy_gameplay.script_proof", return_value=dict(status="passed")), \
                patch("run_xjxqy_gameplay.checkpoint", return_value=state):
            interact_at(client, root, root, "actor", "test.txt", "reserved-companion-step")
        assert attempts == [1, 1]
        attempts.clear()
        client.move = lambda *_: (_ for _ in ()).throw(AutomationError("MoveTo: manual_input"))
        with patch("run_xjxqy_gameplay.idle", return_value=state), \
                patch("run_xjxqy_gameplay.records", return_value=[dict(sequence=1)]):
            try:
                interact_at(client, root, root, "actor", "test.txt", "manual-takeover")
            except AutomationError as error:
                assert str(error) == "MoveTo: manual_input" and attempts == [1]
            else:
                raise AssertionError("Follower recovery ignored manual takeover")
        state["targets"][1]["position"] = dict(x=2, y=2)
        client.move = move
        grid[100 + (2 * 5 + 3) * 10 + 6] = 0x80
        (root / "map/test.map").write_bytes(grid)
        state["player"]["position"] = dict(x=1, y=2)
        attempts.clear()
        with patch("run_xjxqy_gameplay.idle", return_value=state), \
                patch("run_xjxqy_gameplay.records", return_value=[dict(sequence=1)]), \
                patch("run_xjxqy_gameplay.script_proof", return_value=dict(status="passed")), \
                patch("run_xjxqy_gameplay.checkpoint", return_value=state):
            interact_at(client, root, root, "actor", "test.txt", "blocked-object-tile")
        assert attempts == [1, 1] and state["player"]["position"] != dict(x=1, y=2)
        grid[100 + (2 * 5 + 3) * 10 + 6] = 0
        (root / "map/test.map").write_bytes(grid)
        with patch("run_xjxqy_gameplay.idle", return_value=state) as wait, \
                patch("run_xjxqy_gameplay.records", return_value=[dict(sequence=1)]), \
                patch("run_xjxqy_gameplay.script_proof", return_value=dict(status="passed")), \
                patch("run_xjxqy_gameplay.checkpoint", return_value=state):
            interact_at(client, root, root, "actor", "test.txt", "native-random-choice",
                        choices=lambda: [("test.txt:12", 1)])
        assert wait.call_args.kwargs["choices"] == [("test.txt:12", 1)]
        state["targets"].append(dict(id=4, name="actor", position=dict(x=4, y=3)))
        with patch("run_xjxqy_gameplay.idle", return_value=state), \
                patch("run_xjxqy_gameplay.records", return_value=[dict(sequence=1)]), \
                patch("run_xjxqy_gameplay.script_proof", return_value=dict(status="passed")), \
                patch("run_xjxqy_gameplay.checkpoint", return_value=state):
            interact_at(client, root, root, "actor", "test.txt", "moving-duplicate-name", target_id=4)
        assert attempts[-1] == 4
        state["targets"].pop()
        drop_game = root / "drop-gate/user-data/save/xjxqy/game"
        drop_game.mkdir(parents=True)
        (drop_game / "game.ini").write_text("[state]\nnpc=test.npc\n", encoding="utf-8")
        client = SimpleNamespace(act=lambda *_args, **_kwargs: None)
        for level, callback in ((4, "death.txt"), (12, "")):
            (drop_game / "test.npc").write_text(f"[npc000]\nrelation=1\nlevel={level}\ndeathscript={callback}\n", encoding="utf-8")
            with patch("run_xjxqy_gameplay.idle", return_value=state):
                try:
                    dynamic_drop_search(client, root / "drop-gate", root)
                except AutomationError as error:
                    assert "callback-free low-level ordinary source" in str(error)
                else:
                    raise AssertionError("Drop search admitted a plot callback or a higher tier source")
        denied = PermissionError("transient process query denied")
        denied.winerror = 5
        menu = dict(context=1, ui=[dict(name=n) for n in ("new-game", "load-game", "exit")])
        client = SimpleNamespace(observe=lambda: menu, focus=lambda _: None, submit=lambda *args, **kwargs: 1)
        with patch("run_xjxqy_gameplay.process_running", side_effect=[denied, False]), \
                patch("run_xjxqy_gameplay.time.sleep"):
            normal_exit(client, dict(pid=123, command=["game.exe"]), root)
        exit_proof = json.loads((root / "normal-exit-proof.json").read_text(encoding="utf-8"))
        assert exit_proof["processStopped"] and len(exit_proof["transientProcessQueryErrors"]) == 1
        from run_xjxqy_gameplay import remaining_interior_tour
        client = SimpleNamespace(act=lambda *_args, **_kwargs: None)
        for map_name, event, extra in (("map007_武夷山九猴洞.map", "210", {}),
                                      ("map106_成都客栈一楼.map", "620", {"Clue": "25"}),
                                      ("map062_华山栈道3.map", "340", {}),
                                      ("map091_长白山北.map", "530", {"Talktufei": "1"})):
            with patch("run_xjxqy_gameplay.idle", return_value=dict(map=map_name, variables=dict(Event=event, **extra))):
                try:
                    remaining_interior_tour(client, root, root)
                except AutomationError as error:
                    assert "reviewed ordinary entry stage" in str(error)
                else:
                    raise AssertionError("Remaining interior tour admitted a different plot stage")
    print("XJXQY catalog and title checks passed")


if __name__ == "__main__":
    main()
