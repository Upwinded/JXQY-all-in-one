"""Prepare an isolated Qingyu UI session using a MOD's original opening save."""
import argparse
import configparser
import json
from pathlib import Path
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=("潇湘行", "江湖余尘"), default="潇湘行")
    parser.add_argument("--width", type=int, default=800)
    parser.add_argument("--height", type=int, default=480)
    parser.add_argument("--operations", action="store_true", help="Include isolated dialogue, shop and long-description fixtures")
    parser.add_argument("--companions", action="store_true", help="Include two real companion definitions and equipment in the isolated overlay")
    args = parser.parse_args()
    if args.width < 640 or args.height < 480:
        parser.error("Use a logical viewport of at least 640 x 480")
    if (args.operations or args.companions) and args.profile != "潇湘行":
        parser.error("The operations and companion fixtures use the 潇湘行 opening scene")

    workspace = Path(__file__).resolve().parents[1]
    assets = workspace / "assets"
    profile = assets / args.profile
    manifest = configparser.ConfigParser(interpolation=None)
    manifest.read(profile / "game_profile.ini", encoding="utf-8-sig")
    initial = configparser.ConfigParser(interpolation=None)
    initial.read(profile / "ini/save/game.ini", encoding="utf-8-sig")
    session_id = str(uuid.uuid4())
    session = workspace / "tmp/qingyu-ui-scenes" / session_id
    for folder in ("overlay/script/common", "save", "application-state/save", "diagnostics"):
        (session / folder).mkdir(parents=True, exist_ok=True)

    configuration = f"""[game]
qingyuui=1
fullscreenmode=0
fullscreensolutionmode=0
windowwidth={args.width}
windowheight={args.height}
loadAsync=0
musicvolume=0
soundvolume=0
"""
    (session / "application-state/save/config.ini").write_text(configuration, encoding="utf-8")
    # Load the original opening save so NPCs, equipment, weather and map art
    # receive the same initialization as a normal game. All writes stay isolated.
    entry = "loadgame(0);\nchangemapcolor(255,255,255);\nchangeasfcolor(255,255,255);\nsetmainlum(31);\n"
    if args.profile == "潇湘行":
        entry += 'addmagic("001达摩真经.ini");\naddmagic("001治疗术.ini");\n'
        entry += 'addgoods("goods-m00-金花.ini",5);\naddlife(-90);\n'
    entry += "fadein();\n"
    if args.companions:
        (session / "overlay/ini/npc").mkdir(parents=True, exist_ok=True)
        for number, name in enumerate(("纳兰真", "紫轩"), start=1):
            companion = configparser.ConfigParser(interpolation=None, strict=False)
            companion.optionxform = str
            companion.read(assets / "yycs/ini/npc" / f"{name}.ini", encoding="utf-8-sig")
            companion["INIT"].update({"Kind": "3", "CanEquip": "1", "CanLevelUp": "1",
                                      "ScriptFile": "", "DeathScript": ""})
            filename = f"qingyu-partner-{number}.ini"
            with (session / "overlay/ini/npc" / filename).open("w", encoding="utf-8") as output:
                companion.write(output)
            entry += f'addnpc("{filename}",{79 + number},201,0);\n'
        entry += 'addgoods("goods-jian-1-桃木剑.ini",2);\n'
        entry += 'addgoods("goods-cloth-1-书生服.ini",2);\n'
    if args.operations:
        # Test-only copies live in the overlay, never in published MOD resources.
        fixture = configparser.ConfigParser(interpolation=None)
        fixture.optionxform = str
        fixture.read(profile / "ini/goods/goods-h12-五雷珠.ini", encoding="utf-8-sig")
        fixture["Init"]["Name"] = "长说明验收"
        fixture["Init"]["Intro"] = "此条目用于检查分页说明，所有修改只保存在隔离验收目录。" * 12 + "末页终。"
        fixture["Init"]["Cost"] = "100"
        (session / "overlay/ini/goods").mkdir(parents=True, exist_ok=True)
        with (session / "overlay/ini/goods/qingyu-description.ini").open("w", encoding="utf-8") as output:
            fixture.write(output)
        entry += 'addgoods("qingyu-description.ini",1);\naddmoney(5000);\n'
        entry += 'talk("本次检查商铺买卖、物品说明与存取进度。所有操作均在隔离存档中进行。");\n'
        entry += 'choose("前往商铺检查买卖操作。","查看商铺","继续验收","$QingyuChoice");\n'
        entry += 'buygoods("货1级.ini");\n'
    entry += "return;\n"
    (session / "overlay/script/common/qingyu-acceptance.txt").write_text(entry, encoding="utf-8")

    descriptor = {
        "schemaVersion": 2,
        "sessionId": session_id,
        "assetsCollectionRoot": str(assets),
        "activeResourcePackId": manifest["Game"]["Id"],
        "target": {
            "kind": "scene", "sceneId": "qingyu-ui-acceptance", "sceneName": "青玉界面实景验收",
            "map": "map/" + initial["State"]["Map"], "npc": "", "object": "",
            "entryScript": "script/common/qingyu-acceptance.txt", "playerPosition": [0, 0],
            "integerVariables": {},
        },
        "overlayRoot": str(session / "overlay"),
        "isolatedSaveRoot": str(session / "save"),
        "applicationStateRoot": str(session / "application-state"),
        "diagnosticsPath": str(session / "diagnostics/diagnostics.jsonl"),
        "logPath": str(session / "diagnostics/game.log"),
        "autoExit": {"mode": "manual"},
    }
    (session / "launch.json").write_text(json.dumps(descriptor, ensure_ascii=False, indent=2), encoding="utf-8")
    routing = {"schemaVersion": 1, "roots": [
        {"path": str(assets / name), "roles": ["resource"]}
        for name in (args.profile, "jxqy2", "yycs", "common")
    ]}
    (session / "resource-routing-contract.json").write_text(
        json.dumps(routing, ensure_ascii=False, indent=2), encoding="utf-8")
    print(session / "launch.json")


if __name__ == "__main__":
    main()
