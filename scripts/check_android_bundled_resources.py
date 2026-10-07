"""Check native startup discovery on an installed Android APK.

Use an emulator/device with no downloaded or external resource packs. Install
the supplied APK first; this check restarts the app but does not clear its data
or change its network settings. Disable networking to check offline startup.
"""

import argparse
from pathlib import Path
import re
import subprocess
import time
import zipfile


APPLICATION_ID = "com.upwinded.jxqy_all_in_one"
ACTIVITY = APPLICATION_ID + "/com.upwinded.jxqy.JxqyActivity"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument(
        "--allow-empty", action="store_true",
        help="also accept the minimal release APK with no bundled game profiles",
    )
    options = parser.parse_args()
    with zipfile.ZipFile(options.apk, metadata_encoding="utf-8") as archive:
        profiles = sorted(
            name for name in archive.namelist()
            if name.startswith("assets/") and name.count("/") == 2
            and name.endswith("/game_profile.ini")
        )
    if not profiles and not options.allow_empty:
        parser.error("the supplied APK has no bundled resource profiles")

    def adb(*arguments: str, check: bool = True) -> str:
        result = subprocess.run(
            [options.adb, "-s", options.serial, *arguments],
            check=check, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            encoding="utf-8", errors="replace", timeout=30,
        )
        return result.stdout

    # Restrict log capture to the newly launched process, preserving the device
    # log buffer and any unrelated application diagnostics.
    adb("shell", "am", "force-stop", APPLICATION_ID)
    adb("shell", "am", "start", "-W", "-n", ACTIVITY)
    deadline = time.monotonic() + 30
    log = ""
    while time.monotonic() < deadline:
        process = adb("shell", "pidof", APPLICATION_ID, check=False).strip()
        if process.isdecimal():
            log = adb("logcat", "-d", "--pid=" + process, "-v", "brief")
            match = re.search(
                r"ResourceManager: shared catalog discovered (\d+) retained resource entries",
                log,
            )
            if match:
                options.log.parent.mkdir(parents=True, exist_ok=True)
                options.log.write_text(log, encoding="utf-8")
                actual = int(match.group(1))
                print(f"APK profiles={len(profiles)}, runtime discovered={actual}")
                if actual != len(profiles):
                    print(f"FAIL: native startup discovery differs from the APK; see {options.log}")
                    return 1
                print("PASS: native startup discovered every bundled resource pack")
                return 0
        time.sleep(1)
    options.log.parent.mkdir(parents=True, exist_ok=True)
    options.log.write_text(log, encoding="utf-8")
    print(f"FAIL: no native resource discovery result within 30 seconds; see {options.log}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
