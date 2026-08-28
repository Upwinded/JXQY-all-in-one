#!/usr/bin/env python3
"""Static checks for converted MOD resource packs.

The checker is intentionally narrow and non-mutating. It complements
validate-scripts by checking resource-pack metadata and script-visible resource
references that can be verified without launching the game.
"""

from __future__ import annotations

import argparse
import configparser
import json
import os
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


PROFILE_REQUIRED_FIELDS = (
    ("Game", "Id"),
    ("Game", "Name"),
    ("Save", "Namespace"),
    ("Title", "Menu"),
    ("NewGame", "Script"),
)

BASE_PACK_IDS = {"JXQY2", "YYCS", "XJXQY"}
SUPPORTED_UI_PROFILES = {"JXQY2", "YYCS", "XJXQY"}
SCRIPT_EXTENSIONS = {".txt", ".lua"}
STANDARD_RESOURCE_DIRS = ("ini", "map", "script", "asf", "mpc", "sound", "music", "video")
UI_RESOURCE_ROOTS = ("ini/ui", "asf/ui", "mpc/ui", "bmp/ui", "image/ui", "sound/ui")
SHOW_SIGNAL_TIP_RE = re.compile(
    r"\bshowsignaltip\s*\(\s*(?:\"[^\"]*\"|'[^']*'|[^,]*),\s*([0-9]+)",
    re.IGNORECASE,
)
SCRIPT_MEDIA_CALL_RE = re.compile(
    r"\b(playmusic|playsound|playmovie)\s*\(\s*([\"'])(.*?)\2",
    re.IGNORECASE,
)
INI_SECTION_RE = re.compile(r"^\s*\[([^\]]+)\]")
INI_KEY_VALUE_RE = re.compile(r"^\s*([^#;\[\]=][^=]*?)\s*=\s*(.*)$")
MUSIC_FALLBACK_EXTENSIONS = (".mp3", ".ogg", ".wma", ".wav")
VIDEO_FALLBACK_EXTENSIONS = (".avi", ".mp4", ".wmv", ".mpg", ".mpeg")
SOUND_FALLBACK_EXTENSIONS = (".wav",)
MAGIC_LINKED_LOAD_MAX_DEPTH = 16
MAGIC_LINKED_LOAD_MAX_NODES = 256
MAGIC_DIRECT_CHILD_KEYS = {
    "attackfile": "AttackFile",
    "flymagic": "FlyMagic",
    "explodemagicfile": "ExplodeMagicFile",
    "parasiticmagic": "ParasiticMagic",
    "randmagicfile": "RandMagicFile",
    "secondmagicfile": "SecondMagicFile",
    "magicwhennewpos": "MagicWhenNewPos",
    "magictousewhenkillenemy": "MagicToUseWhenKillEnemy",
    "bounceflyendmagic": "BounceFlyEndMagic",
    "changemagic": "ChangeMagic",
    "jumpendmagic": "JumpEndMagic",
}
MAGIC_EXPERIENCE_OWNER_CHILD_KEYS = {
    "flymagic",
    "explodemagicfile",
    "parasiticmagic",
    "jumpendmagic",
}
OBJECT_ENTITY_SIGNATURE_KEYS = {
    "objname",
    "name",
    "objfile",
    "scriptfile",
    "scriptfileright",
    "wavfile",
    "kind",
    "dir",
    "mapx",
    "mapy",
    "offx",
    "offy",
    "offsetx",
    "offsety",
    "damage",
    "frame",
}
OBJECT_RES_COMMON_KEYS = {"image", "shade", "sound"}


@dataclass
class PackInfo:
    section: str
    pack_id: str
    root: Path
    manifest: str
    base_id: str
    ui_base_id: str = ""
    ui_profile: str = ""
    save_namespace: str = ""


@dataclass
class Issue:
    severity: str
    pack_id: str
    message: str
    path: str = ""

    def format(self) -> str:
        location = f" [{self.path}]" if self.path else ""
        return f"{self.severity}: {self.pack_id}: {self.message}{location}"


def lookup_pack_profile(
    pack: PackInfo,
    profiles: dict[str, configparser.ConfigParser | None],
) -> configparser.ConfigParser | None:
    """Find a profile without assuming Game.Id is globally unique."""
    if pack.section in profiles:
        return profiles[pack.section]
    return profiles.get(pack.pack_id)


def first_pack_keys_by_id(packs: dict[str, PackInfo]) -> dict[str, str]:
    """Return the deterministic first catalog owner for each Game.Id."""
    result: dict[str, str] = {}
    for storage_key, pack in packs.items():
        result.setdefault(ascii_lower(pack.pack_id), storage_key)
    return result


def first_packs_by_id(packs: dict[str, PackInfo]) -> dict[str, PackInfo]:
    result: dict[str, PackInfo] = {}
    for pack in packs.values():
        result.setdefault(ascii_lower(pack.pack_id), pack)
    return result


def issue_category(issue: Issue) -> str:
    message = issue.message.lower()
    if "standard resource directory" in message:
        return "standard_directory"
    if "save.namespace" in message:
        return "save_namespace"
    if "ui base" in message or "ui.profile" in message or "ui graph" in message:
        return "ui_configuration"
    if "ui component image" in message:
        return "ui_component_image"
    if "dependency" in message:
        return "dependency"
    if "profile " in message or "game_profile.ini" in message:
        return "profile"
    if "showsignaltip" in message:
        return "signal_tip"
    if "script media" in message:
        return "script_media"
    if "object objfile" in message:
        return "object_objfile"
    if "ini/objres file appears" in message:
        return "object_resource_instance_fields"
    if (
        "object resource has " in message
        or "object resource common keys" in message
        or "malformed [common]" in message
    ):
        return "object_resource_structure"
    if "object resource" in message and "image" in message:
        return "object_resource_image"
    if "object resource" in message and "shade" in message:
        return "object_resource_shade"
    if "object resource" in message and "sound" in message:
        return "object_resource_sound"
    if "goods resource" in message:
        return "goods_resource_image"
    if "goods linked magic" in message:
        return "goods_magic_reference"
    if "magic action resource" in message:
        return "magic_action_image"
    if "magic effect resource" in message:
        return "magic_effect_image"
    if "linked magic" in message:
        return "magic_linked_resource"
    if "magic resource" in message:
        return "magic_resource_image"
    return "other"


OPTIONAL_RESOURCE_REFERENCE_CATEGORIES = {
    "signal_tip",
    "script_media",
    "ui_component_image",
    "object_objfile",
    "object_resource_image",
    "object_resource_shade",
    "object_resource_sound",
    "goods_resource_image",
    "goods_magic_reference",
    "magic_action_image",
    "magic_effect_image",
    "magic_linked_resource",
    "magic_resource_image",
}

NON_BLOCKING_PACK_CONFIGURATION_CATEGORIES = {
    "dependency",
    "profile",
    "save_namespace",
    "ui_configuration",
}


def calibrate_issue_severity(issue: Issue) -> None:
    # 单个包的引用、依赖、UI 或可默认的清单配置问题只影响对应能力，
    # 静态检查仍报告问题，但不再把整包作为发布阻断项。
    if issue.severity == "ERROR" and issue_category(issue) in (
        OPTIONAL_RESOURCE_REFERENCE_CATEGORIES
        | NON_BLOCKING_PACK_CONFIGURATION_CATEGORIES
    ):
        issue.severity = "WARNING"


def severity_counts(issues: list[Issue]) -> dict[str, int]:
    counter = Counter(issue.severity for issue in issues)
    return {
        "ERROR": counter.get("ERROR", 0),
        "WARNING": counter.get("WARNING", 0),
        "INFO": counter.get("INFO", 0),
    }


def category_summary(issues: list[Issue]) -> list[dict[str, int | str]]:
    categories = sorted({issue_category(issue) for issue in issues})
    rows: list[dict[str, int | str]] = []
    for category in categories:
        category_issues = [issue for issue in issues if issue_category(issue) == category]
        counts = severity_counts(category_issues)
        rows.append(
            {
                "category": category,
                "errors": counts["ERROR"],
                "warnings": counts["WARNING"],
                "infos": counts["INFO"],
                "total": len(category_issues),
            }
        )
    rows.sort(key=lambda row: (-int(row["errors"]), -int(row["warnings"]), -int(row["total"]), str(row["category"])))
    return rows


def pack_summary(issues: list[Issue]) -> list[dict[str, int | str]]:
    packs = sorted({issue.pack_id for issue in issues})
    rows: list[dict[str, int | str]] = []
    for pack_id in packs:
        pack_issues = [issue for issue in issues if issue.pack_id == pack_id]
        counts = severity_counts(pack_issues)
        rows.append(
            {
                "pack_id": pack_id,
                "errors": counts["ERROR"],
                "warnings": counts["WARNING"],
                "infos": counts["INFO"],
                "total": len(pack_issues),
            }
        )
    rows.sort(key=lambda row: (-int(row["errors"]), -int(row["warnings"]), -int(row["total"]), str(row["pack_id"])))
    return rows


def report_issue_limit(issues: list[Issue], limit: int) -> list[Issue]:
    if limit < 0:
        return issues
    return issues[:limit]


def parse_filter_set(value: str, *, normalize_upper: bool = False, normalize_lower: bool = False) -> set[str]:
    result: set[str] = set()
    for item in value.split(","):
        item = item.strip()
        if not item:
            continue
        if normalize_upper:
            result.add(item.upper())
        elif normalize_lower:
            result.add(item.lower())
        else:
            result.add(item)
    return result


def matching_gate_issues(
    issues: list[Issue],
    categories: set[str],
    severities: set[str],
) -> list[Issue]:
    if not categories:
        return []
    return [
        issue
        for issue in issues
        if issue_category(issue) in categories and issue.severity.upper() in severities
    ]


def write_json_report(path: Path, assets_root: Path, issues: list[Issue]) -> None:
    payload = {
        "assets_root": str(assets_root.resolve()),
        "summary": severity_counts(issues),
        "categories": category_summary(issues),
        "packs": pack_summary(issues),
        "issues": [
            {
                "severity": issue.severity,
                "pack_id": issue.pack_id,
                "category": issue_category(issue),
                "message": issue.message,
                "path": issue.path,
            }
            for issue in issues
        ],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")


def write_markdown_report(path: Path, assets_root: Path, issues: list[Issue], issue_limit: int) -> None:
    counts = severity_counts(issues)
    lines = [
        "# MOD Resource Static Check",
        "",
        f"- Assets: `{assets_root.resolve()}`",
        f"- Errors: {counts['ERROR']}",
        f"- Warnings: {counts['WARNING']}",
        f"- Infos: {counts['INFO']}",
        "",
        "## Categories",
        "",
        "| Category | Errors | Warnings | Infos | Total |",
        "| --- | ---: | ---: | ---: | ---: |",
    ]
    for row in category_summary(issues):
        lines.append(
            f"| `{row['category']}` | {row['errors']} | {row['warnings']} | {row['infos']} | {row['total']} |"
        )

    lines.extend(
        [
            "",
            "## Packs",
            "",
            "| Pack | Errors | Warnings | Infos | Total |",
            "| --- | ---: | ---: | ---: | ---: |",
        ]
    )
    for row in pack_summary(issues):
        lines.append(
            f"| `{row['pack_id']}` | {row['errors']} | {row['warnings']} | {row['infos']} | {row['total']} |"
        )

    report_issues = report_issue_limit(issues, issue_limit)
    lines.extend(
        [
            "",
            "## Issues",
            "",
        ]
    )
    if issue_limit >= 0 and len(report_issues) < len(issues):
        lines.append(f"Showing first {len(report_issues)} of {len(issues)} issues. JSON report contains the full list.")
        lines.append("")
    for issue in report_issues:
        location = f" `{issue.path}`" if issue.path else ""
        lines.append(f"- `{issue.severity}` `{issue.pack_id}` `{issue_category(issue)}` {issue.message}{location}")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def print_category_summary(issues: list[Issue]) -> None:
    print("Categories:")
    for row in category_summary(issues):
        print(
            f"  {row['category']}: "
            f"errors={row['errors']} warnings={row['warnings']} infos={row['infos']} total={row['total']}"
        )


def read_ini(path: Path) -> configparser.ConfigParser:
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.optionxform = ascii_lower
    parser.read(path, encoding="utf-8")
    return parser


def find_ini_section(parser: configparser.ConfigParser, section: str, key: str = "") -> str:
    normalized_section = ascii_lower(section)
    for candidate in reversed(parser.sections()):
        if ascii_lower(candidate) == normalized_section and (
            not key or parser.has_option(candidate, key)
        ):
            return candidate
    return ""


def get_value(parser: configparser.ConfigParser, section: str, key: str) -> str:
    actual_section = find_ini_section(parser, section, key)
    if not actual_section:
        return ""
    return parser.get(actual_section, key, fallback="").strip()


def get_boolean_value(
    parser: configparser.ConfigParser,
    section: str,
    key: str,
    fallback: bool,
) -> bool:
    actual_section = find_ini_section(parser, section, key)
    if not actual_section:
        return fallback
    return parser.getboolean(actual_section, key, fallback=fallback)


def split_dependency_ids(value: str) -> list[str]:
    result: list[str] = []
    seen: set[str] = set()
    for part in value.split(","):
        dependency_id = part.strip()
        normalized_id = dependency_id.lower()
        if dependency_id and normalized_id not in seen:
            seen.add(normalized_id)
            result.append(dependency_id)
    return result


def normalize_relative_path(value: str) -> Path:
    value = value.strip().replace("\\", "/").lstrip("/")
    return Path(*[part for part in value.split("/") if part and part != "."])


def path_from_profile_value(root: Path, value: str) -> Path:
    value = value.strip().replace("\\", "/")
    if not value:
        return root
    path = Path(value)
    if path.is_absolute():
        return path.resolve(strict=False)
    return (root / path).resolve(strict=False)


def sanitize_save_namespace(value: str) -> str:
    result: list[str] = []
    for char in value.replace("\\", "/"):
        if ord(char) >= 0x80 or char.isascii() and (char.isalnum() or char in {"-", "_"}):
            result.append(char)
        elif char in {"/", ":", "."}:
            result.append("_")
    return "".join(result) or "default"


def find_child_case_insensitive(parent: Path, name: str) -> Path | None:
    if not parent.exists() or not parent.is_dir():
        return None
    lower_name = name.lower()
    for child in parent.iterdir():
        if child.name.lower() == lower_name:
            return child
    return None


def is_path_contained_by_root(root: Path, candidate: Path) -> bool:
    try:
        normalized_root = os.path.normcase(str(root.resolve(strict=False)))
        normalized_candidate = os.path.normcase(str(candidate.resolve(strict=False)))
        return os.path.commonpath([normalized_root, normalized_candidate]) == normalized_root
    except (OSError, ValueError):
        return False


def media_file_stem(name: str) -> str:
    return Path(name).stem


def media_file_suffix(name: str) -> str:
    return Path(name).suffix.lower()


def stable_resource_alias_prefix(stem: str) -> str:
    chars: list[str] = []
    has_digit = False
    for char in stem:
        if ord(char) >= 0x80:
            break
        if char.isalnum() or char in {"-", "_"}:
            chars.append(char.lower())
            has_digit = has_digit or char.isdigit()
            continue
        break
    while chars and chars[-1] in {"-", "_"}:
        chars.pop()
    prefix = "".join(chars)
    if not has_digit or len(prefix) < 3:
        return ""
    return prefix


def ascii_icon_suffix(stem: str) -> bool:
    return bool(stem) and ord(stem[-1]) < 0x80 and stem[-1].lower() == "s"


def find_unique_resource_alias(parent: Path, name: str) -> Path | None:
    if not parent.exists() or not parent.is_dir():
        return None
    suffix = media_file_suffix(name)
    if suffix not in {".asf", ".mpc", ".shd", ".png"}:
        return None
    stem = media_file_stem(name)
    prefix = stable_resource_alias_prefix(stem)
    if not prefix:
        return None
    wants_icon_suffix = ascii_icon_suffix(stem)
    matches: list[Path] = []
    for child in parent.iterdir():
        if not child.is_file():
            continue
        if child.name.lower() == name.lower():
            continue
        if media_file_suffix(child.name) != suffix:
            continue
        child_stem = media_file_stem(child.name)
        if ascii_icon_suffix(child_stem) != wants_icon_suffix:
            continue
        if stable_resource_alias_prefix(child_stem) == prefix:
            matches.append(child)
    if len(matches) == 1:
        return matches[0]
    return None


def is_image_package_relative_path(relative_path: Path) -> bool:
    if not relative_path.parts:
        return False
    first_part = relative_path.parts[0].lower()
    return first_part == "asf" or first_part == "mpc"


def exact_path_status(root: Path, relative_path: Path) -> tuple[bool, bool, Path]:
    current = root
    exact = True
    parts = relative_path.parts
    for index, part in enumerate(parts):
        actual = find_child_case_insensitive(current, part)
        if actual is None:
            if index == len(parts) - 1 and is_image_package_relative_path(relative_path):
                alias = find_unique_resource_alias(current, part)
                if alias is not None and is_path_contained_by_root(root, alias):
                    return True, False, alias
            return False, exact, current / part
        if actual.name != part:
            exact = False
        if index < len(parts) - 1 and not actual.is_dir():
            return False, exact, current / part
        current = actual
    return current.is_file() and is_path_contained_by_root(root, current), exact, current


def discover_packs(assets_root: Path) -> tuple[dict[str, PackInfo], Path | None, list[Issue]]:
    issues: list[Issue] = []
    collection_config_path = assets_root / "resources.ini"
    common_root: Path | None = None
    packs: dict[str, PackInfo] = {}
    canonical_ids: dict[str, str] = {}

    if collection_config_path.exists():
        try:
            collection_config = read_ini(collection_config_path)
            common_path = (
                get_value(collection_config, "Collection", "CommonPath") or "common"
            )
            common_root = assets_root / normalize_relative_path(common_path)
        except (OSError, configparser.Error, ValueError) as exc:
            issues.append(Issue(
                "ERROR",
                "assets",
                f"invalid collection resources.ini: {exc}",
                str(collection_config_path),
            ))
    else:
        default_common_root = assets_root / "common"
        common_root = default_common_root if default_common_root.is_dir() else None

    root_manifest = assets_root / "game_profile.ini"
    if root_manifest.is_file():
        candidate_roots = [("root", assets_root)]
    else:
        candidate_roots = [
            (f"pack.{child.name.casefold()}", child)
            for child in sorted(
                assets_root.iterdir(),
                key=lambda path: (path.name.casefold(), path.name),
            )
            if not child.name.startswith(".")
            and child.is_dir()
            and not child.is_symlink()
            and (child / "game_profile.ini").is_file()
        ]

    for section, root in candidate_roots:
        manifest_path = root / "game_profile.ini"
        try:
            profile = read_ini(manifest_path)
        except (OSError, configparser.Error, ValueError) as exc:
            issues.append(Issue(
                "ERROR",
                section,
                f"invalid game_profile.ini: {exc}",
                str(manifest_path),
            ))
            continue
        pack_id = get_value(profile, "Game", "Id")
        if not pack_id:
            issues.append(Issue(
                "ERROR",
                section,
                "game_profile.ini is missing Game.Id",
                str(manifest_path),
            ))
            continue

        canonical_id = ascii_lower(pack_id)
        storage_key = pack_id
        if canonical_id in canonical_ids:
            stable_suffix = sanitize_save_namespace(section) or "entry"
            storage_key = f"{pack_id}@{stable_suffix}"
            disambiguator = 2
            while storage_key in packs:
                storage_key = f"{pack_id}@{stable_suffix}-{disambiguator}"
                disambiguator += 1
            issues.append(Issue(
                "ERROR",
                pack_id,
                "duplicate Game.Id; runtime selection must remain blocked until the conflict is resolved",
                str(manifest_path),
            ))
        packs[storage_key] = PackInfo(
            section,
            pack_id,
            root,
            "game_profile.ini",
            get_value(profile, "Resource", "DependencyId"),
            get_value(profile, "UI", "BaseId"),
            get_value(profile, "UI", "Profile"),
            get_value(profile, "Save", "Namespace"),
        )
        canonical_ids.setdefault(canonical_id, storage_key)

    if not candidate_roots:
        issues.append(Issue(
            "WARNING",
            "assets",
            "no root-level or direct-child game_profile.ini was discovered",
            str(assets_root),
        ))

    return packs, common_root, issues


def iter_resolution_roots(
    pack: PackInfo,
    packs: dict[str, PackInfo],
    active_common_root: Path | None,
    profile_dependency_id: str = "",
) -> Iterable[tuple[Path, str]]:
    # 返回 (root, source) 二元组，source 用于审计资源到底从哪一层解析到：
    #   "self"       —— 当前 MOD 自身根
    #   "dependency" —— 依赖链上的 MOD 根（DependencyId/Base）
    #   "common"     —— 公共资源根
    packs_by_lower_id = first_packs_by_id(packs)
    seen_pack_ids: set[str] = set()
    manifest_cache: dict[Path, configparser.ConfigParser | None] = {}

    def manifest_for_root(root: Path, manifest_name: str = "game_profile.ini") -> configparser.ConfigParser | None:
        manifest_path = (root / manifest_name).resolve(strict=False)
        if manifest_path not in manifest_cache:
            manifest_cache[manifest_path] = read_ini(manifest_path) if manifest_path.is_file() else None
        return manifest_cache[manifest_path]

    def manifest_dependency_ids(
        root: Path,
        manifest_name: str = "game_profile.ini",
    ) -> str:
        profile = manifest_for_root(root, manifest_name)
        if profile is None:
            return ""
        return get_value(profile, "Resource", "DependencyId")

    def visit_pack(current_pack: PackInfo, source: str, first: bool = False) -> Iterable[tuple[Path, str]]:
        normalized_id = current_pack.pack_id.lower()
        if normalized_id in seen_pack_ids:
            return
        seen_pack_ids.add(normalized_id)
        yield current_pack.root, source

        dependency_value = current_pack.base_id
        manifest_dependency_value = manifest_dependency_ids(
            current_pack.root,
            current_pack.manifest,
        )
        if first and not dependency_value and profile_dependency_id:
            dependency_value = profile_dependency_id
        if not dependency_value:
            dependency_value = manifest_dependency_value
        for dependency_id in split_dependency_ids(dependency_value):
            dependency_pack = packs_by_lower_id.get(dependency_id.lower())
            if dependency_pack is not None:
                yield from visit_pack(dependency_pack, "dependency")

    yield from visit_pack(pack, "self", True)
    if active_common_root is not None:
        yield active_common_root, "common"


def describe_resolution_source(source: str) -> str:
    if source == "dependency":
        return " (via dependency)"
    if source == "common":
        return " (via common)"
    return ""


def resolve_common_root(
    pack: PackInfo,
    profile: configparser.ConfigParser | None,
    collection_common_root: Path | None,
) -> Path | None:
    return collection_common_root


def resolve_resource(
    relative_name: str,
    roots: Iterable[tuple[Path, str]],
) -> tuple[Path | None, bool, str]:
    relative_path = normalize_relative_path(relative_name)
    for root, source in roots:
        exists, exact, actual = exact_path_status(root, relative_path)
        if exists:
            return actual, exact, source
    return None, True, ""


def is_ascii_case_only_resolution(
    relative_name: str,
    resolved_path: Path,
    roots: Iterable[tuple[Path, str]],
) -> bool:
    requested_parts = tuple(
        ascii_lower(part) for part in normalize_relative_path(relative_name).parts
    )
    resolved_absolute = resolved_path.resolve(strict=False)
    for root, _source in roots:
        try:
            relative_resolved = resolved_absolute.relative_to(root.resolve(strict=False))
        except ValueError:
            continue
        actual_parts = tuple(ascii_lower(part) for part in relative_resolved.parts)
        if actual_parts == requested_parts:
            return True
    return False


def resolve_any_resource(
    relative_names: Iterable[str],
    roots: Iterable[tuple[Path, str]],
) -> tuple[Path | None, bool, str, str]:
    root_list = list(roots)
    relative_name_list = list(relative_names)
    for root, source in root_list:
        for relative_name in relative_name_list:
            relative_path = normalize_relative_path(relative_name)
            exists, exact, actual = exact_path_status(root, relative_path)
            if exists:
                return actual, exact, relative_name, source
    return None, True, "", ""


def profile_reference_candidates(label: str, relative_name: str) -> list[str]:
    if label == "Title.Music":
        return media_asset_candidates(
            "music", relative_name, MUSIC_FALLBACK_EXTENSIONS
        )
    if label in {"Startup.Videos", "Title.TeamVideo"}:
        normalized = normalize_relative_path(relative_name)
        if len(normalized.parts) > 1:
            return [relative_name]
        return [f"video\\{relative_name}", relative_name]
    if label != "NewGame.Script":
        return [relative_name]
    normalized = normalize_relative_path(relative_name)
    if len(normalized.parts) > 1:
        return [relative_name]
    return [
        f"script\\common\\{relative_name}",
        f"script\\goods\\{relative_name}",
        relative_name,
    ]


def split_profile_list(value: str) -> list[str]:
    result: list[str] = []
    for item in re.split(r"[,;]", value):
        item = item.strip()
        if not item:
            continue
        if ":" in item and not re.match(r"^[A-Za-z]:[\\/]", item):
            item = item.rsplit(":", 1)[1].strip()
        if item:
            result.append(item)
    return result


def read_text_lossy(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return path.read_text(encoding="gb18030", errors="replace")


def strip_lua_line_comment(line: str) -> str:
    quote = ""
    escaped = False
    for index, char in enumerate(line):
        if escaped:
            escaped = False
            continue
        if char == "\\" and quote:
            escaped = True
            continue
        if quote:
            if char == quote:
                quote = ""
            continue
        if char in {"'", '"'}:
            quote = char
            continue
        if char == "-" and index + 1 < len(line) and line[index + 1] == "-":
            return line[:index]
    return line


def strip_lua_comments(content: str) -> str:
    return "\n".join(strip_lua_line_comment(line) for line in content.splitlines())


def strip_ini_value_comment(value: str) -> str:
    # Match the bundled inih parser: only a semicolon preceded by whitespace
    # starts an inline comment. A bare semicolon is data (for example, the
    # ReplaceMagic list delimiter), and '#' is only a full-line comment.
    for index, char in enumerate(value):
        if char == ";" and index > 0 and value[index - 1] in " \t\r\n\v\f":
            return value[:index]
    return value


def normalize_ini_name(value: str) -> str:
    return value.strip().lower()


def iter_ini_entries(path: Path) -> Iterable[tuple[str, str, str, int, str]]:
    current_section = ""
    for line_number, raw_line in enumerate(read_text_lossy(path).splitlines(), start=1):
        line = raw_line.lstrip("\ufeff")
        section_match = INI_SECTION_RE.match(line)
        if section_match:
            current_section = section_match.group(1).strip()
            continue
        key_match = INI_KEY_VALUE_RE.match(line)
        if key_match is None:
            continue
        key = key_match.group(1).strip()
        value = strip_ini_value_comment(key_match.group(2)).strip()
        yield current_section, key, value, line_number, raw_line.strip()


def iter_files_under(root: Path, suffixes: set[str]) -> Iterable[Path]:
    if not root.exists():
        return []
    return (
        path for path in root.rglob("*")
        if path.is_file() and path.suffix.lower() in suffixes
    )


def check_standard_directories(pack: PackInfo) -> list[Issue]:
    issues: list[Issue] = []
    for directory_name in STANDARD_RESOURCE_DIRS:
        directory_path = pack.root / directory_name
        if not directory_path.is_dir():
            issues.append(Issue("INFO", pack.pack_id, f"standard resource directory is absent: {directory_name}", str(pack.root)))
    return issues


def check_profile(
    pack: PackInfo,
    packs: dict[str, PackInfo],
    collection_common_root: Path | None,
) -> tuple[configparser.ConfigParser | None, list[Issue]]:
    issues: list[Issue] = []
    if not pack.manifest:
        profile = configparser.ConfigParser(interpolation=None)
        profile.read_dict({
            "Game": {
                "Id": pack.pack_id,
                "Name": pack.pack_id,
                "Type": "0",
            },
        })
        return profile, issues

    manifest_path = pack.root / pack.manifest
    if not manifest_path.exists():
        return None, [Issue(
            "WARNING",
            pack.pack_id,
            "missing game_profile.ini; this catalog entry is skipped while other packs remain checkable",
            str(manifest_path),
        )]

    profile = read_ini(manifest_path)
    for section, key in PROFILE_REQUIRED_FIELDS:
        if not get_value(profile, section, key):
            issues.append(Issue(
                "WARNING",
                pack.pack_id,
                f"missing profile field {section}.{key}; runtime fallback/default will be used when available",
                str(manifest_path),
            ))

    dependency_id = get_value(profile, "Resource", "DependencyId")
    if pack.pack_id in BASE_PACK_IDS and not get_value(profile, "Game", "Type"):
        issues.append(Issue(
            "WARNING",
            pack.pack_id,
            "base profile missing Game.Type; default Type=0 will be used",
            str(manifest_path),
        ))

    profile_file_refs: list[tuple[str, str]] = [
        ("Title.Menu", get_value(profile, "Title", "Menu")),
        ("NewGame.Script", get_value(profile, "NewGame", "Script")),
        ("Title.Music", get_value(profile, "Title", "Music")),
        ("Title.TeamVideo", get_value(profile, "Title", "TeamVideo")),
    ]
    for startup_video in split_profile_list(get_value(profile, "Startup", "Videos")):
        profile_file_refs.append(("Startup.Videos", startup_video))

    active_common_root = resolve_common_root(pack, profile, collection_common_root)

    roots = list(iter_resolution_roots(pack, packs, active_common_root, dependency_id))
    for label, relative_name in profile_file_refs:
        if not relative_name:
            continue
        candidates = profile_reference_candidates(label, relative_name)
        resolved, exact, matched_candidate, source = resolve_any_resource(candidates, roots)
        if resolved is None:
            issues.append(Issue("WARNING", pack.pack_id, f"profile {label} target not found: {relative_name}", str(manifest_path)))
        elif not exact:
            issues.append(Issue("WARNING", pack.pack_id, f"profile {label} target resolves to different disk path{describe_resolution_source(source)}: {matched_candidate} -> {resolved}", str(resolved)))

    return profile, issues


def effective_dependency_ids(pack: PackInfo, profile: configparser.ConfigParser | None) -> list[str]:
    if profile is None:
        return split_dependency_ids(pack.base_id)
    return split_dependency_ids(pack.base_id or get_value(profile, "Resource", "DependencyId"))


def effective_ui_base_id(pack: PackInfo, profile: configparser.ConfigParser | None) -> str:
    return pack.ui_base_id or (get_value(profile, "UI", "BaseId") if profile is not None else "")


def effective_ui_profile(pack: PackInfo, profile: configparser.ConfigParser | None) -> str:
    return pack.ui_profile or (get_value(profile, "UI", "Profile") if profile is not None else "")


def is_ui_resource_path(value: str) -> bool:
    normalized = ascii_lower("/".join(normalize_relative_path(value).parts))
    return any(normalized == root or normalized.startswith(root + "/") for root in UI_RESOURCE_ROOTS)


def iter_ui_resolution_roots(
    pack: PackInfo,
    packs: dict[str, PackInfo],
    profiles: dict[str, configparser.ConfigParser | None],
    active_common_root: Path | None,
) -> Iterable[tuple[Path, str]]:
    """Mirror ResourceManager/File ordering for ini/ui and asf/ui resources."""
    packs_by_lower_id = first_packs_by_id(packs)
    seen_pack_ids: set[str] = {pack.pack_id.lower()}
    seen_paths: set[Path] = {pack.root.resolve(strict=False)}
    manifest_cache: dict[Path, configparser.ConfigParser | None] = {}

    def profile_for_pack(current_pack: PackInfo) -> configparser.ConfigParser | None:
        known_profile = lookup_pack_profile(current_pack, profiles)
        if known_profile is not None:
            return known_profile
        manifest_path = (current_pack.root / current_pack.manifest).resolve(strict=False)
        if manifest_path not in manifest_cache:
            manifest_cache[manifest_path] = read_ini(manifest_path) if manifest_path.is_file() else None
        return manifest_cache[manifest_path]

    def visit_pack(current_pack: PackInfo) -> Iterable[tuple[Path, str]]:
        normalized_id = current_pack.pack_id.lower()
        if normalized_id in seen_pack_ids:
            return
        seen_pack_ids.add(normalized_id)
        normalized_root = current_pack.root.resolve(strict=False)
        seen_paths.add(normalized_root)
        yield normalized_root, "dependency"

        current_profile = profile_for_pack(current_pack)
        ui_base_id = effective_ui_base_id(current_pack, current_profile)
        dependency_ids = (
            [ui_base_id]
            if ui_base_id
            else effective_dependency_ids(current_pack, current_profile)
        )
        for dependency_id in dependency_ids:
            dependency_pack = packs_by_lower_id.get(dependency_id.lower())
            if dependency_pack is not None:
                yield from visit_pack(dependency_pack)

    profile = lookup_pack_profile(pack, profiles)
    ui_base_id = effective_ui_base_id(pack, profile)
    dependency_ids = [ui_base_id] if ui_base_id else effective_dependency_ids(pack, profile)
    fallback_roots: list[tuple[Path, str]] = []
    for dependency_id in dependency_ids:
        dependency_pack = packs_by_lower_id.get(dependency_id.lower())
        if dependency_pack is not None:
            fallback_roots.extend(visit_pack(dependency_pack))
    prefer_local = get_boolean_value(profile, "UI", "PreferLocal", True) if profile is not None else True
    active_root = pack.root.resolve(strict=False)
    if prefer_local:
        yield active_root, "self"
    yield from fallback_roots
    if not prefer_local:
        yield active_root, "self"
    if active_common_root is not None:
        normalized_common_root = active_common_root.resolve(strict=False)
        if normalized_common_root not in seen_paths:
            yield normalized_common_root, "common"


def check_ui_graph(
    packs: dict[str, PackInfo],
    profiles: dict[str, configparser.ConfigParser | None],
) -> list[Issue]:
    issues: list[Issue] = []
    canonical_ids = first_pack_keys_by_id(packs)
    pack_states: dict[str, int] = {}
    stack: list[str] = []
    reported_cycles: set[tuple[str, ...]] = set()
    pack_validity: dict[str, bool] = {}

    def report_cycle(label: str) -> None:
        if label in stack:
            cycle = stack[stack.index(label):] + [label]
        else:
            cycle = stack + [label]
        key = tuple(cycle)
        if key not in reported_cycles:
            reported_cycles.add(key)
            issues.append(Issue(
                "WARNING",
                label,
                "UI base graph contains a cycle: " + " -> ".join(cycle),
            ))

    def validate_manifest(
        owner: str,
        root: Path,
        profile: configparser.ConfigParser,
        pack: PackInfo | None = None,
    ) -> bool:
        profile_name = (
            effective_ui_profile(pack, profile)
            if pack is not None
            else get_value(profile, "UI", "Profile")
        )
        if profile_name and profile_name.upper() not in SUPPORTED_UI_PROFILES:
            issues.append(Issue("WARNING", owner, f"unsupported UI.Profile: {profile_name}"))
            return False

        ui_base_id = (
            effective_ui_base_id(pack, profile)
            if pack is not None
            else get_value(profile, "UI", "BaseId")
        )
        if ui_base_id:
            dependency_ids = [ui_base_id]
        elif pack is not None:
            dependency_ids = effective_dependency_ids(pack, profile)
        else:
            dependency_ids = split_dependency_ids(
                get_value(profile, "Resource", "DependencyId")
            )
        valid = True
        for dependency_id in dependency_ids:
            canonical_dependency_id = canonical_ids.get(dependency_id.lower())
            if canonical_dependency_id is None:
                issues.append(Issue("WARNING", owner, f"UI base pack not found: {dependency_id}"))
                valid = False
            elif not visit_pack(canonical_dependency_id):
                valid = False

        return valid

    def visit_pack(pack_id: str) -> bool:
        state = pack_states.get(pack_id, 0)
        if state == 1:
            report_cycle(pack_id)
            return False
        if state == 2:
            return pack_validity[pack_id]

        pack_states[pack_id] = 1
        stack.append(pack_id)
        pack = packs[pack_id]
        profile = lookup_pack_profile(pack, profiles) or configparser.ConfigParser(interpolation=None)
        is_valid = validate_manifest(pack.pack_id, pack.root, profile, pack)
        stack.pop()
        pack_states[pack_id] = 2
        pack_validity[pack_id] = is_valid
        return is_valid

    for pack_id in packs:
        visit_pack(pack_id)

    return issues


def check_ui_component_image_resources(
    pack: PackInfo,
    packs: dict[str, PackInfo],
    profiles: dict[str, configparser.ConfigParser | None],
    active_common_root: Path | None,
) -> list[Issue]:
    ui_ini_root = pack.root / "ini" / "ui"
    if not ui_ini_root.is_dir():
        return []

    profile = lookup_pack_profile(pack, profiles)
    dependency_id = ",".join(effective_dependency_ids(pack, profile))
    content_roots = list(iter_resolution_roots(pack, packs, active_common_root, dependency_id))
    ui_roots = list(iter_ui_resolution_roots(pack, packs, profiles, active_common_root))
    references: dict[str, list[tuple[str, Path, int]]] = defaultdict(list)

    for ini_path in iter_files_under(ui_ini_root, {".ini"}):
        for _section, key, value, line_number, _raw_line in iter_ini_entries(ini_path):
            if normalize_ini_name(key) != "image" or not value:
                continue
            normalized_target = ascii_lower("/".join(normalize_relative_path(value).parts))
            references[normalized_target].append((value, ini_path, line_number))

    issues: list[Issue] = []
    for grouped_references in references.values():
        target, source_path, line_number = grouped_references[0]
        roots = ui_roots if is_ui_resource_path(target) else content_roots
        runtime_target = ascii_lower(target)
        resolved, exact, source = resolve_resource(runtime_target, roots)
        reference_suffix = (
            f" ({len(grouped_references)} references)"
            if len(grouped_references) > 1
            else ""
        )
        issue_path = f"{source_path}:{line_number}"
        if resolved is None:
            issues.append(Issue(
                "ERROR",
                pack.pack_id,
                f"UI component Image target not found: {target}{reference_suffix}",
                issue_path,
            ))
        elif not exact and not is_ascii_case_only_resolution(runtime_target, resolved, roots):
            issues.append(Issue(
                "WARNING",
                pack.pack_id,
                "UI component Image target resolves to different disk path"
                f"{describe_resolution_source(source)}: {target} -> {resolved}{reference_suffix}",
                str(resolved),
            ))
    return issues


def check_dependency_graph(
    packs: dict[str, PackInfo],
    profiles: dict[str, configparser.ConfigParser | None],
) -> list[Issue]:
    issues: list[Issue] = []
    canonical_ids = first_pack_keys_by_id(packs)
    pack_states: dict[str, int] = {}
    pack_validity: dict[str, bool] = {}
    stack: list[str] = []
    reported_cycles: set[tuple[str, ...]] = set()

    def report_cycle(label: str) -> None:
        if label in stack:
            cycle = stack[stack.index(label):] + [label]
        else:
            cycle = stack + [label]
        key = tuple(cycle)
        if key not in reported_cycles:
            reported_cycles.add(key)
            issues.append(Issue("WARNING", label, "dependency chain contains a cycle: " + " -> ".join(cycle)))

    def validate_dependencies(
        owner: str,
        dependency_ids: list[str],
    ) -> bool:
        if not dependency_ids:
            return True

        valid = True
        for dependency_id in dependency_ids:
            canonical_dependency_id = canonical_ids.get(dependency_id.lower())
            if canonical_dependency_id is None:
                issues.append(Issue("WARNING", owner, f"dependency pack not found: {dependency_id}"))
                valid = False
            elif not validate_pack(canonical_dependency_id):
                valid = False
        return valid

    def validate_pack(pack_id: str) -> bool:
        state = pack_states.get(pack_id, 0)
        if state == 1:
            report_cycle(pack_id)
            return False
        if state == 2:
            return pack_validity[pack_id]

        pack_states[pack_id] = 1
        stack.append(pack_id)
        pack = packs[pack_id]
        profile = lookup_pack_profile(pack, profiles)
        dependency_ids = effective_dependency_ids(pack, profile)
        valid = validate_dependencies(
            pack.pack_id,
            dependency_ids,
        )
        stack.pop()
        pack_states[pack_id] = 2
        pack_validity[pack_id] = valid
        return valid

    for pack_id in packs:
        validate_pack(pack_id)

    return issues


def check_save_namespaces(
    packs: dict[str, PackInfo],
    profiles: dict[str, configparser.ConfigParser | None],
) -> list[Issue]:
    namespaces: dict[str, list[tuple[str, PackInfo, str]]] = {}
    for storage_key, pack in packs.items():
        profile = lookup_pack_profile(pack, profiles)
        namespace = pack.save_namespace
        if not namespace and profile is not None:
            namespace = get_value(profile, "Save", "Namespace")
        namespace = namespace or pack.pack_id or pack.root.name
        portable_namespace = sanitize_save_namespace(namespace).lower()
        namespaces.setdefault(portable_namespace, []).append(
            (storage_key, pack, namespace)
        )

    issues: list[Issue] = []
    used_namespaces: set[str] = set()
    for owners in namespaces.values():
        for position, (storage_key, pack, declared_namespace) in enumerate(owners):
            effective_namespace = declared_namespace
            portable_namespace = sanitize_save_namespace(effective_namespace).lower()
            if position > 0 or portable_namespace in used_namespaces:
                suffix = sanitize_save_namespace(pack.section or storage_key)
                effective_namespace = f"{declared_namespace}--{suffix}"
                portable_namespace = sanitize_save_namespace(effective_namespace).lower()
                disambiguator = 2
                while portable_namespace in used_namespaces:
                    effective_namespace = (
                        f"{declared_namespace}--{suffix}-{disambiguator}"
                    )
                    portable_namespace = sanitize_save_namespace(
                        effective_namespace
                    ).lower()
                    disambiguator += 1
            used_namespaces.add(portable_namespace)
            if len(owners) > 1:
                if position == 0:
                    message = (
                        "Save.Namespace conflict retained; first entry keeps "
                        + effective_namespace
                    )
                else:
                    message = (
                        "Save.Namespace conflict retained with effective namespace "
                        + effective_namespace
                    )
                issues.append(Issue("WARNING", pack.pack_id, message))
    return issues


def read_signal_mapping(signal_file: Path, signal_index: int) -> str:
    parser = read_ini(signal_file)
    return get_value(parser, "SignalIcon", str(signal_index))


def signal_image_candidates(signal_index: int, mapped_file: str) -> list[str]:
    values: list[str] = []
    if mapped_file:
        values.append(mapped_file)
    values.append(str(signal_index))

    candidates: list[str] = []
    for value in values:
        value_path = normalize_relative_path(value)
        if len(value_path.parts) == 1:
            value_path = Path("asf") / "signal" / value_path
        candidate = str(value_path).replace("/", "\\")
        candidates.append(candidate)
        if "." not in value_path.name:
            for suffix in (".png", ".asf", ".mpc"):
                candidates.append(candidate + suffix)

    unique: list[str] = []
    seen: set[str] = set()
    for candidate in candidates:
        key = candidate.lower()
        if key not in seen:
            unique.append(candidate)
            seen.add(key)
    return unique


def iter_scripts(pack_root: Path) -> Iterable[Path]:
    script_root = pack_root / "script"
    if not script_root.exists():
        return []
    return (
        path for path in script_root.rglob("*")
        if path.is_file() and path.suffix.lower() in SCRIPT_EXTENSIONS
    )


def check_signal_tip_resources(
    pack: PackInfo,
    packs: dict[str, PackInfo],
    active_common_root: Path | None,
    dependency_id: str,
) -> list[Issue]:
    issues: list[Issue] = []
    signal_refs: dict[int, list[Path]] = {}
    for script_path in iter_scripts(pack.root):
        content = strip_lua_comments(read_text_lossy(script_path))
        for match in SHOW_SIGNAL_TIP_RE.finditer(content):
            signal_index = int(match.group(1))
            signal_refs.setdefault(signal_index, []).append(script_path)

    if not signal_refs:
        return issues

    roots = list(iter_resolution_roots(pack, packs, active_common_root, dependency_id))
    signal_ini_path, signal_ini_exact, signal_ini_source = resolve_resource("ini\\ui\\tips\\SignalFile.ini", roots)
    if signal_ini_path is None:
        for signal_index, scripts in sorted(signal_refs.items()):
            issue_path = str(scripts[0])
            issues.append(Issue("ERROR", pack.pack_id, f"ShowSignalTip index {signal_index} has no resolvable ini/ui/tips/SignalFile.ini", issue_path))
        return issues
    if not signal_ini_exact:
        issues.append(Issue("WARNING", pack.pack_id, f"SignalFile.ini path resolves to different disk path{describe_resolution_source(signal_ini_source)}: ini/ui/tips/SignalFile.ini -> {signal_ini_path}", str(signal_ini_path)))

    for signal_index, scripts in sorted(signal_refs.items()):
        mapped_file = read_signal_mapping(signal_ini_path, signal_index)
        if not mapped_file:
            issues.append(Issue("ERROR", pack.pack_id, f"SignalFile.ini has no SignalIcon entry for index {signal_index}", str(signal_ini_path)))
            continue
        found_image = None
        exact_image = True
        image_source = ""
        for candidate in signal_image_candidates(signal_index, mapped_file):
            found_image, exact_image, image_source = resolve_resource(candidate, roots)
            if found_image is not None:
                break
        if found_image is None:
            issue_path = str(scripts[0])
            issues.append(Issue("ERROR", pack.pack_id, f"ShowSignalTip index {signal_index} maps to missing signal image {mapped_file}", issue_path))
        elif not exact_image:
            issues.append(Issue("WARNING", pack.pack_id, f"signal image path resolves to different disk path{describe_resolution_source(image_source)} for index {signal_index}: {mapped_file} -> {found_image}", str(found_image)))

    return issues


def ascii_lower(value: str) -> str:
    return "".join(chr(ord(char) + 32) if "A" <= char <= "Z" else char for char in value)


def ascii_upper(value: str) -> str:
    return "".join(chr(ord(char) - 32) if "a" <= char <= "z" else char for char in value)


def append_unique(values: list[str], value: str) -> None:
    if value and value not in values:
        values.append(value)


def media_base_name_variants(base_name: str) -> list[str]:
    variants: list[str] = []
    append_unique(variants, base_name)
    append_unique(variants, ascii_lower(base_name))
    append_unique(variants, ascii_upper(base_name))
    if base_name:
        lower = ascii_lower(base_name)
        append_unique(variants, ascii_upper(lower[0]) + lower[1:])
    return variants


def normalize_media_path(value: str) -> str:
    normalized = value.strip().replace("\\", "/")
    while normalized.startswith("/"):
        normalized = normalized[1:]
    while normalized.startswith("./"):
        normalized = normalized[2:]
    return normalized


def normalize_media_folder(folder: str) -> str:
    normalized = normalize_media_path(folder)
    if normalized and not normalized.endswith("/"):
        normalized += "/"
    return normalized


def to_runtime_media_path(value: str) -> str:
    return value.replace("/", "\\")


def has_suspicious_mojibake_characters(value: str) -> bool:
    for char in value:
        codepoint = ord(char)
        if char == "\ufffd" or 0x0100 <= codepoint <= 0x052F:
            return True
    return False


def contains_cjk(value: str) -> bool:
    return any("\u3400" <= char <= "\u9fff" for char in value)


def legacy_utf8_read_as_gb18030_variant(value: str) -> str:
    if not has_suspicious_mojibake_characters(value):
        return ""
    try:
        decoded = value.encode("utf-8").decode("gb18030")
    except UnicodeDecodeError:
        return ""
    if decoded != value and contains_cjk(decoded):
        return decoded
    return ""


def resource_name_variants(value: str) -> list[str]:
    variants = [value]
    legacy_variant = legacy_utf8_read_as_gb18030_variant(value)
    if legacy_variant:
        variants.append(legacy_variant)
    return unique_preserving_order(variants)


def has_media_folder_prefix(file_name: str, folder: str) -> bool:
    normalized_file = ascii_lower(normalize_media_path(file_name))
    normalized_folder = ascii_lower(normalize_media_folder(folder))
    return bool(normalized_folder) and normalized_file.startswith(normalized_folder)


def build_direct_media_path(folder: str, file_name: str) -> str:
    normalized_file = normalize_media_path(file_name)
    if has_media_folder_prefix(normalized_file, folder):
        return to_runtime_media_path(normalized_file)
    return to_runtime_media_path(normalize_media_folder(folder) + normalized_file)


def media_path_directory(value: str) -> str:
    normalized = normalize_media_path(value)
    index = normalized.rfind("/")
    if index < 0:
        return ""
    return normalized[: index + 1]


def media_path_stem(value: str) -> str:
    normalized = normalize_media_path(value)
    base_name = normalized.rsplit("/", 1)[-1]
    index = base_name.rfind(".")
    if index < 0:
        return base_name
    return base_name[:index]


def media_path_suffix(value: str) -> str:
    normalized = normalize_media_path(value)
    base_name = normalized.rsplit("/", 1)[-1]
    index = base_name.rfind(".")
    if index < 0:
        return ""
    return base_name[index:]


def replace_media_path_suffix(value: str, suffix: str) -> str:
    normalized = normalize_media_path(value)
    current_suffix = media_path_suffix(normalized)
    if current_suffix:
        normalized = normalized[: -len(current_suffix)]
    return normalized + suffix


def package_fallback_suffix_for_path(value: str) -> str:
    normalized = ascii_lower(normalize_media_path(value))
    if normalized.startswith("mpc/"):
        return ".mpc"
    return ".asf"


def uses_asf_or_mpc_suffix(value: str) -> bool:
    suffix = media_path_suffix(value).lower()
    return suffix in {".asf", ".mpc"}


def append_media_candidate_with_package_fallbacks(candidates: list[str], candidate: str) -> None:
    normalized = normalize_media_path(candidate)
    candidates.append(to_runtime_media_path(normalized))
    suffix = media_path_suffix(normalized).lower()
    if not suffix:
        candidates.append(to_runtime_media_path(normalized + package_fallback_suffix_for_path(normalized)))
        return
    if suffix not in {".asf", ".mpc", ".shd"}:
        candidates.append(to_runtime_media_path(replace_media_path_suffix(normalized, package_fallback_suffix_for_path(normalized))))
        return
    if uses_asf_or_mpc_suffix(normalized):
        expected_suffix = package_fallback_suffix_for_path(normalized)
        if suffix != expected_suffix:
            candidates.append(to_runtime_media_path(replace_media_path_suffix(normalized, expected_suffix)))


def media_asset_candidates(folder: str, file_name: str, fallback_extensions: list[str]) -> list[str]:
    candidates: list[str] = []
    direct_media_path = build_direct_media_path(folder, file_name)
    direct_path = normalize_media_path(direct_media_path)
    direct_suffix = media_path_suffix(direct_path).lower()
    if not direct_suffix or direct_suffix in fallback_extensions:
        candidates.append(direct_media_path)
    base_directory = media_path_directory(direct_path)
    base_name = media_path_stem(direct_path)
    for base_variant in media_base_name_variants(base_name):
        for extension in fallback_extensions:
            candidates.append(to_runtime_media_path(base_directory + base_variant + extension))
    return unique_preserving_order(candidates)


def script_music_candidates(file_name: str, use_wav: bool) -> list[str]:
    bgm_name = normalize_media_path(file_name)
    suffix = media_path_suffix(bgm_name)
    if not use_wav and (not suffix or suffix.lower() == ".wav"):
        bgm_name = media_path_directory(bgm_name) + media_path_stem(bgm_name) + ".mp3"

    return media_asset_candidates("music", bgm_name, MUSIC_FALLBACK_EXTENSIONS)


def script_movie_candidates(file_name: str) -> list[str]:
    return media_asset_candidates("video", file_name, VIDEO_FALLBACK_EXTENSIONS)


def script_sound_candidates(file_name: str) -> list[str]:
    return media_asset_candidates("sound", file_name, SOUND_FALLBACK_EXTENSIONS)


def unique_preserving_order(values: Iterable[str]) -> list[str]:
    result: list[str] = []
    seen: set[str] = set()
    for value in values:
        if value not in seen:
            result.append(value)
            seen.add(value)
    return result


def script_media_candidates(function_name: str, file_name: str, use_wav: bool) -> list[str]:
    if not file_name:
        return []
    function_name = function_name.lower()
    if function_name == "playmusic":
        return script_music_candidates(file_name, use_wav)
    if function_name == "playsound":
        return script_sound_candidates(file_name)
    if function_name == "playmovie":
        return script_movie_candidates(file_name)
    return []


def check_script_media_resources(
    pack: PackInfo,
    packs: dict[str, PackInfo],
    active_common_root: Path | None,
    dependency_id: str,
    profile: configparser.ConfigParser | None,
) -> list[Issue]:
    issue_counts: dict[tuple[str, str], int] = {}
    issue_paths: dict[tuple[str, str], str] = {}

    def add_issue(severity: str, message: str, path: Path) -> None:
        key = (severity, message)
        issue_counts[key] = issue_counts.get(key, 0) + 1
        issue_paths.setdefault(key, str(path))

    use_wav = False
    if profile is not None:
        use_wav = get_boolean_value(profile, "Game", "UseWav", False)
    roots = list(iter_resolution_roots(pack, packs, active_common_root, dependency_id))

    for script_path in iter_scripts(pack.root):
        content = strip_lua_comments(read_text_lossy(script_path))
        for match in SCRIPT_MEDIA_CALL_RE.finditer(content):
            function_name = match.group(1)
            file_name = match.group(3).strip()
            candidates = script_media_candidates(function_name, file_name, use_wav)
            if not candidates:
                continue
            resolved, exact, matched_candidate, source = resolve_any_resource(candidates, roots)
            label = f"{function_name}({file_name})"
            if resolved is None:
                add_issue("ERROR", f"script media target not found: {label}", script_path)
            elif not exact:
                add_issue("WARNING", f"script media target resolves to different disk path{describe_resolution_source(source)}: {label} -> {matched_candidate}", resolved)

    issues: list[Issue] = []
    for (severity, base_message), count in sorted(issue_counts.items()):
        message = base_message
        if count > 1:
            message = f"{message} ({count} references)"
        issues.append(Issue(severity, pack.pack_id, message, issue_paths[(severity, base_message)]))
    return issues


def object_resource_reference_candidates(obj_file_name: str) -> list[str]:
    if not obj_file_name:
        return []
    candidates: list[str] = []
    for variant in resource_name_variants(obj_file_name):
        normalized = normalize_relative_path(variant)
        normalized_text = str(normalized).replace("\\", "/").lower()
        if normalized_text.startswith("ini/"):
            candidates.append(str(normalized).replace("/", "\\"))
        else:
            candidates.append(f"ini\\objres\\{variant}")
    return unique_preserving_order(candidates)


def image_resource_candidates(category: str, image_name: str, fallback_folders: Iterable[str] = ()) -> list[str]:
    if not category or not image_name:
        return []
    candidates: list[str] = []
    for variant in resource_name_variants(image_name):
        candidates.extend(image_resource_candidates_for_name(category, variant, fallback_folders))
    return unique_preserving_order(candidates)


def image_resource_candidates_for_name(category: str, image_name: str, fallback_folders: Iterable[str] = ()) -> list[str]:
    normalized_category = normalize_media_path(category).strip("/")
    normalized = normalize_media_path(image_name)
    if not normalized:
        return []
    normalized_text = ascii_lower(normalized)
    category_text = ascii_lower(normalized_category)
    candidates: list[str] = []
    asf_category_prefix = f"asf/{category_text}/"
    mpc_category_prefix = f"mpc/{category_text}/"
    category_prefix = f"{category_text}/"
    if normalized_text.startswith(asf_category_prefix):
        suffix = normalized[len(asf_category_prefix) :]
        append_media_candidate_with_package_fallbacks(candidates, f"asf/{normalized_category}/{suffix}")
        append_media_candidate_with_package_fallbacks(candidates, f"mpc/{normalized_category}/{suffix}")
        return unique_preserving_order(candidates)
    if normalized_text.startswith(mpc_category_prefix):
        suffix = normalized[len(mpc_category_prefix) :]
        append_media_candidate_with_package_fallbacks(candidates, f"mpc/{normalized_category}/{suffix}")
        append_media_candidate_with_package_fallbacks(candidates, f"asf/{normalized_category}/{suffix}")
        return unique_preserving_order(candidates)
    if normalized_text.startswith(category_prefix):
        suffix = normalized[len(category_prefix) :]
        append_media_candidate_with_package_fallbacks(candidates, f"asf/{normalized_category}/{suffix}")
        append_media_candidate_with_package_fallbacks(candidates, f"mpc/{normalized_category}/{suffix}")
        return unique_preserving_order(candidates)
    if normalized_text.startswith("asf/"):
        append_media_candidate_with_package_fallbacks(candidates, normalized)
        if not normalized_text.startswith("asf/map/") and normalized_text != "asf/map":
            append_media_candidate_with_package_fallbacks(candidates, "mpc/" + normalized[len("asf/") :])
        return unique_preserving_order(candidates)
    if normalized_text.startswith("mpc/"):
        append_media_candidate_with_package_fallbacks(candidates, normalized)
        if not normalized_text.startswith("mpc/map/") and normalized_text != "mpc/map":
            append_media_candidate_with_package_fallbacks(candidates, "asf/" + normalized[len("mpc/") :])
        return unique_preserving_order(candidates)
    append_media_candidate_with_package_fallbacks(candidates, f"asf/{normalized_category}/{normalized}")
    append_media_candidate_with_package_fallbacks(candidates, f"mpc/{normalized_category}/{normalized}")
    for fallback_folder in fallback_folders:
        fallback = normalize_media_path(fallback_folder)
        if not fallback:
            continue
        append_media_candidate_with_package_fallbacks(candidates, f"{fallback.rstrip('/')}/{normalized}")
    return unique_preserving_order(candidates)


def object_image_candidates(image_name: str) -> list[str]:
    return image_resource_candidates("object", image_name)


def goods_image_candidates(image_name: str) -> list[str]:
    return image_resource_candidates("goods", image_name)


def magic_image_candidates(image_name: str) -> list[str]:
    return image_resource_candidates("magic", image_name)


def magic_effect_image_candidates(image_name: str) -> list[str]:
    return image_resource_candidates("effect", image_name)


def magic_action_image_candidates(image_name: str) -> list[str]:
    return image_resource_candidates("character", image_name)


SPECIAL_ATTACK_NPC_INI_INDEX_CANDIDATES = tuple(range(1, 10))


def magic_special_action_image_candidates(
    action_prefix: str,
    npc_ini_indices: Iterable[int] = SPECIAL_ATTACK_NPC_INI_INDEX_CANDIDATES,
) -> list[str]:
    normalized = normalize_media_path(action_prefix)
    if not normalized:
        return []
    if uses_asf_or_mpc_suffix(normalized):
        return magic_action_image_candidates(action_prefix)

    candidates: list[str] = []
    for npc_ini_index in npc_ini_indices:
        candidates.extend(magic_action_image_candidates(f"{normalized}{npc_ini_index}.asf"))
    return unique_preserving_order(candidates)


def magic_special_action_display_name(action_prefix: str) -> str:
    normalized = normalize_media_path(action_prefix)
    if not normalized:
        return action_prefix
    if uses_asf_or_mpc_suffix(normalized):
        return action_prefix
    first_index = SPECIAL_ATTACK_NPC_INI_INDEX_CANDIDATES[0]
    return to_runtime_media_path(f"{normalized}{first_index}.asf")


def magic_ini_candidates(file_name: str) -> list[str]:
    normalized = normalize_media_path(file_name)
    if not normalized:
        return []
    # Magic::initFromIni concatenates INI_MAGIC_FOLDER with the configured
    # value verbatim. Unlike media loading, there is no extension fallback and
    # a value already prefixed with ini/magic would be prefixed a second time.
    return [f"ini/magic/{normalized}"]


def magic_list_reference_names(value: str) -> list[str]:
    normalized = value.strip().replace("：", ":").replace("；", ";")
    if normalized == "无":
        return []
    names: list[str] = []
    for raw_item in normalized.split(";"):
        item = raw_item.strip()
        if not item:
            continue
        file_name = item.split(":", 1)[0].strip()
        if file_name:
            names.append(file_name)
    return unique_preserving_order(names)


def normalize_magic_graph_name(value: str) -> str:
    return value.strip().replace("\\", "/").lower()


def read_magic_graph_edges(
    magic_path: Path,
    *,
    load_attack_file: bool,
) -> list[tuple[str, str, int]]:
    effective_entries: dict[tuple[str, str], tuple[str, str, str, int, str]] = {}
    for entry in iter_ini_entries(magic_path):
        section, key, _value, _line_number, _raw_line = entry
        effective_entries[(normalize_ini_name(section), normalize_ini_name(key))] = entry

    edges: list[tuple[str, str, int]] = []
    seen: set[tuple[str, str]] = set()
    for section, key, value, line_number, _raw_line in effective_entries.values():
        normalized_section = normalize_ini_name(section)
        lower_key = normalize_ini_name(key)
        if not value or lower_key not in MAGIC_DIRECT_CHILD_KEYS:
            continue
        if normalized_section != "init" and not re.fullmatch(r"level(?:[1-9]|10)", normalized_section):
            continue
        if lower_key == "attackfile" and not load_attack_file:
            continue
        normalized_value = normalize_magic_graph_name(value)
        edge_key = (lower_key, normalized_value)
        if edge_key in seen:
            continue
        seen.add(edge_key)
        edges.append((lower_key, value, line_number))
    return edges


def check_magic_linked_graph(
    pack: PackInfo,
    roots: list[tuple[Path, str]],
) -> list[Issue]:
    issues: list[Issue] = []
    magic_root = pack.root / "ini" / "magic"
    edge_cache: dict[tuple[Path, bool], list[tuple[str, str, int]]] = {}
    emitted: set[tuple[str, ...]] = set()

    def edges_for(path: Path, load_attack_file: bool) -> list[tuple[str, str, int]]:
        cache_key = (path.resolve(strict=False), load_attack_file)
        if cache_key not in edge_cache:
            edge_cache[cache_key] = read_magic_graph_edges(
                path,
                load_attack_file=load_attack_file,
            )
        return edge_cache[cache_key]

    for root_path in iter_files_under(magic_root, {".ini"}):
        try:
            root_name = root_path.relative_to(magic_root).as_posix()
        except ValueError:
            root_name = root_path.name
        normalized_root_name = normalize_magic_graph_name(root_name)
        active_names = [normalized_root_name]
        active_display_names = [root_name]
        loaded_cache: set[tuple[str, str, bool]] = set()
        node_count = 1
        node_budget_reported = False

        def visit(
            current_path: Path,
            current_name: str,
            current_experience_owner: str,
            load_attack_file: bool,
            depth: int,
        ) -> None:
            nonlocal node_count, node_budget_reported
            for lower_key, child_name, line_number in edges_for(current_path, load_attack_file):
                normalized_child_name = normalize_magic_graph_name(child_name)
                relationship = MAGIC_DIRECT_CHILD_KEYS[lower_key]
                source_path = f"{current_path}:{line_number}"
                if normalized_child_name in active_names:
                    cycle_start = active_names.index(normalized_child_name)
                    cycle_names = active_display_names[cycle_start:] + [child_name]
                    cycle_signature = tuple(sorted(set(active_names[cycle_start:])))
                    issue_key = ("cycle",) + cycle_signature
                    if issue_key not in emitted:
                        emitted.add(issue_key)
                        issues.append(Issue(
                            "WARNING",
                            pack.pack_id,
                            "linked magic load cycle would be truncated: " + " -> ".join(cycle_names),
                            source_path,
                        ))
                    continue

                child_loads_attack = lower_key != "attackfile"
                experience_owner = (
                    current_experience_owner
                    if lower_key in MAGIC_EXPERIENCE_OWNER_CHILD_KEYS
                    else child_name
                )
                cache_key = (normalized_child_name, normalize_magic_graph_name(experience_owner), child_loads_attack)
                if cache_key in loaded_cache:
                    continue
                if depth >= MAGIC_LINKED_LOAD_MAX_DEPTH:
                    issue_key = ("depth", normalized_root_name, normalized_child_name)
                    if issue_key not in emitted:
                        emitted.add(issue_key)
                        issues.append(Issue(
                            "WARNING",
                            pack.pack_id,
                            f"linked magic load depth limit {MAGIC_LINKED_LOAD_MAX_DEPTH} would truncate "
                            f"{current_name} -> {child_name} ({relationship})",
                            source_path,
                        ))
                    continue
                if node_count >= MAGIC_LINKED_LOAD_MAX_NODES:
                    if not node_budget_reported:
                        node_budget_reported = True
                        issue_key = ("nodes", normalized_root_name)
                        if issue_key not in emitted:
                            emitted.add(issue_key)
                            issues.append(Issue(
                                "WARNING",
                                pack.pack_id,
                                f"linked magic load node limit {MAGIC_LINKED_LOAD_MAX_NODES} would truncate "
                                f"remaining branches at {current_name} -> {child_name} ({relationship})",
                                source_path,
                            ))
                    continue

                node_count += 1
                resolved_path, _exact, _candidate, _source = resolve_any_resource(
                    magic_ini_candidates(child_name),
                    roots,
                )
                loaded_cache.add(cache_key)
                if resolved_path is None:
                    continue
                active_names.append(normalized_child_name)
                active_display_names.append(child_name)
                visit(
                    resolved_path,
                    child_name,
                    experience_owner,
                    child_loads_attack,
                    depth + 1,
                )
                active_names.pop()
                active_display_names.pop()

        visit(root_path, root_name, root_name, True, 1)
    return issues


def object_sound_candidates(sound_name: str) -> list[str]:
    if not sound_name:
        return []
    # 与 C++ Object::initSound 一致：走 resolveSoundAssetPath，即
    # resolveMediaAssetPath(SOUND_FOLDER, name, {".wav"})，包含大小写变体与 .wav 扩展名回退。
    return media_asset_candidates("sound", sound_name, SOUND_FALLBACK_EXTENSIONS)


def check_object_resource_references(
    pack: PackInfo,
    packs: dict[str, PackInfo],
    active_common_root: Path | None,
    dependency_id: str,
) -> list[Issue]:
    issues: list[Issue] = []
    roots = list(iter_resolution_roots(pack, packs, active_common_root, dependency_id))

    object_roots = [pack.root / "ini" / "obj"]
    map_root = pack.root / "map"
    if map_root.exists():
        object_roots.extend(path.parent for path in map_root.rglob("*.obj") if path.is_file())

    for object_root in object_roots:
        for object_path in iter_files_under(object_root, {".ini", ".obj"}):
            for _section, key, value, line_number, _raw_line in iter_ini_entries(object_path):
                if normalize_ini_name(key) != "objfile" or not value:
                    continue
                resolved, exact, matched_candidate, source = resolve_any_resource(
                    object_resource_reference_candidates(value),
                    roots,
                )
                if resolved is None:
                    issues.append(Issue("ERROR", pack.pack_id, f"object ObjFile target not found: {value}", f"{object_path}:{line_number}"))
                elif not exact:
                    issues.append(Issue("WARNING", pack.pack_id, f"object ObjFile target resolves to different disk path{describe_resolution_source(source)}: {matched_candidate} -> {resolved}", str(resolved)))

    objres_root = pack.root / "ini" / "objres"
    for objres_path in iter_files_under(objres_root, {".ini"}):
        has_common_section = False
        saw_entity_key_outside_common = False
        saw_common_key_outside_common = False
        malformed_common_header_reported = False
        for line_number, raw_line in enumerate(read_text_lossy(objres_path).splitlines(), start=1):
            line = raw_line.lstrip("\ufeff")
            if "[common]" in line.lower() and INI_SECTION_RE.match(line) is None and not malformed_common_header_reported:
                malformed_common_header_reported = True
                issues.append(Issue("ERROR", pack.pack_id, "object resource has malformed [Common] section header", f"{objres_path}:{line_number}"))

        for section, key, value, line_number, _raw_line in iter_ini_entries(objres_path):
            lower_section = normalize_ini_name(section)
            lower_key = normalize_ini_name(key)
            if lower_section == "common":
                has_common_section = True
                candidates = object_image_candidates(value) if lower_key in {"image", "shade"} else object_sound_candidates(value)
                if candidates:
                    resolved, exact, matched_candidate, source = resolve_any_resource(candidates, roots)
                    if resolved is None:
                        issues.append(Issue("ERROR", pack.pack_id, f"object resource {key} target not found: {value}", f"{objres_path}:{line_number}"))
                    elif not exact:
                        issues.append(Issue("WARNING", pack.pack_id, f"object resource {key} target resolves to different disk path{describe_resolution_source(source)}: {matched_candidate} -> {resolved}", str(resolved)))
                continue
            if lower_key in OBJECT_ENTITY_SIGNATURE_KEYS and not saw_entity_key_outside_common:
                saw_entity_key_outside_common = True
                issues.append(Issue("WARNING", pack.pack_id, "ini/objres file appears to contain object instance fields; migrate/check as ini/obj", f"{objres_path}:{line_number}"))
            if lower_key in OBJECT_RES_COMMON_KEYS and not saw_common_key_outside_common:
                saw_common_key_outside_common = True
                issues.append(Issue("ERROR", pack.pack_id, "object resource common keys are outside [Common] section", f"{objres_path}:{line_number}"))

        if not has_common_section and not saw_entity_key_outside_common:
            issues.append(Issue("ERROR", pack.pack_id, "object resource has no [Common] section", str(objres_path)))

    return issues


def append_resolved_resource_issue(
    issues: list[Issue],
    pack_id: str,
    label: str,
    key: str,
    value: str,
    location: str,
    candidates: list[str],
    roots: list[tuple[Path, str]],
) -> None:
    if not candidates:
        return
    resolved, exact, matched_candidate, source = resolve_any_resource(candidates, roots)
    if resolved is None:
        issues.append(Issue("ERROR", pack_id, f"{label} {key} target not found: {value}", location))
    elif not exact:
        issues.append(Issue("WARNING", pack_id, f"{label} {key} target resolves to different disk path{describe_resolution_source(source)}: {matched_candidate} -> {resolved}", str(resolved)))


def check_goods_resource_references(
    pack: PackInfo,
    packs: dict[str, PackInfo],
    active_common_root: Path | None,
    dependency_id: str,
) -> list[Issue]:
    issues: list[Issue] = []
    roots = list(iter_resolution_roots(pack, packs, active_common_root, dependency_id))
    goods_root = pack.root / "ini" / "goods"
    image_keys = {"image", "icon"}
    for goods_path in iter_files_under(goods_root, {".ini"}):
        for section, key, value, line_number, _raw_line in iter_ini_entries(goods_path):
            if normalize_ini_name(section) != "init" or not value:
                continue
            lower_key = normalize_ini_name(key)
            if lower_key in image_keys:
                append_resolved_resource_issue(
                    issues,
                    pack.pack_id,
                    "goods resource",
                    key,
                    value,
                    f"{goods_path}:{line_number}",
                    goods_image_candidates(value),
                    roots,
                )
            elif lower_key == "magicname":
                append_resolved_resource_issue(
                    issues,
                    pack.pack_id,
                    "goods linked magic",
                    key,
                    value,
                    f"{goods_path}:{line_number}",
                    magic_ini_candidates(value),
                    roots,
                )
    return issues


def check_magic_resource_references(
    pack: PackInfo,
    packs: dict[str, PackInfo],
    active_common_root: Path | None,
    dependency_id: str,
) -> list[Issue]:
    issues: list[Issue] = []
    roots = list(iter_resolution_roots(pack, packs, active_common_root, dependency_id))
    magic_root = pack.root / "ini" / "magic"
    magic_image_keys = {"image", "icon"}
    effect_image_keys = {
        "flyingimage",
        "vanishimage",
        "leapimage",
        "supermodeimage",
        "hitcountflyingimage",
        "hitcountvanishimage",
    }
    direct_action_image_keys = {"actionshadowfile", "useactionfile"}
    direct_child_magic_keys = set(MAGIC_DIRECT_CHILD_KEYS)
    indirect_single_magic_keys = {
        "flyini",
        "flyini2",
        "magictousewhenbeattacked",
        "specialkind9replaceflyini",
        "specialkind9replaceflyini2",
    }

    for magic_path in iter_files_under(magic_root, {".ini"}):
        effective_entries: dict[tuple[str, str], tuple[str, str, str, int, str]] = {}
        for entry in iter_ini_entries(magic_path):
            section, key, _value, _line_number, _raw_line = entry
            # INIReader stores values in a section/key map, so a later
            # duplicate replaces the earlier value (case-insensitively).
            effective_entries[(normalize_ini_name(section), normalize_ini_name(key))] = entry
        entries = list(effective_entries.values())
        init_values = {
            normalize_ini_name(key): value
            for section, key, value, _line_number, _raw_line in entries
            if normalize_ini_name(section) == "init"
        }
        attack_file = init_values.get("attackfile", "")
        attack_file_resolved = False
        if attack_file:
            attack_file_path, _exact, _matched_candidate, _source = resolve_any_resource(
                magic_ini_candidates(attack_file),
                roots,
            )
            attack_file_resolved = attack_file_path is not None

        for section, key, value, line_number, _raw_line in entries:
            normalized_section = normalize_ini_name(section)
            if not value:
                continue
            lower_key = normalize_ini_name(key)
            if lower_key in direct_child_magic_keys and (
                normalized_section == "init"
                or re.fullmatch(r"level(?:[1-9]|10)", normalized_section)
            ):
                label = "linked magic"
                candidates = magic_ini_candidates(value)
            elif normalized_section == "init" and lower_key in indirect_single_magic_keys:
                label = "linked magic"
                candidates = magic_ini_candidates(value)
            elif normalized_section == "init" and lower_key == "replacemagic":
                for reference_name in magic_list_reference_names(value):
                    append_resolved_resource_issue(
                        issues,
                        pack.pack_id,
                        "linked magic",
                        key,
                        reference_name,
                        f"{magic_path}:{line_number}",
                        magic_ini_candidates(reference_name),
                        roots,
                    )
                continue
            elif normalized_section != "init":
                continue
            elif lower_key in magic_image_keys:
                label = "magic resource"
                candidates = magic_image_candidates(value)
            elif lower_key in effect_image_keys:
                label = "magic effect resource"
                candidates = magic_effect_image_candidates(value)
            elif lower_key in direct_action_image_keys:
                label = "magic action resource"
                candidates = magic_action_image_candidates(value)
            elif lower_key == "actionfile":
                label = "magic action resource"
                if attack_file_resolved:
                    candidates = magic_special_action_image_candidates(value)
                    value = magic_special_action_display_name(value)
                else:
                    candidates = magic_action_image_candidates(value)
            else:
                continue
            append_resolved_resource_issue(
                issues,
                pack.pack_id,
                label,
                key,
                value,
                f"{magic_path}:{line_number}",
                candidates,
                roots,
            )
    issues.extend(check_magic_linked_graph(pack, roots))
    return issues


def run_checks(assets_root: Path) -> list[Issue]:
    packs, collection_common_root, issues = discover_packs(assets_root)
    profiles: dict[str, configparser.ConfigParser | None] = {}
    for pack in packs.values():
        issues.extend(check_standard_directories(pack))
        profile, profile_issues = check_profile(pack, packs, collection_common_root)
        profiles[pack.section or pack.pack_id] = profile
        issues.extend(profile_issues)
    issues.extend(check_dependency_graph(packs, profiles))
    issues.extend(check_ui_graph(packs, profiles))
    issues.extend(check_save_namespaces(packs, profiles))
    for pack in packs.values():
        profile = lookup_pack_profile(pack, profiles)
        dependency_id = ",".join(effective_dependency_ids(pack, profile))
        active_common_root = resolve_common_root(pack, profile, collection_common_root)
        issues.extend(check_ui_component_image_resources(pack, packs, profiles, active_common_root))
        issues.extend(check_signal_tip_resources(pack, packs, active_common_root, dependency_id))
        issues.extend(check_script_media_resources(pack, packs, active_common_root, dependency_id, profile))
        issues.extend(check_object_resource_references(pack, packs, active_common_root, dependency_id))
        issues.extend(check_goods_resource_references(pack, packs, active_common_root, dependency_id))
        issues.extend(check_magic_resource_references(pack, packs, active_common_root, dependency_id))
    for issue in issues:
        calibrate_issue_severity(issue)
    return issues


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    parser = argparse.ArgumentParser(description="Check converted MOD resource metadata and selected script resource references.")
    parser.add_argument("assets_root", nargs="?", default="assets", help="assets collection root, default: assets")
    parser.add_argument("--strict", action="store_true", help="return non-zero when ERROR issues are found")
    parser.add_argument("--summary-by-category", action="store_true", help="print issue category totals before the full issue list")
    parser.add_argument("--json", type=Path, help="write a full JSON report to this path")
    parser.add_argument("--markdown", type=Path, help="write a Markdown report to this path")
    parser.add_argument(
        "--fail-on-categories",
        default="",
        help=(
            "comma-separated issue categories that should fail the command when matching issues "
            "exist; use with --fail-on-severities to include WARNING/INFO"
        ),
    )
    parser.add_argument(
        "--fail-on-severities",
        default="ERROR",
        help="comma-separated severities used by --fail-on-categories, default: ERROR",
    )
    parser.add_argument(
        "--report-limit",
        type=int,
        default=200,
        help="maximum issues to include in Markdown reports; use -1 for all issues",
    )
    args = parser.parse_args()

    assets_root = Path(args.assets_root)
    issues = run_checks(assets_root)
    errors = [issue for issue in issues if issue.severity == "ERROR"]
    warnings = [issue for issue in issues if issue.severity == "WARNING"]
    infos = [issue for issue in issues if issue.severity == "INFO"]

    print("--- MOD Resource Static Check ---")
    print(f"Assets: {assets_root.resolve()}")
    print(f"Errors: {len(errors)}")
    print(f"Warnings: {len(warnings)}")
    print(f"Infos: {len(infos)}")
    if args.summary_by_category:
        print_category_summary(issues)
    for issue in issues:
        print(issue.format())

    if args.json is not None:
        write_json_report(args.json, assets_root, issues)
    if args.markdown is not None:
        write_markdown_report(args.markdown, assets_root, issues, args.report_limit)

    if args.strict and errors:
        return 1
    fail_categories = parse_filter_set(args.fail_on_categories, normalize_lower=True)
    fail_severities = parse_filter_set(args.fail_on_severities, normalize_upper=True)
    if matching_gate_issues(issues, fail_categories, fail_severities):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
