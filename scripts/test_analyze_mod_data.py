#!/usr/bin/env python3
"""Focused regression tests for analyze_mod_data classification helpers."""

from __future__ import annotations

import tempfile
from argparse import Namespace
from pathlib import Path

from analyze_mod_data import build_report, classify_data_file


def assert_equal(actual: object, expected: object, label: str) -> None:
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def test_ini_npc_classification_boundaries() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        npc_dir = root / "pack" / "ini" / "npc"

        player_snapshot = npc_dir / "player_snapshot.ini"
        write_text(
            player_snapshot,
            "\n".join(
                [
                    "[Init]",
                    "Name=Snapshot",
                    "Kind=2",
                    "Relation=0",
                    "PathFinder=1",
                    "Action=0",
                    "NpcIni=body.ini",
                    "Doing=1",
                    "DesX=12",
                    "DesY=13",
                    "CanRun=1",
                    "CanJump=1",
                    "Fight=0",
                    "Money=99",
                    "TimeScript=script\\common\\timer.txt",
                    "SecondAttack=0",
                    "",
                ]
            ),
        )

        npc_entity = npc_dir / "regular_npc.ini"
        write_text(
            npc_entity,
            "\n".join(
                [
                    "[Init]",
                    "Name=Regular NPC",
                    "Kind=1",
                    "Relation=2",
                    "PathFinder=1",
                    "Action=0",
                    "NpcIni=npcres001.ini",
                    "BodyIni=npcres001.ini",
                    "",
                ]
            ),
        )

        misplaced_npc_res = npc_dir / "misplaced_npcres.ini"
        write_text(
            misplaced_npc_res,
            "\n".join(
                [
                    "[Stand]",
                    "Image=npc001_st.asf",
                    "Sound=",
                    "",
                ]
            ),
        )

        partial_snapshot = npc_dir / "partial_snapshot_like_npc.ini"
        write_text(
            partial_snapshot,
            "\n".join(
                [
                    "[Init]",
                    "Name=Partial",
                    "Kind=1",
                    "Doing=1",
                    "DesX=1",
                    "CanRun=1",
                    "",
                ]
            ),
        )

        assert_equal(classify_data_file(player_snapshot), "player_snapshot", "player snapshot category")
        assert_equal(classify_data_file(npc_entity), "npc", "regular NPC category")
        assert_equal(classify_data_file(misplaced_npc_res), "npc_res", "misplaced NPC resource category")
        assert_equal(classify_data_file(partial_snapshot), "npc", "partial snapshot threshold category")


def test_player_snapshot_report_is_separate_from_npc() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        npc_dir = root / "pack" / "ini" / "npc"

        write_text(
            npc_dir / "player_snapshot.ini",
            "\n".join(
                [
                    "[Init]",
                    "Name=Snapshot",
                    "Kind=2",
                    "Relation=0",
                    "PathFinder=1",
                    "Action=0",
                    "NpcIni=body.ini",
                    "Doing=1",
                    "DesX=12",
                    "DesY=13",
                    "CanRun=1",
                    "CanJump=1",
                    "Fight=0",
                    "Money=99",
                    "TimeScript=script\\common\\timer.txt",
                    "SecondAttack=0",
                    "",
                ]
            ),
        )
        write_text(
            npc_dir / "regular_npc.ini",
            "\n".join(
                [
                    "[Init]",
                    "Name=Regular NPC",
                    "Kind=1",
                    "Relation=2",
                    "PathFinder=1",
                    "Action=0",
                    "NpcIni=npcres001.ini",
                    "BodyIni=npcres001.ini",
                    "",
                ]
            ),
        )
        write_text(
            npc_dir / "misplaced_npcres.ini",
            "\n".join(
                [
                    "[Stand]",
                    "Image=npc001_st.asf",
                    "Sound=",
                    "",
                ]
            ),
        )

        report = build_report(
            Namespace(
                paths=[str(root)],
                repo_root=str(root),
                max_examples=5,
                top=10,
            )
        )
        categories = report["categories"]

        assert_equal(categories["npc"]["fileCount"], 1, "NPC file count")
        assert_equal(categories["player_snapshot"]["fileCount"], 1, "player snapshot file count")
        assert_equal(categories["npc_res"]["fileCount"], 1, "NPC resource file count")
        assert_equal(categories["npc"]["unknownFields"], [], "NPC unknown fields")
        assert_equal(categories["player_snapshot"]["unknownFields"], [], "player snapshot unknown fields")
        assert_equal(categories["player_snapshot"]["unsupportedEnumValues"], [], "player snapshot enum values")


def test_object_animation_fields_are_supported() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        write_text(
            root / "pack" / "ini" / "obj" / "drop.ini",
            "\n".join(
                [
                    "[Init]",
                    "ObjName=Drop",
                    "ObjFile=drop-resource.ini",
                    "ObjFileMovie=legacy-animation-resource.ini",
                    "Kind=8",
                    "Type=2",
                    "",
                ]
            ),
        )
        write_text(
            root / "pack" / "ini" / "objres" / "drop-resource.ini",
            "\n".join(
                [
                    "[Common]",
                    "Image=drop-static.asf",
                    "Animation=drop-animation.asf",
                    "",
                ]
            ),
        )

        report = build_report(
            Namespace(
                paths=[str(root)],
                repo_root=str(root),
                max_examples=5,
                top=10,
            )
        )
        categories = report["categories"]
        assert_equal(categories["object"]["unknownFields"], [], "object animation unknown fields")
        assert_equal(
            [item["key"] for item in categories["object"]["deferredFields"]],
            ["entity.type"],
            "Object Type remains the only deferred object metadata field",
        )
        assert_equal(categories["object_res"]["unknownFields"], [], "objres Animation support")


def test_magic_change_attributes_special_kind_is_supported() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        write_text(
            root / "pack" / "ini" / "magic" / "change_attributes.ini",
            "[Init]\nSpecialKind=99\n",
        )

        report = build_report(
            Namespace(
                paths=[str(root)],
                repo_root=str(root),
                max_examples=5,
                top=10,
            )
        )
        magic = report["categories"]["magic"]
        assert_equal(magic["unsupportedEnumValues"], [], "SpecialKind=99 support")
        assert_equal(
            report["supportedFields"]["magic"]["specialKind"],
            [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 99],
            "reported SpecialKind support",
        )


def main() -> int:
    test_ini_npc_classification_boundaries()
    test_player_snapshot_report_is_separate_from_npc()
    test_object_animation_fields_are_supported()
    test_magic_change_attributes_special_kind_is_supported()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
