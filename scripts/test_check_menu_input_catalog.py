#!/usr/bin/env python3
"""Focused regression tests for the menu-input resource scanner."""

from __future__ import annotations

import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).with_name("check_menu_input_catalog.py")
SPEC = importlib.util.spec_from_file_location("check_menu_input_catalog", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
catalog = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(catalog)


def run_git(repository: Path, *arguments: str) -> None:
    subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


class MenuInputCatalogScannerTests(unittest.TestCase):
    def make_repository(self) -> tuple[tempfile.TemporaryDirectory[str], Path]:
        temporary_directory = tempfile.TemporaryDirectory()
        repository = Path(temporary_directory.name) / "repository"
        repository.mkdir()
        run_git(repository, "init", "--quiet")
        run_git(repository, "config", "user.email", "menu-input-test@example.invalid")
        run_git(repository, "config", "user.name", "Menu Input Test")
        return temporary_directory, repository

    def test_tracked_and_untracked_worktree_menu_files_are_read(self) -> None:
        temporary_directory, repository = self.make_repository()
        with temporary_directory:
            tracked = repository / "pack/ini/ui/top/top.menu.ini"
            tracked.parent.mkdir(parents=True)
            tracked.write_text(
                "[component1]\ntype=Button\nname=tracked\nfile=tracked.ini\n",
                encoding="utf-8",
            )
            run_git(repository, "add", ".")
            run_git(repository, "commit", "--quiet", "-m", "fixture")

            tracked.write_text(
                "[component1]\ntype=Button\nname=worktree\nfile=tracked.ini\n",
                encoding="utf-8",
            )
            untracked = repository / "pack/ini/ui/top/untracked.menu.ini"
            untracked.write_text(
                "[component1]\ntype=Button\nname=untracked\nfile=untracked.ini\n",
                encoding="utf-8",
            )

            errors: list[str] = []
            paths = catalog.git_worktree_files(
                repository, "*.menu.ini", "fixture", errors
            )
            self.assertEqual(errors, [])
            self.assertIn(
                untracked.relative_to(repository).as_posix(),
                {path.replace("\\", "/") for path in paths},
            )
            text = catalog.read_worktree_text(
                repository,
                "fixture",
                tracked.relative_to(repository).as_posix(),
                errors,
            )
            self.assertIsNotNone(text)
            self.assertIn("name=worktree", text)

    def test_worktree_deletions_and_renames_do_not_fall_back_to_index(self) -> None:
        temporary_directory, repository = self.make_repository()
        with temporary_directory:
            deleted = repository / "pack/ini/ui/top/deleted.menu.ini"
            renamed_from = repository / "pack/ini/ui/top/renamed-from.menu.ini"
            renamed_to = repository / "pack/ini/ui/top/renamed-to.menu.ini"
            deleted.parent.mkdir(parents=True)
            deleted.write_text("[menu]\nname=deleted\n", encoding="utf-8")
            renamed_from.write_text("[menu]\nname=renamed\n", encoding="utf-8")
            run_git(repository, "add", ".")
            run_git(repository, "commit", "--quiet", "-m", "fixture")

            deleted.unlink()
            renamed_from.rename(renamed_to)

            errors: list[str] = []
            paths = {
                path.replace("\\", "/")
                for path in catalog.git_worktree_files(
                    repository, "*.menu.ini", "fixture", errors
                )
            }
            self.assertEqual(errors, [])
            self.assertNotIn(deleted.relative_to(repository).as_posix(), paths)
            self.assertNotIn(
                renamed_from.relative_to(repository).as_posix(), paths
            )
            self.assertIn(renamed_to.relative_to(repository).as_posix(), paths)

            read_errors: list[str] = []
            self.assertIsNone(
                catalog.read_worktree_text(
                    repository,
                    "fixture",
                    deleted.relative_to(repository).as_posix(),
                    read_errors,
                )
            )
            self.assertIsNone(
                catalog.read_worktree_text(
                    repository,
                    "fixture",
                    renamed_from.relative_to(repository).as_posix(),
                    read_errors,
                )
            )
            self.assertEqual(len(read_errors), 2)

    def test_numbering_stops_at_the_first_gap_like_config_driven_panel(self) -> None:
        errors: list[str] = []
        inventory = catalog.parse_menu_inventory(
            "root",
            "pack/ini/ui/top/top.menu.ini",
            "\n".join(
                [
                    "[component1]",
                    "type=Button",
                    "name=first",
                    "file=first.ini",
                    "controllerRight=unreachable",
                    "[component3]",
                    "type=Button",
                    "name=unreachable",
                    "file=third.ini",
                    "[submenu1]",
                    "name=first-submenu",
                    "file=first.menu.ini",
                    "[submenu3]",
                    "name=unreachable-submenu",
                    "file=third.menu.ini",
                ]
            ),
            errors,
        )
        sections = {entry[2] for entry in inventory}
        self.assertIn("component1", sections)
        self.assertIn("submenu1", sections)
        self.assertNotIn("component3", sections)
        self.assertNotIn("submenu3", sections)
        self.assertTrue(any("component3" in error for error in errors))
        self.assertTrue(any("submenu3" in error for error in errors))
        self.assertIn(
            "Controller direction target is missing from the current menu "
            "scope: root:pack/ini/ui/top/top.menu.ini"
            "[component1].controllerright=unreachable",
            errors,
        )

    def test_controller_direction_targets_are_exact_unique_and_not_self(
        self,
    ) -> None:
        errors: list[str] = []
        inventory = catalog.parse_menu_inventory(
            "root",
            "pack/ini/ui/top/top.menu.ini",
            "\n".join(
                [
                    "[component1]",
                    "type=Button",
                    "name=alpha",
                    "controllerUp=Alpha",
                    "controllerDown=alpha",
                    "controllerLeft=shared",
                    "controllerRight=beta",
                    "[component2]",
                    "type=Button",
                    "name=beta",
                    "controllerUp=",
                    "[component3]",
                    "type=Button",
                    "name=shared",
                    "[component4]",
                    "type=Button",
                    "name=shared",
                ]
            ),
            errors,
        )

        self.assertEqual(
            errors,
            [
                "Controller direction target is missing from the current menu "
                "scope: root:pack/ini/ui/top/top.menu.ini"
                "[component1].controllerup=Alpha",
                "Controller direction target references its own component: "
                "root:pack/ini/ui/top/top.menu.ini"
                "[component1].controllerdown=alpha",
                "Controller direction target is duplicated in the current "
                "menu scope: root:pack/ini/ui/top/top.menu.ini"
                "[component1].controllerleft=shared matches "
                "[component3], [component4]",
            ],
        )
        self.assertEqual(
            sum(entry[3] == "control" for entry in inventory),
            4,
        )

    def test_controller_direction_parser_matches_runtime_ini_syntax(self) -> None:
        errors: list[str] = []
        catalog.parse_menu_inventory(
            "root",
            "pack/ini/ui/top/top.menu.ini",
            "\n".join(
                [
                    "[component1]",
                    "type=Button",
                    "name=alpha",
                    "controllerUp: beta ; valid inline comment",
                    "controllerDown=missing ; ignored inline comment",
                    "controllerLeft=semi;literal",
                    "controllerRight: absent",
                    "[component2]",
                    "type=Button",
                    "name=beta",
                    "[component3]",
                    "type=Button",
                    "name=semi;literal",
                ]
            ),
            errors,
        )

        self.assertEqual(
            errors,
            [
                "Controller direction target is missing from the current menu "
                "scope: root:pack/ini/ui/top/top.menu.ini"
                "[component1].controllerdown=missing",
                "Controller direction target is missing from the current menu "
                "scope: root:pack/ini/ui/top/top.menu.ini"
                "[component1].controllerright=absent",
            ],
        )

    def test_controller_direction_sources_require_nonempty_unique_names(
        self,
    ) -> None:
        errors: list[str] = []
        inventory = catalog.parse_menu_inventory(
            "root",
            "pack/ini/ui/top/top.menu.ini",
            "\n".join(
                [
                    "[component1]",
                    "type=Button",
                    "controllerRight=target",
                    "[component2]",
                    "type=Button",
                    "name=duplicate-source",
                    "controllerRight=target",
                    "[component3]",
                    "type=Button",
                    "name=duplicate-source",
                    "[component4]",
                    "type=Button",
                    "name=target",
                    "[component5]",
                    "type=Button",
                    "name=historical-duplicate",
                    "[component6]",
                    "type=Button",
                    "name=historical-duplicate",
                    "[component7]",
                    "type=Button",
                ]
            ),
            errors,
        )

        self.assertEqual(
            errors,
            [
                "Controller direction source name is missing: "
                "root:pack/ini/ui/top/top.menu.ini[component1]"
                ".name=<missing-name>",
                "Controller direction source name is duplicated in the current "
                "menu scope: root:pack/ini/ui/top/top.menu.ini"
                "[component2].name=duplicate-source matches "
                "[component2], [component3]",
            ],
        )
        self.assertEqual(
            sum(entry[3] == "control" for entry in inventory),
            7,
        )

    def test_component_section_numbers_are_positive_canonical_and_unique(
        self,
    ) -> None:
        errors: list[str] = []
        inventory = catalog.parse_menu_inventory(
            "root",
            "pack/ini/ui/top/top.menu.ini",
            "\n".join(
                [
                    "[component1]",
                    "type=Button",
                    "name=first",
                    "[Component1]",
                    "type=Button",
                    "name=case-duplicate",
                    "[component01]",
                    "type=Button",
                    "name=padded",
                    "[component0]",
                    "type=Button",
                    "name=zero",
                    "[component2]",
                    "type=Button",
                    "name=second",
                ]
            ),
            errors,
        )

        self.assertTrue(
            any(
                "Component section number must be positive" in error
                and "[component0]" in error
                for error in errors
            )
        )
        self.assertTrue(
            any(
                "Component section number is not canonical" in error
                and "[component01]" in error
                for error in errors
            )
        )
        self.assertTrue(
            any(
                "Component section number is duplicated" in error
                and "[component1], [Component1], [component01]" in error
                for error in errors
            )
        )
        self.assertEqual(
            sum(entry[3] == "control" for entry in inventory),
            2,
        )

    def test_component_file_and_actual_interactive_rect_are_validated(self) -> None:
        temporary_directory, repository = self.make_repository()
        with temporary_directory:
            menu_path = "pack/ini/ui/top/top.menu.ini"
            menu_text = "\n".join(
                [
                    "[component1]",
                    "type=Button",
                    "name=missing",
                    "file=ini\\ui\\top\\missing.ini",
                    "[component2]",
                    "type=Button",
                    "name=zero",
                    "file=ini\\ui\\top\\zero.ini",
                    "[component3]",
                    "type=Button",
                    "name=ready",
                    "file=ini\\ui\\top\\ready.ini",
                ]
            )
            zero = repository / "pack/ini/ui/top/zero.ini"
            ready = repository / "pack/ini/ui/top/ready.ini"
            zero.parent.mkdir(parents=True)
            zero.write_text(
                "[Init]\nLeft=1\nTop=2\nWidth=0\nHeight=20\n",
                encoding="utf-8",
            )
            ready.write_text(
                "[Init]\nLeft=1\nTop=2\nWidth=30\nHeight=20\n",
                encoding="utf-8",
            )

            errors: list[str] = []
            inventory = catalog.parse_menu_inventory(
                "root",
                menu_path,
                menu_text,
                errors,
                repository=repository,
            )
            names = {entry[5] for entry in inventory if entry[3] == "control"}
            self.assertEqual(names, {"ready"})
            self.assertTrue(any("missing.ini" in error for error in errors))

    def test_component_reference_matches_runtime_ascii_case_folding(self) -> None:
        temporary_directory, repository = self.make_repository()
        with temporary_directory:
            component = repository / "pack/ini/ui/choose/btna.ini"
            component.parent.mkdir(parents=True)
            component.write_text(
                "[Init]\nWidth=30\nHeight=20\n",
                encoding="utf-8",
            )

            errors: list[str] = []
            inventory = catalog.parse_menu_inventory(
                "root",
                "pack/ini/ui/choose/choose.menu.ini",
                "\n".join(
                    [
                        "[component1]",
                        "type=ChooseTextButton",
                        "name=selectA",
                        "file=ini/ui/choose/btnA.ini",
                    ]
                ),
                errors,
                repository=repository,
            )

            self.assertEqual(errors, [])
            self.assertIn(
                (
                    "root",
                    "pack/ini/ui/choose/choose.menu.ini",
                    "component1",
                    "control",
                    "ChooseTextButton",
                    "selectA",
                ),
                inventory,
            )

    def test_component_reference_cannot_escape_or_search_another_pack(self) -> None:
        temporary_directory, repository = self.make_repository()
        with temporary_directory:
            other_pack_component = (
                repository / "pack-b/ini/ui/top/shared-button.ini"
            )
            other_pack_component.parent.mkdir(parents=True)
            other_pack_component.write_text(
                "[Init]\nWidth=30\nHeight=20\n",
                encoding="utf-8",
            )
            outside_component = repository.parent / "outside-button.ini"
            outside_component.write_text(
                "[Init]\nWidth=30\nHeight=20\n",
                encoding="utf-8",
            )
            menu_text = "\n".join(
                [
                    "[component1]",
                    "type=Button",
                    "name=cross-pack",
                    "file=ini/ui/top/shared-button.ini",
                    "[component2]",
                    "type=Button",
                    "name=outside",
                    "file=../outside-button.ini",
                ]
            )

            errors: list[str] = []
            inventory = catalog.parse_menu_inventory(
                "root",
                "pack-a/ini/ui/top/top.menu.ini",
                menu_text,
                errors,
                repository=repository,
            )
            self.assertEqual(
                {entry for entry in inventory if entry[3] == "control"},
                set(),
            )
            self.assertEqual(
                sum("Component file is missing" in error for error in errors),
                2,
            )

    def test_submenu_recursively_scans_plain_ini_definition(self) -> None:
        temporary_directory, repository = self.make_repository()
        with temporary_directory:
            nested_menu = repository / "pack/ini/ui/top/nested.ini"
            nested_component = repository / "pack/ini/ui/top/nested-button.ini"
            nested_menu.parent.mkdir(parents=True)
            nested_menu.write_text(
                "\n".join(
                    [
                        "[component1]",
                        "type=Button",
                        "name=nested-ready",
                        "file=ini/ui/top/nested-button.ini",
                    ]
                ),
                encoding="utf-8",
            )
            nested_component.write_text(
                "[Init]\nWidth=30\nHeight=20\n",
                encoding="utf-8",
            )

            errors: list[str] = []
            inventory = catalog.parse_menu_inventory(
                "root",
                "pack/ini/ui/top/root.menu.ini",
                "\n".join(
                    [
                        "[submenu1]",
                        "name=nested",
                        "file=ini/ui/top/nested.ini",
                    ]
                ),
                errors,
                repository=repository,
            )
            self.assertEqual(errors, [])
            self.assertIn(
                (
                    "root",
                    "pack/ini/ui/top/nested.ini",
                    "component1",
                    "control",
                    "Button",
                    "nested-ready",
                ),
                inventory,
            )

    def test_controller_direction_targets_cannot_cross_submenu_scopes(
        self,
    ) -> None:
        temporary_directory, repository = self.make_repository()
        with temporary_directory:
            component = repository / "pack/ini/ui/top/button.ini"
            nested_menu = repository / "pack/ini/ui/top/nested.ini"
            component.parent.mkdir(parents=True)
            component.write_text(
                "[Init]\nWidth=30\nHeight=20\n",
                encoding="utf-8",
            )
            nested_menu.write_text(
                "\n".join(
                    [
                        "[component1]",
                        "type=Button",
                        "name=nested-only",
                        "file=ini/ui/top/button.ini",
                        "controllerLeft=root-only",
                    ]
                ),
                encoding="utf-8",
            )
            root_text = "\n".join(
                [
                    "[component1]",
                    "type=Button",
                    "name=root-only",
                    "file=ini/ui/top/button.ini",
                    "controllerRight=nested-only",
                    "[submenu1]",
                    "name=nested",
                    "file=ini/ui/top/nested.ini",
                ]
            )

            errors: list[str] = []
            inventory = catalog.parse_menu_inventory(
                "root",
                "pack/ini/ui/top/root.menu.ini",
                root_text,
                errors,
                repository=repository,
            )

            self.assertEqual(
                errors,
                [
                    "Controller direction target is missing from the current "
                    "menu scope: root:pack/ini/ui/top/root.menu.ini"
                    "[component1].controllerright=nested-only",
                    "Controller direction target is missing from the current "
                    "menu scope: root:pack/ini/ui/top/nested.ini"
                    "[component1].controllerleft=root-only",
                ],
            )
            self.assertEqual(
                sum(entry[3] == "control" for entry in inventory),
                2,
            )

    def test_recursive_submenu_cycle_is_reported(self) -> None:
        temporary_directory, repository = self.make_repository()
        with temporary_directory:
            root_menu = repository / "pack/ini/ui/top/root.menu.ini"
            nested_menu = repository / "pack/ini/ui/top/nested.ini"
            root_menu.parent.mkdir(parents=True)
            root_text = (
                "[submenu1]\n"
                "name=nested\n"
                "file=ini/ui/top/nested.ini\n"
            )
            root_menu.write_text(root_text, encoding="utf-8")
            nested_menu.write_text(
                "[submenu1]\n"
                "name=root\n"
                "file=ini/ui/top/root.menu.ini\n",
                encoding="utf-8",
            )

            errors: list[str] = []
            catalog.parse_menu_inventory(
                "root",
                root_menu.relative_to(repository).as_posix(),
                root_text,
                errors,
                repository=repository,
            )
            self.assertEqual(
                sum("Recursive submenu cycle" in error for error in errors),
                1,
            )

    def test_interactive_geometry_requires_positive_base_zero_dimensions(
        self,
    ) -> None:
        self.assertFalse(
            catalog.component_has_interactive_geometry(
                "[Init]\nWidth=0\nHeight=0\nImage=anything.asf\n"
            )
        )
        self.assertFalse(
            catalog.component_has_interactive_geometry(
                "[Init]\nWidth=0x20\nHeight=0\nImage=anything.asf\n"
            )
        )
        self.assertTrue(
            catalog.component_has_interactive_geometry(
                "[Init]\nWidth=0x20\nHeight=0x14\n"
            )
        )
        self.assertTrue(
            catalog.component_has_interactive_geometry(
                "[Init]\nWidth=040\nHeight=024\n"
            )
        )

    def test_plain_gamble_resource_ini_files_are_discovered(self) -> None:
        temporary_directory, repository = self.make_repository()
        with temporary_directory:
            expected_paths = {
                "pack/ini/ui/littlegame/chipin.ini",
                "common/ini/ui/dicegame/window.ini",
                "common/ini/ui/fishgame/window.ini",
            }
            for relative_path in expected_paths:
                path = repository / relative_path
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(
                    "[Init]\nLeft=1\nTop=2\nWidth=30\nHeight=20\n",
                    encoding="utf-8",
                )

            errors: list[str] = []
            discovered = catalog.resource_definition_files(
                repository, "fixture", errors
            )
            self.assertEqual(errors, [])
            self.assertTrue(
                expected_paths.issubset(
                    {path.replace("\\", "/") for path in discovered}
                )
            )

    def test_synchronized_assets_without_git_metadata_are_discovered(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            repository = Path(temporary_directory) / "assets"
            menu = repository / "pack/ini/ui/top/top.menu.ini"
            gamble = repository / "pack/ini/ui/littlegame/window.ini"
            menu.parent.mkdir(parents=True)
            gamble.parent.mkdir(parents=True)
            menu.write_text("[menu]\nname=top\n", encoding="utf-8")
            gamble.write_text("[Init]\nWidth=1\n", encoding="utf-8")

            errors: list[str] = []
            discovered = catalog.resource_definition_files(
                repository, "assets", errors
            )

            self.assertEqual(errors, [])
            self.assertEqual(
                {
                    "pack/ini/ui/top/top.menu.ini",
                    "pack/ini/ui/littlegame/window.ini",
                },
                set(discovered),
            )


if __name__ == "__main__":
    unittest.main()
