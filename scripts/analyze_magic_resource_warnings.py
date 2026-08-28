#!/usr/bin/env python3
"""Classify high-risk magic action/effect resource warnings.

The general resource classifier answers the first-stage governance buckets.
This narrower report keeps action/effect warnings actionable: it separates
alias fallback, unresolved base-game resources, custom MOD deferrals, and
direct action-resource misses.
"""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any


MAGIC_CATEGORIES = {"magic_action_image", "magic_effect_image"}
TARGET_RE = re.compile(
    r"resource\s+(?P<field>[A-Za-z0-9_]+)\s+target\s+"
    r"(?P<kind>not found|resolves to different disk path)"
    r"(?:\s+\(via (?:dependency|common)\))?:\s+"
    r"(?P<target>.*?)(?:\s+->\s+(?P<resolved>.*))?$",
    re.IGNORECASE,
)

GROUP_ALIAS_FALLBACK = "alias_fallback_visual_check"
GROUP_BASE_UNRESOLVED = "base_game_unresolved_resource"
GROUP_CUSTOM_MOD_DEFERRED = "custom_mod_deferred"
GROUP_ORIGINAL_MOD_UNRESOLVED = "original_mod_unresolved_resource"
GROUP_DIRECT_ACTION_MISSING = "direct_action_resource_missing"

GROUP_LABELS = {
    GROUP_ALIAS_FALLBACK: "别名/编号回退，需真实技能视觉签收",
    GROUP_BASE_UNRESOLVED: "基础游戏资源未解析，需回查原始包",
    GROUP_CUSTOM_MOD_DEFERRED: "定制 MOD 资源未解析，暂缓但保留清单",
    GROUP_ORIGINAL_MOD_UNRESOLVED: "迁移 MOD 资源未解析，需回查原始包或字段",
    GROUP_DIRECT_ACTION_MISSING: "直接动作资源缺失，需优先确认",
}

GROUP_RECOMMENDATIONS = {
    GROUP_ALIAS_FALLBACK: "不要机械改名或跨包复制；用真实技能路径确认动画是否可接受。",
    GROUP_BASE_UNRESOLVED: "只在本基底或其原始资源中确认；同名资源出现在其它基底时不直接复制。",
    GROUP_CUSTOM_MOD_DEFERRED: "按当前策略先降权处理；进入该 MOD 专项巡检时再决定补资源或修字段。",
    GROUP_ORIGINAL_MOD_UNRESOLVED: "优先回查该 MOD 原始包；若原包也缺失，记录为原包遗漏或字段异常。",
    GROUP_DIRECT_ACTION_MISSING: "优先确认 UseActionFile/ActionShadowFile 是否真实要求 character 动作资源。",
}


def load_report(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict) or not isinstance(data.get("issues"), list):
        raise ValueError(f"invalid resource check report: {path}")
    return data


def parse_warning(issue: dict[str, Any]) -> dict[str, Any]:
    message = str(issue.get("message", ""))
    match = TARGET_RE.search(message)
    field = ""
    target = ""
    resolved = ""
    resolution_kind = ""
    if match:
        field = match.group("field")
        target = match.group("target").strip()
        resolved = (match.group("resolved") or "").strip()
        resolution_kind = match.group("kind").lower()

    row = {
        "pack_id": str(issue.get("pack_id", "")),
        "category": str(issue.get("category", "")),
        "field": field,
        "target": target,
        "resolved": resolved,
        "resolution_kind": resolution_kind,
        "message": message,
        "path": str(issue.get("path", "")),
    }
    row["group"] = classify_row(row)
    row["group_label"] = GROUP_LABELS[row["group"]]
    row["recommendation"] = GROUP_RECOMMENDATIONS[row["group"]]
    return row


def classify_row(row: dict[str, Any]) -> str:
    pack_id = str(row["pack_id"])
    category = str(row["category"])
    resolution_kind = str(row["resolution_kind"])

    if "resolves to different disk path" in resolution_kind:
        return GROUP_ALIAS_FALLBACK
    if category == "magic_action_image":
        return GROUP_DIRECT_ACTION_MISSING
    if pack_id == "XJXQY":
        return GROUP_BASE_UNRESOLVED
    if pack_id.startswith("JIANGHU_YUCHEN"):
        return GROUP_CUSTOM_MOD_DEFERRED
    return GROUP_ORIGINAL_MOD_UNRESOLVED


def collect_rows(report: dict[str, Any]) -> list[dict[str, Any]]:
    rows = []
    for issue in report["issues"]:
        if issue.get("severity") != "WARNING":
            continue
        if issue.get("category") not in MAGIC_CATEGORIES:
            continue
        rows.append(parse_warning(issue))
    return sorted(rows, key=lambda row: (row["group"], row["pack_id"], row["path"], row["field"], row["target"]))


def escape_cell(value: object) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ")


def markdown_table(headers: list[str], rows: list[list[object]]) -> list[str]:
    lines = ["| " + " | ".join(headers) + " |"]
    lines.append("| " + " | ".join("---" for _ in headers) + " |")
    for row in rows:
        lines.append("| " + " | ".join(escape_cell(cell) for cell in row) + " |")
    return lines


def write_markdown(path: Path, rows: list[dict[str, Any]], source: Path) -> None:
    group_counts = Counter(str(row["group"]) for row in rows)
    category_counts = Counter(str(row["category"]) for row in rows)
    lines = [
        "# MOD Magic Action/Effect Resource Classification 2026-07-06",
        "",
        f"Source report: `{source}`",
        "",
        "Scope: `magic_action_image` and `magic_effect_image` warnings only. This report does not mutate assets.",
        "",
        "## Summary",
        "",
    ]
    lines.extend(
        markdown_table(
            ["Metric", "Count"],
            [
                ["Total high-risk magic warnings", len(rows)],
                ["magic_action_image", category_counts.get("magic_action_image", 0)],
                ["magic_effect_image", category_counts.get("magic_effect_image", 0)],
            ],
        )
    )
    lines.append("")
    lines.append("## Group Summary")
    lines.append("")
    lines.extend(
        markdown_table(
            ["Group", "Label", "Count", "Recommendation"],
            [
                [group, GROUP_LABELS[group], group_counts.get(group, 0), GROUP_RECOMMENDATIONS[group]]
                for group in (
                    GROUP_DIRECT_ACTION_MISSING,
                    GROUP_ALIAS_FALLBACK,
                    GROUP_BASE_UNRESOLVED,
                    GROUP_ORIGINAL_MOD_UNRESOLVED,
                    GROUP_CUSTOM_MOD_DEFERRED,
                )
            ],
        )
    )
    lines.append("")

    for group in (
        GROUP_DIRECT_ACTION_MISSING,
        GROUP_ALIAS_FALLBACK,
        GROUP_BASE_UNRESOLVED,
        GROUP_ORIGINAL_MOD_UNRESOLVED,
        GROUP_CUSTOM_MOD_DEFERRED,
    ):
        group_rows = [row for row in rows if row["group"] == group]
        lines.append(f"## {GROUP_LABELS[group]} ({group}, {len(group_rows)})")
        lines.append("")
        lines.extend(
            markdown_table(
                ["Pack", "Category", "Field", "Target", "Resolved", "Location"],
                [
                    [
                        row["pack_id"],
                        row["category"],
                        row["field"],
                        row["target"],
                        row["resolved"],
                        row["path"],
                    ]
                    for row in group_rows
                ],
            )
        )
        lines.append("")

    lines.append("## Current Handling")
    lines.append("")
    lines.append("- `direct_action_resource_missing`: keep as first-stage high-priority resource review.")
    lines.append("- `alias_fallback_visual_check`: no automatic rename/copy; verify by real skill path before editing assets.")
    lines.append("- `base_game_unresolved_resource`: search only the matching base game and dependency chain before changing references.")
    lines.append("- `original_mod_unresolved_resource`: search the MOD original package first; if absent there, document as original omission or bad field.")
    lines.append("- `custom_mod_deferred`: kept visible but not blocking the current non-custom MOD pass.")
    lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report_json", type=Path, help="JSON report from check_mod_resources.py.")
    parser.add_argument("--json", type=Path, help="Write classified rows as JSON.")
    parser.add_argument("--markdown", type=Path, help="Write a Markdown report.")
    args = parser.parse_args()

    report = load_report(args.report_json)
    rows = collect_rows(report)
    output = {
        "source": str(args.report_json),
        "summary": {
            "total": len(rows),
            "byCategory": dict(Counter(row["category"] for row in rows)),
            "byGroup": dict(Counter(row["group"] for row in rows)),
        },
        "warnings": rows,
    }

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.markdown:
        write_markdown(args.markdown, rows, args.report_json)
    if not args.json and not args.markdown:
        print(json.dumps(output, ensure_ascii=False, indent=2))
    else:
        print(json.dumps(output["summary"], ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
