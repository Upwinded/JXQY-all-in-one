#!/usr/bin/env python3
"""Audit converted MOD script API usage against C++ and C# support tables."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


SCRIPT_EXTENSIONS = {".lua", ".txt"}
DEFAULT_COMPARE_REFS = ("main", "origin/main")

LUA_KEYWORDS = {
    "and",
    "break",
    "do",
    "else",
    "elseif",
    "end",
    "false",
    "for",
    "function",
    "goto",
    "if",
    "in",
    "local",
    "nil",
    "not",
    "or",
    "repeat",
    "return",
    "then",
    "true",
    "until",
    "while",
}

LUA_GLOBALS = {
    "_g",
    "_VERSION".lower(),
    "assert",
    "collectgarbage",
    "coroutine",
    "debug",
    "dofile",
    "error",
    "getmetatable",
    "io",
    "ipairs",
    "load",
    "loadfile",
    "math",
    "next",
    "os",
    "package",
    "pairs",
    "pcall",
    "print",
    "rawequal",
    "rawget",
    "rawlen",
    "rawset",
    "require",
    "select",
    "setmetatable",
    "string",
    "table",
    "tonumber",
    "tostring",
    "type",
    "utf8",
    "xpcall",
}

IGNORED_CALLS = LUA_KEYWORDS | LUA_GLOBALS

CALL_RE = re.compile(r"(?<![\w.:])([A-Za-z_][A-Za-z0-9_]*)\s*\(")
FUNCTION_DEFINITION_RE = re.compile(r"\b(?:local\s+)?function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(")
REG_FUNC_RE = re.compile(r"\bregFunc\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)")
REG_ALIAS_RE = re.compile(r'\bregAlias\s*\(\s*"([^"]+)"\s*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)')
CS_CASE_RE = re.compile(r'\bcase\s+"([^"]+)"\s*:')
CPP_COMMENT_OR_LITERAL_RE = re.compile(
    r'"(?:\\.|[^"\\])*"|'
    r"'(?:\\.|[^'\\])*'|"
    r"//[^\r\n]*|/\*.*?\*/",
    re.S,
)


@dataclass(frozen=True)
class Location:
    file: str
    line: int
    text: str


def normalize_api(name: str) -> str:
    return name.strip().lower()


def read_text_best_effort(path: Path) -> str:
    data = path.read_bytes()
    for encoding in ("utf-8-sig", "utf-8", "gbk", "cp950"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", errors="replace")


def match_lua_long_bracket(text: str, index: int) -> tuple[str, int] | None:
    if index >= len(text) or text[index] != "[":
        return None
    cursor = index + 1
    while cursor < len(text) and text[cursor] == "=":
        cursor += 1
    if cursor >= len(text) or text[cursor] != "[":
        return None
    closing = "]" + ("=" * (cursor - index - 1)) + "]"
    return closing, cursor + 1


def strip_lua_comments(line: str, in_block_comment: bool | str) -> tuple[str, bool | str]:
    result = []
    index = 0
    pending_state = in_block_comment
    while index < len(line):
        if pending_state:
            closing = pending_state if isinstance(pending_state, str) else "]]"
            if closing.startswith("comment:") or closing.startswith("string:"):
                closing = closing.split(":", 1)[1]
            end = line.find(closing, index)
            if end < 0:
                return "".join(result), pending_state
            index = end + len(closing)
            pending_state = False
            continue

        if line.startswith("--", index):
            long_comment = match_lua_long_bracket(line, index + 2)
            if long_comment:
                closing, content_start = long_comment
                result.append(" ")
                end = line.find(closing, content_start)
                if end < 0:
                    return "".join(result), f"comment:{closing}"
                index = end + len(closing)
                continue
            return "".join(result), False

        quote = line[index]
        if quote == "'" or quote == '"':
            result.append(" ")
            index += 1
            escaped = False
            while index < len(line):
                ch = line[index]
                if escaped:
                    escaped = False
                elif ch == "\\":
                    escaped = True
                elif ch == quote:
                    index += 1
                    break
                index += 1
            continue

        long_string = match_lua_long_bracket(line, index)
        if long_string:
            closing, content_start = long_string
            result.append(" ")
            end = line.find(closing, content_start)
            if end < 0:
                return "".join(result), f"string:{closing}"
            index = end + len(closing)
            continue

        result.append(line[index])
        index += 1
    return "".join(result), pending_state


def collect_cpp_registered_apis(script_cpp_text: str) -> set[str]:
    scan_text = mask_cpp_preprocessor_directives(mask_cpp_comments(script_cpp_text))
    apis = {normalize_api(match.group(1)) for match in REG_FUNC_RE.finditer(scan_text)}
    apis.update(normalize_api(match.group(1)) for match in REG_ALIAS_RE.finditer(scan_text))
    return apis


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


def collect_csharp_supported_apis(script_runner_text: str) -> set[str]:
    return {normalize_api(match.group(1)) for match in CS_CASE_RE.finditer(script_runner_text)}


def git_show(repo_root: Path, ref: str, relative_path: str) -> str | None:
    try:
        completed = subprocess.run(
            ["git", "-C", str(repo_root), "show", f"{ref}:{relative_path}"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
    except OSError:
        return None
    if completed.returncode != 0:
        return None
    return completed.stdout.decode("utf-8", errors="replace")


def load_compare_cpp_apis(repo_root: Path, compare_ref: str) -> tuple[str | None, set[str]]:
    refs = DEFAULT_COMPARE_REFS if compare_ref == "auto" else (compare_ref,)
    for ref in refs:
        text = git_show(repo_root, ref, "src/Game/Script/Script.cpp")
        if text is not None:
            return ref, collect_cpp_registered_apis(text)
    return None, set()


def find_script_roots(root: Path) -> list[Path]:
    root = root.resolve()
    if root.is_file():
        return []

    if root.name.lower() == "script":
        return [root]

    direct = root / "script"
    if direct.is_dir():
        return [direct.resolve()]

    script_roots = []
    for candidate in root.rglob("script"):
        if candidate.is_dir():
            resolved = candidate.resolve()
            if not any(is_relative_to(resolved, existing) for existing in script_roots):
                script_roots.append(resolved)
    return sorted(script_roots)


def is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def is_documentation_script(path: Path) -> bool:
    name = path.name.lower()
    stem = path.stem.lower()
    return stem.startswith("help") or stem.startswith("readme") or name in {"script.txt"}


def iter_script_files(script_roots: Iterable[Path], include_documentation: bool) -> Iterable[Path]:
    for script_root in script_roots:
        for path in sorted(script_root.rglob("*")):
            if not path.is_file() or path.suffix.lower() not in SCRIPT_EXTENSIONS:
                continue
            if not include_documentation and is_documentation_script(path):
                continue
            else:
                yield path


def scan_script_file(path: Path, repo_root: Path, max_locations_per_api: int,
                     calls: Counter[str], locations: dict[str, list[Location]]) -> None:
    content = read_text_best_effort(path)
    in_block_comment: bool | str = False
    try:
        relative_file = str(path.resolve().relative_to(repo_root.resolve())).replace("\\", "/")
    except ValueError:
        relative_file = str(path)

    for line_number, raw_line in enumerate(content.splitlines(), start=1):
        line, in_block_comment = strip_lua_comments(raw_line, in_block_comment)
        if not line.strip():
            continue

        definition_spans = [
            (match.start(1), match.end(1))
            for match in FUNCTION_DEFINITION_RE.finditer(line)
        ]
        for match in CALL_RE.finditer(line):
            if any(start <= match.start(1) < end for start, end in definition_spans):
                continue
            api = normalize_api(match.group(1))
            if api in IGNORED_CALLS:
                continue
            calls[api] += 1
            if len(locations[api]) < max_locations_per_api:
                locations[api].append(Location(relative_file, line_number, raw_line.strip()))


def summarize_api(api: str, calls: Counter[str], locations: dict[str, list[Location]]) -> dict[str, object]:
    return {
        "api": api,
        "count": calls[api],
        "locations": [location.__dict__ for location in locations.get(api, [])],
    }


def build_report(args: argparse.Namespace) -> dict[str, object]:
    repo_root = Path(args.repo_root).resolve()
    script_cpp_path = repo_root / "src" / "Game" / "Script" / "Script.cpp"
    script_runner_path = repo_root / "JxqyHD-develop" / "Engine" / "Script" / "ScriptRunner.cs"

    cpp_apis = collect_cpp_registered_apis(read_text_best_effort(script_cpp_path))
    csharp_apis = collect_csharp_supported_apis(read_text_best_effort(script_runner_path))
    compare_ref, compare_cpp_apis = load_compare_cpp_apis(repo_root, args.compare_ref)

    input_roots = [Path(path).resolve() for path in args.paths]
    script_roots = []
    for root in input_roots:
        script_roots.extend(find_script_roots(root))
    script_roots = sorted(dict.fromkeys(script_roots))

    calls: Counter[str] = Counter()
    locations: dict[str, list[Location]] = defaultdict(list)
    script_files = list(iter_script_files(script_roots, args.include_documentation))
    for script_file in script_files:
        scan_script_file(script_file, repo_root, args.max_locations, calls, locations)

    used_apis = set(calls)
    missing_cpp = sorted(used_apis - cpp_apis)
    missing_cpp_supported_by_csharp = sorted(set(missing_cpp) & csharp_apis)
    missing_cpp_and_csharp = sorted(set(missing_cpp) - csharp_apis)
    csharp_not_cpp = sorted(csharp_apis - cpp_apis)
    current_not_compare = sorted(cpp_apis - compare_cpp_apis) if compare_ref else []

    return {
        "repoRoot": str(repo_root),
        "inputRoots": [str(path) for path in input_roots],
        "scriptRoots": [str(path) for path in script_roots],
        "scriptFileCount": len(script_files),
        "uniqueCalledApiCount": len(used_apis),
        "cppRegisteredApiCount": len(cpp_apis),
        "csharpSupportedApiCount": len(csharp_apis),
        "compareRef": compare_ref,
        "compareCppRegisteredApiCount": len(compare_cpp_apis) if compare_ref else None,
        "calledApis": [summarize_api(api, calls, locations) for api in sorted(used_apis)],
        "topCalledApis": [
            summarize_api(api, calls, locations)
            for api, _ in calls.most_common(args.top)
        ],
        "missingCppApis": [summarize_api(api, calls, locations) for api in missing_cpp],
        "missingCppApisSupportedByCSharp": [
            summarize_api(api, calls, locations)
            for api in missing_cpp_supported_by_csharp
        ],
        "missingCppApisUnknownToCSharp": [
            summarize_api(api, calls, locations)
            for api in missing_cpp_and_csharp
        ],
        "csharpApisNotRegisteredInCpp": csharp_not_cpp,
        "cppApisNewSinceCompareRef": current_not_compare,
    }


def format_api_list(items: list[dict[str, object]], limit: int | None = None) -> list[str]:
    if limit is not None:
        items = items[:limit]
    lines = []
    for item in items:
        api = item["api"]
        count = item["count"]
        locations = item.get("locations", [])
        if locations:
            first = locations[0]
            lines.append(f"- {api} ({count}) first: {first['file']}:{first['line']}")
        else:
            lines.append(f"- {api} ({count})")
    return lines


def format_markdown(report: dict[str, object], top_limit: int) -> str:
    lines = [
        "# MOD Script API Audit",
        "",
        f"- Script files: {report['scriptFileCount']}",
        f"- Unique called APIs: {report['uniqueCalledApiCount']}",
        f"- C++ registered APIs: {report['cppRegisteredApiCount']}",
        f"- C# supported APIs: {report['csharpSupportedApiCount']}",
        f"- Compare ref: {report['compareRef'] or '<not found>'}",
        f"- Compare ref registered APIs: {report['compareCppRegisteredApiCount'] if report['compareRef'] else '<not found>'}",
        "",
        "## Script Roots",
    ]
    script_roots = report["scriptRoots"]
    if script_roots:
        lines.extend(f"- {root}" for root in script_roots)
    else:
        lines.append("- <none>")

    lines.extend(["", "## Missing In C++ But Present In C#"])
    missing_supported = report["missingCppApisSupportedByCSharp"]
    lines.extend(format_api_list(missing_supported, top_limit) if missing_supported else ["- None"])

    lines.extend(["", "## Missing In C++ And Unknown To C#"])
    missing_unknown = report["missingCppApisUnknownToCSharp"]
    lines.extend(format_api_list(missing_unknown, top_limit) if missing_unknown else ["- None"])

    lines.extend(["", "## Top Called APIs"])
    top_called = report["topCalledApis"]
    lines.extend(format_api_list(top_called, top_limit) if top_called else ["- None"])

    lines.extend(["", "## C# APIs Not Registered In C++"])
    csharp_not_cpp = report["csharpApisNotRegisteredInCpp"]
    lines.extend(f"- {api}" for api in csharp_not_cpp[:top_limit]) if csharp_not_cpp else lines.append("- None")

    lines.extend(["", "## C++ APIs New Since Compare Ref"])
    new_since_ref = report["cppApisNewSinceCompareRef"]
    lines.extend(f"- {api}" for api in new_since_ref) if new_since_ref else lines.append("- None")

    return "\n".join(lines) + "\n"


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Scan converted MOD scripts and compare API calls with C++/C# support.",
    )
    parser.add_argument(
        "paths",
        nargs="*",
        default=["assets"],
        help="Resource pack, assets collection, or script directory paths. Defaults to assets.",
    )
    parser.add_argument("--repo-root", default=".", help="Repository root. Defaults to current directory.")
    parser.add_argument(
        "--compare-ref",
        default="auto",
        help="Git ref for C++ API delta. Use auto to try main then origin/main. Defaults to auto.",
    )
    parser.add_argument("--json", dest="json_path", help="Write full JSON report to this path.")
    parser.add_argument("--markdown", dest="markdown_path", help="Write Markdown summary to this path.")
    parser.add_argument("--top", type=int, default=30, help="Number of top/list entries in text output.")
    parser.add_argument(
        "--max-locations",
        type=int,
        default=5,
        help="Maximum call locations retained per API in the report.",
    )
    parser.add_argument(
        "--include-documentation",
        action="store_true",
        help="Include help/readme-like files under script directories. They are skipped by default.",
    )
    parser.add_argument(
        "--fail-on-missing-cpp",
        action="store_true",
        help="Exit 1 when scanned scripts call APIs not registered in C++.",
    )
    parser.add_argument(
        "--fail-on-missing-csharp",
        action="store_true",
        help="Exit 1 when scanned scripts call APIs unknown to both C++ and C#.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    report = build_report(args)

    markdown = format_markdown(report, args.top)
    print(markdown, end="")

    if args.json_path:
        Path(args.json_path).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    if args.markdown_path:
        Path(args.markdown_path).write_text(markdown, encoding="utf-8")

    if args.fail_on_missing_csharp and report["missingCppApisUnknownToCSharp"]:
        return 1
    if args.fail_on_missing_cpp and report["missingCppApis"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
