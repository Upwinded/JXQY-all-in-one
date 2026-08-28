#!/usr/bin/env python3
from __future__ import annotations

import argparse
import contextlib
import itertools
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

from run_mod_scenario_smoke import (
    ensure_initial_save_seed,
    exclusive_run_lock,
    preserve_resource_save_state,
    resource_pack_lock_path,
    resource_pack_path,
    resolve_contained_path,
)


SKIP_RETURN_CODE = 77
PROCESS_TIMEOUT_SECONDS = 420
NON_EXITING_MARKER_TIMEOUT_SECONDS = 45
WAIT_MILLISECONDS = 350


@dataclass(frozen=True)
class Combination:
    name: str
    arguments: tuple[str, ...]
    auto_start: bool
    exit_after_script: bool
    wait_milliseconds: int


COMBINATIONS = (
    Combination("none", tuple(), False, False, 0),
    Combination("auto", ("--newgame",), True, False, 0),
    Combination(
        "exit",
        ("--exit-after-newgame-script",),
        True,
        True,
        0,
    ),
    Combination(
        "wait",
        ("--post-newgame-wait-ms", str(WAIT_MILLISECONDS)),
        True,
        True,
        WAIT_MILLISECONDS,
    ),
    Combination(
        "auto-exit",
        ("--newgame", "--exit-after-newgame-script"),
        True,
        True,
        0,
    ),
    Combination(
        "auto-wait",
        (
            "--newgame",
            "--post-newgame-wait-ms",
            str(WAIT_MILLISECONDS),
        ),
        True,
        True,
        WAIT_MILLISECONDS,
    ),
    Combination(
        "exit-wait",
        (
            "--exit-after-newgame-script",
            "--post-newgame-wait-ms",
            str(WAIT_MILLISECONDS),
        ),
        True,
        True,
        WAIT_MILLISECONDS,
    ),
    Combination(
        "auto-exit-wait",
        (
            "--newgame",
            "--exit-after-newgame-script",
            "--post-newgame-wait-ms",
            str(WAIT_MILLISECONDS),
        ),
        True,
        True,
        WAIT_MILLISECONDS,
    ),
)


def argument_orderings(arguments: tuple[str, ...]) -> tuple[tuple[str, ...], ...]:
    groups: list[tuple[str, ...]] = []
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if argument == "--post-newgame-wait-ms":
            if index + 1 >= len(arguments):
                raise ValueError("missing --post-newgame-wait-ms value")
            groups.append((argument, arguments[index + 1]))
            index += 2
            continue
        groups.append((argument,))
        index += 1

    orderings: list[tuple[str, ...]] = []
    seen: set[tuple[str, ...]] = set()
    for ordering in itertools.permutations(groups):
        flattened = tuple(
            token
            for group in ordering
            for token in group
        )
        if flattened not in seen:
            seen.add(flattened)
            orderings.append(flattened)
    return tuple(orderings) if orderings else (tuple(),)


def read_log(log_path: Path) -> str:
    try:
        return log_path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def log_tail(log_path: Path, line_count: int = 40) -> str:
    content = read_log(log_path)
    if not content:
        return f"(log not created: {log_path})"
    return "\n".join(content.splitlines()[-line_count:])


def stop_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def wait_for_log_marker(
    process: subprocess.Popen[bytes],
    log_path: Path,
    marker: str,
) -> None:
    deadline = time.monotonic() + NON_EXITING_MARKER_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        if marker in read_log(log_path):
            return
        return_code = process.poll()
        if return_code is not None:
            raise RuntimeError(
                f"process exited with code {return_code} before marker "
                f"{marker!r}\n{log_tail(log_path)}"
            )
        time.sleep(0.05)
    raise RuntimeError(
        f"process did not reach marker {marker!r}\n{log_tail(log_path)}"
    )


def run_argument_probe(probe: Path) -> None:
    for combination in COMBINATIONS:
        for ordering_index, arguments in enumerate(
            argument_orderings(combination.arguments),
            start=1,
        ):
            completed = subprocess.run(
                [
                    str(probe),
                    "--enable-automation-hooks",
                    *arguments,
                ],
                text=True,
                encoding="utf-8",
                errors="replace",
                capture_output=True,
                timeout=10,
            )
            expected = (
                f"auto-start={int(combination.auto_start)};"
                f"wait-ms={combination.wait_milliseconds};"
                f"exit={int(combination.exit_after_script)};"
                "hooks=1"
            )
            if completed.returncode != 0 or completed.stdout.strip() != expected:
                raise RuntimeError(
                    f"argument probe failed for {combination.name} ordering "
                    f"{ordering_index}: expected {expected!r}, "
                    f"code={completed.returncode}, "
                    f"stdout={completed.stdout!r}, "
                    f"stderr={completed.stderr!r}"
                )


def run_game_combination(
    game: Path,
    repo_root: Path,
    assets_root: Path,
    resource_id: str,
    log_directory: Path,
    combination: Combination,
    arguments: tuple[str, ...],
    run_name: str,
) -> None:
    log_path = log_directory / f"{run_name}.log"
    user_data_root = resolve_contained_path(
        log_directory,
        f"{run_name}-state",
        "new-game automation user data root",
        reject_links=True,
    )
    command = [
        str(game),
        "--assets",
        str(assets_root),
        "--user-data-root",
        str(user_data_root),
        "--resource-id",
        resource_id,
        "--skip-startup-video",
        "--enable-automation-hooks",
        "--startup-int",
        "mod_test_auto_scenario_choice=1",
        "--startup-int",
        "mod_test_auto_scenario_enabled=1",
        "--log-file",
        str(log_path),
        *arguments,
    ]

    started_at = time.monotonic()
    process = subprocess.Popen(
        command,
        cwd=repo_root,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        if not combination.exit_after_script:
            marker = (
                "Game: auto start new game by launch argument"
                if combination.auto_start
                else "Begin Game Title"
            )
            wait_for_log_marker(process, log_path, marker)
            time.sleep(0.2)
            if process.poll() is not None:
                raise RuntimeError(
                    f"{combination.name} exited although no exit stage was "
                    f"requested\n{log_tail(log_path)}"
                )
            return

        try:
            return_code = process.wait(timeout=PROCESS_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError(
                f"{combination.name} did not exit within "
                f"{PROCESS_TIMEOUT_SECONDS}s\n{log_tail(log_path)}"
            ) from exc

        elapsed_seconds = time.monotonic() - started_at
        content = read_log(log_path)
        if return_code != 0:
            raise RuntimeError(
                f"{combination.name} exited with code {return_code}\n"
                f"{log_tail(log_path)}"
            )
        if "Game: auto start new game by launch argument" not in content:
            raise RuntimeError(
                f"{combination.name} did not auto-start\n{log_tail(log_path)}"
            )
        if "GameManager: exit after new game script by launch argument" not in content:
            raise RuntimeError(
                f"{combination.name} did not execute the exit stage\n"
                f"{log_tail(log_path)}"
            )
        wait_marker = (
            "GameManager: post new game automation wait "
            f"{combination.wait_milliseconds} ms"
        )
        if combination.wait_milliseconds > 0:
            if wait_marker not in content:
                raise RuntimeError(
                    f"{combination.name} did not schedule its wait stage\n"
                    f"{log_tail(log_path)}"
                )
            minimum_seconds = combination.wait_milliseconds / 1000.0
            if elapsed_seconds < minimum_seconds:
                raise RuntimeError(
                    f"{combination.name} exited after {elapsed_seconds:.3f}s, "
                    f"before its {minimum_seconds:.3f}s wait could complete"
                )
        elif "GameManager: post new game automation wait " in content:
            raise RuntimeError(
                f"{combination.name} unexpectedly scheduled a wait stage\n"
                f"{log_tail(log_path)}"
            )
    finally:
        stop_process(process)


def run_unauthorized_argument_probe(
    game: Path,
    repo_root: Path,
    arguments: tuple[str, ...],
    probe_name: str,
) -> None:
    try:
        completed = subprocess.run(
            [
                str(game),
                *arguments,
            ],
            cwd=repo_root,
            text=True,
            encoding="utf-8",
            errors="replace",
            capture_output=True,
            timeout=10,
        )
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(
            f"unauthorized {probe_name} argument was not rejected at startup"
        ) from exc
    if completed.returncode != 64:
        raise RuntimeError(
            f"unauthorized {probe_name} argument returned "
            f"{completed.returncode}, expected 64; "
            f"stdout={completed.stdout!r}; stderr={completed.stderr!r}"
        )
    diagnostic = completed.stdout + completed.stderr
    if "--enable-automation-hooks" not in diagnostic:
        raise RuntimeError(
            f"unauthorized {probe_name} rejection did not explain the required "
            f"authorization; stdout={completed.stdout!r}; "
            f"stderr={completed.stderr!r}"
        )


def run_restricted_argument_probes(
    game: Path,
    repo_root: Path,
) -> None:
    probes = (
        (
            "startup-int",
            ("--startup-int", "__automation_probe=1"),
        ),
        (
            "expect-int",
            ("--expect-int", "__automation_probe=1"),
        ),
        (
            "test-scenario-choice",
            ("--test-scenario-choice", "1"),
        ),
        (
            "test-scenario",
            ("--test-scenario", "1"),
        ),
        (
            "open-test-runner",
            ("--open-test-runner",),
        ),
    )
    for probe_name, arguments in probes:
        run_unauthorized_argument_probe(
            game,
            repo_root,
            arguments,
            probe_name,
        )
        print(
            f"PASS unauthorized {probe_name} argument is rejected at startup"
        )


def run_process_matrix(
    game: Path,
    repo_root: Path,
    assets_root: Path,
    resource_id: str,
) -> None:
    pack_root = resource_pack_path(assets_root, resource_id)
    with tempfile.TemporaryDirectory(
        prefix="jxqy-newgame-automation-"
    ) as temporary_directory:
        log_directory = Path(temporary_directory)
        with contextlib.ExitStack() as stack:
            stack.enter_context(
                exclusive_run_lock(
                    assets_root / ".resources.ini.jxqy-index.lock"
                )
            )
            stack.enter_context(
                exclusive_run_lock(resource_pack_lock_path(pack_root))
            )
            stack.enter_context(
                exclusive_run_lock(
                    resolve_contained_path(
                        pack_root,
                        ".mod-scenario-smoke.lock",
                        "legacy resource smoke lock",
                        reject_links=True,
                    )
                )
            )
            stack.enter_context(preserve_resource_save_state(pack_root))
            ensure_initial_save_seed(assets_root, resource_id)
            run_restricted_argument_probes(
                game,
                repo_root,
            )
            for combination in COMBINATIONS:
                # The parser probe above exhaustively checks every argument
                # ordering. The real game needs one launch per effective
                # combination; parser-equivalent permutations would only
                # repeat the same expensive resource startup.
                run_name = f"{combination.name}-order-1"
                run_game_combination(
                    game,
                    repo_root,
                    assets_root,
                    resource_id,
                    log_directory,
                    combination,
                    combination.arguments,
                    run_name,
                )
                print(f"PASS process combination {run_name}")


def process_assets_are_writable(
    assets_root: Path,
    resource_id: str,
) -> tuple[bool, str]:
    pack_root = resource_pack_path(assets_root, resource_id)
    for label, directory in (
        ("resource collection", assets_root),
        ("test resource pack", pack_root),
    ):
        try:
            with tempfile.NamedTemporaryFile(
                prefix=".jxqy-automation-write-probe-",
                dir=directory,
            ):
                pass
        except OSError as exc:
            return False, f"{label} is not writable: {exc}"
    return True, ""


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Verify every new-game automation option combination through "
            "both the parser probe and the real game process."
        )
    )
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--assets", required=True, type=Path)
    parser.add_argument("--resource-id", default="XJXQY_TEST_MOD")
    args = parser.parse_args(argv)

    probe = args.probe.resolve()
    game = args.game.resolve()
    repo_root = args.repo.resolve()
    assets_root = args.assets.resolve()
    required_paths = (
        probe,
        game,
        repo_root,
        assets_root / "resources.ini",
    )
    if any(not path.exists() for path in required_paths):
        missing = ", ".join(
            str(path) for path in required_paths if not path.exists()
        )
        print(f"SKIP missing process-test input: {missing}")
        return SKIP_RETURN_CODE

    try:
        resource_pack_path(assets_root, args.resource_id)
    except (OSError, ValueError) as exc:
        print(f"SKIP unavailable process-test resource: {exc}")
        return SKIP_RETURN_CODE

    writable, reason = process_assets_are_writable(
        assets_root,
        args.resource_id,
    )
    if not writable:
        print(f"SKIP process-test resources require write access: {reason}")
        return SKIP_RETURN_CODE

    try:
        run_argument_probe(probe)
        run_process_matrix(
            game,
            repo_root,
            assets_root,
            args.resource_id,
        )
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
