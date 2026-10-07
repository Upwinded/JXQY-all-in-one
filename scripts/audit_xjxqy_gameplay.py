"""Recheck native XJXQY command/trace evidence against the current resources."""
import argparse
import hashlib
import json
import operator
from pathlib import Path
import re

from run_xjxqy_gameplay import catalog_for, title_visible, write_json
from run_yycs_gameplay import choice_variable_changes, source_line


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def lines(path):
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]


def check_choice(directory, resource, site, proof, before, after, trace, commands):
    option, action = proof["choiceIndex"], proof["action"]["actionId"]
    identity = read(directory / "run.json")
    if (identity["resourceId"] != "XJXQY" or not identity["session"].startswith("xjxqy-")
            or proof["cheatAssisted"] != identity["cheatAssisted"]
            or before.get("choiceMessage") != site["message"]
            or [row["text"] for row in before.get("choices", [])] != [row["text"] for row in site["options"]]
            or not 0 <= option < len(site["options"])):
        raise ValueError(f"Choice identity or snapshots mismatch: {directory} / {site['id']}")
    start = proof.get("scriptStart", proof.get("script", {}).get("start"))
    if (start in trace and start["virtualPath"] == site["path"]
            and start["contentSha256"] != site["sourceSha256"]):
        return None  # Historical source evidence cannot count toward the current resource.
    execution = [row for row in trace if row.get("executionId") == start["executionId"]]
    finish = [row for row in execution if row.get("eventType") == "script.finish"]
    calls = [row for row in execution if row.get("apiName") == site["api"]]
    source = [row for row in execution if row.get("eventType") == "source.line" and row["line"] == site["line"]]
    if (start not in trace or start["virtualPath"] != site["path"]
            or start["contentSha256"] != site["sourceSha256"]
            or len(finish) != 1 or finish[0].get("status") != "completed" or not calls or not source):
        raise ValueError(f"Choice has no matching complete current execution: {directory} / {site['id']}")
    changes = choice_variable_changes(execution, source[0]["sequence"], site["variable"])
    if (changes and changes[0].get("afterValue") != str(option)
            or not changes and before.get("variables", {}).get(site["variable"]) != str(option)):
        raise ValueError(f"Choice variable was not assigned natively: {directory} / {site['id']}")
    selected = [row for row in commands if row["request"]["command"] == "Choose"
                and row["request"]["arguments"] == dict(context=before["context"], options=[option])
                and row["response"].get("ok") and row["response"]["data"].get("actionId") == action]
    succeeded = [row for row in commands if row["request"]["command"] == "GetActionStatus"
                 and row["response"].get("data", {}).get("actionId") == action
                 and row["response"]["data"].get("status") == "succeeded"]
    if len(selected) != 1 or not succeeded:
        raise ValueError(f"Choice action did not succeed: {directory} / {site['id']}")
    if not (after.get("worldInput") or title_visible(after) and proof.get("expectedTerminal")):
        raise ValueError(f"Choice has no settled outcome: {directory} / {site['id']}")
    return dict(run=str(directory), choiceSite=site["id"], option=option,
                executionId=start["executionId"], sourceSha256=site["sourceSha256"],
                variableChange=changes[0] if changes else None, cheatAssisted=identity["cheatAssisted"])


def branch_destinations(resource, site):
    text = [source_line(line).strip() for line in (resource / site["path"]).read_text(encoding="utf-8-sig").splitlines()]
    match = re.fullmatch(r"if .+ then goto (\w+) end", text[site["line"] - 1])
    if not match:
        return None
    def executable_after(index):
        return next((i + 1 for i in range(index + 1, len(text))
                     if text[i] and not text[i].startswith("::")), None)
    labels = [i for i, line in enumerate(text) if line == "::" + match[1] + "::"]
    if len(labels) != 1:
        return None
    taken, fallthrough = executable_after(labels[0]), executable_after(site["line"] - 1)
    return dict(taken=taken, fallthrough=fallthrough) if taken != fallthrough else None


def complete_session_trace(trace):
    return bool(trace and isinstance(trace[0].get('sessionId'), str) and trace[0]['sessionId']
                and trace[0].get('eventType') == 'session.start'
                and trace[-1].get('eventType') == 'session.finish'
                and trace[-1].get('status') == 'completed'
                and all(event.get('sequence') == index
                        and event.get('sessionId') == trace[0].get('sessionId')
                        for index, event in enumerate(trace, 1)))


def traced_condition_values(trace, sites, initial_unknown_is_zero=False):
    """Evaluate traced integer reads; reviewed complete sessions also prove unseen keys are zero."""
    comparisons = {'==': operator.eq, '~=': operator.ne, '<': operator.lt,
                   '>': operator.gt, '<=': operator.le, '>=': operator.ge}
    parsed = {}
    for site in sites:
        match = re.fullmatch(r'if getvar\("([^"\n]+)"\) (==|~=|<=|>=|<|>) (-?\d+) then goto \w+ end', site['source'])
        if match:
            parsed[(site['path'], site['line'])] = (site, match[1], comparisons[match[2]], int(match[3]))
    initial_unknown_is_zero = initial_unknown_is_zero and complete_session_trace(trace)
    values, sources, pending, outcomes = {}, {}, {}, {}
    for event in trace:
        kind, execution = event.get('eventType'), event.get('executionId')
        if kind == 'variable.change':
            # load() and clearExcept() also report removed keys as empty values.
            values[event['variableName']] = (event['afterValue'], event['sequence'])
        elif kind == 'script.start':
            sources[execution] = event['virtualPath']
            pending.pop(execution, None)
        elif kind == 'source.line':
            pending[execution] = (parsed.get((sources.get(execution), event['line'])), event['sequence'])
        elif kind == 'api.call':
            candidate, sequence = pending.pop(execution, (None, None))
            if event.get('apiName') != 'getvar' or candidate is None:
                continue
            site, name, compare, operand = candidate
            default_zero = name not in values and initial_unknown_is_zero
            value, changed = values.get(name, ('', None) if default_zero else (None, None))
            if value is None or value and not re.fullmatch(r'-?(?:0|[1-9]\d*)', value):
                continue
            value = int(value) if value else 0
            if not -(2 ** 31) <= value < 2 ** 31:
                continue
            side = 'taken' if compare(value, operand) else 'fallthrough'
            outcomes.setdefault(execution, []).append((site, side, dict(sequence=sequence,
                variableName=name, variableValue=value, variableChangeSequence=changed,
                getVarSequence=event['sequence'], inference=('complete-reviewed-runtime-default-zero-at-getvar'
                    if default_zero else 'traced-integer-variable-at-getvar'))))
        elif kind == 'script.finish':
            sources.pop(execution, None)
            pending.pop(execution, None)
    return outcomes


def audit(resource, directories, initial_zero_engine_hashes=()):
    catalog = catalog_for(resource)
    choices = {site["id"]: site for site in catalog["choices"]}
    source_hashes = {row["path"]: row["sha256"] for row in catalog["sources"]}
    conditions = {}
    for site in catalog["conditions"]:
        destinations = branch_destinations(resource, site)
        if destinations:
            conditions[(site["path"], site["line"])] = (site, destinations)
    completed, verified, endings, stale_choices = [], [], [], []
    variable_comparisons_checked = added_variable_outcomes = 0
    for directory in directories:
        identity = read(directory / "run.json")
        if identity["resourceId"] != "XJXQY" or not identity["session"].startswith("xjxqy-"):
            raise ValueError(f"Not an isolated XJXQY run: {directory}")
        trace, commands = lines(directory / "user-data/automation/trace.jsonl"), lines(directory / "commands.jsonl")
        proof_paths = [directory / "opening-proof.json"] if (directory / "opening-proof.json").exists() else []
        proof_paths += sorted(directory.glob("choice-proof-*.json"))
        for path in proof_paths:
            proof = read(path)
            if proof["status"] != "passed":
                continue
            site = choices[proof["choiceSite"]]
            opening = path.name == "opening-proof.json"
            before = read(directory / "02-difficulty-choice.json") if opening else read(Path(proof["beforeFile"]))
            after = read(directory / "03-opening-complete.json") if opening else read(Path(proof["afterFile"]))
            pngs = ("02-difficulty-choice.png", "03-opening-complete.png") if opening else (
                Path(proof["beforeFile"]).with_suffix(".png"), Path(proof["afterFile"]).with_suffix(".png"))
            for png in pngs:
                image = directory / png
                if image.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
                    raise ValueError(f"Required PNG is missing or invalid: {image}")
            checked = check_choice(directory, resource, site, proof, before, after, trace, commands)
            if checked is None:
                stale_choices.append(str(path))
                continue
            verified.append(checked)
            option = site["options"][proof["choiceIndex"]]
            key = "assistedEvidence" if identity["cheatAssisted"] else "normalEvidence"
            option.setdefault(key, []).append(str(path))
            option["status"] = "passed" if option.get("normalEvidence") else "assisted-passed"
        for path in directory.glob("ending-proof-*.json"):
            proof = read(path)
            if proof.get("status") != "passed":
                continue
            source = proof["expectedTerminal"]
            start = proof["script"]["start"]
            execution = [row for row in trace if row.get("executionId") == start["executionId"]]
            finish = [row for row in execution if row.get("eventType") == "script.finish"]
            dialogue, video, title = [read(Path(proof[key])) for key in ("finalDialogueFile", "videoFile", "titleFile")]
            if proof["endingId"] == "E01":
                branch_verified = (source == "script/map/map026_临安城何员外家/trap02.txt"
                                   and "独孤剑与何梅结为夫妻以后" in dialogue.get("dialogue", {}).get("text", "")
                                   and any(row["run"] == str(directory) and row["executionId"] == start["executionId"] and row["option"] == 0
                                           and row["choiceSite"] == source + ":44" for row in verified))
            elif proof["endingId"] == "E02":
                before = read(Path(proof["beforeFile"]))
                branch_verified = (source == "script/map/map081_凤凰山/方勉第二次死亡.txt"
                                   and (before["variables"].get("End") != "222"
                                        or before["variables"].get("NoEnd") == "1")
                                   and "独自离去，从此不再过问江湖中事" in dialogue.get("dialogue", {}).get("text", "")
                                   and any(row["run"] == str(directory) and row["executionId"] == start["executionId"] and row["option"] == 1
                                           and choices[row["choiceSite"]]["path"] == source for row in verified))
                if Path(proof["beforeFile"]).with_suffix(".png").read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
                    raise ValueError(f"Ending branch PNG missing: {path}")
            elif proof["endingId"] == "E03":
                before = read(Path(proof["beforeFile"]))
                executed_lines = {row["line"] for row in execution if row.get("eventType") == "source.line"}
                branch_verified = (source == "script/map/map120_风波亭/秦桧死亡.txt"
                                   and before["variables"].get("Result") == "3"
                                   and {3, 7} <= executed_lines and 4 not in executed_lines
                                   and "独孤剑死后，杨瑛黯然回到天王帮" in dialogue.get("dialogue", {}).get("text", ""))
                if Path(proof["beforeFile"]).with_suffix(".png").read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
                    raise ValueError(f"Ending branch PNG missing: {path}")
            elif proof["endingId"] in ("E04", "E06"):
                before = read(Path(proof["beforeFile"]))
                variables = before["variables"]
                executed_lines = {row["line"] for row in execution if row.get("eventType") == "source.line"}
                text = dialogue.get("dialogue", {}).get("text", "")
                branch_verified = source == "script/map/map119_玉皇峰/trap02.txt" and before.get("map") == "map119_玉皇峰.map"
                if proof["endingId"] == "E04":
                    branch_verified = (branch_verified and variables.get("Love") == "0"
                                       and "从此，一提起独孤家" in text and {3, 7} <= executed_lines)
                else:
                    source_lines = (resource / source).read_text(encoding="utf-8").splitlines()
                    ending_movie_line = next(index for index, line in enumerate(source_lines, 1)
                                             if 'playmovie("end_1.wmv"' in line)
                    branch_verified = (branch_verified and variables.get("Love") == "1"
                                       and (variables.get("End") != "222" or variables.get("NoEnd") == "1"
                                            or variables.get("Zhujiarenshen") == "1")
                                       and "老夫观今日泰山景致" in text and ending_movie_line in executed_lines)
                if Path(proof["beforeFile"]).with_suffix(".png").read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
                    raise ValueError(f"Ending branch PNG missing: {path}")
            elif proof["endingId"] == "E05":
                before = read(Path(proof["beforeFile"]))
                first = proof["nativeFirstQinDeath"]
                first_execution = [row for row in trace if row.get("executionId") == first["executionId"]]
                first_magic_line = next(index for index, line in enumerate(
                    (resource / first["path"]).read_text(encoding="utf-8").splitlines(), 1)
                    if 'addmagic("magic满江红.ini"' in line)
                branch_verified = (source == "script/map/map120_风波亭/秦桧死亡2.txt"
                                   and before.get("map") == "map120_风波亭.map"
                                   and before["variables"].get("Result") == "5"
                                   and before["variables"].get("qinhuidie") == "1"
                                   and "太好了，就叫“南宫飞云”" in dialogue.get("dialogue", {}).get("text", "")
                                   and first["path"] == "script/map/map120_风波亭/秦桧死亡.txt"
                                   and first["start"] in trace and first["finish"] in trace
                                   and first["finish"].get("status") == "completed"
                                   and source_hashes.get(first["path"]) == first["sourceSha256"]
                                   and any(row.get("eventType") == "variable.change" and row.get("variableName") == "qinhuidie"
                                           and row.get("afterValue") == "1" for row in first_execution)
                                   and any(row.get("eventType") == "source.line" and row.get("line") == first_magic_line
                                           for row in first_execution)
                                   and any(row.get("apiName") == "addmagic" for row in first_execution)
                                   and any(row.get("file") == "magic满江红.ini" and row.get("level", 0) >= 1
                                           for row in before.get("magic", [])))
                if Path(proof["beforeFile"]).with_suffix(".png").read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
                    raise ValueError(f"Ending branch PNG missing: {path}")
            else:
                raise ValueError(f"Ending has no current native acceptance rule: {path}")
            if (not branch_verified or proof["cheatAssisted"] != identity["cheatAssisted"] or start not in trace
                    or source_hashes.get(source) != start["contentSha256"]
                    or len(finish) != 1 or finish[0].get("status") != "completed"
                    or not {"playmovie", "returntotitle"} <= {row.get("apiName") for row in execution}
                    or dialogue.get("script") != source
                    or not dialogue.get("dialogue", {}).get("complete")
                    or Path(video.get("video", "")).name.casefold() != "over.wmv"
                    or not title_visible(title) or proof.get("videoSkipped") is not False):
                raise ValueError(f"Ending lacks complete native evidence: {path}")
            if any(row["request"]["command"] == "SendUIAction"
                   and row["request"]["arguments"].get("context") == video["context"]
                   and row["request"]["arguments"].get("action") == "Cancel" for row in commands):
                raise ValueError(f"Ending video was skipped: {path}")
            for key in ("finalDialogueFile", "videoFile", "titleFile"):
                if Path(proof[key]).with_suffix(".png").read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
                    raise ValueError(f"Ending PNG missing: {path}")
            endings.append(dict(run=str(directory), endingId=proof["endingId"],
                                executionId=start["executionId"], cheatAssisted=identity["cheatAssisted"],
                                videoSkipped=False))
        variable_outcomes = traced_condition_values(trace, catalog['conditions'],
            initial_unknown_is_zero=identity.get('engineSha256') in initial_zero_engine_hashes)
        executions = {}
        for row in trace:
            if "executionId" in row:
                executions.setdefault(row["executionId"], []).append(row)
        for execution_id, execution in executions.items():
            starts = [row for row in execution if row.get("eventType") == "script.start"]
            finish = [row for row in execution if row.get("eventType") == "script.finish"]
            if (len(starts) != 1 or len(finish) != 1 or finish[0].get("status") != "completed"
                    or source_hashes.get(starts[0]["virtualPath"]) != starts[0]["contentSha256"]):
                continue
            path = starts[0]["virtualPath"]
            completed.append(dict(run=str(directory), executionId=execution_id, path=path,
                                  cheatAssisted=identity["cheatAssisted"]))
            executed = [row for row in execution if row.get("eventType") == "source.line"]
            line_outcomes = {}
            for current, following in zip(executed, executed[1:]):
                branch = conditions.get((path, current["line"]))
                if not branch:
                    continue
                site, destinations = branch
                for outcome, line in destinations.items():
                    if following["line"] == line:
                        line_outcomes[current['sequence']] = outcome
                        evidence = dict(run=str(directory), executionId=execution_id,
                                        sequence=current["sequence"], nextLine=line,
                                        cheatAssisted=identity["cheatAssisted"])
                        site.setdefault("normalOutcomes" if not identity["cheatAssisted"] else "assistedOutcomes", {}).setdefault(outcome, []).append(evidence)
                        site["outcomes"][outcome] = "passed" if site.get("normalOutcomes", {}).get(outcome) else "assisted-passed"
            for site, outcome, details in variable_outcomes.get(execution_id, []):
                if details['sequence'] in line_outcomes:
                    if line_outcomes[details['sequence']] != outcome:
                        raise ValueError(f"Variable and source-line condition evidence disagree: {directory} / {site['id']}")
                    variable_comparisons_checked += 1
                    continue
                evidence = dict(run=str(directory), executionId=execution_id,
                                cheatAssisted=identity['cheatAssisted'], **details)
                site.setdefault('assistedOutcomes' if identity['cheatAssisted'] else 'normalOutcomes', {}).setdefault(outcome, []).append(evidence)
                site['outcomes'][outcome] = 'passed' if site.get('normalOutcomes', {}).get(outcome) else 'assisted-passed'
                added_variable_outcomes += 1
    options = [option for site in catalog["choices"] for option in site["options"]]
    catalog["counts"].update(normalChoiceOptions=sum(bool(x.get("normalEvidence")) for x in options),
                            assistedOnlyChoiceOptions=sum(bool(x.get("assistedEvidence")) and not x.get("normalEvidence") for x in options),
                            pendingChoiceOptions=sum(x["status"] == "pending" for x in options),
                            currentCompletedExecutions=len(completed),
                            currentCompletedScripts=len({x["path"] for x in completed}),
                            normalStoryEndings=len({x["endingId"] for x in endings if not x["cheatAssisted"]}),
                            assistedOnlyStoryEndings=len({x["endingId"] for x in endings if x["cheatAssisted"]}
                                - {x["endingId"] for x in endings if not x["cheatAssisted"]}),
                            normalTwoSidedConditions=sum(len(x.get("normalOutcomes", {})) == 2 for x in catalog["conditions"]),
                            assistedOnlyTwoSidedConditions=sum(len(set(x.get("normalOutcomes", {})) | set(x.get("assistedOutcomes", {}))) == 2
                                and len(x.get("normalOutcomes", {})) < 2 for x in catalog["conditions"]))
    catalog["evidenceRuns"] = [str(directory) for directory in directories]
    return catalog, dict(choices=verified, completedExecutions=completed, endings=endings,
                         historicalChoiceProofsExcluded=stale_choices,
                         conditionInference="Distinct next source lines, or an integer comparison at a traced getvar; an unseen variable is zero only for explicitly reviewed engines with a complete session trace",
                         reviewedInitialZeroEngineHashes=sorted(initial_zero_engine_hashes),
                         variableComparisonsCheckedAgainstSourceLines=variable_comparisons_checked,
                         additionalVariableOutcomeObservations=added_variable_outcomes,
                         endingCoverage="partial" if endings else "pending")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, nargs="+", required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    catalog, proof = audit(args.assets.resolve() / "xjxqy", [path.resolve() for path in args.evidence])
    write_json(output / "branch-catalog.json", catalog)
    write_json(output / "native-review.json", proof)
    print(json.dumps(catalog["counts"]))


if __name__ == "__main__":
    main()
