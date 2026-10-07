"""Check actual Yuchen II opening order and learned-skill selection."""
from pathlib import Path
from unittest.mock import Mock
import configparser
import subprocess
import sys
import tempfile

from run_xjxqy_gameplay import assist_saved_branch_numbers

import run_yuchen2_gameplay as yuchen2

resource = Path(__file__).resolve().parents[1] / "assets" / yuchen2.RESOURCE_DIRECTORY
profile = configparser.ConfigParser(interpolation=None)
profile.read(resource / "game_profile.ini", encoding="utf-8-sig")
assert profile["Game"]["Id"] == profile["Save"]["Namespace"] == "JIANGHU_YUCHEN_2"
assert profile["Game"]["Version"] == "1.0" and profile["Resource"]["DependencyId"] == "YYCS"
opening = (resource / "script/map/map_020_樱花谷/begin.txt").read_text(encoding="utf-8-sig")
assert opening.index('playerchange(1)') < opening.index('setlevelfile(')
assert 'setlevelfile("level-hard.ini")' in opening and 'setlevelfile("level-easy.ini")' in opening
for gender, expected in ((0, "player-magic-烈火情天.ini"), (1, "player-magic-逆转心经.ini")):
    magic = configparser.ConfigParser(interpolation=None)
    magic.read(resource / f"ini/save/magic{gender}.ini", encoding="utf-8-sig")
    learned = [dict(file=magic[section]["IniFile"]) for section in magic if "IniFile" in magic[section]]
    client = Mock()
    client.observe.return_value = dict(magic=learned)
    assert yuchen2.combat_magic(client) == expected
client.observe.return_value = dict(magic=[dict(file="001杀意.ini")])
try:
    yuchen2.combat_magic(client)
except yuchen2.AutomationError:
    pass
else:
    raise AssertionError("A utility skill must not be used as an assumed offensive skill")
with tempfile.TemporaryDirectory() as temporary:
    folder = Path(temporary)
    before = b'[init]\r\nmoney=400\r\nlevel=80\r\n'
    for index in (0, 1):
        (folder / f"player{index}.ini").write_bytes(before)
    corrections = assist_saved_branch_numbers(folder, money=2000, money_character_index=1)
    assert (folder / "player0.ini").read_bytes() == before
    assert (folder / "player1.ini").read_bytes() == before.replace(b'money=400', b'money=2000')
    assert corrections[0]['beforeValue'] == 400 and corrections[0]['afterValue'] == 2000
    for route_arguments in (("--route=huian-entry",), ("--route", "opening", "--route", "huian-entry")):
        output = folder / "unstarted-route"
        result = subprocess.run([sys.executable, str(Path(yuchen2.__file__)), *route_arguments,
                                 "--output", str(output)], capture_output=True, text=True, encoding="utf-8")
        assert result.returncode == 2 and "invalid choice" in result.stderr
        assert not output.exists(), "A first-game route must be rejected before creating a test process"
print("Yuchen II actual opening, identity, skill and character-specific money checks passed")
