#!/usr/bin/env python3
"""Scaffold a profile-compliant MOD test pack.

The default mode writes to tmp/ so the tool can be validated without mutating
the assets repository. Use --install when the generated test pack is ready to
become an assets change; installed packs are discovered from game_profile.ini.
"""

from __future__ import annotations

import argparse
import configparser
import contextlib
import os
import re
import shutil
import stat
import sys
import tempfile
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PACK_ID = "XJXQY_TEST_MOD"
DEFAULT_PACK_PATH = "xjxqy_test_mod"
DEFAULT_BASE_ID = "XJXQY"
DEFAULT_AUTHOR = ""
DEFAULT_TITLE_MENU = r"ini\ui\title\title.menu.ini"
DEFAULT_NEW_GAME_SCRIPT = "mod_test_newgame.txt"
PORTABLE_PATH_FORBIDDEN_CHARACTERS = set('<>:"|?*\0')

REQUIRED_PROFILE_FIELDS = (
    ("Game", "Id"),
    ("Game", "Name"),
    ("Game", "Type"),
    ("Resource", "DependencyId"),
    ("Save", "Namespace"),
    ("Title", "Menu"),
    ("NewGame", "Script"),
)

LEGACY_GENERATED_FILES = (
    "script/common/newgame.txt",
    "script/common/mod_test_video_focus_pause.txt",
    "test_scenarios.ini",
)


@dataclass(frozen=True)
class BasePack:
    pack_id: str
    path: str
    root: Path
    profile: configparser.ConfigParser


def read_ini(path: Path) -> configparser.ConfigParser:
    # Runtime INIReader treats keys case-insensitively and keeps the last value.
    # Preserve that compatibility for collection configuration and manifests.
    parser = configparser.ConfigParser(
        interpolation=None,
        strict=False,
        default_section="__JXQY_CONFIGPARSER_DEFAULTS_DISABLED__",
        inline_comment_prefixes=(";",),
    )
    parser.optionxform = str
    with path.open("r", encoding="utf-8-sig") as file:
        parser.read_file(file)
    return parser


def get_value(parser: configparser.ConfigParser, section: str, key: str, default: str = "") -> str:
    result = default
    for actual_section in parser.sections():
        if actual_section.casefold() != section.casefold():
            continue
        for candidate, value in parser.items(actual_section, raw=True):
            if candidate.casefold() == key.casefold():
                result = value.strip()
    return result


def validate_ini_scalar(value: str, label: str, allow_empty: bool = False) -> str:
    value = value.strip()
    if not value and not allow_empty:
        raise ValueError(f"{label} must not be empty")
    if any(character in {"\0", "\r", "\n"} for character in value):
        raise ValueError(f"{label} must be a single INI value")
    if re.search(r"(^|\s);", value):
        raise ValueError(f"{label} contains an INI inline-comment sequence")
    return value


def validate_pack_id(value: str, label: str = "pack id") -> str:
    value = validate_ini_scalar(value, label)
    if any(character in {"[", "]", ","} for character in value):
        raise ValueError(f"{label} contains a character that is unsafe in an INI section or dependency list")
    return value


def is_link_or_reparse_point(path: Path) -> bool:
    try:
        path_stat = path.lstat()
    except FileNotFoundError:
        return False
    is_junction = getattr(path, "is_junction", lambda: False)()
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    file_attributes = getattr(path_stat, "st_file_attributes", 0)
    return stat.S_ISLNK(path_stat.st_mode) or is_junction or bool(file_attributes & reparse_attribute)


def ensure_tree_has_no_links(root: Path, label: str) -> None:
    if is_link_or_reparse_point(root):
        raise ValueError(f"{label} is a symlink, junction, or reparse point: {root}")
    if not root.exists():
        return
    if not root.is_dir():
        raise ValueError(f"{label} is not a directory: {root}")
    for directory, directory_names, file_names in os.walk(root, followlinks=False):
        for name in [*directory_names, *file_names]:
            path = Path(directory) / name
            if is_link_or_reparse_point(path):
                raise ValueError(f"{label} contains a symlink, junction, or reparse point: {path}")


def resolve_contained_path(
    root: Path,
    relative_value: str | Path,
    label: str,
    *,
    reject_links: bool = False,
) -> Path:
    root = root.resolve()
    raw_value = str(relative_value).strip().replace("\\", "/")
    if (not raw_value or raw_value.startswith("/") or
            (len(raw_value) >= 2 and raw_value[0].isalpha() and raw_value[1] == ":")):
        raise ValueError(f"{label} must be a relative path inside {root}: {relative_value!r}")

    parts: list[str] = []
    for part in raw_value.split("/"):
        if not part or part == ".":
            continue
        if part == "..":
            raise ValueError(f"{label} must not contain '..': {relative_value!r}")
        if any(character in PORTABLE_PATH_FORBIDDEN_CHARACTERS or ord(character) < 32
               for character in part):
            raise ValueError(f"{label} contains a non-portable path component: {relative_value!r}")
        parts.append(part)
    if not parts:
        raise ValueError(f"{label} must identify a path inside {root}")

    lexical_candidate = root.joinpath(*parts)
    if reject_links:
        current = root
        for part in parts:
            current /= part
            if is_link_or_reparse_point(current):
                raise ValueError(
                    f"{label} contains a symlink, junction, or reparse point: {current}"
                )
    candidate = lexical_candidate.resolve()
    try:
        candidate.relative_to(root)
    except ValueError as exc:
        raise ValueError(f"{label} escapes {root}: {relative_value!r}") from exc
    return candidate


def normalize_pack_path(value: str) -> str:
    normalized = value.strip().replace("\\", "/")
    if re.search(r"(^|\s);", normalized):
        raise ValueError("pack path contains an INI inline-comment sequence")
    if (normalized.startswith("/") or
            (len(normalized) >= 2 and normalized[0].isalpha() and normalized[1] == ":")):
        raise ValueError("pack path must be a relative path inside assets")
    normalized = normalized.strip("/")
    parts = [part for part in normalized.split("/") if part and part != "."]
    if (not parts or any(part == ".." for part in parts) or
            any(any(character in PORTABLE_PATH_FORBIDDEN_CHARACTERS or ord(character) < 32
                    for character in part) for part in parts)):
        raise ValueError("pack path must be a relative path inside assets")
    return "/".join(parts)


def parse_dependency_ids(value: str) -> list[str]:
    result: list[str] = []
    seen: set[str] = set()
    for part in value.split(","):
        dependency_id = part.strip()
        if dependency_id:
            validate_pack_id(dependency_id, "base id")
        normalized_id = dependency_id.lower()
        if dependency_id and normalized_id not in seen:
            seen.add(normalized_id)
            result.append(dependency_id)
    if not result:
        raise ValueError("at least one base id is required")
    return result


def discovered_resource_packs(assets_root: Path) -> list[BasePack]:
    assets_root = assets_root.resolve()
    candidate_roots: list[Path] = []
    if (assets_root / "game_profile.ini").is_file():
        candidate_roots.append(assets_root)
    for child in sorted(
        assets_root.iterdir(),
        key=lambda path: (path.name.casefold(), path.name),
    ):
        if child.name.startswith(".") or is_link_or_reparse_point(child):
            continue
        if child.is_dir() and (child / "game_profile.ini").is_file():
            candidate_roots.append(child.resolve())

    result: list[BasePack] = []
    owners: dict[str, Path] = {}
    for root in candidate_roots:
        profile_path = root / "game_profile.ini"
        profile = read_ini(profile_path)
        pack_id = validate_pack_id(
            get_value(profile, "Game", "Id"),
            f"{profile_path}.Game.Id",
        )
        normalized_id = pack_id.casefold()
        if normalized_id in owners:
            raise ValueError(
                f"duplicate Game.Id {pack_id!r} in {owners[normalized_id]} and {root}"
            )
        owners[normalized_id] = root
        path = "." if root == assets_root else root.relative_to(assets_root).as_posix()
        result.append(BasePack(pack_id=pack_id, path=path, root=root, profile=profile))
    return result


def discover_base_pack(assets_root: Path, base_id: str) -> BasePack:
    for pack in discovered_resource_packs(assets_root):
        if pack.pack_id.casefold() == base_id.casefold():
            return pack
    raise ValueError(
        f"base pack {base_id} was not discovered from a direct-child game_profile.ini"
    )


def discovered_pack_roots(assets_root: Path) -> list[tuple[str, Path]]:
    return [(pack.pack_id, pack.root) for pack in discovered_resource_packs(assets_root)]


def collection_common_root(assets_root: Path) -> Path | None:
    resources_path = resolve_contained_path(
        assets_root, "resources.ini", "resource collection configuration"
    )
    if not resources_path.exists():
        return None
    resources = read_ini(resources_path)
    common_path = get_value(resources, "Collection", "CommonPath")
    if not common_path:
        return None
    normalized_path = normalize_pack_path(common_path)
    return resolve_contained_path(assets_root, normalized_path, "Collection.CommonPath")


def to_lower_ascii(value: str) -> str:
    return "".join(chr(ord(character) + 32) if "A" <= character <= "Z" else character for character in value)


def sanitize_save_namespace(value: str) -> str:
    result: list[str] = []
    for character in value.replace("\\", "/"):
        codepoint = ord(character)
        if (codepoint >= 0x80 or "a" <= character <= "z" or "A" <= character <= "Z" or
                "0" <= character <= "9" or character in {"-", "_"}):
            result.append(character)
        elif character in {"/", ":", "."}:
            result.append("_")
    return "".join(result) or "default"


def discovered_save_namespaces(assets_root: Path) -> list[tuple[str, Path, str]]:
    result: list[tuple[str, Path, str]] = []
    for pack in discovered_resource_packs(assets_root):
        save_namespace = (
            get_value(pack.profile, "Save", "Namespace") or
            pack.pack_id or
            pack.root.name
        )
        result.append(
            (
                pack.pack_id,
                pack.root,
                to_lower_ascii(sanitize_save_namespace(save_namespace)),
            )
        )
    return result


def pack_lock_path(pack_root: Path) -> Path:
    return pack_root.parent / f".{pack_root.name}.jxqy-pack.lock"


@contextlib.contextmanager
def exclusive_file_lock(lock_path: Path):
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    if is_link_or_reparse_point(lock_path):
        raise ValueError(f"refusing to use a linked lock path: {lock_path}")
    try:
        descriptor = os.open(lock_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError as exc:
        try:
            owner = lock_path.read_text(encoding="ascii", errors="replace").strip()
        except OSError:
            owner = "owner metadata unavailable"
        raise ValueError(
            f"another process may be using this scaffold target; lock exists: {lock_path} "
            f"({owner}). Remove it only after confirming that process is no longer running"
        ) from exc
    try:
        os.write(descriptor, f"pid={os.getpid()} started_ns={time.time_ns()}\n".encode("ascii"))
        os.close(descriptor)
        descriptor = -1
        yield
    finally:
        if descriptor >= 0:
            os.close(descriptor)
        try:
            lock_path.unlink()
        except FileNotFoundError:
            pass


def paths_overlap(left: Path, right: Path) -> bool:
    left = left.resolve()
    right = right.resolve()
    try:
        left.relative_to(right)
        return True
    except ValueError:
        pass
    try:
        right.relative_to(left)
        return True
    except ValueError:
        return False


def write_text(path: Path, text: str, force: bool, written: list[Path]) -> None:
    if path.exists() and not force:
        raise FileExistsError(f"{path} already exists; pass --force to overwrite generated files")
    path.parent.mkdir(parents=True, exist_ok=True)
    atomic_write_text(path, text)
    written.append(path)


def atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="\n",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as temporary_file:
            temporary_file.write(text)
            temporary_path = Path(temporary_file.name)
        os.replace(temporary_path, path)
        temporary_path = None
    finally:
        if temporary_path is not None:
            try:
                temporary_path.unlink()
            except FileNotFoundError:
                pass


def make_profile(args: argparse.Namespace, base_pack: BasePack, pack_path: str) -> str:
    title_menu = validate_ini_scalar(
        get_value(base_pack.profile, "Title", "Menu", DEFAULT_TITLE_MENU),
        "base profile Title.Menu",
    )
    game_type = str(args.game_type)
    if args.game_type is None:
        game_type = get_value(base_pack.profile, "Game", "Type", "2" if base_pack.pack_id.upper() == "XJXQY" else "99")
    minimum_magic_damage = validate_ini_scalar(
        get_value(base_pack.profile, "Combat", "MinimumMagicDamage", ""),
        "base profile Combat.MinimumMagicDamage",
    )

    lines = [
        "[Game]",
        f"Id={args.pack_id}",
        f"Name={args.name}",
    ]
    if args.author:
        lines.append(f"Author={args.author}")
    lines.extend([
        f"Type={game_type}",
        "UseWav=0",
    ])
    if minimum_magic_damage:
        lines.extend([
            "",
            "[Combat]",
            f"MinimumMagicDamage={minimum_magic_damage}",
        ])
    lines.extend([
        "",
        "[Resource]",
        f"DependencyId={args.base_id}",
        "TextEncodingConverted=1",
        "",
        "[Save]",
        f"Namespace={args.save_namespace}",
        "",
        "[Startup]",
        "Videos=",
        "",
        "[Title]",
        f"Menu={title_menu}",
        "Music=",
        "TeamVideo=",
        "",
        "[NewGame]",
        f"Script={DEFAULT_NEW_GAME_SCRIPT}",
        "",
    ])
    return "\n".join(lines)


def make_newgame_script() -> str:
    return "\n".join(
        [
            'displaymessage("正在启动综合测试 MOD");',
            'assign("mod_test_newgame", 1);',
            'runscript("mod_test_runner.txt");',
            "return;",
            "",
        ]
    )


def make_runner_script() -> str:
    scenarios = (
        (1, "Bootstrap", "交互测试广场", "mod_test_bootstrap.txt"),
        (2, "Gamble", "赌博测试", "mod_test_gamble.txt"),
        (3, "DiceGame", "掷骰测试", "mod_test_dice_game.txt"),
        (4, "FishGame", "钓鱼测试", "mod_test_fish_game.txt"),
        (5, "Steal", "偷窃测试", "mod_test_steal.txt"),
        (6, "MagicLifecycle", "武功生命周期", "mod_test_magic_lifecycle.txt"),
        (7, "MagicCollision", "武功碰撞", "mod_test_magic_collision.txt"),
        (8, "EquipmentTrigger", "装备触发", "mod_test_equipment_trigger.txt"),
        (9, "GoodsPricing", "物品定价", "mod_test_goods_pricing.txt"),
        (10, "GoodsRandom", "随机物品", "mod_test_goods_random.txt"),
        (11, "GoodsLifecycle", "物品生命周期", "mod_test_goods_lifecycle.txt"),
        (12, "NpcAI", "NPC 智能", "mod_test_npc_ai.txt"),
        (13, "ObjectState", "对象状态", "mod_test_object_state.txt"),
        (14, "PlayerControlState", "玩家控制状态", "mod_test_player_control_state.txt"),
        (15, "ObjectTrapDamage", "对象陷阱伤害", "mod_test_object_trap_damage.txt"),
        (16, "MagicTransportControl", "武功传送与控制", "mod_test_magic_transport_control.txt"),
        (17, "ChooseMultiple", "多项选择", "mod_test_choose_multiple.txt"),
        (18, "MagicRegionVType", "V 型区域武功", "mod_test_magic_region_vtype.txt"),
        (19, "ScriptTimerParallel", "时间脚本与并行脚本", "mod_test_script_timer_parallel.txt"),
        (20, "MagicChangeHit", "命中变化武功", "mod_test_magic_change_hit.txt"),
        (21, "MagicPostCast", "施法后续武功", "mod_test_magic_post_cast.txt"),
        (22, "ScriptReturnApi", "脚本返回值接口", "mod_test_script_return_api.txt"),
        (23, "MagicSummonBody", "召唤与尸体武功", "mod_test_magic_summon_body.txt"),
        (24, "ChooseExPlus", "扩展选择框", "mod_test_choose_ex_plus.txt"),
        (25, "NpcKindTalent", "NPC 类型与天赋", "mod_test_npc_kind_talent.txt"),
        (26, "NpcSignalTip", "NPC 信号提示", "mod_test_npc_signal_tip.txt"),
        (27, "ScriptSoundPosition", "脚本声音位置", "mod_test_script_sound_position.txt"),
        (28, "MagicSelfSpecial", "自身特殊武功", "mod_test_magic_self_special.txt"),
        (29, "MagicTrail", "武功轨迹", "mod_test_magic_trail.txt"),
        (30, "MagicTempRelation", "临时阵营关系", "mod_test_magic_temp_relation.txt"),
        (31, "NpcDrop", "NPC 掉落", "mod_test_npc_drop.txt"),
        (32, "TimeStopVisualRepro", "时间停止画面复现", "mod_test_time_stop_visual_repro.txt"),
        (33, "ChooseMenuVisual", "选择框画面测试", "mod_test_choose_menu_visual.txt"),
        (34, "MagicExplode", "爆炸子武功", "mod_test_magic_explode.txt"),
        (35, "EnvironmentWeather", "天气与水面效果", "mod_test_environment_weather.txt"),
        (36, "AnimationParametersVisual", "动画参数画面测试", "mod_test_animation_parameters_visual.txt"),
        (37, "VideoBackgroundContinuity", "视频与游戏后台继续", "mod_test_video_background_continuity.txt"),
        (38, "MagicCriticalFeedback", "MG 暴击反馈", "mod_test_magic_critical_feedback.txt"),
        (39, "MagicDetachedCasterVisual", "施法者离场弹体", "mod_test_magic_detached_caster_visual.txt"),
        (40, "ManualMagicArena", "手动武功训练场", "mod_test_manual_magic_arena.txt"),
        (41, "ManualContinuity", "手动闭环验证", "mod_test_manual_continuity.txt"),
    )
    menu_arguments = [*(title for _, _, title, _ in scenarios), "进入新剑原版开局"]
    menu_call = (
        'chooseex("测试场景向导", '
        + ", ".join(f'"{argument}"' for argument in menu_arguments)
        + ', "mod_test_manual_menu_choice");'
    )
    lines = [
        'displaymessage("正在打开测试场景向导；测试广场位于第一项，原版开局位于最后一项");',
        'assign("mod_test_runner", 1);',
        'if getvar("mod_test_auto_scenario_enabled") == 1 then goto AutoScenario end',
        'if getvar("mod_test_auto_scenario_choice") > 0 then goto AutoScenario end',
        "::ManualMenu::",
        'assign("mod_test_manual_menu_choice", -1);',
        menu_call,
        'if getvar("mod_test_manual_menu_choice") < 0 then goto End end',
        f'if getvar("mod_test_manual_menu_choice") == {len(scenarios)} then goto BaseGame end',
    ]
    lines.extend(
        f'if getvar("mod_test_manual_menu_choice") == {choice - 1} then assign("mod_test_scenario_choice", {choice}); goto {label} end'
        for choice, label, _, _ in scenarios
    )
    lines.extend(
        [
            "goto ManualMenu",
            "::AutoScenario::",
            'assign("mod_test_scenario_choice", getvar("mod_test_auto_scenario_choice"));',
            'assign("mod_test_auto_scenario_used", getvar("mod_test_auto_scenario_choice"));',
            'assign("mod_test_auto_scenario_enabled", 0);',
            'assign("mod_test_auto_scenario_choice", 0);',
            'if getvar("mod_test_scenario_choice") == 0 then goto BaseGame end',
            'assign("mod_test_skip_base_newgame", 1);',
        ]
    )
    lines.extend(
        f'if getvar("mod_test_scenario_choice") == {choice} then goto Auto{label} end'
        for choice, label, _, _ in scenarios
    )
    lines.append("goto End")
    for choice, label, _, script_name in scenarios:
        if label == "Steal":
            lines.extend(
                [
                    "::Steal::",
                    'assign("mod_test_steal_interactive", 1);',
                    f'runscript("{script_name}");',
                    'assign("mod_test_steal_interactive", 0);',
                    "goto ManualMenu",
                ]
            )
            continue
        lines.extend(
            [
                f"::{label}::",
                f'runscript("{script_name}");',
                "goto End" if choice == 1 else "goto ManualMenu",
            ]
        )
    for _, label, _, script_name in scenarios:
        if label == "Steal":
            lines.extend(
                [
                    "::AutoSteal::",
                    'assign("mod_test_steal_interactive", 0);',
                    f'runscript("{script_name}");',
                    "goto End",
                ]
            )
            continue
        lines.extend(
            [
                f"::Auto{label}::",
                f'runscript("{script_name}");',
                "goto End",
            ]
        )
    lines.extend(
        [
            "::BaseGame::",
            'assign("mod_test_base_newgame_requested", 1);',
            'runscript("newgame.txt");',
            "goto End",
            "::End::",
            "return;",
            "",
        ]
    )
    return "\n".join(lines)


def make_bootstrap_script() -> str:
    return "\n".join(
        [
            'displaymessage("正在进入交互测试广场");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'loadmap("map001_衡山.map");',
            'loadobj("map001.obj");',
            'setmaptrap(1, "");',
            'setmaptrap(2, "");',
            'setmaptrap(3, "");',
            "disablenpcai();",
            "cleareffect();",
            "setplayerpos(34,22);",
            "setplayerdir(2);",
            "centercamera();",
            "fulllife();",
            "fullmana();",
            "fullthew();",
            'addnpc("mod_test_hub_guide.ini", 33, 20, 0);',
            'addnpc("mod_test_hub_little_games_host.ini", 35, 20, 0);',
            'assign("mod_test_hub_respawn_count", 0);',
            'runscript("mod_test_hub_respawn.txt");',
            'getnpcstate("试炼剑客", "IsPartner", "mod_test_hub_partner_sword_ready");',
            'getnpcstate("试炼女侠", "IsPartner", "mod_test_hub_partner_heroine_ready");',
            'getnpcstate("试炼剑客", "AttackSpeed", "mod_test_hub_partner_sword_attack_speed");',
            'getnpcstate("试炼女侠", "AttackSpeed", "mod_test_hub_partner_heroine_attack_speed");',
            'getnpcstate("试炼剑客", "MapX", "mod_test_hub_partner_sword_x");',
            'getnpcstate("试炼剑客", "MapY", "mod_test_hub_partner_sword_y");',
            'getnpcstate("试炼女侠", "MapX", "mod_test_hub_partner_heroine_x");',
            'getnpcstate("试炼女侠", "MapY", "mod_test_hub_partner_heroine_y");',
            'getnpcstate("演武刀客", "IsEnemy", "mod_test_hub_enemy_melee_ready");',
            'getnpcstate("演武刀客", "AttackSpeed", "mod_test_hub_enemy_melee_attack_speed");',
            'getnpcstate("演武刀客", "NoAutoAttackPlayer", "mod_test_hub_enemy_melee_no_auto");',
            'getnpcstate("演武刀客", "StopFindingTarget", "mod_test_hub_enemy_melee_stop_find");',
            'getnpcstate("演武弓手", "IsEnemy", "mod_test_hub_enemy_ranged_ready");',
            'getnpcstate("演武弓手", "AttackSpeed", "mod_test_hub_enemy_ranged_attack_speed");',
            'getnpcstate("演武弓手", "NoAutoAttackPlayer", "mod_test_hub_enemy_ranged_no_auto");',
            'getnpcstate("演武弓手", "StopFindingTarget", "mod_test_hub_enemy_ranged_stop_find");',
            'assign("mod_test_ready", 1);',
            'displaymessage("测试广场已就绪：点击测试向导或小游戏掌柜交谈");',
            "return;",
            "",
        ]
    )


def make_test_hub_respawn_script() -> str:
    return "\n".join(
        [
            "cleareffect();",
            'delnpc("试炼剑客");',
            'delnpc("试炼女侠");',
            'delnpc("演武刀客");',
            'delnpc("演武弓手");',
            'addnpc("mod_test_hub_partner_sword.ini", 31, 22, 0);',
            'addnpc("mod_test_hub_partner_heroine.ini", 37, 22, 0);',
            'setnpcpartner("试炼剑客");',
            'setnpcpartner("试炼女侠");',
            'setnpcpos("试炼剑客", 33, 22);',
            'setnpcpos("试炼女侠", 35, 22);',
            "setpartnerlevel(5);",
            'addnpc("mod_test_hub_enemy_melee.ini", 31, 14, 4);',
            'addnpc("mod_test_hub_enemy_ranged.ini", 37, 14, 4);',
            "enablepartnercombat();",
            "enablenpcai();",
            'assign("mod_test_hub_respawn_count", getvar("mod_test_hub_respawn_count") + 1);',
            'displaymessage("伙伴和主动敌人已重置：向北靠近敌人即可开战");',
            "return;",
            "",
        ]
    )


def make_test_hub_guide_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_hub_guide_choice", -1);',
            'chooseplus("测试向导", 2, 0, "已进入综合测试 MOD 的交互测试广场。地图和人物素材继承自新剑侠情缘，但这里不会继续原版剧情；身边有两名测试伙伴，北侧有两名主动敌人。", "打开小游戏菜单", "打开专项测试场景", "重置伙伴和敌人", "进入武功训练场", "查看操作说明", "离开", "mod_test_hub_guide_choice");',
            'if getvar("mod_test_hub_guide_choice") == 0 then goto LittleGames end',
            'if getvar("mod_test_hub_guide_choice") == 1 then goto Scenarios end',
            'if getvar("mod_test_hub_guide_choice") == 2 then goto Respawn end',
            'if getvar("mod_test_hub_guide_choice") == 3 then goto MagicArena end',
            'if getvar("mod_test_hub_guide_choice") == 4 then goto Help end',
            "goto End",
            "::LittleGames::",
            'runscript("mod_test_hub_little_games.txt");',
            "goto End",
            "::Scenarios::",
            'runscript("mod_test_runner.txt");',
            "goto End",
            "::Respawn::",
            'runscript("mod_test_hub_respawn.txt");',
            "goto End",
            "::MagicArena::",
            'runscript("mod_test_manual_magic_arena.txt");',
            "goto End",
            "::Help::",
            'assign("mod_test_hub_help_choice", -1);',
            'chooseplus("#name", 2, 0, "与小游戏掌柜交谈可手动打开赌博、掷骰和钓鱼界面。<enter>向北接近敌人会触发战斗，两位伙伴会在开启伙伴战斗后协助。<enter>测试向导可以打开全部专项场景；每项结束后会重新显示场景菜单，按 Esc 可返回当前场景。", "知道了", "mod_test_hub_help_choice");',
            "goto End",
            "::End::",
            "return;",
            "",
        ]
    )


def make_test_hub_little_games_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_hub_little_game_choice", -1);',
            'chooseplus("小游戏掌柜", 2, 0, "请选择要手动操作的小游戏；这些入口不会启用自动点击。", "赌博", "掷骰子", "钓鱼", "离开", "mod_test_hub_little_game_choice");',
            'if getvar("mod_test_hub_little_game_choice") == 0 then goto Gamble end',
            'if getvar("mod_test_hub_little_game_choice") == 1 then goto Dice end',
            'if getvar("mod_test_hub_little_game_choice") == 2 then goto Fish end',
            "goto End",
            "::Gamble::",
            "setmoneynum(1000);",
            'assign("__automation_gamble_enabled", 0);',
            'assign("__automation_gamble_action", -1);',
            'gamble(80, 0, "mod_test_hub_gamble_result");',
            'assign("mod_test_hub_gamble_open_count", getvar("mod_test_hub_gamble_open_count") + 1);',
            'displaymessage("赌博测试已结束，可再次与掌柜交谈");',
            "goto End",
            "::Dice::",
            'showdicegame("测试广场");',
            'assign("mod_test_hub_dice_open_count", getvar("mod_test_hub_dice_open_count") + 1);',
            'displaymessage("掷骰测试已结束，可再次与掌柜交谈");',
            "goto End",
            "::Fish::",
            "showfishgame();",
            'assign("mod_test_hub_fish_open_count", getvar("mod_test_hub_fish_open_count") + 1);',
            'displaymessage("钓鱼测试已结束，可再次与掌柜交谈");',
            "goto End",
            "::End::",
            "return;",
            "",
        ]
    )


def make_gamble_script() -> str:
    return "\n".join(
        [
            'displaymessage("赌博测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            "setmoneynum(1000);",
            'assign("__automation_gamble_enabled", 1);',
            'assign("__automation_gamble_action", 1);',
            'assign("__automation_gamble_player_stake", 0);',
            'assign("__automation_gamble_current_bet", 80);',
            'assign("__automation_gamble_round_win", 0);',
            'gamble(80, 0, "mod_test_gamble_loss_result");',
            'assign("mod_test_gamble_loss_money", getmoney());',
            'assign("mod_test_gamble_loss_delta", getvar("__automation_gamble_money_delta"));',
            'setmoneynum(1000);',
            'assign("__automation_gamble_enabled", 1);',
            'assign("__automation_gamble_action", 0);',
            'gamble(80, 0, "mod_test_gamble_leave_result");',
            'assign("mod_test_gamble_leave_money", getmoney());',
            'assign("mod_test_gamble_leave_delta", getvar("__automation_gamble_money_delta"));',
            'assign("mod_test_gamble_automation_consumed", getvar("__automation_gamble_consumed"));',
            'assign("mod_test_gamble_ready", 1);',
            'displaymessage("赌博测试完成");',
            "return;",
            "",
        ]
    )


def make_dice_script() -> str:
    return "\n".join(
        [
            'displaymessage("掷骰测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'showdicegame("掷骰测试");',
            'assign("mod_test_dice_opened", 1);',
            'displaymessage("掷骰测试完成");',
            "return;",
            "",
        ]
    )


def make_fish_script() -> str:
    return "\n".join(
        [
            'displaymessage("钓鱼测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            "showfishgame();",
            'assign("mod_test_fish_opened", 1);',
            'displaymessage("钓鱼测试完成");',
            "return;",
            "",
        ]
    )


def make_choose_multiple_script() -> str:
    return "\n".join(
        [
            'displaymessage("多项选择测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_choose_multiple_ready", 1);',
            'assign("mod_test_choose_multiple_show_beta", 1);',
            'assign("$mod_test_choose_multiple0", -1);',
            'assign("$mod_test_choose_multiple1", -1);',
            'assign("$mod_test_choose_multiple2", 99);',
            'assign("__automation_choose_multiple_enabled", 1);',
            'assign("__automation_choose_multiple_count", 3);',
            'assign("__automation_choose_multiple_selection0", 3);',
            'assign("__automation_choose_multiple_selection1", 1);',
            'assign("__automation_choose_multiple_selection2", 2);',
            'choosemultiple(2, 2, "mod_test_choose_multiple", "多项选择测试", "甲选项", "{mod_test_choose_multiple_show_beta}乙选项", "丙选项", "{0}隐藏选项");',
            'assign("mod_test_choose_multiple_result_ok", 0);',
            'if getvar("$mod_test_choose_multiple0") == 1 and getvar("$mod_test_choose_multiple1") == 2 then assign("mod_test_choose_multiple_result_ok", 1) end',
            'assign("mod_test_choose_multiple_hidden_ignored", 0);',
            'if getvar("__automation_choose_multiple_valid_count") == 2 and getvar("__automation_choose_multiple_complete") == 1 then assign("mod_test_choose_multiple_hidden_ignored", 1) end',
            'assign("mod_test_choose_multiple_no_extra_write", 0);',
            'if getvar("$mod_test_choose_multiple2") == 99 then assign("mod_test_choose_multiple_no_extra_write", 1) end',
            'assign("$mod_test_choose_multiple_dupe0", -1);',
            'assign("$mod_test_choose_multiple_dupe1", -1);',
            'assign("$mod_test_choose_multiple_dupe2", 99);',
            'assign("__automation_choose_multiple_enabled", 1);',
            'assign("__automation_choose_multiple_count", 3);',
            'assign("__automation_choose_multiple_selection0", 1);',
            'assign("__automation_choose_multiple_selection1", 1);',
            'assign("__automation_choose_multiple_selection2", 2);',
            'choosemultiple(2, 2, "mod_test_choose_multiple_dupe", "多项选择去重测试", "甲选项", "乙选项", "丙选项");',
            'assign("mod_test_choose_multiple_duplicate_ignored", 0);',
            'if getvar("$mod_test_choose_multiple_dupe0") == 1 and getvar("$mod_test_choose_multiple_dupe1") == 2 and getvar("__automation_choose_multiple_valid_count") == 2 and getvar("__automation_choose_multiple_complete") == 1 then assign("mod_test_choose_multiple_duplicate_ignored", 1) end',
            'assign("mod_test_choose_multiple_duplicate_no_extra_write", 0);',
            'if getvar("$mod_test_choose_multiple_dupe2") == 99 then assign("mod_test_choose_multiple_duplicate_no_extra_write", 1) end',
            'displaymessage("多项选择测试完成");',
            "return;",
            "",
        ]
    )


def make_choose_ex_plus_script() -> str:
    return "\n".join(
        [
            'displaymessage("扩展选择框测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_choose_ex_plus_ready", 1);',
            'assign("mod_test_choose_ex_show_beta", 1);',
            'assign("mod_test_choose_ex_result", -1);',
            'assign("__automation_choose_consumed", 0);',
            'assign("__automation_choose_complete", 0);',
            'assign("__automation_choose_visible_count", 0);',
            'assign("__automation_choose_enabled", 1);',
            'assign("__automation_choose_selection", 3);',
            'chooseex("扩展选择框测试", "{0}隐藏选项", "", "甲选项", "{mod_test_choose_ex_show_beta}乙选项", "丙选项", "mod_test_choose_ex_result");',
            'assign("mod_test_choose_ex_result_ok", 0);',
            'if getvar("mod_test_choose_ex_result") == 3 then assign("mod_test_choose_ex_result_ok", 1) end',
            'assign("mod_test_choose_ex_filter_ok", 0);',
            'if getvar("__automation_choose_consumed") == 1 and getvar("__automation_choose_visible_count") == 3 and getvar("__automation_choose_complete") == 1 then assign("mod_test_choose_ex_filter_ok", 1) end',
            'assign("mod_test_choose_plus_result", -1);',
            'assign("__automation_choose_consumed", 0);',
            'assign("__automation_choose_complete", 0);',
            'assign("__automation_choose_visible_count", 0);',
            'assign("__automation_choose_enabled", 1);',
            'assign("__automation_choose_selection", 2);',
            'chooseplus("#name", 2, 0, "增强选择框测试", "{0}隐藏选项", "", "目标选项", "其他选项", "mod_test_choose_plus_result");',
            'assign("mod_test_choose_plus_result_ok", 0);',
            'if getvar("mod_test_choose_plus_result") == 2 then assign("mod_test_choose_plus_result_ok", 1) end',
            'assign("mod_test_choose_plus_filter_ok", 0);',
            'if getvar("__automation_choose_consumed") == 1 and getvar("__automation_choose_visible_count") == 2 and getvar("__automation_choose_complete") == 1 then assign("mod_test_choose_plus_filter_ok", 1) end',
            'displaymessage("扩展选择框测试完成");',
            "return;",
            "",
        ]
    )


def make_choose_menu_visual_script() -> str:
    return "\n".join(
        [
            'displaymessage("选择框画面测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_choose_menu_visual_ready", 1);',
            'assign("mod_test_choose_menu_visual_result", -1);',
            'chooseplus("#name", 2, 0, "选择框长提示第一行<enter>第二行检查自动换行与面板增长<enter>第三行确保所有选项都位于窗口内", "", "选项一", "选项二：这是一条需要在完整点击区域内自动换行的较长文本", "选项三<enter>第二行<enter>第三行", "选项四", "选项五", "选项六", "选项七", "选项八", "选项九", "选项十", "选项十一：请选择这个原始末尾索引", "mod_test_choose_menu_visual_result");',
            'assign("mod_test_choose_menu_visual_last_index_ok", 0);',
            'if getvar("mod_test_choose_menu_visual_result") == 11 then assign("mod_test_choose_menu_visual_last_index_ok", 1) end',
            'assign("mod_test_choose_menu_visual_right_result", -1);',
            'chooseplus("酒肆老板", -1, 1, "右侧标题测试：不显示头像", "完成画面验收", "重新测试", "mod_test_choose_menu_visual_right_result");',
            'assign("mod_test_choose_menu_visual_right_ok", 0);',
            'if getvar("mod_test_choose_menu_visual_right_result") == 0 then assign("mod_test_choose_menu_visual_right_ok", 1) end',
            'displaymessage("选择框画面测试完成");',
            "return;",
            "",
        ]
    )


def make_npc_kind_talent_script() -> str:
    return "\n".join(
        [
            'displaymessage("NPC 类型与天赋测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_npc_kind_talent_ready", 1);',
            'addnpc("mod_test_npc_kind_talent_npc.ini", 35, 21, 0);',
            'getnpcstate("MOD_TEST_NPC_KIND_TALENT", "Exists", "mod_test_npc_kind_talent_exists");',
            'getnpcstate("MOD_TEST_NPC_KIND_TALENT", "KindValue", "mod_test_npc_kind_initial");',
            'getnpcstate("MOD_TEST_NPC_KIND_TALENT", "KindValueMax", "mod_test_npc_kind_max");',
            'addkindvalue("MOD_TEST_NPC_KIND_TALENT", 600);',
            'getnpcstate("MOD_TEST_NPC_KIND_TALENT", "KindValue", "mod_test_npc_kind_after_plus");',
            'addkindvalue("MOD_TEST_NPC_KIND_TALENT", -2000);',
            'getnpcstate("MOD_TEST_NPC_KIND_TALENT", "KindValue", "mod_test_npc_kind_after_minus");',
            'getplayermagiclevel("mod_test_player_talent_probe.ini", "mod_test_player_talent_before");',
            'addtalent("mod_test_player_talent_probe.ini");',
            'getplayermagiclevel("mod_test_player_talent_probe.ini", "mod_test_player_talent_after");',
            'assign("mod_test_npc_kind_clamp_ok", 0);',
            'if getvar("mod_test_npc_kind_initial") == 500 and getvar("mod_test_npc_kind_max") == 1000 and getvar("mod_test_npc_kind_after_plus") == 1000 and getvar("mod_test_npc_kind_after_minus") == 0 then assign("mod_test_npc_kind_clamp_ok", 1) end',
            'assign("mod_test_add_talent_ok", 0);',
            'if getvar("mod_test_player_talent_before") == 0 and getvar("mod_test_player_talent_after") == 1 then assign("mod_test_add_talent_ok", 1) end',
            'displaymessage("NPC 类型与天赋测试完成");',
            "return;",
            "",
        ]
    )


def make_npc_signal_tip_script() -> str:
    return "\n".join(
        [
            'displaymessage("NPC 信号提示测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_npc_signal_tip_ready", 1);',
            'addnpc("mod_test_npc_signal_tip_npc.ini", 35, 21, 0);',
            'getnpcstate("MOD_TEST_NPC_SIGNAL_TIP", "Exists", "mod_test_npc_signal_tip_exists");',
            'getnpcstate("MOD_TEST_NPC_SIGNAL_TIP", "IsSignalShow", "mod_test_npc_signal_initial_show");',
            'getnpcstate("MOD_TEST_NPC_SIGNAL_TIP", "SignalIndex", "mod_test_npc_signal_initial_index");',
            'showsignaltip("MOD_TEST_NPC_SIGNAL_TIP", 23, "t1");',
            'getnpcstate("MOD_TEST_NPC_SIGNAL_TIP", "IsSignalShow", "mod_test_npc_signal_show_after_show");',
            'getnpcstate("MOD_TEST_NPC_SIGNAL_TIP", "SignalIndex", "mod_test_npc_signal_index_after_show");',
            'getnpcstate("MOD_TEST_NPC_SIGNAL_TIP", "IsSignalTypeT1", "mod_test_npc_signal_t1_after_show");',
            'getnpcstate("MOD_TEST_NPC_SIGNAL_TIP", "IsSignalTypeT0", "mod_test_npc_signal_t0_after_show");',
            'setsignaltiphidden("MOD_TEST_NPC_SIGNAL_TIP");',
            'getnpcstate("MOD_TEST_NPC_SIGNAL_TIP", "IsSignalShow", "mod_test_npc_signal_show_after_hide");',
            'getnpcstate("MOD_TEST_NPC_SIGNAL_TIP", "SignalIndex", "mod_test_npc_signal_index_after_hide");',
            'assign("mod_test_npc_signal_show_hide_ok", 0);',
            'if getvar("mod_test_npc_signal_initial_show") == 0 and getvar("mod_test_npc_signal_initial_index") == 0 and getvar("mod_test_npc_signal_show_after_show") == 1 and getvar("mod_test_npc_signal_index_after_show") == 23 and getvar("mod_test_npc_signal_t1_after_show") == 1 and getvar("mod_test_npc_signal_t0_after_show") == 0 and getvar("mod_test_npc_signal_show_after_hide") == 0 and getvar("mod_test_npc_signal_index_after_hide") == 23 then assign("mod_test_npc_signal_show_hide_ok", 1) end',
            'getnpcstate("鸟", "Exists", "mod_test_npc_signal_map_npc_exists");',
            'setmapnpcattr("鸟", "IsSignalShow:1;SignalIndex:24;SignalType:t0", "map001.npc");',
            'getnpcstate("鸟", "IsSignalShow", "mod_test_npc_signal_map_show");',
            'getnpcstate("鸟", "SignalIndex", "mod_test_npc_signal_map_index");',
            'getnpcstate("鸟", "IsSignalTypeT0", "mod_test_npc_signal_map_t0");',
            'getnpcstate("鸟", "IsSignalTypeT1", "mod_test_npc_signal_map_t1");',
            'assign("mod_test_npc_signal_setattr_ok", 0);',
            'if getvar("mod_test_npc_signal_map_npc_exists") == 1 and getvar("mod_test_npc_signal_map_show") == 1 and getvar("mod_test_npc_signal_map_index") == 24 and getvar("mod_test_npc_signal_map_t0") == 1 and getvar("mod_test_npc_signal_map_t1") == 0 then assign("mod_test_npc_signal_setattr_ok", 1) end',
            'setmapnpcattr("鸟", "IsSignalShow:0;SignalIndex:0;SignalType:", "map001.npc");',
            'displaymessage("NPC 信号提示测试完成");',
            "return;",
            "",
        ]
    )


def make_script_sound_position_script() -> str:
    return "\n".join(
        [
            'displaymessage("脚本声音位置测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_script_sound_position_ready", 1);',
            'playsound("mod_test_script_sound.wav");',
            'getplayerstate("LastScriptSoundHasPosition", "mod_test_script_sound_global_has_pos");',
            'getplayerstate("LastScriptSoundSourceType", "mod_test_script_sound_global_source");',
            'assign("mod_test_script_sound_global_fallback_ok", 0);',
            'if getvar("mod_test_script_sound_global_has_pos") == 0 and getvar("mod_test_script_sound_global_source") == 0 then assign("mod_test_script_sound_global_fallback_ok", 1) end',
            'assign("mod_test_script_sound_obj_ran", 0);',
            'assign("mod_test_script_sound_obj_position_ok", 0);',
            'delobj("MOD_TEST_SCRIPT_SOUND_OBJ");',
            'addobj("mod_test_script_sound_obj.ini", 37, 20, 0);',
            'runobjscript("MOD_TEST_SCRIPT_SOUND_OBJ");',
            'assign("mod_test_script_sound_npc_ran", 0);',
            'assign("mod_test_script_sound_npc_position_ok", 0);',
            'delnpc("MOD_TEST_SCRIPT_SOUND_NPC");',
            'addnpc("mod_test_script_sound_npc.ini", 36, 21, 0);',
            "setplayerpos(36, 20);",
            "setplayerscn();",
            'assign("mod_test_script_sound_npc_queued", interactnearestnpc(0, 0, 2));',
            'displaymessage("脚本声音位置测试已进入交互阶段");',
            "return;",
            "",
        ]
    )


def make_script_sound_position_obj_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_script_sound_obj_ran", 1);',
            'playsound("mod_test_script_sound.wav");',
            'getplayerstate("LastScriptSoundHasPosition", "mod_test_script_sound_obj_has_pos");',
            'getplayerstate("LastScriptSoundSourceType", "mod_test_script_sound_obj_source");',
            'getplayerstate("LastScriptSoundMapX", "mod_test_script_sound_obj_mapx");',
            'getplayerstate("LastScriptSoundMapY", "mod_test_script_sound_obj_mapy");',
            'getplayerstate("LastScriptSoundOffsetX1000", "mod_test_script_sound_obj_offset_x");',
            'getplayerstate("LastScriptSoundOffsetY1000", "mod_test_script_sound_obj_offset_y");',
            'if getvar("mod_test_script_sound_obj_has_pos") == 1 and getvar("mod_test_script_sound_obj_source") == 2 and getvar("mod_test_script_sound_obj_mapx") == 37 and getvar("mod_test_script_sound_obj_mapy") == 20 then assign("mod_test_script_sound_obj_position_ok", 1) end',
            "return;",
            "",
        ]
    )


def make_script_sound_position_npc_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_script_sound_npc_ran", 1);',
            'playsound("mod_test_script_sound.wav");',
            'getplayerstate("LastScriptSoundHasPosition", "mod_test_script_sound_npc_has_pos");',
            'getplayerstate("LastScriptSoundSourceType", "mod_test_script_sound_npc_source");',
            'getplayerstate("LastScriptSoundMapX", "mod_test_script_sound_npc_mapx");',
            'getplayerstate("LastScriptSoundMapY", "mod_test_script_sound_npc_mapy");',
            'getplayerstate("LastScriptSoundOffsetX1000", "mod_test_script_sound_npc_offset_x");',
            'getplayerstate("LastScriptSoundOffsetY1000", "mod_test_script_sound_npc_offset_y");',
            'if getvar("mod_test_script_sound_npc_has_pos") == 1 and getvar("mod_test_script_sound_npc_source") == 1 and getvar("mod_test_script_sound_npc_mapx") == 36 and getvar("mod_test_script_sound_npc_mapy") == 21 then assign("mod_test_script_sound_npc_position_ok", 1) end',
            "return;",
            "",
        ]
    )


def make_script_sound_position_obj_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "ObjName=MOD_TEST_SCRIPT_SOUND_OBJ",
            "ObjFile=obj_宝箱1.ini",
            "Kind=1",
            "Dir=0",
            "MapX=0",
            "MapY=0",
            "OffX=0",
            "OffY=0",
            "Damage=0",
            "Frame=0",
            "Height=4",
            "Lum=1",
            "CanInteractDirectly=1",
            "ScriptFileJustTouch=0",
            "ScriptFile=mod_test_script_sound_position_obj.txt",
            "ScriptFileRight=",
            "TimerScriptInterval=60000",
            "MillisecondsToRemove=30000",
            "",
        ]
    )


def make_script_sound_probe_wav() -> str:
    return (
        "RIFF\x26\x00\x00\x00"
        "WAVE"
        "fmt \x10\x00\x00\x00"
        "\x01\x00"
        "\x01\x00"
        "\x00\x02\x00\x00"
        "\x00\x04\x00\x00"
        "\x02\x00"
        "\x10\x00"
        "data\x02\x00\x00\x00"
        "\x00\x00"
    )


def make_object_state_box_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "ObjName=MOD_TEST_OBJECT_STATE",
            "ObjFile=mod_test_object_state_resource.ini",
            "ObjFileMovie=obj_movie_宝箱1.ini",
            "Kind=1",
            "Type=2",
            "Dir=0",
            "MapX=0",
            "MapY=0",
            "OffX=0",
            "OffY=0",
            "Damage=9",
            "Frame=0",
            "Height=4",
            "Lum=1",
            "CanInteractDirectly=1",
            "ScriptFileJustTouch=0",
            "ScriptFile=mod_test_object_interact.txt",
            "ScriptFileRight=mod_test_object_interact_right.txt",
            "TimerScriptInterval=60000",
            "MillisecondsToRemove=30000",
            "",
        ]
    )


def make_object_state_resource_ini() -> str:
    return "\n".join(
        [
            "[Common]",
            "Image=宝箱2.asf",
            "Shade=宝箱2.asf",
            "Sound=恢复生命.wav",
            "",
        ]
    )


def make_animation_parameter_resource_ini() -> str:
    return "\n".join(
        [
            "[Common]",
            "Image=asf/character/npc115_wlk.asf",
            "Shade=",
            "Sound=",
            "",
        ]
    )


def make_animation_parameter_object_ini(direction: int) -> str:
    if direction < 0 or direction >= 8:
        raise ValueError(f"animation parameter direction must be in 0..7, got {direction}")
    return "\n".join(
        [
            "[INIT]",
            f"ObjName=MOD_TEST_ANIMATION_DIRECTION_{direction}",
            "ObjFile=mod_test_animation_parameter_resource.ini",
            "Kind=0",
            f"Dir={direction}",
            "MapX=0",
            "MapY=0",
            "OffX=0",
            "OffY=0",
            "Damage=0",
            "Frame=0",
            "Height=0",
            "Lum=1",
            "CanInteractDirectly=0",
            "ScriptFileJustTouch=0",
            "ScriptFile=",
            "ScriptFileRight=",
            "TimerScriptInterval=60000",
            "MillisecondsToRemove=60000",
            "",
        ]
    )


def make_pickup_object_ini(name: str, object_file: str = "obj_get_钱.ini", kind: int = 7) -> str:
    return "\n".join(
        [
            "[INIT]",
            f"ObjName={name}",
            f"ObjFile={object_file}",
            f"Kind={kind}",
            "Type=0",
            "Dir=0",
            "MapX=0",
            "MapY=0",
            "OffX=0",
            "OffY=0",
            "Damage=0",
            "Frame=0",
            "Height=4",
            "Lum=1",
            "CanInteractDirectly=1",
            "ScriptFileJustTouch=0",
            "ScriptFile=",
            "",
        ]
    )


def make_npc_drop_token_ini(name: str, object_file: str = "obj_get_钱.ini") -> str:
    return make_pickup_object_ini(name, object_file, 7)


def make_npc_drop_table_ini(object_ini: str) -> str:
    return "\n".join(
        [
            "[INIT]",
            "Count=1",
            "",
            "[1]",
            f"ObjFile={object_ini}",
            "Num=1",
            "Odds=1",
            "Group=1",
            "",
        ]
    )


def make_object_timer_box_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "ObjName=MOD_TEST_OBJECT_TIMER",
            "ObjFile=obj_宝箱1.ini",
            "Kind=1",
            "Dir=0",
            "MapX=0",
            "MapY=0",
            "OffX=0",
            "OffY=0",
            "Damage=0",
            "Frame=0",
            "Height=4",
            "Lum=1",
            "CanInteractDirectly=1",
            "ScriptFileJustTouch=0",
            "ScriptFile=0",
            "ScriptFileRight=0",
            "TimerScriptFile=mod_test_object_timer.txt",
            "TimerScriptInterval=120",
            "MillisecondsToRemove=30000",
            "",
        ]
    )


def make_object_remove_box_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "ObjName=MOD_TEST_OBJECT_REMOVE",
            "ObjFile=obj_宝箱1.ini",
            "Kind=1",
            "Dir=0",
            "MapX=0",
            "MapY=0",
            "OffX=0",
            "OffY=0",
            "Damage=0",
            "Frame=0",
            "Height=4",
            "Lum=1",
            "CanInteractDirectly=1",
            "ScriptFileJustTouch=0",
            "ScriptFile=0",
            "ScriptFileRight=0",
            "TimerScriptInterval=60000",
            "MillisecondsToRemove=180",
            "",
        ]
    )


def make_object_touch_box_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "ObjName=MOD_TEST_OBJECT_TOUCH",
            "ObjFile=obj_宝箱1.ini",
            "Kind=1",
            "Dir=0",
            "MapX=0",
            "MapY=0",
            "OffX=0",
            "OffY=0",
            "Damage=0",
            "Frame=0",
            "Height=4",
            "Lum=1",
            "CanInteractDirectly=0",
            "ScriptFileJustTouch=1",
            "ScriptFile=mod_test_object_touch.txt",
            "ScriptFileRight=0",
            "TimerScriptInterval=60000",
            "MillisecondsToRemove=30000",
            "",
        ]
    )


def make_object_trap_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "ObjName=MOD_TEST_OBJECT_TRAP",
            "ObjFile=obj_宝箱1.ini",
            "Kind=6",
            "Dir=0",
            "MapX=0",
            "MapY=0",
            "OffX=0",
            "OffY=0",
            "Damage=7",
            "Frame=0",
            "Height=4",
            "Lum=1",
            "CanInteractDirectly=0",
            "ScriptFileJustTouch=0",
            "ScriptFile=0",
            "ScriptFileRight=0",
            "TimerScriptInterval=60000",
            "MillisecondsToRemove=30000",
            "",
        ]
    )


def make_object_save_box_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "ObjName=MOD_TEST_OBJECT_SAVE",
            "ObjFile=obj_宝箱1.ini",
            "ObjFileMovie=obj_movie_宝箱1.ini",
            "Kind=1",
            "Type=3",
            "Dir=0",
            "MapX=0",
            "MapY=0",
            "OffX=0",
            "OffY=0",
            "Damage=5",
            "Frame=0",
            "Height=4",
            "Lum=1",
            "CanInteractDirectly=1",
            "ScriptFileJustTouch=0",
            "ScriptFile=mod_test_object_interact.txt",
            "ScriptFileRight=mod_test_object_interact_right.txt",
            "TimerScriptInterval=60000",
            "MillisecondsToRemove=30000",
            "",
        ]
    )


def make_object_right_only_box_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "ObjName=MOD_TEST_OBJECT_RIGHT_ONLY",
            "ObjFile=obj_宝箱1.ini",
            "Kind=1",
            "Dir=0",
            "MapX=0",
            "MapY=0",
            "OffX=0",
            "OffY=0",
            "Damage=0",
            "Frame=0",
            "Height=4",
            "Lum=1",
            "CanInteractDirectly=1",
            "ScriptFileJustTouch=0",
            "ScriptFile=",
            "ScriptFileRight=mod_test_object_interact_right.txt",
            "TimerScriptInterval=60000",
            "MillisecondsToRemove=30000",
            "",
        ]
    )


def make_object_left_only_box_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "ObjName=MOD_TEST_OBJECT_LEFT_ONLY",
            "ObjFile=obj_宝箱1.ini",
            "Kind=1",
            "Dir=0",
            "MapX=0",
            "MapY=0",
            "OffX=0",
            "OffY=0",
            "Damage=0",
            "Frame=0",
            "Height=4",
            "Lum=1",
            "CanInteractDirectly=1",
            "ScriptFileJustTouch=0",
            "ScriptFile=mod_test_object_interact.txt",
            "ScriptFileRight=",
            "TimerScriptInterval=60000",
            "MillisecondsToRemove=30000",
            "",
        ]
    )


def make_object_alias_box_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "ObjName=MOD_TEST_OBJECT_ALIAS",
            "ObjFile=obj_宝箱1.ini",
            "Kind=1",
            "Dir=0",
            "MapX=0",
            "MapY=0",
            "OffX=0",
            "OffY=0",
            "Damage=0",
            "Frame=0",
            "Height=4",
            "Lum=1",
            "CanInteractDirectly=1",
            "ScriptFileJustTouch=0",
            "ScriptFile=mod_test_object_alias_delete_current.txt",
            "ScriptFileRight=",
            "TimerScriptInterval=60000",
            "MillisecondsToRemove=30000",
            "",
        ]
    )


def make_object_damage_trap_ini(name: str) -> str:
    return "\n".join(
        [
            "[INIT]",
            f"ObjName={name}",
            "ObjFile=obj_宝箱1.ini",
            "Kind=6",
            "Dir=0",
            "MapX=0",
            "MapY=0",
            "OffX=0",
            "OffY=0",
            "Damage=200",
            "Frame=0",
            "Height=4",
            "Lum=1",
            "CanInteractDirectly=0",
            "ScriptFileJustTouch=0",
            "ScriptFile=",
            "ScriptFileRight=",
            "TimerScriptInterval=60000",
            "MillisecondsToRemove=30000",
            "",
        ]
    )


def make_object_trap_damage_recorder_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "ObjName=MOD_TEST_OBJECT_DAMAGE_RECORDER",
            "ObjFile=obj_宝箱1.ini",
            "Kind=1",
            "Dir=0",
            "MapX=0",
            "MapY=0",
            "OffX=0",
            "OffY=0",
            "Damage=0",
            "Frame=0",
            "Height=4",
            "Lum=1",
            "CanInteractDirectly=0",
            "ScriptFileJustTouch=0",
            "ScriptFile=",
            "ScriptFileRight=",
            "TimerScriptFile=mod_test_object_trap_damage_record.txt",
            "TimerScriptInterval=320",
            "MillisecondsToRemove=30000",
            "",
        ]
    )


def make_npc_ai_body_object_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "ObjName=MOD_TEST_NPC_AI_BODY",
            "ObjFile=obj_宝箱1.ini",
            "Kind=2",
            "Dir=0",
            "MapX=0",
            "MapY=0",
            "OffX=0",
            "OffY=0",
            "Damage=0",
            "Frame=0",
            "Height=4",
            "Lum=1",
            "CanInteractDirectly=0",
            "ScriptFileJustTouch=0",
            "ScriptFile=",
            "ScriptFileRight=",
            "TimerScriptInterval=60000",
            "MillisecondsToRemove=30000",
            "",
        ]
    )


def make_magic_body_object_ini(object_name: str, revive_npc_ini: str = "") -> str:
    lines = [
        "[INIT]",
        f"ObjName={object_name}",
        "ObjFile=obj_宝箱1.ini",
        "Kind=2",
        "Dir=0",
        "MapX=0",
        "MapY=0",
        "OffX=0",
        "OffY=0",
        "Damage=0",
        "Frame=0",
        "Height=4",
        "Lum=1",
        "CanInteractDirectly=0",
        "ScriptFileJustTouch=0",
        "ScriptFile=",
        "ScriptFileRight=",
    ]
    if revive_npc_ini:
        lines.append(f"ReviveNpcIni={revive_npc_ini}")
    lines.extend(
        [
            "TimerScriptInterval=60000",
            "MillisecondsToRemove=30000",
            "",
        ]
    )
    return "\n".join(lines)


def make_object_trap_target_npc_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "Name=MOD_TEST_OBJECT_TRAP_TARGET",
            "NpcIni=npcres001_独孤剑.ini",
            "FlyIni=",
            "BodyIni=",
            "Kind=1",
            "Relation=2",
            "Life=100",
            "LifeMax=100",
            "Thew=100",
            "ThewMax=100",
            "Mana=0",
            "ManaMax=0",
            "Attack=0",
            "Defence=0",
            "Evade=0",
            "Exp=0",
            "WalkSpeed=1",
            "Dir=0",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "DeathScript=",
            "ScriptFile=",
            "TalkContent=MOD_TEST object trap target",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            "",
        ]
    )


def make_magic_body_target_npc_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "Name=MOD_TEST_MAGIC_BODY_TARGET",
            "NpcIni=npcres001_独孤剑.ini",
            "FlyIni=",
            "BodyIni=",
            "Kind=1",
            "Relation=2",
            "Life=500",
            "LifeMax=500",
            "Thew=100",
            "ThewMax=100",
            "Mana=0",
            "ManaMax=0",
            "Attack=0",
            "Defence=0",
            "Evade=0",
            "Exp=0",
            "WalkSpeed=1",
            "Dir=0",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "DeathScript=",
            "ScriptFile=",
            "TalkContent=MOD_TEST magic body target",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            "",
        ]
    )


def make_magic_revive_body_npc_ini(name: str = "MOD_TEST_MAGIC_REVIVE_BODY_NPC") -> str:
    return "\n".join(
        [
            "[INIT]",
            f"Name={name}",
            "NpcIni=npcres001_独孤剑.ini",
            "FlyIni=",
            "BodyIni=",
            "Kind=1",
            "Relation=2",
            "Life=80",
            "LifeMax=80",
            "Thew=100",
            "ThewMax=100",
            "Mana=0",
            "ManaMax=0",
            "Attack=0",
            "Defence=0",
            "Evade=0",
            "Exp=0",
            "WalkSpeed=1",
            "Dir=0",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "DeathScript=",
            "ScriptFile=",
            f"TalkContent={name}",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            "",
        ]
    )


def make_steal_npc_ini(name: str = "MOD_TEST_STEAL_NPC", steal: int = 0) -> str:
    return "\n".join(
        [
            "[INIT]",
            f"Name={name}",
            "NpcIni=npcres_map016周木匠.ini",
            "FlyIni=",
            "BodyIni=",
            "Kind=0",
            "Relation=2",
            "Life=100",
            "LifeMax=100",
            "Thew=100",
            "ThewMax=100",
            "Mana=0",
            "ManaMax=0",
            "Attack=0",
            "Defence=0",
            "Evade=0",
            "Exp=0",
            "WalkSpeed=1",
            "Dir=0",
            "Lum=0",
            "PathFinder=0",
            "Action=0",
            "DeathScript=",
            "ScriptFile=",
            "BagGoods=mod_test_steal_token.ini:100",
            f"Steal={steal}",
            f"TalkContent={name} steal fixture",
            "",
        ]
    )


def make_steal_goods_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_STEAL_TOKEN",
            "Kind=2",
            "Cost=0",
            "Intro=MOD test token stolen from a neutral fixture NPC.",
            "Image=",
            "Icon=",
            "Script=",
            "",
        ]
    )


def make_equipment_trigger_goods_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_EQUIPMENT_TRIGGER",
            "Kind=1",
            "Cost=0",
            "Intro=MOD test equipment covering FlyIni, FlyIni2, be-attacked magic, and C# extension stat fields.",
            "Image=",
            "Icon=",
            "Part=Hand",
            "Script=",
            "ChangeMoveSpeedPercent=25",
            "AddMagicEffectPercent=30",
            "AddMagicEffectAmount=2",
            "AddMagicEffectName=MOD_TEST_EQUIPMENT_POWER",
            "AddMagicEffectType=Attack",
            "EffectType=1",
            "NoNeedToEquip=0",
            "MagicIniWhenUse=",
            "ReplaceMagic=mod_test_magic_equipment_power.ini",
            "UseReplaceMagic=mod_test_magic_equipment_replace.ini",
            "FlyIni=mod_test_magic_equipment_fly.ini",
            "FlyIni2=mod_test_magic_equipment_fly2.ini",
            "MagicToUseWhenBeAttacked=mod_test_magic_equipment_counter.ini",
            "MagicDirectionWhenBeAttacked=1",
            "SpecialEffect=1",
            "SpecialEffectValue=10",
            "LifeMax=10",
            "ThewMax=5",
            "ManaMax=5",
            "Attack=3",
            "Attack2=2",
            "Attack3=1",
            "Defend=4",
            "Defend2=3",
            "Defend3=2",
            "Evade=1",
            "",
        ]
    )


def make_equipment_user_restricted_goods_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_EQUIPMENT_USER_RESTRICTED",
            "Kind=1",
            "Cost=0",
            "Intro=MOD test equipment rejected when User does not match the current player.",
            "Image=",
            "Icon=",
            "Part=Hand",
            "Script=",
            "User=MOD_TEST_OTHER_USER",
            "NoNeedToEquip=0",
            "Attack=9",
            "",
        ]
    )


def make_equipment_level_restricted_goods_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_EQUIPMENT_LEVEL_RESTRICTED",
            "Kind=1",
            "Cost=0",
            "Intro=MOD test equipment rejected below MinUserLevel and accepted after level is raised.",
            "Image=",
            "Icon=",
            "Part=Hand",
            "Script=",
            "MinUserLevel=50",
            "NoNeedToEquip=0",
            "Attack=12",
            "",
        ]
    )


def make_equipment_parasitic_bonus_goods_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_EQUIPMENT_PARASITIC_BONUS",
            "Kind=1",
            "Cost=0",
            "Intro=MOD test no-need equipment that boosts parasitic tick damage.",
            "Image=",
            "Icon=",
            "Part=Hand",
            "Script=",
            "ChangeMoveSpeedPercent=0",
            "AddMagicEffectPercent=0",
            "AddMagicEffectAmount=50",
            "AddMagicEffectName=MOD_TEST_PARASITIC",
            "AddMagicEffectType=",
            "EffectType=0",
            "NoNeedToEquip=1",
            "",
        ]
    )


def make_goods_pricing_drug_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_GOODS_PRICING_DRUG",
            "Kind=0",
            "Cost=0",
            "Intro=MOD test drug with default C# CostRaw formula.",
            "Image=",
            "Icon=",
            "Life=10",
            "Thew=5",
            "Mana=3",
            "EffectType=1",
            "",
        ]
    )


def make_goods_pricing_equipment_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_GOODS_PRICING_EQUIPMENT",
            "Kind=1",
            "Cost=77",
            "SellPrice=33",
            "Intro=MOD test equipment with explicit buy and sell prices.",
            "Image=",
            "Icon=",
            "Part=Hand",
            "EffectType=1",
            "Attack=3",
            "Defend=4",
            "Evade=1",
            "",
        ]
    )


def make_goods_pricing_noneed_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_GOODS_PRICING_NONEED",
            "Kind=1",
            "Cost=0",
            "Intro=MOD test no-need equipment whose C# default CostRaw is zero.",
            "Image=",
            "Icon=",
            "Part=Neck",
            "NoNeedToEquip=1",
            "LifeMax=20",
            "ManaMax=10",
            "",
        ]
    )


def make_goods_random_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_GOODS_RANDOM",
            "Kind=1",
            "Cost=10>20",
            "SellPrice=5,9",
            "Intro=MOD test random equipment whose AttrInt/AttrString values should be instanced into save/game.",
            "Image=",
            "Icon=",
            "Part=Hand",
            "Attack=1>3",
            "Defend=4,8",
            "Evade=1>-5",
            "MagicName=mod_test_magic_equipment_fly.ini",
            "MagicIniWhenUse=mod_test_magic_equipment_fly.ini,mod_test_magic_equipment_fly2.ini[2]",
            "",
        ]
    )


def make_goods_lifecycle_stack_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_GOODS_LIFECYCLE_STACK",
            "Kind=0",
            "Cost=0",
            "Intro=MOD test stacked item used by DelGoods and DelGoodByName lifecycle checks.",
            "Image=",
            "Icon=",
            "",
        ]
    )


def make_goods_lifecycle_case_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=Mod_Test_Goods_Lifecycle_Case",
            "Kind=0",
            "Cost=0",
            "Intro=MOD test item used by GetGoodsNumByName exact display-name checks.",
            "Image=",
            "Icon=",
            "",
        ]
    )


def make_goods_lifecycle_magic_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_GOODS_LIFECYCLE_MAGIC",
            "Kind=1",
            "Cost=0",
            "Intro=MOD test equipment that grants MagicIniWhenUse while equipped.",
            "Image=",
            "Icon=",
            "Part=Hand",
            "MagicIniWhenUse=mod_test_magic_equipment_fly.ini",
            "",
        ]
    )


def make_goods_lifecycle_magic_noneed_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_GOODS_LIFECYCLE_MAGIC_NONEED",
            "Kind=1",
            "Cost=0",
            "Intro=MOD test no-need equipment that grants the same MagicIniWhenUse for hide-count lifecycle checks.",
            "Image=",
            "Icon=",
            "NoNeedToEquip=1",
            "MagicIniWhenUse=mod_test_magic_equipment_fly.ini",
            "",
        ]
    )


def make_goods_friend_drug_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_GOODS_FRIEND_DRUG",
            "Kind=0",
            "Cost=0",
            "Intro=MOD test drug whose FighterFriendHasDrugEffect applies to friendly fighters.",
            "Image=",
            "Icon=",
            "Life=25",
            "LifeMax=7",
            "Thew=0",
            "ThewMax=3",
            "Mana=0",
            "ManaMax=2",
            "FighterFriendHasDrugEffect=1",
            "FollowPartnerHasDrugEffect=0",
            "",
        ]
    )


def make_goods_friend_clear_poison_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_GOODS_FRIEND_CLEAR_POISON",
            "Kind=0",
            "Cost=0",
            "Intro=MOD test drug whose FighterFriendHasDrugEffect clears poison from friendly fighters.",
            "Image=",
            "Icon=",
            "EffectType=2",
            "FighterFriendHasDrugEffect=1",
            "FollowPartnerHasDrugEffect=0",
            "",
        ]
    )


def make_goods_partner_drug_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_GOODS_PARTNER_DRUG",
            "Kind=0",
            "Cost=0",
            "Intro=MOD test drug whose FollowPartnerHasDrugEffect applies to partners only.",
            "Image=",
            "Icon=",
            "Life=35",
            "LifeMax=11",
            "Thew=0",
            "ThewMax=4",
            "Mana=0",
            "ManaMax=5",
            "FighterFriendHasDrugEffect=0",
            "FollowPartnerHasDrugEffect=1",
            "",
        ]
    )


def make_goods_bound_ammo_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_GOODS_BOUND_AMMO",
            "Kind=2",
            "Cost=0",
            "Intro=MOD test ammo consumed through Magic.GoodsName.",
            "Image=",
            "Icon=",
            "",
        ]
    )


def make_goods_script_book_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_GOODS_SCRIPT_BOOK",
            "Kind=2",
            "Cost=0",
            "Intro=MOD test event goods that runs a goods script and deletes itself with DelGoods().",
            "Image=",
            "Icon=",
            "Script=mod_test_goods_script_book.txt",
            "",
        ]
    )


def make_steal_script() -> str:
    return "\n".join(
        [
            'displaymessage("偷窃测试开始");',
            'if getvar("mod_test_steal_interactive") == 1 then goto Interactive end',
            "goto Automated",
            "::Interactive::",
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'loadmap("map001_衡山.map");',
            'loadnpc("map001.npc");',
            'loadobj("map001.obj");',
            "cleargoods();",
            'delnpc("MOD_TEST_STEAL_NPC");',
            'delnpc("MOD_TEST_STEAL_HARD_NPC");',
            'addnpc("mod_test_steal_npc.ini", -1, -1, -1);',
            'assign("touqie", 100);',
            'assign("__automation_choose_enabled", 0);',
            'assign("__automation_choose_selection", -1);',
            'showstealwin("MOD_TEST_STEAL_NPC", "mod_test_steal_success.txt", "mod_test_steal_fail.txt");',
            'assign("mod_test_steal_opened", 1);',
            'displaymessage("手动偷窃测试结束");',
            "goto End",
            "::Automated::",
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'loadmap("map001_衡山.map");',
            'loadnpc("map001.npc");',
            'loadobj("map001.obj");',
            "cleargoods();",
            'delnpc("MOD_TEST_STEAL_NPC");',
            'delnpc("MOD_TEST_STEAL_HARD_NPC");',
            'addnpc("mod_test_steal_npc.ini", -1, -1, -1);',
            'assign("touqie", 100);',
            'assign("__automation_choose_enabled", 1);',
            'assign("__automation_choose_selection", 0);',
            'showstealwin("MOD_TEST_STEAL_NPC", "mod_test_steal_success.txt", "mod_test_steal_fail.txt");',
            'assign("mod_test_steal_opened", 1);',
            'getgoodsnum("mod_test_steal_token.ini");',
            'assign("mod_test_steal_count_after_success", getvar("GoodsNum"));',
            'assign("__automation_choose_enabled", 1);',
            'assign("__automation_choose_selection", 0);',
            'showstealwin("MOD_TEST_STEAL_NPC", "mod_test_steal_success.txt", "mod_test_steal_fail.txt");',
            'getgoodsnum("mod_test_steal_token.ini");',
            'assign("mod_test_steal_count_after_second", getvar("GoodsNum"));',
            'assign("mod_test_steal_second_removed", 0);',
            'if getvar("mod_test_steal_count_after_second") == getvar("mod_test_steal_count_after_success") then assign("mod_test_steal_second_removed", 1) end',
            "cleargoods();",
            'assign("touqie", 0);',
            'addnpc("mod_test_steal_hard_npc.ini", -1, -1, -1);',
            'assign("__automation_choose_enabled", 1);',
            'assign("__automation_choose_selection", 0);',
            'showstealwin("MOD_TEST_STEAL_HARD_NPC", "mod_test_steal_success.txt", "mod_test_steal_fail.txt");',
            'getgoodsnum("mod_test_steal_token.ini");',
            'assign("mod_test_steal_count_after_fail", getvar("GoodsNum"));',
            'displaymessage("偷窃测试完成");',
            "goto End",
            "::End::",
            "return;",
            "",
        ]
    )


def make_steal_success_script() -> str:
    return "\n".join(
        [
            'add("mod_test_steal_success", 1);',
            'displaymessage("偷窃成功");',
            "return;",
            "",
        ]
    )


def make_steal_fail_script() -> str:
    return "\n".join(
        [
            'add("mod_test_steal_fail", 1);',
            'displaymessage("偷窃失败");',
            "return;",
            "",
        ]
    )


def make_magic_fixture_ini(
    display_name: str,
    image: str,
    icon: str,
    flying_image: str,
    vanish_image: str,
    flying_sound: str,
    move_kind: int,
    speed: int,
    life_frame: int,
    extra_init: list[str] | None = None,
    effect: int = 0,
) -> str:
    lines = [
        "[Init]",
        f"Name={display_name}",
        "Intro=MOD test magic fixture.",
        f"Speed={speed}",
        f"MoveKind={move_kind}",
        "Region=0",
        "AlphaBlend=1",
        "FlyingLum=15",
        "VanishLum=15",
        f"Image={image}",
        f"Icon={icon}",
        "WaitFrame=0",
        f"LifeFrame={life_frame}",
        f"FlyingImage={flying_image}",
        f"FlyingSound={flying_sound}",
        f"VanishImage={vanish_image}",
        "VanishSound=",
        "Belong=0",
        "AttackRadius=1",
    ]
    if extra_init:
        lines.extend(extra_init)
    lines.extend(
        [
            "",
            "[Level1]",
            f"Effect={effect}",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            f"MoveKind={move_kind}",
            f"Speed={speed}",
            "AttackRadius=1",
            "",
        ]
    )
    return "\n".join(lines)


def make_player_talent_probe_magic_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_PLAYER_TALENT_PROBE",
        "mag001-衡山有雪.asf",
        "mag001-衡山有雪s.asf",
        "mag001-1-衡山有雪.asf",
        "mag001-2-衡山有雪.asf",
        "",
        1,
        1,
        1,
    )


def make_magic_begin_follow_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_BEGIN_FOLLOW",
        "mag001-衡山有雪.asf",
        "mag001-衡山有雪s.asf",
        "mag001-1-衡山有雪.asf",
        "mag001-2-衡山有雪.asf",
        "武_衡山有雪.wav",
        2,
        8,
        80,
        ["BeginAtMouse=1", "FollowMouse=1", "RandomMoveDegree=80", "NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_trace_enemy_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_TRACE_ENEMY",
        "mag001-衡山有雪.asf",
        "mag001-衡山有雪s.asf",
        "mag001-1-衡山有雪.asf",
        "mag001-2-衡山有雪.asf",
        "",
        2,
        8,
        160,
        ["TraceEnemy=1", "TraceSpeed=12", "TraceEnemyDelayMilliseconds=100", "NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_attack_all_trace_enemy_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_ATTACK_ALL_TRACE_ENEMY",
        "mag001-衡山有雪.asf",
        "mag001-衡山有雪s.asf",
        "mag001-1-衡山有雪.asf",
        "mag001-2-衡山有雪.asf",
        "",
        2,
        8,
        160,
        ["AttackAll=1", "TraceEnemy=1", "TraceSpeed=12", "TraceEnemyDelayMilliseconds=100", "NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_random_move_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_RANDOM_MOVE",
        "mag001-衡山有雪.asf",
        "mag001-衡山有雪s.asf",
        "mag001-1-衡山有雪.asf",
        "mag001-2-衡山有雪.asf",
        "",
        2,
        7,
        120,
        ["RandomMoveDegree=1000", "NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_meteor_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_METEOR",
        "mag003-牧野流星.asf",
        "mag003-牧野流星s.asf",
        "mag003-1-牧野流星.asf",
        "mag003-2-牧野流星.asf",
        "武_牧野流星.wav",
        2,
        9,
        55,
        ["MeteorMove=6", "MeteorMoveDir=5"],
    )


def make_magic_round_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_ROUND",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "",
        "",
        2,
        4,
        90,
        ["RoundMoveClockwise=1", "RoundMoveCount=4", "RoundMoveDegreeSpeed=120", "RoundRadius=72"],
    )


def make_magic_move_imitate_user_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_MOVE_IMITATE_USER",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "",
        "",
        2,
        0,
        120,
        ["MoveImitateUser=1", "NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_moveback_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_MOVEBACK",
        "mag012-碧海潮生.asf",
        "mag012-碧海潮生s.asf",
        "mag012-1-碧海潮生.asf",
        "",
        "武_碧海潮生.wav",
        2,
        10,
        20,
        ["MoveBack=1"],
    )


def make_magic_time_stop_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_TIMESTOP",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        23,
        0,
        50,
        ["NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_time_stop_visual_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_TIMESTOP_VISUAL",
            "Intro=MOD test visual time-stop repro fixture.",
            "MoveKind=23",
            "SpecialKind=6",
            "Effect=0",
            "Speed=0",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag009-风雷九州.asf",
            "Icon=mag009-风雷九州s.asf",
            "WaitFrame=0",
            "LifeFrame=250",
            "FlyingImage=mag009-1-风雷九州.asf",
            "FlyingSound=",
            "VanishImage=mag009-1-风雷九州.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "NoExplodeWhenLifeFrameEnd=1",
            "",
            "[Level1]",
            "Effect=0",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=23",
            "SpecialKind=6",
            "Speed=0",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_trail_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_TRAIL",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        19,
        0,
        200,
        ["KeepMilliseconds=1200"],
    )


def make_magic_summon_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_SUMMON_MAGIC",
            "Intro=MOD test summon fixture.",
            "Speed=0",
            "MoveKind=22",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag009-风雷九州.asf",
            "Icon=mag009-风雷九州s.asf",
            "WaitFrame=0",
            "LifeFrame=240",
            "FlyingImage=mag009-1-风雷九州.asf",
            "FlyingSound=",
            "VanishImage=mag009-1-风雷九州.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "KeepMilliseconds=4000",
            "NpcFile=mod_test_summon_npc.ini",
            "MaxCount=1",
            "",
            "[Level1]",
            "Effect=0",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=22",
            "Speed=0",
            "LifeFrame=240",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_body_medium_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_BODY_MEDIUM",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        21,
        0,
        40,
        ["BodyRadius=2", "VibratingScreen=6", "MaxLevel=99"],
    )


def make_magic_revive_body_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_REVIVE_BODY",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        1,
        0,
        20,
        ["ReviveBodyRadius=2", "ReviveBodyMaxCount=1", "ReviveBodyLifeMilliSeconds=1500"],
    )


def make_magic_state_probe_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_MAGIC_STATE_PROBE",
            "Intro=MOD test C# Magic.Count and SpecialKindValue loader fixture.",
            "MoveKind=13",
            "SpecialKind=4",
            "SpecialKindValue=7",
            "NoSpecialKindEffectExt=1",
            "Count=3",
            "Speed=0",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag009-风雷九州.asf",
            "Icon=mag009-风雷九州s.asf",
            "WaitFrame=0",
            "LifeFrame=20",
            "FlyingImage=mag009-1-风雷九州.asf",
            "FlyingSound=",
            "VanishImage=mag009-1-风雷九州.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "MaxCount=2",
            "",
            "[Level1]",
            "Effect=1200",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=13",
            "SpecialKind=4",
            "SpecialKindValue=8",
            "Count=4",
            "Speed=0",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_self_special_ini(display_name: str, special_kind: int, effect: int, life_frame: int = 20) -> str:
    return "\n".join(
        [
            "[Init]",
            f"Name={display_name}",
            "Intro=MOD test self SpecialKind fixture.",
            "MoveKind=13",
            f"SpecialKind={special_kind}",
            f"Effect={effect}",
            "Speed=0",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag009-风雷九州.asf",
            "Icon=mag009-风雷九州s.asf",
            "WaitFrame=0",
            f"LifeFrame={life_frame}",
            "FlyingImage=mag009-1-风雷九州.asf",
            "FlyingSound=",
            "VanishImage=mag009-1-风雷九州.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "",
            "[Level1]",
            f"Effect={effect}",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=13",
            f"SpecialKind={special_kind}",
            "Speed=0",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_invisible_keep_hidden_ini() -> str:
    return make_magic_self_special_ini("MOD_TEST_INVISIBLE_KEEP_HIDDEN", 4, 1200)


def make_magic_invisible_visible_attack_ini() -> str:
    return make_magic_self_special_ini("MOD_TEST_INVISIBLE_VISIBLE_ATTACK", 5, 1400)


def make_magic_self_block_damage_ini() -> str:
    return make_magic_self_special_ini("MOD_TEST_SELF_BLOCK_DAMAGE", 6, 0, life_frame=120)


def make_magic_self_clear_abnormal_ini() -> str:
    return make_magic_self_special_ini("MOD_TEST_SELF_CLEAR_ABNORMAL", 8, 0)


def make_magic_critical_buff_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_CRITICAL_BUFF",
            "Intro=MOD test MG SpecialKind 99 guaranteed critical fixture.",
            "MoveKind=13",
            "SpecialKind=99",
            "Effect=0",
            "Speed=0",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag009-风雷九州.asf",
            "Icon=mag009-风雷九州s.asf",
            "WaitFrame=0",
            "LifeFrame=3000",
            "FlyingImage=mag009-1-风雷九州.asf",
            "FlyingSound=",
            "VanishImage=mag009-1-风雷九州.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "",
            "[Level1]",
            "Effect=0",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=13",
            "SpecialKind=99",
            "CritChanceAddValue=100",
            "CritDamageAddPercent=99",
            "Speed=0",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_critical_strike_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_CRITICAL_STRIKE",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        1,
        0,
        16,
        ["NoExplodeWhenLifeFrameEnd=1"],
        effect=100,
    )


def make_magic_morph_replace_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_MORPH_REPLACE",
            "Intro=MOD test player MorphMilliseconds ReplaceMagic fixture.",
            "MoveKind=13",
            "SpecialKind=7",
            "Effect=900",
            "MorphMilliseconds=900",
            "ReplaceMagic=mod_test_magic_begin_follow.ini;mod_test_magic_round.ini",
            "Speed=0",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag009-风雷九州.asf",
            "Icon=mag009-风雷九州s.asf",
            "WaitFrame=0",
            "LifeFrame=20",
            "FlyingImage=mag009-1-风雷九州.asf",
            "FlyingSound=",
            "VanishImage=mag009-1-风雷九州.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "",
            "[Level1]",
            "Effect=900",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=13",
            "SpecialKind=7",
            "Speed=0",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_transport_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_TRANSPORT",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        20,
        0,
        10,
        ["MaxLevel=5"],
    )


def make_magic_control_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_CONTROL",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        21,
        0,
        10,
        ["MaxLevel=5"],
    )


def make_magic_region_vtype_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_REGION_VTYPE",
            "Type=MOD_TEST_AREA",
            "InjuryType=MOD_TEST_INJURY",
            "SpriteType=7",
            "Attribute=3",
            "ScriptFile=",
            "Intro=MOD test Region=5 V-type fixture.",
            "Effect=5",
            "MoveKind=11",
            "Region=5",
            "Speed=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag009-风雷九州.asf",
            "Icon=mag009-风雷九州s.asf",
            "WaitFrame=0",
            "LifeFrame=12",
            "FlyingImage=mag009-1-风雷九州.asf",
            "FlyingSound=",
            "VanishImage=",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "NoExplodeWhenLifeFrameEnd=1",
            "RangeAddRage=5",
            "",
            "[Level1]",
            "Effect=5",
            "ManaCost=0",
            "ThewCost=0",
            "RageCost=100",
            "LevelupExp=0",
            "MoveKind=11",
            "Region=5",
            "Speed=0",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_ball_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_BALL",
        "mag001-衡山有雪.asf",
        "mag001-衡山有雪s.asf",
        "mag001-1-衡山有雪.asf",
        "mag001-2-衡山有雪.asf",
        "武_衡山有雪.wav",
        2,
        7,
        32,
        ["Ball=1", "NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_fly_magic_child_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_FLY_MAGIC_CHILD",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        2,
        2,
        80,
        ["NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_fly_magic_parent_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_FLY_MAGIC_PARENT",
        "mag001-衡山有雪.asf",
        "mag001-衡山有雪s.asf",
        "mag001-1-衡山有雪.asf",
        "mag001-2-衡山有雪.asf",
        "",
        2,
        2,
        80,
        [
            "FlyMagic=mod_test_magic_fly_magic_child.ini",
            "FlyInterval=80",
            "NoExplodeWhenLifeFrameEnd=1",
        ],
    )


def make_magic_damage_channels_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_DAMAGE_CHANNELS",
            "Intro=MOD test Effect2/Effect3/EffectMana fixture.",
            "Speed=8",
            "MoveKind=2",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag012-碧海潮生.asf",
            "Icon=mag012-碧海潮生s.asf",
            "WaitFrame=0",
            "LifeFrame=80",
            "FlyingImage=mag012-1-碧海潮生.asf",
            "FlyingSound=",
            "VanishImage=mag012-1-碧海潮生.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "",
            "[Level1]",
            "Effect=20",
            "EffectExt=9",
            "Effect2=17",
            "Effect3=13",
            "EffectMana=15",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=2",
            "Speed=8",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_leap_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_LEAP",
            "Intro=MOD test LeapTimes retarget fixture.",
            "Speed=8",
            "MoveKind=2",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag012-碧海潮生.asf",
            "Icon=mag012-碧海潮生s.asf",
            "WaitFrame=0",
            "LifeFrame=100",
            "FlyingImage=mag012-1-碧海潮生.asf",
            "FlyingSound=",
            "VanishImage=mag012-1-碧海潮生.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "NoExplodeWhenLifeFrameEnd=1",
            "",
            "[Level1]",
            "Effect=40",
            "Effect2=17",
            "Effect3=13",
            "EffectMana=15",
            "LeapTimes=1",
            "LeapFrame=20",
            "EffectReducePercentage=50",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=2",
            "Speed=8",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_attack_all_leap_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_ATTACK_ALL_LEAP",
            "Intro=MOD test AttackAll LeapTimes fighter target fixture.",
            "Speed=8",
            "MoveKind=2",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag012-碧海潮生.asf",
            "Icon=mag012-碧海潮生s.asf",
            "WaitFrame=0",
            "LifeFrame=100",
            "FlyingImage=mag012-1-碧海潮生.asf",
            "FlyingSound=",
            "VanishImage=mag012-1-碧海潮生.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "AttackAll=1",
            "NoExplodeWhenLifeFrameEnd=1",
            "",
            "[Level1]",
            "Effect=40",
            "Effect2=17",
            "Effect3=13",
            "EffectMana=15",
            "LeapTimes=1",
            "LeapFrame=20",
            "EffectReducePercentage=50",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=2",
            "Speed=8",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_restore_ini(display_name: str, restore_type: int) -> str:
    return "\n".join(
        [
            "[Init]",
            f"Name={display_name}",
            "Intro=MOD test RestoreType/RestorePercent/RestoreProbability fixture.",
            "Speed=8",
            "MoveKind=2",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag012-碧海潮生.asf",
            "Icon=mag012-碧海潮生s.asf",
            "WaitFrame=0",
            "LifeFrame=80",
            "FlyingImage=mag012-1-碧海潮生.asf",
            "FlyingSound=",
            "VanishImage=mag012-1-碧海潮生.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "RestoreProbability=100",
            "RestorePercent=50",
            f"RestoreType={restore_type}",
            "",
            "[Level1]",
            "Effect=30",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=2",
            "Speed=8",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_restore_life_ini() -> str:
    return make_magic_restore_ini("MOD_TEST_RESTORE_LIFE", 0)


def make_magic_restore_mana_ini() -> str:
    return make_magic_restore_ini("MOD_TEST_RESTORE_MANA", 1)


def make_magic_restore_thew_ini() -> str:
    return make_magic_restore_ini("MOD_TEST_RESTORE_THEW", 2)


def make_magic_attack_all_projectile_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_ATTACK_ALL_PROJECTILE",
            "Intro=MOD test AttackAll projectile fighter target fixture.",
            "Effect=40",
            "Speed=8",
            "MoveKind=2",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag009-风雷九州.asf",
            "Icon=mag009-风雷九州s.asf",
            "WaitFrame=0",
            "LifeFrame=96",
            "FlyingImage=mag009-1-风雷九州.asf",
            "FlyingSound=",
            "VanishImage=mag009-1-风雷九州.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "AttackAll=1",
            "NoExplodeWhenLifeFrameEnd=1",
            "",
            "[Level1]",
            "Effect=40",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=2",
            "Speed=8",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_wall_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_WALL",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        2,
        32,
        40,
        ["NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_pass_through_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_PASS_THROUGH",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        2,
        8,
        96,
        ["PassThrough=1", "PassThroughWithDestroyEffect=1", "NoExplodeWhenLifeFrameEnd=1"],
        40,
    )


def make_magic_pass_through_wall_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_PASS_THROUGH_WALL",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        2,
        32,
        40,
        ["PassThroughWall=1", "NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_sticky_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_STICKY",
        "mag012-碧海潮生.asf",
        "mag012-碧海潮生s.asf",
        "mag012-1-碧海潮生.asf",
        "",
        "武_碧海潮生.wav",
        2,
        6,
        34,
        ["Sticky=1", "MoveBack=1", "NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_solid_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_SOLID",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        2,
        3,
        42,
        ["Solid=1", "NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_parasitic_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_PARASITIC",
            "Intro=MOD test parasitic target-bound fixture.",
            "Effect=8",
            "Speed=7",
            "MoveKind=2",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag012-碧海潮生.asf",
            "Icon=mag012-碧海潮生s.asf",
            "WaitFrame=0",
            "LifeFrame=90",
            "FlyingImage=mag012-1-碧海潮生.asf",
            "FlyingSound=武_碧海潮生.wav",
            "VanishImage=mag012-1-碧海潮生.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "Parasitic=1",
            "ParasiticInterval=160",
            "ParasiticMaxEffect=0",
            "ParasiticMagic=mod_test_magic_collision_peer.ini",
            "NoExplodeWhenLifeFrameEnd=1",
            "",
            "[Level1]",
            "Effect=8",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=2",
            "Speed=7",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_range_speedup_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_RANGE_SPEEDUP",
            "Intro=MOD test range speed-up owner fixture.",
            "Effect=0",
            "Speed=0",
            "MoveKind=11",
            "Region=1",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag009-风雷九州.asf",
            "Icon=mag009-风雷九州s.asf",
            "WaitFrame=0",
            "LifeFrame=70",
            "FlyingImage=mag009-1-风雷九州.asf",
            "FlyingSound=",
            "VanishImage=mag009-1-风雷九州.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "RangeEffect=1",
            "RangeRadius=4",
            "RangeSpeedUp=45",
            "RangeTimeInterval=100",
            "NoExplodeWhenLifeFrameEnd=1",
            "",
            "[Level1]",
            "Effect=0",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=11",
            "Region=1",
            "Speed=0",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_range_attack_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_RANGE_ATTACK",
            "Intro=MOD test range attack status and damage fixture.",
            "Effect=0",
            "Speed=0",
            "MoveKind=11",
            "Region=1",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag009-风雷九州.asf",
            "Icon=mag009-风雷九州s.asf",
            "WaitFrame=0",
            "LifeFrame=70",
            "FlyingImage=mag009-1-风雷九州.asf",
            "FlyingSound=",
            "VanishImage=mag009-1-风雷九州.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "RangeEffect=1",
            "RangeRadius=4",
            "RangeTimeInterval=100",
            "NoExplodeWhenLifeFrameEnd=1",
            "",
            "[Level1]",
            "Effect=0",
            "Effect2=17",
            "Effect3=13",
            "EffectMana=15",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=11",
            "Region=1",
            "Speed=0",
            "AttackRadius=1",
            "RangeFreeze=900",
            "RangeDamage=25",
            "",
        ]
    )


def make_magic_range_attack_all_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_RANGE_ATTACK_ALL",
            "Intro=MOD test AttackAll range status fixture.",
            "Effect=0",
            "Speed=0",
            "MoveKind=11",
            "Region=1",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag009-风雷九州.asf",
            "Icon=mag009-风雷九州s.asf",
            "WaitFrame=0",
            "LifeFrame=70",
            "FlyingImage=mag009-1-风雷九州.asf",
            "FlyingSound=",
            "VanishImage=mag009-1-风雷九州.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "AttackAll=1",
            "RangeEffect=1",
            "RangeRadius=4",
            "RangeTimeInerval=100",
            "NoExplodeWhenLifeFrameEnd=1",
            "",
            "[Level1]",
            "Effect=0",
            "Effect2=0",
            "Effect3=0",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=11",
            "Region=1",
            "Speed=0",
            "AttackRadius=1",
            "RangeFreeze=600",
            "",
        ]
    )


def make_magic_bounce_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_BOUNCE",
        "mag001-衡山有雪.asf",
        "mag001-衡山有雪s.asf",
        "mag001-1-衡山有雪.asf",
        "mag001-2-衡山有雪.asf",
        "武_衡山有雪.wav",
        2,
        7,
        36,
        ["Bounce=120", "BounceHurt=5", "NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_bouncefly_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_BOUNCEFLY",
        "mag012-碧海潮生.asf",
        "mag012-碧海潮生s.asf",
        "mag046-1-投石惊浪.asf",
        "mag046-2-投石惊浪.asf",
        "武_投石惊浪.wav",
        2,
        7,
        36,
        [
            "BounceFly=3",
            "BounceFlySpeed=32",
            "BounceFlyEndMagic=mod_test_magic_collision_peer.ini",
            "MagicDirectionWhenBounceFlyEnd=1",
            "BounceFlyEndHurt=3",
            "BounceFlyTouchHurt=2",
            "NoExplodeWhenLifeFrameEnd=1",
        ],
    )


def make_magic_bounce_handoff_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_BOUNCE_HANDOFF",
        "mag001-衡山有雪.asf",
        "mag001-衡山有雪s.asf",
        "mag001-1-衡山有雪.asf",
        "mag001-2-衡山有雪.asf",
        "武_衡山有雪.wav",
        2,
        8,
        80,
        ["Bounce=420", "BounceHurt=0", "NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_bouncefly_handoff_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_BOUNCEFLY_HANDOFF",
        "mag012-碧海潮生.asf",
        "mag012-碧海潮生s.asf",
        "mag046-1-投石惊浪.asf",
        "mag046-2-投石惊浪.asf",
        "武_投石惊浪.wav",
        2,
        8,
        40,
        [
            "BounceFly=1",
            "BounceFlySpeed=16",
            "BounceFlyEndHurt=0",
            "BounceFlyTouchHurt=0",
            "NoExplodeWhenLifeFrameEnd=1",
        ],
    )


def make_magic_carry_user4_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_CARRY_USER4",
        "mag012-碧海潮生.asf",
        "mag012-碧海潮生s.asf",
        "mag046-1-投石惊浪.asf",
        "mag046-2-投石惊浪.asf",
        "武_投石惊浪.wav",
        2,
        1,
        80,
        ["CarryUser=4", "CarryUserSpriteIndex=0", "HideUserWhenCarry=0", "NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_carry_user4_hidden_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_CARRY_USER4_HIDDEN",
        "mag012-碧海潮生.asf",
        "mag012-碧海潮生s.asf",
        "mag046-1-投石惊浪.asf",
        "mag046-2-投石惊浪.asf",
        "武_投石惊浪.wav",
        2,
        1,
        80,
        ["CarryUser=4", "CarryUserSpriteIndex=0", "HideUserWhenCarry=1", "NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_carry_user1_hidden_ini() -> str:
    return (
        make_magic_carry_user4_hidden_ini()
        .replace("MOD_TEST_CARRY_USER4_HIDDEN", "MOD_TEST_CARRY_USER1_HIDDEN")
        .replace("CarryUser=4", "CarryUser=1", 1)
    )


def make_magic_discard_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_DISCARD_OPPOSITE",
        "mag001-衡山有雪.asf",
        "mag001-衡山有雪s.asf",
        "mag001-1-衡山有雪.asf",
        "mag001-2-衡山有雪.asf",
        "武_衡山有雪.wav",
        2,
        1,
        80,
        ["DiscardOppositeMagic=1", "NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_exchange_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_EXCHANGE_USER",
        "mag012-碧海潮生.asf",
        "mag012-碧海潮生s.asf",
        "mag012-1-碧海潮生.asf",
        "",
        "武_碧海潮生.wav",
        2,
        1,
        80,
        ["ExchangeUser=1", "NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_post_cast_parent_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_POST_CAST_PARENT",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        1,
        0,
        12,
        [
            "SecondMagicFile=mod_test_magic_post_cast_second.ini",
            "SecondMagicDelay=120",
            "RandMagicFile=mod_test_magic_post_cast_rand.ini",
            "RandMagicProbability=100",
            "SideEffectType=1",
            "SideEffectPercent=100",
            "SideEffectProbability=100",
            "JumpToTarget=1",
            "JumpMoveSpeed=12",
            "JumpEndMagic=mod_test_magic_post_cast_jump_end.ini",
            "NoExplodeWhenLifeFrameEnd=1",
        ],
        effect=20,
    )


def make_magic_post_cast_second_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_POST_CAST_SECOND",
        "mag001-衡山有雪.asf",
        "mag001-衡山有雪s.asf",
        "mag001-1-衡山有雪.asf",
        "mag001-2-衡山有雪.asf",
        "武_衡山有雪.wav",
        1,
        0,
        12,
        ["NoExplodeWhenLifeFrameEnd=1"],
        effect=40,
    )


def make_magic_post_cast_rand_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_POST_CAST_RAND",
        "mag012-碧海潮生.asf",
        "mag012-碧海潮生s.asf",
        "mag012-1-碧海潮生.asf",
        "",
        "武_碧海潮生.wav",
        1,
        0,
        12,
        ["NoExplodeWhenLifeFrameEnd=1"],
        effect=30,
    )


def make_magic_post_cast_jump_end_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_POST_CAST_JUMP_END",
        "mag012-碧海潮生.asf",
        "mag012-碧海潮生s.asf",
        "mag012-1-碧海潮生.asf",
        "",
        "武_碧海潮生.wav",
        1,
        0,
        12,
        ["NoExplodeWhenLifeFrameEnd=1"],
        effect=50,
    )


def make_magic_post_cast_die_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_POST_CAST_DIE",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        1,
        0,
        12,
        ["DieAfterUse=1", "NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_collision_peer_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_COLLISION_PEER",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        2,
        3,
        80,
        ["NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_explode_point_parent_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_EXPLODE_POINT_PARENT",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        1,
        0,
        4,
        ["ExplodeMagicFile=mod_test_magic_explode_child.ini"],
    )


def make_magic_explode_throw_parent_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_EXPLODE_THROW_PARENT",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        17,
        20,
        5,
        ["ExplodeMagicFile=mod_test_magic_explode_throw_child.ini"],
    )


def make_magic_explode_throw_suppressed_parent_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_EXPLODE_THROW_SUPPRESSED_PARENT",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        17,
        20,
        5,
        [
            "ExplodeMagicFile=mod_test_magic_explode_throw_child.ini",
            "NoExplodeWhenLifeFrameEnd=1",
        ],
    )


def make_magic_explode_child_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_EXPLODE_CHILD",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        2,
        0,
        200,
        ["NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_explode_throw_child_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_EXPLODE_THROW_CHILD",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        2,
        0,
        200,
        ["NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_partner_projectile_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_PARTNER_PROJECTILE",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        2,
        3,
        80,
        ["NoExplodeWhenLifeFrameEnd=1"],
        effect=80,
    )


def make_magic_collision_lethal_freeze_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_COLLISION_LETHAL_FREEZE",
            "Intro=MOD test lethal hit pre-damage status timing fixture.",
            "Speed=0",
            "MoveKind=1",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag009-风雷九州.asf",
            "Icon=mag009-风雷九州s.asf",
            "WaitFrame=0",
            "LifeFrame=20",
            "FlyingImage=mag009-1-风雷九州.asf",
            "FlyingSound=",
            "VanishImage=mag009-1-风雷九州.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "Effect=120000",
            "SpecialKind=1",
            "SpecialKindMilliSeconds=3000",
            "",
            "[Level1]",
            "Effect=120000",
            "Evade=200",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=1",
            "Speed=0",
            "AttackRadius=1",
            "SpecialKind=1",
            "SpecialKindMilliSeconds=3000",
            "",
        ]
    )


def make_magic_temp_relation_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_TEMP_RELATION",
            "Intro=MOD test ChangeToFriendMilliseconds temporary relation fixture.",
            "Speed=0",
            "MoveKind=1",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag009-风雷九州.asf",
            "Icon=mag009-风雷九州s.asf",
            "WaitFrame=0",
            "LifeFrame=20",
            "FlyingImage=mag009-1-风雷九州.asf",
            "FlyingSound=",
            "VanishImage=mag009-1-风雷九州.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "Effect=1",
            "MaxLevel=99",
            "ChangeToFriendMilliseconds=1600",
            "",
            "[Level1]",
            "Effect=1",
            "Evade=200",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=1",
            "Speed=0",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_status_duration_ini(display_name: str, special_kind: int, milliseconds: int) -> str:
    return "\n".join(
        [
            "[Init]",
            f"Name={display_name}",
            "Intro=MOD test non-MoveKind=2 SpecialKindMilliSeconds status fixture.",
            "Speed=0",
            "MoveKind=1",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag009-风雷九州.asf",
            "Icon=mag009-风雷九州s.asf",
            "WaitFrame=0",
            "LifeFrame=20",
            "FlyingImage=mag009-1-风雷九州.asf",
            "FlyingSound=",
            "VanishImage=mag009-1-风雷九州.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "Effect=1",
            f"SpecialKind={special_kind}",
            f"SpecialKindMilliSeconds={milliseconds}",
            "",
            "[Level1]",
            "Effect=1",
            "Evade=200",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=1",
            "Speed=0",
            "AttackRadius=1",
            f"SpecialKind={special_kind}",
            f"SpecialKindMilliSeconds={milliseconds}",
            "",
        ]
    )


def make_magic_status_duration_freeze_ini() -> str:
    return make_magic_status_duration_ini("MOD_TEST_STATUS_DURATION_FREEZE", 1, 3200)


def make_magic_status_duration_short_freeze_ini() -> str:
    return make_magic_status_duration_ini("MOD_TEST_STATUS_DURATION_SHORT_FREEZE", 1, 1200)


def make_magic_status_duration_poison_ini() -> str:
    return make_magic_status_duration_ini("MOD_TEST_STATUS_DURATION_POISON", 2, 4100)


def make_magic_status_duration_petrify_ini() -> str:
    return make_magic_status_duration_ini("MOD_TEST_STATUS_DURATION_PETRIFY", 3, 5300)


def make_magic_equipment_fly_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_EQUIPMENT_FLY",
        "mag001-衡山有雪.asf",
        "mag001-衡山有雪s.asf",
        "mag001-1-衡山有雪.asf",
        "mag001-2-衡山有雪.asf",
        "武_衡山有雪.wav",
        2,
        7,
        34,
        ["NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_equipment_fly2_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_EQUIPMENT_FLY2",
        "mag012-碧海潮生.asf",
        "mag012-碧海潮生s.asf",
        "mag046-1-投石惊浪.asf",
        "mag046-2-投石惊浪.asf",
        "武_投石惊浪.wav",
        2,
        8,
        36,
        ["MeteorMove=4", "MeteorMoveDir=5"],
    )


def make_magic_goods_bound_ini() -> str:
    return (
        make_magic_equipment_fly_ini()
        .replace("Name=MOD_TEST_EQUIPMENT_FLY", "Name=MOD_TEST_GOODS_BOUND_MAGIC")
        .replace(
            "NoExplodeWhenLifeFrameEnd=1",
            "GoodsName=mod_test_goods_bound_ammo.ini\nNoExplodeWhenLifeFrameEnd=1",
        )
    )


def make_magic_goods_script_book_ini() -> str:
    return make_magic_equipment_fly_ini().replace(
        "Name=MOD_TEST_EQUIPMENT_FLY",
        "Name=MOD_TEST_GOODS_SCRIPT_BOOK_MAGIC",
    )


def make_magic_equipment_counter_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_EQUIPMENT_COUNTER",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        2,
        5,
        38,
        ["NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_learned_passive_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_LEARNED_PASSIVE",
        "mag012-碧海潮生.asf",
        "mag012-碧海潮生s.asf",
        "mag046-1-投石惊浪.asf",
        "mag046-2-投石惊浪.asf",
        "",
        2,
        0,
        20,
        [
            "FlyIni2=mod_test_magic_equipment_fly2.ini",
            "MagicToUseWhenBeAttacked=mod_test_magic_equipment_counter.ini",
            "MagicDirectionWhenBeAttacked=2",
        ],
    )


def make_magic_change_hit_base_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_CHANGE_HIT_BASE",
            "Intro=MOD test HitCountToChangeMagic base fixture.",
            "Speed=0",
            "MoveKind=1",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag009-风雷九州.asf",
            "Icon=mag009-风雷九州s.asf",
            "WaitFrame=0",
            "LifeFrame=16",
            "FlyingImage=mag009-1-风雷九州.asf",
            "FlyingSound=",
            "VanishImage=mag009-1-风雷九州.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "HitCountToChangeMagic=2",
            "ChangeMagic=mod_test_magic_change_hit_power.ini",
            "HitCountFlyingImage=mag009-1-风雷九州.asf",
            "HitCountVanishImage=mag009-1-风雷九州.asf",
            "HitCountFlyRadius=26",
            "HitCountFlyAngleSpeed=180",
            "NoExplodeWhenLifeFrameEnd=1",
            "Effect=12",
            "",
            "[Level1]",
            "Effect=12",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=1",
            "Speed=0",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_change_hit_power_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_CHANGE_HIT_POWER",
            "Intro=MOD test ChangeMagic high damage fixture.",
            "Speed=0",
            "MoveKind=1",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag012-碧海潮生.asf",
            "Icon=mag012-碧海潮生s.asf",
            "WaitFrame=0",
            "LifeFrame=16",
            "FlyingImage=mag012-1-碧海潮生.asf",
            "FlyingSound=",
            "VanishImage=mag012-1-碧海潮生.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "NoExplodeWhenLifeFrameEnd=1",
            "Effect=120",
            "",
            "[Level1]",
            "Effect=120",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=1",
            "Speed=0",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_equipment_power_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_EQUIPMENT_POWER",
            "Type=Attack",
            "Intro=MOD test equipment magic effect bonus fixture.",
            "Speed=0",
            "MoveKind=1",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag001-衡山有雪.asf",
            "Icon=mag001-衡山有雪s.asf",
            "WaitFrame=0",
            "LifeFrame=20",
            "FlyingImage=mag001-1-衡山有雪.asf",
            "FlyingSound=武_衡山有雪.wav",
            "VanishImage=mag001-2-衡山有雪.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "Effect=100",
            "",
            "[Level1]",
            "Effect=100",
            "Evade=200",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=1",
            "Speed=0",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_equipment_replace_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_EQUIPMENT_REPLACE",
            "Type=Attack",
            "Intro=MOD test equipment ReplaceMagic replacement fixture.",
            "Speed=0",
            "MoveKind=1",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag012-碧海潮生.asf",
            "Icon=mag012-碧海潮生s.asf",
            "WaitFrame=0",
            "LifeFrame=20",
            "FlyingImage=mag012-1-碧海潮生.asf",
            "FlyingSound=武_碧海潮生.wav",
            "VanishImage=mag012-1-碧海潮生.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "Effect=260",
            "",
            "[Level1]",
            "Effect=260",
            "Evade=200",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=1",
            "Speed=0",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_shop_death_kill_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_SHOP_DEATH_KILL",
            "Intro=MOD test shop-owner death transfer point magic fixture.",
            "MoveKind=1",
            "Speed=0",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag012-碧海潮生.asf",
            "Icon=mag012-碧海潮生s.asf",
            "WaitFrame=0",
            "LifeFrame=20",
            "FlyingImage=mag012-1-碧海潮生.asf",
            "FlyingSound=",
            "VanishImage=mag012-1-碧海潮生.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "Effect=200",
            "",
            "[Level1]",
            "Effect=200",
            "Evade=200",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "MoveKind=1",
            "Speed=0",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_equipment_additional_freeze_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_EQUIPMENT_ADDITIONAL_FREEZE",
        "mag001-衡山有雪.asf",
        "mag001-衡山有雪s.asf",
        "mag001-1-衡山有雪.asf",
        "mag001-2-衡山有雪.asf",
        "",
        1,
        0,
        20,
        ["Effect=0", "AdditionalEffect=1"],
        0,
    )


def make_magic_ai_low_life_self_ini() -> str:
    return "\n".join(
        [
            "[Init]",
            "Name=MOD_TEST_AI_LOW_LIFE_SELF",
            "Intro=MOD test AI low-life self magic fixture.",
            "MoveKind=13",
            "SpecialKind=1",
            "Speed=0",
            "Region=0",
            "AlphaBlend=1",
            "FlyingLum=15",
            "VanishLum=15",
            "Image=mag009-风雷九州.asf",
            "Icon=mag009-风雷九州s.asf",
            "WaitFrame=4",
            "LifeFrame=0",
            "FlyingImage=mag009-1-风雷九州.asf",
            "FlyingSound=",
            "VanishImage=mag009-1-风雷九州.asf",
            "VanishSound=",
            "Belong=0",
            "AttackRadius=1",
            "Effect=20",
            "",
            "[Level1]",
            "Effect=20",
            "ManaCost=0",
            "ThewCost=0",
            "LevelupExp=0",
            "Speed=0",
            "AttackRadius=1",
            "",
        ]
    )


def make_magic_ai_death_burst_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_AI_DEATH_BURST",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        15,
        0,
        32,
        ["NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_ai_friend_death_attack_ini() -> str:
    return make_magic_fixture_ini(
        "MOD_TEST_AI_FRIEND_DEATH_ATTACK",
        "mag009-风雷九州.asf",
        "mag009-风雷九州s.asf",
        "mag009-1-风雷九州.asf",
        "mag009-1-风雷九州.asf",
        "",
        2,
        7,
        32,
        ["NoExplodeWhenLifeFrameEnd=1"],
    )


def make_magic_collision_target_npc_ini() -> str:
    return make_magic_collision_npc_ini(
        "MOD_TEST_COLLISION_TARGET",
        "101",
        "MOD_TEST collision target",
    )


def make_magic_critical_target_npc_ini() -> str:
    return make_magic_collision_npc_ini(
        "MOD_TEST_CRITICAL_TARGET",
        "163",
        "MOD_TEST MG critical feedback target",
    )


def make_magic_collision_blocker_npc_ini() -> str:
    return make_magic_collision_npc_ini(
        "MOD_TEST_COLLISION_BLOCKER",
        "103",
        "MOD_TEST collision blocker",
    )


def make_magic_collision_blocker2_npc_ini() -> str:
    return make_magic_collision_npc_ini(
        "MOD_TEST_COLLISION_BLOCKER_2",
        "104",
        "MOD_TEST collision blocker 2",
    )


def make_magic_collision_friend_npc_ini() -> str:
    return make_magic_collision_npc_ini(
        "MOD_TEST_COLLISION_FRIEND",
        "105",
        "MOD_TEST collision friend",
        "0",
    )


def make_magic_collision_partner_npc_ini() -> str:
    return make_magic_collision_npc_ini(
        "MOD_TEST_COLLISION_PARTNER",
        "109",
        "MOD_TEST collision partner",
        "0",
        kind="3",
    )


def make_magic_trace_nonfighter_npc_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_TRACE_NONFIGHTER",
        0,
        0,
        108,
        "MOD_TEST trace nonfighter",
    )


def make_magic_damage_channels_target_npc_ini() -> str:
    return make_magic_collision_npc_ini(
        "MOD_TEST_DAMAGE_CHANNELS_TARGET",
        "106",
        "MOD_TEST damage channels target",
        "1",
        ["Defend2=5", "Defend3=3"],
    )


def make_magic_restore_target_npc_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "Name=MOD_TEST_RESTORE_TARGET",
            "NpcIni=npcres018_金兀术.ini",
            "FlyIni=",
            "BodyIni=",
            "Kind=1",
            "Relation=1",
            "Group=107",
            "Life=99950",
            "LifeMax=99999",
            "Thew=100",
            "ThewMax=100",
            "Mana=100",
            "ManaMax=100",
            "Attack=0",
            "Defence=0",
            "Evade=0",
            "Exp=0",
            "WalkSpeed=1",
            "Dir=0",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            "NoDropWhenDie=1",
            "DeathScript=",
            "ScriptFile=",
            "TalkContent=MOD_TEST restore target",
            "",
        ]
    )


def make_magic_collision_npc_ini(
    name: str,
    group: str,
    talk_content: str,
    relation: str = "1",
    extra_init: list[str] | None = None,
    kind: str = "1",
) -> str:
    extra_init = extra_init or []
    return "\n".join(
        [
            "[INIT]",
            f"Name={name}",
            "NpcIni=npcres018_金兀术.ini",
            "FlyIni=",
            "BodyIni=",
            f"Kind={kind}",
            f"Relation={relation}",
            f"Group={group}",
            "Life=99999",
            "LifeMax=99999",
            "Thew=100",
            "ThewMax=100",
            "Mana=100",
            "ManaMax=100",
            "Attack=0",
            "Defence=0",
            *extra_init,
            "Evade=0",
            "Exp=0",
            "WalkSpeed=1",
            "Dir=0",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            "NoDropWhenDie=1",
            "DeathScript=",
            "ScriptFile=",
            f"TalkContent={talk_content}",
            "",
        ]
    )


def make_npc_kind_talent_npc_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "Name=MOD_TEST_NPC_KIND_TALENT",
            "NpcIni=npcres018_金兀术.ini",
            "FlyIni=",
            "BodyIni=",
            "Kind=0",
            "Relation=0",
            "Group=160",
            "Life=100",
            "LifeMax=100",
            "Thew=100",
            "ThewMax=100",
            "Mana=100",
            "ManaMax=100",
            "Attack=0",
            "Defence=0",
            "Evade=0",
            "Exp=0",
            "WalkSpeed=1",
            "Dir=0",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "KindValue=500",
            "KindValueMax=1000",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            "NoDropWhenDie=1",
            "DeathScript=",
            "ScriptFile=",
            "TalkContent=MOD_TEST npc kind talent",
            "",
        ]
    )


def make_npc_signal_tip_npc_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "Name=MOD_TEST_NPC_SIGNAL_TIP",
            "NpcIni=npcres018_金兀术.ini",
            "FlyIni=",
            "BodyIni=",
            "Kind=0",
            "Relation=0",
            "Group=161",
            "Life=100",
            "LifeMax=100",
            "Thew=100",
            "ThewMax=100",
            "Mana=100",
            "ManaMax=100",
            "Attack=0",
            "Defence=0",
            "Evade=0",
            "Exp=0",
            "WalkSpeed=1",
            "Dir=0",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "IsSignalShow=0",
            "SignalIndex=0",
            "SignalType=",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            "NoDropWhenDie=1",
            "DeathScript=",
            "ScriptFile=",
            "TalkContent=MOD_TEST npc signal tip",
            "",
        ]
    )


def make_script_sound_position_npc_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "Name=MOD_TEST_SCRIPT_SOUND_NPC",
            "NpcIni=npcres018_金兀术.ini",
            "FlyIni=",
            "BodyIni=",
            "Kind=0",
            "Relation=0",
            "Group=162",
            "Life=100",
            "LifeMax=100",
            "Thew=100",
            "ThewMax=100",
            "Mana=100",
            "ManaMax=100",
            "Attack=0",
            "Defence=0",
            "Evade=0",
            "Exp=0",
            "WalkSpeed=1",
            "Dir=0",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            "NoDropWhenDie=1",
            "DeathScript=",
            "ScriptFile=mod_test_script_sound_position_npc.txt",
            "ScriptFileRight=",
            "TalkContent=MOD_TEST script sound npc",
            "",
        ]
    )


def make_magic_collision_caster_npc_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "Name=MOD_TEST_COLLISION_CASTER",
            "NpcIni=npcres018_金兀术.ini",
            "FlyIni=mod_test_magic_collision_peer.ini",
            "BodyIni=",
            "Kind=1",
            "Relation=1",
            "Group=102",
            "Life=99999",
            "LifeMax=99999",
            "Thew=100",
            "ThewMax=100",
            "Mana=100",
            "ManaMax=100",
            "Attack=0",
            "Defence=0",
            "Evade=500",
            "Exp=0",
            "AttackLevel=1",
            "MagicLevel=1",
            "AttackRadius=8",
            "WalkSpeed=1",
            "Dir=4",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            "NoDropWhenDie=1",
            "DeathScript=",
            "ScriptFile=",
            "TalkContent=MOD_TEST collision caster",
            "",
        ]
    )


def make_magic_summon_npc_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_SUMMON_NPC",
        1,
        1,
        104,
        "MOD_TEST summon npc",
        life=100,
        life_max=100,
    )


def make_equipment_power_target_npc_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "Name=MOD_TEST_EQUIPMENT_POWER_TARGET",
            "NpcIni=npcres018_金兀术.ini",
            "FlyIni=",
            "BodyIni=",
            "Kind=1",
            "Relation=1",
            "Group=105",
            "Life=1000",
            "LifeMax=1000",
            "Thew=100",
            "ThewMax=100",
            "Mana=100",
            "ManaMax=100",
            "Attack=0",
            "Defence=0",
            "Evade=-100",
            "Exp=0",
            "WalkSpeed=1",
            "Dir=0",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            "NoDropWhenDie=1",
            "DeathScript=",
            "ScriptFile=",
            "TalkContent=MOD_TEST equipment power target",
            "",
        ]
    )


def make_npc_ai_fixture_ini(
    name: str,
    kind: int,
    relation: int,
    group: int,
    talk_content: str,
    extra_lines: list[str] | None = None,
    life: int = 99999,
    life_max: int = 99999,
    attack: int = 0,
    fly_ini: str = "",
    direction: int = 0,
    path_finder: int = 0,
    action: int = 0,
    no_auto_attack_player: int = 1,
    stop_finding_target: int = 1,
    evade: int = 0,
    npc_ini: str = "npcres001_独孤剑.ini",
) -> str:
    lines = [
        "[INIT]",
        f"Name={name}",
        f"NpcIni={npc_ini}",
        f"FlyIni={fly_ini}",
        "BodyIni=",
        f"Kind={kind}",
        f"Relation={relation}",
        f"Group={group}",
        f"Life={life}",
        f"LifeMax={life_max}",
        "Thew=100",
        "ThewMax=100",
        "Mana=100",
        "ManaMax=100",
        f"Attack={attack}",
        "Defence=0",
        f"Evade={evade}",
        "Exp=0",
        "WalkSpeed=1",
        f"Dir={direction}",
        "Lum=1",
        f"PathFinder={path_finder}",
        f"Action={action}",
        f"NoAutoAttackPlayer={no_auto_attack_player}",
        f"StopFindingTarget={stop_finding_target}",
        "NoDropWhenDie=1",
        "DeathScript=",
        "ScriptFile=",
        f"TalkContent={talk_content}",
    ]
    if extra_lines:
        lines.extend(extra_lines)
    lines.append("")
    return "\n".join(
        lines
    )


def make_test_hub_interactive_npc_ini(
    name: str,
    npc_ini: str,
    talk_content: str,
    script_file: str,
    group: int,
) -> str:
    return "\n".join(
        [
            "[INIT]",
            f"Name={name}",
            f"NpcIni={npc_ini}",
            "FlyIni=",
            "BodyIni=",
            "Kind=0",
            "Relation=0",
            f"Group={group}",
            "Life=99999",
            "LifeMax=99999",
            "Thew=100",
            "ThewMax=100",
            "Mana=100",
            "ManaMax=100",
            "Attack=0",
            "Defence=500",
            "Evade=500",
            "Exp=0",
            "WalkSpeed=1",
            "Dir=0",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "DialogRadius=4",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            "NoDropWhenDie=1",
            "DeathScript=",
            f"ScriptFile={script_file}",
            f"ScriptFileRight={script_file}",
            f"TalkContent={talk_content}",
            "",
        ]
    )


def make_test_hub_guide_npc_ini() -> str:
    return make_test_hub_interactive_npc_ini(
        "测试向导",
        "npcres001_独孤剑.ini",
        "测试向导：交谈可查看说明、重置战斗或进入武功训练场",
        "mod_test_hub_guide.txt",
        270,
    )


def make_test_hub_little_games_host_npc_ini() -> str:
    return make_test_hub_interactive_npc_ini(
        "小游戏掌柜",
        "npcres029_老王.ini",
        "小游戏掌柜：交谈可手动打开赌博、掷骰和钓鱼",
        "mod_test_hub_little_games.txt",
        271,
    )


def make_test_hub_partner_sword_ini() -> str:
    return make_npc_ai_fixture_ini(
        "试炼剑客",
        1,
        0,
        272,
        "试炼剑客：会跟随玩家并协助攻击敌人",
        [
            "AttackLevel=1",
            "AttackRadius=3",
            "AttackSpeed=1",
            "VisionRadius=10",
            "WalkSpeed=3",
        ],
        life=800,
        life_max=800,
        attack=20,
        fly_ini="mod_test_magic_ai_friend_death_attack.ini",
        stop_finding_target=0,
        evade=80,
        npc_ini="npcres001_独孤剑.ini",
    )


def make_test_hub_partner_heroine_ini() -> str:
    return make_npc_ai_fixture_ini(
        "试炼女侠",
        1,
        0,
        273,
        "试炼女侠：会跟随玩家并协助攻击敌人",
        [
            "AttackLevel=1",
            "AttackRadius=5",
            "AttackSpeed=1",
            "VisionRadius=10",
            "WalkSpeed=3",
        ],
        life=700,
        life_max=700,
        attack=18,
        fly_ini="mod_test_magic_ai_friend_death_attack.ini",
        stop_finding_target=0,
        evade=100,
        npc_ini="npcres004_张琳心.ini",
    )


def make_test_hub_enemy_melee_ini() -> str:
    return make_npc_ai_fixture_ini(
        "演武刀客",
        1,
        1,
        274,
        "演武刀客：靠近后会主动追击玩家",
        [
            "AttackLevel=1",
            "AttackRadius=1",
            "AttackSpeed=1",
            "VisionRadius=8",
            "WalkSpeed=3",
        ],
        life=600,
        life_max=600,
        attack=8,
        no_auto_attack_player=0,
        stop_finding_target=0,
        evade=20,
        npc_ini="npcres044_许大马棒.ini",
    )


def make_test_hub_enemy_ranged_ini() -> str:
    return make_npc_ai_fixture_ini(
        "演武弓手",
        1,
        1,
        274,
        "演武弓手：靠近后会主动使用远程武功",
        [
            "AttackLevel=1",
            "AttackRadius=5",
            "AttackSpeed=1",
            "VisionRadius=8",
            "WalkSpeed=2",
        ],
        life=500,
        life_max=500,
        attack=7,
        fly_ini="mod_test_magic_ai_friend_death_attack.ini",
        no_auto_attack_player=0,
        stop_finding_target=0,
        evade=30,
        npc_ini="npcres053_成都杀手远程.ini",
    )


def make_npc_drop_fixture_ini(name: str, drop_ini: str = "", no_drop_when_die: int = 0) -> str:
    return "\n".join(
        [
            "[INIT]",
            f"Name={name}",
            "NpcIni=npcres001_独孤剑.ini",
            "FlyIni=",
            "BodyIni=",
            "Kind=1",
            "Relation=1",
            "Group=260",
            "Life=30",
            "LifeMax=30",
            "Thew=100",
            "ThewMax=100",
            "Mana=100",
            "ManaMax=100",
            "Attack=0",
            "Defence=0",
            "Evade=0",
            "Exp=0",
            "WalkSpeed=1",
            "Dir=0",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            f"DropIni={drop_ini}",
            f"NoDropWhenDie={no_drop_when_die}",
            "DeathScript=",
            "ScriptFile=",
            f"TalkContent={name}",
            "",
        ]
    )


def make_npc_default_drop_boss_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "Name=MOD_TEST_DEFAULT_DROP_BOSS",
            "NpcIni=npcres001_独孤剑.ini",
            "FlyIni=",
            "BodyIni=",
            "Kind=1",
            "Relation=1",
            "Group=261",
            "Life=30",
            "LifeMax=30",
            "Thew=100",
            "ThewMax=100",
            "Mana=100",
            "ManaMax=100",
            "Attack=0",
            "Defence=0",
            "Evade=0",
            "Exp=0",
            "ExpBonus=1",
            "Level=12",
            "WalkSpeed=1",
            "Dir=0",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            "DropIni=",
            "NoDropWhenDie=0",
            "DeathScript=",
            "ScriptFile=",
            "TalkContent=MOD_TEST_DEFAULT_DROP_BOSS",
            "",
        ]
    )


def make_default_drop_pickup_script(script_variable: str) -> str:
    return "\n".join(
        [
            f'assign("{script_variable}", 1);',
            'assign("mod_test_npc_default_drop_script", 1);',
            "delcurobj();",
            "return;",
            "",
        ]
    )


def make_npc_drop_script() -> str:
    return "\n".join(
        [
            'displaymessage("NPC 掉落测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_npc_drop_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            "enabledrop();",
            "setplayerpos(34,20);",
            'delnpc("MOD_TEST_DROP_NPC");',
            'delnpc("MOD_TEST_NO_DROP_NPC");',
            'delnpc("MOD_TEST_DEFAULT_DROP_BOSS");',
            'delobj("MOD_TEST_DROP_TOKEN");',
            'delobj("MOD_TEST_DROP_BLOCKED_TOKEN");',
            'delobj("MOD_TEST_DEFAULT_DROP_WEAPON");',
            'delobj("MOD_TEST_DEFAULT_DROP_ARMOR");',
            'assign("mod_test_npc_default_drop_weapon_exists", 0);',
            'assign("mod_test_npc_default_drop_armor_exists", 0);',
            'assign("mod_test_npc_default_drop_object_exists", 0);',
            'assign("mod_test_npc_default_drop_weapon_script", 0);',
            'assign("mod_test_npc_default_drop_armor_script", 0);',
            'assign("mod_test_npc_default_drop_script", 0);',
            'addnpc("mod_test_drop_npc.ini", 36, 20, 0);',
            'getnpcstate("MOD_TEST_DROP_NPC", "NoDropWhenDie", "mod_test_npc_drop_no_drop_flag_before");',
            'setdropini("MOD_TEST_DROP_NPC", "mod_test_drop_table.ini[100]");',
            'setnpcaction("MOD_TEST_DROP_NPC", 11);',
            "sleep(3600);",
            'getobjstate("MOD_TEST_DROP_TOKEN", "Exists", "mod_test_npc_drop_token_exists");',
            'getobjstate("MOD_TEST_DROP_TOKEN", "IsDrop", "mod_test_npc_drop_token_is_drop");',
            'getobjstate("MOD_TEST_DROP_TOKEN", "MapX", "mod_test_npc_drop_token_x");',
            'getobjstate("MOD_TEST_DROP_TOKEN", "MapY", "mod_test_npc_drop_token_y");',
            'assign("mod_test_npc_drop_token_position", 0);',
            'if getvar("mod_test_npc_drop_token_x") == 36 and getvar("mod_test_npc_drop_token_y") == 20 then assign("mod_test_npc_drop_token_position", 1) end',
            'addnpc("mod_test_no_drop_npc.ini", 37, 20, 0);',
            'getnpcstate("MOD_TEST_NO_DROP_NPC", "NoDropWhenDie", "mod_test_npc_drop_no_drop_flag");',
            'setnpcaction("MOD_TEST_NO_DROP_NPC", 11);',
            "sleep(3600);",
            'getobjstate("MOD_TEST_DROP_BLOCKED_TOKEN", "Exists", "mod_test_npc_drop_blocked_token_exists");',
            'assign("mod_test_npc_drop_no_drop_blocked", 0);',
            'if getvar("mod_test_npc_drop_blocked_token_exists") == 0 then assign("mod_test_npc_drop_no_drop_blocked", 1) end',
            'addnpc("mod_test_default_drop_boss.ini", 38, 20, 0);',
            'setnpcaction("MOD_TEST_DEFAULT_DROP_BOSS", 11);',
            "sleep(3600);",
            'getobjstate("MOD_TEST_DEFAULT_DROP_WEAPON", "Exists", "mod_test_npc_default_drop_weapon_exists");',
            'getobjstate("MOD_TEST_DEFAULT_DROP_ARMOR", "Exists", "mod_test_npc_default_drop_armor_exists");',
            'if getvar("mod_test_npc_default_drop_weapon_exists") == 1 or getvar("mod_test_npc_default_drop_armor_exists") == 1 then assign("mod_test_npc_default_drop_object_exists", 1) end',
            'runobjscript("MOD_TEST_DEFAULT_DROP_WEAPON");',
            'runobjscript("MOD_TEST_DEFAULT_DROP_ARMOR");',
            'delnpc("MOD_TEST_DROP_NPC");',
            'delnpc("MOD_TEST_NO_DROP_NPC");',
            'delnpc("MOD_TEST_DEFAULT_DROP_BOSS");',
            'delobj("MOD_TEST_DROP_TOKEN");',
            'delobj("MOD_TEST_DROP_BLOCKED_TOKEN");',
            'displaymessage("NPC 掉落测试完成");',
            "return;",
            "",
        ]
    )


def make_npc_ai_none_fighter_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_NONE_FIGHTER",
        1,
        3,
        201,
        "MOD_TEST none fighter fixture",
    )


def make_npc_ai_visible_variable_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_VISIBLE_VAR",
        1,
        1,
        218,
        "MOD_TEST visible variable NPC",
        extra_lines=[
            "VisibleVariableName=mod_test_npc_ai_visible_gate",
            "VisibleVariableValue=2",
        ],
    )


def make_magic_control_target_npc_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_CONTROL_TARGET",
        1,
        1,
        240,
        "MOD_TEST control target fixture",
        extra_lines=[
            "Level=1",
            "DialogRadius=4",
        ],
        life=9999,
        life_max=9999,
        attack=0,
    )


def make_magic_control_watcher_npc_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_CONTROL_WATCHER",
        1,
        1,
        240,
        "MOD_TEST control watcher fixture",
        extra_lines=[
            "AttackLevel=1",
            "AttackRadius=3",
            "AttackSpeed=8",
            "VisionRadius=8",
        ],
        life=500,
        life_max=500,
        attack=12,
        no_auto_attack_player=0,
        stop_finding_target=0,
    )


def make_magic_region_vtype_hit_npc_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_REGION_VTYPE_HIT",
        1,
        1,
        241,
        "MOD_TEST V-type hit target",
        life=99999,
        life_max=99999,
        attack=0,
    )


def make_magic_region_vtype_safe_npc_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_REGION_VTYPE_SAFE",
        1,
        1,
        242,
        "MOD_TEST V-type safe target",
        life=99999,
        life_max=99999,
        attack=0,
    )


def make_npc_ai_flyer_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_FLYER",
        7,
        2,
        202,
        "MOD_TEST flying animal fixture",
    )


def make_npc_ai_afraid_animal_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AFRAID_ANIMAL",
        6,
        2,
        246,
        "MOD_TEST afraid-player animal fixture",
        [
            "VisionRadius=5",
            "WalkSpeed=4",
        ],
    )


def make_npc_ai_partner_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_PARTNER",
        1,
        0,
        203,
        "MOD_TEST partner fixture",
    )


def make_goods_friend_drug_target_npc_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_GOODS_FRIEND_DRUG_TARGET",
        1,
        0,
        220,
        "MOD_TEST goods friend drug target",
        life=50,
        life_max=100,
    )


def make_goods_partner_drug_target_npc_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_GOODS_PARTNER_DRUG_TARGET",
        1,
        0,
        221,
        "MOD_TEST goods partner drug target",
        life=60,
        life_max=100,
    )


def make_npc_ai_event_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_EVENT",
        5,
        2,
        245,
        "MOD_TEST event fixture",
    )


def make_npc_ai_mover_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_MOVER",
        1,
        3,
        205,
        "MOD_TEST npc destination mover fixture",
    )


def make_time_stop_visual_mover_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_TIME_STOP_WALKER",
        1,
        3,
        262,
        "MOD_TEST time-stop visual walker fixture",
        direction=2,
    )


def make_npc_ai_path_normal_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_PATH_NORMAL",
        0,
        2,
        206,
        "MOD_TEST normal npc path type fixture",
    )


def make_npc_ai_path_event_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_PATH_EVENT",
        5,
        2,
        207,
        "MOD_TEST event npc path type fixture",
    )


def make_npc_ai_path_enemy_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_PATH_ENEMY",
        1,
        1,
        208,
        "MOD_TEST enemy path type fixture",
    )


def make_npc_ai_path_best_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_PATH_BEST",
        1,
        2,
        209,
        "MOD_TEST PathFinder=1 path type fixture",
        path_finder=1,
    )


def make_npc_ai_path_fixed_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_PATH_FIXED",
        1,
        2,
        210,
        "MOD_TEST FixedPos path type fixture",
        ["FixedPos=1F000000140000002000000014000000", "CurrPos=1"],
    )


def make_npc_ai_destination_blocker_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_DEST_BLOCKER",
        1,
        3,
        211,
        "MOD_TEST destination blocker fixture",
    )


def make_npc_ai_state_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_STATE",
        1,
        2,
        204,
        "MOD_TEST npc ai state fixture",
        [
            "AI_TYPE=2",
            "Idle=5",
            "AttackSpeed=8",
            "Level=4",
            "LevelUpExp=123",
            "CanLevelUp=1",
            "CanEquip=1",
            "AttackLevel=2",
            "MagicLevel=3",
            "ShowName=MOD_TEST_NPC_AI_STATE_DISPLAY",
            "ReviveMilliseconds=2500",
            "LifeMilliseconds=3000",
            "LifeLowPercent=50",
            "KeepRadiusWhenLifeLow=6",
            "KeepRadiusWhenFriendDeath=7",
            "MagicToUseWhenLifeLow=mod_test_magic_equipment_counter.ini",
            "MagicToUseWhenBeAttacked=mod_test_magic_equipment_counter.ini",
            "MagicDirectionWhenBeAttacked=2",
            "MagicToUseWhenDeath=mod_test_magic_equipment_counter.ini",
            "MagicDirectionWhenDeath=1",
            "HurtPlayerInterval=1200",
            "HurtPlayerLife=8",
            "HurtPlayerRadius=2",
            "AutoRunScript=0",
            "Arm=60",
            "EvadeN=61",
            "Gengu=62",
            "Neixi=63",
            "Physique=64",
            "Dodge_BeginFrame=10",
            "Dodge_EndFrame=20",
        ],
        life=45,
        life_max=100,
    )


def make_npc_ai_retreat_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_RETREAT",
        1,
        1,
        206,
        "MOD_TEST npc ai low-life retreat fixture",
        [
            "VisionRadius=10",
            "WalkSpeed=4",
            "LifeLowPercent=50",
            "KeepRadiusWhenLifeLow=5",
            "NoAutoAttackPlayer=0",
            "StopFindingTarget=0",
        ],
        life=40,
        life_max=100,
    )


def make_npc_ai_low_magic_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_LOW_MAGIC",
        1,
        2,
        207,
        "MOD_TEST npc ai low-life magic fixture",
        [
            "VisionRadius=0",
            "AttackLevel=1",
            "LifeLowPercent=50",
            "KeepRadiusWhenLifeLow=0",
            "MagicToUseWhenLifeLow=mod_test_magic_ai_low_life_self.ini",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
        ],
        life=40,
        life_max=100,
    )


def make_npc_ai_revive_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_REVIVE",
        1,
        3,
        208,
        "MOD_TEST npc ai revive fixture",
        [
            "ReviveMilliseconds=700",
        ],
        life=100,
        life_max=100,
    )


def make_npc_ai_noadd_body_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_NOADD_BODY",
        1,
        3,
        218,
        "MOD_TEST npc ai no-add-body death fixture",
        [
            "BodyIni=mod_test_npc_ai_body.ini",
            "VisionRadius=0",
        ],
        life=100,
        life_max=100,
    )


def make_npc_ai_death_caster_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_DEATH_CASTER",
        1,
        1,
        209,
        "MOD_TEST npc ai death magic caster fixture",
        [
            "AttackLevel=1",
            "MagicToUseWhenDeath=mod_test_magic_ai_death_burst.ini",
            "MagicDirectionWhenDeath=0",
        ],
        life=5,
        life_max=5,
        attack=7,
        direction=6,
    )


def make_npc_ai_death_target_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_DEATH_TARGET",
        1,
        0,
        210,
        "MOD_TEST npc ai death magic target fixture",
        [
            "AttackLevel=1",
            "AttackRadius=3",
            "AttackSpeed=8",
            "VisionRadius=0",
        ],
        life=100,
        life_max=100,
        attack=6,
        fly_ini="mod_test_magic_ai_friend_death_attack.ini",
        evade=200,
    )


def make_npc_ai_friend_attacker_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_FRIEND_ATTACKER",
        1,
        0,
        211,
        "MOD_TEST npc ai friend death attacker fixture",
        [
            "AttackLevel=1",
            "AttackRadius=3",
            "AttackSpeed=8",
            "VisionRadius=0",
        ],
        life=100,
        life_max=100,
        attack=6,
        fly_ini="mod_test_magic_ai_friend_death_attack.ini",
        evade=200,
    )


def make_npc_ai_friend_victim_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_FRIEND_VICTIM",
        1,
        1,
        212,
        "MOD_TEST npc ai friend death victim fixture",
        [
            "VisionRadius=0",
        ],
        life=5,
        life_max=5,
    )


def make_npc_ai_friend_watcher_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_FRIEND_WATCHER",
        1,
        1,
        213,
        "MOD_TEST npc ai friend death watcher fixture",
        [
            "VisionRadius=10",
            "WalkSpeed=4",
            "KeepRadiusWhenFriendDeath=5",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
        ],
        life=100,
        life_max=100,
    )


def make_npc_ai_friendly_neutral_attacker_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_FRIEND_NEUTRAL_ATTACKER",
        1,
        0,
        220,
        "MOD_TEST friendly AI should ignore true neutral target fixture",
        [
            "AttackLevel=1",
            "AttackRadius=3",
            "AttackSpeed=8",
            "VisionRadius=10",
        ],
        life=500,
        life_max=500,
        attack=20,
        fly_ini="mod_test_magic_ai_friend_death_attack.ini",
        no_auto_attack_player=0,
        stop_finding_target=0,
        evade=200,
    )


def make_npc_ai_true_neutral_target_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_TRUE_NEUTRAL_TARGET",
        1,
        2,
        221,
        "MOD_TEST true neutral fighter should not be attacked by friendly AI fixture",
        [
            "VisionRadius=0",
        ],
        life=500,
        life_max=500,
    )


def make_npc_ai_friend_none_target_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_FRIEND_NONE_TARGET",
        1,
        3,
        222,
        "MOD_TEST RelationType.None fighter should be attacked by friendly AI fixture",
        [
            "VisionRadius=0",
        ],
        life=500,
        life_max=500,
    )


def make_npc_ai_npcres_fallback_npc_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_NPCRES_FALLBACK",
        1,
        2,
        222,
        "MOD_TEST NPC resource file under ini/npc fallback fixture",
        [
            "VisionRadius=0",
        ],
        life=500,
        life_max=500,
        npc_ini="mod_test_npc_ai_npcres_fallback.ini",
    )


def make_npc_ai_npcres_priority_npc_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_NPCRES_PRIORITY",
        1,
        2,
        223,
        "MOD_TEST NPC resource standard path priority fixture",
        [
            "VisionRadius=0",
        ],
        life=500,
        life_max=500,
        npc_ini="mod_test_npc_ai_npcres_priority.ini",
    )


def make_npc_ai_npcres_fallback_res_ini() -> str:
    return "\n".join(
        [
            "[Stand]",
            "Image=npc001_st.asf",
            "Sound=",
            "",
            "[Walk]",
            "Image=npc001_st.asf",
            "Sound=",
            "",
        ]
    )


def make_npc_ai_npcres_priority_side_res_ini() -> str:
    return "\n".join(
        [
            "[Walk]",
            "Image=npc001_st.asf",
            "Sound=",
            "",
        ]
    )


def make_npc_ai_npcres_priority_standard_res_ini() -> str:
    return "\n".join(
        [
            "[Walk]",
            "Image=",
            "Sound=",
            "",
        ]
    )


def make_npc_ai_fight_res_npc_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_FIGHT_RES",
        1,
        2,
        224,
        "MOD_TEST NPC Fight* resource section fixture",
        [
            "VisionRadius=0",
        ],
        life=500,
        life_max=500,
        npc_ini="mod_test_npc_ai_fight_res.ini",
    )


def make_npc_ai_fight_res_ini() -> str:
    return "\n".join(
        [
            "[Stand]",
            "Image=npc001_st.asf",
            "Sound=",
            "",
            "[AStand]",
            "Image=",
            "Sound=",
            "",
            "[AWalk]",
            "Image=",
            "Sound=",
            "",
            "[ARun]",
            "Image=",
            "Sound=",
            "",
            "[AJump]",
            "Image=",
            "Sound=",
            "",
            "[FightStand]",
            "Image=npc001_st.asf",
            "Sound=",
            "",
            "[FightWalk]",
            "Image=npc001_st.asf",
            "Sound=",
            "",
            "[FightRun]",
            "Image=npc001_st.asf",
            "Sound=",
            "",
            "[FightJump]",
            "Image=npc001_st.asf",
            "Sound=",
            "",
        ]
    )


def make_npc_ai_partner_combat_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_PARTNER_COMBAT",
        1,
        0,
        214,
        "MOD_TEST npc ai partner combat fixture",
        [
            "AttackLevel=1",
            "AttackRadius=3",
            "AttackSpeed=8",
            "VisionRadius=10",
        ],
        life=500,
        life_max=500,
        attack=20,
        fly_ini="mod_test_magic_ai_friend_death_attack.ini",
        stop_finding_target=0,
        evade=200,
    )


def make_npc_ai_partner_target_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_PARTNER_TARGET",
        1,
        3,
        215,
        "MOD_TEST npc ai partner combat target fixture",
        [
            "VisionRadius=0",
        ],
        life=500,
        life_max=500,
    )


def make_npc_ai_blind_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_BLIND",
        1,
        1,
        216,
        "MOD_TEST npc ai blind fixture",
        [
            "VisionRadius=10",
            "NoAutoAttackPlayer=0",
            "StopFindingTarget=0",
        ],
        life=500,
        life_max=500,
        stop_finding_target=0,
    )


def make_npc_ai_global_ai_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_GLOBAL",
        1,
        1,
        217,
        "MOD_TEST npc ai global switch fixture",
        [
            "VisionRadius=10",
            "NoAutoAttackPlayer=0",
            "StopFindingTarget=0",
        ],
        life=500,
        life_max=500,
        stop_finding_target=0,
    )


def make_npc_ai_local_ai_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_LOCAL",
        1,
        1,
        218,
        "MOD_TEST npc ai local switch fixture",
        [
            "VisionRadius=10",
        ],
        life=500,
        life_max=500,
        no_auto_attack_player=0,
        stop_finding_target=0,
    )


def make_npc_ai_randwalk_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_RANDWALK",
        0,
        2,
        219,
        "MOD_TEST npc ai randwalk fixture",
        [
            "VisionRadius=10",
        ],
        action=1,
    )


def make_npc_ai_timer_context_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_NPC_AI_TIMER_CONTEXT",
        0,
        2,
        247,
        "MOD_TEST current NPC script context fixture",
        [
            "TimerScriptFile=mod_test_npc_ai_timer_context.txt",
            "TimerScriptInterval=120",
        ],
        direction=1,
    )


def make_script_return_leechcraft_npc_ini() -> str:
    return make_npc_ai_fixture_ini(
        "MOD_TEST_SCRIPT_RETURN_LEECHCRAFT",
        0,
        2,
        230,
        "MOD_TEST script return leechcraft fixture",
        [
            "Leechcraft=3",
        ],
    )


def make_magic_lifecycle_script() -> str:
    return "\n".join(
        [
            'displaymessage("武功生命周期测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_magic_lifecycle_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadnpc("map001.npc");',
            'loadobj("map001.obj");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            "fullmana();",
            "fullthew();",
            'addmagic("mod_test_magic_begin_follow.ini");',
            'addmagic("mod_test_magic_trace_enemy.ini");',
            'addmagic("mod_test_magic_random_move.ini");',
            'addmagic("mod_test_magic_meteor.ini");',
            'addmagic("mod_test_magic_round.ini");',
            'addmagic("mod_test_magic_move_imitate_user.ini");',
            'addmagic("mod_test_magic_moveback.ini");',
            'addmagic("mod_test_magic_time_stop.ini");',
            'addmagic("mod_test_magic_invisible_keep_hidden.ini");',
            'addmagic("mod_test_magic_invisible_visible_attack.ini");',
            'addmagic("mod_test_magic_morph_replace.ini");',
            'addmagic("mod_test_magic_learned_passive.ini");',
            'displaymessage("正在测试武功起始与跟随效果");',
            'usemagic("mod_test_magic_begin_follow.ini", 34, 14);',
            'assign("mod_test_magic_begin_follow", 1);',
            "sleep(700);",
            'displaymessage("正在测试追踪敌人的武功");',
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 34, 0);',
            'usemagic("mod_test_magic_trace_enemy.ini", 42, 20);',
            "sleep(60);",
            'geteffectstate("mod_test_magic_trace_enemy.ini", "ProjectileCount", "mod_test_magic_trace_enemy_projectile_count");',
            'geteffectstate("mod_test_magic_trace_enemy.ini", "ActiveProjectileFlyingDirectionX", "mod_test_magic_trace_enemy_fly_x_before");',
            'geteffectstate("mod_test_magic_trace_enemy.ini", "ActiveProjectileFlyingDirectionY", "mod_test_magic_trace_enemy_fly_y_before");',
            "sleep(80);",
            'geteffectstate("mod_test_magic_trace_enemy.ini", "ActiveProjectileFlyingDirectionX", "mod_test_magic_trace_enemy_fly_x_after");',
            'geteffectstate("mod_test_magic_trace_enemy.ini", "ActiveProjectileFlyingDirectionY", "mod_test_magic_trace_enemy_fly_y_after");',
            'assign("mod_test_magic_trace_enemy_far_target", 0);',
            'if getvar("mod_test_magic_trace_enemy_projectile_count") == 1 and getvar("mod_test_magic_trace_enemy_fly_y_after") > 0 and getvar("mod_test_magic_trace_enemy_fly_x_after") < 0 then assign("mod_test_magic_trace_enemy_far_target", 1) end',
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'displaymessage("正在测试陨落型武功");',
            'usemagic("mod_test_magic_meteor.ini", 36, 15);',
            'assign("mod_test_magic_meteor", 1);',
            "sleep(80);",
            'geteffectstate("mod_test_magic_meteor.ini", "ProjectileCount", "mod_test_magic_meteor_projectile_count");',
            'geteffectstate("mod_test_magic_meteor.ini", "ActiveProjectileMapX", "mod_test_magic_meteor_entry_x");',
            'geteffectstate("mod_test_magic_meteor.ini", "ActiveProjectileMapY", "mod_test_magic_meteor_entry_y");',
            'assign("mod_test_magic_meteor_entry_path", 0);',
            'if getvar("mod_test_magic_meteor_projectile_count") == 1 and getvar("mod_test_magic_meteor_entry_x") > 0 and getvar("mod_test_magic_meteor_entry_y") > 0 and (getvar("mod_test_magic_meteor_entry_x") ~= 34 or getvar("mod_test_magic_meteor_entry_y") ~= 20) then assign("mod_test_magic_meteor_entry_path", 1) end',
            "sleep(920);",
            "cleareffect();",
            'displaymessage("正在测试随机移动武功");',
            'usemagic("mod_test_magic_random_move.ini", 34, 12);',
            "sleep(80);",
            'geteffectstate("mod_test_magic_random_move.ini", "ProjectileCount", "mod_test_magic_random_projectile_count");',
            'geteffectstate("mod_test_magic_random_move.ini", "ActiveProjectileFlyingDirectionX", "mod_test_magic_random_fly_x_before");',
            'geteffectstate("mod_test_magic_random_move.ini", "ActiveProjectileFlyingDirectionY", "mod_test_magic_random_fly_y_before");',
            "sleep(420);",
            'geteffectstate("mod_test_magic_random_move.ini", "ActiveProjectileFlyingDirectionX", "mod_test_magic_random_fly_x_after");',
            'geteffectstate("mod_test_magic_random_move.ini", "ActiveProjectileFlyingDirectionY", "mod_test_magic_random_fly_y_after");',
            'assign("mod_test_magic_random_direction_changed", 0);',
            'if getvar("mod_test_magic_random_projectile_count") == 1 and getvar("mod_test_magic_random_fly_x_after") ~= getvar("mod_test_magic_random_fly_x_before") then assign("mod_test_magic_random_direction_changed", 1) end',
            'if getvar("mod_test_magic_random_projectile_count") == 1 and getvar("mod_test_magic_random_fly_y_after") ~= getvar("mod_test_magic_random_fly_y_before") then assign("mod_test_magic_random_direction_changed", 1) end',
            "cleareffect();",
            'displaymessage("正在测试环绕型武功");',
            'usemagic("mod_test_magic_round.ini", 34, 17);',
            'assign("mod_test_magic_round", 1);',
            "sleep(120);",
            'geteffectstate("mod_test_magic_round.ini", "Count", "mod_test_magic_round_count");',
            'geteffectstate("mod_test_magic_round.ini", "ProjectileCount", "mod_test_magic_round_projectile_count");',
            'geteffectstate("mod_test_magic_round.ini", "OffsetX", "mod_test_magic_round_offset_x_before");',
            'geteffectstate("mod_test_magic_round.ini", "OffsetY", "mod_test_magic_round_offset_y_before");',
            'geteffectstate("mod_test_magic_round.ini", "OffsetLength", "mod_test_magic_round_offset_length");',
            'assign("mod_test_magic_round_spawned", 0);',
            'if getvar("mod_test_magic_round_count") == 4 and getvar("mod_test_magic_round_projectile_count") == 4 and getvar("mod_test_magic_round_offset_length") > 0 then assign("mod_test_magic_round_spawned", 1) end',
            "sleep(520);",
            'geteffectstate("mod_test_magic_round.ini", "OffsetX", "mod_test_magic_round_offset_x_after");',
            'geteffectstate("mod_test_magic_round.ini", "OffsetY", "mod_test_magic_round_offset_y_after");',
            'assign("mod_test_magic_round_rotated", 0);',
            'if getvar("mod_test_magic_round_offset_x_after") > getvar("mod_test_magic_round_offset_x_before") then assign("mod_test_magic_round_rotated", 1) end',
            'if getvar("mod_test_magic_round_offset_x_before") > getvar("mod_test_magic_round_offset_x_after") then assign("mod_test_magic_round_rotated", 1) end',
            'if getvar("mod_test_magic_round_offset_y_after") > getvar("mod_test_magic_round_offset_y_before") then assign("mod_test_magic_round_rotated", 1) end',
            'if getvar("mod_test_magic_round_offset_y_before") > getvar("mod_test_magic_round_offset_y_after") then assign("mod_test_magic_round_rotated", 1) end',
            "cleareffect();",
            'displaymessage("正在测试模仿施法者移动");',
            "setplayerpos(34, 20);",
            'usemagic("mod_test_magic_move_imitate_user.ini", 34, 14);',
            "sleep(120);",
            'geteffectstate("mod_test_magic_move_imitate_user.ini", "ProjectileCount", "mod_test_magic_move_imitate_projectile_count");',
            'geteffectstate("mod_test_magic_move_imitate_user.ini", "ActiveProjectileMapX", "mod_test_magic_move_imitate_x_before");',
            'geteffectstate("mod_test_magic_move_imitate_user.ini", "ActiveProjectileMapY", "mod_test_magic_move_imitate_y_before");',
            'geteffectstate("mod_test_magic_move_imitate_user.ini", "ActiveProjectileOffsetX", "mod_test_magic_move_imitate_offset_x_before");',
            'geteffectstate("mod_test_magic_move_imitate_user.ini", "ActiveProjectileOffsetY", "mod_test_magic_move_imitate_offset_y_before");',
            "setplayerpos(36, 20);",
            "sleep(220);",
            'geteffectstate("mod_test_magic_move_imitate_user.ini", "ActiveProjectileMapX", "mod_test_magic_move_imitate_x_after");',
            'geteffectstate("mod_test_magic_move_imitate_user.ini", "ActiveProjectileMapY", "mod_test_magic_move_imitate_y_after");',
            'geteffectstate("mod_test_magic_move_imitate_user.ini", "ActiveProjectileOffsetX", "mod_test_magic_move_imitate_offset_x_after");',
            'geteffectstate("mod_test_magic_move_imitate_user.ini", "ActiveProjectileOffsetY", "mod_test_magic_move_imitate_offset_y_after");',
            'assign("mod_test_magic_move_imitate_moved", 0);',
            'if getvar("mod_test_magic_move_imitate_x_after") > getvar("mod_test_magic_move_imitate_x_before") then assign("mod_test_magic_move_imitate_moved", 1) end',
            'if getvar("mod_test_magic_move_imitate_y_after") > getvar("mod_test_magic_move_imitate_y_before") then assign("mod_test_magic_move_imitate_moved", 1) end',
            'if getvar("mod_test_magic_move_imitate_offset_x_after") > getvar("mod_test_magic_move_imitate_offset_x_before") then assign("mod_test_magic_move_imitate_moved", 1) end',
            'if getvar("mod_test_magic_move_imitate_offset_x_before") > getvar("mod_test_magic_move_imitate_offset_x_after") then assign("mod_test_magic_move_imitate_moved", 1) end',
            'if getvar("mod_test_magic_move_imitate_offset_y_after") > getvar("mod_test_magic_move_imitate_offset_y_before") then assign("mod_test_magic_move_imitate_moved", 1) end',
            'if getvar("mod_test_magic_move_imitate_offset_y_before") > getvar("mod_test_magic_move_imitate_offset_y_after") then assign("mod_test_magic_move_imitate_moved", 1) end',
            "cleareffect();",
            "setplayerpos(34, 20);",
            'displaymessage("正在测试武功回退");',
            'usemagic("mod_test_magic_moveback.ini", 31, 17);',
            'assign("mod_test_magic_moveback", 1);',
            "sleep(700);",
            'displaymessage("正在测试隐身效果");',
            'usemagic("mod_test_magic_invisible_keep_hidden.ini", 34, 20);',
            "sleep(200);",
            'getplayerstate("IsInvisibleByMagic", "mod_test_magic_invisible_keep_active");',
            'getplayerstate("IsVisibleWhenAttack", "mod_test_magic_invisible_keep_attack_flag");',
            'usemagic("mod_test_magic_begin_follow.ini", 34, 14);',
            "sleep(200);",
            'getplayerstate("IsInvisibleByMagic", "mod_test_magic_invisible_keep_after_action");',
            "sleep(1300);",
            'usemagic("mod_test_magic_invisible_visible_attack.ini", 34, 20);',
            "sleep(200);",
            'getplayerstate("IsInvisibleByMagic", "mod_test_magic_invisible_attack_active");',
            'getplayerstate("IsVisibleWhenAttack", "mod_test_magic_invisible_attack_flag");',
            'usemagic("mod_test_magic_begin_follow.ini", 34, 14);',
            "sleep(300);",
            'getplayerstate("IsInvisibleByMagic", "mod_test_magic_invisible_attack_after_action");',
            'assign("mod_test_magic_invisible_attack_cleared", 0);',
            'if getvar("mod_test_magic_invisible_attack_after_action") == 0 then assign("mod_test_magic_invisible_attack_cleared", 1) end',
            'displaymessage("正在测试变身替换效果");',
            'getplayerstate("HasActiveReplaceMagicList", "mod_test_magic_morph_replace_before");',
            'getplayerstate("VisibleMagicListCount", "mod_test_magic_morph_visible_before");',
            'usemagic("mod_test_magic_morph_replace.ini", 34, 20);',
            "sleep(200);",
            'getplayerstate("HasActiveReplaceMagicList", "mod_test_magic_morph_replace_active");',
            'getplayerstate("VisibleMagicListCount", "mod_test_magic_morph_visible_during");',
            'getplayerstate("PrimaryMagicListCount", "mod_test_magic_morph_primary_during");',
            'assign("mod_test_magic_morph_visible_two", 0);',
            'if getvar("mod_test_magic_morph_visible_during") == 2 then assign("mod_test_magic_morph_visible_two", 1) end',
            'assign("mod_test_magic_morph_primary_preserved", 0);',
            'if getvar("mod_test_magic_morph_primary_during") == getvar("mod_test_magic_morph_visible_before") then assign("mod_test_magic_morph_primary_preserved", 1) end',
            'addmagic("mod_test_magic_post_cast_die.ini");',
            'setmagiclevel("mod_test_magic_post_cast_die.ini", 2);',
            'getplayermagiclevel("mod_test_magic_post_cast_die.ini", "mod_test_magic_morph_primary_added_level");',
            'getplayerstate("VisibleMagicListCount", "mod_test_magic_morph_visible_after_primary_add");',
            'getplayerstate("PrimaryMagicListCount", "mod_test_magic_morph_primary_after_add");',
            'assign("mod_test_magic_morph_primary_add_recorded", 0);',
            'if getvar("mod_test_magic_morph_primary_after_add") == getvar("mod_test_magic_morph_primary_during") + 1 and getvar("mod_test_magic_morph_primary_added_level") == 2 then assign("mod_test_magic_morph_primary_add_recorded", 1) end',
            'assign("mod_test_magic_morph_visible_add_isolated", 0);',
            'if getvar("mod_test_magic_morph_visible_after_primary_add") == getvar("mod_test_magic_morph_visible_during") then assign("mod_test_magic_morph_visible_add_isolated", 1) end',
            "sleep(1000);",
            'getplayerstate("HasActiveReplaceMagicList", "mod_test_magic_morph_replace_after");',
            'getplayerstate("VisibleMagicListCount", "mod_test_magic_morph_visible_after");',
            'getplayerstate("PrimaryMagicListCount", "mod_test_magic_morph_primary_after");',
            'getplayermagiclevel("mod_test_magic_post_cast_die.ini", "mod_test_magic_morph_added_level_after");',
            'assign("mod_test_magic_morph_visible_restored", 0);',
            'if getvar("mod_test_magic_morph_visible_after") == getvar("mod_test_magic_morph_primary_after_add") then assign("mod_test_magic_morph_visible_restored", 1) end',
            'assign("mod_test_magic_morph_primary_add_restored_visible", 0);',
            'if getvar("mod_test_magic_morph_visible_after") == getvar("mod_test_magic_morph_visible_before") + 1 and getvar("mod_test_magic_morph_primary_after") == getvar("mod_test_magic_morph_primary_after_add") and getvar("mod_test_magic_morph_added_level_after") == 2 then assign("mod_test_magic_morph_primary_add_restored_visible", 1) end',
            'displaymessage("正在测试已学习的被动攻击");',
            "cleareffect();",
            'getplayerstate("AttackOptionCount", "mod_test_magic_learned_passive_options");',
            'assign("mod_test_magic_learned_passive_attack_overlay", 0);',
            'if getvar("mod_test_magic_learned_passive_options") >= 2 then assign("mod_test_magic_learned_passive_attack_overlay", 1) end',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'addnpc("mod_test_collision_caster_npc.ini", 34, 22, 0);',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            "fulllife();",
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_equipment_additional_freeze.ini", 34, 20, 1);',
            "sleep(900);",
            'geteffectstate("mod_test_magic_equipment_counter.ini", "Count", "mod_test_magic_learned_passive_counter_count");',
            'geteffectstate("mod_test_magic_equipment_counter.ini", "UserIsPlayer", "mod_test_magic_learned_passive_counter_user");',
            'assign("mod_test_magic_learned_passive_counter", 0);',
            'if getvar("mod_test_magic_learned_passive_counter_count") > 0 and getvar("mod_test_magic_learned_passive_counter_user") == 1 then assign("mod_test_magic_learned_passive_counter", 1) end',
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'displaymessage("正在测试清除玩家武功状态");',
            'assign("mod_test_magic_clear_effect_status", 1);',
            'assign("mod_test_magic_clear_effect_frozen", 0);',
            'assign("mod_test_magic_clear_effect_poison", 0);',
            'assign("mod_test_magic_clear_effect_petrify", 0);',
            "frozenmillisecond(2000);",
            'getplayerstate("IsFrozened", "mod_test_magic_clear_effect_frozen_before");',
            "cleareffect();",
            'getplayerstate("IsFrozened", "mod_test_magic_clear_effect_frozen_after");',
            'if getvar("mod_test_magic_clear_effect_frozen_before") == 1 and getvar("mod_test_magic_clear_effect_frozen_after") == 0 then assign("mod_test_magic_clear_effect_frozen", 1) end',
            "poisonmillisecond(2000);",
            'getplayerstate("IsPoisoned", "mod_test_magic_clear_effect_poison_before");',
            "cleareffect();",
            'getplayerstate("IsPoisoned", "mod_test_magic_clear_effect_poison_after");',
            'if getvar("mod_test_magic_clear_effect_poison_before") == 1 and getvar("mod_test_magic_clear_effect_poison_after") == 0 then assign("mod_test_magic_clear_effect_poison", 1) end',
            "petrifymillisecond(2000);",
            'getplayerstate("IsPetrified", "mod_test_magic_clear_effect_petrify_before");',
            "cleareffect();",
            'getplayerstate("IsPetrified", "mod_test_magic_clear_effect_petrify_after");',
            'if getvar("mod_test_magic_clear_effect_petrify_before") == 1 and getvar("mod_test_magic_clear_effect_petrify_after") == 0 then assign("mod_test_magic_clear_effect_petrify", 1) end',
            'getplayerstate("BodyFunctionWell", "mod_test_magic_clear_effect_body");',
            'displaymessage("正在测试清除伙伴武功状态");',
            'delnpc("MOD_TEST_NPC_PARTNER");',
            'addnpc("mod_test_npc_ai_partner.ini", 36, 21, 0);',
            'setnpcpartner("MOD_TEST_NPC_PARTNER");',
            'addnpcproperty("MOD_TEST_NPC_PARTNER", "FrozenMilliseconds", 2000);',
            'addnpcproperty("MOD_TEST_NPC_PARTNER", "PoisonedMilliseconds", 2000);',
            'addnpcproperty("MOD_TEST_NPC_PARTNER", "PetrifiedMilliseconds", 2000);',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "IsFrozened", "mod_test_magic_clear_effect_partner_frozen_before");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "IsPoisoned", "mod_test_magic_clear_effect_partner_poison_before");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "IsPetrified", "mod_test_magic_clear_effect_partner_petrify_before");',
            "cleareffect();",
            'getnpcstate("MOD_TEST_NPC_PARTNER", "IsFrozened", "mod_test_magic_clear_effect_partner_frozen_after");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "IsPoisoned", "mod_test_magic_clear_effect_partner_poison_after");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "IsPetrified", "mod_test_magic_clear_effect_partner_petrify_after");',
            'assign("mod_test_magic_clear_effect_partner_status", 0);',
            'if getvar("mod_test_magic_clear_effect_partner_frozen_before") == 1 and getvar("mod_test_magic_clear_effect_partner_poison_before") == 1 and getvar("mod_test_magic_clear_effect_partner_petrify_before") == 1 and getvar("mod_test_magic_clear_effect_partner_frozen_after") == 0 and getvar("mod_test_magic_clear_effect_partner_poison_after") == 0 and getvar("mod_test_magic_clear_effect_partner_petrify_after") == 0 then assign("mod_test_magic_clear_effect_partner_status", 1) end',
            'delnpc("MOD_TEST_NPC_PARTNER");',
            'displaymessage("正在测试时间停止武功");',
            'usemagic("mod_test_magic_time_stop.ini", 34, 20);',
            'geteffectstate("mod_test_magic_time_stop.ini", "Count", "mod_test_magic_time_stop_count_active");',
            'assign("mod_test_magic_time_stop_active", 0);',
            'if getvar("mod_test_magic_time_stop_count_active") > 0 then assign("mod_test_magic_time_stop_active", 1) end',
            'assign("mod_test_magic_time_stop", 1);',
            'displaymessage("武功生命周期测试完成");',
            "return;",
            "",
        ]
    )


def make_time_stop_visual_repro_script() -> str:
    return "\n".join(
        [
            'displaymessage("时间停止画面复现开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_time_stop_visual_repro_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadnpc("map001.npc");',
            'loadobj("map001.obj");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            "fullmana();",
            "fullthew();",
            'addmagic("mod_test_magic_time_stop_visual.ini");',
            'delnpc("MOD_TEST_TIME_STOP_WALKER");',
            'addnpc("mod_test_time_stop_visual_walker.ini", 31, 20, 0);',
            'setnpcdestination("MOD_TEST_TIME_STOP_WALKER", 36, 20);',
            "sleep(80);",
            'displaymessage("时间停止检查：行走 NPC 的位置和动画帧都应冻结");',
            'usemagic("mod_test_magic_time_stop_visual.ini", 34, 20);',
            'assign("mod_test_time_stop_visual_repro_cast", 1);',
            "return;",
            "",
        ]
    )


def make_environment_weather_script() -> str:
    return "\n".join(
        [
            'displaymessage("天气与水面效果测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'loadmap("map001_衡山.map");',
            'loadnpc("map001.npc");',
            'loadobj("map001.obj");',
            "setplayerpos(34,20);",
            'beginrain("rain1.ini");',
            "openwatereffect();",
            'assign("mod_test_environment_weather_ready", 1);',
            'displaymessage("天气检查：调整窗口大小时，雨水和水面效果应保持稳定");',
            "return;",
            "",
        ]
    )


def make_animation_parameters_visual_script() -> str:
    lines = [
        'displaymessage("动画参数画面测试开始");',
        "loadgame(0);",
        'assign("mod_test_skip_base_newgame", 1);',
        'loadmap("map001_衡山.map");',
        'loadnpc("map001.npc");',
        'loadobj("map001.obj");',
        "setplayerpos(34,20);",
        "setplayerscn();",
        'assign("mod_test_animation_parameters_visual_ready", 1);',
    ]
    for direction in range(8):
        if direction > 0:
            lines.append(f'delobj("MOD_TEST_ANIMATION_DIRECTION_{direction - 1}");')
        lines.extend(
            [
                f'delobj("MOD_TEST_ANIMATION_DIRECTION_{direction}");',
                f'addobj("mod_test_animation_direction_{direction}.ini", 31, 17, {direction});',
                f'displaymessage("动画方向 {direction}：零帧间隔，固定地图锚点");',
            ]
        )
        if direction < 7:
            lines.append("sleep(1200);")
    lines.extend(
        [
            "return;",
            "",
        ]
    )
    return "\n".join(lines)


def make_video_background_continuity_script() -> str:
    return "\n".join(
        [
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'loadmap("map001_衡山.map");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            "cleareffect();",
            "setplayerpos(34,22);",
            "setplayerdir(2);",
            "centercamera();",
            'delobj("MOD_TEST_ANIMATION_DIRECTION_0");',
            'addobj("mod_test_animation_direction_0.ini", 31, 17, 0);',
            'assign("mod_test_video_background_start_choice", -1);',
            'choose("后台持续播放测试：选择开始后立即切换到其他窗口，并保持失焦直到视频结束后至少 8 秒。", "开始验证", "取消", "mod_test_video_background_start_choice");',
            'if getvar("mod_test_video_background_start_choice") ~= 0 then return end',
            'assign("mod_test_video_background_ready", 1);',
            'playmovie("start.wmv", 0, 0, 0);',
            'assign("mod_test_video_background_movie_completed", 1);',
            'displaymessage("视频已结束，正在验证游戏后台计时与动画，请继续保持窗口失焦");',
            "sleep(8000);",
            'assign("mod_test_video_background_game_completed", 1);',
            'displaymessage("后台持续播放测试完成：视频和游戏阶段均已结束");',
            "return;",
            "",
        ]
    )


def make_magic_critical_feedback_script() -> str:
    return "\n".join(
        [
            'displaymessage("MG 暴击反馈测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_magic_critical_feedback_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadnpc("map001.npc");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            "setplayerlevel(1);",
            "addevade(10000);",
            "fullmana();",
            "fullthew();",
            "cleareffect();",
            "clearmagic();",
            'delnpc("MOD_TEST_CRITICAL_TARGET");',
            'addnpc("mod_test_magic_critical_target_npc.ini", 36, 20, 0);',
            'addmagic("mod_test_magic_critical_buff.ini");',
            'addmagic("mod_test_magic_critical_strike.ini");',
            'setmagiclevel("mod_test_magic_critical_buff.ini", 1);',
            'setmagiclevel("mod_test_magic_critical_strike.ini", 1);',
            'usemagic("mod_test_magic_critical_buff.ini", 34, 20);',
            "sleep(220);",
            'getplayerstate("CritChance", "mod_test_magic_critical_chance");',
            'getplayerstate("CritDamage", "mod_test_magic_critical_damage_percent");',
            'getnpcstate("MOD_TEST_CRITICAL_TARGET", "Life", "mod_test_magic_critical_initial_life");',
            "for i = 1, 6 do",
            'usemagic("mod_test_magic_critical_strike.ini", 36, 20);',
            "sleep(320);",
            'displaymessage("MG 暴击检查：目标头顶应显示橙色文字‘暴击 200’");',
            "sleep(2280);",
            "end",
            'getnpcstate("MOD_TEST_CRITICAL_TARGET", "Life", "mod_test_magic_critical_final_life");',
            'assign("mod_test_magic_critical_total_damage", getvar("mod_test_magic_critical_initial_life") - getvar("mod_test_magic_critical_final_life"));',
            'assign("mod_test_magic_critical_feedback_pass", 0);',
            'if getvar("mod_test_magic_critical_chance") == 100 and getvar("mod_test_magic_critical_damage_percent") == 100 and getvar("mod_test_magic_critical_total_damage") == 1200 then assign("mod_test_magic_critical_feedback_pass", 1) end',
            'displaymessage("MG 暴击反馈测试完成：6 次命中均造成 200 点伤害");',
            "sleep(3000);",
            "return;",
            "",
        ]
    )


def make_magic_detached_caster_visual_script() -> str:
    return "\n".join(
        [
            'displaymessage("F030 施法者离场弹体测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_magic_detached_caster_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadnpc("map001.npc");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            "setplayerpos(34,24);",
            "setplayerdir(2);",
            'assign("mod_test_magic_detached_caster_cycle_pass_count", 0);',
            "for cycle = 1, 6 do",
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            "cleareffect();",
            'addnpc("mod_test_collision_caster_npc.ini", 29, 20, 2);',
            "sleep(80);",
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "001火药炮.ini", 36, 20, 1);',
            'assign("mod_test_magic_detached_caster_parent_seen", 0);',
            "for wait_step = 1, 30 do",
            "sleep(20);",
            'geteffectstate("001火药炮.ini", "ProjectileCount", "mod_test_magic_detached_caster_parent_count");',
            'if getvar("mod_test_magic_detached_caster_parent_count") > 0 then assign("mod_test_magic_detached_caster_parent_seen", 1) break end',
            "end",
            'geteffectstate("001霹雳烟火弹爆炸.ini", "Count", "mod_test_magic_detached_caster_child_before_delete");',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'getnpcstate("MOD_TEST_COLLISION_CASTER", "Exists", "mod_test_magic_detached_caster_exists_after_delete");',
            'geteffectstate("001火药炮.ini", "UserKind", "mod_test_magic_detached_caster_owner_kind_after_delete");',
            'assign("mod_test_magic_detached_caster_child_seen_after_delete", 0);',
            'displaymessage("F030 检查：炮手消失后，弹体必须继续飞行并爆炸");',
            "for observe_step = 1, 50 do",
            "sleep(40);",
            'geteffectstate("001霹雳烟火弹爆炸.ini", "Count", "mod_test_magic_detached_caster_child_count");',
            'if getvar("mod_test_magic_detached_caster_child_count") > 0 then assign("mod_test_magic_detached_caster_child_seen_after_delete", 1) end',
            "end",
            'if getvar("mod_test_magic_detached_caster_parent_seen") == 1 and getvar("mod_test_magic_detached_caster_child_before_delete") == 0 and getvar("mod_test_magic_detached_caster_exists_after_delete") == 0 and getvar("mod_test_magic_detached_caster_owner_kind_after_delete") == 1 and getvar("mod_test_magic_detached_caster_child_seen_after_delete") == 1 then assign("mod_test_magic_detached_caster_cycle_pass_count", getvar("mod_test_magic_detached_caster_cycle_pass_count") + 1) end',
            "sleep(400);",
            "end",
            'assign("mod_test_magic_detached_caster_visual_pass", 0);',
            'if getvar("mod_test_magic_detached_caster_cycle_pass_count") == 6 then assign("mod_test_magic_detached_caster_visual_pass", 1) end',
            'displaymessage("F030 测试完成：6 枚弹体均在施法者离场后完成运动");',
            "sleep(3000);",
            "return;",
            "",
        ]
    )


def make_manual_magic_arena_magic_ini(
    source_ini: str,
    display_name: str,
    intro: str,
    icon: str,
) -> str:
    replacements = {
        "Name": display_name,
        "Intro": intro,
        "Icon": icon,
    }
    replacement_counts = {field: 0 for field in replacements}
    result: list[str] = []

    for line in source_ini.split("\n"):
        field, separator, _ = line.partition("=")
        if separator and field in replacements:
            result.append(f"{field}={replacements[field]}")
            replacement_counts[field] += 1
        else:
            result.append(line)

    invalid_fields = [
        field for field, count in replacement_counts.items() if count != 1
    ]
    if invalid_fields:
        raise ValueError(
            "manual magic source must contain exactly one field: "
            + ", ".join(invalid_fields)
        )

    return "\n".join(result)


def manual_magic_arena_magic_definitions() -> tuple[
    tuple[str, str, str, str, Callable[[], str]], ...
]:
    return (
        (
            "mod_test_manual_magic_mouse_guide.ini",
            "鼠标引导",
            "弹体从鼠标指向的位置出现，并持续朝鼠标方向移动，适合观察连续引导效果。",
            "mag001-衡山有雪s.asf",
            make_magic_begin_follow_ini,
        ),
        (
            "mod_test_manual_magic_trace_enemy.ini",
            "追魂锁敌",
            "弹体飞出后延迟锁定敌人，并在飞行途中自动修正方向追击目标。",
            "mag024-幻影飞狐s.asf",
            make_magic_trace_enemy_ini,
        ),
        (
            "mod_test_manual_magic_random_move.ini",
            "随风偏转",
            "弹体在飞行途中随机偏转，每次释放都会呈现不同的散射轨迹。",
            "mag016-江南如烟s.asf",
            make_magic_random_move_ini,
        ),
        (
            "mod_test_manual_magic_meteor.ini",
            "流星坠落",
            "弹体如流星般从远处切入目标区域，形成明显的斜向坠落轨迹。",
            "mag003-牧野流星s.asf",
            make_magic_meteor_ini,
        ),
        (
            "mod_test_manual_magic_round.ini",
            "雷环护身",
            "同时生成四枚环绕弹体，围绕施法者持续旋转并维持固定半径。",
            "mag009-风雷九州s.asf",
            make_magic_round_ini,
        ),
        (
            "mod_test_manual_magic_moveback.ini",
            "回潮折返",
            "弹体向前飞出一段距离后折返，最终回到施法者附近。",
            "mag012-碧海潮生s.asf",
            make_magic_moveback_ini,
        ),
        (
            "mod_test_manual_magic_time_stop.ini",
            "凝时诀",
            "释放时间停止效果，目标的位置和动画帧应同时冻结，结束后恢复。",
            "mag007-混元护体s.asf",
            make_magic_time_stop_visual_ini,
        ),
        (
            "mod_test_manual_magic_trail.ini",
            "流光留痕",
            "移动时会在经过的格子留下短暂流光，残影停留片刻后自行消散。",
            "mag019-回风拂柳s.asf",
            make_magic_trail_ini,
        ),
        (
            "mod_test_manual_magic_bounce.ini",
            "震退掌",
            "命中后沿直线击退目标；若途中碰到角色或障碍，位移会立即停止。",
            "mag010-龙爪虎爪s.asf",
            make_magic_bounce_ini,
        ),
        (
            "mod_test_manual_magic_bouncefly.ini",
            "凌空抛飞",
            "命中后将目标凌空抛向指定方向，落地前可对途中接触的敌人造成伤害。",
            "mag014-长烟落日s.asf",
            make_magic_bouncefly_ini,
        ),
        (
            "mod_test_manual_magic_carry.ini",
            "御敌同行",
            "弹体会裹挟命中的目标继续移动，并能撞击路径附近的其他敌人。",
            "mag017-满城花雨s.asf",
            make_magic_carry_user4_ini,
        ),
        (
            "mod_test_manual_magic_explode.ini",
            "霹雳爆裂",
            "武功抵达落点并结束时，会在原地追加一次范围爆炸。",
            "mag056-烟火霹雳弹s.asf",
            make_magic_explode_point_parent_ini,
        ),
        (
            "mod_test_manual_magic_v_region.ini",
            "雁阵齐发",
            "多枚弹体按雁阵般的折线同时生成，形成向前展开的扇形攻击区域。",
            "mag022-梅花弄影s.asf",
            make_magic_region_vtype_ini,
        ),
        (
            "mod_test_manual_magic_friend.ini",
            "化敌为友",
            "暂时把敌对目标转为友方，持续时间结束后恢复原关系。",
            "mag020-清心术s.asf",
            make_magic_temp_relation_ini,
        ),
    )


def manual_magic_arena_magic_names() -> tuple[str, ...]:
    return tuple(
        file_name
        for file_name, _, _, _, _ in manual_magic_arena_magic_definitions()
    )


def manual_magic_arena_grant_lines() -> list[str]:
    return [f'addmagic("{magic_name}");' for magic_name in manual_magic_arena_magic_names()]


def make_manual_magic_arena_script() -> str:
    return "\n".join(
        [
            'displaymessage("正在进入手动武功训练场");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_manual_magic_arena_ready", 0);',
            'loadmap("map001_衡山.map");',
            'loadobj("map001.obj");',
            "enablenpcai();",
            "cleareffect();",
            "clearmagic();",
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            "centercamera();",
            "fulllife();",
            "fullmana();",
            "fullthew();",
            'delnpc("MOD_TEST_ARENA_TARGET_CENTER");',
            'delnpc("MOD_TEST_ARENA_TARGET_LEFT");',
            'delnpc("MOD_TEST_ARENA_TARGET_RIGHT");',
            'delnpc("MOD_TEST_ARENA_TRAINER");',
            'addnpc("mod_test_manual_arena_target_center.ini", 34, 15, 0);',
            'addnpc("mod_test_manual_arena_target_left.ini", 32, 16, 0);',
            'addnpc("mod_test_manual_arena_target_right.ini", 36, 16, 0);',
            'addnpc("mod_test_manual_arena_trainer.ini", 37, 21, 4);',
            *manual_magic_arena_grant_lines(),
            'assign("mod_test_manual_magic_arena_ready", 1);',
            'displaymessage("训练场已就绪：打开武功栏选择测试武功；与右侧教官交谈可重置");',
            "return;",
            "",
        ]
    )


def make_manual_magic_arena_trainer_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_manual_arena_trainer_choice", -1);',
            'chooseplus("#name", 2, 0, "训练场服务：木桩为敌对单位，生命 99999、防御 500、攻击 1。", "重置木桩与玩家位置", "补满生命、内力和体力", "清除场上武功特效", "重新授予全部测试武功", "查看操作说明", "返回交互测试广场", "离开", "mod_test_manual_arena_trainer_choice");',
            'if getvar("mod_test_manual_arena_trainer_choice") == 0 then goto ResetArena end',
            'if getvar("mod_test_manual_arena_trainer_choice") == 1 then goto RestorePlayer end',
            'if getvar("mod_test_manual_arena_trainer_choice") == 2 then goto ClearEffects end',
            'if getvar("mod_test_manual_arena_trainer_choice") == 3 then goto GrantMagic end',
            'if getvar("mod_test_manual_arena_trainer_choice") == 4 then goto ShowHelp end',
            'if getvar("mod_test_manual_arena_trainer_choice") == 5 then goto ReturnHub end',
            "goto End",
            "::ResetArena::",
            "cleareffect();",
            'delnpc("MOD_TEST_ARENA_TARGET_CENTER");',
            'delnpc("MOD_TEST_ARENA_TARGET_LEFT");',
            'delnpc("MOD_TEST_ARENA_TARGET_RIGHT");',
            'addnpc("mod_test_manual_arena_target_center.ini", 34, 15, 0);',
            'addnpc("mod_test_manual_arena_target_left.ini", 32, 16, 0);',
            'addnpc("mod_test_manual_arena_target_right.ini", 36, 16, 0);',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            "centercamera();",
            "fulllife();",
            "fullmana();",
            "fullthew();",
            'displaymessage("训练场木桩已重置");',
            "goto End",
            "::RestorePlayer::",
            "fulllife();",
            "fullmana();",
            "fullthew();",
            'displaymessage("玩家状态已补满");',
            "goto End",
            "::ClearEffects::",
            "cleareffect();",
            'displaymessage("场上武功特效已清除");',
            "goto End",
            "::GrantMagic::",
            "clearmagic();",
            *manual_magic_arena_grant_lines(),
            "fullmana();",
            "fullthew();",
            'displaymessage("测试武功已重新授予");',
            "goto End",
            "::ShowHelp::",
            'assign("mod_test_manual_arena_help_choice", -1);',
            'chooseplus("#name", 2, 0, "打开武功栏选择技能并拖到快捷栏。<enter>单体技能瞄准中间木桩；范围、弹跳和追踪技能可朝三个木桩之间释放。<enter>临时转友、击退或携带改变木桩状态后，请回来选择重置。", "知道了", "mod_test_manual_arena_help_choice");',
            "goto End",
            "::ReturnHub::",
            'runscript("mod_test_bootstrap.txt");',
            "goto End",
            "::End::",
            "return;",
            "",
        ]
    )


def make_manual_continuity_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_manual_continuity_pass", 0);',
            'runscript("mod_test_manual_magic_arena.txt");',
            'assign("__automation_choose_enabled", 1);',
            'assign("__automation_choose_selection", 5);',
            'runscript("mod_test_manual_magic_arena_trainer.txt");',
            'assign("__automation_choose_enabled", 0);',
            'if getvar("mod_test_ready") == 1 and getvar("mod_test_hub_respawn_count") == 1 then assign("mod_test_manual_continuity_pass", 1) end',
            'displaymessage("手动测试闭环已完成");',
            "return;",
            "",
        ]
    )


def make_manual_magic_arena_target_npc_ini(
    name: str,
    talk_content: str,
    npc_resource: str = "npcres018_金兀术.ini",
) -> str:
    return "\n".join(
        [
            "[INIT]",
            f"Name={name}",
            f"NpcIni={npc_resource}",
            "FlyIni=",
            "BodyIni=",
            "Kind=1",
            "Relation=1",
            "Group=170",
            "Life=99999",
            "LifeMax=99999",
            "Thew=100",
            "ThewMax=100",
            "Mana=100",
            "ManaMax=100",
            "Attack=1",
            "Defence=500",
            "Evade=-100",
            "Exp=0",
            "AttackLevel=1",
            "MagicLevel=1",
            "AttackRadius=1",
            "WalkSpeed=1",
            "Dir=0",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            "NoDropWhenDie=1",
            "DeathScript=",
            "ScriptFile=",
            f"TalkContent={talk_content}",
            "",
        ]
    )


def make_manual_magic_arena_trainer_npc_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "Name=MOD_TEST_ARENA_TRAINER",
            "NpcIni=npcres001_独孤剑.ini",
            "FlyIni=",
            "BodyIni=",
            "Kind=0",
            "Relation=0",
            "Group=171",
            "Life=99999",
            "LifeMax=99999",
            "Thew=100",
            "ThewMax=100",
            "Mana=100",
            "ManaMax=100",
            "Attack=0",
            "Defence=500",
            "Evade=500",
            "Exp=0",
            "WalkSpeed=1",
            "Dir=4",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            "NoDropWhenDie=1",
            "DeathScript=",
            "ScriptFile=mod_test_manual_magic_arena_trainer.txt",
            "ScriptFileRight=mod_test_manual_magic_arena_trainer.txt",
            "TalkContent=训练场教官：交谈可重置木桩、补满状态或重新授予武功",
            "",
        ]
    )


def make_magic_self_special_script() -> str:
    return "\n".join(
        [
            'displaymessage("自身特殊武功测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_magic_self_special_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadnpc("map001.npc");',
            'loadobj("map001.obj");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            "fullmana();",
            "fullthew();",
            "addlifemax(1000);",
            "fulllife();",
            'addmagic("mod_test_magic_self_block_damage.ini");',
            'addmagic("mod_test_magic_self_clear_abnormal.ini");',
            'addmagic("mod_test_magic_bounce.ini");',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            "setplayerpos(33,11);",
            "setplayerdir(6);",
            'addnpc("mod_test_collision_caster_npc.ini", 34, 20, 0);',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'getplayerstate("Life", "mod_test_magic_self_block_life_before");',
            'usemagic("mod_test_magic_self_block_damage.ini", 33, 11);',
            "sleep(160);",
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_bounce.ini", 34, 16, 1);',
            "sleep(1800);",
            'getplayerstate("Life", "mod_test_magic_self_block_life_after_shield");',
            "cleareffect();",
            "sleep(120);",
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            "setplayerpos(33,11);",
            "setplayerdir(6);",
            'addnpc("mod_test_collision_caster_npc.ini", 34, 20, 0);',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_bounce.ini", 34, 16, 1);',
            "sleep(1800);",
            'getplayerstate("Life", "mod_test_magic_self_block_life_after_damage");',
            'assign("mod_test_magic_self_block_damage_ok", 0);',
            'if getvar("mod_test_magic_self_block_life_after_shield") == getvar("mod_test_magic_self_block_life_before") and getvar("mod_test_magic_self_block_life_after_damage") < getvar("mod_test_magic_self_block_life_after_shield") then assign("mod_test_magic_self_block_damage_ok", 1) end',
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'assign("mod_test_magic_self_clear_frozen_ok", 0);',
            "frozenmillisecond(2000);",
            'getplayerstate("IsFrozened", "mod_test_magic_self_clear_frozen_before");',
            'usemagic("mod_test_magic_self_clear_abnormal.ini", 34, 20);',
            "sleep(160);",
            'getplayerstate("IsFrozened", "mod_test_magic_self_clear_frozen_after");',
            'if getvar("mod_test_magic_self_clear_frozen_before") == 1 and getvar("mod_test_magic_self_clear_frozen_after") == 0 then assign("mod_test_magic_self_clear_frozen_ok", 1) end',
            'assign("mod_test_magic_self_clear_poison_ok", 0);',
            "poisonmillisecond(2000);",
            'getplayerstate("IsPoisoned", "mod_test_magic_self_clear_poison_before");',
            'usemagic("mod_test_magic_self_clear_abnormal.ini", 34, 20);',
            "sleep(160);",
            'getplayerstate("IsPoisoned", "mod_test_magic_self_clear_poison_after");',
            'if getvar("mod_test_magic_self_clear_poison_before") == 1 and getvar("mod_test_magic_self_clear_poison_after") == 0 then assign("mod_test_magic_self_clear_poison_ok", 1) end',
            'assign("mod_test_magic_self_clear_petrify_ok", 0);',
            "petrifymillisecond(2000);",
            'getplayerstate("IsPetrified", "mod_test_magic_self_clear_petrify_before");',
            'usemagic("mod_test_magic_self_clear_abnormal.ini", 34, 20);',
            "sleep(160);",
            'getplayerstate("IsPetrified", "mod_test_magic_self_clear_petrify_after");',
            'if getvar("mod_test_magic_self_clear_petrify_before") == 1 and getvar("mod_test_magic_self_clear_petrify_after") == 0 then assign("mod_test_magic_self_clear_petrify_ok", 1) end',
            'getplayerstate("BodyFunctionWell", "mod_test_magic_self_clear_body");',
            'displaymessage("自身特殊武功测试完成");',
            "return;",
            "",
        ]
    )


def make_magic_trail_script() -> str:
    return "\n".join(
        [
            'displaymessage("武功轨迹测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_magic_trail_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadnpc("map001.npc");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            "cleareffect();",
            "clearmagic();",
            'addmagic("mod_test_magic_trail.ini");',
            'setmagiclevel("mod_test_magic_trail.ini", 1);',
            'getmagicstate("mod_test_magic_trail.ini", "MoveKind", "mod_test_magic_trail_movekind");',
            'getmagicstate("mod_test_magic_trail.ini", "KeepMilliseconds", "mod_test_magic_trail_keep_ms");',
            'assign("mod_test_magic_trail_state", 0);',
            'if getvar("mod_test_magic_trail_movekind") == 19 and getvar("mod_test_magic_trail_keep_ms") == 1200 then assign("mod_test_magic_trail_state", 1) end',
            "setplayerpos(34, 20);",
            "setplayerdir(6);",
            'usemagic("mod_test_magic_trail.ini", 34, 20);',
            "sleep(120);",
            "setplayerpos(35, 20);",
            "sleep(220);",
            'geteffectstate("mod_test_magic_trail.ini", "Count", "mod_test_magic_trail_first_count");',
            'geteffectstate("mod_test_magic_trail.ini", "MapX", "mod_test_magic_trail_first_x");',
            'geteffectstate("mod_test_magic_trail.ini", "MapY", "mod_test_magic_trail_first_y");',
            'assign("mod_test_magic_trail_first_position", 0);',
            'if getvar("mod_test_magic_trail_first_x") == 34 and getvar("mod_test_magic_trail_first_y") == 20 then assign("mod_test_magic_trail_first_position", 1) end',
            "setplayerpos(36, 20);",
            "sleep(220);",
            'geteffectstate("mod_test_magic_trail.ini", "Count", "mod_test_magic_trail_second_count");',
            "sleep(900);",
            "setplayerpos(37, 20);",
            "sleep(220);",
            'geteffectstate("mod_test_magic_trail.ini", "Count", "mod_test_magic_trail_expired_count");',
            'assign("mod_test_magic_trail_no_after_expire", 0);',
            'if getvar("mod_test_magic_trail_expired_count") == getvar("mod_test_magic_trail_second_count") then assign("mod_test_magic_trail_no_after_expire", 1) end',
            'displaymessage("武功轨迹测试完成");',
            "return;",
            "",
        ]
    )


def make_magic_explode_script() -> str:
    return "\n".join(
        [
            'displaymessage("爆炸子武功测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_magic_explode_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'addmagic("mod_test_magic_explode_point_parent.ini");',
            'addmagic("mod_test_magic_explode_throw_parent.ini");',
            'addmagic("mod_test_magic_explode_throw_suppressed_parent.ini");',
            'getmagicstate("mod_test_magic_explode_point_parent.ini", "HasExplodeMagic", "mod_test_magic_explode_point_has_child");',
            'getmagicstate("mod_test_magic_explode_throw_parent.ini", "HasExplodeMagic", "mod_test_magic_explode_throw_has_child");',
            'getmagicstate("mod_test_magic_explode_throw_suppressed_parent.ini", "HasExplodeMagic", "mod_test_magic_explode_throw_suppressed_has_child");',
            'getmapstate(34, 10, "CanFly", "mod_test_magic_explode_point_tile_flyable");',
            'getmapstate(34, 10, "NpcCount", "mod_test_magic_explode_point_tile_npcs");',
            'assign("mod_test_magic_explode_point_empty_tile", 0);',
            'if getvar("mod_test_magic_explode_point_tile_flyable") == 1 and getvar("mod_test_magic_explode_point_tile_npcs") == 0 then assign("mod_test_magic_explode_point_empty_tile", 1) end',
            "cleareffect();",
            'geteffectstate("mod_test_magic_explode_child.ini", "ProjectileCount", "mod_test_magic_explode_point_child_before");',
            'assign("mod_test_magic_explode_point_child_seen", 0);',
            'assign("mod_test_magic_explode_point_child_max", 0);',
            'usemagic("mod_test_magic_explode_point_parent.ini", 34, 10);',
            "for i = 1, 30 do",
            "sleep(80);",
            'geteffectstate("mod_test_magic_explode_child.ini", "ProjectileCount", "mod_test_magic_explode_point_child_count");',
            'if getvar("mod_test_magic_explode_point_child_count") > getvar("mod_test_magic_explode_point_child_max") then assign("mod_test_magic_explode_point_child_max", getvar("mod_test_magic_explode_point_child_count")) end',
            'if getvar("mod_test_magic_explode_point_child_count") == 1 then assign("mod_test_magic_explode_point_child_seen", 1) end',
            'geteffectstate("mod_test_magic_explode_child.ini", "ActiveProjectileMapX", "mod_test_magic_explode_point_child_x");',
            'geteffectstate("mod_test_magic_explode_child.ini", "ActiveProjectileMapY", "mod_test_magic_explode_point_child_y");',
            'geteffectstate("mod_test_magic_explode_child.ini", "ActiveProjectileFlyingDirectionX", "mod_test_magic_explode_point_child_fly_x");',
            'geteffectstate("mod_test_magic_explode_child.ini", "ActiveProjectileFlyingDirectionY", "mod_test_magic_explode_point_child_fly_y");',
            'geteffectstate("mod_test_magic_explode_child.ini", "ActiveProjectileUserIsPlayer", "mod_test_magic_explode_point_child_owner");',
            'geteffectstate("mod_test_magic_explode_child.ini", "ActiveProjectileLauncherKind", "mod_test_magic_explode_point_child_launcher");',
            "end",
            'assign("mod_test_magic_explode_point_once", 0);',
            'if getvar("mod_test_magic_explode_point_has_child") == 1 and getvar("mod_test_magic_explode_point_empty_tile") == 1 and getvar("mod_test_magic_explode_point_child_before") == 0 and getvar("mod_test_magic_explode_point_child_seen") == 1 and getvar("mod_test_magic_explode_point_child_max") == 1 and getvar("mod_test_magic_explode_point_child_x") == 34 and getvar("mod_test_magic_explode_point_child_y") < 10 and getvar("mod_test_magic_explode_point_child_fly_x") == 0 and getvar("mod_test_magic_explode_point_child_fly_y") < 0 and getvar("mod_test_magic_explode_point_child_owner") == 1 and getvar("mod_test_magic_explode_point_child_launcher") == 2 then assign("mod_test_magic_explode_point_once", 1) end',
            "cleareffect();",
            "setplayerpos(34,20);",
            'getmapstate(34, 19, "CanFly", "mod_test_magic_explode_throw_tile_flyable");',
            'getmapstate(34, 19, "NpcCount", "mod_test_magic_explode_throw_tile_npcs");',
            'assign("mod_test_magic_explode_throw_empty_tile", 0);',
            'if getvar("mod_test_magic_explode_throw_tile_flyable") == 1 and getvar("mod_test_magic_explode_throw_tile_npcs") == 0 then assign("mod_test_magic_explode_throw_empty_tile", 1) end',
            'setmagiclevel("mod_test_magic_explode_throw_suppressed_parent.ini", 4);',
            'geteffectstate("mod_test_magic_explode_throw_child.ini", "ProjectileCount", "mod_test_magic_explode_throw_suppressed_child_before");',
            'assign("mod_test_magic_explode_throw_suppressed_child_max", 0);',
            'assign("mod_test_magic_explode_throw_suppressed_parent_seen", 0);',
            'usemagic("mod_test_magic_explode_throw_suppressed_parent.ini", 34, 19);',
            "for i = 1, 20 do",
            "sleep(80);",
            'geteffectstate("mod_test_magic_explode_throw_suppressed_parent.ini", "ProjectileCount", "mod_test_magic_explode_throw_suppressed_parent_count");',
            'if getvar("mod_test_magic_explode_throw_suppressed_parent_count") > 0 then assign("mod_test_magic_explode_throw_suppressed_parent_seen", 1) end',
            'geteffectstate("mod_test_magic_explode_throw_child.ini", "ProjectileCount", "mod_test_magic_explode_throw_suppressed_child_count");',
            'if getvar("mod_test_magic_explode_throw_suppressed_child_count") > getvar("mod_test_magic_explode_throw_suppressed_child_max") then assign("mod_test_magic_explode_throw_suppressed_child_max", getvar("mod_test_magic_explode_throw_suppressed_child_count")) end',
            "end",
            'assign("mod_test_magic_explode_throw_suppressed", 0);',
            'if getvar("mod_test_magic_explode_throw_suppressed_has_child") == 1 and getvar("mod_test_magic_explode_throw_empty_tile") == 1 and getvar("mod_test_magic_explode_throw_suppressed_parent_seen") == 1 and getvar("mod_test_magic_explode_throw_suppressed_child_before") == 0 and getvar("mod_test_magic_explode_throw_suppressed_child_max") == 0 then assign("mod_test_magic_explode_throw_suppressed", 1) end',
            'setmagiclevel("mod_test_magic_explode_throw_parent.ini", 4);',
            'getmagicstate("mod_test_magic_explode_throw_parent.ini", "CurrentLevel", "mod_test_magic_explode_throw_level");',
            'geteffectstate("mod_test_magic_explode_throw_child.ini", "ProjectileCount", "mod_test_magic_explode_throw_child_before");',
            'assign("mod_test_magic_explode_throw_child_seen_four", 0);',
            'assign("mod_test_magic_explode_throw_child_max", 0);',
            'usemagic("mod_test_magic_explode_throw_parent.ini", 34, 19);',
            "for i = 1, 30 do",
            "sleep(80);",
            'geteffectstate("mod_test_magic_explode_throw_child.ini", "ProjectileCount", "mod_test_magic_explode_throw_child_count");',
            'if getvar("mod_test_magic_explode_throw_child_count") > getvar("mod_test_magic_explode_throw_child_max") then assign("mod_test_magic_explode_throw_child_max", getvar("mod_test_magic_explode_throw_child_count")) end',
            'if getvar("mod_test_magic_explode_throw_child_count") == 4 then assign("mod_test_magic_explode_throw_child_seen_four", 1) end',
            'geteffectstate("mod_test_magic_explode_throw_child.ini", "ActiveProjectileUserIsPlayer", "mod_test_magic_explode_throw_child_owner");',
            'geteffectstate("mod_test_magic_explode_throw_child.ini", "ActiveProjectileLauncherKind", "mod_test_magic_explode_throw_child_launcher");',
            "end",
            'assign("mod_test_magic_explode_throw_four", 0);',
            'if getvar("mod_test_magic_explode_throw_has_child") == 1 and getvar("mod_test_magic_explode_throw_level") == 4 and getvar("mod_test_magic_explode_throw_empty_tile") == 1 and getvar("mod_test_magic_explode_throw_child_before") == 0 and getvar("mod_test_magic_explode_throw_child_seen_four") == 1 and getvar("mod_test_magic_explode_throw_child_max") == 4 and getvar("mod_test_magic_explode_throw_child_owner") == 1 and getvar("mod_test_magic_explode_throw_child_launcher") == 2 then assign("mod_test_magic_explode_throw_four", 1) end',
            "cleareffect();",
            'displaymessage("爆炸子武功测试完成");',
            "return;",
            "",
        ]
    )


def make_magic_collision_script() -> str:
    return "\n".join(
        [
            'displaymessage("武功碰撞测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_magic_collision_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            "fullmana();",
            "fullthew();",
            'getmagicstate("mod_test_magic_state_probe.ini", "Exists", "mod_test_magic_state_exists");',
            'getmagicstate("mod_test_magic_state_probe.ini", "Count", "mod_test_magic_state_count_init");',
            'getmagicstate("mod_test_magic_state_probe.ini", "Count", "mod_test_magic_state_count_level1", 1);',
            'getmagicstate("mod_test_magic_state_probe.ini", "SpecialKindValue", "mod_test_magic_state_special_value_init");',
            'getmagicstate("mod_test_magic_state_probe.ini", "SpecialKindValue", "mod_test_magic_state_special_value_level1", 1);',
            'getmagicstate("mod_test_magic_state_probe.ini", "NoSpecialKindEffect", "mod_test_magic_state_no_effect");',
            'getmagicstate("mod_test_magic_state_probe.ini", "MaxCount", "mod_test_magic_state_max_count");',
            'getmagicstate("mod_test_magic_state_probe.ini", "FileName", "mod_test_magic_state_file_name");',
            'addmagic("mod_test_magic_state_probe.ini");',
            'setmagiclevel("mod_test_magic_state_probe.ini", 2);',
            'getmagicstate("mod_test_magic_state_probe.ini", "CurrentLevel", "mod_test_magic_state_current_level");',
            'getmagicstate("mod_test_magic_state_probe.ini", "EffectLevel", "mod_test_magic_state_effect_level");',
            'getmagicstate("mod_test_magic_state_probe.ini", "ItemInfo", "mod_test_magic_state_item_info");',
            'addmagic("mod_test_magic_ball.ini");',
            'addmagic("mod_test_magic_fly_magic_parent.ini");',
            'addmagic("mod_test_magic_damage_channels.ini");',
            'addmagic("mod_test_magic_leap.ini");',
            'addmagic("mod_test_magic_attack_all_leap.ini");',
            'addmagic("mod_test_magic_restore_life.ini");',
            'addmagic("mod_test_magic_restore_mana.ini");',
            'addmagic("mod_test_magic_restore_thew.ini");',
            'addmagic("mod_test_magic_attack_all_projectile.ini");',
            'addmagic("mod_test_magic_attack_all_trace_enemy.ini");',
            'addmagic("mod_test_magic_wall.ini");',
            'addmagic("mod_test_magic_pass_through.ini");',
            'addmagic("mod_test_magic_pass_through_wall.ini");',
            'addmagic("mod_test_magic_sticky.ini");',
            'addmagic("mod_test_magic_solid.ini");',
            'addmagic("mod_test_magic_parasitic.ini");',
            'addmagic("mod_test_magic_range_speedup.ini");',
            'addmagic("mod_test_magic_range_attack.ini");',
            'addmagic("mod_test_magic_range_attack_all.ini");',
            'addmagic("mod_test_magic_bounce.ini");',
            'addmagic("mod_test_magic_bouncefly.ini");',
            'addmagic("mod_test_magic_bounce_handoff.ini");',
            'addmagic("mod_test_magic_bouncefly_handoff.ini");',
            'addmagic("mod_test_magic_carry_user4.ini");',
            'addmagic("mod_test_magic_carry_user4_hidden.ini");',
            'addmagic("mod_test_magic_carry_user1_hidden.ini");',
            'addmagic("mod_test_magic_discard.ini");',
            'addmagic("mod_test_magic_exchange.ini");',
            'addmagic("mod_test_magic_summon.ini");',
            'addmagic("mod_test_magic_collision_lethal_freeze.ini");',
            'addmagic("mod_test_magic_status_duration_freeze.ini");',
            'addmagic("mod_test_magic_status_duration_short_freeze.ini");',
            'addmagic("mod_test_magic_status_duration_poison.ini");',
            'addmagic("mod_test_magic_status_duration_petrify.ini");',
            'displaymessage("正在测试武功携带武功");',
            'getmagicstate("mod_test_magic_fly_magic_parent.ini", "HasFlyMagic", "mod_test_magic_fly_magic_has_child");',
            'getmagicstate("mod_test_magic_fly_magic_parent.ini", "FlyInterval", "mod_test_magic_fly_magic_interval");',
            "cleareffect();",
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'usemagic("mod_test_magic_fly_magic_parent.ini", 34, 10);',
            "sleep(260);",
            'geteffectstate("mod_test_magic_fly_magic_child.ini", "ProjectileCount", "mod_test_magic_fly_magic_child_projectiles");',
            'assign("mod_test_magic_fly_magic_child_spawned", 0);',
            'if getvar("mod_test_magic_fly_magic_child_projectiles") > 0 then assign("mod_test_magic_fly_magic_child_spawned", 1) end',
            "cleareffect();",
            'displaymessage("正在测试武功伤害通道");',
            'delnpc("MOD_TEST_DAMAGE_CHANNELS_TARGET");',
            'addnpc("mod_test_magic_damage_channels_target_npc.ini", 34, 16, 0);',
            'getmagicstate("mod_test_magic_damage_channels.ini", "Effect", "mod_test_magic_damage_channels_effect", 1);',
            'getmagicstate("mod_test_magic_damage_channels.ini", "EffectExt", "mod_test_magic_damage_channels_effect_ext", 1);',
            'getmagicstate("mod_test_magic_damage_channels.ini", "Effect2", "mod_test_magic_damage_channels_effect2", 1);',
            'getmagicstate("mod_test_magic_damage_channels.ini", "Effect3", "mod_test_magic_damage_channels_effect3", 1);',
            'getmagicstate("mod_test_magic_damage_channels.ini", "EffectMana", "mod_test_magic_damage_channels_effect_mana", 1);',
            'getnpcstate("MOD_TEST_DAMAGE_CHANNELS_TARGET", "Defend2", "mod_test_magic_damage_channels_defend2");',
            'getnpcstate("MOD_TEST_DAMAGE_CHANNELS_TARGET", "Defend3", "mod_test_magic_damage_channels_defend3");',
            'assign("mod_test_magic_damage_channels_fields", 0);',
            'if getvar("mod_test_magic_damage_channels_effect") == 20 and getvar("mod_test_magic_damage_channels_effect_ext") == 9 and getvar("mod_test_magic_damage_channels_effect2") == 17 and getvar("mod_test_magic_damage_channels_effect3") == 13 and getvar("mod_test_magic_damage_channels_effect_mana") == 15 and getvar("mod_test_magic_damage_channels_defend2") == 5 and getvar("mod_test_magic_damage_channels_defend3") == 3 then assign("mod_test_magic_damage_channels_fields", 1) end',
            'getnpcstate("MOD_TEST_DAMAGE_CHANNELS_TARGET", "Life", "mod_test_magic_damage_channels_life_before");',
            'getnpcstate("MOD_TEST_DAMAGE_CHANNELS_TARGET", "Mana", "mod_test_magic_damage_channels_mana_before");',
            'assign("mod_test_magic_damage_channels_life_delta", 0);',
            'assign("mod_test_magic_damage_channels_effect_ext_delta", 0);',
            'assign("mod_test_magic_damage_channels_mana_delta", 0);',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'usemagic("mod_test_magic_damage_channels.ini", 34, 10);',
            'for i = 1, 24 do',
            "sleep(80);",
            'getnpcstate("MOD_TEST_DAMAGE_CHANNELS_TARGET", "Life", "mod_test_magic_damage_channels_life_after");',
            'getnpcstate("MOD_TEST_DAMAGE_CHANNELS_TARGET", "Mana", "mod_test_magic_damage_channels_mana_after");',
            'if getvar("mod_test_magic_damage_channels_life_after") <= getvar("mod_test_magic_damage_channels_life_before") - 42 then assign("mod_test_magic_damage_channels_life_delta", 1) end',
            'if getvar("mod_test_magic_damage_channels_life_after") <= getvar("mod_test_magic_damage_channels_life_before") - 51 then assign("mod_test_magic_damage_channels_effect_ext_delta", 1) end',
            'if getvar("mod_test_magic_damage_channels_mana_after") <= getvar("mod_test_magic_damage_channels_mana_before") - 15 then assign("mod_test_magic_damage_channels_mana_delta", 1) end',
            "end",
            "cleareffect();",
            'delnpc("MOD_TEST_DAMAGE_CHANNELS_TARGET");',
            'displaymessage("正在测试武功跳跃");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_BLOCKER");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'addnpc("mod_test_collision_blocker_npc.ini", 34, 15, 0);',
            'getmagicstate("mod_test_magic_leap.ini", "LeapTimes", "mod_test_magic_leap_times", 1);',
            'getmagicstate("mod_test_magic_leap.ini", "LeapFrame", "mod_test_magic_leap_frame", 1);',
            'getmagicstate("mod_test_magic_leap.ini", "EffectReducePercentage", "mod_test_magic_leap_reduce_percentage", 1);',
            'assign("mod_test_magic_leap_fields", 0);',
            'if getvar("mod_test_magic_leap_times") == 1 and getvar("mod_test_magic_leap_frame") == 20 and getvar("mod_test_magic_leap_reduce_percentage") == 50 then assign("mod_test_magic_leap_fields", 1) end',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_leap_target1_life_before");',
            'getnpcstate("MOD_TEST_COLLISION_BLOCKER", "Life", "mod_test_magic_leap_target2_life_before");',
            'assign("mod_test_magic_leap", 1);',
            'assign("mod_test_magic_leap_target1_damage", 0);',
            'assign("mod_test_magic_leap_target1_single_hit", 0);',
            'assign("mod_test_magic_leap_target2_damage", 0);',
            'assign("mod_test_magic_leap_retarget", 0);',
            'usemagic("mod_test_magic_leap.ini", 34, 10);',
            "for i = 1, 36 do",
            "sleep(60);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_leap_target1_life_after");',
            'getnpcstate("MOD_TEST_COLLISION_BLOCKER", "Life", "mod_test_magic_leap_target2_life_after");',
            'if getvar("mod_test_magic_leap_target1_life_after") < getvar("mod_test_magic_leap_target1_life_before") then assign("mod_test_magic_leap_target1_damage", 1) end',
            'if getvar("mod_test_magic_leap_target1_life_after") == getvar("mod_test_magic_leap_target1_life_before") - 70 then assign("mod_test_magic_leap_target1_single_hit", 1) end',
            'if getvar("mod_test_magic_leap_target2_life_after") < getvar("mod_test_magic_leap_target2_life_before") then assign("mod_test_magic_leap_target2_damage", 1) end',
            'if getvar("mod_test_magic_leap_fields") == 1 and getvar("mod_test_magic_leap_target1_single_hit") == 1 and getvar("mod_test_magic_leap_target2_damage") == 1 then assign("mod_test_magic_leap_retarget", 1) end',
            "end",
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_BLOCKER");',
            'displaymessage("正在测试全体攻击跳跃");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_FRIEND");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'addnpc("mod_test_collision_friend_npc.ini", 34, 15, 0);',
            'getmagicstate("mod_test_magic_attack_all_leap.ini", "AttackAll", "mod_test_magic_attack_all_leap_attack_all", 1);',
            'getmagicstate("mod_test_magic_attack_all_leap.ini", "LeapTimes", "mod_test_magic_attack_all_leap_times", 1);',
            'getmagicstate("mod_test_magic_attack_all_leap.ini", "EffectReducePercentage", "mod_test_magic_attack_all_leap_reduce_percentage", 1);',
            'getnpcstate("MOD_TEST_COLLISION_FRIEND", "Relation", "mod_test_magic_attack_all_leap_friend_relation");',
            'getnpcstate("MOD_TEST_COLLISION_FRIEND", "IsFighter", "mod_test_magic_attack_all_leap_friend_fighter_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_attack_all_leap_target1_life_before");',
            'getnpcstate("MOD_TEST_COLLISION_FRIEND", "Life", "mod_test_magic_attack_all_leap_friend_life_before");',
            'assign("mod_test_magic_attack_all_leap", 1);',
            'assign("mod_test_magic_attack_all_leap_state", 0);',
            'assign("mod_test_magic_attack_all_leap_friend_fighter", 0);',
            'assign("mod_test_magic_attack_all_leap_target1_single_hit", 0);',
            'assign("mod_test_magic_attack_all_leap_friendly_damage", 0);',
            'assign("mod_test_magic_attack_all_leap_retarget", 0);',
            'if getvar("mod_test_magic_attack_all_leap_attack_all") == 1 and getvar("mod_test_magic_attack_all_leap_times") == 1 and getvar("mod_test_magic_attack_all_leap_reduce_percentage") == 50 then assign("mod_test_magic_attack_all_leap_state", 1) end',
            'if getvar("mod_test_magic_attack_all_leap_friend_relation") == 0 and getvar("mod_test_magic_attack_all_leap_friend_fighter_raw") == 1 then assign("mod_test_magic_attack_all_leap_friend_fighter", 1) end',
            'usemagic("mod_test_magic_attack_all_leap.ini", 34, 10);',
            "for i = 1, 36 do",
            "sleep(60);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_attack_all_leap_target1_life_after");',
            'getnpcstate("MOD_TEST_COLLISION_FRIEND", "Life", "mod_test_magic_attack_all_leap_friend_life_after");',
            'if getvar("mod_test_magic_attack_all_leap_target1_life_after") == getvar("mod_test_magic_attack_all_leap_target1_life_before") - 70 then assign("mod_test_magic_attack_all_leap_target1_single_hit", 1) end',
            'if getvar("mod_test_magic_attack_all_leap_friend_life_after") < getvar("mod_test_magic_attack_all_leap_friend_life_before") then assign("mod_test_magic_attack_all_leap_friendly_damage", 1) end',
            'if getvar("mod_test_magic_attack_all_leap_state") == 1 and getvar("mod_test_magic_attack_all_leap_friend_fighter") == 1 and getvar("mod_test_magic_attack_all_leap_target1_single_hit") == 1 and getvar("mod_test_magic_attack_all_leap_friendly_damage") == 1 then assign("mod_test_magic_attack_all_leap_retarget", 1) end',
            "end",
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_FRIEND");',
            'displaymessage("正在测试伙伴全体攻击跳跃");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            "disablepartnercombat();",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_PARTNER");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'addnpc("mod_test_collision_partner_npc.ini", 34, 15, 0);',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "Exists", "mod_test_magic_attack_all_leap_partner_exists");',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "Kind", "mod_test_magic_attack_all_leap_partner_kind");',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "IsFighter", "mod_test_magic_attack_all_leap_partner_is_fighter");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_attack_all_leap_partner_target1_life_before");',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "Life", "mod_test_magic_attack_all_leap_partner_life_before");',
            'assign("mod_test_magic_attack_all_leap_partner", 1);',
            'assign("mod_test_magic_attack_all_leap_partner_ready", 0);',
            'assign("mod_test_magic_attack_all_leap_partner_target1_single_hit", 0);',
            'assign("mod_test_magic_attack_all_leap_partner_damage", 0);',
            'assign("mod_test_magic_attack_all_leap_partner_retarget", 0);',
            'if getvar("mod_test_magic_attack_all_leap_partner_exists") == 1 and getvar("mod_test_magic_attack_all_leap_partner_kind") == 3 and getvar("mod_test_magic_attack_all_leap_partner_is_fighter") == 1 then assign("mod_test_magic_attack_all_leap_partner_ready", 1) end',
            'usemagic("mod_test_magic_attack_all_leap.ini", 34, 10);',
            "for i = 1, 36 do",
            "sleep(60);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_attack_all_leap_partner_target1_life_after");',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "Life", "mod_test_magic_attack_all_leap_partner_life_after");',
            'if getvar("mod_test_magic_attack_all_leap_partner_target1_life_after") == getvar("mod_test_magic_attack_all_leap_partner_target1_life_before") - 70 then assign("mod_test_magic_attack_all_leap_partner_target1_single_hit", 1) end',
            'if getvar("mod_test_magic_attack_all_leap_partner_life_after") < getvar("mod_test_magic_attack_all_leap_partner_life_before") then assign("mod_test_magic_attack_all_leap_partner_damage", 1) end',
            'if getvar("mod_test_magic_attack_all_leap_partner_ready") == 1 and getvar("mod_test_magic_attack_all_leap_partner_target1_single_hit") == 1 and getvar("mod_test_magic_attack_all_leap_partner_damage") == 1 then assign("mod_test_magic_attack_all_leap_partner_retarget", 1) end',
            "end",
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_PARTNER");',
            'displaymessage("正在测试武功恢复效果");',
            'getmagicstate("mod_test_magic_restore_life.ini", "RestoreType", "mod_test_magic_restore_life_type");',
            'getmagicstate("mod_test_magic_restore_life.ini", "RestorePercent", "mod_test_magic_restore_life_percent");',
            'getmagicstate("mod_test_magic_restore_life.ini", "RestoreProbability", "mod_test_magic_restore_life_probability");',
            'getmagicstate("mod_test_magic_restore_mana.ini", "RestoreType", "mod_test_magic_restore_mana_type");',
            'getmagicstate("mod_test_magic_restore_thew.ini", "RestoreType", "mod_test_magic_restore_thew_type");',
            'assign("mod_test_magic_restore_fields", 0);',
            'if getvar("mod_test_magic_restore_life_type") == 0 and getvar("mod_test_magic_restore_life_percent") == 50 and getvar("mod_test_magic_restore_life_probability") == 100 and getvar("mod_test_magic_restore_mana_type") == 1 and getvar("mod_test_magic_restore_thew_type") == 2 then assign("mod_test_magic_restore_fields", 1) end',
            'delnpc("MOD_TEST_RESTORE_TARGET");',
            'addnpc("mod_test_magic_restore_target_npc.ini", 34, 16, 0);',
            "fulllife();",
            "addlife(-40);",
            'getplayerstate("Life", "mod_test_magic_restore_life_before");',
            'getnpcstate("MOD_TEST_RESTORE_TARGET", "Life", "mod_test_magic_restore_life_target_before");',
            'assign("mod_test_magic_restore_life_owner", 0);',
            'assign("mod_test_magic_restore_life_target_damage", 0);',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'usemagic("mod_test_magic_restore_life.ini", 34, 10);',
            'for i = 1, 24 do',
            "sleep(80);",
            'getplayerstate("Life", "mod_test_magic_restore_life_after");',
            'getnpcstate("MOD_TEST_RESTORE_TARGET", "Life", "mod_test_magic_restore_life_target_after");',
            'if getvar("mod_test_magic_restore_life_after") >= getvar("mod_test_magic_restore_life_before") + 10 then assign("mod_test_magic_restore_life_owner", 1) end',
            'if getvar("mod_test_magic_restore_life_target_after") <= getvar("mod_test_magic_restore_life_target_before") - 30 then assign("mod_test_magic_restore_life_target_damage", 1) end',
            "end",
            "cleareffect();",
            'delnpc("MOD_TEST_RESTORE_TARGET");',
            'addnpc("mod_test_magic_restore_target_npc.ini", 34, 16, 0);',
            "fullmana();",
            "addmana(-40);",
            'getplayerstate("Mana", "mod_test_magic_restore_mana_before");',
            'getnpcstate("MOD_TEST_RESTORE_TARGET", "Life", "mod_test_magic_restore_mana_target_before");',
            'assign("mod_test_magic_restore_mana_owner", 0);',
            'assign("mod_test_magic_restore_mana_target_damage", 0);',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'usemagic("mod_test_magic_restore_mana.ini", 34, 10);',
            'for i = 1, 24 do',
            "sleep(80);",
            'getplayerstate("Mana", "mod_test_magic_restore_mana_after");',
            'getnpcstate("MOD_TEST_RESTORE_TARGET", "Life", "mod_test_magic_restore_mana_target_after");',
            'if getvar("mod_test_magic_restore_mana_after") >= getvar("mod_test_magic_restore_mana_before") + 10 then assign("mod_test_magic_restore_mana_owner", 1) end',
            'if getvar("mod_test_magic_restore_mana_target_after") <= getvar("mod_test_magic_restore_mana_target_before") - 30 then assign("mod_test_magic_restore_mana_target_damage", 1) end',
            "end",
            "cleareffect();",
            'delnpc("MOD_TEST_RESTORE_TARGET");',
            'addnpc("mod_test_magic_restore_target_npc.ini", 34, 16, 0);',
            "fullthew();",
            "addthew(-40);",
            'getplayerstate("Thew", "mod_test_magic_restore_thew_before");',
            'getnpcstate("MOD_TEST_RESTORE_TARGET", "Life", "mod_test_magic_restore_thew_target_before");',
            'assign("mod_test_magic_restore_thew_owner", 0);',
            'assign("mod_test_magic_restore_thew_target_damage", 0);',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'usemagic("mod_test_magic_restore_thew.ini", 34, 10);',
            'for i = 1, 24 do',
            "sleep(80);",
            'getplayerstate("Thew", "mod_test_magic_restore_thew_after");',
            'getnpcstate("MOD_TEST_RESTORE_TARGET", "Life", "mod_test_magic_restore_thew_target_after");',
            'if getvar("mod_test_magic_restore_thew_after") >= getvar("mod_test_magic_restore_thew_before") + 10 then assign("mod_test_magic_restore_thew_owner", 1) end',
            'if getvar("mod_test_magic_restore_thew_target_after") <= getvar("mod_test_magic_restore_thew_target_before") - 30 then assign("mod_test_magic_restore_thew_target_damage", 1) end',
            "end",
            "cleareffect();",
            'delnpc("MOD_TEST_RESTORE_TARGET");',
            'displaymessage("正在测试武功召唤");',
            'delnpc("MOD_TEST_SUMMON_NPC");',
            'getplayerstate("SummonedNpcCount", "mod_test_magic_summon_count_before");',
            'getmapstate(36, 20, "NpcCount", "mod_test_magic_summon_second_tile_before");',
            'usemagic("mod_test_magic_summon.ini", 35, 20);',
            "sleep(500);",
            'getnpcstate("MOD_TEST_SUMMON_NPC", "Exists", "mod_test_magic_summon_exists");',
            'getnpcstate("MOD_TEST_SUMMON_NPC", "IsSummonedByMagic", "mod_test_magic_summon_attached");',
            'getnpcstate("MOD_TEST_SUMMON_NPC", "SummonOwnerIsPlayer", "mod_test_magic_summon_owner_player");',
            'getnpcstate("MOD_TEST_SUMMON_NPC", "SummonMagicLauncherKind", "mod_test_magic_summon_launcher");',
            'getnpcstate("MOD_TEST_SUMMON_NPC", "Relation", "mod_test_magic_summon_relation");',
            'getplayerstate("SummonedNpcCount", "mod_test_magic_summon_count_first");',
            'getplayerstate("SummonedNpcsCount", "mod_test_magic_summon_count_plural_first");',
            'usemagic("mod_test_magic_summon.ini", 36, 20);',
            'assign("mod_test_magic_summon_second_tile_added", 0);',
            'assign("mod_test_magic_summon_maxcount_replaced", 0);',
            'for i = 1, 20 do',
            "sleep(80);",
            'getplayerstate("SummonedNpcCount", "mod_test_magic_summon_count_second");',
            'getmapstate(36, 20, "NpcCount", "mod_test_magic_summon_second_tile_after_second");',
            'if getvar("mod_test_magic_summon_second_tile_after_second") > getvar("mod_test_magic_summon_second_tile_before") then assign("mod_test_magic_summon_second_tile_added", 1) end',
            'if getvar("mod_test_magic_summon_count_second") == 1 and getvar("mod_test_magic_summon_second_tile_added") == 1 then assign("mod_test_magic_summon_maxcount_replaced", 1) end',
            "end",
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'addnpc("mod_test_collision_caster_npc.ini", 34, 22, 0);',
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_summon.ini", 37, 20, 1);',
            "sleep(500);",
            'getnpcstate("MOD_TEST_COLLISION_CASTER", "SummonedNpcsCount", "mod_test_magic_summon_caster_count_plural");',
            'getnpcstate("MOD_TEST_COLLISION_CASTER", "LiveSummonedNpcsCount", "mod_test_magic_summon_caster_live_count_plural");',
            'if getvar("mod_test_magic_summon_caster_count_plural") ~= 1 or getvar("mod_test_magic_summon_caster_live_count_plural") ~= 1 then assign("mod_test_magic_summon_maxcount_replaced", 0) end',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'delnpc("MOD_TEST_SUMMON_NPC");',
            'displaymessage("正在测试球形武功");',
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'usemagic("mod_test_magic_ball.ini", 34, 16);',
            'assign("mod_test_magic_ball", 1);',
            'assign("mod_test_magic_ball_projectile", 0);',
            'assign("mod_test_magic_ball_reflected", 0);',
            'for i = 1, 8 do',
            "sleep(120);",
            'geteffectstate("mod_test_magic_ball.ini", "ProjectileCount", "mod_test_magic_ball_projectile_count");',
            'geteffectstate("mod_test_magic_ball.ini", "ActiveProjectileFlyingDirectionY", "mod_test_magic_ball_active_fly_y");',
            'if getvar("mod_test_magic_ball_projectile_count") > 0 then assign("mod_test_magic_ball_projectile", 1) end',
            'if getvar("mod_test_magic_ball_projectile_count") > 0 and getvar("mod_test_magic_ball_active_fly_y") > 0 then assign("mod_test_magic_ball_reflected", 1) end',
            "end",
            "sleep(600);",
            "cleareffect();",
            'displaymessage("正在测试球形武功撞墙");',
            'setplayerpos(31, 20);',
            'setplayerdir(4);',
            'getmapstate(30, 20, "CanFly", "mod_test_magic_wall_source_can_fly");',
            'getmapstate(29, 20, "CanFly", "mod_test_magic_wall_target_can_fly");',
            'assign("mod_test_magic_ball_wall", 1);',
            'assign("mod_test_magic_ball_wall_reflected", 0);',
            'usemagic("mod_test_magic_ball.ini", 29, 20);',
            'for i = 1, 16 do',
            "sleep(60);",
            'geteffectstate("mod_test_magic_ball.ini", "ProjectileCount", "mod_test_magic_ball_wall_projectile_count");',
            'geteffectstate("mod_test_magic_ball.ini", "ActiveProjectileFlyingDirectionX", "mod_test_magic_ball_wall_fly_x");',
            'if getvar("mod_test_magic_wall_source_can_fly") == 1 and getvar("mod_test_magic_wall_target_can_fly") == 0 and getvar("mod_test_magic_ball_wall_projectile_count") > 0 and getvar("mod_test_magic_ball_wall_fly_x") > 0 then assign("mod_test_magic_ball_wall_reflected", 1) end',
            "end",
            "cleareffect();",
            'displaymessage("正在测试全体攻击弹体");',
            'setplayerpos(34,20);',
            'setplayerdir(6);',
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_FRIEND");',
            'addnpc("mod_test_collision_friend_npc.ini", 34, 16, 0);',
            'getmagicstate("mod_test_magic_attack_all_projectile.ini", "AttackAll", "mod_test_magic_attack_all_projectile_attack_all", 1);',
            'getmagicstate("mod_test_magic_attack_all_projectile.ini", "Effect", "mod_test_magic_attack_all_projectile_effect", 1);',
            'assign("mod_test_magic_attack_all_projectile", 1);',
            'assign("mod_test_magic_attack_all_projectile_state", 0);',
            'if getvar("mod_test_magic_attack_all_projectile_attack_all") == 1 and getvar("mod_test_magic_attack_all_projectile_effect") == 40 then assign("mod_test_magic_attack_all_projectile_state", 1) end',
            'assign("mod_test_magic_attack_all_projectile_friend_ready", 0);',
            'getnpcstate("MOD_TEST_COLLISION_FRIEND", "Exists", "mod_test_magic_attack_all_projectile_friend_exists");',
            'getnpcstate("MOD_TEST_COLLISION_FRIEND", "Kind", "mod_test_magic_attack_all_projectile_friend_kind");',
            'getnpcstate("MOD_TEST_COLLISION_FRIEND", "Relation", "mod_test_magic_attack_all_projectile_friend_relation");',
            'getnpcstate("MOD_TEST_COLLISION_FRIEND", "IsFighter", "mod_test_magic_attack_all_projectile_friend_fighter");',
            'getnpcstate("MOD_TEST_COLLISION_FRIEND", "MapX", "mod_test_magic_attack_all_projectile_friend_x");',
            'getnpcstate("MOD_TEST_COLLISION_FRIEND", "MapY", "mod_test_magic_attack_all_projectile_friend_y");',
            'if getvar("mod_test_magic_attack_all_projectile_friend_exists") == 1 and getvar("mod_test_magic_attack_all_projectile_friend_kind") == 1 and getvar("mod_test_magic_attack_all_projectile_friend_relation") == 0 and getvar("mod_test_magic_attack_all_projectile_friend_fighter") == 1 and getvar("mod_test_magic_attack_all_projectile_friend_x") == 34 and getvar("mod_test_magic_attack_all_projectile_friend_y") == 16 then assign("mod_test_magic_attack_all_projectile_friend_ready", 1) end',
            'assign("mod_test_magic_attack_all_projectile_friendly_damage", 0);',
            'assign("mod_test_magic_attack_all_projectile_exploded", 0);',
            'getnpcstate("MOD_TEST_COLLISION_FRIEND", "Life", "mod_test_magic_attack_all_projectile_life_before");',
            'usemagic("mod_test_magic_attack_all_projectile.ini", 34, 10);',
            'for i = 1, 20 do',
            "sleep(80);",
            'getnpcstate("MOD_TEST_COLLISION_FRIEND", "Life", "mod_test_magic_attack_all_projectile_life_after");',
            'geteffectstate("mod_test_magic_attack_all_projectile.ini", "ExplodingCount", "mod_test_magic_attack_all_projectile_exploding_count");',
            'if getvar("mod_test_magic_attack_all_projectile_life_after") < getvar("mod_test_magic_attack_all_projectile_life_before") then assign("mod_test_magic_attack_all_projectile_friendly_damage", 1) end',
            'if getvar("mod_test_magic_attack_all_projectile_exploding_count") > 0 then assign("mod_test_magic_attack_all_projectile_exploded", 1) end',
            "end",
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_FRIEND");',
            'displaymessage("正在测试全体攻击追踪敌人");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            "disablepartnercombat();",
            'delnpc("MOD_TEST_TRACE_NONFIGHTER");',
            'delnpc("MOD_TEST_COLLISION_PARTNER");',
            'addnpc("mod_test_magic_trace_nonfighter_npc.ini", 38, 16, 0);',
            'addnpc("mod_test_collision_partner_npc.ini", 34, 34, 0);',
            'getmagicstate("mod_test_magic_attack_all_trace_enemy.ini", "AttackAll", "mod_test_magic_attack_all_trace_enemy_attack_all", 1);',
            'getnpcstate("MOD_TEST_TRACE_NONFIGHTER", "Exists", "mod_test_magic_attack_all_trace_enemy_nonfighter_exists");',
            'getnpcstate("MOD_TEST_TRACE_NONFIGHTER", "IsFighter", "mod_test_magic_attack_all_trace_enemy_nonfighter_is_fighter");',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "Exists", "mod_test_magic_attack_all_trace_enemy_partner_exists");',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "Kind", "mod_test_magic_attack_all_trace_enemy_partner_kind");',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "IsFighter", "mod_test_magic_attack_all_trace_enemy_partner_is_fighter");',
            'assign("mod_test_magic_attack_all_trace_enemy", 1);',
            'assign("mod_test_magic_attack_all_trace_enemy_state", 0);',
            'assign("mod_test_magic_attack_all_trace_enemy_nonfighter_ready", 0);',
            'assign("mod_test_magic_attack_all_trace_enemy_partner_ready", 0);',
            'assign("mod_test_magic_attack_all_trace_enemy_fighter_trace", 0);',
            'if getvar("mod_test_magic_attack_all_trace_enemy_attack_all") == 1 then assign("mod_test_magic_attack_all_trace_enemy_state", 1) end',
            'if getvar("mod_test_magic_attack_all_trace_enemy_nonfighter_exists") == 1 and getvar("mod_test_magic_attack_all_trace_enemy_nonfighter_is_fighter") == 0 then assign("mod_test_magic_attack_all_trace_enemy_nonfighter_ready", 1) end',
            'if getvar("mod_test_magic_attack_all_trace_enemy_partner_exists") == 1 and getvar("mod_test_magic_attack_all_trace_enemy_partner_kind") == 3 and getvar("mod_test_magic_attack_all_trace_enemy_partner_is_fighter") == 1 then assign("mod_test_magic_attack_all_trace_enemy_partner_ready", 1) end',
            'usemagic("mod_test_magic_attack_all_trace_enemy.ini", 42, 20);',
            "sleep(60);",
            'geteffectstate("mod_test_magic_attack_all_trace_enemy.ini", "ProjectileCount", "mod_test_magic_attack_all_trace_enemy_projectile_count");',
            'geteffectstate("mod_test_magic_attack_all_trace_enemy.ini", "ActiveProjectileFlyingDirectionX", "mod_test_magic_attack_all_trace_enemy_fly_x_before");',
            'geteffectstate("mod_test_magic_attack_all_trace_enemy.ini", "ActiveProjectileFlyingDirectionY", "mod_test_magic_attack_all_trace_enemy_fly_y_before");',
            "sleep(80);",
            'geteffectstate("mod_test_magic_attack_all_trace_enemy.ini", "ActiveProjectileFlyingDirectionX", "mod_test_magic_attack_all_trace_enemy_fly_x_after");',
            'geteffectstate("mod_test_magic_attack_all_trace_enemy.ini", "ActiveProjectileFlyingDirectionY", "mod_test_magic_attack_all_trace_enemy_fly_y_after");',
            'if getvar("mod_test_magic_attack_all_trace_enemy_state") == 1 and getvar("mod_test_magic_attack_all_trace_enemy_nonfighter_ready") == 1 and getvar("mod_test_magic_attack_all_trace_enemy_partner_ready") == 1 and getvar("mod_test_magic_attack_all_trace_enemy_projectile_count") == 1 and getvar("mod_test_magic_attack_all_trace_enemy_fly_y_after") > getvar("mod_test_magic_attack_all_trace_enemy_fly_y_before") and getvar("mod_test_magic_attack_all_trace_enemy_fly_x_after") < getvar("mod_test_magic_attack_all_trace_enemy_fly_x_before") then assign("mod_test_magic_attack_all_trace_enemy_fighter_trace", 1) end',
            "cleareffect();",
            'delnpc("MOD_TEST_TRACE_NONFIGHTER");',
            'delnpc("MOD_TEST_COLLISION_PARTNER");',
            'displaymessage("正在测试撞墙爆炸");',
            'setplayerpos(31, 20);',
            'setplayerdir(4);',
            'assign("mod_test_magic_wall_explode", 1);',
            'assign("mod_test_magic_wall_exploded_on_wall", 0);',
            'usemagic("mod_test_magic_wall.ini", 29, 20);',
            'for i = 1, 20 do',
            "sleep(60);",
            'geteffectstate("mod_test_magic_wall.ini", "ExplodingCount", "mod_test_magic_wall_exploding_count");',
            'geteffectstate("mod_test_magic_wall.ini", "LifeExhaustCount", "mod_test_magic_wall_life_exhaust_count");',
            'geteffectstate("mod_test_magic_wall.ini", "ProjectileCount", "mod_test_magic_wall_projectile_count");',
            'if getvar("mod_test_magic_wall_source_can_fly") == 1 and getvar("mod_test_magic_wall_target_can_fly") == 0 and getvar("mod_test_magic_wall_projectile_count") == 0 and getvar("mod_test_magic_wall_exploding_count") > 0 then assign("mod_test_magic_wall_exploded_on_wall", 1) end',
            'if getvar("mod_test_magic_wall_source_can_fly") == 1 and getvar("mod_test_magic_wall_target_can_fly") == 0 and getvar("mod_test_magic_wall_life_exhaust_count") > 0 then assign("mod_test_magic_wall_exploded_on_wall", 1) end',
            "end",
            "cleareffect();",
            'displaymessage("正在测试穿墙武功");',
            'setplayerpos(31, 20);',
            'setplayerdir(4);',
            'assign("mod_test_magic_wall_blocked_on_wall", getvar("mod_test_magic_wall_exploded_on_wall"));',
            'assign("mod_test_magic_wall_exploded_on_wall", 0);',
            'getmagicstate("mod_test_magic_pass_through_wall.ini", "PassThroughWall", "mod_test_magic_pass_through_wall_flag");',
            'assign("mod_test_magic_pass_through_wall", 1);',
            'assign("mod_test_magic_pass_through_wall_state", 0);',
            'if getvar("mod_test_magic_wall_source_can_fly") == 1 and getvar("mod_test_magic_wall_target_can_fly") == 0 and getvar("mod_test_magic_pass_through_wall_flag") == 1 then assign("mod_test_magic_pass_through_wall_state", 1) end',
            'assign("mod_test_magic_pass_through_wall_projectile", 0);',
            'assign("mod_test_magic_pass_through_wall_crossed", 0);',
            'assign("mod_test_magic_pass_through_wall_no_explode", 0);',
            'usemagic("mod_test_magic_pass_through_wall.ini", 27, 20);',
            "for i = 1, 24 do",
            "sleep(60);",
            'geteffectstate("mod_test_magic_pass_through_wall.ini", "ProjectileCount", "mod_test_magic_pass_through_wall_projectile_count");',
            'geteffectstate("mod_test_magic_pass_through_wall.ini", "ActiveProjectileMapX", "mod_test_magic_pass_through_wall_projectile_x");',
            'geteffectstate("mod_test_magic_pass_through_wall.ini", "ExplodingCount", "mod_test_magic_pass_through_wall_exploding_count");',
            'if getvar("mod_test_magic_pass_through_wall_state") == 1 and getvar("mod_test_magic_pass_through_wall_projectile_count") > 0 then assign("mod_test_magic_pass_through_wall_projectile", 1) end',
            'if getvar("mod_test_magic_pass_through_wall_state") == 1 and getvar("mod_test_magic_pass_through_wall_projectile_count") > 0 and getvar("mod_test_magic_pass_through_wall_projectile_x") <= 29 then assign("mod_test_magic_pass_through_wall_crossed", 1) end',
            'if getvar("mod_test_magic_pass_through_wall_state") == 1 and getvar("mod_test_magic_pass_through_wall_projectile_count") > 0 and getvar("mod_test_magic_pass_through_wall_projectile_x") <= 29 and getvar("mod_test_magic_pass_through_wall_exploding_count") == 0 then assign("mod_test_magic_pass_through_wall_no_explode", 1) end',
            'if getvar("mod_test_magic_wall_blocked_on_wall") == 1 and getvar("mod_test_magic_pass_through_wall_crossed") == 1 and getvar("mod_test_magic_pass_through_wall_no_explode") == 1 then assign("mod_test_magic_wall_exploded_on_wall", 1) end',
            "end",
            "cleareffect();",
            'displaymessage("正在测试穿透目标");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_BLOCKER");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'addnpc("mod_test_collision_blocker_npc.ini", 34, 14, 0);',
            'getmagicstate("mod_test_magic_pass_through.ini", "PassThrough", "mod_test_magic_pass_through_flag");',
            'getmagicstate("mod_test_magic_pass_through.ini", "PassThroughWithDestroyEffect", "mod_test_magic_pass_through_destroy_flag");',
            'getmagicstate("mod_test_magic_pass_through.ini", "PassThroughWall", "mod_test_magic_pass_through_wall_flag");',
            'assign("mod_test_magic_pass_through_state", 0);',
            'if getvar("mod_test_magic_pass_through_flag") == 1 and getvar("mod_test_magic_pass_through_destroy_flag") == 1 and getvar("mod_test_magic_pass_through_wall_flag") == 0 then assign("mod_test_magic_pass_through_state", 1) end',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_pass_through_target1_life_before");',
            'getnpcstate("MOD_TEST_COLLISION_BLOCKER", "Life", "mod_test_magic_pass_through_target2_life_before");',
            'usemagic("mod_test_magic_pass_through.ini", 34, 10);',
            'assign("mod_test_magic_pass_through", 1);',
            'assign("mod_test_magic_pass_through_target1_damage", 0);',
            'assign("mod_test_magic_pass_through_target2_damage", 0);',
            'assign("mod_test_magic_pass_through_hits", 0);',
            'assign("mod_test_magic_pass_through_destroy_effect", 0);',
            "for i = 1, 36 do",
            "sleep(60);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_pass_through_target1_life_after");',
            'getnpcstate("MOD_TEST_COLLISION_BLOCKER", "Life", "mod_test_magic_pass_through_target2_life_after");',
            'geteffectstate("mod_test_magic_pass_through.ini", "ActiveProjectilePassThroughHitCount", "mod_test_magic_pass_through_hit_count");',
            'geteffectstate("mod_test_magic_pass_through.ini", "ExplodingCount", "mod_test_magic_pass_through_exploding_count");',
            'if getvar("mod_test_magic_pass_through_target1_life_after") < getvar("mod_test_magic_pass_through_target1_life_before") then assign("mod_test_magic_pass_through_target1_damage", 1) end',
            'if getvar("mod_test_magic_pass_through_target2_life_after") < getvar("mod_test_magic_pass_through_target2_life_before") then assign("mod_test_magic_pass_through_target2_damage", 1) end',
            'if getvar("mod_test_magic_pass_through_hit_count") >= 2 then assign("mod_test_magic_pass_through_hits", 1) end',
            'if getvar("mod_test_magic_pass_through_exploding_count") > 0 then assign("mod_test_magic_pass_through_destroy_effect", 1) end',
            "end",
            "cleareffect();",
            'displaymessage("正在测试黏附武功");',
            'setplayerpos(34, 24);',
            'setplayerdir(6);',
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'usemagic("mod_test_magic_sticky.ini", 34, 16);',
            'assign("mod_test_magic_sticky", 1);',
            'assign("mod_test_magic_sticky_attached", 0);',
            'for i = 1, 40 do',
            "sleep(40);",
            'geteffectstate("mod_test_magic_sticky.ini", "HasStickyTarget", "mod_test_magic_sticky_target");',
            'geteffectstate("mod_test_magic_sticky.ini", "AttachedNpcCount", "mod_test_magic_sticky_attached_count");',
            'if getvar("mod_test_magic_sticky_target") == 1 and getvar("mod_test_magic_sticky_attached_count") > 0 then assign("mod_test_magic_sticky_attached", 1) end',
            "end",
            "sleep(600);",
            "cleareffect();",
            'setplayerpos(34, 20);',
            'setplayerdir(6);',
            'displaymessage("正在测试实体武功");',
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 15, 0);',
            'usemagic("mod_test_magic_solid.ini", 34, 14);',
            'assign("mod_test_magic_solid", 1);',
            'assign("mod_test_magic_solid_obstacle", 0);',
            'assign("mod_test_magic_solid_blocks_walk", 0);',
            'assign("mod_test_magic_solid_clears_effect", 0);',
            'assign("mod_test_magic_solid_seen_map_x", 34);',
            'assign("mod_test_magic_solid_seen_map_y", 14);',
            'for i = 1, 16 do',
            "sleep(60);",
            'geteffectstate("mod_test_magic_solid.ini", "SolidObstacleCount", "mod_test_magic_solid_obstacle_count");',
            'geteffectstate("mod_test_magic_solid.ini", "IsSolidObstacle", "mod_test_magic_solid_first_obstacle");',
            'geteffectstate("mod_test_magic_solid.ini", "MapX", "mod_test_magic_solid_map_x");',
            'geteffectstate("mod_test_magic_solid.ini", "MapY", "mod_test_magic_solid_map_y");',
            'getmapstate(getvar("mod_test_magic_solid_map_x"), getvar("mod_test_magic_solid_map_y"), "CanWalk", "mod_test_magic_solid_can_walk_active");',
            'if getvar("mod_test_magic_solid_obstacle_count") > 0 then assign("mod_test_magic_solid_seen_map_x", getvar("mod_test_magic_solid_map_x")) end',
            'if getvar("mod_test_magic_solid_obstacle_count") > 0 then assign("mod_test_magic_solid_seen_map_y", getvar("mod_test_magic_solid_map_y")) end',
            'if getvar("mod_test_magic_solid_obstacle_count") > 0 and getvar("mod_test_magic_solid_first_obstacle") == 1 then assign("mod_test_magic_solid_obstacle", 1) end',
            'if getvar("mod_test_magic_solid_obstacle_count") > 0 and getvar("mod_test_magic_solid_can_walk_active") == 0 then assign("mod_test_magic_solid_blocks_walk", 1) end',
            "end",
            "cleareffect();",
            "sleep(160);",
            'getmapstate(getvar("mod_test_magic_solid_seen_map_x"), getvar("mod_test_magic_solid_seen_map_y"), "HasSolidEffect", "mod_test_magic_solid_effect_after");',
            'if getvar("mod_test_magic_solid_effect_after") == 0 then assign("mod_test_magic_solid_clears_effect", 1) end',
            'displaymessage("正在测试寄生武功");',
            'setplayerpos(34, 24);',
            'setplayerdir(6);',
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'usemagic("mod_test_magic_parasitic.ini", 34, 16);',
            'assign("mod_test_magic_parasitic", 1);',
            'assign("mod_test_magic_parasitic_active_seen", 0);',
            'assign("mod_test_magic_parasitic_total_seen", 0);',
            'for i = 1, 45 do',
            "sleep(80);",
            'geteffectstate("mod_test_magic_parasitic.ini", "ParasiticActiveCount", "mod_test_magic_parasitic_active_count");',
            'geteffectstate("mod_test_magic_parasitic.ini", "HasParasiticTarget", "mod_test_magic_parasitic_has_target");',
            'geteffectstate("mod_test_magic_parasitic.ini", "ParasiticTotalEffect", "mod_test_magic_parasitic_total");',
            'if getvar("mod_test_magic_parasitic_active_count") > 0 and getvar("mod_test_magic_parasitic_has_target") == 1 then assign("mod_test_magic_parasitic_active_seen", 1) end',
            'if getvar("mod_test_magic_parasitic_total") > 0 then assign("mod_test_magic_parasitic_total_seen", 1) end',
            "end",
            "cleareffect();",
            'displaymessage("正在测试范围加速");',
            'setplayerpos(34,20);',
            'setplayerdir(6);',
            'usemagic("mod_test_magic_range_speedup.ini", 34, 20);',
            'assign("mod_test_magic_range_speedup", 1);',
            'assign("mod_test_magic_range_speedup_active_seen", 0);',
            'assign("mod_test_magic_range_speedup_effect_seen", 0);',
            'assign("mod_test_magic_range_speedup_fold_seen", 0);',
            'assign("mod_test_magic_range_speedup_expired", 0);',
            'assign("mod_test_magic_range_speedup_reapplied", 0);',
            'assign("mod_test_magic_range_speedup_reapply_fold_seen", 0);',
            'for i = 1, 20 do',
            "sleep(80);",
            'geteffectstate("mod_test_magic_range_speedup.ini", "RangeSpeedUpActiveCount", "mod_test_magic_range_speedup_effect_count");',
            'getplayerstate("SppedUpByMagicSprite", "mod_test_magic_range_speedup_player_active");',
            'getplayerstate("MoveSpeedFoldPermille", "mod_test_magic_range_speedup_fold");',
            'if getvar("mod_test_magic_range_speedup_effect_count") > 0 then assign("mod_test_magic_range_speedup_effect_seen", 1) end',
            'if getvar("mod_test_magic_range_speedup_player_active") == 1 then assign("mod_test_magic_range_speedup_active_seen", 1) end',
            'if getvar("mod_test_magic_range_speedup_fold") > 1000 then assign("mod_test_magic_range_speedup_fold_seen", 1) end',
            "end",
            "sleep(2200);",
            'geteffectstate("mod_test_magic_range_speedup.ini", "RangeSpeedUpActiveCount", "mod_test_magic_range_speedup_effect_count_after_life");',
            'getplayerstate("SppedUpByMagicSprite", "mod_test_magic_range_speedup_player_after_life");',
            'getplayerstate("MoveSpeedFoldPermille", "mod_test_magic_range_speedup_fold_after_life");',
            'if getvar("mod_test_magic_range_speedup_effect_count_after_life") == 0 and getvar("mod_test_magic_range_speedup_player_after_life") == 0 and getvar("mod_test_magic_range_speedup_fold_after_life") <= 1000 then assign("mod_test_magic_range_speedup_expired", 1) end',
            'usemagic("mod_test_magic_range_speedup.ini", 34, 20);',
            'for i = 1, 10 do',
            "sleep(80);",
            'getplayerstate("SppedUpByMagicSprite", "mod_test_magic_range_speedup_player_reapplied");',
            'getplayerstate("MoveSpeedFoldPermille", "mod_test_magic_range_speedup_fold_reapplied");',
            'if getvar("mod_test_magic_range_speedup_player_reapplied") == 1 then assign("mod_test_magic_range_speedup_reapplied", 1) end',
            'if getvar("mod_test_magic_range_speedup_fold_reapplied") > 1000 then assign("mod_test_magic_range_speedup_reapply_fold_seen", 1) end',
            "end",
            "sleep(3200);",
            'geteffectstate("mod_test_magic_range_speedup.ini", "RangeSpeedUpActiveCount", "mod_test_magic_range_speedup_effect_count_after_reapply_life");',
            'getplayerstate("SppedUpByMagicSprite", "mod_test_magic_range_speedup_player_after_clear");',
            'getplayerstate("MoveSpeedFoldPermille", "mod_test_magic_range_speedup_fold_after_clear");',
            'assign("mod_test_magic_range_speedup_cleared", 0);',
            'if getvar("mod_test_magic_range_speedup_effect_count_after_reapply_life") == 0 and getvar("mod_test_magic_range_speedup_player_after_clear") == 0 and getvar("mod_test_magic_range_speedup_fold_after_clear") <= 1000 then assign("mod_test_magic_range_speedup_cleared", 1) end',
            "cleareffect();",
            "sleep(160);",
            'displaymessage("正在测试范围攻击");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'getmagicstate("mod_test_magic_range_attack.ini", "RangeEffect", "mod_test_magic_range_attack_effect");',
            'getmagicstate("mod_test_magic_range_attack.ini", "RangeRadius", "mod_test_magic_range_attack_radius");',
            'getmagicstate("mod_test_magic_range_attack.ini", "RangeTimeInterval", "mod_test_magic_range_attack_interval");',
            'getmagicstate("mod_test_magic_range_attack.ini", "RangeFreeze", "mod_test_magic_range_attack_freeze_ms", 1);',
            'getmagicstate("mod_test_magic_range_attack.ini", "RangeDamage", "mod_test_magic_range_attack_damage_value", 1);',
            'getmagicstate("mod_test_magic_range_attack.ini", "Effect2", "mod_test_magic_range_attack_effect2", 1);',
            'getmagicstate("mod_test_magic_range_attack.ini", "Effect3", "mod_test_magic_range_attack_effect3", 1);',
            'getmagicstate("mod_test_magic_range_attack.ini", "EffectMana", "mod_test_magic_range_attack_effect_mana", 1);',
            'assign("mod_test_magic_range_attack_state", 0);',
            'if getvar("mod_test_magic_range_attack_effect") == 1 and getvar("mod_test_magic_range_attack_radius") == 4 and getvar("mod_test_magic_range_attack_interval") == 100 and getvar("mod_test_magic_range_attack_freeze_ms") == 900 and getvar("mod_test_magic_range_attack_damage_value") == 25 and getvar("mod_test_magic_range_attack_effect2") == 17 and getvar("mod_test_magic_range_attack_effect3") == 13 and getvar("mod_test_magic_range_attack_effect_mana") == 15 then assign("mod_test_magic_range_attack_state", 1) end',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_range_attack_life_before");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Mana", "mod_test_magic_range_attack_mana_before");',
            'usemagic("mod_test_magic_range_attack.ini", 34, 20);',
            'assign("mod_test_magic_range_attack", 1);',
            'assign("mod_test_magic_range_attack_frozen", 0);',
            'assign("mod_test_magic_range_attack_damage", 0);',
            'assign("mod_test_magic_range_attack_no_mana_damage", 0);',
            "for i = 1, 20 do",
            "sleep(80);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "IsFrozened", "mod_test_magic_range_attack_frozen_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "FrozenMilliseconds", "mod_test_magic_range_attack_frozen_ms_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_range_attack_life_after");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Mana", "mod_test_magic_range_attack_mana_after");',
            'if getvar("mod_test_magic_range_attack_frozen_raw") == 1 and getvar("mod_test_magic_range_attack_frozen_ms_raw") > 0 then assign("mod_test_magic_range_attack_frozen", 1) end',
            'if getvar("mod_test_magic_range_attack_life_after") < getvar("mod_test_magic_range_attack_life_before") then assign("mod_test_magic_range_attack_damage", 1) end',
            'if getvar("mod_test_magic_range_attack_mana_after") == getvar("mod_test_magic_range_attack_mana_before") then assign("mod_test_magic_range_attack_no_mana_damage", 1) end',
            "end",
            "cleareffect();",
            'assign("mod_test_magic_range_attack_enemy_damage", getvar("mod_test_magic_range_attack_damage"));',
            'assign("mod_test_magic_range_attack_damage", 0);',
            'displaymessage("正在测试全体范围攻击");',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'addnpc("mod_test_collision_caster_npc.ini", 34, 18, 0);',
            'getmagicstate("mod_test_magic_range_attack_all.ini", "AttackAll", "mod_test_magic_range_attack_all_attack_all", 1);',
            'getmagicstate("mod_test_magic_range_attack_all.ini", "RangeFreeze", "mod_test_magic_range_attack_all_freeze_ms", 1);',
            'assign("mod_test_magic_range_attack_all_state", 0);',
            'if getvar("mod_test_magic_range_attack_all_attack_all") == 1 and getvar("mod_test_magic_range_attack_all_freeze_ms") == 600 then assign("mod_test_magic_range_attack_all_state", 1) end',
            'assign("mod_test_magic_range_attack_all_self_status", 0);',
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_range_attack_all.ini", 34, 18, 1);',
            "for i = 1, 20 do",
            "sleep(80);",
            'getnpcstate("MOD_TEST_COLLISION_CASTER", "IsFrozened", "mod_test_magic_range_attack_all_self_frozen_raw");',
            'getnpcstate("MOD_TEST_COLLISION_CASTER", "FrozenMilliseconds", "mod_test_magic_range_attack_all_self_frozen_ms_raw");',
            'if getvar("mod_test_magic_range_attack_all_self_frozen_raw") == 1 and getvar("mod_test_magic_range_attack_all_self_frozen_ms_raw") > 0 then assign("mod_test_magic_range_attack_all_self_status", 1) end',
            "end",
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'if getvar("mod_test_magic_range_attack_enemy_damage") == 1 and getvar("mod_test_magic_range_attack_all_state") == 1 and getvar("mod_test_magic_range_attack_all_self_status") == 1 then assign("mod_test_magic_range_attack_damage", 1) end',
            'setplayerpos(34,20);',
            'setplayerdir(6);',
            'displaymessage("正在测试弹跳武功");',
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_BLOCKER");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'usemagic("mod_test_magic_bounce.ini", 34, 16);',
            'assign("mod_test_magic_bounce", 1);',
            'assign("mod_test_magic_bounce_active_seen", 0);',
            'assign("mod_test_magic_bounce_action_seen", 0);',
            'assign("mod_test_magic_bounce_velocity_seen", 0);',
            "for i = 1, 20 do",
            "sleep(80);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "IsBouncing", "mod_test_magic_bounce_active_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "CurrentAction", "mod_test_magic_bounce_action_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "BounceVelocity", "mod_test_magic_bounce_velocity_raw");',
            'if getvar("mod_test_magic_bounce_active_raw") == 1 then assign("mod_test_magic_bounce_active_seen", 1) end',
            'if getvar("mod_test_magic_bounce_action_raw") == 25 then assign("mod_test_magic_bounce_action_seen", 1) end',
            'if getvar("mod_test_magic_bounce_velocity_raw") > 0 then assign("mod_test_magic_bounce_velocity_seen", 1) end',
            "end",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "MapY", "mod_test_magic_bounce_target_y");',
            'assign("mod_test_magic_bounce_moved", 0);',
            'if getvar("mod_test_magic_bounce_target_y") < 16 then assign("mod_test_magic_bounce_moved", 1) end',
            'displaymessage("正在测试弹跳伤害");',
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_BLOCKER");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'addnpc("mod_test_collision_blocker_npc.ini", 33, 11, 0);',
            'usemagic("mod_test_magic_bounce.ini", 34, 16);',
            'assign("mod_test_magic_bounce_hurt", 1);',
            "sleep(1800);",
            'getnpcstate("MOD_TEST_COLLISION_BLOCKER", "Life", "mod_test_magic_bounce_blocker_life");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "LastBounceBlockedByCharacter", "mod_test_magic_bounce_npc_blocked_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "LastBounceBlockedMapX", "mod_test_magic_bounce_npc_blocked_x");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "LastBounceBlockedMapY", "mod_test_magic_bounce_npc_blocked_y");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "LastBounceEndMapY", "mod_test_magic_bounce_npc_end_y");',
            'assign("mod_test_magic_bounce_hurt_applied", 0);',
            'if getvar("mod_test_magic_bounce_blocker_life") == 99994 then assign("mod_test_magic_bounce_hurt_applied", 1) end',
            'assign("mod_test_magic_bounce_npc_blocked", 0);',
            'if getvar("mod_test_magic_bounce_npc_blocked_raw") == 1 and getvar("mod_test_magic_bounce_npc_blocked_x") == 33 and getvar("mod_test_magic_bounce_npc_blocked_y") == 11 and getvar("mod_test_magic_bounce_npc_end_y") > 11 then assign("mod_test_magic_bounce_npc_blocked", 1) end',
            'displaymessage("正在测试玩家阻挡弹跳");',
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_BLOCKER");',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            "setplayerpos(33, 11);",
            "setplayerdir(6);",
            "fulllife();",
            'getplayerstate("Life", "mod_test_magic_bounce_player_life_before");',
            'addnpc("mod_test_collision_caster_npc.ini", 34, 20, 0);',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_bounce.ini", 34, 16, 1);',
            'assign("mod_test_magic_bounce_player_block", 1);',
            "sleep(1800);",
            'getplayerstate("Life", "mod_test_magic_bounce_player_life_after");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "LastBounceBlockedByCharacter", "mod_test_magic_bounce_player_blocked_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "LastBounceBlockedMapX", "mod_test_magic_bounce_player_blocked_x");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "LastBounceBlockedMapY", "mod_test_magic_bounce_player_blocked_y");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "LastBounceEndMapY", "mod_test_magic_bounce_player_end_y");',
            'assign("mod_test_magic_bounce_player_blocked", 0);',
            'if getvar("mod_test_magic_bounce_player_life_after") < getvar("mod_test_magic_bounce_player_life_before") then assign("mod_test_magic_bounce_player_blocked", 1) end',
            'assign("mod_test_magic_bounce_player_blocked_position", 0);',
            'if getvar("mod_test_magic_bounce_player_blocked_raw") == 1 and getvar("mod_test_magic_bounce_player_blocked_x") == 33 and getvar("mod_test_magic_bounce_player_blocked_y") == 11 and getvar("mod_test_magic_bounce_player_end_y") > 11 then assign("mod_test_magic_bounce_player_blocked_position", 1) end',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'displaymessage("正在测试弹飞武功");',
            "setplayerpos(34,24);",
            "setplayerdir(6);",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_BLOCKER");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'usemagic("mod_test_magic_bouncefly.ini", 34, 16);',
            'assign("mod_test_magic_bouncefly", 1);',
            "sleep(700);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "IsMagicForcedMoving", "mod_test_magic_bouncefly_active");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "CurrentAction", "mod_test_magic_bouncefly_action_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "MagicForcedMoveDestinationX", "mod_test_magic_bouncefly_dest_x");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "MagicForcedMoveDestinationY", "mod_test_magic_bouncefly_dest_y");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "MagicForcedMoveSpeed", "mod_test_magic_bouncefly_speed");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "MagicForcedMoveEndHurt", "mod_test_magic_bouncefly_end_hurt");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "MagicForcedMoveTouchHurt", "mod_test_magic_bouncefly_touch_hurt");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "MagicForcedMoveTouchDistance", "mod_test_magic_bouncefly_touch_distance");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "MagicForcedMoveHasTouchDirection", "mod_test_magic_bouncefly_has_touch_direction");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "MagicForcedMoveTouchDirectionY", "mod_test_magic_bouncefly_touch_direction_y");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "MagicForcedMoveHasEndMagic", "mod_test_magic_bouncefly_end_magic");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "MagicForcedMoveBlockCharactersOnPath", "mod_test_magic_bouncefly_block_characters");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "MagicForcedMoveUsesBezier", "mod_test_magic_bouncefly_uses_bezier");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "MagicForcedMoveProgressPermille", "mod_test_magic_bouncefly_progress_permille");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "MagicForcedMoveBezierOffsetLength", "mod_test_magic_bouncefly_bezier_offset_length");',
            'assign("mod_test_magic_bouncefly_action", 0);',
            'if getvar("mod_test_magic_bouncefly_action_raw") == 26 then assign("mod_test_magic_bouncefly_action", 1) end',
            'assign("mod_test_magic_bouncefly_destination_changed", 0);',
            'if getvar("mod_test_magic_bouncefly_dest_x") > 34 then assign("mod_test_magic_bouncefly_destination_changed", 1) end',
            'if getvar("mod_test_magic_bouncefly_dest_x") < 34 then assign("mod_test_magic_bouncefly_destination_changed", 1) end',
            'if getvar("mod_test_magic_bouncefly_dest_y") > 16 then assign("mod_test_magic_bouncefly_destination_changed", 1) end',
            'if getvar("mod_test_magic_bouncefly_dest_y") < 16 then assign("mod_test_magic_bouncefly_destination_changed", 1) end',
            'assign("mod_test_magic_bouncefly_progress_seen", 0);',
            'if getvar("mod_test_magic_bouncefly_progress_permille") > 0 and getvar("mod_test_magic_bouncefly_progress_permille") < 1000 then assign("mod_test_magic_bouncefly_progress_seen", 1) end',
            'assign("mod_test_magic_bouncefly_bezier_seen", 0);',
            'if getvar("mod_test_magic_bouncefly_uses_bezier") == 1 and getvar("mod_test_magic_bouncefly_bezier_offset_length") > 0 then assign("mod_test_magic_bouncefly_bezier_seen", 1) end',
            'assign("mod_test_magic_bouncefly_touch_direction_preserved", 0);',
            'if getvar("mod_test_magic_bouncefly_has_touch_direction") == 1 and getvar("mod_test_magic_bouncefly_touch_direction_y") < 0 then assign("mod_test_magic_bouncefly_touch_direction_preserved", 1) end',
            "sleep(500);",
            'displaymessage("正在测试强制移动交接");',
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'assign("mod_test_magic_forced_handoff", 1);',
            'assign("mod_test_magic_forced_handoff_bounce_seen", 0);',
            'assign("mod_test_magic_forced_handoff_forced_seen", 0);',
            'assign("mod_test_magic_forced_handoff_overlap", 0);',
            'assign("mod_test_magic_forced_handoff_action_forced", 0);',
            'assign("mod_test_magic_forced_handoff_action_bounce", 0);',
            'assign("mod_test_magic_forced_handoff_cast_forced", 0);',
            'usemagic("mod_test_magic_bounce_handoff.ini", 34, 16);',
            "for i = 1, 20 do",
            "sleep(80);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "IsBouncing", "mod_test_magic_forced_handoff_bounce_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "IsMagicForcedMoving", "mod_test_magic_forced_handoff_forced_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "CurrentAction", "mod_test_magic_forced_handoff_action_raw");',
            'if getvar("mod_test_magic_forced_handoff_bounce_raw") == 1 then assign("mod_test_magic_forced_handoff_bounce_seen", 1) end',
            'if getvar("mod_test_magic_forced_handoff_bounce_raw") == 1 and getvar("mod_test_magic_forced_handoff_cast_forced") == 0 then usemagic("mod_test_magic_bouncefly_handoff.ini", 34, 16); assign("mod_test_magic_forced_handoff_cast_forced", 1) end',
            'if getvar("mod_test_magic_forced_handoff_forced_raw") == 1 then assign("mod_test_magic_forced_handoff_forced_seen", 1) end',
            'if getvar("mod_test_magic_forced_handoff_bounce_raw") == 1 and getvar("mod_test_magic_forced_handoff_forced_raw") == 1 then assign("mod_test_magic_forced_handoff_overlap", 1) end',
            'if getvar("mod_test_magic_forced_handoff_forced_raw") == 1 and getvar("mod_test_magic_forced_handoff_action_raw") == 26 then assign("mod_test_magic_forced_handoff_action_forced", 1) end',
            "end",
            "for i = 1, 50 do",
            "sleep(80);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "IsBouncing", "mod_test_magic_forced_handoff_bounce_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "IsMagicForcedMoving", "mod_test_magic_forced_handoff_forced_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "CurrentAction", "mod_test_magic_forced_handoff_action_raw");',
            'if getvar("mod_test_magic_forced_handoff_bounce_raw") == 1 then assign("mod_test_magic_forced_handoff_bounce_seen", 1) end',
            'if getvar("mod_test_magic_forced_handoff_forced_raw") == 1 then assign("mod_test_magic_forced_handoff_forced_seen", 1) end',
            'if getvar("mod_test_magic_forced_handoff_bounce_raw") == 1 and getvar("mod_test_magic_forced_handoff_forced_raw") == 1 then assign("mod_test_magic_forced_handoff_overlap", 1) end',
            'if getvar("mod_test_magic_forced_handoff_forced_raw") == 1 and getvar("mod_test_magic_forced_handoff_action_raw") == 26 then assign("mod_test_magic_forced_handoff_action_forced", 1) end',
            'if getvar("mod_test_magic_forced_handoff_bounce_raw") == 1 and getvar("mod_test_magic_forced_handoff_forced_raw") == 0 and getvar("mod_test_magic_forced_handoff_cast_forced") == 1 and getvar("mod_test_magic_forced_handoff_action_raw") == 25 then assign("mod_test_magic_forced_handoff_action_bounce", 1) end',
            "end",
            'displaymessage("正在测试弹飞接触伤害");',
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_BLOCKER");',
            'delnpc("MOD_TEST_COLLISION_BLOCKER_2");',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            "setplayerpos(36,24);",
            "setplayerdir(6);",
            'addnpc("mod_test_collision_caster_npc.ini", 34, 24, 0);',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'addnpc("mod_test_collision_blocker_npc.ini", 35, 22, 0);',
            'addnpc("mod_test_collision_blocker2_npc.ini", 33, 22, 0);',
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_bouncefly.ini", 34, 16, 1);',
            'assign("mod_test_magic_bouncefly_touch", 1);',
            "sleep(4200);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_bouncefly_target_life");',
            'getnpcstate("MOD_TEST_COLLISION_BLOCKER", "Life", "mod_test_magic_bouncefly_blocker_life");',
            'getnpcstate("MOD_TEST_COLLISION_BLOCKER_2", "Life", "mod_test_magic_bouncefly_blocker2_life");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "MapY", "mod_test_magic_bouncefly_touch_target_y");',
            'getnpcstate("MOD_TEST_COLLISION_BLOCKER", "MapY", "mod_test_magic_bouncefly_touch_blocker_y");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "CurrentAction", "mod_test_magic_bouncefly_touch_target_action");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "LastMagicForcedMoveTouchTargetCount", "mod_test_magic_bouncefly_touch_target_count");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "LastMagicForcedMoveTouchHurtCount", "mod_test_magic_bouncefly_touch_hurt_count");',
            'assign("mod_test_magic_bouncefly_end_hurt_applied", 0);',
            'if getvar("mod_test_magic_bouncefly_target_life") < 99999 then assign("mod_test_magic_bouncefly_end_hurt_applied", 1) end',
            'assign("mod_test_magic_bouncefly_touch_hurt_applied", 0);',
            'if getvar("mod_test_magic_bouncefly_blocker_life") < 99999 then assign("mod_test_magic_bouncefly_touch_hurt_applied", 1) end',
            'assign("mod_test_magic_bouncefly_multi_touch", 0);',
            'if getvar("mod_test_magic_bouncefly_touch_target_count") >= 2 and getvar("mod_test_magic_bouncefly_touch_hurt_count") >= 2 and getvar("mod_test_magic_bouncefly_blocker2_life") < 99999 then assign("mod_test_magic_bouncefly_multi_touch", 1) end',
            'displaymessage("正在测试玩家阻挡弹飞");',
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_BLOCKER");',
            'delnpc("MOD_TEST_COLLISION_BLOCKER_2");',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            "setplayerpos(34,17);",
            "setplayerdir(6);",
            'addnpc("mod_test_collision_caster_npc.ini", 34, 24, 0);',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_bouncefly.ini", 34, 16, 1);',
            "sleep(4200);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "LastMagicForcedMoveBlockedByCharacter", "mod_test_magic_bouncefly_player_blocked_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "LastMagicForcedMoveBlockedMapX", "mod_test_magic_bouncefly_player_blocked_x");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "LastMagicForcedMoveBlockedMapY", "mod_test_magic_bouncefly_player_blocked_y");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "LastMagicForcedMoveEndMapX", "mod_test_magic_bouncefly_player_block_end_x");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "LastMagicForcedMoveEndMapY", "mod_test_magic_bouncefly_player_block_end_y");',
            'assign("mod_test_magic_bouncefly_player_blocked", 0);',
            'if getvar("mod_test_magic_bouncefly_player_blocked_raw") == 1 and getvar("mod_test_magic_bouncefly_player_blocked_x") == 34 and getvar("mod_test_magic_bouncefly_player_blocked_y") == 17 and getvar("mod_test_magic_bouncefly_player_block_end_x") == 34 and getvar("mod_test_magic_bouncefly_player_block_end_y") <= 16 then assign("mod_test_magic_bouncefly_player_blocked", 1) end',
            'displaymessage("正在测试携带四号施法者");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_BLOCKER");',
            'delnpc("MOD_TEST_COLLISION_BLOCKER_2");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 18, 0);',
            'addnpc("mod_test_collision_blocker_npc.ini", 35, 18, 0);',
            'addnpc("mod_test_collision_blocker2_npc.ini", 33, 18, 0);',
            'assign("mod_test_magic_carry_user4_carry_active_seen", 0);',
            'assign("mod_test_magic_carry_user4_attached", 0);',
            'assign("mod_test_magic_carry_user4_multi_attached", 0);',
            'assign("mod_test_magic_carry_user4_neighbor_hurt", 0);',
            'usemagic("mod_test_magic_carry_user4.ini", 34, 16);',
            'assign("mod_test_magic_carry_user4", 1);',
            'assign("mod_test_magic_carry_user4_target_moved", 0);',
            'geteffectstate("mod_test_magic_carry_user4.ini", "CarryUserActive", "mod_test_magic_carry_user4_carry_active");',
            'geteffectstate("mod_test_magic_carry_user4.ini", "HasAttachedNpc", "mod_test_magic_carry_user4_has_attached");',
            'geteffectstate("mod_test_magic_carry_user4.ini", "AttachedNpcCount", "mod_test_magic_carry_user4_attached_count");',
            'if getvar("mod_test_magic_carry_user4_carry_active") == 1 then assign("mod_test_magic_carry_user4_carry_active_seen", 1) end',
            'if getvar("mod_test_magic_carry_user4_has_attached") == 1 and getvar("mod_test_magic_carry_user4_attached_count") > 0 then assign("mod_test_magic_carry_user4_attached", 1) end',
            'for i = 1, 60 do',
            "sleep(20);",
            'geteffectstate("mod_test_magic_carry_user4.ini", "CarryUserActive", "mod_test_magic_carry_user4_carry_active");',
            'geteffectstate("mod_test_magic_carry_user4.ini", "HasAttachedNpc", "mod_test_magic_carry_user4_has_attached");',
            'geteffectstate("mod_test_magic_carry_user4.ini", "AttachedNpcCount", "mod_test_magic_carry_user4_attached_count");',
            'geteffectstate("mod_test_magic_carry_user4.ini", "HasMovedAttachedNpc", "mod_test_magic_carry_user4_has_moved_attached");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "MapY", "mod_test_magic_carry_user4_target_y");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "HasNonZeroOffset", "mod_test_magic_carry_user4_target_offset");',
            'getnpcstate("MOD_TEST_COLLISION_BLOCKER", "Life", "mod_test_magic_carry_user4_blocker_life");',
            'getnpcstate("MOD_TEST_COLLISION_BLOCKER_2", "Life", "mod_test_magic_carry_user4_blocker2_life");',
            'if getvar("mod_test_magic_carry_user4_carry_active") == 1 then assign("mod_test_magic_carry_user4_carry_active_seen", 1) end',
            'if getvar("mod_test_magic_carry_user4_has_attached") == 1 and getvar("mod_test_magic_carry_user4_attached_count") > 0 then assign("mod_test_magic_carry_user4_attached", 1) end',
            'if getvar("mod_test_magic_carry_user4_attached_count") >= 2 then assign("mod_test_magic_carry_user4_multi_attached", 1) end',
            'if getvar("mod_test_magic_carry_user4_target_y") < 18 then assign("mod_test_magic_carry_user4_target_moved", 1) end',
            'if getvar("mod_test_magic_carry_user4_target_offset") == 1 then assign("mod_test_magic_carry_user4_target_moved", 1) end',
            'if getvar("mod_test_magic_carry_user4_has_moved_attached") == 1 then assign("mod_test_magic_carry_user4_target_moved", 1) end',
            'if getvar("mod_test_magic_carry_user4_blocker_life") < 99999 and getvar("mod_test_magic_carry_user4_blocker2_life") < 99999 then assign("mod_test_magic_carry_user4_neighbor_hurt", 1) end',
            "end",
            "sleep(500);",
            "cleareffect();",
            'displaymessage("正在测试隐藏四号施法者携带");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_BLOCKER");',
            'delnpc("MOD_TEST_COLLISION_BLOCKER_2");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 18, 0);',
            'assign("mod_test_magic_carry_user4_hide", 1);',
            'assign("mod_test_magic_carry_user4_hide_hidden_seen", 0);',
            'assign("mod_test_magic_carry_user4_hide_exploding_hidden_seen", 0);',
            'assign("mod_test_magic_carry_user4_hide_restored", 0);',
            'usemagic("mod_test_magic_carry_user4_hidden.ini", 34, 16);',
            'for i = 1, 140 do',
            "sleep(20);",
            'geteffectstate("mod_test_magic_carry_user4_hidden.ini", "CarryUserActive", "mod_test_magic_carry_user4_hide_active");',
            'geteffectstate("mod_test_magic_carry_user4_hidden.ini", "ExplodingCount", "mod_test_magic_carry_user4_hide_exploding");',
            'getplayerstate("IsDraw", "mod_test_magic_carry_user4_hide_draw");',
            'if getvar("mod_test_magic_carry_user4_hide_active") == 1 and getvar("mod_test_magic_carry_user4_hide_draw") == 0 then assign("mod_test_magic_carry_user4_hide_hidden_seen", 1) end',
            'if getvar("mod_test_magic_carry_user4_hide_exploding") > 0 and getvar("mod_test_magic_carry_user4_hide_draw") == 0 then assign("mod_test_magic_carry_user4_hide_exploding_hidden_seen", 1) end',
            'if getvar("mod_test_magic_carry_user4_hide_hidden_seen") == 1 and getvar("mod_test_magic_carry_user4_hide_active") == 0 and getvar("mod_test_magic_carry_user4_hide_draw") == 1 then assign("mod_test_magic_carry_user4_hide_restored", 1) end',
            "end",
            "cleareffect();",
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'displaymessage("正在测试隐藏一号施法者携带");',
            'assign("mod_test_magic_carry_user1_hide", 1);',
            'assign("mod_test_magic_carry_user1_hide_hidden_seen", 0);',
            'assign("mod_test_magic_carry_user1_hide_exploding_hidden_seen", 0);',
            'assign("mod_test_magic_carry_user1_hide_player_moved", 0);',
            'assign("mod_test_magic_carry_user1_hide_restored", 0);',
            'usemagic("mod_test_magic_carry_user1_hidden.ini", 34, 16);',
            'for i = 1, 140 do',
            "sleep(20);",
            'geteffectstate("mod_test_magic_carry_user1_hidden.ini", "CarryUserActive", "mod_test_magic_carry_user1_hide_active");',
            'geteffectstate("mod_test_magic_carry_user1_hidden.ini", "ExplodingCount", "mod_test_magic_carry_user1_hide_exploding");',
            'getplayerstate("IsDraw", "mod_test_magic_carry_user1_hide_draw");',
            'getplayerstate("MapY", "mod_test_magic_carry_user1_hide_player_y");',
            'getplayerstate("HasNonZeroOffset", "mod_test_magic_carry_user1_hide_player_offset");',
            'if getvar("mod_test_magic_carry_user1_hide_active") == 1 and getvar("mod_test_magic_carry_user1_hide_draw") == 0 then assign("mod_test_magic_carry_user1_hide_hidden_seen", 1) end',
            'if getvar("mod_test_magic_carry_user1_hide_exploding") > 0 and getvar("mod_test_magic_carry_user1_hide_draw") == 0 then assign("mod_test_magic_carry_user1_hide_exploding_hidden_seen", 1) end',
            'if getvar("mod_test_magic_carry_user1_hide_active") == 1 and getvar("mod_test_magic_carry_user1_hide_player_y") < 20 then assign("mod_test_magic_carry_user1_hide_player_moved", 1) end',
            'if getvar("mod_test_magic_carry_user1_hide_active") == 1 and getvar("mod_test_magic_carry_user1_hide_player_offset") == 1 then assign("mod_test_magic_carry_user1_hide_player_moved", 1) end',
            'if getvar("mod_test_magic_carry_user1_hide_hidden_seen") == 1 and getvar("mod_test_magic_carry_user1_hide_active") == 0 and getvar("mod_test_magic_carry_user1_hide_draw") == 1 then assign("mod_test_magic_carry_user1_hide_restored", 1) end',
            "end",
            'assign("mod_test_magic_carry_user_hidden_contract", 1);',
            'if getvar("mod_test_magic_carry_user4_hide_hidden_seen") == 0 then assign("mod_test_magic_carry_user_hidden_contract", 0) end',
            'if getvar("mod_test_magic_carry_user4_hide_restored") == 0 then assign("mod_test_magic_carry_user_hidden_contract", 0) end',
            'if getvar("mod_test_magic_carry_user4_hide_exploding_hidden_seen") ~= 0 then assign("mod_test_magic_carry_user_hidden_contract", 0) end',
            'if getvar("mod_test_magic_carry_user1_hide_hidden_seen") == 0 then assign("mod_test_magic_carry_user_hidden_contract", 0) end',
            'if getvar("mod_test_magic_carry_user1_hide_exploding_hidden_seen") == 0 then assign("mod_test_magic_carry_user_hidden_contract", 0) end',
            'if getvar("mod_test_magic_carry_user1_hide_player_moved") == 0 then assign("mod_test_magic_carry_user_hidden_contract", 0) end',
            'if getvar("mod_test_magic_carry_user1_hide_restored") == 0 then assign("mod_test_magic_carry_user_hidden_contract", 0) end',
            "cleareffect();",
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'displaymessage("正在测试丢弃对立目标");',
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'addnpc("mod_test_collision_caster_npc.ini", 34, 18, 4);',
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_collision_peer.ini", 34, 19, 1);',
            'geteffectstate("mod_test_magic_collision_peer.ini", "ProjectileCount", "mod_test_magic_discard_peer_probe");',
            'geteffectstate("mod_test_magic_collision_peer.ini", "CanBeDiscardedByOppositeMagicCount", "mod_test_magic_discard_peer_can_discard_before");',
            'assign("mod_test_magic_discard_peer_before", getvar("mod_test_magic_discard_peer_probe"));',
            'assign("mod_test_magic_discard_capable", 0);',
            'if getvar("mod_test_magic_discard_peer_before") > 0 and getvar("mod_test_magic_discard_peer_can_discard_before") > 0 then assign("mod_test_magic_discard_capable", 1) end',
            'usemagic("mod_test_magic_discard.ini", 34, 19);',
            'assign("mod_test_magic_discard_opposite", 1);',
            "sleep(1600);",
            'geteffectstate("mod_test_magic_collision_peer.ini", "ProjectileCount", "mod_test_magic_discard_peer_after");',
            'geteffectstate("mod_test_magic_discard.ini", "ProjectileCount", "mod_test_magic_discard_self_after");',
            'assign("mod_test_magic_discard_cleared", 0);',
            'if getvar("mod_test_magic_discard_peer_before") > 0 and getvar("mod_test_magic_discard_peer_after") == 0 and getvar("mod_test_magic_discard_self_after") == 0 then assign("mod_test_magic_discard_cleared", 1) end',
            "sleep(900);",
            "cleareffect();",
            'displaymessage("正在测试交换施法者");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'addnpc("mod_test_collision_caster_npc.ini", 34, 18, 4);',
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_collision_peer.ini", 34, 19, 1);',
            'geteffectstate("mod_test_magic_collision_peer.ini", "ProjectileCount", "mod_test_magic_exchange_peer_probe");',
            'geteffectstate("mod_test_magic_collision_peer.ini", "ActiveProjectileFlyingDirectionX", "mod_test_magic_exchange_peer_fly_x_before");',
            'geteffectstate("mod_test_magic_collision_peer.ini", "ActiveProjectileFlyingDirectionY", "mod_test_magic_exchange_peer_fly_y_before");',
            'geteffectstate("mod_test_magic_collision_peer.ini", "ActiveProjectileSpeed", "mod_test_magic_exchange_peer_speed_before");',
            'assign("mod_test_magic_exchange_peer_before", getvar("mod_test_magic_exchange_peer_probe"));',
            'usemagic("mod_test_magic_exchange.ini", 34, 19);',
            'assign("mod_test_magic_exchange_user", 1);',
            'assign("mod_test_magic_exchange_transferred", 0);',
            'assign("mod_test_magic_exchange_launcher_transferred", 0);',
            'assign("mod_test_magic_exchange_direction_combined", 0);',
            "for i = 1, 30 do",
            "sleep(20);",
            'geteffectstate("mod_test_magic_collision_peer.ini", "ProjectileCount", "mod_test_magic_exchange_peer_after");',
            'geteffectstate("mod_test_magic_collision_peer.ini", "ActiveProjectileUserIsPlayer", "mod_test_magic_exchange_peer_user_player");',
            'geteffectstate("mod_test_magic_collision_peer.ini", "ActiveProjectileLauncherKind", "mod_test_magic_exchange_peer_launcher");',
            'geteffectstate("mod_test_magic_collision_peer.ini", "ActiveProjectileFlyingDirectionX", "mod_test_magic_exchange_peer_fly_x");',
            'geteffectstate("mod_test_magic_collision_peer.ini", "ActiveProjectileFlyingDirectionY", "mod_test_magic_exchange_peer_fly_y");',
            'geteffectstate("mod_test_magic_collision_peer.ini", "ActiveProjectileSpeed", "mod_test_magic_exchange_peer_speed");',
            'geteffectstate("mod_test_magic_exchange.ini", "ProjectileCount", "mod_test_magic_exchange_self_after");',
            'if getvar("mod_test_magic_exchange_peer_before") > 0 and getvar("mod_test_magic_exchange_peer_after") > 0 and getvar("mod_test_magic_exchange_peer_user_player") == 1 and getvar("mod_test_magic_exchange_self_after") == 0 then assign("mod_test_magic_exchange_transferred", 1) end',
            'if getvar("mod_test_magic_exchange_peer_before") > 0 and getvar("mod_test_magic_exchange_peer_after") > 0 and getvar("mod_test_magic_exchange_peer_launcher") == 2 and getvar("mod_test_magic_exchange_self_after") == 0 then assign("mod_test_magic_exchange_launcher_transferred", 1) end',
            'if getvar("mod_test_magic_exchange_peer_before") > 0 and getvar("mod_test_magic_exchange_peer_after") > 0 and (getvar("mod_test_magic_exchange_peer_fly_x") ~= getvar("mod_test_magic_exchange_peer_fly_x_before") or getvar("mod_test_magic_exchange_peer_fly_y") ~= getvar("mod_test_magic_exchange_peer_fly_y_before") or getvar("mod_test_magic_exchange_peer_speed") ~= getvar("mod_test_magic_exchange_peer_speed_before")) then assign("mod_test_magic_exchange_direction_combined", 1) end',
            "end",
            "sleep(900);",
            "cleareffect();",
            'displaymessage("正在测试武功状态持续时间");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'getmagicstate("mod_test_magic_status_duration_freeze.ini", "SpecialKindMilliSeconds", "mod_test_magic_status_freeze_field", 1);',
            'getmagicstate("mod_test_magic_status_duration_poison.ini", "SpecialKindMilliSeconds", "mod_test_magic_status_poison_field", 1);',
            'getmagicstate("mod_test_magic_status_duration_petrify.ini", "SpecialKindMilliSeconds", "mod_test_magic_status_petrify_field", 1);',
            'assign("mod_test_magic_status_duration_fields", 0);',
            'if getvar("mod_test_magic_status_freeze_field") == 3200 and getvar("mod_test_magic_status_poison_field") == 4100 and getvar("mod_test_magic_status_petrify_field") == 5300 then assign("mod_test_magic_status_duration_fields", 1) end',
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'assign("mod_test_magic_status_freeze_preserved", 0);',
            'assign("mod_test_magic_status_freeze_short_seen", 0);',
            'usemagic("mod_test_magic_status_duration_short_freeze.ini", 34, 16);',
            "for i = 1, 5 do",
            "sleep(80);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "IsFrozened", "mod_test_magic_status_freeze_short_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "FrozenMilliseconds", "mod_test_magic_status_freeze_short_ms");',
            'if getvar("mod_test_magic_status_freeze_short_raw") == 1 and getvar("mod_test_magic_status_freeze_short_ms") > 0 and getvar("mod_test_magic_status_freeze_short_ms") < 2000 then assign("mod_test_magic_status_freeze_short_seen", 1) end',
            "end",
            'usemagic("mod_test_magic_status_duration_freeze.ini", 34, 16);',
            "sleep(160);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "IsFrozened", "mod_test_magic_status_freeze_preserved_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "FrozenMilliseconds", "mod_test_magic_status_freeze_preserved_ms");',
            'if getvar("mod_test_magic_status_freeze_short_seen") == 1 and getvar("mod_test_magic_status_freeze_preserved_raw") == 1 and getvar("mod_test_magic_status_freeze_preserved_ms") > 0 and getvar("mod_test_magic_status_freeze_preserved_ms") < 2000 then assign("mod_test_magic_status_freeze_preserved", 1) end',
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'assign("mod_test_magic_status_duration_freeze", 0);',
            'usemagic("mod_test_magic_status_duration_freeze.ini", 34, 16);',
            "for i = 1, 20 do",
            "sleep(80);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "IsFrozened", "mod_test_magic_status_freeze_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "FrozenMilliseconds", "mod_test_magic_status_freeze_ms");',
            'if getvar("mod_test_magic_status_freeze_raw") == 1 and getvar("mod_test_magic_status_freeze_ms") >= 2400 then assign("mod_test_magic_status_duration_freeze", 1) end',
            "end",
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'assign("mod_test_magic_status_duration_poison", 0);',
            'usemagic("mod_test_magic_status_duration_poison.ini", 34, 16);',
            "for i = 1, 20 do",
            "sleep(80);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "IsPoisoned", "mod_test_magic_status_poison_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "PoisonedMilliseconds", "mod_test_magic_status_poison_ms");',
            'if getvar("mod_test_magic_status_poison_raw") == 1 and getvar("mod_test_magic_status_poison_ms") >= 3200 then assign("mod_test_magic_status_duration_poison", 1) end',
            "end",
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'assign("mod_test_magic_status_duration_petrify", 0);',
            'usemagic("mod_test_magic_status_duration_petrify.ini", 34, 16);',
            "for i = 1, 20 do",
            "sleep(80);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "IsPetrified", "mod_test_magic_status_petrify_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "PetrifiedMilliseconds", "mod_test_magic_status_petrify_ms");',
            'if getvar("mod_test_magic_status_petrify_raw") == 1 and getvar("mod_test_magic_status_petrify_ms") >= 4400 then assign("mod_test_magic_status_duration_petrify", 1) end',
            "end",
            "cleareffect();",
            'displaymessage("正在测试致死冻结状态");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'assign("mod_test_magic_lethal_freeze_status", 0);',
            'usemagic("mod_test_magic_collision_lethal_freeze.ini", 34, 16);',
            "for i = 1, 20 do",
            "sleep(80);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "IsDeath", "mod_test_magic_lethal_freeze_death");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "IsFrozened", "mod_test_magic_lethal_freeze_frozen");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "FrozenMilliseconds", "mod_test_magic_lethal_freeze_ms");',
            'if getvar("mod_test_magic_lethal_freeze_death") == 1 and getvar("mod_test_magic_lethal_freeze_frozen") == 1 and getvar("mod_test_magic_lethal_freeze_ms") > 0 then assign("mod_test_magic_lethal_freeze_status", 1) end',
            "end",
            "cleareffect();",
            'displaymessage("正在测试伙伴全体范围攻击");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            "disablepartnercombat();",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'delnpc("MOD_TEST_COLLISION_PARTNER");',
            'addnpc("mod_test_collision_caster_npc.ini", 34, 18, 0);',
            'addnpc("mod_test_collision_partner_npc.ini", 34, 16, 0);',
            'assign("mod_test_magic_range_attack_all_partner_ready", 0);',
            'assign("mod_test_magic_range_attack_all_partner_status", 0);',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "Exists", "mod_test_magic_range_attack_all_partner_exists");',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "Kind", "mod_test_magic_range_attack_all_partner_kind");',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "IsFighter", "mod_test_magic_range_attack_all_partner_fighter");',
            'if getvar("mod_test_magic_range_attack_all_partner_exists") == 1 and getvar("mod_test_magic_range_attack_all_partner_kind") == 3 and getvar("mod_test_magic_range_attack_all_partner_fighter") == 1 then assign("mod_test_magic_range_attack_all_partner_ready", 1) end',
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_range_attack_all.ini", 34, 18, 1);',
            "for i = 1, 20 do",
            "sleep(80);",
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "IsFrozened", "mod_test_magic_range_attack_all_partner_frozen_raw");',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "FrozenMilliseconds", "mod_test_magic_range_attack_all_partner_frozen_ms_raw");',
            'if getvar("mod_test_magic_range_attack_all_partner_frozen_raw") == 1 and getvar("mod_test_magic_range_attack_all_partner_frozen_ms_raw") > 0 then assign("mod_test_magic_range_attack_all_partner_status", 1) end',
            "end",
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'delnpc("MOD_TEST_COLLISION_PARTNER");',
            'displaymessage("正在测试伙伴弹体限制");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            "fulllife();",
            "disablepartnercombat();",
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'delnpc("MOD_TEST_COLLISION_PARTNER");',
            'addnpc("mod_test_collision_caster_npc.ini", 34, 12, 0);',
            'addnpc("mod_test_collision_partner_npc.ini", 34, 16, 0);',
            'assign("mod_test_magic_partner_projectile_gate", 0);',
            'assign("mod_test_magic_partner_projectile_ready", 0);',
            'assign("mod_test_magic_partner_projectile_disabled_gate", 0);',
            'assign("mod_test_magic_partner_projectile_enabled_hit", 0);',
            'assign("mod_test_magic_partner_projectile_enabled_stopped", 0);',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "Exists", "mod_test_magic_partner_projectile_partner_exists");',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "Kind", "mod_test_magic_partner_projectile_partner_kind");',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "IsFighter", "mod_test_magic_partner_projectile_partner_fighter");',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "MapX", "mod_test_magic_partner_projectile_partner_x");',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "MapY", "mod_test_magic_partner_projectile_partner_y");',
            'if getvar("mod_test_magic_partner_projectile_partner_exists") == 1 and getvar("mod_test_magic_partner_projectile_partner_kind") == 3 and getvar("mod_test_magic_partner_projectile_partner_fighter") == 1 and getvar("mod_test_magic_partner_projectile_partner_x") == 34 and getvar("mod_test_magic_partner_projectile_partner_y") == 16 then assign("mod_test_magic_partner_projectile_ready", 1) end',
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "Life", "mod_test_magic_partner_projectile_disabled_partner_life_before");',
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_partner_projectile.ini", 34, 20, 1);',
            "for i = 1, 30 do",
            "sleep(80);",
            "end",
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "Life", "mod_test_magic_partner_projectile_disabled_partner_life_after");',
            'if getvar("mod_test_magic_partner_projectile_disabled_partner_life_after") == getvar("mod_test_magic_partner_projectile_disabled_partner_life_before") then assign("mod_test_magic_partner_projectile_disabled_gate", 1) end',
            "cleareffect();",
            "fulllife();",
            "enablepartnercombat();",
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "Life", "mod_test_magic_partner_projectile_enabled_partner_life_before");',
            'getplayerstate("Life", "mod_test_magic_partner_projectile_enabled_player_life_before");',
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_partner_projectile.ini", 34, 20, 1);',
            "for i = 1, 30 do",
            "sleep(80);",
            "end",
            'getnpcstate("MOD_TEST_COLLISION_PARTNER", "Life", "mod_test_magic_partner_projectile_enabled_partner_life_after");',
            'getplayerstate("Life", "mod_test_magic_partner_projectile_enabled_player_life_after");',
            'if getvar("mod_test_magic_partner_projectile_enabled_partner_life_after") < getvar("mod_test_magic_partner_projectile_enabled_partner_life_before") then assign("mod_test_magic_partner_projectile_enabled_hit", 1) end',
            'if getvar("mod_test_magic_partner_projectile_enabled_player_life_after") == getvar("mod_test_magic_partner_projectile_enabled_player_life_before") then assign("mod_test_magic_partner_projectile_enabled_stopped", 1) end',
            'if getvar("mod_test_magic_partner_projectile_ready") == 1 and getvar("mod_test_magic_partner_projectile_disabled_gate") == 1 and getvar("mod_test_magic_partner_projectile_enabled_hit") == 1 and getvar("mod_test_magic_partner_projectile_enabled_stopped") == 1 then assign("mod_test_magic_partner_projectile_gate", 1) end',
            "disablepartnercombat();",
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'delnpc("MOD_TEST_COLLISION_PARTNER");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'displaymessage("武功碰撞测试完成");',
            "return;",
            "",
        ]
    )


def make_magic_temp_relation_script() -> str:
    return "\n".join(
        [
            'displaymessage("临时阵营关系测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_magic_temp_relation_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            "fulllife();",
            "fullmana();",
            "fullthew();",
            'addmagic("mod_test_magic_temp_relation.ini");',
            'getmagicstate("mod_test_magic_temp_relation.ini", "MaxLevel", "mod_test_magic_temp_relation_max_level");',
            'assign("mod_test_magic_temp_relation_fields", 0);',
            'if getvar("mod_test_magic_temp_relation_max_level") == 99 then assign("mod_test_magic_temp_relation_fields", 1) end',
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Relation", "mod_test_magic_temp_relation_before_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "RuntimeRelation", "mod_test_magic_temp_relation_before_runtime");',
            'assign("mod_test_magic_temp_relation_before", 0);',
            'if getvar("mod_test_magic_temp_relation_before_raw") == 1 and getvar("mod_test_magic_temp_relation_before_runtime") == 1 then assign("mod_test_magic_temp_relation_before", 1) end',
            'assign("mod_test_magic_temp_relation_flipped", 0);',
            'usemagic("mod_test_magic_temp_relation.ini", 34, 16);',
            'for i = 1, 16 do',
            "sleep(80);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Relation", "mod_test_magic_temp_relation_flipped_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "RuntimeRelation", "mod_test_magic_temp_relation_flipped_runtime");',
            'if getvar("mod_test_magic_temp_relation_flipped_raw") == 0 and getvar("mod_test_magic_temp_relation_flipped_runtime") == 0 then assign("mod_test_magic_temp_relation_flipped", 1) end',
            "end",
            'addnpc("mod_test_collision_caster_npc.ini", 34, 18, 4);',
            'assign("mod_test_magic_temp_relation_cancelled", 0);',
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_temp_relation.ini", 34, 16, 1);',
            'for i = 1, 16 do',
            "sleep(80);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Relation", "mod_test_magic_temp_relation_cancel_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "RuntimeRelation", "mod_test_magic_temp_relation_cancel_runtime");',
            'if getvar("mod_test_magic_temp_relation_cancel_raw") == 1 and getvar("mod_test_magic_temp_relation_cancel_runtime") == 1 then assign("mod_test_magic_temp_relation_cancelled", 1) end',
            "end",
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'assign("mod_test_magic_temp_relation_second_active", 0);',
            'usemagic("mod_test_magic_temp_relation.ini", 34, 16);',
            'for i = 1, 16 do',
            "sleep(80);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Relation", "mod_test_magic_temp_relation_second_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "RuntimeRelation", "mod_test_magic_temp_relation_second_runtime");',
            'if getvar("mod_test_magic_temp_relation_second_raw") == 0 and getvar("mod_test_magic_temp_relation_second_runtime") == 0 then assign("mod_test_magic_temp_relation_second_active", 1) end',
            "end",
            "sleep(2200);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Relation", "mod_test_magic_temp_relation_restore_raw");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "RuntimeRelation", "mod_test_magic_temp_relation_restore_runtime");',
            'assign("mod_test_magic_temp_relation_restored", 0);',
            'if getvar("mod_test_magic_temp_relation_restore_raw") == 1 and getvar("mod_test_magic_temp_relation_restore_runtime") == 1 then assign("mod_test_magic_temp_relation_restored", 1) end',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'addnpc("mod_test_collision_caster_npc.ini", 34, 18, 4);',
            'assign("mod_test_magic_temp_relation_player_immune", 0);',
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_temp_relation.ini", 34, 20, 1);',
            'for i = 1, 16 do',
            "sleep(80);",
            'getplayerstate("Relation", "mod_test_magic_temp_relation_player_raw");',
            'getplayerstate("RuntimeRelation", "mod_test_magic_temp_relation_player_runtime");',
            'if getvar("mod_test_magic_temp_relation_player_raw") == 0 and getvar("mod_test_magic_temp_relation_player_runtime") == 0 then assign("mod_test_magic_temp_relation_player_immune", 1) end',
            "end",
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'displaymessage("临时阵营关系测试完成");',
            "return;",
            "",
        ]
    )


def make_object_state_script() -> str:
    return "\n".join(
        [
            'displaymessage("对象状态测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_object_state_ready", 1);',
            'assign("mod_test_object_interacted", 0);',
            'assign("mod_test_object_interacted_right", 0);',
            'assign("mod_test_object_right_only_primary_ran", 0);',
            'assign("mod_test_object_right_only_explicit_left_blocked", 0);',
            'assign("mod_test_object_interact_right_missing_blocked", 0);',
            'assign("mod_test_object_interact_action_queued", 0);',
            'assign("mod_test_npc_interact_action_queued", 0);',
            'assign("mod_test_npc_interact_right_missing_blocked", 0);',
            'assign("mod_test_npc_interacted_right", 0);',
            'assign("mod_test_object_touched", 0);',
            'assign("mod_test_interaction_chain", 0);',
            'delobj("MOD_TEST_OBJECT_STATE");',
            'addobj("mod_test_object_state_box.ini", 30, 20, 0);',
            'getobjstate("MOD_TEST_OBJECT_STATE", "Exists", "mod_test_object_exists_before");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "FileName", "mod_test_object_file_name");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "Kind", "mod_test_object_kind_box");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "Type", "mod_test_object_type_metadata");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "HasObjFileMovie", "mod_test_object_has_movie_metadata");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "HasObjResourceImage", "mod_test_object_has_resource_image");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "HasObjResourceShade", "mod_test_object_has_resource_shade");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "HasObjResourceSound", "mod_test_object_has_resource_sound");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "ObjResourceImageLoaded", "mod_test_object_resource_image_loaded");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "ObjResourceShadeLoaded", "mod_test_object_resource_shade_loaded");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "MapX", "mod_test_object_map_x");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "MapY", "mod_test_object_map_y");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "RegionInWorld", "mod_test_object_region_exists");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "ReginInWorldBeginPosition", "mod_test_object_region_begin_exists");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "RegionInWorldX", "mod_test_object_region_x");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "RegionInWorldY", "mod_test_object_region_y");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "OffX", "mod_test_object_offset_x_before");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "OffY", "mod_test_object_offset_y_before");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "HasInteractScript", "mod_test_object_has_script");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "HasInteractScriptRight", "mod_test_object_has_script_right");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "HasAnyInteractScript", "mod_test_object_has_any_script");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "IsInteractive", "mod_test_object_is_interactive");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "HasPrimaryInteractScript", "mod_test_object_has_primary_interact");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "CanSelectForInteraction", "mod_test_object_can_select");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "CanInteractDirectly", "mod_test_object_can_interact_directly");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "ScriptFileJustTouch", "mod_test_object_just_touch");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "IsBox", "mod_test_object_is_box");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "IsDoor", "mod_test_object_is_door");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "Selecting", "mod_test_object_selecting_initial");',
            'setobjofs("MOD_TEST_OBJECT_STATE", 6, -4);',
            'getobjstate("MOD_TEST_OBJECT_STATE", "OffX", "mod_test_object_offset_x_after");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "OffY", "mod_test_object_offset_y_after");',
            'openbox("MOD_TEST_OBJECT_STATE");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "NowAction", "mod_test_object_open_action");',
            'closebox("MOD_TEST_OBJECT_STATE");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "NowAction", "mod_test_object_close_action");',
            'openobj("MOD_TEST_OBJECT_STATE");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "NowAction", "mod_test_object_named_open_obj_action");',
            'setobjkind("MOD_TEST_OBJECT_STATE", 6);',
            'getobjstate("MOD_TEST_OBJECT_STATE", "IsTrap", "mod_test_object_is_trap");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "IsAutoPlay", "mod_test_object_trap_auto_play");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "IsObstacle", "mod_test_object_trap_obstacle");',
            'setobjkind("MOD_TEST_OBJECT_STATE", 2);',
            'getobjstate("MOD_TEST_OBJECT_STATE", "IsBody", "mod_test_object_is_body");',
            'setobjkind("MOD_TEST_OBJECT_STATE", 0);',
            'getobjstate("MOD_TEST_OBJECT_STATE", "IsObstacle", "mod_test_object_static_obstacle");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "IsAutoPlay", "mod_test_object_static_auto_play");',
            'delobj("MOD_TEST_OBJECT_STATE");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "Exists", "mod_test_object_exists_after");',
            'getobjstate("MOD_TEST_OBJECT_STATE", "IsRemoved", "mod_test_object_is_removed");',
            'delobj("MOD_TEST_OBJECT_PICKUP");',
            'addobj("mod_test_object_pickup.ini", 31, 20, 0);',
            'getobjstate("MOD_TEST_OBJECT_PICKUP", "Kind", "mod_test_object_pickup_kind");',
            'getobjstate("MOD_TEST_OBJECT_PICKUP", "IsDrop", "mod_test_object_is_drop");',
            'getobjstate("MOD_TEST_OBJECT_PICKUP", "IsAutoPlay", "mod_test_object_drop_auto_play");',
            'getobjstate("MOD_TEST_OBJECT_PICKUP", "IsObstacle", "mod_test_object_drop_obstacle");',
            'delobj("MOD_TEST_OBJECT_PICKUP");',
            'delobj("MOD_TEST_OBJECT_LEGACY_PICKUP");',
            'addobj("mod_test_object_legacy_pickup.ini", 32, 20, 0);',
            'getobjstate("MOD_TEST_OBJECT_LEGACY_PICKUP", "Exists", "mod_test_object_legacy_pickup_exists");',
            'getobjstate("MOD_TEST_OBJECT_LEGACY_PICKUP", "Kind", "mod_test_object_legacy_pickup_kind");',
            'getobjstate("MOD_TEST_OBJECT_LEGACY_PICKUP", "IsDrop", "mod_test_object_legacy_pickup_is_drop");',
            'getobjstate("MOD_TEST_OBJECT_LEGACY_PICKUP", "IsAutoPlay", "mod_test_object_legacy_pickup_auto_play");',
            'getobjstate("MOD_TEST_OBJECT_LEGACY_PICKUP", "IsObstacle", "mod_test_object_legacy_pickup_obstacle");',
            'delobj("MOD_TEST_OBJECT_LEGACY_PICKUP");',
            'delobj("MOD_TEST_OBJECT_SAVE");',
            'addobj("mod_test_object_save_box.ini", 35, 20, 0);',
            'setobjofs("MOD_TEST_OBJECT_SAVE", 9, -3);',
            'setobjkind("MOD_TEST_OBJECT_SAVE", 7);',
            'saveobj("mod_test_object_runtime_save.obj");',
            'delobj("MOD_TEST_OBJECT_SAVE");',
            'loadobj("mod_test_object_runtime_save.obj");',
            'getobjstate("MOD_TEST_OBJECT_SAVE", "Exists", "mod_test_object_save_exists_after_load");',
            'getobjstate("MOD_TEST_OBJECT_SAVE", "Kind", "mod_test_object_save_kind_after_load");',
            'getobjstate("MOD_TEST_OBJECT_SAVE", "OffX", "mod_test_object_save_offset_x_after_load");',
            'getobjstate("MOD_TEST_OBJECT_SAVE", "OffY", "mod_test_object_save_offset_y_after_load");',
            'getobjstate("MOD_TEST_OBJECT_SAVE", "IsDrop", "mod_test_object_save_is_drop_after_load");',
            'getobjstate("MOD_TEST_OBJECT_SAVE", "HasInteractScriptRight", "mod_test_object_save_right_after_load");',
            'getobjstate("MOD_TEST_OBJECT_SAVE", "Type", "mod_test_object_save_type_after_load");',
            'getobjstate("MOD_TEST_OBJECT_SAVE", "HasObjFileMovie", "mod_test_object_save_has_movie_after_load");',
            'delobj("MOD_TEST_OBJECT_SAVE");',
            'delobj("MOD_TEST_OBJECT_ALIAS");',
            'addobj("mod_test_object_alias_box.ini", 39, 20, 0);',
            'setobjoffset("MOD_TEST_OBJECT_ALIAS", 11, -7);',
            'getobjstate("MOD_TEST_OBJECT_ALIAS", "OffX", "mod_test_object_setobjoffset_alias_x");',
            'getobjstate("MOD_TEST_OBJECT_ALIAS", "OffY", "mod_test_object_setobjoffset_alias_y");',
            'saveobj("mod_test_object_alias_runtime_save.obj");',
            'deleteobj("MOD_TEST_OBJECT_ALIAS");',
            'getobjstate("MOD_TEST_OBJECT_ALIAS", "Exists", "mod_test_object_deleteobj_alias_exists_after");',
            'lodaobj("mod_test_object_alias_runtime_save.obj");',
            'getobjstate("MOD_TEST_OBJECT_ALIAS", "Exists", "mod_test_object_lodaobj_alias_exists_after");',
            'getobjstate("MOD_TEST_OBJECT_ALIAS", "OffX", "mod_test_object_lodaobj_alias_offset_x");',
            'getobjstate("MOD_TEST_OBJECT_ALIAS", "OffY", "mod_test_object_lodaobj_alias_offset_y");',
            'assign("mod_test_object_deletecurrent_alias_script", 0);',
            'assign("mod_test_object_deletecurrent_alias_exists_after", 1);',
            'runobjscript("MOD_TEST_OBJECT_ALIAS");',
            'delobj("MOD_TEST_OBJECT_RIGHT_ONLY");',
            'addobj("mod_test_object_right_only_box.ini", 36, 20, 0);',
            'getobjstate("MOD_TEST_OBJECT_RIGHT_ONLY", "HasInteractScript", "mod_test_object_right_only_has_left");',
            'getobjstate("MOD_TEST_OBJECT_RIGHT_ONLY", "HasInteractScriptRight", "mod_test_object_right_only_has_right");',
            'getobjstate("MOD_TEST_OBJECT_RIGHT_ONLY", "HasAnyInteractScript", "mod_test_object_right_only_has_any");',
            'getobjstate("MOD_TEST_OBJECT_RIGHT_ONLY", "CanSelectForInteraction", "mod_test_object_right_only_can_select");',
            'getobjstate("MOD_TEST_OBJECT_RIGHT_ONLY", "FallbackRightScript", "mod_test_object_right_only_fallback");',
            'getobjstate("MOD_TEST_OBJECT_RIGHT_ONLY", "HasPrimaryInteractScript", "mod_test_object_right_only_primary_available");',
            'delobj("MOD_TEST_OBJECT_RIGHT_ONLY");',
            'delobj("MOD_TEST_OBJECT_REMOVE");',
            'addobj("mod_test_object_remove_box.ini", 31, 20, 0);',
            'getobjstate("MOD_TEST_OBJECT_REMOVE", "Exists", "mod_test_object_remove_exists_before");',
            'getobjstate("MOD_TEST_OBJECT_REMOVE", "MillisecondsToRemove", "mod_test_object_remove_ms_before");',
            "sleep(260);",
            'getobjstate("MOD_TEST_OBJECT_REMOVE", "Exists", "mod_test_object_remove_exists_after");',
            'getobjstate("MOD_TEST_OBJECT_REMOVE", "IsRemoved", "mod_test_object_remove_is_removed");',
            'delobj("MOD_TEST_OBJECT_TOUCH");',
            'addobj("mod_test_object_touch_box.ini", 32, 20, 0);',
            'getobjstate("MOD_TEST_OBJECT_TOUCH", "HasInteractScript", "mod_test_object_touch_has_script");',
            'getobjstate("MOD_TEST_OBJECT_TOUCH", "ScriptFileJustTouch", "mod_test_object_touch_just_touch");',
            'getobjstate("MOD_TEST_OBJECT_TOUCH", "CanSelectForInteraction", "mod_test_object_touch_can_select");',
            'delobj("MOD_TEST_OBJECT_TOUCH");',
            'delobj("MOD_TEST_OBJECT_TRAP");',
            'addobj("mod_test_object_trap.ini", 33, 20, 0);',
            'getobjstate("MOD_TEST_OBJECT_TRAP", "IsTrap", "mod_test_object_trap_fixture_is_trap");',
            'getobjstate("MOD_TEST_OBJECT_TRAP", "Damage", "mod_test_object_trap_damage");',
            'getobjstate("MOD_TEST_OBJECT_TRAP", "TrapDamageInterval", "mod_test_object_trap_interval");',
            'getobjstate("MOD_TEST_OBJECT_TRAP", "LastTrapDamageCycle", "mod_test_object_trap_last_cycle");',
            'getobjstate("MOD_TEST_OBJECT_TRAP", "TrapDamageCycle", "mod_test_object_trap_cycle");',
            'assign("mod_test_object_trap_interval_positive", 0);',
            'if getvar("mod_test_object_trap_interval") > 0 then assign("mod_test_object_trap_interval_positive", 1) end',
            'delobj("MOD_TEST_OBJECT_TRAP");',
            'assign("mod_test_object_timer_ran", 0);',
            'assign("mod_test_object_timer_self_deleted", 0);',
            'delobj("MOD_TEST_OBJECT_TIMER");',
            'addobj("mod_test_object_timer_box.ini", 34, 20, 0);',
            'getobjstate("MOD_TEST_OBJECT_TIMER", "Exists", "mod_test_object_timer_exists_before");',
            "sleep(260);",
            'delobj("MOD_TEST_OBJECT_STATE");',
            'addobj("mod_test_object_state_box.ini", 37, 20, 0);',
            'runobjscript("MOD_TEST_OBJECT_STATE", 0);',
            'runobjrightscript("MOD_TEST_OBJECT_STATE");',
            'delobj("MOD_TEST_OBJECT_RIGHT_ONLY");',
            'addobj("mod_test_object_right_only_box.ini", 38, 20, 0);',
            'assign("mod_test_object_interacted_right", 0);',
            'assign("mod_test_object_right_only_primary_ran", 0);',
            'runobjscript("MOD_TEST_OBJECT_RIGHT_ONLY", 0);',
            'if getvar("mod_test_object_interacted_right") == 0 and getvar("mod_test_object_right_only_primary_ran") == 0 then assign("mod_test_object_right_only_explicit_left_blocked", 1) end',
            'runobjscript("MOD_TEST_OBJECT_RIGHT_ONLY");',
            "cleargoods();",
            "cleareffect();",
            "clearmagic();",
            'addmagic("mod_test_magic_shop_death_kill.ini");',
            'setmagiclevel("mod_test_magic_shop_death_kill.ini", 1);',
            'delnpc("MOD_TEST_SHOP_DEATH_NPC");',
            'addnpc("mod_test_npc_shop_death.ini", 36, 17, 0);',
            'getnpcstate("MOD_TEST_SHOP_DEATH_NPC", "HasBuyIniFile", "mod_test_npc_shop_death_has_buy_file");',
            'getnpcstate("MOD_TEST_SHOP_DEATH_NPC", "HasBuyIniString", "mod_test_npc_shop_death_has_buy_string");',
            "setplayerpos(36, 20);",
            "setplayerdir(4);",
            'usemagic("mod_test_magic_shop_death_kill.ini", 36, 17);',
            "sleep(1800);",
            'getnpcstate("MOD_TEST_SHOP_DEATH_NPC", "IsDeath", "mod_test_npc_shop_death_dead");',
            'getgoodsnum("mod_test_goods_pricing_drug.ini");',
            'assign("mod_test_npc_shop_death_drop_count", getvar("GoodsNum"));',
            'assign("mod_test_npc_shop_death_drop", 0);',
            'if getvar("mod_test_npc_shop_death_drop_count") == 3 then assign("mod_test_npc_shop_death_drop", 1) end',
            'delnpc("MOD_TEST_SHOP_DEATH_NPC");',
            "cleareffect();",
            'delnpc("MOD_TEST_INTERACT_NPC");',
            'addnpc("mod_test_npc_interact.ini", 36, 21, 0);',
            'getnpcstate("MOD_TEST_INTERACT_NPC", "HasInteractScriptRight", "mod_test_npc_interact_has_right");',
            'getnpcstate("MOD_TEST_INTERACT_NPC", "HasBuyIniFile", "mod_test_npc_interact_has_buy_file");',
            'getnpcstate("MOD_TEST_INTERACT_NPC", "HasBuyIniString", "mod_test_npc_interact_has_buy_string");',
            'setplayerpos(36, 20);',
            'delobj("MOD_TEST_OBJECT_LEFT_ONLY");',
            'addobj("mod_test_object_left_only_box.ini", 36, 20, 0);',
            'if interactnearestobj(1, 0, 0) == 0 then assign("mod_test_object_interact_right_missing_blocked", 1) end',
            'delobj("MOD_TEST_OBJECT_LEFT_ONLY");',
            'delnpc("MOD_TEST_INTERACT_NPC");',
            'delnpc("MOD_TEST_SHOP_DEATH_NPC");',
            'addnpc("mod_test_npc_shop_death.ini", 36, 20, 0);',
            'if interactnearestnpc(1, 0, 0) == 0 then assign("mod_test_npc_interact_right_missing_blocked", 1) end',
            'delnpc("MOD_TEST_SHOP_DEATH_NPC");',
            'addnpc("mod_test_npc_interact.ini", 36, 21, 0);',
            'delobj("MOD_TEST_OBJECT_TOUCH");',
            'addobj("mod_test_object_touch_box.ini", 36, 20, 0);',
            'assign("mod_test_object_interacted_right", 0);',
            'assign("mod_test_interaction_chain", 1);',
            "setwalkisrun(1);",
            "disablerun();",
            'assign("mod_test_object_interact_action_queued", interactnearestobj(1, 0, 2));',
            'displaymessage("对象状态测试完成");',
            "return;",
            "",
        ]
    )


def make_object_timer_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_object_timer_ran", 1);',
            'getobjstate("", "Exists", "mod_test_object_timer_self_exists_before");',
            'getobjstate("", "NowAction", "mod_test_object_timer_self_action_before");',
            "setobjofs(13, -9);",
            'getobjstate("", "OffX", "mod_test_object_timer_self_offset_x");',
            'getobjstate("", "OffY", "mod_test_object_timer_self_offset_y");',
            "openbox();",
            'getobjstate("", "NowAction", "mod_test_object_timer_open_action");',
            "closebox();",
            'getobjstate("", "NowAction", "mod_test_object_timer_close_action");',
            "openobj();",
            'getobjstate("", "NowAction", "mod_test_object_timer_open_obj_action");',
            'delobj("");',
            'getobjstate("", "Exists", "mod_test_object_timer_self_exists_after");',
            'getobjstate("", "IsRemoved", "mod_test_object_timer_self_removed");',
            'assign("mod_test_object_timer_self_deleted", 1);',
            "return;",
            "",
        ]
    )


def make_object_interact_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_object_interacted", 1);',
            "return;",
            "",
        ]
    )


def make_object_interact_right_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_object_interacted_right", 1);',
            "setwalkisrun(0);",
            "enablerun();",
            'getobjstate("", "FallbackRightScript", "mod_test_object_interact_right_fallback_raw");',
            'if getvar("mod_test_object_interact_right_fallback_raw") == 1 then assign("mod_test_object_right_only_primary_ran", 1) end',
            'if getvar("mod_test_interaction_chain") == 1 then assign("mod_test_npc_interact_action_queued", interactnearestnpc(1)) end',
            "return;",
            "",
        ]
    )


def make_object_alias_delete_current_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_object_deletecurrent_alias_script", 1);',
            "deletecurrentobj();",
            'getobjstate("MOD_TEST_OBJECT_ALIAS", "Exists", "mod_test_object_deletecurrent_alias_exists_after");',
            "return;",
            "",
        ]
    )


def make_npc_interact_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "Name=MOD_TEST_INTERACT_NPC",
            "NpcIni=npcres001_独孤剑.ini",
            "FlyIni=",
            "BodyIni=",
            "Kind=1",
            "Relation=2",
            "Group=230",
            "Life=100",
            "LifeMax=100",
            "Thew=100",
            "ThewMax=100",
            "Mana=100",
            "ManaMax=100",
            "Attack=0",
            "Defence=0",
            "Evade=0",
            "Exp=0",
            "WalkSpeed=1",
            "Dir=0",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            "NoDropWhenDie=1",
            "DeathScript=",
            "ScriptFile=mod_test_npc_interact_left.txt",
            "ScriptFileRight=mod_test_npc_interact_right.txt",
            "BuyIniFile=mod_test_shop_head_numbervalid.ini",
            "TalkContent=MOD_TEST interact NPC fixture",
            "",
        ]
    )


def make_npc_shop_death_ini() -> str:
    return "\n".join(
        [
            "[INIT]",
            "Name=MOD_TEST_SHOP_DEATH_NPC",
            "NpcIni=npcres001_独孤剑.ini",
            "FlyIni=",
            "BodyIni=",
            "Kind=1",
            "Relation=1",
            "Group=231",
            "Life=10",
            "LifeMax=10",
            "Thew=100",
            "ThewMax=100",
            "Mana=0",
            "ManaMax=0",
            "Attack=0",
            "Defence=0",
            "Evade=0",
            "Exp=0",
            "WalkSpeed=1",
            "Dir=0",
            "Lum=1",
            "PathFinder=0",
            "Action=0",
            "NoAutoAttackPlayer=1",
            "StopFindingTarget=1",
            "NoDropWhenDie=1",
            "DeathScript=",
            "ScriptFile=",
            "ScriptFileRight=",
            "BuyIniFile=mod_test_shop_head_numbervalid.ini",
            "TalkContent=MOD_TEST shop death fixture",
            "",
        ]
    )


def make_npc_interact_left_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_npc_interacted_left", 1);',
            "return;",
            "",
        ]
    )


def make_npc_interact_right_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_npc_interacted_right", 1);',
            "return;",
            "",
        ]
    )


def make_shop_head_numbervalid_ini() -> str:
    return "\n".join(
        [
            "[Head]",
            "Count=2",
            "NumberValid=1",
            "BuyPercent=125",
            "RecyclePercent=75",
            "",
            "[1]",
            "IniFile=mod_test_goods_pricing_drug.ini",
            "Number=3",
            "",
            "[2]",
            "IniFile=mod_test_goods_pricing_equipment.ini",
            "Number=0",
            "",
        ]
    )


def make_object_touch_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_object_touched", 1);',
            'delobj("");',
            "return;",
            "",
        ]
    )


def make_object_trap_damage_script() -> str:
    return "\n".join(
        [
            'displaymessage("对象陷阱伤害测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_object_trap_damage_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadnpc("map001.npc");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            "addlifemax(1000);",
            "fulllife();",
            "setplayerpos(35,20);",
            'getplayerstate("Life", "mod_test_object_trap_player_life_before");',
            'delnpc("MOD_TEST_OBJECT_TRAP_TARGET");',
            'addnpc("mod_test_object_trap_target_npc.ini", 38, 20, 0);',
            'getnpcstate("MOD_TEST_OBJECT_TRAP_TARGET", "Life", "mod_test_object_trap_npc_life_before");',
            'delobj("MOD_TEST_OBJECT_DAMAGE_PLAYER_TRAP");',
            'delobj("MOD_TEST_OBJECT_DAMAGE_NPC_TRAP");',
            'delobj("MOD_TEST_OBJECT_DAMAGE_RECORDER");',
            'addobj("mod_test_object_damage_player_trap.ini", 35, 20, 0);',
            'addobj("mod_test_object_damage_npc_trap.ini", 38, 20, 0);',
            'addobj("mod_test_object_trap_damage_recorder.ini", 36, 20, 0);',
            'getplayerstate("MapX", "mod_test_object_trap_player_mapx_before");',
            'getplayerstate("MapY", "mod_test_object_trap_player_mapy_before");',
            'getplayerstate("Defend", "mod_test_object_trap_player_defend");',
            'getobjstate("MOD_TEST_OBJECT_DAMAGE_PLAYER_TRAP", "MapX", "mod_test_object_trap_player_trap_mapx");',
            'getobjstate("MOD_TEST_OBJECT_DAMAGE_PLAYER_TRAP", "MapY", "mod_test_object_trap_player_trap_mapy");',
            'getobjstate("MOD_TEST_OBJECT_DAMAGE_PLAYER_TRAP", "Damage", "mod_test_object_trap_player_trap_damage");',
            'assign("mod_test_object_trap_player_setup_position_match", 0);',
            'if getvar("mod_test_object_trap_player_mapx_before") == getvar("mod_test_object_trap_player_trap_mapx") then if getvar("mod_test_object_trap_player_mapy_before") == getvar("mod_test_object_trap_player_trap_mapy") then assign("mod_test_object_trap_player_setup_position_match", 1) end end',
            'assign("mod_test_object_trap_player_damage_over_defend", 0);',
            'if getvar("mod_test_object_trap_player_trap_damage") > getvar("mod_test_object_trap_player_defend") then assign("mod_test_object_trap_player_damage_over_defend", 1) end',
            'assign("mod_test_object_trap_damage_recorded", 0);',
            "return;",
            "",
        ]
    )


def make_object_trap_damage_record_script() -> str:
    return "\n".join(
        [
            'getplayerstate("Life", "mod_test_object_trap_player_life_after");',
            'getplayerstate("MapX", "mod_test_object_trap_player_mapx_after");',
            'getplayerstate("MapY", "mod_test_object_trap_player_mapy_after");',
            'getnpcstate("MOD_TEST_OBJECT_TRAP_TARGET", "Life", "mod_test_object_trap_npc_life_after");',
            'getobjstate("MOD_TEST_OBJECT_DAMAGE_PLAYER_TRAP", "LastTrapDamageCycle", "mod_test_object_trap_player_last_cycle");',
            'getobjstate("MOD_TEST_OBJECT_DAMAGE_NPC_TRAP", "LastTrapDamageCycle", "mod_test_object_trap_npc_last_cycle");',
            'assign("mod_test_object_trap_player_record_position_match", 0);',
            'if getvar("mod_test_object_trap_player_mapx_after") == getvar("mod_test_object_trap_player_trap_mapx") then if getvar("mod_test_object_trap_player_mapy_after") == getvar("mod_test_object_trap_player_trap_mapy") then assign("mod_test_object_trap_player_record_position_match", 1) end end',
            'assign("mod_test_object_trap_player_hit", 0);',
            'if getvar("mod_test_object_trap_player_life_after") < getvar("mod_test_object_trap_player_life_before") then assign("mod_test_object_trap_player_hit", 1) end',
            'assign("mod_test_object_trap_npc_hit", 0);',
            'if getvar("mod_test_object_trap_npc_life_after") < getvar("mod_test_object_trap_npc_life_before") then assign("mod_test_object_trap_npc_hit", 1) end',
            'assign("mod_test_object_trap_player_cycle_recorded", 0);',
            'if getvar("mod_test_object_trap_player_last_cycle") >= 0 then assign("mod_test_object_trap_player_cycle_recorded", 1) end',
            'assign("mod_test_object_trap_npc_cycle_recorded", 0);',
            'if getvar("mod_test_object_trap_npc_last_cycle") >= 0 then assign("mod_test_object_trap_npc_cycle_recorded", 1) end',
            'assign("mod_test_object_trap_damage_recorded", 1);',
            'delobj("MOD_TEST_OBJECT_DAMAGE_PLAYER_TRAP");',
            'delobj("MOD_TEST_OBJECT_DAMAGE_NPC_TRAP");',
            'delobj("MOD_TEST_OBJECT_DAMAGE_RECORDER");',
            'delnpc("MOD_TEST_OBJECT_TRAP_TARGET");',
            "return;",
            "",
        ]
    )


def make_magic_summon_body_script() -> str:
    return "\n".join(
        [
            'displaymessage("召唤与尸体武功测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_magic_summon_body_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            "setplayerpos(34, 20);",
            "setplayerdir(6);",
            "fullmana();",
            "fullthew();",
            'addmagic("mod_test_magic_body_medium.ini");',
            'setmagiclevel("mod_test_magic_body_medium.ini", 1);',
            'addmagic("mod_test_magic_revive_body.ini");',
            'setmagiclevel("mod_test_magic_revive_body.ini", 1);',
            'getmagicstate("mod_test_magic_body_medium.ini", "BodyRadius", "mod_test_magic_body_radius");',
            'getmagicstate("mod_test_magic_body_medium.ini", "VibratingScreen", "mod_test_magic_body_vibrating");',
            'getmagicstate("mod_test_magic_revive_body.ini", "ReviveBodyRadius", "mod_test_magic_revive_radius");',
            'getmagicstate("mod_test_magic_revive_body.ini", "ReviveBodyMaxCount", "mod_test_magic_revive_max_count");',
            'getmagicstate("mod_test_magic_revive_body.ini", "ReviveBodyLifeMilliseconds", "mod_test_magic_revive_life_ms");',
            'delnpc("MOD_TEST_MAGIC_BODY_TARGET");',
            'delnpc("MOD_TEST_MAGIC_REVIVE_BODY_NPC");',
            'delnpc("MOD_TEST_MAGIC_REVIVE_BODY_OVERFLOW_NPC");',
            'delobj("MOD_TEST_MAGIC_BODY_MEDIUM_BODY");',
            'delobj("MOD_TEST_MAGIC_REVIVE_BODY");',
            'delobj("MOD_TEST_MAGIC_REVIVE_BODY_OVERFLOW");',
            'addnpc("mod_test_magic_body_target_npc.ini", 38, 20, 0);',
            'addobj("mod_test_magic_body_medium_body.ini", 37, 20, 0);',
            'getobjstate("MOD_TEST_MAGIC_BODY_MEDIUM_BODY", "Exists", "mod_test_magic_body_medium_body_before");',
            'usemagic("mod_test_magic_body_medium.ini", 38, 20);',
            "sleep(200);",
            'getobjstate("MOD_TEST_MAGIC_BODY_MEDIUM_BODY", "Exists", "mod_test_magic_body_medium_body_after");',
            'geteffectstate("mod_test_magic_body_medium.ini", "Count", "mod_test_magic_body_medium_effect_count");',
            'assign("mod_test_magic_body_medium_effect_seen", 0);',
            'if getvar("mod_test_magic_body_medium_effect_count") > 0 then assign("mod_test_magic_body_medium_effect_seen", 1) end',
            "cleareffect();",
            "sleep(120);",
            "setplayerpos(34, 20);",
            'addobj("mod_test_magic_revive_body_object.ini", 36, 22, 5);',
            'addobj("mod_test_magic_revive_body_overflow_object.ini", 37, 22, 2);',
            'getobjstate("MOD_TEST_MAGIC_REVIVE_BODY", "Exists", "mod_test_magic_revive_body_before");',
            'getobjstate("MOD_TEST_MAGIC_REVIVE_BODY_OVERFLOW", "Exists", "mod_test_magic_revive_overflow_body_before");',
            'usemagic("mod_test_magic_revive_body.ini", 36, 22);',
            "sleep(200);",
            'getobjstate("MOD_TEST_MAGIC_REVIVE_BODY", "Exists", "mod_test_magic_revive_body_after");',
            'getobjstate("MOD_TEST_MAGIC_REVIVE_BODY_OVERFLOW", "Exists", "mod_test_magic_revive_overflow_body_after");',
            'getnpcstate("MOD_TEST_MAGIC_REVIVE_BODY_NPC", "Exists", "mod_test_magic_revive_npc_exists");',
            'getnpcstate("MOD_TEST_MAGIC_REVIVE_BODY_OVERFLOW_NPC", "Exists", "mod_test_magic_revive_overflow_npc_exists");',
            'getnpcstate("MOD_TEST_MAGIC_REVIVE_BODY_NPC", "Relation", "mod_test_magic_revive_npc_relation");',
            'getnpcstate("MOD_TEST_MAGIC_REVIVE_BODY_NPC", "MapX", "mod_test_magic_revive_npc_x");',
            'getnpcstate("MOD_TEST_MAGIC_REVIVE_BODY_NPC", "MapY", "mod_test_magic_revive_npc_y");',
            'getnpcstate("MOD_TEST_MAGIC_REVIVE_BODY_NPC", "Dir", "mod_test_magic_revive_npc_dir");',
            'getnpcstate("MOD_TEST_MAGIC_REVIVE_BODY_NPC", "LifeMilliseconds", "mod_test_magic_revive_npc_life_ms");',
            'assign("mod_test_magic_revive_npc_life_ms_positive", 0);',
            'if getvar("mod_test_magic_revive_npc_life_ms") > 0 then assign("mod_test_magic_revive_npc_life_ms_positive", 1) end',
            'displaymessage("召唤与尸体武功测试完成");',
            "return;",
            "",
        ]
    )


def make_magic_change_hit_script() -> str:
    return "\n".join(
        [
            'displaymessage("命中变化武功测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_magic_change_hit_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            "setplayerpos(34, 20);",
            "setplayerdir(6);",
            "fullmana();",
            "fullthew();",
            "addevade(200);",
            "clearmagic();",
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'addnpc("mod_test_collision_target_npc.ini", 34, 16, 0);',
            'addmagic("mod_test_magic_change_hit_base.ini");',
            'setmagiclevel("mod_test_magic_change_hit_base.ini", 1);',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_change_hit_life_before");',
            'usemagic("mod_test_magic_change_hit_base.ini", 34, 16);',
            "sleep(500);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_change_hit_life_after1");',
            'assign("mod_test_magic_change_hit_first_hit", 0);',
            'if getvar("mod_test_magic_change_hit_life_after1") <= getvar("mod_test_magic_change_hit_life_before") - 10 then assign("mod_test_magic_change_hit_first_hit", 1) end',
            "cleareffect();",
            'usemagic("mod_test_magic_change_hit_base.ini", 34, 16);',
            "sleep(500);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_change_hit_life_after2");',
            'assign("mod_test_magic_change_hit_second_hit", 0);',
            'if getvar("mod_test_magic_change_hit_life_after2") <= getvar("mod_test_magic_change_hit_life_after1") - 10 then assign("mod_test_magic_change_hit_second_hit", 1) end',
            "cleareffect();",
            'usemagic("mod_test_magic_change_hit_base.ini", 34, 16);',
            "sleep(700);",
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_change_hit_life_after3");',
            'assign("mod_test_magic_change_hit_changed", 0);',
            'if getvar("mod_test_magic_change_hit_life_after3") <= getvar("mod_test_magic_change_hit_life_after2") - 80 then assign("mod_test_magic_change_hit_changed", 1) end',
            "cleareffect();",
            'displaymessage("命中变化武功测试完成");',
            "return;",
            "",
        ]
    )


def make_magic_post_cast_script() -> str:
    return "\n".join(
        [
            'displaymessage("施法后续武功测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_magic_post_cast_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            "setplayerpos(34, 20);",
            "setplayerdir(6);",
            "fullmana();",
            "fullthew();",
            "addevade(200);",
            "clearmagic();",
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_TARGET");',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'addnpc("mod_test_collision_target_npc.ini", 36, 20, 0);',
            'addmagic("mod_test_magic_post_cast_parent.ini");',
            'setmagiclevel("mod_test_magic_post_cast_parent.ini", 1);',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_post_cast_life_before");',
            'getplayerstate("Mana", "mod_test_magic_post_cast_mana_before");',
            'usemagic("mod_test_magic_post_cast_parent.ini", 36, 20);',
            'getplayerstate("IsMagicForcedMoving", "mod_test_magic_post_cast_jump_active");',
            'getplayerstate("MagicForcedMoveDestinationX", "mod_test_magic_post_cast_jump_dest_x");',
            'getplayerstate("MagicForcedMoveDestinationY", "mod_test_magic_post_cast_jump_dest_y");',
            'getplayerstate("MagicForcedMoveSpeed", "mod_test_magic_post_cast_jump_speed");',
            'getplayerstate("MagicForcedMoveHasEndMagic", "mod_test_magic_post_cast_jump_has_end_magic");',
            "sleep(700);",
            'getplayerstate("CurrentAction", "mod_test_magic_post_cast_jump_action_raw");',
            'getplayerstate("MagicForcedMoveUsesBezier", "mod_test_magic_post_cast_jump_uses_bezier");',
            'getplayerstate("MagicForcedMoveProgressPermille", "mod_test_magic_post_cast_jump_progress_permille");',
            'getplayerstate("MagicForcedMoveBezierOffsetLength", "mod_test_magic_post_cast_jump_bezier_offset_length");',
            'assign("mod_test_magic_post_cast_jump_action", 0);',
            'if getvar("mod_test_magic_post_cast_jump_action_raw") == 26 then assign("mod_test_magic_post_cast_jump_action", 1) end',
            'assign("mod_test_magic_post_cast_jump_progress_seen", 0);',
            'if getvar("mod_test_magic_post_cast_jump_progress_permille") > 0 and getvar("mod_test_magic_post_cast_jump_progress_permille") < 1000 then assign("mod_test_magic_post_cast_jump_progress_seen", 1) end',
            'assign("mod_test_magic_post_cast_jump_bezier_seen", 0);',
            'if getvar("mod_test_magic_post_cast_jump_uses_bezier") == 1 and getvar("mod_test_magic_post_cast_jump_bezier_offset_length") > 0 then assign("mod_test_magic_post_cast_jump_bezier_seen", 1) end',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_post_cast_life_mid");',
            'assign("mod_test_magic_post_cast_damage_mid", getvar("mod_test_magic_post_cast_life_before") - getvar("mod_test_magic_post_cast_life_mid"));',
            "sleep(1900);",
            'getplayerstate("MapX", "mod_test_magic_post_cast_player_x");',
            'getplayerstate("MapY", "mod_test_magic_post_cast_player_y");',
            'getplayerstate("Mana", "mod_test_magic_post_cast_mana_after");',
            'getnpcstate("MOD_TEST_COLLISION_TARGET", "Life", "mod_test_magic_post_cast_life_after");',
            'assign("mod_test_magic_post_cast_jump_destination", 0);',
            'if getvar("mod_test_magic_post_cast_jump_dest_x") == 36 and getvar("mod_test_magic_post_cast_jump_dest_y") == 20 then assign("mod_test_magic_post_cast_jump_destination", 1) end',
            'assign("mod_test_magic_post_cast_jump_moved", 0);',
            'if getvar("mod_test_magic_post_cast_player_x") == 36 and getvar("mod_test_magic_post_cast_player_y") == 20 then assign("mod_test_magic_post_cast_jump_moved", 1) end',
            'assign("mod_test_magic_post_cast_side_effect", 0);',
            'if getvar("mod_test_magic_post_cast_mana_after") <= getvar("mod_test_magic_post_cast_mana_before") - 20 then assign("mod_test_magic_post_cast_side_effect", 1) end',
            'assign("mod_test_magic_post_cast_damage_delta", getvar("mod_test_magic_post_cast_life_before") - getvar("mod_test_magic_post_cast_life_after"));',
            'assign("mod_test_magic_post_cast_child_chain", 0);',
            'if getvar("mod_test_magic_post_cast_damage_delta") >= 110 then assign("mod_test_magic_post_cast_child_chain", 1) end',
            'assign("mod_test_magic_post_cast_no_primary_extra", 0);',
            'if getvar("mod_test_magic_post_cast_damage_delta") <= 130 then assign("mod_test_magic_post_cast_no_primary_extra", 1) end',
            "cleareffect();",
            'addnpc("mod_test_collision_caster_npc.ini", 34, 22, 0);',
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_post_cast_die.ini", 34, 22, 1);',
            "sleep(500);",
            'getnpcstate("MOD_TEST_COLLISION_CASTER", "IsDeath", "mod_test_magic_post_cast_die_after_use");',
            'displaymessage("施法后续武功测试完成");',
            "return;",
            "",
        ]
    )


def make_equipment_trigger_script() -> str:
    return "\n".join(
        [
            'displaymessage("装备触发测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_equipment_trigger_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadnpc("map001.npc");',
            'loadobj("map001.obj");',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            "cleargoods();",
            'addgoods("mod_test_equipment_trigger.ini");',
            "equipgoods(1, 1);",
            'isequipweapon("mod_test_equipment_wrong_part_weapon");',
            "cleargoods();",
            'addgoods("mod_test_equipment_user_restricted.ini");',
            "equipgoods(1, 5);",
            'isequipweapon("mod_test_equipment_user_restricted_weapon");',
            "cleargoods();",
            "setplayerlevel(1);",
            'addgoods("mod_test_equipment_level_restricted.ini");',
            "equipgoods(1, 5);",
            'isequipweapon("mod_test_equipment_level_low_weapon");',
            "setplayerlevel(50);",
            "equipgoods(1, 5);",
            'isequipweapon("mod_test_equipment_level_ok_weapon");',
            "cleargoods();",
            "setplayerlevel(10);",
            'addgoods("mod_test_equipment_trigger.ini");',
            'isequipweapon("mod_test_equipment_weapon_before");',
            'getplayerstate("AttackAdditionalEffect", "mod_test_equipment_attack_additional_before");',
            'getplayerstate("LifeMax", "mod_test_equipment_lifemax_before");',
            'getplayerstate("ThewMax", "mod_test_equipment_thewmax_before");',
            'getplayerstate("ManaMax", "mod_test_equipment_manamax_before");',
            'getplayerstate("Attack", "mod_test_equipment_attack_before");',
            'getplayerstate("Defend", "mod_test_equipment_defend_before");',
            'getplayerstate("Evade", "mod_test_equipment_evade_before");',
            'addmagic("mod_test_magic_equipment_power.ini");',
            'addmagic("mod_test_magic_equipment_replace.ini");',
            'setmagiclevel("mod_test_magic_equipment_power.ini", 1);',
            'setmagiclevel("mod_test_magic_equipment_replace.ini", 1);',
            'delnpc("MOD_TEST_EQUIPMENT_POWER_TARGET");',
            'addnpc("mod_test_equipment_power_target_npc.ini", 34, 16, 0);',
            "fullmana();",
            "fullthew();",
            'usemagic("mod_test_magic_equipment_power.ini", 34, 16);',
            "sleep(900);",
            'getnpcstate("MOD_TEST_EQUIPMENT_POWER_TARGET", "Life", "mod_test_equipment_magic_baseline_life");',
            'delnpc("MOD_TEST_EQUIPMENT_POWER_TARGET");',
            "equipgoods(1, 5);",
            'isequipweapon("mod_test_equipment_weapon_equipped");',
            'addmagic("mod_test_magic_equipment_fly.ini");',
            'addmagic("mod_test_magic_equipment_fly2.ini");',
            'addmagic("mod_test_magic_equipment_counter.ini");',
            'assign("mod_test_equipment_flyini", 1);',
            'assign("mod_test_equipment_flyini2", 1);',
            'assign("mod_test_equipment_counter", 1);',
            'getplayerstate("EquipmentLifeMax", "mod_test_equipment_lifemax_bonus");',
            'getplayerstate("EquipmentThewMax", "mod_test_equipment_thewmax_bonus");',
            'getplayerstate("EquipmentManaMax", "mod_test_equipment_manamax_bonus");',
            'getplayerstate("EquipmentAttack", "mod_test_equipment_attack_bonus");',
            'getplayerstate("EquipmentAttack2", "mod_test_equipment_attack2_bonus");',
            'getplayerstate("EquipmentAttack3", "mod_test_equipment_attack3_bonus");',
            'getplayerstate("EquipmentDefend", "mod_test_equipment_defend_bonus");',
            'getplayerstate("EquipmentDefend2", "mod_test_equipment_defend2_bonus");',
            'getplayerstate("EquipmentDefend3", "mod_test_equipment_defend3_bonus");',
            'getplayerstate("EquipmentEvade", "mod_test_equipment_evade_bonus");',
            'getplayerstate("EquipmentLifeRestorePercent", "mod_test_equipment_life_restore_percent");',
            'getplayerstate("AttackAdditionalEffect", "mod_test_equipment_attack_additional_effect");',
            'getplayerstate("AdditionalEffect", "mod_test_equipment_additional_effect_alias");',
            'getplayerstate("LifeMax", "mod_test_equipment_lifemax_after");',
            'getplayerstate("ThewMax", "mod_test_equipment_thewmax_after");',
            'getplayerstate("ManaMax", "mod_test_equipment_manamax_after");',
            'getplayerstate("Attack", "mod_test_equipment_attack_after");',
            'getplayerstate("Defend", "mod_test_equipment_defend_after");',
            'getplayerstate("Evade", "mod_test_equipment_evade_after");',
            'assign("mod_test_equipment_lifemax_increased", 0);',
            'if getvar("mod_test_equipment_lifemax_after") > getvar("mod_test_equipment_lifemax_before") then assign("mod_test_equipment_lifemax_increased", 1) end',
            'assign("mod_test_equipment_thewmax_increased", 0);',
            'if getvar("mod_test_equipment_thewmax_after") > getvar("mod_test_equipment_thewmax_before") then assign("mod_test_equipment_thewmax_increased", 1) end',
            'assign("mod_test_equipment_manamax_increased", 0);',
            'if getvar("mod_test_equipment_manamax_after") > getvar("mod_test_equipment_manamax_before") then assign("mod_test_equipment_manamax_increased", 1) end',
            'assign("mod_test_equipment_attack_increased", 0);',
            'if getvar("mod_test_equipment_attack_after") > getvar("mod_test_equipment_attack_before") then assign("mod_test_equipment_attack_increased", 1) end',
            'assign("mod_test_equipment_defend_increased", 0);',
            'if getvar("mod_test_equipment_defend_after") > getvar("mod_test_equipment_defend_before") then assign("mod_test_equipment_defend_increased", 1) end',
            'assign("mod_test_equipment_evade_increased", 0);',
            'if getvar("mod_test_equipment_evade_after") > getvar("mod_test_equipment_evade_before") then assign("mod_test_equipment_evade_increased", 1) end',
            "fulllife();",
            "addlife(-50);",
            'getplayerstate("Life", "mod_test_equipment_life_restore_before");',
            "sleep(1300);",
            'getplayerstate("Life", "mod_test_equipment_life_restore_after");',
            'assign("mod_test_equipment_life_restored", 0);',
            'if getvar("mod_test_equipment_life_restore_after") > getvar("mod_test_equipment_life_restore_before") then assign("mod_test_equipment_life_restored", 1) end',
            'getplayerstate("EquipmentChangeMoveSpeedPercent", "mod_test_equipment_speed_percent");',
            'getplayerstate("MoveSpeedFoldPermille", "mod_test_equipment_speed_fold");',
            'getplayerstate("EquipmentMagicEffectNameCount", "mod_test_equipment_magic_name_count");',
            'getplayerstate("EquipmentMagicEffectTypeCount", "mod_test_equipment_magic_type_count");',
            'getplayerstate("EquipmentMagicEffectNamePercent", "mod_test_equipment_magic_name_percent");',
            'getplayerstate("EquipmentMagicEffectNameAmount", "mod_test_equipment_magic_name_amount");',
            'getgoodsstate("mod_test_equipment_trigger.ini", "HasReplaceMagic", "mod_test_equipment_has_replace_magic");',
            'getgoodsstate("mod_test_equipment_trigger.ini", "HasUseReplaceMagic", "mod_test_equipment_has_use_replace_magic");',
            'delnpc("MOD_TEST_EQUIPMENT_POWER_TARGET");',
            'addnpc("mod_test_equipment_power_target_npc.ini", 34, 16, 0);',
            "fullmana();",
            "fullthew();",
            'usemagic("mod_test_magic_equipment_power.ini", 34, 16);',
            "sleep(1500);",
            'getnpcstate("MOD_TEST_EQUIPMENT_POWER_TARGET", "Life", "mod_test_equipment_magic_target_life");',
            'assign("mod_test_equipment_magic_damage_applied", 0);',
            'if getvar("mod_test_equipment_magic_target_life") < getvar("mod_test_equipment_magic_baseline_life") then assign("mod_test_equipment_magic_damage_applied", 1) end',
            'assign("mod_test_equipment_replace_magic_applied", 0);',
            'if getvar("mod_test_equipment_magic_target_life") < getvar("mod_test_equipment_magic_baseline_life") - 120 then assign("mod_test_equipment_replace_magic_applied", 1) end',
            "cleareffect();",
            'addmagic("mod_test_magic_parasitic.ini");',
            'delnpc("MOD_TEST_EQUIPMENT_POWER_TARGET");',
            'addnpc("mod_test_equipment_power_target_npc.ini", 34, 16, 0);',
            "fullmana();",
            "fullthew();",
            'usemagic("mod_test_magic_parasitic.ini", 34, 16);',
            "sleep(800);",
            'geteffectstate("mod_test_magic_parasitic.ini", "ParasiticTotalEffect", "mod_test_equipment_parasitic_tick_baseline");',
            "cleareffect();",
            'delnpc("MOD_TEST_EQUIPMENT_POWER_TARGET");',
            'addgoods("mod_test_equipment_parasitic_bonus.ini");',
            'getplayerstate("EquipmentMagicEffectNameCount", "mod_test_equipment_magic_name_count_after_parasitic_bonus");',
            'addnpc("mod_test_equipment_power_target_npc.ini", 34, 16, 0);',
            "fullmana();",
            "fullthew();",
            'usemagic("mod_test_magic_parasitic.ini", 34, 16);',
            "sleep(800);",
            'geteffectstate("mod_test_magic_parasitic.ini", "ParasiticTotalEffect", "mod_test_equipment_parasitic_tick_bonus_total");',
            'assign("mod_test_equipment_parasitic_tick_bonus", 0);',
            'if getvar("mod_test_equipment_parasitic_tick_baseline") > 0 and getvar("mod_test_equipment_parasitic_tick_bonus_total") >= getvar("mod_test_equipment_parasitic_tick_baseline") + 50 then assign("mod_test_equipment_parasitic_tick_bonus", 1) end',
            "cleareffect();",
            'delnpc("MOD_TEST_EQUIPMENT_POWER_TARGET");',
            'displaymessage("正在测试装备附加效果");',
            'addmagic("mod_test_magic_equipment_additional_freeze.ini");',
            'getmagicstate("mod_test_magic_equipment_additional_freeze.ini", "AdditionalEffect", "mod_test_equipment_magic_additional_effect_state");',
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'addnpc("mod_test_collision_caster_npc.ini", 34, 22, 0);',
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            "fulllife();",
            'npcusemagic("MOD_TEST_COLLISION_CASTER", "mod_test_magic_equipment_additional_freeze.ini", 34, 20, 1);',
            "sleep(900);",
            'getplayerstate("IsFrozened", "mod_test_equipment_additional_frozen");',
            'getplayerstate("FrozenMilliseconds", "mod_test_equipment_additional_frozen_ms");',
            'assign("mod_test_equipment_additional_status", 0);',
            'if getvar("mod_test_equipment_magic_additional_effect_state") == 1 and getvar("mod_test_equipment_additional_frozen") == 1 and getvar("mod_test_equipment_additional_frozen_ms") > 0 then assign("mod_test_equipment_additional_status", 1) end',
            "cleareffect();",
            'delnpc("MOD_TEST_COLLISION_CASTER");',
            'displaymessage("装备触发测试完成");',
            "return;",
            "",
        ]
    )


def make_goods_pricing_script() -> str:
    return "\n".join(
        [
            'displaymessage("物品定价测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            "cleargoods();",
            'getgoodsstate("mod_test_goods_pricing_drug.ini", "Exists", "mod_test_goods_state_drug_exists");',
            'getgoodsstate("mod_test_goods_pricing_drug.ini", "TheEffectType", "mod_test_goods_state_drug_effect_type");',
            'getgoodsstate("mod_test_goods_pricing_drug.ini", "EffectType", "mod_test_goods_state_drug_effect_type_raw");',
            'getgoodsstate("mod_test_goods_pricing_drug.ini", "FileName", "mod_test_goods_state_drug_file_name");',
            'getgoodsstate("mod_test_goods_pricing_drug.ini", "CostRaw", "mod_test_goods_state_drug_cost_raw");',
            'getgoodsstate("mod_test_goods_pricing_drug.ini", "BuyPrice", "mod_test_goods_state_drug_buy_price");',
            'getgoodsstate("mod_test_goods_pricing_drug.ini", "SellPriceActual", "mod_test_goods_state_drug_sell_price");',
            'getgoodsstate("mod_test_goods_pricing_equipment.ini", "Exists", "mod_test_goods_state_equipment_exists");',
            'getgoodsstate("mod_test_goods_pricing_equipment.ini", "Kind", "mod_test_goods_state_equipment_kind");',
            'getgoodsstate("mod_test_goods_pricing_equipment.ini", "TheEffectType", "mod_test_goods_state_equipment_effect_type");',
            'getgoodsstate("mod_test_goods_pricing_equipment.ini", "RawEffectType", "mod_test_goods_state_equipment_effect_type_raw");',
            'getgoodsstate("mod_test_goods_pricing_equipment.ini", "CostRaw", "mod_test_goods_state_equipment_cost_raw");',
            'getgoodsstate("mod_test_goods_pricing_equipment.ini", "IsSellPriceSetted", "mod_test_goods_state_equipment_sell_set");',
            'getgoodsstate("mod_test_goods_pricing_equipment.ini", "SellPriceActual", "mod_test_goods_state_equipment_sell_price");',
            'getgoodsstate("mod_test_goods_pricing_equipment.ini", "HasPart", "mod_test_goods_state_equipment_has_part");',
            'getgoodsstate("mod_test_goods_pricing_equipment.ini", "Attack", "mod_test_goods_state_equipment_attack");',
            'getgoodsstate("mod_test_goods_pricing_equipment.ini", "Defend", "mod_test_goods_state_equipment_defend");',
            'getgoodsstate("mod_test_goods_pricing_equipment.ini", "Evade", "mod_test_goods_state_equipment_evade");',
            'getgoodsstate("mod_test_goods_pricing_noneed.ini", "CostRaw", "mod_test_goods_state_noneed_cost_raw");',
            'getgoodsstate("mod_test_goods_pricing_noneed.ini", "NoNeedToEquip", "mod_test_goods_state_noneed_flag");',
            'getgoodsstate("mod_test_goods_pricing_noneed.ini", "LifeMax", "mod_test_goods_state_noneed_lifemax");',
            'getgoodsstate("mod_test_goods_pricing_noneed.ini", "ManaMax", "mod_test_goods_state_noneed_manamax");',
            'getgoodsstate("mod_test_goods_random.ini", "HasRandAttr", "mod_test_goods_state_random_has_rand");',
            'getgoodsstate("mod_test_goods_random.ini", "HasMagicName", "mod_test_goods_state_random_has_magic_name");',
            'getgoodsstate("mod_test_goods_random.ini", "HasMagicIniWhenUse", "mod_test_goods_state_random_has_magic");',
            'addgoods("mod_test_goods_pricing_drug.ini");',
            'getgoodsnum("mod_test_goods_pricing_drug.ini");',
            'assign("mod_test_goods_pricing_drug_count", getvar("GoodsNum"));',
            'addgoods("mod_test_goods_pricing_equipment.ini");',
            'getgoodsnum("mod_test_goods_pricing_equipment.ini");',
            'assign("mod_test_goods_pricing_equipment_count", getvar("GoodsNum"));',
            'addgoods("mod_test_goods_pricing_noneed.ini");',
            'getgoodsnum("mod_test_goods_pricing_noneed.ini");',
            'assign("mod_test_goods_pricing_noneed_count", getvar("GoodsNum"));',
            'assign("mod_test_goods_pricing_ready", 1);',
            'displaymessage("物品定价测试完成");',
            "return;",
            "",
        ]
    )


def make_goods_random_script() -> str:
    return "\n".join(
        [
            'displaymessage("随机物品测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            "cleargoods();",
            'addgoods("mod_test_goods_random.ini");',
            'addgoods("mod_test_goods_random.ini");',
            'getgoodsnum("mod_test_goods_random.ini");',
            'assign("mod_test_goods_random_template_count", getvar("GoodsNum"));',
            'getgoodsnumbyname("MOD_TEST_GOODS_RANDOM");',
            'assign("mod_test_goods_random_count", getvar("GoodsNum"));',
            'if getvar("mod_test_goods_random_count") == 2 and getvar("mod_test_goods_random_template_count") == 0 then assign("mod_test_goods_random_instanced", 1) end',
            'assign("mod_test_goods_random_ready", 1);',
            'displaymessage("随机物品测试完成");',
            "return;",
            "",
        ]
    )


def make_goods_script_book_script() -> str:
    return "\n".join(
        [
            'displaymessage("正在使用脚本秘籍物品");',
            'add("mod_test_goods_lifecycle_script_used", 1);',
            'addmagic("mod_test_magic_goods_script_book.ini");',
            'setmagiclevel("mod_test_magic_goods_script_book.ini", 1);',
            "delgoods();",
            'getgoodsnum("mod_test_goods_script_book.ini");',
            'assign("mod_test_goods_lifecycle_script_remaining", getvar("GoodsNum"));',
            'if getvar("mod_test_goods_lifecycle_script_used") == 1 then assign("mod_test_goods_lifecycle_script_after_first", getvar("GoodsNum")) end',
            'getplayermagiclevel("mod_test_magic_goods_script_book.ini", "mod_test_goods_lifecycle_script_magic_level");',
            "return;",
            "",
        ]
    )


def make_goods_lifecycle_script() -> str:
    return "\n".join(
        [
            'displaymessage("物品生命周期测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            "cleargoods();",
            "clearmagic();",
            'addgoods("mod_test_goods_lifecycle_stack.ini");',
            'addgoods("mod_test_goods_lifecycle_stack.ini");',
            'getgoodsnum("mod_test_goods_lifecycle_stack.ini");',
            'assign("mod_test_goods_lifecycle_delete_before", getvar("GoodsNum"));',
            "savegoods(9);",
            'delgoods("mod_test_goods_lifecycle_stack.ini");',
            'getgoodsnum("mod_test_goods_lifecycle_stack.ini");',
            'assign("mod_test_goods_lifecycle_delete_after_file", getvar("GoodsNum"));',
            'delgoodbyname("MOD_TEST_GOODS_LIFECYCLE_STACK", 1);',
            'getgoodsnum("mod_test_goods_lifecycle_stack.ini");',
            'assign("mod_test_goods_lifecycle_delete_after_name", getvar("GoodsNum"));',
            'addgoods("mod_test_goods_lifecycle_case.ini");',
            'getgoodsnumbyname("MOD_TEST_GOODS_LIFECYCLE_CASE");',
            'assign("mod_test_goods_lifecycle_name_case_upper", getvar("GoodsNum"));',
            'getgoodsnumbyname("Mod_Test_Goods_Lifecycle_Case");',
            'assign("mod_test_goods_lifecycle_name_case_exact", getvar("GoodsNum"));',
            'delgoodbyname("MOD_TEST_GOODS_LIFECYCLE_CASE", 1);',
            'getgoodsnumbyname("Mod_Test_Goods_Lifecycle_Case");',
            'assign("mod_test_goods_lifecycle_name_case_after_delete", getvar("GoodsNum"));',
            'addgoods("mod_test_goods_pricing_noneed.ini");',
            "savegoods(8);",
            "cleargoods();",
            "loadgoods(8);",
            'getgoodsnum("mod_test_goods_pricing_noneed.ini");',
            'assign("mod_test_goods_lifecycle_noneed_load_count", getvar("GoodsNum"));',
            "cleargoods();",
            "clearmagic();",
            'addgoods("mod_test_goods_lifecycle_magic.ini");',
            'getplayermagiclevel("mod_test_magic_equipment_fly.ini", "mod_test_goods_lifecycle_magic_before");',
            "equipgoods(1, 5);",
            'getplayermagiclevel("mod_test_magic_equipment_fly.ini", "mod_test_goods_lifecycle_magic_equipped");',
            "cleargoods();",
            'getplayermagiclevel("mod_test_magic_equipment_fly.ini", "mod_test_goods_lifecycle_magic_after_clear");',
            'addgoods("mod_test_goods_lifecycle_magic.ini");',
            "equipgoods(1, 5);",
            'getplayermagiclevel("mod_test_magic_equipment_fly.ini", "mod_test_goods_lifecycle_magic_reequipped");',
            "cleargoods();",
            "clearmagic();",
            'addmagic("mod_test_magic_equipment_fly.ini");',
            'setmagiclevel("mod_test_magic_equipment_fly.ini", 2);',
            'getplayermagiclevel("mod_test_magic_equipment_fly.ini", "mod_test_goods_lifecycle_learned_before");',
            'addgoods("mod_test_goods_lifecycle_magic.ini");',
            "equipgoods(1, 5);",
            'getplayermagiclevel("mod_test_magic_equipment_fly.ini", "mod_test_goods_lifecycle_learned_equipped");',
            'addgoods("mod_test_goods_lifecycle_magic_noneed.ini");',
            'getplayermagiclevel("mod_test_magic_equipment_fly.ini", "mod_test_goods_lifecycle_learned_duplicate");',
            "cleargoods();",
            'getplayermagiclevel("mod_test_magic_equipment_fly.ini", "mod_test_goods_lifecycle_learned_after_clear");',
            "clearmagic();",
            'loadmap("map001_衡山.map");',
            'loadnpc("map001.npc");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            "setplayerpos(34, 20);",
            "setplayerdir(6);",
            'delnpc("MOD_TEST_GOODS_FRIEND_DRUG_TARGET");',
            'delnpc("MOD_TEST_GOODS_PARTNER_DRUG_TARGET");',
            'addnpc("mod_test_goods_friend_drug_target_npc.ini", 35, 20, 0);',
            'getnpcstate("MOD_TEST_GOODS_FRIEND_DRUG_TARGET", "Life", "mod_test_goods_friend_drug_life_before");',
            'getnpcstate("MOD_TEST_GOODS_FRIEND_DRUG_TARGET", "LifeMax", "mod_test_goods_friend_drug_lifemax_before");',
            'getplayerstate("LifeMax", "mod_test_goods_player_friend_drug_lifemax_before");',
            'addgoods("mod_test_goods_friend_drug.ini");',
            'getgoodsnum("mod_test_goods_friend_drug.ini");',
            'assign("mod_test_goods_friend_drug_count_before", getvar("GoodsNum"));',
            "equipgoods(1, 5);",
            'getnpcstate("MOD_TEST_GOODS_FRIEND_DRUG_TARGET", "Life", "mod_test_goods_friend_drug_life_after");',
            'getnpcstate("MOD_TEST_GOODS_FRIEND_DRUG_TARGET", "LifeMax", "mod_test_goods_friend_drug_lifemax_after");',
            'getnpcstate("MOD_TEST_GOODS_FRIEND_DRUG_TARGET", "ThewMax", "mod_test_goods_friend_drug_thewmax_after");',
            'getnpcstate("MOD_TEST_GOODS_FRIEND_DRUG_TARGET", "ManaMax", "mod_test_goods_friend_drug_manamax_after");',
            'getplayerstate("LifeMax", "mod_test_goods_player_friend_drug_lifemax_after");',
            'getgoodsnum("mod_test_goods_friend_drug.ini");',
            'assign("mod_test_goods_friend_drug_count_after", getvar("GoodsNum"));',
            'assign("mod_test_goods_friend_drug_applied", 0);',
            'if getvar("mod_test_goods_friend_drug_life_after") > getvar("mod_test_goods_friend_drug_life_before") then assign("mod_test_goods_friend_drug_applied", 1) end',
            'assign("mod_test_goods_player_friend_drug_lifemax_increased", 0);',
            'if getvar("mod_test_goods_player_friend_drug_lifemax_after") > getvar("mod_test_goods_player_friend_drug_lifemax_before") then assign("mod_test_goods_player_friend_drug_lifemax_increased", 1) end',
            'getgoodsstate("mod_test_goods_friend_clear_poison.ini", "FighterFriendHasDrugEffect", "mod_test_goods_friend_clear_poison_fighter_flag");',
            'getgoodsstate("mod_test_goods_friend_clear_poison.ini", "EffectType", "mod_test_goods_friend_clear_poison_effect_type");',
            'setmapnpcattr("MOD_TEST_GOODS_FRIEND_DRUG_TARGET", "PoisonedMilliseconds:5000", "map001.npc");',
            'getnpcstate("MOD_TEST_GOODS_FRIEND_DRUG_TARGET", "IsPoisoned", "mod_test_goods_friend_clear_poison_before");',
            'addgoods("mod_test_goods_friend_clear_poison.ini");',
            'getgoodsnum("mod_test_goods_friend_clear_poison.ini");',
            'assign("mod_test_goods_friend_clear_poison_count_before", getvar("GoodsNum"));',
            "equipgoods(1, 5);",
            'getnpcstate("MOD_TEST_GOODS_FRIEND_DRUG_TARGET", "IsPoisoned", "mod_test_goods_friend_clear_poison_after");',
            'getgoodsnum("mod_test_goods_friend_clear_poison.ini");',
            'assign("mod_test_goods_friend_clear_poison_count_after", getvar("GoodsNum"));',
            'assign("mod_test_goods_friend_clear_poison_ok", 0);',
            'if getvar("mod_test_goods_friend_clear_poison_before") == 1 and getvar("mod_test_goods_friend_clear_poison_after") == 0 then assign("mod_test_goods_friend_clear_poison_ok", 1) end',
            'delnpc("MOD_TEST_GOODS_FRIEND_DRUG_TARGET");',
            "cleargoods();",
            'addnpc("mod_test_goods_partner_drug_target_npc.ini", 36, 20, 0);',
            'setnpcpartner("MOD_TEST_GOODS_PARTNER_DRUG_TARGET");',
            'getnpcstate("MOD_TEST_GOODS_PARTNER_DRUG_TARGET", "IsPartner", "mod_test_goods_partner_drug_is_partner");',
            'getnpcstate("MOD_TEST_GOODS_PARTNER_DRUG_TARGET", "Life", "mod_test_goods_partner_drug_life_before");',
            'getnpcstate("MOD_TEST_GOODS_PARTNER_DRUG_TARGET", "LifeMax", "mod_test_goods_partner_drug_lifemax_before");',
            'getplayerstate("LifeMax", "mod_test_goods_player_partner_drug_lifemax_before");',
            'addgoods("mod_test_goods_partner_drug.ini");',
            'getgoodsnum("mod_test_goods_partner_drug.ini");',
            'assign("mod_test_goods_partner_drug_count_before", getvar("GoodsNum"));',
            "equipgoods(1, 5);",
            'getnpcstate("MOD_TEST_GOODS_PARTNER_DRUG_TARGET", "Life", "mod_test_goods_partner_drug_life_after");',
            'getnpcstate("MOD_TEST_GOODS_PARTNER_DRUG_TARGET", "LifeMax", "mod_test_goods_partner_drug_lifemax_after");',
            'getnpcstate("MOD_TEST_GOODS_PARTNER_DRUG_TARGET", "ThewMax", "mod_test_goods_partner_drug_thewmax_after");',
            'getnpcstate("MOD_TEST_GOODS_PARTNER_DRUG_TARGET", "ManaMax", "mod_test_goods_partner_drug_manamax_after");',
            'getplayerstate("LifeMax", "mod_test_goods_player_partner_drug_lifemax_after");',
            'getgoodsnum("mod_test_goods_partner_drug.ini");',
            'assign("mod_test_goods_partner_drug_count_after", getvar("GoodsNum"));',
            'assign("mod_test_goods_partner_drug_applied", 0);',
            'if getvar("mod_test_goods_partner_drug_life_after") > getvar("mod_test_goods_partner_drug_life_before") then assign("mod_test_goods_partner_drug_applied", 1) end',
            'assign("mod_test_goods_player_partner_drug_lifemax_increased", 0);',
            'if getvar("mod_test_goods_player_partner_drug_lifemax_after") > getvar("mod_test_goods_player_partner_drug_lifemax_before") then assign("mod_test_goods_player_partner_drug_lifemax_increased", 1) end',
            'delnpc("MOD_TEST_GOODS_PARTNER_DRUG_TARGET");',
            "cleargoods();",
            'addgoods("mod_test_goods_bound_ammo.ini");',
            'addgoods("mod_test_goods_bound_ammo.ini");',
            'addmagic("mod_test_magic_goods_bound.ini");',
            'setmagiclevel("mod_test_magic_goods_bound.ini", 1);',
            'getgoodsnum("mod_test_goods_bound_ammo.ini");',
            'assign("mod_test_goods_lifecycle_bound_before", getvar("GoodsNum"));',
            'usemagic("mod_test_magic_goods_bound.ini", 34, 16);',
            'getgoodsnum("mod_test_goods_bound_ammo.ini");',
            'assign("mod_test_goods_lifecycle_bound_after_one", getvar("GoodsNum"));',
            'usemagic("mod_test_magic_goods_bound.ini", 34, 16);',
            'getgoodsnum("mod_test_goods_bound_ammo.ini");',
            'assign("mod_test_goods_lifecycle_bound_after_two", getvar("GoodsNum"));',
            'usemagic("mod_test_magic_goods_bound.ini", 34, 16);',
            'getgoodsnum("mod_test_goods_bound_ammo.ini");',
            'assign("mod_test_goods_lifecycle_bound_after_missing", getvar("GoodsNum"));',
            "cleargoods();",
            "clearmagic();",
            'assign("mod_test_goods_lifecycle_script_used", 0);',
            'addgoods("mod_test_goods_script_book.ini");',
            'addgoods("mod_test_goods_script_book.ini");',
            'getgoodsnum("mod_test_goods_script_book.ini");',
            'assign("mod_test_goods_lifecycle_script_before", getvar("GoodsNum"));',
            "equipgoods(1, 5);",
            "equipgoods(1, 5);",
            'assign("mod_test_goods_lifecycle_ready", 1);',
            'displaymessage("物品生命周期测试完成");',
            "return;",
            "",
        ]
    )


def make_player_control_state_script() -> str:
    return "\n".join(
        [
            'displaymessage("玩家控制状态测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            "enablerun();",
            "enablejump();",
            "enablefight();",
            'assign("mod_test_player_control_ready", 1);',
            'getplayerstate("IsRunDisabled", "mod_test_player_run_initial");',
            'getplayerstate("CanRun", "mod_test_player_can_run_initial");',
            'disablerun();',
            'getplayerstate("IsRunDisabled", "mod_test_player_run_disabled");',
            'getplayerstate("CanRun", "mod_test_player_can_run_disabled");',
            'enablerun();',
            'getplayerstate("IsRunDisabled", "mod_test_player_run_enabled");',
            'getplayerstate("CanRun", "mod_test_player_can_run_enabled");',
            'getplayerstate("IsJumpDisabled", "mod_test_player_jump_initial");',
            'getplayerstate("CanJump", "mod_test_player_can_jump_initial");',
            'disablejump();',
            'getplayerstate("IsJumpDisabled", "mod_test_player_jump_disabled");',
            'getplayerstate("CanJump", "mod_test_player_can_jump_disabled");',
            'enablejump();',
            'getplayerstate("IsJumpDisabled", "mod_test_player_jump_enabled");',
            'getplayerstate("CanJump", "mod_test_player_can_jump_enabled");',
            'getplayerstate("IsFightDisabled", "mod_test_player_fight_initial");',
            'getplayerstate("CanFight", "mod_test_player_can_fight_initial");',
            'disablefight();',
            'getplayerstate("IsFightDisabled", "mod_test_player_fight_disabled");',
            'getplayerstate("CanFight", "mod_test_player_can_fight_disabled");',
            'enablefight();',
            'getplayerstate("IsFightDisabled", "mod_test_player_fight_enabled");',
            'getplayerstate("CanFight", "mod_test_player_can_fight_enabled");',
            'disablerun();',
            'disablejump();',
            'disablefight();',
            'saveplayersnapshot("mod_test_player_disabled_state");',
            'enablerun();',
            'enablejump();',
            'enablefight();',
            'loadplayersnapshot("mod_test_player_disabled_state");',
            'getplayerstate("IsRunDisabled", "mod_test_player_run_disabled_after_load");',
            'getplayerstate("CanRun", "mod_test_player_can_run_disabled_after_load");',
            'getplayerstate("IsJumpDisabled", "mod_test_player_jump_disabled_after_load");',
            'getplayerstate("CanJump", "mod_test_player_can_jump_disabled_after_load");',
            'getplayerstate("IsFightDisabled", "mod_test_player_fight_disabled_after_load");',
            'getplayerstate("CanFight", "mod_test_player_can_fight_disabled_after_load");',
            'enablerun();',
            'enablejump();',
            'enablefight();',
            'getplayerstate("CanUseMana", "mod_test_player_can_use_mana");',
            'getplayerstate("FightState", "mod_test_player_fight_state");',
            'getplayerstate("BodyFunctionWell", "mod_test_player_body_function_well");',
            'getplayerstate("ControledMagicSprite", "mod_test_player_controled_magic_sprite");',
            'getplayerstate("MovedByMagicSprite", "mod_test_player_moved_by_magic_sprite");',
            'getplayerstate("IsMagicFromCache", "mod_test_player_magic_from_cache");',
            'getplayerstate("IsFullLife", "mod_test_player_full_life");',
            'getplayerstate("IsInFighting", "mod_test_player_in_fighting");',
            'loadmap("map001_衡山.map");',
            "disablenpcai();",
            "setplayerpos(30, 20);",
            "playgoto(31, 20);",
            'getplayerstate("MapX", "mod_test_player_playgoto_x");',
            'getplayerstate("MapY", "mod_test_player_playgoto_y");',
            "playerwalkto(32, 20);",
            'getplayerstate("MapX", "mod_test_player_walkto_x");',
            'getplayerstate("MapY", "mod_test_player_walkto_y");',
            'assign("mod_test_player_walktodir_alias", 0);',
            "playerwalktodir(0, 0);",
            'assign("mod_test_player_walktodir_alias", 1);',
            'assign("mod_test_player_walkto_nonblocking_alias", 0);',
            "playerwalktononblocking(32, 20);",
            'assign("mod_test_player_walkto_nonblocking_alias", 1);',
            'assign("mod_test_player_runto_nonblocking_alias", 0);',
            "playerruntononblocking(32, 20);",
            'assign("mod_test_player_runto_nonblocking_alias", 1);',
            "return;",
            "",
        ]
    )


def make_magic_transport_control_script() -> str:
    return "\n".join(
        [
            'displaymessage("武功传送与控制测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_magic_transport_control_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadnpc("map001.npc");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            'delnpc("MOD_TEST_NPC_CONTROL_TARGET");',
            'delnpc("MOD_TEST_NPC_CONTROL_WATCHER");',
            "clearmagic();",
            'addmagic("mod_test_magic_transport.ini");',
            'addmagic("mod_test_magic_control.ini");',
            'setmagiclevel("mod_test_magic_transport.ini", 1);',
            'setmagiclevel("mod_test_magic_control.ini", 1);',
            "setplayerpos(34, 20);",
            "setplayerdir(6);",
            'getplayerstate("IsTransporting", "mod_test_magic_transport_before");',
            'usemagic("mod_test_magic_transport.ini", 36, 20);',
            'getplayerstate("IsTransporting", "mod_test_magic_transport_active");',
            'getplayerstate("IsVisible", "mod_test_magic_transport_visible_active");',
            'getplayerstate("IsObstacle", "mod_test_magic_transport_obstacle_active");',
            "sleep(650);",
            'getplayerstate("IsTransporting", "mod_test_magic_transport_after");',
            'getplayerstate("MapX", "mod_test_magic_transport_x");',
            'getplayerstate("MapY", "mod_test_magic_transport_y");',
            'getplayerstate("IsVisible", "mod_test_magic_transport_visible_after");',
            'getplayerstate("IsObstacle", "mod_test_magic_transport_obstacle_after");',
            'assign("mod_test_magic_transport_moved", 0);',
            'if getvar("mod_test_magic_transport_x") == 36 and getvar("mod_test_magic_transport_y") == 20 then assign("mod_test_magic_transport_moved", 1) end',
            "setplayerpos(34, 20);",
            "setplayerdir(6);",
            'addnpc("mod_test_magic_control_target_npc.ini", 36, 20, 0);',
            'addnpc("mod_test_magic_control_watcher_npc.ini", 37, 20, 0);',
            'getnpcstate("MOD_TEST_NPC_CONTROL_WATCHER", "HasCurrentCombatTarget", "mod_test_magic_control_watcher_before");',
            'getplayerstate("IsControllingCharacter", "mod_test_magic_control_before");',
            'usemagic("mod_test_magic_control.ini", 36, 20);',
            'getplayerstate("IsControllingCharacter", "mod_test_magic_control_active");',
            'getplayerstate("CameraFollowNpc", "mod_test_magic_control_camera_follow_active");',
            'getplayerstate("ControlledTargetMapX", "mod_test_magic_control_target_x");',
            'getplayerstate("ControlledTargetMapY", "mod_test_magic_control_target_y");',
            'getplayerstate("ControlledTargetKind", "mod_test_magic_control_target_kind");',
            'getplayerstate("ControlledTargetRelation", "mod_test_magic_control_target_raw_relation");',
            'getplayerstate("ControlledTargetRuntimeRelation", "mod_test_magic_control_target_runtime_relation");',
            'getnpcstate("MOD_TEST_NPC_CONTROL_TARGET", "IsControlledByPlayer", "mod_test_magic_control_npc_controlled");',
            'getnpcstate("MOD_TEST_NPC_CONTROL_TARGET", "RuntimeRelation", "mod_test_magic_control_npc_runtime_relation");',
            'getnpcstate("MOD_TEST_NPC_CONTROL_TARGET", "Relation", "mod_test_magic_control_npc_raw_relation");',
            'getnpcstate("MOD_TEST_NPC_CONTROL_TARGET", "IsRuntimeFighterFriend", "mod_test_magic_control_runtime_friend");',
            'assign("mod_test_magic_control_target_match", 0);',
            'if getvar("mod_test_magic_control_target_x") == 36 and getvar("mod_test_magic_control_target_y") == 20 then assign("mod_test_magic_control_target_match", 1) end',
            "enablenpcai();",
            "sleep(120);",
            'getnpcstate("MOD_TEST_NPC_CONTROL_WATCHER", "HasCurrentCombatTarget", "mod_test_magic_control_watcher_active");',
            'getnpcstate("MOD_TEST_NPC_CONTROL_WATCHER", "CurrentCombatTargetMapX", "mod_test_magic_control_watcher_target_x");',
            'getnpcstate("MOD_TEST_NPC_CONTROL_WATCHER", "CurrentCombatTargetMapY", "mod_test_magic_control_watcher_target_y");',
            'assign("mod_test_magic_control_watcher_target_match", 0);',
            'if getvar("mod_test_magic_control_watcher_target_x") == 36 and getvar("mod_test_magic_control_watcher_target_y") == 20 then assign("mod_test_magic_control_watcher_target_match", 1) end',
            "disablenpcai();",
            "sleep(900);",
            'getplayerstate("IsControllingCharacter", "mod_test_magic_control_after");',
            'getplayerstate("CameraFollowNpc", "mod_test_magic_control_camera_follow_after");',
            'getnpcstate("MOD_TEST_NPC_CONTROL_TARGET", "IsControlledByPlayer", "mod_test_magic_control_npc_after");',
            'getnpcstate("MOD_TEST_NPC_CONTROL_TARGET", "RuntimeRelation", "mod_test_magic_control_npc_runtime_after");',
            'getnpcstate("MOD_TEST_NPC_CONTROL_WATCHER", "HasCurrentCombatTarget", "mod_test_magic_control_watcher_after");',
            'delnpc("MOD_TEST_NPC_CONTROL_TARGET");',
            'delnpc("MOD_TEST_NPC_CONTROL_WATCHER");',
            "cleareffect();",
            "setplayerpos(34, 20);",
            "setplayerdir(6);",
            'addnpc("mod_test_magic_control_target_npc.ini", 36, 20, 0);',
            'usemagic("mod_test_magic_control.ini", 36, 20);',
            'getplayerstate("IsControllingCharacter", "mod_test_magic_control_death_active");',
            'getplayerstate("CameraFollowNpc", "mod_test_magic_control_death_camera_active");',
            'getnpcstate("MOD_TEST_NPC_CONTROL_TARGET", "IsControlledByPlayer", "mod_test_magic_control_death_npc_controlled");',
            'setnpcaction("MOD_TEST_NPC_CONTROL_TARGET", 11);',
            "sleep(120);",
            'getplayerstate("IsControllingCharacter", "mod_test_magic_control_death_after");',
            'getplayerstate("CameraFollowNpc", "mod_test_magic_control_death_camera_after");',
            'getnpcstate("MOD_TEST_NPC_CONTROL_TARGET", "IsControlledByPlayer", "mod_test_magic_control_death_npc_after");',
            'displaymessage("武功传送与控制测试完成");',
            "return;",
            "",
        ]
    )


def make_magic_region_vtype_script() -> str:
    return "\n".join(
        [
            'displaymessage("V 型区域武功测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_magic_region_vtype_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            "cleareffect();",
            "clearmagic();",
            'addmagic("mod_test_magic_region_vtype.ini");',
            'setmagiclevel("mod_test_magic_region_vtype.ini", 1);',
            'getmagicstate("mod_test_magic_region_vtype.ini", "HasType", "mod_test_magic_region_vtype_has_type");',
            'getmagicstate("mod_test_magic_region_vtype.ini", "HasInjuryType", "mod_test_magic_region_vtype_has_injury_type");',
            'getmagicstate("mod_test_magic_region_vtype.ini", "SpriteType", "mod_test_magic_region_vtype_sprite_type");',
            'getmagicstate("mod_test_magic_region_vtype.ini", "HasSpriteType", "mod_test_magic_region_vtype_has_sprite_type");',
            'getmagicstate("mod_test_magic_region_vtype.ini", "Attribute", "mod_test_magic_region_vtype_attribute");',
            'getmagicstate("mod_test_magic_region_vtype.ini", "HasAttribute", "mod_test_magic_region_vtype_has_attribute");',
            'getmagicstate("mod_test_magic_region_vtype.ini", "HasScriptFile", "mod_test_magic_region_vtype_has_script_file");',
            'getmagicstate("mod_test_magic_region_vtype.ini", "RangeAddRage", "mod_test_magic_region_vtype_range_add_rage", 1);',
            'getmagicstate("mod_test_magic_region_vtype.ini", "HasRangeAddRage", "mod_test_magic_region_vtype_has_range_add_rage", 1);',
            'getmagicstate("mod_test_magic_region_vtype.ini", "RageCost", "mod_test_magic_region_vtype_rage_cost", 1);',
            'getmagicstate("mod_test_magic_region_vtype.ini", "HasRageCost", "mod_test_magic_region_vtype_has_rage_cost", 1);',
            'getmagicstate("mod_test_magic_region_vtype.ini", "MoveKind", "mod_test_magic_region_vtype_movekind");',
            'getmagicstate("mod_test_magic_region_vtype.ini", "Region", "mod_test_magic_region_vtype_region");',
            "setplayerpos(34, 20);",
            "setplayerdir(4);",
            "addevade(200);",
            'delnpc("MOD_TEST_REGION_VTYPE_HIT");',
            'delnpc("MOD_TEST_REGION_VTYPE_SAFE");',
            'addnpc("mod_test_magic_region_vtype_hit_npc.ini", 34, 17, 0);',
            'addnpc("mod_test_magic_region_vtype_safe_npc.ini", 36, 17, 0);',
            'getmapstate(34, 17, "NpcCount", "mod_test_magic_region_vtype_hit_tile_npc_count");',
            'getmapstate(34, 17, "CanFly", "mod_test_magic_region_vtype_hit_tile_can_fly");',
            'getnpcstate("MOD_TEST_REGION_VTYPE_HIT", "MapX", "mod_test_magic_region_vtype_hit_x");',
            'getnpcstate("MOD_TEST_REGION_VTYPE_HIT", "MapY", "mod_test_magic_region_vtype_hit_y");',
            'getnpcstate("MOD_TEST_REGION_VTYPE_HIT", "IsVisible", "mod_test_magic_region_vtype_hit_visible");',
            'getnpcstate("MOD_TEST_REGION_VTYPE_HIT", "Relation", "mod_test_magic_region_vtype_hit_relation");',
            'usemagic("mod_test_magic_region_vtype.ini", 34, 16);',
            'geteffectstate("mod_test_magic_region_vtype.ini", "Count", "mod_test_magic_region_vtype_effect_count");',
            'geteffectstate("mod_test_magic_region_vtype.ini", "ProjectileLauncherKind", "mod_test_magic_region_vtype_projectile_launcher");',
            'geteffectstate("mod_test_magic_region_vtype.ini", "ProjectileUserIsPlayer", "mod_test_magic_region_vtype_projectile_user_is_player");',
            'geteffectstate("mod_test_magic_region_vtype.ini", "ProjectileMapX", "mod_test_magic_region_vtype_projectile_x");',
            'geteffectstate("mod_test_magic_region_vtype.ini", "ProjectileMapY", "mod_test_magic_region_vtype_projectile_y");',
            "sleep(650);",
            'getnpcstate("MOD_TEST_REGION_VTYPE_HIT", "Life", "mod_test_magic_region_vtype_hit_life");',
            'getnpcstate("MOD_TEST_REGION_VTYPE_SAFE", "Life", "mod_test_magic_region_vtype_safe_life");',
            'assign("mod_test_magic_region_vtype_hit", 0);',
            'if getvar("mod_test_magic_region_vtype_hit_life") == 99989 then assign("mod_test_magic_region_vtype_hit", 1) end',
            'assign("mod_test_magic_region_vtype_safe", 0);',
            'if getvar("mod_test_magic_region_vtype_safe_life") == 99999 then assign("mod_test_magic_region_vtype_safe", 1) end',
            "cleareffect();",
            'displaymessage("V 型区域武功测试完成");',
            "return;",
            "",
        ]
    )


def make_script_timer_parallel_script() -> str:
    return "\n".join(
        [
            'displaymessage("时间脚本与并行脚本测试开始：70 秒倒计时结束时触发时间脚本");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_script_timer_triggered", 0);',
            'assign("mod_test_script_parallel_immediate", 0);',
            'assign("mod_test_script_parallel_delayed", 0);',
            'settimescript(0, "mod_test_script_timer_trigger.txt");',
            "opentimelimit(70);",
            'runparallelscript("mod_test_script_parallel_delayed.txt", 1500);',
            "savegame();",
            "loadgame(-1);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_script_timer_parallel_ready", 1);',
            'runparallelscript("mod_test_script_parallel_immediate.txt", 0);',
            'assign("mod_test_script_messagebox_alias", 0);',
            'messagebox("MOD_TEST messagebox alias");',
            'assign("mod_test_script_messagebox_alias", 1);',
            'assign("mod_test_script_message_alias", 0);',
            'message("MOD_TEST message alias");',
            'assign("mod_test_script_message_alias", 1);',
            'assign("mod_test_script_assing_alias", 0);',
            'assing("mod_test_script_assing_alias", 1);',
            'assign("mod_test_script_setvar_alias", 0);',
            'setvar("mod_test_script_setvar_alias", 1);',
            'assign("mod_test_script_sub_command", 5);',
            'sub("mod_test_script_sub_command", 2);',
            'assign("mod_test_script_system_message_alias", 0);',
            'showsystemmessage("MOD_TEST system message alias", 500);',
            'assign("mod_test_script_system_message_alias", 1);',
            'assign("mod_test_script_enabeldrop_alias", 0);',
            "enabeldrop();",
            'assign("mod_test_script_enabeldrop_alias", 1);',
            'cleargoods();',
            'addgoods("mod_test_goods_lifecycle_stack.ini", 2);',
            'getgoodsmun("mod_test_goods_lifecycle_stack.ini");',
            'assign("mod_test_script_getgoodsmun_alias", getvar("GoodsNum"));',
            'assign("mod_test_script_runscirpt_alias", 0);',
            'runscirpt("mod_test_script_alias_run.txt");',
            'setplayrdir(5);',
            'getplayerstate("Dir", "mod_test_script_setplayrdir_alias");',
            'assign("mod_test_script_centercamera_alias", 0);',
            "centercamera();",
            'assign("mod_test_script_centercamera_alias", 1);',
            'assign("Emotion", 0);',
            'assign("emotion", 0);',
            'assign("Justice", 0);',
            'assign("justice", 0);',
            "playeraddemotion(2);",
            "playeraddemotion(-1);",
            "playeraddjustice(3);",
            "playeraddjustice(-2);",
            'assign("mod_test_script_emotion", getvar("Emotion"));',
            'assign("mod_test_script_emotion_alias", getvar("emotion"));',
            'assign("mod_test_script_justice", getvar("Justice"));',
            'assign("mod_test_script_justice_alias", getvar("justice"));',
            'getplayerstate("MapLoaded", "mod_test_script_freemap_loaded_before");',
            'getplayerstate("MapDataTileRows", "mod_test_script_freemap_rows_before");',
            'assign("mod_test_script_freemap_rows_before_positive", 0);',
            'if getvar("mod_test_script_freemap_rows_before") > 0 then assign("mod_test_script_freemap_rows_before_positive", 1) end',
            'delobj("MOD_TEST_OBJECT_RIGHT_ONLY");',
            'delnpc("MOD_TEST_INTERACT_NPC");',
            'addobj("mod_test_object_right_only_box.ini", 37, 20, 0);',
            'addnpc("mod_test_npc_interact.ini", 38, 20, 0);',
            'getobjstate("MOD_TEST_OBJECT_RIGHT_ONLY", "Exists", "mod_test_script_freemap_object_before");',
            'getnpcstate("MOD_TEST_INTERACT_NPC", "Exists", "mod_test_script_freemap_npc_before");',
            "freemap();",
            'getplayerstate("MapLoaded", "mod_test_script_freemap_loaded_after");',
            'getplayerstate("MapDataTileRows", "mod_test_script_freemap_rows_after");',
            'getobjstate("MOD_TEST_OBJECT_RIGHT_ONLY", "Exists", "mod_test_script_freemap_object_after");',
            'getnpcstate("MOD_TEST_INTERACT_NPC", "Exists", "mod_test_script_freemap_npc_after");',
            'loadmap("map001_衡山.map");',
            'displaymessage("时间脚本与并行脚本测试已启动");',
            "return;",
            "",
        ]
    )


def make_script_return_api_script() -> str:
    return "\n".join(
        [
            'displaymessage("脚本返回值接口测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            "cleargoods();",
            "clearmagic();",
            "setmoneynum(12345);",
            'assign("mod_test_return_money", getmoney());',
            'saveplayersnapshot("script-return-player");',
            "setmoneynum(77);",
            'assign("mod_test_return_player_snapshot_modified_money", getmoney());',
            'loadplayersnapshot("script-return-player");',
            'assign("mod_test_return_player_snapshot_money", getmoney());',
            "fulllife();",
            'assign("mod_test_return_player_full_life", getplayerstat("IsFullLife"));',
            'assign("mod_test_return_goods_space", hasgoodsfreespace());',
            'assign("mod_test_return_magic_space", hasmagicfreespace());',
            'addgoods("mod_test_goods_lifecycle_stack.ini", 2);',
            'assign("mod_test_return_goods_file_count", getgoodscountbyfile("mod_test_goods_lifecycle_stack.ini"));',
            'assign("mod_test_return_goods_name_count", getgoodscountbyname("MOD_TEST_GOODS_LIFECYCLE_STACK"));',
            'savegoodssnapshot("script-return-goods");',
            "cleargoods();",
            'assign("mod_test_return_goods_snapshot_cleared", getgoodscountbyfile("mod_test_goods_lifecycle_stack.ini"));',
            'loadgoodssnapshot("script-return-goods");',
            'assign("mod_test_return_goods_snapshot_restored", getgoodscountbyfile("mod_test_goods_lifecycle_stack.ini"));',
            'addmagic("mod_test_magic_goods_bound.ini");',
            'setmagiclevel("mod_test_magic_goods_bound.ini", 1);',
            'assign("mod_test_return_magic_level", getmagiclevel("mod_test_magic_goods_bound.ini"));',
            'loadmap("map001_衡山.map");',
            'getmapstate(0, 0, "HasMap", "mod_test_return_map_has_map");',
            'getmapstate(0, 0, "Width", "mod_test_return_map_width");',
            'assign("mod_test_return_map_width_positive", 0);',
            'if getvar("mod_test_return_map_width") > 0 then assign("mod_test_return_map_width_positive", 1) end',
            'getmapstate(0, 0, "Height", "mod_test_return_map_height");',
            'assign("mod_test_return_map_height_positive", 0);',
            'if getvar("mod_test_return_map_height") > 0 then assign("mod_test_return_map_height_positive", 1) end',
            'getmapstate(-1, -1, "IsInMap", "mod_test_return_map_outside");',
            'getmapstate(-1, -1, "Obstacle", "mod_test_return_map_outside_obstacle");',
            'loadnpc("map001.npc");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            'delnpc("MOD_TEST_SCRIPT_RETURN_LEECHCRAFT");',
            'addnpc("mod_test_script_return_leechcraft_npc.ini", 35, 21, 0);',
            'getnpcstate("MOD_TEST_SCRIPT_RETURN_LEECHCRAFT", "Leechcraft", "mod_test_return_leechcraft_required");',
            'assign("yiliao", 1);',
            'getleechcraftdifference("MOD_TEST_SCRIPT_RETURN_LEECHCRAFT", "mod_test_return_leechcraft_short");',
            'assign("yiliao", 3);',
            'getleechcraftdifference("MOD_TEST_SCRIPT_RETURN_LEECHCRAFT", "mod_test_return_leechcraft_success");',
            'getleechcraftdifference("MOD_TEST_SCRIPT_RETURN_MISSING", "mod_test_return_leechcraft_missing");',
            'assign("yiliao", 0);',
            'delnpc("MOD_TEST_SCRIPT_RETURN_LEECHCRAFT");',
            'delnpc("MOD_TEST_INTERACT_NPC");',
            'addnpc("mod_test_npc_interact.ini", 36, 21, 0);',
            'local npcX, npcY = getnpcpos("MOD_TEST_INTERACT_NPC");',
            'assign("mod_test_return_npc_x", npcX);',
            'assign("mod_test_return_npc_y", npcY);',
            'delobj("MOD_TEST_OBJECT_STATE");',
            'addobj("mod_test_object_state_box.ini", 37, 21, 0);',
            'local objX, objY = getobjpos("MOD_TEST_OBJECT_STATE");',
            'assign("mod_test_return_obj_x", objX);',
            'assign("mod_test_return_obj_y", objY);',
            'assign("mod_test_return_partner_index_non_negative", 0);',
            'if getpartnerindex() >= 0 then assign("mod_test_return_partner_index_non_negative", 1) end',
            'assign("mod_test_script_return_api_ready", 1);',
            'displaymessage("脚本返回值接口测试完成");',
            "return;",
            "",
        ]
    )


def make_script_timer_trigger_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_script_timer_triggered", 1);',
            'displaymessage("70 秒倒计时结束，时间脚本已触发");',
            "return;",
            "",
        ]
    )


def make_script_parallel_immediate_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_script_parallel_immediate", 1);',
            "return;",
            "",
        ]
    )


def make_script_parallel_delayed_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_script_parallel_delayed", 1);',
            "return;",
            "",
        ]
    )


def make_script_alias_run_script() -> str:
    return "\n".join(
        [
            'assign("mod_test_script_runscirpt_alias", 1);',
            "return;",
            "",
        ]
    )


def make_npc_ai_timer_context_script() -> str:
    return "\n".join(
        [
            'if getvar("mod_test_npc_ai_timer_context_done") == 1 then return end',
            'assign("mod_test_npc_ai_timer_context_ran", 1);',
            "setnpcdir();",
            'getnpcstate("MOD_TEST_NPC_AI_TIMER_CONTEXT", "Dir", "mod_test_npc_ai_timer_context_dir_zero");',
            "setnpcdir(5);",
            'getnpcstate("MOD_TEST_NPC_AI_TIMER_CONTEXT", "Dir", "mod_test_npc_ai_timer_context_dir_after");',
            "setnpcpos();",
            'getnpcstate("MOD_TEST_NPC_AI_TIMER_CONTEXT", "MapX", "mod_test_npc_ai_timer_context_x_zero");',
            'getnpcstate("MOD_TEST_NPC_AI_TIMER_CONTEXT", "MapY", "mod_test_npc_ai_timer_context_y_zero");',
            "setnpcpos(39, 20);",
            'getnpcstate("MOD_TEST_NPC_AI_TIMER_CONTEXT", "MapX", "mod_test_npc_ai_timer_context_x_after");',
            'getnpcstate("MOD_TEST_NPC_AI_TIMER_CONTEXT", "MapY", "mod_test_npc_ai_timer_context_y_after");',
            'getnpcstate("MOD_TEST_NPC_AI_TIMER_CONTEXT", "MapX", "mod_test_npc_ai_timer_context_x_final");',
            'getnpcstate("MOD_TEST_NPC_AI_TIMER_CONTEXT", "MapY", "mod_test_npc_ai_timer_context_y_final");',
            'getnpcstate("MOD_TEST_NPC_AI_TIMER_CONTEXT", "Dir", "mod_test_npc_ai_timer_context_dir_final");',
            "npcspecialaction();",
            "npcspecialactionex();",
            'assign("mod_test_npc_ai_timer_context_special_returned", 0);',
            'npcspecialaction("mpc049.asf");',
            'getnpcstate("MOD_TEST_NPC_AI_TIMER_CONTEXT", "IsInSpecialAction", "mod_test_npc_ai_timer_context_special_nonblocking_active");',
            'assign("mod_test_npc_ai_timer_context_special_returned", 1);',
            'assign("mod_test_npc_ai_timer_context_special_ex_returned", 0);',
            'npcspecialactionex("mpc049.asf");',
            'getnpcstate("MOD_TEST_NPC_AI_TIMER_CONTEXT", "IsInSpecialAction", "mod_test_npc_ai_timer_context_special_ex_after");',
            'assign("mod_test_npc_ai_timer_context_special_ex_returned", 1);',
            'assign("mod_test_npc_ai_timer_context_special_alias_returned", 0);',
            'npcspecialactionnonblocking("mpc049.asf");',
            'getnpcstate("MOD_TEST_NPC_AI_TIMER_CONTEXT", "IsInSpecialAction", "mod_test_npc_ai_timer_context_special_alias_active");',
            'assign("mod_test_npc_ai_timer_context_special_alias_returned", 1);',
            'assign("mod_test_npc_ai_timer_context_done", 1);',
            "return;",
            "",
        ]
    )


def make_npc_ai_script() -> str:
    return "\n".join(
        [
            'displaymessage("NPC 智能测试开始");',
            "loadgame(0);",
            'assign("mod_test_skip_base_newgame", 1);',
            'assign("mod_test_npc_ai_ready", 1);',
            'loadmap("map001_衡山.map");',
            'loadobj("map001.obj");',
            "disablenpcai();",
            "setplayerpos(34,20);",
            "setplayerdir(6);",
            'delnpc("MOD_TEST_NPC_NONE_FIGHTER");',
            'delnpc("MOD_TEST_NPC_FLYER");',
            'delnpc("MOD_TEST_NPC_AFRAID_ANIMAL");',
            'delnpc("MOD_TEST_NPC_PARTNER");',
            'delnpc("MOD_TEST_NPC_AI_STATE");',
            'delnpc("MOD_TEST_NPC_EVENT");',
            'delnpc("MOD_TEST_NPC_MOVER");',
            'delnpc("MOD_TEST_NPC_PATH_NORMAL");',
            'delnpc("MOD_TEST_NPC_PATH_EVENT");',
            'delnpc("MOD_TEST_NPC_PATH_ENEMY");',
            'delnpc("MOD_TEST_NPC_PATH_BEST");',
            'delnpc("MOD_TEST_NPC_PATH_FIXED");',
            'delnpc("MOD_TEST_NPC_DEST_BLOCKER");',
            'delnpc("MOD_TEST_NPC_AI_BLIND");',
            'delnpc("MOD_TEST_NPC_AI_GLOBAL");',
            'delnpc("MOD_TEST_NPC_AI_LOCAL");',
            'delnpc("MOD_TEST_NPC_AI_RANDWALK");',
            'delnpc("MOD_TEST_NPC_AI_TIMER_CONTEXT");',
            'delnpc("MOD_TEST_NPC_AI_NOADD_BODY");',
            'delnpc("MOD_TEST_NPC_AI_FIGHT_RES");',
            'delnpc("MOD_TEST_NPC_VISIBLE_VAR");',
            'delobj("MOD_TEST_NPC_AI_BODY");',
            'assign("mod_test_npc_ai_visible_gate", 0);',
            'getmapstate(37, 22, "NpcCount", "mod_test_npc_ai_visible_map_count_before");',
            'addnpc("mod_test_npc_ai_visible_variable.ini", 37, 22, 0);',
            'getnpcstate("MOD_TEST_NPC_VISIBLE_VAR", "Exists", "mod_test_npc_ai_visible_exists_hidden");',
            'getnpcstate("MOD_TEST_NPC_VISIBLE_VAR", "HasVisibleVariableName", "mod_test_npc_ai_visible_has_name");',
            'getnpcstate("MOD_TEST_NPC_VISIBLE_VAR", "VisibleVariableValue", "mod_test_npc_ai_visible_threshold");',
            'getnpcstate("MOD_TEST_NPC_VISIBLE_VAR", "IsVisibleByVariable", "mod_test_npc_ai_visible_hidden_by_var");',
            'getnpcstate("MOD_TEST_NPC_VISIBLE_VAR", "IsVisible", "mod_test_npc_ai_visible_hidden_runtime");',
            'getnpcstate("MOD_TEST_NPC_VISIBLE_VAR", "IsObstacle", "mod_test_npc_ai_visible_hidden_obstacle");',
            'getmapstate(37, 22, "NpcCount", "mod_test_npc_ai_visible_map_count_hidden");',
            'assign("mod_test_npc_ai_visible_hidden_not_mapped", 0);',
            'if getvar("mod_test_npc_ai_visible_map_count_hidden") == getvar("mod_test_npc_ai_visible_map_count_before") then assign("mod_test_npc_ai_visible_hidden_not_mapped", 1) end',
            'assign("$mod_test_npc_ai_visible_gate", 2);',
            "sleep(80);",
            'getnpcstate("MOD_TEST_NPC_VISIBLE_VAR", "IsVisibleByVariable", "mod_test_npc_ai_visible_after_var");',
            'getnpcstate("MOD_TEST_NPC_VISIBLE_VAR", "IsVisible", "mod_test_npc_ai_visible_runtime_after");',
            'getnpcstate("MOD_TEST_NPC_VISIBLE_VAR", "IsObstacle", "mod_test_npc_ai_visible_obstacle_after");',
            'getmapstate(37, 22, "NpcCount", "mod_test_npc_ai_visible_map_count_after");',
            'assign("mod_test_npc_ai_visible_mapped_after", 0);',
            'if getvar("mod_test_npc_ai_visible_map_count_after") == getvar("mod_test_npc_ai_visible_map_count_before") + 1 then assign("mod_test_npc_ai_visible_mapped_after", 1) end',
            'delnpc("MOD_TEST_NPC_VISIBLE_VAR");',
            'addnpc("mod_test_npc_ai_mover.ini", 31, 20, 0);',
            'getnpcstate("MOD_TEST_NPC_MOVER", "MapX", "mod_test_npc_ai_move_start_x");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "MapY", "mod_test_npc_ai_move_start_y");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "IsStanding", "mod_test_npc_ai_move_start_standing");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "CanWalkAction", "mod_test_npc_ai_move_can_walk");',
            'setnpcdestination("MOD_TEST_NPC_MOVER", 32, 20);',
            'getnpcstate("MOD_TEST_NPC_MOVER", "PathType", "mod_test_npc_ai_move_path_type");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "UsePathFinder", "mod_test_npc_ai_move_use_pathfinder");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "DestinationPathType", "mod_test_npc_ai_move_destination_path_type");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "DestinationUsesTemporaryDisableRestrict", "mod_test_npc_ai_move_destination_unrestricted");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "DestinationBlockedByCharacter", "mod_test_npc_ai_move_destination_blocked");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "MapX", "mod_test_npc_ai_move_after_set_x");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "MapY", "mod_test_npc_ai_move_after_set_y");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "StepState", "mod_test_npc_ai_move_step_state");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "StepListLength", "mod_test_npc_ai_move_step_list_len");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "StepTargetX", "mod_test_npc_ai_move_step_target_x");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "StepTargetY", "mod_test_npc_ai_move_step_target_y");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "StepOccupiedTileCount", "mod_test_npc_ai_move_step_occupied_count");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "StepLastMilliseconds", "mod_test_npc_ai_move_step_last");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "IsSmoothStepMoving", "mod_test_npc_ai_move_smooth_after_set");',
            'assign("mod_test_npc_ai_move_step_last_positive", 0);',
            'if getvar("mod_test_npc_ai_move_step_last") > 0 then assign("mod_test_npc_ai_move_step_last_positive", 1) end',
            'getnpcstate("MOD_TEST_NPC_MOVER", "DestinationPathLength", "mod_test_npc_ai_move_path_len");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "DestinationFirstStepCanWalk", "mod_test_npc_ai_move_first_step");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "CurrentAction", "mod_test_npc_ai_move_action");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "IsWalking", "mod_test_npc_ai_move_is_walking");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "DestinationMapPosX", "mod_test_npc_ai_move_dest_x");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "DestinationMapPosY", "mod_test_npc_ai_move_dest_y");',
            "sleep(40);",
            'getnpcstate("MOD_TEST_NPC_MOVER", "MapX", "mod_test_npc_ai_move_mid_x");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "MapY", "mod_test_npc_ai_move_mid_y");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "StepProgressPermille", "mod_test_npc_ai_move_mid_progress");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "HasNonZeroOffset", "mod_test_npc_ai_move_mid_offset");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "IsSmoothStepMoving", "mod_test_npc_ai_move_mid_smooth");',
            'assign("mod_test_npc_ai_move_mid_progress_started", 0);',
            'if getvar("mod_test_npc_ai_move_mid_progress") > 0 then assign("mod_test_npc_ai_move_mid_progress_started", 1) end',
            'assign("mod_test_npc_ai_move_mid_progress_not_finished", 0);',
            'if getvar("mod_test_npc_ai_move_mid_progress") < 1000 then assign("mod_test_npc_ai_move_mid_progress_not_finished", 1) end',
            "sleep(300);",
            'getnpcstate("MOD_TEST_NPC_MOVER", "MapX", "mod_test_npc_ai_move_end_x");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "MapY", "mod_test_npc_ai_move_end_y");',
            "sleep(250);",
            'getnpcstate("MOD_TEST_NPC_MOVER", "IsStanding", "mod_test_npc_ai_move_standing_after_arrival");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "HasDestination", "mod_test_npc_ai_move_destination_after_arrival");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "DestinationMapPosX", "mod_test_npc_ai_move_dest_x_after_arrival");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "DestinationMapPosY", "mod_test_npc_ai_move_dest_y_after_arrival");',
            'setnpcdestination("MOD_TEST_NPC_MOVER", 31, 20);',
            'getnpcstate("MOD_TEST_NPC_MOVER", "IsWalking", "mod_test_npc_ai_action1_canonical_started");',
            'setnpcaction("MOD_TEST_NPC_MOVER", 1);',
            'getnpcstate("MOD_TEST_NPC_MOVER", "IsStanding", "mod_test_npc_ai_action1_canonical_standing");',
            'setnpcdestination("MOD_TEST_NPC_MOVER", 31, 20);',
            'getnpcstate("MOD_TEST_NPC_MOVER", "IsWalking", "mod_test_npc_ai_action1_alias_started");',
            'npcaction("MOD_TEST_NPC_MOVER", 1);',
            'getnpcstate("MOD_TEST_NPC_MOVER", "IsStanding", "mod_test_npc_ai_action1_alias_standing");',
            'delnpc("MOD_TEST_NPC_MOVER");',
            'addnpc("mod_test_npc_ai_path_normal.ini", 31, 20, 0);',
            'addnpc("mod_test_npc_ai_path_event.ini", 32, 20, 0);',
            'addnpc("mod_test_npc_ai_path_enemy.ini", 33, 20, 0);',
            'addnpc("mod_test_npc_ai_path_best.ini", 34, 20, 0);',
            'addnpc("mod_test_npc_ai_path_fixed.ini", 35, 20, 0);',
            'getnpcstate("MOD_TEST_NPC_PATH_NORMAL", "PathType", "mod_test_npc_ai_path_normal_type");',
            'getnpcstate("MOD_TEST_NPC_PATH_EVENT", "PathType", "mod_test_npc_ai_path_event_type");',
            'getnpcstate("MOD_TEST_NPC_PATH_ENEMY", "PathType", "mod_test_npc_ai_path_enemy_type");',
            'getnpcstate("MOD_TEST_NPC_PATH_BEST", "PathType", "mod_test_npc_ai_path_best_type");',
            'getnpcstate("MOD_TEST_NPC_PATH_FIXED", "PathType", "mod_test_npc_ai_path_fixed_type");',
            'getnpcstate("MOD_TEST_NPC_PATH_FIXED", "HasFixedPath", "mod_test_npc_ai_path_fixed_has_path");',
            'getnpcstate("MOD_TEST_NPC_PATH_FIXED", "FixedPathCount", "mod_test_npc_ai_path_fixed_count");',
            'getnpcstate("MOD_TEST_NPC_PATH_FIXED", "CurrentFixedPosIndex", "mod_test_npc_ai_path_fixed_currpos");',
            'getnpcstate("MOD_TEST_NPC_PATH_NORMAL", "PathSearchMaxTry", "mod_test_npc_ai_path_normal_maxtry");',
            'getnpcstate("MOD_TEST_NPC_PATH_ENEMY", "PathSearchMaxTry", "mod_test_npc_ai_path_enemy_maxtry");',
            'getnpcstate("MOD_TEST_NPC_PATH_BEST", "PathSearchMaxTry", "mod_test_npc_ai_path_best_maxtry");',
            'getnpcstate("MOD_TEST_NPC_PATH_NORMAL", "UsePathFinder", "mod_test_npc_ai_path_normal_use_pathfinder");',
            'getnpcstate("MOD_TEST_NPC_PATH_EVENT", "UsePathFinder", "mod_test_npc_ai_path_event_use_pathfinder");',
            'getnpcstate("MOD_TEST_NPC_PATH_ENEMY", "UsePathFinder", "mod_test_npc_ai_path_enemy_use_pathfinder");',
            'getnpcstate("MOD_TEST_NPC_PATH_BEST", "UsePathFinder", "mod_test_npc_ai_path_best_use_pathfinder");',
            'getnpcstate("MOD_TEST_NPC_PATH_FIXED", "UsePathFinder", "mod_test_npc_ai_path_fixed_use_pathfinder");',
            'delnpc("MOD_TEST_NPC_PATH_NORMAL");',
            'delnpc("MOD_TEST_NPC_PATH_EVENT");',
            'delnpc("MOD_TEST_NPC_PATH_ENEMY");',
            'delnpc("MOD_TEST_NPC_PATH_BEST");',
            'delnpc("MOD_TEST_NPC_PATH_FIXED");',
            'addnpc("mod_test_npc_ai_mover.ini", 31, 20, 0);',
            'addnpc("mod_test_npc_ai_destination_blocker.ini", 33, 20, 0);',
            'setnpcdestination("MOD_TEST_NPC_MOVER", 33, 20);',
            'getnpcstate("MOD_TEST_NPC_MOVER", "DestinationBlockedByCharacter", "mod_test_npc_ai_blocked_destination_blocked");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "DestinationPathType", "mod_test_npc_ai_blocked_destination_path_type");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "DestinationMoveTilePosition", "mod_test_npc_ai_blocked_destination_exists");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "DestinationFirstStepCanWalk", "mod_test_npc_ai_blocked_first_step");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "DestinationPathLength", "mod_test_npc_ai_blocked_path_len");',
            "sleep(700);",
            'getnpcstate("MOD_TEST_NPC_MOVER", "MapX", "mod_test_npc_ai_blocked_end_x");',
            'getnpcstate("MOD_TEST_NPC_MOVER", "DestinationMapPosX", "mod_test_npc_ai_blocked_dest_x");',
            'delnpc("MOD_TEST_NPC_MOVER");',
            'delnpc("MOD_TEST_NPC_DEST_BLOCKER");',
            'addnpc("mod_test_npc_ai_none_fighter.ini", 31, 20, 0);',
            'addnpc("mod_test_npc_ai_flyer.ini", 32, 20, 0);',
            'addnpc("mod_test_npc_ai_afraid_animal.ini", 36, 20, 0);',
            'addnpc("mod_test_npc_ai_partner.ini", 33, 20, 0);',
            'addnpc("mod_test_npc_ai_state.ini", 34, 20, 0);',
            'addnpc("mod_test_npc_ai_event.ini", 35, 20, 0);',
            'getnpcstate("MOD_TEST_NPC_NONE_FIGHTER", "Kind", "mod_test_npc_ai_none_kind");',
            'getnpcstate("MOD_TEST_NPC_NONE_FIGHTER", "Relation", "mod_test_npc_ai_none_relation");',
            'getnpcstate("MOD_TEST_NPC_NONE_FIGHTER", "Attack", "mod_test_npc_ai_none_attack");',
            'getnpcstate("MOD_TEST_NPC_NONE_FIGHTER", "Group", "mod_test_npc_ai_none_group");',
            'getnpcstate("MOD_TEST_NPC_NONE_FIGHTER", "NoAutoAttackPlayer", "mod_test_npc_ai_none_noauto");',
            'getnpcstate("MOD_TEST_NPC_NONE_FIGHTER", "StopFindingTarget", "mod_test_npc_ai_none_stopfind");',
            'getnpcstate("MOD_TEST_NPC_NONE_FIGHTER", "IsNoneFighter", "mod_test_npc_ai_none_is_none_fighter");',
            'getnpcstate("MOD_TEST_NPC_NONE_FIGHTER", "IsEnemy", "mod_test_npc_ai_none_is_enemy");',
            'getnpcstate("MOD_TEST_NPC_NONE_FIGHTER", "IsFighter", "mod_test_npc_ai_none_is_fighter");',
            'getnpcstate("MOD_TEST_NPC_NONE_FIGHTER", "IsFighterKind", "mod_test_npc_ai_none_is_fighter_kind");',
            'getnpcstate("MOD_TEST_NPC_NONE_FIGHTER", "IsInteractive", "mod_test_npc_ai_none_is_interactive");',
            'getnpcstate("MOD_TEST_NPC_NONE_FIGHTER", "IsObstacle", "mod_test_npc_ai_none_is_obstacle");',
            'getmapstate(31, 20, "CanWalk", "mod_test_npc_ai_none_tile_can_walk");',
            'getnpcstate("MOD_TEST_NPC_FLYER", "Kind", "mod_test_npc_ai_flyer_kind");',
            'getnpcstate("MOD_TEST_NPC_FLYER", "Relation", "mod_test_npc_ai_flyer_relation");',
            'getnpcstate("MOD_TEST_NPC_FLYER", "IsObstacle", "mod_test_npc_ai_flyer_is_obstacle");',
            'getnpcstate("MOD_TEST_NPC_FLYER", "IsFighter", "mod_test_npc_ai_flyer_is_fighter");',
            'getnpcstate("MOD_TEST_NPC_FLYER", "IsInteractive", "mod_test_npc_ai_flyer_is_interactive");',
            'getmapstate(32, 20, "CanWalk", "mod_test_npc_ai_flyer_tile_can_walk");',
            'getnpcstate("MOD_TEST_NPC_FLYER", "PathType", "mod_test_npc_ai_flyer_path_type");',
            'getnpcstate("MOD_TEST_NPC_FLYER", "UsePathFinder", "mod_test_npc_ai_flyer_use_pathfinder");',
            'npcgotoex("MOD_TEST_NPC_FLYER", 34, 20);',
            'getnpcstate("MOD_TEST_NPC_FLYER", "StepListLength", "mod_test_npc_ai_flyer_step_list_len");',
            'getnpcstate("MOD_TEST_NPC_FLYER", "StepTargetX", "mod_test_npc_ai_flyer_step_target_x");',
            'getnpcstate("MOD_TEST_NPC_FLYER", "StepTargetY", "mod_test_npc_ai_flyer_step_target_y");',
            'getnpcstate("MOD_TEST_NPC_FLYER", "IsWalking", "mod_test_npc_ai_flyer_is_walking");',
            'getnpcstate("MOD_TEST_NPC_AFRAID_ANIMAL", "Kind", "mod_test_npc_ai_afraid_kind");',
            'getnpcstate("MOD_TEST_NPC_AFRAID_ANIMAL", "Relation", "mod_test_npc_ai_afraid_relation");',
            'getnpcstate("MOD_TEST_NPC_AFRAID_ANIMAL", "IsObstacle", "mod_test_npc_ai_afraid_is_obstacle");',
            'getnpcstate("MOD_TEST_NPC_AFRAID_ANIMAL", "IsFighter", "mod_test_npc_ai_afraid_is_fighter");',
            'getnpcstate("MOD_TEST_NPC_AFRAID_ANIMAL", "IsInteractive", "mod_test_npc_ai_afraid_is_interactive");',
            'getnpcstate("MOD_TEST_NPC_AFRAID_ANIMAL", "PathType", "mod_test_npc_ai_afraid_path_type");',
            'getnpcstate("MOD_TEST_NPC_AFRAID_ANIMAL", "UsePathFinder", "mod_test_npc_ai_afraid_use_pathfinder");',
            'getnpcstate("MOD_TEST_NPC_EVENT", "Kind", "mod_test_npc_ai_event_kind");',
            'getnpcstate("MOD_TEST_NPC_EVENT", "IsEventCharacter", "mod_test_npc_ai_event_is_event");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "Kind", "mod_test_npc_ai_partner_kind_before");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "Relation", "mod_test_npc_ai_partner_relation_before");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "IsFighterFriend", "mod_test_npc_ai_partner_is_fighter_friend_before");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "IsPartner", "mod_test_npc_ai_partner_is_partner_before");',
            'setnpcdestination("MOD_TEST_NPC_PARTNER", 33, 21);',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "HasDestination", "mod_test_npc_ai_partner_destination_before");',
            'setnpcpartner("MOD_TEST_NPC_PARTNER");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "Kind", "mod_test_npc_ai_partner_kind_after");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "Relation", "mod_test_npc_ai_partner_relation_after");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "IsFighterFriend", "mod_test_npc_ai_partner_is_fighter_friend_after");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "IsPartner", "mod_test_npc_ai_partner_is_partner_after");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "IsFighter", "mod_test_npc_ai_partner_is_fighter_after");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "HasDestination", "mod_test_npc_ai_partner_destination_after");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "IsStanding", "mod_test_npc_ai_partner_standing_after");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "PathType", "mod_test_npc_ai_partner_path_type_after");',
            'getnpcstate("MOD_TEST_NPC_PARTNER", "UsePathFinder", "mod_test_npc_ai_partner_use_pathfinder_after");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "AIType", "mod_test_npc_ai_state_ai_type");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "IsRandMoveRandAttack", "mod_test_npc_ai_state_is_rand_move");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "IsNotFightBackWhenBeHit", "mod_test_npc_ai_state_no_fightback");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "Idle", "mod_test_npc_ai_state_idle");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "IdledFrame", "mod_test_npc_ai_state_idled_frame");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "AttackSpeed", "mod_test_npc_ai_state_attack_speed");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasAttackSpeed", "mod_test_npc_ai_state_has_attack_speed");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "Group", "mod_test_npc_ai_state_group");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasShowName", "mod_test_npc_ai_state_has_show_name");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "DisplayNameUsesShowName", "mod_test_npc_ai_state_display_uses_show_name");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "Life", "mod_test_npc_ai_state_life");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "LifeMax", "mod_test_npc_ai_state_lifemax");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "RealAttack", "mod_test_npc_ai_state_real_attack");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "RealDefend", "mod_test_npc_ai_state_real_defend");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "RealEvade", "mod_test_npc_ai_state_real_evade");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "IsLifeLow", "mod_test_npc_ai_state_life_low");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "IsFrozened", "mod_test_npc_ai_state_is_frozened");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "IsPoisoned", "mod_test_npc_ai_state_is_poisoned");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "IsPetrified", "mod_test_npc_ai_state_is_petrified");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "IsDraw", "mod_test_npc_ai_state_is_draw");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "BodyFunctionWell", "mod_test_npc_ai_state_body_function_well");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "IsInSpecialAction", "mod_test_npc_ai_state_in_special_action");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "IsDeath", "mod_test_npc_ai_state_is_death");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "IsDeathScriptEnd", "mod_test_npc_ai_state_death_script_end");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "IsBodyIniOk", "mod_test_npc_ai_state_body_ini_ok");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "IsMagicFromCache", "mod_test_npc_ai_state_magic_from_cache");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "IsFullLife", "mod_test_npc_ai_state_full_life");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "IsInFighting", "mod_test_npc_ai_state_in_fighting");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "ReviveMilliseconds", "mod_test_npc_ai_state_revive");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "LifeMilliseconds", "mod_test_npc_ai_state_life_ms");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "LifeLowPercent", "mod_test_npc_ai_state_life_low_percent");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "KeepRadiusWhenLifeLow", "mod_test_npc_ai_state_keep_life_low");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "KeepRadiusWhenFriendDeath", "mod_test_npc_ai_state_keep_friend_death");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "MagicToUseWhenLifeLowLoaded", "mod_test_npc_ai_state_life_low_magic");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "MagicToUseWhenBeAttackedLoaded", "mod_test_npc_ai_state_beattacked_magic");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "MagicDirectionWhenBeAttacked", "mod_test_npc_ai_state_beattacked_dir");',
            'setnpcmagictousewhenbeattacked("MOD_TEST_NPC_AI_STATE", "mod_test_magic_equipment_counter.ini", 1);',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "MagicToUseWhenBeAttackedLoaded", "mod_test_npc_ai_state_beattacked_magic_script");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "MagicDirectionWhenBeAttacked", "mod_test_npc_ai_state_beattacked_dir_script");',
            'setplayermagictousewhenbeattacked("mod_test_magic_equipment_counter.ini", 2);',
            'getplayerstate("MagicToUseWhenBeAttackedLoaded", "mod_test_player_beattacked_magic_script");',
            'getplayerstate("MagicDirectionWhenBeAttacked", "mod_test_player_beattacked_dir_script");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "MagicToUseWhenDeathLoaded", "mod_test_npc_ai_state_death_magic");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "MagicDirectionWhenDeath", "mod_test_npc_ai_state_death_dir");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HurtPlayerInterval", "mod_test_npc_ai_state_hurt_interval");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HurtPlayerLife", "mod_test_npc_ai_state_hurt_life");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HurtPlayerRadius", "mod_test_npc_ai_state_hurt_radius");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "CanEquip", "mod_test_npc_ai_state_can_equip");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "CanLevelUp", "mod_test_npc_ai_state_can_levelup");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "LevelUpExp", "mod_test_npc_ai_state_levelup_exp");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "AutoRunScript", "mod_test_npc_ai_state_auto_run_script");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasAutoRunScript", "mod_test_npc_ai_state_has_auto_run_script");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "Arm", "mod_test_npc_ai_state_arm");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasArm", "mod_test_npc_ai_state_has_arm");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "EvadeN", "mod_test_npc_ai_state_evaden");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasEvadeN", "mod_test_npc_ai_state_has_evaden");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "Gengu", "mod_test_npc_ai_state_gengu");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasGengu", "mod_test_npc_ai_state_has_gengu");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "Neixi", "mod_test_npc_ai_state_neixi");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasNeixi", "mod_test_npc_ai_state_has_neixi");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "Physique", "mod_test_npc_ai_state_physique");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasPhysique", "mod_test_npc_ai_state_has_physique");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "Dodge_BeginFrame", "mod_test_npc_ai_state_dodge_begin");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasDodge_BeginFrame", "mod_test_npc_ai_state_has_dodge_begin");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "Dodge_EndFrame", "mod_test_npc_ai_state_dodge_end");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasDodge_EndFrame", "mod_test_npc_ai_state_has_dodge_end");',
            'displaymessage("正在测试 NPC 资源回退");',
            'delnpc("MOD_TEST_NPC_AI_NPCRES_FALLBACK");',
            'delnpc("MOD_TEST_NPC_AI_NPCRES_PRIORITY");',
            'addnpc("mod_test_npc_ai_npcres_fallback_npc.ini", 35, 20, 0);',
            'getnpcstate("MOD_TEST_NPC_AI_NPCRES_FALLBACK", "CanWalkAction", "mod_test_npc_ai_npcres_fallback_walk");',
            'delnpc("MOD_TEST_NPC_AI_NPCRES_FALLBACK");',
            'addnpc("mod_test_npc_ai_npcres_priority_npc.ini", 35, 20, 0);',
            'getnpcstate("MOD_TEST_NPC_AI_NPCRES_PRIORITY", "CanWalkAction", "mod_test_npc_ai_npcres_priority_walk");',
            'delnpc("MOD_TEST_NPC_AI_NPCRES_PRIORITY");',
            'addnpc("mod_test_npc_ai_fight_res_npc.ini", 35, 20, 0);',
            'getnpcstate("MOD_TEST_NPC_AI_FIGHT_RES", "CanFightStandAction", "mod_test_npc_ai_fight_res_stand");',
            'getnpcstate("MOD_TEST_NPC_AI_FIGHT_RES", "CanFightWalkAction", "mod_test_npc_ai_fight_res_walk");',
            'getnpcstate("MOD_TEST_NPC_AI_FIGHT_RES", "CanFightRunAction", "mod_test_npc_ai_fight_res_run");',
            'getnpcstate("MOD_TEST_NPC_AI_FIGHT_RES", "CanFightJumpAction", "mod_test_npc_ai_fight_res_jump");',
            'getnpcstate("MOD_TEST_NPC_AI_FIGHT_RES", "CanMoveDirCount", "mod_test_npc_ai_fight_res_move_dir");',
            'assign("mod_test_npc_ai_fight_res_move_dir_positive", 0);',
            'if getvar("mod_test_npc_ai_fight_res_move_dir") > 0 then assign("mod_test_npc_ai_fight_res_move_dir_positive", 1) end',
            'getnpcstate("MOD_TEST_NPC_AI_FIGHT_RES", "CanJumpDirCount", "mod_test_npc_ai_fight_res_jump_dir");',
            'assign("mod_test_npc_ai_fight_res_jump_dir_positive", 0);',
            'if getvar("mod_test_npc_ai_fight_res_jump_dir") > 0 then assign("mod_test_npc_ai_fight_res_jump_dir_positive", 1) end',
            'delnpc("MOD_TEST_NPC_AI_FIGHT_RES");',
            'delnpc("MOD_TEST_NPC_EVENT");',
            'displaymessage("正在测试 NPC 失明与全局开关");',
            'delnpc("MOD_TEST_NPC_AI_BLIND");',
            'delnpc("MOD_TEST_NPC_AI_GLOBAL");',
            'delnpc("MOD_TEST_NPC_AI_LOCAL");',
            "setplayerpos(34,20);",
            'addnpc("mod_test_npc_ai_blind.ini", 35, 20, 0);',
            'addnpcproperty("MOD_TEST_NPC_AI_BLIND", "DisableMoveMilliseconds", 900);',
            'addnpcproperty("MOD_TEST_NPC_AI_BLIND", "DisableSkillMilliseconds", 900);',
            'addnpcproperty("MOD_TEST_NPC_AI_BLIND", "BlindMilliseconds", 1200);',
            'getnpcstate("MOD_TEST_NPC_AI_BLIND", "IsMoveDisabled", "mod_test_npc_ai_blind_move_disabled");',
            'getnpcstate("MOD_TEST_NPC_AI_BLIND", "CanWalkAction", "mod_test_npc_ai_blind_can_walk");',
            'getnpcstate("MOD_TEST_NPC_AI_BLIND", "IsSkillDisabled", "mod_test_npc_ai_blind_skill_disabled");',
            'getnpcstate("MOD_TEST_NPC_AI_BLIND", "IsBlind", "mod_test_npc_ai_blind_is_blind");',
            'getnpcstate("MOD_TEST_NPC_AI_BLIND", "CanSeePlayer", "mod_test_npc_ai_blind_can_see_player");',
            "enablenpcai();",
            "sleep(300);",
            'getnpcstate("MOD_TEST_NPC_AI_BLIND", "HasCurrentCombatTarget", "mod_test_npc_ai_blind_has_target");',
            "sleep(1300);",
            'getnpcstate("MOD_TEST_NPC_AI_BLIND", "IsBlind", "mod_test_npc_ai_blind_expired");',
            'getnpcstate("MOD_TEST_NPC_AI_BLIND", "CanSeePlayer", "mod_test_npc_ai_blind_can_see_after");',
            "sleep(700);",
            'getnpcstate("MOD_TEST_NPC_AI_BLIND", "HasCurrentCombatTarget", "mod_test_npc_ai_blind_reacquired_target");',
            "disablenpcai();",
            'delnpc("MOD_TEST_NPC_AI_BLIND");',
            "setplayerpos(34,20);",
            'addnpc("mod_test_npc_ai_global_ai.ini", 35, 20, 0);',
            "disablenpcai();",
            "sleep(300);",
            'getnpcstate("MOD_TEST_NPC_AI_GLOBAL", "IsAIDisabled", "mod_test_npc_ai_global_disabled");',
            'getnpcstate("MOD_TEST_NPC_AI_GLOBAL", "HasCurrentCombatTarget", "mod_test_npc_ai_global_disabled_has_target");',
            "enablenpcai();",
            "sleep(700);",
            'getnpcstate("MOD_TEST_NPC_AI_GLOBAL", "IsAIDisabled", "mod_test_npc_ai_global_enabled");',
            'getnpcstate("MOD_TEST_NPC_AI_GLOBAL", "HasCurrentCombatTarget", "mod_test_npc_ai_global_enabled_has_target");',
            "disablenpcai();",
            'delnpc("MOD_TEST_NPC_AI_GLOBAL");',
            "setplayerpos(34,20);",
            'addnpc("mod_test_npc_ai_local_ai.ini", 35, 20, 0);',
            "enablenpcai();",
            'disablenpcai("MOD_TEST_NPC_AI_LOCAL");',
            'getnpcstate("MOD_TEST_NPC_AI_LOCAL", "IsLocalAIDisabled", "mod_test_npc_ai_local_disabled_local");',
            'getnpcstate("MOD_TEST_NPC_AI_LOCAL", "IsAIDisabled", "mod_test_npc_ai_local_disabled_effective");',
            'getnpcstate("MOD_TEST_NPC_AI_LOCAL", "AIEnabled", "mod_test_npc_ai_local_disabled_ai_enabled");',
            "sleep(300);",
            'getnpcstate("MOD_TEST_NPC_AI_LOCAL", "HasCurrentCombatTarget", "mod_test_npc_ai_local_disabled_has_target");',
            'enablenpcai("MOD_TEST_NPC_AI_LOCAL");',
            "sleep(700);",
            'getnpcstate("MOD_TEST_NPC_AI_LOCAL", "IsLocalAIDisabled", "mod_test_npc_ai_local_enabled_local");',
            'getnpcstate("MOD_TEST_NPC_AI_LOCAL", "IsAIDisabled", "mod_test_npc_ai_local_enabled_effective");',
            'getnpcstate("MOD_TEST_NPC_AI_LOCAL", "AIEnabled", "mod_test_npc_ai_local_enabled_ai_enabled");',
            'getnpcstate("MOD_TEST_NPC_AI_LOCAL", "HasCurrentCombatTarget", "mod_test_npc_ai_local_enabled_has_target");',
            "disablenpcai();",
            'delnpc("MOD_TEST_NPC_AI_LOCAL");',
            'addnpc("mod_test_npc_ai_randwalk.ini", 36, 20, 0);',
            'getnpcstate("MOD_TEST_NPC_AI_RANDWALK", "RandWalkPathLength", "mod_test_npc_ai_randwalk_path_before");',
            'assign("mod_test_npc_ai_randwalk_path_before_empty", 0);',
            'if getvar("mod_test_npc_ai_randwalk_path_before") == 0 then assign("mod_test_npc_ai_randwalk_path_before_empty", 1) end',
            "enablenpcai();",
            'getnpcstate("MOD_TEST_NPC_AI_RANDWALK", "EnsureRandWalkPathLength", "mod_test_npc_ai_randwalk_path_after");',
            'assign("mod_test_npc_ai_randwalk_path_ready", 0);',
            'if getvar("mod_test_npc_ai_randwalk_path_after") > 1 then assign("mod_test_npc_ai_randwalk_path_ready", 1) end',
            "disablenpcai();",
            'delnpc("MOD_TEST_NPC_AI_RANDWALK");',
            'displaymessage("正在测试友方忽略真正中立目标");',
            'delnpc("MOD_TEST_NPC_NONE_FIGHTER");',
            'delnpc("MOD_TEST_NPC_FLYER");',
            'delnpc("MOD_TEST_NPC_AFRAID_ANIMAL");',
            'delnpc("MOD_TEST_NPC_PARTNER");',
            'delnpc("MOD_TEST_NPC_AI_STATE");',
            'delnpc("MOD_TEST_NPC_AI_FRIEND_NEUTRAL_ATTACKER");',
            'delnpc("MOD_TEST_NPC_AI_TRUE_NEUTRAL_TARGET");',
            "setplayerpos(30,23);",
            'addnpc("mod_test_npc_ai_friendly_neutral_attacker.ini", 34, 20, 0);',
            'addnpc("mod_test_npc_ai_true_neutral_target.ini", 35, 20, 0);',
            'getnpcstate("MOD_TEST_NPC_AI_TRUE_NEUTRAL_TARGET", "Life", "mod_test_npc_ai_true_neutral_life_before");',
            "enablenpcai();",
            "sleep(900);",
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_NEUTRAL_ATTACKER", "HasCurrentCombatTarget", "mod_test_npc_ai_friend_neutral_has_target");',
            'getnpcstate("MOD_TEST_NPC_AI_TRUE_NEUTRAL_TARGET", "Life", "mod_test_npc_ai_true_neutral_life_after");',
            'assign("mod_test_npc_ai_friend_ignored_true_neutral", 0);',
            'if getvar("mod_test_npc_ai_friend_neutral_has_target") == 0 and getvar("mod_test_npc_ai_true_neutral_life_after") == getvar("mod_test_npc_ai_true_neutral_life_before") then assign("mod_test_npc_ai_friend_ignored_true_neutral", 1) end',
            "disablenpcai();",
            'delnpc("MOD_TEST_NPC_AI_FRIEND_NEUTRAL_ATTACKER");',
            'delnpc("MOD_TEST_NPC_AI_TRUE_NEUTRAL_TARGET");',
            'displaymessage("正在测试友方选择非战斗目标");',
            'delnpc("MOD_TEST_NPC_AI_FRIEND_NEUTRAL_ATTACKER");',
            'delnpc("MOD_TEST_NPC_AI_FRIEND_NONE_TARGET");',
            "setplayerpos(30,23);",
            'addnpc("mod_test_npc_ai_friendly_neutral_attacker.ini", 34, 20, 0);',
            'addnpc("mod_test_npc_ai_friend_none_target.ini", 35, 20, 0);',
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_NONE_TARGET", "Relation", "mod_test_npc_ai_friend_none_relation");',
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_NONE_TARGET", "IsNoneFighter", "mod_test_npc_ai_friend_none_is_none_fighter");',
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_NONE_TARGET", "Life", "mod_test_npc_ai_friend_none_life_before");',
            "enablenpcai();",
            "sleep(5000);",
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_NEUTRAL_ATTACKER", "HasCurrentCombatTarget", "mod_test_npc_ai_friend_none_has_target");',
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_NONE_TARGET", "Life", "mod_test_npc_ai_friend_none_life_after");',
            'assign("mod_test_npc_ai_friend_hit_none_fighter", 0);',
            'if getvar("mod_test_npc_ai_friend_none_life_after") < getvar("mod_test_npc_ai_friend_none_life_before") then assign("mod_test_npc_ai_friend_hit_none_fighter", 1) end',
            "disablenpcai();",
            'delnpc("MOD_TEST_NPC_AI_FRIEND_NEUTRAL_ATTACKER");',
            'delnpc("MOD_TEST_NPC_AI_FRIEND_NONE_TARGET");',
            'displaymessage("正在测试 NPC 动态行为");',
            'delnpc("MOD_TEST_NPC_AFRAID_ANIMAL");',
            "setplayerpos(34,20);",
            'addnpc("mod_test_npc_ai_afraid_animal.ini", 35, 20, 0);',
            'getnpcstate("MOD_TEST_NPC_AFRAID_ANIMAL", "MapX", "mod_test_npc_ai_afraid_disabled_x");',
            'getnpcstate("MOD_TEST_NPC_AFRAID_ANIMAL", "StepTargetX", "mod_test_npc_ai_afraid_disabled_step_x");',
            'getnpcstate("MOD_TEST_NPC_AFRAID_ANIMAL", "IsWalking", "mod_test_npc_ai_afraid_disabled_walking");',
            'assign("mod_test_npc_ai_afraid_disabled_still", 1);',
            'if getvar("mod_test_npc_ai_afraid_disabled_walking") == 1 then assign("mod_test_npc_ai_afraid_disabled_still", 0) end',
            'if getvar("mod_test_npc_ai_afraid_disabled_x") ~= 35 then assign("mod_test_npc_ai_afraid_disabled_still", 0) end',
            "enablenpcai();",
            "sleep(320);",
            'getnpcstate("MOD_TEST_NPC_AFRAID_ANIMAL", "CurrentAction", "mod_test_npc_ai_afraid_action_raw");',
            'getnpcstate("MOD_TEST_NPC_AFRAID_ANIMAL", "MapX", "mod_test_npc_ai_afraid_map_x");',
            'getnpcstate("MOD_TEST_NPC_AFRAID_ANIMAL", "StepTargetX", "mod_test_npc_ai_afraid_step_x");',
            'getnpcstate("MOD_TEST_NPC_AFRAID_ANIMAL", "IsWalking", "mod_test_npc_ai_afraid_is_walking");',
            'assign("mod_test_npc_ai_afraid_started", 0);',
            'if getvar("mod_test_npc_ai_afraid_is_walking") == 1 then assign("mod_test_npc_ai_afraid_started", 1) end',
            'if getvar("mod_test_npc_ai_afraid_action_raw") == 2 then assign("mod_test_npc_ai_afraid_started", 1) end',
            'if getvar("mod_test_npc_ai_afraid_action_raw") == 21 then assign("mod_test_npc_ai_afraid_started", 1) end',
            'if getvar("mod_test_npc_ai_afraid_map_x") > 35 then assign("mod_test_npc_ai_afraid_started", 1) end',
            'if getvar("mod_test_npc_ai_afraid_step_x") > 35 then assign("mod_test_npc_ai_afraid_started", 1) end',
            "disablenpcai();",
            'delnpc("MOD_TEST_NPC_AFRAID_ANIMAL");',
            'delnpc("MOD_TEST_NPC_AI_RETREAT");',
            'delnpc("MOD_TEST_NPC_AI_LOW_MAGIC");',
            'delnpc("MOD_TEST_NPC_AI_REVIVE");',
            'delnpc("MOD_TEST_NPC_AI_NOADD_BODY");',
            "setplayerpos(34,20);",
            'addnpc("mod_test_npc_ai_retreat.ini", 35, 20, 0);',
            "enablenpcai();",
            "sleep(260);",
            'getnpcstate("MOD_TEST_NPC_AI_RETREAT", "HasCurrentCombatTarget", "mod_test_npc_ai_retreat_has_target");',
            'getnpcstate("MOD_TEST_NPC_AI_RETREAT", "CurrentCombatTargetIsPlayer", "mod_test_npc_ai_retreat_target_player");',
            'getnpcstate("MOD_TEST_NPC_AI_RETREAT", "IsFollowTargetFound", "mod_test_npc_ai_retreat_follow_found");',
            'getnpcstate("MOD_TEST_NPC_AI_RETREAT", "FollowTarget", "mod_test_npc_ai_retreat_follow_target");',
            'getnpcstate("MOD_TEST_NPC_AI_RETREAT", "FollowTargetIsPlayer", "mod_test_npc_ai_retreat_follow_player");',
            'getnpcstate("MOD_TEST_NPC_AI_RETREAT", "FollowTargetMapX", "mod_test_npc_ai_retreat_follow_x");',
            'getnpcstate("MOD_TEST_NPC_AI_RETREAT", "FollowTargetMapY", "mod_test_npc_ai_retreat_follow_y");',
            'getnpcstate("MOD_TEST_NPC_AI_RETREAT", "CurrentAction", "mod_test_npc_ai_retreat_action_raw");',
            'getnpcstate("MOD_TEST_NPC_AI_RETREAT", "MapX", "mod_test_npc_ai_retreat_map_x");',
            'getnpcstate("MOD_TEST_NPC_AI_RETREAT", "StepTargetX", "mod_test_npc_ai_retreat_step_x");',
            'getnpcstate("MOD_TEST_NPC_AI_RETREAT", "IsWalking", "mod_test_npc_ai_retreat_is_walking");',
            'assign("mod_test_npc_ai_retreat_started", 0);',
            'if getvar("mod_test_npc_ai_retreat_is_walking") == 1 then assign("mod_test_npc_ai_retreat_started", 1) end',
            'if getvar("mod_test_npc_ai_retreat_action_raw") == 2 then assign("mod_test_npc_ai_retreat_started", 1) end',
            'if getvar("mod_test_npc_ai_retreat_action_raw") == 21 then assign("mod_test_npc_ai_retreat_started", 1) end',
            'if getvar("mod_test_npc_ai_retreat_map_x") > 35 then assign("mod_test_npc_ai_retreat_started", 1) end',
            'if getvar("mod_test_npc_ai_retreat_step_x") > 35 then assign("mod_test_npc_ai_retreat_started", 1) end',
            "disablenpcai();",
            'delnpc("MOD_TEST_NPC_AI_RETREAT");',
            'addnpc("mod_test_npc_ai_low_magic.ini", 37, 20, 0);',
            'getnpcstate("MOD_TEST_NPC_AI_LOW_MAGIC", "MagicToUseWhenLifeLowLoaded", "mod_test_npc_ai_low_magic_loaded");',
            'getnpcstate("MOD_TEST_NPC_AI_LOW_MAGIC", "IsLifeLow", "mod_test_npc_ai_low_magic_life_low");',
            "enablenpcai();",
            "sleep(180);",
            'getnpcstate("MOD_TEST_NPC_AI_LOW_MAGIC", "CurrentAction", "mod_test_npc_ai_low_magic_action_raw");',
            'getnpcstate("MOD_TEST_NPC_AI_LOW_MAGIC", "IsMagicing", "mod_test_npc_ai_low_magic_is_magicing");',
            "sleep(1800);",
            'getnpcstate("MOD_TEST_NPC_AI_LOW_MAGIC", "Life", "mod_test_npc_ai_low_magic_life");',
            'assign("mod_test_npc_ai_low_magic_started", 0);',
            'if getvar("mod_test_npc_ai_low_magic_action_raw") == 8 then assign("mod_test_npc_ai_low_magic_started", 1) end',
            'if getvar("mod_test_npc_ai_low_magic_is_magicing") == 1 then assign("mod_test_npc_ai_low_magic_started", 1) end',
            'if getvar("mod_test_npc_ai_low_magic_life") > 40 then assign("mod_test_npc_ai_low_magic_started", 1) end',
            'assign("mod_test_npc_ai_low_magic_recovered", 0);',
            'if getvar("mod_test_npc_ai_low_magic_life") > 40 then assign("mod_test_npc_ai_low_magic_recovered", 1) end',
            "disablenpcai();",
            'delnpc("MOD_TEST_NPC_AI_LOW_MAGIC");',
            'addnpc("mod_test_npc_ai_revive.ini", 37, 20, 0);',
            'setnpcaction("MOD_TEST_NPC_AI_REVIVE", 11);',
            'getnpcstate("MOD_TEST_NPC_AI_REVIVE", "CurrentAction", "mod_test_npc_ai_revive_action_raw");',
            'getnpcstate("MOD_TEST_NPC_AI_REVIVE", "LeftMillisecondsToRevive", "mod_test_npc_ai_revive_left_ms");',
            'getnpcstate("MOD_TEST_NPC_AI_REVIVE", "IsInDeathing", "mod_test_npc_ai_revive_dying");',
            'assign("mod_test_npc_ai_revive_started", 0);',
            'if getvar("mod_test_npc_ai_revive_action_raw") == 11 then assign("mod_test_npc_ai_revive_started", 1) end',
            'if getvar("mod_test_npc_ai_revive_left_ms") > 0 then assign("mod_test_npc_ai_revive_started", 1) end',
            'if getvar("mod_test_npc_ai_revive_dying") == 1 then assign("mod_test_npc_ai_revive_started", 1) end',
            "sleep(900);",
            'getnpcstate("MOD_TEST_NPC_AI_REVIVE", "IsStanding", "mod_test_npc_ai_revive_standing");',
            'getnpcstate("MOD_TEST_NPC_AI_REVIVE", "Life", "mod_test_npc_ai_revive_life");',
            'assign("mod_test_npc_ai_revived", 0);',
            'if getvar("mod_test_npc_ai_revive_standing") == 1 then assign("mod_test_npc_ai_revived", 1) end',
            'assign("mod_test_npc_ai_revive_life_full", 0);',
            'if getvar("mod_test_npc_ai_revive_life") == 100 then assign("mod_test_npc_ai_revive_life_full", 1) end',
            'delnpc("MOD_TEST_NPC_AI_REVIVE");',
            'displaymessage("正在测试死亡后不生成尸体");',
            'delnpc("MOD_TEST_NPC_AI_NOADD_BODY");',
            'delobj("MOD_TEST_NPC_AI_BODY");',
            'addnpc("mod_test_npc_ai_noadd_body.ini", 37, 20, 0);',
            'getnpcstate("MOD_TEST_NPC_AI_NOADD_BODY", "IsBodyIniOk", "mod_test_npc_ai_noadd_body_ini_ok");',
            'getnpcstate("MOD_TEST_NPC_AI_NOADD_BODY", "ShouldAddBody", "mod_test_npc_ai_noadd_should_before");',
            'addnpcproperty("MOD_TEST_NPC_AI_NOADD_BODY", "FrozenMilliseconds", 2000);',
            'getnpcstate("MOD_TEST_NPC_AI_NOADD_BODY", "FrozenMilliseconds", "mod_test_npc_ai_noadd_frozen_ms");',
            'assign("mod_test_npc_ai_noadd_frozen_positive", 0);',
            'if getvar("mod_test_npc_ai_noadd_frozen_ms") > 0 then assign("mod_test_npc_ai_noadd_frozen_positive", 1) end',
            'setnpcaction("MOD_TEST_NPC_AI_NOADD_BODY", 11);',
            'getnpcstate("MOD_TEST_NPC_AI_NOADD_BODY", "IsNodAddBody", "mod_test_npc_ai_noadd_body_flag");',
            'getnpcstate("MOD_TEST_NPC_AI_NOADD_BODY", "NoAddBody", "mod_test_npc_ai_noadd_alias_flag");',
            'getnpcstate("MOD_TEST_NPC_AI_NOADD_BODY", "ShouldAddBody", "mod_test_npc_ai_noadd_should_after");',
            "sleep(1200);",
            'getobjstate("MOD_TEST_NPC_AI_BODY", "Exists", "mod_test_npc_ai_noadd_body_exists");',
            'delnpc("MOD_TEST_NPC_AI_NOADD_BODY");',
            'delobj("MOD_TEST_NPC_AI_BODY");',
            'displaymessage("正在测试 NPC 死亡武功");',
            'delnpc("MOD_TEST_NPC_AI_DEATH_CASTER");',
            'delnpc("MOD_TEST_NPC_AI_DEATH_TARGET");',
            "setplayerpos(30,20);",
            'addnpc("mod_test_npc_ai_death_target.ini", 34, 19, 4);',
            'addnpc("mod_test_npc_ai_death_caster.ini", 34, 20, 6);',
            'getnpcstate("MOD_TEST_NPC_AI_DEATH_CASTER", "MagicToUseWhenDeathLoaded", "mod_test_npc_ai_death_magic_loaded");',
            'npcattack("MOD_TEST_NPC_AI_DEATH_TARGET", 34, 20);',
            "sleep(3600);",
            'getnpcstate("MOD_TEST_NPC_AI_DEATH_TARGET", "Life", "mod_test_npc_ai_death_target_life");',
            'assign("mod_test_npc_ai_death_magic_hit", 0);',
            'if getvar("mod_test_npc_ai_death_target_life") < 100 then assign("mod_test_npc_ai_death_magic_hit", 1) end',
            'delnpc("MOD_TEST_NPC_AI_DEATH_CASTER");',
            'delnpc("MOD_TEST_NPC_AI_DEATH_TARGET");',
            'displaymessage("正在测试友方死亡后撤退");',
            'delnpc("MOD_TEST_NPC_AI_FRIEND_ATTACKER");',
            'delnpc("MOD_TEST_NPC_AI_FRIEND_VICTIM");',
            'delnpc("MOD_TEST_NPC_AI_FRIEND_WATCHER");',
            'delnpc("MOD_TEST_NPC_NONE_FIGHTER");',
            'delnpc("MOD_TEST_NPC_FLYER");',
            'delnpc("MOD_TEST_NPC_AFRAID_ANIMAL");',
            'delnpc("MOD_TEST_NPC_PARTNER");',
            'delnpc("MOD_TEST_NPC_AI_STATE");',
            'delnpc("MOD_TEST_NPC_EVENT");',
            "setplayerpos(30,23);",
            'addnpc("mod_test_npc_ai_friend_attacker.ini", 31, 20, 0);',
            'addnpc("mod_test_npc_ai_friend_victim.ini", 32, 20, 0);',
            'addnpc("mod_test_npc_ai_friend_watcher.ini", 33, 20, 0);',
            "enablenpcai();",
            'npcusemagic("MOD_TEST_NPC_AI_FRIEND_ATTACKER", "mod_test_magic_ai_friend_death_attack.ini", 32, 20, 1);',
            "sleep(1800);",
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_VICTIM", "Life", "mod_test_npc_ai_friend_victim_life");',
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_VICTIM", "CurrentAction", "mod_test_npc_ai_friend_victim_action_raw");',
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_VICTIM", "IsHide", "mod_test_npc_ai_friend_victim_hide");',
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_WATCHER", "CurrentAction", "mod_test_npc_ai_friend_watcher_action_raw");',
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_WATCHER", "MapX", "mod_test_npc_ai_friend_watcher_map_x");',
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_WATCHER", "StepTargetX", "mod_test_npc_ai_friend_watcher_step_x");',
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_WATCHER", "IsWalking", "mod_test_npc_ai_friend_watcher_is_walking");',
            'assign("mod_test_npc_ai_friend_victim_dead", 0);',
            'if getvar("mod_test_npc_ai_friend_victim_life") == 0 then assign("mod_test_npc_ai_friend_victim_dead", 1) end',
            'if getvar("mod_test_npc_ai_friend_victim_action_raw") == 11 then assign("mod_test_npc_ai_friend_victim_dead", 1) end',
            'if getvar("mod_test_npc_ai_friend_victim_hide") == 1 then assign("mod_test_npc_ai_friend_victim_dead", 1) end',
            'assign("mod_test_npc_ai_friend_retreat_away_from_attacker", 0);',
            'if getvar("mod_test_npc_ai_friend_victim_dead") == 1 and getvar("mod_test_npc_ai_friend_watcher_map_x") > 33 then assign("mod_test_npc_ai_friend_retreat_away_from_attacker", 1) end',
            'if getvar("mod_test_npc_ai_friend_victim_dead") == 1 and getvar("mod_test_npc_ai_friend_watcher_step_x") > 33 then assign("mod_test_npc_ai_friend_retreat_away_from_attacker", 1) end',
            'assign("mod_test_npc_ai_friend_retreat_started", 0);',
            'if getvar("mod_test_npc_ai_friend_watcher_is_walking") == 1 then assign("mod_test_npc_ai_friend_retreat_started", 1) end',
            'if getvar("mod_test_npc_ai_friend_watcher_action_raw") == 2 then assign("mod_test_npc_ai_friend_retreat_started", 1) end',
            'if getvar("mod_test_npc_ai_friend_watcher_action_raw") == 21 then assign("mod_test_npc_ai_friend_retreat_started", 1) end',
            'if getvar("mod_test_npc_ai_friend_watcher_map_x") > 33 then assign("mod_test_npc_ai_friend_retreat_started", 1) end',
            'if getvar("mod_test_npc_ai_friend_watcher_step_x") > 33 then assign("mod_test_npc_ai_friend_retreat_started", 1) end',
            "disablenpcai();",
            'setnpcpos("MOD_TEST_NPC_AI_FRIEND_WATCHER", 33, 20);',
            "sleep(300);",
            "enablenpcai();",
            "sleep(900);",
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_WATCHER", "CurrentAction", "mod_test_npc_ai_friend_cached_watcher_action_raw");',
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_WATCHER", "MapX", "mod_test_npc_ai_friend_cached_watcher_map_x");',
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_WATCHER", "StepTargetX", "mod_test_npc_ai_friend_cached_watcher_step_x");',
            'getnpcstate("MOD_TEST_NPC_AI_FRIEND_WATCHER", "IsWalking", "mod_test_npc_ai_friend_cached_watcher_is_walking");',
            'assign("mod_test_npc_ai_friend_retreat_cached_target", 0);',
            'if getvar("mod_test_npc_ai_friend_cached_watcher_is_walking") == 1 then assign("mod_test_npc_ai_friend_retreat_cached_target", 1) end',
            'if getvar("mod_test_npc_ai_friend_cached_watcher_action_raw") == 2 then assign("mod_test_npc_ai_friend_retreat_cached_target", 1) end',
            'if getvar("mod_test_npc_ai_friend_cached_watcher_action_raw") == 21 then assign("mod_test_npc_ai_friend_retreat_cached_target", 1) end',
            'if getvar("mod_test_npc_ai_friend_cached_watcher_map_x") > 33 then assign("mod_test_npc_ai_friend_retreat_cached_target", 1) end',
            'if getvar("mod_test_npc_ai_friend_cached_watcher_step_x") > 33 then assign("mod_test_npc_ai_friend_retreat_cached_target", 1) end',
            "disablenpcai();",
            'delnpc("MOD_TEST_NPC_AI_FRIEND_ATTACKER");',
            'delnpc("MOD_TEST_NPC_AI_FRIEND_VICTIM");',
            'delnpc("MOD_TEST_NPC_AI_FRIEND_WATCHER");',
            'delnpc("MOD_TEST_NPC_NONE_FIGHTER");',
            'delnpc("MOD_TEST_NPC_FLYER");',
            'delnpc("MOD_TEST_NPC_AFRAID_ANIMAL");',
            'delnpc("MOD_TEST_NPC_PARTNER");',
            'delnpc("MOD_TEST_NPC_AI_STATE");',
            'displaymessage("正在测试伙伴忽略真正中立目标");',
            "disablepartnercombat();",
            'delnpc("MOD_TEST_NPC_AI_PARTNER_COMBAT");',
            'delnpc("MOD_TEST_NPC_AI_PARTNER_TARGET");',
            'delnpc("MOD_TEST_NPC_AI_TRUE_NEUTRAL_TARGET");',
            "setplayerpos(30,24);",
            'addnpc("mod_test_npc_ai_partner_combat.ini", 34, 20, 0);',
            'addnpc("mod_test_npc_ai_true_neutral_target.ini", 35, 20, 0);',
            'setnpcpartner("MOD_TEST_NPC_AI_PARTNER_COMBAT");',
            'getnpcstate("MOD_TEST_NPC_AI_TRUE_NEUTRAL_TARGET", "Life", "mod_test_npc_ai_partner_true_neutral_life_before");',
            "enablepartnercombat();",
            "enablenpcai();",
            "sleep(900);",
            'getnpcstate("MOD_TEST_NPC_AI_PARTNER_COMBAT", "HasCurrentCombatTarget", "mod_test_npc_ai_partner_neutral_has_target");',
            'getnpcstate("MOD_TEST_NPC_AI_TRUE_NEUTRAL_TARGET", "Life", "mod_test_npc_ai_partner_true_neutral_life_after");',
            'assign("mod_test_npc_ai_partner_ignored_true_neutral", 0);',
            'if getvar("mod_test_npc_ai_partner_neutral_has_target") == 0 and getvar("mod_test_npc_ai_partner_true_neutral_life_after") == getvar("mod_test_npc_ai_partner_true_neutral_life_before") then assign("mod_test_npc_ai_partner_ignored_true_neutral", 1) end',
            "disablepartnercombat();",
            "disablenpcai();",
            'delnpc("MOD_TEST_NPC_AI_PARTNER_COMBAT");',
            'delnpc("MOD_TEST_NPC_AI_TRUE_NEUTRAL_TARGET");',
            'displaymessage("正在测试伙伴战斗");',
            "disablepartnercombat();",
            'delnpc("MOD_TEST_NPC_AI_PARTNER_COMBAT");',
            'delnpc("MOD_TEST_NPC_AI_PARTNER_TARGET");',
            "setplayerpos(30,24);",
            'addnpc("mod_test_npc_ai_partner_combat.ini", 34, 20, 0);',
            'addnpc("mod_test_npc_ai_partner_target.ini", 35, 20, 0);',
            'setnpcpartner("MOD_TEST_NPC_AI_PARTNER_COMBAT");',
            "enablenpcai();",
            "sleep(300);",
            'getnpcstate("MOD_TEST_NPC_AI_PARTNER_COMBAT", "HasCurrentCombatTarget", "mod_test_npc_ai_partner_combat_disabled_has_target");',
            'getnpcstate("MOD_TEST_NPC_AI_PARTNER_TARGET", "Life", "mod_test_npc_ai_partner_combat_disabled_life");',
            "enablepartnercombat();",
            "sleep(5000);",
            'getnpcstate("MOD_TEST_NPC_AI_PARTNER_COMBAT", "AttackOptionCount", "mod_test_npc_ai_partner_combat_attack_options");',
            'getnpcstate("MOD_TEST_NPC_AI_PARTNER_COMBAT", "NpcMagicLoaded", "mod_test_npc_ai_partner_combat_magic_loaded");',
            'getnpcstate("MOD_TEST_NPC_AI_PARTNER_COMBAT", "HasLastUsedAttackOption", "mod_test_npc_ai_partner_combat_last_option");',
            'getnpcstate("MOD_TEST_NPC_AI_PARTNER_TARGET", "Life", "mod_test_npc_ai_partner_combat_target_life");',
            'assign("mod_test_npc_ai_partner_combat_hit", 0);',
            'if getvar("mod_test_npc_ai_partner_combat_target_life") < 500 then assign("mod_test_npc_ai_partner_combat_hit", 1) end',
            "disablepartnercombat();",
            "disablenpcai();",
            'delnpc("MOD_TEST_NPC_AI_PARTNER_COMBAT");',
            'delnpc("MOD_TEST_NPC_AI_PARTNER_TARGET");',
            'displaymessage("正在测试 NPC 扩展字段存档读取");',
            "disablenpcai();",
            'loadnpc("map001.npc");',
            'delnpc("MOD_TEST_NPC_AI_STATE");',
            'addnpc("mod_test_npc_ai_state.ini", 34, 20, 0);',
            'savenpc("mod_test_npc_ai_extension_save.npc");',
            'delnpc("MOD_TEST_NPC_AI_STATE");',
            'loadnpc("mod_test_npc_ai_extension_save.npc");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "AutoRunScript", "mod_test_npc_ai_state_auto_run_script_after_load");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasAutoRunScript", "mod_test_npc_ai_state_has_auto_run_script_after_load");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "Arm", "mod_test_npc_ai_state_arm_after_load");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasArm", "mod_test_npc_ai_state_has_arm_after_load");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "EvadeN", "mod_test_npc_ai_state_evaden_after_load");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasEvadeN", "mod_test_npc_ai_state_has_evaden_after_load");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "Gengu", "mod_test_npc_ai_state_gengu_after_load");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasGengu", "mod_test_npc_ai_state_has_gengu_after_load");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "Neixi", "mod_test_npc_ai_state_neixi_after_load");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasNeixi", "mod_test_npc_ai_state_has_neixi_after_load");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "Physique", "mod_test_npc_ai_state_physique_after_load");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasPhysique", "mod_test_npc_ai_state_has_physique_after_load");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "Dodge_BeginFrame", "mod_test_npc_ai_state_dodge_begin_after_load");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasDodge_BeginFrame", "mod_test_npc_ai_state_has_dodge_begin_after_load");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "Dodge_EndFrame", "mod_test_npc_ai_state_dodge_end_after_load");',
            'getnpcstate("MOD_TEST_NPC_AI_STATE", "HasDodge_EndFrame", "mod_test_npc_ai_state_has_dodge_end_after_load");',
            'delnpc("MOD_TEST_NPC_AI_STATE");',
            'displaymessage("正在测试 NPC 定时器上下文");',
            'assign("mod_test_npc_ai_timer_context_ran", 0);',
            'assign("mod_test_npc_ai_timer_context_done", 0);',
            'assign("mod_test_npc_ai_timer_context_dir_zero", 0);',
            'assign("mod_test_npc_ai_timer_context_dir_after", 0);',
            'assign("mod_test_npc_ai_timer_context_x_zero", 0);',
            'assign("mod_test_npc_ai_timer_context_y_zero", 0);',
            'assign("mod_test_npc_ai_timer_context_x_after", 0);',
            'assign("mod_test_npc_ai_timer_context_y_after", 0);',
            'assign("mod_test_npc_ai_timer_context_x_final", 0);',
            'assign("mod_test_npc_ai_timer_context_y_final", 0);',
            'assign("mod_test_npc_ai_timer_context_dir_final", 0);',
            'assign("mod_test_npc_ai_timer_context_special_returned", 0);',
            'assign("mod_test_npc_ai_timer_context_special_nonblocking_active", 0);',
            'assign("mod_test_npc_ai_timer_context_special_ex_returned", 0);',
            'assign("mod_test_npc_ai_timer_context_special_ex_after", 1);',
            'assign("mod_test_npc_ai_timer_context_special_alias_returned", 0);',
            'assign("mod_test_npc_ai_timer_context_special_alias_active", 0);',
            'delnpc("MOD_TEST_NPC_AI_TIMER_CONTEXT");',
            'addnpc("mod_test_npc_ai_timer_context.ini", 37, 20, 1);',
            'getnpcstate("MOD_TEST_NPC_AI_TIMER_CONTEXT", "MapX", "mod_test_npc_ai_timer_context_x_before");',
            'getnpcstate("MOD_TEST_NPC_AI_TIMER_CONTEXT", "MapY", "mod_test_npc_ai_timer_context_y_before");',
            'getnpcstate("MOD_TEST_NPC_AI_TIMER_CONTEXT", "Dir", "mod_test_npc_ai_timer_context_dir_before");',
            "enablenpcai();",
            'displaymessage("NPC 智能测试完成");',
            "return;",
            "",
        ]
    )


def make_placeholder_script(name: str) -> str:
    return "\n".join(
        [
            f'displaymessage("测试素材缺失：{name}");',
            f'assign("mod_test_{name}_placeholder", 1);',
            "return;",
            "",
        ]
    )


def make_ui_settings_ini() -> str:
    return "\n".join(
        [
            "[GoodsInit]",
            "GoodsListType=1",
            "StoreIndexBegin=1",
            "StoreIndexEnd=198",
            "EquipIndexBegin=201",
            "EquipIndexEnd=207",
            "BottomIndexBegin=221",
            "BottomIndexEnd=223",
            "",
            "[MagicInit]",
            "StoreIndexBegin=1",
            "StoreIndexEnd=36",
            "BottomIndexBegin=40",
            "BottomIndexEnd=44",
            "XiuLianIndex=49",
            "HideStartIndex=1000",
            "",
        ]
    )


def make_scenarios_ini() -> str:
    npc_ai_expect_variables = ";".join(
        [
            "mod_test_npc_ai_ready=1",
            "mod_test_npc_ai_visible_exists_hidden=1",
            "mod_test_npc_ai_visible_has_name=1",
            "mod_test_npc_ai_visible_threshold=2",
            "mod_test_npc_ai_visible_hidden_by_var=0",
            "mod_test_npc_ai_visible_hidden_runtime=0",
            "mod_test_npc_ai_visible_hidden_obstacle=0",
            "mod_test_npc_ai_visible_hidden_not_mapped=1",
            "mod_test_npc_ai_visible_after_var=1",
            "mod_test_npc_ai_visible_runtime_after=1",
            "mod_test_npc_ai_visible_obstacle_after=1",
            "mod_test_npc_ai_visible_mapped_after=1",
            "mod_test_npc_ai_none_kind=1",
            "mod_test_npc_ai_none_relation=3",
            "mod_test_npc_ai_none_attack=0",
            "mod_test_npc_ai_none_group=201",
            "mod_test_npc_ai_none_noauto=1",
            "mod_test_npc_ai_none_stopfind=1",
            "mod_test_npc_ai_none_is_none_fighter=1",
            "mod_test_npc_ai_none_is_enemy=0",
            "mod_test_npc_ai_none_is_fighter=1",
            "mod_test_npc_ai_none_is_fighter_kind=1",
            "mod_test_npc_ai_none_is_interactive=1",
            "mod_test_npc_ai_none_is_obstacle=1",
            "mod_test_npc_ai_none_tile_can_walk=0",
            "mod_test_npc_ai_flyer_kind=7",
            "mod_test_npc_ai_flyer_relation=2",
            "mod_test_npc_ai_flyer_is_obstacle=0",
            "mod_test_npc_ai_flyer_is_fighter=0",
            "mod_test_npc_ai_flyer_is_interactive=0",
            "mod_test_npc_ai_flyer_tile_can_walk=1",
            "mod_test_npc_ai_afraid_kind=6",
            "mod_test_npc_ai_afraid_relation=2",
            "mod_test_npc_ai_afraid_is_obstacle=1",
            "mod_test_npc_ai_afraid_is_fighter=0",
            "mod_test_npc_ai_afraid_is_interactive=0",
            "mod_test_npc_ai_afraid_path_type=2",
            "mod_test_npc_ai_afraid_use_pathfinder=1",
            "mod_test_npc_ai_event_kind=5",
            "mod_test_npc_ai_event_is_event=1",
            "mod_test_npc_ai_partner_kind_before=1",
            "mod_test_npc_ai_partner_relation_before=0",
            "mod_test_npc_ai_partner_is_fighter_friend_before=1",
            "mod_test_npc_ai_partner_is_partner_before=0",
            "mod_test_npc_ai_partner_destination_before=1",
            "mod_test_npc_ai_partner_kind_after=3",
            "mod_test_npc_ai_partner_relation_after=0",
            "mod_test_npc_ai_partner_is_fighter_friend_after=1",
            "mod_test_npc_ai_partner_is_partner_after=1",
            "mod_test_npc_ai_partner_is_fighter_after=1",
            "mod_test_npc_ai_partner_destination_after=0",
            "mod_test_npc_ai_partner_standing_after=1",
            "mod_test_npc_ai_state_ai_type=2",
            "mod_test_npc_ai_state_is_rand_move=1",
            "mod_test_npc_ai_state_no_fightback=1",
            "mod_test_npc_ai_state_idle=5",
            "mod_test_npc_ai_state_idled_frame=0",
            "mod_test_npc_ai_state_attack_speed=8",
            "mod_test_npc_ai_state_has_attack_speed=1",
            "mod_test_npc_ai_state_group=204",
            "mod_test_npc_ai_state_has_show_name=1",
            "mod_test_npc_ai_state_display_uses_show_name=1",
            "mod_test_npc_ai_state_life=45",
            "mod_test_npc_ai_state_lifemax=100",
            "mod_test_npc_ai_state_real_attack=0",
            "mod_test_npc_ai_state_real_defend=0",
            "mod_test_npc_ai_state_real_evade=0",
            "mod_test_npc_ai_state_life_low=1",
            "mod_test_npc_ai_state_is_frozened=0",
            "mod_test_npc_ai_state_is_poisoned=0",
            "mod_test_npc_ai_state_is_petrified=0",
            "mod_test_npc_ai_state_is_draw=1",
            "mod_test_npc_ai_state_body_function_well=1",
            "mod_test_npc_ai_state_in_special_action=0",
            "mod_test_npc_ai_state_is_death=0",
            "mod_test_npc_ai_state_death_script_end=1",
            "mod_test_npc_ai_state_body_ini_ok=0",
            "mod_test_npc_ai_state_magic_from_cache=1",
            "mod_test_npc_ai_state_full_life=0",
            "mod_test_npc_ai_state_in_fighting=0",
            "mod_test_npc_ai_state_revive=2500",
            "mod_test_npc_ai_state_life_ms=3000",
            "mod_test_npc_ai_state_life_low_percent=50",
            "mod_test_npc_ai_state_keep_life_low=6",
            "mod_test_npc_ai_state_keep_friend_death=7",
            "mod_test_npc_ai_state_life_low_magic=1",
            "mod_test_npc_ai_state_beattacked_magic=1",
            "mod_test_npc_ai_state_beattacked_dir=2",
            "mod_test_npc_ai_state_beattacked_magic_script=1",
            "mod_test_npc_ai_state_beattacked_dir_script=1",
            "mod_test_player_beattacked_magic_script=1",
            "mod_test_player_beattacked_dir_script=2",
            "mod_test_npc_ai_state_death_magic=1",
            "mod_test_npc_ai_state_death_dir=1",
            "mod_test_npc_ai_state_hurt_interval=1200",
            "mod_test_npc_ai_state_hurt_life=8",
            "mod_test_npc_ai_state_hurt_radius=2",
            "mod_test_npc_ai_state_can_equip=1",
            "mod_test_npc_ai_state_can_levelup=1",
            "mod_test_npc_ai_state_levelup_exp=123",
            "mod_test_npc_ai_state_auto_run_script=0",
            "mod_test_npc_ai_state_has_auto_run_script=1",
            "mod_test_npc_ai_state_arm=60",
            "mod_test_npc_ai_state_has_arm=1",
            "mod_test_npc_ai_state_evaden=61",
            "mod_test_npc_ai_state_has_evaden=1",
            "mod_test_npc_ai_state_gengu=62",
            "mod_test_npc_ai_state_has_gengu=1",
            "mod_test_npc_ai_state_neixi=63",
            "mod_test_npc_ai_state_has_neixi=1",
            "mod_test_npc_ai_state_physique=64",
            "mod_test_npc_ai_state_has_physique=1",
            "mod_test_npc_ai_state_dodge_begin=10",
            "mod_test_npc_ai_state_has_dodge_begin=1",
            "mod_test_npc_ai_state_dodge_end=20",
            "mod_test_npc_ai_state_has_dodge_end=1",
            "mod_test_npc_ai_state_auto_run_script_after_load=0",
            "mod_test_npc_ai_state_has_auto_run_script_after_load=1",
            "mod_test_npc_ai_state_arm_after_load=60",
            "mod_test_npc_ai_state_has_arm_after_load=1",
            "mod_test_npc_ai_state_evaden_after_load=61",
            "mod_test_npc_ai_state_has_evaden_after_load=1",
            "mod_test_npc_ai_state_gengu_after_load=62",
            "mod_test_npc_ai_state_has_gengu_after_load=1",
            "mod_test_npc_ai_state_neixi_after_load=63",
            "mod_test_npc_ai_state_has_neixi_after_load=1",
            "mod_test_npc_ai_state_physique_after_load=64",
            "mod_test_npc_ai_state_has_physique_after_load=1",
            "mod_test_npc_ai_state_dodge_begin_after_load=10",
            "mod_test_npc_ai_state_has_dodge_begin_after_load=1",
            "mod_test_npc_ai_state_dodge_end_after_load=20",
            "mod_test_npc_ai_state_has_dodge_end_after_load=1",
            "mod_test_npc_ai_npcres_fallback_walk=1",
            "mod_test_npc_ai_npcres_priority_walk=0",
            "mod_test_npc_ai_fight_res_stand=1",
            "mod_test_npc_ai_fight_res_walk=1",
            "mod_test_npc_ai_fight_res_run=1",
            "mod_test_npc_ai_fight_res_jump=1",
            "mod_test_npc_ai_fight_res_move_dir_positive=1",
            "mod_test_npc_ai_fight_res_jump_dir_positive=1",
            "mod_test_npc_ai_blind_move_disabled=1",
            "mod_test_npc_ai_blind_can_walk=0",
            "mod_test_npc_ai_blind_skill_disabled=1",
            "mod_test_npc_ai_blind_is_blind=1",
            "mod_test_npc_ai_blind_can_see_player=0",
            "mod_test_npc_ai_blind_has_target=0",
            "mod_test_npc_ai_blind_expired=0",
            "mod_test_npc_ai_blind_can_see_after=1",
            "mod_test_npc_ai_blind_reacquired_target=1",
            "mod_test_npc_ai_global_disabled=1",
            "mod_test_npc_ai_global_disabled_has_target=0",
            "mod_test_npc_ai_global_enabled=0",
            "mod_test_npc_ai_global_enabled_has_target=1",
            "mod_test_npc_ai_local_disabled_local=1",
            "mod_test_npc_ai_local_disabled_effective=1",
            "mod_test_npc_ai_local_disabled_ai_enabled=0",
            "mod_test_npc_ai_local_disabled_has_target=0",
            "mod_test_npc_ai_local_enabled_local=0",
            "mod_test_npc_ai_local_enabled_effective=0",
            "mod_test_npc_ai_local_enabled_ai_enabled=1",
            "mod_test_npc_ai_local_enabled_has_target=1",
            "mod_test_npc_ai_randwalk_path_before_empty=1",
            "mod_test_npc_ai_randwalk_path_ready=1",
            "mod_test_npc_ai_friend_neutral_has_target=0",
            "mod_test_npc_ai_true_neutral_life_after=500",
            "mod_test_npc_ai_friend_ignored_true_neutral=1",
            "mod_test_npc_ai_friend_none_relation=3",
            "mod_test_npc_ai_friend_none_is_none_fighter=1",
            "mod_test_npc_ai_friend_hit_none_fighter=1",
            "mod_test_npc_ai_partner_neutral_has_target=0",
            "mod_test_npc_ai_partner_true_neutral_life_after=500",
            "mod_test_npc_ai_partner_ignored_true_neutral=1",
            "mod_test_npc_ai_timer_context_ran=1",
            "mod_test_npc_ai_timer_context_done=1",
            "mod_test_npc_ai_timer_context_x_before=37",
            "mod_test_npc_ai_timer_context_y_before=20",
            "mod_test_npc_ai_timer_context_dir_before=1",
            "mod_test_npc_ai_timer_context_dir_zero=0",
            "mod_test_npc_ai_timer_context_dir_after=5",
            "mod_test_npc_ai_timer_context_x_zero=0",
            "mod_test_npc_ai_timer_context_y_zero=0",
            "mod_test_npc_ai_timer_context_x_after=39",
            "mod_test_npc_ai_timer_context_y_after=20",
            "mod_test_npc_ai_timer_context_x_final=39",
            "mod_test_npc_ai_timer_context_y_final=20",
            "mod_test_npc_ai_timer_context_dir_final=5",
            "mod_test_npc_ai_timer_context_special_nonblocking_active=1",
            "mod_test_npc_ai_timer_context_special_returned=1",
            "mod_test_npc_ai_timer_context_special_ex_returned=1",
            "mod_test_npc_ai_timer_context_special_ex_after=0",
            "mod_test_npc_ai_timer_context_special_alias_active=1",
            "mod_test_npc_ai_timer_context_special_alias_returned=1",
            "mod_test_npc_ai_afraid_disabled_still=1",
            "mod_test_npc_ai_afraid_started=1",
            "mod_test_npc_ai_retreat_has_target=1",
            "mod_test_npc_ai_retreat_target_player=1",
            "mod_test_npc_ai_retreat_follow_found=1",
            "mod_test_npc_ai_retreat_follow_target=1",
            "mod_test_npc_ai_retreat_follow_player=1",
            "mod_test_npc_ai_retreat_follow_x=34",
            "mod_test_npc_ai_retreat_follow_y=20",
            "mod_test_npc_ai_retreat_started=1",
            "mod_test_npc_ai_low_magic_loaded=1",
            "mod_test_npc_ai_low_magic_life_low=1",
            "mod_test_npc_ai_low_magic_started=1",
            "mod_test_npc_ai_low_magic_recovered=1",
            "mod_test_npc_ai_revive_started=1",
            "mod_test_npc_ai_revived=1",
            "mod_test_npc_ai_revive_life_full=1",
            "mod_test_npc_ai_noadd_body_ini_ok=1",
            "mod_test_npc_ai_noadd_should_before=1",
            "mod_test_npc_ai_noadd_frozen_positive=1",
            "mod_test_npc_ai_noadd_body_flag=1",
            "mod_test_npc_ai_noadd_alias_flag=1",
            "mod_test_npc_ai_noadd_should_after=0",
            "mod_test_npc_ai_noadd_body_exists=0",
            "mod_test_npc_ai_death_magic_loaded=1",
            "mod_test_npc_ai_death_magic_hit=1",
            "mod_test_npc_ai_friend_victim_dead=1",
            "mod_test_npc_ai_friend_retreat_started=1",
            "mod_test_npc_ai_friend_retreat_away_from_attacker=1",
            "mod_test_npc_ai_friend_retreat_cached_target=1",
            "mod_test_npc_ai_partner_combat_disabled_has_target=0",
            "mod_test_npc_ai_partner_combat_disabled_life=500",
            "mod_test_npc_ai_partner_combat_attack_options=1",
            "mod_test_npc_ai_partner_combat_magic_loaded=1",
            "mod_test_npc_ai_partner_combat_last_option=1",
            "mod_test_npc_ai_partner_combat_hit=1",
            "mod_test_npc_ai_move_start_x=31",
            "mod_test_npc_ai_move_start_y=20",
            "mod_test_npc_ai_move_start_standing=1",
            "mod_test_npc_ai_move_can_walk=1",
            "mod_test_npc_ai_move_path_type=0",
            "mod_test_npc_ai_move_destination_path_type=3",
            "mod_test_npc_ai_move_destination_unrestricted=1",
            "mod_test_npc_ai_move_destination_blocked=0",
            "mod_test_npc_ai_move_after_set_x=31",
            "mod_test_npc_ai_move_after_set_y=20",
            "mod_test_npc_ai_move_step_state=1",
            "mod_test_npc_ai_move_step_list_len=1",
            "mod_test_npc_ai_move_step_target_x=32",
            "mod_test_npc_ai_move_step_target_y=20",
            "mod_test_npc_ai_move_step_occupied_count=1",
            "mod_test_npc_ai_move_step_last_positive=1",
            "mod_test_npc_ai_move_smooth_after_set=1",
            "mod_test_npc_ai_move_path_len=1",
            "mod_test_npc_ai_move_first_step=1",
            "mod_test_npc_ai_move_action=2",
            "mod_test_npc_ai_move_is_walking=1",
            "mod_test_npc_ai_move_dest_x=32",
            "mod_test_npc_ai_move_dest_y=20",
            "mod_test_npc_ai_move_mid_x=31",
            "mod_test_npc_ai_move_mid_y=20",
            "mod_test_npc_ai_move_mid_smooth=1",
            "mod_test_npc_ai_move_mid_progress_started=1",
            "mod_test_npc_ai_move_mid_progress_not_finished=1",
            "mod_test_npc_ai_move_end_x=32",
            "mod_test_npc_ai_move_end_y=20",
            "mod_test_npc_ai_move_standing_after_arrival=1",
            "mod_test_npc_ai_move_destination_after_arrival=0",
            "mod_test_npc_ai_move_dest_x_after_arrival=0",
            "mod_test_npc_ai_move_dest_y_after_arrival=0",
            "mod_test_npc_ai_action1_canonical_started=1",
            "mod_test_npc_ai_action1_canonical_standing=1",
            "mod_test_npc_ai_action1_alias_started=1",
            "mod_test_npc_ai_action1_alias_standing=1",
            "mod_test_npc_ai_move_use_pathfinder=0",
            "mod_test_npc_ai_path_normal_type=3",
            "mod_test_npc_ai_path_event_type=3",
            "mod_test_npc_ai_path_enemy_type=0",
            "mod_test_npc_ai_path_best_type=2",
            "mod_test_npc_ai_path_fixed_type=0",
            "mod_test_npc_ai_path_fixed_has_path=1",
            "mod_test_npc_ai_path_fixed_count=2",
            "mod_test_npc_ai_path_fixed_currpos=1",
            "mod_test_npc_ai_path_normal_maxtry=500",
            "mod_test_npc_ai_path_enemy_maxtry=10",
            "mod_test_npc_ai_path_best_maxtry=100",
            "mod_test_npc_ai_path_normal_use_pathfinder=1",
            "mod_test_npc_ai_path_event_use_pathfinder=1",
            "mod_test_npc_ai_path_enemy_use_pathfinder=0",
            "mod_test_npc_ai_path_best_use_pathfinder=1",
            "mod_test_npc_ai_path_fixed_use_pathfinder=0",
            "mod_test_npc_ai_blocked_destination_blocked=1",
            "mod_test_npc_ai_blocked_destination_path_type=3",
            "mod_test_npc_ai_blocked_destination_exists=1",
            "mod_test_npc_ai_blocked_first_step=1",
            "mod_test_npc_ai_blocked_path_len=3",
            "mod_test_npc_ai_blocked_end_x=32",
            "mod_test_npc_ai_blocked_dest_x=0",
            "mod_test_npc_ai_flyer_path_type=4",
            "mod_test_npc_ai_flyer_use_pathfinder=1",
            "mod_test_npc_ai_flyer_step_list_len=2",
            "mod_test_npc_ai_flyer_step_target_x=33",
            "mod_test_npc_ai_flyer_step_target_y=20",
            "mod_test_npc_ai_flyer_is_walking=1",
            "mod_test_npc_ai_partner_path_type_after=2",
            "mod_test_npc_ai_partner_use_pathfinder_after=1",
        ]
    )
    object_state_expect_variables = ";".join(
        [
            "mod_test_object_state_ready=1",
            "mod_test_object_exists_before=1",
            "mod_test_object_file_name=1",
            "mod_test_object_kind_box=1",
            "mod_test_object_type_metadata=2",
            "mod_test_object_has_movie_metadata=1",
            "mod_test_object_has_resource_image=1",
            "mod_test_object_has_resource_shade=1",
            "mod_test_object_has_resource_sound=1",
            "mod_test_object_resource_image_loaded=1",
            "mod_test_object_resource_shade_loaded=1",
            "mod_test_object_map_x=30",
            "mod_test_object_map_y=20",
            "mod_test_object_region_exists=1",
            "mod_test_object_region_begin_exists=1",
            "mod_test_object_region_x=30",
            "mod_test_object_region_y=20",
            "mod_test_object_offset_x_before=0",
            "mod_test_object_offset_y_before=0",
            "mod_test_object_has_script=1",
            "mod_test_object_has_script_right=1",
            "mod_test_object_has_any_script=1",
            "mod_test_object_is_interactive=1",
            "mod_test_object_has_primary_interact=1",
            "mod_test_object_can_select=1",
            "mod_test_object_can_interact_directly=1",
            "mod_test_object_just_touch=0",
            "mod_test_object_is_box=1",
            "mod_test_object_is_door=0",
            "mod_test_object_selecting_initial=0",
            "mod_test_object_offset_x_after=6",
            "mod_test_object_offset_y_after=-4",
            "mod_test_object_open_action=2",
            "mod_test_object_close_action=3",
            "mod_test_object_named_open_obj_action=2",
            "mod_test_object_is_trap=1",
            "mod_test_object_trap_auto_play=1",
            "mod_test_object_trap_obstacle=0",
            "mod_test_object_is_body=1",
            "mod_test_object_pickup_kind=7",
            "mod_test_object_is_drop=1",
            "mod_test_object_drop_auto_play=1",
            "mod_test_object_drop_obstacle=0",
            "mod_test_object_static_obstacle=1",
            "mod_test_object_static_auto_play=1",
            "mod_test_object_exists_after=0",
            "mod_test_object_is_removed=1",
            "mod_test_object_legacy_pickup_exists=1",
            "mod_test_object_legacy_pickup_kind=8",
            "mod_test_object_legacy_pickup_is_drop=1",
            "mod_test_object_legacy_pickup_auto_play=1",
            "mod_test_object_legacy_pickup_obstacle=0",
            "mod_test_object_save_exists_after_load=1",
            "mod_test_object_save_kind_after_load=7",
            "mod_test_object_save_offset_x_after_load=9",
            "mod_test_object_save_offset_y_after_load=-3",
            "mod_test_object_save_is_drop_after_load=1",
            "mod_test_object_save_right_after_load=1",
            "mod_test_object_save_type_after_load=3",
            "mod_test_object_save_has_movie_after_load=1",
            "mod_test_object_setobjoffset_alias_x=11",
            "mod_test_object_setobjoffset_alias_y=-7",
            "mod_test_object_deleteobj_alias_exists_after=0",
            "mod_test_object_lodaobj_alias_exists_after=1",
            "mod_test_object_lodaobj_alias_offset_x=11",
            "mod_test_object_lodaobj_alias_offset_y=-7",
            "mod_test_object_deletecurrent_alias_script=1",
            "mod_test_object_deletecurrent_alias_exists_after=0",
            "mod_test_object_right_only_has_left=0",
            "mod_test_object_right_only_has_right=1",
            "mod_test_object_right_only_has_any=1",
            "mod_test_object_right_only_can_select=1",
            "mod_test_object_right_only_fallback=1",
            "mod_test_object_right_only_primary_available=1",
            "mod_test_object_remove_exists_before=1",
            "mod_test_object_remove_ms_before=180",
            "mod_test_object_remove_exists_after=0",
            "mod_test_object_remove_is_removed=1",
            "mod_test_object_touch_has_script=1",
            "mod_test_object_touch_just_touch=1",
            "mod_test_object_touch_can_select=0",
            "mod_test_object_trap_fixture_is_trap=1",
            "mod_test_object_trap_damage=7",
            "mod_test_object_trap_interval_positive=1",
            "mod_test_object_trap_last_cycle=-1",
            "mod_test_object_trap_cycle=0",
            "mod_test_object_timer_exists_before=1",
            "mod_test_object_timer_ran=1",
            "mod_test_object_timer_self_exists_before=1",
            "mod_test_object_timer_self_action_before=0",
            "mod_test_object_timer_self_offset_x=13",
            "mod_test_object_timer_self_offset_y=-9",
            "mod_test_object_timer_open_action=2",
            "mod_test_object_timer_close_action=3",
            "mod_test_object_timer_open_obj_action=2",
            "mod_test_object_timer_self_exists_after=0",
            "mod_test_object_timer_self_removed=1",
            "mod_test_object_timer_self_deleted=1",
            "mod_test_object_interacted=1",
            "mod_test_object_interacted_right=1",
            "mod_test_object_right_only_explicit_left_blocked=1",
            "mod_test_object_right_only_primary_ran=1",
            "mod_test_object_interact_right_missing_blocked=1",
            "mod_test_object_interact_action_queued=1",
            "mod_test_npc_shop_death_has_buy_file=1",
            "mod_test_npc_shop_death_has_buy_string=1",
            "mod_test_npc_shop_death_dead=1",
            "mod_test_npc_shop_death_drop=1",
            "mod_test_npc_interact_has_right=1",
            "mod_test_npc_interact_has_buy_file=1",
            "mod_test_npc_interact_has_buy_string=1",
            "mod_test_npc_interact_right_missing_blocked=1",
            "mod_test_npc_interact_action_queued=1",
            "mod_test_npc_interacted_right=1",
            "mod_test_object_touched=1",
        ]
    )
    object_trap_damage_expect_variables = ";".join(
        [
            "mod_test_object_trap_damage_ready=1",
            "mod_test_object_trap_player_mapx_before=35",
            "mod_test_object_trap_player_mapy_before=20",
            "mod_test_object_trap_player_trap_mapx=35",
            "mod_test_object_trap_player_trap_mapy=20",
            "mod_test_object_trap_player_trap_damage=200",
            "mod_test_object_trap_player_setup_position_match=1",
            "mod_test_object_trap_player_damage_over_defend=1",
            "mod_test_object_trap_damage_recorded=1",
            "mod_test_object_trap_player_record_position_match=1",
            "mod_test_object_trap_player_hit=1",
            "mod_test_object_trap_npc_hit=1",
            "mod_test_object_trap_player_cycle_recorded=1",
            "mod_test_object_trap_npc_cycle_recorded=1",
        ]
    )
    player_control_expect_variables = ";".join(
        [
            "mod_test_player_control_ready=1",
            "mod_test_player_run_initial=0",
            "mod_test_player_can_run_initial=1",
            "mod_test_player_run_disabled=1",
            "mod_test_player_can_run_disabled=0",
            "mod_test_player_run_enabled=0",
            "mod_test_player_can_run_enabled=1",
            "mod_test_player_jump_initial=0",
            "mod_test_player_can_jump_initial=1",
            "mod_test_player_jump_disabled=1",
            "mod_test_player_can_jump_disabled=0",
            "mod_test_player_jump_enabled=0",
            "mod_test_player_can_jump_enabled=1",
            "mod_test_player_fight_initial=0",
            "mod_test_player_can_fight_initial=1",
            "mod_test_player_fight_disabled=1",
            "mod_test_player_can_fight_disabled=0",
            "mod_test_player_fight_enabled=0",
            "mod_test_player_can_fight_enabled=1",
            "mod_test_player_run_disabled_after_load=1",
            "mod_test_player_can_run_disabled_after_load=0",
            "mod_test_player_jump_disabled_after_load=1",
            "mod_test_player_can_jump_disabled_after_load=0",
            "mod_test_player_fight_disabled_after_load=1",
            "mod_test_player_can_fight_disabled_after_load=0",
            "mod_test_player_can_use_mana=1",
            "mod_test_player_fight_state=0",
            "mod_test_player_body_function_well=1",
            "mod_test_player_controled_magic_sprite=0",
            "mod_test_player_moved_by_magic_sprite=0",
            "mod_test_player_magic_from_cache=0",
            "mod_test_player_full_life=1",
            "mod_test_player_in_fighting=0",
            "mod_test_player_playgoto_x=31",
            "mod_test_player_playgoto_y=20",
            "mod_test_player_walkto_x=32",
            "mod_test_player_walkto_y=20",
            "mod_test_player_walktodir_alias=1",
            "mod_test_player_walkto_nonblocking_alias=1",
            "mod_test_player_runto_nonblocking_alias=1",
        ]
    )
    magic_transport_control_expect_variables = ";".join(
        [
            "mod_test_magic_transport_control_ready=1",
            "mod_test_magic_transport_before=0",
            "mod_test_magic_transport_active=1",
            "mod_test_magic_transport_visible_active=0",
            "mod_test_magic_transport_obstacle_active=0",
            "mod_test_magic_transport_after=0",
            "mod_test_magic_transport_moved=1",
            "mod_test_magic_transport_visible_after=1",
            "mod_test_magic_transport_obstacle_after=1",
            "mod_test_magic_control_before=0",
            "mod_test_magic_control_active=1",
            "mod_test_magic_control_camera_follow_active=1",
            "mod_test_magic_control_target_match=1",
            "mod_test_magic_control_target_kind=1",
            "mod_test_magic_control_target_raw_relation=1",
            "mod_test_magic_control_target_runtime_relation=0",
            "mod_test_magic_control_npc_controlled=1",
            "mod_test_magic_control_npc_runtime_relation=0",
            "mod_test_magic_control_npc_raw_relation=1",
            "mod_test_magic_control_runtime_friend=1",
            "mod_test_magic_control_watcher_before=0",
            "mod_test_magic_control_watcher_active=1",
            "mod_test_magic_control_watcher_target_match=1",
            "mod_test_magic_control_after=0",
            "mod_test_magic_control_camera_follow_after=0",
            "mod_test_magic_control_npc_after=0",
            "mod_test_magic_control_npc_runtime_after=1",
            "mod_test_magic_control_watcher_after=0",
            "mod_test_magic_control_death_active=1",
            "mod_test_magic_control_death_camera_active=1",
            "mod_test_magic_control_death_npc_controlled=1",
            "mod_test_magic_control_death_after=0",
            "mod_test_magic_control_death_camera_after=0",
            "mod_test_magic_control_death_npc_after=0",
        ]
    )
    choose_multiple_expect_variables = ";".join(
        [
            "mod_test_choose_multiple_ready=1",
            "__automation_choose_multiple_consumed=1",
            "__automation_choose_multiple_valid_count=2",
            "__automation_choose_multiple_complete=1",
            "$mod_test_choose_multiple0=1",
            "$mod_test_choose_multiple1=2",
            "$mod_test_choose_multiple2=99",
            "mod_test_choose_multiple_result_ok=1",
            "mod_test_choose_multiple_hidden_ignored=1",
            "mod_test_choose_multiple_no_extra_write=1",
            "mod_test_choose_multiple_duplicate_ignored=1",
            "mod_test_choose_multiple_duplicate_no_extra_write=1",
        ]
    )
    choose_ex_plus_expect_variables = ";".join(
        [
            "mod_test_choose_ex_plus_ready=1",
            "mod_test_choose_ex_result=3",
            "mod_test_choose_ex_result_ok=1",
            "mod_test_choose_ex_filter_ok=1",
            "mod_test_choose_plus_result=2",
            "mod_test_choose_plus_result_ok=1",
            "mod_test_choose_plus_filter_ok=1",
            "__automation_choose_consumed=1",
            "__automation_choose_visible_count=2",
            "__automation_choose_complete=1",
        ]
    )
    choose_menu_visual_expect_variables = ";".join(
        [
            "mod_test_choose_menu_visual_ready=1",
            "mod_test_choose_menu_visual_result=11",
            "mod_test_choose_menu_visual_last_index_ok=1",
            "mod_test_choose_menu_visual_right_result=0",
            "mod_test_choose_menu_visual_right_ok=1",
        ]
    )
    npc_kind_talent_expect_variables = ";".join(
        [
            "mod_test_npc_kind_talent_ready=1",
            "mod_test_npc_kind_talent_exists=1",
            "mod_test_npc_kind_initial=500",
            "mod_test_npc_kind_max=1000",
            "mod_test_npc_kind_after_plus=1000",
            "mod_test_npc_kind_after_minus=0",
            "mod_test_npc_kind_clamp_ok=1",
            "mod_test_player_talent_before=0",
            "mod_test_player_talent_after=1",
            "mod_test_add_talent_ok=1",
        ]
    )
    npc_signal_tip_expect_variables = ";".join(
        [
            "mod_test_npc_signal_tip_ready=1",
            "mod_test_npc_signal_tip_exists=1",
            "mod_test_npc_signal_initial_show=0",
            "mod_test_npc_signal_initial_index=0",
            "mod_test_npc_signal_show_after_show=1",
            "mod_test_npc_signal_index_after_show=23",
            "mod_test_npc_signal_t1_after_show=1",
            "mod_test_npc_signal_t0_after_show=0",
            "mod_test_npc_signal_show_after_hide=0",
            "mod_test_npc_signal_index_after_hide=23",
            "mod_test_npc_signal_show_hide_ok=1",
            "mod_test_npc_signal_map_npc_exists=1",
            "mod_test_npc_signal_map_show=1",
            "mod_test_npc_signal_map_index=24",
            "mod_test_npc_signal_map_t0=1",
            "mod_test_npc_signal_map_t1=0",
            "mod_test_npc_signal_setattr_ok=1",
        ]
    )
    script_sound_position_expect_variables = ";".join(
        [
            "mod_test_script_sound_position_ready=1",
            "mod_test_script_sound_global_fallback_ok=1",
            "mod_test_script_sound_obj_ran=1",
            "mod_test_script_sound_obj_has_pos=1",
            "mod_test_script_sound_obj_source=2",
            "mod_test_script_sound_obj_mapx=37",
            "mod_test_script_sound_obj_mapy=20",
            "mod_test_script_sound_obj_position_ok=1",
            "mod_test_script_sound_npc_queued=1",
            "mod_test_script_sound_npc_ran=1",
            "mod_test_script_sound_npc_has_pos=1",
            "mod_test_script_sound_npc_source=1",
            "mod_test_script_sound_npc_mapx=36",
            "mod_test_script_sound_npc_mapy=21",
            "mod_test_script_sound_npc_position_ok=1",
        ]
    )
    magic_self_special_expect_variables = ";".join(
        [
            "mod_test_magic_self_special_ready=1",
            "mod_test_magic_self_block_damage_ok=1",
            "mod_test_magic_self_clear_frozen_ok=1",
            "mod_test_magic_self_clear_poison_ok=1",
            "mod_test_magic_self_clear_petrify_ok=1",
            "mod_test_magic_self_clear_body=1",
        ]
    )
    magic_trail_expect_variables = ";".join(
        [
            "mod_test_magic_trail_ready=1",
            "mod_test_magic_trail_state=1",
            "mod_test_magic_trail_first_count=1",
            "mod_test_magic_trail_first_position=1",
            "mod_test_magic_trail_second_count=2",
            "mod_test_magic_trail_no_after_expire=1",
        ]
    )
    magic_region_vtype_expect_variables = ";".join(
        [
            "mod_test_magic_region_vtype_ready=1",
            "mod_test_magic_region_vtype_has_type=1",
            "mod_test_magic_region_vtype_has_injury_type=1",
            "mod_test_magic_region_vtype_sprite_type=7",
            "mod_test_magic_region_vtype_has_sprite_type=1",
            "mod_test_magic_region_vtype_attribute=3",
            "mod_test_magic_region_vtype_has_attribute=1",
            "mod_test_magic_region_vtype_has_script_file=1",
            "mod_test_magic_region_vtype_range_add_rage=5",
            "mod_test_magic_region_vtype_has_range_add_rage=1",
            "mod_test_magic_region_vtype_rage_cost=100",
            "mod_test_magic_region_vtype_has_rage_cost=1",
            "mod_test_magic_region_vtype_movekind=11",
            "mod_test_magic_region_vtype_region=5",
            "mod_test_magic_region_vtype_hit_tile_can_fly=1",
            "mod_test_magic_region_vtype_hit_x=34",
            "mod_test_magic_region_vtype_hit_y=17",
            "mod_test_magic_region_vtype_hit_visible=1",
            "mod_test_magic_region_vtype_hit_relation=1",
            "mod_test_magic_region_vtype_effect_count=5",
            "mod_test_magic_region_vtype_projectile_launcher=2",
            "mod_test_magic_region_vtype_projectile_user_is_player=1",
            "mod_test_magic_region_vtype_projectile_x=34",
            "mod_test_magic_region_vtype_projectile_y=18",
            "mod_test_magic_region_vtype_hit=1",
            "mod_test_magic_region_vtype_safe=1",
        ]
    )
    script_timer_parallel_expect_variables = ";".join(
        [
            "mod_test_script_timer_parallel_ready=1",
            "mod_test_script_timer_triggered=1",
            "mod_test_script_parallel_immediate=1",
            "mod_test_script_parallel_delayed=1",
            "mod_test_script_messagebox_alias=1",
            "mod_test_script_message_alias=1",
            "mod_test_script_assing_alias=1",
            "mod_test_script_setvar_alias=1",
            "mod_test_script_sub_command=3",
            "mod_test_script_system_message_alias=1",
            "mod_test_script_enabeldrop_alias=1",
            "mod_test_script_getgoodsmun_alias=2",
            "mod_test_script_runscirpt_alias=1",
            "mod_test_script_setplayrdir_alias=5",
            "mod_test_script_centercamera_alias=1",
            "mod_test_script_emotion=1",
            "mod_test_script_emotion_alias=1",
            "mod_test_script_justice=1",
            "mod_test_script_justice_alias=1",
            "mod_test_script_freemap_loaded_before=1",
            "mod_test_script_freemap_rows_before_positive=1",
            "mod_test_script_freemap_object_before=1",
            "mod_test_script_freemap_npc_before=1",
            "mod_test_script_freemap_loaded_after=0",
            "mod_test_script_freemap_rows_after=0",
            "mod_test_script_freemap_object_after=1",
            "mod_test_script_freemap_npc_after=1",
        ]
    )
    script_return_api_expect_variables = ";".join(
        [
            "mod_test_script_return_api_ready=1",
            "mod_test_return_money=12345",
            "mod_test_return_player_snapshot_modified_money=77",
            "mod_test_return_player_snapshot_money=12345",
            "mod_test_return_player_full_life=1",
            "mod_test_return_goods_space=1",
            "mod_test_return_magic_space=1",
            "mod_test_return_goods_file_count=2",
            "mod_test_return_goods_name_count=2",
            "mod_test_return_goods_snapshot_cleared=0",
            "mod_test_return_goods_snapshot_restored=2",
            "mod_test_return_magic_level=1",
            "mod_test_return_map_has_map=1",
            "mod_test_return_map_width_positive=1",
            "mod_test_return_map_height_positive=1",
            "mod_test_return_map_outside=0",
            "mod_test_return_map_outside_obstacle=-1",
            "mod_test_return_leechcraft_required=3",
            "mod_test_return_leechcraft_short=2",
            "mod_test_return_leechcraft_success=-1",
            "mod_test_return_leechcraft_missing=0",
            "mod_test_return_npc_x=36",
            "mod_test_return_npc_y=21",
            "mod_test_return_obj_x=37",
            "mod_test_return_obj_y=21",
            "mod_test_return_partner_index_non_negative=1",
        ]
    )
    magic_summon_body_expect_variables = ";".join(
        [
            "mod_test_magic_summon_body_ready=1",
            "mod_test_magic_body_radius=2",
            "mod_test_magic_body_vibrating=6",
            "mod_test_magic_revive_radius=2",
            "mod_test_magic_revive_max_count=1",
            "mod_test_magic_revive_life_ms=1500",
            "mod_test_magic_body_medium_body_before=1",
            "mod_test_magic_body_medium_body_after=0",
            "mod_test_magic_body_medium_effect_seen=1",
            "mod_test_magic_revive_body_before=1",
            "mod_test_magic_revive_body_after=0",
            "mod_test_magic_revive_overflow_body_before=1",
            "mod_test_magic_revive_overflow_body_after=0",
            "mod_test_magic_revive_npc_exists=1",
            "mod_test_magic_revive_overflow_npc_exists=0",
            "mod_test_magic_revive_npc_relation=0",
            "mod_test_magic_revive_npc_x=36",
            "mod_test_magic_revive_npc_y=22",
            "mod_test_magic_revive_npc_dir=5",
            "mod_test_magic_revive_npc_life_ms_positive=1",
        ]
    )
    magic_temp_relation_expect_variables = ";".join(
        [
            "mod_test_magic_temp_relation_ready=1",
            "mod_test_magic_temp_relation_fields=1",
            "mod_test_magic_temp_relation_before=1",
            "mod_test_magic_temp_relation_flipped=1",
            "mod_test_magic_temp_relation_cancelled=1",
            "mod_test_magic_temp_relation_second_active=1",
            "mod_test_magic_temp_relation_restored=1",
            "mod_test_magic_temp_relation_player_immune=1",
        ]
    )
    npc_drop_expect_variables = ";".join(
        [
            "mod_test_npc_drop_ready=1",
            "mod_test_npc_drop_no_drop_flag_before=0",
            "mod_test_npc_drop_token_exists=1",
            "mod_test_npc_drop_token_is_drop=1",
            "mod_test_npc_drop_token_position=1",
            "mod_test_npc_drop_no_drop_flag=1",
            "mod_test_npc_drop_blocked_token_exists=0",
            "mod_test_npc_drop_no_drop_blocked=1",
            "mod_test_npc_default_drop_object_exists=1",
            "mod_test_npc_default_drop_script=1",
        ]
    )
    return "\n".join(
        [
            "[Scenario.runner]",
            "Status=ready",
            "Choice=0",
            "Smoke=0",
            "EntryScript=mod_test_runner.txt",
            "VisibleResult=ChooseEx scenario menu and selected scenario variable",
            "Requires=ChooseMenu",
            "",
            "[Scenario.bootstrap]",
            "Status=ready",
            "Choice=1",
            "Smoke=1",
            "EntryScript=mod_test_bootstrap.txt",
            "ExpectVariables=mod_test_ready=1;mod_test_hub_respawn_count=1;mod_test_hub_partner_sword_ready=1;mod_test_hub_partner_heroine_ready=1;mod_test_hub_partner_sword_attack_speed=1;mod_test_hub_partner_heroine_attack_speed=1;mod_test_hub_partner_sword_x=33;mod_test_hub_partner_sword_y=22;mod_test_hub_partner_heroine_x=35;mod_test_hub_partner_heroine_y=22;mod_test_hub_enemy_melee_ready=1;mod_test_hub_enemy_melee_attack_speed=1;mod_test_hub_enemy_melee_no_auto=0;mod_test_hub_enemy_melee_stop_find=0;mod_test_hub_enemy_ranged_ready=1;mod_test_hub_enemy_ranged_attack_speed=1;mod_test_hub_enemy_ranged_no_auto=0;mod_test_hub_enemy_ranged_stop_find=0",
            "VisibleResult=opens an interactive XJXQY test plaza with a guide, a manual little-game host, two combat partners reset to distinct adjacent tiles, and two northern enemies; all four combat fixtures use normal attack cadence",
            "Requires=base XJXQY save index 0 and map001 resources, NPC interaction, Gamble/Dice/Fish menus, SetNpcPartner, PartnerCombat, and active NPC AI",
            "",
            "[Scenario.script_gamble]",
            "Status=ready",
            "Choice=2",
            "Smoke=1",
            "EntryScript=mod_test_gamble.txt",
            "ExpectVariables=mod_test_gamble_ready=1;mod_test_gamble_loss_result=0;mod_test_gamble_loss_money=920;mod_test_gamble_loss_delta=-80;mod_test_gamble_leave_result=1;mod_test_gamble_leave_money=1000;mod_test_gamble_leave_delta=0;mod_test_gamble_automation_consumed=1",
            "VisibleResult=GambleMenu automation covers all-in loss settlement and no-bet leave result",
            "Requires=Gamble API, player money, and __automation_gamble_* test variables",
            "",
            "[Scenario.script_dice_game]",
            "Status=ready",
            "Choice=3",
            "Smoke=0",
            "EntryScript=mod_test_dice_game.txt",
            "ExpectVariables=mod_test_dice_opened=1",
            "VisibleResult=Dice mini-game opens and mod_test_dice_opened variable",
            "Requires=GambleMenu",
            "",
            "[Scenario.script_fish_game]",
            "Status=ready",
            "Choice=4",
            "Smoke=0",
            "EntryScript=mod_test_fish_game.txt",
            "ExpectVariables=mod_test_fish_opened=1",
            "VisibleResult=Fishing mini-game opens and mod_test_fish_opened variable",
            "Requires=GambleMenu",
            "",
            "[Scenario.script_steal]",
            "Status=ready",
            "Choice=5",
            "Smoke=1",
            "EntryScript=mod_test_steal.txt",
            "ExpectVariables=mod_test_steal_opened=1;mod_test_steal_success=1;mod_test_steal_fail=1;mod_test_steal_count_after_success=1;mod_test_steal_count_after_second=1;mod_test_steal_second_removed=1;mod_test_steal_count_after_fail=0;__automation_choose_consumed=1;__automation_choose_complete=1;__automation_choose_visible_count=2",
            "VisibleResult=loads a safe XJXQY map, automates ChooseMenu item selection, adds MOD_TEST_STEAL_TOKEN once, verifies BagGoods removal prevents a repeated steal, and high Steal threshold executes the fail script without adding goods",
            "Requires=neutral NPC fixtures with BagGoods and Steal fields plus ChooseMenu automation variables",
            "",
            "[Scenario.magic_lifecycle]",
            "Status=ready",
            "Choice=6",
            "Smoke=1",
            "EntryScript=mod_test_magic_lifecycle.txt",
            "ExpectVariables=mod_test_magic_lifecycle_ready=1;mod_test_magic_begin_follow=1;mod_test_magic_trace_enemy_far_target=1;mod_test_magic_meteor=1;mod_test_magic_meteor_entry_path=1;mod_test_magic_random_projectile_count=1;mod_test_magic_random_direction_changed=1;mod_test_magic_round=1;mod_test_magic_round_spawned=1;mod_test_magic_round_rotated=1;mod_test_magic_move_imitate_projectile_count=1;mod_test_magic_move_imitate_moved=1;mod_test_magic_moveback=1;mod_test_magic_time_stop=1;mod_test_magic_time_stop_active=1;mod_test_magic_invisible_keep_active=1;mod_test_magic_invisible_keep_attack_flag=0;mod_test_magic_invisible_keep_after_action=1;mod_test_magic_invisible_attack_active=1;mod_test_magic_invisible_attack_flag=1;mod_test_magic_invisible_attack_cleared=1;mod_test_magic_morph_replace_before=0;mod_test_magic_morph_replace_active=1;mod_test_magic_morph_visible_two=1;mod_test_magic_morph_primary_preserved=1;mod_test_magic_morph_primary_add_recorded=1;mod_test_magic_morph_visible_add_isolated=1;mod_test_magic_morph_replace_after=0;mod_test_magic_morph_visible_restored=1;mod_test_magic_morph_primary_add_restored_visible=1;mod_test_magic_learned_passive_attack_overlay=1;mod_test_magic_learned_passive_counter=1;mod_test_magic_clear_effect_status=1;mod_test_magic_clear_effect_frozen=1;mod_test_magic_clear_effect_poison=1;mod_test_magic_clear_effect_petrify=1;mod_test_magic_clear_effect_body=1;mod_test_magic_clear_effect_partner_status=1",
            "VisibleResult=loads a safe XJXQY map, grants and releases BeginAtMouse/FollowMouse, records TraceEnemy retarget toward a far enemy after delay, records MeteorMove entry-path state, records RandomMoveDegree direction perturbation, RoundMove, MoveImitateUser, MoveBack, MoveKind=23 time-stop active effect state, invisibility, player Morph/ReplaceMagic with primary-list isolation, learned-magic passive attack option and be-attacked trigger state, and ClearEffect player/partner abnormal-state cleanup test magic",
            "Requires=base XJXQY map001 resources, inherited magic ASF/Sound resources, and GetPlayerState replacement-list/primary-list readback",
            "",
            "[Scenario.magic_collision]",
            "Status=ready",
            "Choice=7",
            "Smoke=1",
            "EntryScript=mod_test_magic_collision.txt",
            "TimeoutSeconds=180",
            "ExpectVariables=mod_test_magic_collision_ready=1;mod_test_magic_state_exists=1;mod_test_magic_state_count_init=3;mod_test_magic_state_count_level1=4;mod_test_magic_state_special_value_init=7;mod_test_magic_state_special_value_level1=8;mod_test_magic_state_no_effect=1;mod_test_magic_state_max_count=2;mod_test_magic_state_file_name=1;mod_test_magic_state_current_level=2;mod_test_magic_state_effect_level=2;mod_test_magic_state_item_info=1;mod_test_magic_summon_count_before=0;mod_test_magic_summon_exists=1;mod_test_magic_summon_attached=1;mod_test_magic_summon_owner_player=1;mod_test_magic_summon_launcher=2;mod_test_magic_summon_relation=0;mod_test_magic_summon_count_first=1;mod_test_magic_summon_count_plural_first=1;mod_test_magic_summon_count_second=1;mod_test_magic_summon_second_tile_added=1;mod_test_magic_summon_maxcount_replaced=1;mod_test_magic_ball=1;mod_test_magic_ball_projectile=1;mod_test_magic_ball_reflected=1;mod_test_magic_fly_magic_has_child=1;mod_test_magic_fly_magic_interval=80;mod_test_magic_fly_magic_child_spawned=1;mod_test_magic_damage_channels_fields=1;mod_test_magic_damage_channels_life_delta=1;mod_test_magic_damage_channels_effect_ext_delta=1;mod_test_magic_damage_channels_mana_delta=1;mod_test_magic_leap=1;mod_test_magic_leap_fields=1;mod_test_magic_leap_target1_damage=1;mod_test_magic_leap_target2_damage=1;mod_test_magic_leap_retarget=1;mod_test_magic_restore_fields=1;mod_test_magic_restore_life_owner=1;mod_test_magic_restore_life_target_damage=1;mod_test_magic_restore_mana_owner=1;mod_test_magic_restore_mana_target_damage=1;mod_test_magic_restore_thew_owner=1;mod_test_magic_restore_thew_target_damage=1;mod_test_magic_ball_wall=1;mod_test_magic_ball_wall_reflected=1;mod_test_magic_attack_all_projectile=1;mod_test_magic_attack_all_projectile_state=1;mod_test_magic_attack_all_projectile_friend_ready=1;mod_test_magic_attack_all_projectile_exploded=1;mod_test_magic_attack_all_projectile_friendly_damage=1;mod_test_magic_wall_explode=1;mod_test_magic_wall_exploded_on_wall=1;mod_test_magic_pass_through=1;mod_test_magic_pass_through_state=1;mod_test_magic_pass_through_target1_damage=1;mod_test_magic_pass_through_target2_damage=1;mod_test_magic_pass_through_hits=1;mod_test_magic_pass_through_destroy_effect=1;mod_test_magic_sticky=1;mod_test_magic_sticky_attached=1;mod_test_magic_solid=1;mod_test_magic_solid_obstacle=1;mod_test_magic_solid_blocks_walk=1;mod_test_magic_solid_clears_effect=1;mod_test_magic_parasitic=1;mod_test_magic_parasitic_active_seen=1;mod_test_magic_parasitic_total_seen=1;mod_test_magic_range_speedup=1;mod_test_magic_range_speedup_effect_seen=1;mod_test_magic_range_speedup_active_seen=1;mod_test_magic_range_speedup_fold_seen=1;mod_test_magic_range_speedup_expired=1;mod_test_magic_range_speedup_reapplied=1;mod_test_magic_range_speedup_reapply_fold_seen=1;mod_test_magic_range_speedup_cleared=1;mod_test_magic_range_attack=1;mod_test_magic_range_attack_state=1;mod_test_magic_range_attack_frozen=1;mod_test_magic_range_attack_damage=1;mod_test_magic_range_attack_no_mana_damage=1;mod_test_magic_bounce=1;mod_test_magic_bounce_active_seen=1;mod_test_magic_bounce_action_seen=1;mod_test_magic_bounce_velocity_seen=1;mod_test_magic_bounce_moved=1;mod_test_magic_bounce_hurt=1;mod_test_magic_bounce_hurt_applied=1;mod_test_magic_bounce_npc_blocked=1;mod_test_magic_bounce_player_block=1;mod_test_magic_bounce_player_blocked=1;mod_test_magic_bounce_player_blocked_position=1;mod_test_magic_bouncefly=1;mod_test_magic_bouncefly_active=1;mod_test_magic_bouncefly_action=1;mod_test_magic_bouncefly_destination_changed=1;mod_test_magic_bouncefly_speed=32;mod_test_magic_bouncefly_end_hurt=3;mod_test_magic_bouncefly_touch_hurt=2;mod_test_magic_bouncefly_touch_distance=3;mod_test_magic_bouncefly_progress_seen=1;mod_test_magic_bouncefly_bezier_seen=1;mod_test_magic_bouncefly_touch_direction_preserved=1;mod_test_magic_forced_handoff=1;mod_test_magic_forced_handoff_bounce_seen=1;mod_test_magic_forced_handoff_cast_forced=1;mod_test_magic_forced_handoff_forced_seen=1;mod_test_magic_forced_handoff_action_forced=1;mod_test_magic_bouncefly_end_magic=1;mod_test_magic_bouncefly_block_characters=1;mod_test_magic_bouncefly_touch=1;mod_test_magic_bouncefly_end_hurt_applied=1;mod_test_magic_bouncefly_touch_hurt_applied=1;mod_test_magic_bouncefly_multi_touch=1;mod_test_magic_bouncefly_player_blocked=1;mod_test_magic_carry_user4=1;mod_test_magic_carry_user4_carry_active_seen=1;mod_test_magic_carry_user4_attached=1;mod_test_magic_carry_user4_target_moved=1;mod_test_magic_carry_user4_multi_attached=1;mod_test_magic_carry_user4_neighbor_hurt=1;mod_test_magic_carry_user4_hide=1;mod_test_magic_carry_user4_hide_hidden_seen=1;mod_test_magic_carry_user4_hide_exploding_hidden_seen=0;mod_test_magic_carry_user4_hide_restored=1;mod_test_magic_carry_user1_hide_hidden_seen=1;mod_test_magic_carry_user1_hide_exploding_hidden_seen=1;mod_test_magic_carry_user1_hide_player_moved=1;mod_test_magic_carry_user1_hide_restored=1;mod_test_magic_carry_user_hidden_contract=1;mod_test_magic_discard_opposite=1;mod_test_magic_discard_capable=1;mod_test_magic_discard_cleared=1;mod_test_magic_exchange_user=1;mod_test_magic_exchange_transferred=1;mod_test_magic_exchange_launcher_transferred=1;mod_test_magic_exchange_direction_combined=1;mod_test_magic_status_duration_fields=1;mod_test_magic_status_freeze_preserved=1;mod_test_magic_status_duration_freeze=1;mod_test_magic_status_duration_poison=1;mod_test_magic_status_duration_petrify=1;mod_test_magic_lethal_freeze_status=1",
            "ExpectVariablesExtra=mod_test_magic_leap_target1_single_hit=1;mod_test_magic_attack_all_leap=1;mod_test_magic_attack_all_leap_state=1;mod_test_magic_attack_all_leap_friend_fighter=1;mod_test_magic_attack_all_leap_target1_single_hit=1;mod_test_magic_attack_all_leap_friendly_damage=1;mod_test_magic_attack_all_leap_retarget=1;mod_test_magic_attack_all_leap_partner=1;mod_test_magic_attack_all_leap_partner_ready=1;mod_test_magic_attack_all_leap_partner_target1_single_hit=1;mod_test_magic_attack_all_leap_partner_damage=1;mod_test_magic_attack_all_leap_partner_retarget=1;mod_test_magic_attack_all_trace_enemy=1;mod_test_magic_attack_all_trace_enemy_state=1;mod_test_magic_attack_all_trace_enemy_nonfighter_ready=1;mod_test_magic_attack_all_trace_enemy_partner_ready=1;mod_test_magic_attack_all_trace_enemy_fighter_trace=1;mod_test_magic_partner_projectile_gate=1;mod_test_magic_partner_projectile_ready=1;mod_test_magic_partner_projectile_disabled_gate=1;mod_test_magic_partner_projectile_enabled_hit=1;mod_test_magic_partner_projectile_enabled_stopped=1;mod_test_magic_range_attack_all_partner_ready=1;mod_test_magic_range_attack_all_partner_status=1",
            "VisibleResult=loads a safe XJXQY map, disables NPC AI, reads C# Magic.Count/SpecialKindValue fields through GetMagicState, launches Summon/Ball/FlyMagic/Effect/EffectExt/Effect2/Effect3/EffectMana/LeapTimes/Restore/AttackAllProjectile/AttackAllLeap/AttackAllTraceEnemy/Wall/PassThrough/Sticky/Solid/Parasitic/RangeSpeedUp/RangeAttack/Bounce/BounceFly/CarryUser=4/Discard/Exchange/status-duration/lethal-freeze fixtures, and asserts summon ownership plus MaxCount replacement at a new tile, Ball character reflection, FlyMagic interval readback and child projectile spawn, multi-channel field readback plus EffectExt primary damage addition and life/mana damage state, LeapTimes field readback plus first-target single-hit and two-target retarget damage state, AttackAll LeapTimes friendly-fighter and disabled-combat partner retarget, Restore field readback plus caster life/mana/thew recovery and target damage state, Ball wall reflection, AttackAll projectile friendly-fighter hit, AttackAll TraceEnemy disabled-combat partner retarget over a nearer non-fighter, normal projectile wall explosion, PassThrough field readback plus two-target hit and destroy effect, Solid obstacle state plus dynamic map walk blocking and solid-effect clear, Parasitic target binding, RangeSpeedUp owner state, natural expiry, and reapply state, RangeAttack field readback plus enemy freeze/damage and RangeDamage non-inherited EffectMana, Bounce/BounceFly forced-move state, Bounce NPC/player collision positions, BounceFly preserved touch direction/neighbor TouchHurt/player path blocking, Bounce followed by BounceFly replacement into acMagicForcedMove, CarryUser=4 target motion plus HideUserWhenCarry draw suppression/restoration, CarryUser=1 hidden explosion/restoration, Discard capability and projectile clear, Exchange owner/launcher transfer, SpecialKindMilliSeconds freeze/poison/petrify states on non-MoveKind=2 hits plus existing-freeze preservation, pre-damage status on lethal hit, and Effect ownership/attached/projectile state through GetNpcState/GetEffectState/GetMapState",
            "Requires=hostile no-AI target/caster NPC fixtures and inherited magic ASF/Sound resources",
            "",
            "[Scenario.equipment_trigger]",
            "Status=ready",
            "Choice=8",
            "Smoke=1",
            "EntryScript=mod_test_equipment_trigger.txt",
            "ExpectVariables=mod_test_equipment_trigger_ready=1;mod_test_equipment_wrong_part_weapon=0;mod_test_equipment_user_restricted_weapon=0;mod_test_equipment_level_low_weapon=0;mod_test_equipment_level_ok_weapon=1;mod_test_equipment_weapon_before=0;mod_test_equipment_attack_additional_before=0;mod_test_equipment_weapon_equipped=1;mod_test_equipment_flyini=1;mod_test_equipment_flyini2=1;mod_test_equipment_counter=1;mod_test_equipment_lifemax_bonus=10;mod_test_equipment_thewmax_bonus=5;mod_test_equipment_manamax_bonus=5;mod_test_equipment_attack_bonus=3;mod_test_equipment_attack2_bonus=2;mod_test_equipment_attack3_bonus=1;mod_test_equipment_defend_bonus=4;mod_test_equipment_defend2_bonus=3;mod_test_equipment_defend3_bonus=2;mod_test_equipment_evade_bonus=1;mod_test_equipment_life_restore_percent=10;mod_test_equipment_attack_additional_effect=1;mod_test_equipment_additional_effect_alias=1;mod_test_equipment_lifemax_increased=1;mod_test_equipment_thewmax_increased=1;mod_test_equipment_manamax_increased=1;mod_test_equipment_attack_increased=1;mod_test_equipment_defend_increased=1;mod_test_equipment_evade_increased=1;mod_test_equipment_life_restored=1;mod_test_equipment_speed_percent=25;mod_test_equipment_speed_fold=1250;mod_test_equipment_magic_name_count=1;mod_test_equipment_magic_type_count=0;mod_test_equipment_magic_name_percent=30;mod_test_equipment_magic_name_amount=2;mod_test_equipment_has_replace_magic=1;mod_test_equipment_has_use_replace_magic=1;mod_test_equipment_magic_damage_applied=1;mod_test_equipment_replace_magic_applied=1;mod_test_equipment_magic_name_count_after_parasitic_bonus=2;mod_test_equipment_parasitic_tick_bonus=1;mod_test_equipment_magic_additional_effect_state=1;mod_test_equipment_additional_status=1",
            "VisibleResult=loads a safe XJXQY map, rejects wrong-slot/User/low-level EquipGoods cases, equips MOD_TEST_EQUIPMENT_TRIGGER to Hand, grants FlyIni/FlyIni2/be-attacked and ReplaceMagic fixtures, and verifies C# equipment stat, restore, speed, direct magic-effect, ReplaceMagic, parasitic tick magic-effect bonuses, and Magic.AdditionalEffect trigger/readback status",
            "Requires=test equipment goods, User/MinUserLevel restriction fixtures, and inherited magic ASF/Sound resources",
            "",
            "[Scenario.goods_pricing]",
            "Status=ready",
            "Choice=9",
            "Smoke=1",
            "EntryScript=mod_test_goods_pricing.txt",
            "ExpectVariables=mod_test_goods_pricing_ready=1;mod_test_goods_state_drug_exists=1;mod_test_goods_state_drug_effect_type=4;mod_test_goods_state_drug_effect_type_raw=1;mod_test_goods_state_drug_file_name=1;mod_test_goods_state_drug_cost_raw=92;mod_test_goods_state_drug_buy_price=92;mod_test_goods_state_drug_sell_price=46;mod_test_goods_state_equipment_exists=1;mod_test_goods_state_equipment_kind=1;mod_test_goods_state_equipment_effect_type=3;mod_test_goods_state_equipment_effect_type_raw=1;mod_test_goods_state_equipment_cost_raw=77;mod_test_goods_state_equipment_sell_set=1;mod_test_goods_state_equipment_sell_price=33;mod_test_goods_state_equipment_has_part=1;mod_test_goods_state_equipment_attack=3;mod_test_goods_state_equipment_defend=4;mod_test_goods_state_equipment_evade=1;mod_test_goods_state_noneed_cost_raw=0;mod_test_goods_state_noneed_flag=1;mod_test_goods_state_noneed_lifemax=20;mod_test_goods_state_noneed_manamax=10;mod_test_goods_state_random_has_rand=1;mod_test_goods_state_random_has_magic_name=1;mod_test_goods_state_random_has_magic=1;mod_test_goods_pricing_drug_count=1;mod_test_goods_pricing_equipment_count=1;mod_test_goods_pricing_noneed_count=1",
            "VisibleResult=reads C#-style goods derived state and MagicName/MagicIniWhenUse cross-reference flags through GetGoodsState, then adds drug, explicit-price equipment, and no-need equipment pricing fixtures and records inventory counts",
            "Requires=test goods pricing INI fixtures",
            "",
            "[Scenario.goods_random]",
            "Status=ready",
            "Choice=10",
            "Smoke=1",
            "EntryScript=mod_test_goods_random.txt",
            "ExpectVariables=mod_test_goods_random_ready=1;mod_test_goods_random_count=2;mod_test_goods_random_template_count=0;mod_test_goods_random_instanced=1",
            "VisibleResult=adds two random equipment instances, verifies display-name counting, and confirms the random template file name is not stored directly",
            "Requires=test random goods INI fixture and save/game runtime generated goods support",
            "",
            "[Scenario.goods_lifecycle]",
            "Status=ready",
            "Choice=11",
            "Smoke=1",
            "EntryScript=mod_test_goods_lifecycle.txt",
            "ExpectVariables=mod_test_goods_lifecycle_ready=1;mod_test_goods_lifecycle_delete_before=2;mod_test_goods_lifecycle_delete_after_file=1;mod_test_goods_lifecycle_delete_after_name=0;mod_test_goods_lifecycle_name_case_upper=0;mod_test_goods_lifecycle_name_case_exact=1;mod_test_goods_lifecycle_name_case_after_delete=0;mod_test_goods_lifecycle_noneed_load_count=1;mod_test_goods_lifecycle_magic_before=0;mod_test_goods_lifecycle_magic_equipped=1;mod_test_goods_lifecycle_magic_after_clear=0;mod_test_goods_lifecycle_magic_reequipped=1;mod_test_goods_lifecycle_learned_before=2;mod_test_goods_lifecycle_learned_equipped=2;mod_test_goods_lifecycle_learned_duplicate=2;mod_test_goods_lifecycle_learned_after_clear=2;mod_test_goods_friend_drug_life_before=50;mod_test_goods_friend_drug_life_after=75;mod_test_goods_friend_drug_lifemax_before=100;mod_test_goods_friend_drug_lifemax_after=107;mod_test_goods_friend_drug_thewmax_after=103;mod_test_goods_friend_drug_manamax_after=102;mod_test_goods_player_friend_drug_lifemax_increased=1;mod_test_goods_friend_drug_count_before=1;mod_test_goods_friend_drug_count_after=0;mod_test_goods_friend_drug_applied=1;mod_test_goods_friend_clear_poison_fighter_flag=1;mod_test_goods_friend_clear_poison_effect_type=2;mod_test_goods_friend_clear_poison_before=1;mod_test_goods_friend_clear_poison_after=0;mod_test_goods_friend_clear_poison_count_before=1;mod_test_goods_friend_clear_poison_count_after=0;mod_test_goods_friend_clear_poison_ok=1;mod_test_goods_partner_drug_is_partner=1;mod_test_goods_partner_drug_life_before=60;mod_test_goods_partner_drug_life_after=95;mod_test_goods_partner_drug_lifemax_before=100;mod_test_goods_partner_drug_lifemax_after=111;mod_test_goods_partner_drug_thewmax_after=104;mod_test_goods_partner_drug_manamax_after=105;mod_test_goods_player_partner_drug_lifemax_increased=1;mod_test_goods_partner_drug_count_before=1;mod_test_goods_partner_drug_count_after=0;mod_test_goods_partner_drug_applied=1;mod_test_goods_lifecycle_bound_before=2;mod_test_goods_lifecycle_bound_after_one=1;mod_test_goods_lifecycle_bound_after_two=0;mod_test_goods_lifecycle_bound_after_missing=0;mod_test_goods_lifecycle_script_before=2;mod_test_goods_lifecycle_script_after_first=1;mod_test_goods_lifecycle_script_remaining=0;mod_test_goods_lifecycle_script_used=2;mod_test_goods_lifecycle_script_magic_level=1",
            "ExpectSavedGoodsSlots=9|mod_test_goods_lifecycle_stack.ini|2|1",
            "VisibleResult=checks case-insensitive DelGoods one-item deletion, exact GetGoodsNumByName display-name counting, case-insensitive DelGoodByName count deletion, NoNeedToEquip save/load, MagicIniWhenUse equip/hide lifecycle, learned same-magic hide-count preservation, friend-fighter numeric and clear-poison drug propagation, partner drug propagation, Magic.GoodsName consumption through UseMagic, and goods-script DelGoods() one-item self deletion",
            "Requires=test goods lifecycle INI fixtures, friend/partner drug fixtures, bound magic goods fixture, goods script fixture, and inherited magic resources",
            "",
            "[Scenario.npc_ai]",
            "Status=ready",
            "Choice=12",
            "Smoke=1",
            "EntryScript=mod_test_npc_ai.txt",
            "TimeoutSeconds=180",
            "PostNewGameWaitMilliseconds=5000",
            f"ExpectVariables={npc_ai_expect_variables}",
            "VisibleResult=loads a safe XJXQY map, disables NPC AI for setup, asserts VisibleVariableName/VisibleVariableValue gating and map occupancy state, adds none/flying/partner/state/mover fixtures, checks canonical SetNpcAction and NpcAction alias Stand1 stop a moving NPC, asserts C#-style relation helpers, ShowName display fallback, AI state, and player/NPC be-attacked magic script setters, checks SetNpcPartner kind/path conversion plus pending-destination cleanup, checks NPC resource fallback from ini/npc while preserving ini/npcres priority, then briefly enables safe AI to check friendly/partner AI ignoring true neutral, ordinary friendly AI hitting RelationType.None, low-life retreat, low-life magic, revive lifecycle, death magic, friend-death retreat, PartnerCombat-gated none-fighter targeting, timer-script current-NPC omitted SetNpcDir/SetNpcPos parameters, no-arg special-action fallback, and non-blocking special-action return",
            "Requires=safe visible-variable, none fighter, true neutral target, friendly RelationType.None target, flying animal, partner, AI state, destination mover, SetNpcAction/NpcAction Stand1, NPC resource fallback/priority fixtures, counter magic, death magic, friend-death NPC fixtures, and partner combat target fixtures",
            "",
            "[Scenario.object_state]",
            "Status=ready",
            "Choice=13",
            "Smoke=1",
            "EntryScript=mod_test_object_state.txt",
            "PostNewGameWaitMilliseconds=900",
            f"ExpectVariables={object_state_expect_variables}",
            "VisibleResult=loads a safe XJXQY save, adds object fixtures, asserts C#-style object runtime helpers and metadata readbacks, checks named OpenBox/CloseBox/OpenObj target actions, switches kind through trap/body/drop/static, checks deletion/save-load/object aliases/right-only fallback/timer current-object offset/touch-only/trap-cycle state, verifies NPC shop-owner BuyIniFile/BuyIniString plus death stock transfer, and queues nearest object/NPC right-script interactions through NextAction while WalkIsRun is enabled and running is disabled",
            "Requires=box, Type/ObjFileMovie metadata, save-load, object alias, right-only, timer, touch-only, remove, trap object, shop-owner NPC death transfer, interact NPC fixtures, and WalkIsRun/DisableRun walk fallback plus inherited XJXQY objres resources",
            "",
            "[Scenario.player_control_state]",
            "Status=ready",
            "Choice=14",
            "Smoke=1",
            "EntryScript=mod_test_player_control_state.txt",
            f"ExpectVariables={player_control_expect_variables}",
            "VisibleResult=toggles DisableRun/DisableJump/DisableFight, records C# Is*Disabled plus C++ Can* player state readbacks, and covers PlayGoto/PlayerWalkTo movement aliases plus non-blocking movement alias callability",
            "Requires=GetPlayerState, safe map001 walk tiles, and player control/movement script commands",
            "",
            "[Scenario.object_trap_damage]",
            "Status=ready",
            "Choice=15",
            "Smoke=1",
            "EntryScript=mod_test_object_trap_damage.txt",
            "PostNewGameWaitMilliseconds=900",
            f"ExpectVariables={object_trap_damage_expect_variables}",
            "VisibleResult=sets player and a safe NPC on trap tiles, exits the setup script, waits in non-event runtime, then records player/NPC life loss through a timer object",
            "Requires=post-newgame automation wait, safe trap target NPC, and trap object fixtures",
            "",
            "[Scenario.magic_transport_control]",
            "Status=ready",
            "Choice=16",
            "Smoke=1",
            "EntryScript=mod_test_magic_transport_control.txt",
            f"ExpectVariables={magic_transport_control_expect_variables}",
            "VisibleResult=casts MoveKind=20 transport and MoveKind=21 player-control fixtures, then records transport visibility/obstacle state and controlled-target runtime relation recovery",
            "Requires=GetPlayerState/GetNpcState transport-control readbacks and safe hostile target NPC fixture",
            "",
            "[Scenario.choose_multiple]",
            "Status=ready",
            "Choice=17",
            "Smoke=1",
            "EntryScript=mod_test_choose_multiple.txt",
            f"ExpectVariables={choose_multiple_expect_variables}",
            "VisibleResult=executes ChooseMultiple through automation input, records selected option indices, and checks hidden options are ignored",
            "Requires=ChooseMultiple script command, conditional option filtering, and automation multi-select variables",
            "",
            "[Scenario.magic_region_vtype]",
            "Status=ready",
            "Choice=18",
            "Smoke=1",
            "EntryScript=mod_test_magic_region_vtype.txt",
            f"ExpectVariables={magic_region_vtype_expect_variables}",
            "VisibleResult=casts MoveKind=11 Region=5 V-type fixture and records effect count plus hit/safe NPC life changes",
            "Requires=Region=5 V-type runtime and safe hostile NPC fixtures",
            "",
            "[Scenario.script_timer_parallel]",
            "Status=ready",
            "Choice=19",
            "Smoke=1",
            "EntryScript=mod_test_script_timer_parallel.txt",
            "TimeoutSeconds=100",
            "PostNewGameWaitMilliseconds=72000",
            f"ExpectVariables={script_timer_parallel_expect_variables}",
            "VisibleResult=shows a seventy-second countdown and a message when SetTimeScript fires as the countdown reaches zero, while also saving/loading pending timer and parallel scripts",
            "Requires=OpenTimeLimit, SetTimeScript, RunParallelScript, SaveGame/LoadGame auto-save restore",
            "",
            "[Scenario.magic_change_hit]",
            "Status=ready",
            "Choice=20",
            "Smoke=1",
            "EntryScript=mod_test_magic_change_hit.txt",
            "TimeoutSeconds=180",
            "ExpectVariables=mod_test_magic_change_hit_ready=1;mod_test_magic_change_hit_first_hit=1;mod_test_magic_change_hit_second_hit=1;mod_test_magic_change_hit_changed=1",
            "VisibleResult=casts a HitCountToChangeMagic fixture twice to build caster-side hit count visuals, then verifies the third cast switches to high-damage ChangeMagic and consumes the count",
            "Requires=inherited magic ASF resources and collision target NPC fixture",
            "",
            "[Scenario.magic_post_cast]",
            "Status=ready",
            "Choice=21",
            "Smoke=1",
            "EntryScript=mod_test_magic_post_cast.txt",
            "ExpectVariables=mod_test_magic_post_cast_ready=1;mod_test_magic_post_cast_jump_active=1;mod_test_magic_post_cast_jump_destination=1;mod_test_magic_post_cast_jump_speed=12;mod_test_magic_post_cast_jump_has_end_magic=1;mod_test_magic_post_cast_jump_action=1;mod_test_magic_post_cast_jump_progress_seen=1;mod_test_magic_post_cast_jump_bezier_seen=1;mod_test_magic_post_cast_damage_mid=70;mod_test_magic_post_cast_jump_moved=1;mod_test_magic_post_cast_side_effect=1;mod_test_magic_post_cast_damage_delta=120;mod_test_magic_post_cast_child_chain=1;mod_test_magic_post_cast_no_primary_extra=1;mod_test_magic_post_cast_die_after_use=1",
            "VisibleResult=casts a JumpToTarget fixture that also schedules SecondMagic, RandMagic, SideEffect, JumpEndMagic, and DieAfterUse, then asserts the child chain damage without the skipped parent point effect",
            "Requires=inherited magic ASF resources and collision caster/target fixtures",
            "",
            "[Scenario.script_return_api]",
            "Status=ready",
            "Choice=22",
            "Smoke=1",
            "EntryScript=mod_test_script_return_api.txt",
            f"ExpectVariables={script_return_api_expect_variables}",
            "VisibleResult=exercises return-value aliases for money/player/goods/magic/NPC/object/partner helper APIs and MOD leechcraft difference output",
            "Requires=script return wrappers and safe fixture objects/NPCs with Leechcraft",
            "",
            "[Scenario.magic_summon_body]",
            "Status=ready",
            "Choice=23",
            "Smoke=1",
            "EntryScript=mod_test_magic_summon_body.txt",
            f"ExpectVariables={magic_summon_body_expect_variables}",
            "VisibleResult=casts BodyRadius and ReviveBody fixtures, consumes corpse objects, revives a friendly NPC, checks MaxCount overflow body removal, and records VibratingScreen/ReviveBody field readbacks",
            "Requires=script target resolution for control-style BodyRadius fixture, corpse objects with ReviveNpcIni, and inherited magic ASF resources",
            "",
            "[Scenario.choose_ex_plus]",
            "Status=ready",
            "Choice=24",
            "Smoke=1",
            "EntryScript=mod_test_choose_ex_plus.txt",
            f"ExpectVariables={choose_ex_plus_expect_variables}",
            "VisibleResult=executes ChooseEx and ChoosePlus through automation input, records original selected option indices, and checks hidden options are ignored",
            "Requires=ChooseEx/ChoosePlus script commands, conditional option filtering, and automation choose variables",
            "",
            "[Scenario.npc_kind_talent]",
            "Status=ready",
            "Choice=25",
            "Smoke=1",
            "EntryScript=mod_test_npc_kind_talent.txt",
            f"ExpectVariables={npc_kind_talent_expect_variables}",
            "VisibleResult=adds a friendly NPC fixture, reads KindValue/KindValueMax through GetNpcState, clamps AddKindValue at both bounds, and verifies AddTalent grants a player magic level",
            "Requires=GetNpcState KindValue/KindValueMax, AddKindValue clamp semantics, AddTalent/AddMagic alias behavior, and GetPlayerMagicLevel",
            "",
            "[Scenario.npc_signal_tip]",
            "Status=ready",
            "Choice=26",
            "Smoke=1",
            "EntryScript=mod_test_npc_signal_tip.txt",
            f"ExpectVariables={npc_signal_tip_expect_variables}",
            "VisibleResult=adds a friendly NPC fixture, toggles ShowSignalTip/SetSignalTipHidden state, then applies IsSignalShow/SignalIndex/SignalType through SetMapNpcAttr on the current map NPC file",
            "Requires=ShowSignalTip, SetSignalTipHidden, GetNpcState signal readbacks, and current-map SetMapNpcAttr signal field synchronization",
            "",
            "[Scenario.script_sound_position]",
            "Status=ready",
            "Choice=27",
            "Smoke=1",
            "PostNewGameWaitMilliseconds=1200",
            "EntryScript=mod_test_script_sound_position.txt",
            f"ExpectVariables={script_sound_position_expect_variables}",
            "VisibleResult=plays a script sound globally, then from OBJ and NPC script contexts, and records the selected 3D sound source state through GetPlayerState",
            "Requires=PlaySound, scriptObj/scriptNPC context propagation, runobjscript, interactnearestnpc, and LastScriptSound readbacks",
            "",
            "[Scenario.magic_self_special]",
            "Status=ready",
            "Choice=28",
            "Smoke=1",
            "EntryScript=mod_test_magic_self_special.txt",
            f"ExpectVariables={magic_self_special_expect_variables}",
            "VisibleResult=casts MoveKind=13 SpecialKind=6 to block a hostile damage spell, then casts SpecialKind=8 to clear frozen, poison, and petrify states",
            "Requires=MoveKind=13 self magic, SpecialKind=6 block-damage buff, SpecialKind=8 clear-abnormal behavior, and GetPlayerState status readbacks",
            "",
            "[Scenario.magic_trail]",
            "Status=ready",
            "Choice=29",
            "Smoke=1",
            "EntryScript=mod_test_magic_trail.txt",
            f"ExpectVariables={magic_trail_expect_variables}",
            "VisibleResult=casts MoveKind=19 trail magic, moves the player across tiles, and records fixed effects left at previous tile positions until KeepMilliseconds expires",
            "Requires=MoveKind=19 trail runtime, KeepMilliseconds, SetPlayerPos tile changes, and GetEffectState effect readbacks",
            "",
            "[Scenario.magic_temp_relation]",
            "Status=ready",
            "Choice=30",
            "Smoke=1",
            "EntryScript=mod_test_magic_temp_relation.txt",
            f"ExpectVariables={magic_temp_relation_expect_variables}",
            "VisibleResult=casts ChangeToFriendMilliseconds magic to flip a hostile NPC to friend, cancel it with a second hostile hit, restore after timer expiry, and verify player immunity",
            "Requires=ChangeToFriendMilliseconds, MaxLevel, GetNpcState Relation/RuntimeRelation, and safe hostile target/caster fixtures",
            "",
            "[Scenario.npc_drop]",
            "Status=ready",
            "Choice=31",
            "Smoke=1",
            "EntryScript=mod_test_npc_drop.txt",
            f"ExpectVariables={npc_drop_expect_variables}",
            "VisibleResult=sets DropIni through script, kills a hostile NPC, verifies the named pickup object appears at the death tile, verifies NoDropWhenDie blocks a second named drop, then kills a boss fixture and verifies the default weapon/armor pickup runs its level-based pickup script",
            "Requires=SetDropIni, EnableDrop, hostile NPC drop fixtures, custom DropIni table, default boss weapon/armor pickup fixtures, RunObjScript, and GetObjState pickup readbacks",
            "",
            "[Scenario.time_stop_visual_repro]",
            "Status=ready",
            "Choice=32",
            "Smoke=0",
            "EntryScript=mod_test_time_stop_visual_repro.txt",
            "ExpectVariables=mod_test_time_stop_visual_repro_ready=1;mod_test_time_stop_visual_repro_cast=1",
            "VisibleResult=starts a friendly walker, casts a long MoveKind=23 time-stop, and leaves the window for checking that both position and animation frames freeze",
            "Requires=visible window or computer-use inspection for animation-frame acceptance; smoke only verifies the trigger chain",
            "",
            "[Scenario.choose_menu_visual]",
            "Status=ready",
            "Choice=33",
            "Smoke=0",
            "EntryScript=mod_test_choose_menu_visual.txt",
            f"ExpectVariables={choose_menu_visual_expect_variables}",
            "VisibleResult=shows a long paged ChoosePlus with portrait and left speaker name, preserves an empty leading original index, then shows a right speaker name without portrait",
            "Requires=visible window or computer-use inspection for layout, pagination, resize, portrait, speaker alignment, keyboard, mouse, and touch acceptance",
            "",
            "[Scenario.magic_explode]",
            "Status=ready",
            "Choice=34",
            "Smoke=1",
            "EntryScript=mod_test_magic_explode.txt",
            "ExpectVariables=mod_test_magic_explode_ready=1;mod_test_magic_explode_point_has_child=1;mod_test_magic_explode_throw_has_child=1;mod_test_magic_explode_throw_suppressed_has_child=1;mod_test_magic_explode_point_empty_tile=1;mod_test_magic_explode_point_child_before=0;mod_test_magic_explode_point_child_seen=1;mod_test_magic_explode_point_child_max=1;mod_test_magic_explode_point_once=1;mod_test_magic_explode_throw_empty_tile=1;mod_test_magic_explode_throw_suppressed_parent_seen=1;mod_test_magic_explode_throw_suppressed_child_before=0;mod_test_magic_explode_throw_suppressed_child_max=0;mod_test_magic_explode_throw_suppressed=1;mod_test_magic_explode_throw_level=4;mod_test_magic_explode_throw_child_before=0;mod_test_magic_explode_throw_child_seen_four=1;mod_test_magic_explode_throw_child_max=4;mod_test_magic_explode_throw_four=1",
            "VisibleResult=casts a short-lived MoveKind=1 parent onto an empty tile, verifies NoExplodeWhenLifeFrameEnd suppresses a throw child, then casts a level-4 MoveKind=17 parent onto empty ground and verifies ExplodeMagicFile creates exactly one and four child projectiles with preserved owner and launcher state",
            "Requires=default fixed-point destroy-on-end, per-landing throw explosion dispatch, ExplodeMagicFile child loading, and GetEffectState projectile ownership readbacks",
            "",
            "[Scenario.environment_weather]",
            "Status=ready",
            "Choice=35",
            "Smoke=0",
            "EntryScript=mod_test_environment_weather.txt",
            "ExpectVariables=mod_test_environment_weather_ready=1",
            "VisibleResult=loads a production XJXQY map, starts Rain1 ambient rain and water distortion, and remains stable across window resize",
            "Requires=production map001 resources, custom rain configuration/audio, OpenWaterEffect, and visible computer-use inspection",
            "",
            "[Scenario.animation_parameters_visual]",
            "Status=ready",
            "Choice=36",
            "Smoke=0",
            "EntryScript=mod_test_animation_parameters_visual.txt",
            "ExpectVariables=mod_test_animation_parameters_visual_ready=1",
            "VisibleResult=cycles all eight directions of the production XJXQY npc115_wlk.asf zero-interval animation on one stable map anchor",
            "Requires=production npc115_wlk.asf with 80 frames, 8 directions, interval 0, per-frame offsets, and visible computer-use inspection",
            "",
            "[Scenario.video_background_continuity]",
            "Status=ready",
            "Choice=37",
            "Smoke=0",
            "EntryScript=mod_test_video_background_continuity.txt",
            "ExpectVariables=mod_test_video_background_ready=1;mod_test_video_background_movie_completed=1;mod_test_video_background_game_completed=1",
            "VisibleResult=plays the production XJXQY start.wmv and then an animated map countdown while another window stays in the foreground; video/audio and the game phase must both continue without waiting for focus restoration",
            "Requires=production start.wmv, production map001 resources, the animation-parameter fixture, visible computer-use inspection, and an unrelated foreground window",
            "",
            "[Scenario.magic_critical_feedback]",
            "Status=ready",
            "Choice=38",
            "Smoke=0",
            "EntryScript=mod_test_magic_critical_feedback.txt",
            "ExpectVariables=mod_test_magic_critical_feedback_ready=1;mod_test_magic_critical_chance=100;mod_test_magic_critical_damage_percent=100;mod_test_magic_critical_total_damage=1200;mod_test_magic_critical_feedback_pass=1",
            "VisibleResult=repeats six deterministic 100-damage attacks under the MG SpecialKind 99 buff; each hit removes exactly 200 Life and shows the orange target-head text 暴击 200 moving upward and fading out",
            "Requires=temporary explicit RageSystem=1 profile activation, inherited XJXQY magic/NPC resources, and visible computer-use inspection",
            "",
            "[Scenario.magic_detached_caster_visual]",
            "Status=ready",
            "Choice=39",
            "Smoke=0",
            "EntryScript=mod_test_magic_detached_caster_visual.txt",
            "TimeoutSeconds=60",
            "ExpectVariables=mod_test_magic_detached_caster_ready=1;mod_test_magic_detached_caster_cycle_pass_count=6;mod_test_magic_detached_caster_visual_pass=1",
            "VisibleResult=repeats six launches of the production Xiaoxiangxing gunpowder-cannon projectile; the gunner disappears immediately after launch while the white-ring projectile continues across the map, flashes yellow-orange, expands into an orange explosion cloud, and fades through dark smoke",
            "Requires=temporary byte-exact overlay of Xiaoxiangxing 001火药炮.ini, 001霹雳烟火弹爆炸.ini, their referenced ASF files, and WAV files, plus visible computer-use inspection; subjective sound remains a separate checkpoint",
            "",
            "[Scenario.manual_magic_arena]",
            "Status=ready",
            "Choice=40",
            "Smoke=1",
            "EntryScript=mod_test_manual_magic_arena.txt",
            "ExpectVariables=mod_test_manual_magic_arena_ready=1",
            "VisibleResult=opens a persistent XJXQY practice map with three hostile Life=99999 Defence=500 Attack=1 targets, grants fourteen Chinese-labeled test magics with distinct production icons, and leaves a friendly trainer NPC for resetting targets, restoring resources, clearing effects, showing help, and re-granting magics through ChoosePlus",
            "Requires=base XJXQY map001 resources, inherited magic ASF/Sound resources, player magic menu/toolbar, NPC interaction, and ChoosePlus",
            "",
            "[Scenario.manual_continuity]",
            "Status=ready",
            "Choice=41",
            "Smoke=1",
            "EntryScript=mod_test_manual_continuity.txt",
            "ExpectVariables=mod_test_manual_continuity_pass=1;mod_test_ready=1;mod_test_hub_respawn_count=1",
            "VisibleResult=enters the manual magic arena, selects the trainer return action through ChoosePlus automation, and re-enters a fully initialized interactive test plaza",
            "Requires=manual arena setup, trainer ReturnHub branch, automated ChoosePlus selection, and interactive hub bootstrap",
            "",
        ]
    )


def make_readme(pack_id: str, base_id: str) -> str:
    return "\n".join(
        [
            f"# {pack_id}",
            "",
            f"Generated MOD integration test scaffold based on {base_id}.",
            "",
            f"- NewGame.Script is `{DEFAULT_NEW_GAME_SCRIPT}`. A normal manual launch opens the complete paged scenario runner; automated command-line selection uses a separate one-shot dispatch path.",
            "- Scenario status and entry scripts are listed in `ini/test_mod_scenarios.ini`.",
            "- Automated smoke can select a runner entry with `--test-scenario-choice <N>`; choice 0 continues to the base game, 1 interactive test plaza, 2 gamble, 3 dice, 4 fish, 5 steal, 6 magic lifecycle, 7 magic collision, 8 equipment trigger, 9 goods pricing, 10 goods random, 11 goods lifecycle, 12 NPC AI, 13 Object state, 14 player control state, 15 object trap damage, 16 magic transport/control, 17 choose multiple, 18 magic region V, 19 script timer/parallel, 20 magic change hit, 21 magic post cast, 22 script return API, 23 magic summon/body, 24 choose ex/plus, 25 NPC kind/talent, 26 NPC signal tip, 27 script sound position, 28 magic self special, 29 magic trail, 30 magic temp relation, 31 NPC drop, 32 time stop visual repro, 33 choose UI visual, 34 magic explode, 35 environment/weather visual, 36 animation parameters visual, 37 video/game background continuity, 38 MG critical feedback, 39 detached-caster production visual, 40 manual magic arena, 41 manual continuity loop.",
            "- Manual scenarios return to the runner after their script completes; press Esc to resume the current scene. Choice 0 explicitly runs the inherited XJXQY `newgame.txt`.",
            "- Choice 1 enters the manual plaza. Use 小游戏掌柜 for non-automated Gamble/Dice/Fish, or 测试向导 to reopen the complete scenario runner, respawn partners/enemies, and enter the magic arena.",
            "- The manual plaza intentionally reuses XJXQY production maps and character art, but replaces the story actors with test-only guides, partners, enemies, and services; it does not continue the base newgame script.",
            "- The four manual-plaza combat fixtures use AttackSpeed=1. Respawning converts both partners first, then places them on separate adjacent tiles beside the player to avoid inheriting competing follow destinations.",
            "- A normal manual launch stays open until the player exits; the arena trainer can return to the plaza for a continuous hub -> arena -> hub loop.",
            "- `Choice`, `Smoke`, `PostNewGameWaitMilliseconds`, and `ExpectVariables` in `ini/test_mod_scenarios.ini` are machine-readable smoke metadata used by `scripts/run_mod_scenario_smoke.py`.",
            "- Manual magic arena fixtures clone the automated behavior INIs but use Chinese names, Chinese descriptions, and fourteen distinct XJXQY production icons, so visual polish cannot change regression assertions.",
            "- Keep fixtures non-hostile unless a scenario explicitly needs combat.",
            "- Keep visible results in messages, variables, inventory changes, NPC state, or map changes.",
            "- Commit generated assets separately from runtime code changes.",
            "- Promote scenario Status from needs_fixture to ready only after the fixture script can be run.",
            "- NPC AI smoke covers state/readback safety, one script-driven destination walk check, combat target acquisition with low-life retreat, low-life self magic, revive countdown, death magic, and friend-death retreat. Partner battle still needs visible or log-backed follow-up scenes.",
            "- Object state smoke covers C#-style object readbacks, kind transitions, save/load, right-only fallback, timer self deletion, and trap-cycle readbacks. Object trap damage smoke covers non-event player/NPC damage; full object UX still needs visible checks for animation and touch selection.",
            "",
        ]
    )


def scenario_files() -> dict[str, str]:
    return {
        f"script/common/{DEFAULT_NEW_GAME_SCRIPT}": make_newgame_script(),
        "script/common/mod_test_runner.txt": make_runner_script(),
        "script/common/mod_test_bootstrap.txt": make_bootstrap_script(),
        "script/common/mod_test_hub_respawn.txt": make_test_hub_respawn_script(),
        "script/common/mod_test_hub_guide.txt": make_test_hub_guide_script(),
        "script/common/mod_test_hub_little_games.txt": make_test_hub_little_games_script(),
        "script/common/mod_test_gamble.txt": make_gamble_script(),
        "script/common/mod_test_dice_game.txt": make_dice_script(),
        "script/common/mod_test_fish_game.txt": make_fish_script(),
        "script/common/mod_test_choose_multiple.txt": make_choose_multiple_script(),
        "script/common/mod_test_choose_ex_plus.txt": make_choose_ex_plus_script(),
        "script/common/mod_test_choose_menu_visual.txt": make_choose_menu_visual_script(),
        "script/common/mod_test_npc_kind_talent.txt": make_npc_kind_talent_script(),
        "script/common/mod_test_npc_signal_tip.txt": make_npc_signal_tip_script(),
        "script/common/mod_test_script_sound_position.txt": make_script_sound_position_script(),
        "script/common/mod_test_script_sound_position_obj.txt": make_script_sound_position_obj_script(),
        "script/common/mod_test_script_sound_position_npc.txt": make_script_sound_position_npc_script(),
        "sound/mod_test_script_sound.wav": make_script_sound_probe_wav(),
        "script/common/mod_test_steal.txt": make_steal_script(),
        "script/common/mod_test_steal_success.txt": make_steal_success_script(),
        "script/common/mod_test_steal_fail.txt": make_steal_fail_script(),
        "script/common/mod_test_magic_lifecycle.txt": make_magic_lifecycle_script(),
        "script/common/mod_test_time_stop_visual_repro.txt": make_time_stop_visual_repro_script(),
        "script/common/mod_test_magic_self_special.txt": make_magic_self_special_script(),
        "script/common/mod_test_magic_trail.txt": make_magic_trail_script(),
        "script/common/mod_test_magic_collision.txt": make_magic_collision_script(),
        "script/common/mod_test_magic_explode.txt": make_magic_explode_script(),
        "script/common/mod_test_environment_weather.txt": make_environment_weather_script(),
        "script/common/mod_test_animation_parameters_visual.txt": make_animation_parameters_visual_script(),
        "script/common/mod_test_video_background_continuity.txt": make_video_background_continuity_script(),
        "script/common/mod_test_magic_critical_feedback.txt": make_magic_critical_feedback_script(),
        "script/common/mod_test_magic_detached_caster_visual.txt": make_magic_detached_caster_visual_script(),
        "script/common/mod_test_manual_magic_arena.txt": make_manual_magic_arena_script(),
        "script/common/mod_test_manual_magic_arena_trainer.txt": make_manual_magic_arena_trainer_script(),
        "script/common/mod_test_manual_continuity.txt": make_manual_continuity_script(),
        "script/common/mod_test_magic_temp_relation.txt": make_magic_temp_relation_script(),
        "script/common/mod_test_npc_drop.txt": make_npc_drop_script(),
        "script/common/2级武器.txt": make_default_drop_pickup_script("mod_test_npc_default_drop_weapon_script"),
        "script/common/3级武器.txt": make_default_drop_pickup_script("mod_test_npc_default_drop_weapon_script"),
        "script/common/4级武器.txt": make_default_drop_pickup_script("mod_test_npc_default_drop_weapon_script"),
        "script/common/2级防具.txt": make_default_drop_pickup_script("mod_test_npc_default_drop_armor_script"),
        "script/common/3级防具.txt": make_default_drop_pickup_script("mod_test_npc_default_drop_armor_script"),
        "script/common/4级防具.txt": make_default_drop_pickup_script("mod_test_npc_default_drop_armor_script"),
        "script/common/mod_test_magic_summon_body.txt": make_magic_summon_body_script(),
        "script/common/mod_test_magic_change_hit.txt": make_magic_change_hit_script(),
        "script/common/mod_test_magic_post_cast.txt": make_magic_post_cast_script(),
        "script/common/mod_test_script_return_api.txt": make_script_return_api_script(),
        "script/common/mod_test_object_state.txt": make_object_state_script(),
        "script/common/mod_test_object_interact.txt": make_object_interact_script(),
        "script/common/mod_test_object_interact_right.txt": make_object_interact_right_script(),
        "script/common/mod_test_npc_interact_left.txt": make_npc_interact_left_script(),
        "script/common/mod_test_npc_interact_right.txt": make_npc_interact_right_script(),
        "ini/buy/mod_test_shop_head_numbervalid.ini": make_shop_head_numbervalid_ini(),
        "script/common/mod_test_object_timer.txt": make_object_timer_script(),
        "script/common/mod_test_object_alias_delete_current.txt": make_object_alias_delete_current_script(),
        "script/common/mod_test_object_touch.txt": make_object_touch_script(),
        "script/common/mod_test_object_trap_damage.txt": make_object_trap_damage_script(),
        "script/common/mod_test_object_trap_damage_record.txt": make_object_trap_damage_record_script(),
        "script/common/mod_test_equipment_trigger.txt": make_equipment_trigger_script(),
        "script/common/mod_test_goods_pricing.txt": make_goods_pricing_script(),
        "script/common/mod_test_goods_random.txt": make_goods_random_script(),
        "script/common/mod_test_goods_lifecycle.txt": make_goods_lifecycle_script(),
        "script/common/mod_test_player_control_state.txt": make_player_control_state_script(),
        "script/common/mod_test_magic_transport_control.txt": make_magic_transport_control_script(),
        "script/common/mod_test_magic_region_vtype.txt": make_magic_region_vtype_script(),
        "script/common/mod_test_script_timer_parallel.txt": make_script_timer_parallel_script(),
        "script/common/mod_test_script_timer_trigger.txt": make_script_timer_trigger_script(),
        "script/common/mod_test_script_parallel_immediate.txt": make_script_parallel_immediate_script(),
        "script/common/mod_test_script_parallel_delayed.txt": make_script_parallel_delayed_script(),
        "script/common/mod_test_script_alias_run.txt": make_script_alias_run_script(),
        "script/common/mod_test_npc_ai_timer_context.txt": make_npc_ai_timer_context_script(),
        "script/goods/mod_test_goods_script_book.txt": make_goods_script_book_script(),
        "script/common/mod_test_npc_ai.txt": make_npc_ai_script(),
        "ini/ui/ui_settings.ini": make_ui_settings_ini(),
        "ini/obj/mod_test_object_state_box.ini": make_object_state_box_ini(),
        "ini/objres/mod_test_object_state_resource.ini": make_object_state_resource_ini(),
        "ini/objres/mod_test_animation_parameter_resource.ini": make_animation_parameter_resource_ini(),
        "ini/obj/mod_test_animation_direction_0.ini": make_animation_parameter_object_ini(0),
        "ini/obj/mod_test_animation_direction_1.ini": make_animation_parameter_object_ini(1),
        "ini/obj/mod_test_animation_direction_2.ini": make_animation_parameter_object_ini(2),
        "ini/obj/mod_test_animation_direction_3.ini": make_animation_parameter_object_ini(3),
        "ini/obj/mod_test_animation_direction_4.ini": make_animation_parameter_object_ini(4),
        "ini/obj/mod_test_animation_direction_5.ini": make_animation_parameter_object_ini(5),
        "ini/obj/mod_test_animation_direction_6.ini": make_animation_parameter_object_ini(6),
        "ini/obj/mod_test_animation_direction_7.ini": make_animation_parameter_object_ini(7),
        "ini/obj/mod_test_object_pickup.ini": make_pickup_object_ini("MOD_TEST_OBJECT_PICKUP", "obj_get_钱.ini", 7),
        "ini/obj/mod_test_object_legacy_pickup.ini": make_pickup_object_ini("MOD_TEST_OBJECT_LEGACY_PICKUP", "obj_get_药品.ini", 8),
        "ini/obj/mod_test_object_timer_box.ini": make_object_timer_box_ini(),
        "ini/obj/mod_test_object_remove_box.ini": make_object_remove_box_ini(),
        "ini/obj/mod_test_object_touch_box.ini": make_object_touch_box_ini(),
        "ini/obj/mod_test_object_trap.ini": make_object_trap_ini(),
        "ini/obj/mod_test_object_save_box.ini": make_object_save_box_ini(),
        "ini/obj/mod_test_object_right_only_box.ini": make_object_right_only_box_ini(),
        "ini/obj/mod_test_object_left_only_box.ini": make_object_left_only_box_ini(),
        "ini/obj/mod_test_object_alias_box.ini": make_object_alias_box_ini(),
        "ini/obj/mod_test_object_damage_player_trap.ini": make_object_damage_trap_ini("MOD_TEST_OBJECT_DAMAGE_PLAYER_TRAP"),
        "ini/obj/mod_test_object_damage_npc_trap.ini": make_object_damage_trap_ini("MOD_TEST_OBJECT_DAMAGE_NPC_TRAP"),
        "ini/obj/mod_test_object_trap_damage_recorder.ini": make_object_trap_damage_recorder_ini(),
        "ini/obj/mod_test_script_sound_obj.ini": make_script_sound_position_obj_ini(),
        "ini/obj/mod_test_npc_ai_body.ini": make_npc_ai_body_object_ini(),
        "ini/obj/mod_test_magic_body_medium_body.ini": make_magic_body_object_ini("MOD_TEST_MAGIC_BODY_MEDIUM_BODY"),
        "ini/obj/mod_test_magic_revive_body_object.ini": make_magic_body_object_ini("MOD_TEST_MAGIC_REVIVE_BODY", "mod_test_magic_revive_body_npc.ini"),
        "ini/obj/mod_test_magic_revive_body_overflow_object.ini": make_magic_body_object_ini("MOD_TEST_MAGIC_REVIVE_BODY_OVERFLOW", "mod_test_magic_revive_body_overflow_npc.ini"),
        "ini/obj/mod_test_drop_token.ini": make_npc_drop_token_ini("MOD_TEST_DROP_TOKEN", "obj_get_钱.ini"),
        "ini/obj/mod_test_drop_blocked_token.ini": make_npc_drop_token_ini("MOD_TEST_DROP_BLOCKED_TOKEN", "obj_get_药品.ini"),
        "ini/obj/mod_test_drop_table.ini": make_npc_drop_table_ini("mod_test_drop_token.ini"),
        "ini/obj/mod_test_drop_blocked_table.ini": make_npc_drop_table_ini("mod_test_drop_blocked_token.ini"),
        "ini/obj/可捡武器.ini": make_pickup_object_ini("MOD_TEST_DEFAULT_DROP_WEAPON", "obj_get_武器.ini", 7),
        "ini/obj/可捡防具.ini": make_pickup_object_ini("MOD_TEST_DEFAULT_DROP_ARMOR", "obj_get_防具.ini", 7),
        "ini/npc/mod_test_hub_guide.ini": make_test_hub_guide_npc_ini(),
        "ini/npc/mod_test_hub_little_games_host.ini": make_test_hub_little_games_host_npc_ini(),
        "ini/npc/mod_test_hub_partner_sword.ini": make_test_hub_partner_sword_ini(),
        "ini/npc/mod_test_hub_partner_heroine.ini": make_test_hub_partner_heroine_ini(),
        "ini/npc/mod_test_hub_enemy_melee.ini": make_test_hub_enemy_melee_ini(),
        "ini/npc/mod_test_hub_enemy_ranged.ini": make_test_hub_enemy_ranged_ini(),
        "ini/npc/mod_test_steal_npc.ini": make_steal_npc_ini(),
        "ini/npc/mod_test_steal_hard_npc.ini": make_steal_npc_ini("MOD_TEST_STEAL_HARD_NPC", 200),
        "ini/npc/mod_test_drop_npc.ini": make_npc_drop_fixture_ini("MOD_TEST_DROP_NPC", "mod_test_drop_table.ini[100]"),
        "ini/npc/mod_test_no_drop_npc.ini": make_npc_drop_fixture_ini("MOD_TEST_NO_DROP_NPC", "mod_test_drop_blocked_table.ini[100]", 1),
        "ini/npc/mod_test_default_drop_boss.ini": make_npc_default_drop_boss_ini(),
        "ini/npc/mod_test_object_trap_target_npc.ini": make_object_trap_target_npc_ini(),
        "ini/npc/mod_test_magic_body_target_npc.ini": make_magic_body_target_npc_ini(),
        "ini/npc/mod_test_magic_revive_body_npc.ini": make_magic_revive_body_npc_ini(),
        "ini/npc/mod_test_magic_revive_body_overflow_npc.ini": make_magic_revive_body_npc_ini("MOD_TEST_MAGIC_REVIVE_BODY_OVERFLOW_NPC"),
        "ini/npc/mod_test_collision_target_npc.ini": make_magic_collision_target_npc_ini(),
        "ini/npc/mod_test_magic_critical_target_npc.ini": make_magic_critical_target_npc_ini(),
        "ini/npc/mod_test_magic_damage_channels_target_npc.ini": make_magic_damage_channels_target_npc_ini(),
        "ini/npc/mod_test_magic_restore_target_npc.ini": make_magic_restore_target_npc_ini(),
        "ini/npc/mod_test_collision_friend_npc.ini": make_magic_collision_friend_npc_ini(),
        "ini/npc/mod_test_collision_partner_npc.ini": make_magic_collision_partner_npc_ini(),
        "ini/npc/mod_test_magic_trace_nonfighter_npc.ini": make_magic_trace_nonfighter_npc_ini(),
        "ini/npc/mod_test_collision_blocker_npc.ini": make_magic_collision_blocker_npc_ini(),
        "ini/npc/mod_test_collision_blocker2_npc.ini": make_magic_collision_blocker2_npc_ini(),
        "ini/npc/mod_test_collision_caster_npc.ini": make_magic_collision_caster_npc_ini(),
        "ini/npc/mod_test_npc_kind_talent_npc.ini": make_npc_kind_talent_npc_ini(),
        "ini/npc/mod_test_npc_signal_tip_npc.ini": make_npc_signal_tip_npc_ini(),
        "ini/npc/mod_test_script_sound_npc.ini": make_script_sound_position_npc_ini(),
        "ini/npc/mod_test_manual_arena_target_center.ini": make_manual_magic_arena_target_npc_ini(
            "MOD_TEST_ARENA_TARGET_CENTER",
            "训练场中央木桩（受击动画）",
            "npcres059_金兵(刀兵).ini",
        ),
        "ini/npc/mod_test_manual_arena_target_left.ini": make_manual_magic_arena_target_npc_ini(
            "MOD_TEST_ARENA_TARGET_LEFT", "训练场左侧木桩"
        ),
        "ini/npc/mod_test_manual_arena_target_right.ini": make_manual_magic_arena_target_npc_ini(
            "MOD_TEST_ARENA_TARGET_RIGHT", "训练场右侧木桩"
        ),
        "ini/npc/mod_test_manual_arena_trainer.ini": make_manual_magic_arena_trainer_npc_ini(),
        "ini/npc/mod_test_summon_npc.ini": make_magic_summon_npc_ini(),
        "ini/npc/mod_test_equipment_power_target_npc.ini": make_equipment_power_target_npc_ini(),
        "ini/npc/mod_test_npc_interact.ini": make_npc_interact_ini(),
        "ini/npc/mod_test_npc_shop_death.ini": make_npc_shop_death_ini(),
        "ini/npc/mod_test_npc_ai_none_fighter.ini": make_npc_ai_none_fighter_ini(),
        "ini/npc/mod_test_npc_ai_visible_variable.ini": make_npc_ai_visible_variable_ini(),
        "ini/npc/mod_test_npc_ai_flyer.ini": make_npc_ai_flyer_ini(),
        "ini/npc/mod_test_npc_ai_afraid_animal.ini": make_npc_ai_afraid_animal_ini(),
        "ini/npc/mod_test_npc_ai_partner.ini": make_npc_ai_partner_ini(),
        "ini/npc/mod_test_goods_friend_drug_target_npc.ini": make_goods_friend_drug_target_npc_ini(),
        "ini/npc/mod_test_goods_partner_drug_target_npc.ini": make_goods_partner_drug_target_npc_ini(),
        "ini/npc/mod_test_npc_ai_event.ini": make_npc_ai_event_ini(),
        "ini/npc/mod_test_npc_ai_state.ini": make_npc_ai_state_ini(),
        "ini/npc/mod_test_npc_ai_mover.ini": make_npc_ai_mover_ini(),
        "ini/npc/mod_test_time_stop_visual_walker.ini": make_time_stop_visual_mover_ini(),
        "ini/npc/mod_test_npc_ai_path_normal.ini": make_npc_ai_path_normal_ini(),
        "ini/npc/mod_test_npc_ai_path_event.ini": make_npc_ai_path_event_ini(),
        "ini/npc/mod_test_npc_ai_path_enemy.ini": make_npc_ai_path_enemy_ini(),
        "ini/npc/mod_test_npc_ai_path_best.ini": make_npc_ai_path_best_ini(),
        "ini/npc/mod_test_npc_ai_path_fixed.ini": make_npc_ai_path_fixed_ini(),
        "ini/npc/mod_test_npc_ai_destination_blocker.ini": make_npc_ai_destination_blocker_ini(),
        "ini/npc/mod_test_npc_ai_retreat.ini": make_npc_ai_retreat_ini(),
        "ini/npc/mod_test_npc_ai_low_magic.ini": make_npc_ai_low_magic_ini(),
        "ini/npc/mod_test_npc_ai_revive.ini": make_npc_ai_revive_ini(),
        "ini/npc/mod_test_npc_ai_noadd_body.ini": make_npc_ai_noadd_body_ini(),
        "ini/npc/mod_test_npc_ai_death_caster.ini": make_npc_ai_death_caster_ini(),
        "ini/npc/mod_test_npc_ai_death_target.ini": make_npc_ai_death_target_ini(),
        "ini/npc/mod_test_npc_ai_friend_attacker.ini": make_npc_ai_friend_attacker_ini(),
        "ini/npc/mod_test_npc_ai_friend_victim.ini": make_npc_ai_friend_victim_ini(),
        "ini/npc/mod_test_npc_ai_friend_watcher.ini": make_npc_ai_friend_watcher_ini(),
        "ini/npc/mod_test_npc_ai_friendly_neutral_attacker.ini": make_npc_ai_friendly_neutral_attacker_ini(),
        "ini/npc/mod_test_npc_ai_true_neutral_target.ini": make_npc_ai_true_neutral_target_ini(),
        "ini/npc/mod_test_npc_ai_friend_none_target.ini": make_npc_ai_friend_none_target_ini(),
        "ini/npc/mod_test_npc_ai_npcres_fallback_npc.ini": make_npc_ai_npcres_fallback_npc_ini(),
        "ini/npc/mod_test_npc_ai_npcres_priority_npc.ini": make_npc_ai_npcres_priority_npc_ini(),
        "ini/npc/mod_test_npc_ai_npcres_fallback.ini": make_npc_ai_npcres_fallback_res_ini(),
        "ini/npc/mod_test_npc_ai_npcres_priority.ini": make_npc_ai_npcres_priority_side_res_ini(),
        "ini/npcres/mod_test_npc_ai_npcres_priority.ini": make_npc_ai_npcres_priority_standard_res_ini(),
        "ini/npc/mod_test_npc_ai_fight_res_npc.ini": make_npc_ai_fight_res_npc_ini(),
        "ini/npcres/mod_test_npc_ai_fight_res.ini": make_npc_ai_fight_res_ini(),
        "ini/npc/mod_test_npc_ai_partner_combat.ini": make_npc_ai_partner_combat_ini(),
        "ini/npc/mod_test_npc_ai_partner_target.ini": make_npc_ai_partner_target_ini(),
        "ini/npc/mod_test_npc_ai_blind.ini": make_npc_ai_blind_ini(),
        "ini/npc/mod_test_npc_ai_global_ai.ini": make_npc_ai_global_ai_ini(),
        "ini/npc/mod_test_npc_ai_local_ai.ini": make_npc_ai_local_ai_ini(),
        "ini/npc/mod_test_npc_ai_randwalk.ini": make_npc_ai_randwalk_ini(),
        "ini/npc/mod_test_npc_ai_timer_context.ini": make_npc_ai_timer_context_ini(),
        "ini/npc/mod_test_script_return_leechcraft_npc.ini": make_script_return_leechcraft_npc_ini(),
        "ini/npc/mod_test_magic_control_target_npc.ini": make_magic_control_target_npc_ini(),
        "ini/npc/mod_test_magic_control_watcher_npc.ini": make_magic_control_watcher_npc_ini(),
        "ini/npc/mod_test_magic_region_vtype_hit_npc.ini": make_magic_region_vtype_hit_npc_ini(),
        "ini/npc/mod_test_magic_region_vtype_safe_npc.ini": make_magic_region_vtype_safe_npc_ini(),
        "ini/goods/mod_test_steal_token.ini": make_steal_goods_ini(),
        "ini/goods/mod_test_equipment_trigger.ini": make_equipment_trigger_goods_ini(),
        "ini/goods/mod_test_equipment_user_restricted.ini": make_equipment_user_restricted_goods_ini(),
        "ini/goods/mod_test_equipment_level_restricted.ini": make_equipment_level_restricted_goods_ini(),
        "ini/goods/mod_test_equipment_parasitic_bonus.ini": make_equipment_parasitic_bonus_goods_ini(),
        "ini/goods/mod_test_goods_pricing_drug.ini": make_goods_pricing_drug_ini(),
        "ini/goods/mod_test_goods_pricing_equipment.ini": make_goods_pricing_equipment_ini(),
        "ini/goods/mod_test_goods_pricing_noneed.ini": make_goods_pricing_noneed_ini(),
        "ini/goods/mod_test_goods_random.ini": make_goods_random_ini(),
        "ini/goods/mod_test_goods_lifecycle_stack.ini": make_goods_lifecycle_stack_ini(),
        "ini/goods/mod_test_goods_lifecycle_case.ini": make_goods_lifecycle_case_ini(),
        "ini/goods/mod_test_goods_lifecycle_magic.ini": make_goods_lifecycle_magic_ini(),
        "ini/goods/mod_test_goods_lifecycle_magic_noneed.ini": make_goods_lifecycle_magic_noneed_ini(),
        "ini/goods/mod_test_goods_friend_drug.ini": make_goods_friend_drug_ini(),
        "ini/goods/mod_test_goods_friend_clear_poison.ini": make_goods_friend_clear_poison_ini(),
        "ini/goods/mod_test_goods_partner_drug.ini": make_goods_partner_drug_ini(),
        "ini/goods/mod_test_goods_bound_ammo.ini": make_goods_bound_ammo_ini(),
        "ini/goods/mod_test_goods_script_book.ini": make_goods_script_book_ini(),
        "ini/magic/mod_test_player_talent_probe.ini": make_player_talent_probe_magic_ini(),
        **{
            f"ini/magic/{file_name}": make_manual_magic_arena_magic_ini(
                source_factory(), display_name, intro, icon
            )
            for file_name, display_name, intro, icon, source_factory in (
                manual_magic_arena_magic_definitions()
            )
        },
        "ini/magic/mod_test_magic_begin_follow.ini": make_magic_begin_follow_ini(),
        "ini/magic/mod_test_magic_trace_enemy.ini": make_magic_trace_enemy_ini(),
        "ini/magic/mod_test_magic_random_move.ini": make_magic_random_move_ini(),
        "ini/magic/mod_test_magic_meteor.ini": make_magic_meteor_ini(),
        "ini/magic/mod_test_magic_round.ini": make_magic_round_ini(),
        "ini/magic/mod_test_magic_move_imitate_user.ini": make_magic_move_imitate_user_ini(),
        "ini/magic/mod_test_magic_moveback.ini": make_magic_moveback_ini(),
        "ini/magic/mod_test_magic_time_stop.ini": make_magic_time_stop_ini(),
        "ini/magic/mod_test_magic_time_stop_visual.ini": make_magic_time_stop_visual_ini(),
        "ini/magic/mod_test_magic_trail.ini": make_magic_trail_ini(),
        "ini/magic/mod_test_magic_summon.ini": make_magic_summon_ini(),
        "ini/magic/mod_test_magic_body_medium.ini": make_magic_body_medium_ini(),
        "ini/magic/mod_test_magic_revive_body.ini": make_magic_revive_body_ini(),
        "ini/magic/mod_test_magic_state_probe.ini": make_magic_state_probe_ini(),
        "ini/magic/mod_test_magic_invisible_keep_hidden.ini": make_magic_invisible_keep_hidden_ini(),
        "ini/magic/mod_test_magic_invisible_visible_attack.ini": make_magic_invisible_visible_attack_ini(),
        "ini/magic/mod_test_magic_self_block_damage.ini": make_magic_self_block_damage_ini(),
        "ini/magic/mod_test_magic_self_clear_abnormal.ini": make_magic_self_clear_abnormal_ini(),
        "ini/magic/mod_test_magic_critical_buff.ini": make_magic_critical_buff_ini(),
        "ini/magic/mod_test_magic_critical_strike.ini": make_magic_critical_strike_ini(),
        "ini/magic/mod_test_magic_morph_replace.ini": make_magic_morph_replace_ini(),
        "ini/magic/mod_test_magic_transport.ini": make_magic_transport_ini(),
        "ini/magic/mod_test_magic_control.ini": make_magic_control_ini(),
        "ini/magic/mod_test_magic_region_vtype.ini": make_magic_region_vtype_ini(),
        "ini/magic/mod_test_magic_ball.ini": make_magic_ball_ini(),
        "ini/magic/mod_test_magic_fly_magic_parent.ini": make_magic_fly_magic_parent_ini(),
        "ini/magic/mod_test_magic_fly_magic_child.ini": make_magic_fly_magic_child_ini(),
        "ini/magic/mod_test_magic_damage_channels.ini": make_magic_damage_channels_ini(),
        "ini/magic/mod_test_magic_leap.ini": make_magic_leap_ini(),
        "ini/magic/mod_test_magic_attack_all_leap.ini": make_magic_attack_all_leap_ini(),
        "ini/magic/mod_test_magic_restore_life.ini": make_magic_restore_life_ini(),
        "ini/magic/mod_test_magic_restore_mana.ini": make_magic_restore_mana_ini(),
        "ini/magic/mod_test_magic_restore_thew.ini": make_magic_restore_thew_ini(),
        "ini/magic/mod_test_magic_attack_all_projectile.ini": make_magic_attack_all_projectile_ini(),
        "ini/magic/mod_test_magic_partner_projectile.ini": make_magic_partner_projectile_ini(),
        "ini/magic/mod_test_magic_attack_all_trace_enemy.ini": make_magic_attack_all_trace_enemy_ini(),
        "ini/magic/mod_test_magic_wall.ini": make_magic_wall_ini(),
        "ini/magic/mod_test_magic_pass_through.ini": make_magic_pass_through_ini(),
        "ini/magic/mod_test_magic_pass_through_wall.ini": make_magic_pass_through_wall_ini(),
        "ini/magic/mod_test_magic_sticky.ini": make_magic_sticky_ini(),
        "ini/magic/mod_test_magic_solid.ini": make_magic_solid_ini(),
        "ini/magic/mod_test_magic_parasitic.ini": make_magic_parasitic_ini(),
        "ini/magic/mod_test_magic_range_speedup.ini": make_magic_range_speedup_ini(),
        "ini/magic/mod_test_magic_range_attack.ini": make_magic_range_attack_ini(),
        "ini/magic/mod_test_magic_range_attack_all.ini": make_magic_range_attack_all_ini(),
        "ini/magic/mod_test_magic_bounce.ini": make_magic_bounce_ini(),
        "ini/magic/mod_test_magic_bouncefly.ini": make_magic_bouncefly_ini(),
        "ini/magic/mod_test_magic_bounce_handoff.ini": make_magic_bounce_handoff_ini(),
        "ini/magic/mod_test_magic_bouncefly_handoff.ini": make_magic_bouncefly_handoff_ini(),
        "ini/magic/mod_test_magic_carry_user4.ini": make_magic_carry_user4_ini(),
        "ini/magic/mod_test_magic_carry_user4_hidden.ini": make_magic_carry_user4_hidden_ini(),
        "ini/magic/mod_test_magic_carry_user1_hidden.ini": make_magic_carry_user1_hidden_ini(),
        "ini/magic/mod_test_magic_discard.ini": make_magic_discard_ini(),
        "ini/magic/mod_test_magic_exchange.ini": make_magic_exchange_ini(),
        "ini/magic/mod_test_magic_collision_peer.ini": make_magic_collision_peer_ini(),
        "ini/magic/mod_test_magic_collision_lethal_freeze.ini": make_magic_collision_lethal_freeze_ini(),
        "ini/magic/mod_test_magic_explode_point_parent.ini": make_magic_explode_point_parent_ini(),
        "ini/magic/mod_test_magic_explode_throw_parent.ini": make_magic_explode_throw_parent_ini(),
        "ini/magic/mod_test_magic_explode_throw_suppressed_parent.ini": make_magic_explode_throw_suppressed_parent_ini(),
        "ini/magic/mod_test_magic_explode_child.ini": make_magic_explode_child_ini(),
        "ini/magic/mod_test_magic_explode_throw_child.ini": make_magic_explode_throw_child_ini(),
        "ini/magic/mod_test_magic_temp_relation.ini": make_magic_temp_relation_ini(),
        "ini/magic/mod_test_magic_status_duration_freeze.ini": make_magic_status_duration_freeze_ini(),
        "ini/magic/mod_test_magic_status_duration_short_freeze.ini": make_magic_status_duration_short_freeze_ini(),
        "ini/magic/mod_test_magic_status_duration_poison.ini": make_magic_status_duration_poison_ini(),
        "ini/magic/mod_test_magic_status_duration_petrify.ini": make_magic_status_duration_petrify_ini(),
        "ini/magic/mod_test_magic_equipment_fly.ini": make_magic_equipment_fly_ini(),
        "ini/magic/mod_test_magic_equipment_fly2.ini": make_magic_equipment_fly2_ini(),
        "ini/magic/mod_test_magic_goods_bound.ini": make_magic_goods_bound_ini(),
        "ini/magic/mod_test_magic_goods_script_book.ini": make_magic_goods_script_book_ini(),
        "ini/magic/mod_test_magic_equipment_counter.ini": make_magic_equipment_counter_ini(),
        "ini/magic/mod_test_magic_learned_passive.ini": make_magic_learned_passive_ini(),
        "ini/magic/mod_test_magic_equipment_power.ini": make_magic_equipment_power_ini(),
        "ini/magic/mod_test_magic_equipment_replace.ini": make_magic_equipment_replace_ini(),
        "ini/magic/mod_test_magic_shop_death_kill.ini": make_magic_shop_death_kill_ini(),
        "ini/magic/mod_test_magic_equipment_additional_freeze.ini": make_magic_equipment_additional_freeze_ini(),
        "ini/magic/mod_test_magic_change_hit_base.ini": make_magic_change_hit_base_ini(),
        "ini/magic/mod_test_magic_change_hit_power.ini": make_magic_change_hit_power_ini(),
        "ini/magic/mod_test_magic_post_cast_parent.ini": make_magic_post_cast_parent_ini(),
        "ini/magic/mod_test_magic_post_cast_second.ini": make_magic_post_cast_second_ini(),
        "ini/magic/mod_test_magic_post_cast_rand.ini": make_magic_post_cast_rand_ini(),
        "ini/magic/mod_test_magic_post_cast_jump_end.ini": make_magic_post_cast_jump_end_ini(),
        "ini/magic/mod_test_magic_post_cast_die.ini": make_magic_post_cast_die_ini(),
        "ini/magic/mod_test_magic_ai_low_life_self.ini": make_magic_ai_low_life_self_ini(),
        "ini/magic/mod_test_magic_ai_death_burst.ini": make_magic_ai_death_burst_ini(),
        "ini/magic/mod_test_magic_ai_friend_death_attack.ini": make_magic_ai_friend_death_attack_ini(),
        "ini/test_mod_scenarios.ini": make_scenarios_ini(),
    }


def generated_relative_names() -> list[str]:
    return [
        "game_profile.ini",
        "readme.md",
        *scenario_files().keys(),
    ]


def clean_generated_pack(root: Path) -> list[Path]:
    removed: list[Path] = []
    relative_names = set(generated_relative_names())
    relative_names.update(LEGACY_GENERATED_FILES)
    for relative_name in sorted(relative_names):
        path = resolve_contained_path(
            root,
            relative_name,
            "generated cleanup path",
            reject_links=True,
        )
        if path.is_file():
            path.unlink()
            removed.append(path)

    directories = sorted(
        {
            resolve_contained_path(
                root,
                Path(relative_name).parent,
                "generated cleanup directory",
                reject_links=True,
            )
            for relative_name in relative_names
            if Path(relative_name).parent != Path(".")
        },
        key=lambda path: len(path.parts),
        reverse=True,
    )
    for directory in directories:
        try:
            directory.rmdir()
        except OSError:
            pass
    return removed


def validate_generated_pack(root: Path, pack_id: str) -> list[str]:
    errors: list[str] = []
    profile_path = resolve_contained_path(root, "game_profile.ini", "generated profile")
    if not profile_path.exists():
        errors.append("missing game_profile.ini")
        return errors

    profile = read_ini(profile_path)
    for section, key in REQUIRED_PROFILE_FIELDS:
        if not get_value(profile, section, key):
            errors.append(f"missing profile field {section}.{key}")

    if get_value(profile, "Game", "Id") != pack_id:
        errors.append("Game.Id does not match pack id")

    for relative_name in (
        f"script/common/{DEFAULT_NEW_GAME_SCRIPT}",
        "script/common/mod_test_runner.txt",
        "script/common/mod_test_bootstrap.txt",
        "script/common/mod_test_gamble.txt",
        "script/common/mod_test_dice_game.txt",
        "script/common/mod_test_fish_game.txt",
        "ini/test_mod_scenarios.ini",
        "script/common/mod_test_magic_collision.txt",
        "script/common/mod_test_magic_temp_relation.txt",
        "script/common/mod_test_npc_kind_talent.txt",
        "script/common/mod_test_npc_signal_tip.txt",
        "script/common/mod_test_script_sound_position.txt",
        "script/common/mod_test_magic_self_special.txt",
        "script/common/mod_test_magic_trail.txt",
        "sound/mod_test_script_sound.wav",
    ):
        if not resolve_contained_path(root, relative_name, "generated fixture").exists():
            errors.append(f"missing generated file {relative_name}")

    scenarios_path = resolve_contained_path(
        root,
        "ini/test_mod_scenarios.ini",
        "generated scenario manifest",
    )
    scenarios = read_ini(scenarios_path) if scenarios_path.exists() else None
    if scenarios is not None:
        try:
            from run_mod_scenario_smoke import load_scenarios
            load_scenarios(scenarios_path)
        except (OSError, configparser.Error, ValueError) as exc:
            errors.append(f"invalid scenario schema: {exc}")
        scenario_sections = [
            section for section in scenarios.sections()
            if section.casefold().startswith("scenario.")
        ]
        choices: set[int] = set()
        runner_path = resolve_contained_path(
            root,
            "script/common/mod_test_runner.txt",
            "generated scenario runner",
        )
        runner_script = runner_path.read_text(encoding="utf-8") if runner_path.exists() else ""
        for section in scenario_sections:
            choice_text = get_value(scenarios, section, "Choice")
            entry_script = get_value(scenarios, section, "EntryScript")
            try:
                choice = int(choice_text, 10)
            except ValueError:
                errors.append(f"{section} has invalid Choice={choice_text!r}")
                continue
            if choice < 0 or choice in choices:
                errors.append(f"{section} has negative or duplicate Choice={choice}")
            choices.add(choice)
            if not entry_script:
                errors.append(f"{section} is missing EntryScript")
                continue
            try:
                entry_path = resolve_contained_path(
                    root,
                    f"script/common/{entry_script}",
                    f"{section}.EntryScript",
                )
            except ValueError as exc:
                errors.append(str(exc))
                continue
            if not entry_path.exists():
                errors.append(f"{section} EntryScript does not exist: {entry_script}")
            if choice > 0:
                if f'== {choice} then goto ' not in runner_script:
                    errors.append(f"{section} Choice={choice} is not dispatched by the runner")
                if f'runscript("{entry_script.lower()}");' not in runner_script.lower():
                    errors.append(f"{section} EntryScript is not invoked by the runner: {entry_script}")
        expected_choices = set(range(len(scenario_sections)))
        if choices != expected_choices:
            errors.append(
                "Scenario.* choices must be contiguous from 0 through "
                f"{len(scenario_sections) - 1}: got {sorted(choices)}"
            )

    return errors


def list_scenarios(root: Path) -> None:
    scenarios_path = root / "ini" / "test_mod_scenarios.ini"
    if not scenarios_path.exists():
        print(f"No scenarios file: {scenarios_path}")
        return

    scenarios = read_ini(scenarios_path)
    for section in scenarios.sections():
        if not section.lower().startswith("scenario."):
            continue
        status = get_value(scenarios, section, "Status", "unknown")
        entry_script = get_value(scenarios, section, "EntryScript", "")
        visible_result = get_value(scenarios, section, "VisibleResult", "")
        print(f"{section}: {status} | {entry_script} | {visible_result}")


def write_transaction_journal(
    journal_path: Path,
    phase: str,
    output_root: Path,
    staging_root: Path,
    backup_root: Path,
) -> None:
    atomic_write_text(
        journal_path,
        "\n".join(
            [
                "[Transaction]",
                f"Phase={phase}",
                f"Output={output_root}",
                f"Staging={staging_root}",
                f"Backup={backup_root}",
                "",
            ]
        ),
    )


def create_transaction_root(parent: Path, prefix: str) -> Path:
    # tempfile.mkdtemp() uses an owner-only ACL on recent Windows Python builds.
    # A staged directory later renamed into the live pack would keep that ACL and
    # become unreadable to sandboxed or other normal launch contexts. Path.mkdir()
    # uses the repository parent's inheritable permissions instead.
    for attempt in range(100):
        candidate = parent / f"{prefix}{os.getpid()}-{time.time_ns():x}-{attempt}"
        try:
            candidate.mkdir()
            return candidate
        except FileExistsError:
            continue
    raise FileExistsError(f"unable to allocate scaffold transaction directory under {parent}")


def scaffold(args: argparse.Namespace) -> int:
    assets_root = args.assets_root.resolve()
    args.pack_id = validate_pack_id(args.pack_id)
    args.name = validate_ini_scalar(args.name, "pack name")
    args.author = validate_ini_scalar(args.author, "pack author", allow_empty=True)
    pack_path = normalize_pack_path(args.pack_path)
    if "/" in pack_path:
        raise ValueError(
            "pack path must name one direct child of assets so the runtime can discover it"
        )
    if args.save_namespace is None:
        args.save_namespace = args.pack_id
    args.save_namespace = validate_ini_scalar(args.save_namespace, "save namespace")
    if args.game_type is not None and args.game_type not in {0, 1, 2, 3}:
        raise ValueError("--game-type must be one of 0, 1, 2, or 3")
    requested_base_ids = parse_dependency_ids(args.base_id)

    output_root = None
    if args.install:
        output_root = resolve_contained_path(
            assets_root,
            pack_path,
            "installed pack path",
            reject_links=True,
        )
    elif args.output_root is not None:
        if is_link_or_reparse_point(args.output_root):
            raise ValueError(f"scaffold output is a symlink, junction, or reparse point: {args.output_root}")
        output_root = args.output_root
    if output_root is None:
        output_root = resolve_contained_path(
            REPO_ROOT / "tmp" / "test_mod_skeleton",
            pack_path,
            "default scaffold output path",
            reject_links=True,
        )
    output_root = output_root.resolve()

    output_root.parent.mkdir(parents=True, exist_ok=True)
    output_lock_path = pack_lock_path(output_root)
    removed_relative_names: list[Path] = []
    written_relative_names: list[Path] = []
    base_packs: list[BasePack] = []

    with contextlib.ExitStack() as lock_stack:
        lock_stack.enter_context(exclusive_file_lock(output_lock_path))

        base_packs = [discover_base_pack(assets_root, base_id) for base_id in requested_base_ids]
        base_pack = base_packs[0]
        args.base_id = ",".join(pack.pack_id for pack in base_packs)
        validate_ini_scalar(args.base_id, "base ids")

        for discovered_id, discovered_root in discovered_pack_roots(assets_root):
            if discovered_id.casefold() == args.pack_id.casefold():
                if args.install and discovered_root != output_root:
                    raise ValueError(
                        f"pack id {args.pack_id} is already discovered at {discovered_root}; "
                        f"refusing to rewrite it at {output_root} without an explicit move operation"
                    )
                if not args.install and paths_overlap(output_root, discovered_root):
                    raise ValueError(
                        f"non-install output path {output_root} overlaps the discovered pack at "
                        f"{discovered_root}"
                    )
                continue
            if paths_overlap(output_root, discovered_root):
                raise ValueError(
                    f"output path {output_root} overlaps discovered pack {discovered_id} at {discovered_root}"
                )
        common_root = collection_common_root(assets_root)
        if common_root is not None and paths_overlap(output_root, common_root):
            raise ValueError(
                f"output path {output_root} overlaps shared Collection.CommonPath at {common_root}"
            )
        for base in base_packs:
            if paths_overlap(output_root, base.root):
                raise ValueError(f"output path {output_root} overlaps base pack {base.pack_id} at {base.root}")

        if args.install:
            requested_namespace = to_lower_ascii(sanitize_save_namespace(args.save_namespace))
            for existing_id, existing_root, existing_namespace in discovered_save_namespaces(assets_root):
                if (existing_id.casefold() == args.pack_id.casefold() and
                        existing_root == output_root):
                    continue
                if existing_namespace == requested_namespace:
                    raise ValueError(
                        f"Save.Namespace {args.save_namespace!r} normalizes to "
                        f"{requested_namespace!r}, already used by pack {existing_id} at {existing_root}"
                    )

        if output_root.exists():
            ensure_tree_has_no_links(output_root, "existing scaffold output")
            legacy_lock = output_root / ".mod-scenario-smoke.lock"
            legacy_backup = output_root / ".mod-scenario-smoke-save-backup"
            if legacy_lock.exists() or legacy_backup.exists():
                raise ValueError(
                    f"resource smoke state is active or needs recovery under {output_root}; "
                    "finish that smoke run before scaffolding"
                )

        generated_contents = [
            ("game_profile.ini", make_profile(args, base_pack, pack_path)),
            ("readme.md", make_readme(args.pack_id, args.base_id)),
            *scenario_files().items(),
        ]
        transaction_prefix = f".{output_root.name}.scaffold-"
        stale_transactions = sorted(
            path
            for path in output_root.parent.iterdir()
            if path.name.startswith(transaction_prefix)
        )
        if stale_transactions:
            raise ValueError(
                "scaffold recovery artifacts already exist; inspect them before retrying: "
                + ", ".join(str(path) for path in stale_transactions)
            )
        transaction_root = create_transaction_root(output_root.parent, transaction_prefix)
        staging_root = transaction_root / "staging"
        backup_root = transaction_root / "backup"
        journal_path = transaction_root / "transaction.ini"
        write_transaction_journal(
            journal_path,
            "staging",
            output_root,
            staging_root,
            backup_root,
        )
        preserve_transaction = False
        try:
            output_existed = output_root.exists()
            if output_existed:
                shutil.copytree(output_root, staging_root, copy_function=shutil.copy2)
            else:
                staging_root.mkdir(parents=True)

            if args.clean:
                removed_relative_names = [
                    path.relative_to(staging_root)
                    for path in clean_generated_pack(staging_root)
                ]

            generated_targets = [
                (
                    resolve_contained_path(
                        staging_root,
                        relative_name,
                        "generated staging output",
                        reject_links=True,
                    ),
                    text,
                )
                for relative_name, text in generated_contents
            ]
            if not args.force:
                conflicts = [path for path, _ in generated_targets if path.exists()]
                if conflicts:
                    preview_paths = [output_root / path.relative_to(staging_root) for path in conflicts[:3]]
                    preview = ", ".join(str(path) for path in preview_paths)
                    if len(conflicts) > 3:
                        preview += f", ... ({len(conflicts)} total)"
                    raise FileExistsError(
                        f"generated targets already exist: {preview}; "
                        "pass --force to overwrite generated files"
                    )

            staged_written: list[Path] = []
            for path, content in generated_targets:
                write_text(path, content, args.force, staged_written)
            written_relative_names = [path.relative_to(staging_root) for path in staged_written]

            errors = validate_generated_pack(staging_root, args.pack_id)
            if errors:
                for error in errors:
                    print(f"ERROR: {error}", file=sys.stderr)
                return 1

            write_transaction_journal(
                journal_path,
                "prepared",
                output_root,
                staging_root,
                backup_root,
            )
            old_moved = False
            new_published = False
            preserve_transaction = True
            try:
                if output_existed:
                    output_root.rename(backup_root)
                    old_moved = True
                    write_transaction_journal(
                        journal_path,
                        "old_moved",
                        output_root,
                        staging_root,
                        backup_root,
                    )
                staging_root.rename(output_root)
                new_published = True
                write_transaction_journal(
                    journal_path,
                    "new_published",
                    output_root,
                    staging_root,
                    backup_root,
                )
                preserve_transaction = False
            except BaseException as publish_error:
                old_moved = old_moved or backup_root.exists()
                new_published = new_published or (
                    output_root.exists() and not staging_root.exists()
                )
                try:
                    if new_published:
                        failed_new_root = transaction_root / "failed-new"
                        output_root.rename(failed_new_root)
                    if old_moved:
                        backup_root.rename(output_root)
                    write_transaction_journal(
                        journal_path,
                        "rolled_back",
                        output_root,
                        staging_root,
                        backup_root,
                    )
                    preserve_transaction = False
                except BaseException as rollback_error:
                    preserve_transaction = True
                    raise RuntimeError(
                        f"scaffold publish failed ({publish_error}) and rollback also failed "
                        f"({rollback_error}); preserve and inspect {transaction_root}"
                    ) from publish_error
                raise

            try:
                write_transaction_journal(
                    journal_path,
                    "committed",
                    output_root,
                    staging_root,
                    backup_root,
                )
            except OSError as journal_error:
                preserve_transaction = True
                print(
                    f"WARNING: scaffold committed but final journal update failed at "
                    f"{transaction_root}: {journal_error}",
                    file=sys.stderr,
                )
        finally:
            if not preserve_transaction:
                try:
                    shutil.rmtree(transaction_root)
                except OSError as cleanup_error:
                    print(
                        f"WARNING: scaffold transaction cleanup incomplete at {transaction_root}: "
                        f"{cleanup_error}",
                        file=sys.stderr,
                    )

    print(f"Generated test MOD scaffold: {output_root}")
    print("Bases: " + ", ".join(f"{pack.pack_id} ({pack.path})" for pack in base_packs))
    if args.install:
        print(f"Installed for automatic discovery by Game.Id: {args.pack_id}")
    for relative_name in removed_relative_names:
        print(f"REMOVED: {output_root / relative_name}")
    for relative_name in written_relative_names:
        print(f"WROTE: {output_root / relative_name}")
    if args.list_scenarios:
        print("Scenarios:")
        list_scenarios(output_root)
    return 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets-root", type=Path, default=REPO_ROOT / "assets")
    parser.add_argument("--pack-id", default=DEFAULT_PACK_ID)
    parser.add_argument("--pack-path", default=DEFAULT_PACK_PATH)
    parser.add_argument("--name", default="新剑侠情缘综合测试 MOD")
    parser.add_argument("--author", default=DEFAULT_AUTHOR)
    parser.add_argument("--base-id", default=DEFAULT_BASE_ID,
                        help="Ordered comma-separated content base ids; the first base supplies profile defaults.")
    parser.add_argument("--save-namespace", default=None)
    parser.add_argument("--game-type", type=int, default=None, help="Override Game.Type. Defaults to the base profile type.")
    parser.add_argument("--output-root", type=Path, default=None, help="Output directory for non-install mode.")
    parser.add_argument("--install", action="store_true", help="Write the pack under assets-root/pack-path.")
    parser.add_argument("--force", action="store_true", help="Overwrite generated files in the selected output directory.")
    parser.add_argument("--clean", action="store_true", help="Remove files generated by current or legacy scaffold versions before writing.")
    parser.add_argument("--list-scenarios", action="store_true", help="Print generated scenario status after validation.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    try:
        return scaffold(parse_args(argv))
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
