"""Count completed Chenghe scripts and native condition outcomes at current hashes."""
import argparse
import json
from pathlib import Path

from audit_xjxqy_gameplay import branch_destinations
from run_chenghe_gameplay import RESOURCE_ID, source_candidates, write_json


def audit(assets, directories, *, catalog=None, resource_id=RESOURCE_ID, session_prefix="chenghe-"):
    if catalog is None:
        catalog = source_candidates(assets)
    sources = {site["path"]: site for site in catalog["sources"]}
    conditions = {}
    for site in catalog["conditions"]:
        destinations = branch_destinations(Path(site["sourceRoot"]), site)
        if destinations:
            conditions[(site["path"], site["line"])] = (site, destinations)
    completed, sides, errors, stale = {}, {}, [], []
    for directory in directories:
        identity = json.loads((directory / "run.json").read_text(encoding="utf-8"))
        if identity["resourceId"] != resource_id or not identity["session"].startswith(session_prefix):
            raise ValueError(f"Not an isolated {resource_id} run: {directory}")
        trace = []
        with (directory / "user-data/automation/trace.jsonl").open(encoding="utf-8") as stream:
            for line in stream:
                if line.endswith("\n"):
                    trace.append(json.loads(line))
        executions = {}
        for index, event in enumerate(trace, 1):
            if event["sequence"] != index:
                raise ValueError(f"Non-contiguous trace: {directory}:{index}")
            if "executionId" in event:
                executions.setdefault(event["executionId"], []).append(event)
        for execution, events in executions.items():
            starts = [event for event in events if event["eventType"] == "script.start"]
            finishes = [event for event in events if event["eventType"] == "script.finish"]
            if not starts or not finishes:
                continue
            start = starts[0]
            path = start["virtualPath"]
            evidence = dict(directory=str(directory.resolve()), executionId=execution,
                            startSequence=start["sequence"], cheatAssisted=identity["cheatAssisted"])
            if len(starts) != 1 or len(finishes) != 1 or finishes[0]["status"] != "completed":
                errors.append(dict(evidence, path=path, finishes=finishes))
                continue
            source = sources.get(path)
            if source is None or start["contentSha256"] != source["sha256"]:
                stale.append(dict(evidence, path=path))
                continue
            completed.setdefault(path, []).append(evidence)
            lines = [event for event in events if event["eventType"] == "source.line"]
            for current, following in zip(lines, lines[1:]):
                candidate = conditions.get((path, current["line"]))
                if candidate:
                    site, destinations = candidate
                    outcomes = [side for side, target in destinations.items() if target == following["line"]]
                    if len(outcomes) == 1:
                        sides.setdefault(site["id"], {}).setdefault(outcomes[0], []).append(
                            dict(evidence, sequence=current["sequence"], nextLine=following["line"],
                                 sourceSha256=start["contentSha256"]))
    return dict(resourceId=resource_id, fullCoverage=False, fullPlaythrough=False,
                candidateCounts=catalog["counts"], completedScriptCount=len(completed),
                completedExecutionCount=sum(map(len, completed.values())),
                unassistedCompletedExecutionCount=sum(not proof["cheatAssisted"]
                    for proofs in completed.values() for proof in proofs),
                assistedCompletedExecutionCount=sum(proof["cheatAssisted"]
                    for proofs in completed.values() for proof in proofs),
                unassistedCompletedScriptCount=sum(any(not proof["cheatAssisted"] for proof in proofs)
                    for proofs in completed.values()),
                observedConditionSides=sum(map(len, sides.values())),
                conditionSitesBothSides=sum(len(value) == 2 for value in sides.values()),
                completedScripts=completed, conditionOutcomes=sides, scriptErrors=errors, staleExecutions=stale)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, default=Path("assets"))
    parser.add_argument("--evidence", type=Path, nargs="+", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.assets.resolve(), args.evidence)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_json(args.output, result)
    print(json.dumps({key: result[key] for key in ("candidateCounts", "completedScriptCount",
                     "completedExecutionCount", "observedConditionSides", "conditionSitesBothSides")}, ensure_ascii=False))


if __name__ == "__main__":
    main()
