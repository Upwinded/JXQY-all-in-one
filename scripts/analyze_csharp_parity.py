#!/usr/bin/env python3
"""Compare C# runtime properties with current C++ INI loader keys.

This is a code-first orientation tool. It does not prove behavior parity; it
surfaces C# properties and C++ loader keys so missing or computed behavior can
be triaged from the C# side before resource-audit counts drive implementation.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from analyze_mod_scripts import (
    CALL_RE as LUA_CALL_RE,
    IGNORED_CALLS as LUA_IGNORED_CALLS,
    is_documentation_script,
    read_text_best_effort,
    strip_lua_comments,
)


REPO_ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class Domain:
    name: str
    csharp_files: tuple[str, ...]
    cpp_files: tuple[str, ...]


DOMAINS = {
    "magic": Domain(
        name="magic",
        csharp_files=("JxqyHD-develop/Engine/Magic.cs",),
        cpp_files=("src/Game/Data/Magic.cpp",),
    ),
    "goods": Domain(
        name="goods",
        csharp_files=("JxqyHD-develop/Engine/Good.cs",),
        cpp_files=("src/Game/Data/Goods.cpp",),
    ),
    "object": Domain(
        name="object",
        csharp_files=("JxqyHD-develop/Engine/Obj.cs",),
        cpp_files=("src/Game/Data/Object.cpp",),
    ),
    "npc": Domain(
        name="npc",
        csharp_files=(
            "JxqyHD-develop/Engine/Character.cs",
            "JxqyHD-develop/Engine/Npc.cs",
        ),
        cpp_files=("src/Game/Data/NPC.cpp",),
    ),
}

RUNTIME_STATE_FUNCTIONS = {
    "magic": ("getMagicRuntimePropertyValue", "ScriptAPI::getMagicState"),
    "goods": ("getGoodsRuntimePropertyValue",),
    "object": ("getObjectRuntimePropertyValue",),
    "npc": ("getNpcRuntimePropertyValue", "ScriptAPI::getPlayerState"),
}
RUNTIME_STATE_FILES = ("src/Game/Script/ScriptAPI.cpp",)

BEHAVIOR_COVERAGE_PATTERNS = {
    "magic": {
        "AdditionalEffect": [
            (
                "equipment hand EffectType maps to attack additional effect",
                "src/Game/Data/Player.cpp",
                "equipmentAttackAdditionalEffect = maeFrozen",
            ),
            (
                "NPC equipment EffectType maps to attack additional effect",
                "src/Game/Data/NPC.cpp",
                "attackAdditionalEffect = maeFrozen",
            ),
            (
                "attack magic effect carries additional effect",
                "src/Game/Data/NPC.cpp",
                "effect->additionalEffect = getAttackAdditionalEffect()",
            ),
            (
                "hit target receives additional status effect",
                "src/Game/Data/NPC.cpp",
                "void NPC::applyAdditionalAttackEffect",
            ),
        ],
    },
}


PROPERTY_RE = re.compile(
    r"^\s*public\s+"
    r"(?:(?:new|override|virtual|static|abstract|sealed)\s+)*"
    r"(?P<type>[A-Za-z_][A-Za-z0-9_<>,\.\[\]\?]*)\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*(?:\{|$)"
)
CPP_GET_RE = re.compile(r"(?:ini|INIReader\([^)]*\))\s*(?:->|\.)\s*Get\w*\s*\((?P<args>[^;]*)")
CPP_HELPER_GET_RE = re.compile(
    r"\b(?:readPositiveTime|readNonNegativeTime|readSecondsFieldAsMilliseconds|readBooleanAlias|readRandomInteger|readRandomString)\s*\((?P<args>[^;]*)"
)
QUOTED_RE = re.compile(r'"([^"]+)"')
NON_PROPERTY_TYPES = {"class", "struct", "enum", "interface", "delegate"}
SECTION_NAMES = {
    "init",
    "head",
    "header",
    "common",
    "signalicon",
}
CSHARP_SCRIPT_FILES = ("JxqyHD-develop/Engine/Script/ScriptRunner.cs",)
CPP_SCRIPT_FILES = ("src/Game/Script/Script.cpp",)
SCRIPT_CASE_RE = re.compile(r'case\s+"(?P<name>[^"]+)"\s*:')
CPP_REG_FUNC_RE = re.compile(r"\bregFunc\(\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\)")
CPP_REG_ALIAS_RE = re.compile(r'\bregAlias\(\s*"(?P<name>[^"]+)"\s*,\s*(?P<target>[A-Za-z_][A-Za-z0-9_]*)\s*\)')
CPP_COMMENT_OR_LITERAL_RE = re.compile(
    r'"(?:\\.|[^"\\])*"|'
    r"'(?:\\.|[^'\\])*'|"
    r"//[^\r\n]*|/\*.*?\*/",
    re.S,
)
SCRIPT_EXTENSIONS = {".lua", ".txt"}


def normalize_name(value: str) -> str:
    return re.sub(r"[^a-z0-9]", "", value.lower())


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig", errors="replace")


def iter_existing(paths: Iterable[str]) -> Iterable[Path]:
    for raw_path in paths:
        path = REPO_ROOT / raw_path
        if path.exists():
            yield path


def find_line(path: Path, pattern: str) -> int:
    for line_no, line in enumerate(read_text(path).splitlines(), start=1):
        if pattern in line:
            return line_no
    return 1


def extract_csharp_properties(paths: Iterable[str]) -> list[dict[str, object]]:
    properties: list[dict[str, object]] = []
    seen: set[tuple[str, str]] = set()
    for path in iter_existing(paths):
        lines = read_text(path).splitlines()
        for line_no, line in enumerate(lines, start=1):
            if "(" in line.split("{", 1)[0]:
                continue
            match = PROPERTY_RE.match(line)
            if not match:
                continue
            property_type = match.group("type")
            name = match.group("name")
            if property_type in NON_PROPERTY_TYPES:
                continue
            key = (str(path.relative_to(REPO_ROOT)), name)
            if key in seen:
                continue
            seen.add(key)
            properties.append(
                {
                    "name": name,
                    "normalized": normalize_name(name),
                    "type": property_type,
                    "file": str(path.relative_to(REPO_ROOT)).replace("\\", "/"),
                    "line": line_no,
                }
            )
    return properties


def extract_cpp_ini_keys(paths: Iterable[str]) -> list[dict[str, object]]:
    keys: list[dict[str, object]] = []
    seen: set[tuple[str, str, int]] = set()
    for path in iter_existing(paths):
        text = read_text(path)
        calls = list(CPP_GET_RE.finditer(text)) + list(CPP_HELPER_GET_RE.finditer(text))
        calls.sort(key=lambda call: call.start())
        for call in calls:
            quoted = QUOTED_RE.findall(call.group("args"))
            if not quoted:
                continue
            candidates = quoted
            if len(quoted) >= 2 and normalize_name(quoted[0]) in SECTION_NAMES:
                candidates = quoted[1:]
            line_no = text[: call.start()].count("\n") + 1
            for name in candidates:
                if not name or "%" in name:
                    continue
                key = (str(path.relative_to(REPO_ROOT)), name, line_no)
                if key in seen:
                    continue
                seen.add(key)
                keys.append(
                    {
                        "name": name,
                        "normalized": normalize_name(name),
                        "file": str(path.relative_to(REPO_ROOT)).replace("\\", "/"),
                        "line": line_no,
                    }
                )
    return keys


def iter_cpp_function_bodies(path: Path, function_names: Iterable[str]) -> Iterable[tuple[str, int, str]]:
    function_names = tuple(function_names)
    lines = read_text(path).splitlines()
    line_index = 0
    while line_index < len(lines):
        line = lines[line_index]
        matched_name = next((name for name in function_names if f"{name}(" in line), None)
        if matched_name is None:
            line_index += 1
            continue

        signature_lines: list[str] = []
        signature_start = line_index + 1
        cursor = line_index
        found_body = False
        while cursor < len(lines):
            signature_lines.append(lines[cursor])
            signature_text = "\n".join(signature_lines)
            if ";" in signature_text and "{" not in signature_text:
                break
            if "{" in signature_text:
                found_body = True
                break
            cursor += 1

        if not found_body:
            line_index += 1
            continue

        body_lines: list[str] = []
        brace_depth = 0
        body_cursor = cursor
        while body_cursor < len(lines):
            body_line = lines[body_cursor]
            body_lines.append(body_line)
            brace_depth += body_line.count("{")
            brace_depth -= body_line.count("}")
            if brace_depth == 0:
                break
            body_cursor += 1

        yield matched_name, signature_start, "\n".join(body_lines)
        line_index = body_cursor + 1


def extract_cpp_runtime_state_names(domain_name: str) -> list[dict[str, object]]:
    function_names = RUNTIME_STATE_FUNCTIONS.get(domain_name, ())
    if not function_names:
        return []

    states: list[dict[str, object]] = []
    seen: set[tuple[str, str, int]] = set()
    for path in iter_existing(RUNTIME_STATE_FILES):
        for function_name, function_line, body in iter_cpp_function_bodies(path, function_names):
            for match in QUOTED_RE.finditer(body):
                name = match.group(1)
                if not name or "%" in name:
                    continue
                key = (function_name, name, function_line)
                if key in seen:
                    continue
                seen.add(key)
                states.append(
                    {
                        "name": name,
                        "normalized": normalize_name(name),
                        "function": function_name,
                        "file": str(path.relative_to(REPO_ROOT)).replace("\\", "/"),
                        "line": function_line,
                    }
                )
    return states


def extract_cpp_behavior_coverage(domain_name: str) -> list[dict[str, object]]:
    coverage: list[dict[str, object]] = []
    for name, evidence_items in BEHAVIOR_COVERAGE_PATTERNS.get(domain_name, {}).items():
        evidence: list[dict[str, object]] = []
        for description, raw_path, pattern in evidence_items:
            path = REPO_ROOT / raw_path
            if not path.exists():
                continue
            evidence.append(
                {
                    "description": description,
                    "pattern": pattern,
                    "file": str(path.relative_to(REPO_ROOT)).replace("\\", "/"),
                    "line": find_line(path, pattern),
                }
            )
        if evidence:
            coverage.append(
                {
                    "name": name,
                    "normalized": normalize_name(name),
                    "evidence": evidence,
                }
            )
    return coverage


def item_key(item: dict[str, object]) -> str:
    return str(item["normalized"])


def unique_by_normalized(items: Iterable[dict[str, object]]) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    seen: set[str] = set()
    for item in items:
        key = item_key(item)
        if key in seen:
            continue
        seen.add(key)
        result.append(item)
    return result


def extract_csharp_script_commands(paths: Iterable[str]) -> list[dict[str, object]]:
    commands: list[dict[str, object]] = []
    for path in iter_existing(paths):
        text = read_text(path)
        for match in SCRIPT_CASE_RE.finditer(text):
            name = match.group("name")
            commands.append(
                {
                    "name": name,
                    "normalized": normalize_name(name),
                    "file": str(path.relative_to(REPO_ROOT)).replace("\\", "/"),
                    "line": text[: match.start()].count("\n") + 1,
                }
            )
    return unique_by_normalized(commands)


def extract_cpp_script_registrations(paths: Iterable[str]) -> list[dict[str, object]]:
    registrations: list[dict[str, object]] = []
    for path in iter_existing(paths):
        text = read_text(path)
        scan_text = mask_cpp_preprocessor_directives(mask_cpp_comments(text))
        for match in CPP_REG_FUNC_RE.finditer(scan_text):
            name = match.group("name")
            registrations.append(
                {
                    "name": name,
                    "normalized": normalize_name(name),
                    "alias_target": "",
                    "file": str(path.relative_to(REPO_ROOT)).replace("\\", "/"),
                    "line": text[: match.start()].count("\n") + 1,
                }
            )
        for match in CPP_REG_ALIAS_RE.finditer(scan_text):
            name = match.group("name")
            registrations.append(
                {
                    "name": name,
                    "normalized": normalize_name(name),
                    "alias_target": match.group("target"),
                    "file": str(path.relative_to(REPO_ROOT)).replace("\\", "/"),
                    "line": text[: match.start()].count("\n") + 1,
                }
            )
    return unique_by_normalized(registrations)


def mask_cpp_comments(text: str) -> str:
    def replace(match: re.Match[str]) -> str:
        value = match.group(0)
        if not value.startswith(("//", "/*")):
            return value
        return "".join(character if character in "\r\n" else " " for character in value)

    return CPP_COMMENT_OR_LITERAL_RE.sub(replace, text)


def mask_cpp_preprocessor_directives(text: str) -> str:
    """Mask logical preprocessor directives while preserving offsets and line numbers."""
    result: list[str] = []
    in_directive = False
    for line in text.splitlines(keepends=True):
        if not in_directive and line.lstrip().startswith("#"):
            in_directive = True
        if in_directive:
            result.append("".join(character if character in "\r\n" else " " for character in line))
        else:
            result.append(line)
        if in_directive and not line.rstrip("\r\n").endswith("\\"):
            in_directive = False
    return "".join(result)


def scan_resource_script_calls(root: Path, max_examples: int) -> list[dict[str, object]]:
    if not root.exists():
        return []

    calls: dict[str, dict[str, object]] = {}
    for path in root.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SCRIPT_EXTENSIONS:
            continue
        if "script" not in {part.lower() for part in path.parts}:
            continue
        if is_documentation_script(path):
            continue
        text = read_text_best_effort(path)
        in_block_comment = False
        for line_no, raw_line in enumerate(text.splitlines(), start=1):
            line, in_block_comment = strip_lua_comments(raw_line, in_block_comment)
            if not line.strip():
                continue
            for match in LUA_CALL_RE.finditer(line):
                name = match.group(1)
                normalized = normalize_name(name)
                if normalized in LUA_IGNORED_CALLS:
                    continue
                item = calls.setdefault(
                    normalized,
                    {
                        "name": name,
                        "normalized": normalized,
                        "count": 0,
                        "examples": [],
                    },
                )
                item["count"] = int(item["count"]) + 1
                examples = item["examples"]
                if isinstance(examples, list) and len(examples) < max_examples:
                    examples.append(
                        {
                            "file": str(path.relative_to(REPO_ROOT)).replace("\\", "/"),
                            "line": line_no,
                        }
                    )
    return sorted(calls.values(), key=lambda item: (str(item["name"]).lower(), str(item["name"])))


def analyze_script_commands(assets_root: Path, max_examples: int) -> dict[str, object]:
    csharp_commands = extract_csharp_script_commands(CSHARP_SCRIPT_FILES)
    cpp_registrations = extract_cpp_script_registrations(CPP_SCRIPT_FILES)
    asset_calls = scan_resource_script_calls(assets_root, max_examples)

    csharp_by_normalized = {item_key(item): item for item in csharp_commands}
    cpp_by_normalized = {item_key(item): item for item in cpp_registrations}
    asset_by_normalized = {item_key(item): item for item in asset_calls}

    csharp_only = [item for item in csharp_commands if item_key(item) not in cpp_by_normalized]
    cpp_only = [item for item in cpp_registrations if item_key(item) not in csharp_by_normalized]
    asset_missing_cpp = [item for item in asset_calls if item_key(item) not in cpp_by_normalized]
    asset_missing_csharp = [item for item in asset_calls if item_key(item) not in csharp_by_normalized]
    csharp_missing_cpp_used = [item for item in csharp_only if item_key(item) in asset_by_normalized]

    return {
        "csharp_command_count": len(csharp_commands),
        "cpp_registered_count": len(cpp_registrations),
        "asset_call_count": len(asset_calls),
        "csharp_commands": csharp_commands,
        "cpp_registrations": cpp_registrations,
        "asset_calls": asset_calls,
        "csharp_only_count": len(csharp_only),
        "cpp_only_count": len(cpp_only),
        "asset_missing_cpp_count": len(asset_missing_cpp),
        "asset_missing_csharp_count": len(asset_missing_csharp),
        "csharp_missing_cpp_used_count": len(csharp_missing_cpp_used),
        "csharp_only": csharp_only,
        "cpp_only": cpp_only,
        "asset_missing_cpp": asset_missing_cpp,
        "asset_missing_csharp": asset_missing_csharp,
        "csharp_missing_cpp_used": csharp_missing_cpp_used,
    }


def analyze_domain(domain: Domain) -> dict[str, object]:
    csharp_properties = extract_csharp_properties(domain.csharp_files)
    cpp_keys = extract_cpp_ini_keys(domain.cpp_files)
    cpp_runtime_states = extract_cpp_runtime_state_names(domain.name)
    cpp_behavior_coverage = extract_cpp_behavior_coverage(domain.name)
    cpp_by_normalized: dict[str, list[dict[str, object]]] = {}
    runtime_by_normalized: dict[str, list[dict[str, object]]] = {}
    behavior_by_normalized: dict[str, list[dict[str, object]]] = {}
    csharp_by_normalized: dict[str, list[dict[str, object]]] = {}
    for item in cpp_keys:
        cpp_by_normalized.setdefault(str(item["normalized"]), []).append(item)
    for item in cpp_runtime_states:
        runtime_by_normalized.setdefault(str(item["normalized"]), []).append(item)
    for item in cpp_behavior_coverage:
        behavior_by_normalized.setdefault(str(item["normalized"]), []).append(item)
    for item in csharp_properties:
        csharp_by_normalized.setdefault(str(item["normalized"]), []).append(item)

    matched = []
    runtime_covered = []
    behavior_covered = []
    csharp_only = []
    for item in csharp_properties:
        matches = cpp_by_normalized.get(str(item["normalized"]), [])
        if matches:
            matched.append({"csharp": item, "cpp": matches})
        elif runtime_by_normalized.get(str(item["normalized"]), []):
            runtime_covered.append({"csharp": item, "runtime": runtime_by_normalized[str(item["normalized"])]})
        elif behavior_by_normalized.get(str(item["normalized"]), []):
            behavior_covered.append({"csharp": item, "behavior": behavior_by_normalized[str(item["normalized"])]})
        else:
            csharp_only.append(item)

    cpp_only = []
    for item in cpp_keys:
        if str(item["normalized"]) not in csharp_by_normalized:
            cpp_only.append(item)

    return {
        "domain": domain.name,
        "csharp_files": domain.csharp_files,
        "cpp_files": domain.cpp_files,
        "csharp_property_count": len(csharp_properties),
        "cpp_key_count": len(cpp_keys),
        "cpp_runtime_state_count": len(cpp_runtime_states),
        "cpp_behavior_coverage_count": len(cpp_behavior_coverage),
        "matched_count": len(matched),
        "runtime_covered_count": len(runtime_covered),
        "behavior_covered_count": len(behavior_covered),
        "csharp_only_count": len(csharp_only),
        "cpp_only_count": len(cpp_only),
        "matched": matched,
        "runtime_covered": runtime_covered,
        "behavior_covered": behavior_covered,
        "csharp_only": csharp_only,
        "cpp_only": cpp_only,
        "cpp_runtime_states": cpp_runtime_states,
        "cpp_behavior_coverage": cpp_behavior_coverage,
    }


def format_location(item: dict[str, object]) -> str:
    return f"{item['file']}:{item['line']}"


def format_examples(item: dict[str, object]) -> str:
    examples = item.get("examples", [])
    if not isinstance(examples, list) or not examples:
        return ""
    return "<br>".join(format_location(example) for example in examples)


def write_markdown(
    results: list[dict[str, object]],
    script_result: dict[str, object],
    output: Path,
    max_rows: int,
) -> None:
    lines: list[str] = ["# C# Runtime Parity Audit", ""]
    lines.append("This report is code-first: C# public properties are compared with C++ INI loader keys by normalized name.")
    lines.append("A match only means the key is loaded somewhere; behavior still needs C# semantic review.")
    lines.append("")

    for result in results:
        lines.append(f"## {result['domain']}")
        lines.append("")
        lines.append(f"- C# properties: {result['csharp_property_count']}")
        lines.append(f"- C++ loader keys: {result['cpp_key_count']}")
        lines.append(f"- C++ runtime state aliases: {result['cpp_runtime_state_count']}")
        lines.append(f"- C++ behavior coverage entries: {result['cpp_behavior_coverage_count']}")
        lines.append(f"- Name matches: {result['matched_count']}")
        lines.append(f"- Runtime state covered: {result['runtime_covered_count']}")
        lines.append(f"- Behavior covered: {result['behavior_covered_count']}")
        lines.append(f"- C# only: {result['csharp_only_count']}")
        lines.append(f"- C++ only: {result['cpp_only_count']}")
        lines.append("")

        lines.append("### C# Covered By Runtime State API")
        lines.append("")
        lines.append("| Property | Type | C# Location | Runtime API |")
        lines.append("| --- | --- | --- | --- |")
        for item in result["runtime_covered"][:max_rows]:
            csharp = item["csharp"]
            runtime_items = item["runtime"]
            runtime_locations = "<br>".join(
                f"`{runtime['name']}` {runtime['function']} {format_location(runtime)}" for runtime in runtime_items
            )
            lines.append(f"| `{csharp['name']}` | `{csharp['type']}` | `{format_location(csharp)}` | {runtime_locations} |")
        if len(result["runtime_covered"]) > max_rows:
            lines.append(f"| ... | ... | ... | {len(result['runtime_covered']) - max_rows} more |")
        lines.append("")

        lines.append("### C# Covered By C++ Behavior")
        lines.append("")
        lines.append("| Property | Type | C# Location | C++ Evidence |")
        lines.append("| --- | --- | --- | --- |")
        for item in result["behavior_covered"][:max_rows]:
            csharp = item["csharp"]
            behavior_items = item["behavior"]
            evidence_parts: list[str] = []
            for behavior in behavior_items:
                for evidence in behavior["evidence"]:
                    evidence_parts.append(
                        f"{evidence['description']} {format_location(evidence)}"
                    )
            lines.append(
                f"| `{csharp['name']}` | `{csharp['type']}` | `{format_location(csharp)}` | "
                + "<br>".join(evidence_parts)
                + " |"
            )
        if len(result["behavior_covered"]) > max_rows:
            lines.append(f"| ... | ... | ... | {len(result['behavior_covered']) - max_rows} more |")
        lines.append("")

        lines.append("### C# Only")
        lines.append("")
        lines.append("| Property | Type | Location |")
        lines.append("| --- | --- | --- |")
        for item in result["csharp_only"][:max_rows]:
            lines.append(f"| `{item['name']}` | `{item['type']}` | `{format_location(item)}` |")
        if len(result["csharp_only"]) > max_rows:
            lines.append(f"| ... | ... | {len(result['csharp_only']) - max_rows} more |")
        lines.append("")

        lines.append("### C++ Only")
        lines.append("")
        lines.append("| Key | Location |")
        lines.append("| --- | --- |")
        for item in result["cpp_only"][:max_rows]:
            lines.append(f"| `{item['name']}` | `{format_location(item)}` |")
        if len(result["cpp_only"]) > max_rows:
            lines.append(f"| ... | {len(result['cpp_only']) - max_rows} more |")
        lines.append("")

    lines.append("## script")
    lines.append("")
    lines.append("- C# runner commands: " + str(script_result["csharp_command_count"]))
    lines.append("- C++ Lua registrations: " + str(script_result["cpp_registered_count"]))
    lines.append("- Asset script calls: " + str(script_result["asset_call_count"]))
    lines.append("- C# commands missing in C++: " + str(script_result["csharp_only_count"]))
    lines.append("- Asset calls missing in C++: " + str(script_result["asset_missing_cpp_count"]))
    lines.append("- C# missing in C++ and used by assets: " + str(script_result["csharp_missing_cpp_used_count"]))
    lines.append("")

    lines.append("### C# Commands Missing In C++")
    lines.append("")
    lines.append("| Command | C# Location | Asset Examples |")
    lines.append("| --- | --- | --- |")
    for item in script_result["csharp_only"][:max_rows]:
        asset_item = next(
            (
                used
                for used in script_result["asset_calls"]
                if str(used["normalized"]) == str(item["normalized"])
            ),
            None,
        )
        examples = format_examples(asset_item) if asset_item else ""
        lines.append(f"| `{item['name']}` | `{format_location(item)}` | {examples} |")
    if len(script_result["csharp_only"]) > max_rows:
        lines.append(f"| ... | ... | {len(script_result['csharp_only']) - max_rows} more |")
    lines.append("")

    lines.append("### Asset Calls Missing In C++")
    lines.append("")
    lines.append("| Call | Count | Examples |")
    lines.append("| --- | ---: | --- |")
    for item in script_result["asset_missing_cpp"][:max_rows]:
        lines.append(f"| `{item['name']}` | {item['count']} | {format_examples(item)} |")
    if len(script_result["asset_missing_cpp"]) > max_rows:
        lines.append(f"| ... | ... | {len(script_result['asset_missing_cpp']) - max_rows} more |")
    lines.append("")

    lines.append("### C++ Registrations Missing In C# Runner")
    lines.append("")
    lines.append("| Registration | Alias Target | Location |")
    lines.append("| --- | --- | --- |")
    for item in script_result["cpp_only"][:max_rows]:
        lines.append(f"| `{item['name']}` | `{item['alias_target']}` | `{format_location(item)}` |")
    if len(script_result["cpp_only"]) > max_rows:
        lines.append(f"| ... | ... | {len(script_result['cpp_only']) - max_rows} more |")
    lines.append("")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--domain",
        action="append",
        choices=sorted(DOMAINS.keys()),
        help="Domain to analyze. May be passed multiple times. Defaults to all domains.",
    )
    parser.add_argument("--json", type=Path, help="Write JSON report.")
    parser.add_argument("--markdown", type=Path, help="Write Markdown report.")
    parser.add_argument("--max-rows", type=int, default=40, help="Rows per Markdown table.")
    parser.add_argument("--max-examples", type=int, default=5, help="Asset examples per script command.")
    parser.add_argument("--assets-root", type=Path, default=REPO_ROOT / "assets", help="Assets root for script call usage scan.")
    args = parser.parse_args()

    domain_names = args.domain or sorted(DOMAINS.keys())
    results = [analyze_domain(DOMAINS[name]) for name in domain_names]
    assets_root = args.assets_root.resolve()
    script_result = analyze_script_commands(assets_root, args.max_examples)

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps({"domains": results, "script": script_result}, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
    if args.markdown:
        write_markdown(results, script_result, args.markdown, args.max_rows)
    if not args.json and not args.markdown:
        print(json.dumps({"domains": results, "script": script_result}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
