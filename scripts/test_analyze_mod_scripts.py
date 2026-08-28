#!/usr/bin/env python3
"""Regression tests for analyze_mod_scripts static call scanning."""

from __future__ import annotations

import tempfile
from collections import Counter, defaultdict
from pathlib import Path

from analyze_mod_scripts import collect_cpp_registered_apis, scan_script_file, strip_lua_comments


def assert_equal(actual: object, expected: object, label: str) -> None:
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def test_strip_lua_non_code_segments() -> None:
    line, state = strip_lua_comments('DisplayMessage("LoadNpc(1) -- text") -- AddNpc()', False)
    assert_equal(line, "DisplayMessage( ) ", "strip short string and line comment")
    assert_equal(state, False, "line comment state")

    line, state = strip_lua_comments("--[=[ AddNpc() ]=] DisplayMessage()", False)
    assert_equal(line, "  DisplayMessage()", "strip same-line long comment")
    assert_equal(state, False, "same-line long comment state")

    line, state = strip_lua_comments("message = [=[ LoadNpc()", False)
    assert_equal(line, "message =  ", "strip long string start")
    assert_equal(state, "string:]=]", "long string state")

    line, state = strip_lua_comments("still text DelNpc() ]=] RealCall()", state)
    assert_equal(line, " RealCall()", "resume after long string end")
    assert_equal(state, False, "long string closed state")


def test_scan_ignores_strings_comments_and_local_functions() -> None:
    script = "\n".join(
        [
            'DisplayMessage("LoadNpc(1)")',
            "local function LocalOnly()",
            "end",
            "-- AddNpc()",
            "--[=[ DelNpc() ]=]",
            "payload = [[ SetNpcPos() ]]",
            "RealCall()",
            "object:MethodCall()",
            "namespace.FunctionCall()",
            "",
        ]
    )

    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        path = root / "script" / "common" / "case.txt"
        path.parent.mkdir(parents=True)
        path.write_text(script, encoding="utf-8")

        calls: Counter[str] = Counter()
        locations: dict[str, list[object]] = defaultdict(list)
        scan_script_file(path, root, 5, calls, locations)

    assert_equal(calls, Counter({"displaymessage": 1, "realcall": 1}), "scanned calls")


def test_cpp_registration_parser_ignores_macro_parameter() -> None:
    apis = collect_cpp_registered_apis(
        """
        //#define regFunc(func) commented_out_registration(func)
        #define regFunc(func) register_lua_function(func)
        regFunc(RealCall);
        regFunc(func);
        regAlias("HistoricalCall", RealCall);
        """
    )
    assert_equal(apis, {"realcall", "func", "historicalcall"}, "registered API names")


def main() -> int:
    test_strip_lua_non_code_segments()
    test_scan_ignores_strings_comments_and_local_functions()
    test_cpp_registration_parser_ignores_macro_parameter()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
