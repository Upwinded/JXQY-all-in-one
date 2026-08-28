#!/usr/bin/env python3
from __future__ import annotations

import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path

from run_mod_scenario_smoke import (
    SavedGoodsSlotsExpectation,
    Scenario,
    check_saved_goods_slots,
    copy_directory_contents,
    default_executable,
    ensure_initial_save_seed,
    exclusive_run_lock,
    find_initial_save_template,
    load_scenarios,
    main,
    parse_saved_goods_slots,
    preserve_resource_save_state,
    resolve_contained_path,
    resource_pack_path,
    run_scenario,
    scenario_log_path,
    select_scenarios,
)


class SavedGoodsSlotsTests(unittest.TestCase):
    def test_parse_saved_goods_slots_supports_multiple_expectations(self) -> None:
        expectations = parse_saved_goods_slots(
            "9|mod_test_goods_lifecycle_stack.ini|2|1;"
            "8|mod_test_goods_pricing_noneed.ini|1|1"
        )

        self.assertEqual(
            expectations,
            (
                SavedGoodsSlotsExpectation(9, "mod_test_goods_lifecycle_stack.ini", 2, 1),
                SavedGoodsSlotsExpectation(8, "mod_test_goods_pricing_noneed.ini", 1, 1),
            ),
        )

    def test_check_saved_goods_slots_uses_discovered_pack_path(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            assets_root = Path(temp_dir)
            write_text(
                assets_root / "resources.ini",
                "[Collection]\nCommonPath=common\n",
            )
            write_text(
                assets_root / "custom_test_pack" / "game_profile.ini",
                "[Game]\nId=XJXQY_TEST_MOD\n",
            )
            write_text(
                assets_root / "custom_test_pack" / "save" / "game" / "goods9.ini",
                "\n".join(
                    [
                        "[Head]",
                        "Count=2",
                        "[1]",
                        "IniFile=mod_test_goods_lifecycle_stack.ini",
                        "Number=1",
                        "[2]",
                        "IniFile=MOD_TEST_GOODS_LIFECYCLE_STACK.INI",
                        "Number=1",
                        "",
                    ]
                ),
            )

            self.assertTrue(
                check_saved_goods_slots(
                    assets_root,
                    "XJXQY_TEST_MOD",
                    (
                        SavedGoodsSlotsExpectation(
                            9,
                            "mod_test_goods_lifecycle_stack.ini",
                            2,
                            1,
                        ),
                    ),
                )
            )

    def test_check_saved_goods_slots_rejects_wrong_slot_number(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            assets_root = Path(temp_dir)
            write_text(
                assets_root / "xjxqy_test_mod" / "game_profile.ini",
                "[Game]\nId=XJXQY_TEST_MOD\n",
            )
            write_text(
                assets_root / "xjxqy_test_mod" / "save" / "game" / "goods9.ini",
                "\n".join(
                    [
                        "[Head]",
                        "Count=1",
                        "[1]",
                        "IniFile=mod_test_goods_lifecycle_stack.ini",
                        "Number=2",
                        "",
                    ]
                ),
            )

            stderr = StringIO()
            with redirect_stderr(stderr):
                self.assertFalse(
                    check_saved_goods_slots(
                        assets_root,
                        "XJXQY_TEST_MOD",
                        (
                            SavedGoodsSlotsExpectation(
                                9,
                                "mod_test_goods_lifecycle_stack.ini",
                                1,
                                1,
                            ),
                        ),
                    )
                )
            self.assertIn("expected 1 slots", stderr.getvalue())


class InitialSaveSeedTests(unittest.TestCase):
    def test_manifest_dependency_is_authoritative_for_template(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            assets_root = Path(temp_dir)
            write_text(
                assets_root / "resources.ini",
                "[Collection]\nCommonPath=common\n",
            )
            write_text(
                assets_root / "mod" / "game_profile.ini",
                "[Game]\nId=MOD\n[Resource]\nDependencyId=LEFT\n",
            )
            write_text(assets_root / "left" / "game_profile.ini", "[Game]\nId=LEFT\n")
            write_text(assets_root / "right" / "game_profile.ini", "[Game]\nId=RIGHT\n")
            write_text(assets_root / "left" / "ini" / "save" / "game.ini", "left\n")
            right_template = assets_root / "right" / "ini" / "save"
            write_text(right_template / "game.ini", "right\n")

            self.assertEqual(
                find_initial_save_template(assets_root, "MOD"),
                assets_root / "left" / "ini" / "save",
            )

    def test_ensure_initial_save_seed_copies_dependency_template(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            assets_root = Path(temp_dir)
            write_text(
                assets_root / "resources.ini",
                "[Collection]\nCommonPath=common\n",
            )
            write_text(
                assets_root / "test_mod" / "game_profile.ini",
                "\n".join(
                    [
                        "[Game]",
                        "Id=XJXQY_TEST_MOD",
                        "[Resource]",
                        "DependencyId=XJXQY",
                        "",
                    ]
                ),
            )
            write_text(
                assets_root / "xjxqy" / "game_profile.ini",
                "[Game]\nId=XJXQY\n",
            )
            template_dir = assets_root / "xjxqy" / "ini" / "save"
            write_text(template_dir / "game.ini", "[Global]\nMap=map001\n")
            write_text(template_dir / "npc" / "npc001.ini", "[Init]\nName=NPC\n")

            self.assertEqual(
                find_initial_save_template(assets_root, "XJXQY_TEST_MOD"),
                template_dir,
            )

            ensure_initial_save_seed(assets_root, "XJXQY_TEST_MOD")

            seed_dir = assets_root / "test_mod" / "save" / "rpg0"
            self.assertEqual((seed_dir / "game.ini").read_text(encoding="utf-8"), "[Global]\nMap=map001\n")
            self.assertEqual((seed_dir / "npc" / "npc001.ini").read_text(encoding="utf-8"), "[Init]\nName=NPC\n")

    def test_direct_child_game_profile_controls_dependency(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            assets_root = Path(temp_dir)
            write_text(
                assets_root / "resources.ini",
                "[Collection]\nCommonPath=common\n",
            )
            write_text(
                assets_root / "mod" / "game_profile.ini",
                "[Game]\nId=MOD\n[Resource]\nDependencyId=RIGHT\n",
            )
            write_text(
                assets_root / "right" / "game_profile.ini",
                "[Game]\nId=RIGHT\nName=Right\n",
            )
            right_template = assets_root / "right" / "ini" / "save"
            write_text(right_template / "game.ini", "right\n")

            self.assertEqual(find_initial_save_template(assets_root, "MOD"), right_template)

    def test_ensure_initial_save_seed_keeps_existing_seed(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            assets_root = Path(temp_dir)
            write_text(
                assets_root / "xjxqy_test_mod" / "game_profile.ini",
                "\n".join(
                    [
                        "[Game]",
                        "Id=XJXQY_TEST_MOD",
                        "[Resource]",
                        "DependencyId=XJXQY",
                        "",
                    ]
                ),
            )
            write_text(
                assets_root / "xjxqy" / "game_profile.ini",
                "[Game]\nId=XJXQY\n",
            )
            write_text(assets_root / "xjxqy" / "ini" / "save" / "game.ini", "template\n")
            write_text(assets_root / "xjxqy" / "ini" / "save" / "npc" / "npc001.ini", "template npc\n")
            write_text(assets_root / "xjxqy_test_mod" / "save" / "rpg0" / "game.ini", "existing\n")

            ensure_initial_save_seed(assets_root, "XJXQY_TEST_MOD")

            seed_dir = assets_root / "xjxqy_test_mod" / "save" / "rpg0"
            self.assertEqual((seed_dir / "game.ini").read_text(encoding="utf-8"), "existing\n")
            self.assertFalse((seed_dir / "npc" / "npc001.ini").exists())

    def test_find_initial_save_template_stops_on_dependency_cycle(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            assets_root = Path(temp_dir)
            write_text(
                assets_root / "a" / "game_profile.ini",
                "[Game]\nId=A\n[Resource]\nDependencyId=B\n",
            )
            write_text(
                assets_root / "b" / "game_profile.ini",
                "[Game]\nId=B\n[Resource]\nDependencyId=A\n",
            )

            self.assertIsNone(find_initial_save_template(assets_root, "A"))
            ensure_initial_save_seed(assets_root, "A")
            self.assertFalse((assets_root / "a" / "save" / "rpg0" / "game.ini").exists())

    def test_find_initial_save_template_searches_ordered_multiple_dependencies(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            assets_root = Path(temp_dir)
            write_text(
                assets_root / "mod" / "game_profile.ini",
                "[Game]\nId=MOD\n[Resource]\nDependencyId=FIRST, SECOND, first\n",
            )
            write_text(
                assets_root / "first" / "game_profile.ini",
                "[Game]\nId=FIRST\n[Resource]\nDependencyId=FIRST_BASE\n",
            )
            write_text(
                assets_root / "first_base" / "game_profile.ini",
                "[Game]\nId=FIRST_BASE\n",
            )
            write_text(
                assets_root / "second" / "game_profile.ini",
                "[Game]\nId=SECOND\n",
            )
            first_template = assets_root / "first_base" / "ini" / "save"
            second_template = assets_root / "second" / "ini" / "save"
            write_text(first_template / "game.ini", "first depth-first template\n")
            write_text(second_template / "game.ini", "second direct template\n")

            self.assertEqual(
                find_initial_save_template(assets_root, "MOD"),
                first_template,
            )


class ScenarioSchemaTests(unittest.TestCase):
    def test_load_scenarios_rejects_default_value_inheritance(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            scenario_path = Path(temp_dir) / "scenarios.ini"
            write_text(
                scenario_path,
                "\n".join(
                    [
                        "[DEFAULT]",
                        "Status=ready",
                        "Smoke=1",
                        "ExpectVariables=inherited=7",
                        "[Scenario.good]",
                        "Choice=1",
                        "EntryScript=good.txt",
                        "",
                    ]
                ),
            )

            with self.assertRaises(ValueError):
                load_scenarios(scenario_path)

    def test_load_scenarios_rejects_misspelled_section(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            scenario_path = Path(temp_dir) / "scenarios.ini"
            write_text(
                scenario_path,
                "\n".join(
                    [
                        "[Scenario.good]",
                        "Status=ready",
                        "Choice=1",
                        "Smoke=1",
                        "EntryScript=good.txt",
                        "[Scenaro.bad]",
                        "Status=ready",
                        "Choice=2",
                        "Smoke=1",
                        "EntryScript=bad.txt",
                        "",
                    ]
                ),
            )

            with self.assertRaises(ValueError):
                load_scenarios(scenario_path)

    def test_load_scenarios_rejects_silent_exclusion_typos(self) -> None:
        for malformed_line in ("Smoke=treu", "Status=raedy", "missing Choice", "missing Smoke"):
            with self.subTest(malformed_line=malformed_line), tempfile.TemporaryDirectory() as temp_dir:
                scenario_path = Path(temp_dir) / "scenarios.ini"
                lines = [
                    "[Scenario.bad]",
                    "Status=ready",
                    "Choice=1",
                    "Smoke=1",
                    "EntryScript=bad.txt",
                ]
                if malformed_line == "missing Choice":
                    lines.remove("Choice=1")
                elif malformed_line == "missing Smoke":
                    lines.remove("Smoke=1")
                elif malformed_line.startswith("Smoke="):
                    lines[3] = malformed_line
                else:
                    lines[1] = malformed_line
                write_text(scenario_path, "\n".join(lines) + "\n")

                with self.assertRaises(ValueError):
                    load_scenarios(scenario_path)

    def test_load_scenarios_rejects_duplicate_choice(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            scenario_path = Path(temp_dir) / "scenarios.ini"
            write_text(
                scenario_path,
                "\n".join(
                    [
                        "[Scenario.good]",
                        "Status=ready",
                        "Choice=1",
                        "Smoke=1",
                        "EntryScript=good.txt",
                        "[Scenario.other]",
                        "Status=ready",
                        "Choice=1",
                        "Smoke=1",
                        "EntryScript=escape.txt",
                        "",
                    ]
                ),
            )

            with self.assertRaises(ValueError):
                load_scenarios(scenario_path)

    def test_load_scenarios_rejects_unsafe_name(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            scenario_path = Path(temp_dir) / "scenarios.ini"
            write_text(
                scenario_path,
                "\n".join(
                    [
                        "[Scenario...\\escape]",
                        "Status=ready",
                        "Choice=1",
                        "Smoke=1",
                        "EntryScript=escape.txt",
                        "",
                    ]
                ),
            )

            with self.assertRaises(ValueError):
                load_scenarios(scenario_path)

    def test_load_scenarios_rejects_unknown_expectation_key(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            scenario_path = Path(temp_dir) / "scenarios.ini"
            write_text(
                scenario_path,
                "\n".join(
                    [
                        "[Scenario.good]",
                        "Status=ready",
                        "Choice=1",
                        "Smoke=1",
                        "EntryScript=good.txt",
                        "ExpectVaribles=answer=42",
                        "",
                    ]
                ),
            )

            with self.assertRaises(ValueError):
                load_scenarios(scenario_path)

    def test_load_and_select_ready_smoke_scenario(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            scenario_path = Path(temp_dir) / "scenarios.ini"
            write_text(
                scenario_path,
                "\n".join(
                    [
                        "[Scenario.good]",
                        "Status=ready",
                        "Choice=1",
                        "Smoke=yes",
                        "EntryScript=good.txt",
                        "ExpectVariables=answer=42",
                        "",
                    ]
                ),
            )

            scenarios = load_scenarios(scenario_path)
            self.assertEqual([scenario.name for scenario in select_scenarios(scenarios, [])], ["good"])

    def test_load_scenarios_parses_scenario_timeout(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            scenario_path = Path(temp_dir) / "scenarios.ini"
            write_text(
                scenario_path,
                "\n".join(
                    [
                        "[Scenario.slow]",
                        "Status=ready",
                        "Choice=1",
                        "Smoke=1",
                        "EntryScript=slow.txt",
                        "TimeoutSeconds=180",
                        "",
                    ]
                ),
            )

            scenarios = load_scenarios(scenario_path)
            self.assertEqual(scenarios[0].timeout_seconds, 180)

    def test_load_scenarios_rejects_negative_scenario_timeout(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            scenario_path = Path(temp_dir) / "scenarios.ini"
            write_text(
                scenario_path,
                "\n".join(
                    [
                        "[Scenario.bad]",
                        "Status=ready",
                        "Choice=1",
                        "Smoke=1",
                        "EntryScript=bad.txt",
                        "TimeoutSeconds=-1",
                        "",
                    ]
                ),
            )

            with self.assertRaises(ValueError):
                load_scenarios(scenario_path)


class PathAndStateIsolationTests(unittest.TestCase):
    def test_pack_sections_do_not_override_direct_child_discovery(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            assets_root = Path(temp_dir)
            write_text(
                assets_root / "resources.ini",
                "\n".join(
                    [
                        "[Collection]",
                        "CommonPath=common",
                        "[Pack.MOD]",
                        "Id=MOD",
                        "Path=first",
                        "[pack.mod]",
                        "path=second ; runtime inline comment",
                        "",
                    ]
                ),
            )
            write_text(assets_root / "second" / "game_profile.ini", "[Game]\nId=MOD\n")
            self.assertEqual(resource_pack_path(assets_root, "MOD"), (assets_root / "second").resolve())

    def test_duplicate_pack_sections_are_ignored_by_discovery(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            assets_root = Path(temp_dir)
            write_text(
                assets_root / "resources.ini",
                "[Pack.MOD]\nId=MOD\nPath=first\n[Pack.MOD]\nPath=second\n",
            )
            write_text(assets_root / "second" / "game_profile.ini", "[Game]\nId=MOD\n")
            self.assertEqual(resource_pack_path(assets_root, "MOD"), (assets_root / "second").resolve())

    def test_resolve_contained_path_rejects_traversal_drive_and_existing_symlink(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir, tempfile.TemporaryDirectory() as outside_dir:
            root = Path(temp_dir)
            for unsafe in ("../outside", "C:/outside", "/outside"):
                with self.subTest(unsafe=unsafe), self.assertRaises(ValueError):
                    resolve_contained_path(root, unsafe, "test path")

            link = root / "link"
            try:
                link.symlink_to(Path(outside_dir), target_is_directory=True)
            except OSError:
                return
            with self.assertRaises(ValueError):
                resolve_contained_path(root, "link/file.txt", "test symlink path")

    def test_scenario_log_path_rejects_section_name_escape(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            for unsafe in ("../victim", "C:\\victim", "nested/name"):
                with self.subTest(unsafe=unsafe), self.assertRaises(ValueError):
                    scenario_log_path(Path(temp_dir), unsafe)

    def test_scenario_log_path_rejects_existing_internal_symlink(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            log_root = Path(temp_dir)
            victim = log_root / "important.txt"
            write_text(victim, "keep\n")
            try:
                (log_root / "good.log").symlink_to(victim)
            except OSError:
                return

            with self.assertRaises(ValueError):
                scenario_log_path(log_root, "good")
            self.assertEqual(victim.read_text(encoding="utf-8"), "keep\n")

    def test_pack_path_entry_cannot_escape_direct_child_discovery(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            assets_root = root / "assets"
            write_text(
                assets_root / "resources.ini",
                "[Pack.MOD]\nId=MOD\nPath=../outside\n",
            )
            write_text(root / "outside" / "game_profile.ini", "[Game]\nId=MOD\n")

            with self.assertRaises(ValueError):
                find_initial_save_template(assets_root, "MOD")

    def test_initial_save_copy_rejects_linked_source_content(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "source"
            target = root / "target"
            victim = root / "outside.txt"
            write_text(victim, "outside secret\n")
            source.mkdir()
            try:
                (source / "game.ini").symlink_to(victim)
            except OSError:
                return

            with self.assertRaises(ValueError):
                copy_directory_contents(source, target)
            self.assertFalse((target / "game.ini").exists())

    def test_initial_save_seed_rejects_linked_destination(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            assets_root = Path(temp_dir)
            write_text(
                assets_root / "resources.ini",
                "[Collection]\nCommonPath=common\n",
            )
            pack_root = assets_root / "mod"
            write_text(
                pack_root / "game_profile.ini",
                "[Game]\nId=MOD\n[Resource]\nDependencyId=\n",
            )
            write_text(pack_root / "ini" / "save" / "game.ini", "template\n")
            victim = pack_root / "ini" / "victim"
            write_text(victim / "settings.ini", "keep\n")
            (pack_root / "save").mkdir()
            try:
                (pack_root / "save" / "rpg0").symlink_to(victim, target_is_directory=True)
            except OSError:
                return

            with self.assertRaises(ValueError):
                ensure_initial_save_seed(assets_root, "MOD")
            self.assertEqual((victim / "settings.ini").read_text(encoding="utf-8"), "keep\n")
            self.assertFalse((victim / "game.ini").exists())

    def test_preserve_resource_save_state_restores_all_slots(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            pack_root = Path(temp_dir) / "pack"
            write_text(pack_root / "save" / "game" / "game.ini", "before\n")
            write_text(pack_root / "save" / "rpg1" / "game.ini", "slot one\n")

            with preserve_resource_save_state(pack_root):
                self.assertFalse((pack_root / "save").exists())
                write_text(pack_root / "save" / "game" / "game.ini", "after\n")
                write_text(pack_root / "save" / "rpg_auto" / "game.ini", "auto\n")

            self.assertEqual(
                (pack_root / "save" / "game" / "game.ini").read_text(encoding="utf-8"),
                "before\n",
            )
            self.assertEqual(
                (pack_root / "save" / "rpg1" / "game.ini").read_text(encoding="utf-8"),
                "slot one\n",
            )
            self.assertFalse((pack_root / "save" / "rpg_auto").exists())

    def test_preserve_resource_save_state_refuses_link_replacement_without_deleting_pack(self) -> None:
        for target_kind in ("pack", "internal", "outside"):
            with self.subTest(target_kind=target_kind), tempfile.TemporaryDirectory() as temp_dir:
                pack_root = Path(temp_dir) / "pack"
                write_text(pack_root / "save" / "game" / "game.ini", "before\n")
                custom_root = pack_root / "custom"
                write_text(custom_root / "sentinel.txt", "keep\n")
                outside_root = Path(temp_dir) / "outside"
                write_text(outside_root / "sentinel.txt", "outside keep\n")
                target = {
                    "pack": pack_root,
                    "internal": custom_root,
                    "outside": outside_root,
                }[target_kind]

                try:
                    with self.assertRaises(ValueError):
                        with preserve_resource_save_state(pack_root):
                            (pack_root / "save").symlink_to(target, target_is_directory=True)
                except OSError:
                    return

                self.assertTrue(pack_root.exists())
                self.assertEqual((custom_root / "sentinel.txt").read_text(encoding="utf-8"), "keep\n")
                self.assertEqual((outside_root / "sentinel.txt").read_text(encoding="utf-8"), "outside keep\n")
                self.assertTrue((pack_root / ".mod-scenario-smoke-save-backup").exists())

    def test_exclusive_run_lock_rejects_concurrent_owner_and_cleans_up(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            lock_path = Path(temp_dir) / "smoke.lock"
            with exclusive_run_lock(lock_path):
                self.assertTrue(lock_path.exists())
                with self.assertRaises(ValueError):
                    with exclusive_run_lock(lock_path):
                        pass
            self.assertFalse(lock_path.exists())

    def test_exclusive_run_lock_rejects_broken_symlink_without_touching_target(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            outside_target = root / "outside" / "missing.lock"
            lock_path = root / "smoke.lock"
            try:
                lock_path.symlink_to(outside_target)
            except OSError:
                return
            with self.assertRaises(ValueError):
                with exclusive_run_lock(lock_path):
                    pass
            self.assertFalse(outside_target.exists())

    def test_run_scenario_rejects_non_positive_timeout_before_process_launch(self) -> None:
        scenario = Scenario(
            section="Scenario.good",
            name="good",
            choice=1,
            status="ready",
            smoke=True,
            post_newgame_wait_ms=0,
            timeout_seconds=0,
            expected_variables=tuple(),
            expected_saved_goods_slots=tuple(),
        )
        with tempfile.TemporaryDirectory() as temp_dir, self.assertRaises(ValueError):
            root = Path(temp_dir)
            run_scenario(
                repo_root=root,
                executable=root / "missing.exe",
                assets_root=root / "assets",
                resource_id="MOD",
                log_dir=root / "logs",
                timeout_seconds=0,
                scenario=scenario,
                verbose=False,
                prepare_initial_save=False,
            )


class CommandLineTests(unittest.TestCase):
    def test_default_executable_prefers_debug_suffix(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            release = repo_root / "bin" / "win64" / "Release" / "jxqy-all-in-one.exe"
            debug = repo_root / "bin" / "win64" / "Debug" / "jxqy-all-in-one-debug.exe"
            write_text(release, "release")
            self.assertEqual(default_executable(repo_root), release)
            write_text(debug, "debug")
            self.assertEqual(default_executable(repo_root), debug)

    def test_list_does_not_require_game_executable(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            scenario_path = root / "scenarios.ini"
            write_text(
                scenario_path,
                "\n".join(
                    [
                        "[Scenario.good]",
                        "Status=ready",
                        "Choice=1",
                        "Smoke=1",
                        "EntryScript=good.txt",
                        "ExpectVariables=answer=42",
                        "",
                    ]
                ),
            )
            stdout = StringIO()
            stderr = StringIO()
            with redirect_stdout(stdout), redirect_stderr(stderr):
                result = main(
                    [
                        "--list",
                        "--exe",
                        str(root / "missing.exe"),
                        "--scenarios-ini",
                        str(scenario_path),
                    ]
                )

            self.assertEqual(result, 0, stderr.getvalue())
            self.assertIn("Scenario.good", stdout.getvalue())


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    unittest.main()
