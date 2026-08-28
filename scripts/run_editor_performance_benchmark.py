#!/usr/bin/env python3
"""Run and aggregate the opt-in jxqy-editor GUI performance benchmark."""

from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
from typing import Any, Iterable


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_EDITOR = REPOSITORY_ROOT / "jxqy-editor" / "build" / "jxqy-editor-debug.exe"
BENCHMARK_ENVIRONMENT_VARIABLES = (
    "JXQY_EDITOR_PERFORMANCE_MAP",
    "JXQY_EDITOR_PERFORMANCE_ASSETS",
    "JXQY_EDITOR_PERFORMANCE_REPORT",
    "JXQY_EDITOR_PERFORMANCE_ITERATIONS",
    "JXQY_MAP_IDLE_BENCHMARK",
    "JXQY_MAP_IDLE_BENCHMARK_MS",
    "JXQY_MAP_IDLE_BENCHMARK_REPORT",
)


def _nearest_rank_percentile(values: Iterable[float], fraction: float) -> float:
    ordered = sorted(float(value) for value in values)
    if not ordered:
        raise ValueError("at least one value is required")
    if fraction <= 0.0 or fraction > 1.0:
        raise ValueError("fraction must be in (0, 1]")
    index = max(0, math.ceil(len(ordered) * fraction) - 1)
    return ordered[index]


def _metric_statistics(values: Iterable[float]) -> dict[str, float | int]:
    samples = [float(value) for value in values]
    if not samples:
        raise ValueError("at least one metric sample is required")
    return {
        "sample_count": len(samples),
        "minimum_ms": min(samples),
        "median_ms": statistics.median(samples),
        "p95_ms": _nearest_rank_percentile(samples, 0.95),
        "maximum_ms": max(samples),
    }


def _aggregate_runs(reports: list[dict[str, Any]]) -> dict[str, Any]:
    if not reports:
        raise ValueError("at least one benchmark report is required")
    if any(report.get("schema_version") != 1 for report in reports):
        raise ValueError("unsupported benchmark report schema")
    if any(not report.get("valid") for report in reports):
        raise ValueError("cannot aggregate an invalid benchmark report")

    fixed_contract_fields = (
        "map_path",
        "assets_path",
        "map_file_bytes",
        "qt_version",
        "platform",
        "cpu_architecture",
        "build_type",
        "contract",
        "map_size",
        "canvas_size",
    )
    baseline = reports[0]
    for field in fixed_contract_fields:
        if field not in baseline:
            raise ValueError(f"benchmark report is missing {field}")
        if any(report.get(field) != baseline[field] for report in reports[1:]):
            raise ValueError(f"benchmark reports disagree on {field}")

    expected_iterations = baseline["contract"].get("interaction_iterations")
    if not isinstance(expected_iterations, int):
        raise ValueError("benchmark report has an invalid interaction contract")
    for report in reports:
        for sequence_name in ("zoom", "pan"):
            sequence = report.get(sequence_name, {})
            samples = sequence.get("samples_ms", [])
            if (
                not sequence.get("valid")
                or sequence.get("iterations_completed") != expected_iterations
                or len(samples) != expected_iterations
                or sequence.get("paint_events", 0) < expected_iterations
            ):
                raise ValueError(
                    f"benchmark report has an incomplete {sequence_name} sequence"
                )

    zoom_samples = [
        sample
        for report in reports
        for sample in report["zoom"]["samples_ms"]
    ]
    pan_samples = [
        sample
        for report in reports
        for sample in report["pan"]["samples_ms"]
    ]
    return {
        "process_to_main_window_first_paint_ms": _metric_statistics(
            report["process_to_main_window_first_paint_ms"] for report in reports
        ),
        "main_window_to_first_paint_ms": _metric_statistics(
            report["main_window_to_first_paint_ms"] for report in reports
        ),
        "map_open_to_first_paint_ms": _metric_statistics(
            report["map_open_to_first_paint_ms"] for report in reports
        ),
        "zoom_frame_ms": _metric_statistics(zoom_samples),
        "pan_frame_ms": _metric_statistics(pan_samples),
        "map_art_coverage_ratio": {
            "minimum": min(report["map_art_coverage_ratio"] for report in reports),
            "maximum": max(report["map_art_coverage_ratio"] for report in reports),
        },
    }


def _write_json_atomic(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        dir=path.parent,
        prefix=f".{path.name}.",
        suffix=".tmp",
        delete=False,
    ) as temporary_file:
        json.dump(payload, temporary_file, ensure_ascii=False, indent=2)
        temporary_file.write("\n")
        temporary_path = Path(temporary_file.name)
    os.replace(temporary_path, path)


def _run_once(
    editor: Path,
    map_path: Path,
    assets_path: Path,
    iterations: int,
    timeout_seconds: int,
    report_path: Path,
) -> dict[str, Any]:
    environment = os.environ.copy()
    for variable_name in BENCHMARK_ENVIRONMENT_VARIABLES:
        environment.pop(variable_name, None)
    environment.update(
        {
            "JXQY_EDITOR_PERFORMANCE_MAP": str(map_path),
            "JXQY_EDITOR_PERFORMANCE_ASSETS": str(assets_path),
            "JXQY_EDITOR_PERFORMANCE_REPORT": str(report_path),
            "JXQY_EDITOR_PERFORMANCE_ITERATIONS": str(iterations),
        }
    )

    creation_flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    completed = subprocess.run(
        [str(editor)],
        cwd=editor.parent,
        env=environment,
        capture_output=True,
        timeout=timeout_seconds,
        check=False,
        creationflags=creation_flags,
    )
    if not report_path.is_file():
        stderr = completed.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(
            f"benchmark did not write a report (exit={completed.returncode}): {stderr}"
        )

    report = json.loads(report_path.read_text(encoding="utf-8"))
    if completed.returncode != 0 or not report.get("valid"):
        raise RuntimeError(
            "benchmark failed at "
            f"{report.get('error_stage', 'unknown')}: {report.get('error', 'unknown error')}"
        )
    return report


def _positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor", type=Path, default=DEFAULT_EDITOR)
    parser.add_argument("--map", dest="map_path", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--runs", type=_positive_int, default=5)
    parser.add_argument("--iterations", type=_positive_int, default=16)
    parser.add_argument("--timeout-seconds", type=_positive_int, default=180)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    editor = arguments.editor.resolve()
    map_path = arguments.map_path.resolve()
    assets_path = arguments.assets.resolve()

    if not editor.is_file():
        raise FileNotFoundError(f"editor executable does not exist: {editor}")
    if not map_path.is_file():
        raise FileNotFoundError(f"map does not exist: {map_path}")
    if not assets_path.is_dir():
        raise FileNotFoundError(f"assets directory does not exist: {assets_path}")
    if arguments.runs > 20:
        raise ValueError("runs must not exceed 20")
    if arguments.iterations < 4 or arguments.iterations > 100:
        raise ValueError("iterations must be between 4 and 100")

    reports: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(prefix="jxqy-editor-performance-") as temporary_dir:
        temporary_root = Path(temporary_dir)
        for run_index in range(arguments.runs):
            reports.append(
                _run_once(
                    editor,
                    map_path,
                    assets_path,
                    arguments.iterations,
                    arguments.timeout_seconds,
                    temporary_root / f"run-{run_index + 1}.json",
                )
            )

    summary = {
        "schema_version": 1,
        "valid": True,
        "run_count": len(reports),
        "editor": str(editor),
        "map_path": str(map_path),
        "assets_path": str(assets_path),
        "contract": reports[0]["contract"],
        "map_size": reports[0]["map_size"],
        "canvas_size": reports[0]["canvas_size"],
        "aggregate": _aggregate_runs(reports),
        "runs": reports,
    }
    if arguments.output:
        _write_json_atomic(arguments.output.resolve(), summary)

    print(json.dumps(summary["aggregate"], ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
