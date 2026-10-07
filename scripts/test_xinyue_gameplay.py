"""Small checks for Xinyue identity, dependency precedence and ending candidates."""
from pathlib import Path
import re

from gameplay_automation import AutomationError
from run_xinyue_gameplay import RESOURCE_ID, RESOURCE_DIRECTORY, SAVE_NAMESPACE, fight, source_candidates


def main():
    root = Path(__file__).resolve().parents[1] / "assets"
    catalog = source_candidates(root)
    sources = {row["path"]: row for row in catalog["sources"]}
    assert RESOURCE_ID == "XINYUE_WUHEN_3_0" and SAVE_NAMESPACE == "xinyue_wuhen_3_0"
    assert all(Path(row["sourceRoot"]).name in (RESOURCE_DIRECTORY, "jxqy2") for row in sources.values())
    assert Path(sources["script/common/newgame.txt"]["sourceRoot"]).name == RESOURCE_DIRECTORY
    for filename in ("大结局.txt", "真结局.txt", "飞云败.txt"):
        path = f"script/map/天忍教-地下迷宫3/{filename}"
        assert Path(sources[path]["sourceRoot"]).name == RESOURCE_DIRECTORY
        text = (root / RESOURCE_DIRECTORY / path).read_text(encoding="utf-8")
        assert 'loadmap("主角家-狂沙镇.map")' in text and "returntotitle(" not in text
    assert catalog["fullCoverage"] is False
    night = (root / RESOURCE_DIRECTORY / "ini/save/ks-1.npc").read_text(encoding="utf-8-sig")
    sections = re.findall(r"(?m)^\[(NPC\d{3})\]", night)
    assert list(dict.fromkeys(sections)) == [f"NPC{index:03d}" for index in range(61)]
    assert "Count=61" in night and len(sections) == 62
    battle = (root / RESOURCE_DIRECTORY / "ini/save/zhongdukill.npc").read_text(encoding="utf-8-sig")
    assert re.findall(r"(?m)^\[(NPC\d{3})\]", battle) == [f"NPC{index:03d}" for index in range(130)]
    assert "Count=130" in battle and len(re.findall(r"(?m)^Name=金国将领\r?$", battle)) == 41
    assert "DeathScript=邂逅夜明珠.txt" in battle and "DeathScript=中都夜金兵头目.txt" in battle
    for filename, prefix in (("changan.npc", "NPC"), ("changan1.npc", "NPC"),
                             ("home-ks.npc", "NPC"), ("home.obj", "OBJ"), ("pilitang.npc", "NPC")):
        content = (root / RESOURCE_DIRECTORY / "ini/save" / filename).read_text(encoding="utf-8-sig")
        headers = list(dict.fromkeys(re.findall(r"(?m)^\[(" + prefix + r"\d{3})\]", content)))
        assert headers == [f"{prefix}{index:03d}" for index in range(len(headers))], filename
        assert int(re.search(r"(?m)^Count=(\d+)", content)[1]) == len(headers), filename
    class EmptySkillClient:
        def observe(self, variables=()):
            return dict(scene="MainScene", worldInput=True, cheatInvincibilityEnabled=True,
                        layout=dict(magicQuickBegin=36), magic=[])

    try:
        fight(EmptySkillClient(), dict(id=1))
    except AutomationError as error:
        assert "Assigned learned skill is absent" in str(error)
    else:
        raise AssertionError("An empty skill slot must stop before native combat submission")
    print("Xinyue identity, dependency precedence, post-movie candidates and learned-slot guard passed")


if __name__ == "__main__":
    main()
