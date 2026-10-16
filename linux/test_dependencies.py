#!/usr/bin/env python3
"""Run with python3 linux/test_dependencies.py on Linux (no downloads/builds)."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


class DependencyCheckTests(unittest.TestCase):
    def test_manifest_line_endings_and_incomplete_dependencies(self):
        source = Path(__file__).resolve().parent
        script = (source / "build-dependencies.sh").read_text(encoding="utf-8")
        required = re.search(r"required_files=\(\n(.*?)\n\)", script, re.S).group(1).split()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "linux").mkdir()
            for name in ("build-dependencies.sh", "dependencies.lock"):
                shutil.copyfile(source / name, root / "linux" / name)
            dependency = root / "ThirdParty/devel/linux/x86_64"
            for name in required:
                target = dependency / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.touch()
            manifest = dependency / ".dependency-set"
            versions = dict(re.findall(r"^(\w+)=(.+)$",
                (source / "dependencies.lock").read_text(encoding="utf-8"), re.M))
            expected = ";".join(f"{name}={versions[key]}" for name, key in (
                ("SDL3", "SDL3_VERSION"), ("SDL3_image", "SDL3_IMAGE_VERSION"),
                ("SDL3_ttf", "SDL3_TTF_VERSION"), ("SDL3_mixer", "SDL3_MIXER_VERSION"),
                ("FreeType", "FREETYPE_VERSION"), ("FFmpeg", "FFMPEG_VERSION")))

            def check(passes):
                result = subprocess.run(["bash", str(root / "linux/build-dependencies.sh"),
                    "--check"], capture_output=True, timeout=5)
                self.assertEqual(result.returncode == 0, passes, result.stderr.decode())

            for ending in (b"\n", b"\r\n"):
                manifest.write_bytes(expected.encode("utf-8") + ending)
                check(True)
            manifest.write_bytes((expected + ";wrong-version\r\n").encode("utf-8"))
            check(False)
            manifest.write_bytes((expected + "\r\n").encode("utf-8"))
            header = dependency / "include/SDL3/SDL.h"
            header.unlink()
            check(False)
            header.touch()
            link = dependency / "lib/test-link.so"
            link.symlink_to("libSDL3.so.0.4.18")
            check(True)
            link.unlink()
            link.symlink_to("missing-library.so")
            check(False)
            link.unlink()
            link.symlink_to(dependency / "lib/libSDL3.so.0.4.18")
            check(False)
            link.unlink()
            manifest.unlink()
            check(False)


if __name__ == "__main__":
    unittest.main()
