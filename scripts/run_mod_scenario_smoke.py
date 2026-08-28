#!/usr/bin/env python3
from __future__ import annotations

import argparse
import configparser
import contextlib
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator


DEFAULT_RESOURCE_ID = "XJXQY_TEST_MOD"
DEFAULT_SCENARIOS_INI = Path("assets") / "xjxqy_test_mod" / "ini" / "test_mod_scenarios.ini"
DEFAULT_LOG_DIR = Path("tmp") / "mod_scenario_smoke"
DEFAULT_SCENARIO_TIMEOUT_SECONDS = 90
TRUE_VALUES = {"1", "true", "yes", "on"}
FALSE_VALUES = {"0", "false", "no", "off"}
SCENARIO_STATUSES = {"ready", "needs_fixture", "blocked"}
SCENARIO_KEYS = {
    "Choice",
    "Status",
    "Smoke",
    "EntryScript",
    "PostNewGameWaitMilliseconds",
    "TimeoutSeconds",
    "ExpectVariables",
    "ExpectVariablesExtra",
    "ExpectSavedGoodsSlots",
    "Requires",
    "VisibleResult",
}
PORTABLE_PATH_FORBIDDEN_CHARACTERS = set('<>:"|?*\0')


@dataclass(frozen=True)
class Scenario:
    section: str
    name: str
    choice: int
    status: str
    smoke: bool
    post_newgame_wait_ms: int
    timeout_seconds: int
    expected_variables: tuple[tuple[str, int], ...]
    expected_saved_goods_slots: tuple["SavedGoodsSlotsExpectation", ...]


@dataclass(frozen=True)
class SavedGoodsSlotsExpectation:
    index: int
    ini_file: str
    slot_count: int
    number_per_slot: int


@dataclass(frozen=True)
class DiscoveredResourcePack:
    pack_id: str
    root: Path
    manifest: str
    base_ids: str


def parse_name_value_int(text: str) -> tuple[str, int]:
    name, separator, value_text = text.partition("=")
    name = name.strip()
    value_text = value_text.strip()
    if not separator or not name or not value_text:
        raise ValueError(f"expected name=value, got {text!r}")
    try:
        value = int(value_text, 10)
    except ValueError as exc:
        raise ValueError(f"expected integer value in {text!r}") from exc
    return name, value


def parse_expected_variables(raw: str) -> tuple[tuple[str, int], ...]:
    if not raw.strip():
        return tuple()
    result = tuple(parse_name_value_int(item.strip()) for item in raw.split(";") if item.strip())
    names: set[str] = set()
    for name, _ in result:
        if name in names:
            raise ValueError(f"duplicate expected variable {name!r}")
        names.add(name)
    return result


def parse_saved_goods_slots(raw: str) -> tuple[SavedGoodsSlotsExpectation, ...]:
    if not raw.strip():
        return tuple()
    expectations: list[SavedGoodsSlotsExpectation] = []
    for item in raw.split(";"):
        item = item.strip()
        if not item:
            continue
        parts = [part.strip() for part in item.split("|")]
        if len(parts) != 4:
            raise ValueError(f"expected goods slots format index|iniFile|slotCount|numberPerSlot, got {item!r}")
        try:
            expectation = SavedGoodsSlotsExpectation(
                index=int(parts[0], 10),
                ini_file=parts[1],
                slot_count=int(parts[2], 10),
                number_per_slot=int(parts[3], 10),
            )
        except ValueError as exc:
            raise ValueError(f"expected integer goods slot fields in {item!r}") from exc
        if expectation.index < 0 or expectation.slot_count < 0 or expectation.number_per_slot < 0:
            raise ValueError(f"goods slot fields must be non-negative in {item!r}")
        if not expectation.ini_file:
            raise ValueError(f"goods slot iniFile must not be empty in {item!r}")
        expectations.append(expectation)
    return tuple(expectations)


def validate_portable_name(value: str, label: str) -> str:
    value = value.strip()
    if not value or value in {".", ".."}:
        raise ValueError(f"{label} must not be empty or a dot path")
    if any(character in PORTABLE_PATH_FORBIDDEN_CHARACTERS or ord(character) < 32
           for character in value):
        raise ValueError(f"{label} contains a non-portable character: {value!r}")
    if "/" in value or "\\" in value:
        raise ValueError(f"{label} must be a file name, not a path: {value!r}")
    return value


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
            if is_link_or_junction(current):
                raise ValueError(f"{label} contains a symlink or junction: {current}")
    candidate = lexical_candidate.resolve()
    try:
        candidate.relative_to(root)
    except ValueError as exc:
        raise ValueError(f"{label} escapes {root}: {relative_value!r}") from exc
    return candidate


def parse_strict_boolean(value: str, label: str) -> bool:
    normalized = value.strip().lower()
    if normalized in TRUE_VALUES:
        return True
    if normalized in FALSE_VALUES:
        return False
    raise ValueError(f"{label} must be one of 1/0, true/false, yes/no, or on/off; got {value!r}")


def load_scenarios(path: Path) -> list[Scenario]:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    with path.open("r", encoding="utf-8-sig") as file:
        parser.read_file(file)
    if parser.defaults():
        raise ValueError(f"{path} contains unsupported DEFAULT values; define every Scenario.* key explicitly")

    scenarios: list[Scenario] = []
    seen_names: set[str] = set()
    seen_choices: set[int] = set()
    for section in parser.sections():
        if not section.startswith("Scenario."):
            raise ValueError(
                f"unexpected section {section!r} in {path}; only Scenario.* sections are supported"
            )
        unknown_keys = sorted(set(parser[section]) - SCENARIO_KEYS)
        if unknown_keys:
            raise ValueError(f"{section} contains unknown keys: {', '.join(unknown_keys)}")
        name = section[len("Scenario."):]
        validate_portable_name(name, f"{section} name")
        normalized_name = name.casefold()
        if normalized_name in seen_names:
            raise ValueError(f"duplicate scenario name ignoring case: {name!r}")
        seen_names.add(normalized_name)

        choice_text = parser.get(section, "Choice", fallback="").strip()
        if not choice_text:
            raise ValueError(f"{section} is missing Choice")
        try:
            choice = int(choice_text, 10)
        except ValueError as exc:
            raise ValueError(f"{section}.Choice must be an integer, got {choice_text!r}") from exc
        if choice < 0:
            raise ValueError(f"{section}.Choice must be non-negative")
        if choice in seen_choices:
            raise ValueError(f"duplicate scenario Choice={choice}")
        seen_choices.add(choice)

        smoke_text = parser.get(section, "Smoke", fallback="").strip().lower()
        smoke = parse_strict_boolean(smoke_text, f"{section}.Smoke")
        status = parser.get(section, "Status", fallback="").strip().lower()
        if status not in SCENARIO_STATUSES:
            raise ValueError(
                f"{section}.Status must be one of {', '.join(sorted(SCENARIO_STATUSES))}; got {status!r}"
            )
        entry_script = parser.get(section, "EntryScript", fallback="").strip()
        validate_portable_name(entry_script, f"{section}.EntryScript")
        expected_variables_text = ";".join(
            raw
            for raw in [
                parser.get(section, "ExpectVariables", fallback=""),
                parser.get(section, "ExpectVariablesExtra", fallback=""),
            ]
            if raw.strip()
        )
        post_newgame_wait_ms = parser.getint(section, "PostNewGameWaitMilliseconds", fallback=0)
        if post_newgame_wait_ms < 0:
            raise ValueError(f"{section}.PostNewGameWaitMilliseconds must be non-negative")
        timeout_seconds = parser.getint(section, "TimeoutSeconds", fallback=0)
        if timeout_seconds < 0:
            raise ValueError(f"{section}.TimeoutSeconds must be non-negative")
        scenarios.append(
            Scenario(
                section=section,
                name=name,
                choice=choice,
                status=status,
                smoke=smoke,
                post_newgame_wait_ms=post_newgame_wait_ms,
                timeout_seconds=timeout_seconds,
                expected_variables=parse_expected_variables(expected_variables_text),
                expected_saved_goods_slots=parse_saved_goods_slots(
                    parser.get(section, "ExpectSavedGoodsSlots", fallback="")
                ),
            )
        )
    if not scenarios:
        raise ValueError(f"no Scenario.* sections found in {path}")
    return scenarios


def select_scenarios(scenarios: list[Scenario], names: list[str]) -> list[Scenario]:
    if not names:
        return [scenario for scenario in scenarios if scenario.smoke and scenario.status == "ready"]

    by_name = {scenario.name.lower(): scenario for scenario in scenarios}
    by_section = {scenario.section.lower(): scenario for scenario in scenarios}
    selected: list[Scenario] = []
    for name in names:
        key = name.lower()
        scenario = by_name.get(key) or by_section.get(key)
        if scenario is None:
            raise ValueError(f"unknown scenario {name!r}")
        selected.append(scenario)
    return selected


def executable_candidates(repo_root: Path) -> tuple[Path, ...]:
    windows_candidates = (
        repo_root / "bin" / "win64" / "Debug" / "jxqy-all-in-one-debug.exe",
        repo_root / "bin" / "win32" / "Debug" / "jxqy-all-in-one-debug.exe",
        repo_root / "bin" / "win64" / "Release" / "jxqy-all-in-one.exe",
        repo_root / "bin" / "win32" / "Release" / "jxqy-all-in-one.exe",
        repo_root / "bin" / "win64" / "Debug" / "jxqy-all-in-one.exe",
        repo_root / "bin" / "win32" / "Debug" / "jxqy-all-in-one.exe",
    )
    unix_candidates = (
        repo_root / "bin" / "linux" / "jxqy-all-in-one-debug",
        repo_root / "bin" / "linux" / "jxqy-all-in-one",
        repo_root / "bin" / "macos" / "jxqy-all-in-one-debug",
        repo_root / "bin" / "macos" / "jxqy-all-in-one",
    )
    return windows_candidates + unix_candidates if sys.platform == "win32" else unix_candidates + windows_candidates


def default_executable(repo_root: Path) -> Path:
    candidates = executable_candidates(repo_root)
    return next((candidate for candidate in candidates if candidate.is_file()), candidates[0])


def log_tail(path: Path, line_count: int = 40) -> str:
    if not path.exists():
        return "<log missing>"
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    return "\n".join(lines[-line_count:])


def load_ini(path: Path) -> configparser.ConfigParser:
    parser = configparser.ConfigParser(
        interpolation=None,
        strict=False,
        default_section="__JXQY_CONFIGPARSER_DEFAULTS_DISABLED__",
        inline_comment_prefixes=(";",),
    )
    parser.optionxform = str
    with path.open("r", encoding="utf-8-sig", errors="replace") as file:
        parser.read_file(file)
    return parser


def get_ini_value_case_insensitive(
    parser: configparser.ConfigParser,
    section: str,
    key: str,
    fallback: str = "",
) -> str:
    result = fallback
    for actual_section in parser.sections():
        if actual_section.casefold() != section.casefold():
            continue
        for existing_key, value in parser.items(actual_section, raw=True):
            if existing_key.casefold() == key.casefold():
                result = value
    return result


def discover_resource_pack(assets_root: Path, resource_id: str) -> DiscoveredResourcePack | None:
    assets_root = assets_root.resolve()
    candidate_roots: list[Path] = []
    if (assets_root / "game_profile.ini").is_file():
        candidate_roots.append(assets_root)
    for child in sorted(
        assets_root.iterdir(),
        key=lambda path: (path.name.casefold(), path.name),
    ):
        if child.name.startswith(".") or is_link_or_junction(child):
            continue
        if child.is_dir() and (child / "game_profile.ini").is_file():
            candidate_roots.append(child.resolve())

    matches: list[DiscoveredResourcePack] = []
    for pack_root in candidate_roots:
        profile_path = pack_root / "game_profile.ini"
        try:
            profile = load_ini(profile_path)
        except (OSError, configparser.Error, ValueError):
            continue
        pack_id = get_ini_value_case_insensitive(profile, "Game", "Id").strip()
        if not pack_id or pack_id.casefold() != resource_id.casefold():
            continue
        matches.append(
            DiscoveredResourcePack(
                pack_id=pack_id,
                root=pack_root,
                manifest="game_profile.ini",
                base_ids=get_ini_value_case_insensitive(
                    profile,
                    "Resource",
                    "DependencyId",
                ).strip(),
            )
        )
    if len(matches) > 1:
        raise ValueError(
            f"duplicate Game.Id {resource_id!r}: "
            + ", ".join(str(match.root) for match in matches)
        )
    return matches[0] if matches else None


def resource_pack_path(assets_root: Path, resource_id: str) -> Path:
    assets_root = assets_root.resolve()
    registration = discover_resource_pack(assets_root, resource_id)
    if registration is not None:
        return registration.root
    raise ValueError(
        f"resource pack was not discovered from a root-level or direct-child "
        f"game_profile.ini: {resource_id}"
    )


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


def resource_pack_dependency_ids(assets_root: Path, resource_id: str, pack_root: Path) -> list[str]:
    registration = discover_resource_pack(assets_root, resource_id)
    if registration is not None and registration.base_ids:
        return split_dependency_ids(registration.base_ids)

    profile_path = resolve_contained_path(
        pack_root,
        registration.manifest if registration is not None else "game_profile.ini",
        f"resource pack {resource_id!r} manifest",
    )
    if not profile_path.exists():
        return []
    parser = load_ini(profile_path)
    return split_dependency_ids(
        get_ini_value_case_insensitive(parser, "Resource", "DependencyId")
    )


def find_initial_save_template(
    assets_root: Path,
    resource_id: str,
    visited: set[str] | None = None,
) -> Path | None:
    if visited is None:
        visited = set()
    normalized_id = resource_id.strip().lower()
    if not normalized_id or normalized_id in visited:
        return None
    visited.add(normalized_id)

    pack_root = resource_pack_path(assets_root, resource_id)
    own_template = resolve_contained_path(
        pack_root,
        "ini/save",
        f"resource pack {resource_id!r} initial save template",
    )
    if (own_template / "game.ini").exists():
        return own_template

    for dependency_id in resource_pack_dependency_ids(assets_root, resource_id, pack_root):
        template = find_initial_save_template(assets_root, dependency_id, visited)
        if template is not None:
            return template
    return None


def copy_directory_contents(source: Path, target: Path) -> None:
    if is_link_or_junction(source):
        raise ValueError(f"initial save template is a symlink or junction: {source}")
    if is_link_or_junction(target):
        raise ValueError(f"initial save destination is a symlink or junction: {target}")
    source = source.resolve()
    target = target.resolve()
    ensure_tree_is_contained(source, "initial save template")
    target.mkdir(parents=True, exist_ok=True)
    for path in source.rglob("*"):
        relative = path.relative_to(source)
        destination = resolve_contained_path(
            target,
            relative,
            "initial save destination",
            reject_links=True,
        )
        if path.is_dir():
            destination.mkdir(parents=True, exist_ok=True)
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)


def ensure_initial_save_seed(assets_root: Path, resource_id: str) -> None:
    pack_root = resource_pack_path(assets_root, resource_id)
    seed_dir = resolve_contained_path(
        pack_root,
        "save/rpg0",
        f"resource pack {resource_id!r} initial save seed",
        reject_links=True,
    )
    if (seed_dir / "game.ini").exists():
        return

    template_dir = find_initial_save_template(assets_root, resource_id)
    if template_dir is None:
        return

    copy_directory_contents(template_dir, seed_dir)


def check_saved_goods_slots(
    assets_root: Path,
    resource_id: str,
    expectations: tuple[SavedGoodsSlotsExpectation, ...],
    previous_file_states: dict[Path, tuple[int, int, int]] | None = None,
) -> bool:
    ok = True
    pack_root = resource_pack_path(assets_root, resource_id)
    for expectation in expectations:
        path = resolve_contained_path(
            pack_root,
            f"save/game/goods{expectation.index}.ini",
            "saved goods result",
        )
        if not path.exists():
            print(f"FAIL saved goods check: missing {path}", file=sys.stderr)
            ok = False
            continue
        if previous_file_states is not None:
            stat = path.stat()
            current_state = (stat.st_ctime_ns, stat.st_mtime_ns, stat.st_size)
            if previous_file_states.get(path) == current_state:
                print(f"FAIL saved goods check: stale pre-run file {path}", file=sys.stderr)
                ok = False
                continue
        parser = load_ini(path)
        matching_slots = 0
        bad_number_slots: list[str] = []
        for section in parser.sections():
            if section.lower() == "head":
                continue
            ini_file = get_ini_value_case_insensitive(parser, section, "IniFile")
            if ini_file.lower() != expectation.ini_file.lower():
                continue
            number_text = get_ini_value_case_insensitive(parser, section, "Number", "0")
            try:
                number = int(number_text, 10)
            except ValueError:
                number = 0
            if number == expectation.number_per_slot:
                matching_slots += 1
            else:
                bad_number_slots.append(f"{section}:{number}")
        if matching_slots != expectation.slot_count or bad_number_slots:
            print(
                "FAIL saved goods check: "
                f"goods{expectation.index}.ini expected {expectation.slot_count} slots of "
                f"{expectation.ini_file} with Number={expectation.number_per_slot}, "
                f"got {matching_slots}; bad slots={','.join(bad_number_slots) or '-'}",
                file=sys.stderr,
            )
            ok = False
    return ok


def expected_saved_goods_file_states(
    pack_root: Path,
    expectations: tuple[SavedGoodsSlotsExpectation, ...],
) -> dict[Path, tuple[int, int, int]]:
    result: dict[Path, tuple[int, int, int]] = {}
    for expectation in expectations:
        path = resolve_contained_path(
            pack_root,
            f"save/game/goods{expectation.index}.ini",
            "saved goods result",
        )
        if path.exists():
            stat = path.stat()
            result[path] = (stat.st_ctime_ns, stat.st_mtime_ns, stat.st_size)
    return result


def is_link_or_junction(path: Path) -> bool:
    return path.is_symlink() or getattr(path, "is_junction", lambda: False)()


def ensure_tree_is_contained(root: Path, label: str) -> None:
    if is_link_or_junction(root):
        raise ValueError(f"{label} is a symlink or junction: {root}")
    root = root.resolve()
    if not root.exists():
        return
    if not root.is_dir():
        raise ValueError(f"{label} is not a directory: {root}")
    for directory, directory_names, file_names in os.walk(root, followlinks=False):
        for name in [*directory_names, *file_names]:
            path = Path(directory) / name
            if is_link_or_junction(path):
                raise ValueError(f"{label} contains a symlink or junction: {path}")
            try:
                path.resolve().relative_to(root)
            except ValueError as exc:
                raise ValueError(f"{label} contains an escaping path: {path}") from exc


@contextlib.contextmanager
def preserve_resource_save_state(pack_root: Path) -> Iterator[None]:
    pack_root = pack_root.resolve()
    save_root = resolve_contained_path(
        pack_root,
        "save",
        "resource save directory",
        reject_links=True,
    )
    backup_root = resolve_contained_path(
        pack_root,
        ".mod-scenario-smoke-save-backup",
        "resource save backup directory",
        reject_links=True,
    )
    if backup_root.exists():
        raise ValueError(
            f"a previous smoke save backup still exists: {backup_root}; "
            "restore or remove it after confirming no smoke process is running"
        )
    existed = save_root.exists()
    if existed:
        ensure_tree_is_contained(save_root, "resource save directory")
        save_root.rename(backup_root)
    try:
        yield
    finally:
        if save_root.exists() or is_link_or_junction(save_root):
            if is_link_or_junction(save_root):
                raise ValueError(
                    f"refusing to remove a linked resource save directory: {save_root}; "
                    f"the original save remains in {backup_root}"
                )
            resolved_save_root = save_root.resolve()
            if (resolved_save_root != save_root or
                    resolved_save_root.parent != pack_root or
                    resolved_save_root.name != "save"):
                raise ValueError(
                    f"refusing to remove an unexpected resource save directory: {resolved_save_root}; "
                    f"the original save remains in {backup_root}"
                )
            ensure_tree_is_contained(save_root, "fresh resource save directory")
            shutil.rmtree(save_root)
        if existed:
            if not backup_root.exists():
                raise ValueError(f"resource save backup disappeared during smoke run: {backup_root}")
            backup_root.rename(save_root)


@contextlib.contextmanager
def exclusive_run_lock(lock_path: Path) -> Iterator[None]:
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    if is_link_or_junction(lock_path):
        raise ValueError(f"refusing to use a linked lock path: {lock_path}")
    try:
        descriptor = os.open(lock_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError as exc:
        try:
            owner = lock_path.read_text(encoding="ascii", errors="replace").strip()
        except OSError:
            owner = "owner metadata unavailable"
        raise ValueError(
            f"another smoke run may be using this resource or log directory; lock exists: "
            f"{lock_path} ({owner}). Remove it only after confirming that process is no longer running"
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


def resource_pack_lock_path(pack_root: Path) -> Path:
    return pack_root.parent / f".{pack_root.name}.jxqy-pack.lock"


def scenario_log_path(log_dir: Path, scenario_name: str) -> Path:
    validate_portable_name(scenario_name, "scenario name")
    return resolve_contained_path(
        log_dir,
        f"{scenario_name}.log",
        "scenario log",
        reject_links=True,
    )


def run_scenario(
    repo_root: Path,
    executable: Path,
    assets_root: Path,
    resource_id: str,
    log_dir: Path,
    timeout_seconds: int,
    scenario: Scenario,
    verbose: bool,
    prepare_initial_save: bool,
) -> int:
    if timeout_seconds <= 0:
        raise ValueError("timeout seconds must be positive")
    if prepare_initial_save:
        ensure_initial_save_seed(assets_root, resource_id)

    log_path = scenario_log_path(log_dir, scenario.name)
    if log_path.exists():
        log_path.unlink()

    pack_root = resource_pack_path(assets_root, resource_id)
    previous_saved_goods_states = expected_saved_goods_file_states(
        pack_root,
        scenario.expected_saved_goods_slots,
    )

    command = [
        str(executable),
        "--assets",
        str(assets_root),
        "--resource-id",
        resource_id,
        "--skip-startup-video",
        "--enable-automation-hooks",
        "--test-scenario-choice",
        str(scenario.choice),
        "--exit-after-newgame-script",
        "--log-file",
        str(log_path),
    ]
    if scenario.post_newgame_wait_ms > 0:
        command.extend(["--post-newgame-wait-ms", str(scenario.post_newgame_wait_ms)])
    for name, value in scenario.expected_variables:
        command.extend(["--expect-int", f"{name}={value}"])

    print(f"RUN {scenario.section} choice={scenario.choice} log={log_path}")
    completed = subprocess.run(
        command,
        cwd=repo_root,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        timeout=timeout_seconds,
    )
    if verbose and completed.stdout and completed.stdout.strip():
        print(completed.stdout.rstrip())
    if verbose and completed.stderr and completed.stderr.strip():
        print(completed.stderr.rstrip(), file=sys.stderr)

    if completed.returncode != 0:
        print(f"FAIL {scenario.section}: exit code {completed.returncode}", file=sys.stderr)
        if not verbose and completed.stderr and completed.stderr.strip():
            print(completed.stderr.rstrip(), file=sys.stderr)
        print(log_tail(log_path), file=sys.stderr)
        return completed.returncode

    if scenario.expected_saved_goods_slots and not check_saved_goods_slots(
        assets_root,
        resource_id,
        scenario.expected_saved_goods_slots,
        previous_saved_goods_states,
    ):
        print(log_tail(log_path), file=sys.stderr)
        return 1

    print(f"PASS {scenario.section}")
    return 0


def main(argv: list[str] | None = None) -> int:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(
        description="Run parameterized MOD test scenarios and assert their result variables."
    )
    parser.add_argument(
        "--exe",
        type=Path,
        default=default_executable(repo_root),
        help="Game executable. Defaults to the first existing platform/configuration candidate.",
    )
    parser.add_argument("--assets-root", type=Path, default=repo_root / "assets")
    parser.add_argument("--resource-id", default=DEFAULT_RESOURCE_ID)
    parser.add_argument("--scenarios-ini", type=Path, default=repo_root / DEFAULT_SCENARIOS_INI)
    parser.add_argument("--log-dir", type=Path, default=repo_root / DEFAULT_LOG_DIR)
    parser.add_argument(
        "--timeout-seconds",
        type=int,
        default=None,
        help=(
            "Override every scenario timeout. Without this option, each scenario uses TimeoutSeconds "
            f"or the {DEFAULT_SCENARIO_TIMEOUT_SECONDS}s default."
        ),
    )
    parser.add_argument(
        "--scenario",
        action="append",
        default=[],
        help="Scenario name or section. Defaults to Smoke=1 ready scenarios.",
    )
    parser.add_argument("--list", action="store_true", help="List selected scenarios without running them.")
    parser.add_argument("--verbose", action="store_true", help="Print captured game stdout and stderr.")
    parser.add_argument(
        "--no-prepare-initial-save",
        action="store_true",
        help="Do not seed save/rpg0 from the selected resource pack's own or dependency ini/save template.",
    )
    parser.add_argument(
        "--keep-save-state",
        action="store_true",
        help="Keep save/ changes after each scenario for debugging. The default snapshots and restores them.",
    )
    args = parser.parse_args(argv)

    if args.list:
        try:
            scenarios = load_scenarios(args.scenarios_ini.resolve())
            selected = select_scenarios(scenarios, args.scenario)
        except (OSError, configparser.Error, ValueError) as exc:
            print(f"Invalid scenario configuration: {exc}", file=sys.stderr)
            return 2
        if not selected:
            print("No scenarios selected", file=sys.stderr)
            return 2
        for scenario in selected:
            expects = ";".join(f"{name}={value}" for name, value in scenario.expected_variables)
            wait = f" postWaitMs={scenario.post_newgame_wait_ms}" if scenario.post_newgame_wait_ms > 0 else ""
            timeout = f" timeout={scenario.timeout_seconds}s" if scenario.timeout_seconds > 0 else ""
            saved_goods = ";".join(
                f"{item.index}|{item.ini_file}|{item.slot_count}|{item.number_per_slot}"
                for item in scenario.expected_saved_goods_slots
            )
            saved_goods_text = f" savedGoods={saved_goods}" if saved_goods else ""
            seed = " seed=off" if args.no_prepare_initial_save else " seed=on"
            save_state = " save=keep" if args.keep_save_state else " save=restore"
            print(
                f"{scenario.section}: choice={scenario.choice} status={scenario.status}"
                f"{wait}{timeout}{seed}{save_state} expect={expects}{saved_goods_text}"
            )
        return 0

    executable = args.exe.resolve()
    if not executable.is_file():
        print(f"Executable not found: {executable}", file=sys.stderr)
        return 2
    if args.timeout_seconds is not None and args.timeout_seconds <= 0:
        print("--timeout-seconds must be positive", file=sys.stderr)
        return 2

    assets_root = args.assets_root.resolve()
    log_dir = args.log_dir.resolve()
    log_dir.mkdir(parents=True, exist_ok=True)

    failed = 0
    try:
        with contextlib.ExitStack() as lock_stack:
            pack_root = resource_pack_path(assets_root, args.resource_id)
            external_pack_lock_path = resource_pack_lock_path(pack_root)
            lock_stack.enter_context(exclusive_run_lock(external_pack_lock_path))
            legacy_pack_lock_path = resolve_contained_path(
                pack_root,
                ".mod-scenario-smoke.lock",
                "legacy resource smoke lock",
                reject_links=True,
            )
            lock_stack.enter_context(exclusive_run_lock(legacy_pack_lock_path))
            log_lock_path = log_dir / ".mod-scenario-smoke.lock"
            if log_lock_path not in {
                external_pack_lock_path,
                legacy_pack_lock_path,
            }:
                lock_stack.enter_context(exclusive_run_lock(log_lock_path))

            try:
                scenarios = load_scenarios(args.scenarios_ini.resolve())
                selected = select_scenarios(scenarios, args.scenario)
            except (OSError, configparser.Error, ValueError) as exc:
                print(f"Invalid scenario configuration: {exc}", file=sys.stderr)
                return 2
            if not selected:
                print("No scenarios selected", file=sys.stderr)
                return 2

            for scenario in selected:
                timeout_seconds = (
                    args.timeout_seconds
                    if args.timeout_seconds is not None
                    else scenario.timeout_seconds or DEFAULT_SCENARIO_TIMEOUT_SECONDS
                )
                try:
                    save_context = (
                        contextlib.nullcontext()
                        if args.keep_save_state
                        else preserve_resource_save_state(pack_root)
                    )
                    with save_context:
                        result = run_scenario(
                            repo_root=repo_root,
                            executable=executable,
                            assets_root=assets_root,
                            resource_id=args.resource_id,
                            log_dir=log_dir,
                            timeout_seconds=timeout_seconds,
                            scenario=scenario,
                            verbose=args.verbose,
                            prepare_initial_save=not args.no_prepare_initial_save,
                        )
                except subprocess.TimeoutExpired:
                    print(
                        f"FAIL {scenario.section}: timed out after {timeout_seconds}s",
                        file=sys.stderr,
                    )
                    failed = 1
                    continue
                except (OSError, configparser.Error, ValueError) as exc:
                    print(f"FAIL {scenario.section}: {exc}", file=sys.stderr)
                    return 2
                if result != 0:
                    failed = 1
    except (OSError, ValueError) as exc:
        print(f"Smoke run setup failed: {exc}", file=sys.stderr)
        return 2

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
