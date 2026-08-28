#!/usr/bin/env python3
"""Validate platform source, identity, and packaged-asset contracts."""

from __future__ import annotations

import re
import sys
from pathlib import Path


SOURCE_SUFFIXES = {".c", ".cpp"}
ARTIFACT_NAME = "jxqy-all-in-one"
DISPLAY_NAME = "剑侠情缘 All-in-One"
ANDROID_APPLICATION_ID = "com.upwinded.jxqy_all_in_one"
APPLE_BUNDLE_IDENTIFIER = "com.upwinded.jxqy-all-in-one"
PREFERENCE_ORGANIZATION_NAME = "Upwinded"
PREFERENCE_APPLICATION_NAME = "JXQY All In One"
ANDROID_ASSET_EXCLUDES = {
    "**/save/rpg0/**",
    "**/save/game/**",
    "**/save/rpg1/**",
    "**/save/rpg2/**",
    "**/save/rpg3/**",
    "**/save/rpg4/**",
    "**/save/rpg5/**",
    "**/save/rpg6/**",
    "**/save/rpg7/**",
    "**/save/rpg8/**",
    "**/save/rpg9/**",
    "**/save/rpg_auto/**",
    "**/save/shot/**",
    "**/save/system/resource_selection.ini",
    "**/save/system/**",
    "**/save/game_build/**",
    "**/save/load_candidate/**",
    "**/save/load_rollback/**",
    "**/save/live_rollback/**",
    "**/save/save_rollback/**",
    "**/save/save_secondary_rollback/**",
    "**/save/save_journal_build/**",
    "**/save/save_journal/**",
    "**/save/.jxqy-*/**",
    "**/save/.jxqy-*",
    "**/common/config/*.ini",
    "**/log.txt",
    "**/*.log",
    "**/*.tmp",
    "**/*.bak",
    "**/*.orig",
    "**/migration_report.*",
    "**/conversion_report.*",
    "**/*migration-report*.*",
    "**/*conversion-report*.*",
    "**/*迁移报告*.*",
    "**/*转换报告*.*",
    "**/vssver.scc",
    "**/*.zip",
    "**/*.rar",
    "**/*.7z",
    "assets-folder.md",
    "**/README.md",
    "**/readme.md",
    "**/.git/**",
    "**/.gitignore",
    "**/.gitattributes",
    "**/.DS_Store",
    "**/Thumbs.db",
    "**/*~",
}
FORMAL_INITIAL_SAVE_TEMPLATES = {
    "jxqy2/ini/save/game.ini",
    "新月无痕3.0/ini/save/game.ini",
}
MINIZ_RUNTIME_SOURCES = (
    "ThirdParty/miniz/miniz.c",
    "ThirdParty/miniz/miniz_zip.c",
    "ThirdParty/miniz/miniz_tinfl.c",
    "ThirdParty/miniz/miniz_tdef.c",
)
APPLE_SHARED_SOURCE_NAMES = {"AppleHttpsDownload.mm"}
MACOS_ONLY_SOURCE_NAMES = {"MacProgramUpdate.mm"}
APPLE_ONLY_REFERENCE_NAMES = (
    APPLE_SHARED_SOURCE_NAMES
    | MACOS_ONLY_SOURCE_NAMES
    | {"AppleHttpsDownload.h", "MacProgramUpdate.h"}
)


def production_sources(root: Path) -> list[str]:
    result: list[str] = []
    for path in (root / "src").rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        relative = path.relative_to(root)
        if len(relative.parts) >= 2 and relative.parts[1] in {"tests", "automation"}:
            continue
        result.append(relative.as_posix())
    return sorted(result)


def normalize_visual_studio_path(value: str) -> str:
    normalized = value.replace("\\", "/")
    while normalized.startswith("../"):
        normalized = normalized[3:]
    return normalized


def visual_studio_sources(project_text: str) -> set[str]:
    entries = re.findall(
        r'<ClCompile Include="([^\"]+\.(?:c|cpp))"', project_text, re.IGNORECASE
    )
    return {
        normalized
        for entry in entries
        if (normalized := normalize_visual_studio_path(entry)).startswith("src/")
    }


def report_set_difference(
    label: str, expected: set[str], actual: set[str], errors: list[str]
) -> None:
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    if missing:
        errors.append(f"{label} missing: {', '.join(missing)}")
    if extra:
        errors.append(f"{label} extra: {', '.join(extra)}")


def gradle_string_list(
    text: str, variable_name: str, errors: list[str]
) -> list[str]:
    match = re.search(
        rf"\bdef\s+{re.escape(variable_name)}\s*=\s*\[(.*?)\](?:\s+as\s+\w+)?",
        text,
        re.DOTALL,
    )
    if match is None:
        errors.append(f"Android Gradle list is missing: {variable_name}")
        return []
    return re.findall(r"'([^']+)'", match.group(1))


def check_android_asset_exclusion_contract(root: Path, errors: list[str]) -> None:
    gradle_path = root / "android/app/build.gradle"
    gradle_text = gradle_path.read_text(encoding="utf-8")
    common_excludes = gradle_string_list(
        gradle_text, "androidAssetExcludes", errors
    )
    if len(common_excludes) != len(set(common_excludes)):
        errors.append("Android common asset exclusions contain duplicate patterns")
    report_set_difference(
        "Android common asset exclusions",
        ANDROID_ASSET_EXCLUDES,
        set(common_excludes),
        errors,
    )

    for forbidden_pattern in (
        "**/save/**",
        "**/*.ini",
        "**/*.txt",
        "**/*.obj",
    ):
        if forbidden_pattern in common_excludes:
            errors.append(
                "Android common asset exclusions contain unsafe broad pattern: "
                + forbidden_pattern
            )

    required_gradle_contracts = (
        "def keepOnlyReleaseBootstrapAssets = { File assetsDirectory ->",
        "entry.name != 'engine' && entry.name != 'resources.ini'",
        "relativePath != 'resources.ini'",
        "!relativePath.startsWith('engine/')",
        "debug {",
        "androidAssetExcludes.each { pattern ->",
        "release {",
        "def variantAssetExcludes = androidAssetExcludes.toList()",
        "def isReleaseVariant = variant.buildType == 'release'",
        "keepOnlyReleaseBootstrapAssets(",
        "def excludedAssetEntries = findAndroidAssetEntries(",
    )
    for expected in required_gradle_contracts:
        if expected not in gradle_text:
            errors.append(
                "Android asset exclusion contract is missing Gradle behavior: "
                + expected
            )

    resource_manager_text = (
        root / "src/Resource/ResourceManager.cpp"
    ).read_text(encoding="utf-8")
    if (
        'RecentResourceSelectionFile[] = '
        '"save/system/resource_selection.ini"'
        not in resource_manager_text
    ):
        errors.append(
            "Android asset exclusion contract cannot locate the recent "
            "resource-selection record"
        )


    for relative_path in sorted(FORMAL_INITIAL_SAVE_TEMPLATES):
        if not (root / "assets" / relative_path).is_file():
            errors.append(
                "Canonical initial-save template is missing: "
                + relative_path
            )


def check_runtime_resource_transport_contract(
    root: Path, errors: list[str]
) -> None:
    file_source = (root / "src/File/File.cpp").read_text(encoding="utf-8")
    android_root_mode = re.search(
        r"#if defined\(__ANDROID__\).*?"
        r'constexpr char AssetsPath\[\] = "";.*?'
        r"PlatformBundledRootMode\s*=\s*"
        r"\n?\s*ResourceReadPrefixPolicy::BundledRootMode::"
        r"AndroidAssetNamespace;",
        file_source,
        re.DOTALL,
    )
    if android_root_mode is None:
        errors.append(
            "Android runtime resource routing does not preserve the empty "
            "APK asset namespace root"
        )
    if (
        "ResourceReadPrefixPolicy::appendPrimaryPrefix("
        not in file_source
    ):
        errors.append(
            "Runtime resource routing does not apply the platform bundled-root policy"
        )

    policy_source = (
        root / "src/File/ResourceReadPrefixPolicy.h"
    ).read_text(encoding="utf-8")
    for expected in (
        "FilesystemPath",
        "AndroidAssetNamespace",
        "rootMode == BundledRootMode::AndroidAssetNamespace",
    ):
        if expected not in policy_source:
            errors.append(
                "Runtime resource prefix policy is missing behavior: "
                + expected
            )

    resource_manager_source = (
        root / "src/Resource/ResourceManager.cpp"
    ).read_text(encoding="utf-8")
    if re.search(
        r"\bstd::ifstream\s+\w+\s*\(",
        resource_manager_source,
    ):
        errors.append(
            "ResourceManager must not use std::ifstream for packaged manifests"
        )
    for expected in (
        "readPackagedCatalogFile(",
        'SDL_IOFromFile(fullPathText.c_str(), "rb")',
        "SDL_GetIOSize(input.get())",
        "fileAccess.readFileFromRoot = readPackagedCatalogFile",
    ):
        if expected not in resource_manager_source:
            errors.append(
                "Packaged manifest transport is missing behavior: "
                + expected
            )

    resource_catalog_source = (
        root / "src/Resource/ResourceCatalog.cpp"
    ).read_text(encoding="utf-8")
    for expected in (
        "MaximumCatalogIniFileBytes",
        "catalog.fileAccess->readFileFromRoot(",
        "manifest.loadFromBuffer(",
    ):
        if expected not in resource_catalog_source:
            errors.append(
                "Resource catalog manifest parsing is missing behavior: "
                + expected
            )


def check_android_external_storage_bridge_contract(
    root: Path, errors: list[str]
) -> None:
    activity_source = (
        root
        / "android/app/src/main/java/com/upwinded/jxqy/JxqyActivity.java"
    ).read_text(encoding="utf-8")
    required_activity_behavior = (
        "activity.runOnUiThread(",
        "requestAllFilesAccessOnUiThread(activity)",
        "activity.requestPermissions(",
        "activity.startActivity(intent)",
        "consumeAllFilesAccessRequestCompleted()",
        "onPause()",
        "onResume()",
        "onWindowFocusChanged(boolean hasFocus)",
        "onRequestPermissionsResult(",
    )
    for expected in required_activity_behavior:
        if expected not in activity_source:
            errors.append(
                "Android external-storage Activity bridge is missing behavior: "
                + expected
            )

    settings_actions = (
        "Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION",
        "Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION",
        "Settings.ACTION_APPLICATION_DETAILS_SETTINGS",
    )
    action_positions = [activity_source.find(action) for action in settings_actions]
    if any(position < 0 for position in action_positions) or action_positions != sorted(
        action_positions
    ):
        errors.append(
            "Android external-storage settings fallbacks are missing or out of order"
        )

    native_source = (
        root / "src/Platform/AndroidExternalStorage.cpp"
    ).read_text(encoding="utf-8")
    required_native_behavior = (
        "SDL_GetAndroidActivity()",
        "GetObjectClass(activity)",
        "std::mutex",
        "std::lock_guard<std::mutex>",
        "std::atomic<bool>",
        '"consumeAllFilesAccessRequestCompleted", "()Z"',
        "bool consumeAllFilesAccessRequestCompleted()",
    )
    for expected in required_native_behavior:
        if expected not in native_source:
            errors.append(
                "Android external-storage native bridge is missing behavior: "
                + expected
            )
    if "->FindClass(" in native_source:
        errors.append(
            "Android external-storage native bridge must use the SDL Activity class loader"
        )
    if "com/upwinded/jxqy/JxqyActivity" in native_source:
        errors.append(
            "Android external-storage native bridge must not hard-code the Activity class"
        )

    native_header = (
        root / "src/Platform/AndroidExternalStorage.h"
    ).read_text(encoding="utf-8")
    if "bool consumeAllFilesAccessRequestCompleted();" not in native_header:
        errors.append(
            "Android external-storage header does not expose request completion"
        )


def check_cmake_source_exclusions(path: Path, errors: list[str]) -> None:
    text = path.read_text(encoding="utf-8").lower()
    source_glob = re.search(
        r"file\s*\(\s*glob_recurse\s+source_files\s+configure_depends\b",
        text,
        re.DOTALL,
    )
    if source_glob is None:
        errors.append(
            f"{path.as_posix()} runtime source glob does not use CONFIGURE_DEPENDS"
        )
    if "android/app/jni/src" in path.as_posix().lower():
        for suffix in ("*.cpp", "*.c"):
            expected = (
                "${cmake_current_source_dir}/../../../../src/" + suffix
            )
            if expected not in text:
                errors.append(
                    f"{path.as_posix()} does not explicitly select Android "
                    f"runtime {suffix} sources"
                )
        if re.search(
            r"\$\{cmake_current_source_dir\}/\.\./\.\./\.\./\.\./src/\*\s",
            text,
        ):
            errors.append(
                f"{path.as_posix()} broad src glob includes Apple-only sources"
            )
    for variable in ("src_test_files", "src_automation_files"):
        declaration = re.search(
            rf"file\s*\(\s*glob_recurse\s+{variable}\b", text, re.DOTALL
        )
        removal = re.search(
            rf"list\s*\(\s*remove_item\s+source_files\s+\$\{{{variable}\}}\s*\)",
            text,
            re.DOTALL,
        )
        if declaration is None or removal is None:
            errors.append(f"{path.as_posix()} does not exclude {variable}")


def check_engine_version_contract(root: Path, errors: list[str]) -> None:
    literal_path = root / "cmake/JxqyEngineVersion.inc"
    literal = literal_path.read_text(encoding="utf-8").strip()
    literal_match = re.fullmatch(
        r'"([0-9]+\.[0-9]+\.[0-9]+)"',
        literal,
    )
    if literal_match is None:
        errors.append(
            "cmake/JxqyEngineVersion.inc is not one quoted major.minor.patch value"
        )
        return
    engine_version = literal_match.group(1)

    module_text = (root / "cmake/JxqyEngineVersion.cmake").read_text(
        encoding="utf-8"
    )
    if "JxqyEngineVersion.inc" not in module_text:
        errors.append("CMake engine-version module does not read the shared literal")

    for path in (
        root / "CMakeLists.txt",
        root / "jxqy-editor/CMakeLists.txt",
    ):
        if "JxqyEngineVersion.cmake" not in path.read_text(encoding="utf-8"):
            errors.append(f"{path.as_posix()} does not include the engine-version module")

    root_cmake_text = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    if "win/jxqy-all-in-one/jxqy-all-in-one.rc" not in root_cmake_text:
        errors.append("Windows CMake target does not compile the version resource")
    if "win/jxqy-starter/jxqy-starter.rc" not in root_cmake_text:
        errors.append("Windows starter target does not compile its version resource")

    runtime_header = (root / "src/JxqyEngineVersion.h").read_text(encoding="utf-8")
    if "../cmake/JxqyEngineVersion.inc" not in runtime_header:
        errors.append("runtime engine-version header does not include the shared literal")
    if "ProgramUpdateTarget" not in runtime_header:
        errors.append("runtime build identity does not declare a program update target")
    for platform_target in ("windows", "android", "macos", "ios", "linux"):
        if f'"{platform_target}"' not in runtime_header:
            errors.append(
                f"runtime build identity is missing platform target {platform_target}"
            )

    apple_project_text = (
        root / "macos_ios/jxqy/jxqy.xcodeproj/project.pbxproj"
    ).read_text(encoding="utf-8")
    if 'ARCHS = "arm64 x86_64";' not in apple_project_text:
        errors.append("macOS Release target does not build a Universal 2 application")
    for path in (
        root / "src/Resource/ResourceManager.cpp",
        root / "src/Resource/ResourceSelectScene.cpp",
        root / "jxqy-editor/core/BuildVersion.cpp",
    ):
        if "JxqyEngineVersion.h" not in path.read_text(encoding="utf-8"):
            errors.append(f"{path.as_posix()} does not use the shared runtime header")

    android_gradle_text = (root / "android/app/build.gradle").read_text(
        encoding="utf-8"
    )
    android_version_match = re.search(
        r'^def appVersion = "([^"]+)"$',
        android_gradle_text,
        re.MULTILINE,
    )
    if (
        android_version_match is None
        or android_version_match.group(1) != engine_version
    ):
        errors.append("Android appVersion does not match the shared engine version")
    if "versionCode appVersionCode" not in android_gradle_text or \
            "appVersionParts[0] * 1_000_000" not in android_gradle_text or \
            "appVersionCode > 2_100_000_000" not in android_gradle_text:
        errors.append(
            "Android versionCode is not derived from the public semantic version"
        )

    private_build_root = root / "private-build"
    if private_build_root.is_dir():
        application_catalog_text = (
            private_build_root / "application/catalog.ini"
        ).read_text(encoding="utf-8")
        if "SchemaVersion=1" not in application_catalog_text:
            errors.append("application catalog does not use the current schema 1")
    android_manifest_text = (
        root / "android/app/src/main/AndroidManifest.xml"
    ).read_text(encoding="utf-8")
    if re.search(r"android:version(?:Code|Name)=", android_manifest_text):
        errors.append(
            "Android manifest duplicates versionCode/versionName from Gradle"
        )

    windows_numeric_version = engine_version.replace(".", ",") + ",0"
    windows_string_version = engine_version + ".0"
    for windows_resource_path in (
        root / "win/jxqy-all-in-one/jxqy-all-in-one.rc",
        root / "win/jxqy-starter/jxqy-starter.rc",
    ):
        windows_resource_text = windows_resource_path.read_text(
            encoding="utf-8"
        )
        for expected in (
            f" FILEVERSION {windows_numeric_version}",
            f" PRODUCTVERSION {windows_numeric_version}",
            f'VALUE "FileVersion", "{windows_string_version}"',
            f'VALUE "ProductVersion", "{windows_string_version}"',
        ):
            if expected not in windows_resource_text:
                errors.append(
                    f"{windows_resource_path.as_posix()} does not contain "
                    f"expected version value: {expected}"
                )

    xcode_text = (
        root / "macos_ios/jxqy/jxqy.xcodeproj/project.pbxproj"
    ).read_text(encoding="utf-8")
    marketing_versions = re.findall(
        r"\bMARKETING_VERSION = ([^;]+);",
        xcode_text,
    )
    if len(marketing_versions) != 4 or any(
        version != engine_version for version in marketing_versions
    ):
        errors.append(
            "Apple MARKETING_VERSION entries do not all match the shared engine version"
        )
    apple_bundle_versions = re.findall(
        r"\bCURRENT_PROJECT_VERSION = ([^;]+);",
        xcode_text,
    )
    if len(apple_bundle_versions) != 4 or any(
        value != engine_version for value in apple_bundle_versions
    ):
        errors.append(
            "Apple CURRENT_PROJECT_VERSION entries do not match the public version"
        )


def check_application_identity_contract(root: Path, errors: list[str]) -> None:
    root_cmake_text = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    expected_project_name = f'set(PROJECT_NAME "{ARTIFACT_NAME}")'
    if expected_project_name not in root_cmake_text:
        errors.append(
            f"CMakeLists.txt does not use product artifact name {ARTIFACT_NAME}"
        )
    linux_launcher_text = (root / "run(put this out of bin).sh").read_text(
        encoding="utf-8"
    )
    if f'executable="{ARTIFACT_NAME}"' not in linux_launcher_text:
        errors.append("Linux launcher does not select the canonical product artifact")
    if "JXQY2" in linux_launcher_text or "for executable in" in linux_launcher_text:
        errors.append("Linux launcher still accepts a legacy product artifact name")

    updater_sources = (
        "updater/main.cpp",
        "updater/DesktopProgramUpdater.cpp",
    )
    root_cmake_normalized = root_cmake_text.replace("\\", "/")
    for source in updater_sources:
        if source not in root_cmake_normalized:
            errors.append(f"desktop program updater CMake target misses {source}")
    updater_project = (
        root / "win/jxqy-program-updater/jxqy-program-updater.vcxproj"
    )
    updater_project_text = updater_project.read_text(encoding="utf-8")
    updater_project_normalized = updater_project_text.replace("\\", "/")
    for source in updater_sources:
        project_source = "../../" + source
        if project_source not in updater_project_normalized:
            errors.append(f"Visual Studio updater project misses {source}")
    for output_directory in (
        "bin/updater/win32",
        "bin/updater/win64",
    ):
        if output_directory not in updater_project_normalized:
            errors.append(
                f"Visual Studio updater project misses {output_directory} output"
            )
    solution_text = (root / "win/jxqy-all-in-one.sln").read_text(
        encoding="utf-8"
    )
    if "jxqy-program-updater\\jxqy-program-updater.vcxproj" not in solution_text:
        errors.append("Visual Studio solution does not include the program updater")

    android_gradle_text = (root / "android/app/build.gradle").read_text(
        encoding="utf-8"
    )
    android_expected_values = (
        f'def artifactName = "{ARTIFACT_NAME}";',
        f'def displayName = "{DISPLAY_NAME}";',
        f'def applicationIdentifier = "{ANDROID_APPLICATION_ID}";',
        'resValue "string", "app_name", displayName',
        'def outputAPKName = "${artifactName}_${variant.name}_v${appVersion}.apk";',
    )
    for expected in android_expected_values:
        if expected not in android_gradle_text:
            errors.append(f"Android identity does not contain expected value: {expected}")
    if "androidComponents" not in android_gradle_text:
        errors.append("Android build does not use the public variant API")
    if "applicationVariants" in android_gradle_text:
        errors.append("Android build still uses the legacy variant API")
    if re.search(r"\babortOnError\s*(?:=\s*)?false\b", android_gradle_text):
        errors.append("Android lint errors are configured to be ignored")
    if "appNameCN" in android_gradle_text or "app_name_cn" in android_gradle_text:
        errors.append("Android identity still contains the legacy localized app-name key")

    android_gradle_properties = (root / "android/gradle.properties").read_text(
        encoding="utf-8"
    )
    if "android.newDsl=false" in android_gradle_properties:
        errors.append("Android build still opts out of the public AGP DSL")

    android_manifest_text = (
        root / "android/app/src/main/AndroidManifest.xml"
    ).read_text(encoding="utf-8")
    if 'android:label="@string/app_name"' not in android_manifest_text:
        errors.append("Android manifest does not use the canonical app_name resource")

    windows_resource_text = (
        root / "win/jxqy-all-in-one/jxqy-all-in-one.rc"
    ).read_text(encoding="utf-8")
    if "#pragma code_page(65001)" not in windows_resource_text:
        errors.append("Windows resource file does not declare UTF-8 source encoding")
    if f'VALUE "ProductName", "{DISPLAY_NAME}"' not in windows_resource_text:
        errors.append("Windows ProductName does not use the canonical display name")

    file_source_text = (root / "src/File/File.cpp").read_text(encoding="utf-8")
    preference_values = (
        f'#define PREF_PATH_ORGANIZATION_NAME "{PREFERENCE_ORGANIZATION_NAME}"',
        f'#define PREF_PATH_APPLICATION_NAME "{PREFERENCE_APPLICATION_NAME}"',
    )
    for expected in preference_values:
        if expected not in file_source_text:
            errors.append(f"Apple preference identity is missing: {expected}")
    if "SDL_GetPrefPath(\n" not in file_source_text:
        errors.append("Apple writable path does not use SDL_GetPrefPath")

    game_header_text = (root / "src/Game/Game.h").read_text(encoding="utf-8")
    if f'u8"{DISPLAY_NAME}"' not in game_header_text:
        errors.append("Game window title does not use the canonical display name")
    resource_select_text = (
        root / "src/Resource/ResourceSelectScene.cpp"
    ).read_text(encoding="utf-8")
    if f'u8"{DISPLAY_NAME}"' not in resource_select_text:
        errors.append("Resource selection title does not use the canonical display name")

    xcode_text = (
        root / "macos_ios/jxqy/jxqy.xcodeproj/project.pbxproj"
    ).read_text(encoding="utf-8")
    apple_display_names = re.findall(
        r'\bINFOPLIST_KEY_CFBundleDisplayName = "([^"]+)";',
        xcode_text,
    )
    if len(apple_display_names) != 4 or any(
        name != DISPLAY_NAME for name in apple_display_names
    ):
        errors.append("Apple display-name entries are not canonical for all configurations")

    apple_product_names = re.findall(
        r'\bPRODUCT_NAME = "?([^";]+)"?;',
        xcode_text,
    )
    if len(apple_product_names) != 4 or any(
        name != ARTIFACT_NAME for name in apple_product_names
    ):
        errors.append("Apple product-name entries are not canonical for all configurations")

    apple_bundle_identifiers = re.findall(
        r'\bPRODUCT_BUNDLE_IDENTIFIER = "?([^";]+)"?;',
        xcode_text,
    )
    if len(apple_bundle_identifiers) != 4 or any(
        identifier != APPLE_BUNDLE_IDENTIFIER
        for identifier in apple_bundle_identifiers
    ):
        errors.append("Apple bundle identifiers are not canonical for all configurations")

    apple_app_name = f"{ARTIFACT_NAME}.app"
    if xcode_text.count(f"path = {apple_app_name};") != 2:
        errors.append("Apple product references do not use the canonical app bundle name")
    for scheme_name in ("jxqy iOS.xcscheme", "jxqy macOS.xcscheme"):
        scheme_text = (
            root
            / "macos_ios/jxqy/jxqy.xcodeproj/xcshareddata/xcschemes"
            / scheme_name
        ).read_text(encoding="utf-8")
        if scheme_text.count(f'BuildableName = "{apple_app_name}"') != 3:
            errors.append(
                f"Apple scheme {scheme_name} does not use the canonical app bundle name"
            )


def check_miniz_runtime_source_contract(root: Path, errors: list[str]) -> None:
    if not (root / "cmake/miniz_export.h").is_file():
        errors.append("Tracked miniz_export.h is missing for manual platform projects")

    root_cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    android_cmake = (
        root / "android/app/jni/src/CMakeLists.txt"
    ).read_text(encoding="utf-8")
    visual_studio = (
        root / "win/jxqy-all-in-one/jxqy-all-in-one.vcxproj"
    ).read_text(encoding="utf-8")
    xcode = (
        root / "macos_ios/jxqy/jxqy.xcodeproj/project.pbxproj"
    ).read_text(encoding="utf-8")

    for relative_path in MINIZ_RUNTIME_SOURCES:
        name = Path(relative_path).name
        if f"${{THIRD_PARTY_DIR}}/miniz/{name}" not in root_cmake:
            errors.append(f"Root CMake miniz source missing: {name}")
        if f"ThirdParty/miniz/{name}" not in android_cmake:
            errors.append(f"Android CMake miniz source missing: {name}")
        visual_studio_path = relative_path.replace("/", "\\")
        if f'Include="..\\..\\{visual_studio_path}"' not in visual_studio:
            errors.append(f"Visual Studio miniz source missing: {name}")
        if xcode.count(f"/* {name} in Sources */") != 4:
            errors.append(
                f"Xcode miniz source is not present in both targets: {name}"
            )

    if xcode.count("../../ThirdParty/miniz,") != 4 or xcode.count(
        "../../cmake,"
    ) != 4:
        errors.append("Xcode miniz header search paths are incomplete")


def check_windows_https_transport_contract(root: Path, errors: list[str]) -> None:
    root_cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    visual_studio = (
        root / "win/jxqy-all-in-one/jxqy-all-in-one.vcxproj"
    ).read_text(encoding="utf-8")
    runtime_link = re.search(
        r"target_link_libraries\(\$\{PROJECT_NAME\}(.*?)\n\s*\)",
        root_cmake,
        re.DOTALL,
    )
    if runtime_link is None or not re.search(
        r"\bwinhttp\b", runtime_link.group(1)
    ):
        errors.append("Root CMake Windows runtime does not link WinHTTP")
    test_link = re.search(
        r"target_link_libraries\(jxqy-https-download-tests\s+"
        r"PRIVATE\s+winhttp\s*\)",
        root_cmake,
    )
    if test_link is None:
        errors.append("Root CMake HTTPS download test does not link WinHTTP")
    if visual_studio.count("winhttp.lib;") != 4:
        errors.append("Visual Studio configurations do not all link winhttp.lib")


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    private_build_root = root / "private-build"
    expected_sources = set(production_sources(root))
    errors: list[str] = []
    check_engine_version_contract(root, errors)
    check_application_identity_contract(root, errors)
    check_android_asset_exclusion_contract(root, errors)
    check_runtime_resource_transport_contract(root, errors)
    check_android_external_storage_bridge_contract(root, errors)
    check_miniz_runtime_source_contract(root, errors)
    check_windows_https_transport_contract(root, errors)

    basenames = [Path(path).name for path in expected_sources]
    duplicate_basenames = sorted(
        name for name in set(basenames) if basenames.count(name) > 1
    )
    if duplicate_basenames:
        errors.append(
            "runtime source basenames are not unique for Xcode validation: "
            + ", ".join(duplicate_basenames)
        )

    visual_studio_project = root / "win/jxqy-all-in-one/jxqy-all-in-one.vcxproj"
    visual_studio_filters = visual_studio_project.with_suffix(".vcxproj.filters")
    report_set_difference(
        "Visual Studio project",
        expected_sources,
        visual_studio_sources(visual_studio_project.read_text(encoding="utf-8")),
        errors,
    )
    report_set_difference(
        "Visual Studio filters",
        expected_sources,
        visual_studio_sources(visual_studio_filters.read_text(encoding="utf-8")),
        errors,
    )

    xcode_project = root / "macos_ios/jxqy/jxqy.xcodeproj/project.pbxproj"
    xcode_text = xcode_project.read_text(encoding="utf-8")
    xcode_reference_names = set(
        Path(value.strip('"')).name
        for value in re.findall(
            r"PBXFileReference;[^\n]*path = ([^;]+\.(?:c|cpp|mm|h));",
            xcode_text,
            re.IGNORECASE,
        )
    )
    expected_source_names = set(basenames) | APPLE_SHARED_SOURCE_NAMES
    expected_reference_names = set(basenames) | APPLE_ONLY_REFERENCE_NAMES
    missing_references = sorted(
        expected_reference_names - xcode_reference_names
    )
    if missing_references:
        errors.append("Xcode file references missing: " + ", ".join(missing_references))

    source_section_match = re.search(
        r"/\* Begin PBXSourcesBuildPhase section \*/(.*?)/\* End PBXSourcesBuildPhase section \*/",
        xcode_text,
        re.DOTALL,
    )
    source_phases = (
        re.findall(
            r"isa = PBXSourcesBuildPhase;.*?files = \((.*?)\);\s*runOnlyForDeploymentPostprocessing",
            source_section_match.group(1),
            re.DOTALL,
        )
        if source_section_match is not None
        else []
    )
    if len(source_phases) != 2:
        errors.append(f"Xcode expected 2 source phases, found {len(source_phases)}")
    for index, phase in enumerate(source_phases, start=1):
        phase_names = set(
            re.findall(r"/\* ([^*/]+\.(?:c|cpp|mm)) in Sources \*/", phase)
        )
        phase_expected = set(expected_source_names)
        if index == 2:
            phase_expected |= MACOS_ONLY_SOURCE_NAMES
        missing = sorted(phase_expected - phase_names)
        if missing:
            errors.append(
                f"Xcode source phase {index} missing: " + ", ".join(missing)
            )
        if index == 1:
            unexpected_macos_sources = sorted(MACOS_ONLY_SOURCE_NAMES & phase_names)
            if unexpected_macos_sources:
                errors.append(
                    "Xcode iOS source phase contains macOS-only sources: "
                    + ", ".join(unexpected_macos_sources)
                )

    definition_ids = re.findall(
        r"^\s*([A-F0-9]{24}) /\*.*?\*/ = \{isa = ", xcode_text, re.MULTILINE
    )
    duplicate_ids = sorted(
        identifier
        for identifier in set(definition_ids)
        if definition_ids.count(identifier) > 1
    )
    if duplicate_ids:
        errors.append("Xcode duplicate object IDs: " + ", ".join(duplicate_ids))
    if xcode_text.count("{") != xcode_text.count("}"):
        errors.append("Xcode project braces are unbalanced")

    for expected, expected_count in (
        ('name = "Prepare Runtime Assets";', 2),
        ('if [ \\"${CONFIGURATION}\\" = \\"Release\\" ]; then', 2),
        ('! -name engine ! -name resources.ini', 2),
        ('/usr/bin/ditto \\"${ASSETS_SOURCE}/engine\\"', 2),
        ('Release bootstrap assets are incomplete', 2),
    ):
        if xcode_text.count(expected) != expected_count:
            errors.append(
                "Apple release bootstrap contract is missing from both targets: "
                + expected
            )
    if "/* assets in Resources */" in xcode_text:
        errors.append(
            "Apple targets must stage runtime assets conditionally instead of "
            "copying the complete assets collection"
        )
    if xcode_text.count("ENABLE_HARDENED_RUNTIME = YES;") != 1:
        errors.append("macOS Release must enable hardened runtime")
    if (
        xcode_text.count(
            "path = System/Library/Frameworks/Foundation.framework; "
            "sourceTree = SDKROOT;"
        )
        != 1
        or xcode_text.count(
            "Foundation.framework in Frameworks"
        )
        != 4
    ):
        errors.append("macOS target does not explicitly link Foundation.framework")

    for expected in (
        'repositoryURL = "https://github.com/sparkle-project/Sparkle";',
        "kind = exactVersion;",
        "version = 2.9.6;",
        "productName = Sparkle;",
        'name = "Configure Sparkle Updates";',
        'INFOPLIST_FILE = "jxqy-macOS-Info.plist";',
    ):
        if expected not in xcode_text:
            errors.append("macOS Sparkle contract is missing: " + expected)
    if xcode_text.count("Sparkle in Frameworks") != 2:
        errors.append("macOS target does not link the Sparkle package product exactly once")
    if xcode_text.count('INFOPLIST_FILE = "jxqy-macOS-Info.plist";') != 2:
        errors.append("macOS Debug and Release do not share the Sparkle Info.plist")
    if "Release requires JXQY_SPARKLE_PUBLIC_ED_KEY" in xcode_text:
        errors.append(
            "ordinary macOS Release builds must work without a publisher identity"
        )

    macos_info_plist = (
        root / "macos_ios/jxqy/jxqy-macOS-Info.plist"
    ).read_text(encoding="utf-8")
    for expected in (
        "<key>SUEnableAutomaticChecks</key>\n\t<false/>",
        "<key>SUEnableInstallerLauncherService</key>\n\t<true/>",
        "<key>SUPublicEDKey</key>\n\t<string>$(JXQY_SPARKLE_PUBLIC_ED_KEY)</string>",
    ):
        if expected not in macos_info_plist:
            errors.append("macOS Sparkle Info.plist contract is missing: " + expected)

    mac_program_update = (
        root / "src/Platform/MacProgramUpdate.mm"
    ).read_text(encoding="utf-8")
    resource_select_scene = (
        root / "src/Resource/ResourceSelectScene.cpp"
    ).read_text(encoding="utf-8")
    for expected in (
        "bool isConfigured()",
        'objectForInfoDictionaryKey:@"SUPublicEDKey"',
        "decodedKey.length == 32",
    ):
        if expected not in mac_program_update:
            errors.append("macOS external Sparkle identity guard is missing: " + expected)
    if "MacProgramUpdate::isConfigured()" not in resource_select_scene:
        errors.append(
            "macOS program update UI is not disabled when no public key is injected"
        )

    if private_build_root.is_dir():
        private_release_scripts = (
            "package_apple_release.py",
            "package_android_release.py",
            "package_desktop_release.py",
            "package_third_party_dependencies.py",
            "update_version.py",
        )
        for script_name in private_release_scripts:
            if not (private_build_root / "scripts" / script_name).is_file():
                errors.append(
                    "private release script is missing: " + script_name
                )

        apple_packaging = (
            private_build_root / "scripts/package_apple_release.py"
        ).read_text(encoding="utf-8")
        for expected in (
            'SPARKLE_PUBLIC_KEY_FILE_ENV = "JXQY_SPARKLE_PUBLIC_KEY_FILE"',
            'APPLICATION_DOWNLOAD_URL_PREFIX_ENV = '
            '"JXQY_APPLICATION_DOWNLOAD_URL_PREFIX"',
            '"--sparkle-public-key-file"',
            '"--application-download-url-prefix"',
            '"private-build/identity/SparklePublicEDKey.txt"',
        ):
            if expected not in apple_packaging:
                errors.append(
                    "Apple private release configuration is missing: " + expected
                )
        if "cnb.cool/upwinded/jxqy-all-in-one" in apple_packaging:
            errors.append("Apple packaging must not hard-code the official release URL")
        if not (
            private_build_root / "identity/SparklePublicEDKey.txt"
        ).is_file():
            errors.append("private Apple release public key is missing")

        android_packaging = (
            private_build_root / "scripts/package_android_release.py"
        ).read_text(encoding="utf-8")
        for expected in (
            '"private-build/secrets/android-release.properties"',
            '"private-build/identity/AndroidSigningCertificateSha256.txt"',
            '"JXQY_ANDROID_KEYSTORE"',
            '"JXQY_ANDROID_STORE_PASSWORD"',
            '"JXQY_ANDROID_KEY_ALIAS"',
            '"JXQY_ANDROID_KEY_PASSWORD"',
            '"jxqy-all-in-one-android-universal-{version}.apk"',
            'EXPECTED_ABIS = {"arm64-v8a", "x86_64"}',
        ):
            if expected not in android_packaging:
                errors.append(
                    "Android private release configuration is missing: " + expected
                )
        if not (
            private_build_root / "identity/AndroidSigningCertificateSha256.txt"
        ).is_file():
            errors.append("private Android release certificate fingerprint is missing")

        desktop_packaging = (
            private_build_root / "scripts/package_desktop_release.py"
        ).read_text(encoding="utf-8")
        for expected in (
            '"windows": "win32"',
            '"linux": "linux"',
            '"windows": "x86"',
            '"linux": "x86_64"',
            'return f"jxqy-program-{platform}-{architecture}-{version}.zip"',
        ):
            if expected not in desktop_packaging:
                errors.append(
                    "desktop private release configuration is missing: " + expected
                )
        dependency_packaging = (
            private_build_root / "scripts/package_third_party_dependencies.py"
        ).read_text(encoding="utf-8")
        for expected in (
            '"linux": "linux"',
            '"linux": "Linux"',
            '"linux": "x86_64"',
        ):
            if expected not in dependency_packaging:
                errors.append(
                    "dependency private release configuration is missing: "
                    + expected
                )
    if (root / "macos_ios/jxqy/SparklePublicEDKey.txt").exists():
        errors.append("legacy Sparkle public-key path must remain unused")

    macos_entitlements = (
        root / "macos_ios/jxqy/jxqy macOS/jxqy_macOS.entitlements"
    ).read_text(encoding="utf-8")
    if (
        "<key>com.apple.security.network.client</key>\n\t<true/>"
        not in macos_entitlements
    ):
        errors.append(
            "macOS sandbox does not allow the HTTPS client's outgoing network access"
        )
    for expected in (
        "<key>com.apple.security.temporary-exception.mach-lookup.global-name</key>",
        "<string>$(PRODUCT_BUNDLE_IDENTIFIER)-spks</string>",
        "<string>$(PRODUCT_BUNDLE_IDENTIFIER)-spki</string>",
    ):
        if expected not in macos_entitlements:
            errors.append("macOS Sparkle sandbox entitlement is missing: " + expected)

    for cmake_path in (
        root / "CMakeLists.txt",
        root / "android/app/jni/src/CMakeLists.txt",
    ):
        check_cmake_source_exclusions(cmake_path, errors)

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print(
        "Platform project source contracts passed: "
        f"{len(expected_sources)} shared runtime sources, "
        f"{len(APPLE_SHARED_SOURCE_NAMES)} Apple-shared source, "
        f"{len(MACOS_ONLY_SOURCE_NAMES)} macOS-only source, 2 Xcode targets."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
