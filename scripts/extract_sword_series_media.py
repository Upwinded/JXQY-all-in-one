"""Extract the reviewed Sword of Fate media bundles without changing story files."""
from __future__ import annotations

import argparse
import configparser
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

MEDIA_ROOTS = {'asf', 'bmp', 'font', 'image', 'img', 'mpc', 'music', 'sound', 'video'}
MEDIA_EXTENSIONS = {'.asf', '.bmp', '.fnt', '.jpg', '.jpeg', '.png', '.wma', '.wav',
                    '.ogg', '.mp3', '.mpc', '.mpi', '.avi', '.mp4', '.wmv', '.bik',
                    '.webm', '.ttf', '.otf'}
CORE = ('JIAN_ZHI_YUAN_SHARED', '剑之缘公共媒体')
PAIR = ('JIAN_ZHI_YUAN_5_6_SHARED', '剑之缘5-6公共媒体')
ASCII_LOWER = str.maketrans('ABCDEFGHIJKLMNOPQRSTUVWXYZ', 'abcdefghijklmnopqrstuvwxyz')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def profile(path):
    result = configparser.ConfigParser(interpolation=None)
    result.optionxform = str
    result.read_string(path.read_text(encoding='utf-8-sig'))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    assets = args.assets.resolve()
    evidence = args.evidence.resolve()
    assert not evidence.exists(), 'Use a new evidence directory'
    for _, folder in (CORE, PAIR):
        assert not (assets / folder).exists(), folder
    folders = {number: f'剑之缘{number}高清版' for number in range(1, 7)}
    profiles = {number: profile(assets / folder / 'game_profile.ini')
                for number, folder in folders.items()}
    for number, ini in profiles.items():
        assert ini['Game']['Id'] == f'JIAN_ZHI_YUAN_{number}_HD'
        assert ini['UI']['BaseId'] == 'YYCS' and ini['UI']['PreferLocal'] == '1'
    tracked = subprocess.check_output(['git', '-C', str(assets), 'ls-files', '-z', '--',
                                      *folders.values()]).decode('utf-8').split('\0')
    inventories = {number: {} for number in folders}
    source_paths = {number: {} for number in folders}
    by_folder = {folder: number for number, folder in folders.items()}
    for path in filter(None, tracked):
        folder, relative = path.split('/', 1)
        source = assets / path
        assert source.is_file() and not source.is_symlink(), source
        number = by_folder[folder]
        name = relative.translate(ASCII_LOWER)
        assert name not in inventories[number], ('case collision', number, name)
        source_paths[number][name] = relative
        inventories[number][name] = {
            'size': source.stat().st_size, 'sha256': digest(source)}
    eligible = lambda name: name.split('/')[0] in MEDIA_ROOTS and Path(name).suffix.lower() in MEDIA_EXTENSIONS
    def common(consumers):
        first = inventories[consumers[0]]
        return {name: detail for name, detail in first.items() if eligible(name)
                and all(inventories[number].get(name) == detail for number in consumers[1:])}
    core = common(list(folders))
    pair = {name: detail for name, detail in common([5, 6]).items() if name not in core}
    print(f'Reviewed groups: core={len(core)}, pair={len(pair)}', flush=True)
    assert len(core) == 782 and len(pair) == 2605, 'Review changed media inventory before extraction'
    evidence.mkdir(parents=True)
    before_profiles = {str(number): (assets / folder / 'game_profile.ini').read_bytes().decode('utf-8')
                       for number, folder in folders.items()}
    record = dict(sourceCommit=subprocess.check_output(['git', '-C', str(assets), 'rev-parse', 'HEAD']).decode().strip(),
                  before=inventories, sourcePaths=source_paths, profiles=before_profiles, core=core, pair=pair, status='prepared')
    record_path = evidence / 'extraction.json'
    record_path.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf-8')

    # Copy and verify every shared file before unlinking any reviewed duplicate.
    for (identity, folder), files, ui_base in ((CORE, core, 'YYCS'), (PAIR, pair, CORE[0])):
        target = assets / folder
        target.mkdir()
        for name, detail in files.items():
            destination = target / name
            assert destination.resolve().is_relative_to(assets)
            destination.parent.mkdir(parents=True, exist_ok=True)
            owner = 1 if identity == CORE[0] else 5
            shutil.copyfile(assets / folders[owner] / source_paths[owner][name], destination)
            assert digest(destination) == detail['sha256']
        dependency = f'DependencyId={CORE[0]}\n' if identity == PAIR[0] else ''
        text = (f'[Game]\nId={identity}\nName={folder}\nInstallDirectory={folder}\nVersion=1.0.0\n\n'
                f'[Resource]\nResourceOnly=1\nTextEncodingConverted=1\n{dependency}\n'
                f'[UI]\nBaseId={ui_base}\n\n[Release]\nMinimumEngineVersion=1.2.0\n')
        (target / 'game_profile.ini').write_text(text, encoding='utf-8')

    for number, folder in folders.items():
        root = assets / folder
        shared = {**core, **(pair if number in (5, 6) else {})}
        dependency = PAIR[0] if number in (5, 6) else CORE[0]
        path = root / 'game_profile.ini'
        text = before_profiles[str(number)]
        dependencies = profiles[number]['Resource']['DependencyId'].split(',')
        # These packs already use YYCS UI. Include that original package in
        # the download closure without changing the order of existing bases.
        if 'YYCS' not in dependencies:
            dependencies.append('YYCS')
        text, count = re.subn(r'^DependencyId=[^\r\n]*',
                             'DependencyId=' + ','.join([dependency, *dependencies]), text, flags=re.M)
        assert count == 1
        text, count = re.subn(r'^BaseId=YYCS(?=\r?$)', 'BaseId=' + dependency, text, flags=re.M)
        assert count == 1
        path.write_bytes(text.encode('utf-8'))
        for name, detail in shared.items():
            source = root / source_paths[number][name]
            assert source.resolve().is_relative_to(assets) and digest(source) == detail['sha256']
            source.unlink()
        # No other tracked bytes, including initial saves and story/INI files, may change.
        for name, detail in inventories[number].items():
            if name in shared or name == 'game_profile.ini':
                continue
            assert digest(root / source_paths[number][name]) == detail['sha256'], (number, name)
        current = profile(path)
        previous = profiles[number]
        current['Resource']['DependencyId'] = previous['Resource']['DependencyId']
        current['UI']['BaseId'] = previous['UI']['BaseId']
        assert {s: dict(current[s]) for s in current.sections()} == {
            s: dict(previous[s]) for s in previous.sections()}
    record.update(status='extracted', removedCopies=6 * len(core) + 2 * len(pair),
                  savedMediaBytes=5 * sum(x['size'] for x in core.values()) + sum(x['size'] for x in pair.values()))
    record_path.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({key: record[key] for key in ('status', 'removedCopies', 'savedMediaBytes')}), flush=True)


if __name__ == '__main__':
    main()
