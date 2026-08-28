#!/usr/bin/env python3
"""Focused regression tests for check_mod_resources helpers."""

from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path


def load_module():
    script_path = Path(__file__).resolve().with_name("check_mod_resources.py")
    spec = importlib.util.spec_from_file_location("check_mod_resources_under_test", script_path)
    if spec is None or spec.loader is None:
        raise RuntimeError("failed to load check_mod_resources.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def main() -> int:
    module = load_module()

    with tempfile.TemporaryDirectory() as temp_dir:
        profile_path = Path(temp_dir) / "game_profile.ini"
        profile_path.write_text(
            "[gAmE]\n"
            "iD=OLD\n"
            "NaMe=100% ready\n"
            "[GAME]\n"
            "ID=MOD\n"
            "uSeWaV=YeS\n"
            "[rEsOuRcE]\n"
            "dEpEnDeNcYiD=BASE\n",
            encoding="utf-8",
        )
        profile = module.read_ini(profile_path)
        assert module.get_value(profile, "Game", "Id") == "MOD"
        assert module.get_value(profile, "GAME", "NAME") == "100% ready"
        assert module.get_value(profile, "Resource", "DependencyId") == "BASE"
        assert module.get_boolean_value(profile, "Game", "UseWav", False)

    sound_candidates = module.script_sound_candidates("voice.xnb")
    assert "sound\\voice.xnb" not in sound_candidates
    assert sound_candidates[0] == "sound\\voice.wav"

    music_candidates = module.script_music_candidates("Mc023.xnb", use_wav=False)
    assert "music\\Mc023.xnb" not in music_candidates
    assert "music\\Mc023.wma" in music_candidates

    assert module.object_image_candidates("asf/object/box.asf") == [
        "asf\\object\\box.asf",
        "mpc\\object\\box.asf",
        "mpc\\object\\box.mpc",
    ]
    assert module.goods_image_candidates("goods/potion.asf") == [
        "asf\\goods\\potion.asf",
        "mpc\\goods\\potion.asf",
        "mpc\\goods\\potion.mpc",
    ]
    assert module.magic_effect_image_candidates("mpc/effect/hit.mpc") == [
        "mpc\\effect\\hit.mpc",
        "asf\\effect\\hit.mpc",
        "asf\\effect\\hit.asf",
    ]
    assert module.magic_action_image_candidates("role") == [
        "asf\\character\\role",
        "asf\\character\\role.asf",
        "mpc\\character\\role",
        "mpc\\character\\role.mpc",
    ]
    assert module.magic_special_action_image_candidates("role", npc_ini_indices=(1, 2)) == [
        "asf\\character\\role1.asf",
        "mpc\\character\\role1.asf",
        "mpc\\character\\role1.mpc",
        "asf\\character\\role2.asf",
        "mpc\\character\\role2.asf",
        "mpc\\character\\role2.mpc",
    ]
    assert module.magic_special_action_image_candidates("role2.asf", npc_ini_indices=(1, 2)) == [
        "asf\\character\\role2.asf",
        "mpc\\character\\role2.asf",
        "mpc\\character\\role2.mpc",
    ]
    assert module.magic_special_action_display_name("role") == "role1.asf"
    assert module.magic_image_candidates("icon.png") == [
        "asf\\magic\\icon.png",
        "asf\\magic\\icon.asf",
        "mpc\\magic\\icon.png",
        "mpc\\magic\\icon.mpc",
    ]
    assert module.magic_ini_candidates("child.ini") == ["ini/magic/child.ini"]
    assert module.magic_ini_candidates("child") == ["ini/magic/child"]
    assert module.magic_ini_candidates("ini/magic/child.ini") == [
        "ini/magic/ini/magic/child.ini"
    ]
    assert module.strip_ini_value_comment("base.ini;alternate.ini") == "base.ini;alternate.ini"
    assert module.strip_ini_value_comment("base.ini ; comment") == "base.ini "
    assert module.strip_ini_value_comment("base#variant.ini") == "base#variant.ini"
    assert module.magic_list_reference_names("base.ini:2； alternate.ini：3;无;;") == [
        "base.ini",
        "alternate.ini",
        "无",
    ]
    assert module.magic_list_reference_names("无") == []
    assert module.object_image_candidates("asf/map/tile.mpc") == [
        "asf\\map\\tile.mpc",
        "asf\\map\\tile.asf",
    ]

    assert module.split_dependency_ids("BASE, EXTRA, base, ,THIRD") == [
        "BASE",
        "EXTRA",
        "THIRD",
    ]

    packs = {
        "MOD": module.PackInfo("Pack.MOD", "MOD", Path("/packs/mod"), "game_profile.ini", "LEFT,RIGHT"),
        "LEFT": module.PackInfo("Pack.LEFT", "LEFT", Path("/packs/left"), "game_profile.ini", "SHARED"),
        "RIGHT": module.PackInfo("Pack.RIGHT", "RIGHT", Path("/packs/right"), "game_profile.ini", "SHARED"),
        "SHARED": module.PackInfo("Pack.SHARED", "SHARED", Path("/packs/shared"), "game_profile.ini", ""),
    }
    roots = list(module.iter_resolution_roots(
        packs["MOD"], packs, Path("/packs/common"), ""
    ))
    assert [root for root, _source in roots] == [
        Path("/packs/mod"),
        Path("/packs/left"),
        Path("/packs/shared"),
        Path("/packs/right"),
        Path("/packs/common"),
    ]

    with tempfile.TemporaryDirectory() as temp_dir:
        temp_root = Path(temp_dir)
        active_root = temp_root / "active"
        first_parent_root = temp_root / "parent-a"
        second_parent_root = temp_root / "parent-b"
        for root in (active_root, first_parent_root, second_parent_root):
            (root / "music").mkdir(parents=True)
        (active_root / "music" / "cross.ogg").write_bytes(b"active")
        (active_root / "music" / "CASE.MP3").write_bytes(b"case")
        (active_root / "music" / "other.ogg").write_bytes(b"other")
        (active_root / "music" / "directory.mp3").mkdir()
        (active_root / "music" / "directory.ogg").write_bytes(b"file")
        (first_parent_root / "music" / "cross.mp3").write_bytes(b"parent")
        resolved, exact, candidate, source = module.resolve_any_resource(
            ["music/cross.mp3", "music/cross.ogg"],
            [
                (active_root, "self"),
                (first_parent_root, "dependency"),
            ],
        )
        assert resolved == active_root / "music" / "cross.ogg"
        assert exact
        assert candidate == "music/cross.ogg"
        assert source == "self"

        resolved, exact, candidate, source = module.resolve_any_resource(
            ["music/directory.mp3", "music/directory.ogg"],
            [(active_root, "self")],
        )
        assert resolved == active_root / "music" / "directory.ogg"
        assert exact
        assert candidate == "music/directory.ogg"
        assert source == "self"

        outside_file = temp_root / "outside.ogg"
        outside_file.write_bytes(b"outside")
        linked_file = active_root / "music" / "linked.ogg"
        try:
            linked_file.symlink_to(outside_file)
        except OSError:
            pass
        else:
            resolved, _exact, _candidate, _source = module.resolve_any_resource(
                ["music/linked.ogg"],
                [(active_root, "self")],
            )
            assert resolved is None

        resolved, exact, candidate, source = module.resolve_any_resource(
            ["music/case.mp3", "music/other.ogg"],
            [(active_root, "self")],
        )
        assert resolved == active_root / "music" / "CASE.MP3"
        assert not exact
        assert candidate == "music/case.mp3"
        assert source == "self"

        (first_parent_root / "music" / "parent-order.ogg").write_bytes(b"first")
        (second_parent_root / "music" / "parent-order.mp3").write_bytes(b"second")
        resolved, exact, candidate, source = module.resolve_any_resource(
            ["music/parent-order.mp3", "music/parent-order.ogg"],
            [
                (first_parent_root, "dependency"),
                (second_parent_root, "dependency"),
            ],
        )
        assert resolved == first_parent_root / "music" / "parent-order.ogg"
        assert exact
        assert candidate == "music/parent-order.ogg"
        assert source == "dependency"

    with tempfile.TemporaryDirectory() as temp_dir:
        temp_root = Path(temp_dir)
        manifest_packs = {
            "MOD": module.PackInfo("Pack.MOD", "MOD", temp_root / "mod", "game_profile.ini", "LEFT"),
            "LEFT": module.PackInfo("Pack.LEFT", "LEFT", temp_root / "left", "game_profile.ini", ""),
            "SHARED": module.PackInfo("Pack.SHARED", "SHARED", temp_root / "shared", "game_profile.ini", ""),
        }
        (temp_root / "left").mkdir(parents=True)
        (temp_root / "left" / "game_profile.ini").write_text(
            "[Resource]\nDependencyId=SHARED\n",
            encoding="utf-8",
        )
        manifest_roots = list(module.iter_resolution_roots(
            manifest_packs["MOD"], manifest_packs, None, ""
        ))
        assert [root for root, _source in manifest_roots] == [
            temp_root / "mod",
            temp_root / "left",
            temp_root / "shared",
        ]

    with tempfile.TemporaryDirectory() as temp_dir:
        temp_root = Path(temp_dir)
        path_mod_root = temp_root / "PathMod"
        path_mid_root = temp_root / "PathMid"
        path_base_root = temp_root / "PathBase"
        for root in (path_mod_root, path_mid_root, path_base_root):
            root.mkdir(parents=True)
        (path_mod_root / "game_profile.ini").write_text(
            "[Game]\nId=PATHMOD\nName=Path Mod\n"
            "[Resource]\nDependencyId=PATHMID\n",
            encoding="utf-8",
        )
        (path_mid_root / "game_profile.ini").write_text(
            "[Game]\nId=PATHMID\nName=Path Mid\n"
            "[Resource]\nDependencyId=PATHBASE\n",
            encoding="utf-8",
        )
        (path_base_root / "game_profile.ini").write_text(
            "[Game]\nId=PATHBASE\nName=Path Base\nType=1\n",
            encoding="utf-8",
        )
        path_packs = {
            "PATHMOD": module.PackInfo(
                "Pack.PATHMOD", "PATHMOD", path_mod_root, "game_profile.ini", ""
            ),
            "PATHMID": module.PackInfo(
                "Pack.PATHMID", "PATHMID", path_mid_root, "game_profile.ini", ""
            ),
            "PATHBASE": module.PackInfo(
                "Pack.PATHBASE", "PATHBASE", path_base_root, "game_profile.ini", ""
            ),
        }
        path_profiles = {
            key: module.read_ini(pack.root / "game_profile.ini")
            for key, pack in path_packs.items()
        }
        path_roots = list(module.iter_resolution_roots(
            path_packs["PATHMOD"], path_packs, None, ""
        ))
        assert [root for root, _source in path_roots] == [
            path_mod_root,
            path_mid_root,
            path_base_root,
        ]
        assert not module.check_dependency_graph(path_packs, path_profiles)

        (path_mid_root / "game_profile.ini").write_text(
            "[Game]\nId=PATHMID\nName=Path Mid\n"
            "[Resource]\nDependencyId=PATHBASE\n"
            "[UI]\nProfile=unsupported_path_profile\n",
            encoding="utf-8",
        )
        path_profiles["PATHMID"] = module.read_ini(path_mid_root / "game_profile.ini")
        path_ui_issues = module.check_ui_graph(path_packs, path_profiles)
        assert any("unsupported UI.Profile" in issue.message for issue in path_ui_issues)

        (path_base_root / "game_profile.ini").write_text(
            "[Game]\nId=PATHBASE\nName=Path Base\n"
            "[Resource]\nDependencyId=PATHMID\n",
            encoding="utf-8",
        )
        path_profiles["PATHBASE"] = module.read_ini(path_base_root / "game_profile.ini")
        path_cycle_issues = module.check_dependency_graph(path_packs, path_profiles)
        assert any("cycle" in issue.message for issue in path_cycle_issues)

    assert module.sanitize_save_namespace("潇湘行.1/slot") == "潇湘行_1_slot"
    save_packs = {
        "A": module.PackInfo(
            "Pack.A", "A", Path("/packs/a"), "game_profile.ini", "",
            save_namespace="shared.save",
        ),
        "B": module.PackInfo(
            "Pack.B", "B", Path("/packs/b"), "game_profile.ini", "",
            save_namespace="shared:save",
        ),
    }
    save_issues = module.check_save_namespaces(save_packs, {"A": None, "B": None})
    assert len(save_issues) == 2
    assert "first entry keeps shared.save" in save_issues[0].message
    assert "shared:save--Pack_B" in save_issues[1].message
    assert all(issue.severity == "WARNING" for issue in save_issues)

    with tempfile.TemporaryDirectory() as temp_dir:
        assets_root = Path(temp_dir)
        (assets_root / "resources.ini").write_text(
            "[Collection]\nCommonPath=common\n",
            encoding="utf-8",
        )
        for folder, pack_id in (("A", "DUPLICATE"), ("B", "duplicate")):
            pack_root = assets_root / folder
            pack_root.mkdir()
            (pack_root / "game_profile.ini").write_text(
                f"[Game]\nId={pack_id}\nName={pack_id}\nType=3\n",
                encoding="utf-8",
            )
        duplicate_packs, _common_root, duplicate_issues = module.discover_packs(assets_root)
        assert len(duplicate_packs) == 2
        assert list(duplicate_packs)[0] == "DUPLICATE"
        assert list(duplicate_packs.values())[0].pack_id == "DUPLICATE"
        assert list(duplicate_packs.values())[1].pack_id == "duplicate"
        assert any("duplicate Game.Id" in issue.message for issue in duplicate_issues)
        assert any(issue.severity == "ERROR" for issue in duplicate_issues)

    with tempfile.TemporaryDirectory() as temp_dir:
        assets_root = Path(temp_dir)
        (assets_root / "resources.ini").write_text(
            "[cOlLeCtIoN]\ncOmMoNpAtH=common\n",
            encoding="utf-8",
        )
        pack_root = assets_root / "mod"
        pack_root.mkdir()
        (pack_root / "game_profile.ini").write_text(
            "[gAmE]\niD=MOD\nnAmE=Mod\ntYpE=3\n",
            encoding="utf-8",
        )
        mixed_case_packs, _common_root, mixed_case_issues = module.discover_packs(assets_root)
        assert list(mixed_case_packs) == ["MOD"]
        assert not mixed_case_issues

    with tempfile.TemporaryDirectory() as temp_dir:
        loose_root = Path(temp_dir)
        (loose_root / "script").mkdir()
        loose_packs, _common_root, loose_issues = module.discover_packs(loose_root)
        assert not loose_packs
        assert any("no root-level or direct-child" in issue.message for issue in loose_issues)

    with tempfile.TemporaryDirectory() as temp_dir:
        type3_root = Path(temp_dir)
        (type3_root / "game_profile.ini").write_text(
            "[Game]\nId=CUSTOM\nName=Custom\nType=3\n",
            encoding="utf-8",
        )
        type3_pack = module.PackInfo(
            "root", "CUSTOM", type3_root, "game_profile.ini", ""
        )
        type3_profile, type3_issues = module.check_profile(
            type3_pack,
            {"CUSTOM": type3_pack},
            None,
        )
        assert type3_profile is not None
        assert not any("DependencyId" in issue.message for issue in type3_issues)
        assert not module.check_dependency_graph(
            {"CUSTOM": type3_pack},
            {"CUSTOM": type3_profile},
        )

    with tempfile.TemporaryDirectory() as temp_dir:
        temp_root = Path(temp_dir)
        pack_root = temp_root / "mod"
        magic_root = pack_root / "ini" / "magic"
        right_magic_root = temp_root / "right" / "ini" / "magic"
        magic_root.mkdir(parents=True)
        right_magic_root.mkdir(parents=True)
        (magic_root / "parent.ini").write_text(
            "[Init]\n"
            "AttackFile=base.ini\n"
            "FlyIni=alternate.ini\n"
            "FlyMagic=missing_shadowed.ini\n"
            "FlyMagic=alternate.ini\n"
            "RandMagicFile=missing_shadowed_by_empty.ini\n"
            "RandMagicFile=\n"
            "ExplodeMagicFile=base.ini\n"
            "SecondMagicFile=missing_second.ini\n"
            "ReplaceMagic=base.ini:1;alternate.ini:2;missing_replace.ini\n"
            "[Level2]\n"
            "ExplodeMagicFile=alternate.ini\n"
            "[Level3]\n"
            "ExplodeMagicFile=\n"
            "[Level4]\n"
            "ExplodeMagicFile=missing.ini\n"
            "[Level11]\n"
            "ExplodeMagicFile=ignored_missing.ini\n"
            "[Level02]\n"
            "ExplodeMagicFile=ignored_missing.ini\n"
            "[Level5]\n"
            "SecondMagicFile=missing_level_second.ini\n"
            "[Level6]\n"
            "FlyMagic=missing_level_fly.ini\n"
            "[Level7]\n"
            "AttackFile=missing_level_attack.ini\n"
            "[Level8]\n"
            "RandMagicFile=\n",
            encoding="utf-8",
        )
        (magic_root / "base.ini").write_text("[Init]\nName=BASE\n", encoding="utf-8")
        (right_magic_root / "alternate.ini").write_text(
            "[Init]\nName=ALTERNATE\n", encoding="utf-8"
        )
        magic_packs = {
            "MOD": module.PackInfo(
                "Pack.MOD", "MOD", pack_root, "game_profile.ini", "LEFT,RIGHT"
            ),
            "LEFT": module.PackInfo(
                "Pack.LEFT", "LEFT", temp_root / "left", "game_profile.ini", ""
            ),
            "RIGHT": module.PackInfo(
                "Pack.RIGHT", "RIGHT", temp_root / "right", "game_profile.ini", ""
            ),
        }
        magic_issues = module.check_magic_resource_references(
            magic_packs["MOD"], magic_packs, None, ""
        )
        assert sum("linked magic" in issue.message and "target not found" in issue.message for issue in magic_issues) == 6
        assert any("linked magic ExplodeMagicFile target not found" in issue.message for issue in magic_issues)
        assert any("linked magic SecondMagicFile target not found" in issue.message for issue in magic_issues)
        assert any("missing_level_second.ini" in issue.message for issue in magic_issues)
        assert any("missing_level_fly.ini" in issue.message for issue in magic_issues)
        assert any("missing_level_attack.ini" in issue.message for issue in magic_issues)
        assert any("missing_replace.ini" in issue.message for issue in magic_issues)
        assert any("missing.ini" in issue.message for issue in magic_issues)
        assert not any("missing_shadowed" in issue.message for issue in magic_issues)
        assert all(module.issue_category(issue) == "magic_linked_resource" for issue in magic_issues)
        for issue in magic_issues:
            module.calibrate_issue_severity(issue)
        assert all(issue.severity == "WARNING" for issue in magic_issues)

    with tempfile.TemporaryDirectory() as temp_dir:
        temp_root = Path(temp_dir)
        pack_root = temp_root / "mod"
        dependency_root = temp_root / "base"
        magic_root = pack_root / "ini" / "magic"
        dependency_magic_root = dependency_root / "ini" / "magic"
        magic_root.mkdir(parents=True)
        dependency_magic_root.mkdir(parents=True)
        (magic_root / "leaf.ini").write_text("[Init]\nName=LEAF\n", encoding="utf-8")
        (magic_root / "self.ini").write_text(
            "[Init]\n"
            "RandMagicFile=self.ini\n"
            "SecondMagicFile=dependency_child.ini\n",
            encoding="utf-8",
        )
        (magic_root / "mutual_a.ini").write_text(
            "[Init]\nRandMagicFile=mutual_b.ini\n",
            encoding="utf-8",
        )
        (magic_root / "mutual_b.ini").write_text(
            "[Init]\nSecondMagicFile=mutual_a.ini\nFlyMagic=leaf.ini\n",
            encoding="utf-8",
        )
        (dependency_magic_root / "dependency_child.ini").write_text(
            "[Init]\nSecondMagicFile=dependency_grandchild.ini\n",
            encoding="utf-8",
        )
        (dependency_magic_root / "dependency_grandchild.ini").write_text(
            "[Init]\nName=DEPENDENCY_GRANDCHILD\n",
            encoding="utf-8",
        )
        for index in range(module.MAGIC_LINKED_LOAD_MAX_DEPTH + 2):
            child_line = ""
            if index <= module.MAGIC_LINKED_LOAD_MAX_DEPTH:
                child_line = f"RandMagicFile=depth_{index + 1}.ini\n"
            (magic_root / f"depth_{index}.ini").write_text(
                "[Init]\n" + child_line,
                encoding="utf-8",
            )
        for index in range(1, module.MAGIC_LINKED_LOAD_MAX_NODES + 1):
            content = "[Init]\n"
            left = index * 2
            right = left + 1
            if left < module.MAGIC_LINKED_LOAD_MAX_NODES:
                content += f"RandMagicFile=budget_{left}.ini\n"
            if right < module.MAGIC_LINKED_LOAD_MAX_NODES:
                content += f"SecondMagicFile=budget_{right}.ini\n"
            (magic_root / f"budget_{index}.ini").write_text(content, encoding="utf-8")
        (magic_root / "budget_root.ini").write_text(
            "[Init]\n"
            "RandMagicFile=budget_1.ini\n"
            f"SecondMagicFile=budget_{module.MAGIC_LINKED_LOAD_MAX_NODES}.ini\n",
            encoding="utf-8",
        )
        graph_packs = {
            "MOD": module.PackInfo(
                "Pack.MOD", "MOD", pack_root, "game_profile.ini", "BASE"
            ),
            "BASE": module.PackInfo(
                "Pack.BASE", "BASE", dependency_root, "game_profile.ini", ""
            ),
        }
        graph_issues = module.check_magic_resource_references(
            graph_packs["MOD"], graph_packs, None, ""
        )
        assert sum("linked magic load cycle" in issue.message for issue in graph_issues) == 2
        assert sum("linked magic load depth limit" in issue.message for issue in graph_issues) >= 1
        assert sum("linked magic load node limit" in issue.message for issue in graph_issues) == 1
        assert not any("target not found" in issue.message for issue in graph_issues)
        assert not any("dependency_child" in issue.message for issue in graph_issues)
        assert all(
            module.issue_category(issue) == "magic_linked_resource"
            for issue in graph_issues
        )

    with tempfile.TemporaryDirectory() as temp_dir:
        temp_root = Path(temp_dir)
        pack_root = temp_root / "mod"
        dependency_root = temp_root / "base"
        goods_root = pack_root / "ini" / "goods"
        dependency_magic_root = dependency_root / "ini" / "magic"
        goods_root.mkdir(parents=True)
        dependency_magic_root.mkdir(parents=True)
        (goods_root / "projectile.ini").write_text(
            "[Init]\n"
            "MagicName=existing.ini\n",
            encoding="utf-8",
        )
        (goods_root / "broken.ini").write_text(
            "[Init]\n"
            "MagicName=missing.ini\n",
            encoding="utf-8",
        )
        (dependency_magic_root / "existing.ini").write_text(
            "[Init]\nName=EXISTING\n",
            encoding="utf-8",
        )
        goods_packs = {
            "MOD": module.PackInfo(
                "Pack.MOD", "MOD", pack_root, "game_profile.ini", "BASE"
            ),
            "BASE": module.PackInfo(
                "Pack.BASE", "BASE", dependency_root, "game_profile.ini", ""
            ),
        }
        goods_issues = module.check_goods_resource_references(
            goods_packs["MOD"], goods_packs, None, ""
        )
        linked_magic_issues = [
            issue for issue in goods_issues if "goods linked magic" in issue.message
        ]
        assert len(linked_magic_issues) == 1
        assert "missing.ini" in linked_magic_issues[0].message
        assert module.issue_category(linked_magic_issues[0]) == "goods_magic_reference"
        module.calibrate_issue_severity(linked_magic_issues[0])
        assert linked_magic_issues[0].severity == "WARNING"

    with tempfile.TemporaryDirectory() as temp_dir:
        temp_root = Path(temp_dir)
        active_root = temp_root / "active"
        content_root = temp_root / "content"
        ui_base_root = temp_root / "ui-base"
        ui_parent_root = temp_root / "ui-parent"
        common_root = temp_root / "common"
        for root in (active_root, content_root, ui_base_root, ui_parent_root, common_root):
            root.mkdir(parents=True)

        (active_root / "game_profile.ini").write_text(
            "[Resource]\nDependencyId=CONTENT\n"
            "[UI]\nBaseId=UIBASE\nPreferLocal=1\n",
            encoding="utf-8",
        )
        (content_root / "game_profile.ini").write_text("[Game]\nType=1\n", encoding="utf-8")
        (ui_base_root / "game_profile.ini").write_text(
            "[Resource]\nDependencyId=UIPARENT\n",
            encoding="utf-8",
        )
        (ui_parent_root / "game_profile.ini").write_text("[Game]\nType=1\n", encoding="utf-8")

        ui_packs = {
            "ACTIVE": module.PackInfo(
                "Pack.ACTIVE", "ACTIVE", active_root, "game_profile.ini", "CONTENT", "UIBASE", "YYCS"
            ),
            "CONTENT": module.PackInfo(
                "Pack.CONTENT", "CONTENT", content_root, "game_profile.ini", ""
            ),
            "UIBASE": module.PackInfo(
                "Pack.UIBASE", "UIBASE", ui_base_root, "game_profile.ini", "UIPARENT"
            ),
            "UIPARENT": module.PackInfo(
                "Pack.UIPARENT", "UIPARENT", ui_parent_root, "game_profile.ini", ""
            ),
        }
        ui_profiles = {
            pack_id: module.read_ini(pack.root / pack.manifest)
            for pack_id, pack in ui_packs.items()
        }
        ui_roots = list(module.iter_ui_resolution_roots(
            ui_packs["ACTIVE"], ui_packs, ui_profiles, common_root
        ))
        assert [root for root, _source in ui_roots] == [
            active_root,
            ui_base_root,
            ui_parent_root,
            common_root,
        ]

        (active_root / "game_profile.ini").write_text(
            "[Resource]\nDependencyId=CONTENT\n"
            "[UI]\nBaseId=UIBASE\nPreferLocal=0\n",
            encoding="utf-8",
        )
        ui_profiles["ACTIVE"] = module.read_ini(active_root / "game_profile.ini")
        ui_roots = list(module.iter_ui_resolution_roots(
            ui_packs["ACTIVE"], ui_packs, ui_profiles, common_root
        ))
        assert [root for root, _source in ui_roots] == [
            ui_base_root,
            ui_parent_root,
            active_root,
            common_root,
        ]

        (active_root / "game_profile.ini").write_text(
            "[Resource]\nDependencyId=CONTENT\n"
            "[UI]\nBaseId=UIBASE\nPreferLocal=1\n",
            encoding="utf-8",
        )
        ui_profiles["ACTIVE"] = module.read_ini(active_root / "game_profile.ini")
        active_ui_root = active_root / "ini" / "ui" / "option"
        active_ui_root.mkdir(parents=True)
        (active_ui_root / "present.ini").write_text(
            "[Init]\nImage=\\asf\\ui\\option\\Present.asf\n",
            encoding="utf-8",
        )
        (active_ui_root / "legacy.ini").write_text(
            "[Init]\nImage=\\asf\\interface\\system\\title.asf\n",
            encoding="utf-8",
        )
        (active_ui_root / "missing-a.ini").write_text(
            "[Init]\nImage=\\asf\\ui\\option\\missing.asf\n",
            encoding="utf-8",
        )
        (active_ui_root / "missing-b.ini").write_text(
            "[Init]\nImage=\\asf\\ui\\option\\missing.asf\n",
            encoding="utf-8",
        )
        (active_ui_root / "content-only.ini").write_text(
            "[Init]\nImage=\\asf\\ui\\option\\content-only.asf\n",
            encoding="utf-8",
        )
        (ui_base_root / "asf" / "ui" / "option").mkdir(parents=True)
        (ui_base_root / "asf" / "ui" / "option" / "Present.asf").write_bytes(b"ui")
        (content_root / "asf" / "interface" / "system").mkdir(parents=True)
        (content_root / "asf" / "interface" / "system" / "title.asf").write_bytes(b"legacy")
        (content_root / "asf" / "ui" / "option").mkdir(parents=True)
        (content_root / "asf" / "ui" / "option" / "content-only.asf").write_bytes(b"wrong-domain")

        ui_image_issues = module.check_ui_component_image_resources(
            ui_packs["ACTIVE"], ui_packs, ui_profiles, common_root
        )
        assert len(ui_image_issues) == 2
        assert any(
            "missing.asf (2 references)" in issue.message
            for issue in ui_image_issues
        )
        assert any(
            "content-only.asf" in issue.message
            for issue in ui_image_issues
        )
        assert all(module.issue_category(issue) == "ui_component_image" for issue in ui_image_issues)
        for issue in ui_image_issues:
            module.calibrate_issue_severity(issue)
            assert issue.severity == "WARNING"

    cycle_packs = {
        "A": module.PackInfo("Pack.A", "A", Path("/packs/a"), "game_profile.ini", "BASE,B"),
        "B": module.PackInfo("Pack.B", "B", Path("/packs/b"), "game_profile.ini", "A"),
        "BASE": module.PackInfo("Pack.BASE", "BASE", Path("/packs/base"), "game_profile.ini", ""),
    }
    cycle_issues = module.check_dependency_graph(cycle_packs, {key: None for key in cycle_packs})
    assert any("cycle" in issue.message for issue in cycle_issues)

    invalid_ui_packs = {
        "BASE": module.PackInfo("Pack.BASE", "BASE", Path("/packs/base"), "game_profile.ini", ""),
        "MOD": module.PackInfo(
            "Pack.MOD", "MOD", Path("/packs/mod"), "game_profile.ini", "BASE", "NO_UI", ""
        ),
        "BADPROFILE": module.PackInfo(
            "Pack.BADPROFILE",
            "BADPROFILE",
            Path("/packs/badprofile"),
            "game_profile.ini",
            "BASE",
            "",
            "UNKNOWN",
        ),
        "UPSTREAM": module.PackInfo(
            "Pack.UPSTREAM",
            "UPSTREAM",
            Path("/packs/upstream"),
            "game_profile.ini",
            "TOP",
            "BASE",
            "",
        ),
        "TOP": module.PackInfo(
            "Pack.TOP",
            "TOP",
            Path("/packs/top"),
            "game_profile.ini",
            "BADPROFILE",
            "BASE",
            "",
        ),
    }
    invalid_ui_issues = module.check_ui_graph(invalid_ui_packs, {key: None for key in invalid_ui_packs})
    assert any("UI base pack not found" in issue.message for issue in invalid_ui_issues)
    assert any("unsupported UI.Profile" in issue.message for issue in invalid_ui_issues)
    assert not any("runtime disables pack" in issue.message for issue in invalid_ui_issues)
    assert all(issue.severity == "WARNING" for issue in invalid_ui_issues)

    with tempfile.TemporaryDirectory() as temp_dir:
        assets_root = Path(temp_dir)
        pack_root = assets_root / "mod"
        (pack_root / "ini" / "objres").mkdir(parents=True)
        (assets_root / "resources.ini").write_text(
            "[Collection]\nCommonPath=common\n",
            encoding="utf-8",
        )
        (pack_root / "game_profile.ini").write_text(
            "[Game]\n"
            "Id=MOD\n"
            "Name=MOD\n"
            "Type=3\n",
            encoding="utf-8",
        )
        (pack_root / "ini" / "objres" / "broken.ini").write_text(
            "[Bad]\n"
            "Image=missing.mpc\n",
            encoding="utf-8",
        )
        script_path = Path(module.__file__).resolve()

        def run_checker(*arguments: str) -> subprocess.CompletedProcess[str]:
            return subprocess.run(
                [
                    sys.executable,
                    str(script_path),
                    str(assets_root),
                    *arguments,
                ],
                text=True,
                encoding="utf-8",
                errors="replace",
                capture_output=True,
                timeout=30,
            )

        default_result = run_checker()
        assert default_result.returncode == 0
        assert "Errors: 0" not in default_result.stdout

        strict_result = run_checker("--strict")
        assert strict_result.returncode == 1

        selected_gate_result = run_checker(
            "--fail-on-categories",
            "object_resource_structure",
            "--fail-on-severities",
            "ERROR",
        )
        assert selected_gate_result.returncode == 1

        unrelated_gate_result = run_checker(
            "--fail-on-categories",
            "dependency",
            "--fail-on-severities",
            "ERROR",
        )
        assert unrelated_gate_result.returncode == 0

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
