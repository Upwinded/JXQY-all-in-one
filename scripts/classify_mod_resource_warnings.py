#!/usr/bin/env python3
"""Classify MOD resource warnings into governance action buckets."""

from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


DECISION_ACCEPTED_MISSING = "accepted_missing"
DECISION_NEEDS_RESOURCE = "needs_resource"
DECISION_NEEDS_REFERENCE_OR_CASE_FIX = "needs_reference_or_case_fix"

DECISION_ORDER = (
    DECISION_NEEDS_REFERENCE_OR_CASE_FIX,
    DECISION_NEEDS_RESOURCE,
    DECISION_ACCEPTED_MISSING,
)

DECISION_LABELS = {
    DECISION_ACCEPTED_MISSING: "可接受缺失/运行时回退",
    DECISION_NEEDS_RESOURCE: "需要补资源",
    DECISION_NEEDS_REFERENCE_OR_CASE_FIX: "需要修引用/大小写",
}


def load_report(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as file:
        data = json.load(file)
    if not isinstance(data, dict) or not isinstance(data.get("issues"), list):
        raise ValueError(f"invalid resource check report: {path}")
    return data


def normalized_parts(value: str) -> list[str]:
    path = Path(value.replace("\\", "/"))
    return [part for part in path.parts if part and part != "."]


def parse_resolved_path_pair(message: str) -> tuple[str, str] | None:
    if "->" not in message:
        return None
    before, after = message.rsplit("->", 1)
    if ":" not in before:
        return None
    candidate = before.rsplit(":", 1)[1].strip()
    resolved = after.strip()
    return candidate, resolved


def is_case_only_resolution(message: str) -> bool:
    parsed = parse_resolved_path_pair(message)
    if parsed is None:
        return False

    candidate, resolved = parsed
    candidate_parts = normalized_parts(candidate)
    resolved_parts = normalized_parts(resolved)
    if not candidate_parts or len(candidate_parts) > len(resolved_parts):
        return False

    resolved_tail = resolved_parts[-len(candidate_parts) :]
    if [part.casefold() for part in candidate_parts] != [part.casefold() for part in resolved_tail]:
        return False
    return candidate_parts != resolved_tail


def classify_warning(issue: dict[str, Any]) -> tuple[str, str]:
    category = str(issue.get("category", ""))
    message = str(issue.get("message", ""))
    message_lower = message.lower()

    if "resolves to different disk path" in message_lower:
        if is_case_only_resolution(message):
            return (
                DECISION_NEEDS_REFERENCE_OR_CASE_FIX,
                "引用路径与实际解析路径仅大小写不一致；Windows 可解析，但大小写敏感平台会失败。",
            )
        return (
            DECISION_ACCEPTED_MISSING,
            "C++ File 层可通过唯一编号/前缀别名解析到运行资源；不自动复制或改名，保留真实 MOD 视觉验收。",
        )

    if category in {"script_media", "signal_tip"}:
        return (
            DECISION_ACCEPTED_MISSING,
            "运行时按可选资源降级处理；当前作为允许缺失保留可见。",
        )

    if category == "object_objfile" and "target not found" in message_lower:
        return (
            DECISION_ACCEPTED_MISSING,
            "对象 ObjFile 真实缺失；当前运行时允许缺失并降级，后续按资源归属决定是否补齐。",
        )

    if "target not found" in message_lower or "no resolvable" in message_lower:
        return (
            DECISION_NEEDS_RESOURCE,
            "资源引用目标不可解析；需要回到原始 MOD 或依赖/common 归属确认后补资源或修引用。",
        )

    return (
        DECISION_NEEDS_RESOURCE,
        "未命中已确认允许缺失规则；需要人工复核资源归属。",
    )


def warning_rows(report: dict[str, Any]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for issue in report["issues"]:
        if issue.get("severity") != "WARNING":
            continue
        decision, reason = classify_warning(issue)
        rows.append(
            {
                "decision": decision,
                "decision_label": DECISION_LABELS[decision],
                "reason": reason,
                "pack_id": str(issue.get("pack_id", "")),
                "category": str(issue.get("category", "")),
                "message": str(issue.get("message", "")),
                "path": str(issue.get("path", "")),
            }
        )
    return rows


def count_by(rows: list[dict[str, Any]], *keys: str) -> list[tuple[tuple[str, ...], int]]:
    counter: Counter[tuple[str, ...]] = Counter(tuple(str(row[key]) for key in keys) for row in rows)
    return sorted(counter.items(), key=lambda item: (-item[1], item[0]))


def markdown_table(headers: list[str], rows: list[list[str]]) -> list[str]:
    lines = ["| " + " | ".join(headers) + " |"]
    lines.append("| " + " | ".join("---" for _ in headers) + " |")
    for row in rows:
        lines.append("| " + " | ".join(escape_markdown_cell(cell) for cell in row) + " |")
    return lines


def escape_markdown_cell(value: str) -> str:
    return value.replace("|", "\\|").replace("\n", " ")


def issue_line(row: dict[str, Any]) -> str:
    path = row["path"]
    location = f" `{path}`" if path else ""
    return f"- [{row['pack_id']}] {row['category']}: {row['message']}{location}"


def build_markdown(report: dict[str, Any], rows: list[dict[str, Any]], *, include_issues: bool) -> str:
    lines: list[str] = []
    lines.append("# MOD Resource Warning Classification 2026-07-06")
    lines.append("")
    lines.append("Source command baseline: `scripts/check_mod_resources.py assets --strict --summary-by-category --json ...`.")
    lines.append("")

    summary = report.get("summary", {})
    lines.extend(
        markdown_table(
            ["Severity", "Count"],
            [[key, str(summary.get(key, 0))] for key in ("ERROR", "WARNING", "INFO")],
        )
    )
    lines.append("")

    decision_counts = Counter(row["decision"] for row in rows)
    lines.append("## Decision Summary")
    lines.append("")
    lines.extend(
        markdown_table(
            ["Decision", "Label", "Count"],
            [
                [decision, DECISION_LABELS[decision], str(decision_counts.get(decision, 0))]
                for decision in DECISION_ORDER
            ],
        )
    )
    lines.append("")

    lines.append("## Decision By Category")
    lines.append("")
    lines.extend(
        markdown_table(
            ["Decision", "Label", "Category", "Count"],
            [
                [decision, DECISION_LABELS[decision], category, str(count)]
                for (decision, category), count in count_by(rows, "decision", "category")
            ],
        )
    )
    lines.append("")

    lines.append("## Decision By Pack")
    lines.append("")
    lines.extend(
        markdown_table(
            ["Decision", "Label", "Pack", "Count"],
            [
                [decision, DECISION_LABELS[decision], pack_id, str(count)]
                for (decision, pack_id), count in count_by(rows, "decision", "pack_id")
            ],
        )
    )
    lines.append("")

    lines.append("## Classification Rules")
    lines.append("")
    lines.append("- `needs_reference_or_case_fix`: `resolves to different disk path`, and the requested path equals the resolved tail path after case folding.")
    lines.append("- `needs_resource`: unresolved targets that do not have a runtime alias/fallback match and are not already documented as optional degradation.")
    lines.append("- `accepted_missing`: `script_media`, `signal_tip`, missing `object_objfile`, or unique image package alias fallback already supported by the C++ `File` layer. Alias fallback still needs real MOD visual review before final signoff.")
    lines.append("")

    if include_issues:
        grouped: dict[str, dict[str, list[dict[str, Any]]]] = defaultdict(lambda: defaultdict(list))
        for row in rows:
            grouped[row["decision"]][row["category"]].append(row)

        lines.append("## Full Issue Index")
        lines.append("")
        for decision in DECISION_ORDER:
            decision_rows = grouped.get(decision, {})
            total = sum(len(items) for items in decision_rows.values())
            lines.append(f"### {DECISION_LABELS[decision]} ({decision}, {total})")
            lines.append("")
            for category in sorted(decision_rows):
                items = sorted(
                    decision_rows[category],
                    key=lambda row: (row["pack_id"], row["path"], row["message"]),
                )
                lines.append(f"#### {category} ({len(items)})")
                lines.append("")
                lines.extend(issue_line(row) for row in items)
                lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Classify check_mod_resources warning output.")
    parser.add_argument("report_json", type=Path, help="JSON output produced by check_mod_resources.py.")
    parser.add_argument("--markdown", type=Path, help="Write a Markdown classification report.")
    parser.add_argument("--json", type=Path, help="Write full classified warnings as JSON.")
    parser.add_argument("--include-issues", action="store_true", help="Include the full issue index in Markdown output.")
    args = parser.parse_args()

    report = load_report(args.report_json)
    rows = warning_rows(report)

    output = {
        "source": str(args.report_json),
        "summary": report.get("summary", {}),
        "decisionSummary": dict(Counter(row["decision"] for row in rows)),
        "warnings": rows,
    }

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        with args.json.open("w", encoding="utf-8", newline="\n") as handle:
            handle.write(json.dumps(output, ensure_ascii=False, indent=2))
            handle.write("\n")

    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        with args.markdown.open("w", encoding="utf-8", newline="\n") as handle:
            handle.write(build_markdown(report, rows, include_issues=args.include_issues))

    for decision in DECISION_ORDER:
        print(f"{decision}: {output['decisionSummary'].get(decision, 0)}")
    print(f"total_warnings: {len(rows)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
