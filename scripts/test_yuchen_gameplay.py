"""Small checks against real Yuchen choices and original identity."""
from pathlib import Path
import tempfile
from unittest.mock import Mock, patch

import run_yuchen_gameplay as yuchen

resource = Path(__file__).resolve().parents[1] / "assets" / yuchen.RESOURCE_DIRECTORY
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / "script/common").mkdir(parents=True)
    script = root / "script/common/choice.txt"
    for parameters in ('"-1","1"', '-1,1'):
        script.write_text(f'chooseplus("Speaker",{parameters},"Question","A","B","Result");\n', encoding="utf-8")
        choice = yuchen.yycs.inventory(root)["choices"][0]
        assert choice["api"] == "chooseplus" and choice["message"] == "Question"
        assert choice["variable"] == "Result" and [row["text"] for row in choice["options"]] == ["A", "B"]
catalog = yuchen.yycs.inventory(resource)
opening = {site["line"]: site for site in catalog["choices"]
           if site["path"] == "script/map/map_020_樱花谷/begin.txt"}
assert opening[6]["variable"] == "Level" and opening[7]["variable"] == "XZ"
assert yuchen.RESOURCE_ID in (resource / "game_profile.ini").read_text(encoding="utf-8")
conditional = dict(message="Question", options=[dict(index=0, text="Persuade{$koucai >= 1}"),
                   dict(index=1, text="Attack"), dict(index=2, text="Leave")])
state = dict(choiceMessage="Question", choices=[dict(index=1, text="Attack"), dict(index=2, text="Leave")])
assert yuchen.yycs.choice_matches_source(conditional, state, 2)
assert not yuchen.yycs.choice_matches_source(conditional, state, 0)
state["choices"].insert(0, dict(index=0, text="Persuade"))
assert yuchen.yycs.choice_matches_source(conditional, state, 0)
state["choices"][1]["text"] = "Different result"
assert not yuchen.yycs.choice_matches_source(conditional, state, 0)
conditional["options"].append(dict(index=3, text=""))
state["choices"][1]["text"] = "Attack"
assert yuchen.yycs.choice_matches_source(conditional, state, 0)
plain = dict(message="Question", options=[dict(index=0, text=""), dict(index=1, text="Leave")])
assert yuchen.yycs.choice_matches_source(plain, dict(choiceMessage="Question", choices=[dict(index=1, text="Leave")]), 1)
assert not yuchen.yycs.choice_matches_source(plain, dict(choiceMessage="Question", choices=[dict(index=0, text="Leave")]), 0)
gate_client = Mock()
gate_before = dict(map="map_024_倚天山.map", generation=7, targets=[], player=dict(position=dict(x=27, y=25)))
gate_client.submit.return_value = 8
gate_client.request.side_effect = [dict(status="running"), {}]
gate_client.wait_until.side_effect = lambda predicate, **kwargs: predicate(dict(inEvent=True, generation=7))
with patch.object(yuchen.yuemeier, "approach_exit"), patch.object(yuchen.yycs, "idle", return_value=gate_before), \
     patch.object(yuchen.yycs, "reachable_trap", return_value=(28, 24)), \
     patch.object(yuchen.yuemeier, "checkpoint", return_value=gate_before):
    yuchen.yuemeier.blocked_exit(gate_client, Path("."), resource, 2, "closed")
gate_client.request.assert_any_call("CancelAction", actionId=8)
assert not gate_client.move.called
moving_client = Mock()
moving_client.move.side_effect = [yuchen.AutomationError("MoveTo: no_progress"), None]
moving_foe = dict(id=9, name="Moving foe", kind="npc", hostile=True, attackable=True,
                  visibleFromPlayer=True, position=dict(x=20, y=44))
moving_state = dict(gate_before, targets=[moving_foe])
with patch.object(yuchen.yycs, "idle", return_value=moving_state), \
     patch.object(yuchen.yycs, "reachable_trap", return_value=[(19, 45), (20, 46)]), \
     patch.object(yuchen.yycs, "fight_named") as combat:
    yuchen.yuemeier.approach_exit(moving_client, Path("."), resource, 0, destination=(20, 46),
                                 magic_file="player-magic-烈火情天.ini")
combat.assert_called_once()
assert combat.call_args.kwargs["magic_file"] == "player-magic-烈火情天.ini"
assert moving_client.move.call_count == 2
hidden = dict(moving_foe, action=255, position=dict(x=19, y=45))
hidden_state = dict(gate_before, targets=[hidden])
with patch.object(yuchen.yycs, "idle", return_value=hidden_state), \
     patch.object(yuchen.yycs, "reachable_trap", return_value=[(19, 45), (20, 46)]) as plan:
    yuchen.yuemeier.approach_exit(Mock(), Path("."), resource, 1)
assert not plan.call_args.args[4] and not plan.call_args.kwargs["avoid"]
print("Yuchen choice parser, opening source and native gate cancellation checks passed")
