"""Small checks for resource precedence and conservative native coverage counting."""
import hashlib
import json
from pathlib import Path
import tempfile

from audit_chenghe_gameplay import audit
from run_chenghe_gameplay import RESOURCE_DIRECTORY, RESOURCE_ID, save_hashes, source_candidates, write_json
from run_jxqy2_mainline import tournament_resources


def main():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        manual = root / "manual"
        (manual / "nested").mkdir(parents=True)
        (manual / "game.ini").write_text("[state]\nmap=临安城.map\n", encoding="utf-8")
        (manual / "nested/goods.ini").write_bytes(b"unchanged")
        hashes = save_hashes(manual)
        assert set(hashes) == {"game.ini", "nested/goods.ini"}
        (manual / "nested/goods.ini").write_bytes(b"changed")
        assert save_hashes(manual) != hashes and save_hashes(root / "missing") == {}
        assets = root / "assets"
        relative = "script/map/test/story.txt"
        local = assets / RESOURCE_DIRECTORY / relative
        local.parent.mkdir(parents=True)
        local.write_text('if getvar("Gate") == 1 then goto done end\ntalk("blocked");\n'
                         '::done::\ntalk("ready");\nreturn;\n', encoding="utf-8")
        dependency = assets / "jxqy2" / relative
        dependency.parent.mkdir(parents=True)
        dependency.write_text('return;\n', encoding="utf-8")
        (dependency.parent / "fallback.txt").write_text('return;\n', encoding="utf-8")
        catalog = source_candidates(assets)
        assert catalog["counts"]["scripts"] == 2
        assert len(catalog["conditions"]) == 1
        assert next(site for site in catalog["sources"] if site["path"] == relative)["sourceRoot"] == str(local.parents[3])
        directory = root / "run"
        trace_folder = directory / "user-data/automation"
        trace_folder.mkdir(parents=True)
        write_json(directory / "run.json", dict(resourceId=RESOURCE_ID, session="chenghe-check", cheatAssisted=False))
        digest = hashlib.sha256(local.read_bytes()).hexdigest()
        events = [dict(eventType="session.start", sessionId="native-check")]
        def execution(identity, source_hash, next_line, status):
            events.extend([dict(eventType="script.start", executionId=identity, virtualPath=relative, contentSha256=source_hash),
                           dict(eventType="source.line", executionId=identity, line=1),
                           dict(eventType="api.call", executionId=identity, apiName="getvar"),
                           dict(eventType="source.line", executionId=identity, line=next_line)])
            if status:
                events.append(dict(eventType="script.finish", executionId=identity, status=status))
        execution(1, digest, 4, "completed")
        execution(2, digest, 2, None)  # No completed script, no coverage.
        execution(3, digest, 2, "error")
        execution(4, "outdated-source", 2, "completed")
        (trace_folder / "trace.jsonl").write_text("".join(
            json.dumps(dict(event, sequence=index)) + "\n" for index, event in enumerate(events, 1)), encoding="utf-8")
        result = audit(assets, [directory])
        assert result["completedExecutionCount"] == result["completedScriptCount"] == 1
        assert result["observedConditionSides"] == 1 and result["conditionSitesBothSides"] == 0
        assert list(result["conditionOutcomes"][relative + ":1"]) == ["taken"]
        assert len(result["scriptErrors"]) == len(result["staleExecutions"]) == 1
        assert result["fullCoverage"] is False and result["fullPlaythrough"] is False
        assisted = root / "assisted"
        (assisted / "user-data/automation").mkdir(parents=True)
        write_json(assisted / "run.json", dict(resourceId=RESOURCE_ID, session="chenghe-assisted", cheatAssisted=True))
        (assisted / "user-data/automation/trace.jsonl").write_text("".join(
            json.dumps(dict(event, sequence=index)) + "\n" for index, event in enumerate(events[:6], 1)), encoding="utf-8")
        combined = audit(assets, [directory, assisted])
        assert combined["completedExecutionCount"] == 2
        assert combined["assistedCompletedExecutionCount"] == combined["unassistedCompletedExecutionCount"] == 1
        assert combined["unassistedCompletedScriptCount"] == 1
        # MOD section casing and spell identities differ from the base game.
        npc = assets / RESOURCE_DIRECTORY / "ini/save/fengchibw.npc"
        npc.parent.mkdir(parents=True)
        npc.write_text("[NPC000]\nName=赵无双\nDefend=900\n", encoding="utf-8")
        for filename in ("0player-magic-天意剑诀.ini", "0player-magic-梦蝶神功.ini"):
            spell = assets / RESOURCE_DIRECTORY / "ini/magic" / filename
            spell.parent.mkdir(parents=True, exist_ok=True)
            spell.write_text("".join(f"[Level{level}]\nEffect={500 + level}\nManaCost={level}\n"
                                      for level in range(1, 11)), encoding="utf-8")
        write_json(directory / "run.json", dict(command=["--assets", str(assets)]))
        defenses, spells = tournament_resources(directory, resource_directory=RESOURCE_DIRECTORY,
                                                magic_file_prefix="0player-magic-")
        assert defenses == {"赵无双": 900}
        assert spells["0player-magic-天意剑诀.ini"][5] == dict(effect=505, manaCost=5, thewCost=0)
    print("Chenghe resource precedence and incomplete/error/stale coverage checks passed")


if __name__ == "__main__":
    main()
