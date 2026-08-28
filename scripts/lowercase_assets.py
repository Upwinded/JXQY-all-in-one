#!/usr/bin/env python3
"""Lowercase every resource path below an assets root.

The script is intentionally conservative:
- .git is ignored.
- ASCII letters in every path segment are lowercased; non-ASCII bytes/chars are
  preserved.
- Case-only/case-fold collisions are allowed only when colliding files have
  identical content. Different-content collisions abort before modifying files.
- Existing empty directories are retained; this tool only renames paths and
  removes byte-identical file duplicates that collapse to one lowercase path.
- Default mode is dry-run. Pass --apply to rename/remove files.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import uuid
from pathlib import Path


def ascii_lower(value: str) -> str:
    chars: list[str] = []
    for ch in value:
        if "A" <= ch <= "Z":
            chars.append(chr(ord(ch) + ord("a") - ord("A")))
        else:
            chars.append(ch)
    return "".join(chars)


def should_skip(path: Path, root: Path) -> bool:
    try:
        relative_parts = path.relative_to(root).parts
    except ValueError:
        return True
    return ".git" in relative_parts


def lower_relative_path(path: Path, root: Path) -> Path:
    relative = path.relative_to(root).as_posix()
    return Path(ascii_lower(relative))


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def same_file(path_a: Path, path_b: Path) -> bool:
    try:
        return path_a.exists() and path_b.exists() and os.path.samefile(path_a, path_b)
    except OSError:
        return False


def same_path_text(path_a: Path, path_b: Path) -> bool:
    return os.fspath(path_a) == os.fspath(path_b)


def relative_text(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def move_file(source: Path, target: Path, apply: bool) -> None:
    print(f"rename {source} -> {target}")
    if not apply:
        return

    target.parent.mkdir(parents=True, exist_ok=True)
    if same_path_text(source, target):
        return

    if target.exists() and not same_file(source, target):
        raise RuntimeError(f"target already exists: {target}")

    temp = source.with_name(f"{source.name}.__lowercase_tmp__{uuid.uuid4().hex}")
    source.rename(temp)
    if target.exists() and not same_file(temp, target):
        temp.rename(source)
        raise RuntimeError(f"target appeared while renaming: {target}")
    temp.rename(target)


def move_directory(source: Path, target: Path, apply: bool) -> None:
    print(f"rename dir {source} -> {target}")
    if not apply:
        return

    if same_path_text(source, target):
        return
    if target.exists() and not same_file(source, target):
        raise RuntimeError(f"target directory already exists: {target}")

    temp = source.with_name(f"{source.name}.__lowercase_tmp__{uuid.uuid4().hex}")
    source.rename(temp)
    temp.rename(target)


def remove_file(path: Path, apply: bool) -> None:
    print(f"remove duplicate {path}")
    if apply:
        path.unlink()


def collect_entries(root: Path) -> tuple[dict[Path, list[Path]], dict[Path, list[Path]]]:
    files: dict[Path, list[Path]] = {}
    directories: dict[Path, list[Path]] = {}
    for path in root.rglob("*"):
        if should_skip(path, root):
            continue
        key = lower_relative_path(path, root)
        if path.is_dir():
            directories.setdefault(key, []).append(path)
        elif path.is_file():
            files.setdefault(key, []).append(path)
    return files, directories


def validate_collisions(files: dict[Path, list[Path]], directories: dict[Path, list[Path]], root: Path) -> None:
    file_keys = set(files.keys())
    directory_keys = set(directories.keys())
    conflicts = sorted(file_keys & directory_keys, key=lambda p: p.as_posix())
    if conflicts:
        text = "\n".join(str(root / conflict) for conflict in conflicts[:20])
        raise RuntimeError(f"file/directory lowercase collisions detected:\n{text}")

    for key, paths in sorted(files.items(), key=lambda item: item[0].as_posix()):
        if len(paths) <= 1:
            continue
        hashes = {file_hash(path) for path in paths}
        if len(hashes) != 1:
            lines = "\n".join(str(path) for path in paths)
            raise RuntimeError(f"different files would collapse to {root / key}:\n{lines}")

    for key, paths in sorted(directories.items(), key=lambda item: item[0].as_posix()):
        if len(paths) <= 1:
            continue
        if not all(same_file(paths[0], path) for path in paths[1:]):
            lines = "\n".join(str(path) for path in paths)
            raise RuntimeError(f"directory lowercase collision requires manual merge at {root / key}:\n{lines}")


def choose_kept_file(paths: list[Path], target: Path) -> Path:
    for path in paths:
        if same_path_text(path, target):
            return path
    return sorted(paths, key=lambda p: p.as_posix())[0]


def lowercase_assets(root: Path, apply: bool) -> None:
    root = root.resolve()
    if not root.is_dir():
        raise RuntimeError(f"assets root is not a directory: {root}")

    files, directories = collect_entries(root)
    validate_collisions(files, directories, root)

    moves = 0
    duplicate_removals = 0
    for key, paths in sorted(files.items(), key=lambda item: item[0].as_posix()):
        target = root / key
        kept = choose_kept_file(paths, target)

        for path in paths:
            if path != kept:
                remove_file(path, apply)
                duplicate_removals += 1

        kept_name_target = kept.with_name(ascii_lower(kept.name))
        if not same_path_text(kept, kept_name_target):
            move_file(kept, kept_name_target, apply)
            moves += 1

    dir_moves = 0
    for _, paths in sorted(directories.items(), key=lambda item: len(item[0].parts), reverse=True):
        source = sorted(paths, key=lambda p: p.as_posix())[0]
        target = source.with_name(ascii_lower(source.name))
        if not same_path_text(source, target):
            move_directory(source, target, apply)
            dir_moves += 1

    action = "applied" if apply else "dry-run"
    print(f"{action}: {moves} file renames, {dir_moves} directory renames, "
          f"{duplicate_removals} duplicate removals")


def main() -> int:
    parser = argparse.ArgumentParser(description="Lowercase all resource file and directory names.")
    parser.add_argument("assets_root", help="assets root directory")
    parser.add_argument("--apply", action="store_true", help="perform changes; default is dry-run")
    args = parser.parse_args()

    lowercase_assets(Path(args.assets_root), args.apply)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
