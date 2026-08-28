#!/usr/bin/env python3
"""Audit resource warnings against explicit original MOD source roots.

The checker answers a narrow question: for each resource warning from
check_mod_resources.py, does any runtime candidate exist in the original
directory or archive supplied for that pack?
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import re
import shutil
import subprocess
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


TARGET_RE = re.compile(
    r"target (?:not found|resolves to different disk path(?: \([^)]+\))?): (?P<target>.*?)(?: -> (?P<resolved>.*))?$"
)

DEFAULT_CATEGORIES = {
    "goods_resource_image",
    "magic_resource_image",
    "magic_effect_image",
    "magic_action_image",
    "object_resource_image",
    "object_resource_shade",
    "ui_component_image",
    "script_media",
    "signal_tip",
}

SCRIPT_MEDIA_RE = re.compile(r"script media target not found: (?P<function>[A-Za-z_][A-Za-z0-9_]*)\((?P<file>.*?)\)")


@dataclass(frozen=True)
class SourceIndex:
    pack_id: str
    description: str
    relative_paths: set[str]


def load_check_mod_resources_module():
    script_path = Path(__file__).resolve().with_name("check_mod_resources.py")
    spec = importlib.util.spec_from_file_location("check_mod_resources_for_original_audit", script_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {script_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def normalized_relative_key(value: str) -> str:
    return "/".join(part for part in value.replace("\\", "/").split("/") if part and part != ".").casefold()


def directory_index(pack_id: str, source_path: Path) -> SourceIndex:
    if not source_path.exists() or not source_path.is_dir():
        raise FileNotFoundError(f"source directory for {pack_id} does not exist: {source_path}")
    paths: set[str] = set()
    for file_path in source_path.rglob("*"):
        if file_path.is_file():
            paths.add(normalized_relative_key(str(file_path.relative_to(source_path))))
    return SourceIndex(pack_id, f"directory:{source_path}", paths)


def find_7z(explicit_path: str = "") -> str:
    if explicit_path:
        path = Path(explicit_path)
        if path.exists():
            return str(path)
        resolved = shutil.which(explicit_path)
        if resolved:
            return resolved
        raise FileNotFoundError(f"7z executable not found: {explicit_path}")

    for candidate in (
        shutil.which("7z"),
        shutil.which("7zz"),
        shutil.which("7za"),
        r"C:\Program Files\7-Zip\7z.exe",
        r"C:\Program Files (x86)\7-Zip\7z.exe",
    ):
        if candidate and Path(candidate).exists():
            return str(candidate)
    raise FileNotFoundError("7z executable not found; pass --seven-zip")


def archive_index(pack_id: str, archive_path: Path, prefix: str, password: str, seven_zip: str) -> SourceIndex:
    if not archive_path.exists() or not archive_path.is_file():
        raise FileNotFoundError(f"source archive for {pack_id} does not exist: {archive_path}")

    command = [seven_zip, "l", "-slt", "-sccUTF-8"]
    if password:
        command.append(f"-p{password}")
    command.append(str(archive_path))
    completed = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if completed.returncode != 0:
        raise RuntimeError(f"7z list failed for {archive_path}: {completed.stderr.strip()}")

    normalized_prefix = normalized_relative_key(prefix)
    paths: set[str] = set()
    for line in completed.stdout.splitlines():
        if not line.startswith("Path = "):
            continue
        path_text = line[len("Path = ") :]
        key = normalized_relative_key(path_text)
        if normalized_prefix:
            prefix_with_slash = normalized_prefix + "/"
            if key == normalized_prefix:
                continue
            if not key.startswith(prefix_with_slash):
                continue
            key = key[len(prefix_with_slash) :]
        paths.add(key)
    description = f"archive:{archive_path}"
    if prefix:
        description += f"|prefix={prefix}"
    return SourceIndex(pack_id, description, paths)


def parse_source_mapping(value: str) -> tuple[str, Path]:
    if "=" not in value:
        raise ValueError(f"invalid --source mapping, expected PACK=PATH: {value}")
    pack_id, path_text = value.split("=", 1)
    pack_id = pack_id.strip()
    if not pack_id:
        raise ValueError(f"invalid --source pack id: {value}")
    return pack_id, Path(path_text.strip())


def parse_archive_mapping(value: str) -> tuple[str, Path, str, str]:
    if "=" not in value:
        raise ValueError(f"invalid --archive mapping, expected PACK=ARCHIVE|PREFIX|PASSWORD: {value}")
    pack_id, payload = value.split("=", 1)
    parts = payload.split("|")
    if not pack_id.strip() or not parts or not parts[0].strip():
        raise ValueError(f"invalid --archive mapping: {value}")
    archive_path = Path(parts[0].strip())
    prefix = parts[1].strip() if len(parts) > 1 else ""
    password = parts[2].strip() if len(parts) > 2 else ""
    return pack_id.strip(), archive_path, prefix, password


def load_report(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict) or not isinstance(data.get("issues"), list):
        raise ValueError(f"invalid check_mod_resources report: {path}")
    return data


def parse_warning_target(message: str) -> tuple[str, str]:
    match = TARGET_RE.search(message)
    if match is None:
        return "", ""
    return match.group("target").strip(), (match.group("resolved") or "").strip()


def parse_script_media_target(message: str) -> tuple[str, str]:
    match = SCRIPT_MEDIA_RE.search(message)
    if match is None:
        return "", ""
    return match.group("function").strip(), match.group("file").strip()


def candidates_for_category(module: Any, category: str, target: str) -> list[str]:
    if category in {"object_resource_image", "object_resource_shade"}:
        return module.object_image_candidates(target)
    if category == "goods_resource_image":
        return module.goods_image_candidates(target)
    if category == "magic_resource_image":
        return module.magic_image_candidates(target)
    if category == "magic_effect_image":
        return module.magic_effect_image_candidates(target)
    if category == "magic_action_image":
        return module.magic_action_image_candidates(target)
    if category == "ui_component_image":
        return [target]
    if category == "signal_tip":
        return ["ini\\ui\\tips\\SignalFile.ini"]
    return []


def candidates_for_issue(module: Any, category: str, message: str, target: str) -> tuple[str, list[str]]:
    if category == "script_media":
        function_name, file_name = parse_script_media_target(message)
        if not function_name or not file_name:
            return "", []
        candidates: list[str] = []
        for use_wav in (False, True):
            candidates.extend(module.script_media_candidates(function_name, file_name, use_wav))
        return f"{function_name}({file_name})", module.unique_preserving_order(candidates)
    if category == "signal_tip":
        return "ini\\ui\\tips\\SignalFile.ini", candidates_for_category(module, category, target)
    return target, candidates_for_category(module, category, target)


def classify_source_match(index: SourceIndex | None, candidates: Iterable[str], resolved: str) -> tuple[str, list[str]]:
    if index is None:
        return "no_source_mapping", []
    hits = [candidate for candidate in candidates if normalized_relative_key(candidate) in index.relative_paths]
    if hits:
        return "original_exact", hits
    if resolved:
        return "runtime_alias_only", []
    return "missing_in_original", []


def build_markdown(output: dict[str, Any]) -> str:
    lines: list[str] = []
    lines.append("# MOD Original Resource Audit 2026-07-06")
    lines.append("")
    lines.append("Source: `check_mod_resources.py` JSON warning output plus explicit original source mappings.")
    lines.append("")

    lines.append("## Source Mappings")
    lines.append("")
    for pack_id, description in sorted(output["sources"].items()):
        lines.append(f"- `{pack_id}`: `{description}`")
    lines.append("")

    lines.append("## Summary")
    lines.append("")
    lines.append("| Pack | Category | Status | Count |")
    lines.append("| --- | --- | --- | ---: |")
    for item in output["summary"]:
        lines.append(f"| {item['pack_id']} | {item['category']} | {item['status']} | {item['count']} |")
    lines.append("")

    lines.append("## Interpretation")
    lines.append("")
    lines.append("- `original_exact`: at least one checker candidate exists in the supplied original source.")
    lines.append("- `runtime_alias_only`: original exact candidate is absent, but the current runtime/checker resolves a unique image-package alias. Do not copy or rename automatically; verify visually in a real MOD scene.")
    lines.append("- `missing_in_original`: no checker candidate exists in the supplied original source. Treat this as a reported source/MOD defect, not as a required lookup task.")
    lines.append("- `no_source_mapping`: no original source was supplied for that pack.")
    lines.append("- Only `original_exact` is a safe resource-copy candidate. Fan-made MODs can legitimately contain broken or incomplete references; reporting those gaps is sufficient unless a later real-scene review proves a visible blocker.")
    lines.append("")

    lines.append("## Examples")
    lines.append("")
    for key, examples in output["examples"].items():
        lines.append(f"### {key}")
        lines.append("")
        for example in examples:
            hits = ", ".join(f"`{hit}`" for hit in example["original_hits"]) or "-"
            resolved = f"`{example['resolved']}`" if example["resolved"] else "-"
            lines.append(f"- `{example['target']}` at `{example['path']}`; original hits: {hits}; resolved: {resolved}")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit resource warnings against original MOD sources.")
    parser.add_argument("report_json", type=Path, help="JSON output from check_mod_resources.py")
    parser.add_argument("--source", action="append", default=[], help="Original directory mapping: PACK=PATH")
    parser.add_argument(
        "--archive",
        action="append",
        default=[],
        help="Original archive mapping: PACK=ARCHIVE|PREFIX|PASSWORD",
    )
    parser.add_argument("--seven-zip", default="", help="Path to 7z/7zz/7za executable")
    parser.add_argument(
        "--category",
        action="append",
        default=[],
        help="Category to include. Defaults to image/action resource warning categories.",
    )
    parser.add_argument(
        "--mapped-only",
        action="store_true",
        help="Only include packs that have an explicit --source or --archive mapping.",
    )
    parser.add_argument("--json", type=Path, help="Write JSON output")
    parser.add_argument("--markdown", type=Path, help="Write Markdown output")
    args = parser.parse_args()

    module = load_check_mod_resources_module()
    report = load_report(args.report_json)
    categories = set(args.category) if args.category else DEFAULT_CATEGORIES

    indexes: dict[str, SourceIndex] = {}
    for mapping in args.source:
        pack_id, source_path = parse_source_mapping(mapping)
        indexes[pack_id] = directory_index(pack_id, source_path)

    archive_mappings = [parse_archive_mapping(mapping) for mapping in args.archive]
    if archive_mappings:
        seven_zip = find_7z(args.seven_zip)
        for pack_id, archive_path, prefix, password in archive_mappings:
            indexes[pack_id] = archive_index(pack_id, archive_path, prefix, password, seven_zip)

    rows: list[dict[str, Any]] = []
    counts: Counter[tuple[str, str, str]] = Counter()
    examples: dict[str, list[dict[str, str | list[str]]]] = defaultdict(list)
    for issue in report["issues"]:
        if issue.get("severity") != "WARNING":
            continue
        category = str(issue.get("category", ""))
        if category not in categories:
            continue
        pack_id = str(issue.get("pack_id", ""))
        if args.mapped_only and pack_id not in indexes:
            continue
        message = str(issue.get("message", ""))
        target, resolved = parse_warning_target(message)
        target, candidates = candidates_for_issue(module, category, message, target)
        if not target:
            continue
        status, hits = classify_source_match(indexes.get(pack_id), candidates, resolved)
        row = {
            "pack_id": pack_id,
            "category": category,
            "status": status,
            "target": target,
            "path": str(issue.get("path", "")),
            "resolved": resolved,
            "candidates": candidates,
            "original_hits": hits,
        }
        rows.append(row)
        counts[(pack_id, category, status)] += 1
        example_key = f"{pack_id} / {category} / {status}"
        if len(examples[example_key]) < 8:
            examples[example_key].append(
                {
                    "target": target,
                    "path": row["path"],
                    "resolved": resolved,
                    "original_hits": hits,
                }
            )

    summary = [
        {"pack_id": pack_id, "category": category, "status": status, "count": count}
        for (pack_id, category, status), count in sorted(counts.items(), key=lambda item: (item[0][0], item[0][1], item[0][2]))
    ]
    output = {
        "source": str(args.report_json),
        "sources": {pack_id: index.description for pack_id, index in sorted(indexes.items())},
        "summary": summary,
        "warnings": rows,
        "examples": examples,
    }

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(build_markdown(output), encoding="utf-8", newline="\n")

    for item in summary:
        print(f"{item['pack_id']} {item['category']} {item['status']}: {item['count']}")
    print(f"total_checked: {len(rows)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
