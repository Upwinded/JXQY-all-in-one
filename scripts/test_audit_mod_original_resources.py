#!/usr/bin/env python3
"""Focused regression tests for audit_mod_original_resources helpers."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path


def load_module():
    script_path = Path(__file__).resolve().with_name("audit_mod_original_resources.py")
    spec = importlib.util.spec_from_file_location("audit_mod_original_resources_under_test", script_path)
    if spec is None or spec.loader is None:
        raise RuntimeError("failed to load audit_mod_original_resources.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def main() -> int:
    module = load_module()
    resource_module = module.load_check_mod_resources_module()

    message = "script media target not found: playsound(open.wav) (3 references)"
    function_name, file_name = module.parse_script_media_target(message)
    assert function_name == "playsound"
    assert file_name == "open.wav"

    target, candidates = module.candidates_for_issue(resource_module, "script_media", message, "")
    assert target == "playsound(open.wav)"
    assert candidates[0] == "sound\\open.wav"
    assert candidates == ["sound\\open.wav", "sound\\OPEN.wav", "sound\\Open.wav"]

    signal_target, signal_candidates = module.candidates_for_issue(
        resource_module,
        "signal_tip",
        "signal tip target not found: ignored",
        "",
    )
    assert signal_target == "ini\\ui\\tips\\SignalFile.ini"
    assert signal_candidates == ["ini\\ui\\tips\\SignalFile.ini"]

    ui_target, ui_candidates = module.candidates_for_issue(
        resource_module,
        "ui_component_image",
        "UI component Image target not found: asf\\ui\\option\\checkbox.asf",
        "asf\\ui\\option\\checkbox.asf",
    )
    assert ui_target == "asf\\ui\\option\\checkbox.asf"
    assert ui_candidates == ["asf\\ui\\option\\checkbox.asf"]

    source_index = module.SourceIndex(
        pack_id="XJXQY",
        description="test source",
        relative_paths={module.normalized_relative_key("sound/open.wav")},
    )
    status, hits = module.classify_source_match(source_index, candidates, "")
    assert status == "original_exact"
    assert hits == candidates

    empty_index = module.SourceIndex(pack_id="XJXQY", description="empty source", relative_paths=set())
    status, hits = module.classify_source_match(empty_index, candidates, "sound\\open.wav")
    assert status == "runtime_alias_only"
    assert hits == []

    status, hits = module.classify_source_match(None, candidates, "")
    assert status == "no_source_mapping"
    assert hits == []

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
