#!/usr/bin/env python3
"""Fail when a runtime UI/input surface is missing from the catalog contract.

Production C++ is scanned across the whole ``src`` tree. Aggregate menu
definitions are enumerated from tracked and untracked, non-ignored worktree
files, and unstaged contents are read directly from disk. A synchronized assets
directory without usable Git metadata is scanned directly. The scan is
read-only. Surface creation points, script registrations, and interactive
resource entries use explicit deny-by-default baselines so additions cannot be
silently omitted from the menu-input audit.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path
from typing import Tuple


ROOT = Path(__file__).resolve().parents[1]
CATALOG_PATH = ROOT / "src" / "Game" / "Menu" / "MenuSurfaceCatalog.h"
SCRIPT_BASELINE_PATH = ROOT / "scripts" / "menu_input_non_surface_registrations.txt"
RESOURCE_BASELINE_PATH = ROOT / "scripts" / "menu_input_resource_interactions.tsv"
CREATION_BASELINE_PATH = ROOT / "scripts" / "menu_input_surface_creation_entries.tsv"

REQUIRED_SURFACE_KEYS = {
    "startup.resource-select",
    "startup.title",
    "title.team",
    "video.playback",
    "system",
    "option",
    "save-load.title",
    "save-load.system",
    "global.yes-no",
    "dialog",
    "choice",
    "buy-sell",
    "gamble.normal",
    "gamble.dice",
    "gamble.fish",
    "partner-equipment",
    "rpg.state",
    "rpg.equipment",
    "rpg.practice",
    "rpg.goods",
    "rpg.magic",
    "rpg.memo",
    "hud.bottom",
    "hud.top",
    "hud.column",
    "partner-head",
    "map-thumbnail",
    "system-notice",
    "message",
    "timer",
    "tooltip",
    "npc-info",
    "mobile.joystick",
    "mobile.skills",
    "loading-text-overlay",
}

# Every ComponentRegistry type must be classified here. "interactive" entries
# are additionally snapshotted per worktree menu section below.
RESOURCE_COMPONENT_INPUT_POLICIES = {
    "Button": "interactive",
    "CheckBox": "interactive",
    "ChooseTextButton": "interactive",
    "ColumnImage": "passive",
    "DragButton": "interactive",
    "DragRoundButton": "interactive",
    "FadeMask": "interactive",
    "FlatScrollbar": "interactive",
    "FlatTextButton": "interactive",
    "ImageContainer": "passive",
    "Item": "interactive",
    "Joystick": "interactive",
    "Label": "passive",
    "ListBox": "interactive",
    "MemoText": "interactive",
    "RoundButton": "interactive",
    "Scrollbar": "interactive",
    "TalkLabel": "passive",
    "TextButton": "interactive",
    "TransImage": "passive",
    "VideoPlayer": "interactive",
}

# ImageContainer is display-only by default, but the map-thumbnail controller
# explicitly turns this named resource control into a pointer hit target.
RESOURCE_INTERACTIVE_CONTROL_OVERRIDES = {
    ("littlemap", "ImageContainer", "thumbnailContainer"),
    ("mapthumbnail", "ImageContainer", "thumbnailContainer"),
}

CONTROLLER_DIRECTION_KEYS = (
    "controllerup",
    "controllerdown",
    "controllerleft",
    "controllerright",
)
INI_WHITESPACE = " \t\v\f\r\n"

PRODUCTION_SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp"}
ResourceInventoryEntry = Tuple[str, str, str, str, str, str]
SurfaceCreationEntry = Tuple[str, str, str, str, str]

SURFACE_CREATION_ROLES = {
    "embedded-component-factory",
    "recursive-submenu",
    "surface-container",
    "surface-function",
    "surface-instance",
    "surface-rebuild",
    "surface-resource",
    "surface-trigger",
}

SURFACE_CREATION_POLICY_EXEMPTIONS = {
    "exempt:owner-policy-inherited",
    "exempt:submenu-inherits-owner",
    "exempt:surface-container",
}

SURFACE_CREATION_POLICY_EXEMPTION_ROLES = {
    "exempt:owner-policy-inherited": "surface-resource",
    "exempt:submenu-inherits-owner": "recursive-submenu",
    "exempt:surface-container": "surface-container",
}

SURFACE_CREATION_ROLE_MECHANISMS = {
    "embedded-component-factory": {"make-shared", "make-unique", "new"},
    "recursive-submenu": {"resource-load"},
    "surface-container": {"AddMenuChild", "AddUpMenuChild"},
    "surface-function": {"function-definition"},
    "surface-instance": {
        "AddMenuChild",
        "AddUpMenuChild",
        "make-shared",
        "make-unique",
        "new",
        "stack-instance",
    },
    "surface-rebuild": {
        "make-shared",
        "make-unique",
        "new",
        "stack-instance",
    },
    "surface-resource": {"resource-load"},
    "surface-trigger": {"function-call"},
}


def read_utf8(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def production_source_paths() -> list[Path]:
    paths: list[Path] = []
    for path in (ROOT / "src").rglob("*"):
        if not path.is_file() or path.suffix.lower() not in PRODUCTION_SOURCE_SUFFIXES:
            continue
        relative_parts = path.relative_to(ROOT / "src").parts
        if relative_parts and relative_parts[0].lower() == "tests":
            continue
        paths.append(path)
    return sorted(paths)


def array_body(text: str, name: str, errors: list[str]) -> str:
    match = re.search(
        rf"\b{name}\s*=\s*\{{\{{(?P<body>.*?)\}}\}};",
        text,
        flags=re.DOTALL,
    )
    if match is None:
        errors.append(f"{CATALOG_PATH.relative_to(ROOT)}: cannot parse {name}")
        return ""
    return match.group("body")


def parse_catalog(errors: list[str]) -> tuple[
    dict[str, str],
    dict[str, set[str]],
    dict[str, set[str]],
    dict[str, set[str]],
    dict[str, str],
]:
    text = read_utf8(CATALOG_PATH)

    policy_body = array_body(text, "kSurfacePolicies", errors)
    policy_pairs = re.findall(
        r"\{\s*SurfaceId::([A-Za-z_][A-Za-z0-9_]*)\s*,\s*\"([^\"]+)\"",
        policy_body,
    )
    policy_rows = re.findall(
        r"\{\s*SurfaceId::([A-Za-z_][A-Za-z0-9_]*)\s*,\s*\"([^\"]+)\"\s*,"
        r"\s*SurfaceScope::([A-Za-z_][A-Za-z0-9_]*)\s*,"
        r"\s*ModalKind::([A-Za-z_][A-Za-z0-9_]*)\s*,"
        r"\s*WorldPointerPolicy::([A-Za-z_][A-Za-z0-9_]*)\s*,"
        r"\s*WorldSemanticPolicy::([A-Za-z_][A-Za-z0-9_]*)\s*,"
        r"\s*FocusPolicy::([A-Za-z_][A-Za-z0-9_]*)\s*,"
        r"\s*DefaultFocusPolicy::([A-Za-z_][A-Za-z0-9_]*)\s*,"
        r"\s*FocusRestorePolicy::([A-Za-z_][A-Za-z0-9_]*)\s*,"
        r"\s*ControllerInteractionKind::([A-Za-z_][A-Za-z0-9_]*)\s*,"
        r"\s*((?:\"[^\"]*\"\s*)+)\}",
        policy_body,
        flags=re.DOTALL,
    )
    surface_by_id = dict(policy_pairs)
    surface_keys = set(surface_by_id.values())
    if len(policy_pairs) != len(surface_by_id):
        errors.append("Menu surface catalog contains duplicate SurfaceId entries")
    if len(policy_pairs) != len(surface_keys):
        errors.append("Menu surface catalog contains duplicate policy keys")
    if len(policy_rows) != len(policy_pairs):
        errors.append(
            "Every menu surface must explicitly declare controller, scope, modal, pointer, "
            "semantic, focus, default, restore, and discovery-source policies"
        )
    for (
        surface_id,
        key,
        _scope,
        modal,
        pointer,
        semantic,
        focus,
        default_focus,
        restore,
        _controller,
        dynamic_source_literals,
    ) in policy_rows:
        dynamic_source = "".join(re.findall(r'"([^"]*)"', dynamic_source_literals))
        if not key or not dynamic_source:
            errors.append(
                f"SurfaceId::{surface_id} has an empty key or discovery source"
            )
        if focus == "None" and default_focus != "None":
            errors.append(
                f"{key}: a no-focus surface cannot declare a default focus"
            )
        if modal == "PassiveOverlay" and (
            pointer != "PassThrough"
            or semantic != "Allow"
            or focus != "None"
            or restore != "None"
        ):
            errors.append(
                f"{key}: a passive overlay must pass pointer/semantic input "
                "and own no focus"
            )
        if modal == "NonModal" and pointer == "BlockAll":
            errors.append(
                f"{key}: a non-modal surface cannot block all world pointer input"
            )
        if modal in {"Modal", "RootScene"} and pointer != "BlockAll":
            errors.append(
                f"{key}: an explicit modal/root surface must block world pointer input"
            )

    enum_match = re.search(
        r"enum class SurfaceId\s*\{(?P<body>.*?)\};", text, flags=re.DOTALL
    )
    if enum_match is None:
        errors.append("Menu surface catalog has no parseable SurfaceId enum")
        enum_ids: set[str] = set()
    else:
        enum_ids = {
            token
            for token in re.findall(
                r"\b([A-Za-z_][A-Za-z0-9_]*)\b", enum_match.group("body")
            )
            if token != "Count"
        }
    if enum_ids != set(surface_by_id):
        missing = sorted(enum_ids - set(surface_by_id))
        unknown = sorted(set(surface_by_id) - enum_ids)
        if missing:
            errors.append(f"SurfaceId values without policies: {', '.join(missing)}")
        if unknown:
            errors.append(f"Policies with unknown SurfaceId values: {', '.join(unknown)}")

    missing_required = sorted(REQUIRED_SURFACE_KEYS - surface_keys)
    if missing_required:
        errors.append(
            "Required menu surfaces are not cataloged: " + ", ".join(missing_required)
        )

    type_body = array_body(text, "kTypeBindings", errors)
    type_bindings: dict[str, set[str]] = {}
    for type_name, surface_id in re.findall(
        r"\{\s*\"([^\"]+)\"\s*,\s*SurfaceId::([A-Za-z_][A-Za-z0-9_]*)\s*\}",
        type_body,
    ):
        type_bindings.setdefault(type_name, set()).add(surface_id)
        if surface_id not in surface_by_id:
            errors.append(
                f"Type binding {type_name} refers to unknown SurfaceId::{surface_id}"
            )
    bound_surface_ids = {
        surface_id
        for surface_ids in type_bindings.values()
        for surface_id in surface_ids
    }
    missing_type_sources = sorted(set(surface_by_id) - bound_surface_ids)
    if missing_type_sources:
        errors.append(
            "Surface policies without a C++ type/function source: "
            + ", ".join(missing_type_sources)
        )

    resource_body = array_body(text, "kResourceBindings", errors)
    resource_bindings: dict[str, set[str]] = {}
    for directory, surface_id in re.findall(
        r"\{\s*\"([^\"]+)\"\s*,\s*SurfaceId::([A-Za-z_][A-Za-z0-9_]*)\s*\}",
        resource_body,
    ):
        resource_bindings.setdefault(directory.lower(), set()).add(surface_id)
        if surface_id not in surface_by_id:
            errors.append(
                f"Resource binding {directory} refers to unknown SurfaceId::{surface_id}"
            )

    script_body = array_body(text, "kScriptBindings", errors)
    script_rows = re.findall(
        r"\{\s*\"([^\"]+)\"\s*,\s*\"([^\"]+)\"\s*\}", script_body
    )
    script_bindings = dict(script_rows)
    if len(script_rows) != len(script_bindings):
        errors.append("Menu surface catalog contains duplicate script registrations")
    for registration, target in script_bindings.items():
        if not target.startswith("exempt:") and target not in surface_keys:
            errors.append(
                f"Script binding {registration} refers to unknown policy key {target}"
            )

    exemption_body = array_body(text, "kExemptions", errors)
    exemptions_by_category: dict[str, set[str]] = {}
    exemption_rows = re.findall(
        r"\{\s*\"([^\"]+)\"\s*,\s*\"([^\"]+)\"\s*,\s*\"([^\"]+)\"\s*\}",
        exemption_body,
    )
    for category, name, _reason in exemption_rows:
        exemptions_by_category.setdefault(category, set()).add(name)
    if sum(len(names) for names in exemptions_by_category.values()) != len(
        exemption_rows
    ):
        errors.append("Menu surface catalog contains duplicate exemptions")

    required_exemptions = {
        "MenuController",
        "Panel",
        "ConfigDrivenPanel",
        "ResourcePackList",
        "ResourcePackCard",
        "MinimapToggleButton",
        "ChooseMultipleSelection",
        "DiceGambleState",
        "FishGameState",
        "Nurturance",
        "MainScene",
        "GameManager",
        "GameController",
        "Camera",
        "Weather",
        "Map",
        "NPCManager",
        "ObjectManager",
        "EffectManager",
        "Player",
    }
    missing_exemptions = sorted(
        required_exemptions - exemptions_by_category.get("type", set())
    )
    if missing_exemptions:
        errors.append(
            "Required non-surface type exemptions are missing: "
            + ", ".join(missing_exemptions)
        )
    required_dynamic_exemption = (
        "ConfigDrivenPanel::loadMenuDefinition(menuFile)"
    )
    if required_dynamic_exemption not in exemptions_by_category.get("dynamic", set()):
        errors.append(
            "Required dynamic submenu exemption is missing: "
            + required_dynamic_exemption
        )

    return (
        surface_by_id,
        exemptions_by_category,
        type_bindings,
        resource_bindings,
        script_bindings,
    )


def discover_element_types() -> dict[str, list[str]]:
    class_pattern = re.compile(
        r"\b(?:class|struct)\s+([A-Za-z_][A-Za-z0-9_]*)\s*"
        r"(?:final\s*)?:\s*([^{;]+)\{",
        flags=re.DOTALL,
    )
    direct_bases: dict[str, set[str]] = {}
    declaration_sources: dict[str, set[str]] = {}
    source_paths = production_source_paths()
    for path in source_paths:
        relative = path.relative_to(ROOT).as_posix()
        for match in class_pattern.finditer(read_utf8(path)):
            type_name = match.group(1)
            bases: set[str] = set()
            for raw_base in match.group(2).split(","):
                cleaned = re.sub(
                    r"\b(?:public|protected|private|virtual)\b", "", raw_base
                ).strip()
                base_match = re.match(
                    r"([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)",
                    cleaned,
                )
                if base_match is not None:
                    bases.add(base_match.group(1).rsplit("::", 1)[-1])
            if bases:
                direct_bases.setdefault(type_name, set()).update(bases)
                declaration_sources.setdefault(type_name, set()).add(relative)

    element_types = {"Element"}
    changed = True
    while changed:
        changed = False
        for type_name, bases in direct_bases.items():
            if type_name not in element_types and bases & element_types:
                element_types.add(type_name)
                changed = True

    discovered: dict[str, list[str]] = {}
    for type_name in element_types - {"Element"}:
        discovered[type_name] = sorted(declaration_sources.get(type_name, set()))

    creation_pattern = re.compile(
        r"std::make_(?:shared|unique)<\s*"
        r"([A-Za-z_][A-Za-z0-9_:]*)"
        r"|(?:^|[^\w])new\s+([A-Za-z_][A-Za-z0-9_:]*)"
        r"|Add(?:Up)?MenuChild\(\s*([A-Za-z_][A-Za-z0-9_:]*)",
        flags=re.MULTILINE,
    )
    for path in source_paths:
        relative = path.relative_to(ROOT).as_posix()
        for match in creation_pattern.finditer(read_utf8(path)):
            qualified_name = next(group for group in match.groups() if group)
            type_name = qualified_name.rsplit("::", 1)[-1]
            if type_name in element_types:
                discovered.setdefault(type_name, []).append(relative)
    return discovered


def check_ui_types(
    type_bindings: dict[str, set[str]],
    type_exemptions: set[str],
    errors: list[str],
) -> int:
    discovered = discover_element_types()
    for type_name, sources in sorted(discovered.items()):
        if type_name not in type_bindings and type_name not in type_exemptions:
            errors.append(
                f"Unclassified Element-derived UI/world type {type_name}: "
                + ", ".join(sorted(set(sources)))
            )

    source_implementations = "\n".join(
        read_utf8(path)
        for path in production_source_paths()
        if path.suffix.lower() in {".c", ".cc", ".cpp"}
    )
    for qualified_name in sorted(name for name in type_bindings if "::" in name):
        if (
            re.search(
                rf"\b{re.escape(qualified_name)}\s*\(", source_implementations
            )
            is None
        ):
            errors.append(
                f"Cataloged C++ function source disappeared: {qualified_name}"
            )
    return len(discovered)


def resource_directory(path_text: str) -> str | None:
    normalized = re.sub(r"/+", "/", path_text.replace("\\", "/").lower())
    marker = "ini/ui/"
    position = normalized.find(marker)
    if position < 0:
        return None
    relative = normalized[position + len(marker) :].lstrip("/")
    if relative.startswith("mobile/"):
        parts = relative.split("/")
        return "/".join(parts[:2]) if len(parts) >= 2 else relative
    return relative.split("/", 1)[0]


def ascii_lower(value: str) -> str:
    return "".join(
        chr(ord(character) + 32)
        if "A" <= character <= "Z"
        else character
        for character in value
    )


def git_worktree_files(
    repository: Path,
    patterns: tuple[str, ...],
    repository_key: str,
    errors: list[str],
) -> list[str]:
    result = subprocess.run(
        [
            "git",
            "-c",
            f"safe.directory={repository.resolve()}",
            "-C",
            str(repository),
            "ls-files",
            "-z",
            "--cached",
            "--others",
            "--exclude-standard",
            "--",
            *patterns,
        ],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        errors.append(
            f"Cannot enumerate repository files in {repository_key}: {detail}"
        )
        return []
    try:
        output = result.stdout.decode("utf-8")
    except UnicodeDecodeError as exc:
        errors.append(
            f"Repository path list in {repository_key} is not UTF-8: {exc}"
        )
        return []
    return [
        path
        for path in output.split("\0")
        if path and (repository / Path(path.replace("\\", "/"))).is_file()
    ]


def read_worktree_text(
    repository: Path, repository_key: str, tracked_path: str, errors: list[str]
) -> str | None:
    worktree_path = repository / Path(tracked_path.replace("\\", "/"))
    if not worktree_path.is_file():
        errors.append(
            f"Cannot read worktree menu {repository_key}:{tracked_path}: "
            "file does not exist"
        )
        return None
    try:
        content = worktree_path.read_bytes()
    except OSError as exc:
        errors.append(
            f"Cannot read worktree menu {repository_key}:{tracked_path}: {exc}"
        )
        return None
    try:
        return content.decode("utf-8-sig")
    except UnicodeDecodeError:
        try:
            return content.decode("gb18030")
        except UnicodeDecodeError as exc:
            errors.append(
                f"Worktree menu is neither UTF-8 nor GB18030 "
                f"({repository_key}:{tracked_path}): {exc}"
            )
            return None


def resource_definition_files(
    repository: Path, repository_key: str, errors: list[str]
) -> list[str]:
    patterns = ("*.menu.ini",) + tuple(
        f"*/ini/ui/{directory}/*.ini"
        for directory in ("littlegame", "dicegame", "fishgame")
    )
    git_errors: list[str] = []
    files = git_worktree_files(
        repository, patterns, repository_key, git_errors
    )
    if not git_errors:
        return sorted(set(files))
    if repository_key != "assets":
        errors.extend(git_errors)
        return []

    discovered: set[str] = set()
    for path in repository.rglob("*.menu.ini"):
        if path.is_file():
            discovered.add(path.relative_to(repository).as_posix())
    for directory in ("littlegame", "dicegame", "fishgame"):
        for path in repository.glob(f"*/ini/ui/{directory}/*.ini"):
            if path.is_file():
                discovered.add(path.relative_to(repository).as_posix())
    return sorted(discovered)


def find_ini_delimiter(value: str, delimiters: str) -> int | None:
    was_space = False
    for index, character in enumerate(value):
        if character in delimiters:
            return index
        if character == ";" and was_space:
            return None
        was_space = character in INI_WHITESPACE
    return None


def strip_ini_inline_comment(value: str) -> str:
    was_space = False
    for index, character in enumerate(value):
        if character == ";" and was_space:
            return value[:index]
        was_space = character in INI_WHITESPACE
    return value


def parse_ini_sections(text: str) -> list[tuple[str, dict[str, str]]]:
    sections: list[tuple[str, dict[str, str]]] = []
    section_name: str | None = None
    values: dict[str, str] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip(INI_WHITESPACE)
        if not line or line.startswith((";", "#")):
            continue
        if line.startswith("["):
            section_end = find_ini_delimiter(line[1:], "]")
            if section_end is None:
                continue
            if section_name is not None:
                sections.append((section_name, values))
            section_name = line[1 : section_end + 1]
            values = {}
            continue
        if section_name is None:
            continue
        separator = find_ini_delimiter(line, "=:")
        if separator is None:
            continue
        key = line[:separator].rstrip(INI_WHITESPACE).lower()
        value = strip_ini_inline_comment(line[separator + 1 :])
        values[key] = value.strip(INI_WHITESPACE)
    if section_name is not None:
        sections.append((section_name, values))
    return sections


def inventory_field(value: str, empty_value: str) -> str:
    normalized = value.strip()
    if not normalized:
        return empty_value
    return normalized.encode("unicode_escape").decode("ascii")


def resolve_resource_reference(
    repository: Path,
    definition_path: str,
    reference: str,
) -> Path | None:
    normalized_reference = reference.strip().replace("\\", "/")
    if (
        not normalized_reference
        or "\0" in normalized_reference
        or normalized_reference.startswith("//")
        or ":" in normalized_reference
    ):
        return None
    if normalized_reference.startswith("/"):
        normalized_reference = normalized_reference[1:]
    reference_parts: list[str] = []
    for part in normalized_reference.split("/"):
        if not part or part == ".":
            continue
        base_name = part.split(".", 1)[0].lower()
        if (
            part == ".."
            or part.endswith((".", " "))
            or any(character in part for character in '<>"|?*')
            or any(ord(character) < 0x20 for character in part)
            or base_name in {
                "con",
                "prn",
                "aux",
                "nul",
                "conin$",
                "conout$",
            }
            or (
                len(base_name) == 4
                and base_name[3] in "123456789"
                and base_name[:3] in {"com", "lpt"}
            )
        ):
            return None
        reference_parts.append(part)
    if not reference_parts:
        return None
    normalized_reference = "/".join(reference_parts)

    normalized_definition = definition_path.replace("\\", "/")
    lowered_definition = normalized_definition.lower()
    marker = "/ini/ui/"
    marker_position = lowered_definition.find(marker)
    if marker_position >= 0:
        profile_prefix = normalized_definition[:marker_position]
        resource_root = repository / profile_prefix
    elif lowered_definition.startswith("ini/ui/"):
        resource_root = repository
    else:
        return None

    candidate = resource_root / normalized_reference
    try:
        candidate.resolve().relative_to(resource_root.resolve())
    except (OSError, ValueError):
        return None
    if candidate.is_file():
        return candidate

    # Runtime resource paths are ASCII case-insensitive. Resolve historical
    # mixed-case INI references deterministically on case-sensitive hosts.
    candidate = resource_root
    for part in reference_parts:
        if not candidate.is_dir():
            return None
        try:
            matches = [
                entry
                for entry in candidate.iterdir()
                if ascii_lower(entry.name) == ascii_lower(part)
            ]
        except OSError:
            return None
        if len(matches) != 1:
            return None
        candidate = matches[0]
    try:
        candidate.resolve().relative_to(resource_root.resolve())
    except (OSError, ValueError):
        return None
    return candidate if candidate.is_file() else None


def parse_base_zero_integer(value: str) -> int | None:
    normalized = value.strip()
    match = re.fullmatch(
        r"(?P<sign>[+-]?)(?P<number>"
        r"0[xX][0-9a-fA-F]+|0[0-7]*|[1-9][0-9]*)",
        normalized,
    )
    if match is None:
        return None
    number = match.group("number")
    if number.lower().startswith("0x"):
        base = 16
    elif len(number) > 1 and number.startswith("0"):
        base = 8
    else:
        base = 10
    parsed = int(number, base)
    return -parsed if match.group("sign") == "-" else parsed


def component_has_interactive_geometry(component_text: str) -> bool:
    sections = {
        name.lower(): values
        for name, values in parse_ini_sections(component_text)
    }
    init = sections.get("init", {})
    width = parse_base_zero_integer(init.get("width", "0"))
    height = parse_base_zero_integer(init.get("height", "0"))
    return (
        width is not None
        and height is not None
        and width > 0
        and height > 0
    )


def validate_controller_direction_targets(
    repository_key: str,
    normalized_path: str,
    components: list[tuple[str, dict[str, str]]],
    errors: list[str],
) -> None:
    sections_by_name: dict[str, list[str]] = {}
    for section_name, values in components:
        component_name = values.get("name", "").strip()
        if component_name:
            sections_by_name.setdefault(component_name, []).append(section_name)

    for section_name, values in components:
        component_name = values.get("name", "").strip()
        direction_targets = [
            (key, values.get(key, "").strip())
            for key in CONTROLLER_DIRECTION_KEYS
            if values.get(key, "").strip()
        ]
        if not direction_targets:
            continue
        source_location = (
            f"{repository_key}:{normalized_path}[{section_name}].name"
        )
        if not component_name:
            errors.append(
                f"Controller direction source name is missing: "
                f"{source_location}=<missing-name>"
            )
            continue
        source_sections = sections_by_name.get(component_name, [])
        if len(source_sections) != 1:
            matched_sections = ", ".join(
                f"[{source_section}]" for source_section in source_sections
            )
            errors.append(
                f"Controller direction source name is duplicated in the current "
                f"menu scope: {source_location}="
                f"{inventory_field(component_name, '<missing-name>')} matches "
                f"{matched_sections}"
            )
            continue

        for key, target_name in direction_targets:
            location = (
                f"{repository_key}:{normalized_path}[{section_name}].{key}"
            )
            escaped_target = inventory_field(target_name, "<missing-target>")
            if component_name and target_name == component_name:
                errors.append(
                    f"Controller direction target references its own component: "
                    f"{location}={escaped_target}"
                )
                continue
            target_sections = sections_by_name.get(target_name, [])
            if not target_sections:
                errors.append(
                    f"Controller direction target is missing from the current "
                    f"menu scope: {location}={escaped_target}"
                )
                continue
            if len(target_sections) != 1:
                matched_sections = ", ".join(
                    f"[{target_section}]"
                    for target_section in target_sections
                )
                errors.append(
                    f"Controller direction target is duplicated in the current "
                    f"menu scope: {location}={escaped_target} matches "
                    f"{matched_sections}"
                )


def validate_component_section_numbers(
    repository_key: str,
    normalized_path: str,
    parsed_sections: list[tuple[str, dict[str, str]]],
    errors: list[str],
) -> None:
    sections_by_number: dict[int, list[str]] = {}
    for section_name, _values in parsed_sections:
        match = re.fullmatch(r"component(\d+)", section_name, re.IGNORECASE)
        if match is None:
            continue
        component_number = int(match.group(1))
        sections_by_number.setdefault(component_number, []).append(section_name)
        location = f"{repository_key}:{normalized_path}[{section_name}]"
        if component_number <= 0:
            errors.append(
                f"Component section number must be positive: {location}"
            )
            continue
        canonical_name = f"component{component_number}"
        if section_name.lower() != canonical_name:
            errors.append(
                f"Component section number is not canonical: {location} "
                f"(expected [{canonical_name}])"
            )

    for component_number, section_names in sections_by_number.items():
        if len(section_names) <= 1:
            continue
        matched_sections = ", ".join(
            f"[{section_name}]" for section_name in section_names
        )
        errors.append(
            f"Component section number is duplicated: "
            f"{repository_key}:{normalized_path} component{component_number} "
            f"matches {matched_sections}"
        )


def parse_menu_inventory(
    repository_key: str,
    tracked_path: str,
    text: str,
    errors: list[str],
    repository: Path | None = None,
    _submenu_stack: tuple[str, ...] = (),
) -> set[ResourceInventoryEntry]:
    inventory: set[ResourceInventoryEntry] = set()
    normalized_path = inventory_field(
        tracked_path.replace("\\", "/"), "<missing-path>"
    )
    submenu_stack = _submenu_stack
    if repository is not None:
        current_path_key = normalized_path.lower()
        if current_path_key in submenu_stack:
            cycle = " -> ".join((*submenu_stack, current_path_key))
            errors.append(
                f"Recursive submenu cycle: {repository_key}:{cycle}"
            )
            return inventory
        submenu_stack = (*submenu_stack, current_path_key)
    parsed_sections = parse_ini_sections(text)
    validate_component_section_numbers(
        repository_key,
        normalized_path,
        parsed_sections,
        errors,
    )
    sections = {
        section_name.lower(): (section_name, values)
        for section_name, values in parsed_sections
    }

    reachable_components: list[tuple[str, dict[str, str]]] = []
    component_index = 1
    while True:
        section_key = f"component{component_index}"
        section = sections.get(section_key)
        if section is None or not section[1].get("type", "").strip():
            break
        reachable_components.append(section)
        component_index += 1

    validate_controller_direction_targets(
        repository_key,
        normalized_path,
        reachable_components,
        errors,
    )

    for section_name, values in reachable_components:
        component_type = values["type"].strip()
        input_policy = RESOURCE_COMPONENT_INPUT_POLICIES.get(component_type)
        if input_policy is None:
            errors.append(
                f"Unclassified resource component type {component_type}: "
                f"{repository_key}:{normalized_path}[{section_name}]"
            )
            continue
        component_file = values.get("file", "").strip()
        component_path = (
            resolve_resource_reference(
                repository, tracked_path, component_file
            )
            if repository is not None and component_file
            else None
        )
        component_text: str | None = None
        if repository is not None:
            if component_path is None:
                errors.append(
                    f"Component file is missing: "
                    f"{repository_key}:{normalized_path}[{section_name}] "
                    f"{inventory_field(component_file, '<missing-file>')}"
                )
            else:
                component_text = read_worktree_text(
                    repository,
                    repository_key,
                    component_path.relative_to(repository).as_posix(),
                    errors,
                )

        component_name = inventory_field(
            values.get("name", ""), "<unnamed>"
        )
        override_key = (
            resource_directory(tracked_path) or "",
            component_type,
            component_name,
        )
        interactive_policy = (
            input_policy == "interactive"
            or override_key in RESOURCE_INTERACTIVE_CONTROL_OVERRIDES
        )
        actual_interactive = interactive_policy
        if component_text is not None:
            actual_interactive = (
                interactive_policy
                and component_has_interactive_geometry(component_text)
            )
        elif repository is not None:
            actual_interactive = False

        if actual_interactive:
            inventory.add(
                (
                    repository_key,
                    normalized_path,
                    inventory_field(section_name, "<unnamed-section>"),
                    "control",
                    component_type,
                    component_name,
                )
            )

    for section_name, values in parsed_sections:
        match = re.fullmatch(r"component(\d+)", section_name, re.IGNORECASE)
        if match is None:
            continue
        if int(match.group(1)) >= component_index:
            errors.append(
                f"Component section is unreachable after the production "
                f"numbering stop: {repository_key}:{normalized_path}"
                f"[{section_name}]"
            )

    submenu_index = 1
    while True:
        section_key = f"submenu{submenu_index}"
        section = sections.get(section_key)
        if section is None or not section[1].get("file", "").strip():
            break
        section_name, values = section
        submenu_file = values["file"].strip()
        submenu_path = (
            resolve_resource_reference(
                repository, tracked_path, submenu_file
            )
            if repository is not None
            else None
        )
        if repository is not None and submenu_path is None:
            errors.append(
                f"Submenu file is missing: "
                f"{repository_key}:{normalized_path}[{section_name}] "
                f"{inventory_field(submenu_file, '<missing-file>')}"
            )
        else:
            inventory.add(
                (
                    repository_key,
                    normalized_path,
                    inventory_field(section_name, "<unnamed-section>"),
                    "submenu",
                    inventory_field(values.get("name", ""), "<unnamed>"),
                    inventory_field(submenu_file, "<missing-file>"),
                )
            )
            if repository is not None and submenu_path is not None:
                submenu_relative_path = (
                    submenu_path.relative_to(repository).as_posix()
                )
                submenu_text = read_worktree_text(
                    repository,
                    repository_key,
                    submenu_relative_path,
                    errors,
                )
                if submenu_text is not None:
                    inventory.update(
                        parse_menu_inventory(
                            repository_key,
                            submenu_relative_path,
                            submenu_text,
                            errors,
                            repository=repository,
                            _submenu_stack=submenu_stack,
                        )
                    )
        submenu_index += 1

    for section_name, values in parsed_sections:
        match = re.fullmatch(r"submenu(\d+)", section_name, re.IGNORECASE)
        if match is None:
            continue
        if int(match.group(1)) >= submenu_index:
            errors.append(
                f"Submenu section is unreachable after the production "
                f"numbering stop: {repository_key}:{normalized_path}"
                f"[{section_name}]"
            )
    return inventory


def load_resource_inventory_baseline(
    errors: list[str],
) -> set[ResourceInventoryEntry]:
    if not RESOURCE_BASELINE_PATH.is_file():
        errors.append(
            f"Missing resource interaction baseline: "
            f"{RESOURCE_BASELINE_PATH.relative_to(ROOT)}"
        )
        return set()
    inventory: set[ResourceInventoryEntry] = set()
    for line_number, raw_line in enumerate(
        read_utf8(RESOURCE_BASELINE_PATH).splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = tuple(field.strip() for field in raw_line.split("\t"))
        if len(fields) != 6 or any(not field for field in fields):
            errors.append(
                f"{RESOURCE_BASELINE_PATH.relative_to(ROOT)}:{line_number}: "
                "expected six non-empty tab-separated fields"
            )
            continue
        if fields[0] not in {"root", "assets"}:
            errors.append(
                f"{RESOURCE_BASELINE_PATH.relative_to(ROOT)}:{line_number}: "
                f"unknown repository key {fields[0]}"
            )
            continue
        if fields in inventory:
            errors.append(
                f"{RESOURCE_BASELINE_PATH.relative_to(ROOT)}:{line_number}: "
                "duplicate resource interaction entry"
            )
            continue
        inventory.add(fields)
    return inventory


def discover_registered_component_types() -> set[str]:
    registered: set[str] = set()
    pattern = re.compile(r"\bregisterType\(\s*\"([^\"]+)\"")
    for path in production_source_paths():
        if path.suffix.lower() not in {".c", ".cc", ".cpp"}:
            continue
        registered.update(pattern.findall(read_utf8(path)))
    return registered


def check_resources(
    resource_bindings: dict[str, set[str]], errors: list[str]
) -> tuple[int, set[str], int]:
    source_paths = production_source_paths()
    source_resource_pattern = re.compile(
        r"ini[\\/]+ui[\\/]+[A-Za-z0-9_.-]+"
        r"(?:[\\/]+[A-Za-z0-9_.-]+)*\.menu\.ini",
        flags=re.IGNORECASE,
    )
    for path in source_paths:
        relative = path.relative_to(ROOT).as_posix()
        for match in source_resource_pattern.finditer(read_utf8(path)):
            directory = resource_directory(match.group(0))
            if directory and directory not in resource_bindings:
                errors.append(
                    f"Unclassified ConfigDriven/resource profile directory "
                    f"{directory}: {relative}"
                )

    dynamic_calls: set[tuple[str, str]] = set()
    dynamic_call_pattern = re.compile(
        r"(?<!::)\bloadMenuDefinition\(\s*([^,\n\)]+)"
    )
    for path in source_paths:
        if path.suffix.lower() not in {".c", ".cc", ".cpp"}:
            continue
        relative = path.relative_to(ROOT).as_posix()
        for raw_argument in dynamic_call_pattern.findall(read_utf8(path)):
            argument = raw_argument.strip()
            if argument.startswith('"'):
                continue
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", argument):
                dynamic_calls.add((relative, argument))
            elif (
                relative == "src/Game/Menu/MapThumbnailMenu.cpp"
                and argument == "MenuResource::selectByMenuProfile("
            ):
                continue
            else:
                errors.append(
                    "Unclassified dynamic loadMenuDefinition expression "
                    f"{relative}:{argument}"
                )
    allowed_dynamic_calls = {
        ("src/Component/ConfigDrivenPanel.cpp", "menuDefinitionFile"),
        ("src/Component/ConfigDrivenPanel.cpp", "menuFile"),
        ("src/Game/Scene/Title.cpp", "menuName"),
    }
    for relative, argument in sorted(dynamic_calls - allowed_dynamic_calls):
        errors.append(
            "Unclassified dynamic loadMenuDefinition entry "
            f"{relative}:{argument}"
        )
    for relative, argument in sorted(allowed_dynamic_calls - dynamic_calls):
        errors.append(
            f"Cataloged dynamic loadMenuDefinition entry disappeared: "
            f"{relative}:{argument}"
        )

    registered_component_types = discover_registered_component_types()
    missing_component_policies = sorted(
        registered_component_types - set(RESOURCE_COMPONENT_INPUT_POLICIES)
    )
    if missing_component_policies:
        errors.append(
            "ComponentRegistry types without input classification: "
            + ", ".join(missing_component_policies)
        )
    stale_component_policies = sorted(
        set(RESOURCE_COMPONENT_INPUT_POLICIES) - registered_component_types
    )
    if stale_component_policies:
        errors.append(
            "Input-classified component types no longer registered: "
            + ", ".join(stale_component_policies)
        )

    repositories = [("root", ROOT)]
    assets_root = ROOT / "assets"
    if assets_root.is_dir():
        repositories.append(("assets", assets_root))

    tracked_menu_count = 0
    profiles: set[str] = set()
    discovered_inventory: set[ResourceInventoryEntry] = set()
    asset_menu_count = 0
    for repository_key, repository in repositories:
        definition_paths = resource_definition_files(
            repository, repository_key, errors
        )
        tracked_paths = [
            path for path in definition_paths
            if path.lower().endswith(".menu.ini")
        ]
        tracked_menu_count += len(tracked_paths)
        if repository_key == "assets":
            asset_menu_count = len(tracked_paths)
        for tracked_path in definition_paths:
            directory = resource_directory(tracked_path)
            if directory is None:
                errors.append(
                    f"Repository menu definition has no ini/ui scope: "
                    f"{repository_key}:{tracked_path}"
                )
            elif directory not in resource_bindings:
                errors.append(
                    f"Unclassified menu definition directory {directory}: "
                    f"{repository_key}:{tracked_path}"
                )

            normalized_path = tracked_path.replace("\\", "/")
            marker = "/ini/ui/"
            if (repository_key == "assets"
                and normalized_path.lower().endswith(".menu.ini")
                and marker in normalized_path.lower()):
                profiles.add(normalized_path.split(marker, 1)[0])

            menu_text = read_worktree_text(
                repository, repository_key, tracked_path, errors
            )
            if menu_text is not None and normalized_path.lower().endswith(
                ".menu.ini"
            ):
                discovered_inventory.update(
                    parse_menu_inventory(
                        repository_key,
                        tracked_path,
                        menu_text,
                        errors,
                        repository=repository,
                    )
                )
            elif menu_text is not None:
                discovered_inventory.add(
                    (
                        repository_key,
                        inventory_field(normalized_path, "<missing-path>"),
                        "<file>",
                        "resource",
                        inventory_field(directory or "", "<unknown-directory>"),
                        inventory_field(
                            Path(normalized_path).name, "<missing-file>"
                        ),
                    )
                )

    if asset_menu_count:
        required_profiles = {"common", "jxqy2", "xjxqy", "yycs"}
        missing_profiles = sorted(required_profiles - profiles)
        if missing_profiles:
            errors.append(
                "Tracked aggregate menu scan missed required profiles: "
                + ", ".join(missing_profiles)
            )

    baseline_inventory = load_resource_inventory_baseline(errors)
    for entry in sorted(discovered_inventory - baseline_inventory):
        errors.append(
            "Unregistered repository resource interaction: " + " | ".join(entry)
        )
    for entry in sorted(baseline_inventory - discovered_inventory):
        errors.append(
            "Stale repository resource interaction baseline: " + " | ".join(entry)
        )

    return tracked_menu_count, profiles, len(discovered_inventory)


def extract_braced_block(text: str, opening_brace: int) -> str | None:
    depth = 0
    index = opening_brace
    state = "normal"
    while index < len(text):
        character = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "normal":
            if character == "/" and following == "/":
                state = "line-comment"
                index += 2
                continue
            if character == "/" and following == "*":
                state = "block-comment"
                index += 2
                continue
            if character == '"':
                state = "string"
            elif character == "'":
                state = "character"
            elif character == "{":
                depth += 1
            elif character == "}":
                depth -= 1
                if depth == 0:
                    return text[opening_brace + 1 : index]
        elif state == "line-comment":
            if character == "\n":
                state = "normal"
        elif state == "block-comment":
            if character == "*" and following == "/":
                state = "normal"
                index += 2
                continue
        elif state in {"string", "character"}:
            if character == "\\":
                index += 2
                continue
            if (state == "string" and character == '"') or (
                state == "character" and character == "'"
            ):
                state = "normal"
        index += 1
    return None


def cpp_code_mask(text: str) -> str:
    """Keep C++ code offsets while blanking comments and quoted contents."""

    characters = list(text)
    index = 0
    state = "normal"
    while index < len(text):
        character = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "normal":
            if character == "/" and following == "/":
                characters[index] = " "
                characters[index + 1] = " "
                state = "line-comment"
                index += 2
                continue
            if character == "/" and following == "*":
                characters[index] = " "
                characters[index + 1] = " "
                state = "block-comment"
                index += 2
                continue
            if character == '"':
                characters[index] = " "
                state = "string"
            elif character == "'":
                characters[index] = " "
                state = "character"
        elif state == "line-comment":
            if character == "\n":
                state = "normal"
            else:
                characters[index] = " "
        elif state == "block-comment":
            if character == "*" and following == "/":
                characters[index] = " "
                characters[index + 1] = " "
                state = "normal"
                index += 2
                continue
            if character != "\n":
                characters[index] = " "
        elif state in {"string", "character"}:
            if character == "\\":
                characters[index] = " "
                if index + 1 < len(text):
                    if characters[index + 1] != "\n":
                        characters[index + 1] = " "
                    index += 2
                    continue
            if (
                (state == "string" and character == '"')
                or (state == "character" and character == "'")
            ):
                state = "normal"
            if character != "\n":
                characters[index] = " "
        index += 1
    return "".join(characters)


def cpp_matching_brace(masked_text: str, opening_brace: int) -> int | None:
    depth = 0
    for index in range(opening_brace, len(masked_text)):
        if masked_text[index] == "{":
            depth += 1
        elif masked_text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return None


def cpp_function_ranges(
    text: str, masked_text: str
) -> list[tuple[int, int, str]]:
    qualified_pattern = re.compile(
        r"^[ \t]*(?:[A-Za-z_][A-Za-z0-9_:<>,*&]*[ \t]+)*"
        r"((?:[A-Za-z_][A-Za-z0-9_]*::)+"
        r"(?:operator\s*[^\s\(]+|~?[A-Za-z_][A-Za-z0-9_]*))"
        r"\s*\([^;{}]*\)"
        r"(?:[ \t\r\n]*(?:const|noexcept|override|final))*"
        r"[ \t\r\n]*(?::[^;{}]*)?\{",
        flags=re.MULTILINE,
    )
    ranges: list[tuple[int, int, str]] = []
    for match in qualified_pattern.finditer(masked_text):
        opening_brace = masked_text.rfind("{", match.start(), match.end())
        closing_brace = cpp_matching_brace(masked_text, opening_brace)
        if closing_brace is not None:
            ranges.append((opening_brace + 1, closing_brace, match.group(1)))

    free_pattern = re.compile(
        r"^[ \t]*"
        r"((?:[A-Za-z_][A-Za-z0-9_:<>,*&]*[ \t]+)+)"
        r"([A-Za-z_][A-Za-z0-9_]*)"
        r"\s*\([^;{}]*\)"
        r"(?:[ \t\r\n]*(?:const|noexcept))*"
        r"[ \t\r\n]*\{",
        flags=re.MULTILINE,
    )
    control_words = {
        "catch",
        "else",
        "for",
        "if",
        "return",
        "switch",
        "while",
    }
    for match in free_pattern.finditer(masked_text):
        prefix = match.group(1).strip()
        if (
            not prefix
            or prefix.split()[0] in control_words
            or "::" in prefix.rsplit(None, 1)[-1]
        ):
            continue
        opening_brace = masked_text.rfind("{", match.start(), match.end())
        closing_brace = cpp_matching_brace(masked_text, opening_brace)
        if closing_brace is not None:
            ranges.append((opening_brace + 1, closing_brace, match.group(2)))
    return ranges


def cpp_owner_at(
    position: int, function_ranges: list[tuple[int, int, str]]
) -> str | None:
    owners = [
        (start, owner)
        for start, end, owner in function_ranges
        if start <= position < end
    ]
    if not owners:
        return None
    return max(owners, key=lambda entry: entry[0])[1]


def cpp_call_argument(text: str, opening_parenthesis: int) -> str | None:
    depth = 0
    index = opening_parenthesis
    state = "normal"
    while index < len(text):
        character = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "normal":
            if character == "/" and following == "/":
                state = "line-comment"
                index += 2
                continue
            if character == "/" and following == "*":
                state = "block-comment"
                index += 2
                continue
            if character == '"':
                state = "string"
            elif character == "'":
                state = "character"
            elif character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
                if depth == 0:
                    return text[opening_parenthesis + 1 : index]
        elif state == "line-comment":
            if character == "\n":
                state = "normal"
        elif state == "block-comment":
            if character == "*" and following == "/":
                state = "normal"
                index += 2
                continue
        elif state in {"string", "character"}:
            if character == "\\":
                index += 2
                continue
            if (
                (state == "string" and character == '"')
                or (state == "character" and character == "'")
            ):
                state = "normal"
        index += 1
    return None


def normalized_cpp_expression(expression: str) -> str:
    normalized = re.sub(r"\s+", " ", expression.strip())
    if not normalized:
        return "<none>"
    return normalized.encode("unicode_escape").decode("ascii")


def cpp_assignment_target(text: str, position: int) -> str:
    line_start = text.rfind("\n", 0, position) + 1
    prefix = text[line_start:position].strip()
    if re.search(r"\breturn\s*$", prefix):
        return "<return>"
    if "=" in prefix:
        left = prefix.rsplit("=", 1)[0]
        target_match = re.search(
            r"([A-Za-z_][A-Za-z0-9_]*"
            r"(?:(?:->|\.)[A-Za-z_][A-Za-z0-9_]*)*)\s*$",
            left,
        )
        if target_match is not None:
            return target_match.group(1)
    return "<temporary>"


def discover_surface_creation_entries(
    type_bindings: dict[str, set[str]], errors: list[str]
) -> dict[SurfaceCreationEntry, int]:
    bound_types = {
        name.rsplit("::", 1)[-1]
        for name in type_bindings
        if "::" not in name
    }
    # Panel is the one generic surface-level container created through the
    # MenuController macros. Ordinary registered child controls stay excluded.
    construct_types = bound_types | {"Panel"}
    type_pattern = "|".join(
        re.escape(type_name)
        for type_name in sorted(construct_types, key=len, reverse=True)
    )
    function_sources = sorted(name for name in type_bindings if "::" in name)
    function_leaves: dict[str, list[str]] = {}
    for source in function_sources:
        function_leaves.setdefault(source.rsplit("::", 1)[-1], []).append(source)

    entries: dict[SurfaceCreationEntry, int] = {}

    def register(entry: SurfaceCreationEntry) -> None:
        entries[entry] = entries.get(entry, 0) + 1

    for path in production_source_paths():
        if path.suffix.lower() not in {".c", ".cc", ".cpp"}:
            continue
        relative = path.relative_to(ROOT).as_posix()
        text = read_utf8(path)
        masked_text = cpp_code_mask(text)
        function_ranges = cpp_function_ranges(text, masked_text)

        make_pattern = re.compile(
            rf"\bstd::make_(shared|unique)<\s*"
            rf"((?:[A-Za-z_][A-Za-z0-9_]*::)*(?:{type_pattern}))\b"
        )
        for match in make_pattern.finditer(masked_text):
            owner = cpp_owner_at(match.start(), function_ranges)
            if owner is None:
                owner = "<namespace-static>"
            register(
                (
                    relative,
                    owner,
                    "make-" + match.group(1),
                    match.group(2).rsplit("::", 1)[-1],
                    cpp_assignment_target(text, match.start()),
                )
            )

        new_pattern = re.compile(
            rf"(?<![A-Za-z0-9_:])new\s+"
            rf"((?:[A-Za-z_][A-Za-z0-9_]*::)*(?:{type_pattern}))\b"
        )
        for match in new_pattern.finditer(masked_text):
            owner = cpp_owner_at(match.start(), function_ranges)
            if owner is None:
                owner = "<namespace-static>"
            register(
                (
                    relative,
                    owner,
                    "new",
                    match.group(1).rsplit("::", 1)[-1],
                    cpp_assignment_target(text, match.start()),
                )
            )

        stack_pattern = re.compile(
            rf"(?<![A-Za-z0-9_:>])"
            rf"((?:[A-Za-z_][A-Za-z0-9_]*::)*(?:{type_pattern}))"
            rf"\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?=[\(\{{])"
        )
        for match in stack_pattern.finditer(masked_text):
            owner = cpp_owner_at(match.start(), function_ranges)
            if owner is None:
                owner = "<namespace-static>"
            register(
                (
                    relative,
                    owner,
                    "stack-instance",
                    match.group(1).rsplit("::", 1)[-1],
                    match.group(2),
                )
            )

        macro_pattern = re.compile(
            r"\b(Add(?:Up)?MenuChild)\(\s*"
            r"([A-Za-z_][A-Za-z0-9_:]*)\s*,\s*"
            r"([A-Za-z_][A-Za-z0-9_]*)\s*\)"
        )
        for match in macro_pattern.finditer(masked_text):
            line_start = masked_text.rfind("\n", 0, match.start()) + 1
            if masked_text[line_start:match.start()].lstrip().startswith("#"):
                continue
            owner = cpp_owner_at(match.start(), function_ranges)
            if owner is None:
                continue
            register(
                (
                    relative,
                    owner,
                    match.group(1),
                    match.group(2).rsplit("::", 1)[-1],
                    match.group(3),
                )
            )

        resource_pattern = re.compile(r"\bloadMenuDefinition\s*\(")
        for match in resource_pattern.finditer(masked_text):
            owner = cpp_owner_at(match.start(), function_ranges)
            if owner is None:
                continue
            opening_parenthesis = masked_text.find("(", match.start(), match.end())
            argument = cpp_call_argument(text, opening_parenthesis)
            if argument is None:
                errors.append(
                    f"Cannot parse loadMenuDefinition call in {relative}:{owner}"
                )
                continue
            line_start = masked_text.rfind("\n", 0, match.start()) + 1
            receiver_text = masked_text[line_start:match.start()]
            receiver_match = re.search(
                r"([A-Za-z_][A-Za-z0-9_]*)\s*(?:->|\.)\s*$",
                receiver_text,
            )
            receiver = receiver_match.group(1) if receiver_match else "this"
            register(
                (
                    relative,
                    owner,
                    "resource-load",
                    "loadMenuDefinition",
                    receiver + ":" + normalized_cpp_expression(argument),
                )
            )

        for function_source in function_sources:
            for start, _end, owner in function_ranges:
                if owner == function_source:
                    register(
                        (
                            relative,
                            owner,
                            "function-definition",
                            function_source,
                            "<entry>",
                        )
                    )

        for leaf, candidate_sources in function_leaves.items():
            call_pattern = re.compile(rf"\b{re.escape(leaf)}\s*\(")
            for match in call_pattern.finditer(masked_text):
                owner = cpp_owner_at(match.start(), function_ranges)
                if owner is None:
                    continue
                matching_sources = [
                    source
                    for source in candidate_sources
                    if owner.startswith(source.rsplit("::", 1)[0] + "::")
                ]
                if len(matching_sources) != 1:
                    continue
                function_source = matching_sources[0]
                if owner == function_source:
                    continue
                opening_parenthesis = masked_text.find(
                    "(", match.start(), match.end()
                )
                argument = cpp_call_argument(text, opening_parenthesis)
                if argument is None:
                    errors.append(
                        f"Cannot parse {function_source} call in {relative}:{owner}"
                    )
                    continue
                register(
                    (
                        relative,
                        owner,
                        "function-call",
                        function_source,
                        normalized_cpp_expression(argument),
                    )
                )
    return entries


def load_surface_creation_baseline(
    surface_keys: set[str], errors: list[str]
) -> dict[SurfaceCreationEntry, tuple[str, str, int]]:
    if not CREATION_BASELINE_PATH.is_file():
        errors.append(
            f"Missing surface creation baseline: "
            f"{CREATION_BASELINE_PATH.relative_to(ROOT)}"
        )
        return {}
    entries: dict[SurfaceCreationEntry, tuple[str, str, int]] = {}
    for line_number, raw_line in enumerate(
        read_utf8(CREATION_BASELINE_PATH).splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = tuple(field.strip() for field in raw_line.split("\t"))
        if len(fields) not in {7, 8} or any(not field for field in fields):
            errors.append(
                f"{CREATION_BASELINE_PATH.relative_to(ROOT)}:{line_number}: "
                "expected role, policy, five stable-key fields, and an "
                "optional positive occurrence count"
            )
            continue
        role = fields[0]
        policy = fields[1]
        entry: SurfaceCreationEntry = fields[2:7]
        occurrence_text = fields[7] if len(fields) == 8 else "1"
        try:
            occurrence_count = int(occurrence_text)
        except ValueError:
            occurrence_count = 0
        if occurrence_count <= 0:
            errors.append(
                f"{CREATION_BASELINE_PATH.relative_to(ROOT)}:{line_number}: "
                f"invalid occurrence count {occurrence_text}"
            )
            continue
        if role not in SURFACE_CREATION_ROLES:
            errors.append(
                f"{CREATION_BASELINE_PATH.relative_to(ROOT)}:{line_number}: "
                f"unknown creation role {role}"
            )
            continue
        if entry[2] not in SURFACE_CREATION_ROLE_MECHANISMS[role]:
            errors.append(
                f"{CREATION_BASELINE_PATH.relative_to(ROOT)}:{line_number}: "
                f"role {role} cannot classify mechanism {entry[2]}"
            )
            continue
        if policy.startswith("exempt:"):
            if policy not in SURFACE_CREATION_POLICY_EXEMPTIONS:
                errors.append(
                    f"{CREATION_BASELINE_PATH.relative_to(ROOT)}:{line_number}: "
                    f"unknown creation policy exemption {policy}"
                )
                continue
            expected_role = SURFACE_CREATION_POLICY_EXEMPTION_ROLES[policy]
            if role != expected_role:
                errors.append(
                    f"{CREATION_BASELINE_PATH.relative_to(ROOT)}:{line_number}: "
                    f"{policy} requires role {expected_role}, found {role}"
                )
                continue
        else:
            policy_keys = policy.split(",")
            if len(set(policy_keys)) != len(policy_keys):
                errors.append(
                    f"{CREATION_BASELINE_PATH.relative_to(ROOT)}:{line_number}: "
                    f"duplicate policy key in {policy}"
                )
                continue
            unknown_policy_keys = sorted(set(policy_keys) - surface_keys)
            if unknown_policy_keys:
                errors.append(
                    f"{CREATION_BASELINE_PATH.relative_to(ROOT)}:{line_number}: "
                    "unknown surface policy key(s): "
                    + ", ".join(unknown_policy_keys)
                )
                continue
        if entry in entries:
            errors.append(
                f"{CREATION_BASELINE_PATH.relative_to(ROOT)}:{line_number}: "
                "duplicate creation entry"
            )
            continue
        entries[entry] = (role, policy, occurrence_count)
    return entries


def check_surface_creation_entries(
    type_bindings: dict[str, set[str]],
    surface_by_id: dict[str, str],
    errors: list[str],
) -> tuple[int, dict[str, int]]:
    surface_keys = set(surface_by_id.values())
    discovered = discover_surface_creation_entries(type_bindings, errors)
    baseline = load_surface_creation_baseline(surface_keys, errors)
    discovered_entries = set(discovered)
    baseline_entries = set(baseline)
    for entry in sorted(discovered_entries - baseline_entries):
        errors.append(
            "Unregistered surface creation entry: " + " | ".join(entry)
        )
    for entry in sorted(baseline_entries - discovered_entries):
        errors.append(
            "Stale surface creation entry "
            f"({baseline[entry][0]}, {baseline[entry][1]}): "
            + " | ".join(entry)
        )
    for entry in sorted(discovered_entries & baseline_entries):
        expected_count = baseline[entry][2]
        if discovered[entry] != expected_count:
            errors.append(
                "Surface creation occurrence count changed "
                f"(expected {expected_count}, found {discovered[entry]}): "
                + " | ".join(entry)
            )
        declared_policy = baseline[entry][1]
        subject = entry[3]
        if (
            not declared_policy.startswith("exempt:")
            and subject in type_bindings
        ):
            type_policy_keys = {
                surface_by_id[surface_id]
                for surface_id in type_bindings[subject]
            }
            declared_policy_keys = set(declared_policy.split(","))
            if not declared_policy_keys <= type_policy_keys:
                errors.append(
                    f"Creation entry policy {declared_policy} is incompatible "
                    f"with catalog binding for {subject}: "
                    + ", ".join(sorted(type_policy_keys))
                )
    registered_policy_keys = {
        policy_key
        for _role, policy, _occurrence_count in baseline.values()
        if not policy.startswith("exempt:")
        for policy_key in policy.split(",")
    }
    missing_creation_policies = sorted(surface_keys - registered_policy_keys)
    if missing_creation_policies:
        errors.append(
            "Surface policies without a registered creation entry: "
            + ", ".join(missing_creation_policies)
        )
    role_counts = {
        role: sum(
            occurrence_count
            for registered_role, _policy, occurrence_count in baseline.values()
            if registered_role == role
        )
        for role in sorted(SURFACE_CREATION_ROLES)
    }
    return sum(discovered.values()), role_counts


def discover_script_registrations(errors: list[str]) -> set[str]:
    script_path = ROOT / "src" / "Game" / "Script" / "Script.cpp"
    text = read_utf8(script_path)
    function_match = re.search(
        r"\bvoid\s+Script::registerFunc\s*\(\s*\)\s*\{", text
    )
    if function_match is None:
        errors.append("Cannot find Script::registerFunc()")
        return set()
    opening_brace = text.find("{", function_match.start())
    body = extract_braced_block(text, opening_brace)
    if body is None:
        errors.append("Cannot parse Script::registerFunc() body")
        return set()
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
    body = re.sub(r"//[^\n]*", "", body)
    body = re.sub(r"^\s*#[^\n]*", "", body, flags=re.MULTILINE)
    registrations = set(
        re.findall(r"\bregFunc\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)", body)
    )
    registrations.update(
        re.findall(
            r"\bregAlias\(\s*\"([^\"\r\n]+)\"\s*,",
            body,
        )
    )
    return registrations


def load_non_surface_script_baseline(errors: list[str]) -> set[str]:
    if not SCRIPT_BASELINE_PATH.is_file():
        errors.append(
            f"Missing non-surface script baseline: "
            f"{SCRIPT_BASELINE_PATH.relative_to(ROOT)}"
        )
        return set()
    names: set[str] = set()
    for line_number, raw_line in enumerate(
        read_utf8(SCRIPT_BASELINE_PATH).splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line != raw_line or "\t" in line:
            errors.append(
                f"{SCRIPT_BASELINE_PATH.relative_to(ROOT)}:{line_number}: "
                f"invalid registration name {line}"
            )
            continue
        if line in names:
            errors.append(
                f"{SCRIPT_BASELINE_PATH.relative_to(ROOT)}:{line_number}: "
                f"duplicate registration name {line}"
            )
            continue
        names.add(line)
    return names


def check_script_registrations(
    script_bindings: dict[str, str], errors: list[str]
) -> tuple[int, int]:
    registrations = discover_script_registrations(errors)
    non_surface_registrations = load_non_surface_script_baseline(errors)
    surface_registrations = set(script_bindings)
    overlap = sorted(surface_registrations & non_surface_registrations)
    if overlap:
        errors.append(
            "Script registrations classified as both surface and non-surface: "
            + ", ".join(overlap)
        )

    classified = surface_registrations | non_surface_registrations
    missing = sorted(registrations - classified)
    if missing:
        errors.append(
            "Unclassified script registrations (add a surface binding or explicit "
            "non-surface baseline entry): "
            + ", ".join(missing)
        )
    stale = sorted(classified - registrations)
    if stale:
        errors.append(
            "Classified script registrations no longer registered: "
            + ", ".join(stale)
        )
    return len(registrations), len(non_surface_registrations)


def main() -> int:
    errors: list[str] = []
    if not CATALOG_PATH.is_file():
        print(f"ERROR: missing {CATALOG_PATH.relative_to(ROOT)}", file=sys.stderr)
        return 1

    (
        surface_by_id,
        exemptions_by_category,
        type_bindings,
        resource_bindings,
        script_bindings,
    ) = parse_catalog(errors)
    discovered_type_count = check_ui_types(
        type_bindings, exemptions_by_category.get("type", set()), errors
    )
    creation_entry_count, creation_role_counts = check_surface_creation_entries(
        type_bindings, surface_by_id, errors
    )
    aggregate_count, profiles, interaction_count = check_resources(
        resource_bindings, errors
    )
    script_registration_count, non_surface_registration_count = (
        check_script_registrations(script_bindings, errors)
    )

    if errors:
        print("Menu input catalog contract FAILED:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    profile_text = f"{len(profiles)} profiles" if profiles else "source-only"
    creation_role_text = ", ".join(
        f"{count} {role}"
        for role, count in creation_role_counts.items()
        if count
    )
    print(
        "Menu input catalog contract passed: "
        f"{len(surface_by_id)} surfaces, "
        f"{discovered_type_count} Element-derived types, "
        f"{len(type_bindings)} C++ type/function bindings, "
        f"{creation_entry_count} registered surface creation entries "
        f"({creation_role_text}), "
        f"{aggregate_count} worktree aggregate menu files "
        f"({profile_text}), "
        f"{interaction_count} registered resource interactions, "
        f"{script_registration_count} script registrations "
        f"({len(script_bindings)} surface, "
        f"{non_surface_registration_count} explicit non-surface)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
