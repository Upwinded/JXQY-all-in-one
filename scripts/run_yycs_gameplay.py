"""Inventory YYCS branches and execute normal saved-game story routes."""
from __future__ import annotations

import argparse
from collections import deque
import configparser
import ctypes
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time
import uuid

from gameplay_automation import Client, AutomationError, npc_attackable

VARIABLES = ("Event", "Result", "Level", "SenseVal", "EvilVal", "EvilValue",
             "zixuan", "SubEvent01", "SubEvent02", "SubEvent03", "Enemy", "SelectVal",
             "Sub01Death", "Sub02Rand", "GetMoney", "Enemy006", "wangwei01")
VARIABLES += ("DeadRobber", "DeadRobber1", "Sub03TalkTimes", "SubEvent19", "YinyuanSense")
STRING = re.compile(r'"(?:\\.|[^"\\])*"')


def write_json(path, value):
    identity_path = path.parent / "run.json"
    if (path != identity_path and isinstance(value, dict) and "cheatAssisted" in value
            and identity_path.exists() and json.loads(identity_path.read_text(encoding="utf-8")).get("cheatAssisted") is True):
        value["cheatAssisted"] = True
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def source_line(line):
    """Remove line comments without treating -- inside a Lua string as a comment."""
    quoted = escaped = False
    for index, character in enumerate(line):
        if escaped:
            escaped = False
        elif quoted and character == "\\":
            escaped = True
        elif character == '"':
            quoted = not quoted
        elif not quoted and line[index:index + 2] == "--":
            return line[:index]
    return line


def inventory(resource):
    """Source obligations are candidates until normal gameplay proves reachability."""
    choices, conditions, terminals, sources = [], [], [], []
    talk_file = resource / "script/common/talkindex.txt"
    talks = {}
    if talk_file.is_file():
        for line in talk_file.read_text(encoding="utf-8-sig").splitlines():
            match = re.match(r"\[(\d+),-?\d+\](.*)", line)
            if match:
                talks[int(match[1])] = match[2]
    choice_count = select_count = 0
    for path in sorted((resource / "script").rglob("*.txt")):
        relative = path.relative_to(resource).as_posix()
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        sources.append(dict(path=relative, sha256=digest))
        label = "entry"
        for number, original in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
            line = source_line(original).strip()
            match = re.match(r"::([^:]+)::", line)
            if match:
                label = match[1]
            site = dict(id=f"{relative}:{number}", path=relative, line=number,
                        label=label, source=line, sourceSha256=digest,
                        reachability="unverified")
            if re.match(r"(?:choose(?:ex|plus|multiple)?|select)\s*\(", line, re.I):
                strings = [json.loads(value) for value in STRING.findall(line)]
                selected = re.fullmatch(r'select\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*("(?:\\.|[^"\\])*")\s*\);?', line, re.I)
                plus = re.fullmatch(r'chooseplus\s*\(\s*"(?:\\.|[^"\\])*"\s*,\s*"?-?\d+"?\s*,\s*"?-?\d+"?\s*,(.*)\);?', line, re.I)
                missing = []
                if selected:
                    indices = [int(value) for value in selected.groups()[:3]]
                    missing = [index for index in indices if index not in talks]
                    strings = [talks.get(index, f"[对白编号 {index} 未找到]") for index in indices] + strings
                    select_count += 1
                    coverage_id, api = f"S{select_count:02}", "select"
                elif plus:
                    strings = [json.loads(value) for value in STRING.findall(plus[1])]
                    if len(strings) < 3:
                        raise ValueError(f"ChoosePlus needs a message, option and variable: {site['id']}")
                    choice_count += 1
                    coverage_id, api = f"C{choice_count:02}", "chooseplus"
                elif len(strings) >= 4 and re.match(r"choose(?:ex)?\s*\(", line, re.I):
                    choice_count += 1
                    coverage_id = f"C{choice_count:02}"
                    api = "chooseex" if re.match(r"chooseex\s*\(", line, re.I) else "choose"
                else:
                    raise ValueError(f"Choice needs explicit parsing: {site['id']}")
                choices.append(dict(**site, coverageId=coverage_id, api=api, missingTalkIds=missing,
                                    message=strings[0], variable=strings[-1],
                                    options=[dict(index=index, text=text, status="pending")
                                             for index, text in enumerate(strings[1:-1])]))
            if re.match(r"if\s+", line) and "getvar(" in line:
                conditions.append(dict(**site, outcomes=dict(taken="pending", fallthrough="pending")))
            if re.match(r"(?:playmovie|returntotitle)\s*\(", line, re.I):
                terminals.append(site)
    return dict(resourceId="YYCS", inventoryType="source-candidates", fullCoverage=False,
                sources=sources, choices=choices, conditions=conditions, terminals=terminals,
                talkIndexSha256=hashlib.sha256(talk_file.read_bytes()).hexdigest() if talk_file.is_file() else None,
                counts=dict(scripts=len(sources), choiceSites=len(choices), chooseSites=choice_count,
                            selectSites=select_count,
                            choiceOptions=sum(len(site["options"]) for site in choices),
                            conditionalSites=len(conditions), mediaAndReturnSites=len(terminals)))


def opening_evidence(directory):
    identity = json.loads((directory / "run.json").read_text(encoding="utf-8"))
    state = json.loads((directory / "03-opening-complete.json").read_text(encoding="utf-8"))
    choice = json.loads((directory / "02-difficulty-choice.json").read_text(encoding="utf-8"))
    hard = identity["difficulty"] == "hard"
    option = 0 if hard else 1
    expected = dict(Event="10", Result="0", SenseVal="1000", EvilVal="1000", Level=str(option))
    if (identity["resourceId"] != "YYCS" or identity.get("cheatAssisted")
            or state["map"] != "map_002_凌绝峰峰顶.map"
            or any(state["variables"].get(key) != value for key, value in expected.items())
            or state["player"]["level"] != (4 if hard else 5)
            or state["player"]["levelFile"] != ("level-hard.ini" if hard else "level-easy.ini")):
        raise ValueError(f"Native opening assertions failed: {directory}")
    for name in ("02-difficulty-choice.png", "03-opening-complete.png"):
        if not (directory / name).is_file():
            raise ValueError(f"Opening screenshot is missing: {directory / name}")
    trace = [json.loads(line) for line in (directory / "user-data/automation/trace.jsonl").read_text(
        encoding="utf-8").splitlines()]
    starts = [record for record in trace if record.get("eventType") == "script.start"
              and record.get("virtualPath", "").endswith("map_002_凌绝峰峰顶/begin.txt")]
    if len(starts) != 1:
        raise ValueError(f"Opening script start is missing or ambiguous: {directory}")
    execution = [record for record in trace if record.get("executionId") == starts[0]["executionId"]]
    if not (any(record.get("eventType") == "script.finish" and record.get("status") == "completed"
                for record in execution) and any(record.get("apiName") == "choose" for record in execution)):
        raise ValueError(f"Native opening choice has no completed script trace: {directory}")
    commands = [json.loads(line) for line in (directory / "commands.jsonl").read_text(encoding="utf-8").splitlines()]
    chosen = [record for record in commands if record["request"]["command"] == "Choose"
              and record["request"]["arguments"] == dict(context=choice["context"], options=[option])
              and record["response"].get("ok")]
    if len(chosen) != 1:
        raise ValueError(f"Opening option has no matching actual command: {directory}")
    action = chosen[0]["response"]["data"]["actionId"]
    if not any(record["request"]["command"] == "GetActionStatus"
               and record["response"].get("data", {}).get("actionId") == action
               and record["response"]["data"].get("status") == "succeeded" for record in commands):
        raise ValueError(f"Opening choice action did not succeed: {directory}")
    return dict(choiceSite=starts[0]["virtualPath"] + ":30", sourceSha256=starts[0]["contentSha256"],
                choiceIndex=option, scriptCompleted=True, scriptStart=starts[0], variables=state["variables"],
                player=state["player"], evidenceDirectory=str(directory.resolve()))


def choice_variable_changes(records, sequence, variable):
    end = next((record["sequence"] for record in records
                if record.get("eventType") == "source.line" and record["sequence"] > sequence), float("inf"))
    return [record for record in records if record.get("eventType") == "variable.change"
            and record.get("variableName") == variable and sequence < record["sequence"] < end]


def choice_evidence(directory, path, site, *, trace=None, commands=None, allow_cheats=False):
    recorded = json.loads(path.read_text(encoding="utf-8"))
    identity = json.loads((directory / "run.json").read_text(encoding="utf-8"))
    assisted = identity.get("cheatAssisted")
    if (identity.get("resourceId") != "YYCS" or not isinstance(assisted, bool)
            or assisted and not allow_cheats or recorded.get("cheatAssisted") is not assisted
            or recorded.get("status") != "passed"
            or recorded["sourceSha256"] != site["sourceSha256"] or not recorded.get("scriptCompleted")):
        raise ValueError(f"Choice proof is incomplete or uses different resources: {path}")
    before = json.loads(Path(recorded["beforeFile"]).read_text(encoding="utf-8"))
    after = json.loads(Path(recorded["afterFile"]).read_text(encoding="utf-8"))
    option = recorded["choiceIndex"]
    if (option not in (0, 1) or before.get("choiceMessage") != site["message"]
            or [item["text"] for item in before.get("choices", [])] != [item["text"] for item in site["options"]]
            or not (after.get("worldInput") or after.get("scene") == "Title" and recorded.get("expectedTerminal"))):
        raise ValueError(f"Choice snapshots do not establish its native outcome: {path}")
    execution = [record for record in (trace_records(directory) if trace is None else trace)
                 if record.get("executionId") == recorded["scriptStart"]["executionId"]]
    source = [record for record in execution if record.get("eventType") == "script.start"]
    finished = [record for record in execution if record.get("eventType") == "script.finish"]
    line = [record for record in execution if record.get("eventType") == "source.line" and record.get("line") == site["line"]]
    changed = recorded.get("variableChange")
    choice_changes = choice_variable_changes(execution, line[0]["sequence"], site["variable"]) if line else []
    variable_proven = (changed in choice_changes and changed.get("variableName") == site["variable"]
                       and changed.get("afterValue") == str(option)
                       and bool(line) and changed["sequence"] > line[0]["sequence"]) if changed else (
        recorded.get("variableUnchanged") is True
        and before.get("variables", {}).get(site["variable"]) == str(option)
        and bool(line) and not choice_changes)
    if (source != [recorded["scriptStart"]] or source[0]["virtualPath"].casefold() != site["path"].casefold()
            or source[0].get("contentSha256") != site["sourceSha256"]
            or len(finished) != 1 or finished[0].get("status") != "completed" or not line
            or not variable_proven
            or not any(record.get("apiName") == site["api"] for record in execution)):
        raise ValueError(f"Choice has no matching complete native execution: {path}")
    if commands is None:
        commands = [json.loads(line) for line in (directory / "commands.jsonl").read_text(encoding="utf-8").splitlines()]
    action_id = recorded["action"]["actionId"]
    chosen = [record for record in commands if record["request"]["command"] == "Choose"
              and record["request"]["arguments"] == dict(context=before["context"], options=[option])
              and record["response"].get("ok") and record["response"]["data"].get("actionId") == action_id]
    succeeded = any(record["request"]["command"] == "GetActionStatus"
                    and record["response"].get("data", {}).get("actionId") == action_id
                    and record["response"]["data"].get("status") == "succeeded" for record in commands)
    if len(chosen) != 1 or not succeeded:
        raise ValueError(f"Choice has no matching successful normal command: {path}")
    if recorded.get("expectedTerminal"):
        terminal = recorded["expectedTerminal"]
        if (site["path"] not in ("script/map/map_025_摘星楼/杨影枫死亡.txt", "script/map/map_062_禁地密室/杨影枫死亡.txt")
                or terminal != "script/common/主角死亡.txt" or after.get("scene") != "Title"):
            raise ValueError(f"Choice has an unexpected terminal outcome: {path}")
        terminal_starts = [record for record in (trace_records(directory) if trace is None else trace)
                           if record == recorded.get("terminalStart") and record.get("eventType") == "script.start"
                           and record.get("virtualPath") == terminal and record["sequence"] > line[0]["sequence"]]
        assets = Path(identity["command"][identity["command"].index("--assets") + 1])
        if len(terminal_starts) != 1 or terminal_starts[0]["contentSha256"] != hashlib.sha256((assets / "yycs" / terminal).read_bytes()).hexdigest():
            raise ValueError(f"Choice has no matching native death source: {path}")
        ending = [record for record in (trace_records(directory) if trace is None else trace)
                  if record.get("executionId") == terminal_starts[0]["executionId"]]
        followup = commands[commands.index(chosen[0]) + 1:]
        title_index = next((index for index, record in enumerate(followup)
                            if record["request"]["command"] == "Observe"
                            and record["response"].get("data", {}).get("scene") == "Title"), None)
        if (not any(record.get("eventType") == "script.finish" and record.get("status") == "completed" for record in ending)
                or not any(record.get("apiName") == "returntotitle" for record in ending)
                or title_index is None
                or not any(record["request"]["command"] == "Observe"
                           and record["response"].get("data", {}).get("video") == "die.wmv"
                           for record in followup[:title_index])):
            raise ValueError(f"Choice death, movie, or title evidence is incomplete: {path}")
    return recorded


def save_inventory(resource, output, evidence=(), allow_cheats=False):
    for directory in evidence:
        if not directory.is_dir():
            raise ValueError(f"Evidence directory is missing: {directory}")
    catalog = inventory(resource)
    for directory in evidence:
        if (directory / "opening-proof.json").exists():
            proof = opening_evidence(directory)
            site = next(item for item in catalog["choices"] if item["id"] == proof["choiceSite"])
            if proof["sourceSha256"] != site["sourceSha256"] or not proof["scriptCompleted"]:
                raise ValueError(f"Opening proof has different source or incomplete trace: {directory}")
            option = site["options"][proof["choiceIndex"]]
            audit = output / f"opening-audit-{site['options'].index(option)}.json"
            write_json(audit, proof)
            option.update(status="passed", evidence=str(audit))
            site["reachability"] = "observed"
        proof_paths = sorted(directory.glob("choice-proof-*.json"))
        trace = trace_records(directory) if proof_paths else []
        commands = ([json.loads(line) for line in (directory / "commands.jsonl").read_text(encoding="utf-8").splitlines()]
                    if proof_paths else [])
        for path in proof_paths:
            recorded = json.loads(path.read_text(encoding="utf-8"))
            selected = next(item for item in catalog["choices"] if item["id"] == recorded["choiceSite"])
            choice_evidence(directory, path, selected, trace=trace, commands=commands, allow_cheats=allow_cheats)
            option = selected["options"][recorded["choiceIndex"]]
            if option["status"] != "passed" or option.get("cheatAssisted", False):
                option.update(status="passed", evidence=str(path.resolve()), cheatAssisted=recorded["cheatAssisted"])
            selected["reachability"] = "observed"
    catalog["counts"]["passedChoiceOptions"] = sum(
        option["status"] == "passed" for site in catalog["choices"] for option in site["options"])
    catalog["counts"]["cheatAssistedPassedChoiceOptions"] = sum(
        option["status"] == "passed" and option.get("cheatAssisted", False)
        for site in catalog["choices"] for option in site["options"])
    catalog["counts"]["normalPassedChoiceOptions"] = (
        catalog["counts"]["passedChoiceOptions"] - catalog["counts"]["cheatAssistedPassedChoiceOptions"])
    write_json(output / "branch-catalog.json", catalog)
    rows = ["# 月影传说选择覆盖表", "", "来源为当前资源脚本。C 表示 choose，S 表示 select，对白编号按 script/common/talkindex.txt 解析。每行是一个独立调用点；相同题目在不同剧情阶段分别验收。可达性须由正常路线核实。", "",
            "| 编号 | 地图和脚本 | 行号 | 前置标签 | 题目 | 选项和状态 |", "| --- | --- | --- | --- | --- | --- |"]
    for site in catalog["choices"]:
        options = "；".join(f"{item['index']} {item['text']}（{'作弊辅助通过' if item.get('cheatAssisted') else '已通过' if item['status'] == 'passed' else '待测'}）"
                           for item in site["options"])
        rows.append(f"| {site['coverageId']} | {site['path']} | {site['line']} | {site['label']} | {site['message']} | {options} |")
    (output / "choice-coverage.md").write_text("\n".join(rows) + "\n", encoding="utf-8")
    return catalog


def checkpoint(client, output, name, variables=()):
    state = client.observe((*VARIABLES, *variables))
    write_json(output / f"{name}.json", state)
    if not state.get("outputHealthy"):
        raise AutomationError("Required trace output is unavailable")
    shutil.copyfile(client.snapshot(), output / f"{name}.png")
    return state


def trace_records(output):
    with (output / "user-data/automation/trace.jsonl").open(encoding="utf-8") as stream:
        return [json.loads(line) for line in stream if line.endswith("\n")]


def completed_script(output, start):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        records = [record for record in trace_records(output) if record.get("executionId") == start["executionId"]]
        finished = [record for record in records if record.get("eventType") == "script.finish"]
        if finished:
            if len(finished) != 1 or finished[0].get("status") != "completed":
                raise AutomationError(f"Script did not complete normally: {finished}")
            return records
        time.sleep(0.1)
    raise AutomationError(f"Script completion trace is missing: {start['virtualPath']}")


def choice_matches_source(site, state, option):
    if state.get("choiceMessage") != site["message"]:
        return False
    conditions = re.compile(r'\{\$[^}]*\}')
    expected = {item["index"]: conditions.sub("", item["text"]) for item in site["options"]
                if conditions.sub("", item["text"])}
    visible = {item.get("index"): item["text"] for item in state.get("choices", [])}
    unconditional = {item["index"] for item in site["options"]
                     if item["text"] and not conditions.search(item["text"])}
    return (len(visible) == len(state.get("choices", [])) and option in visible
            and unconditional <= visible.keys() and visible.keys() <= expected.keys()
            and all(text == expected[index] for index, text in visible.items()))


def choose_site(client, output, resource, site_id, option, remaining=(), expected_terminal=None):
    site = next(site for site in inventory(resource)["choices"] if site["id"] == site_id)
    state = client.observe((*VARIABLES, site["variable"]))
    if not choice_matches_source(site, state, option):
        raise AutomationError(f"Choice does not match {site_id}: {state.get('choices')}")
    deadline = time.monotonic() + 3
    while True:
        trace = trace_records(output)
        starts = [record for record in trace if record.get("eventType") == "script.start"
                  and record.get("virtualPath", "").casefold() == site["path"].casefold()]
        if starts and starts[-1]["contentSha256"] != site["sourceSha256"]:
            raise AutomationError(f"Choice source is missing or changed: {site_id}")
        start = starts[-1] if starts else None
        lines = [record for record in trace if start and record.get("executionId") == start["executionId"]
                 and record.get("eventType") == "source.line"]
        if lines and lines[-1]["line"] == site["line"]:
            break
        if time.monotonic() >= deadline:
            raise AutomationError(f"Choice has no matching active source line: {site_id}")
        time.sleep(0.05)
    name = f"choice-{site['coverageId']}-{option}-{time.time_ns()}"
    write_json(output / f"{name}-before.json", state)
    shutil.copyfile(client.snapshot(), output / f"{name}-before.png")
    action = client.act("Choose", context=state["context"], options=[option])
    after = idle(client, choice=remaining or None, output=output, resource=resource, expected_terminal=expected_terminal)
    records = completed_script(output, start)
    changed = choice_variable_changes(records, lines[-1]["sequence"], site["variable"])
    unchanged = not changed and state["variables"].get(site["variable"]) == str(option)
    if ((not unchanged and (not changed or changed[0].get("afterValue") != str(option)))
            or not any(record.get("apiName") == site["api"] for record in records)):
        raise AutomationError(f"Choice lacks native variable/API evidence: {site_id}")
    checkpoint(client, output, name + "-after", variables=(site["variable"],))
    proof = dict(status="passed", choiceSite=site_id, sourceSha256=site["sourceSha256"],
                 choiceIndex=option, scriptCompleted=True, scriptStart=start,
                 variableChange=changed[0] if changed else None, variableUnchanged=unchanged,
                 action=action, beforeFile=str(output / f"{name}-before.json"),
                  afterFile=str(output / f"{name}-after.json"), cheatAssisted=False, fullPlaythrough=False)
    if expected_terminal:
        proof["expectedTerminal"] = expected_terminal
        proof["terminalStart"] = next(record for record in reversed(trace_records(output))
                                      if record.get("eventType") == "script.start" and record.get("virtualPath") == expected_terminal)
    write_json(output / f"choice-proof-{name}.json", proof)
    print(f"Choice verified: {site['coverageId']} / {option} / {after.get('variables', {})}", flush=True)
    return after


def idle(client, timeout=None, choice=None, output=None, resource=None, expected_terminal=None, final_dialogue="剧终"):
    if timeout is None:
        timeout = 600 if expected_terminal else 180
    deadline = time.monotonic() + timeout
    captured_dialogues = set()
    while time.monotonic() < deadline:
        state = client.observe(VARIABLES)
        if state.get("choices"):
            if choice is not None:
                sequence = [choice] if isinstance(choice[0], str) else choice
                return choose_site(client, output, resource, *sequence[0], remaining=sequence[1:], expected_terminal=expected_terminal)
            raise AutomationError(f"Unmapped choice: {state['script']} / {state['choices']}")
        if state.get("scene") == "Title":
            if expected_terminal:
                start = next(record for record in reversed(trace_records(output))
                             if record.get("eventType") == "script.start" and record.get("virtualPath", "").casefold() == expected_terminal.casefold())
                if start["contentSha256"] != hashlib.sha256((resource / expected_terminal).read_bytes()).hexdigest() or not any(
                        record.get("apiName") == "returntotitle" for record in completed_script(output, start)):
                    raise AutomationError("Expected terminal source did not complete its native return")
                return state
            raise AutomationError("Unexpected return to title")
        if state.get("video"):
            if output is not None:
                checkpoint(client, output, f"story-video-{state['context']}")
            client.ui("Cancel")
        elif (expected_terminal and output is not None and state["context"] not in captured_dialogues
              and any(text in state.get("dialogue", {}).get("text", "")
                      for text in ((final_dialogue,) if isinstance(final_dialogue, str) else final_dialogue))):
            client.act("SetAutoDialogue", enabled=False)
            rendered = client.wait_until(lambda value: value.get("context") == state["context"]
                                         and value.get("dialogue", {}).get("complete"), timeout=15,
                                         description="fully rendered final dialogue")
            client.wait_until(lambda value: value.get("context") == state["context"]
                              and value.get("frame", 0) >= rendered["frame"] + 30
                              and value.get("dialogue", {}).get("complete"), timeout=5,
                              description="presented final dialogue frames")
            checkpoint(client, output, f"ending-dialogue-{state['context']}")
            captured_dialogues.add(state["context"])
            client.act("SetAutoDialogue", enabled=True, intervalMs=1000)
        elif state.get("worldInput") and not expected_terminal:
            return state
        elif (not expected_terminal and state.get("scene") == "MainScene" and not state.get("inEvent")
              and any(item["name"].startswith("goods-item-") for item in state.get("ui", []))):
            client.ui("Cancel")
        time.sleep(0.2)
    raise TimeoutError(f"Story did not settle: {state.get('map')} / {state.get('script')}")


def opening(client, output, difficulty):
    state = client.wait_until(lambda value: value["scene"] == "Title", description="YYCS title")
    if state["resourceId"] != "YYCS":
        raise AutomationError(f"Wrong resource: {state['resourceId']}")
    checkpoint(client, output, "01-title")
    client.act("SetAutoDialogue", enabled=True)
    client.activate("new-game")
    videos = []
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        state = client.observe(VARIABLES)
        if state.get("video"):
            videos.append(state["video"])
            client.ui("Cancel")
        elif state.get("choices"):
            if (state.get("choiceMessage") != "请选择游戏的难易："
                    or [item["text"] for item in state["choices"]] != ["难", "易"]):
                raise AutomationError(f"Unexpected opening choice: {state}")
            checkpoint(client, output, "02-difficulty-choice")
            client.act("Choose", context=state["context"], options=[0 if difficulty == "hard" else 1])
            break
        time.sleep(0.2)
    else:
        raise TimeoutError("Opening difficulty choice was not reached")
    state = idle(client)
    expected = dict(Event="10", Result="0", SenseVal="1000", EvilVal="1000",
                    Level="0" if difficulty == "hard" else "1")
    if state.get("map") != "map_002_凌绝峰峰顶.map" or any(
            state["variables"].get(key) != value for key, value in expected.items()):
        raise AutomationError(f"Opening state mismatch: {state}")
    if state["player"]["level"] != (4 if difficulty == "hard" else 5):
        raise AutomationError(f"Difficulty did not apply native player level: {state['player']}")
    checkpoint(client, output, "03-opening-complete")
    client.save_or_load(0)
    checkpoint(client, output, "04-opening-saved")
    proof = opening_evidence(output)
    proof.update(difficulty=difficulty, observedVideos=videos, fullPlaythrough=False, cheatAssisted=False)
    write_json(output / "opening-proof.json", proof)
    print(f"Normal {difficulty} opening saved in slot 0", flush=True)


def trap_points(resource, map_name, trap):
    data = (resource / "map" / map_name).read_bytes()
    _, width, height, image_size, _ = struct.unpack_from("<5i", data, 64)
    header, _, _, image_count = struct.unpack_from("<4i", data, 84)
    offset = header + image_count * image_size
    if width <= 0 or height <= 0 or offset + width * height * 10 > len(data):
        raise ValueError(f"Invalid map dimensions: {map_name}")
    return [(x, y) for y in range(height) for x in range(width)
            if data[offset + (y * width + x) * 10 + 7] == trap
            and not data[offset + (y * width + x) * 10 + 6] & 0xc0]


def reachable_trap(resource, map_name, trap, position, occupied=(), with_path=False, avoid=(), destination=None):
    """Pick an exit connected to the player, rather than an isolated edge tile."""
    data = (resource / "map" / map_name).read_bytes()
    _, width, height, image_size, _ = struct.unpack_from("<5i", data, 64)
    header, _, _, image_count = struct.unpack_from("<4i", data, 84)
    offset = header + image_count * image_size
    goals = ({destination} if destination is not None else set(trap_points(resource, map_name, trap))) - set(occupied)
    start = (position["x"], position["y"])
    avoid = set(avoid)
    queue, seen = deque([start]), {start}
    previous = {}
    while queue:
        point = queue.popleft()
        if point in goals:
            if with_path:
                path = [point]
                while path[-1] != start:
                    path.append(previous[path[-1]])
                return list(reversed(path))
            return point
        x, y = point
        neighbors = ((x, y + 2), (x + y % 2 - 1, y + 1), (x - 1, y),
                     (x + y % 2 - 1, y - 1), (x, y - 2),
                     (x + y % 2, y - 1), (x + 1, y), (x + y % 2, y + 1))
        for direction, neighbor in enumerate(neighbors):
            nx, ny = neighbor
            if (0 <= nx < width and 0 <= ny < height and neighbor not in seen and neighbor not in avoid
                    and not data[offset + (ny * width + nx) * 10 + 6] & 0xc0):
                if direction % 2 == 0:
                    sides = (neighbors[(direction - 1) % 8], neighbors[(direction + 1) % 8])
                    if any(not (0 <= sx < width and 0 <= sy < height)
                           or (data[offset + (sy * width + sx) * 10 + 6] & 0xc0
                               and not data[offset + (sy * width + sx) * 10 + 6] & 0x40)
                           for sx, sy in sides):
                        continue
                seen.add(neighbor)
                previous[neighbor] = point
                queue.append(neighbor)
    raise AutomationError(f"No connected trap {trap}: {map_name} from {start}")


def transition(client, resource, destination, trap, timeout=180, choice=None, output=None, running=False):
    deadline = time.monotonic() + timeout
    for _ in range(5):
        state = idle(client)
        position = state["player"]["position"]
        occupied = {(target["position"]["x"], target["position"]["y"]) for target in state["targets"]
                    if target.get("action") != 255}
        x, y = reachable_trap(resource, state["map"], trap, position, occupied)
        print(f"Move through trap {trap}: {state['map']} ({x},{y}) -> {destination}", flush=True)
        action = client.submit("MoveTo", generation=state["generation"], x=x, y=y,
                               running=running, timeoutMs=timeout * 1000)
        while time.monotonic() < deadline:
            current = client.observe(VARIABLES)
            status = client.request("GetActionStatus", actionId=action)
            if current["generation"] != state["generation"] or current.get("inEvent"):
                if status["status"] == "running":
                    client.request("CancelAction", actionId=action)
                current = idle(client, choice=choice, output=output, resource=resource)
                if current.get("map") == destination:
                    return current
                if current.get("map") != state["map"]:
                    raise AutomationError(f"Unexpected transition: {current['map']} / {current['script']}")
                # An on-route conversation can finish before the requested exit.
                break
            if status["status"] != "running":
                raise AutomationError(f"Trap was not entered: {status}")
            time.sleep(0.2)
        else:
            client.request("CancelAction", actionId=action)
            raise TimeoutError(f"Map transition timed out: {destination}")
    raise AutomationError(f"Exit remained closed after five native events: {destination}")


def departure(client, output, resource):
    transition(client, resource, "map_001_凌绝峰连接地图.map", 1)
    checkpoint(client, output, "05-mountain-departure")
    transition(client, resource, "map_003_武当山下.map", 2)
    checkpoint(client, output, "06-wudang-foothill")
    client.save_or_load(1)
    before = checkpoint(client, output, "07-departure-saved")
    client.save_or_load(1, load=True)
    after = checkpoint(client, output, "08-departure-reloaded")
    for key in ("map", "variables", "inventory", "magic"):
        if before.get(key) != after.get(key):
            raise AutomationError(f"Normal save/load changed {key}")
    if before["player"]["position"] != after["player"]["position"]:
        raise AutomationError("Normal save/load changed player position")
    print("Normal departure and save/load passed", flush=True)


def wudang_introduction(client, output, resource):
    state = idle(client)
    if state["map"] != "map_003_武当山下.map" or state["variables"].get("Event") != "10":
        raise AutomationError("Wudang introduction requires its normal Event 10 checkpoint")
    transition(client, resource, "map_003_武当山下.map", 3)
    state = checkpoint(client, output, "10-wudang-introduction")
    if state["variables"].get("Event") != "20":
        raise AutomationError("Native tavern story did not advance to Event 20")
    client.save_or_load(2)
    checkpoint(client, output, "11-wudang-introduction-saved")
    print("Native tavern introduction completed; Event 20 saved in slot 2", flush=True)


def medicine(state, resource, attribute):
    candidates = []
    for item in state["inventory"]:
        config = configparser.ConfigParser(interpolation=None)
        config.read(resource / "ini/goods" / item["file"], encoding="utf-8-sig")
        if not config.has_section("Init"):
            continue
        values = config["Init"]
        amount = int(values.get(attribute, "0") or "0")
        if int(values.get("Kind", "2") or "2") == 0 and amount > 0 and item["quantity"] > 0:
            candidates.append((amount, item))
    if not candidates:
        return None
    deficit = max(1, state["player"][attribute.lower() + "Max"] - state["player"][attribute.lower()])
    return min(candidates, key=lambda entry: (max(0, deficit - entry[0]), max(0, entry[0] - deficit)))[1]


def fight_named(client, output, resource, name, timeout=180, use_magic=False, single_target=False, choice=None, target_id=None, magic_file="player-magic-烈火情天.ini", expected_terminal=None, final_dialogue="剧终"):
    state = idle(client)
    if state["player"]["life"] * 2 < state["player"]["lifeMax"]:
        healing = next(item for item in state["magic"] if item["file"] == "player-magic-清心咒.ini")
        config = configparser.ConfigParser(interpolation=None)
        config.read(resource / "ini/magic" / healing["file"], encoding="utf-8-sig")
        if state["player"]["mana"] >= config.getint(f"Level{healing['level']}", "ManaCost"):
            state = client.assign_magic(healing["slot"], 0)
            life_before = state["player"]["life"]
            try:
                client.act("CastSkill", generation=state["generation"], slot=0)
            except AutomationError as error:
                if str(error) != "CastSkill: action_not_executed" or client.observe()["player"]["life"] >= life_before:
                    raise
            state = idle(client)
    targets = [target for target in state["targets"] if target["name"] == name and npc_attackable(target)
               and (target_id is None or target["id"] == target_id)]
    if not targets:
        raise AutomationError(f"Expected an attackable {name}")
    position = state["player"]["position"]
    target = min(targets, key=lambda item: abs(item["position"]["x"] - position["x"]) * 2
                 + abs(item["position"]["y"] - position["y"]))
    ranged = (not single_target and target.get("attackRadius", 1) >= 5
              and target.get("hasWalkAction", True))
    skills = []
    if ranged or use_magic:
        spell = next(item for item in state["magic"] if item["file"] == magic_file)
        state = client.assign_magic(spell["slot"], 1)
        skills = [1]
    supplies = {}
    for attribute, parameter in (("Life", "lifeItem"), ("Mana", "manaItem")):
        item = medicine(state, resource, attribute)
        if item and (attribute == "Life" or skills):
            supplies[parameter] = item["file"]
    result = client.act("StartCombat", timeout=timeout + 5, generation=state["generation"],
                        targetId=target["id"], radius=20, kills=1, skills=skills,
                        allowMeleeFallback=not ranged, timeoutMs=timeout * 1000, **supplies)
    if result.get("kills") != 1:
        raise AutomationError(f"Normal combat did not defeat {name}: {result}")
    state = idle(client, choice=choice, output=output, resource=resource, expected_terminal=expected_terminal, final_dialogue=final_dialogue)
    write_json(output / f"combat-{time.time_ns()}.json", dict(target=target, result=result,
               after=state, cheatAssisted=False))
    if state.get("scene") == "Title":
        print(f"Normal combat defeated {name}; expected terminal completed", flush=True)
    else:
        print(f"Normal combat defeated {name}: life {state['player']['life']}/{state['player']['lifeMax']}", flush=True)
    return state


def wudang_pool(client, output, resource):
    state = idle(client)
    if state["map"] not in ("map_003_武当山下.map", "map_005_洗剑池.map") or state["variables"].get("Event") != "20":
        raise AutomationError("Wudang pool requires the native Event 20 checkpoint")
    if state["map"] == "map_003_武当山下.map":
        transition(client, resource, "map_005_洗剑池.map", 2)
    transition(client, resource, "map_005_洗剑池.map", 3)
    transition(client, resource, "map_005_洗剑池.map", 4)
    state = checkpoint(client, output, "12-pool-combat-start")
    if state["variables"].get("Event") != "35":
        raise AutomationError("Pool battle did not start through its native script")
    for name in ("胖道士", "瘦道士"):
        fight_named(client, output, resource, name)
    state = checkpoint(client, output, "13-pool-victory")
    if state["variables"].get("Event") != "40" or state["variables"].get("Enemy") != "2":
        raise AutomationError("Pool victory did not complete its native two-enemy death script")
    transition(client, resource, "map_004_武当山连接地图.map", 2)
    transition(client, resource, "map_006_武当山山顶.map", 1)
    client.save_or_load(3)
    checkpoint(client, output, "14-wudang-gate-source-slot3")


def wudang_gate(client, output, resource):
    source = idle(client)
    if source["map"] != "map_006_武当山山顶.map" or source["variables"].get("Event") != "40":
        raise AutomationError("Wudang gate requires its normal Event 40 slot 3 source")
    for option in (1, 0):
        if option == 0:
            client.save_or_load(3, load=True)
            client.act("SetAutoDialogue", enabled=True)
        state = transition(client, resource, "map_006_武当山山顶.map", 3, output=output,
                           choice=("script/map/map_006_武当山山顶/trap03.txt:16", option))
        expected_event = "41" if option == 1 else "44"
        expected_evil = int(source["variables"]["EvilVal"]) + (-60 if option == 1 else 20)
        if state["variables"].get("Event") != expected_event or state["variables"].get("EvilVal") != str(expected_evil):
            raise AutomationError(f"Gate branch outcome mismatch: {state['variables']}")
        if option == 1:
            while state["variables"].get("Event") == "41":
                enemies = [target for target in state["targets"] if npc_attackable(target)]
                if not enemies:
                    raise AutomationError("Forced entry stopped before its eight-enemy death count")
                position = state["player"]["position"]
                target = min(enemies, key=lambda item: abs(item["position"]["x"] - position["x"]) * 2
                             + abs(item["position"]["y"] - position["y"]))
                state = fight_named(client, output, resource, target["name"])
            state = checkpoint(client, output, "15-gate-forced-victory")
            if state["variables"].get("Event") != "44" or state["variables"].get("Enemy006") != "8":
                raise AutomationError("Forced entry did not finish its native guard death script")
        client.save_or_load(4 if option == 1 else 5)
        checkpoint(client, output, f"16-gate-option-{option}-saved")


def interact_named(client, output, resource, name, choice=None, position=None, shop=False):
    state = idle(client)
    targets = [target for target in state["targets"] if target["name"] == name
               and (target["kind"] == "object" or target.get("interactive"))
               and (target["kind"] != "npc" or target.get("action") not in (11, 255))
               and (position is None or target["position"] == dict(x=position[0], y=position[1]))]
    if not targets:
        raise AutomationError(f"No normal interaction target: {name}")
    position = state["player"]["position"]
    target = min(targets, key=lambda item: abs(item["position"]["x"] - position["x"]) * 2
                 + abs(item["position"]["y"] - position["y"]))
    print(f"Interact: {state['map']} / {name} / {target['position']}", flush=True)
    for attempt in range(4):
        try:
            action = client.submit("Interact", generation=state["generation"],
                                   targetId=target["id"], running=False, timeoutMs=180000)
            deadline = time.monotonic() + 185
            while time.monotonic() < deadline:
                current = client.observe(VARIABLES)
                if current.get("video"):
                    checkpoint(client, output, f"interaction-video-{current['context']}")
                    client.ui("Cancel")
                status = client.request("GetActionStatus", actionId=action)
                if status["status"] == "succeeded":
                    break
                if status["status"] in ("failed", "cancelled"):
                    raise AutomationError(f"Interact: {status['reason']}")
                time.sleep(0.2)
            else:
                client.request("CancelAction", actionId=action)
                raise TimeoutError(f"Interaction timed out: {name}")
            break
        except AutomationError as error:
            if str(error) != "Interact: interaction_not_observed" or attempt == 3:
                raise
            current = idle(client)
            if current["generation"] != state["generation"]:
                raise AutomationError("Interaction retry crossed a world generation") from error
            time.sleep(0.3)
            print(f"Retry normal interaction: {name}", flush=True)
    if shop:
        return client.wait_until(lambda value: "shop" in value, timeout=30, description=f"normal {name} shop")
    return idle(client, choice=choice, output=output, resource=resource)


def recover(client, output, resource):
    state = idle(client)
    if (state["player"]["life"] == state["player"]["lifeMax"]
            and state["player"]["mana"] == state["player"]["manaMax"]):
        return state
    if any(target.get("hostile") and npc_attackable(target) for target in state["targets"]):
        raise AutomationError("Normal recovery requires a safe checkpoint")
    before = state["player"]
    healing = next(item for item in state["magic"] if item["file"] == "player-magic-清心咒.ini")
    costs = configparser.ConfigParser(interpolation=None)
    costs.read(resource / "ini/magic" / healing["file"], encoding="utf-8-sig")
    mana_cost = costs.getint(f"Level{healing['level']}", "ManaCost")
    state = client.assign_magic(healing["slot"], 0)
    deadline = time.monotonic() + 90
    while state["player"]["life"] < state["player"]["lifeMax"] or state["player"]["mana"] < state["player"]["manaMax"]:
        if time.monotonic() >= deadline or not state["worldInput"]:
            raise AutomationError("Normal recovery did not finish at its safe checkpoint")
        player = state["player"]
        if player["life"] < player["lifeMax"] and player["mana"] >= mana_cost:
            if player["sitting"]:
                client.act("ToggleSit", generation=state["generation"])
            client.act("CastSkill", generation=state["generation"], slot=0)
            state = client.wait_until(lambda value: value["player"]["life"] > player["life"], timeout=5)
        else:
            if not player["sitting"] and player["thew"] >= 5:
                state = client.wait_until(lambda value: value.get("player", {}).get("action") in (0, 1, 20), timeout=5, description="ordinary spell animation before sitting")
                client.act("ToggleSit", generation=state["generation"])
            time.sleep(0.2)
            state = client.observe(VARIABLES)
    if state["player"]["sitting"]:
        client.act("ToggleSit", generation=state["generation"])
    state = idle(client)
    write_json(output / f"recovery-{time.time_ns()}.json", dict(before=before, after=state["player"], cheatAssisted=False))
    return state


def wudang_challenge(client, output, resource):
    for branch, slot in ((1, 4), (0, 5)):
        proof_path = output / f"challenge-defeat-{branch}-proof.json"
        if proof_path.exists():
            proof = json.loads(proof_path.read_text(encoding="utf-8"))
            if (proof.get("status") == "passed" and proof.get("sourceSlot") == slot
                    and proof["scriptStart"]["contentSha256"] == hashlib.sha256(
                        (resource / proof["scriptStart"]["virtualPath"]).read_bytes()).hexdigest()
                    and proof["scriptFinish"].get("status") == "completed"
                    and (output / f"20-challenge-{branch}-departure.json").exists()):
                print(f"Retain verified challenge branch {branch}", flush=True)
                continue
        client.save_or_load(slot, load=True)
        client.act("SetAutoDialogue", enabled=True)
        state = checkpoint(client, output, f"17-challenge-{branch}-source")
        if state["map"] != "map_006_武当山山顶.map" or state["variables"].get("Event") != "44":
            raise AutomationError("Challenge source must be the completed native gate branch")
        source_evil = state["variables"]["EvilVal"]
        state = interact_named(client, output, resource, "张惟宜")
        if state["variables"].get("wangwei01") or any(target.get("hostile") for target in state["targets"]):
            raise AutomationError("Premature Zhang challenge did not refuse normally")
        for name in ("清云", "清河", "清越", "清木", "清霄"):
            recover(client, output, resource)
            interact_named(client, output, resource, name)
            fight_named(client, output, resource, name)
        state = checkpoint(client, output, f"18-challenge-{branch}-five-duels")
        if state["variables"].get("wangwei01") != "5":
            raise AutomationError("Five normal challenge victories were not recorded")
        recover(client, output, resource)
        state = interact_named(client, output, resource, "张惟宜")
        target = next(target for target in state["targets"] if target["name"] == "张惟宜" and npc_attackable(target))
        action_error = None
        try:
            action = client.act("StartCombat", timeout=185, generation=state["generation"],
                                targetId=target["id"], kills=1, skills=[], timeoutMs=180000)
        except AutomationError as error:
            action, action_error = None, str(error)
        state = idle(client)
        if (state["variables"].get("Event") != "46" or state["player"]["life"] <= 0
                or state["variables"].get("EvilVal") != source_evil):
            raise AutomationError(f"Scripted challenge defeat failed: {state['variables']} / {action_error}")
        starts = [record for record in trace_records(output) if record.get("eventType") == "script.start"
                  and record.get("virtualPath") == "script/map/map_006_武当山山顶/杨影枫死亡.txt"]
        records = completed_script(output, starts[-1])
        write_json(output / f"challenge-defeat-{branch}-proof.json", dict(status="passed", sourceSlot=slot,
                   scriptStart=starts[-1], scriptFinish=records[-1], action=action, actionError=action_error,
                   event="46", cheatAssisted=False, fullPlaythrough=False))
        checkpoint(client, output, f"19-challenge-{branch}-native-recovery")
        transition(client, resource, "map_006_武当山山顶.map", 8)
        checkpoint(client, output, f"20-challenge-{branch}-departure")
    client.save_or_load(6)
    checkpoint(client, output, "21-wudang-completed-slot6")


def wudang_remaining_duels(client, output, resource):
    source = idle(client)
    if source["map"] != "map_006_武当山山顶.map" or source["variables"].get("Event") != "44" or source["variables"].get("wangwei01"):
        raise AutomationError("Remaining Wudang duels require a native gate-completed source before any duel")
    client.save_or_load(0)
    checkpoint(client, output, "17-wudang-other-five-source-slot0")
    for name in ("清虚", "清水", "清微", "清灵", "清玄"):
        recover(client, output, resource)
        interact_named(client, output, resource, name)
        fight_named(client, output, resource, name)
    state = checkpoint(client, output, "18-wudang-other-five-wins")
    if state["variables"].get("wangwei01") != "5":
        raise AutomationError("The other five Wudang victories did not accumulate their native count")
    client.save_or_load(1)
    recover(client, output, resource)
    state = interact_named(client, output, resource, "张惟宜")
    target = next(target for target in state["targets"] if target["name"] == "张惟宜" and npc_attackable(target))
    action_error = None
    try:
        client.act("StartCombat", timeout=185, generation=state["generation"],
                   targetId=target["id"], kills=1, skills=[], timeoutMs=180000)
    except AutomationError as error:
        action_error = str(error)
    idle(client)
    state = checkpoint(client, output, "19-wudang-other-five-zhang-recovery")
    if (state["variables"].get("Event") != "46" or state["player"]["life"] <= 0
            or state["variables"].get("EvilVal") != source["variables"]["EvilVal"]):
        raise AutomationError("Other-five route did not complete its native Zhang recovery")
    start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start"
                 and record.get("virtualPath") == "script/map/map_006_武当山山顶/杨影枫死亡.txt")
    write_json(output / "wudang-other-five-defeat-proof.json", dict(records=completed_script(output, start),
               observedEvent="46", actionError=action_error, cheatAssisted=False))
    transition(client, resource, "map_006_武当山山顶.map", 8)
    client.save_or_load(6)
    checkpoint(client, output, "21-wudang-other-five-departure-slot6")


def wudang_zhang_win(client, output, resource):
    state = idle(client)
    if (state["map"] != "map_006_武当山山顶.map" or state["variables"].get("Event") not in ("44", "45")
            or int(state["variables"].get("wangwei01") or 0) < 5):
        raise AutomationError("Early Zhang victory requires five ordinary Wudang duel wins")
    recover(client, output, resource)
    client.save_or_load(0)
    checkpoint(client, output, "22-wudang-zhang-win-source-slot0")
    state = interact_named(client, output, resource, "张惟宜")
    client.save_or_load(1)
    checkpoint(client, output, "23-wudang-zhang-fight-slot1")
    client.act("SetAutoDialogue", enabled=True, intervalMs=1000)
    path = "script/map/map_006_武当山山顶/张惟宜死亡.txt"
    after = fight_named(client, output, resource, "张惟宜", use_magic=True, timeout=600,
                        expected_terminal=path, final_dialogue="跳涯自杀")
    start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start"
                 and record.get("virtualPath", "").casefold() == path.casefold())
    records = completed_script(output, start)
    dialogues = [json.loads(p.read_text(encoding="utf-8")) for p in output.glob("ending-dialogue-*.json")]
    if (after.get("scene") != "Title" or start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest()
            or not any("跳涯自杀" in value.get("dialogue", {}).get("text", "") for value in dialogues)):
        raise AutomationError("Early Zhang ending lacks current source, last dialogue, or Title evidence")
    checkpoint(client, output, "24-wudang-zhang-ending-title")
    write_json(output / "wudang-zhang-win-proof.json", dict(records=records, after=after,
               finalDialogue=dialogues, storyEnding=True, cheatAssisted=False))


def foothill_rescue(client, output, resource):
    state = idle(client)
    if state["map"] != "map_006_武当山山顶.map" or state["variables"].get("Event") != "46":
        raise AutomationError("Foothill rescue requires completed normal Wudang departure")
    transition(client, resource, "map_004_武当山连接地图.map", 1)
    transition(client, resource, "map_005_洗剑池.map", 2)
    transition(client, resource, "map_003_武当山下.map", 1)
    state = checkpoint(client, output, "23-rescue-source")
    if state["variables"].get("SubEvent01") != "2":
        raise AutomationError("Merchant rescue was not installed by the native pool exit")
    recover(client, output, resource)
    state = transition(client, resource, "map_003_武当山下.map", 7)
    if state["variables"].get("SubEvent01") != "5":
        raise AutomationError("Merchant rescue battle did not start through its native trap")
    for name in ("强盗头目", "强盗跟班甲", "强盗跟班乙"):
        fight_named(client, output, resource, name)
    state = checkpoint(client, output, "24-merchant-rescued")
    if state["variables"].get("SubEvent01") != "10" or state["variables"].get("Sub01Death") != "3":
        raise AutomationError("Merchant rescue did not finish all three native death scripts")
    client.save_or_load(3)
    checkpoint(client, output, "25-rescue-completed-slot3")


def wudang_herb_dialogues(client, output, resource):
    source = idle(client)
    if (source["map"] != "map_003_武当山下.map" or source["variables"].get("Event") != "46"
            or source["variables"].get("SubEvent01") != "10" or source["variables"].get("SubEvent02")):
        raise AutomationError("Herb dialogues require the native rescued-merchant source before quest acceptance")
    client.save_or_load(0)
    checkpoint(client, output, "550-herb-dialogue-source-slot0")
    path = "script/map/map_003_武当山下/酒肆老板对话.txt"
    digest = hashlib.sha256((resource / path).read_bytes()).hexdigest()
    proof = []
    for phase, expected in (("before-request", {3, 4}), ("incomplete-request", {1, 2})):
        seen = set()
        for attempt in range(32):
            state = idle(client)
            if phase == "before-request" and state["variables"].get("SubEvent02") == "2":
                client.save_or_load(0, load=True)
                client.act("SetAutoDialogue", enabled=True)
                state = idle(client)
            earlier = {record["executionId"] for record in trace_records(output) if record.get("eventType") == "script.start"}
            state = interact_named(client, output, resource, "武当山下酒肆老板")
            value = int(state["variables"].get("Sub02Rand", "0"))
            event = state["variables"].get("SubEvent02")
            if value not in expected or event != ("2" if phase == "incomplete-request" or value == 4 else ""):
                raise AutomationError("Native random herb dialogue changed its expected quest stage")
            if (state["inventory"] != source["inventory"] or state["player"]["money"] != source["player"]["money"]
                    or any(state["variables"].get(key) != source["variables"].get(key) for key in ("Event", "SenseVal", "EvilVal"))):
                raise AutomationError("Random herb dialogue changed items, money, or main-story variables")
            start = next((record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start"
                          and record["executionId"] not in earlier and record.get("virtualPath") == path), None)
            if start is None or start["contentSha256"] != digest:
                raise AutomationError("Random herb dialogue lacks current native script evidence")
            records = completed_script(output, start)
            if not any(record.get("apiName") == "getrandnum" for record in records):
                raise AutomationError("Herb dialogue did not use its native random draw")
            if value not in seen:
                checkpoint(client, output, f"551-herb-{phase}-random-{value}")
                proof.append(dict(phase=phase, randomValue=value, start=start, records=records))
                seen.add(value)
            if seen == expected and (phase != "before-request" or event == "2"):
                break
        else:
            raise AutomationError("Native herb random dialogue did not cover both outcomes within 32 interactions")
    write_json(output / "herb-random-dialogue-proof.json", dict(dialogues=proof, cheatAssisted=False, fullPlaythrough=False))
    client.save_or_load(6)
    checkpoint(client, output, "552-herb-incomplete-source-slot6")


def wudang_herbs(client, output, resource):
    state = idle(client)
    if (state["map"] != "map_003_武当山下.map" or state["variables"].get("Event") != "46"
            or state["variables"].get("SubEvent01") != "10" or state["variables"].get("SubEvent02")):
        raise AutomationError("Herbs require the normal rescued-merchant slot 3 before quest acceptance")
    for _ in range(20):
        state = interact_named(client, output, resource, "武当山下酒肆老板")
        if state["variables"].get("SubEvent02") == "2":
            break
    else:
        raise AutomationError("Native random tavern dialogue did not offer its herb quest")
    checkpoint(client, output, "26-herbs-requested")
    state = interact_named(client, output, resource, "武当山下酒肆老板")
    if state["variables"].get("SubEvent02") != "2":
        raise AutomationError("Missing herbs unexpectedly advanced the quest")
    transition(client, resource, "map_005_洗剑池.map", 2)
    state = interact_named(client, output, resource, "罂粟")
    if state["variables"].get("SubEvent02") != "4":
        raise AutomationError("Native poppy collection did not advance by two")
    transition(client, resource, "map_004_武当山连接地图.map", 2)
    state = interact_named(client, output, resource, "草葱")
    if state["variables"].get("SubEvent02") != "6":
        raise AutomationError("Native scallion collection did not advance by two")
    transition(client, resource, "map_005_洗剑池.map", 2)
    transition(client, resource, "map_003_武当山下.map", 1)
    transition(client, resource, "map_007_连接地图.map", 4)
    for _ in range(20):
        state = idle(client)
        farmer = next(target for target in state["targets"] if target["name"] == "农夫")
        if farmer["position"] != dict(x=0, y=0):
            break
        transition(client, resource, "map_003_武当山下.map", 1)
        transition(client, resource, "map_007_连接地图.map", 4)
    else:
        raise AutomationError("Native random map entrance did not expose its farmer")
    state = interact_named(client, output, resource, "农夫")
    if state["variables"].get("SubEvent02") != "8":
        raise AutomationError("Farmer did not deliver native wild ginger")
    before_goods = state["inventory"]
    state = interact_named(client, output, resource, "农夫")
    if state["inventory"] != before_goods or state["variables"].get("SubEvent02") != "8":
        raise AutomationError("Repeated farmer dialogue duplicated its herb reward")
    checkpoint(client, output, "27-three-herbs-collected")
    transition(client, resource, "map_003_武当山下.map", 1)
    state = interact_named(client, output, resource, "武当山下酒肆老板")
    if state["variables"].get("SubEvent02") != "10" or any(
            item["file"] in ("goods-e15-草葱.ini", "goods-e16-罂粟.ini", "goods-e17-野姜.ini") for item in state["inventory"]):
        raise AutomationError("Native herb handover did not consume its three quest items")
    client.save_or_load(0)
    source = checkpoint(client, output, "28-herb-payment-source-slot0")
    for option in (1, 0):
        if option == 0:
            client.save_or_load(0, load=True)
            client.act("SetAutoDialogue", enabled=True)
        state = interact_named(client, output, resource, "武当山下酒肆老板",
                               choice=("script/map/map_003_武当山下/酒肆老板对话.txt:96", option))
        if (state["variables"].get("SubEvent02") != "20"
                or state["player"]["money"] != source["player"]["money"] + (200 if option == 0 else 0)
                or int(state["variables"]["EvilVal"]) != int(source["variables"]["EvilVal"]) + (-10 if option == 0 else 10)):
            raise AutomationError(f"Tavern payment outcome mismatch: {state['variables']}")
        after = interact_named(client, output, resource, "武当山下酒肆老板")
        if after["player"]["money"] != state["player"]["money"] or after["variables"] != state["variables"]:
            raise AutomationError("Completed herb quest repeated its reward")
        client.save_or_load(2 if option == 0 else 1)
        checkpoint(client, output, f"29-herb-payment-option-{option}")
    client.save_or_load(6)
    checkpoint(client, output, "30-herbs-completed-slot6")


def process_running(pid, executable=None):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = (ctypes.c_uint, ctypes.c_bool, ctypes.c_uint)
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.WaitForSingleObject.argtypes = (ctypes.c_void_p, ctypes.c_uint)
    kernel.CloseHandle.argtypes = (ctypes.c_void_p,)
    handle = kernel.OpenProcess(0x101000 if executable else 0x100000, False, pid)
    if not handle:
        error = ctypes.get_last_error()
        if error == 87:
            return False
        if error == 5 and executable:
            # A stopped game PID can be reused by a protected system process.
            # Its public name can disprove identity; a matching name stays unknown.
            result = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command",
                                     f"(Get-Process -Id {int(pid)} -ErrorAction SilentlyContinue).ProcessName"],
                                    capture_output=True, text=True, check=True, creationflags=0x08000000)
            if result.stdout.strip().casefold() != Path(executable).stem.casefold():
                return False
        raise ctypes.WinError(error)
    try:
        status = kernel.WaitForSingleObject(handle, 0)
        if status not in (0, 258):
            raise ctypes.WinError(ctypes.get_last_error())
        if status != 258:
            return False
        if executable:
            kernel.QueryFullProcessImageNameW.argtypes = (ctypes.c_void_p, ctypes.c_uint, ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_uint))
            image = ctypes.create_unicode_buffer(32768)
            length = ctypes.c_uint(len(image))
            if not kernel.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(length)):
                error = ctypes.get_last_error()
                if kernel.WaitForSingleObject(handle, 0) == 0:
                    return False
                raise ctypes.WinError(error)
            return Path(image.value) == Path(executable)
        return True
    finally:
        kernel.CloseHandle(handle)


def load_checkpoint(client, slot):
    state = client.observe()
    if state["scene"] != "Title":
        return client.save_or_load(slot, load=True)
    client.activate("load-game")
    state = client.wait_until(lambda value: "saveSlot" in value)
    while state["saveSlot"] != slot:
        client.focus("load")
        client.ui("Down" if state["saveSlot"] < slot else "Up")
        state = client.observe()
    client.activate("load")
    return client.wait_until(lambda value: value["worldInput"], timeout=60)


def cave_entry(client, output, resource):
    state = idle(client)
    if state["map"] == "map_009_山洞内部.map" and state["variables"].get("Event") == "50":
        return cave_bats(client, output, resource)
    if state["map"] == "map_003_武当山下.map" and state["variables"].get("Event") == "46":
        recover(client, output, resource)
        transition(client, resource, "map_007_连接地图.map", 4)
        transition(client, resource, "map_008_野树林.map", 2)
        state = transition(client, resource, "map_008_野树林.map", 2)
        if state["variables"].get("Event") != "50":
            raise AutomationError("Native bandit pursuit did not advance to Event 50")
        client.save_or_load(0)
        checkpoint(client, output, "32-forest-pursuit-slot0")
    elif state["map"] != "map_008_野树林.map" or state["variables"].get("Event") != "50":
        raise AutomationError("Cave entry requires its normal Event 46 source or Event 50 forest slot 0")
    # These positions come from the current production OBJ bindings.
    for position in ((3, 20), (18, 24), (35, 102), (7, 56)):
        if position == (18, 24):
            # The island is disconnected on foot; this diagonal crosses only native jumpable tiles.
            client.move(10, 25, running=False)
            state = checkpoint(client, output, "forest-island-jump-before")
            client.act("JumpTo", generation=state["generation"], x=14, y=17, timeoutMs=10000)
            checkpoint(client, output, "forest-island-jump-after")
        before = checkpoint(client, output, f"forest-box-{position[0]}-{position[1]}-before")
        state = interact_named(client, output, resource, "宝箱", position=position)
        if state["inventory"] == before["inventory"] and state["player"]["money"] == before["player"]["money"]:
            raise AutomationError(f"Native forest box did not deliver its reward: {position}")
        try:
            repeated = interact_named(client, output, resource, "宝箱", position=position)
        except AutomationError as error:
            if str(error) not in ("action_rejected", "Interact: action_rejected"):
                raise
            repeated = idle(client)
        if repeated["inventory"] != state["inventory"] or repeated["player"]["money"] != state["player"]["money"]:
            raise AutomationError(f"Forest box duplicated its reward: {position}")
        checkpoint(client, output, f"forest-box-{position[0]}-{position[1]}-after")
        if position == (18, 24):
            client.move(14, 17, running=False)
            state = client.observe()
            client.act("JumpTo", generation=state["generation"], x=10, y=25, timeoutMs=10000)
            checkpoint(client, output, "forest-island-return")
    transition(client, resource, "map_009_山洞内部.map", 1)
    client.save_or_load(1)
    checkpoint(client, output, "33-bat-cave-source-slot1")
    cave_bats(client, output, resource)


def cave_bats(client, output, resource):
    # Forest rewards are ordinary equipment; use the existing inventory callbacks.
    for item in client.observe()["inventory"]:
        config = configparser.ConfigParser(interpolation=None)
        config.read(resource / "ini/goods" / item["file"], encoding="utf-8-sig")
        if item["slot"] < 200 and config.getint("Init", "Kind") == 1:
            client.equip(item["slot"])
            if not any(row["file"] == item["file"] and row["slot"] >= 200 for row in client.observe()["inventory"]):
                raise AutomationError(f"Normal equipment action did not equip {item['file']}")
    checkpoint(client, output, "forest-reward-equipment")
    while True:
        state = idle(client)
        enemies = [target for target in state["targets"] if npc_attackable(target)]
        if not enemies:
            break
        position = state["player"]["position"]
        target = min(enemies, key=lambda item: abs(item["position"]["x"] - position["x"]) * 2
                     + abs(item["position"]["y"] - position["y"]))
        fight_named(client, output, resource, target["name"], use_magic=True)
    recover(client, output, resource)
    checkpoint(client, output, "34-bat-cave-cleared")
    transition(client, resource, "map_010_山洞内部.map", 2)
    state = transition(client, resource, "map_010_山洞内部.map", 2)
    if state["variables"].get("Event") != "51":
        raise AutomationError("Native cave ambush did not advance to Event 51")
    client.save_or_load(2)
    checkpoint(client, output, "35-bandit-first-round-slot2")


def cave_battles(client, output, resource):
    state = idle(client)
    if state["map"] != "map_010_山洞内部.map" or state["variables"].get("Event") != "51":
        raise AutomationError("Cave battles require the native first-round slot 2")
    if int(state["variables"].get("DeadRobber") or 0) < 23:
        client.act("JumpTo", generation=state["generation"], x=20, y=60, timeoutMs=10000)
        state = idle(client)
    while int(state["variables"].get("DeadRobber") or 0) < 23:
        targets = [target for target in state["targets"] if npc_attackable(target) and target["name"] != "龙寨主"]
        if not targets:
            raise AutomationError("First cave round lost its bandits before the source death count")
        position = state["player"]["position"]
        target = min(targets, key=lambda item: abs(item["position"]["x"] - position["x"]) * 2
                     + abs(item["position"]["y"] - position["y"]))
        state = fight_named(client, output, resource, target["name"], single_target=True)
    enemies = [target for target in state["targets"] if npc_attackable(target)]
    if len(enemies) != 2 or sum(target["name"] == "龙寨主" for target in enemies) != 1:
        raise AutomationError("First cave branch must retain its chief and one regular bandit")
    if not (output / "36-cave-last-enemy-source-slot3.json").exists():
        client.save_or_load(3)
        checkpoint(client, output, "36-cave-last-enemy-source-slot3")
    for last in ("chief", "bandit"):
        previous = output / f"cave-{last}-second-round-proof.json"
        if previous.exists():
            proof = json.loads(previous.read_text(encoding="utf-8"))
            observed = json.loads(Path(proof["afterFile"]).read_text(encoding="utf-8"))
            records = completed_script(output, proof["scriptStart"])
            if (proof.get("cheatAssisted") is not False or records[-1] != proof["scriptFinish"]
                    or observed["variables"].get("Event") != "60" or observed["variables"].get("DeadRobber1") != "25"):
                raise AutomationError(f"Previous {last}-last cave evidence is invalid")
            print(f"Retain completed {last}-last cave branch", flush=True)
            continue
        if last == "bandit":
            client.save_or_load(3, load=True)
            client.act("SetAutoDialogue", enabled=True)
        state = idle(client)
        bandit = next(target["name"] for target in state["targets"] if npc_attackable(target) and target["name"] != "龙寨主")
        order = (bandit, "龙寨主") if last == "chief" else ("龙寨主", bandit)
        for name in order:
            # Use one-target attacks while both remain; the final archer can safely use magic.
            state = fight_named(client, output, resource, name, single_target=name != order[-1])
        if state["variables"].get("Event") != "55" or state["variables"].get("DeadRobber") != "25":
            raise AutomationError(f"Native {last}-last ambush did not start its second round")
        script = "强盗头子死亡.txt" if last == "chief" else "强盗死亡.txt"
        starts = [record for record in trace_records(output) if record.get("eventType") == "script.start"
                  and record.get("virtualPath") == f"script/map/map_010_山洞内部/{script}"]
        records = completed_script(output, starts[-1])
        write_json(output / f"cave-{last}-last-proof.json", dict(status="passed", sourceSlot=3, order=order,
                   scriptStart=starts[-1], scriptFinish=records[-1], event="55", cheatAssisted=False))
        client.save_or_load(4 if last == "chief" else 5)
        checkpoint(client, output, f"37-cave-{last}-last-second-round")
        cave_second_round(client, output, resource, last)
    client.save_or_load(6)
    checkpoint(client, output, "39-cave-completed-slot6")


def cave_second_round(client, output, resource, last):
    state = idle(client)
    if state["map"] != "map_010_山洞内部.map" or state["variables"].get("Event") != "55":
        raise AutomationError("Second cave round requires its native Event 55 source")
    first = json.loads((output / f"cave-{last}-last-proof.json").read_text(encoding="utf-8"))
    completed_script(output, first["scriptStart"])
    client.act("JumpTo", generation=state["generation"], x=20, y=60, timeoutMs=10000)
    state = idle(client)
    while state["variables"].get("Event") == "55":
        targets = [target for target in state["targets"] if npc_attackable(target)]
        if not targets:
            raise AutomationError("Second cave round stopped before all 25 deaths")
        position = state["player"]["position"]
        target = min(targets, key=lambda item: abs(item["position"]["x"] - position["x"]) * 2
                     + abs(item["position"]["y"] - position["y"]))
        state = fight_named(client, output, resource, target["name"], use_magic=True)
    if state["variables"].get("Event") != "60" or state["variables"].get("DeadRobber1") != "25":
        raise AutomationError(f"Native second cave round failed after {last}-last branch")
    checkpoint(client, output, f"38-cave-{last}-last-victory")
    starts = [record for record in trace_records(output) if record.get("eventType") == "script.start"
              and record.get("virtualPath") == "script/map/map_010_山洞内部/强盗死亡1.txt"]
    records = completed_script(output, starts[-1])
    write_json(output / f"cave-{last}-second-round-proof.json", dict(status="passed", cheatAssisted=False,
               scriptStart=starts[-1], scriptFinish=records[-1],
               afterFile=str(output / f"38-cave-{last}-last-victory.json")))


def huian_sidequests(client, output, resource):
    state = idle(client)
    if (output / "40-huian-arrival-slot0.json").exists() and int(state["variables"].get("Sub03TalkTimes") or 0) > 0:
        return huian_beggar(client, output, resource)
    if state["map"] == "map_010_山洞内部.map" and state["variables"].get("Event") == "60":
        recover(client, output, resource)
        for destination, trap in (("map_009_山洞内部.map", 1), ("map_008_野树林.map", 1),
                                  ("map_011_连接地图.map", 4)):
            state = transition(client, resource, destination, trap)
        client.save_or_load(5)
        checkpoint(client, output, "huian-entry-source-slot5")
        state = transition(client, resource, "map_012_惠安镇.map", 2)
    if state["map"] != "map_012_惠安镇.map" or state["variables"].get("Event") != "60":
        raise AutomationError("Huian sidequests require the normal Event 60 arrival")
    # Event 60 closes town exits. Reload the normal entrance save for the hidden-beggar case.
    while next(target["position"] for target in state["targets"] if target["name"] == "乞丐") == dict(x=0, y=0):
        checkpoint(client, output, f"huian-hidden-beggar-{time.time_ns()}", variables=("QiGaiPos",))
        if not (output / "huian-entry-source-slot5.json").exists():
            raise AutomationError("A hidden beggar requires the normal saved entrance before Event 60")
        client.save_or_load(5, load=True)
        client.act("SetAutoDialogue", enabled=True)
        state = transition(client, resource, "map_012_惠安镇.map", 2)
    client.save_or_load(0)
    source = checkpoint(client, output, "40-huian-arrival-slot0")
    fortune = "script/map/map_012_惠安镇/惠安镇路人16对话.txt"
    for branch, payment, cost in (("news", 0, 10), ("news", 1, 0), ("future", 0, 50),
                                  ("future", 1, 0), ("marriage", 0, 200), ("marriage", 1, 0)):
        client.save_or_load(0, load=True)
        client.act("SetAutoDialogue", enabled=True)
        sequence = [(f"{fortune}:4", 0 if branch == "news" else 1)]
        if branch == "news":
            sequence.append((f"{fortune}:10", payment))
        else:
            sequence.extend(((f"{fortune}:33", 0 if branch == "future" else 1),
                             (f"{fortune}:{38 if branch == 'future' else 80}", payment)))
        state = interact_named(client, output, resource, "刘半仙", choice=sequence)
        if state["variables"].get("Event") != "60" or state["player"]["money"] != source["player"]["money"] - cost:
            raise AutomationError(f"Native fortune payment mismatch: {branch}/{payment}")
        expected_sense = int(source["variables"]["SenseVal"]) + (5 if branch == "marriage" and payment == 0 else 0)
        if state["variables"].get("SenseVal") != str(expected_sense):
            raise AutomationError("Native marriage fortune did not apply its sense change")
        checkpoint(client, output, f"41-fortune-{branch}-{payment}")
    flower = "script/map/map_012_惠安镇/惠安镇路人19对话.txt:11"
    for option in (0, 1):
        client.save_or_load(0, load=True)
        client.act("SetAutoDialogue", enabled=True)
        state = interact_named(client, output, resource, "春泥", choice=(flower, option))
        roses = sum(item["quantity"] for item in state["inventory"] if item["file"] == "goods-e21-玫瑰花.ini")
        old_roses = sum(item["quantity"] for item in source["inventory"] if item["file"] == "goods-e21-玫瑰花.ini")
        if state["player"]["money"] != source["player"]["money"] - (100 if option == 0 else 0) or roses != old_roses + (option == 0):
            raise AutomationError("Native flower purchase did not match its money/item change")
        checkpoint(client, output, f"42-flower-{option}")
    client.save_or_load(0, load=True)
    client.act("SetAutoDialogue", enabled=True)
    huian_beggar(client, output, resource)


def huian_beggar(client, output, resource):
    source = json.loads((output / "40-huian-arrival-slot0.json").read_text(encoding="utf-8"))
    state = idle(client)
    count = int(state["variables"].get("Sub03TalkTimes") or 0)
    if (state["map"] != "map_012_惠安镇.map" or state["variables"].get("Event") != "60"
            or not 0 <= count < 10 or state["player"]["money"] != source["player"]["money"] - count * 80):
        raise AutomationError("Beggar continuation does not match its ordinary donation source")
    beggar = "script/map/map_012_惠安镇/惠安镇路人14对话.txt"
    if count == 0:
        state = interact_named(client, output, resource, "乞丐", choice=(f"{beggar}:20", 1))
        if state["player"]["money"] != source["player"]["money"] or int(state["variables"].get("Sub03TalkTimes") or 0) != 0:
            raise AutomationError("Refusing the beggar unexpectedly changed money or donation count")
    if source["player"]["money"] < 800:
        raise AutomationError("Ten ordinary beggar donations require 800 coins")
    for number in range(count + 1, 11):
        state = interact_named(client, output, resource, "乞丐", choice=(f"{beggar}:20", 0))
        if state["player"]["money"] != source["player"]["money"] - 80 * number or state["variables"].get("Sub03TalkTimes") != str(number):
            raise AutomationError("Beggar donation count/payment mismatch")
        if state["variables"].get("EvilVal") != str(int(source["variables"]["EvilVal"]) + 5 * number):
            raise AutomationError("Beggar donation did not apply its native evil change")
    client.save_or_load(1)
    donation = checkpoint(client, output, "43-beggar-book-source-slot1", variables=("MoneyTalk", "MoenyTalk"))
    for option in (0, 1):
        if option == 1:
            client.save_or_load(1, load=True)
            client.act("SetAutoDialogue", enabled=True)
        state = interact_named(client, output, resource, "乞丐", choice=(f"{beggar}:81", option))
        books = sum(item["quantity"] for item in state["inventory"] if item["file"] == "book07-潮月剑法.ini")
        if books != (option == 0) or state["variables"].get("SubEvent03") != "10" or state["variables"].get("Sub03TalkTimes") != "11":
            raise AutomationError("Beggar book branch did not complete its native reward")
        if state["variables"].get("EvilVal") != str(int(donation["variables"]["EvilVal"]) + (-40 if option == 0 else 10)):
            raise AutomationError("Beggar book branch evil change mismatch")
        repeated = interact_named(client, output, resource, "乞丐")
        if repeated["inventory"] != state["inventory"] or repeated["player"]["money"] != state["player"]["money"]:
            raise AutomationError("Beggar repeated its completed reward")
        client.save_or_load(2 if option == 0 else 3)
        checkpoint(client, output, f"44-beggar-book-{option}")
    client.save_or_load(2, load=True)
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(6)
    checkpoint(client, output, "45-huian-sidequests-slot6")
    sites = [site for site in inventory(resource)["choices"] if site["coverageId"] in
             ("C07", "C08", "C09", "C10", "C11", "C12", "C13", "C14")]
    proven = set()
    for path in output.glob("choice-proof-*.json"):
        recorded = json.loads(path.read_text(encoding="utf-8"))
        site = next((site for site in sites if site["id"] == recorded["choiceSite"]), None)
        if site:
            choice_evidence(output, path, site)
            proven.add((site["id"], recorded["choiceIndex"]))
    if proven != {(site["id"], option) for site in sites for option in (0, 1)}:
        raise AutomationError("Huian sidequest options lack complete native evidence")


def huian_night(client, output, resource):
    state = idle(client)
    if state["map"] != "map_012_惠安镇.map" or state["variables"].get("Event") != "60":
        raise AutomationError("Huian night requires its normal Event 60 town checkpoint")
    source = checkpoint(client, output, "46-huian-inn-before", variables=("StealMoney",))
    state = transition(client, resource, "map_012_惠安镇.map", 13)
    if state["variables"].get("Event") != "70" or state["player"]["money"] != 0:
        raise AutomationError("Native inn arrival did not complete the stolen-money story")
    stolen = checkpoint(client, output, "47-huian-woodbox-source-slot0", variables=("StealMoney",))
    if int(stolen["variables"].get("StealMoney") or 0) != source["player"]["money"]:
        raise AutomationError("The thief did not preserve the actual stolen amount")
    client.save_or_load(0)
    for option in (0, 1):
        if option == 1:
            client.save_or_load(0, load=True)
            client.act("SetAutoDialogue", enabled=True)
        state = transition(client, resource, "map_012_惠安镇.map", 5, output=output,
                           choice=("script/map/map_012_惠安镇/trap05.txt:21", option))
        boxes = sum(item["quantity"] for item in state["inventory"] if item["file"] == "goods-e00-木匣.ini")
        if state["variables"].get("Event") != "85" or boxes != option:
            raise AutomationError("Native woodbox decision did not start its expected night ambush")
        if state["variables"].get("EvilVal") != str(int(source["variables"]["EvilVal"]) + (-40 if option == 0 else 20)):
            raise AutomationError("Woodbox decision evil change mismatch")
        client.save_or_load(4 if option == 0 else 5)
        checkpoint(client, output, f"48-huian-woodbox-{option}-ambush")
        while state["variables"].get("Event") == "85":
            targets = [target for target in state["targets"] if npc_attackable(target)]
            if not targets:
                raise AutomationError("Night ambush stopped before its five deaths")
            position = state["player"]["position"]
            target = min(targets, key=lambda item: abs(item["position"]["x"] - position["x"]) * 2
                         + abs(item["position"]["y"] - position["y"]))
            state = fight_named(client, output, resource, target["name"], use_magic=True)
        state = checkpoint(client, output, f"49-huian-woodbox-{option}-victory", variables=("Enemyblack",))
        if state["variables"].get("Event") != "90" or state["variables"].get("Enemyblack") != "5":
            raise AutomationError("Native night victory did not complete all five black-clad deaths")
        for file in ("goods-e01-银针.ini", "goods-e00-木匣.ini"):
            if sum(item["quantity"] for item in state["inventory"] if item["file"] == file) != 1:
                raise AutomationError(f"Night aftermath did not deliver exactly one {file}")
        for trap, event in ((7, "95"), (8, "100"), (9, "110")):
            state = transition(client, resource, "map_012_惠安镇.map", trap)
            if state["variables"].get("Event") != event:
                raise AutomationError(f"Native night aftermath trap {trap} did not reach Event {event}")
        client.save_or_load(1 if option == 0 else 2)
        checkpoint(client, output, f"50-huian-woodbox-{option}-aftermath")
    client.save_or_load(6)
    checkpoint(client, output, "51-huian-night-completed-slot6")


def cangjian_first_visit(client, output, resource):
    state = idle(client)
    if state["map"] not in ("map_012_惠安镇.map", "map_015_藏剑山庄.map") or state["variables"].get("Event") != "110":
        raise AutomationError("First Cangjian visit requires the native Huian Event 110 checkpoint")
    if state["map"] == "map_012_惠安镇.map":
        transition(client, resource, "map_014_连接地图.map", 2)
        transition(client, resource, "map_015_藏剑山庄.map", 2)
        client.save_or_load(0)
        checkpoint(client, output, "52-cangjian-first-arrival-slot0")
    state = transition(client, resource, "map_015_藏剑山庄.map", 12)
    if state["variables"].get("Event") != "120":
        raise AutomationError("Native feast and sworn-brother ceremony did not reach Event 120")
    client.save_or_load(1)
    checkpoint(client, output, "53-cangjian-duel-source-slot1")
    state = interact_named(client, output, resource, "卓非凡")
    if state["variables"].get("Event") != "124":
        raise AutomationError("Native Zhuo dialogue did not start the first duel")
    client.save_or_load(2)
    checkpoint(client, output, "54-cangjian-duel-active-slot2")
    state = fight_named(client, output, resource, "卓非凡", single_target=True)
    state = checkpoint(client, output, "55-cangjian-duel-win")
    if state["variables"].get("Event") != "130" or state["player"]["life"] != state["player"]["lifeMax"]:
        raise AutomationError("First duel victory did not complete its native overnight aftermath")
    start = next(record for record in reversed(trace_records(output))
                 if record.get("eventType") == "script.start"
                 and record.get("virtualPath") == "script/map/map_015_藏剑山庄/卓非凡死亡.txt")
    write_json(output / "cangjian-first-duel-win-proof.json", dict(
        observedEvent="130", sourceSlot=2, afterFile="55-cangjian-duel-win.json",
        records=completed_script(output, start), cheatAssisted=False))
    client.save_or_load(3)
    state = interact_named(client, output, resource, "卓非凡")
    if state["variables"].get("Event") != "135":
        raise AutomationError("Native next-morning dialogue did not reach Event 135")
    client.save_or_load(6)
    checkpoint(client, output, "56-cangjian-next-morning-slot6")


def cangjian_first_defeat(client, output, resource):
    state = client.observe(VARIABLES)
    if state.get("scene") != "Title":
        state = idle(client)
    if state.get("scene") != "Title" and (state["map"] != "map_015_藏剑山庄.map" or state["variables"].get("Event") != "124"):
        raise AutomationError("First Cangjian defeat requires its native active-duel checkpoint")
    source_path = output / "52-cangjian-defeat-source.json"
    source = (json.loads(source_path.read_text(encoding="utf-8")) if source_path.exists()
              else checkpoint(client, output, "52-cangjian-defeat-source"))
    video_path = output / "53-cangjian-defeat-video.json"
    videos = [json.loads(video_path.read_text(encoding="utf-8"))["video"]] if video_path.exists() else []
    deadline, next_report = time.monotonic() + 1800, time.monotonic()
    while time.monotonic() < deadline:
        state = client.observe(VARIABLES)
        if state.get("video"):
            videos.append(state["video"])
            checkpoint(client, output, "53-cangjian-defeat-video")
            client.ui("Cancel")
        if state.get("scene") == "Title" or state["variables"].get("Event") == "130":
            break
        if state.get("choices"):
            raise AutomationError("Unexpected choice while observing the native first-duel defeat")
        if time.monotonic() >= next_report:
            print(f"Native passive duel: life {state['player']['life']}/{state['player']['lifeMax']}", flush=True)
            next_report = time.monotonic() + 30
        time.sleep(0.2)
    else:
        raise TimeoutError("Native passive first-duel defeat was not reached within 30 minutes")
    after = checkpoint(client, output, "54-cangjian-defeat-after")
    starts = [record for record in trace_records(output) if record.get("eventType") == "script.start"
              and record.get("virtualPath", "").endswith(("/主角死亡.txt", "/杨影枫死亡.txt"))]
    records = completed_script(output, starts[-1])
    write_json(output / "cangjian-first-defeat-proof.json", dict(source=source, after=after,
        observedVideos=videos, records=records, cheatAssisted=False,
        expectedEvent="130", recovered=after.get("variables", {}).get("Event") == "130"))
    if after.get("variables", {}).get("Event") != "130" or after.get("scene") == "Title":
        raise AutomationError("Candidate first-duel recovery script was not reached: native defeat returned to Title")


def hanbo_first_visit(client, output, resource):
    state = idle(client)
    if state["map"] != "map_015_藏剑山庄.map" or state["variables"].get("Event") != "135":
        raise AutomationError("First Hanbo visit requires the native Cangjian Event 135 checkpoint")
    transition(client, resource, "map_014_连接地图.map", 1)
    transition(client, resource, "map_017_连接地图.map", 3)
    transition(client, resource, "map_018_连接地图.map", 2)
    transition(client, resource, "map_019_寒波谷.map", 2)
    client.save_or_load(0)
    checkpoint(client, output, "57-hanbo-arrival-slot0")
    state = transition(client, resource, "map_019_寒波谷.map", 3)
    if state["variables"].get("Event") != "140" or not state["player"]["canJump"]:
        raise AutomationError("Native Hanbo introduction did not reach Event 140 and restore jumping")
    client.save_or_load(1)
    checkpoint(client, output, "58-hanbo-explore-slot1")
    state = transition(client, resource, "map_019_寒波谷.map", 2)
    if state["variables"].get("Event") != "142":
        raise AutomationError("First native Zixuan encounter did not complete overnight to Event 142")
    client.save_or_load(2)
    checkpoint(client, output, "59-zixuan-first-meeting-slot2")
    state = transition(client, resource, "map_020_樱花谷.map", 2)
    if state["variables"].get("Event") != "144":
        raise AutomationError("Second native Zixuan encounter did not reach the escort Event 144")
    client.save_or_load(3)
    source = checkpoint(client, output, "60-zixuan-escort-before-talk-slot3", variables=("ZiXuanSense", "Talkzx0201"))
    base = int(source["variables"].get("ZiXuanSense") or "0")
    sense = int(source["variables"]["SenseVal"])
    for number in range(1, 8):
        state = interact_named(client, output, resource, "紫轩")
        state = checkpoint(client, output, f"61-zixuan-escort-talk-{number}", variables=("ZiXuanSense", "Talkzx0201"))
        expected = min(base + number * 5, 25) if base <= 20 else base
        if (state["variables"].get("ZiXuanSense") != str(expected)
                or state["variables"].get("SenseVal") != str(sense + expected - base)
                or state["variables"].get("Event") != "144"):
            raise AutomationError("Native Zixuan conversation reward/cap mismatch")
        if number == 4:
            client.save_or_load(4)
    client.save_or_load(6)
    checkpoint(client, output, "62-zixuan-escort-reward-cap-slot6", variables=("ZiXuanSense", "Talkzx0201"))


def huian_date_arrival(client, output, resource):
    state = idle(client)
    if state["map"] != "map_020_樱花谷.map" or state["variables"].get("Event") != "144":
        raise AutomationError("Huian date requires the native Zixuan escort Event 144 checkpoint")
    for destination, trap in (("map_019_寒波谷.map", 1), ("map_018_连接地图.map", 1),
                              ("map_017_连接地图.map", 1), ("map_023_连接地图.map", 3),
                              ("map_024_倚天山.map", 1), ("map_021_油菜花地.map", 1)):
        transition(client, resource, destination, trap)
    client.save_or_load(0)
    checkpoint(client, output, "63-zixuan-escort-farewell-source-slot0")
    state = transition(client, resource, "map_019_寒波谷.map", 3)
    if state["variables"].get("Event") != "145":
        raise AutomationError("Native escort farewell did not complete overnight to Event 145")
    client.save_or_load(1)
    checkpoint(client, output, "64-zixuan-third-morning-slot1")
    state = transition(client, resource, "map_020_樱花谷.map", 2)
    if state["variables"].get("Event") != "146":
        raise AutomationError("Third native Zixuan encounter did not reach the date Event 146")
    client.save_or_load(2)
    checkpoint(client, output, "65-zixuan-date-departure-slot2")
    for destination, trap in (("map_019_寒波谷.map", 1), ("map_018_连接地图.map", 1),
                              ("map_017_连接地图.map", 1), ("map_014_连接地图.map", 1),
                              ("map_012_惠安镇.map", 1)):
        transition(client, resource, destination, trap)
    state = checkpoint(client, output, "66-huian-date-arrival-slot6", variables=("Zixuan", "ZiXuanSense"))
    if state["variables"].get("Event") != "146" or state["variables"].get("Zixuan") != "0":
        raise AutomationError("Native Huian date entrance did not initialize its activity counter")
    client.save_or_load(6)


def huian_singers(client, output, resource):
    state = idle(client)
    if state["map"] != "map_012_惠安镇.map" or state["variables"].get("Event") != "146" or state["player"]["money"] != 0:
        raise AutomationError("Singer tests require the normal cashless Huian date checkpoint")
    client.save_or_load(0)
    singers = (("买唱女", "script/map/map_012_惠安镇/惠安镇卖唱女对话.txt:7"),
               ("买唱老头", "script/map/map_012_惠安镇/惠安镇卖唱老头对话.txt:7"))
    for trap, name in ((17, "song"), (18, "cloth")):
        state = transition(client, resource, "map_012_惠安镇.map", trap)
        state = checkpoint(client, output, f"67-date-{name}-cashless-gate", variables=("Zixuan",))
        if state["player"]["money"] != 0 or state["variables"].get("Zixuan") != "0":
            raise AutomationError("Cashless date entrance unexpectedly charged money or advanced an activity")
    before = 0
    client.move(90, 100, running=False)
    state = interact_named(client, output, resource, "宝箱", position=(7, 78))
    if not 1200 <= state["player"]["money"] - before <= 2000:
        raise AutomationError("Native Huian money chest did not provide its documented random reward")
    client.save_or_load(0)
    checkpoint(client, output, "68-date-funded-before-activities-slot0", variables=("Zixuan",))
    before = state["player"]["money"]
    state = transition(client, resource, "map_012_惠安镇.map", 17)
    state = checkpoint(client, output, "68-date-first-song", variables=("Zixuan",))
    if state["player"]["money"] != before - 1 or state["variables"].get("Zixuan") != "10":
        raise AutomationError("Native first-song entrance did not charge one and advance the activity counter")
    client.save_or_load(1)
    source = checkpoint(client, output, "68-singer-funded-source-slot1")
    for name, site in singers:
        for option in (0, 1):
            client.save_or_load(1, load=True)
            client.act("SetAutoDialogue", enabled=True)
            before = len(trace_records(output))
            command_offset = (output / "commands.jsonl").stat().st_size
            interact_named(client, output, resource, name, choice=(site, option))
            state = checkpoint(client, output, f"69-singer-{name}-funded-{option}")
            records = trace_records(output)[before:]
            movies = [record for record in records if record.get("apiName") == "playmovie"]
            with (output / "commands.jsonl").open(encoding="utf-8") as stream:
                stream.seek(command_offset)
                observations = [json.loads(line)["response"].get("data", {}) for line in stream]
            videos = [value["video"] for value in observations if value.get("video")]
            if (state["player"]["money"] != source["player"]["money"] - (1 if option == 0 else 0)
                    or bool(movies) != (option == 0) or (option == 0 and "sing.wmv" not in videos)
                    or (option == 1 and videos)):
                raise AutomationError("Native funded singer payment/movie mismatch")
    client.save_or_load(1, load=True)
    client.act("SetAutoDialogue", enabled=True)
    # Preserve a funded continuation, then spend normally to reach zero upstairs.
    for x, y in ((49, 198), (46, 210), (53, 219), (50, 231), (48, 235), (90, 100), (102, 70)):
        client.move(x, y, running=False)
        idle(client)
    state = interact_named(client, output, resource, "宝箱", position=(102, 68))
    huian_fortune_singers(client, output, resource)


def huian_fortune_singers(client, output, resource):
    state = idle(client)
    if state["map"] != "map_012_惠安镇.map" or state["variables"].get("Event") != "146" or state["player"]["money"] < 1749:
        raise AutomationError("Fortune cap needs the normally funded Huian date source")
    singers = (("买唱女", "script/map/map_012_惠安镇/惠安镇卖唱女对话.txt:7"),
               ("买唱老头", "script/map/map_012_惠安镇/惠安镇卖唱老头对话.txt:7"))
    client.move(90, 100, running=False)
    idle(client)
    client.save_or_load(2)
    source = checkpoint(client, output, "71-fortune-cap-source-slot2")
    if int(source["variables"].get("YinyuanSense") or "0") != 0:
        raise AutomationError("Fortune cap source already contains marriage rewards")
    fortune = "script/map/map_012_惠安镇/惠安镇路人16对话.txt"
    sequences = dict(marriage=((f"{fortune}:4", 1), (f"{fortune}:33", 1), (f"{fortune}:80", 0)),
                     future=((f"{fortune}:4", 1), (f"{fortune}:33", 0), (f"{fortune}:38", 0)),
                     news=((f"{fortune}:4", 0), (f"{fortune}:10", 0)))
    for number in range(1, 8):
        state = interact_named(client, output, resource, "刘半仙", choice=sequences["marriage"])
        state = checkpoint(client, output, f"72-fortune-marriage-payment-{number}")
        if (state["player"]["money"] != source["player"]["money"] - number * 200
                or state["variables"].get("YinyuanSense") != str(min(number, 6) * 5)
                or state["variables"].get("SenseVal") != str(int(source["variables"]["SenseVal"]) + min(number, 6) * 5)):
            raise AutomationError("Native marriage payment or reward cap mismatch")
        if number in (6, 7):
            client.save_or_load(5 if number == 6 else 4)
    for branch, cost in (("marriage", 200), ("future", 50), ("news", 10)):
        while state["player"]["money"] >= cost:
            before = state["player"]["money"]
            state = interact_named(client, output, resource, "刘半仙", choice=sequences[branch])
            if state["player"]["money"] != before - cost:
                raise AutomationError(f"Native {branch} spending did not charge {cost}")
        before = state["player"]["money"]
        state = interact_named(client, output, resource, "刘半仙", choice=sequences[branch])
        checkpoint(client, output, f"73-fortune-{branch}-insufficient")
        if state["player"]["money"] != before:
            raise AutomationError(f"Insufficient {branch} funds were charged")
    while state["player"]["money"] > 0:
        before = state["player"]["money"]
        state = interact_named(client, output, resource, singers[0][0], choice=(singers[0][1], 0))
        if state["player"]["money"] != before - 1:
            raise AutomationError("Native repeat singing did not spend the remaining single coins")
    client.save_or_load(3)
    checkpoint(client, output, "74-singers-cashless-upstairs-slot3")
    for name, site in singers:
        for option in (0, 1):
            client.save_or_load(3, load=True)
            client.act("SetAutoDialogue", enabled=True)
            before = len(trace_records(output))
            interact_named(client, output, resource, name, choice=(site, option))
            state = checkpoint(client, output, f"75-singer-{name}-cashless-{option}", variables=("SongMoney", "SognMoney"))
            if state["player"]["money"] != 0 or any(record.get("apiName") == "playmovie" for record in trace_records(output)[before:]):
                raise AutomationError("Cashless/refused singer unexpectedly charged money or played a movie")
    client.save_or_load(4, load=True)
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(6)
    checkpoint(client, output, "70-huian-singers-completed-slot6")



def huian_date_orders(client, output, resource, single_order=False):
    state = idle(client)
    state = client.observe((*VARIABLES, "Zixuan"))
    if state["map"] != "map_012_惠安镇.map" or state["variables"].get("Event") != "146" or state["variables"].get("Zixuan") != "0":
        raise AutomationError("Date orders require the native pre-activity checkpoint")
    if state["player"]["money"] == 0:
        client.move(90, 100, running=False)
        idle(client)
        state = interact_named(client, output, resource, "宝箱", position=(7, 78))
    if state["player"]["money"] < 151:
        raise AutomationError("Both date activities need at least 151 normal coins")
    client.save_or_load(0)
    source = checkpoint(client, output, "76-date-orders-funded-source-slot0", variables=("Zixuan",))
    orders = (("song-cloth", (17, 18), (1, 2, 6)), ("cloth-song", (18, 17), (3, 4, 5)))
    for order, traps, slots in (orders[:1] if single_order else orders):
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        spent = 0
        for number, trap in enumerate(traps, 1):
            spent += 1 if trap == 17 else 150
            transition(client, resource, "map_012_惠安镇.map", trap)
            state = checkpoint(client, output, f"77-date-{order}-activity-{number}", variables=("Zixuan",))
            if state["variables"].get("Event") != "146" or state["variables"].get("Zixuan") != str(number * 10) or state["player"]["money"] != source["player"]["money"] - spent:
                raise AutomationError("Native date activity cost/order mismatch")
            client.save_or_load(slots[number - 1])
        state = transition(client, resource, "map_022_清平乡.map", 16)
        state = checkpoint(client, output, f"78-date-{order}-rain-qingping", variables=("Zixuan",))
        if state["variables"].get("Event") != "150" or state["variables"].get("Zixuan") != "20":
            raise AutomationError("Native rain aftermath did not reach Qingping Event 150")
        start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == "script/map/map_012_惠安镇/rain.txt")
        write_json(output / f"date-{order}-proof.json", dict(sourceSlot=0, afterFile=f"78-date-{order}-rain-qingping.json", records=completed_script(output, start), cheatAssisted=False))
        client.save_or_load(slots[2])
    load_checkpoint(client, 6)
    client.act("SetAutoDialogue", enabled=True)
    checkpoint(client, output, "79-date-orders-completed-slot6", variables=("Zixuan",))


def qingping_rescue(client, output, resource):
    state = idle(client)
    if state["map"] == "map_012_惠安镇.map" and state["variables"].get("Event") == "146":
        state = client.observe((*VARIABLES, "Zixuan"))
        if state["variables"].get("Zixuan") != "10" or state["player"]["money"] < 150:
            raise AutomationError("Funded continuation must have completed only the first song")
        before = state["player"]["money"]
        transition(client, resource, "map_012_惠安镇.map", 18)
        state = checkpoint(client, output, "80-date-cloth-completed", variables=("Zixuan",))
        if state["player"]["money"] != before - 150 or state["variables"].get("Zixuan") != "20":
            raise AutomationError("Native remaining cloth activity mismatch")
        state = transition(client, resource, "map_022_清平乡.map", 16)
    if state["map"] != "map_022_清平乡.map" or state["variables"].get("Event") != "150":
        raise AutomationError("Qingping rescue needs its normal Event 150 checkpoint")
    client.save_or_load(0)
    source = checkpoint(client, output, "81-qingping-arrival-slot0")
    beggars = [target for target in source["targets"] if target["name"] == "乞丐" and target["position"] != {"x": 0, "y": 0}]
    if beggars and source["variables"].get("SubEvent03") == "10":
        state = interact_named(client, output, resource, "乞丐")
        state = checkpoint(client, output, "82-qingping-beggar-completed-shared-quest")
        if state["player"]["money"] != source["player"]["money"] or state["variables"].get("Sub03TalkTimes") != source["variables"].get("Sub03TalkTimes") or sum(item["quantity"] for item in state["inventory"] if item["file"] == "book07-潮月剑法.ini") != sum(item["quantity"] for item in source["inventory"] if item["file"] == "book07-潮月剑法.ini"):
            raise AutomationError("Completed Huian beggar quest rewarded again in Qingping")
    state = transition(client, resource, "map_019_寒波谷.map", 2)
    if state["variables"].get("Event") != "162":
        raise AutomationError("Native Qingping farewell did not finish at Hanbo Event 162")
    client.save_or_load(1)
    checkpoint(client, output, "83-qingping-farewell-hanbo-slot1")
    transition(client, resource, "map_020_樱花谷.map", 2)
    state = transition(client, resource, "map_022_清平乡.map", 2)
    if state["variables"].get("Event") != "164":
        raise AutomationError("Native missing-Zixuan scene did not reach Qingping Event 164")
    client.save_or_load(2)
    checkpoint(client, output, "84-qingping-missing-zixuan-slot2")
    state = interact_named(client, output, resource, "鲁老头")
    if state["variables"].get("Event") != "168":
        raise AutomationError("Native Lu dialogue did not start the three-bandit rescue")
    client.save_or_load(3)
    checkpoint(client, output, "85-qingping-rescue-battle-slot3", variables=("Map022Enemy",))
    for name in ("贾少", "曾泼皮", "史泼皮"):
        state = fight_named(client, output, resource, name, single_target=True)
    state = checkpoint(client, output, "86-qingping-rescue-win", variables=("Map022Enemy",))
    if state["variables"].get("Event") != "170" or state["variables"].get("Map022Enemy") != "3":
        raise AutomationError("Native three-bandit deaths did not rescue Zixuan")
    starts = [record for record in trace_records(output) if record.get("eventType") == "script.start" and record.get("virtualPath") == "script/map/map_022_清平乡/清平乡无赖死亡脚本.txt"]
    write_json(output / "qingping-rescue-proof.json", dict(afterFile="86-qingping-rescue-win.json", records=[completed_script(output, start) for start in starts[-3:]], cheatAssisted=False))
    client.save_or_load(4)
    for destination, trap in (("map_021_油菜花地.map", 1), ("map_024_倚天山.map", 1), ("map_023_连接地图.map", 3), ("map_017_连接地图.map", 2), ("map_018_连接地图.map", 2), ("map_019_寒波谷.map", 2)):
        transition(client, resource, destination, trap)
    client.save_or_load(5)
    state = transition(client, resource, "map_019_寒波谷.map", 4)
    if state["variables"].get("Event") != "185" or sum(item["quantity"] for item in state["inventory"] if item["file"] == "goods-e00-木匣.ini") != 0:
        raise AutomationError("Native shelter/overnight did not consume the box and reach Event 185")
    client.save_or_load(6)
    checkpoint(client, output, "87-zixuan-sheltered-next-morning-slot6")


def qingping_missing_boy(client, output, resource):
    variables = ("SubEvent08", "HaveTalk")
    source = client.observe((*VARIABLES, *variables))
    if (source["map"] != "map_022_清平乡.map" or source["variables"].get("Event") != "170"
            or source["variables"].get("Result") != "0"
            or any(int(source["variables"].get(key) or 0) != 0 for key in variables)):
        raise AutomationError("Missing-boy branches require the ordinary post-rescue village without a prior quest")
    client.save_or_load(0)
    source = checkpoint(client, output, "138-missing-boy-source-slot0", variables=variables)
    evidence = []
    directory = "script/map/map_022_清平乡/"

    def talk(name, filename, line):
        path = directory + filename
        sequence = trace_records(output)[-1]["sequence"]
        interact_named(client, output, resource, name)
        starts = [record for record in trace_records(output) if record.get("eventType") == "script.start"
                  and record.get("virtualPath") == path and record["sequence"] > sequence]
        if len(starts) != 1 or starts[0]["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Missing-boy dialogue lacks a fresh current-source execution")
        records = completed_script(output, starts[0])
        if not any(record.get("eventType") == "source.line" and record.get("line") == line for record in records):
            raise AutomationError("Missing-boy dialogue did not complete its expected native branch")
        evidence.append(dict(path=path, expectedLine=line, records=records))
        return client.observe((*VARIABLES, *variables))

    def check(state, quest, evil_delta, talked):
        if (state["variables"].get("SubEvent08") != str(quest)
                or int(state["variables"].get("HaveTalk") or 0) != talked
                or int(state["variables"]["EvilVal"]) != int(source["variables"]["EvilVal"]) + evil_delta
                or state["inventory"] != source["inventory"] or state["player"]["money"] != source["player"]["money"]
                or any(state["variables"].get(key) != source["variables"].get(key) for key in ("Event", "Result", "SenseVal", "EvilValue"))):
            raise AutomationError("Missing-boy branch changed its quest, reward, or main story state")

    check(talk("阿亮他妈", "阿亮他妈对话.txt", 17), 5, -10, 0)
    check(talk("阿亮他妈", "阿亮他妈对话.txt", 20), 5, -10, 0)
    check(talk("顺子", "清平乡村民4对话.txt", 42), 5, -10, 1)
    check(talk("顺子", "清平乡村民4对话.txt", 34), 5, -10, 1)
    client.save_or_load(1)
    checkpoint(client, output, "139-missing-boy-pending-source-slot1", variables=variables)
    sequence = trace_records(output)[-1]["sequence"]
    state = transition(client, resource, "map_021_油菜花地.map", 1, running=True)
    state = client.observe((*VARIABLES, *variables))
    check(state, 20, -10, 1)
    path = directory + "trap01.txt"
    starts = [record for record in trace_records(output) if record.get("eventType") == "script.start"
              and record.get("virtualPath") == path and record["sequence"] > sequence]
    if len(starts) != 1 or starts[0]["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
        raise AutomationError("Missing-boy departure lacks its fresh native trap execution")
    records = completed_script(output, starts[0])
    if not any(record.get("eventType") == "source.line" and record.get("line") == 81 for record in records):
        raise AutomationError("Village departure did not complete the missing-boy failure branch")
    evidence.append(dict(path=path, expectedLine=81, records=records))
    transition(client, resource, "map_022_清平乡.map", 2, running=True)
    check(talk("阿亮他妈", "阿亮他妈对话.txt", 24), 20, -10, 1)
    client.save_or_load(3)
    checkpoint(client, output, "140-missing-boy-left-village-slot3", variables=variables)
    load_checkpoint(client, 1)
    client.act("SetAutoDialogue", enabled=True)
    state = talk("阿亮", "清平乡阿亮对话.txt", 41)
    check(state, 10, 10, 1)
    checkpoint(client, output, "141-missing-boy-home", variables=variables)
    check(talk("阿亮", "清平乡阿亮对话.txt", 5), 10, 10, 1)
    check(talk("阿亮他妈", "阿亮他妈对话.txt", 59), 10, 10, 1)
    client.save_or_load(6)
    checkpoint(client, output, "142-missing-boy-complete-slot6", variables=variables)
    write_json(output / "missing-boy-native-proof.json", dict(executions=evidence, cheatAssisted=False, fullPlaythrough=False))


def huian_investigation(client, output, resource):
    state = idle(client)
    if state["map"] != "map_019_寒波谷.map" or state["variables"].get("Event") != "185":
        raise AutomationError("Investigation needs the normal sheltered-Zixuan checkpoint")
    for destination, trap in (("map_018_连接地图.map", 1), ("map_017_连接地图.map", 1), ("map_014_连接地图.map", 1), ("map_012_惠安镇.map", 1)):
        transition(client, resource, destination, trap)
    client.save_or_load(0)
    source = checkpoint(client, output, "88-investigation-town-source-slot0")
    for name, slot in (("任冬", 1), ("沈三石", 2)):
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        state = interact_named(client, output, resource, name)
        if state["variables"].get("Event") != "190" or state["player"]["money"] != source["player"]["money"]:
            raise AutomationError("Native townsman investigation did not reach Event 190")
        client.save_or_load(slot)
        checkpoint(client, output, f"89-investigation-{name}-slot{slot}")
    for destination, trap in (("map_014_连接地图.map", 2), ("map_017_连接地图.map", 3), ("map_018_连接地图.map", 2), ("map_019_寒波谷(a).map", 2)):
        transition(client, resource, destination, trap)
    client.save_or_load(3)
    checkpoint(client, output, "90-hanbo-fire-arrival-slot3")
    state = transition(client, resource, "map_019_寒波谷(a).map", 5)
    if state["variables"].get("Event") != "195" or len([target for target in state["targets"] if npc_attackable(target)]) != 10:
        raise AutomationError("Native burning-Hanbo scene did not activate all ten enemies")
    client.save_or_load(6)
    checkpoint(client, output, "91-hanbo-fire-battle-source-slot6", variables=("Enemy2",))


def hanbo_fire_battle(client, output, resource):
    state = idle(client)
    if state["map"] != "map_019_寒波谷(a).map" or state["variables"].get("Event") != "195":
        raise AutomationError("Fire battle needs its ordinary ten-enemy source")
    if not (output / "92-hanbo-fire-ten-source-slot0.json").exists():
        client.save_or_load(0)
        checkpoint(client, output, "92-hanbo-fire-ten-source-slot0", variables=("Enemy2",))
    chief, companion = "夺命一点金", "大块"
    for name in ("阿忠", "阿叶", "光头", "阿彪", "阿开", "阿生", "秃头", "大傻"):
        state = client.observe(VARIABLES)
        if not any(target["name"] == name and npc_attackable(target) for target in state["targets"]):
            continue
        try:
            state = fight_named(client, output, resource, name, single_target=True)
        except AutomationError as error:
            if name != "阿生" or str(error) != "StartCombat: no_progress":
                raise
            checkpoint(client, output, "93-hanbo-archer-stalled", variables=("Enemy2",))
            client.act("JumpTo", generation=client.observe()["generation"], x=33, y=68, timeoutMs=30000)
            client.move(28, 70, running=True)
            state = fight_named(client, output, resource, name, single_target=True)
    state = checkpoint(client, output, "93-hanbo-fire-eight-killed-slot1", variables=("Enemy2",))
    if state["variables"].get("Enemy2") != "8" or {target["name"] for target in state["targets"] if npc_attackable(target)} != {chief, companion}:
        raise AutomationError("Normal single attacks did not preserve both last-death candidates")
    client.save_or_load(1)
    site = "script/map/map_063_药王谷/事件21_4.txt:11"
    sense = int(state["variables"]["SenseVal"])
    for order, first, last, slot in (("chief-last", companion, chief, 2), ("companion-last", chief, companion, 3)):
        load_checkpoint(client, 1)
        client.act("SetAutoDialogue", enabled=True)
        fight_named(client, output, resource, first, single_target=True)
        client.save_or_load(slot)
        checkpoint(client, output, f"94-hanbo-fire-{order}-nine-source-slot{slot}", variables=("Enemy2",))
        for option in (0, 1):
            load_checkpoint(client, slot)
            client.act("SetAutoDialogue", enabled=True)
            state = fight_named(client, output, resource, last, single_target=True, choice=(site, option))
            state = checkpoint(client, output, f"95-hanbo-fire-{order}-doctor-{option}", variables=("Enemy2", "SelectVal63"))
            if state["map"] != "map_063_药王谷.map" or state["variables"].get("Event") != "210" or state["variables"].get("Enemy2") != "10" or state["variables"].get("SenseVal") != str(sense + (40 if option == 0 else -40)):
                raise AutomationError("Native fire rescue/doctor choice did not complete its expected aftermath")
            path = "script/map/map_019_寒波谷(a)/" + ("夺命一点金死亡.txt" if last == chief else "夺命一点金跟班死亡.txt")
            start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath", "").casefold() == path.casefold() and record.get("contentSha256") == hashlib.sha256((resource / path).read_bytes()).hexdigest())
            write_json(output / f"hanbo-fire-{order}-doctor-{option}-proof.json", dict(sourceSlot=slot, afterFile=f"95-hanbo-fire-{order}-doctor-{option}.json", records=completed_script(output, start), cheatAssisted=False))
            if order == "companion-last":
                client.save_or_load(4 if option == 0 else 5)
    load_checkpoint(client, 4)
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(6)
    checkpoint(client, output, "96-yaowang-accepted-request-slot6", variables=("Enemy2", "SelectVal63"))


def qingping_beggar(client, output, resource):
    state = idle(client)
    if state["map"] != "map_022_清平乡.map" or state["variables"].get("Event") not in ("150", "170") or int(state["variables"].get("SubEvent03") or 0) != 0 or int(state["variables"].get("Sub03TalkTimes") or 0) != 0 or state["player"]["money"] < 800:
        raise AutomationError("Qingping beggar needs its funded normal route with no earlier donations")
    client.save_or_load(0)
    source = checkpoint(client, output, "97-qingping-beggar-source-slot0")
    site = "script/map/map_022_清平乡/乞丐对话.txt"
    state = interact_named(client, output, resource, "乞丐", choice=(f"{site}:20", 1))
    if state["player"]["money"] != source["player"]["money"] or int(state["variables"].get("Sub03TalkTimes") or 0) != 0:
        raise AutomationError("Refused Qingping donation unexpectedly changed funds/count")
    checkpoint(client, output, "98-qingping-beggar-refused")
    load_checkpoint(client, 0)
    client.act("SetAutoDialogue", enabled=True)
    for number in range(1, 11):
        state = interact_named(client, output, resource, "乞丐", choice=(f"{site}:20", 0))
        state = checkpoint(client, output, f"99-qingping-beggar-donation-{number}")
        if state["player"]["money"] != source["player"]["money"] - number * 80 or state["variables"].get("Sub03TalkTimes") != str(number) or state["variables"].get("EvilVal") != str(int(source["variables"]["EvilVal"]) + number * 5):
            raise AutomationError("Qingping donation payment/count/evil mismatch")
    client.save_or_load(1)
    book_source = checkpoint(client, output, "100-qingping-beggar-book-source-slot1")
    for option in (0, 1):
        load_checkpoint(client, 1)
        client.act("SetAutoDialogue", enabled=True)
        state = interact_named(client, output, resource, "乞丐", choice=(f"{site}:80", option))
        books = sum(item["quantity"] for item in state["inventory"] if item["file"] == "book07-潮月剑法.ini")
        if state["variables"].get("SubEvent03") != "10" or state["variables"].get("Sub03TalkTimes") != "11" or books != 1 - option or state["player"]["money"] != book_source["player"]["money"] or state["variables"].get("EvilVal") != str(int(book_source["variables"]["EvilVal"]) + (-40 if option == 0 else 10)):
            raise AutomationError("Native Qingping book decision reward mismatch")
        client.save_or_load(2 if option == 0 else 3)
        checkpoint(client, output, f"101-qingping-beggar-book-{option}")
        repeated = interact_named(client, output, resource, "乞丐")
        if repeated["variables"] != state["variables"] or repeated["inventory"] != state["inventory"] or repeated["player"]["money"] != state["player"]["money"]:
            raise AutomationError("Completed Qingping beggar quest rewarded twice")
        checkpoint(client, output, f"102-qingping-beggar-book-{option}-repeat")
    load_checkpoint(client, 2)
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(6)
    checkpoint(client, output, "103-qingping-beggar-completed-slot6")


def pili_entry(client, output, resource):
    state = idle(client)
    if state["map"] != "map_063_药王谷.map" or state["variables"].get("Event") != "210":
        raise AutomationError("Pili entry requires the normal doctor's request checkpoint")
    transition(client, resource, "map_064_霹雳堂.map", 1, running=True)
    client.save_or_load(0)
    checkpoint(client, output, "104-pili-day-arrival-slot0")
    state = transition(client, resource, "map_064_霹雳堂.map", 5, running=True)
    if state["variables"].get("Event") != "211":
        raise AutomationError("Native overheard Pili defence dialogue did not reach Event 211")
    client.save_or_load(6)
    checkpoint(client, output, "105-pili-day-or-night-source-slot6", variables=("DayIn",))


def fight_hostiles(client, output, resource, use_magic=True, choice=None, single_target=False, magic_file="player-magic-烈火情天.ini"):
    while True:
        state = client.observe(VARIABLES)
        foes = [target for target in state["targets"] if target.get("hostile") and npc_attackable(target)]
        if not foes:
            break
        spell = next(item for item in state["magic"] if item["file"] == magic_file)
        config = configparser.ConfigParser(interpolation=None)
        config.read(resource / "ini/magic" / spell["file"], encoding="utf-8-sig")
        cost = config.getint(f"Level{spell['level']}", "ManaCost")
        melee_only = use_magic and state["player"]["mana"] < cost
        position = state["player"]["position"]
        target = min(foes, key=lambda item: abs(item["position"]["x"] - position["x"]) * 2 + abs(item["position"]["y"] - position["y"]))
        try:
            fight_named(client, output, resource, target["name"], use_magic=use_magic and not melee_only,
                        target_id=target["id"], choice=choice, single_target=single_target or melee_only, magic_file=magic_file)
        except AutomationError as error:
            if str(error) == "StartCombat: target_unreachable":
                try:
                    fight_named(client, output, resource, target["name"], use_magic=False,
                                target_id=target["id"], choice=choice, single_target=True, magic_file=magic_file)
                except AutomationError as fallback_error:
                    if str(fallback_error) != "StartCombat: no_progress":
                        raise
                    error = fallback_error
                else:
                    continue
            if str(error) in ("StartCombat: no_progress", "StartCombat: action_timeout"):
                current = checkpoint(client, output, f"combat-crowded-{time.time_ns()}")
                nearby = [item for item in current["targets"] if item.get("hostile") and npc_attackable(item)]
                position = current["player"]["position"]
                distance = lambda item: abs(item["position"]["x"] - position["x"]) * 2 + abs(item["position"]["y"] - position["y"])
                nearest = min(nearby, key=distance) if nearby else None
                if nearest and nearest["id"] != target["id"] and distance(nearest) <= 14:
                    fight_named(client, output, resource, nearest["name"], use_magic=False,
                                target_id=nearest["id"], choice=choice, single_target=True, magic_file=magic_file)
                    continue
            if str(error) == f"Expected an attackable {target['name']}" and not any(
                    item["id"] == target["id"] and npc_attackable(item) for item in client.observe()["targets"]):
                print("Pending native damage defeated the selected target; observe the remaining enemies", flush=True)
                continue
            if str(error).startswith("StartCombat: item_depleted: "):
                depleted = str(error).removeprefix("StartCombat: item_depleted: ")
                if not any(item["file"] == depleted and item["quantity"] > 0 for item in client.observe()["inventory"]):
                    print(f"Native combat consumed the last {depleted}; observe remaining supplies", flush=True)
                    continue
            if str(error) != "StartCombat: skill_resources_unavailable" or client.observe()["player"]["mana"] >= cost:
                raise error
            print("Native spell exhausted mana; continue with ordinary melee", flush=True)


def pili_day_route(client, output, resource):
    state = idle(client)
    if state["map"] != "map_064_霹雳堂.map" or state["variables"].get("Event") not in ("211", "212"):
        raise AutomationError("Day break-in requires the ordinary Pili Event 211 source")
    if not (output / "106-pili-day-break-in.json").exists():
        client.save_or_load(0)
    if state["variables"].get("Event") == "211":
        state = transition(client, resource, "map_064_霹雳堂.map", 11, running=True)
        if state["variables"].get("Event") == "211":
            for x, y in ((22, 77), (25, 83), (29, 87), (34, 90)):
                client.move(x, y, running=True)
                idle(client)
            action = client.submit("JumpTo", generation=client.observe()["generation"], x=35, y=84, timeoutMs=30000)
            client.wait_action(action, allow_cancelled=True)
            state = idle(client)
    state = checkpoint(client, output, "106-pili-day-break-in", variables=("DayIn",))
    if state["variables"].get("Event") != "212" or state["variables"].get("DayIn") != "10":
        raise AutomationError("Native daytime trespass did not activate Event 212/DayIn 10")
    client.save_or_load(1)
    for name in ("霹雳堂门卫", "霹雳堂大雷", "霹雳堂二雷"):
        state = client.observe(VARIABLES)
        if any(target["name"] == name and npc_attackable(target) for target in state["targets"]):
            fight_named(client, output, resource, name, use_magic=name != "霹雳堂门卫")
    fight_hostiles(client, output, resource)
    client.save_or_load(2)
    source = checkpoint(client, output, "107-pili-day-thunder-before-pickup-slot2", variables=("DayIn",))
    state = interact_named(client, output, resource, "雷震子")
    if state["variables"].get("Event") != "217" or sum(item["quantity"] for item in state["inventory"] if item["file"] == "goods-e02-雷震子.ini") != 1:
        raise AutomationError("Native daytime treasure pickup did not start Event 217")
    transition(client, resource, "map_064_霹雳堂.map", 12, running=True)
    state = checkpoint(client, output, "108-pili-day-timer-escaped", variables=("DayIn",))
    start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == "script/map/map_064_霹雳堂/爆炸停止.txt")
    records = completed_script(output, start)
    if not any(record.get("apiName") == "closetimelimit" for record in records):
        raise AutomationError("Day escape did not close the native explosion timer")
    write_json(output / "pili-day-escape-proof.json", dict(sourceSlot=2, afterFile="108-pili-day-timer-escaped.json", records=records, cheatAssisted=False))
    client.save_or_load(3)
    state = transition(client, resource, "map_063_药王谷.map", 1, running=True)
    if state["variables"].get("Event") != "220":
        raise AutomationError("Daytime escape did not return with the treasure to Yaowang Event 220")
    client.save_or_load(6)
    checkpoint(client, output, "109-pili-day-return-slot6", variables=("DayIn",))


def pili_night_route(client, output, resource):
    state = idle(client)
    if state["map"] != "map_064_霹雳堂.map" or state["variables"].get("Event") not in ("211", "213", "218"):
        raise AutomationError("Night infiltration requires an ordinary Pili night checkpoint")
    if state["variables"].get("Event") == "211":
        client.save_or_load(0)
        state = transition(client, resource, "map_064_霹雳堂.map", 2, running=True)
        if state["variables"].get("Event") != "213":
            raise AutomationError("Waiting until night did not reach Event 213")
        client.save_or_load(1)
        checkpoint(client, output, "110-pili-night-arrival-slot1", variables=("DayIn",))
    if state["variables"].get("Event") == "213":
        if state["player"]["position"]["y"] > 90:
            # Enter over the native jumpable wall while the gate remains closed.
            for x, y in ((22, 77), (25, 83), (29, 87), (34, 90)):
                client.move(x, y, running=True)
                idle(client)
            action = client.submit("JumpTo", generation=client.observe()["generation"], x=35, y=84, timeoutMs=30000)
            client.wait_action(action, allow_cancelled=True)
            idle(client)
        state = client.observe(VARIABLES)
        if any(target["name"] == "段峥" and npc_attackable(target) for target in state["targets"]):
            transition(client, resource, "map_064_霹雳堂.map", 7, running=True)
            fight_named(client, output, resource, "段峥", use_magic=True)
        client.save_or_load(2)
        checkpoint(client, output, "111-pili-night-thunder-before-pickup-slot2", variables=("DayIn",))
        state = interact_named(client, output, resource, "雷震子")
    if state["variables"].get("Event") != "218" or sum(item["quantity"] for item in state["inventory"] if item["file"] == "goods-e02-雷震子.ini") != 1:
        raise AutomationError("Native night pickup did not start Event 218")
    try:
        fight_hostiles(client, output, resource)
    except AutomationError as error:
        if str(error) != "StartCombat: no_progress" or client.observe(VARIABLES)["variables"].get("Event") != "218":
            raise
        checkpoint(client, output, "112-pili-night-blocked-combat-route")
        print("Night combat stopped at an obstructed target; continue the ordinary exit route", flush=True)
    for x, y in ((50, 54), (50, 60), (40, 78), (35, 84)):
        client.move(x, y, running=True)
        idle(client)
    state = idle(client)
    action = client.submit("JumpTo", generation=state["generation"], x=34, y=90, timeoutMs=15000)
    client.wait_action(action, allow_cancelled=True)
    idle(client)
    state = checkpoint(client, output, "112-pili-night-timer-escaped", variables=("DayIn",))
    start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == "script/map/map_064_霹雳堂/爆炸停止.txt")
    records = completed_script(output, start)
    if not any(record.get("apiName") == "closetimelimit" for record in records) or any(target["name"] in ("霹雳堂大门", "门类障碍") for target in state["targets"]):
        raise AutomationError("Night escape did not close its timer and remove the gate obstacles")
    write_json(output / "pili-night-escape-proof.json", dict(sourceSlot=2, afterFile="112-pili-night-timer-escaped.json", records=records, cheatAssisted=False))
    client.save_or_load(3)
    state = transition(client, resource, "map_063_药王谷.map", 1, running=True)
    if state["variables"].get("Event") != "220":
        raise AutomationError("Night escape did not reach Yaowang Event 220")
    client.save_or_load(6)
    checkpoint(client, output, "113-pili-night-return-slot6", variables=("DayIn",))


def pili_timeout(client, output, resource):
    state = idle(client)
    event = state["variables"].get("Event")
    if state["map"] != "map_064_霹雳堂.map" or event not in ("212", "213"):
        raise AutomationError("Explosion timeout requires an ordinary pre-pickup save")
    source = checkpoint(client, output, "114-pili-timeout-source")
    start_offset = len(trace_records(output))
    state = interact_named(client, output, resource, "雷震子")
    expected_event, seconds = ("217", 20) if event == "212" else ("218", 60)
    if state["variables"].get("Event") != expected_event:
        raise AutomationError("Timeout pickup did not start its native timed branch")
    videos = []
    deadline, next_report = time.monotonic() + seconds + 90, time.monotonic()
    while time.monotonic() < deadline:
        state = client.observe(VARIABLES)
        if state.get("video"):
            videos.append(state["video"])
            checkpoint(client, output, "115-pili-explosion-video")
            client.ui("Cancel")
        if state.get("scene") == "Title":
            break
        if time.monotonic() >= next_report:
            print(f"Waiting for native {seconds}-second explosion: life {state.get('player', {}).get('life')}", flush=True)
            next_report = time.monotonic() + 20
        time.sleep(0.2)
    else:
        raise TimeoutError("Pili explosion did not return to Title")
    after = checkpoint(client, output, "116-pili-explosion-title")
    trace = trace_records(output)[start_offset:]
    start = next(record for record in trace if record.get("eventType") == "script.start" and record.get("virtualPath") == "script/common/霹雳堂爆炸.txt")
    records = completed_script(output, start)
    pickup = next(record for record in trace if record.get("eventType") == "script.start" and record.get("virtualPath") == "script/map/map_064_霹雳堂/雷震子.txt")
    pickup_records = completed_script(output, pickup)
    pickup_path = resource / "script/map/map_064_霹雳堂/雷震子.txt"
    timer_line = next(number for number, line in enumerate(pickup_path.read_text(encoding="utf-8-sig").splitlines(), 1) if source_line(line).strip() == f"opentimelimit({seconds});")
    timer_called = any(record.get("apiName") == "opentimelimit" and index > 0 and pickup_records[index - 1].get("eventType") == "source.line" and pickup_records[index - 1].get("line") == timer_line for index, record in enumerate(pickup_records))
    if "die.wmv" not in videos or not any(record.get("apiName") == "returntotitle" for record in records) or pickup.get("contentSha256") != hashlib.sha256(pickup_path.read_bytes()).hexdigest() or not timer_called:
        raise AutomationError("Timed explosion lacks native timer, movie, and Title evidence")
    write_json(output / "pili-timeout-proof.json", dict(source=source, after=after, seconds=seconds, observedVideos=videos, records=records, pickupRecords=pickup_records, cheatAssisted=False, storyEnding=False))


def yaowang_heal(client, output, resource):
    state = idle(client)
    if state["map"] != "map_063_药王谷.map" or state["variables"].get("Event") != "220":
        raise AutomationError("Zixuan treatment requires the ordinary thunder delivery checkpoint")
    client.save_or_load(0)
    checkpoint(client, output, "117-yaowang-before-delivery-slot0")
    fight_hostiles(client, output, resource)
    client.move(35, 80, running=True)
    idle(client)
    state = interact_named(client, output, resource, "胡神医")
    if state["map"] != "map_022_清平乡.map" or state["variables"].get("Event") != "230" or any(item["file"] == "goods-e02-雷震子.ini" for item in state["inventory"]):
        raise AutomationError("Native treatment did not consume thunder and bring Zixuan home")
    start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == "script/map/map_063_药王谷/trap02.txt")
    write_json(output / "yaowang-heal-proof.json", dict(sourceSlot=0, records=completed_script(output, start), cheatAssisted=False))
    client.save_or_load(6)
    checkpoint(client, output, "118-zixuan-homeward-slot6")


def qingping_beggar_insufficient(client, output, resource):
    state = idle(client)
    if state["map"] != "map_022_清平乡.map" or state["variables"].get("Event") != "170" or int(state["variables"].get("Sub03TalkTimes") or 0) != 0 or not 1160 <= state["player"]["money"] < 1240:
        raise AutomationError("Insufficient-donation preparation requires the ordinary untouched village source and medicine budget")
    client.save_or_load(0)
    source = checkpoint(client, output, "126-qingping-before-medicine-purchase")
    state = interact_named(client, output, resource, "梁掌柜", shop=True)
    drug = next(item for item in state["shop"] if item["file"] == "goods-m07-生黄芩.ini")
    client.buy(drug["slot"])
    bought = client.observe(VARIABLES)
    if bought["player"]["money"] != source["player"]["money"] - 1160 or not any(item["file"] == drug["file"] and item["quantity"] == 1 for item in bought["inventory"]):
        raise AutomationError("Normal medicine purchase did not charge its derived 1160-coin price")
    checkpoint(client, output, "127-qingping-medicine-purchased")
    client.ui("Cancel")
    state = idle(client)
    client.save_or_load(1)
    before = checkpoint(client, output, "128-qingping-insufficient-donation-source-slot1")
    state = interact_named(client, output, resource, "乞丐", choice=("script/map/map_022_清平乡/乞丐对话.txt:20", 0))
    if state["player"]["money"] != before["player"]["money"] or int(state["variables"].get("Sub03TalkTimes") or 0) != 0 or state["variables"].get("EvilVal") != before["variables"].get("EvilVal"):
        raise AutomationError("Insufficient Qingping donation changed payment/count/evil")
    client.save_or_load(6)
    checkpoint(client, output, "129-qingping-insufficient-donation-completed-slot6")


def qingping_clue(client, output, resource):
    state = idle(client)
    if state["map"] != "map_022_清平乡.map" or state["variables"].get("Event") != "230":
        raise AutomationError("Investigation decision requires the normal cured-Zixuan village arrival")
    state = transition(client, resource, "map_022_清平乡.map", 2)
    if state["variables"].get("Event") != "231":
        raise AutomationError("Escorting Zixuan home did not enable the innkeeper clue")
    client.save_or_load(0)
    source = checkpoint(client, output, "119-qingping-investigation-source-slot0")
    site = next(site for site in inventory(resource)["choices"] if site["path"] == "script/map/map_022_清平乡/trap06.txt")
    for option in (0, 1):
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        state = transition(client, resource, "map_022_清平乡.map", 6, choice=(site["id"], option), output=output)
        if state["variables"].get("Event") != "232" or state["variables"].get("EvilVal") != str(int(source["variables"]["EvilVal"]) + (20 if option == 0 else -20)):
            raise AutomationError("Native open/covert investigation decision mismatch")
        client.save_or_load(1 + option)
        checkpoint(client, output, f"120-qingping-investigation-{option}", variables=("Event24Select",))
    client.save_or_load(6)
    checkpoint(client, output, "121-qingping-investigation-covert-slot6", variables=("Event24Select",))


def cangjian_truth(client, output, resource):
    idle(client)
    for option, slot in ((0, 1), (1, 2)):
        proof_path = output / f"cangjian-truth-{option}-proof.json"
        path = f"script/map/map_016_剑气峰/trap0{2 + option}.txt"
        observations = [json.loads(line)["response"].get("data", {}) for line in (output / "commands.jsonl").open(encoding="utf-8")]
        videos = [value["video"] for value in observations if value.get("video") == "yyf-fall.wmv" and value.get("script") == path]
        if proof_path.exists() and videos:
            proof = json.loads(proof_path.read_text(encoding="utf-8"))
            proof["observedVideos"] = videos
            write_json(proof_path, proof)
            continue
        state = client.observe((*VARIABLES, "Event24Select"))
        if not (videos and state["map"] == "map_051_海边.map" and state["variables"].get("Event") == "250" and state["variables"].get("Event24Select") == str(option)):
            load_checkpoint(client, slot)
            client.act("SetAutoDialogue", enabled=True)
            state = client.observe((*VARIABLES, "Event24Select"))
            if state["map"] != "map_022_清平乡.map" or state["variables"].get("Event") != "232" or state["variables"].get("Event24Select") != str(option):
                raise AutomationError("Cangjian truth lacks its ordinary investigation branch source")
            for destination, trap in (("map_021_油菜花地.map", 1), ("map_024_倚天山.map", 1), ("map_023_连接地图.map", 3), ("map_017_连接地图.map", 2), ("map_014_连接地图.map", 1), ("map_015_藏剑山庄.map", 2)):
                transition(client, resource, destination, trap, running=True)
            state = transition(client, resource, "map_015_藏剑山庄.map", 12)
            if state["variables"].get("Event") != ("234" if option == 0 else "233"):
                raise AutomationError("Native Cangjian investigation entry mismatch")
            checkpoint(client, output, f"122-cangjian-investigation-{option}", variables=("Event24Select",))
            if option == 1:
                state = transition(client, resource, "map_015_藏剑山庄.map", 7)
                if state["variables"].get("Event") != "235":
                    raise AutomationError("Covert silver-needle discovery did not reach Event 235")
                checkpoint(client, output, "123-cangjian-silver-needle-discovered")
            transition(client, resource, "map_016_剑气峰.map", 2)
            try:
                state = interact_named(client, output, resource, "卓非凡")
            except AutomationError as error:
                if str(error) != "Interact: world_changed":
                    raise
                state = idle(client)
        if state["map"] != "map_051_海边.map" or state["variables"].get("Event") != "250":
            raise AutomationError("Native cliff betrayal did not complete Nalan Zhen's rescue")
        path = f"script/map/map_016_剑气峰/trap0{2 + option}.txt"
        start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
        records = completed_script(output, start)
        observations = [json.loads(line)["response"].get("data", {}) for line in (output / "commands.jsonl").open(encoding="utf-8")]
        videos = [value["video"] for value in observations if value.get("video") == "yyf-fall.wmv" and value.get("script") == path]
        if not videos or start.get("contentSha256") != hashlib.sha256((resource / path).read_bytes()).hexdigest() or not any(record.get("apiName") == "playmovie" for record in records):
            raise AutomationError("Cliff betrayal lacks its native movie call")
        client.save_or_load(3 + option)
        checkpoint(client, output, f"124-forget-worry-island-{option}", variables=("Event24Select",))
        write_json(output / f"cangjian-truth-{option}-proof.json", dict(sourceSlot=slot, afterFile=f"124-forget-worry-island-{option}.json", records=records, observedVideos=videos, cheatAssisted=False))
    client.save_or_load(6)
    checkpoint(client, output, "125-forget-worry-island-continuation-slot6")


def huian_knife(client, output, resource):
    state = idle(client)
    if state["map"] != "map_022_清平乡.map" or state["variables"].get("Event") != "170":
        raise AutomationError("Knife preparation requires the ordinary rescued-Zixuan village checkpoint")
    for destination, trap in (("map_021_油菜花地.map", 1), ("map_024_倚天山.map", 1), ("map_023_连接地图.map", 3), ("map_017_连接地图.map", 2), ("map_018_连接地图.map", 2), ("map_019_寒波谷.map", 2)):
        transition(client, resource, destination, trap, running=True)
    state = transition(client, resource, "map_019_寒波谷.map", 4)
    if state["variables"].get("Event") != "185":
        raise AutomationError("Ordinary shelter did not reach the investigation morning")
    for destination, trap in (("map_018_连接地图.map", 1), ("map_017_连接地图.map", 1), ("map_014_连接地图.map", 1), ("map_012_惠安镇.map", 1)):
        transition(client, resource, destination, trap, running=True)
    if client.observe()["player"]["money"] < 1300:
        client.move(90, 100, running=True)
        idle(client)
        state = interact_named(client, output, resource, "宝箱", position=(102, 68))
        if state["player"]["money"] < 1300:
            raise AutomationError("Normal treasure pickup did not fund the knife")
    source = checkpoint(client, output, "130-huian-knife-before-purchase")
    state = interact_named(client, output, resource, "王铁匠", shop=True)
    knife = next(item for item in state["shop"] if item["file"] == "goods-w19-土龙刀.ini")
    client.buy(knife["slot"])
    state = client.observe(VARIABLES)
    if state["player"]["money"] != source["player"]["money"] - 1300 or not any(item["file"] == knife["file"] and item["quantity"] == 1 for item in state["inventory"]):
        raise AutomationError("Normal knife purchase did not charge 1300 and deliver one item")
    checkpoint(client, output, "131-huian-knife-purchased")
    client.ui("Cancel")
    idle(client)
    client.save_or_load(0)
    source = checkpoint(client, output, "132-huian-knife-decision-source-slot0")
    for option in (1, 0):
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        state = interact_named(client, output, resource, "刀客", choice=("script/map/map_012_惠安镇/刀客对话.txt:22", option))
        knives = sum(item["quantity"] for item in state["inventory"] if item["file"] == "goods-w19-土龙刀.ini")
        books = sum(item["quantity"] for item in state["inventory"] if item["file"] == "book09-漫天花雨.ini")
        if knives != option or books != 1 - option or int(state["variables"].get("SubEvent19") or 0) != (0 if option == 1 else 5) or state["variables"].get("EvilVal") != str(int(source["variables"]["EvilVal"]) - (50 if option == 0 else 0)):
            raise AutomationError("Knife decision item/book/evil mismatch")
        client.save_or_load(1 if option == 1 else 2)
        checkpoint(client, output, f"133-huian-knife-{option}", variables=("GiveDao",))
        if option == 0:
            repeated = interact_named(client, output, resource, "刀客")
            if repeated["inventory"] != state["inventory"] or repeated["variables"].get("EvilVal") != state["variables"].get("EvilVal"):
                raise AutomationError("Knife quest repeated its book or evil reward")
    client.save_or_load(6)
    checkpoint(client, output, "134-huian-knife-completed-slot6")


def huian_knife_missing(client, output, resource):
    state = client.observe((*VARIABLES, "DaoKeAddMemo"))
    if (state["map"] != "map_012_惠安镇.map"
            or (state["variables"].get("Result"), state["variables"].get("Event")) not in (("0", "185"), ("1", "570"), ("2", "1806"))
            or int(state["variables"].get("SubEvent19") or 0) != 0
            or any(item["file"] == "goods-w19-土龙刀.ini" for item in state["inventory"])):
        raise AutomationError("Missing-knife dialogue requires a normal town source without the quest item")
    source = checkpoint(client, output, "135-missing-knife-source", variables=("DaoKeAddMemo",))
    path = "script/map/map_012_惠安镇/刀客对话.txt"
    lines = (resource / path).read_text(encoding="utf-8-sig").splitlines()
    no_knife_line = next(index + 2 for index, text in enumerate(lines) if text.strip() == "::HaveNoDao::")
    proofs = []
    for repeat in range(2):
        sequence = trace_records(output)[-1]["sequence"]
        interact_named(client, output, resource, "刀客")
        state = checkpoint(client, output, f"136-missing-knife-dialogue-{repeat}", variables=("DaoKeAddMemo",))
        if (state["inventory"] != source["inventory"] or state["player"]["money"] != source["player"]["money"]
                or state["variables"] != source["variables"]):
            raise AutomationError("Missing-knife dialogue changed money, inventory, or plot variables")
        starts = [record for record in trace_records(output) if record.get("eventType") == "script.start"
                  and record.get("virtualPath") == path and record["sequence"] > sequence]
        if len(starts) != 1 or starts[0]["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Missing-knife dialogue lacks a fresh current-source execution")
        records = completed_script(output, starts[0])
        if (not any(record.get("eventType") == "source.line" and record.get("line") == no_knife_line for record in records)
                or any(record.get("apiName") in ("select", "choose", "addgoods", "delgoods", "addmoney") for record in records)):
            raise AutomationError("Missing-knife dialogue did not take its expected native branch")
        proofs.append(records)
    client.save_or_load(6)
    checkpoint(client, output, "137-missing-knife-complete-slot6", variables=("DaoKeAddMemo",))
    write_json(output / "missing-knife-native-proof.json", dict(executions=proofs, cheatAssisted=False, fullPlaythrough=False))


def forget_worry_herbs(client, output, resource):
    state = idle(client)
    if state["map"] == "map_051_海边.map" and state["variables"].get("Event") == "250":
        checkpoint(client, output, "135-nalan-zhen-player-source")
        transition(client, resource, "map_050_忘忧岛.map", 1, running=True)
        state = interact_named(client, output, resource, "阿里", shop=True)
        if state.get("variables", {}).get("Event") != "254":
            # Shop observations omit requested variables unless explicitly observed.
            state = client.observe(VARIABLES)
        if state["variables"].get("Event") != "254":
            raise AutomationError("Native herbalist request did not reach Event 254")
        drug = next(item for item in state["shop"] if item["file"] == "goods-m00-金花.ini")
        before = state["player"]["money"]
        client.buy(drug["slot"])
        state = client.observe(VARIABLES)
        if state["player"]["money"] != before - 140 or not any(item["file"] == drug["file"] and item["quantity"] == 1 for item in state["inventory"]):
            raise AutomationError("Nalan Zhen's ordinary medicine purchase mismatch")
        client.ui("Cancel")
        idle(client)
        client.save_or_load(0)
        checkpoint(client, output, "136-silver-grass-request-slot0")
        transition(client, resource, "map_053_连接地图.map", 2, running=True)
        transition(client, resource, "map_054_北山.map", 2, running=True)
        client.save_or_load(1)
        checkpoint(client, output, "137-silver-grass-bees-source-slot1")
    elif state["map"] != "map_054_北山.map" or state["variables"].get("Event") not in ("254", "260"):
        raise AutomationError("Silver-grass expedition requires its native rescue or North Mountain checkpoint")
    while True:
        state = client.observe(VARIABLES)
        foes = [target for target in state["targets"] if target.get("hostile") and npc_attackable(target)]
        if not foes:
            break
        position = state["player"]["position"]
        target = min(foes, key=lambda item: abs(item["position"]["x"] - position["x"]) * 2 + abs(item["position"]["y"] - position["y"]))
        fight_named(client, output, resource, target["name"], target_id=target["id"])
    client.save_or_load(2)
    while True:
        before = client.observe(VARIABLES)
        herbs = [target for target in before["targets"] if target["name"] == "草药"]
        if not herbs:
            break
        position = before["player"]["position"]
        target = min(herbs, key=lambda item: abs(item["position"]["x"] - position["x"]) * 2 + abs(item["position"]["y"] - position["y"]))
        quantity = sum(item["quantity"] for item in before["inventory"] if item["file"] == "goods-e12-银丝草.ini")
        checkpoint(client, output, f"138-silver-grass-{quantity}-before")
        state = interact_named(client, output, resource, "草药", position=(target["position"]["x"], target["position"]["y"]))
        if state["variables"].get("Event") != "260" or sum(item["quantity"] for item in state["inventory"] if item["file"] == "goods-e12-银丝草.ini") != quantity + 1 or any(item["id"] == target["id"] for item in state["targets"]):
            raise AutomationError("Native silver-grass pickup did not add one item and remove the selected object")
        checkpoint(client, output, f"138-silver-grass-{quantity + 1}-after")
    if sum(item["quantity"] for item in state["inventory"] if item["file"] == "goods-e12-银丝草.ini") != 7:
        raise AutomationError("North Mountain did not deliver its seven native silver-grass objects")
    checkpoint(client, output, "138-silver-grass-picked")
    for destination, trap in (("map_053_连接地图.map", 1), ("map_050_忘忧岛.map", 1), ("map_051_海边.map", 1)):
        transition(client, resource, destination, trap, running=True)
    client.save_or_load(6)
    checkpoint(client, output, "139-silver-grass-return-slot6")


def forget_worry_care(client, output, resource):
    state = idle(client)
    if state["map"] != "map_051_海边.map" or state["variables"].get("Event") != "260":
        raise AutomationError("Island care requires the ordinary silver-grass return checkpoint")
    client.save_or_load(0)
    source = checkpoint(client, output, "140-island-care-decision-source-slot0")
    for option in (1, 0):
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        state = interact_named(client, output, resource, "躺着的杨影枫", choice=("script/map/map_051_海边/事件27.txt:37", option))
        if state["variables"].get("Event") != "270" or state["variables"].get("SelectVal") != str(option) or state["variables"].get("SenseVal") != str(int(source["variables"]["SenseVal"]) + (40 if option == 0 else -20)) or any(item["file"] == "goods-e12-银丝草.ini" for item in state["inventory"]):
            raise AutomationError("Island care choice sense/herb/event mismatch")
        client.save_or_load(1 + option)
        checkpoint(client, output, f"141-island-care-{option}")
        transition(client, resource, "map_050_忘忧岛.map", 1, running=True)
        state = interact_named(client, output, resource, "纳兰潜凛")
        if state["variables"].get("Event") != "275" or state["player"]["canJump"] != (option == 0):
            raise AutomationError("Native father escort event/jump permission mismatch")
        checkpoint(client, output, f"142-island-father-escort-{option}", variables=("Talknalanql",))
        for number in (1, 2):
            interact_named(client, output, resource, "纳兰潜凛")
            state = checkpoint(client, output, f"143-island-father-repeat-{option}-{number}", variables=("Talknalanql",))
            if state["variables"].get("Talknalanql") != "1" or state["variables"].get("Event") != "275":
                raise AutomationError("Father escort repeat dialogue state mismatch")
        transition(client, resource, "map_051_海边.map", 1)
        state = transition(client, resource, "map_051_海边.map", 3)
        if state["variables"].get("Event") != "285" or not state["player"]["canJump"]:
            raise AutomationError("Native father treatment did not reach the seashore evening")
        client.save_or_load(4 + option)
        checkpoint(client, output, f"144-island-treatment-evening-{option}", variables=("Talknalanql",))
        path = "script/map/map_051_海边/事件28_2.txt"
        start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
        records = completed_script(output, start)
        if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest() or not any(record.get("variableName") == "Event" and record.get("afterValue") == "280" for record in records):
            raise AutomationError("Father treatment lacks its completed native Event 280 evidence")
        write_json(output / f"island-care-{option}-proof.json", dict(sourceFile="140-island-care-decision-source-slot0.json", afterFile=f"144-island-treatment-evening-{option}.json", records=records, cheatAssisted=False))
    client.save_or_load(6)
    checkpoint(client, output, "145-island-treatment-continuation-slot6")


def forget_worry_forbidden(client, output, resource):
    state = idle(client)
    if state["map"] != "map_051_海边.map" or state["variables"].get("Event") != "285":
        raise AutomationError("Forbidden-area investigation requires its native evening checkpoint")
    client.save_or_load(0)
    source = checkpoint(client, output, "146-island-forbidden-decision-source-slot0")
    for option in (1, 0):
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        state = interact_named(client, output, resource, "杨影枫", choice=("script/map/map_051_海边/事件30_1.txt:25", option))
        if state["variables"].get("Event") != ("294" if option else "296") or state["variables"].get("EvilVal") != str(int(source["variables"]["EvilVal"]) + (20 if option else -40)):
            raise AutomationError("Forbidden-area decision evil/event mismatch")
        client.save_or_load(1 + option)
        checkpoint(client, output, f"147-island-forbidden-{option}")
        if option == 1:
            state = interact_named(client, output, resource, "纳兰真")
            if state["variables"].get("Event") != "296":
                raise AutomationError("Refusal could not continue through Nalan Zhen's invitation")
            checkpoint(client, output, "148-island-refusal-rejoin")
        for destination, trap in (("map_050_忘忧岛.map", 1), ("map_057_连接地图.map", 4), ("map_058_禁地.map", 2), ("map_059_禁地一层.map", 2)):
            transition(client, resource, destination, trap)
        state = checkpoint(client, output, f"149-island-forbidden-father-{option}", variables=("NaLanZhenSense",))
        if state["variables"].get("Event") != "300":
            raise AutomationError("Native forbidden-area discovery did not reach Event 300")
        interact_named(client, output, resource, "纳兰潜凛")
        if option == 0:
            sense = int(state["variables"]["SenseVal"])
            for number in range(1, 6):
                interact_named(client, output, resource, "杨影枫")
                after = checkpoint(client, output, f"150-island-nalan-sense-{number}", variables=("NaLanZhenSense",))
                if after["variables"].get("NaLanZhenSense") != str(min(number, 4) * 5) or int(after["variables"]["SenseVal"]) != sense + min(number, 4) * 5:
                    raise AutomationError("Nalan Zhen's ordinary dialogue cap mismatch")
        for destination, trap in (("map_058_禁地.map", 1), ("map_057_连接地图.map", 1), ("map_050_忘忧岛.map", 1), ("map_051_海边.map", 1)):
            transition(client, resource, destination, trap)
        state = transition(client, resource, "map_051_海边.map", 3)
        if state["variables"].get("Event") != "302":
            raise AutomationError("Native forbidden-area return did not finish the next morning")
        client.save_or_load(4 + option)
        checkpoint(client, output, f"151-island-next-morning-{option}", variables=("NaLanZhenSense",))
    client.save_or_load(6)
    checkpoint(client, output, "152-island-next-morning-continuation-slot6", variables=("NaLanZhenSense",))


def pili_boxes(client, output, resource):
    state = idle(client)
    if state["map"] != "map_064_霹雳堂.map" or state["variables"].get("Event") not in ("212", "213"):
        raise AutomationError("Pili boxes require the ordinary pre-thunder pickup checkpoint")
    night = state["variables"]["Event"] == "213"
    client.save_or_load(0)
    checkpoint(client, output, "153-pili-boxes-source-slot0")
    proofs = []
    for number in ((3, 4) if night else (1, 2)):
        seen = set()
        expected = {0, 1, 2} if night else {0, 1}
        for attempt in range(1, 21):
            if seen == expected:
                break
            load_checkpoint(client, 0)
            client.act("SetAutoDialogue", enabled=True)
            before = checkpoint(client, output, f"154-pili-box-{number}-{attempt}-before", variables=("Temp",))
            state = interact_named(client, output, resource, f"宝箱爆{number}", position={1: (47, 21), 2: (39, 75), 3: (45, 17), 4: (49, 28)}[number])
            state = checkpoint(client, output, f"155-pili-box-{number}-{attempt}-after", variables=("Temp",))
            value = int(state["variables"]["Temp"])
            bomb = value == (0 if night else 1)
            damage = 600 if night else 1000
            if state["player"]["life"] != before["player"]["life"] - (damage if bomb else 0):
                raise AutomationError("Pili random box damage mismatch")
            if bomb:
                if any(item["name"] == f"宝箱爆{number}" for item in state["targets"]):
                    raise AutomationError("Exploded Pili boxes were not removed")
            elif night:
                money = state["player"]["money"] - before["player"]["money"]
                minimum, maximum = (550, 900) if number == 3 else (800, 1500)
                if not minimum <= money <= maximum:
                    raise AutomationError("Pili night random money reward outside its native range")
            elif state["inventory"] == before["inventory"]:
                raise AutomationError("Pili day random item reward was not delivered")
            path = f"script/map/map_064_霹雳堂/炸药箱子{number}.txt"
            start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
            records = completed_script(output, start)
            if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest() or not any(record.get("apiName") == "getrandnum" for record in records):
                raise AutomationError("Pili box lacks its completed native random source")
            proofs.append(dict(box=number, value=value, bomb=bomb, beforeFile=f"154-pili-box-{number}-{attempt}-before.json", afterFile=f"155-pili-box-{number}-{attempt}-after.json", records=records))
            seen.add(value)
        if seen != expected:
            raise AutomationError(f"Pili box {number} random values still pending: {expected - seen}")
    if night:
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        before = client.observe(VARIABLES)
        state = interact_named(client, output, resource, "宝箱", position=(37, 14))
        quantity = lambda value: sum(item["quantity"] for item in value["inventory"] if item["file"] == "book01-风火雷.ini")
        if quantity(state) != quantity(before) + 1:
            raise AutomationError("Pili martial-arts box did not deliver one book")
        try:
            interact_named(client, output, resource, "宝箱", position=(37, 14))
        except AutomationError as error:
            if str(error) not in ("action_rejected", "Interact: action_rejected"):
                raise
        state = checkpoint(client, output, "156-pili-martial-book-repeat")
        if quantity(state) != quantity(before) + 1:
            raise AutomationError("Pili martial-arts box duplicated its book")
    write_json(output / "pili-boxes-proof.json", dict(proofs=proofs, cheatAssisted=False))
    client.save_or_load(6)
    checkpoint(client, output, "157-pili-boxes-completed-slot6")


def north_cave_descent(client, output, resource):
    state = idle(client)
    if state["map"] == "map_051_海边.map" and state["variables"].get("Event") == "302":
        checkpoint(client, output, "158-north-cave-morning-source")
        for destination, trap in (("map_050_忘忧岛.map", 1), ("map_053_连接地图.map", 2), ("map_054_北山.map", 2)):
            transition(client, resource, destination, trap)
        client.save_or_load(1)
        checkpoint(client, output, "159-north-cave-bees-source-slot1")
    elif (state["map"], state["variables"].get("Event")) not in (("map_054_北山.map", "302"), ("map_055_山洞.map", "304"), ("map_055_山洞.map", "305")):
        raise AutomationError("North Cave requires its ordinary next-morning mountain checkpoint")
    state = idle(client)
    if state["map"] == "map_054_北山.map":
        while True:
            state = client.observe(VARIABLES)
            foes = [target for target in state["targets"] if target.get("hostile") and npc_attackable(target)]
            if not foes:
                break
            position = state["player"]["position"]
            target = min(foes, key=lambda item: abs(item["position"]["x"] - position["x"]) * 2 + abs(item["position"]["y"] - position["y"]))
            fight_named(client, output, resource, target["name"], target_id=target["id"])
        client.save_or_load(2)
        transition(client, resource, "map_055_山洞.map", 2)
        client.save_or_load(3)
    checkpoint(client, output, "160-north-cave-bats-source-slot3", variables=("Deadbat",))
    while True:
        state = client.observe(VARIABLES)
        foes = [target for target in state["targets"] if target.get("hostile") and npc_attackable(target)]
        if not foes:
            break
        position = state["player"]["position"]
        target = min(foes, key=lambda item: abs(item["position"]["x"] - position["x"]) * 2 + abs(item["position"]["y"] - position["y"]))
        fight_named(client, output, resource, target["name"], target_id=target["id"])
    source = checkpoint(client, output, "161-north-cave-bats-cleared", variables=("Deadbat",))
    if source["variables"].get("Event") != "305" or source["variables"].get("Deadbat") != "25":
        raise AutomationError("Native North Cave complete clearing aftermath mismatch")
    records = trace_records(output)
    advance = next(record for record in reversed(records) if record.get("eventType") == "variable.change" and record.get("variableName") == "Event" and record.get("afterValue") == "305")
    start = next(record for record in records if record.get("eventType") == "script.start" and record.get("executionId") == advance["executionId"])
    completed = completed_script(output, start)
    if start["virtualPath"] != "script/map/map_055_山洞/吸血蝙蝠死亡.txt" or not any(record.get("variableName") == "Deadbat" and record.get("afterValue") == "14" for record in completed):
        raise AutomationError("North Cave story did not advance through its fourteenth native bat death")
    write_json(output / "north-cave-bats-proof.json", dict(afterFile="161-north-cave-bats-cleared.json", records=completed, clearedCount=25, storyThreshold=14, cheatAssisted=False))
    client.save_or_load(0)
    for option in (1, 0):
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        state = transition(client, resource, "map_055_山洞.map" if option else "map_056_盆地.map", 2, choice=("script/map/map_055_山洞/trap02.txt:27", option), output=output)
        if state["variables"].get("SenseVal") != str(int(source["variables"]["SenseVal"]) + (20 if option == 0 else -30)) or state["variables"].get("EvilVal") != str(int(source["variables"]["EvilVal"]) + (20 if option == 0 else -20)):
            raise AutomationError("North Cave descent choice sense/evil mismatch")
        checkpoint(client, output, f"162-north-cave-descent-{option}", variables=("Deadbat",))
        if option == 1:
            if state["variables"].get("Event") != "308":
                raise AutomationError("Deferred jump did not preserve Event 308")
            client.save_or_load(1)
            state = transition(client, resource, "map_055_山洞.map", 1)
            if state["variables"].get("Event") != "309":
                raise AutomationError("Attempted normal return did not activate the deferred jump")
            checkpoint(client, output, "163-north-cave-deferred-jump-rejoin")
            state = transition(client, resource, "map_056_盆地.map", 2)
        if state["variables"].get("Event") != "320":
            raise AutomationError("North Cave descent did not reach the native basin reunion")
        observations = [json.loads(line)["response"].get("data", {}) for line in (output / "commands.jsonl").open(encoding="utf-8")]
        if not any(value.get("video") == "nlz-fall.wmv" and value.get("script") == "script/map/map_055_山洞/trap02.txt" for value in observations):
            raise AutomationError("North Cave lacks the actual Nalan Zhen fall video")
        client.save_or_load(2 if option else 3)
        checkpoint(client, output, f"164-basin-arrival-{option}")
    client.save_or_load(6)
    checkpoint(client, output, "165-basin-arrival-continuation-slot6")


def basin_martial(client, output, resource):
    state = idle(client)
    returning = state["variables"].get("Event") == "340" and state["map"] in ("map_055_山洞.map", "map_054_北山.map", "map_053_连接地图.map", "map_050_忘忧岛.map")
    if returning:
        source = json.loads((output / "166-basin-martial-decision-source-slot0.json").read_text(encoding="utf-8"))
    else:
        if state["map"] != "map_056_盆地.map" or state["variables"].get("Event") not in ("320", "322"):
            raise AutomationError("Basin martial study requires its ordinary reunion checkpoint")
        if state["variables"].get("Event") == "320":
            state = transition(client, resource, "map_056_盆地.map", 4)
        if state["variables"].get("Event") != "322":
            raise AutomationError("Native basin skeleton discovery did not reach Event 322")
        client.save_or_load(0)
        source = checkpoint(client, output, "166-basin-martial-decision-source-slot0")
    for option in (1, 0):
        completed = output / f"171-island-farewell-{option}.json"
        if completed.exists() and json.loads(completed.read_text(encoding="utf-8"))["variables"].get("Event") == "350":
            continue
        if returning and state["variables"].get("SelectVal") == str(option):
            returning = False
        else:
            load_checkpoint(client, 0)
            client.act("SetAutoDialogue", enabled=True)
            state = interact_named(client, output, resource, "丝帕1", choice=("script/map/map_056_盆地/丝帕1.txt:20", option))
            if state["variables"].get("Event") != "325" or state["variables"].get("EvilVal") != str(int(source["variables"]["EvilVal"]) + (-20 if option == 0 else 20)) or not any(item["file"] == "goods-e03-丝绸手帕.ini" and item["quantity"] == 1 for item in state["inventory"]):
                raise AutomationError("Basin martial choice evil/handkerchief/event mismatch")
            client.save_or_load(1 + option)
            checkpoint(client, output, f"167-basin-martial-choice-{option}")
            state = transition(client, resource, "map_056_盆地加坟墓.map", 1)
            if state["variables"].get("Event") != "330" or not any(item["file"] == "player-magic-蚀骨血仞.ini" and item["level"] == 4 for item in state["magic"]) or any(item["name"] == "白骨" for item in state["targets"]):
                raise AutomationError("Native basin burial did not grant level-four martial art")
            checkpoint(client, output, f"168-basin-burial-martial-{option}")
            state = interact_named(client, output, resource, "丝帕2")
            if state["variables"].get("Event") != "335":
                raise AutomationError("Native mother's cloth evidence did not reopen the cave exit")
            checkpoint(client, output, f"169-basin-mothers-cloth-{option}")
            transition(client, resource, "map_056_盆地加坟墓.map", 5)
            checkpoint(client, output, f"169-basin-grave-kneeling-{option}")
        for origin, destination in (("map_056_盆地加坟墓.map", "map_055_山洞.map"), ("map_055_山洞.map", "map_054_北山.map"), ("map_054_北山.map", "map_053_连接地图.map"), ("map_053_连接地图.map", "map_050_忘忧岛.map")):
            state = idle(client)
            if state["map"] != origin:
                continue
            if origin in ("map_055_山洞.map", "map_054_北山.map"):
                while True:
                    state = client.observe(VARIABLES)
                    foes = [target for target in state["targets"] if target.get("hostile") and npc_attackable(target)]
                    if not foes:
                        break
                    position = state["player"]["position"]
                    target = min(foes, key=lambda item: abs(item["position"]["x"] - position["x"]) * 2 + abs(item["position"]["y"] - position["y"]))
                    fight_named(client, output, resource, target["name"], target_id=target["id"])
                state = idle(client)
                if origin == "map_054_北山.map" and state["player"]["position"]["x"] >= 65 and state["player"]["position"]["y"] >= 43:
                    # Leave the narrow northern lane until the partner follows behind.
                    client.move(64, 60, running=False)
                    client.move(66, 48, running=False)
            transition(client, resource, destination, 1)
        state = transition(client, resource, "map_050_忘忧岛.map", 18)
        checkpoint(client, output, f"170-island-return-to-father-{option}")
        if state["variables"].get("Event") != "350":
            raise AutomationError("Native island return did not reach the farewell morning")
        client.save_or_load(4 + option)
        checkpoint(client, output, f"171-island-farewell-{option}")
    client.save_or_load(6)
    checkpoint(client, output, "172-island-farewell-continuation-slot6")


def island_departure(client, output, resource):
    state = idle(client)
    if state["map"] == "map_052_码头.map" and state["variables"].get("Event") == "350" and (output / "173-island-dock-decision-source-slot0.json").exists():
        source = json.loads((output / "173-island-dock-decision-source-slot0.json").read_text(encoding="utf-8"))
    else:
        if state["map"] != "map_050_忘忧岛.map" or state["variables"].get("Event") != "350":
            raise AutomationError("Island departure requires its ordinary farewell-morning checkpoint")
        transition(client, resource, "map_052_码头.map", 3)
        client.save_or_load(0)
        source = checkpoint(client, output, "173-island-dock-decision-source-slot0")
    if any(item["file"] in ("goods-e03-丝绸手帕.ini", "goods-e04-一块绸布.ini") for item in source["inventory"]):
        raise AutomationError("Native return to Yang Yingfeng retained the basin cloth items")
    checkpoint(client, output, "174-dock-boat-jump-disabled")
    write_json(output / "dock-boat-probe.json", dict(sourceFile="173-island-dock-decision-source-slot0.json", sourcePath="script/map/map_052_码头/窦昊对话.txt", nativeCanJump=source["player"]["canJump"], choicePassed=False, cheatAssisted=False))
    for option in (1, 0):
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        state = transition(client, resource, "map_029_码头.map", 2, choice=("script/map/map_052_码头/trap02.txt:16", option), output=output)
        quantity = lambda value: sum(item["quantity"] for item in value["inventory"] if item["file"] == "book04-无忧剑法.ini")
        if state["variables"].get("Event") != "365" or state["variables"].get("SenseVal") != str(int(source["variables"]["SenseVal"]) + (20 if option == 0 else 0)) or state["variables"].get("EvilVal") != str(int(source["variables"]["EvilVal"]) + (20 if option == 1 else 0)) or quantity(state) != quantity(source) + 1:
            raise AutomationError("Island farewell decision event/sense/evil/book mismatch")
        client.save_or_load(1 + option)
        checkpoint(client, output, f"175-island-departure-{option}")
    client.save_or_load(6)
    checkpoint(client, output, "176-island-departure-continuation-slot6")


def zixuan_search(client, output, resource):
    state = idle(client)
    if state["variables"].get("Event") not in ("365", "368", "370", "375", "380", "382"):
        raise AutomationError("Zixuan search requires the ordinary post-island story checkpoint")
    if state["map"] == "map_029_码头.map":
        client.save_or_load(0)
        checkpoint(client, output, "177-zixuan-search-source-slot0")
    for origin, destination, trap in (("map_029_码头.map", "map_028_连接地图.map", 1), ("map_028_连接地图.map", "map_027_连接地图.map", 3), ("map_027_连接地图.map", "map_012_惠安镇.map", 2), ("map_012_惠安镇.map", "map_014_连接地图.map", 2)):
        if idle(client)["map"] == origin:
            transition(client, resource, destination, trap, running=True)
    state = idle(client)
    if state["map"] == "map_014_连接地图.map":
        if state["variables"].get("Event") == "365":
            state = transition(client, resource, state["map"], 4)
        if state["variables"].get("Event") == "368":
            checkpoint(client, output, "178-mu-tianlan-rescue-battle", variables=("NpcCount",))
            fight_hostiles(client, output, resource)
            client.wait_until(lambda value: value.get("variables", {}).get("Event") == "370", timeout=30, variables=VARIABLES, description="completed native rescue death script")
            idle(client)
        state = checkpoint(client, output, "179-mu-tianlan-rescued", variables=("NpcCount",))
        if state["variables"].get("Event") != "370" or state["variables"].get("NpcCount") != "0":
            raise AutomationError("Mu Tianlan rescue did not finish its native enemy count")
        client.save_or_load(1)
    for origin, destination, trap in (("map_014_连接地图.map", "map_017_连接地图.map", 3), ("map_017_连接地图.map", "map_023_连接地图.map", 3), ("map_023_连接地图.map", "map_024_倚天山.map", 1), ("map_024_倚天山.map", "map_021_油菜花地.map", 1), ("map_021_油菜花地.map", "map_022_清平乡.map", 2)):
        if idle(client)["map"] == origin:
            transition(client, resource, destination, trap, running=True)
    state = idle(client)
    if state["map"] == "map_022_清平乡.map":
        if state["variables"].get("Event") == "370":
            state = transition(client, resource, state["map"], 7)
        if state["variables"].get("Event") != "375":
            raise AutomationError("Zixuan's empty home did not reach Event 375")
        client.save_or_load(2)
        checkpoint(client, output, "180-zixuan-empty-home-slot2")
        state = interact_named(client, output, resource, "阿婉")
        if state["map"] != "map_015_藏剑山庄.map" or state["variables"].get("Event") != "380":
            raise AutomationError("A Wan's clue did not enter the native night infiltration")
        client.save_or_load(3)
        checkpoint(client, output, "181-cangjian-night-arrival-slot3")
    state = idle(client)
    if state["map"] != "map_015_藏剑山庄.map":
        raise AutomationError("Night infiltration did not reach Cangjian Villa")
    if state["variables"].get("Event") == "380":
        state = transition(client, resource, state["map"], 11)
    if state["variables"].get("Event") != "382":
        raise AutomationError("Native night infiltration did not finish its wall jump")
    client.save_or_load(4)
    checkpoint(client, output, "182-cangjian-overhear-source-slot4")
    for trap in (6, 8):
        load_checkpoint(client, 4)
        client.act("SetAutoDialogue", enabled=True)
        state = transition(client, resource, "map_027_连接地图.map", trap)
        if state["variables"].get("Event") != "390":
            raise AutomationError("Cangjian overhearing did not reach Event 390")
        checkpoint(client, output, f"183-cangjian-overhear-{trap}")
        path = "script/map/map_015_藏剑山庄/" + ("事件39_1.txt" if trap == 6 else "事件39.txt")
        start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
        records = completed_script(output, start)
        if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest() or not any(record.get("variableName") == "Event" and record.get("afterValue") == "390" for record in records):
            raise AutomationError("Cangjian overhearing lacks its exact completed native source")
        write_json(output / f"cangjian-overhear-{trap}-proof.json", dict(records=records, afterFile=f"183-cangjian-overhear-{trap}.json", sourceSlot=4, cheatAssisted=False))
    client.save_or_load(6)
    checkpoint(client, output, "184-yue-meier-rescue-source-slot6")


def yue_meier_rescue(client, output, resource):
    state = idle(client)
    purchase_resume = state["map"] == "map_012_惠安镇.map" and state["variables"].get("Event") == "402" and (output / "186-yue-meier-medicine-0.json").exists()
    if not purchase_resume and (state["map"] != "map_027_连接地图.map" or state["variables"].get("Event") not in ("390", "392")):
        raise AutomationError("Yue Meier rescue requires its ordinary post-overhearing checkpoint")
    if state["variables"].get("Event") == "390":
        state = transition(client, resource, state["map"], 3)
    if not purchase_resume and state["variables"].get("Event") != "392":
        raise AutomationError("Native Yue Meier ambush did not reach Event 392")
    if not (output / "185-yue-meier-ambush-source-slot0.json").exists():
        client.save_or_load(0)
        source = checkpoint(client, output, "185-yue-meier-ambush-source-slot0", variables=("NpcCount",))
    else:
        source = json.loads((output / "185-yue-meier-ambush-source-slot0.json").read_text(encoding="utf-8"))
    site = "script/map/map_028_连接地图/事件41.txt:14"
    for option in (1, 0):
        completed = output / f"190-yue-meier-left-{option}.json"
        if completed.exists() and json.loads(completed.read_text(encoding="utf-8"))["variables"].get("Event") == "408":
            continue
        if not (purchase_resume and option == 0):
            load_checkpoint(client, 0)
            client.act("SetAutoDialogue", enabled=True)
            try:
                fight_hostiles(client, output, resource, choice=(site, option))
            except AutomationError as error:
                if str(error) != "StartCombat: skill_resources_unavailable":
                    raise
                checkpoint(client, output, f"185-yue-meier-mana-exhausted-{option}")
                fight_hostiles(client, output, resource, use_magic=False, choice=(site, option), single_target=True)
            state = idle(client, choice=(site, option), output=output, resource=resource)
            if state["map"] != "map_028_连接地图.map" or state["variables"].get("Event") != ("408" if option else "402") or state["variables"].get("SenseVal") != str(int(source["variables"]["SenseVal"]) + (40 if option == 0 else 0)):
                raise AutomationError("Yue Meier medicine decision event/sense mismatch")
            client.save_or_load(1 + option)
            checkpoint(client, output, f"186-yue-meier-medicine-{option}", variables=("NpcCount",))
        if option == 0:
            if idle(client)["map"] == "map_028_连接地图.map":
                transition(client, resource, "map_027_连接地图.map", 3, running=True)
                transition(client, resource, "map_012_惠安镇.map", 2, running=True)
            before = checkpoint(client, output, "187-yue-meier-medicine-before-purchase")
            state = interact_named(client, output, resource, "药店老板")
            if before["player"]["money"] < 1000:
                if state["variables"].get("Event") != "402" or state["player"]["money"] != before["player"]["money"] or state["inventory"] != before["inventory"]:
                    raise AutomationError("Insufficient medicine money changed the native request")
                checkpoint(client, output, "187-yue-meier-medicine-insufficient")
                state = interact_named(client, output, resource, "宝箱", position=(52, 28))
                if state["player"]["money"] < 1000:
                    raise AutomationError("Normal treasure pickup did not fund Yue Meier's medicine")
                before = checkpoint(client, output, "187-yue-meier-medicine-funded")
                state = interact_named(client, output, resource, "药店老板")
            if state["variables"].get("Event") != "404" or state["player"]["money"] != before["player"]["money"] - 1000 or not any(item["file"] == "goods-e13-金创药.ini" and item["quantity"] == 1 for item in state["inventory"]):
                raise AutomationError("Native medicine purchase did not charge 1000 and deliver one item")
            client.save_or_load(3)
            checkpoint(client, output, "188-yue-meier-medicine-purchased-slot3")
            transition(client, resource, "map_027_连接地图.map", 3, running=True)
            transition(client, resource, "map_028_连接地图.map", 1, running=True)
            state = transition(client, resource, "map_028_连接地图.map", 4)
            if state["variables"].get("Event") != "408":
                raise AutomationError("Native medicine return did not reach Event 408")
            checkpoint(client, output, "189-yue-meier-medicine-return")
        client.save_or_load(4 + option)
        checkpoint(client, output, f"190-yue-meier-left-{option}")
    client.save_or_load(6)
    checkpoint(client, output, "191-return-to-forget-worry-source-slot6")


def forget_worry_return(client, output, resource):
    state = idle(client)
    if state["map"] != "map_028_连接地图.map" or state["variables"].get("Event") != "408":
        raise AutomationError("Forget Worry return requires the ordinary Yue Meier departure checkpoint")
    checkpoint(client, output, "192-forget-worry-return-source")
    transition(client, resource, "map_029_码头.map", 1, running=True)
    state = interact_named(client, output, resource, "渔夫窦昊")
    if state["map"] != "map_052_码头.map" or state["variables"].get("Event") != "412":
        raise AutomationError("Native return voyage did not reach Event 412")
    checkpoint(client, output, "193-forget-worry-return-voyage")
    transition(client, resource, "map_050_忘忧岛.map", 1, running=True)
    transition(client, resource, "map_051_海边.map", 1, running=True)
    state = interact_named(client, output, resource, "纳兰真")
    if state["variables"].get("Event") != "415":
        raise AutomationError("Native seashore reunion did not reach Event 415")
    client.save_or_load(0)
    checkpoint(client, output, "194-forget-worry-reunion-slot0", variables=("Talknlz050",))
    state = interact_named(client, output, resource, "纳兰真")
    checkpoint(client, output, "194-forget-worry-reunion-repeat", variables=("Talknlz050",))
    if state["variables"].get("Event") != "415":
        raise AutomationError("Seashore reunion repeat changed the native event")
    transition(client, resource, "map_050_忘忧岛.map", 1, running=True)
    state = transition(client, resource, "map_051_海边.map", 9)
    if state["variables"].get("Event") != "425":
        raise AutomationError("Native father meeting did not reach the heart-demon invitation")
    client.save_or_load(1)
    checkpoint(client, output, "195-heart-demon-invitation-slot1")
    for destination, trap in (("map_050_忘忧岛.map", 1), ("map_057_连接地图.map", 4), ("map_058_禁地.map", 2)):
        transition(client, resource, destination, trap, running=True)
    checkpoint(client, output, "196-heart-demon-entry-before")
    state = interact_named(client, output, resource, "纳兰潜凛")
    if state["map"] != "map_016_剑气峰.map" or state["variables"].get("Event") != "430" or not any(item["file"] == "player-magic-逆转心经.ini" for item in state["magic"]):
        raise AutomationError("Native heart-demon entry did not grant its skill and enter Event 430")
    client.save_or_load(6)
    checkpoint(client, output, "197-heart-demon-first-source-slot6")


def heart_demon_first(client, output, resource):
    state = idle(client)
    if state["map"] != "map_016_剑气峰.map" or state["variables"].get("Event") != "430":
        raise AutomationError("First heart-demon trial requires its ordinary illusion entry checkpoint")
    client.save_or_load(0)
    checkpoint(client, output, "198-heart-demon-zhuo-source-slot0")
    if not state["player"]["canFight"]:
        if state["player"]["mana"] < state["player"]["manaMax"]:
            client.act("ToggleSit", generation=state["generation"])
            client.wait_until(lambda value: value.get("player", {}).get("mana", 0) >= value.get("player", {}).get("manaMax", 1), timeout=90, description="ordinary pre-trial mana recovery")
            client.act("ToggleSit", generation=client.observe()["generation"])
        transition(client, resource, "map_016_剑气峰.map", 5)
    checkpoint(client, output, "199-heart-demon-zhuo-battle")
    fight_named(client, output, resource, "卓非凡", use_magic=True)
    client.wait_until(lambda value: value.get("variables", {}).get("Event") == "440", timeout=45, variables=VARIABLES, description="completed native first heart-demon trial")
    state = idle(client)
    if state["map"] != "map_059_禁地一层.map":
        raise AutomationError("First heart-demon trial did not return to Forbidden Area floor one")
    checkpoint(client, output, "200-heart-demon-zhuo-cleared")
    path = "script/map/map_016_剑气峰/假卓非凡死亡脚本.txt"
    start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
    records = completed_script(output, start)
    if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest() or not any(record.get("variableName") == "Event" and record.get("afterValue") == "440" for record in records):
        raise AutomationError("First heart-demon trial lacks its exact completed native death source")
    write_json(output / "heart-demon-zhuo-proof.json", dict(records=records, afterFile="200-heart-demon-zhuo-cleared.json", cheatAssisted=False))
    client.save_or_load(6)
    checkpoint(client, output, "201-heart-demon-second-source-slot6")


def heart_demon_second(client, output, resource):
    source = idle(client)
    if source["map"] != "map_059_禁地一层.map" or source["variables"].get("Event") != "440":
        raise AutomationError("Second heart-demon trial requires the ordinary first-trial completion")
    client.save_or_load(0)
    checkpoint(client, output, "202-heart-demon-zixuan-source-slot0")
    for option in (1, 0):
        if option == 0:
            load_checkpoint(client, 0)
            client.act("SetAutoDialogue", enabled=True)
        recover(client, output, resource)
        transition(client, resource, "map_019_寒波谷.map", 2)
        transition(client, resource, "map_019_寒波谷.map", 8)
        state = checkpoint(client, output, f"203-heart-demon-zixuan-battle-{option}", variables=("NpcCount",))
        if state["variables"].get("NpcCount") != "12":
            raise AutomationError("Second trial did not install its twelve ordinary enemy death scripts")
        choice = ("script/map/map_019_寒波谷/心魔阵战斗死亡.txt:20", option)
        try:
            fight_hostiles(client, output, resource, choice=choice)
        except AutomationError as error:
            if str(error) != "StartCombat: skill_resources_unavailable":
                raise
            checkpoint(client, output, f"203-heart-demon-zixuan-mana-depleted-{option}")
            fight_hostiles(client, output, resource, use_magic=False, single_target=True, choice=choice)
        client.wait_until(lambda value: bool(value.get("choices")) or value.get("variables", {}).get("Event") == "450", timeout=45, variables=VARIABLES, description="native second-trial final enemy death")
        state = idle(client, choice=choice, output=output, resource=resource)
        state = checkpoint(client, output, f"204-heart-demon-zixuan-choice-{option}", variables=("NpcCount", "Sel"))
        expected = int(source["variables"]["SenseVal"]) + (20 if option == 1 else -40)
        if (state["map"] != "map_060_禁地二层.map" or state["variables"].get("Event") != "450"
                or state["variables"].get("SenseVal") != str(expected)
                or state["variables"].get("NpcCount") != "0"):
            raise AutomationError("Second heart-demon trial choice or completed enemy count mismatch")
        client.save_or_load(1 if option == 1 else 2)
    load_checkpoint(client, 1)
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(6)
    checkpoint(client, output, "205-heart-demon-third-source-slot6")


def heart_demon_third(client, output, resource):
    source = idle(client)
    resume_kill = (source["map"] == "map_050_忘忧岛.map" and source["variables"].get("Event") == "454"
                   and (output / "206-heart-demon-nalan-source-slot0.json").exists()
                   and (output / "209-heart-demon-nalan-cleared-1.json").exists())
    if resume_kill:
        source = json.loads((output / "206-heart-demon-nalan-source-slot0.json").read_text(encoding="utf-8"))
    elif source["map"] != "map_060_禁地二层.map" or source["variables"].get("Event") != "450":
        raise AutomationError("Third heart-demon trial requires the ordinary second-trial completion")
    else:
        client.save_or_load(0)
        checkpoint(client, output, "206-heart-demon-nalan-source-slot0")
    for option in (1, 0):
        if resume_kill and option == 1:
            continue
        if option == 0 and not resume_kill:
            load_checkpoint(client, 0)
            client.act("SetAutoDialogue", enabled=True)
        if not resume_kill:
            recover(client, output, resource)
            state = transition(client, resource, "map_050_忘忧岛.map", 2)
            if state["variables"].get("Event") != "452":
                raise AutomationError("Third trial did not activate its ordinary Nalan Qianlin fight")
            choice = ("script/map/map_050_忘忧岛/纳兰潜凛死亡.txt:14", option)
            fight_named(client, output, resource, "纳兰潜凛", use_magic=True, choice=choice)
            client.wait_until(lambda value: bool(value.get("choices")) or value.get("variables", {}).get("Event") in ("454", "455"), timeout=45, variables=VARIABLES, description="native third-trial boss death")
            state = idle(client, choice=choice, output=output, resource=resource)
            checkpoint(client, output, f"207-heart-demon-nalan-choice-{option}", variables=("Sel", "TalkNa", "NaDead"))
        else:
            state = client.observe((*VARIABLES, "NaDead"))
        expected = int(source["variables"]["SenseVal"]) + (20 if option == 1 else -40)
        if state["variables"].get("SenseVal") != str(expected) or state["variables"].get("Event") != ("455" if option == 1 else "454"):
            raise AutomationError("Third heart-demon trial choice outcome mismatch")
        if option == 0:
            for index in range(int(state["variables"].get("NaDead") or 0), 10):
                state = idle(client)
                if not any(target["name"] == "纳兰真" and npc_attackable(target) for target in state["targets"]):
                    recover(client, output, resource)
                    state = idle(client)
                    position = state["player"]["position"]
                    targets = [target for target in state["targets"] if target["name"] == "纳兰真"
                               and target.get("interactive") and target.get("action") not in (11, 255)]
                    target = min(targets, key=lambda item: abs(item["position"]["x"] - position["x"]) * 2 + abs(item["position"]["y"] - position["y"]))
                    # The platform approach crosses trap 18; an ordinary move finishes
                    # that no-op story trigger before queuing the NPC interaction.
                    if target["position"] == dict(x=62, y=84):
                        client.move(61, 86)
                        checkpoint(client, output, "208-heart-demon-platform-approach")
                    state = interact_named(client, output, resource, "纳兰真", position=(target["position"]["x"], target["position"]["y"]))
                if not any(target["name"] == "纳兰真" and npc_attackable(target) for target in state["targets"]):
                    raise AutomationError("Illusion dialogue did not activate its ordinary opponent")
                fight_named(client, output, resource, "纳兰真", single_target=True)
                client.wait_until(lambda value: int(value.get("variables", {}).get("NaDead") or 0) == index + 1, timeout=30, variables=(*VARIABLES, "NaDead"), description="native Nalan illusion death counter")
                checkpoint(client, output, f"208-heart-demon-nalan-illusion-{index + 1}", variables=("TalkNa", "NaDead"))
            recover(client, output, resource)
            interact_named(client, output, resource, "纳兰真", position=(30, 59))
            fight_named(client, output, resource, "纳兰真", single_target=True)
            client.wait_until(lambda value: value.get("variables", {}).get("Event") == "455", timeout=30, variables=VARIABLES, description="native eleventh Nalan illusion completion")
        state = checkpoint(client, output, f"209-heart-demon-nalan-cleared-{option}", variables=("TalkNa", "NaDead"))
        if state["map"] != "map_061_禁地三层.map" or state["variables"].get("Event") != "455" or state["variables"].get("SenseVal") != str(expected):
            raise AutomationError("Third heart-demon trial did not reach its ordinary blade chamber")
        client.save_or_load(1 if option == 1 else 2)
    load_checkpoint(client, 1)
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(6)
    checkpoint(client, output, "210-heart-demon-blade-source-slot6")


def heart_demon_blade(client, output, resource):
    source = idle(client)
    if source["map"] != "map_061_禁地三层.map" or source["variables"].get("Event") != "455":
        raise AutomationError("Blade decision requires the ordinary completed heart-demon chamber")
    client.save_or_load(0)
    checkpoint(client, output, "211-heart-demon-blade-source-slot0", variables=("Sel", "NaDead"))
    blade = "goods-w11-悲魔之刃.ini"
    initial = sum(item["quantity"] for item in source["inventory"] if item["file"] == blade)
    for option in (0, 1):
        if option == 1:
            load_checkpoint(client, 0)
            client.act("SetAutoDialogue", enabled=True)
        state = interact_named(client, output, resource, "宝箱", position=(5, 30),
                               choice=("script/map/map_061_禁地三层/得到悲魔刀.txt:14", option))
        expected_evil = int(source["variables"]["EvilVal"]) - (20 if option == 1 else 0)
        if (state["variables"].get("Event") != "460" or state["variables"].get("EvilVal") != str(expected_evil)
                or sum(item["quantity"] for item in state["inventory"] if item["file"] == blade) != initial + 1):
            raise AutomationError("Native blade decision did not grant its single blade or correct morality")
        checkpoint(client, output, f"212-heart-demon-blade-decision-{option}", variables=("Sel", "SelectBlade"))
        transition(client, resource, "map_060_禁地二层.map", 1)
        transition(client, resource, "map_059_禁地一层.map", 1)
        state = transition(client, resource, "map_030_悲魔山庄.map", 1, output=output)
        state = checkpoint(client, output, f"213-heart-demon-blade-left-{option}", variables=("Sel", "SelectBlade"))
        if (state["variables"].get("Event") != "475" or state["variables"].get("EvilVal") != str(expected_evil)
                or sum(item["quantity"] for item in state["inventory"] if item["file"] == blade) != initial + option
                or state["variables"].get("SenseVal") != source["variables"].get("SenseVal")):
            raise AutomationError("Native return/keep blade departure outcome mismatch")
        path = "script/map/map_058_禁地/事件47_1.txt"
        start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
        records = completed_script(output, start)
        if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Post-trial departure used a different blade source")
        write_json(output / f"blade-departure-{option}-proof.json", dict(records=records, afterFile=f"213-heart-demon-blade-left-{option}.json", cheatAssisted=False))
        client.save_or_load(1 if option == 0 else 2)
    client.save_or_load(6)
    checkpoint(client, output, "214-cangjian-invitation-source-slot6")


def cangjian_final_duels(client, output, resource):
    state = idle(client)
    event = state["variables"].get("Event")
    if event not in ("475", "485", "490", "492", "494", "498", "500"):
        raise AutomationError("Final Cangjian challenge requires its ordinary invitation or duel checkpoint")
    if event == "475":
        if not (output / "215-cangjian-invitation-source-slot0.json").exists():
            client.save_or_load(0)
            checkpoint(client, output, "215-cangjian-invitation-source-slot0")
        for origin, destination, trap in (
                ("map_030_悲魔山庄.map", "map_028_连接地图.map", 5),
                ("map_028_连接地图.map", "map_027_连接地图.map", 3),
                ("map_027_连接地图.map", "map_012_惠安镇.map", 2),
                ("map_012_惠安镇.map", "map_014_连接地图.map", 2),
                ("map_014_连接地图.map", "map_015_藏剑山庄.map", 2)):
            if idle(client)["map"] == origin:
                transition(client, resource, destination, trap, running=True)
        state = transition(client, resource, "map_030_悲魔山庄.map", 9, output=output)
        if state["variables"].get("Event") != "485":
            raise AutomationError("Native challenge invitation did not return to Beimo Manor")
        client.save_or_load(1)
        checkpoint(client, output, "216-cangjian-invitation-delivered-slot1")
    if idle(client)["variables"].get("Event") == "485":
        client.move(44, 143)  # Finish the approach across the no-op trap 9 first.
        state = interact_named(client, output, resource, "纳兰真")
        if state["variables"].get("Event") != "490":
            raise AutomationError("Native pre-duel overnight did not complete")
        client.save_or_load(2)
        checkpoint(client, output, "216-cangjian-overnight-slot2")
    if idle(client)["variables"].get("Event") == "490":
        transition(client, resource, "map_015_藏剑山庄.map", 9)
    if idle(client)["variables"].get("Event") == "492":
        if idle(client)["map"] == "map_015_藏剑山庄.map":
            transition(client, resource, "map_016_剑气峰.map", 2)
        state = transition(client, resource, "map_016_剑气峰.map", 6)
        if state["variables"].get("Event") != "494":
            raise AutomationError("Native public challenge did not activate Zhuo Feifan")
    if idle(client)["variables"].get("Event") == "494":
        state = idle(client)
        blade = next((item for item in state["inventory"] if item["file"] == "goods-w11-悲魔之刃.ini" and item["slot"] < state["layout"]["equipmentBegin"]), None)
        if blade:
            client.equip(blade["slot"])
        client.save_or_load(3)
        checkpoint(client, output, "217-cangjian-final-zhuo-source-slot3")
        fight_named(client, output, resource, "卓非凡", use_magic=True)
        client.wait_until(lambda value: value.get("variables", {}).get("Event") == "498", timeout=90, variables=VARIABLES, description="native Zhuo death and Tianxing challenge")
    if idle(client)["variables"].get("Event") == "498":
        client.save_or_load(4)
        checkpoint(client, output, "218-cangjian-tianxing-source-slot4")
        fight_named(client, output, resource, "天星道长", single_target=True)
        client.wait_until(lambda value: value.get("variables", {}).get("Event") == "500", timeout=90, variables=VARIABLES, description="native Tianxing duel completion")
    checkpoint(client, output, "219-cangjian-final-duels-cleared")
    client.save_or_load(6)
    checkpoint(client, output, "220-cangjian-final-return-source-slot6")


def hanbo_reunion(client, output, resource):
    state = idle(client)
    source_file = output / "221-hanbo-reunion-source-slot0.json"
    if state["map"] in ("map_016_剑气峰.map", "map_030_悲魔山庄.map") and state["variables"].get("Event") == "500":
        if state["map"] == "map_016_剑气峰.map":
            for destination, trap in (("map_015_藏剑山庄.map", 1), ("map_014_连接地图.map", 1),
                                      ("map_012_惠安镇.map", 1), ("map_027_连接地图.map", 3),
                                      ("map_028_连接地图.map", 1), ("map_030_悲魔山庄.map", 2)):
                transition(client, resource, destination, trap, running=True)
        state = transition(client, resource, "map_019_寒波谷.map", 7, output=output)
        if state["map"] != "map_019_寒波谷.map" or state["variables"].get("Event") != "510":
            raise AutomationError("Native post-duel letter did not lead to the Hanbo reunion")
        client.save_or_load(0)
        source = checkpoint(client, output, "221-hanbo-reunion-source-slot0")
    elif state["map"] == "map_019_寒波谷.map" and state["variables"].get("Event") == "510" and source_file.is_file():
        source = json.loads(source_file.read_text(encoding="utf-8"))
    else:
        raise AutomationError("Hanbo reunion requires the ordinary two-duel victory or its recorded reunion save")
    for option in (1, 0):
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        state = transition(client, resource, "map_018_连接地图.map", 11, output=output,
                           choice=("script/map/map_019_寒波谷/事件52.txt:11", option))
        expected = int(source["variables"]["SenseVal"]) + (20 if option == 1 else -20)
        if state["variables"].get("Event") != "520" or state["variables"].get("SenseVal") != str(expected):
            raise AutomationError("Hanbo reunion answer did not preserve its native affection outcome")
        client.save_or_load(1 if option == 1 else 2)
        checkpoint(client, output, f"222-hanbo-reunion-left-{option}")
        for destination, trap in (("map_017_连接地图.map", 1), ("map_014_连接地图.map", 1),
                                  ("map_012_惠安镇.map", 1), ("map_027_连接地图.map", 3)):
            transition(client, resource, destination, trap, running=True)
        transition(client, resource, "map_027_连接地图.map", 4)
        checkpoint(client, output, f"222-hanbo-return-ambush-{option}", variables=("NpcCount",))
        try:
            fight_hostiles(client, output, resource)
        except AutomationError as error:
            if "skill_resources_unavailable" not in str(error):
                raise
            fight_hostiles(client, output, resource, use_magic=False)
        state = checkpoint(client, output, f"222-hanbo-return-ambush-cleared-{option}", variables=("NpcCount",))
        if state["variables"].get("NpcCount") != "0":
            raise AutomationError("Native returning assassin ambush did not release its exits")
        transition(client, resource, "map_028_连接地图.map", 1, running=True)
        transition(client, resource, "map_030_悲魔山庄.map", 2, running=True)
        state = transition(client, resource, "map_030_悲魔山庄.map", 8)
        if state["variables"].get("Event") != "525":
            raise AutomationError("Native night manor arrival did not install Nalan Zhen's confrontation")
        state = transition(client, resource, "map_030_悲魔山庄.map", 17)
        if state["variables"].get("Event") != "530" or state["variables"].get("SenseVal") != str(expected):
            raise AutomationError("Native confrontation did not reach the next morning")
        checkpoint(client, output, f"223-hanbo-reunion-next-morning-{option}", variables=("SelectVal",))
        path = "script/map/map_030_悲魔山庄/事件53_2.txt"
        start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
        records = completed_script(output, start)
        if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Night confrontation used a different native source")
        write_json(output / f"hanbo-confrontation-{option}-proof.json", dict(records=records, afterFile=f"223-hanbo-reunion-next-morning-{option}.json", cheatAssisted=False))
        client.save_or_load(4 if option == 1 else 5)
    load_checkpoint(client, 4)
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(6)
    checkpoint(client, output, "224-meng-zhiqiu-challenge-source-slot6")


def tianshan_traps(client, output, resource):
    state = idle(client)
    source_file = output / "225-tianshan-traps-source-slot0.json"
    if state["map"] == "map_030_悲魔山庄.map" and state["variables"].get("Event") == "530":
        for destination, trap in (("map_065_天山古道.map", 1), ("map_031_连接地图.map", 1),
                                  ("map_032_天山.map", 1)):
            transition(client, resource, destination, trap, running=True)
        client.save_or_load(0)
        source = checkpoint(client, output, "225-tianshan-traps-source-slot0")
    elif state["map"] == "map_032_天山.map" and state["variables"].get("Event") == "530" and source_file.is_file():
        source = json.loads(source_file.read_text(encoding="utf-8"))
    else:
        raise AutomationError("Tianshan traps require the native next-morning manor save")
    for initial in (0, 1):
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        state = transition(client, resource, "map_032_天山.map", 8, output=output,
                           choice=("script/map/map_032_天山/事件54_ab.txt:26", initial))
        expected = int(source["variables"]["EvilVal"]) + (20 if initial else -20)
        if state["variables"].get("Event") != "532" or state["variables"].get("EvilVal") != str(expected):
            raise AutomationError("Initial animal-trap choice did not apply its native outcome")
        checkpoint(client, output, f"226-tianshan-initial-answer-{initial}")
        if initial == 0:
            client.save_or_load(1)
            state = interact_named(client, output, resource, "捕兽夹",
                                   choice=("script/map/map_032_天山/事件54_ab.txt:6", 0))
            if state["variables"].get("EvilVal") != str(expected) or state["variables"].get("Event") != "532":
                raise AutomationError("Leaving the trap on a second visit changed the native state")
            checkpoint(client, output, "227-tianshan-revisit-ignore")
            load_checkpoint(client, 1)
            client.act("SetAutoDialogue", enabled=True)
            state = interact_named(client, output, resource, "捕兽夹",
                                   choice=("script/map/map_032_天山/事件54_ab.txt:6", 1))
            expected += 40
            if state["variables"].get("EvilVal") != str(expected):
                raise AutomationError("Revisited trap collection did not apply both native +20 writes")
        client.save_or_load(2 if initial == 0 else 3)
        checkpoint(client, output, f"228-tianshan-trap-fight-source-{initial}")
        fight_hostiles(client, output, resource, use_magic=False, single_target=True)
        state = idle(client)
        if state["variables"].get("Event") != "534":
            raise AutomationError("Ordinary Qiangwei victory did not start her escort")
        checkpoint(client, output, f"229-tianshan-escort-{initial}", variables=("XiaoHuaSub",))
        # The two wrong turns are real escort scenes, reached by ordinary exits.
        transition(client, resource, "map_031_连接地图.map", 2)
        state = checkpoint(client, output, f"230-tianshan-escort-wrong-turn-{initial}", variables=("XiaoHuaSub",))
        if state["variables"].get("XiaoHuaSub") != "20":
            raise AutomationError("Escort's southern wrong turn did not complete")
        transition(client, resource, "map_032_天山.map", 1)
        state = transition(client, resource, "map_034_天池.map", 3)
        state = checkpoint(client, output, f"231-tianshan-escort-lake-{initial}", variables=("XiaoHuaSub",))
        if state["variables"].get("XiaoHuaSub") != "30":
            raise AutomationError("Escort's lake diversion did not complete")
        interact_named(client, output, resource, "蔷薇")
        state = transition(client, resource, "map_032_天山.map", 1)
        state = checkpoint(client, output, f"232-tianshan-escort-departed-{initial}", variables=("XiaoHuaSub",))
        if state["variables"].get("XiaoHuaSub") != "40":
            raise AutomationError("Qiangwei did not leave after the lake diversion")
        transition(client, resource, "map_033_落叶谷.map", 1)
        state = transition(client, resource, "map_033_落叶谷.map", 5)
        if state["variables"].get("Event") != "540":
            raise AutomationError("Native manor reception did not request Qiangwei's return")
        checkpoint(client, output, f"233-tianshan-manor-reception-{initial}")
        client.save_or_load(4 if initial == 0 else 5)
    load_checkpoint(client, 5)
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(6)
    checkpoint(client, output, "234-tianchi-hairpin-source-slot6")


def tianshan_delayed_trap(client, output, resource):
    state = idle(client)
    if state["map"] != "map_032_天山.map" or state["variables"].get("Event") != "532" or state["variables"].get("SelectVal") != "0":
        raise AutomationError("Delayed trap collection requires the ordinary leave-it-alone save")
    source = checkpoint(client, output, "241-tianshan-delayed-source")
    transition(client, resource, "map_033_落叶谷.map", 1)
    state = transition(client, resource, "map_033_落叶谷.map", 5)
    if state["variables"].get("Event") != "535":
        raise AutomationError("Leaving the animal trap did not reach the distinct manor request")
    checkpoint(client, output, "242-tianshan-delayed-manor-request")
    transition(client, resource, "map_032_天山.map", 1)
    state = interact_named(client, output, resource, "捕兽夹")
    if state["variables"].get("Event") != "537" or state["variables"].get("EvilVal") != source["variables"].get("EvilVal"):
        raise AutomationError("Collecting the trap after the manor request changed its native outcome")
    client.save_or_load(0)
    checkpoint(client, output, "243-tianshan-delayed-fight-source-slot0")
    fight_hostiles(client, output, resource, use_magic=False, single_target=True)
    state = idle(client)
    if state["variables"].get("Event") != "540":
        raise AutomationError("Delayed Qiangwei victory did not complete the alternative death branch")
    transition(client, resource, "map_034_天池.map", 3)
    client.save_or_load(6)
    checkpoint(client, output, "244-tianshan-delayed-hairpin-source-slot6")


def tianchi_walk_to(client, output, resource, point):
    while True:
        try:
            client.move(*point, running=True)
            return
        except AutomationError as error:
            if str(error) != "MoveTo: no_progress":
                raise
            value = idle(client)
            foes = [target for target in value["targets"] if target.get("hostile") and npc_attackable(target)]
            if not foes:
                raise
            position = value["player"]["position"]
            target = min(foes, key=lambda item: abs(item["position"]["x"] - position["x"]) * 2 + abs(item["position"]["y"] - position["y"]))
            fight_named(client, output, resource, target["name"], timeout=30, use_magic=True, target_id=target["id"])


def tianchi_crossings(layout):
    crossings = [((11, 125), ((23, 125),))]
    if layout == 2:
        crossings.append(((50, 27), ((50, 11),)))
    elif layout == 0:
        crossings.append(((15, 154), ((11, 161),)))
    return crossings


def tianchi_hairpin(client, output, resource, affection=True, roses=0):
    quantity = lambda value, file: sum(item["quantity"] for item in value["inventory"] if item["file"] == file)
    walk_to = lambda point: tianchi_walk_to(client, output, resource, point)
    state = idle(client)
    if state["variables"].get("Event") != "540" or state["map"] not in ("map_033_落叶谷.map", "map_034_天池.map"):
        raise AutomationError("Hairpin rescue requires the ordinary Qiangwei search request")
    if state["map"] == "map_033_落叶谷.map":
        transition(client, resource, "map_032_天山.map", 1)
        transition(client, resource, "map_034_天池.map", 3)
    state = idle(client)
    if state["player"]["position"]["x"] < 7 or state["player"]["position"] == dict(x=14, y=4):
        client.move(5, 24)
        client.act("JumpTo", generation=client.observe()["generation"], x=11, y=24, timeoutMs=30000)
        checkpoint(client, output, "235-hairpin-island-normal-jump")
    client.move(17, 24)
    recover(client, output, resource)
    client.save_or_load(0)
    source = checkpoint(client, output, "235-hairpin-request-source-slot0")
    seen = set()
    for attempt in range(1, 16):
        if attempt > 1:
            load_checkpoint(client, 0)
            client.act("SetAutoDialogue", enabled=True)
        state = interact_named(client, output, resource, "蔷薇")
        state = checkpoint(client, output, f"236-hairpin-dive-{attempt}", variables=("wangwei",))
        if state["map"] != "map_035_天池内部.map" or state["variables"].get("Event") != "542":
            raise AutomationError("Native hairpin dive did not start its timed search")
        if attempt == 1:
            client.save_or_load(4)
            checkpoint(client, output, "236-hairpin-timeout-source-slot4", variables=("wangwei",))
        layout = int(state["variables"]["wangwei"])
        expected_position = ((7, 161), (34, 15), (53, 11))[layout]
        if not any(target["name"] == "发钗" and target["position"] == dict(zip(("x", "y"), expected_position)) for target in state["targets"]):
            raise AutomationError("Native random hairpin did not appear at its source-defined position")
        if layout in seen:
            continue
        crossings = tianchi_crossings(layout)
        for approach, jumps in crossings:
            walk_to(approach)
            for landing in jumps:
                client.act("JumpTo", generation=client.observe()["generation"], x=landing[0], y=landing[1], timeoutMs=10000)
        walk_to(((8, 161), (34, 17), (52, 11))[layout])
        state = interact_named(client, output, resource, "发钗", position=expected_position)
        if state["variables"].get("Event") != "545" or quantity(state, "goods-e09-发钗.ini") != quantity(source, "goods-e09-发钗.ini") + 1:
            raise AutomationError("Normal hairpin pickup did not grant the plot item")
        checkpoint(client, output, f"237-hairpin-picked-layout-{layout}")
        for approach, jumps in reversed(crossings):
            walk_to(jumps[-1])
            for landing in reversed((approach,) + jumps[:-1]):
                if layout == 0 and landing == (15, 154):
                    landing = (17, 154)
                client.act("JumpTo", generation=client.observe()["generation"], x=landing[0], y=landing[1], timeoutMs=10000)
        state = transition(client, resource, "map_034_天池.map", 1)
        if state["variables"].get("Event") != "550" or quantity(state, "goods-e09-发钗.ini") != quantity(source, "goods-e09-发钗.ini"):
            raise AutomationError("Hairpin return did not consume the item and rescue Qiangwei")
        checkpoint(client, output, f"238-hairpin-returned-layout-{layout}")
        client.save_or_load(layout + 1)
        seen.add(layout)
        if len(seen) == 3:
            break
    if seen != {0, 1, 2}:
        raise AutomationError(f"Native random hairpin layouts remain uncovered: {seen}")
    load_checkpoint(client, 1)
    client.act("SetAutoDialogue", enabled=True)
    base = int(source["variables"]["SenseVal"])
    for count in range(1, roses + 1):
        before = client.observe(variables=("FlowerSense",))
        flowers = [item for item in before["inventory"] if item["file"] == "goods-e21-玫瑰花.ini"]
        if not flowers or int(before["variables"].get("FlowerSense") or 0) >= 50:
            raise AutomationError("The normal Qiangwei gift source lacks flowers or has reached its affection cap")
        client.equip(flowers[0]["slot"])
        idle(client)
        state = checkpoint(client, output, f"239-qiangwei-rose-{count}", variables=("FlowerSense", "PartnerNo"))
        if quantity(state, "goods-e21-玫瑰花.ini") != quantity(before, "goods-e21-玫瑰花.ini") - 1 or state["variables"].get("SenseVal") != str(base + count * 5) or state["variables"].get("PartnerNo") != "4" or state["variables"].get("FlowerSense") != str(int(before["variables"].get("FlowerSense") or 0) + 5):
            raise AutomationError("The native Qiangwei rose did not consume one flower and add five affection")
        path = "script/goods/使用玫瑰花.txt"
        start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
        if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("The Qiangwei gift used a different native source")
        write_json(output / f"qiangwei-rose-{count}-proof.json", dict(records=completed_script(output, start), cheatAssisted=False))
    base += roses * 5
    for count in (range(1, 6) if affection else ()):
        state = interact_named(client, output, resource, "蔷薇")
        state = checkpoint(client, output, f"239-qiangwei-affection-{count}", variables=("QiangWeiSense",))
        if state["variables"].get("SenseVal") != str(base + min(count, 4) * 5) or state["variables"].get("QiangWeiSense") != str(min(count, 4) * 5):
            raise AutomationError("Qiangwei's ordinary conversations did not preserve their affection cap")
    transition(client, resource, "map_032_天山.map", 1)
    transition(client, resource, "map_033_落叶谷.map", 1)
    state = transition(client, resource, "map_033_落叶谷.map", 5)
    if state["variables"].get("Event") != "555":
        raise AutomationError("Returning Qiangwei did not install the native Meng Zhiqiu challenge")
    if not affection and state["variables"].get("SenseVal") != source["variables"].get("SenseVal"):
        raise AutomationError("The hairpin route changed affection without an optional conversation")
    client.save_or_load(6)
    checkpoint(client, output, "240-meng-challenge-source-slot6")


def tianchi_revisit(client, output, resource):
    state = idle(client)
    source_file = output / "271-tianchi-revisit-choice-source-slot0.json"
    if state["map"] == "map_033_落叶谷.map" and state["variables"].get("Event") == "560":
        transition(client, resource, "map_032_天山.map", 1)
        transition(client, resource, "map_034_天池.map", 3)
        recover(client, output, resource)
        client.move(5, 24)
        client.act("JumpTo", generation=client.observe()["generation"], x=11, y=24, timeoutMs=30000)
        client.save_or_load(0)
        source = checkpoint(client, output, "271-tianchi-revisit-choice-source-slot0")
    elif state["map"] == "map_034_天池.map" and source_file.is_file():
        source = json.loads(source_file.read_text(encoding="utf-8"))
    else:
        raise AutomationError("Tianchi revisit requires the ordinary recovered Meng challenge save")
    state = transition(client, resource, "map_034_天池.map", 2, output=output,
                       choice=("script/map/map_034_天池/trap02.txt:7", 1))
    if state["variables"].get("Event") != source["variables"]["Event"] or state["inventory"] != source["inventory"]:
        raise AutomationError("Refusing the ordinary dive changed the plot or goods")
    checkpoint(client, output, "272-tianchi-revisit-refused")
    seen = set()
    for attempt in range(1, 16):
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        state = transition(client, resource, "map_035_天池内部.map", 2, output=output,
                           choice=("script/map/map_034_天池/trap02.txt:7", 0))
        state = checkpoint(client, output, f"273-tianchi-revisit-dive-{attempt}", variables=("wangwei",))
        layout = int(state["variables"]["wangwei"])
        if layout in seen:
            continue
        name, position = (("防具", (7, 161)), ("药品", (34, 15)), ("武器", (53, 11)))[layout]
        crossings = tianchi_crossings(layout)
        for approach, jumps in crossings:
            tianchi_walk_to(client, output, resource, approach)
            for landing in jumps:
                client.act("JumpTo", generation=client.observe()["generation"], x=landing[0], y=landing[1], timeoutMs=10000)
        tianchi_walk_to(client, output, resource, ((8, 161), (34, 17), (52, 11))[layout])
        before = client.observe(VARIABLES)
        state = interact_named(client, output, resource, name, position=position)
        if sum(item["quantity"] for item in state["inventory"]) <= sum(item["quantity"] for item in before["inventory"]):
            raise AutomationError("Ordinary Tianchi treasure did not enter the inventory")
        checkpoint(client, output, f"274-tianchi-revisit-reward-layout-{layout}")
        for approach, jumps in reversed(crossings):
            tianchi_walk_to(client, output, resource, jumps[-1])
            for landing in reversed((approach,) + jumps[:-1]):
                if layout == 0 and landing == (15, 154):
                    landing = (17, 154)
                client.act("JumpTo", generation=client.observe()["generation"], x=landing[0], y=landing[1], timeoutMs=10000)
        state = transition(client, resource, "map_034_天池.map", 1)
        if any(state["variables"].get(key) != source["variables"].get(key) for key in ("Event", "SenseVal", "EvilVal")):
            raise AutomationError("Ordinary Tianchi treasure changed the continuing story route")
        checkpoint(client, output, f"275-tianchi-revisit-returned-layout-{layout}")
        client.save_or_load(layout + 1)
        seen.add(layout)
        if seen == {0, 1, 2}:
            break
    if seen != {0, 1, 2}:
        raise AutomationError(f"Native Tianchi treasure categories remain uncovered: {seen}")
    load_checkpoint(client, 1)
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(6)
    checkpoint(client, output, "276-tianchi-revisit-complete-slot6")


def tianchi_timeout(client, output, resource):
    state = idle(client)
    if state["map"] != "map_034_天池.map" or state["variables"].get("Event") != "540":
        raise AutomationError("Tianchi timeout requires the ordinary hairpin request save")
    source = checkpoint(client, output, "260-tianchi-timeout-source")
    offset = len(trace_records(output))
    interact_named(client, output, resource, "蔷薇")
    state = checkpoint(client, output, "261-tianchi-timer-started")
    if state["map"] != "map_035_天池内部.map" or state["variables"].get("Event") != "542":
        raise AutomationError("Tianchi timeout did not enter its native timed dive")
    videos = []
    deadline, next_report = time.monotonic() + 240, time.monotonic()
    while time.monotonic() < deadline:
        state = client.observe(VARIABLES)
        if state.get("video"):
            videos.append(state["video"])
            checkpoint(client, output, "262-tianchi-timeout-video")
            client.ui("Cancel")
        if state.get("scene") == "Title":
            break
        if time.monotonic() >= next_report:
            print("Waiting for native 180-second Tianchi timeout", flush=True)
            next_report = time.monotonic() + 20
        time.sleep(0.2)
    else:
        raise TimeoutError("Tianchi timeout did not return to Title")
    after = checkpoint(client, output, "263-tianchi-timeout-title")
    trace = trace_records(output)[offset:]
    paths = ("script/map/map_034_天池/事件55_1.txt", "script/common/主角死亡.txt")
    records = {}
    for path in paths:
        start = next(record for record in trace if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
        if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Tianchi timeout used a different native source")
        records[path] = completed_script(output, start)
    dive = records[paths[0]]
    timer_line = next(number for number, line in enumerate((resource / paths[0]).read_text(encoding="utf-8-sig").splitlines(), 1) if source_line(line).strip() == "opentimelimit(180);")
    timer_called = any(record.get("apiName") == "opentimelimit" and index > 0 and dive[index - 1].get("line") == timer_line for index, record in enumerate(dive))
    if "die.wmv" not in videos or not timer_called or not any(record.get("apiName") == "returntotitle" for record in records[paths[1]]):
        raise AutomationError("Tianchi timeout lacks native timer, movie, and Title evidence")
    write_json(output / "tianchi-timeout-proof.json", dict(source=source, after=after, seconds=180, observedVideos=videos, records=records, cheatAssisted=False, storyEnding=False))


def ending_junction(client, output, resource):
    state = idle(client)
    source_file = output / "264-ending-junction-source-slot0.json"
    if state["map"] != "map_030_悲魔山庄.map":
        raise AutomationError("Ending junction requires the ordinary post-farewell save")
    if state["variables"].get("Event") == "562":
        client.save_or_load(0)
        source = checkpoint(client, output, "264-ending-junction-source-slot0")
    elif state["variables"].get("Event") in ("565", "1800") and source_file.is_file():
        source = json.loads(source_file.read_text(encoding="utf-8"))
    else:
        raise AutomationError("Ending junction lacks its recorded ordinary source")
    sense, evil = (int(source["variables"][name]) for name in ("SenseVal", "EvilVal"))
    gate = "G01" if sense >= 1175 else "G02" if sense <= 1025 else "G03" if evil >= 860 else "G04"
    expected = "1800" if gate in ("G01", "G03") else "565"
    if state["variables"].get("Event") == "562":
        offset = len(trace_records(output))
        try:
            state = interact_named(client, output, resource, "纳兰真")
        except AutomationError as error:
            # This native dialogue deletes its own interaction target before it ends.
            started = any(record.get("eventType") == "script.start"
                          and record.get("virtualPath") == "script/map/map_030_悲魔山庄/纳兰真对话.txt"
                          for record in trace_records(output)[offset:])
            if str(error) != "Interact: target_unavailable" or not started:
                raise
            state = idle(client)
    if state["variables"].get("Event") != expected:
        raise AutomationError(f"Native {gate} ending junction did not follow its source conditions")
    paths = ["script/map/map_030_悲魔山庄/纳兰真对话.txt"]
    if expected == "1800":
        paths.append("script/map/map_030_悲魔山庄/结局二.txt")
    else:
        state = transition(client, resource, "map_030_悲魔山庄.map", 9)
        if state["variables"].get("Event") != "570":
            raise AutomationError("Ordinary ending-one letter did not complete")
        paths.append("script/map/map_030_悲魔山庄/trap09.txt")
    result = "2" if expected == "1800" else "1"
    if state["variables"].get("Result") != result:
        raise AutomationError("Ending junction did not assign its native Result")
    records = {}
    for path in paths:
        start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
        if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Ending junction used a different native source")
        records[path] = completed_script(output, start)
    client.save_or_load(6)
    after = checkpoint(client, output, "265-ending-route-source-slot6")
    write_json(output / "ending-junction-proof.json", dict(gate=gate, source=source, after=after, records=records, cheatAssisted=False, storyEnding=False))


def ending_junction_low_evil(client, output, resource, boundary=False):
    state = idle(client)
    source_file = output / "560-ending-low-evil-preparation-source-slot5.json"
    if not source_file.is_file():
        if state["map"] != "map_033_落叶谷.map" or state["variables"].get("Event") != "560" or state["variables"].get("Result") != "0":
            raise AutomationError("Low-evil gate preparation requires the ordinary pre-farewell Meng source")
        client.save_or_load(5)
        checkpoint(client, output, "560-ending-low-evil-preparation-source-slot5")
    source = json.loads(source_file.read_text(encoding="utf-8"))
    for origin, destination, trap in (("map_033_落叶谷.map", "map_032_天山.map", 1),
            ("map_032_天山.map", "map_034_天池.map", 3)):
        if state["map"] == origin:
            state = transition(client, resource, destination, trap, running=True)
    if state["map"] != "map_034_天池.map":
        raise AutomationError("Low-evil preparation has not reached Wang Wei's ordinary map")
    if state["player"]["position"]["x"] < 20:
        client.move(14, 4)
        client.act("JumpTo", generation=client.observe()["generation"], x=28, y=9, timeoutMs=30000)
    for _ in range(20):
        before = client.observe(VARIABLES)
        if int(before["variables"]["EvilVal"]) < 860:
            break
        for _ in range(60):
            state = interact_named(client, output, resource, "王炜")
            book = next((item for item in state["inventory"] if item["file"] == "book10-孤烟逐云.ini"), None)
            if book:
                break
        else:
            raise AutomationError("Wang Wei's ordinary random dialogue did not grant its book")
        client.equip(book["slot"])
        state = checkpoint(client, output, f"561-ending-low-evil-book-{time.time_ns()}")
        if (state["variables"].get("EvilVal") != str(int(before["variables"]["EvilVal"]) - 25)
                or state["variables"].get("Event") != before["variables"].get("Event")
                or any(state["variables"].get(key) != source["variables"].get(key) for key in ("Result", "SenseVal"))):
            raise AutomationError("Ordinary cloud-book use did not preserve the pre-letter story and affection")
    else:
        raise AutomationError("Low-evil preparation exceeded its ordinary book limit")
    client.move(28, 9)
    client.act("JumpTo", generation=client.observe()["generation"], x=14, y=4, timeoutMs=30000)
    state = transition(client, resource, "map_032_天山.map", 1)
    if state["variables"].get("Event") == "560":
        state = transition(client, resource, "map_033_落叶谷.map", 1)
        state = transition(client, resource, "map_033_落叶谷.map", 9)
    if state["variables"].get("Event") != "562":
        raise AutomationError("Low-evil preparation did not complete the native farewell")
    if state["map"] == "map_033_落叶谷.map":
        transition(client, resource, "map_032_天山.map", 1)
    for destination, trap in (("map_031_连接地图.map", 2), ("map_065_天山古道.map", 2),
                              ("map_030_悲魔山庄.map", 2)):
        transition(client, resource, destination, trap, running=True)
    prepared = checkpoint(client, output, "562-ending-low-evil-prepared")
    records = []
    paths = ("script/map/map_034_天池/王炜对话.txt", "script/goods/book10-孤烟逐云.txt")
    starts = [item for item in trace_records(output) if item.get("eventType") == "script.start" and item.get("virtualPath") in paths]
    for start in starts:
        if start["contentSha256"] != hashlib.sha256((resource / start["virtualPath"]).read_bytes()).hexdigest():
            raise AutomationError("Low-evil preparation used a different native source")
        records.extend(completed_script(output, start))
    uses = sum(start["virtualPath"] == paths[1] for start in starts)
    if not uses or int(prepared["variables"]["EvilVal"]) != int(source["variables"]["EvilVal"]) - uses * 25:
        raise AutomationError("Low-evil preparation lacks its completed native book-use chain")
    write_json(output / "ending-low-evil-preparation-proof.json", dict(source=source, after=prepared,
               bookUses=uses, records=records, cheatAssisted=False))
    if boundary:
        if prepared["variables"].get("EvilVal") != "840":
            raise AutomationError("Exact 860 gate preparation requires the ordinary 840 book-chain result")
        book = next(item for item in prepared["inventory"] if item["file"] == "book07-潮月剑法.ini")
        client.equip(book["slot"])
        after = checkpoint(client, output, "563-ending-evil-860-boundary")
        path = "script/goods/book07-潮月剑法.txt"
        start = next(item for item in reversed(trace_records(output)) if item.get("eventType") == "script.start" and item.get("virtualPath") == path)
        if (after["variables"].get("EvilVal") != "860"
                or any(after["variables"].get(key) != prepared["variables"].get(key) for key in ("Event", "Result", "SenseVal"))
                or start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest()):
            raise AutomationError("Native tide-book use did not establish the exact 860 gate source")
        write_json(output / "ending-evil-860-boundary-proof.json", dict(source=prepared, after=after,
                   records=completed_script(output, start), cheatAssisted=False))
    ending_junction(client, output, resource)


def petrify_weapon_preparation(client, output, resource, repeat_visitor=False):
    source = idle(client)
    if (source["map"] != "map_030_悲魔山庄.map" or source["variables"].get("Event") != "570"
            or source["variables"].get("Result") != "1"):
        raise AutomationError("Petrify weapon preparation requires the ordinary ending-one pre-marriage source")
    checkpoint(client, output, "petrify-weapon-source", variables=("SubEvent18",))
    for destination, trap in (("map_028_连接地图.map", 5), ("map_027_连接地图.map", 3),
                              ("map_012_惠安镇.map", 2), ("map_011_连接地图.map", 1)):
        transition(client, resource, destination, trap, running=True)
    checkpoint(client, output, "petrify-weapon-visitor-before", variables=("SubEvent18",))
    interact_named(client, output, resource, "天王岛游客")
    state = checkpoint(client, output, "petrify-weapon-visitor-after", variables=("SubEvent18",))
    if state["variables"].get("SubEvent18") != "10":
        raise AutomationError("Native visitor did not bind the Dugu sword chest")
    state = interact_named(client, output, resource, "支线宝箱", position=(7, 68))
    weapon = next((item for item in state["inventory"] if item["file"] == "goods-w20-独孤剑.ini" and item["quantity"] > 0), None)
    if weapon is None:
        raise AutomationError("Native sidequest chest did not grant Dugu sword")
    client.equip(weapon["slot"])
    state = idle(client)
    if not any(item["file"] == weapon["file"] and item["slot"] == 204 for item in state["inventory"]):
        raise AutomationError("Ordinary equipment menu did not equip Dugu sword")
    if repeat_visitor:
        quantity = lambda value: sum(item["quantity"] for item in value["inventory"] if item["file"] == weapon["file"])
        before = checkpoint(client, output, "dugu-visitor-repeat-before", variables=("SubEvent18",))
        earlier = {record["executionId"] for record in trace_records(output) if record.get("eventType") == "script.start"}
        interact_named(client, output, resource, "天王岛游客")
        after = checkpoint(client, output, "dugu-visitor-repeat-after", variables=("SubEvent18",))
        path = "script/map/map_011_连接地图/天王岛游客对话.txt"
        start = next((record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start"
                      and record["executionId"] not in earlier and record.get("virtualPath") == path), None)
        if start is None or start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Repeated Dugu visitor lacks current native script evidence")
        records = completed_script(output, start)
        if (quantity(after) != quantity(before) or after["variables"].get("SubEvent18") != "10"
                or any(record.get("apiName") in ("setobjscript", "saveobj", "assign", "addgoods") for record in records)):
            raise AutomationError("Repeated Dugu visitor rebound the chest or changed its completed quest")
        chest = next(target for target in after["targets"] if target["name"] == "支线宝箱"
                     and target["position"] == dict(x=7, y=68))
        chest_path = "script/map/map_011_连接地图/支线18宝箱.txt"
        opened = next((record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start"
                       and record["executionId"] in earlier and record.get("virtualPath") == chest_path), None)
        if opened is None or opened["contentSha256"] != hashlib.sha256((resource / chest_path).read_bytes()).hexdigest():
            raise AutomationError("Empty Dugu chest lacks its current native reward execution")
        opened_records = completed_script(output, opened)
        if not any(record.get("apiName") == "setobjscript" for record in opened_records):
            raise AutomationError("Native Dugu reward did not clear the chest script")
        chest_earlier = {record["executionId"] for record in trace_records(output) if record.get("eventType") == "script.start"}
        chest_response = "succeeded"
        try:
            client.interact(chest["id"], timeout=20)
        except AutomationError as error:
            if str(error) not in ("Interact: interaction_not_observed", "Interact: action_rejected"):
                raise
            chest_response = str(error)
        current = checkpoint(client, output, "dugu-empty-chest-repeat", variables=("SubEvent18",))
        new_chest_scripts = [record for record in trace_records(output) if record.get("eventType") == "script.start"
                             and record["executionId"] not in chest_earlier
                             and record.get("virtualPath") == chest_path]
        if (quantity(current) != quantity(before) or current["player"]["money"] != before["player"]["money"]
                or current["variables"].get("SubEvent18") != "10" or new_chest_scripts):
            raise AutomationError("Repeated Dugu chest started a new reward script or changed its reward")
        write_json(output / "dugu-repeat-native-proof.json", dict(visitorStart=start, visitorRecords=records,
                   openedChestStart=opened, openedChestRecords=opened_records, chestResponse=chest_response,
                   newChestScriptExecutions=0, weaponQuantity=quantity(current),
                   cheatAssisted=False, fullPlaythrough=False))
    for destination, trap in (("map_012_惠安镇.map", 2), ("map_027_连接地图.map", 3),
                              ("map_028_连接地图.map", 1), ("map_030_悲魔山庄.map", 2)):
        transition(client, resource, destination, trap, running=True)
    state = checkpoint(client, output, "petrify-weapon-return", variables=("SubEvent18",))
    if any(state["variables"].get(key) != source["variables"].get(key) for key in ("Event", "Result", "SenseVal", "EvilVal")):
        raise AutomationError("Ordinary weapon preparation changed the challenge story source")
    for path in ("script/map/map_011_连接地图/天王岛游客对话.txt", "script/map/map_011_连接地图/支线18宝箱.txt"):
        start = next(r for r in reversed(trace_records(output)) if r.get("eventType") == "script.start" and r.get("virtualPath") == path)
        if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Weapon preparation used a different native source")
        completed_script(output, start)
    client.save_or_load(6)
    checkpoint(client, output, "petrify-weapon-source-slot6", variables=("SubEvent18",))


def fight_with_poison(client, output, resource, name):
    state = client.observe(VARIABLES)
    data = (resource / "map" / state["map"]).read_bytes()
    _, width, height, image_size, _ = struct.unpack_from("<5i", data, 64)
    header, _, _, image_count = struct.unpack_from("<4i", data, 84)
    offset = header + image_count * image_size
    deadline = time.monotonic() + 600
    for attempt in range(200):
        state = client.observe(VARIABLES)
        if state["player"]["life"] <= 0:
            raise AutomationError("Poison combat: player_dead")
        targets = [item for item in state["targets"] if item["name"] == name and npc_attackable(item)]
        if not targets:
            return idle(client)
        if time.monotonic() >= deadline or state["player"]["mana"] < 8:
            raise AutomationError("Poison combat exceeded its normal time or mana supply")
        target = targets[0]
        position, goal = state["player"]["position"], target["position"]
        distance = lambda point, other: 2 * abs(point[0] - other["x"]) + abs(point[1] - other["y"])
        occupied = {(item["position"]["x"], item["position"]["y"]) for item in state["targets"]
                    if item["kind"] == "npc" and item.get("life", 1) > 0}
        candidates = [(x, y) for x in range(max(0, position["x"] - 8), min(width, position["x"] + 9))
                      for y in range(max(0, position["y"] - 16), min(height, position["y"] + 17))
                      if 4 <= distance((x, y), position) <= 16 and 14 <= distance((x, y), goal) <= 20
                      and (x, y) not in occupied and not data[offset + (y * width + x) * 10 + 6] & 0xc0
                      and data[offset + (y * width + x) * 10 + 7] != 1]
        if not candidates:
            raise AutomationError("Poison combat has no ordinary escape landing")
        landing = max(candidates, key=lambda point: (distance(point, goal), -distance(point, position)))
        client.act("JumpTo", generation=state["generation"], x=landing[0], y=landing[1], timeoutMs=10000)
        state = client.observe(VARIABLES)
        if state["player"]["life"] <= 0:
            raise AutomationError("Poison combat: player_dead")
        target = next((item for item in state["targets"] if item["id"] == target["id"] and npc_attackable(item)), None)
        if target is None:
            return idle(client)
        client.act("CastSkill", generation=state["generation"], slot=1, targetId=target["id"], timeoutMs=10000)
        if attempt % 10 == 0:
            checkpoint(client, output, f"poison-combat-{attempt}")
    raise AutomationError("Poison combat exceeded its ordinary input limit")


def meng_challenge_defeat(client, output, resource, farewell=True, win=False, kite=False):
    state = idle(client)
    event = state["variables"].get("Event")
    source_file = output / "245-meng-challenge-source-slot0.json"
    if state["map"] != "map_033_落叶谷.map" or event not in ("555", "558", "560") or event != "555" and not source_file.exists():
        raise AutomationError("Meng challenge requires the ordinary return with Qiangwei")
    if event == "555":
        client.save_or_load(0)
        source = checkpoint(client, output, "245-meng-challenge-source-slot0")
        if kite:
            spell = next(item for item in state["magic"] if item["file"] == "player-magic-孤烟逐云.ini")
            client.assign_magic(spell["slot"], 1)
        for _ in range(5):
            state = transition(client, resource, "map_033_落叶谷.map", 4)
            if state["variables"].get("Event") != "555":
                break
    else:
        source = json.loads(source_file.read_text(encoding="utf-8"))
    if state["variables"].get("Event") not in ("558", "560"):
        raise AutomationError("Ordinary Meng conversation did not begin the challenge")
    if state["variables"].get("Event") == "558" and state["player"]["life"] > 0:
        if not kite:
            client.save_or_load(1)
            checkpoint(client, output, "246-meng-fight-source-slot1")
        if win:
            if kite:
                fight_with_poison(client, output, resource, "孟知秋")
            else:
                magic_file = "player-magic-孤烟逐云.ini" if any(item["file"] == "player-magic-孤烟逐云.ini" for item in state.get("magic", [])) else "player-magic-烈火情天.ini"
                fight_named(client, output, resource, "孟知秋", use_magic=True, timeout=600, magic_file=magic_file)
        else:
            target = next(target for target in state["targets"] if target["name"] == "孟知秋" and target.get("hostile"))
            position = state["player"]["position"]
            if abs(target["position"]["x"] - position["x"]) * 2 + abs(target["position"]["y"] - position["y"]) > 2:
                client.submit("MoveTo", generation=state["generation"], x=target["position"]["x"],
                              y=target["position"]["y"] + 1, running=True, timeoutMs=90000)
    client.wait_until(lambda value: value["variables"].get("Event") == "560", timeout=120,
                      variables=VARIABLES, description="ordinary Meng challenge defeat and recovery")
    state = idle(client)
    if state["player"]["life"] <= 0 or state["variables"].get("SenseVal") != source["variables"].get("SenseVal"):
        raise AutomationError("Meng challenge defeat did not restore the native player state")
    path = "script/map/map_033_落叶谷/孟知秋死亡.txt" if win else "script/map/map_033_落叶谷/杨影枫死亡.txt"
    start = next((record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path), None)
    if start is None:
        raise AutomationError(f"Meng challenge lacks its required native source: {path}")
    records = completed_script(output, start)
    if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
        raise AutomationError("Meng challenge defeat used a different native source")
    write_json(output / ("meng-challenge-win-proof.json" if win else "meng-challenge-defeat-proof.json"),
               dict(records=records, event="560", cheatAssisted=False))
    checkpoint(client, output, "247-meng-challenge-recovered")
    client.save_or_load(2)
    if not farewell:
        return
    state = transition(client, resource, "map_033_落叶谷.map", 9)
    if state["variables"].get("Event") != "562":
        raise AutomationError("Native Qiangwei farewell did not reach the ending junction")
    checkpoint(client, output, "248-meng-farewell-complete")
    for destination, trap in (("map_032_天山.map", 1), ("map_031_连接地图.map", 2),
                              ("map_065_天山古道.map", 2), ("map_030_悲魔山庄.map", 2)):
        transition(client, resource, destination, trap, running=True)
    client.save_or_load(6)
    checkpoint(client, output, "249-ending-junction-source-slot6")


def cangjian_shoes_insufficient(client, output, resource):
    source = idle(client)
    if (source["map"] != "map_022_清平乡.map" or source["variables"].get("Event") != "232"
            or source["player"]["money"] != 570):
        raise AutomationError("Free shoes preparation requires the ordinary 570-coin open-investigation source")
    client.save_or_load(4)
    checkpoint(client, output, "560-free-shoes-before-purchases-slot4")
    for count in range(1, 5):
        state = interact_named(client, output, resource, "梁掌柜", shop=True)
        drug = next(item for item in state["shop"] if item["file"] == "goods-m00-金花.ini")
        client.buy(drug["slot"])
        state = client.observe(VARIABLES)
        if state["player"]["money"] != 570 - 140 * count:
            raise AutomationError("Normal medicine spending did not produce the free-shoes budget")
        client.ui("Cancel")
        idle(client)
    prepared = checkpoint(client, output, "561-free-shoes-ten-coins")
    if any(prepared["variables"].get(key) != source["variables"].get(key) for key in ("Event", "Result", "SenseVal", "EvilVal")):
        raise AutomationError("Medicine spending changed the investigation source")
    earlier = {record["executionId"] for record in trace_records(output) if record.get("eventType") == "script.start"}
    cangjian_shoes(client, output, resource)
    path = "script/map/map_015_藏剑山庄/找剑者对话.txt"
    lines = (resource / path).read_text(encoding="utf-8-sig").splitlines()
    label = next(number for number, line in enumerate(lines, 1) if line.strip() == "::NoMoney::")
    statement = next(number for number in range(label + 1, len(lines) + 1) if source_line(lines[number - 1]).strip())
    proof = []
    for start in trace_records(output):
        if (start.get("eventType") != "script.start" or start["executionId"] in earlier
                or start.get("virtualPath") != path):
            continue
        if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Free shoes used a different native quest script")
        records = completed_script(output, start)
        if any(record.get("eventType") == "source.line" and record.get("line") == statement for record in records):
            proof.append(dict(start=start, records=records))
    if len(proof) != 1:
        raise AutomationError("Free shoes lack one current completed insufficient-money execution")
    state = checkpoint(client, output, "562-free-shoes-quest-completed", variables=("SubEvent21", "ShoeMoney"))
    if state["player"]["money"] != 10 or state["variables"].get("SubEvent21") != "10":
        raise AutomationError("Free shoes did not preserve the ten-coin budget and finish the quest")
    write_json(output / "free-shoes-native-proof.json", dict(scripts=proof, moneyBeforeChoice=10, moneyAfterChoice=10,
               freeGift=True, repeatedReward=False, cheatAssisted=False, fullPlaythrough=False))


def cangjian_shoes(client, output, resource):
    state = idle(client)
    if (state["map"], state["variables"].get("Event")) not in (("map_022_清平乡.map", "232"), ("map_015_藏剑山庄.map", "234")):
        raise AutomationError("Sword seeker's shoes require the ordinary investigation save that loads cangjian2.npc")
    if state["map"] == "map_022_清平乡.map":
        for destination, trap in (("map_021_油菜花地.map", 1), ("map_024_倚天山.map", 1),
                                  ("map_023_连接地图.map", 3), ("map_017_连接地图.map", 2),
                                  ("map_014_连接地图.map", 1), ("map_015_藏剑山庄.map", 2)):
            transition(client, resource, destination, trap, running=True)
    state = idle(client)
    if state["player"]["position"]["y"] < 34:
        client.move(40, 31)
        client.act("JumpTo", generation=client.observe()["generation"], x=40, y=34, timeoutMs=30000)
        checkpoint(client, output, "250-sword-seeker-gate-normal-jump")
    if state["player"]["position"]["y"] < 132:
        client.move(39, 131, timeout=120)
        client.act("JumpTo", generation=client.observe()["generation"], x=44, y=132, timeoutMs=30000)
        checkpoint(client, output, "250-sword-seeker-garden-normal-jump")
    for count in range(1, 7):
        interact_named(client, output, resource, "找剑者")
        state = checkpoint(client, output, f"250-sword-seeker-conversation-{count}", variables=("SubEvent21",))
        if state["variables"].get("SubEvent21") != str(count):
            raise AutomationError("Sword seeker's ordinary conversation count did not advance")
    client.save_or_load(0)
    source = checkpoint(client, output, "251-sword-seeker-shoes-source-slot0", variables=("SubEvent21",))
    quantity = lambda value: sum(item["quantity"] for item in value["inventory"] if item["file"] == "goods-f08-速攻鞋.ini")
    for option in (1, 0):
        if option == 0:
            load_checkpoint(client, 0)
            client.act("SetAutoDialogue", enabled=True)
        interact_named(client, output, resource, "找剑者", choice=("script/map/map_015_藏剑山庄/找剑者对话.txt:25", option))
        state = checkpoint(client, output, f"252-sword-seeker-shoes-answer-{option}", variables=("SubEvent21",))
        charge = 20 if option == 0 and source["player"]["money"] >= 20 else 0
        if state["variables"].get("SubEvent21") != "10" or state["player"]["money"] != source["player"]["money"] - charge or quantity(state) != quantity(source) + (option == 0):
            raise AutomationError("Shoes choice did not apply its native money/item outcome")
        before = state
        state = interact_named(client, output, resource, "找剑者")
        if state["player"]["money"] != before["player"]["money"] or quantity(state) != quantity(before):
            raise AutomationError("Sword seeker repeated his completed reward")
        client.save_or_load(1 if option == 0 else 2)
    client.save_or_load(6)
    checkpoint(client, output, "253-sword-seeker-complete-slot6", variables=("SubEvent21",))


def hermit_amulet_early(client, output, resource):
    source = client.observe((*VARIABLES, "Lany"))
    amulet = "goods-n13-紫霞玉佩.ini"
    quantity = lambda state: sum(item["quantity"] for item in state["inventory"] if item["file"] == amulet)
    if (source["map"] != "map_012_惠安镇.map" or source["variables"].get("Event") != "110"
            or source["variables"].get("Result") != "0" or int(source["variables"].get("Lany") or 0) != 0
            or quantity(source)):
        raise AutomationError("Early amulet preparation requires the ordinary first-morning town source without the amulet")
    source = checkpoint(client, output, "580-hermit-early-amulet-source", variables=("Lany",))
    for name, position in (("宝箱", (4, 30)), ("宝箱", (52, 28)), ("宝盒", (96, 54)), ("宝箱", (7, 78))):
        state = client.observe(VARIABLES)
        if state["player"]["money"] >= 2300:
            break
        if not any(target["kind"] == "object" and target["name"] == name and target["position"] == dict(zip(("x", "y"), position))
                   for target in state["targets"]):
            continue
        client.move(90, 100, running=True)
        interact_named(client, output, resource, name, position=position)
    before = checkpoint(client, output, "581-hermit-early-amulet-before-shop", variables=("Lany",))
    if (before["player"]["money"] < 2300 or any(before["variables"].get(key) != source["variables"].get(key)
                                               for key in ("Event", "Result", "SenseVal", "EvilVal"))):
        raise AutomationError("Native treasure did not fund the amulet and the later date")
    state = interact_named(client, output, resource, "牛老板", shop=True)
    item = next(item for item in state["shop"] if item["file"] == amulet)
    client.buy(item["slot"])
    after = client.observe(VARIABLES)
    if quantity(after) != 1 or after["player"]["money"] != before["player"]["money"] - 1900:
        raise AutomationError("Early native amulet purchase did not match its current derived price")
    client.ui("Cancel")
    idle(client)
    client.save_or_load(5)
    checkpoint(client, output, "582-hermit-early-amulet-purchased-slot5", variables=("Lany",))
    for chapter in (cangjian_first_visit, hanbo_first_visit, huian_date_arrival):
        chapter(client, output, resource)
        if quantity(client.observe()) != 1:
            raise AutomationError("Ordinary early story progression lost the purchased amulet")
    huian_date_orders(client, output, resource, single_order=True)
    qingping_rescue(client, output, resource)
    final = checkpoint(client, output, "583-hermit-early-amulet-cold-source-slot6", variables=("Lany",))
    if quantity(final) != 1 or int(final["variables"].get("Lany") or 0) != 0 or not final["player"]["canJump"]:
        raise AutomationError("Ordinary early story did not preserve the unvisited hermit source with its amulet")
    paths = ("script/common/捡多钱.txt", "script/common/捡多多钱.txt", "script/map/map_012_惠安镇/卖货牛老板.txt")
    records = []
    for start in trace_records(output):
        if start.get("eventType") == "script.start" and start.get("virtualPath") in paths:
            if start["contentSha256"] != hashlib.sha256((resource / start["virtualPath"]).read_bytes()).hexdigest():
                raise AutomationError("Early amulet preparation used a different native resource")
            records.append(completed_script(output, start))
    write_json(output / "hermit-early-amulet-native-proof.json", dict(records=records, cheatAssisted=False, fullPlaythrough=False))


def hanbo_hermit(client, output, resource, own_before=None):
    variables = ("Lany", "TouchedWooy", "FollowWaitSide", "RecentlyMeetWooy", "Wooy")
    source = client.observe((*VARIABLES, *variables))
    amulet = "goods-n13-紫霞玉佩.ini"
    quantity = lambda state: sum(item["quantity"] for item in state["inventory"] if item["file"] == amulet)
    phase = (source["variables"].get("Event"), source["variables"].get("Result"))
    already_owned = (own_before == 10 and source["map"] == "map_019_寒波谷.map"
                     and phase == ("185", "0") and quantity(source) == 1)
    allowed_source = (source["map"], *phase) in (("map_019_寒波谷.map", "185", "0"), ("map_019_寒波谷(b).map", "231", "0"))
    if (not allowed_source
            or int(source["variables"].get("Lany") or 0) != 0 or own_before not in (None, 10, 11)
            or own_before is not None and phase != ("231", "0") and not already_owned):
        raise AutomationError("Hermit gifts require an ordinary source with the needed exits open before any meeting")
    if quantity(source) and not already_owned:
        raise AutomationError("Hermit gift preparation must start without the amulet")
    source = checkpoint(client, output, "266-hermit-gift-source", variables=variables)
    client.save_or_load(0)
    evidence = []
    expected_money = source["player"]["money"]
    path = "script/common/鼓眼鱼.txt"

    def check_story(state):
        if any(state["variables"].get(key) != source["variables"].get(key) for key in ("Event", "Result", "SenseVal", "EvilVal", "EvilValue")):
            raise AutomationError("Hermit visits changed the main story or affection")

    def visit(expected_meeting, expected_quantity, repeated=False):
        sequence = trace_records(output)[-1]["sequence"]
        interact_named(client, output, resource, "鼓眼鱼")
        state = client.observe((*VARIABLES, *variables))
        check_story(state)
        if (int(state["variables"].get("Lany") or 0) != expected_meeting or quantity(state) != expected_quantity
                or state["player"]["money"] != expected_money):
            raise AutomationError("Hermit visit changed its native meeting count, gift, or money")
        starts = [record for record in trace_records(output) if record.get("eventType") == "script.start"
                  and record.get("virtualPath") == path and record["sequence"] > sequence]
        if len(starts) != 1 or starts[0]["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Hermit visit lacks a fresh current-source execution")
        records = completed_script(output, starts[0])
        if repeated and any(record.get("apiName") == "addgoods" for record in records):
            raise AutomationError("Repeated hermit dialogue awarded another amulet")
        evidence.append(dict(meeting=expected_meeting, quantity=expected_quantity, repeated=repeated, records=records))
        checkpoint(client, output, f"267-hermit-gift-{len(evidence)}", variables=variables)
        return state

    def ensure_thew():
        if client.observe()["player"]["thew"] < 80:
            client.act("ToggleSit")
            client.wait_until(lambda state: state["player"]["thew"] >= source["player"]["thew"],
                              timeout=90, description="ordinary sitting recovery before stream jumps")
            client.act("ToggleSit")
            idle(client)

    def cross_to_hermit():
        ensure_thew()
        client.move(44, 20, running=True)
        client.act("JumpTo", generation=client.observe()["generation"], x=37, y=20, timeoutMs=30000)
        idle(client)
        if int(client.observe(variables)["variables"].get("TouchedWooy") or 0) != 0:
            raise AutomationError("Native stream crossing did not reset the hermit visit")

    def buy_amulet():
        nonlocal expected_money
        for destination, trap in (("map_018_连接地图.map", 1), ("map_017_连接地图.map", 1), ("map_014_连接地图.map", 1), ("map_012_惠安镇.map", 1)):
            transition(client, resource, destination, trap, running=True)
        for name, position in (("宝箱", (4, 30)), ("宝箱", (52, 28)), ("宝盒", (96, 54))):
            if client.observe()["player"]["money"] >= 1900:
                break
            client.move(90, 100, running=True)
            interact_named(client, output, resource, name, position=position)
            check_story(client.observe(VARIABLES))
        before = checkpoint(client, output, "268-hermit-amulet-before-shop", variables=variables)
        if before["player"]["money"] < 1900:
            raise AutomationError("Native town treasure did not fund the amulet")
        sequence = trace_records(output)[-1]["sequence"]
        state = interact_named(client, output, resource, "牛老板", shop=True)
        item = next(item for item in state["shop"] if item["file"] == amulet)
        client.buy(item["slot"])
        state = client.observe((*VARIABLES, *variables))
        check_story(state)
        # Cost=0 selects the native equipment formula: 25*20 + 700*2 = 1900.
        if quantity(state) != quantity(before) + 1 or state["player"]["money"] != before["player"]["money"] - 1900:
            raise AutomationError("Native amulet purchase did not match its current derived price")
        expected_money = state["player"]["money"]
        client.ui("Cancel")
        idle(client)
        shop_path = "script/map/map_012_惠安镇/卖货牛老板.txt"
        starts = [record for record in trace_records(output) if record.get("eventType") == "script.start"
                  and record.get("virtualPath") == shop_path and record["sequence"] > sequence]
        if len(starts) != 1 or starts[0]["contentSha256"] != hashlib.sha256((resource / shop_path).read_bytes()).hexdigest():
            raise AutomationError("Native amulet shop lacks a fresh current-source execution")
        write_json(output / "hermit-amulet-shop-proof.json", dict(records=completed_script(output, starts[0]),
                   goodsSha256=hashlib.sha256((resource / "ini/goods" / amulet).read_bytes()).hexdigest(), cheatAssisted=False))
        checkpoint(client, output, "268-hermit-amulet-purchased", variables=variables)
        for destination, trap in (("map_014_连接地图.map", 2), ("map_017_连接地图.map", 3), ("map_018_连接地图.map", 2), (source["map"], 2)):
            transition(client, resource, destination, trap, running=True)

    if own_before == 10 and not already_owned:
        buy_amulet()
    cross_to_hermit()
    for meeting in range(1, 12):
        if meeting > 1:
            ensure_thew()
            client.move(37, 20, running=True)
            client.act("JumpTo", generation=client.observe()["generation"], x=44, y=20, timeoutMs=30000)
            idle(client)
            if meeting == 11 and own_before == 11:
                buy_amulet()
            cross_to_hermit()
        expected_meeting = 12 if (meeting == 10 and own_before == 10 or meeting == 11 and own_before == 11) else meeting
        expected_quantity = int(own_before == 10 or meeting == 11)
        state = visit(expected_meeting, expected_quantity)
        if meeting == 10:
            client.save_or_load(1)
            checkpoint(client, output, "269-hermit-tenth-source-slot1", variables=variables)
            visit(expected_meeting, expected_quantity, repeated=True)
        if expected_meeting == 12:
            break
    visit(expected_meeting, expected_quantity, repeated=True)
    client.save_or_load(6)
    checkpoint(client, output, "270-hermit-gift-complete-slot6", variables=variables)
    write_json(output / "hermit-gift-native-proof.json", dict(ownBeforeMeeting=own_before, amuletAlreadyOwnedAtSource=already_owned, executions=evidence,
               cheatAssisted=False, fullPlaythrough=False))


def huian_fishing_hook(client, output, resource):
    variables = ("SubEvent14",)
    source = client.observe((*VARIABLES, *variables))
    phase = (source["variables"].get("Event"), source["variables"].get("Result"))
    if (source["map"] != "map_012_惠安镇.map" or phase not in (("500", "0"), ("570", "1"))
            or int(source["variables"].get("SubEvent14") or 0) != 0):
        raise AutomationError("Fishing hook requires an ordinary daytime town source before accepting the quest")
    quantity = lambda state, file: sum(item["quantity"] for item in state["inventory"] if item["file"] == file)
    hook, book = "goods-e14-鱼钩.ini", "book14-金钟罩.ini"
    if quantity(source, hook) or quantity(source, book):
        raise AutomationError("Fishing hook source already contains its quest items")
    client.save_or_load(0)
    source = checkpoint(client, output, "570-fishing-hook-source-slot0", variables=variables)
    evidence = []

    def talk(name, stage, evil_offset, hooks, books):
        sequence = trace_records(output)[-1]["sequence"]
        interact_named(client, output, resource, name)
        state = client.observe((*VARIABLES, *variables))
        if (state["variables"].get("SubEvent14") != str(stage)
                or quantity(state, hook) != hooks or quantity(state, book) != books
                or state["player"]["money"] != source["player"]["money"]
                or state["variables"].get("EvilVal") != str(int(source["variables"]["EvilVal"]) + evil_offset)
                or any(state["variables"].get(key) != source["variables"].get(key) for key in ("Event", "Result", "SenseVal"))):
            raise AutomationError("Fishing dialogue did not apply its native quest, item, and affection outcome")
        path = ("script/map/map_014_连接地图/路人钓鱼翁对话.txt" if name == "钓鱼翁"
                else "script/map/map_012_惠安镇/惠安镇渔夫对话.txt")
        starts = [record for record in trace_records(output) if record.get("eventType") == "script.start"
                  and record.get("virtualPath") == path and record["sequence"] > sequence]
        if len(starts) != 1 or starts[0]["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Fishing dialogue lacks one fresh current-source execution")
        evidence.append(completed_script(output, starts[0]))
        checkpoint(client, output, f"571-fishing-hook-dialogue-{len(evidence)}", variables=variables)

    transition(client, resource, "map_014_连接地图.map", 2, running=True)
    talk("钓鱼翁", 2, -10, 0, 0)
    talk("钓鱼翁", 2, -10, 0, 0)
    transition(client, resource, "map_012_惠安镇.map", 1, running=True)
    talk("陈放", 5, -10, 1, 0)
    talk("陈放", 5, -10, 1, 0)
    transition(client, resource, "map_014_连接地图.map", 2, running=True)
    talk("钓鱼翁", 10, 10, 0, 1)
    talk("钓鱼翁", 10, 10, 0, 1)
    client.save_or_load(6)
    checkpoint(client, output, "572-fishing-hook-complete-slot6", variables=variables)
    write_json(output / "fishing-hook-native-proof.json", dict(records=evidence, cheatAssisted=False, fullPlaythrough=False))


def ending_two_zixuan(client, output, resource):
    state = idle(client)
    if state["map"] != "map_030_悲魔山庄.map" or state["variables"].get("Event") != "1800" or state["variables"].get("Result") != "2":
        raise AutomationError("Zixuan's late encounter requires the ordinary ending-two entry")
    client.save_or_load(0)
    source = checkpoint(client, output, "280-ending-two-zixuan-source-slot0")
    path = "script/map/map_036_连接地图/路遇紫轩.txt"
    for slot, answers in ((1, ((path + ":23", 1),)),
                          (2, ((path + ":23", 0), (path + ":30", 1))),
                          (3, ((path + ":23", 0), (path + ":30", 0)))):
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        state = transition(client, resource, "map_036_连接地图.map", 6, output=output, choice=answers)
        if state["variables"].get("Event") != "1801" or state["variables"].get("Result") != "2" or any(state["variables"].get(key) != source["variables"].get(key) for key in ("SenseVal", "EvilVal")):
            raise AutomationError("Zixuan's late encounter did not preserve its native continuation")
        if any(target.get("name") == "紫轩" for target in state["targets"]):
            raise AutomationError("Zixuan's native departure did not complete")
        client.save_or_load(slot)
        checkpoint(client, output, f"281-ending-two-zixuan-route-{slot}")
    load_checkpoint(client, 3)
    client.act("SetAutoDialogue", enabled=True)
    transition(client, resource, "map_037_敦煌十洞.map", 2)
    client.save_or_load(6)
    checkpoint(client, output, "282-ending-two-dunhuang-source-slot6")


def ending_two_fortress(client, output, resource):
    state = idle(client)
    source_file = output / "283-ending-two-fortress-source-slot0.json"
    if state["variables"].get("Event") != "1801" or state["variables"].get("Result") != "2":
        raise AutomationError("Fortress rescue requires the ordinary ending-two Zixuan encounter")
    if state["map"] == "map_037_敦煌十洞.map":
        client.save_or_load(0)
        checkpoint(client, output, "283-ending-two-fortress-source-slot0")
        for trap in (3, 4):
            transition(client, resource, "map_037_敦煌十洞.map", trap)
        transition(client, resource, "map_038_连接地图.map", 1)
        transition(client, resource, "map_039_飞龙堡.map", 2)
        client.save_or_load(1)
        checkpoint(client, output, "284-ending-two-fortress-battle-slot1", variables=("NpcCount",))
    elif state["map"] != "map_039_飞龙堡.map" or not source_file.is_file():
        raise AutomationError("Fortress rescue lacks its recorded ordinary battle source")
    fight_hostiles(client, output, resource, magic_file="player-magic-魂牵梦绕.ini")
    state = checkpoint(client, output, "285-ending-two-fortress-cleared", variables=("NpcCount",))
    if state["variables"].get("Event") != "1802" or state["variables"].get("NpcCount") != "0":
        raise AutomationError("Native fortress deaths did not release the post-battle search")
    path = "script/map/map_039_飞龙堡/死亡.txt"
    start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
    if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
        raise AutomationError("Fortress battle used a different native death script")
    write_json(output / "ending-two-fortress-proof.json", dict(records=completed_script(output, start), cheatAssisted=False, storyEnding=False))
    transition(client, resource, "map_039_飞龙堡.map", 6)
    client.save_or_load(6)
    checkpoint(client, output, "286-ending-two-fortress-search-complete-slot6")


def ending_one_marriage(client, output, resource):
    state = idle(client)
    source_file = output / "300-ending-one-proposal-source-slot0.json"
    if state["variables"].get("Result") != "1":
        raise AutomationError("Marriage requires the ordinary ending-one proposal letter")
    if state["map"] == "map_030_悲魔山庄.map" and state["variables"].get("Event") == "570":
        client.save_or_load(0)
        checkpoint(client, output, "300-ending-one-proposal-source-slot0")
        for destination, trap in (("map_065_天山古道.map", 1), ("map_031_连接地图.map", 1),
                                  ("map_032_天山.map", 1), ("map_033_落叶谷.map", 1)):
            transition(client, resource, destination, trap, running=True)
        state = interact_named(client, output, resource, "家丁阿甘")
    elif not (source_file.is_file() and (
            state["map"] == "map_033_落叶谷.map" and state["variables"].get("Event") == "572" or
            state["map"] == "map_030_悲魔山庄.map" and state["variables"].get("Event") == "574"
            and (output / "301-ending-one-proposal-complete.json").is_file())):
        raise AutomationError("Marriage lacks its recorded ordinary proposal source")
    if state["map"] == "map_033_落叶谷.map":
        if state["variables"].get("Event") != "572":
            raise AutomationError("Native proposal reception did not reach Meng's dialogue")
        state = interact_named(client, output, resource, "孟知秋")
        if state["variables"].get("Event") != "574":
            raise AutomationError("Native Meng proposal did not complete")
        checkpoint(client, output, "301-ending-one-proposal-complete")
        for destination, trap in (("map_032_天山.map", 1), ("map_031_连接地图.map", 2),
                                  ("map_065_天山古道.map", 2), ("map_030_悲魔山庄.map", 2)):
            transition(client, resource, destination, trap, running=True)
    state = idle(client)
    if not any(target.get("hostile") and npc_attackable(target) for target in state["targets"]):
        client.move(44, 140)
        interact_named(client, output, resource, "管家铁云")
        client.save_or_load(2)
        checkpoint(client, output, "302-ending-one-wedding-duels-source-slot2")
    elif not (output / "302-ending-one-wedding-duels-source-slot2.json").is_file():
        raise AutomationError("Wedding combat lacks its recorded normal source")
    fight_hostiles(client, output, resource)
    state = checkpoint(client, output, "303-ending-one-marriage-complete")
    if state["variables"].get("Event") != "576" or state["variables"].get("Result") != "1":
        raise AutomationError("Native wedding duels did not complete the marriage")
    paths = ("挑战者死亡.txt", "张惟宜死亡.txt", "婚礼.txt")
    records = {}
    for name in paths:
        path = "script/map/map_030_悲魔山庄/" + name
        start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
        if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Wedding used a different native source")
        records[path] = completed_script(output, start)
    write_json(output / "ending-one-marriage-proof.json", dict(records=records, cheatAssisted=False, storyEnding=False))
    client.save_or_load(6)
    checkpoint(client, output, "304-ending-one-zhen-pursuit-source-slot6")


def ending_one_aftermath(client, output, resource, win=False):
    state = idle(client)
    if state["variables"].get("Result") != "1":
        raise AutomationError("Marriage aftermath requires its ordinary three-month checkpoint")
    if state["map"] == "map_030_悲魔山庄.map" and state["variables"].get("Event") == "580":
        client.save_or_load(0)
        checkpoint(client, output, "330-ending-one-aftermath-source-slot0")
        state = transition(client, resource, "map_028_连接地图.map", 5)
        if state["variables"].get("Event") != "585":
            raise AutomationError("Native masked challengers did not start their ambush")
        client.save_or_load(1)
        checkpoint(client, output, "331-ending-one-masked-challenge-source-slot1", variables=("NpcCount",))
    elif state["map"] != "map_028_连接地图.map" or state["variables"].get("Event") != "585" or not (output / "331-ending-one-masked-challenge-source-slot1.json").is_file():
        raise AutomationError("Marriage aftermath lacks its recorded ordinary ambush source")
    if win:
        if any(item["file"] == "goods-w20-独孤剑.ini" and item["slot"] == 204 for item in state["inventory"]):
            fight_named(client, output, resource, "紫衣杀手", timeout=600, single_target=True)
        else:
            fight_hostiles(client, output, resource, magic_file="player-magic-魂牵梦绕.ini")
    else:
        client.move(10, 20)
    client.wait_until(lambda value: value.get("worldInput") and value.get("player", {}).get("life", 0) > 0
                      and not any(target.get("hostile") and npc_attackable(target) for target in value["targets"]),
                      timeout=120, description="ordinary masked challenge defeat and recovery")
    state = idle(client)
    if state["map"] != "map_028_连接地图.map" or state["variables"].get("Event") != "585" or state["player"]["life"] <= 0 or any(target.get("hostile") and npc_attackable(target) for target in state["targets"]):
        raise AutomationError("Native poison ambush did not complete its scripted recovery")
    records = {}
    paths = ["script/map/map_028_连接地图/杨影枫死亡.txt"]
    if win:
        paths.append("script/map/map_028_连接地图/紫衫蒙面人死亡.txt")
    for path in paths:
        start = next((record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path), None)
        if start is None:
            raise AutomationError(f"Poison ambush lacks its required native source: {path}")
        if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Poison ambush used a different native source")
        records[path] = completed_script(output, start)
    write_json(output / "ending-one-poison-ambush-proof.json", dict(records=records, victory=win, cheatAssisted=False, storyEnding=False))
    checkpoint(client, output, "332-ending-one-poison-ambush-recovered")
    transition(client, resource, "map_030_悲魔山庄.map", 2)
    client.move(44, 140)
    state = interact_named(client, output, resource, "管家铁云")
    if state["variables"].get("Event") != "590":
        raise AutomationError("Native steward did not release Meng's martial lesson")
    for destination, trap in (("map_065_天山古道.map", 1), ("map_031_连接地图.map", 1),
                              ("map_032_天山.map", 1), ("map_033_落叶谷.map", 1)):
        transition(client, resource, destination, trap, running=True)
    state = interact_named(client, output, resource, "家丁阿甘")
    if state["variables"].get("Event") != "591":
        raise AutomationError("Native Meng reception did not release the martial lesson")
    state = interact_named(client, output, resource, "孟知秋")
    if state["variables"].get("Event") != "592" or not any(item["file"] == "player-magic-沧海月明.ini" for item in state["magic"]):
        raise AutomationError("Native Meng lesson did not teach Cang Hai Yue Ming")
    client.save_or_load(6)
    checkpoint(client, output, "333-ending-one-zixuan-duel-route-slot6")


def tower_sit(client, state):
    try:
        client.act("ToggleSit", generation=state["generation"])
    except AutomationError as error:
        current = client.observe()
        if (str(error) != "ToggleSit: action_rejected" or current["generation"] != state["generation"]
                or current["player"]["action"] != 9):
            raise
        return False
    return True


def tower_transition(client, output, resource, destination, trap, choice=None):
    """Use short ordinary moves and clear nearby enemies in the crowded tower."""
    config = configparser.ConfigParser(interpolation=None)
    config.read(resource / "ini/magic/player-magic-魂牵梦绕.ini", encoding="utf-8-sig")
    trace_file = output / "user-data/automation/trace.jsonl"
    earlier = {item["executionId"] for item in trace_records(output) if item.get("eventType") == "script.start"} if trace_file.is_file() else set()
    backtracked = False
    for _ in range(256):
        state = idle(client)
        position = state["player"]["position"]
        entrances = trap_points(resource, state["map"], 1) if trap != 1 else ()
        blocked = set(entrances) | {(item["position"]["x"], item["position"]["y"]) for item in state["targets"]
                                   if item["kind"] == "npc" and item.get("life", 1) > 0
                                   and not (item.get("hostile") and npc_attackable(item))}
        if trap != 1:
            if any(2 * abs(x - position["x"]) + abs(y - position["y"]) <= 6 for x, y in entrances):
                ahead = reachable_trap(resource, state["map"], trap, position, with_path=True, avoid=blocked)
                if len(ahead) > 17:
                    try:
                        client.move(*ahead[1])
                    except AutomationError as error:
                        if str(error) != "MoveTo: blocked_destination":
                            raise
                        current = checkpoint(client, output, f"tower-blocked-entrance-{time.time_ns()}")
                        point = current["player"]["position"]
                        if current["generation"] != state["generation"] or not any(item.get("hostile") and npc_attackable(item)
                                and 2 * abs(item["position"]["x"] - point["x"]) + abs(item["position"]["y"] - point["y"]) <= 14
                                for item in current["targets"]):
                            raise
                        state, position = current, point
                    else:
                        continue
        foes = [target for target in state["targets"] if target.get("hostile") and npc_attackable(target)]
        distance = lambda target: 2 * abs(target["position"]["x"] - position["x"]) + abs(target["position"]["y"] - position["y"])
        target = min(foes, key=distance) if foes else None
        if target and distance(target) <= 14:
            spell = next(item for item in state["magic"] if item["file"] == "player-magic-魂牵梦绕.ini")
            cost = config.getint(f"Level{spell['level']}", "ManaCost")
            use_magic = state["player"]["mana"] >= cost
            if not use_magic and target.get("attackRadius", 1) >= 5:
                deadline = time.monotonic() + 90
                current = state
                while current["player"]["mana"] < cost:
                    player = current["player"]
                    if time.monotonic() >= deadline or player["life"] * 4 < player["lifeMax"]:
                        raise AutomationError("Tower ranged recovery did not safely restore one spell")
                    if not player["sitting"] and player["thew"] >= 5 and player["action"] in (0, 1, 20):
                        tower_sit(client, current)
                    time.sleep(0.2)
                    current = client.observe(VARIABLES)
                if current["player"]["sitting"]:
                    tower_sit(client, current)
                checkpoint(client, output, f"tower-ranged-sitting-{time.time_ns()}")
                continue
            try:
                fight_named(client, output, resource, target["name"], target_id=target["id"],
                            use_magic=use_magic, single_target=not use_magic, magic_file=spell["file"])
            except AutomationError as error:
                if str(error) == "StartCombat: world_changed":
                    current = idle(client)
                    if current["generation"] != state["generation"] and current["map"] == destination:
                        return current
                    source = f"script/map/{Path(state['map']).stem}/trap01.txt"
                    start = next((item for item in reversed(trace_records(output)) if item.get("eventType") == "script.start"
                                  and item["executionId"] not in earlier and item.get("virtualPath", "").casefold() == source.casefold()), None)
                    if (backtracked or current["generation"] == state["generation"] or start is None
                            or current["variables"].get("Event") != state["variables"].get("Event")
                            or start["contentSha256"] != hashlib.sha256((resource / source).read_bytes()).hexdigest()
                            or f'loadmap("{current["map"]}")' not in (resource / source).read_text(encoding="utf-8")):
                        raise
                    completed_script(output, start)
                    checkpoint(client, output, f"tower-native-backtrack-{start['executionId']}")
                    transition(client, resource, state["map"], 2, output=output, running=True)
                    backtracked = True
                    continue
                if str(error) == "StartCombat: no_progress":
                    current = checkpoint(client, output, f"tower-crowded-combat-{time.time_ns()}")
                    nearby = [item for item in current["targets"] if item.get("hostile") and npc_attackable(item)]
                    position = current["player"]["position"]
                    nearest = min(nearby, key=distance) if nearby else None
                    if nearest is None or distance(nearest) > 14:
                        raise
                    fight_named(client, output, resource, nearest["name"], target_id=nearest["id"],
                                single_target=True)
                    continue
                if str(error) != "StartCombat: skill_resources_unavailable" or client.observe()["player"]["mana"] >= cost:
                    raise
            continue
        if state["player"]["mana"] < 128 and (not target or distance(target) > 20):
            standing = client.wait_until(lambda value: value["player"]["action"] in (0, 1, 20), timeout=10)
            if not tower_sit(client, standing):
                continue
            client.wait_until(lambda value: value["player"]["mana"] >= min(128, value["player"]["manaMax"]),
                              timeout=120, description="ordinary tower sitting recovery")
            rested = client.observe()
            if rested["player"]["sitting"]:
                tower_sit(client, rested)
            checkpoint(client, output, f"tower-sitting-{time.time_ns()}")
            continue
        occupied = {(target["position"]["x"], target["position"]["y"]) for target in state["targets"]
                    if target["kind"] == "npc" and target.get("life", 1) > 0}
        path = reachable_trap(resource, state["map"], trap, position, occupied, with_path=True, avoid=blocked)
        if len(path) <= 17:
            try:
                return transition(client, resource, destination, trap, choice=choice, output=output, running=True)
            except AutomationError as error:
                if not str(error).startswith("Trap was not entered:"):
                    raise
                current = client.observe(VARIABLES)
                source = f"script/map/{Path(state['map']).stem}/trap{trap:02d}.txt"
                start = next((item for item in reversed(trace_records(output)) if item.get("eventType") == "script.start"
                              and item["executionId"] not in earlier and item.get("virtualPath", "").casefold() == source.casefold()), None)
                if (start is None or current["map"] != destination
                        or start["contentSha256"] != hashlib.sha256((resource / source).read_bytes()).hexdigest()):
                    raise
                completed_script(output, start)
                return checkpoint(client, output, f"tower-fast-trap-{trap}-{start['executionId']}")
        endpoint = path[16]
        for index, point in enumerate(path[1:17], 1):
            if point in occupied:
                endpoint = path[index - 1]
                break
        if endpoint == (position["x"], position["y"]):
            raise AutomationError("Tower path begins with an occupied tile outside combat range")
        print(f"Tower waypoint {endpoint}; remaining path {len(path)}", flush=True)
        try:
            client.move(*endpoint)
        except AutomationError as error:
            if str(error) == "MoveTo: blocked_destination" and path.index(endpoint) > 1:
                checkpoint(client, output, f"tower-blocked-waypoint-{time.time_ns()}")
                client.move(*path[path.index(endpoint) // 2])
                continue
            if str(error) != "MoveTo: no_progress":
                raise
            current = checkpoint(client, output, f"tower-crowded-move-{time.time_ns()}")
            position = current["player"]["position"]
            if current["generation"] != state["generation"] or not any(
                    item.get("hostile") and npc_attackable(item) and distance(item) <= 14 for item in current["targets"]):
                raise
    raise AutomationError("Tower traversal exceeded its ordinary move and combat limit")


def ending_one_tower_rescue(client, output, resource):
    locks = tuple(f"Lock{n}" for n in range(1, 9))
    source_file = output / "520-ending-one-tower-upper-source-slot0.json"
    state = idle(client)
    if state["variables"].get("Event") != "3182":
        raise AutomationError("Tower rescue requires the ordinary open sixth-floor switch")
    if state["map"] == "map_046_通天塔第六层.map":
        if not source_file.is_file():
            state = client.observe((*VARIABLES, *locks))
            if any(state["variables"].get(f"Lock{n}") != "1" for n in range(1, 7)):
                raise AutomationError("Tower rescue lacks its six opened switches")
            client.save_or_load(0)
            checkpoint(client, output, "520-ending-one-tower-upper-source-slot0", variables=locks)
        state = tower_transition(client, output, resource, "map_047_通天塔第七层.map", 2)
    elif not source_file.is_file():
        raise AutomationError("Upper tower rescue lacks its recorded ordinary source")
    for number, map_name in ((7, "map_047_通天塔第七层.map"), (8, "map_049_通天塔第八层.map")):
        if state["map"] != map_name:
            continue
        state = client.observe((*VARIABLES, *locks))
        if state["variables"].get(f"Lock{number}") != "1":
            tower_transition(client, output, resource, map_name, 8)
        state = checkpoint(client, output, f"521-ending-one-tower-floor-{number}-open", variables=locks)
        if state["variables"].get(f"Lock{number}") != "1":
            raise AutomationError(f"Native tower switch {number} did not open")
        if number == 7:
            state = tower_transition(client, output, resource, "map_049_通天塔第八层.map", 2)
    for origin, destination in (("map_049_通天塔第八层.map", "map_047_通天塔第七层.map"),
                               ("map_047_通天塔第七层.map", "map_046_通天塔第六层.map"),
                               ("map_046_通天塔第六层.map", "map_045_通天塔第五层.map")):
        if state["map"] == origin:
            state = tower_transition(client, output, resource, destination, 1)
    if state["map"] != "map_045_通天塔第五层.map":
        raise AutomationError("Native tower rescue did not return to Nalan Zhen")
    state = tower_transition(client, output, resource, "map_030_悲魔山庄.map", 7)
    if state["variables"].get("Event") != "3185":
        raise AutomationError("Native stone-door rescue did not reach the manor aftermath")
    path = "script/map/map_045_通天塔第五层/trap07.txt"
    start = next(r for r in reversed(trace_records(output)) if r.get("eventType") == "script.start" and r.get("virtualPath", "").casefold() == path.casefold())
    if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
        raise AutomationError("Tower rescue used a different native source")
    write_json(output / "ending-one-tower-rescue-proof.json", dict(records=completed_script(output, start), cheatAssisted=False))
    client.save_or_load(6)
    checkpoint(client, output, "522-ending-one-final-battle-source-slot6", variables=locks)


def ending_one_finale(client, output, resource, defeat=False):
    state = client.observe((*VARIABLES, "NpcCount"))
    source_file = output / "530-ending-one-finale-source-slot0.json"
    terminal = "script/map/map_030_悲魔山庄/杨影枫死亡.txt" if defeat else "script/map/map_030_悲魔山庄/死亡.txt"
    if (source_file.is_file() and state.get("inEvent")
            and state.get("script", "").casefold() == terminal.casefold()):
        client.act("SetAutoDialogue", enabled=True, intervalMs=1000)
        state = idle(client, output=output, resource=resource, expected_terminal=terminal)
    if state.get("scene") != "Title":
        state = idle(client)
        if state["map"] != "map_030_悲魔山庄.map" or state["variables"].get("Event") not in ("3185", "3210", "3220"):
            raise AutomationError("Manor finale requires its ordinary tower rescue or dream battle")
        if not source_file.is_file():
            client.save_or_load(0)
            checkpoint(client, output, "530-ending-one-finale-source-slot0", variables=("NpcCount",))
        if state["variables"].get("Event") == "3185":
            recover(client, output, resource)
            state = transition(client, resource, state["map"], 11)
            if state["variables"].get("Event") != "3210":
                raise AutomationError("Native martial siege did not begin")
            client.save_or_load(1)
            checkpoint(client, output, "531-ending-one-martial-siege-slot1", variables=("NpcCount",))
    source = json.loads(source_file.read_text(encoding="utf-8"))
    high = int(source["variables"]["EvilValue"]) > 118
    if defeat and not high:
        raise AutomationError("Dream defeat requires an ordinary evil value above 118")
    config = configparser.ConfigParser(interpolation=None)
    config.read(resource / "ini/magic/player-magic-魂牵梦绕.ini", encoding="utf-8-sig")
    for _ in range(128):
        state = client.observe((*VARIABLES, "NpcCount"))
        if state.get("scene") == "Title":
            break
        if state["variables"].get("Event") == "3220" and not (output / "532-ending-one-dream-battle-slot2.json").is_file():
            client.save_or_load(2)
            checkpoint(client, output, "532-ending-one-dream-battle-slot2", variables=("NpcCount",))
        if defeat and state["variables"].get("Event") == "3220":
            client.act("SetAutoDialogue", enabled=True, intervalMs=1000)
            state = idle(client, timeout=1800, output=output, resource=resource, expected_terminal=terminal)
            break
        foes = [target for target in state["targets"] if target.get("hostile") and npc_attackable(target)]
        if not foes:
            raise AutomationError("Manor finale has no active opponents and has not reached its ending")
        position = state["player"]["position"]
        target = min(foes, key=lambda item: 2 * abs(item["position"]["x"] - position["x"]) + abs(item["position"]["y"] - position["y"]))
        last = state["variables"].get("NpcCount") == "1" and (state["variables"].get("Event") == "3220" or not high)
        if last:
            client.act("SetAutoDialogue", enabled=True, intervalMs=1000)
        spell = next(item for item in state["magic"] if item["file"] == "player-magic-魂牵梦绕.ini")
        cost = config.getint(f"Level{spell['level']}", "ManaCost")
        use_magic = state["player"]["mana"] >= cost
        try:
            fight_named(client, output, resource, target["name"], target_id=target["id"],
                        use_magic=use_magic, single_target=not use_magic, magic_file=spell["file"], expected_terminal=terminal if last else None)
        except AutomationError as error:
            if str(error) in ("StartCombat: no_progress", "StartCombat: action_timeout"):
                current = checkpoint(client, output, f"finale-crowded-combat-{time.time_ns()}")
                position = current["player"]["position"]
                distance = lambda item: 2 * abs(item["position"]["x"] - position["x"]) + abs(item["position"]["y"] - position["y"])
                nearby = [item for item in current["targets"] if item.get("hostile") and npc_attackable(item)]
                nearest = min(nearby, key=distance) if nearby else None
                if nearest and nearest["id"] != target["id"] and distance(nearest) <= 14:
                    continue
            if str(error).startswith("StartCombat: item_depleted:"):
                depleted = str(error).split(": ", 2)[-1]
                remaining = client.observe().get("inventory", [])
                if any(item["file"] == depleted and item["quantity"] > 0 for item in remaining):
                    raise
                checkpoint(client, output, f"finale-item-depleted-{time.time_ns()}")
                continue
            if str(error) == "StartCombat: skill_resources_unavailable" and client.observe()["player"]["mana"] < cost:
                continue
            if str(error) != "Unexpected return to title" or client.observe().get("scene") != "Title":
                raise
            break
    else:
        raise AutomationError("Manor finale exceeded its ordinary combat limit")
    after = checkpoint(client, output, "533-ending-one-finale-title")
    start = next(r for r in reversed(trace_records(output)) if r.get("eventType") == "script.start" and r.get("virtualPath", "").casefold() == terminal.casefold())
    records = completed_script(output, start)
    if start["contentSha256"] != hashlib.sha256((resource / terminal).read_bytes()).hexdigest():
        raise AutomationError("Manor ending used a different native source")
    lines = {r["line"] for r in records if r.get("eventType") == "source.line"}
    source_lines = (resource / terminal).read_text(encoding="utf-8-sig").splitlines()
    dialogs = [json.loads(p.read_text(encoding="utf-8")) for p in output.glob("ending-dialogue-*.json")]
    videos = [json.loads(p.read_text(encoding="utf-8")) for p in output.glob("story-video-*.json")]
    movie = "end1.wmv" if high else "end2.wmv"
    if (after.get("scene") != "Title" or not any(r.get("apiName") == "returntotitle" for r in records)
            or not any("剧终" in source_lines[n - 1] for n in lines)
            or not any("剧终" in value.get("dialogue", {}).get("text", "")
                       and value["dialogue"].get("complete") for value in dialogs)
            or not any(value.get("video", "").lower().endswith(movie) for value in videos)):
        raise AutomationError("Manor ending lacks completed source, final dialogue, native video, or Title evidence")
    write_json(output / "ending-one-finale-proof.json", dict(records=records, source=source, after=after,
               movie=movie, dreamDefeat=defeat, finalDialogue=dialogs, observedVideos=videos, storyEnding=True, cheatAssisted=False))


def ending_one_tower_switches(client, output, resource):
    state = idle(client)
    source_file = output / "510-ending-one-tower-switch-source-slot0.json"
    locks = tuple(f"Lock{n}" for n in range(1, 9))
    if state["map"] != "map_046_通天塔第六层.map" or state["variables"].get("Event") != "3182":
        raise AutomationError("Tower switches require their ordinary sixth-floor arrival")
    high = int(state["variables"].get("EvilValue") or 0) > 112
    real, reset = ("B", "A") if high else ("A", "B")
    real_trap, reset_trap = (9, 8) if high else (8, 9)
    if not source_file.is_file():
        state = client.observe((*VARIABLES, *locks))
        if any(state["variables"].get(f"Lock{n}") != "1" for n in range(1, 6)) or state["variables"].get("Lock6") == "1":
            raise AutomationError("Tower switch source lacks its first five ordinary switches")
        client.save_or_load(0)
        checkpoint(client, output, "510-ending-one-tower-switch-source-slot0", variables=locks)
    path = f"script/map/map_046_通天塔第六层/trap{real_trap:02d}.txt"
    for option in (1, 0):
        if (output / f"511-ending-one-tower-{real}-{option}.json").is_file():
            continue
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        tower_transition(client, output, resource, state["map"], real_trap, choice=(f"{path}:14", option))
        after = checkpoint(client, output, f"511-ending-one-tower-{real}-{option}", variables=locks)
        if any(after["variables"].get(f"Lock{n}") != "1" for n in range(1, 6)) or (after["variables"].get("Lock6") == "1") != (option == 0):
            raise AutomationError(f"Native {real} switch did not retain or open the gate")
        client.save_or_load(3 if option == 0 else 2)
    load_checkpoint(client, 3)
    client.act("SetAutoDialogue", enabled=True)
    starts = [r for r in trace_records(output) if r.get("eventType") == "script.start" and r.get("virtualPath", "").casefold() == path.casefold()]
    reset_site = f"script/map/map_046_通天塔第六层/trap{reset_trap:02d}.txt:{35 if high else 47}"
    # Each switch rebinds the opposite trap. Refuse its reset before revisiting the open switch.
    tower_transition(client, output, resource, state["map"], reset_trap, choice=(reset_site, 1))
    after = checkpoint(client, output, f"512-ending-one-tower-{reset}-rearm-{real}", variables=locks)
    if any(after["variables"].get(f"Lock{n}") != "1" for n in range(1, 7)):
        raise AutomationError("Native opposite-switch refusal changed the opened switches")
    current = client.observe()
    away = reachable_trap(resource, state["map"], 1, current["player"]["position"], with_path=True)
    client.move(*away[min(4, len(away) - 1)])
    repeated = next((r for r in reversed(trace_records(output)) if r.get("eventType") == "script.start"
                     and r.get("virtualPath", "").casefold() == path.casefold()
                     and r["executionId"] not in {item["executionId"] for item in starts}), None)
    if repeated is None:
        tower_transition(client, output, resource, state["map"], real_trap)
    after = checkpoint(client, output, f"512-ending-one-tower-{real}-already-open", variables=locks)
    start = next(r for r in reversed(trace_records(output)) if r.get("eventType") == "script.start" and r.get("virtualPath", "").casefold() == path.casefold())
    records = completed_script(output, start)
    source = (resource / path).read_text(encoding="utf-8-sig")
    if (start["executionId"] in {r["executionId"] for r in starts}
            or start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest()
            or after["variables"].get("Lock6") != "1" or any(r.get("apiName") == "choose" for r in records) or "goto HaveClose" in source):
        raise AutomationError("Repeated B switch did not verify the inaccessible close-choice label")
    if high:
        write_json(output / "tower-C44-unreachable-proof.json", dict(status="unreachable-in-current-script", choiceSite=f"{path}:37",
                   sourceSha256=hashlib.sha256((resource / path).read_bytes()).hexdigest(), records=records,
                   alreadyOpenBranch="Lock6 == 1 goes to L_end", orphanLabel="HaveClose", choicePassed=False, cheatAssisted=False))
    for option in (1, 0):
        load_checkpoint(client, 3)
        client.act("SetAutoDialogue", enabled=True)
        tower_transition(client, output, resource, state["map"], reset_trap, choice=(reset_site, option))
        after = checkpoint(client, output, f"513-ending-one-tower-{reset}-reset-{option}", variables=locks)
        if any(after["variables"].get(f"Lock{n}") != ("1" if option == 1 else "0") for n in range(1, 7)):
            raise AutomationError(f"Native {reset} switch did not retain or reset the switches")
        client.save_or_load(4 if option == 1 else 5)
    load_checkpoint(client, 2)
    client.act("SetAutoDialogue", enabled=True)
    tower_transition(client, output, resource, state["map"], reset_trap, choice=(reset_site, 0))
    after = checkpoint(client, output, f"514-ending-one-tower-{reset}-reset-with-{real}-closed", variables=locks)
    if any(after["variables"].get(f"Lock{n}") != "0" for n in (1, 2, 3, 4, 5, 7, 8)) or after["variables"].get("Lock6") == "1":
        raise AutomationError("Native A reset with B closed did not clear the other seven switches")
    load_checkpoint(client, 3)
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(6)
    checkpoint(client, output, "515-ending-one-tower-seven-source-slot6", variables=locks)


def ending_one_tower_entry(client, output, resource):
    state = idle(client)
    source_file = output / "500-ending-one-tower-pursuit-source-slot0.json"
    if state["variables"].get("Result") != "1" or state["variables"].get("Event") not in ("3175", "3180", "3182"):
        raise AutomationError("Tower pursuit requires the ordinary Wudang and disguised-Moon battles")
    if state["map"] == "map_030_悲魔山庄.map":
        client.save_or_load(0)
        checkpoint(client, output, "500-ending-one-tower-pursuit-source-slot0")
        recover(client, output, resource)
    elif not source_file.is_file():
        raise AutomationError("Tower pursuit lacks its recorded ordinary route source")
    for origin, destination, trap in (("map_030_悲魔山庄.map", "map_036_连接地图.map", 6),
            ("map_036_连接地图.map", "map_037_敦煌十洞.map", 2),
            ("map_037_敦煌十洞.map", "map_038_连接地图.map", 1),
            ("map_038_连接地图.map", "map_039_飞龙堡.map", 2)):
        if idle(client)["map"] == origin:
            if origin == "map_037_敦煌十洞.map":
                for portal in (3, 4):
                    current = idle(client)
                    try:
                        reachable_trap(resource, origin, portal, current["player"]["position"])
                    except AutomationError as error:
                        if not str(error).startswith(f"No connected trap {portal}:"):
                            raise
                        continue
                    transition(client, resource, origin, portal)
            state = transition(client, resource, destination, trap, running=True)
    state = idle(client)
    if state["map"] == "map_039_飞龙堡.map":
        for _ in range(5):
            if state["variables"].get("Event") != "3175":
                break
            state = transition(client, resource, state["map"], 8)
        if state["variables"].get("Event") == "3180":
            client.move(42, 27)
            state = interact_named(client, output, resource, "大门守卫")
        if state["variables"].get("Event") != "3182":
            raise AutomationError("Native Moon visit and guard dialogue did not release the tower")
        checkpoint(client, output, "501-ending-one-fortress-guard-complete")
        state = transition(client, resource, "map_040_沙漠.map", 2)
    if state["map"] == "map_040_沙漠.map":
        state = transition(client, resource, "map_041_通天塔一层.map", 1)
    floors = ("map_041_通天塔一层.map", "map_042_通天塔二层.map", "map_043_通天塔第三层.map",
              "map_044_通天塔四层.map", "map_045_通天塔第五层.map", "map_046_通天塔第六层.map")
    for number, map_name in enumerate(floors[:-1], 1):
        if state["map"] != map_name:
            continue
        if number == 5:
            fight_hostiles(client, output, resource, magic_file="player-magic-魂牵梦绕.ini")
            recover(client, output, resource)
        state = client.observe((*VARIABLES, f"Lock{number}"))
        for _ in range(5):
            if state["variables"].get(f"Lock{number}") == "1":
                break
            state = tower_transition(client, output, resource, map_name, 8)
            state = client.observe((*VARIABLES, f"Lock{number}"))
            if state["variables"].get(f"Lock{number}") == "1":
                break
        if state["variables"].get(f"Lock{number}") != "1":
            raise AutomationError(f"Native tower floor {number} switch did not open")
        checkpoint(client, output, f"502-ending-one-tower-floor-{number}-opened", variables=(f"Lock{number}",))
        state = tower_transition(client, output, resource, floors[number], 2)
    source = json.loads(source_file.read_text(encoding="utf-8"))
    state = checkpoint(client, output, "503-ending-one-tower-six-arrival", variables=tuple(f"Lock{n}" for n in range(1, 9)))
    if state["map"] != floors[-1] or state["variables"].get("Event") != "3182" or any(
            state["variables"].get(f"Lock{n}") != "1" for n in range(1, 6)) or any(
            state["variables"].get(key) != source["variables"].get(key) for key in ("Result", "SenseVal", "EvilVal", "EvilValue")):
        raise AutomationError("Native tower switches did not reach the sixth-floor branch")
    client.save_or_load(6)
    checkpoint(client, output, "504-ending-one-tower-six-choice-source-slot6", variables=tuple(f"Lock{n}" for n in range(1, 9)))


def ending_one_wudang_attack(client, output, resource):
    state = idle(client)
    source_file = output / "490-ending-one-wudang-attack-source-slot0.json"
    if state["variables"].get("Result") != "1":
        raise AutomationError("Wudang attack requires the ordinary Meng-battle aftermath")
    if state["map"] == "map_030_悲魔山庄.map" and state["variables"].get("Event") == "3145":
        client.save_or_load(0)
        source = checkpoint(client, output, "490-ending-one-wudang-attack-source-slot0")
        choice_path = "script/map/map_030_悲魔山庄/月眉儿对话.txt:12"
        state = interact_named(client, output, resource, "月眉儿", choice=(choice_path, 1))
        if state["variables"].get("Event") != "3145" or any(state["variables"].get(key) != source["variables"].get(key)
                for key in ("Result", "SenseVal", "EvilVal", "EvilValue")):
            raise AutomationError("Native Wudang postponement did not retain the story state")
        client.save_or_load(1)
        checkpoint(client, output, "491-ending-one-wudang-postponed-slot1")
        state = interact_named(client, output, resource, "月眉儿", choice=(choice_path, 0))
        if state["map"] != "map_006_武当山山顶.map" or state["variables"].get("Event") != "3150":
            raise AutomationError("Native Wudang agreement did not start its battle")
        client.save_or_load(2)
        checkpoint(client, output, "492-ending-one-wudang-battle-source-slot2", variables=("NpcCount",))
    elif not source_file.is_file() or (state["map"], state["variables"].get("Event")) not in (
            ("map_006_武当山山顶.map", "3150"), ("map_030_悲魔山庄.map", "3175")):
        raise AutomationError("Wudang attack lacks its recorded ordinary battle source")
    fight_hostiles(client, output, resource, magic_file="player-magic-魂牵梦绕.ini")
    state = checkpoint(client, output, "493-ending-one-wudang-cleared", variables=("NpcCount",))
    source = json.loads(source_file.read_text(encoding="utf-8"))
    if state["map"] != "map_030_悲魔山庄.map" or state["variables"].get("Event") != "3175" or state["variables"].get("NpcCount") != "0" or any(
            state["variables"].get(key) != source["variables"].get(key) for key in ("Result", "SenseVal", "EvilVal", "EvilValue")) or not any(
            item["file"] == "player-magic-武道德经.ini" for item in state["magic"]):
        raise AutomationError("Native Wudang battle and secret martial training did not finish")
    records = {}
    for path in ("script/map/map_006_武当山山顶/死亡.txt", "script/map/map_030_悲魔山庄/月眉儿死亡.txt"):
        start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
        if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Wudang and disguised-Moon battles used a different native source")
        records[path] = completed_script(output, start)
    write_json(output / "ending-one-wudang-attack-proof.json", dict(records=records, cheatAssisted=False))
    client.save_or_load(6)
    checkpoint(client, output, "494-ending-one-tower-pursuit-source-slot6")


def ending_one_meng_infiltration(client, output, resource):
    state = idle(client)
    source_file = output / "470-ending-one-meng-infiltration-source-slot0.json"
    if state["variables"].get("Result") != "1":
        raise AutomationError("Meng infiltration requires the ordinary Moon-room aftermath")
    if state["map"] == "map_030_悲魔山庄.map" and state["variables"].get("Event") == "3120":
        client.save_or_load(0)
        checkpoint(client, output, "470-ending-one-meng-infiltration-source-slot0")
        for destination, trap in (("map_065_天山古道.map", 1), ("map_031_连接地图.map", 1),
                                  ("map_032_天山.map", 1), ("map_033_落叶谷.map", 1)):
            state = transition(client, resource, destination, trap, running=True)
        state = interact_named(client, output, resource, "家丁阿甘")
    elif state["map"] != "map_033_落叶谷.map" or not source_file.is_file():
        raise AutomationError("Meng infiltration lacks its recorded ordinary journey")
    if state["variables"].get("Event") == "3122":
        state = interact_named(client, output, resource, "孟知秋")
        if state["variables"].get("Event") != "3130" or not any(item["file"] == "player-magic-云生结海.ini" for item in state["magic"]):
            raise AutomationError("Native Meng lesson did not release the night infiltration")
        client.save_or_load(1)
        checkpoint(client, output, "471-ending-one-meng-ambush-source-slot1")
    if state["variables"].get("Event") == "3130":
        for _ in range(5):
            state = transition(client, resource, "map_033_落叶谷.map", 7)
            if state["variables"].get("Event") != "3130":
                break
        client.save_or_load(2)
        checkpoint(client, output, "472-ending-one-meng-battle-source-slot2")
    if state["variables"].get("Event") != "3140":
        raise AutomationError("Native Meng ambush did not start its ordinary battle")
    state = fight_named(client, output, resource, "孟知秋", timeout=360,
                        use_magic=True, single_target=True, magic_file="player-magic-魂牵梦绕.ini")
    source = json.loads(source_file.read_text(encoding="utf-8"))
    if state["map"] != "map_030_悲魔山庄.map" or state["variables"].get("Event") != "3145" or any(
            state["variables"].get(key) != source["variables"].get(key) for key in ("Result", "SenseVal", "EvilVal", "EvilValue")):
        raise AutomationError("Native Meng death did not release Wudang preparations")
    path = "script/map/map_033_落叶谷/孟知秋死亡.txt"
    start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
    if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
        raise AutomationError("Meng battle used a different native source")
    write_json(output / "ending-one-meng-battle-proof.json", dict(records=completed_script(output, start), cheatAssisted=False))
    client.save_or_load(6)
    checkpoint(client, output, "473-ending-one-wudang-attack-source-slot6")


def ending_one_moon_room(client, output, resource):
    state = idle(client)
    source_file = output / "460-ending-one-moon-room-route-source-slot0.json"
    if state["variables"].get("Result") != "1":
        raise AutomationError("Moon's room choice requires the ordinary island-raider aftermath")
    if state["map"] == "map_029_码头.map" and state["variables"].get("Event") == "3100":
        client.save_or_load(0)
        checkpoint(client, output, "460-ending-one-moon-room-route-source-slot0")
        transition(client, resource, "map_028_连接地图.map", 1)
        state = transition(client, resource, "map_030_悲魔山庄.map", 2)
        for _ in range(5):
            if state["variables"].get("Event") != "3100":
                break
            state = transition(client, resource, "map_030_悲魔山庄.map", 15)
    elif state["map"] != "map_030_悲魔山庄.map" or not source_file.is_file():
        raise AutomationError("Moon's room choice lacks its recorded ordinary return")
    if state["variables"].get("Event") != "3110":
        raise AutomationError("Native steward message and night story did not complete")
    client.save_or_load(1)
    source = checkpoint(client, output, "461-ending-one-moon-room-choice-source-slot1")
    for option in (1, 0):
        load_checkpoint(client, 1)
        client.act("SetAutoDialogue", enabled=True)
        state = transition(client, resource, "map_030_悲魔山庄.map", 16,
                           choice=("script/map/map_030_悲魔山庄/trap16.txt:73", option), output=output)
        if (state["variables"].get("Event") != "3120"
                or state["variables"].get("EvilValue") != str(int(source["variables"]["EvilValue"]) + (5 if option == 0 else -5))
                or any(state["variables"].get(key) != source["variables"].get(key) for key in ("Result", "SenseVal", "EvilVal"))):
            raise AutomationError("Native Moon room choice and next-day story did not complete")
        client.save_or_load(option + 2)
        checkpoint(client, output, f"462-ending-one-moon-room-{option}-slot{option + 2}")
    client.save_or_load(6)
    checkpoint(client, output, "463-ending-one-meng-infiltration-source-slot6")


def ending_one_island_raiders(client, output, resource, spare=False):
    state = idle(client)
    source_file = output / "410-ending-one-island-return-source-slot0.json"
    if state["variables"].get("Result") != "1" or state["variables"].get("Event") != "3060":
        raise AutomationError("Island raiders require an ordinary accepted Moon offer")
    if state["map"] == "map_062_禁地密室.map":
        client.save_or_load(0)
        checkpoint(client, output, "410-ending-one-island-return-source-slot0")
        for destination in ("map_061_禁地三层.map", "map_060_禁地二层.map",
                            "map_059_禁地一层.map", "map_058_禁地.map"):
            transition(client, resource, destination, 1)
        state = idle(client)
        occupied = {(target["position"]["x"], target["position"]["y"]) for target in state["targets"]}
        x, y = reachable_trap(resource, state["map"], 1, state["player"]["position"], occupied)
        action = client.submit("MoveTo", generation=state["generation"], x=x, y=y,
                               running=True, timeoutMs=180000)
        client.wait_until(lambda value: value.get("generation") != state["generation"], timeout=90,
                          variables=VARIABLES, description="native parallel tower battle")
        if client.request("GetActionStatus", actionId=action)["status"] == "running":
            client.request("CancelAction", actionId=action)
        state = client.observe(VARIABLES)
    elif state["map"] not in ("map_025_摘星楼.map", "map_057_连接地图.map", "map_050_忘忧岛.map") or not source_file.is_file():
        raise AutomationError("Island raiders lack their recorded ordinary journey")
    if state["map"] in ("map_025_摘星楼.map", "map_057_连接地图.map"):
        state = client.wait_until(lambda value: value.get("map") == "map_057_连接地图.map" and value.get("worldInput"),
                                  timeout=3600, variables=VARIABLES, description="native Nalan-Meng battle aftermath")
        path = "script/map/map_025_摘星楼/纳兰潜凛死亡.txt"
        start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
        if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Parallel tower battle used a different native death script")
        write_json(output / "ending-one-parallel-battle-proof.json", dict(records=completed_script(output, start), cheatAssisted=False))
        recover(client, output, resource)
        state = transition(client, resource, "map_050_忘忧岛.map", 1)
        client.save_or_load(1)
        checkpoint(client, output, "411-ending-one-island-raiders-source-slot1", variables=("NpcCount",))
    if state["map"] != "map_050_忘忧岛.map" or not (output / "411-ending-one-island-raiders-source-slot1.json").is_file():
        raise AutomationError("Island raiders lack their recorded ordinary journey")
    source = json.loads(source_file.read_text(encoding="utf-8"))
    choice = ("script/map/map_050_忘忧岛/死亡.txt:135", int(spare))
    fight_hostiles(client, output, resource, choice=choice, magic_file="player-magic-魂牵梦绕.ini")
    state = idle(client, choice=choice, output=output, resource=resource)
    if (state["map"] != "map_029_码头.map" or state["variables"].get("Event") != "3100"
            or state["variables"].get("EvilValue") != str(int(source["variables"]["EvilValue"]) + (-10 if spare else 10))
            or not any(target["name"] == "紫轩尸体" for target in state["targets"])):
        raise AutomationError("Native raider battle and Zixuan choice did not complete")
    path = "script/map/map_050_忘忧岛/死亡.txt"
    starts = [record for record in trace_records(output) if record.get("eventType") == "script.start" and record.get("virtualPath") == path]
    if any(start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest() for start in starts):
        raise AutomationError("Raider battle used a different native death script")
    write_json(output / "ending-one-island-raiders-proof.json", dict(records=[completed_script(output, start) for start in starts],
               option=int(spare), cheatAssisted=False, storyEnding=False))
    client.save_or_load(6)
    checkpoint(client, output, "412-ending-one-zixuan-aftermath-slot6", variables=("NpcCount", "KillZX"))


def ending_one_moon_choices(client, output, resource, reject=False):
    state = idle(client)
    if state["map"] != "map_062_禁地密室.map" or state["variables"].get("Event") != "3050" or state["variables"].get("Result") != "1":
        raise AutomationError("Moon's late offers require the ordinary secret-chamber reveal")
    client.save_or_load(0)
    source = checkpoint(client, output, "390-ending-one-moon-offer-source-slot0")
    path = "script/map/map_062_禁地密室/杨影枫死亡.txt"
    for offer in ((2,) if reject else (0, 1)):
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        state = client.observe(VARIABLES)
        occupied = {(target["position"]["x"], target["position"]["y"]) for target in state["targets"]}
        x, y = reachable_trap(resource, state["map"], 4, state["player"]["position"], occupied)
        action = client.submit("MoveTo", generation=state["generation"], x=x, y=y,
                               running=True, timeoutMs=90000)
        client.wait_until(lambda value: value.get("inEvent") or value.get("choices")
                          or any(target["name"] == "月眉儿" and target.get("hostile") for target in value.get("targets", [])),
                          timeout=30, variables=VARIABLES, description="native Moon confrontation")
        if client.request("GetActionStatus", actionId=action)["status"] == "running":
            client.request("CancelAction", actionId=action)
        client.wait_until(lambda value: bool(value.get("choices")), timeout=90,
                          variables=VARIABLES, description="native Moon defeat and late offers")
        choices = [(f"{path}:{line}", 1) for line in (44, 50)[:offer]]
        if not reject:
            choices.append((f"{path}:{(44, 50)[offer]}", 0))
        state = idle(client, choice=choices, output=output, resource=resource,
                     expected_terminal="script/common/主角死亡.txt" if reject else None)
        if reject:
            checkpoint(client, output, "392-ending-one-moon-refusal-title")
            return
        if (state["variables"].get("Event") != "3060"
                or state["variables"].get("EvilValue") != str(int(source["variables"]["EvilValue"]) + (10, 5)[offer])):
            raise AutomationError("Native Moon offer did not preserve its accepted aftermath")
        client.save_or_load(offer + 1)
        checkpoint(client, output, f"391-ending-one-moon-offer-{offer}")
    load_checkpoint(client, 1)
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(6)
    checkpoint(client, output, "393-ending-one-moon-accepted-slot6")


def ending_one_forget_worry(client, output, resource):
    state = idle(client)
    source_file = output / "370-ending-one-island-pursuit-source-slot0.json"
    if state["variables"].get("Result") != "1":
        raise AutomationError("Late island pursuit requires the ordinary captive aftermath")
    if state["map"] == "map_025_摘星楼.map" and state["variables"].get("Event") == "3010":
        client.save_or_load(0)
        source = checkpoint(client, output, "370-ending-one-island-pursuit-source-slot0")
        state = interact_named(client, output, resource, "纳兰真")
        if state["map"] != "map_024_倚天山.map" or state["variables"].get("Event") != "3020":
            raise AutomationError("Native Nalan Zhen night dialogue did not start the island journey")
        checkpoint(client, output, "371-ending-one-island-journey")
        for destination, trap in (("map_023_连接地图.map", 3), ("map_017_连接地图.map", 2),
                                  ("map_014_连接地图.map", 1), ("map_012_惠安镇.map", 1),
                                  ("map_027_连接地图.map", 3), ("map_028_连接地图.map", 1),
                                  ("map_029_码头.map", 1)):
            transition(client, resource, destination, trap)
        state = interact_named(client, output, resource, "渔夫窦昊")
        if state["map"] != "map_051_海边.map" or state["variables"].get("Event") != "3030":
            raise AutomationError("Native voyage and masked follower story did not complete")
        client.save_or_load(1)
        checkpoint(client, output, "372-ending-one-masked-follower-slot1")
        state = transition(client, resource, "map_058_禁地.map", 1)
    elif ((state["map"], state["variables"].get("Event")) in
            (("map_058_禁地.map", "3030"), ("map_061_禁地三层.map", "3040"))
            and source_file.is_file() and (output / "372-ending-one-masked-follower-slot1.json").is_file()):
        source = json.loads(source_file.read_text(encoding="utf-8"))
    else:
        raise AutomationError("Late island pursuit lacks its recorded native voyage")
    if state["map"] != "map_061_禁地三层.map":
        for destination, trap in (("map_059_禁地一层.map", 2), ("map_060_禁地二层.map", 2),
                                  ("map_061_禁地三层.map", 2)):
            state = transition(client, resource, destination, trap)
    if state["variables"].get("Event") != "3040":
        raise AutomationError("Native masked follower did not open the third-layer passage")
    state = transition(client, resource, "map_062_禁地密室.map", 2)
    if (state["variables"].get("Event") != "3050" or state["variables"].get("EvilValue") != source["variables"].get("EvilValue")
            or not any(target["name"] == "月眉儿" for target in state["targets"])):
        raise AutomationError("Native secret chamber reveal did not preserve its ordinary choice source")
    client.save_or_load(6)
    checkpoint(client, output, "373-ending-one-moon-challenge-source-slot6")


def ending_one_captive_reject(client, output, resource):
    state = idle(client)
    if state["map"] != "map_025_摘星楼.map" or state["variables"].get("Event") != "3005" or state["variables"].get("Result") != "1":
        raise AutomationError("Final captive refusal requires the ordinary Nalan battle source")
    checkpoint(client, output, "362-ending-one-captive-refusal-source")
    action = client.submit("MoveTo", generation=state["generation"], x=20, y=66,
                           running=True, timeoutMs=90000)
    client.wait_until(lambda value: bool(value.get("choices")), timeout=90,
                      variables=VARIABLES, description="native captive refusal source")
    if client.request("GetActionStatus", actionId=action)["status"] == "running":
        client.request("CancelAction", actionId=action)
    path = "script/map/map_025_摘星楼/杨影枫死亡.txt"
    state = idle(client, choice=[(f"{path}:{line}", 1) for line in (53, 59, 63)],
                 output=output, resource=resource, expected_terminal="script/common/主角死亡.txt")
    if state["scene"] != "Title":
        raise AutomationError("Final captive refusal did not complete its expected native death")
    checkpoint(client, output, "363-ending-one-captive-refusal-title")


def ending_one_captive_choices(client, output, resource, win=False):
    state = idle(client)
    source_file = output / "360-ending-one-captive-choice-source-slot0.json"
    if state["map"] != "map_025_摘星楼.map" or state["variables"].get("Result") != "1":
        raise AutomationError("Captive choices require the ordinary ending-one tower battle")
    if state["variables"].get("Event") == "3005":
        client.save_or_load(0)
        checkpoint(client, output, "360-ending-one-captive-choice-source-slot0")
    elif state["variables"].get("Event") != "3010" or not source_file.is_file():
        raise AutomationError("Captive choices lack their recorded normal battle source")
    path = "script/map/map_025_摘星楼/杨影枫死亡.txt"
    for offer in ((0,) if win else range(3)):
        for kill in ((1,) if win else (0, 1)):
            name = f"361-ending-one-captive-offer-{offer}-kill-{kill}"
            if (output / f"{name}.json").is_file():
                continue
            load_checkpoint(client, 0)
            client.act("SetAutoDialogue", enabled=True)
            state = client.observe(VARIABLES)
            choices = [(f"{path}:{line}", 1) for line in (53, 59, 63)[:offer]]
            choices += [(f"{path}:{(53, 59, 63)[offer]}", 0), (f"{path}:115", kill)]
            if win:
                fight_hostiles(client, output, resource, choice=choices, magic_file="player-magic-魂牵梦绕.ini")
            elif not state.get("choices"):
                target = next(target for target in state["targets"] if target["name"] == "纳兰潜凛" and target.get("hostile"))
                action = client.submit("MoveTo", generation=state["generation"], x=target["position"]["x"], y=target["position"]["y"] + 1,
                                       running=True, timeoutMs=90000)
                client.wait_until(lambda value: bool(value.get("choices")), timeout=90,
                                  variables=VARIABLES, description="native Nalan defeat and captive choice")
                if client.request("GetActionStatus", actionId=action)["status"] == "running":
                    client.request("CancelAction", actionId=action)
            state = idle(client, choice=choices, output=output, resource=resource)
            expected_evil = (110, 105, 95)[offer] + (15 if kill == 0 else -10)
            if (state["map"] != "map_025_摘星楼.map" or state["variables"].get("Event") != "3010"
                    or state["variables"].get("EvilValue") != str(expected_evil)
                    or client.observe(("KillQW1",))["variables"].get("KillQW1") != str(kill)):
                raise AutomationError("Captive offer or Qiang Wei choice did not complete its native aftermath")
            client.save_or_load(offer * 2 + kill + 1)
            checkpoint(client, output, name, variables=("KillQW1",))
    if win:
        death_path = "script/map/map_025_摘星楼/摘星楼弟子死亡1.txt"
        start = next((record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start"
                      and record.get("virtualPath") == death_path), None)
        if start is None:
            raise AutomationError("Captive battle lacks the native enemy-count completion")
        records = completed_script(output, start)
        if (start["contentSha256"] != hashlib.sha256((resource / death_path).read_bytes()).hexdigest()
                or not any(r.get("variableName") == "NpcCount" and r.get("afterValue") == "0" for r in records)
                or not any(r.get("apiName") == "runscript" for r in records)):
            raise AutomationError("Captive battle ended without the current native all-enemies-defeated path")
        write_json(output / "ending-one-captive-win-proof.json", dict(records=records, narrative="native forced defeat and surrender offers",
                   cheatAssisted=False, storyEnding=False))


def ending_one_captive_source(client, output, resource):
    state = idle(client)
    source_file = output / "350-ending-one-captive-route-source-slot0.json"
    if state["variables"].get("Event") != "600" or state["variables"].get("Result") != "1":
        raise AutomationError("Captive rescue requires the ordinary moonlit duel aftermath")
    if state["map"] == "map_019_寒波谷.map":
        client.save_or_load(0)
        checkpoint(client, output, "350-ending-one-captive-route-source-slot0")
        for destination, trap in (("map_018_连接地图.map", 1), ("map_017_连接地图.map", 1),
                                  ("map_014_连接地图.map", 1), ("map_012_惠安镇.map", 1),
                                  ("map_027_连接地图.map", 3), ("map_028_连接地图.map", 1),
                                  ("map_030_悲魔山庄.map", 2)):
            transition(client, resource, destination, trap, running=True)
    elif state["map"] != "map_030_悲魔山庄.map" or not source_file.is_file():
        raise AutomationError("Captive rescue lacks its recorded ordinary source")
    client.move(48, 155)
    state = interact_named(client, output, resource, "蔷薇")
    if state["variables"].get("Event") != "3000":
        raise AutomationError("Native Nalan message did not start Qiang Wei's captive rescue")
    client.save_or_load(1)
    checkpoint(client, output, "351-ending-one-captive-message-slot1")
    transition(client, resource, "map_012_惠安镇.map", 5)
    state = interact_named(client, output, resource, "铁中雷")
    if state["variables"].get("Event") != "3000":
        raise AutomationError("Ordinary tower directions changed the captive plot")
    for destination, trap in (("map_014_连接地图.map", 2), ("map_017_连接地图.map", 3),
                              ("map_023_连接地图.map", 3), ("map_024_倚天山.map", 1),
                              ("map_025_摘星楼.map", 2)):
        transition(client, resource, destination, trap, running=True)
    state = idle(client)
    if state["variables"].get("Event") != "3005" or not any(target["name"] == "纳兰潜凛" and target.get("hostile") and npc_attackable(target) for target in state["targets"]):
        raise AutomationError("Native tower rescue did not release the Nalan battle")
    client.save_or_load(6)
    checkpoint(client, output, "352-ending-one-nalan-battle-source-slot6", variables=("NpcCount",))


def ending_one_zixuan_duel(client, output, resource):
    state = idle(client)
    if state["map"] != "map_033_落叶谷.map" or state["variables"].get("Event") != "592" or state["variables"].get("Result") != "1":
        raise AutomationError("Zixuan's moonlit duel requires Meng's normal martial lesson")
    client.save_or_load(0)
    checkpoint(client, output, "340-ending-one-zixuan-route-source-slot0")
    for destination, trap in (("map_032_天山.map", 1), ("map_031_连接地图.map", 2),
                              ("map_065_天山古道.map", 2), ("map_030_悲魔山庄.map", 2),
                              ("map_028_连接地图.map", 5), ("map_027_连接地图.map", 3),
                              ("map_012_惠安镇.map", 2), ("map_014_连接地图.map", 2),
                              ("map_017_连接地图.map", 3), ("map_018_连接地图.map", 2),
                              ("map_019_寒波谷.map", 2)):
        transition(client, resource, destination, trap, running=True)
    for _ in range(5):
        state = transition(client, resource, "map_019_寒波谷.map", 7)
        if state["variables"].get("Event") != "592":
            break
    if state["variables"].get("Event") != "595":
        raise AutomationError("Native moonlit confrontation did not start Zixuan's duel")
    client.save_or_load(1)
    source = checkpoint(client, output, "341-ending-one-zixuan-duel-source-slot1")
    for option in (1, 0):
        load_checkpoint(client, 1)
        client.act("SetAutoDialogue", enabled=True)
        state = fight_named(client, output, resource, "紫轩", use_magic=True,
                            choice=("script/map/map_019_寒波谷/紫轩死亡.txt:21", option))
        if state["map"] != "map_019_寒波谷.map" or state["variables"].get("Event") != "600" or any(state["variables"].get(key) != source["variables"].get(key) for key in ("SenseVal", "EvilVal", "Result")):
            raise AutomationError("Native Zixuan forgiveness and dream did not complete")
        client.save_or_load(option + 2)
        checkpoint(client, output, f"342-ending-one-zixuan-dream-{option}-slot{option + 2}")
    load_checkpoint(client, 2)
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(6)
    checkpoint(client, output, "343-ending-one-zixuan-return-source-slot6")


def ending_one_wedding_night(client, output, resource):
    state = idle(client)
    if state["map"] != "map_030_悲魔山庄.map" or state["variables"].get("Event") != "576" or state["variables"].get("Result") != "1":
        raise AutomationError("Wedding night requires the normal completed marriage")
    client.save_or_load(0)
    source = checkpoint(client, output, "305-ending-one-wedding-night-source-slot0")
    for option in (1, 0):
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        state = transition(client, resource, "map_030_悲魔山庄.map", 14, output=output,
                           choice=("script/map/map_030_悲魔山庄/trap14.txt:33", option))
        if state["variables"].get("Event") != "578" or any(state["variables"].get(key) != source["variables"].get(key) for key in ("SenseVal", "EvilVal", "Result")):
            raise AutomationError("Native wedding pursuit did not complete its unchanged story state")
        checkpoint(client, output, f"306-ending-one-wedding-pursuit-{option}")
        state = interact_named(client, output, resource, "蔷薇")
        if state["variables"].get("Event") != "580" or state["variables"].get("Result") != "1":
            raise AutomationError("Native wedding night did not reach the three-month aftermath")
        client.save_or_load(option + 1)
        checkpoint(client, output, f"307-ending-one-married-aftermath-{option}-slot{option + 1}")
    load_checkpoint(client, 1)
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(6)
    checkpoint(client, output, "308-ending-one-aftermath-source-slot6")


def ending_two_wudang_truth(client, output, resource):
    state = idle(client)
    source_file = output / "480-ending-two-wudang-truth-source-slot0.json"
    if state["variables"].get("Result") != "2":
        raise AutomationError("Wudang testimony requires the ordinary injured-Zhen rescue")
    if state["map"] == "map_051_海边.map" and state["variables"].get("Event") == "1818":
        client.save_or_load(0)
        checkpoint(client, output, "480-ending-two-wudang-truth-source-slot0")
        transition(client, resource, "map_050_忘忧岛.map", 1)
        transition(client, resource, "map_052_码头.map", 3)
        checkpoint(client, output, "480a-ending-two-dock-before-talk")
        state = interact_named(client, output, resource, "渔夫窦昊")
        if state["map"] != "map_029_码头.map":
            raise AutomationError("Native boat did not reach Huian dock")
        checkpoint(client, output, "480b-ending-two-native-boat-arrival")
        for destination, trap in (("map_028_连接地图.map", 1), ("map_030_悲魔山庄.map", 2),
                                  ("map_065_天山古道.map", 1), ("map_031_连接地图.map", 1),
                                  ("map_032_天山.map", 1), ("map_033_落叶谷.map", 1)):
            if state["map"] == "map_030_悲魔山庄.map":
                state = interact_named(client, output, resource, "管家铁云")
                checkpoint(client, output, "480c-ending-two-steward-opens-north-exit")
            state = transition(client, resource, destination, trap, running=True)
        for _ in range(5):
            if state["variables"].get("Event") != "1818":
                break
            state = transition(client, resource, "map_033_落叶谷.map", 19)
        if state["variables"].get("Event") != "1819":
            raise AutomationError("Native Meng accusation did not release Wudang testimony")
        client.save_or_load(1)
        checkpoint(client, output, "481-ending-two-meng-testimony-slot1")
    elif state["map"] not in ("map_033_落叶谷.map", "map_006_武当山山顶.map") or not source_file.is_file():
        raise AutomationError("Wudang testimony lacks its recorded ordinary journey")
    if state["map"] == "map_033_落叶谷.map" and state["variables"].get("Event") == "1819":
        for destination, trap in (("map_032_天山.map", 1), ("map_031_连接地图.map", 2),
                                  ("map_065_天山古道.map", 2), ("map_030_悲魔山庄.map", 2),
                                  ("map_028_连接地图.map", 5), ("map_027_连接地图.map", 3),
                                  ("map_012_惠安镇.map", 2), ("map_011_连接地图.map", 1),
                                  ("map_008_野树林.map", 1), ("map_007_连接地图.map", 3),
                                  ("map_003_武当山下.map", 1), ("map_005_洗剑池.map", 2),
                                  ("map_004_武当山连接地图.map", 2), ("map_006_武当山山顶.map", 1)):
            state = transition(client, resource, destination, trap, running=True)
    if state["map"] != "map_006_武当山山顶.map" or state["variables"].get("Event") != "1819":
        raise AutomationError("Wudang testimony did not reach Tian Xing")
    state = interact_named(client, output, resource, "天星道长")
    if state["variables"].get("Event") != "1820":
        raise AutomationError("Native Tian Xing testimony did not finish")
    client.save_or_load(2)
    checkpoint(client, output, "482-ending-two-wudang-testimony-slot2")
    for destination, trap in (("map_004_武当山连接地图.map", 1), ("map_005_洗剑池.map", 2),
                              ("map_003_武当山下.map", 1), ("map_007_连接地图.map", 4),
                              ("map_008_野树林.map", 2), ("map_011_连接地图.map", 4),
                              ("map_012_惠安镇.map", 2)):
        state = transition(client, resource, destination, trap, running=True)
    source = json.loads(source_file.read_text(encoding="utf-8"))
    if state["variables"].get("Event") != "1821" or any(state["variables"].get(key) != source["variables"].get(key)
            for key in ("Result", "SenseVal", "EvilVal")):
        raise AutomationError("Native Zixuan meeting and Zhang confession did not finish")
    client.save_or_load(6)
    checkpoint(client, output, "483-ending-two-island-sisters-search-source-slot6")


def ending_two_island_search(client, output, resource):
    source = idle(client)
    if (source["map"] != "map_012_惠安镇.map" or source["variables"].get("Event") != "1821"
            or source["variables"].get("Result") != "2"):
        raise AutomationError("Island search requires the native Wudang testimony Event 1821 source")
    earlier = {record["executionId"] for record in trace_records(output) if record.get("eventType") == "script.start"}
    client.save_or_load(0)
    checkpoint(client, output, "490-ending-two-island-search-source-slot0")
    for destination, trap in (("map_027_连接地图.map", 3), ("map_028_连接地图.map", 1),
                              ("map_029_码头.map", 1)):
        transition(client, resource, destination, trap, running=True)
    state = interact_named(client, output, resource, "渔夫窦昊")
    if state["map"] != "map_052_码头.map" or state["variables"].get("Event") != "1821":
        raise AutomationError("Native voyage did not return to the island for the sisters")
    transition(client, resource, "map_050_忘忧岛.map", 1)
    transition(client, resource, "map_051_海边.map", 1)
    state = transition(client, resource, "map_051_海边.map", 3)
    if state["variables"].get("Event") != "1822":
        raise AutomationError("Native seaside disappearance did not release the forbidden-area search")
    for destination, trap in (("map_050_忘忧岛.map", 1), ("map_057_连接地图.map", 4),
                              ("map_058_禁地.map", 2)):
        transition(client, resource, destination, trap, running=True)
    checkpoint(client, output, "491-ending-two-forbidden-battle", variables=("NpcCount",))
    fight_hostiles(client, output, resource)
    state = checkpoint(client, output, "492-ending-two-forbidden-all-clear", variables=("NpcCount",))
    if (state["map"] != "map_058_禁地.map" or state["variables"].get("Event") != "1822"
            or state["variables"].get("NpcCount") != "0" or state["player"]["life"] <= 0):
        raise AutomationError("Forbidden-area entry did not clear enemies through native combat")
    for destination in ("map_059_禁地一层.map", "map_060_禁地二层.map", "map_061_禁地三层.map",
                        "map_062_禁地密室.map"):
        transition(client, resource, destination, 2, running=True)
    state = transition(client, resource, "map_062_禁地密室.map", 9)
    if state["variables"].get("Event") != "1823" or any(state["variables"].get(key) != source["variables"].get(key)
            for key in ("Result", "SenseVal", "EvilVal")):
        raise AutomationError("Native secret-chamber reunion did not complete")
    paths = ("script/map/map_051_海边/trap03.txt", "script/map/map_058_禁地/无忧教死亡.txt",
             "script/map/map_062_禁地密室/trap09.txt")
    proof = []
    for path in paths:
        start = next((record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start"
                      and record["executionId"] not in earlier and record.get("virtualPath") == path), None)
        if start is None or start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Island search lacks its current native script evidence")
        records = completed_script(output, start)
        if path == paths[1] and not any(record.get("variableName") == "NpcCount" and record.get("afterValue") == "0" for record in records):
            raise AutomationError("Forbidden-area entry lacks the native all-clear death execution")
        proof.append(dict(start=start, records=records))
    write_json(output / "ending-two-island-search-proof.json", dict(scripts=proof, cheatAssisted=False, fullPlaythrough=False))
    client.save_or_load(6)
    checkpoint(client, output, "493-ending-two-ambush-source-slot6")


def ending_two_ambush(client, output, resource):
    source = idle(client)
    if (source["map"] != "map_062_禁地密室.map" or source["variables"].get("Event") != "1823"
            or source["variables"].get("Result") != "2"):
        raise AutomationError("Sisters' ambush requires the native secret-chamber reunion Event 1823 source")
    earlier = {record["executionId"] for record in trace_records(output) if record.get("eventType") == "script.start"}
    client.save_or_load(0)
    checkpoint(client, output, "500-ending-two-ambush-source-slot0")
    for destination in ("map_061_禁地三层.map", "map_060_禁地二层.map", "map_059_禁地一层.map", "map_058_禁地.map"):
        transition(client, resource, destination, 1, running=True)
    state = checkpoint(client, output, "501-ending-two-ambush-entry", variables=("NpcCount", "TongBanDeath"))
    if state["variables"].get("Event") == "1823":
        try:
            fight_hostiles(client, output, resource)
        except AutomationError as error:
            if str(error) not in ("StartCombat: world_changed", "StartCombat: target_unavailable"):
                raise
    state = idle(client)
    if (state["map"] != "map_058_禁地.map" or state["variables"].get("Event") != "2000"
            or state["player"]["life"] <= 0 or any(state["variables"].get(key) != source["variables"].get(key)
            for key in ("Result", "SenseVal", "EvilVal"))):
        raise AutomationError("Native ambush did not complete the sisters' capture and Meng referral")
    path = "script/map/map_058_禁地/同伴死亡.txt"
    start = next((record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start"
                  and record["executionId"] not in earlier and record.get("virtualPath") == path), None)
    if start is None or start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
        raise AutomationError("Sisters' capture lacks its current native companion script")
    records = completed_script(output, start)
    if not any(record.get("variableName") == "Event" and record.get("afterValue") == "2000" for record in records):
        raise AutomationError("Sisters' capture script did not assign the native continuation event")
    all_clear = []
    death_path = "script/map/map_058_禁地/无忧教死亡.txt"
    for death in trace_records(output):
        if death.get("eventType") != "script.start" or death["executionId"] in earlier or death.get("virtualPath") != death_path:
            continue
        if death["contentSha256"] != hashlib.sha256((resource / death_path).read_bytes()).hexdigest():
            raise AutomationError("Ambush combat used a different native death script")
        execution = completed_script(output, death)
        if any(record.get("variableName") == "NpcCount" and record.get("afterValue") == "0" for record in execution):
            all_clear.append(dict(start=death, records=execution))
    write_json(output / "ending-two-ambush-proof.json", dict(captureStart=start, captureRecords=records,
               route="all-enemies-cleared" if all_clear else "companion-loss", allClearExecutions=all_clear,
               cheatAssisted=False, fullPlaythrough=False))
    client.save_or_load(6)
    checkpoint(client, output, "502-ending-two-meng-help-source-slot6", variables=("NpcCount", "TongBanDeath"))


def ending_two_meng_help(client, output, resource):
    source = idle(client)
    if (source["map"] != "map_058_禁地.map" or source["variables"].get("Event") != "2000"
            or source["variables"].get("Result") != "2"):
        raise AutomationError("Meng's help requires the native sisters' capture Event 2000 source")
    earlier = {record["executionId"] for record in trace_records(output) if record.get("eventType") == "script.start"}
    client.save_or_load(0)
    checkpoint(client, output, "510-ending-two-meng-help-source-slot0")
    for destination, trap in (("map_057_连接地图.map", 1), ("map_050_忘忧岛.map", 1),
                              ("map_052_码头.map", 3)):
        transition(client, resource, destination, trap, running=True)
    checkpoint(client, output, "511-ending-two-second-dock-before-talk")
    state = interact_named(client, output, resource, "渔夫窦昊")
    if state["map"] != "map_029_码头.map" or state["variables"].get("Event") != "2000":
        raise AutomationError("Native second boat and steward warning did not finish")
    checkpoint(client, output, "512-ending-two-second-boat-arrival")
    transition(client, resource, "map_028_连接地图.map", 1, running=True)
    transition(client, resource, "map_030_悲魔山庄.map", 2, running=True)
    client.save_or_load(1)
    checkpoint(client, output, "513-ending-two-manor-battle-slot1", variables=("NpcCount",))
    fight_hostiles(client, output, resource)
    state = checkpoint(client, output, "514-ending-two-manor-cleared", variables=("NpcCount",))
    if (state["map"] != "map_030_悲魔山庄.map" or state["variables"].get("Event") != "2000"
            or state["variables"].get("NpcCount") != "0" or state["player"]["life"] <= 0):
        raise AutomationError("Native manor battle did not clear its enemies")
    for destination in ("map_065_天山古道.map", "map_031_连接地图.map", "map_032_天山.map",
                        "map_033_落叶谷(破坏后).map"):
        transition(client, resource, destination, 1, running=True)
    state = interact_named(client, output, resource, "孟知秋")
    if state["variables"].get("Event") != "2001" or any(state["variables"].get(key) != source["variables"].get(key)
            for key in ("Result", "SenseVal", "EvilVal")):
        raise AutomationError("Native Meng power transfer did not complete")
    checkpoint(client, output, "515-ending-two-meng-power-transfer")
    state = transition(client, resource, "map_032_天山.map", 1)
    if state["variables"].get("Event") != "2002":
        raise AutomationError("Native valley departure did not release the tower rescue")
    paths = ("script/map/map_052_码头/窦昊对话.txt", "script/map/map_030_悲魔山庄/无忧教死亡.txt",
             "script/map/map_033_落叶谷(破坏后)/孟知秋临终对话.txt")
    proof = []
    for path in paths:
        start = next((record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start"
                      and record["executionId"] not in earlier and record.get("virtualPath") == path), None)
        if start is None or start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Meng's help lacks its current native script evidence")
        records = completed_script(output, start)
        if path == paths[1] and not any(record.get("variableName") == "NpcCount" and record.get("afterValue") == "0" for record in records):
            raise AutomationError("Manor battle lacks the native all-clear death execution")
        if path == paths[2] and not any(record.get("variableName") == "Event" and record.get("afterValue") == "2001" for record in records):
            raise AutomationError("Meng's power transfer lacks its native continuation event")
        proof.append(dict(start=start, records=records))
    write_json(output / "ending-two-meng-help-proof.json", dict(scripts=proof, cheatAssisted=False, fullPlaythrough=False))
    client.save_or_load(6)
    checkpoint(client, output, "516-ending-two-tower-rescue-source-slot6")


def ending_two_tower_offers(client, output, resource):
    source = idle(client)
    if (source["map"] != "map_032_天山.map" or source["variables"].get("Event") != "2002"
            or source["variables"].get("Result") != "2"):
        raise AutomationError("Tower offers require the native Meng power-transfer departure Event 2002 source")
    client.save_or_load(0)
    checkpoint(client, output, "520-ending-two-tower-journey-source-slot0")
    for destination, trap in (("map_031_连接地图.map", 2), ("map_065_天山古道.map", 2),
                              ("map_030_悲魔山庄.map", 2), ("map_028_连接地图.map", 5),
                              ("map_027_连接地图.map", 3), ("map_012_惠安镇.map", 2),
                              ("map_014_连接地图.map", 2), ("map_017_连接地图.map", 3),
                              ("map_018_连接地图.map", 2), ("map_019_寒波谷.map", 2)):
        transition(client, resource, destination, trap, running=True)
    state = transition(client, resource, "map_019_寒波谷.map", 4)
    if state["variables"].get("Event") != "2003":
        raise AutomationError("Native Hanbo search did not release the tower journey")
    checkpoint(client, output, "521-ending-two-zixuan-missing")
    for destination, trap in (("map_018_连接地图.map", 1), ("map_017_连接地图.map", 1),
                              ("map_023_连接地图.map", 3), ("map_024_倚天山.map", 1)):
        transition(client, resource, destination, trap, running=True)
    client.save_or_load(1)
    checkpoint(client, output, "522-ending-two-tower-offers-source-slot1")
    path = "script/map/map_025_摘星楼/大战摘星楼.txt"
    for accepted_offer in range(4):
        load_checkpoint(client, 1)
        client.act("SetAutoDialogue", enabled=True)
        choices = [(f"{path}:{line}", 1) for line in (42, 50, 57)[:accepted_offer]]
        if accepted_offer < 3:
            choices.append((f"{path}:{(42, 50, 57)[accepted_offer]}", 0))
        destination = "map_006_武当山山顶.map" if accepted_offer < 3 else "map_025_摘星楼.map"
        state = transition(client, resource, destination, 2, choice=choices, output=output, running=True)
        if (state["variables"].get("Event") != ("2500" if accepted_offer < 3 else "2004")
                or any(state["variables"].get(key) != source["variables"].get(key)
                       for key in ("Result", "SenseVal", "EvilVal"))):
            raise AutomationError("Native tower offer did not enter its expected battle branch")
        slot = accepted_offer + 2 if accepted_offer < 3 else 6
        client.save_or_load(slot)
        checkpoint(client, output, f"523-ending-two-tower-offer-{accepted_offer}-slot{slot}", variables=("NpcCount",))


def ending_two_zixuan_rescue(client, output, resource):
    source = idle(client)
    if (source["map"] != "map_025_摘星楼.map" or source["variables"].get("Event") != "2004"
            or source["variables"].get("Result") != "2"):
        raise AutomationError("Zixuan rescue requires the native refusal of all three tower offers")
    earlier = {record["executionId"] for record in trace_records(output) if record.get("eventType") == "script.start"}
    client.save_or_load(0)
    checkpoint(client, output, "530-ending-two-tower-refusal-source-slot0", variables=("NpcCount",))
    fight_hostiles(client, output, resource)
    state = checkpoint(client, output, "531-ending-two-tower-cleared", variables=("NpcCount",))
    if state["variables"].get("Event") != "2005" or state["variables"].get("NpcCount") != "0":
        raise AutomationError("Native tower guard battle did not release the dungeon")
    client.save_or_load(1)
    transition(client, resource, "map_026_摘星楼地下.map", 2)
    fight_hostiles(client, output, resource)
    state = checkpoint(client, output, "532-ending-two-dungeon-cleared", variables=("NpcCount",))
    if state["variables"].get("NpcCount") != "0" or state["player"]["life"] <= 0:
        raise AutomationError("Native dungeon guards were not cleared")
    state = interact_named(client, output, resource, "紫轩")
    if state["variables"].get("Event") != "2006":
        raise AutomationError("Native dungeon reunion did not release Zixuan")
    client.save_or_load(2)
    checkpoint(client, output, "533-ending-two-zixuan-rescued-slot2")
    for destination, trap in (("map_025_摘星楼.map", 1), ("map_024_倚天山.map", 1),
                              ("map_023_连接地图.map", 3), ("map_017_连接地图.map", 2),
                              ("map_018_连接地图.map", 2), ("map_019_寒波谷.map", 2)):
        transition(client, resource, destination, trap, running=True)
    state = transition(client, resource, "map_019_寒波谷.map", 9)
    if state["variables"].get("Event") != "2007" or any(state["variables"].get(key) != source["variables"].get(key)
            for key in ("Result", "SenseVal", "EvilVal")):
        raise AutomationError("Native Zixuan homecoming did not release the fortress rescue")
    paths = ("script/map/map_025_摘星楼/摘星楼弟子死亡.txt", "script/map/map_026_摘星楼地下/摘星楼弟子死亡.txt",
             "script/map/map_026_摘星楼地下/紫轩对话.txt", "script/map/map_019_寒波谷/trap09.txt")
    proof = []
    for path in paths:
        start = next((record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start"
                      and record["executionId"] not in earlier and record.get("virtualPath") == path), None)
        if start is None or start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Zixuan rescue lacks its current native script evidence")
        records = completed_script(output, start)
        if path in paths[:2] and not any(record.get("variableName") == "NpcCount" and record.get("afterValue") == "0" for record in records):
            raise AutomationError("Zixuan rescue lacks the native all-clear death execution")
        proof.append(dict(start=start, records=records))
    write_json(output / "ending-two-zixuan-rescue-proof.json", dict(scripts=proof, cheatAssisted=False, fullPlaythrough=False))
    client.save_or_load(6)
    checkpoint(client, output, "534-ending-two-fortress-rescue-source-slot6")


def ending_two_fortress_rescue(client, output, resource):
    source = idle(client)
    if (source["map"] not in ("map_019_寒波谷.map", "map_018_连接地图.map") or source["variables"].get("Event") != "2007"
            or source["variables"].get("Result") != "2"):
        raise AutomationError("Late fortress rescue requires the native Zixuan homecoming Event 2007 source")
    earlier = {record["executionId"] for record in trace_records(output) if record.get("eventType") == "script.start"}
    client.save_or_load(0)
    checkpoint(client, output, "540-ending-two-late-fortress-source-slot0")
    if source["map"] == "map_019_寒波谷.map":
        transition(client, resource, "map_018_连接地图.map", 1, running=True)
    for destination in ("map_017_连接地图.map", "map_014_连接地图.map"):
        transition(client, resource, destination, 1, running=True)
    client.save_or_load(1)
    checkpoint(client, output, "541-ending-two-moon-ambush-slot1", variables=("NpcCount",))
    fight_hostiles(client, output, resource)
    state = checkpoint(client, output, "542-ending-two-moon-escape-complete", variables=("NpcCount",))
    if state["variables"].get("Event") != "2008" or state["variables"].get("NpcCount") != "0":
        raise AutomationError("Native Moon ambush deaths did not complete her escape dialogue")
    client.save_or_load(2)
    for destination, trap in (("map_012_惠安镇.map", 1), ("map_027_连接地图.map", 3),
                              ("map_028_连接地图.map", 1), ("map_030_悲魔山庄.map", 2)):
        transition(client, resource, destination, trap, running=True)
    checkpoint(client, output, "543-ending-two-xin-chu-battle", variables=("NpcCount",))
    fight_hostiles(client, output, resource)
    state = checkpoint(client, output, "544-ending-two-xin-chu-cleared", variables=("NpcCount",))
    if state["variables"].get("Event") != "2008" or state["variables"].get("NpcCount") != "0":
        raise AutomationError("Native Xin Chu manor battle did not reopen its exits")
    client.save_or_load(3)
    for destination, trap in (("map_036_连接地图.map", 6), ("map_037_敦煌十洞.map", 2),
                              ("map_037_敦煌十洞.map", 3), ("map_037_敦煌十洞.map", 4),
                              ("map_038_连接地图.map", 1), ("map_039_飞龙堡.map", 2)):
        transition(client, resource, destination, trap, running=True)
    checkpoint(client, output, "545-ending-two-late-fortress-battle", variables=("NpcCount",))
    fight_hostiles(client, output, resource, magic_file="player-magic-魂牵梦绕.ini")
    state = checkpoint(client, output, "546-ending-two-late-fortress-cleared", variables=("NpcCount",))
    if state["variables"].get("Event") != "2009" or state["variables"].get("NpcCount") != "0":
        raise AutomationError("Native fortress all-clear did not release the tower rescue")
    client.save_or_load(4)
    transition(client, resource, "map_040_沙漠.map", 2, running=True)
    transition(client, resource, "map_041_通天塔一层.map", 1, running=True)
    # The Event 2009 rescue uses the ordinary stairs; the lock puzzles belong to Event 3182/3184.
    for destination in ("map_042_通天塔二层.map", "map_043_通天塔第三层.map", "map_044_通天塔四层.map",
                        "map_045_通天塔第五层.map", "map_046_通天塔第六层.map", "map_047_通天塔第七层.map",
                        "map_049_通天塔第八层.map"):
        tower_transition(client, output, resource, destination, 2)
        checkpoint(client, output, f"547-ending-two-tower-arrival-{Path(destination).stem}")
    state = idle(client)
    if (state["map"] != "map_049_通天塔第八层.map" or state["variables"].get("Event") != "2009"
            or state["player"]["life"] <= 0 or not any(target.get("name") == "蔷薇" for target in state["targets"])
            or any(state["variables"].get(key) != source["variables"].get(key) for key in ("Result", "SenseVal", "EvilVal"))):
        raise AutomationError("Native tower ascent did not reach the unchanged Rose reunion source")
    executions = [record for record in trace_records(output) if record.get("eventType") == "script.start"
                  and record["executionId"] not in earlier]
    if any(record.get("virtualPath") == "script/map/map_049_通天塔第八层/蔷薇对话.txt" for record in executions):
        raise AutomationError("Rose reunion was entered before its branch checkpoint")
    paths = (("script/map/map_014_连接地图/无忧教死亡.txt", "NpcCount", "0"),
             ("script/map/map_014_连接地图/月眉儿对话.txt", "Event", "2008"),
             ("script/map/map_030_悲魔山庄/无忧教死亡.txt", "NpcCount", "0"),
             ("script/map/map_039_飞龙堡/无忧教死亡.txt", "Event", "2009"),
             ("script/map/map_047_通天塔第七层/trap02.txt", None, None))
    proof = []
    for path, variable, expected in paths:
        start = next((record for record in reversed(executions) if record.get("virtualPath") == path), None)
        if start is None or start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Late fortress rescue lacks its current native script evidence")
        records = completed_script(output, start)
        if variable and not any(record.get("variableName") == variable and record.get("afterValue") == expected for record in records):
            raise AutomationError("Late fortress rescue lacks its native all-clear or story continuation")
        proof.append(dict(start=start, records=records))
    write_json(output / "ending-two-fortress-rescue-proof.json", dict(scripts=proof, cheatAssisted=False, fullPlaythrough=False))
    client.save_or_load(6)
    checkpoint(client, output, "548-ending-two-rose-reunion-source-slot6")


def ending_two_rose_reunion(client, output, resource):
    source = idle(client)
    if (source["map"] != "map_049_通天塔第八层.map" or source["variables"].get("Event") != "2009"
            or source["variables"].get("Result") != "2"):
        raise AutomationError("Rose reunion requires the ordinary tower rescue before C46")
    client.save_or_load(0)
    checkpoint(client, output, "560-rose-reunion-source-slot0")
    path = "script/map/map_049_通天塔第八层/蔷薇对话.txt"
    inventory_items = lambda value: [(item["file"], item["quantity"], item["slot"]) for item in value["inventory"]]
    learned_magic = lambda value: [(item["file"], item["level"], item["exp"], item["slot"]) for item in value["magic"]]
    for option in (0, 1):
        if option:
            load_checkpoint(client, 0)
        state = interact_named(client, output, resource, "蔷薇", choice=(f"{path}:27", option))
        if (state["variables"].get("Event") != "2010"
                or any(state["variables"].get(key) != source["variables"].get(key) for key in ("Result", "SenseVal", "EvilVal"))
                or inventory_items(state) != inventory_items(source) or learned_magic(state) != learned_magic(source)
                or state["player"]["money"] != source["player"]["money"]
                or not any(target["name"] == "蔷薇" for target in state["targets"])):
            raise AutomationError("Rose kiss choice did not preserve its shared native continuation")
        client.save_or_load(option + 1)
        checkpoint(client, output, f"561-rose-kiss-option-{option}-slot{option + 1}")
    for destination in ("map_047_通天塔第七层.map", "map_046_通天塔第六层.map", "map_045_通天塔第五层.map",
                        "map_044_通天塔四层.map", "map_043_通天塔第三层.map", "map_042_通天塔二层.map",
                        "map_041_通天塔一层.map", "map_040_沙漠.map"):
        tower_transition(client, output, resource, destination, 1)
    for destination, trap in (("map_039_飞龙堡.map", 2), ("map_038_连接地图.map", 1),
                              ("map_037_敦煌十洞.map", 1), ("map_037_敦煌十洞.map", 12),
                              ("map_037_敦煌十洞.map", 8), ("map_037_敦煌十洞.map", 9),
                              ("map_036_连接地图.map", 2), ("map_030_悲魔山庄.map", 1),
                              ("map_065_天山古道.map", 1), ("map_031_连接地图.map", 1),
                              ("map_032_天山.map", 1), ("map_033_落叶谷(破坏后).map", 1)):
        transition(client, resource, destination, trap, running=True)
    state = transition(client, resource, "map_033_落叶谷(破坏后).map", 3, running=False)
    if state["variables"].get("Event") != "2021":
        raise AutomationError("Native return to Meng's body did not enter Event2021")
    checkpoint(client, output, "562-rose-meng-farewell")
    state = interact_named(client, output, resource, "蔷薇", position=(18, 94))
    if (state["variables"].get("Event") != "2022" or state["player"]["position"] != {"x": 4, "y": 53}
            or any(target["name"] == "孟知秋" for target in state["targets"])
            or not any(target["name"] == "杨熙烈之墓" for target in state["targets"])):
        raise AutomationError("Native Meng burial did not reach the first invitation source")
    client.save_or_load(6)
    checkpoint(client, output, "563-rose-invitations-source-slot6")


def ending_two_rose_invitations(client, output, resource):
    source = idle(client)
    if (source["map"] != "map_033_落叶谷(破坏后).map" or source["variables"].get("Event") != "2022"
            or source["variables"].get("Result") != "2"):
        raise AutomationError("Rose invitations require the ordinary Meng burial Event2022 source")
    client.save_or_load(0)
    checkpoint(client, output, "570-rose-first-invitation-source-slot0")
    path = "script/map/map_033_落叶谷(破坏后)/蔷薇对话.txt"
    lines = (62, 88, 111, 137)
    accepted = []
    for invitation in range(4):
        if invitation:
            load_checkpoint(client, 0)
        for index in range(invitation + 1):
            option = 0 if index == invitation else 1
            state = interact_named(client, output, resource, "蔷薇", choice=(f"{path}:{lines[index]}", option))
            expected = "2030" if option == 0 else str(2023 + index)
            if state["variables"].get("Event") != expected:
                raise AutomationError("Rose invitation did not apply its native acceptance or next invitation")
        if (state["map"] != "map_063_药王谷.map" or state["player"]["life"] != state["player"]["lifeMax"]
                or state["player"]["mana"] != state["player"]["manaMax"]
                or any(state["variables"].get(key) != source["variables"].get(key) for key in ("Result", "SenseVal", "EvilVal"))):
            raise AutomationError("Accepted Rose invitation did not reach its unchanged native medical route")
        accepted.append(dict(invitation=invitation + 1, player=state["player"], variables=state["variables"]))
        if state["player"]["thew"] != accepted[0]["player"]["thew"]:
            raise AutomationError("Invitation number changed the shared full-thew-minus-500 result")
        client.save_or_load(invitation + 1)
        checkpoint(client, output, f"571-rose-invitation-{invitation + 1}-accepted-slot{invitation + 1}")
    for check_neighbor in (0, 1):
        load_checkpoint(client, 0)
        for index in range(3):
            state = interact_named(client, output, resource, "蔷薇", choice=(f"{path}:{lines[index]}", 1))
            if state["variables"].get("Event") != str(2023 + index):
                raise AutomationError("Repeated Rose refusal did not reach its next native invitation")
        state = interact_named(client, output, resource, "蔷薇", choice=((f"{path}:137", 1), (f"{path}:164", check_neighbor)))
        if state["variables"].get("Event") != str(2026 + check_neighbor):
            raise AutomationError("Night investigation choice did not apply its distinct pre-death event")
        checkpoint(client, output, f"572-rose-night-check-{check_neighbor}")
        state = interact_named(client, output, resource, "蔷薇", position=(24, 18))
        if (state["variables"].get("Event") != "2100" or state["player"]["position"] != {"x": 5, "y": 51}
                or any(target["name"] == "蔷薇" for target in state["targets"])):
            raise AutomationError("Native Rose death and burial did not complete after the night choice")
        client.save_or_load(5 if check_neighbor == 0 else 6)
        checkpoint(client, output, f"573-rose-death-check-{check_neighbor}-slot{5 if check_neighbor == 0 else 6}")
    write_json(output / "rose-invitations-proof.json", dict(accepted=accepted, deathSlots={"checked": 5, "slept": 6},
               ordinaryMedicalSlot=1, cheatAssisted=False, fullPlaythrough=False))


def ending_two_rose_care(client, output, resource):
    source = idle(client)
    if (source["map"] != "map_063_药王谷.map" or source["variables"].get("Event") != "2030"
            or source["variables"].get("Result") != "2"):
        raise AutomationError("Rose care requires an ordinary accepted invitation before Hu's diagnosis")
    client.move(35, 80, running=True)
    idle(client)
    state = interact_named(client, output, resource, "胡神医", position=(36, 76))
    if state["map"] != "map_030_悲魔山庄.map" or state["variables"].get("Event") != "2031":
        raise AutomationError("Native diagnosis and return did not reach Rose's treatment Event2031")
    state = interact_named(client, output, resource, "蔷薇", position=(56, 158))
    if state["variables"].get("Event") != "2032":
        raise AutomationError("Rose check did not release Zhen's night conversation")
    client.save_or_load(0)
    checkpoint(client, output, "580-zhen-night-source-slot0")
    path = "script/map/map_030_悲魔山庄/真儿对话.txt"
    for option in (0, 1):
        if option:
            load_checkpoint(client, 0)
        state = interact_named(client, output, resource, "纳兰真", choice=(f"{path}:80", option))
        expected_rose = {"x": 64, "y": 79} if option == 0 else {"x": 37, "y": 90}
        if (state["variables"].get("Event") != "2033"
                or not any(target["name"] == "蔷薇" and target["position"] == expected_rose for target in state["targets"])
                or (option == 0 and (state["player"]["life"] != state["player"]["lifeMax"]
                                    or state["player"]["mana"] != state["player"]["manaMax"]))):
            raise AutomationError("Zhen's two answers did not produce their native resources and Rose positions")
        checkpoint(client, output, f"581-zhen-night-answer-{option}")
        state = interact_named(client, output, resource, "蔷薇", position=(expected_rose["x"], expected_rose["y"]))
        if (state["variables"].get("Event") != "2034"
                or any(state["variables"].get(key) != source["variables"].get(key) for key in ("Result", "SenseVal", "EvilVal"))):
            raise AutomationError("Rose's recovery did not reach the unchanged Hanbo return Event2034")
        client.save_or_load(option + 1)
        checkpoint(client, output, f"582-rose-recovered-answer-{option}-slot{option + 1}")
    client.save_or_load(6)
    checkpoint(client, output, "583-hanbo-final-return-source-slot6")


def ending_two_hanbo_pickup(client, output, resource):
    source = idle(client)
    if (source["map"] != "map_030_悲魔山庄.map" or source["variables"].get("Event") != "2034"
            or source["variables"].get("Result") != "2"):
        raise AutomationError("Late Hanbo pickup requires Rose's ordinary recovery Event2034")
    for destination, trap in (("map_028_连接地图.map", 5), ("map_027_连接地图.map", 3),
                              ("map_012_惠安镇.map", 2), ("map_014_连接地图.map", 2),
                              ("map_017_连接地图.map", 3), ("map_018_连接地图.map", 2),
                              ("map_019_寒波谷.map", 2), ("map_020_樱花谷.map", 2)):
        if destination == "map_017_连接地图.map":
            tower_transition(client, output, resource, destination, trap)
        else:
            transition(client, resource, destination, trap, running=True)
    client.save_or_load(0)
    checkpoint(client, output, "590-sakura-pickup-order-source-slot0")
    proof = []
    for zixuan, first, first_position, second in ((1, "紫轩", (13, 35), "月眉儿"),
                                                (0, "月眉儿", (14, 35), "紫轩")):
        if not zixuan:
            load_checkpoint(client, 0)
        state = interact_named(client, output, resource, first, position=first_position)
        if state["variables"].get("Event") != str(2036 - zixuan):
            raise AutomationError("Sakura conversation order did not apply its distinct native event")
        checkpoint(client, output, f"591-sakura-first-{first}")
        transition(client, resource, "map_019_寒波谷.map", 1, running=True)
        state = interact_named(client, output, resource, second, position=(23, 94))
        if (state["map"] != "map_018_连接地图.map" or state["variables"].get("Event") != "2040"
                or state["variables"].get("zixuan") != str(zixuan)
                or any(state["variables"].get(key) != source["variables"].get(key) for key in ("Result", "SenseVal", "EvilVal"))):
            raise AutomationError("Native Hanbo departure did not preserve its companion-order branch")
        client.save_or_load(1 if zixuan else 2)
        saved = checkpoint(client, output, f"592-hanbo-departure-zixuan-{zixuan}-slot{1 if zixuan else 2}")
        proof.append(dict(zixuan=zixuan, first=first, slot=1 if zixuan else 2,
                          player=saved["player"], variables=saved["variables"], targets=saved["targets"]))
    write_json(output / "hanbo-pickup-order-proof.json", dict(branches=proof, cheatAssisted=False, fullPlaythrough=False))


def ending_two_finale(client, output, resource, offer):
    source = idle(client)
    if (source["map"] not in ("map_018_连接地图.map", "map_028_连接地图.map") or source["variables"].get("Event") != "2040"
            or source["variables"].get("Result") != "2" or source["variables"].get("zixuan") not in ("0", "1")):
        raise AutomationError("Ending-two finale requires an ordinary final Hanbo departure")
    if source["map"] == "map_018_连接地图.map":
        approach = (("map_017_连接地图.map", 1), ("map_014_连接地图.map", 1), ("map_012_惠安镇.map", 1))
    elif source["player"]["life"] < source["player"]["lifeMax"] or source["player"]["mana"] < source["player"]["manaMax"]:
        approach = (("map_027_连接地图.map", 3), ("map_012_惠安镇.map", 2))
    else:
        approach = ()
    if approach:
        for destination, trap in approach:
            transition(client, resource, destination, trap, running=True)
        recover(client, output, resource)
        for destination, trap in (("map_027_连接地图.map", 3), ("map_028_连接地图.map", 1)):
            transition(client, resource, destination, trap, running=True)
    client.save_or_load(0)
    checkpoint(client, output, "600-final-beimo-return-source-slot0")
    zixuan = source["variables"]["zixuan"]
    path = "script/map/map_030_悲魔山庄/寒波谷归来" + ("3" if zixuan == "1" else "2") + ".txt"
    state = transition(client, resource, "map_030_悲魔山庄.map", 2, output=output,
                       choice=(f"{path}:{64 if zixuan == '1' else 65}", offer), running=True)
    if (state["variables"].get("Event") != "2041" or state["variables"].get("zixuan") != zixuan
            or any(state["variables"].get(key) != source["variables"].get(key) for key in ("Result", "SenseVal", "EvilVal"))):
        raise AutomationError("Final join-or-refuse answer did not start the shared native final battle")
    client.save_or_load(1)
    checkpoint(client, output, f"601-final-nalan-zixuan-{zixuan}-offer-{offer}-slot1")
    terminal = "script/map/map_030_悲魔山庄/纳兰潜凛死亡.txt"
    client.act("SetAutoDialogue", enabled=True, intervalMs=1000)
    state = fight_named(client, output, resource, "纳兰潜凛", use_magic=True, expected_terminal=terminal,
                        magic_file="player-magic-魂牵梦绕.ini", final_dialogue=("真儿姐，快去哄哄你的孩子", "剧终"))
    if state["scene"] != "Title":
        raise AutomationError("Final Nalan victory did not return to the native title")
    start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start"
                 and record.get("virtualPath") == terminal)
    records = completed_script(output, start)
    epilogues = [json.loads(file.read_text(encoding="utf-8")) for file in output.glob("ending-dialogue-*.json")]
    videos = [json.loads(file.read_text(encoding="utf-8")) for file in output.glob("story-video-*.json")]
    actor = "紫轩" if zixuan == "1" else "蔷薇"
    if (not any(value.get("video", "").lower().endswith("end3.wmv") for value in videos)
            or not any(value["map"] == "map_051_海边.map" and actor + "：真儿姐" in value.get("dialogue", {}).get("text", "") for value in epilogues)
            or not any("剧终" in value.get("dialogue", {}).get("text", "") and value["dialogue"].get("complete") for value in epilogues)):
        raise AutomationError("Finale lacks its native end3 video or distinct companion epilogue frame")
    write_json(output / "ending-two-finale-proof.json", dict(zixuan=zixuan, offer=offer, terminalStart=start,
               records=records, epilogues=epilogues, observedVideos=videos, returnedToTitle=True, cheatAssisted=False, fullPlaythrough=False))
    checkpoint(client, output, f"602-ending-two-zixuan-{zixuan}-offer-{offer}-title")


def ending_two_letter_aftermath(client, output, resource):
    state = idle(client)
    source_file = output / "450-ending-two-letter-aftermath-source-slot0.json"
    if state["variables"].get("Result") != "2" or state["variables"].get("Event") not in ("1816", "1817"):
        raise AutomationError("Nalan Zhen's injury requires the ordinary mother's-letter aftermath")
    if state["map"] == "map_062_禁地密室.map" and state["variables"].get("Event") == "1816":
        client.save_or_load(0)
        checkpoint(client, output, "450-ending-two-letter-aftermath-source-slot0")
        for destination in ("map_061_禁地三层.map", "map_060_禁地二层.map", "map_059_禁地一层.map",
                            "map_058_禁地.map", "map_057_连接地图.map", "map_050_忘忧岛.map", "map_051_海边.map"):
            state = transition(client, resource, destination, 1)
        for _ in range(5):
            if state["variables"].get("Event") != "1816":
                break
            state = transition(client, resource, "map_051_海边.map", 3)
    elif state["map"] not in ("map_051_海边.map", "map_062_禁地密室.map") or not source_file.is_file():
        raise AutomationError("Nalan Zhen's injury lacks its recorded ordinary letter")
    if state["map"] == "map_051_海边.map" and state["variables"].get("Event") == "1817":
        client.save_or_load(1)
        checkpoint(client, output, "451-ending-two-zhen-search-slot1")
        for destination, trap in (("map_050_忘忧岛.map", 1), ("map_057_连接地图.map", 4),
                                  ("map_058_禁地.map", 2), ("map_059_禁地一层.map", 2),
                                  ("map_060_禁地二层.map", 2), ("map_061_禁地三层.map", 2),
                                  ("map_062_禁地密室.map", 2)):
            state = transition(client, resource, destination, trap, running=True)
    if state["map"] != "map_062_禁地密室.map" or state["variables"].get("Event") != "1817":
        raise AutomationError("Native Nalan Zhen search did not reach the injured secret-chamber scene")
    checkpoint(client, output, "452-ending-two-injured-zhen")
    state = transition(client, resource, "map_051_海边.map", 9)
    source = json.loads(source_file.read_text(encoding="utf-8"))
    if state["variables"].get("Event") != "1818" or any(state["variables"].get(key) != source["variables"].get(key)
            for key in ("Result", "SenseVal", "EvilVal")):
        raise AutomationError("Native injured-Zhen rescue and Meng accusation did not complete")
    client.save_or_load(6)
    checkpoint(client, output, "453-ending-two-meng-accusation-source-slot6")


def ending_two_mother_letter(client, output, resource):
    state = idle(client)
    source_file = output / "440-ending-two-mother-letter-source-slot0.json"
    if state["variables"].get("Result") != "2" or state["variables"].get("Event") not in ("1814", "1815"):
        raise AutomationError("Mother's letter requires the ordinary twelve-plant treatment")
    if state["map"] == "map_051_海边.map" and state["variables"].get("Event") == "1814":
        client.save_or_load(0)
        checkpoint(client, output, "440-ending-two-mother-letter-source-slot0")
        for destination, trap in (("map_050_忘忧岛.map", 1), ("map_057_连接地图.map", 4),
                                  ("map_058_禁地.map", 2), ("map_059_禁地一层.map", 2),
                                  ("map_060_禁地二层.map", 2), ("map_061_禁地三层.map", 2)):
            state = transition(client, resource, destination, trap, running=True)
    elif state["map"] not in ("map_061_禁地三层.map", "map_062_禁地密室.map") or not source_file.is_file():
        raise AutomationError("Mother's letter lacks its recorded ordinary expedition")
    if state["map"] == "map_061_禁地三层.map":
        state = transition(client, resource, "map_062_禁地密室.map", 2)
    if state["variables"].get("Event") != "1815":
        raise AutomationError("Native jade pendants did not open the secret chamber")
    client.save_or_load(1)
    checkpoint(client, output, "441-ending-two-letter-box-source-slot1")
    before = sum(item["quantity"] for item in state["inventory"] if item["file"] == "goods-e11-信.ini")
    client.act("JumpTo", generation=client.observe()["generation"], x=16, y=14, timeoutMs=30000)
    state = interact_named(client, output, resource, "宝盒")
    source = json.loads(source_file.read_text(encoding="utf-8"))
    if (state["variables"].get("Event") != "1816"
            or sum(item["quantity"] for item in state["inventory"] if item["file"] == "goods-e11-信.ini") != before + 1
            or any(state["variables"].get(key) != source["variables"].get(key) for key in ("Result", "SenseVal", "EvilVal"))):
        raise AutomationError("Native letter-box story did not deliver its item and aftermath")
    client.save_or_load(6)
    checkpoint(client, output, "442-ending-two-mother-letter-slot6")


def ending_two_silver_grass(client, output, resource):
    state = idle(client)
    source_file = output / "430-ending-two-silver-grass-source-slot0.json"
    if state["variables"].get("Result") != "2" or state["variables"].get("Event") != "1813":
        raise AutomationError("Twelve silver-grass plants require the ordinary Moon treatment request")
    quantity = lambda value: sum(item["quantity"] for item in value["inventory"] if item["file"] == "goods-e12-银丝草.ini")
    if state["map"] == "map_051_海边.map" and quantity(state) < 12:
        client.save_or_load(0)
        source = checkpoint(client, output, "430-ending-two-silver-grass-source-slot0")
        state = interact_named(client, output, resource, "纳兰真")
        if state["variables"].get("Event") != "1813" or quantity(state) != quantity(source):
            raise AutomationError("Native treatment accepted an incomplete silver-grass delivery")
        recover(client, output, resource)
        for destination, trap in (("map_050_忘忧岛.map", 1), ("map_053_连接地图.map", 2), ("map_054_北山.map", 2)):
            state = transition(client, resource, destination, trap, running=True)
        checkpoint(client, output, "431-ending-two-twelve-plants")
    elif state["map"] not in ("map_054_北山.map", "map_051_海边.map") or not source_file.is_file():
        raise AutomationError("Silver-grass delivery lacks its recorded ordinary expedition")
    if state["map"] == "map_054_北山.map":
        fight_hostiles(client, output, resource)
        while True:
            before = client.observe(VARIABLES)
            herbs = [target for target in before["targets"] if target["name"] == "草药"]
            if not herbs:
                break
            position = before["player"]["position"]
            target = min(herbs, key=lambda item: abs(item["position"]["x"] - position["x"]) * 2 + abs(item["position"]["y"] - position["y"]))
            state = interact_named(client, output, resource, "草药", position=(target["position"]["x"], target["position"]["y"]))
            if quantity(state) != quantity(before) + 1 or any(item["id"] == target["id"] for item in state["targets"]):
                raise AutomationError("Native silver-grass pickup did not add one item and remove its object")
            checkpoint(client, output, f"432-ending-two-silver-grass-{quantity(state)}")
        if quantity(state) != 12:
            raise AutomationError("Native North Mountain expedition did not collect twelve silver-grass plants")
        client.save_or_load(1)
        for destination, trap in (("map_053_连接地图.map", 1), ("map_050_忘忧岛.map", 1), ("map_051_海边.map", 1)):
            state = transition(client, resource, destination, trap, running=True)
    before = state
    state = interact_named(client, output, resource, "纳兰真")
    source = json.loads(source_file.read_text(encoding="utf-8"))
    if quantity(before) != 12 or quantity(state) != 0 or state["variables"].get("Event") != "1814" or any(
            state["variables"].get(key) != source["variables"].get(key) for key in ("Result", "SenseVal", "EvilVal")):
        raise AutomationError("Native Moon treatment did not consume twelve plants and reveal the sisters' letter")
    client.save_or_load(6)
    checkpoint(client, output, "433-ending-two-mother-letter-source-slot6")


def ending_two_moon_care(client, output, resource):
    state = idle(client)
    source_file = output / "420-ending-two-moon-care-source-slot0.json"
    event = state["variables"].get("Event")
    if state["variables"].get("Result") != "2" or event not in tuple(str(value) for value in range(1806, 1814)):
        raise AutomationError("Moon's treatment requires the ordinary peak-duel aftermath")
    if event == "1806" and state["map"] == "map_033_落叶谷.map":
        client.save_or_load(0)
        checkpoint(client, output, "420-ending-two-moon-care-source-slot0")
        state = interact_named(client, output, resource, "纳兰真")
    elif not source_file.is_file():
        raise AutomationError("Moon's treatment lacks its recorded ordinary source")
    if state["variables"].get("Event") == "1807":
        state = interact_named(client, output, resource, "侍女萱儿")
        client.save_or_load(1)
        checkpoint(client, output, "421-ending-two-qiangwei-search-slot1")
    if state["variables"].get("Event") == "1808":
        if state["map"] == "map_033_落叶谷.map":
            transition(client, resource, "map_032_天山.map", 1)
            state = transition(client, resource, "map_034_天池.map", 3)
        if state["map"] != "map_034_天池.map":
            raise AutomationError("Qiangwei search did not enter the ordinary Tianchi scene")
        if state["player"]["position"]["x"] < 7:
            client.move(5, 24)
            client.act("JumpTo", generation=client.observe()["generation"], x=11, y=24, timeoutMs=30000)
        client.move(17, 24)
        state = interact_named(client, output, resource, "蔷薇")
        checkpoint(client, output, "422-ending-two-meng-treatment")
    if state["variables"].get("Event") == "1809":
        state = interact_named(client, output, resource, "纳兰真")
    if state["variables"].get("Event") == "1810":
        state = interact_named(client, output, resource, "蔷薇")
        checkpoint(client, output, "423-ending-two-hairpin-gift-next-day")
    if state["variables"].get("Event") == "1811":
        state = interact_named(client, output, resource, "侍女樱儿")
    if state["variables"].get("Event") == "1812":
        state = interact_named(client, output, resource, "纳兰真")
    source = json.loads(source_file.read_text(encoding="utf-8"))
    if state["map"] != "map_051_海边.map" or state["variables"].get("Event") != "1813" or any(
            state["variables"].get(key) != source["variables"].get(key) for key in ("Result", "SenseVal", "EvilVal")):
        raise AutomationError("Native Moon treatment and island departure did not complete")
    client.save_or_load(6)
    checkpoint(client, output, "424-ending-two-silver-grass-source-slot6")


def ending_two_moon_duel(client, output, resource):
    state = idle(client)
    if state["variables"].get("Result") != "2" or state["variables"].get("Event") != "1806":
        raise AutomationError("Moon's peak duel requires the ordinary rescued-Qiangwei letter")
    if state["map"] == "map_012_惠安镇.map":
        recover(client, output, resource)
        client.save_or_load(0)
        checkpoint(client, output, "400-ending-two-peak-route-source-slot0")
        for destination, trap in (("map_011_连接地图.map", 1), ("map_008_野树林.map", 1),
                                  ("map_007_连接地图.map", 3), ("map_003_武当山下.map", 1),
                                  ("map_001_凌绝峰连接地图.map", 1), ("map_002_凌绝峰峰顶.map", 1)):
            transition(client, resource, destination, trap, running=True)
        recover(client, output, resource)
        state = interact_named(client, output, resource, "月眉儿")
        if not any(target["name"] == "月眉儿" and npc_attackable(target) for target in state["targets"]):
            raise AutomationError("Native Moon confrontation did not start the peak duel")
        client.save_or_load(1)
        checkpoint(client, output, "401-ending-two-peak-duel-source-slot1")
    elif state["map"] != "map_002_凌绝峰峰顶.map" or not (output / "401-ending-two-peak-duel-source-slot1.json").is_file():
        raise AutomationError("Peak duel lacks its recorded normal journey")
    source = json.loads((output / "401-ending-two-peak-duel-source-slot1.json").read_text(encoding="utf-8"))
    for option in (0, 1):
        load_checkpoint(client, 1)
        client.act("SetAutoDialogue", enabled=True)
        state = fight_named(client, output, resource, "月眉儿", use_magic=True,
                            choice=("script/map/map_002_凌绝峰峰顶/眉儿死亡.txt:114", option))
        if state["map"] != "map_033_落叶谷.map" or any(state["variables"].get(key) != source["variables"].get(key)
                for key in ("Event", "Result", "SenseVal", "EvilVal")):
            raise AutomationError("Native Moon rescue and Meng marriage offer did not complete")
        client.save_or_load(option + 2)
        checkpoint(client, output, f"402-ending-two-meng-offer-{option}-slot{option + 2}")
    load_checkpoint(client, 2)
    client.act("SetAutoDialogue", enabled=True)
    client.save_or_load(6)
    checkpoint(client, output, "403-ending-two-moon-rescue-slot6")


def ending_two_moon_letter(client, output, resource):
    state = idle(client)
    if state["map"] != "map_033_落叶谷.map" or state["variables"].get("Event") != "1805" or state["variables"].get("Result") != "2":
        raise AutomationError("Moon duel invitation requires the ordinary rescued Qiang Wei return")
    recover(client, output, resource)
    client.save_or_load(0)
    checkpoint(client, output, "380-ending-two-moon-invitation-source-slot0")
    for destination, trap in (("map_032_天山.map", 1), ("map_031_连接地图.map", 2),
                              ("map_065_天山古道.map", 2), ("map_030_悲魔山庄.map", 2)):
        transition(client, resource, destination, trap, running=True)
    client.move(44, 140)
    state = interact_named(client, output, resource, "管家铁云")
    if state["variables"].get("Event") != "1806" or state["variables"].get("Result") != "2":
        raise AutomationError("Native steward message did not release the Moon duel invitation")
    client.save_or_load(6)
    checkpoint(client, output, "381-ending-two-moon-invitation-slot6")


def ending_two_rescue(client, output, resource):
    state = idle(client)
    source_file = output / "310-ending-two-rescue-source-slot0.json"
    if state["variables"].get("Result") != "2" or state["variables"].get("Event") not in ("1802", "1803"):
        raise AutomationError("Qiang Wei rescue requires the normal cleared fortress")
    if state["map"] == "map_039_飞龙堡.map" and state["variables"].get("Event") == "1802":
        recover(client, output, resource)
        client.save_or_load(0)
        source = checkpoint(client, output, "310-ending-two-rescue-source-slot0")
        transition(client, resource, "map_038_连接地图.map", 1)
        transition(client, resource, "map_037_敦煌十洞.map", 1)
        for trap in (12, 8, 9):
            transition(client, resource, "map_037_敦煌十洞.map", trap)
        transition(client, resource, "map_036_连接地图.map", 2)
        client.save_or_load(1)
        checkpoint(client, output, "311-ending-two-qiangwei-battle-slot1", variables=("NpcCount",))
    elif source_file.is_file() and (state["map"] == "map_036_连接地图.map" and state["variables"].get("Event") == "1802"
            or state["map"] == "map_030_悲魔山庄.map" and state["variables"].get("Event") == "1803"
            and (output / "312-ending-two-qiangwei-rescued.json").is_file()):
        source = json.loads(source_file.read_text(encoding="utf-8"))
    else:
        raise AutomationError("Qiang Wei rescue lacks its recorded normal battle source")
    if idle(client)["map"] == "map_036_连接地图.map":
        fight_hostiles(client, output, resource)
        state = checkpoint(client, output, "312-ending-two-qiangwei-rescued", variables=("NpcCount",))
        if state["variables"].get("Event") != "1803" or state["variables"].get("NpcCount") != "0":
            raise AutomationError("Native masked enemy deaths did not rescue Qiang Wei")
        for number in range(5):
            interact_named(client, output, resource, "蔷薇")
            checkpoint(client, output, f"312-ending-two-qiangwei-chat-{number + 1}", variables=("qiangwei",))
        transition(client, resource, "map_030_悲魔山庄.map", 1)
    state = interact_named(client, output, resource, "侍女可意")
    if state["variables"].get("Event") != "1804":
        raise AutomationError("Native ransom message did not release Qiang Wei's return")
    for destination, trap in (("map_065_天山古道.map", 1), ("map_031_连接地图.map", 1),
                              ("map_032_天山.map", 1), ("map_033_落叶谷.map", 1)):
        transition(client, resource, destination, trap, running=True)
    for _ in range(5):
        state = transition(client, resource, "map_033_落叶谷.map", 9)
        if state["variables"].get("Event") != "1804":
            break
    if state["variables"].get("Event") != "1805" or any(state["variables"].get(key) != source["variables"].get(key) for key in ("SenseVal", "EvilVal", "Result")) or not any(item["file"] == "book00-太极剑谱.ini" for item in state["inventory"]):
        raise AutomationError("Native Qiang Wei return did not grant Meng's sword book")
    records = {}
    for path in ("script/map/map_036_连接地图/杀手死亡.txt", "script/map/map_033_落叶谷/trap09.txt"):
        start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
        if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Qiang Wei rescue used a different native source")
        records[path] = completed_script(output, start)
    write_json(output / "ending-two-qiangwei-rescue-proof.json", dict(records=records, cheatAssisted=False, storyEnding=False))
    client.save_or_load(6)
    checkpoint(client, output, "313-ending-two-meng-return-slot6")


def late_huian_flowers(client, output, resource):
    state = idle(client)
    if state["map"] != "map_030_悲魔山庄.map" or (state["variables"].get("Result"), state["variables"].get("Event")) not in (("1", "570"), ("2", "1806")):
        raise AutomationError("Late flower choices require an ordinary ending route with the town exit available")
    for destination, trap in (("map_028_连接地图.map", 5), ("map_027_连接地图.map", 3), ("map_012_惠安镇.map", 2)):
        transition(client, resource, destination, trap, running=True)
    client.save_or_load(0)
    source = checkpoint(client, output, "320-late-flowers-source-slot0")
    if source["player"]["money"] < 100:
        raise AutomationError("Late flower choices require a normally funded source")
    line = 36 if source["variables"]["Result"] == "1" else 61
    baseline = sum(item["quantity"] for item in source["inventory"] if item["file"] == "goods-e21-玫瑰花.ini")
    for option in (1, 0):
        load_checkpoint(client, 0)
        client.act("SetAutoDialogue", enabled=True)
        state = interact_named(client, output, resource, "春泥", choice=(f"script/map/map_012_惠安镇/惠安镇路人19对话.txt:{line}", option))
        flowers = sum(item["quantity"] for item in state["inventory"] if item["file"] == "goods-e21-玫瑰花.ini")
        if state["player"]["money"] != source["player"]["money"] - (100 if option == 0 else 0) or flowers != baseline + (1 if option == 0 else 0) or any(state["variables"].get(key) != source["variables"].get(key) for key in ("Event", "Result", "SenseVal", "EvilVal")):
            raise AutomationError("Native late flower choice changed its payment, reward, or story state")
        client.save_or_load(option + 1)
        checkpoint(client, output, f"321-late-flowers-choice-{option}")
    client.save_or_load(6)
    checkpoint(client, output, "322-late-flowers-complete-slot6")


def huian_flowers_insufficient(client, output, resource):
    source = idle(client)
    phase = (source["variables"].get("Result"), source["variables"].get("Event"))
    if source["map"] == "map_016_剑气峰.map" and phase == ("0", "500"):
        for destination, trap in (("map_015_藏剑山庄.map", 1), ("map_014_连接地图.map", 1), ("map_012_惠安镇.map", 1)):
            transition(client, resource, destination, trap, running=True)
    elif source["map"] != "map_012_惠安镇.map" or phase not in (("0", "500"), ("1", "570"), ("2", "1806")):
        raise AutomationError("Insufficient flower funds require a normal funded town or final-duel source")
    client.move(57, 140, running=True)
    client.save_or_load(0)
    source = checkpoint(client, output, "323-flowers-funded-source-slot0")
    if source["player"]["money"] < 100:
        raise AutomationError("Flower depletion must start with enough money for a native purchase")
    path = "script/map/map_012_惠安镇/惠安镇路人19对话.txt"
    line = {"0": 11, "1": 36, "2": 61}[phase[0]]
    quantity = lambda state: sum(item["quantity"] for item in state["inventory"] if item["file"] == "goods-e21-玫瑰花.ini")
    proofs = []
    state = source
    # Spend through the native flower choice, then repeat the insufficient-funds outcome.
    options = [1] + [0] * (source["player"]["money"] // 100 + 2) + [1]
    for index, option in enumerate(options, 1):
        before = state
        sequence = trace_records(output)[-1]["sequence"]
        state = interact_named(client, output, resource, "春泥", choice=(f"{path}:{line}", option))
        paid = option == 0 and before["player"]["money"] >= 100
        if (state["player"]["money"] != before["player"]["money"] - (100 if paid else 0)
                or quantity(state) != quantity(before) + int(paid)
                or any(state["variables"].get(key) != source["variables"].get(key) for key in ("Event", "Result", "SenseVal", "EvilVal", "EvilValue"))):
            raise AutomationError("Native flower choice changed its payment, reward, or story state")
        starts = [record for record in trace_records(output) if record.get("eventType") == "script.start"
                  and record.get("virtualPath") == path and record["sequence"] > sequence]
        if len(starts) != 1 or starts[0]["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Flower outcome lacks a fresh current-source execution")
        records = completed_script(output, starts[0])
        lines = {record.get("line") for record in records if record.get("eventType") == "source.line"}
        apis = {record.get("apiName") for record in records}
        insufficient = option == 0 and not paid
        expected_line = 17 if paid else 21 if insufficient else 25
        if (line not in lines or expected_line not in lines
                or (not paid and apis & {"addgoods", "addmoney"})
                or (paid and not {"addgoods", "addmoney"}.issubset(apis))):
            raise AutomationError("Flower outcome did not complete its expected native source branch")
        checkpoint(client, output, f"324-flower-outcome-{index}")
        proofs.append(dict(option=option, paid=paid, insufficient=insufficient,
                           moneyBefore=before["player"]["money"], moneyAfter=state["player"]["money"],
                           rosesBefore=quantity(before), rosesAfter=quantity(state), records=records))
    if state["player"]["money"] >= 100 or sum(proof["insufficient"] for proof in proofs) != 2:
        raise AutomationError("Flower depletion did not verify repeated insufficient funds")
    client.save_or_load(6)
    checkpoint(client, output, "325-flowers-insufficient-complete-slot6")
    write_json(output / "flowers-insufficient-native-proof.json", dict(result=phase[0], outcomes=proofs,
               cheatAssisted=False, fullPlaythrough=False))


def pre_letter_flowers(client, output, resource, roses=4):
    source = idle(client)
    if source["map"] != "map_016_剑气峰.map" or source["variables"].get("Event") != "500" or source["variables"].get("Result") != "0":
        raise AutomationError("Boundary flower preparation requires the normal final-duel victory")
    checkpoint(client, output, "290-boundary-flowers-source")
    for destination, trap in (("map_015_藏剑山庄.map", 1), ("map_014_连接地图.map", 1), ("map_012_惠安镇.map", 1)):
        transition(client, resource, destination, trap, running=True)
    if idle(client)["player"]["money"] < roses * 100:
        client.move(90, 100, running=True)
        interact_named(client, output, resource, "宝箱", position=(4, 30))
        state = checkpoint(client, output, "290-boundary-flower-native-funding")
        if state["player"]["money"] < roses * 100 or any(state["variables"].get(key) != source["variables"].get(key) for key in ("Event", "Result", "SenseVal", "EvilVal")):
            raise AutomationError("Ordinary town treasure did not fund the flowers while preserving the story source")
    path = "script/map/map_012_惠安镇/惠安镇路人19对话.txt"
    quantity = lambda value: sum(item["quantity"] for item in value["inventory"] if item["file"] == "goods-e21-玫瑰花.ini")
    for count in range(1, roses + 1):
        before = idle(client)
        state = interact_named(client, output, resource, "春泥", choice=(f"{path}:11", 0))
        if state["player"]["money"] != before["player"]["money"] - 100 or quantity(state) != quantity(before) + 1 or any(state["variables"].get(key) != source["variables"].get(key) for key in ("Event", "Result", "SenseVal", "EvilVal")):
            raise AutomationError("Native boundary flower purchase changed its payment, reward, or story state")
        checkpoint(client, output, f"291-boundary-flower-bought-{count}")
        start = next(record for record in reversed(trace_records(output)) if record.get("eventType") == "script.start" and record.get("virtualPath") == path)
        if start["contentSha256"] != hashlib.sha256((resource / path).read_bytes()).hexdigest():
            raise AutomationError("Boundary flower purchase used a different native source")
        write_json(output / f"boundary-flower-{count}-proof.json", dict(records=completed_script(output, start), cheatAssisted=False))
    for destination, trap in (("map_027_连接地图.map", 3), ("map_028_连接地图.map", 1), ("map_030_悲魔山庄.map", 2)):
        transition(client, resource, destination, trap, running=True)
    state = idle(client)
    if any(state["variables"].get(key) != source["variables"].get(key) for key in ("Event", "Result", "SenseVal", "EvilVal")):
        raise AutomationError("Boundary flower preparation changed its pre-letter story source")
    client.save_or_load(6)
    checkpoint(client, output, "292-boundary-flowers-pre-letter-slot6")


def ending_four_preparation(client, output, resource):
    state = idle(client)
    source_file = output / "290-ending-four-preparation-source-slot0.json"
    if state["variables"].get("Event") != "500" or state["variables"].get("EvilVal") != "970":
        raise AutomationError("Fourth ending preparation requires the ordinary final-duel victory before the Hanbo letter")
    if state["map"] == "map_016_剑气峰.map":
        client.save_or_load(0)
        source = checkpoint(client, output, "290-ending-four-preparation-source-slot0")
        for destination, trap in (("map_015_藏剑山庄.map", 1), ("map_014_连接地图.map", 1),
                                  ("map_012_惠安镇.map", 1)):
            transition(client, resource, destination, trap, running=True)
    elif state["map"] == "map_012_惠安镇.map" and source_file.is_file():
        source = json.loads(source_file.read_text(encoding="utf-8"))
    else:
        raise AutomationError("Fourth ending preparation lacks its recorded pre-letter source")
    client.move(90, 100, running=True)
    for position in ((4, 30), (102, 101), (67, 116), (11, 129), (40, 54)):
        if idle(client)["player"]["money"] >= 1300:
            break
        interact_named(client, output, resource, "宝箱", position=position)
        checkpoint(client, output, f"291-ending-four-funding-{position[0]}-{position[1]}")
    before = idle(client)
    if before["player"]["money"] < 1300:
        raise AutomationError("Ordinary unopened town treasure did not fund the knife")
    state = interact_named(client, output, resource, "王铁匠", shop=True)
    knife = next(item for item in state["shop"] if item["file"] == "goods-w19-土龙刀.ini")
    client.buy(knife["slot"])
    if client.observe()["player"]["money"] != before["player"]["money"] - 1300:
        raise AutomationError("Ordinary late knife purchase did not charge its native price")
    client.ui("Cancel")
    state = interact_named(client, output, resource, "刀客", choice=("script/map/map_012_惠安镇/刀客对话.txt:22", 0))
    if state["variables"].get("EvilVal") != "920" or state["variables"].get("SubEvent19") != "5":
        raise AutomationError("Unique knife handover did not lower evil by 50")
    checkpoint(client, output, "292-ending-four-knife-handed-over")
    transition(client, resource, "map_014_连接地图.map", 2)
    transition(client, resource, "map_015_藏剑山庄.map", 2)
    client.move(40, 31)
    client.act("JumpTo", generation=client.observe()["generation"], x=40, y=34, timeoutMs=30000)
    interact_named(client, output, resource, "宝箱", position=(45, 79))
    state = idle(client)
    book = next(item for item in state["inventory"] if item["file"] == "book02-灭绝剑法.ini")
    client.equip(book["slot"])
    state = checkpoint(client, output, "293-ending-four-extinction-book-used")
    if state["variables"].get("EvilVal") != "895" or not any(item["file"] == "player-magic-绝情断意剑.ini" for item in state["magic"]):
        raise AutomationError("Ordinary extinction book did not teach its skill and lower evil by 25")
    client.move(40, 34)
    client.act("JumpTo", generation=client.observe()["generation"], x=40, y=31, timeoutMs=30000)
    for destination, trap in (("map_014_连接地图.map", 1), ("map_012_惠安镇.map", 1),
                              ("map_027_连接地图.map", 3), ("map_028_连接地图.map", 1),
                              ("map_030_悲魔山庄.map", 2)):
        transition(client, resource, destination, trap, running=True)
    state = idle(client)
    if state["variables"].get("Event") != "500" or state["variables"].get("EvilVal") != "895" or state["variables"].get("SenseVal") != source["variables"].get("SenseVal"):
        raise AutomationError("Fourth ending sidequests did not preserve its pre-letter source")
    client.save_or_load(6)
    checkpoint(client, output, "293-ending-four-pre-letter-slot6")


def ending_four_cloud_book(client, output, resource):
    state = idle(client)
    if state["map"] != "map_034_天池.map" or state["variables"].get("Event") != "540" or state["variables"].get("EvilVal") != "875":
        raise AutomationError("Fourth ending cloud book requires the ordinary delayed-trap source")
    source = checkpoint(client, output, "294-ending-four-cloud-book-source")
    client.move(14, 4)
    client.act("JumpTo", generation=client.observe()["generation"], x=28, y=9, timeoutMs=30000)
    checkpoint(client, output, "294-ending-four-wangwei-bank-normal-jump")
    for _ in range(60):
        state = interact_named(client, output, resource, "王炜")
        books = [item for item in state["inventory"] if item["file"] == "book10-孤烟逐云.ini"]
        if books:
            break
    else:
        raise AutomationError("Native Wang Wei random dialogue has not granted its book")
    client.equip(books[0]["slot"])
    state = checkpoint(client, output, "295-ending-four-cloud-book-used")
    if state["variables"].get("EvilVal") != "850" or state["variables"].get("SenseVal") != source["variables"].get("SenseVal") or not any(item["file"] == "player-magic-孤烟逐云.ini" for item in state["magic"]):
        raise AutomationError("Fourth ending ordinary preparation did not preserve its affection and evil conditions")
    client.move(28, 9)
    client.act("JumpTo", generation=client.observe()["generation"], x=14, y=4, timeoutMs=30000)
    client.save_or_load(6)
    checkpoint(client, output, "296-ending-four-hairpin-source-slot6")


def forget_worry_scholar(client, output, resource):
    state = idle(client)
    if state["map"] != "map_051_海边.map" or state["variables"].get("Event") != "425":
        raise AutomationError("Scholar couplet requires the ordinary post-reunion invitation save")
    transition(client, resource, "map_050_忘忧岛.map", 1)
    interact_named(client, output, resource, "支线书生")
    state = checkpoint(client, output, "254-scholar-request", variables=("SubEvent20",))
    if state["variables"].get("SubEvent20") != "2":
        raise AutomationError("Native scholar did not request his couplet")
    interact_named(client, output, resource, "支线书生")
    state = checkpoint(client, output, "255-scholar-without-butterfly", variables=("SubEvent20",))
    if state["variables"].get("SubEvent20") != "2":
        raise AutomationError("Scholar advanced before the normal butterfly clue")
    interact_named(client, output, resource, "彩蝶")
    state = checkpoint(client, output, "256-scholar-butterfly-clue", variables=("SubEvent20",))
    if state["variables"].get("SubEvent20") != "5":
        raise AutomationError("Normal butterfly interaction did not provide the clue")
    client.save_or_load(0)
    source = checkpoint(client, output, "257-scholar-choice-source-slot0", variables=("SubEvent20",))
    quantity = lambda value: sum(item["quantity"] for item in value["inventory"] if item["file"] == "book03-醉花诀.ini")
    for option in (0, 1):
        if option == 1:
            load_checkpoint(client, 0)
            client.act("SetAutoDialogue", enabled=True)
        interact_named(client, output, resource, "支线书生", choice=("script/map/map_050_忘忧岛/忘忧岛书生对话.txt:43", option))
        state = checkpoint(client, output, f"258-scholar-answer-{option}", variables=("SubEvent20",))
        if state["variables"].get("SubEvent20") != "10" or quantity(state) != quantity(source) + option:
            raise AutomationError("Couplet answer did not apply its native reward")
        before = state
        state = interact_named(client, output, resource, "支线书生")
        if quantity(state) != quantity(before):
            raise AutomationError("Scholar repeated his completed reward")
        client.save_or_load(option + 1)
    client.save_or_load(6)
    checkpoint(client, output, "259-scholar-complete-slot6", variables=("SubEvent20",))


def qingping_beggar_return(client, output, resource):
    state = idle(client)
    if state["map"] != "map_022_清平乡.map" or state["variables"].get("Event") != "170":
        raise AutomationError("Beggar reentry requires the normal village checkpoint after defeating its three bullies")
    for attempt in range(1, 11):
        state = checkpoint(client, output, f"97-qingping-beggar-reentry-{attempt}", variables=("QiGaiPos",))
        if any(target["name"] == "乞丐" and target["position"] != {"x": 0, "y": 0} for target in state["targets"]):
            break
        transition(client, resource, "map_021_油菜花地.map", 1, running=True)
        transition(client, resource, "map_022_清平乡.map", 2, running=True)
    else:
        raise AutomationError("Beggar stayed absent after ten ordinary village entrances")
    client.save_or_load(6)
    checkpoint(client, output, "97-qingping-beggar-visible-source-slot6", variables=("QiGaiPos",))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--assets", type=Path, default=Path("assets"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--inventory-only", action="store_true")
    parser.add_argument("--evidence", type=Path, nargs="*", default=())
    parser.add_argument("--difficulty", choices=("easy", "hard"), default="easy")
    parser.add_argument("--roses", type=int, choices=(3, 4, 5), default=4)
    parser.add_argument("--chapter", choices=("opening", "departure", "wudang-introduction", "wudang-pool", "wudang-gate", "wudang-challenge", "wudang-remaining-duels", "wudang-zhang-win", "foothill-rescue", "wudang-herb-dialogues", "wudang-herbs", "cave-entry", "cave-battles", "cave-chief-second-round", "huian-sidequests", "huian-night", "cangjian-first-visit", "cangjian-first-defeat", "hanbo-first-visit", "huian-date-arrival", "huian-singers", "huian-fortune-singers", "huian-date-orders", "qingping-rescue", "qingping-missing-boy", "huian-investigation", "hanbo-fire-battle", "qingping-beggar", "qingping-beggar-arrival", "qingping-beggar-return", "pili-entry", "pili-day-route", "pili-night-route", "pili-timeout", "yaowang-heal", "qingping-clue", "cangjian-truth", "qingping-beggar-insufficient", "huian-knife", "huian-knife-missing", "huian-fishing-hook", "forget-worry-herbs", "forget-worry-care", "forget-worry-forbidden", "pili-boxes", "north-cave-descent", "basin-martial", "island-departure", "zixuan-search", "yue-meier-rescue", "forget-worry-return", "heart-demon-first", "heart-demon-second", "heart-demon-third", "heart-demon-blade", "cangjian-final-duels", "hanbo-reunion", "tianshan-traps", "tianchi-hairpin", "tianchi-hairpin-no-affection", "tianchi-hairpin-four-roses", "tianchi-hairpin-roses", "tianshan-delayed-trap", "meng-challenge-defeat", "meng-challenge-win", "meng-challenge-kite-win", "petrify-weapon-preparation", "petrify-weapon-repeat", "cangjian-shoes", "cangjian-shoes-insufficient", "forget-worry-scholar", "tianchi-timeout", "ending-junction", "ending-junction-low-evil", "ending-junction-evil-boundary", "hermit-amulet-early", "hanbo-hermit", "hanbo-hermit-owned", "hanbo-hermit-after-tenth", "meng-challenge-recovery", "tianchi-revisit", "ending-two-zixuan", "ending-four-preparation", "pre-letter-flowers", "ending-four-cloud-book", "ending-two-fortress", "ending-one-marriage", "ending-one-wedding-night", "ending-two-rescue", "late-huian-flowers", "huian-flowers-insufficient", "ending-one-aftermath", "ending-one-ambush-win", "ending-one-zixuan-duel", "ending-one-captive-source", "ending-one-captive-choices", "ending-one-captive-win", "ending-one-captive-reject", "ending-one-forget-worry", "ending-two-moon-letter", "ending-two-moon-duel", "ending-two-moon-care", "ending-two-silver-grass", "ending-two-mother-letter", "ending-two-letter-aftermath", "ending-one-moon-choices", "ending-one-moon-reject", "ending-one-island-raiders", "ending-one-island-raiders-spare", "ending-one-moon-room", "ending-one-meng-infiltration", "ending-two-wudang-truth", "ending-two-island-search", "ending-two-ambush", "ending-two-meng-help", "ending-two-tower-offers", "ending-two-zixuan-rescue", "ending-two-fortress-rescue", "ending-one-wudang-attack", "ending-one-tower-entry", "ending-one-tower-switches", "ending-one-tower-rescue", "ending-one-finale", "ending-one-dream-defeat", "ending-two-rose-reunion", "ending-two-rose-invitations", "ending-two-rose-care", "ending-two-hanbo-pickup", "ending-two-finale"), default="departure")
    parser.add_argument("--final-offer", type=int, choices=(0, 1), default=0)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--cheat-assisted", action="store_true", help="Label assisted routes and explicitly admit assisted parents/evidence; does not change game state")
    parser.add_argument("--parent", type=Path, help="Continue a normal save in a new independent run directory")
    parser.add_argument("--load-slot", type=int, choices=range(7))
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    output, assets = args.output.resolve(), args.assets.resolve()
    resource = assets / "yycs"
    parent_identity = None
    if args.parent:
        if args.resume or args.load_slot is None or args.chapter == "opening":
            parser.error("A saved-game child requires --load-slot and a later chapter, without --resume")
        parent = args.parent.resolve()
        parent_identity = json.loads((parent / "run.json").read_text(encoding="utf-8"))
        if (parent_identity.get("resourceId") != "YYCS" or not isinstance(parent_identity.get("cheatAssisted"), bool)
                or parent_identity["cheatAssisted"] and not args.cheat_assisted):
            raise ValueError("Parent requires a YYCS save and explicit admission of cheat assistance")
        if Path(parent_identity["command"][parent_identity["command"].index("--assets") + 1]) != assets:
            raise ValueError("Parent and child assets differ")
        if output.exists():
            raise FileExistsError(output)
        if process_running(parent_identity["pid"], parent_identity["command"][0]):
            with Client(parent_identity["session"], transcript=parent / "commands.jsonl") as parent_client:
                parent_client.exit_game()
            deadline = time.monotonic() + 15
            while process_running(parent_identity["pid"], parent_identity["command"][0]):
                if time.monotonic() >= deadline:
                    raise TimeoutError("Normal parent exit did not complete; saves were not copied")
                time.sleep(0.1)
        args.exe = Path(parent_identity["command"][0])
    if args.inventory_only:
        output.mkdir(parents=True, exist_ok=False)
        print(json.dumps(save_inventory(resource, output, args.evidence, allow_cheats=args.cheat_assisted)["counts"], ensure_ascii=False))
        return
    if args.resume:
        if args.chapter == "opening":
            parser.error("Opening must be tested from a fresh normal new game")
        identity = json.loads((output / "run.json").read_text(encoding="utf-8"))
        if identity["resourceId"] != "YYCS":
            raise ValueError("Resume directory is not a YYCS run")
        if Path(identity["command"][identity["command"].index("--assets") + 1]) != assets:
            raise ValueError("Resume assets differ from the recorded resource directory")
        if args.cheat_assisted:
            identity["cheatAssisted"] = True
            write_json(output / "run.json", identity)
    else:
        if not args.exe:
            parser.error("A fresh run requires --exe")
        output.mkdir(parents=True, exist_ok=False)
        executable, session = args.exe.resolve(), str(uuid.uuid4())
        catalog = save_inventory(resource, output)
        if parent_identity:
            shutil.copytree(parent / "user-data/save", output / "user-data/save")
            source_hashes = {path.relative_to(parent / "user-data/save").as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                             for path in (parent / "user-data/save").rglob("*") if path.is_file()}
            copied_hashes = {path.relative_to(output / "user-data/save").as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                             for path in (output / "user-data/save").rglob("*") if path.is_file()}
            if source_hashes != copied_hashes:
                raise AutomationError("Normal saved-game copy did not retain its bytes")
            write_json(output / "normal-save-clone.json", dict(parentRun=str(parent), sourceSlot=args.load_slot,
                       exitVerified=True, hashes=source_hashes, cheatAssisted=False))
            source_names = ("410-ending-one-island-return-source-slot0.json", "411-ending-one-island-raiders-source-slot1.json")
            if (args.chapter in ("ending-one-island-raiders", "ending-one-island-raiders-spare") and args.load_slot == 1
                    and all((parent / name).is_file() for name in source_names)):
                for name in source_names:
                    shutil.copyfile(parent / name, output / name)
        command = [str(executable), "--assets", str(assets), "--resource-id", "YYCS",
                   "--skip-startup-video", "--enable-automation-hooks", "--automation-pipe", session,
                   "--user-data-root", str(output / "user-data"), "--log-file", str(output / "game.log")]
        with (output / "stdout.log").open("wb") as stdout, (output / "stderr.log").open("wb") as stderr:
            process = subprocess.Popen(command, cwd=executable.parent, stdout=stdout, stderr=stderr)
        identity = dict(resourceId="YYCS", session=session, pid=process.pid, command=command,
                        engineSha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                        resourceProfileSha256=hashlib.sha256((resource / "game_profile.ini").read_bytes()).hexdigest(),
                        resourceScriptsSha256=hashlib.sha256(json.dumps(catalog["sources"], sort_keys=True).encode()).hexdigest(),
                        difficulty=args.difficulty, cheatAssisted=args.cheat_assisted)
        if parent_identity:
            identity.update(parentRun=str(parent), parentSlot=args.load_slot, normalSaveClone=True,
                            difficulty=parent_identity["difficulty"])
        write_json(output / "run.json", identity)
    snapshot = output / f"route-{time.time_ns()}.py"
    shutil.copyfile(Path(__file__), snapshot)
    result = dict(status="running", chapter=args.chapter, difficulty=identity["difficulty"],
                  fullCoverage=False, fullPlaythrough=False, cheatAssisted=identity.get("cheatAssisted", False), resumed=args.resume,
                  routeSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), started=time.time())
    if args.resume:
        previous = [json.loads(path.read_text(encoding="utf-8")) for path in sorted(output.glob("result-*.json"))]
        result["completedChapters"] = list(dict.fromkeys(
            chapter for recorded in previous for chapter in recorded.get("completedChapters", [])))
    result["routeSnapshot"] = str(snapshot)
    write_json(output / "progress.json", result)
    with Client(identity["session"], timeout=20, transcript=output / "commands.jsonl") as client:
        try:
            if args.resume or parent_identity:
                state = client.observe()
                if any(item["name"] == "return-to-title" for item in state.get("ui", [])):
                    client.ui("Cancel")
                client.act("SetAutoDialogue", enabled=True)
                if args.load_slot is not None:
                    client.wait_until(lambda value: not value.get("loading"), timeout=60)
                    load_checkpoint(client, args.load_slot)
                    client.act("SetAutoDialogue", enabled=True)
            else:
                opening(client, output, args.difficulty)
                result["completedChapters"] = ["opening"]
                write_json(output / "progress.json", result)
            if args.chapter == "departure" or (args.chapter == "wudang-introduction" and not args.resume):
                departure(client, output, resource)
                result.setdefault("completedChapters", []).append("departure")
            if args.chapter == "wudang-introduction":
                wudang_introduction(client, output, resource)
                result.setdefault("completedChapters", []).append("wudang-introduction")
            if args.chapter == "wudang-pool":
                wudang_pool(client, output, resource)
                result.setdefault("completedChapters", []).append("wudang-pool")
            if args.chapter == "wudang-gate":
                wudang_gate(client, output, resource)
                result.setdefault("completedChapters", []).append("wudang-gate")
            if args.chapter == "wudang-challenge":
                wudang_challenge(client, output, resource)
                result.setdefault("completedChapters", []).append("wudang-challenge")
            if args.chapter == "wudang-remaining-duels":
                wudang_remaining_duels(client, output, resource)
                result.setdefault("completedChapters", []).append("wudang-remaining-duels")
            if args.chapter == "wudang-zhang-win":
                wudang_zhang_win(client, output, resource)
                result.setdefault("completedChapters", []).append("wudang-zhang-win")
            if args.chapter == "foothill-rescue":
                foothill_rescue(client, output, resource)
                result.setdefault("completedChapters", []).append("foothill-rescue")
            if args.chapter == "wudang-herb-dialogues":
                wudang_herb_dialogues(client, output, resource)
                result.setdefault("completedChapters", []).append("wudang-herb-dialogues")
            if args.chapter == "wudang-herbs":
                wudang_herbs(client, output, resource)
                result.setdefault("completedChapters", []).append("wudang-herbs")
            if args.chapter == "cave-entry":
                cave_entry(client, output, resource)
                result.setdefault("completedChapters", []).append("cave-entry")
            if args.chapter == "cave-battles":
                cave_battles(client, output, resource)
                result.setdefault("completedChapters", []).append("cave-battles")
            if args.chapter == "cave-chief-second-round":
                cave_second_round(client, output, resource, "chief")
                result.setdefault("completedChapters", []).append("cave-chief-second-round")
            if args.chapter == "huian-sidequests":
                huian_sidequests(client, output, resource)
                result.setdefault("completedChapters", []).append("huian-sidequests")
            if args.chapter == "huian-night":
                huian_night(client, output, resource)
                result.setdefault("completedChapters", []).append("huian-night")
            if args.chapter == "cangjian-first-visit":
                cangjian_first_visit(client, output, resource)
                result.setdefault("completedChapters", []).append("cangjian-first-visit")
            if args.chapter == "hanbo-first-visit":
                hanbo_first_visit(client, output, resource)
                result.setdefault("completedChapters", []).append("hanbo-first-visit")
            if args.chapter == "cangjian-first-defeat":
                cangjian_first_defeat(client, output, resource)
                result.setdefault("completedChapters", []).append("cangjian-first-defeat")
            if args.chapter == "huian-date-arrival":
                huian_date_arrival(client, output, resource)
                result.setdefault("completedChapters", []).append("huian-date-arrival")
            if args.chapter == "qingping-beggar-return":
                qingping_beggar_return(client, output, resource)
                result.setdefault("completedChapters", []).append("qingping-beggar-return")
            if args.chapter == "qingping-beggar-arrival":
                state = idle(client)
                if int(state["variables"].get("SubEvent03") or 0) != 0 or int(state["variables"].get("Sub03TalkTimes") or 0) != 0:
                    raise AutomationError("Beggar arrival must retain its untouched donation source")
                huian_date_orders(client, output, resource, single_order=True)
                result.setdefault("completedChapters", []).append("qingping-beggar-arrival")
            if args.chapter == "qingping-beggar-insufficient":
                qingping_beggar_insufficient(client, output, resource)
                result.setdefault("completedChapters", []).append("qingping-beggar-insufficient")
            if args.chapter == "huian-knife-missing":
                huian_knife_missing(client, output, resource)
                result.setdefault("completedChapters", []).append("huian-knife-missing")
            if args.chapter == "huian-knife":
                huian_knife(client, output, resource)
                result.setdefault("completedChapters", []).append("huian-knife")
            if args.chapter == "basin-martial":
                basin_martial(client, output, resource)
                result.setdefault("completedChapters", []).append("basin-martial")
            if args.chapter == "island-departure":
                island_departure(client, output, resource)
                result.setdefault("completedChapters", []).append("island-departure")
            if args.chapter == "zixuan-search":
                zixuan_search(client, output, resource)
                result.setdefault("completedChapters", []).append("zixuan-search")
            if args.chapter == "yue-meier-rescue":
                yue_meier_rescue(client, output, resource)
                result.setdefault("completedChapters", []).append("yue-meier-rescue")
            if args.chapter == "forget-worry-return":
                forget_worry_return(client, output, resource)
                result.setdefault("completedChapters", []).append("forget-worry-return")
            if args.chapter == "heart-demon-first":
                heart_demon_first(client, output, resource)
                result.setdefault("completedChapters", []).append("heart-demon-first")
            if args.chapter == "heart-demon-second":
                heart_demon_second(client, output, resource)
                result.setdefault("completedChapters", []).append("heart-demon-second")
            if args.chapter == "heart-demon-third":
                heart_demon_third(client, output, resource)
                result.setdefault("completedChapters", []).append("heart-demon-third")
            if args.chapter == "heart-demon-blade":
                heart_demon_blade(client, output, resource)
                result.setdefault("completedChapters", []).append("heart-demon-blade")
            if args.chapter == "cangjian-final-duels":
                cangjian_final_duels(client, output, resource)
                result.setdefault("completedChapters", []).append("cangjian-final-duels")
            if args.chapter == "hanbo-reunion":
                hanbo_reunion(client, output, resource)
                result.setdefault("completedChapters", []).append("hanbo-reunion")
            if args.chapter == "tianshan-traps":
                tianshan_traps(client, output, resource)
                result.setdefault("completedChapters", []).append("tianshan-traps")
            if args.chapter in ("tianchi-hairpin", "tianchi-hairpin-no-affection", "tianchi-hairpin-four-roses", "tianchi-hairpin-roses"):
                roses = args.roses if args.chapter == "tianchi-hairpin-roses" else 4 if args.chapter == "tianchi-hairpin-four-roses" else 0
                tianchi_hairpin(client, output, resource, affection=args.chapter != "tianchi-hairpin-no-affection", roses=roses)
                result.setdefault("completedChapters", []).append(args.chapter)
            if args.chapter == "tianshan-delayed-trap":
                tianshan_delayed_trap(client, output, resource)
                result.setdefault("completedChapters", []).append("tianshan-delayed-trap")
            if args.chapter == "tianchi-revisit":
                tianchi_revisit(client, output, resource)
                result.setdefault("completedChapters", []).append("tianchi-revisit")
            if args.chapter == "tianchi-timeout":
                tianchi_timeout(client, output, resource)
                result.setdefault("completedChapters", []).append("tianchi-timeout")
            if args.chapter == "ending-four-cloud-book":
                ending_four_cloud_book(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-four-cloud-book")
            if args.chapter == "petrify-weapon-repeat":
                petrify_weapon_preparation(client, output, resource, repeat_visitor=True)
                result.setdefault("completedChapters", []).append("petrify-weapon-repeat")
            if args.chapter == "petrify-weapon-preparation":
                petrify_weapon_preparation(client, output, resource)
                result.setdefault("completedChapters", []).append("petrify-weapon-preparation")
            if args.chapter == "pre-letter-flowers":
                pre_letter_flowers(client, output, resource, roses=args.roses)
                result.setdefault("completedChapters", []).append("pre-letter-flowers")
            if args.chapter == "ending-four-preparation":
                ending_four_preparation(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-four-preparation")
            if args.chapter == "ending-one-tower-rescue":
                ending_one_tower_rescue(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-one-tower-rescue")
            if args.chapter in ("ending-one-finale", "ending-one-dream-defeat"):
                ending_one_finale(client, output, resource, defeat=args.chapter == "ending-one-dream-defeat")
                result.setdefault("completedChapters", []).append(args.chapter)
            if args.chapter == "ending-one-tower-switches":
                ending_one_tower_switches(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-one-tower-switches")
            if args.chapter == "ending-one-tower-entry":
                ending_one_tower_entry(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-one-tower-entry")
            if args.chapter == "ending-one-wudang-attack":
                ending_one_wudang_attack(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-one-wudang-attack")
            if args.chapter == "ending-one-meng-infiltration":
                ending_one_meng_infiltration(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-one-meng-infiltration")
            if args.chapter == "ending-one-moon-room":
                ending_one_moon_room(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-one-moon-room")
            if args.chapter in ("ending-one-island-raiders", "ending-one-island-raiders-spare"):
                ending_one_island_raiders(client, output, resource, spare=args.chapter.endswith("-spare"))
                result.setdefault("completedChapters", []).append(args.chapter)
            if args.chapter in ("ending-one-moon-choices", "ending-one-moon-reject"):
                ending_one_moon_choices(client, output, resource, reject=args.chapter == "ending-one-moon-reject")
                result.setdefault("completedChapters", []).append(args.chapter)
            if args.chapter == "ending-one-forget-worry":
                ending_one_forget_worry(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-one-forget-worry")
            if args.chapter == "ending-one-captive-reject":
                ending_one_captive_reject(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-one-captive-reject")
            if args.chapter in ("ending-one-captive-choices", "ending-one-captive-win"):
                ending_one_captive_choices(client, output, resource, win=args.chapter == "ending-one-captive-win")
                result.setdefault("completedChapters", []).append(args.chapter)
            if args.chapter == "ending-one-captive-source":
                ending_one_captive_source(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-one-captive-source")
            if args.chapter == "ending-one-zixuan-duel":
                ending_one_zixuan_duel(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-one-zixuan-duel")
            if args.chapter in ("ending-one-aftermath", "ending-one-ambush-win"):
                ending_one_aftermath(client, output, resource, win=args.chapter == "ending-one-ambush-win")
                result.setdefault("completedChapters", []).append(args.chapter)
            if args.chapter == "huian-flowers-insufficient":
                huian_flowers_insufficient(client, output, resource)
                result.setdefault("completedChapters", []).append("huian-flowers-insufficient")
            if args.chapter == "late-huian-flowers":
                late_huian_flowers(client, output, resource)
                result.setdefault("completedChapters", []).append("late-huian-flowers")
            if args.chapter == "ending-one-wedding-night":
                ending_one_wedding_night(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-one-wedding-night")
            if args.chapter == "ending-two-island-search":
                ending_two_island_search(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-two-island-search")
            if args.chapter == "ending-two-ambush":
                ending_two_ambush(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-two-ambush")
            if args.chapter == "ending-two-meng-help":
                ending_two_meng_help(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-two-meng-help")
            if args.chapter == "ending-two-tower-offers":
                ending_two_tower_offers(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-two-tower-offers")
            if args.chapter == "ending-two-zixuan-rescue":
                ending_two_zixuan_rescue(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-two-zixuan-rescue")
            if args.chapter == "ending-two-rose-reunion":
                ending_two_rose_reunion(client, output, resource)
                result.setdefault("completedChapters", []).append(args.chapter)
            if args.chapter == "ending-two-rose-invitations":
                ending_two_rose_invitations(client, output, resource)
                result.setdefault("completedChapters", []).append(args.chapter)
            if args.chapter == "ending-two-hanbo-pickup":
                ending_two_hanbo_pickup(client, output, resource)
                result.setdefault("completedChapters", []).append(args.chapter)
            if args.chapter == "ending-two-finale":
                ending_two_finale(client, output, resource, args.final_offer)
                result.setdefault("completedChapters", []).append(args.chapter)
            if args.chapter == "ending-two-rose-care":
                ending_two_rose_care(client, output, resource)
                result.setdefault("completedChapters", []).append(args.chapter)
            if args.chapter == "ending-two-fortress-rescue":
                ending_two_fortress_rescue(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-two-fortress-rescue")
            if args.chapter == "ending-two-wudang-truth":
                ending_two_wudang_truth(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-two-wudang-truth")
            if args.chapter == "ending-two-letter-aftermath":
                ending_two_letter_aftermath(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-two-letter-aftermath")
            if args.chapter == "ending-two-mother-letter":
                ending_two_mother_letter(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-two-mother-letter")
            if args.chapter == "ending-two-silver-grass":
                ending_two_silver_grass(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-two-silver-grass")
            if args.chapter == "ending-two-moon-care":
                ending_two_moon_care(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-two-moon-care")
            if args.chapter == "ending-two-moon-duel":
                ending_two_moon_duel(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-two-moon-duel")
            if args.chapter == "ending-two-moon-letter":
                ending_two_moon_letter(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-two-moon-letter")
            if args.chapter == "ending-two-rescue":
                ending_two_rescue(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-two-rescue")
            if args.chapter == "ending-one-marriage":
                ending_one_marriage(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-one-marriage")
            if args.chapter == "ending-two-fortress":
                ending_two_fortress(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-two-fortress")
            if args.chapter == "ending-two-zixuan":
                ending_two_zixuan(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-two-zixuan")
            if args.chapter in ("ending-junction-low-evil", "ending-junction-evil-boundary"):
                ending_junction_low_evil(client, output, resource, boundary=args.chapter.endswith("-boundary"))
                result.setdefault("completedChapters", []).append(args.chapter)
            if args.chapter == "ending-junction":
                ending_junction(client, output, resource)
                result.setdefault("completedChapters", []).append("ending-junction")
            if args.chapter == "meng-challenge-recovery":
                meng_challenge_defeat(client, output, resource, farewell=False)
                result.setdefault("completedChapters", []).append("meng-challenge-recovery")
            if args.chapter in ("meng-challenge-defeat", "meng-challenge-win", "meng-challenge-kite-win"):
                meng_challenge_defeat(client, output, resource, win=args.chapter != "meng-challenge-defeat", kite=args.chapter == "meng-challenge-kite-win")
                result.setdefault("completedChapters", []).append(args.chapter)
            if args.chapter == "cangjian-shoes-insufficient":
                cangjian_shoes_insufficient(client, output, resource)
                result.setdefault("completedChapters", []).append("cangjian-shoes-insufficient")
            if args.chapter == "cangjian-shoes":
                cangjian_shoes(client, output, resource)
                result.setdefault("completedChapters", []).append("cangjian-shoes")
            if args.chapter == "huian-fishing-hook":
                huian_fishing_hook(client, output, resource)
                result.setdefault("completedChapters", []).append(args.chapter)
            if args.chapter == "hermit-amulet-early":
                hermit_amulet_early(client, output, resource)
                result.setdefault("completedChapters", []).append(args.chapter)
            if args.chapter in ("hanbo-hermit-owned", "hanbo-hermit-after-tenth"):
                hanbo_hermit(client, output, resource, own_before=10 if args.chapter == "hanbo-hermit-owned" else 11)
                result.setdefault("completedChapters", []).append(args.chapter)
            if args.chapter == "hanbo-hermit":
                hanbo_hermit(client, output, resource)
                result.setdefault("completedChapters", []).append("hanbo-hermit")
            if args.chapter == "forget-worry-scholar":
                forget_worry_scholar(client, output, resource)
                result.setdefault("completedChapters", []).append("forget-worry-scholar")
            if args.chapter == "north-cave-descent":
                north_cave_descent(client, output, resource)
                result.setdefault("completedChapters", []).append("north-cave-descent")
            if args.chapter == "pili-boxes":
                pili_boxes(client, output, resource)
                result.setdefault("completedChapters", []).append("pili-boxes")
            if args.chapter == "forget-worry-forbidden":
                forget_worry_forbidden(client, output, resource)
                result.setdefault("completedChapters", []).append("forget-worry-forbidden")
            if args.chapter == "forget-worry-care":
                forget_worry_care(client, output, resource)
                result.setdefault("completedChapters", []).append("forget-worry-care")
            if args.chapter == "forget-worry-herbs":
                forget_worry_herbs(client, output, resource)
                result.setdefault("completedChapters", []).append("forget-worry-herbs")
            if args.chapter == "qingping-clue":
                qingping_clue(client, output, resource)
                result.setdefault("completedChapters", []).append("qingping-clue")
            if args.chapter == "cangjian-truth":
                cangjian_truth(client, output, resource)
                result.setdefault("completedChapters", []).append("cangjian-truth")
            if args.chapter == "pili-night-route":
                pili_night_route(client, output, resource)
                result.setdefault("completedChapters", []).append("pili-night-route")
            if args.chapter == "pili-timeout":
                pili_timeout(client, output, resource)
                result.setdefault("completedChapters", []).append("pili-timeout")
            if args.chapter == "yaowang-heal":
                yaowang_heal(client, output, resource)
                result.setdefault("completedChapters", []).append("yaowang-heal")
            if args.chapter == "pili-day-route":
                pili_day_route(client, output, resource)
                result.setdefault("completedChapters", []).append("pili-day-route")
            if args.chapter == "pili-entry":
                pili_entry(client, output, resource)
                result.setdefault("completedChapters", []).append("pili-entry")
            if args.chapter == "qingping-beggar":
                qingping_beggar(client, output, resource)
                result.setdefault("completedChapters", []).append("qingping-beggar")
            if args.chapter == "hanbo-fire-battle":
                hanbo_fire_battle(client, output, resource)
                result.setdefault("completedChapters", []).append("hanbo-fire-battle")
            if args.chapter == "huian-investigation":
                huian_investigation(client, output, resource)
                result.setdefault("completedChapters", []).append("huian-investigation")
            if args.chapter == "qingping-missing-boy":
                qingping_missing_boy(client, output, resource)
                result.setdefault("completedChapters", []).append("qingping-missing-boy")
            if args.chapter == "qingping-rescue":
                qingping_rescue(client, output, resource)
                result.setdefault("completedChapters", []).append("qingping-rescue")
            if args.chapter == "huian-date-orders":
                huian_date_orders(client, output, resource)
                result.setdefault("completedChapters", []).append("huian-date-orders")
            if args.chapter == "huian-fortune-singers":
                huian_fortune_singers(client, output, resource)
                result.setdefault("completedChapters", []).append("huian-fortune-singers")
            if args.chapter == "huian-singers":
                huian_singers(client, output, resource)
                result.setdefault("completedChapters", []).append("huian-singers")
            if args.chapter not in result.get("completedChapters", []):
                raise AutomationError(f"Requested chapter did not execute: {args.chapter}")
            result.update(status="passed", finalState=checkpoint(client, output, "09-final-checkpoint"))
        except Exception as error:
            result.update(status="failed", error=str(error))
            try:
                name = f"failure-{args.chapter}-{time.time_ns()}"
                checkpoint(client, output, name)
                result["failureCheckpoint"] = str(output / f"{name}.json")
            except Exception as evidence_error:
                result["evidenceError"] = str(evidence_error)
        finally:
            result["finished"] = time.time()
            write_json(output / f"result-{int(time.time())}.json", result)
            write_json(output / "progress.json", result)
            print(json.dumps({key: value for key, value in result.items() if key != "finalState"}, ensure_ascii=False), flush=True)
    raise SystemExit(0 if result["status"] == "passed" else 1)


if __name__ == "__main__":
    main()
