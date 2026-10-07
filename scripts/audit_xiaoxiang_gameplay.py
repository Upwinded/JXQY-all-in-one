"""Count completed Xiaoxiang scripts at current local and dependency hashes."""
import argparse
import json
from pathlib import Path

from audit_chenghe_gameplay import audit
from run_xiaoxiang_gameplay import RESOURCE_ID, source_candidates, write_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, default=Path("assets"))
    parser.add_argument("--evidence", type=Path, nargs="+", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    assets = args.assets.resolve()
    result = audit(assets, args.evidence, catalog=source_candidates(assets),
                   resource_id=RESOURCE_ID, session_prefix="xiaoxiang-")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_json(args.output, result)
    print(json.dumps({key: result[key] for key in ("candidateCounts", "completedScriptCount",
                     "completedExecutionCount", "observedConditionSides", "conditionSitesBothSides")}, ensure_ascii=False))


if __name__ == "__main__":
    main()
