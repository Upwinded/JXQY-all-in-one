#!/usr/bin/env python3
"""Build a multi-source MOD capability inventory.

This tool compares the current C++ runtime surface against the reference
projects that are useful for MOD integration:

- JxqyHD-develop: primary C# reference already used by parity checks.
- 新剑侠源码: older/new-JXQY C# source tree.
- miu2d: TypeScript runtime and mobile-oriented script/UI reference.

It is intentionally static. It does not prove behavior parity; it creates a
repeatable backlog so future work can batch related gaps instead of relying on
ad-hoc searches.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from analyze_csharp_parity import (
    CPP_SCRIPT_FILES,
    DOMAINS,
    REPO_ROOT,
    extract_cpp_behavior_coverage,
    extract_cpp_ini_keys,
    extract_cpp_runtime_state_names,
    extract_cpp_script_registrations,
    extract_csharp_properties,
    extract_csharp_script_commands,
    format_location,
    normalize_name,
    scan_resource_script_calls,
)


@dataclass(frozen=True)
class SourceSpec:
    source_id: str
    label: str
    kind: str
    script_files: tuple[str, ...]
    script_doc_files: tuple[str, ...]
    domain_files: dict[str, tuple[str, ...]]
    enum_files: tuple[str, ...]


@dataclass(frozen=True)
class BehaviorTopic:
    topic_id: str
    area: str
    label: str
    patterns: tuple[str, ...]


REFERENCE_SOURCES: tuple[SourceSpec, ...] = (
    SourceSpec(
        source_id="jxqyhd",
        label="JxqyHD-develop C#",
        kind="csharp",
        script_files=("JxqyHD-develop/Engine/Script/ScriptRunner.cs",),
        script_doc_files=(),
        domain_files={
            domain_name: domain.csharp_files
            for domain_name, domain in DOMAINS.items()
        },
        enum_files=(
            "JxqyHD-develop/Engine/Magic.cs",
            "JxqyHD-develop/Engine/Good.cs",
            "JxqyHD-develop/Engine/Obj.cs",
            "JxqyHD-develop/Engine/Character.cs",
        ),
    ),
    SourceSpec(
        source_id="xjxqy_cs",
        label="新剑侠源码 C#",
        kind="csharp",
        script_files=("新剑侠源码/newjxsrc/CrossPlatform/Script/ScriptRunner.cs",),
        script_doc_files=(),
        domain_files={
            "magic": ("新剑侠源码/newjxsrc/CrossPlatform/Magic.cs",),
            "goods": ("新剑侠源码/newjxsrc/CrossPlatform/Good.cs",),
            "object": ("新剑侠源码/newjxsrc/CrossPlatform/Obj.cs",),
            "npc": (
                "新剑侠源码/newjxsrc/CrossPlatform/Character.cs",
                "新剑侠源码/newjxsrc/CrossPlatform/Npc.cs",
            ),
        },
        enum_files=(
            "新剑侠源码/newjxsrc/CrossPlatform/Magic.cs",
            "新剑侠源码/newjxsrc/CrossPlatform/Good.cs",
            "新剑侠源码/newjxsrc/CrossPlatform/Obj.cs",
            "新剑侠源码/newjxsrc/CrossPlatform/Character.cs",
        ),
    ),
    SourceSpec(
        source_id="miu2d",
        label="miu2d TypeScript",
        kind="typescript",
        script_files=(
            "miu2d/packages/engine/src/script/commands/audio-commands.ts",
            "miu2d/packages/engine/src/script/commands/dialog-commands.ts",
            "miu2d/packages/engine/src/script/commands/effect-commands.ts",
            "miu2d/packages/engine/src/script/commands/game-state-commands.ts",
            "miu2d/packages/engine/src/script/commands/goods-commands.ts",
            "miu2d/packages/engine/src/script/commands/misc-commands.ts",
            "miu2d/packages/engine/src/script/commands/npc-commands.ts",
            "miu2d/packages/engine/src/script/commands/obj-commands.ts",
            "miu2d/packages/engine/src/script/commands/player-commands.ts",
        ),
        script_doc_files=("miu2d/docs/script-commands.md",),
        domain_files={
            "magic": (
                "miu2d/packages/engine/src/magic/magic-data.ts",
                "miu2d/packages/engine/src/magic/magic-enums.ts",
            ),
            "goods": ("miu2d/packages/engine/src/player/goods/good.ts",),
            "object": (
                "miu2d/packages/engine/src/obj/obj.ts",
                "miu2d/packages/engine/src/obj/types.ts",
            ),
            "npc": (
                "miu2d/packages/engine/src/character/character.ts",
                "miu2d/packages/engine/src/npc/npc.ts",
            ),
        },
        enum_files=(
            "miu2d/packages/engine/src/magic/magic-enums.ts",
            "miu2d/packages/engine/src/player/goods/good.ts",
            "miu2d/packages/engine/src/obj/obj.ts",
            "miu2d/packages/engine/src/core/types.ts",
        ),
    ),
)

REFERENCE_BEHAVIOR_GLOBS: dict[str, tuple[str, ...]] = {
    "jxqyhd": ("JxqyHD-develop/Engine/**/*.cs",),
    "xjxqy_cs": ("*/newjxsrc/CrossPlatform/**/*.cs",),
    "miu2d": (
        "miu2d/packages/engine/src/**/*.ts",
        "miu2d/packages/game/src/components/**/*.tsx",
        "miu2d/packages/game/src/contexts/**/*.tsx",
        "miu2d/packages/game/src/hooks/**/*.ts",
    ),
}
CPP_BEHAVIOR_GLOBS = (
    "src/Game/**/*.h",
    "src/Game/**/*.cpp",
    "src/Component/**/*.h",
    "src/Component/**/*.cpp",
    "src/Engine/**/*.h",
    "src/Engine/**/*.cpp",
    "src/Platform/**/*.h",
    "src/Platform/**/*.cpp",
)
CPP_ENUM_FILES = (
    "src/Game/Data/Magic.h",
    "src/Game/Data/Goods.h",
    "src/Game/Data/Object.h",
    "src/Game/Data/NPC.h",
    "src/Game/Data/NPCAction/NPCActionType.h",
)
BEHAVIOR_TOPICS: tuple[BehaviorTopic, ...] = (
    BehaviorTopic("script.choose_multiple", "script", "multi-select script/UI completion", (r"choosemultiple", r"SelectionMultiple", r"multiple selection")),
    BehaviorTopic("script.gamble", "script", "gamble and gambling UI scripts", (r"\bgamble\b", r"ShowGamble", r"GambleGui", r"GambleMenu")),
    BehaviorTopic("script.timer", "script", "timer and time scripts", (r"\btimer\b", r"TimerGui", r"settimer", r"gettimer")),
    BehaviorTopic("script.media", "script", "script-triggered audio/video", (r"playsound", r"playmusic", r"playmovie", r"video", r"VideoPlayer")),
    BehaviorTopic("goods.use_drug", "goods", "drug and consumable use", (r"\bgkDrug\b", r"\bDrug\b", r"useItem", r"canUseGoods", r"drug effect")),
    BehaviorTopic("goods.script_item", "goods", "script-backed item use", (r"\bScript\b", r"runGoodsScript", r"UseScript", r"ScriptFile")),
    BehaviorTopic("goods.equip", "goods", "equipment checks and equip slots", (r"\bgkEquipment\b", r"\bEquipment\b", r"canEquip", r"equipGoods", r"EquipGui")),
    BehaviorTopic("goods.drag_drop", "goods", "goods drag/drop and touch drop", (r"DragDrop", r"drag", r"drop", r"TouchDrag")),
    BehaviorTopic("magic.move_kind", "magic", "magic movement kinds", (r"MoveKind", r"MoveBack", r"Follow", r"MeteorMove", r"CircleMove")),
    BehaviorTopic("magic.special_kind", "magic", "magic special kinds", (r"SpecialKind", r"RangePetrify", r"PassThrough", r"Sticky", r"Parasitic")),
    BehaviorTopic("magic.region", "magic", "region and attack shape", (r"Region", r"AttackRadius", r"RegionFile", r"MagicRegion")),
    BehaviorTopic("magic.bounce", "magic", "bounce/knockback action", (r"\bbounce\b", r"BounceMotion", r"NPCActionBounce", r"MagicForcedMove")),
    BehaviorTopic("magic.transport", "magic", "jump/transport magic", (r"Transport", r"Teleport", r"Jump", r"BeginAt")),
    BehaviorTopic("magic.summon", "magic", "summon and generated NPC/object effects", (r"Summon", r"CreateNpc", r"AddNpc", r"GenerateNpc")),
    BehaviorTopic("magic.state_effect", "magic", "state/status effects", (r"Status", r"State", r"Petrify", r"Poison", r"Blind", r"Hide")),
    BehaviorTopic("effect.lifecycle", "effect", "effect lifecycle and sprite frames", (r"LifeFrame", r"WaitFrame", r"Vanish", r"FlyingImage", r"effect")),
    BehaviorTopic("npc.relation", "npc", "NPC relation/camp/kind", (r"Relation", r"Camp", r"Kind", r"RelationType")),
    BehaviorTopic("npc.ai_targeting", "npc", "NPC AI targeting", (r"target", r"Target", r"AI", r"SearchEnemy", r"FindEnemy")),
    BehaviorTopic("npc.partner", "npc", "partner follow and team behavior", (r"partner", r"Partner", r"FollowPartner", r"LittleHead", r"teammate")),
    BehaviorTopic("npc.revive_death", "npc", "death/revive lifecycle", (r"Revive", r"Death", r"Dead", r"Respawn")),
    BehaviorTopic("npc.pathfinding", "npc", "grid/path movement", (r"Path", r"Walk", r"Run", r"AStar", r"Tile")),
    BehaviorTopic("object.right_script", "object", "object right-click/right action script", (r"ScriptFileRight", r"RightScript", r"right[- ]?click", r"RunObjRightScript", r"hasInteractScriptRight")),
    BehaviorTopic("object.touch_only", "object", "touch-only object interaction", (r"JustTouch", r"Touch", r"long[- ]?press", r"MobileTouch")),
    BehaviorTopic("object.trap_timer", "object", "object trap/timer behavior", (r"Trap", r"Timer", r"ObjTrap", r"trigger")),
    BehaviorTopic("object.resource_variant", "object", "object resource variants and media", (r"ObjFile", r"Shade", r"Sound", r"Movie", r"ObjRes")),
    BehaviorTopic("ui.save_load", "ui", "save/load UI", (r"SaveLoad", r"save slot", r"LoadGame")),
    BehaviorTopic("ui.title_video", "ui", "title and startup video UI", (r"TitleGui", r"TitleTeam", r"startup video", r"VideoPlayer")),
    BehaviorTopic("ui.dialog_message", "ui", "dialog/message menus", (r"Dialog", r"Message", r"MsgBox", r"Talk")),
    BehaviorTopic("mobile.input", "mobile", "mobile/touch input path", (r"Mobile", r"Touch", r"Pointer", r"LongPress")),
    BehaviorTopic("mobile.virtual_controls", "mobile", "virtual joystick/action buttons", (r"VirtualJoystick", r"Joystick", r"MobileAction", r"MobileSkill")),
)
PROPERTY_GAP_TRIAGE: dict[tuple[str, str, str], tuple[str, str, str]] = {
    ("xjxqy_cs", "npc", "summonednpcscount"): (
        "runtime_counter",
        "review",
        "New-JXQY counter/state name. Treat as behavior-review backlog, not a resource loader field.",
    ),
    ("miu2d", "goods", "imagepath"): (
        "resolved_resource_path",
        "no_action",
        "miu2d stores resolved image paths; C++ stores resource names and resolves through ResourceManager.",
    ),
    ("miu2d", "goods", "iconpath"): (
        "resolved_resource_path",
        "no_action",
        "miu2d stores resolved icon paths; C++ stores resource names and resolves through ResourceManager.",
    ),
    ("miu2d", "magic", "circlemoveclockwise"): (
        "loader_alias",
        "implemented",
        "Standard spelling alias for the historical CircleMoveColockwise key.",
    ),
    ("miu2d", "magic", "roundmoveclockwise"): (
        "loader_alias",
        "implemented",
        "Standard spelling alias for the historical RoundMoveColockwise key.",
    ),
    ("miu2d", "magic", "rangetimeinterval"): (
        "loader_alias",
        "implemented",
        "Standard spelling alias for the historical RangeTimeInerval key.",
    ),
    ("miu2d", "magic", "passwidth"): (
        "miu2d_engine_extension",
        "defer",
        "Projectile sweep width in miu2d; C++ already has pass-path logic, so add an INI field only with asset/runtime evidence.",
    ),
    ("miu2d", "magic", "levels"): (
        "inventory_runtime_container",
        "no_action",
        "Magic-list runtime container, not a MagicData/INI resource field.",
    ),
    ("miu2d", "magic", "magic"): (
        "inventory_runtime_container",
        "no_action",
        "Magic-list runtime container, not a MagicData/INI resource field.",
    ),
    ("miu2d", "magic", "exp"): (
        "inventory_runtime_container",
        "no_action",
        "Learned-magic runtime progress, not a MagicData/INI resource field.",
    ),
    ("miu2d", "magic", "remaincoldmilliseconds"): (
        "inventory_runtime_container",
        "no_action",
        "Cooldown runtime state, not a MagicData/INI resource field.",
    ),
    ("miu2d", "magic", "hidecount"): (
        "internal_runtime_state",
        "no_action",
        "miu2d hide/visibility runtime bookkeeping.",
    ),
    ("miu2d", "magic", "lastindexwhenhide"): (
        "internal_runtime_state",
        "no_action",
        "miu2d hide/visibility runtime bookkeeping.",
    ),
    ("miu2d", "magic", "userid"): (
        "call_parameter",
        "no_action",
        "UseMagicParams input, not a loaded magic field.",
    ),
    ("miu2d", "magic", "origin"): (
        "call_parameter",
        "no_action",
        "UseMagicParams input, not a loaded magic field.",
    ),
    ("miu2d", "magic", "destination"): (
        "call_parameter",
        "no_action",
        "UseMagicParams input, not a loaded magic field.",
    ),
    ("miu2d", "magic", "targetid"): (
        "call_parameter",
        "no_action",
        "UseMagicParams input, not a loaded magic field.",
    ),
    ("miu2d", "magic", "belongcharacterid"): (
        "internal_effect_state",
        "no_action",
        "Trail/effect runtime state with C++-specific ownership handling.",
    ),
    ("miu2d", "magic", "lasttileposition"): (
        "internal_effect_state",
        "no_action",
        "Trail/effect runtime state with C++-specific tile handling.",
    ),
    ("miu2d", "npc", "destinationpixelposition"): (
        "movement_runtime_state",
        "no_action",
        "miu2d movement cache; C++ keeps movement state in its own NPC action/path system.",
    ),
    ("miu2d", "npc", "magicdata"): (
        "factory_or_call_parameter",
        "no_action",
        "Factory/API parameter rather than an NPC INI field.",
    ),
    ("miu2d", "npc", "options"): (
        "factory_or_call_parameter",
        "no_action",
        "Factory/API options object rather than an NPC INI field.",
    ),
    ("miu2d", "npc", "renderer"): (
        "render_parameter",
        "no_action",
        "Web renderer dependency, not C++ NPC runtime data.",
    ),
    ("miu2d", "npc", "camerax"): (
        "render_parameter",
        "no_action",
        "Draw-time camera parameter, not C++ NPC runtime data.",
    ),
    ("miu2d", "npc", "cameray"): (
        "render_parameter",
        "no_action",
        "Draw-time camera parameter, not C++ NPC runtime data.",
    ),
    ("miu2d", "npc", "highlightcolor"): (
        "render_parameter",
        "no_action",
        "Web highlight rendering option, not C++ NPC resource data.",
    ),
    ("miu2d", "npc", "playerindex"): (
        "ui_api_bridge",
        "no_action",
        "Player/UI bridge index, not an NPC INI field.",
    ),
    ("miu2d", "npc", "playergoodsmanager"): (
        "ui_api_bridge",
        "no_action",
        "Partner/equipment UI bridge state, not an NPC INI field.",
    ),
    ("miu2d", "npc", "playerbagindex"): (
        "ui_api_bridge",
        "no_action",
        "Partner/equipment UI bridge state, not an NPC INI field.",
    ),
    ("miu2d", "npc", "equipposition"): (
        "ui_api_bridge",
        "no_action",
        "Partner/equipment UI bridge state, not an NPC INI field.",
    ),
    ("miu2d", "npc", "bottomslot"): (
        "ui_api_bridge",
        "no_action",
        "Partner/equipment UI bridge state, not an NPC INI field.",
    ),
    ("miu2d", "npc", "desttile"): (
        "movement_call_parameter",
        "no_action",
        "Path/movement call parameter, not an NPC INI field.",
    ),
    ("miu2d", "npc", "pathtypeoverride"): (
        "movement_call_parameter",
        "no_action",
        "Path/movement call parameter, not an NPC INI field.",
    ),
    ("miu2d", "npc", "configpath"): (
        "factory_or_call_parameter",
        "no_action",
        "Factory input path, not a loaded NPC property.",
    ),
    ("miu2d", "npc", "tilex"): (
        "factory_or_call_parameter",
        "no_action",
        "Factory spawn coordinate, not a loaded NPC property.",
    ),
    ("miu2d", "npc", "tiley"): (
        "factory_or_call_parameter",
        "no_action",
        "Factory spawn coordinate, not a loaded NPC property.",
    ),
    ("miu2d", "npc", "config"): (
        "factory_or_call_parameter",
        "no_action",
        "Factory input object, not a loaded NPC property.",
    ),
    ("miu2d", "object", "renderer"): (
        "render_parameter",
        "no_action",
        "Web renderer dependency, not C++ object runtime data.",
    ),
    ("miu2d", "object", "camerax"): (
        "render_parameter",
        "no_action",
        "Draw-time camera parameter, not C++ object runtime data.",
    ),
    ("miu2d", "object", "cameray"): (
        "render_parameter",
        "no_action",
        "Draw-time camera parameter, not C++ object runtime data.",
    ),
}
ENUM_CONFLICT_TRIAGE: dict[str, tuple[str, str, str]] = {
    "magicaddoneffect": (
        "naming_only",
        "no_action",
        "C++ and miu2d numeric values match; the difference is member naming/prefix style.",
    ),
    "magicmovekind": (
        "numeric_equivalent",
        "no_action",
        "C++/C# and miu2d share the same numeric model for implemented move kinds; miu2d NoMove=0 is a default/unset value.",
    ),
    "magicspecialkind": (
        "value_extension",
        "no_action",
        "Resolved by maintainer decision: C++ uses SpecialKind=3 for reference damage-reduce shields, SpecialKind=10 for miu2d-style Immobilize, and SpecialKind=11 as a JXQY2 full-absorb shield extension.",
    ),
    "relationtype": (
        "reference_superset",
        "no_action",
        "C++ keeps JxqyHD None=3 support; the New-JXQY source simply lacks that value.",
    ),
}
CONTROL_COMMANDS = {"if", "goto", "return"}
TS_REGISTRY_RE = re.compile(r"\bregistry\.set\(\s*[\"'](?P<name>[^\"']+)[\"']")
MARKDOWN_COMMAND_RE = re.compile(r"`(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\(")
C_SHARP_ENUM_RE = re.compile(r"\benum\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\{(?P<body>.*?)\}", re.S)
CPP_ENUM_RE = re.compile(r"\benum(?:\s+class)?\s+(?P<name>[A-Za-z_][A-Za-z0-9_:]*)[^{;]*\{(?P<body>.*?)\}", re.S)
TS_ENUM_RE = re.compile(r"\benum\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\{(?P<body>.*?)\}", re.S)
ENUM_MEMBER_RE = re.compile(
    r"^[ \t]*(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    r"[ \t]*(?:=[ \t]*(?P<value>[^,\r\n]+?))?[ \t]*,?[ \t]*(?://.*)?\r?$",
    re.M,
)
TS_CONTEXT_START_RE = re.compile(
    r"^\s*(?:export\s+)?(?:abstract\s+)?(?P<kind>interface|class)\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
)
TS_PROPERTY_RE = re.compile(
    r"^\s*(?:public|private|protected|readonly|static|override|declare|abstract|\s)*"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\??\s*:\s*(?P<type>[^;={(),]+)[;,]?"
)


def read_text_best_effort(path: Path) -> str:
    data = path.read_bytes()
    for encoding in ("utf-8-sig", "utf-8", "gbk", "cp950"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", errors="replace")


def iter_existing(paths: Iterable[str]) -> Iterable[Path]:
    for raw_path in paths:
        path = REPO_ROOT / raw_path
        if path.exists():
            yield path


def iter_glob_existing(patterns: Iterable[str]) -> list[Path]:
    result: list[Path] = []
    seen: set[Path] = set()
    for pattern in patterns:
        for path in REPO_ROOT.glob(pattern):
            if not path.is_file() or path in seen:
                continue
            seen.add(path)
            result.append(path)
    return sorted(result)


def make_item(name: str, path: Path, line: int, **extra: object) -> dict[str, object]:
    item: dict[str, object] = {
        "name": name,
        "normalized": normalize_name(name),
        "file": str(path.relative_to(REPO_ROOT)).replace("\\", "/"),
        "line": line,
    }
    item.update(extra)
    return item


def unique_by_normalized(items: Iterable[dict[str, object]]) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    seen: set[str] = set()
    for item in items:
        key = str(item["normalized"])
        if key in seen:
            continue
        seen.add(key)
        result.append(item)
    return result


def make_evidence_item(pattern: str, path: Path, line: int, text: str) -> dict[str, object]:
    return {
        "pattern": pattern,
        "file": str(path.relative_to(REPO_ROOT)).replace("\\", "/"),
        "line": line,
        "snippet": text.strip()[:160],
    }


def extract_typescript_registry_commands(paths: Iterable[str]) -> list[dict[str, object]]:
    commands: list[dict[str, object]] = []
    for path in iter_existing(paths):
        text = read_text_best_effort(path)
        for match in TS_REGISTRY_RE.finditer(text):
            commands.append(make_item(match.group("name"), path, text[: match.start()].count("\n") + 1))
    return unique_by_normalized(commands)


def extract_markdown_commands(paths: Iterable[str]) -> list[dict[str, object]]:
    commands: list[dict[str, object]] = []
    for path in iter_existing(paths):
        text = read_text_best_effort(path)
        for match in MARKDOWN_COMMAND_RE.finditer(text):
            commands.append(make_item(match.group("name"), path, text[: match.start()].count("\n") + 1))
    return unique_by_normalized(commands)


def extract_script_commands(source: SourceSpec) -> list[dict[str, object]]:
    if source.kind == "typescript":
        return unique_by_normalized(
            extract_typescript_registry_commands(source.script_files)
            + extract_markdown_commands(source.script_doc_files)
        )
    return extract_csharp_script_commands(source.script_files)


def extract_typescript_properties(paths: Iterable[str]) -> list[dict[str, object]]:
    properties: list[dict[str, object]] = []
    for path in iter_existing(paths):
        lines = read_text_best_effort(path).splitlines()
        context_name = ""
        context_kind = ""
        brace_depth = 0
        pending_context: tuple[str, str] | None = None
        for line_no, line in enumerate(lines, start=1):
            start = TS_CONTEXT_START_RE.match(line)
            if start:
                pending_context = (start.group("kind"), start.group("name"))
            if pending_context and "{" in line:
                context_kind, context_name = pending_context
                pending_context = None
                brace_depth = line.count("{") - line.count("}")
                continue
            if not context_name:
                continue

            depth_before_line = brace_depth
            stripped = line.strip()
            if (
                not stripped
                or stripped.startswith("//")
                or "(" in stripped.split(":", 1)[0]
                or stripped.startswith(("constructor", "get ", "set ", "return ", "case ", "if "))
            ):
                brace_depth += line.count("{") - line.count("}")
                if brace_depth <= 0:
                    context_name = ""
                    context_kind = ""
                continue

            match = TS_PROPERTY_RE.match(line) if depth_before_line == 1 else None
            if match:
                name = match.group("name")
                if name.startswith("_") or stripped.startswith("private "):
                    brace_depth += line.count("{") - line.count("}")
                    if brace_depth <= 0:
                        context_name = ""
                        context_kind = ""
                    continue
                properties.append(
                    make_item(
                        name,
                        path,
                        line_no,
                        type=match.group("type").strip(),
                        context=context_name,
                        context_kind=context_kind,
                    )
                )
            brace_depth += line.count("{") - line.count("}")
            if brace_depth <= 0:
                context_name = ""
                context_kind = ""
    return unique_by_normalized(properties)


def extract_domain_properties(source: SourceSpec, domain_name: str) -> list[dict[str, object]]:
    paths = source.domain_files.get(domain_name, ())
    if source.kind == "typescript":
        return extract_typescript_properties(paths)
    return extract_csharp_properties(paths)


def classify_domain_gap(source_id: str, domain_name: str, normalized_name: str) -> tuple[str, str, str]:
    return PROPERTY_GAP_TRIAGE.get(
        (source_id, domain_name, normalized_name),
        ("unclassified", "review", "Needs manual triage before it becomes an implementation task."),
    )


def parse_enum_member_value(expression: str, known_values: dict[str, int]) -> int | None:
    expression = expression.split("//", 1)[0]
    expression = re.sub(r"/\*.*?\*/", "", expression).strip()
    numeric_match = re.fullmatch(r"([+-]?(?:0[xX][0-9A-Fa-f]+|\d+))(?:[uUlL]+)?", expression)
    if numeric_match is not None:
        try:
            return int(numeric_match.group(1), 0)
        except ValueError:
            return None

    alias_match = re.fullmatch(
        r"(?:(?:[A-Za-z_][A-Za-z0-9_]*)(?:::|\.))*"
        r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)",
        expression,
    )
    if alias_match is not None:
        return known_values.get(alias_match.group("name"))
    return None


def parse_enum_members(body: str) -> list[dict[str, object]]:
    members: list[dict[str, object]] = []
    known_values: dict[str, int] = {}
    next_value: int | None = 0
    for match in ENUM_MEMBER_RE.finditer(body):
        name = match.group("name")
        if name in {"get", "set", "public", "private", "protected"}:
            continue
        raw_value = match.group("value")
        if raw_value is not None:
            value = parse_enum_member_value(raw_value, known_values)
        else:
            value = next_value
        members.append({"name": name, "normalized": normalize_name(name), "value": value})
        if value is None:
            next_value = None
        else:
            known_values[name] = value
            next_value = value + 1
    return members


def extract_enums(paths: Iterable[str], language: str) -> list[dict[str, object]]:
    pattern = TS_ENUM_RE if language == "typescript" else C_SHARP_ENUM_RE
    if language == "cpp":
        pattern = CPP_ENUM_RE
    enums: list[dict[str, object]] = []
    for path in iter_existing(paths):
        text = read_text_best_effort(path)
        for match in pattern.finditer(text):
            name = match.group("name").split("::")[-1]
            enums.append(
                make_item(
                    name,
                    path,
                    text[: match.start()].count("\n") + 1,
                    members=parse_enum_members(match.group("body")),
                )
            )
    return unique_by_normalized(enums)


def analyze_domain_for_source(
    source: SourceSpec,
    domain_name: str,
    cpp_coverage: dict[str, dict[str, list[dict[str, object]]]],
) -> dict[str, object]:
    properties = extract_domain_properties(source, domain_name)
    domain_coverage = cpp_coverage[domain_name]
    cpp_names = set(domain_coverage["loader"]) | set(domain_coverage["runtime"]) | set(domain_coverage["behavior"])
    missing: list[dict[str, object]] = []
    decision_counts: dict[str, int] = {}
    for item in properties:
        normalized_name = str(item["normalized"])
        if normalized_name in cpp_names:
            continue
        gap = dict(item)
        category, decision, note = classify_domain_gap(source.source_id, domain_name, normalized_name)
        gap["triage_category"] = category
        gap["triage_decision"] = decision
        gap["triage_note"] = note
        decision_counts[decision] = decision_counts.get(decision, 0) + 1
        missing.append(gap)
    return {
        "domain": domain_name,
        "property_count": len(properties),
        "cpp_missing_count": len(missing),
        "triage_decision_counts": decision_counts,
        "properties": properties,
        "cpp_missing": missing,
    }


def build_cpp_domain_coverage() -> dict[str, dict[str, list[dict[str, object]]]]:
    coverage: dict[str, dict[str, list[dict[str, object]]]] = {}
    for domain_name, domain in DOMAINS.items():
        coverage[domain_name] = {
            "loader": {
                str(item["normalized"]): item
                for item in extract_cpp_ini_keys(domain.cpp_files)
            },
            "runtime": {
                str(item["normalized"]): item
                for item in extract_cpp_runtime_state_names(domain_name)
            },
            "behavior": {
                str(item["normalized"]): item
                for item in extract_cpp_behavior_coverage(domain_name)
            },
        }
    return coverage


def analyze_scripts(assets_root: Path, max_examples: int) -> dict[str, object]:
    cpp_commands = extract_cpp_script_registrations(CPP_SCRIPT_FILES)
    asset_calls = scan_resource_script_calls(assets_root, max_examples)
    cpp_by_name = {str(item["normalized"]): item for item in cpp_commands}
    asset_by_name = {str(item["normalized"]): item for item in asset_calls}
    source_results = []
    union: dict[str, dict[str, object]] = {}

    for source in REFERENCE_SOURCES:
        commands = extract_script_commands(source)
        missing_cpp_all = [item for item in commands if str(item["normalized"]) not in cpp_by_name]
        missing_cpp = [
            item
            for item in missing_cpp_all
            if str(item["normalized"]) not in CONTROL_COMMANDS
        ]
        missing_cpp_used_by_assets = [
            item
            for item in missing_cpp
            if str(item["normalized"]) in asset_by_name
        ]
        for item in commands:
            normalized = str(item["normalized"])
            entry = union.setdefault(
                normalized,
                {
                    "name": item["name"],
                    "normalized": normalized,
                    "sources": [],
                    "cpp_registered": normalized in cpp_by_name,
                    "asset_used": normalized in asset_by_name,
                    "is_control": normalized in CONTROL_COMMANDS,
                    "examples": asset_by_name.get(normalized, {}).get("examples", []),
                },
            )
            sources = entry["sources"]
            if isinstance(sources, list):
                sources.append(source.source_id)
        source_results.append(
            {
                "source": source.source_id,
                "label": source.label,
                "command_count": len(commands),
                "cpp_missing_all_count": len(missing_cpp_all),
                "cpp_missing_count": len(missing_cpp),
                "cpp_missing_used_by_assets_count": len(missing_cpp_used_by_assets),
                "commands": commands,
                "cpp_missing_all": missing_cpp_all,
                "cpp_missing": missing_cpp,
                "cpp_missing_used_by_assets": missing_cpp_used_by_assets,
            }
        )

    union_items = sorted(union.values(), key=lambda item: (str(item["name"]).lower(), str(item["name"])))
    return {
        "cpp_registered_count": len(cpp_commands),
        "asset_call_count": len(asset_calls),
        "sources": source_results,
        "union_count": len(union_items),
        "union_cpp_missing": [
            item for item in union_items
            if not item["cpp_registered"] and not item["is_control"]
        ],
        "union_cpp_missing_used_by_assets": [
            item for item in union_items
            if not item["cpp_registered"] and item["asset_used"] and not item["is_control"]
        ],
        "union_commands": union_items,
        "cpp_registrations": cpp_commands,
        "asset_calls": asset_calls,
    }


def analyze_domains() -> list[dict[str, object]]:
    cpp_coverage = build_cpp_domain_coverage()
    results = []
    for source in REFERENCE_SOURCES:
        source_domains = [
            analyze_domain_for_source(source, domain_name, cpp_coverage)
            for domain_name in sorted(DOMAINS.keys())
        ]
        results.append(
            {
                "source": source.source_id,
                "label": source.label,
                "domains": source_domains,
            }
        )
    return results


def analyze_enums() -> dict[str, object]:
    cpp_enums = extract_enums(CPP_ENUM_FILES, "cpp")
    cpp_names = {str(item["normalized"]) for item in cpp_enums}
    source_results = []
    for source in REFERENCE_SOURCES:
        enums = extract_enums(source.enum_files, source.kind)
        missing = [item for item in enums if str(item["normalized"]) not in cpp_names]
        source_results.append(
            {
                "source": source.source_id,
                "label": source.label,
                "enum_count": len(enums),
                "cpp_missing_count": len(missing),
                "enums": enums,
                "cpp_missing": missing,
            }
        )
    return {
        "cpp_enum_count": len(cpp_enums),
        "cpp_enums": cpp_enums,
        "sources": source_results,
    }


def scan_behavior_topic(files: list[Path], topic: BehaviorTopic, max_examples: int) -> dict[str, object]:
    patterns = [(pattern, re.compile(pattern, re.I)) for pattern in topic.patterns]
    match_count = 0
    matched_files: set[str] = set()
    examples: list[dict[str, object]] = []
    for path in files:
        text = read_text_best_effort(path)
        for line_no, line in enumerate(text.splitlines(), start=1):
            matched_pattern = None
            for raw_pattern, pattern in patterns:
                if pattern.search(line):
                    matched_pattern = raw_pattern
                    break
            if matched_pattern is None:
                continue
            match_count += 1
            matched_files.add(str(path.relative_to(REPO_ROOT)).replace("\\", "/"))
            if len(examples) < max_examples:
                examples.append(make_evidence_item(matched_pattern, path, line_no, line))
    return {
        "match_count": match_count,
        "matched_file_count": len(matched_files),
        "examples": examples,
    }


def analyze_behavior_topics(max_examples: int) -> dict[str, object]:
    cpp_files = iter_glob_existing(CPP_BEHAVIOR_GLOBS)
    source_files = {
        source.source_id: iter_glob_existing(REFERENCE_BEHAVIOR_GLOBS.get(source.source_id, ()))
        for source in REFERENCE_SOURCES
    }
    topics: list[dict[str, object]] = []
    for topic in BEHAVIOR_TOPICS:
        cpp_scan = scan_behavior_topic(cpp_files, topic, max_examples)
        source_scans: list[dict[str, object]] = []
        for source in REFERENCE_SOURCES:
            scan = scan_behavior_topic(source_files[source.source_id], topic, max_examples)
            source_scans.append(
                {
                    "source": source.source_id,
                    "label": source.label,
                    **scan,
                }
            )
        reference_sources_present = [
            str(scan["source"])
            for scan in source_scans
            if int(scan["match_count"]) > 0
        ]
        topics.append(
            {
                "topic_id": topic.topic_id,
                "area": topic.area,
                "label": topic.label,
                "patterns": list(topic.patterns),
                "reference_sources_present": reference_sources_present,
                "reference_present": bool(reference_sources_present),
                "cpp_present": int(cpp_scan["match_count"]) > 0,
                "cpp_no_static_evidence": bool(reference_sources_present) and int(cpp_scan["match_count"]) == 0,
                "sources": source_scans,
                "cpp": cpp_scan,
            }
        )
    return {
        "cpp_file_count": len(cpp_files),
        "source_file_counts": {source_id: len(files) for source_id, files in source_files.items()},
        "topic_count": len(topics),
        "cpp_no_static_evidence_count": sum(1 for topic in topics if topic["cpp_no_static_evidence"]),
        "topics": topics,
    }


def enum_signature(enum_item: dict[str, object]) -> tuple[tuple[str, object], ...]:
    members = enum_item.get("members", [])
    if not isinstance(members, list):
        return ()
    signature: list[tuple[str, object]] = []
    for member in members:
        if isinstance(member, dict):
            signature.append((str(member.get("normalized", member.get("name", ""))), member.get("value")))
    return tuple(signature)


def classify_enum_conflict(normalized_name: str) -> tuple[str, str, str]:
    return ENUM_CONFLICT_TRIAGE.get(
        normalized_name,
        ("unclassified", "review", "Needs maintainer decision before any implementation change."),
    )


def analyze_enum_conflicts(enum_result: dict[str, object]) -> list[dict[str, object]]:
    by_enum: dict[str, list[dict[str, object]]] = {}
    for item in enum_result["cpp_enums"]:
        by_enum.setdefault(str(item["normalized"]), []).append({"source": "cpp", "label": "C++", "enum": item})
    for source in enum_result["sources"]:
        for item in source["enums"]:
            by_enum.setdefault(str(item["normalized"]), []).append(
                {"source": source["source"], "label": source["label"], "enum": item}
            )

    conflicts: list[dict[str, object]] = []
    for normalized, entries in sorted(by_enum.items()):
        if len(entries) < 2:
            continue
        unique_signatures = {enum_signature(entry["enum"]) for entry in entries}
        reference_count = sum(1 for entry in entries if entry["source"] != "cpp")
        if len(unique_signatures) <= 1 or reference_count == 0:
            continue
        category, decision, note = classify_enum_conflict(normalized)
        conflicts.append(
            {
                "normalized": normalized,
                "name": entries[0]["enum"]["name"],
                "entry_count": len(entries),
                "triage_category": category,
                "triage_decision": decision,
                "triage_note": note,
                "sources": entries,
            }
        )
    return conflicts


def format_examples(item: dict[str, object]) -> str:
    examples = item.get("examples", [])
    if not isinstance(examples, list):
        return ""
    return "<br>".join(
        f"{example.get('file')}:{example.get('line')}"
        for example in examples
        if isinstance(example, dict)
    )


def write_domain_section(lines: list[str], domain_result: dict[str, object], max_rows: int) -> None:
    lines.append(f"### {domain_result['domain']}")
    lines.append("")
    lines.append(f"- Reference properties: {domain_result['property_count']}")
    lines.append(f"- Not name-matched to C++ loader/runtime/behavior names: {domain_result['cpp_missing_count']}")
    decision_counts = domain_result.get("triage_decision_counts", {})
    if isinstance(decision_counts, dict) and decision_counts:
        counts_text = ", ".join(
            f"{decision}={count}"
            for decision, count in sorted(decision_counts.items())
        )
        lines.append(f"- Triage decisions: {counts_text}")
    lines.append("")
    lines.append("| Property | Type | Decision | Category | Location | Note |")
    lines.append("| --- | --- | --- | --- | --- | --- |")
    for item in domain_result["cpp_missing"][:max_rows]:
        property_type = item.get("type", "")
        lines.append(
            f"| `{item['name']}` | `{property_type}` | {item.get('triage_decision', 'review')} | "
            f"`{item.get('triage_category', 'unclassified')}` | `{format_location(item)}` | "
            f"{item.get('triage_note', '')} |"
        )
    if len(domain_result["cpp_missing"]) > max_rows:
        lines.append(f"| ... | ... | ... | ... | ... | {len(domain_result['cpp_missing']) - max_rows} more |")
    lines.append("")


def write_enum_summary(lines: list[str], enum_result: dict[str, object], max_rows: int) -> None:
    lines.append("## Enum Inventory")
    lines.append("")
    lines.append(f"- C++ enums: {enum_result['cpp_enum_count']}")
    for source in enum_result["sources"]:
        lines.append(f"- {source['label']}: {source['enum_count']} enums, {source['cpp_missing_count']} not name-matched in C++")
    lines.append("")
    lines.append("### Reference Enums Not Name-Matched In C++")
    lines.append("")
    lines.append("| Source | Enum | Location | Members |")
    lines.append("| --- | --- | --- | --- |")
    for source in enum_result["sources"]:
        for item in source["cpp_missing"][:max_rows]:
            members = item.get("members", [])
            member_summary = ", ".join(
                f"{member['name']}={member['value']}"
                for member in members[:8]
                if isinstance(member, dict)
            )
            if isinstance(members, list) and len(members) > 8:
                member_summary += f", ... {len(members) - 8} more"
            lines.append(f"| {source['source']} | `{item['name']}` | `{format_location(item)}` | {member_summary} |")
    lines.append("")


def write_behavior_summary(lines: list[str], behavior_result: dict[str, object], max_rows: int) -> None:
    lines.append("## Behavior Topic Matrix")
    lines.append("")
    lines.append("This is keyword-based static evidence. `No C++ static evidence` means the references mention the topic but the configured C++ behavior search did not find a matching keyword; it is a backlog signal, not proof of a missing runtime feature.")
    lines.append("")
    lines.append(f"- Behavior topics: {behavior_result['topic_count']}")
    lines.append(f"- C++ scanned files: {behavior_result['cpp_file_count']}")
    lines.append(f"- Topics with reference evidence but no C++ static evidence: {behavior_result['cpp_no_static_evidence_count']}")
    lines.append("")
    lines.append("| Area | Topic | Reference Sources | C++ Evidence | First C++/Reference Evidence |")
    lines.append("| --- | --- | --- | --- | --- |")
    for topic in behavior_result["topics"]:
        reference_sources = ", ".join(topic["reference_sources_present"]) or "-"
        cpp_evidence = "yes" if topic["cpp_present"] else "no"
        evidence_text = "-"
        cpp_examples = topic["cpp"].get("examples", [])
        if isinstance(cpp_examples, list) and cpp_examples:
            example = cpp_examples[0]
            evidence_text = f"C++ `{example.get('file')}:{example.get('line')}`"
        else:
            for source_scan in topic["sources"]:
                examples = source_scan.get("examples", [])
                if isinstance(examples, list) and examples:
                    example = examples[0]
                    evidence_text = f"{source_scan['source']} `{example.get('file')}:{example.get('line')}`"
                    break
        lines.append(
            f"| {topic['area']} | `{topic['topic_id']}` {topic['label']} | "
            f"{reference_sources} | {cpp_evidence} | {evidence_text} |"
        )
    lines.append("")
    missing_topics = [topic for topic in behavior_result["topics"] if topic["cpp_no_static_evidence"]]
    if missing_topics:
        lines.append("### Topics With Reference Evidence But No C++ Static Evidence")
        lines.append("")
        lines.append("| Area | Topic | Reference Sources | Example |")
        lines.append("| --- | --- | --- | --- |")
        for topic in missing_topics[:max_rows]:
            example_text = "-"
            for source_scan in topic["sources"]:
                examples = source_scan.get("examples", [])
                if isinstance(examples, list) and examples:
                    example = examples[0]
                    example_text = f"{source_scan['source']} `{example.get('file')}:{example.get('line')}`"
                    break
            lines.append(
                f"| {topic['area']} | `{topic['topic_id']}` {topic['label']} | "
                f"{', '.join(topic['reference_sources_present'])} | {example_text} |"
            )
        if len(missing_topics) > max_rows:
            lines.append(f"| ... | ... | ... | {len(missing_topics) - max_rows} more |")
        lines.append("")


def format_enum_member_summary(enum_item: dict[str, object], max_members: int = 12) -> str:
    members = enum_item.get("members", [])
    if not isinstance(members, list):
        return ""
    member_summary = ", ".join(
        f"{member['name']}={member['value']}"
        for member in members[:max_members]
        if isinstance(member, dict)
    )
    if len(members) > max_members:
        member_summary += f", ... {len(members) - max_members} more"
    return member_summary


def write_enum_conflict_section(lines: list[str], conflicts: list[dict[str, object]], max_rows: int) -> None:
    lines.append("## Enum Conflict Candidates")
    lines.append("")
    lines.append("Conflicts are recorded for later decision. The script does not change C++ behavior for these entries.")
    lines.append("")
    lines.append(f"- Conflict candidates: {len(conflicts)}")
    lines.append("")
    lines.append("| Enum | Decision | Category | Sources | Notes |")
    lines.append("| --- | --- | --- | --- | --- |")
    for conflict in conflicts[:max_rows]:
        source_names = ", ".join(str(entry["source"]) for entry in conflict["sources"])
        notes = []
        if conflict.get("triage_note"):
            notes.append(str(conflict["triage_note"]))
        for entry in conflict["sources"][:4]:
            enum_item = entry["enum"]
            notes.append(f"{entry['source']}: {format_enum_member_summary(enum_item, 6)}")
        lines.append(
            f"| `{conflict['name']}` | {conflict.get('triage_decision', 'review')} | "
            f"`{conflict.get('triage_category', 'unclassified')}` | {source_names} | {'<br>'.join(notes)} |"
        )
    if len(conflicts) > max_rows:
        lines.append(f"| ... | ... | ... | ... | {len(conflicts) - max_rows} more |")
    lines.append("")


def write_conflict_markdown(conflicts: list[dict[str, object]], output: Path, max_rows: int) -> None:
    unresolved_conflicts = [
        conflict
        for conflict in conflicts
        if conflict.get("triage_decision") != "no_action"
    ]
    lines: list[str] = [
        "# MOD Reference Source Conflict Candidates",
        "",
        "This document records reference-source conflicts found by `scripts/analyze_reference_sources.py`. It is intentionally conservative: conflicts are not implementation tasks until a maintainer decides which source should win.",
        "",
    ]
    write_enum_conflict_section(lines, unresolved_conflicts, max_rows)
    while lines and lines[-1] == "":
        lines.pop()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_markdown(report: dict[str, object], output: Path, max_rows: int) -> None:
    lines: list[str] = [
        "# MOD Reference Source Capability Inventory",
        "",
        "This is a static inventory, not a behavior sign-off. A C++ name match means the command/property is registered, loaded, exposed through runtime state, or explicitly covered by static behavior evidence; it does not prove identical behavior.",
        "",
        "Resource-reference misses in converted community MODs are report items by default. Do not treat missing media/image/object references as mandatory asset-fix work unless they break profile entry points, dependency resolution, save isolation, or a separately verified runtime path.",
        "",
        "## Script Commands",
        "",
    ]
    script_result = report["scripts"]
    lines.append(f"- C++ registered Lua commands: {script_result['cpp_registered_count']}")
    lines.append(f"- Asset script calls: {script_result['asset_call_count']}")
    lines.append(f"- Reference command union: {script_result['union_count']}")
    lines.append(f"- Reference non-control commands missing in C++: {len(script_result['union_cpp_missing'])}")
    lines.append(f"- Missing in C++ and used by current assets: {len(script_result['union_cpp_missing_used_by_assets'])}")
    lines.append("")
    lines.append("| Source | Commands | Non-Control Missing In C++ | Missing In C++ And Used By Assets |")
    lines.append("| --- | ---: | ---: | ---: |")
    for source in script_result["sources"]:
        lines.append(
            f"| {source['label']} | {source['command_count']} | "
            f"{source['cpp_missing_count']} | {source['cpp_missing_used_by_assets_count']} |"
        )
    lines.append("")
    lines.append("### Reference Non-Control Commands Missing In C++")
    lines.append("")
    lines.append("| Command | Sources | Asset Used | Asset Examples |")
    lines.append("| --- | --- | --- | --- |")
    for item in script_result["union_cpp_missing"][:max_rows]:
        lines.append(
            f"| `{item['name']}` | {', '.join(item['sources'])} | "
            f"{'yes' if item['asset_used'] else 'no'} | {format_examples(item)} |"
        )
    if len(script_result["union_cpp_missing"]) > max_rows:
        lines.append(f"| ... | ... | ... | {len(script_result['union_cpp_missing']) - max_rows} more |")
    lines.append("")

    lines.append("## Domain Properties")
    lines.append("")
    for source in report["domains"]:
        lines.append(f"## {source['label']}")
        lines.append("")
        for domain_result in source["domains"]:
            write_domain_section(lines, domain_result, max_rows)

    write_enum_summary(lines, report["enums"], max_rows)
    write_enum_conflict_section(lines, report["enum_conflicts"], max_rows)
    write_behavior_summary(lines, report["behavior_topics"], max_rows)

    while lines and lines[-1] == "":
        lines.pop()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets-root", type=Path, default=REPO_ROOT / "assets")
    parser.add_argument("--json", type=Path, help="Write JSON report.")
    parser.add_argument("--markdown", type=Path, help="Write Markdown report.")
    parser.add_argument("--conflicts-markdown", type=Path, help="Write reference-source conflict candidates.")
    parser.add_argument("--max-rows", type=int, default=80)
    parser.add_argument("--max-examples", type=int, default=5)
    args = parser.parse_args()

    enum_result = analyze_enums()
    report = {
        "scripts": analyze_scripts(args.assets_root.resolve(), args.max_examples),
        "domains": analyze_domains(),
        "enums": enum_result,
        "enum_conflicts": analyze_enum_conflicts(enum_result),
        "behavior_topics": analyze_behavior_topics(args.max_examples),
    }

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.markdown:
        write_markdown(report, args.markdown, args.max_rows)
    if args.conflicts_markdown:
        write_conflict_markdown(report["enum_conflicts"], args.conflicts_markdown, args.max_rows)
    if not args.json and not args.markdown and not args.conflicts_markdown:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
