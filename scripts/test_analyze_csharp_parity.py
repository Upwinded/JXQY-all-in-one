#!/usr/bin/env python3
"""Focused regression tests for analyze_csharp_parity parsers."""

from __future__ import annotations

import tempfile
from pathlib import Path

import analyze_csharp_parity as parity


def main() -> int:
    original_repo_root = parity.REPO_ROOT
    try:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            source_path = repo_root / "src" / "Fixture.cpp"
            source_path.parent.mkdir(parents=True)
            source_path.write_text(
                """
                void load(INIReader* ini, const std::string& section)
                {
                    value = readPositiveTime(
                        ini,
                        section,
                        "MultilineMilliseconds",
                        0);
                }

                //#define regFunc(func) commented_out_registration(func)
                #define regFunc(func) register_lua_function(func)
                #define regAlias(name, func) register_lua_alias(name, func)
                regFunc(RealCall);
                regFunc(func);
                regAlias("HistoricalCall", RealCall);
                """,
                encoding="utf-8",
            )
            parity.REPO_ROOT = repo_root

            keys = parity.extract_cpp_ini_keys(("src/Fixture.cpp",))
            assert [item["name"] for item in keys] == ["MultilineMilliseconds"]

            registrations = parity.extract_cpp_script_registrations(("src/Fixture.cpp",))
            assert [(item["name"], item["alias_target"]) for item in registrations] == [
                ("RealCall", ""),
                ("func", ""),
                ("HistoricalCall", "RealCall"),
            ]
    finally:
        parity.REPO_ROOT = original_repo_root
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
