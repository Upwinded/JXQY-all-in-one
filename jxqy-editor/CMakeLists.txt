cmake_minimum_required(VERSION 3.15)

set(PROJECT_NAME "jxqy-editor")
set(JXQY_EDITOR_PRODUCT_VERSION 1.0.0)
include(../cmake/JxqyEngineVersion.cmake)

project(${PROJECT_NAME} VERSION ${JXQY_EDITOR_PRODUCT_VERSION} LANGUAGES C CXX)

set(JXQY_EDITOR_GENERATED_INCLUDE_DIRECTORY
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${JXQY_EDITOR_GENERATED_INCLUDE_DIRECTORY}")
configure_file(
    cmake/JxqyEditorBuildVersion.h.in
    "${JXQY_EDITOR_GENERATED_INCLUDE_DIRECTORY}/JxqyEditorBuildVersion.h"
    @ONLY
)
configure_file(
    ../cmake/miniz_export.h.in
    "${JXQY_EDITOR_GENERATED_INCLUDE_DIRECTORY}/miniz_export.h"
    @ONLY
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(JXQY_EDITOR_BUILD_BENCHMARK
    "Build the editor performance benchmark and sampling hooks"
    OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_options($<$<CONFIG:Release>:/O2>)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wno-multichar)
    add_compile_options($<$<CONFIG:Release>:-O2>)
endif()

if(WIN32)
    add_compile_definitions(WIN32)
endif()

# Allow CMAKE_PREFIX_PATH or Qt6_DIR to specify Qt location; do not force a default
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia LinguistTools)
if(NOT WIN32)
    find_package(Iconv REQUIRED)
endif()

include(GNUInstallDirs)
include(cmake/ConfigureDesktopIntegration.cmake)

set(JXQY_EDITOR_DESKTOP_EXECUTABLE_NAME "${PROJECT_NAME}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(JXQY_EDITOR_DESKTOP_EXECUTABLE_NAME "${PROJECT_NAME}-debug")
endif()
jxqy_configure_desktop_integration(
    "${CMAKE_CURRENT_BINARY_DIR}/desktop-integration")

set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty)
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)

include(cmake/GenerateStoryGraphRuntimeApiCatalog.cmake)
set(STORY_GRAPH_RUNTIME_API_CATALOG_INCLUDE
    "${JXQY_EDITOR_GENERATED_INCLUDE_DIRECTORY}/StoryGraphRuntimeApiCatalogEntries.inc")
generate_story_graph_runtime_api_catalog(
    "${SRC_DIR}/Game/Script/Script.cpp"
    "${STORY_GRAPH_RUNTIME_API_CATALOG_INCLUDE}")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SRC_DIR}
    ${THIRD_PARTY_DIR}/minilzo
    ${THIRD_PARTY_DIR}/lua
)

set(STORY_GRAPH_CORE_SOURCES
    core/EditorProcessLifecycle.cpp
    core/LuaLexer.cpp
    core/StoryGraphAnalysisCoordinator.cpp
    core/StoryGraphAnalyzer.cpp
    core/StoryGraphLayout.cpp
    core/StoryGraphModel.cpp
    core/StoryGraphProjectResolver.cpp
    core/StoryGraphRuntimeTrace.cpp
    core/StoryGraphRuntimeApiCatalog.cpp
    core/StoryGraphSemanticCatalog.cpp
    core/StoryGraphTraceMatcher.cpp
)

set(STORY_GRAPH_CORE_HEADERS
    core/EditorProcessLifecycle.h
    core/LuaLexer.h
    core/StoryGraphAnalysisCoordinator.h
    core/StoryGraphAnalyzer.h
    core/StoryGraphLayout.h
    core/StoryGraphModel.h
    core/StoryGraphProjectResolver.h
    core/StoryGraphRuntimeTrace.h
    core/StoryGraphRuntimeApiCatalog.h
    core/StoryGraphSemanticCatalog.h
    core/StoryGraphTraceMatcher.h
    ${STORY_GRAPH_RUNTIME_API_CATALOG_INCLUDE}
)

set(CORE_SOURCES
    core/BuildVersion.cpp
    core/EditorApplicationBootstrap.cpp
    ${SRC_DIR}/Resource/SemanticVersion.cpp
    ${SRC_DIR}/Resource/ModReleaseMetadata.cpp
    ${SRC_DIR}/Resource/ResourceCatalog.cpp
    ${SRC_DIR}/Resource/ResourceManifest.cpp
    ${SRC_DIR}/Update/ArtifactChecksum.cpp
    ${SRC_DIR}/Update/OnlineUpdateCatalog.cpp
    ${SRC_DIR}/File/StrictRelativeResourcePath.cpp
    ${SRC_DIR}/File/RootedResourceReader.cpp
    ${SRC_DIR}/Launch/EditorRunDescriptor.cpp
    ${SRC_DIR}/Resource/ModReleaseAssets.cpp
    core/AssetReferenceScanner.cpp
    core/AuthoringMutationGate.cpp
    core/DesktopFileAssociationManager.cpp
    core/DesktopFileOpenEventRouter.cpp
    core/ScriptProjectSearch.cpp
    core/StoryGraphResourceContext.cpp
    core/AssetMigrationPolicy.cpp
    core/AndroidExternalResourcePackager.cpp
    core/OnlineResourcePackageExporter.cpp
    core/OnlineUpdateCatalogPublisher.cpp
    core/AssetPreviewLoader.cpp
    core/LegacyTextDecoder.cpp
    core/LuaFunctionOutline.cpp
    core/ScriptCallTemplate.cpp
    core/ImageFrameBatchExchange.cpp
    core/ImageFrameImport.cpp
    core/ImageFrameImportRecipe.cpp
    core/ImageFrameSequence.cpp
    core/ImageFrameTransform.cpp
    core/PicFileEditor.cpp
    core/ScriptConverter.cpp
    core/LuaScriptSyntaxValidator.cpp
    core/MapConverter.cpp
    core/IMPImageFile.cpp
    core/Util.cpp
    core/ini.cpp
    core/INIFileEditor.cpp
    core/MapFileEditor.cpp
    core/MpcImageCache.cpp
    core/ProjectManager.cpp
    core/ProjectRuntimeConfiguration.cpp
    core/ProjectDocumentRegistry.cpp
    core/DesktopRunDocumentSnapshot.cpp
    core/DesktopRunCurrentTarget.cpp
    core/FocusedContentPlaytest.cpp
    core/SavedSceneLaunchPreparation.cpp
    core/WindowPlacement.cpp
    core/AssetCliRunner.cpp
    core/JxAssetMigrator.cpp
    core/EditorSettings.cpp
    core/DesktopRunController.cpp
    core/DesktopRunSessionBase.cpp
    core/DesktopRunSessionCoordinator.cpp
    core/DesktopRunSessionCleanup.cpp
    core/DesktopRunSessionWorkspace.cpp
    core/SaveEditContext.cpp
    core/TranslationManager.cpp
    core/GameProfile.cpp
    core/MagicDocument.cpp
    core/GoodsShopDocument.cpp
    core/DialogueDocument.cpp
    core/DurableFileTransaction.cpp
)

set(CORE_HEADERS
    core/BuildVersion.h
    ${SRC_DIR}/Resource/SemanticVersion.h
    ${SRC_DIR}/Resource/ModReleaseMetadata.h
    ${SRC_DIR}/Resource/ResourceCatalog.h
    ${SRC_DIR}/Resource/ResourceIniReader.h
    ${SRC_DIR}/Resource/ResourceManifest.h
    ${SRC_DIR}/Update/ArtifactChecksum.h
    ${SRC_DIR}/Update/OnlineUpdateCatalog.h
    ${SRC_DIR}/File/StrictRelativeResourcePath.h
    ${SRC_DIR}/File/RootedResourceReader.h
    ${SRC_DIR}/Launch/EditorRunDescriptor.h
    ${SRC_DIR}/Resource/ModReleaseAssets.h
    core/AssetReferenceScanner.h
    core/AuthoringMutationGate.h
    core/DesktopFileAssociationManager.h
    core/DesktopFileOpenEventRouter.h
    ${JXQY_EDITOR_GENERATED_CPP_FILE_TYPES}
    core/ScriptProjectSearch.h
    core/StoryGraphResourceContext.h
    core/AssetMigrationPolicy.h
    core/AndroidExternalResourcePackager.h
    core/OnlineResourcePackageExporter.h
    core/OnlineUpdateCatalogPublisher.h
    core/AssetPreviewLoader.h
    core/LegacyTextDecoder.h
    core/LuaFunctionOutline.h
    core/ScriptCallTemplate.h
    core/ImageFrameBatchExchange.h
    core/ImageFrameImport.h
    core/ImageFrameImportRecipe.h
    core/ImageFrameSequence.h
    core/ImageFrameTransform.h
    core/PicFileEditor.h
    core/ScriptConverter.h
    core/LuaScriptSyntaxValidator.h
    core/MapConverter.h
    core/IMPImageFile.h
    core/Util.h
    core/ini.h
    core/INIFileEditor.h
    core/ImageResourceCandidates.h
    core/MapFileEditor.h
    core/MpcImageCache.h
    core/ProjectManager.h
    core/ProjectRuntimeConfiguration.h
    core/ProjectDocumentRegistry.h
    core/DesktopRunCurrentTarget.h
    core/FocusedContentPlaytest.h
    core/SavedSceneLaunchPreparation.h
    core/WindowPlacement.h
    core/AssetCliRunner.h
    core/JxAssetMigrator.h
    core/EditorSettings.h
    core/DesktopRunController.h
    core/DesktopRunDocumentSnapshot.h
    core/DesktopRunSessionBase.h
    core/DesktopRunSessionCoordinator.h
    core/DesktopRunSessionCleanup.h
    core/DesktopRunSessionWorkspace.h
    core/SaveEditContext.h
    core/TranslationManager.h
    core/GameProfile.h
    core/MagicDocument.h
    core/GoodsShopDocument.h
    core/DialogueDocument.h
    core/DurableFileTransaction.h
    core/EditorAssetPath.h
    core/ResourcePathValidation.h
)

set(UI_SOURCES
    ui/MainWindow.cpp
    ui/AndroidExternalResourcePackageDialog.cpp
    ui/DesktopRunDiagnosticsModel.cpp
    ui/DesktopRunPanel.cpp
    ui/FileAssociationDialog.cpp
    ui/AssetReferenceDialog.cpp
    ui/ScriptProjectSearchDialog.cpp
    ui/StoryGraphView.cpp
    ui/StoryGraphWindow.cpp
    ui/ScriptCallDialog.cpp
    ui/ImageBatchExportDialog.cpp
    ui/ImageBatchImportDialog.cpp
    ui/ImageEditorWindow.cpp
    ui/ImageEditDialog.cpp
    ui/BatchConvertWindow.cpp
    ui/AboutDialog.cpp
    ui/MenuPreviewCanvas.cpp
    ui/ComponentPropertyEditor.cpp
    ui/MenuEditorWindow.cpp
    ui/AssetBrowser.cpp
    ui/AssetDragDrop.cpp
    ui/DarkMdiSubWindow.cpp
    ui/ThemeManager.cpp
    ui/ScriptEditorWindow.cpp
    ui/ScriptApiList.cpp
    ui/ScriptVariableHints.cpp
    ui/MapRenderCanvas.cpp
    ui/MapEditorWindow.cpp
    ui/MagicEditorWindow.cpp
    ui/MagicRangePreview.cpp
    ui/GoodsShopEditorWindow.cpp
    ui/DialogueEditorWindow.cpp
    ui/MpcPreviewLabel.cpp
    ui/EntityPropertyDialog.cpp
    ui/FilePickerHelper.cpp
    ui/MinimapWidget.cpp
    ui/NpcDataEditorWindow.cpp
    ui/TrapScriptEditorDialog.cpp
    ui/ResourceProfileEditorWindow.cpp
    ui/ProjectExplorerWidget.cpp
    ui/ProjectSettingsDialog.cpp
    ui/ProjectSceneListModel.cpp
    ui/ProjectSceneDialog.cpp
    ui/ProjectRuntimeConfigurationDialog.cpp
)

set(UI_HEADERS
    ui/AssetsPathSwitchParticipant.h
    ui/CloseTransactionParticipant.h
    ui/MainWindow.h
    ui/AndroidExternalResourcePackageDialog.h
    ui/DesktopRunDiagnosticsModel.h
    ui/DesktopRunPanel.h
    ui/FileAssociationDialog.h
    ui/AssetReferenceDialog.h
    ui/ScriptProjectSearchDialog.h
    ui/StoryGraphView.h
    ui/StoryGraphWindow.h
    ui/ScriptCallDialog.h
    ui/ImageBatchExportDialog.h
    ui/ImageBatchImportDialog.h
    ui/ImageEditorWindow.h
    ui/ImageEditDialog.h
    ui/BatchConvertWindow.h
    ui/AboutDialog.h
    ui/MenuPreviewCanvas.h
    ui/ComponentPropertyEditor.h
    ui/MenuEditorWindow.h
    ui/AssetBrowser.h
    ui/AssetDragDrop.h
    ui/DarkMdiSubWindow.h
    ui/ThemeManager.h
    ui/ScriptEditorWindow.h
    ui/ScriptApiList.h
    ui/ScriptVariableHints.h
    ui/MapRenderCanvas.h
    ui/MapEditorWindow.h
    ui/MagicEditorWindow.h
    ui/MagicRangePreview.h
    ui/GoodsShopEditorWindow.h
    ui/DialogueEditorWindow.h
    ui/MpcPreviewLabel.h
    ui/EntityPropertyDialog.h
    ui/FilePickerHelper.h
    ui/MinimapWidget.h
    ui/UndoRedoManager.h
    ui/NpcDataEditorWindow.h
    ui/TrapScriptEditorDialog.h
    ui/ResourceProfileEditorWindow.h
    ui/ProjectExplorerWidget.h
    ui/ProjectSettingsDialog.h
    ui/ProjectSceneListModel.h
    ui/ProjectSceneDialog.h
    ui/ProjectRuntimeConfigurationDialog.h
)

set(UI_FORMS
    ui/MainWindow.ui
    ui/ImageBatchExportDialog.ui
    ui/ImageBatchImportDialog.ui
    ui/ImageEditorWindow.ui
    ui/ImageEditDialog.ui
    ui/BatchConvertWindow.ui
    ui/AboutDialog.ui
    ui/MenuEditorWindow.ui
    ui/ComponentPropertyEditor.ui
    ui/AssetBrowser.ui
    ui/ScriptEditorWindow.ui
    ui/MapEditorWindow.ui
    ui/NpcDataEditorWindow.ui
)

set(RESOURCE_FILES
    resources.qrc
)

# The editor is part of the same JXQY product family as the game. Reuse the
# canonical game artwork directly so regenerated brand assets cannot leave the
# two executables with different icons.
set(JXQY_EDITOR_BRAND_ICON_PNG
    "${CMAKE_CURRENT_SOURCE_DIR}/../branding/jxqy-all-in-one-icon-1024.png")
if(NOT EXISTS "${JXQY_EDITOR_BRAND_ICON_PNG}")
    message(FATAL_ERROR
        "Required editor application icon is missing: ${JXQY_EDITOR_BRAND_ICON_PNG}")
endif()

if(WIN32)
    set(JXQY_EDITOR_WINDOWS_ICON_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/../win/jxqy-all-in-one/jxqy.ico")
    if(NOT EXISTS "${JXQY_EDITOR_WINDOWS_ICON_PATH}")
        message(FATAL_ERROR
            "Required Windows editor icon is missing: ${JXQY_EDITOR_WINDOWS_ICON_PATH}")
    endif()
    set(JXQY_EDITOR_WINDOWS_RESOURCE
        "${CMAKE_CURRENT_BINARY_DIR}/generated/UPEdit-JXQY.rc")
    configure_file(
        packaging/windows/UPEdit-JXQY.rc.in
        "${JXQY_EDITOR_WINDOWS_RESOURCE}"
        @ONLY)
    set_source_files_properties(
        "${JXQY_EDITOR_WINDOWS_ICON_PATH}"
        PROPERTIES HEADER_FILE_ONLY TRUE)
    list(APPEND RESOURCE_FILES
        "${JXQY_EDITOR_WINDOWS_RESOURCE}"
        "${JXQY_EDITOR_WINDOWS_ICON_PATH}")
elseif(APPLE)
    set_source_files_properties(
        "${JXQY_EDITOR_BRAND_ICON_PNG}"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND RESOURCE_FILES "${JXQY_EDITOR_BRAND_ICON_PNG}")
endif()

set(TRANSLATION_TS_FILES
    translations/jxqy-editor_zh_CN.ts
    translations/jxqy-editor_zh_TW.ts
    translations/jxqy-editor_en_US.ts
)

set(TRANSLATION_QM_FILES)
foreach(TRANSLATION_TS_FILE IN LISTS TRANSLATION_TS_FILES)
    get_filename_component(TRANSLATION_NAME "${TRANSLATION_TS_FILE}" NAME_WE)
    set(TRANSLATION_QM_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/translations/${TRANSLATION_NAME}.qm")
    add_custom_command(
        OUTPUT "${TRANSLATION_QM_FILE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/translations"
        COMMAND Qt6::lrelease
            "${CMAKE_CURRENT_SOURCE_DIR}/${TRANSLATION_TS_FILE}"
            -qm "${TRANSLATION_QM_FILE}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${TRANSLATION_TS_FILE}"
        VERBATIM
    )
    list(APPEND TRANSLATION_QM_FILES "${TRANSLATION_QM_FILE}")
endforeach()

# Qt's own standard-button/control translations are separate from the editor
# catalogs. Deploy both the tiny qt_*.qm dependency catalogs and qtbase_*.qm;
# copying qt_*.qm alone would leave a portable/non-SDK installation unable to
# resolve its dependency and silently mix Chinese application text with
# English standard controls.
set(QT_TRANSLATIONS_SOURCE_DIR
    "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
if(NOT IS_DIRECTORY "${QT_TRANSLATIONS_SOURCE_DIR}")
    message(FATAL_ERROR
        "Unable to locate Qt translation catalogs for the selected Qt6 package: "
        "${QT_TRANSLATIONS_SOURCE_DIR}")
endif()

set(QT_STANDARD_TRANSLATION_NAMES
    qt_zh_CN.qm
    qtbase_zh_CN.qm
    qt_zh_TW.qm
    qtbase_zh_TW.qm
)
set(QT_STANDARD_TRANSLATION_QM_FILES)
foreach(QT_TRANSLATION_NAME IN LISTS QT_STANDARD_TRANSLATION_NAMES)
    set(QT_TRANSLATION_SOURCE
        "${QT_TRANSLATIONS_SOURCE_DIR}/${QT_TRANSLATION_NAME}")
    if(NOT EXISTS "${QT_TRANSLATION_SOURCE}")
        message(FATAL_ERROR
            "Required Qt translation catalog is missing: ${QT_TRANSLATION_SOURCE}")
    endif()
    set(QT_TRANSLATION_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/translations/${QT_TRANSLATION_NAME}")
    add_custom_command(
        OUTPUT "${QT_TRANSLATION_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/translations"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT_TRANSLATION_SOURCE}" "${QT_TRANSLATION_OUTPUT}"
        DEPENDS "${QT_TRANSLATION_SOURCE}"
        VERBATIM
    )
    list(APPEND QT_STANDARD_TRANSLATION_QM_FILES "${QT_TRANSLATION_OUTPUT}")
endforeach()

set(DEPLOY_TRANSLATION_QM_FILES
    ${TRANSLATION_QM_FILES}
    ${QT_STANDARD_TRANSLATION_QM_FILES}
)
add_custom_target(jxqy-editor-translations ALL
    DEPENDS ${DEPLOY_TRANSLATION_QM_FILES})

function(deploy_editor_translations target_name)
    add_dependencies(${target_name} jxqy-editor-translations)
    # Deploy each QM via a tracked stamp output that is added to the target's sources.
    # This way the copy re-runs whenever a QM changes — even if the executable itself
    # does not relink (e.g. a TS-only edit). A plain POST_BUILD copy would leave the
    # deployed QM stale in that case.
    set(deploy_stamps "")
    foreach(qm IN LISTS DEPLOY_TRANSLATION_QM_FILES)
        get_filename_component(qm_name "${qm}" NAME)
        string(MAKE_C_IDENTIFIER "deploy_${target_name}_${qm_name}" stamp_name)
        # Include $<CONFIG> so Debug/Release (and other configs in a multi-config
        # generator like Visual Studio) get independent stamps. Without it the
        # configs share one stamp and whichever builds last can skip the copy.
        set(stamp "${CMAKE_CURRENT_BINARY_DIR}/${stamp_name}_$<CONFIG>.stamp")
        add_custom_command(
            OUTPUT "${stamp}"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${target_name}>/translations"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${qm}" "$<TARGET_FILE_DIR:${target_name}>/translations/${qm_name}"
            COMMAND ${CMAKE_COMMAND} -E touch "${stamp}"
            DEPENDS "${qm}"
            COMMENT "Deploying ${qm_name} for ${target_name}"
            VERBATIM
        )
        list(APPEND deploy_stamps "${stamp}")
    endforeach()
    target_sources(${target_name} PRIVATE ${deploy_stamps})
endfunction()

set(MINILZO_SOURCES
    ${THIRD_PARTY_DIR}/minilzo/minilzo.c
)

list(REMOVE_ITEM MINILZO_SOURCES
    ${THIRD_PARTY_DIR}/minilzo/testmini.c
)

set(LUA_SYNTAX_SOURCES
    ${THIRD_PARTY_DIR}/lua/lapi.c
    ${THIRD_PARTY_DIR}/lua/lauxlib.c
    ${THIRD_PARTY_DIR}/lua/lcode.c
    ${THIRD_PARTY_DIR}/lua/lctype.c
    ${THIRD_PARTY_DIR}/lua/ldebug.c
    ${THIRD_PARTY_DIR}/lua/ldo.c
    ${THIRD_PARTY_DIR}/lua/ldump.c
    ${THIRD_PARTY_DIR}/lua/lfunc.c
    ${THIRD_PARTY_DIR}/lua/lgc.c
    ${THIRD_PARTY_DIR}/lua/llex.c
    ${THIRD_PARTY_DIR}/lua/lmem.c
    ${THIRD_PARTY_DIR}/lua/lobject.c
    ${THIRD_PARTY_DIR}/lua/lopcodes.c
    ${THIRD_PARTY_DIR}/lua/lparser.c
    ${THIRD_PARTY_DIR}/lua/lstate.c
    ${THIRD_PARTY_DIR}/lua/lstring.c
    ${THIRD_PARTY_DIR}/lua/ltable.c
    ${THIRD_PARTY_DIR}/lua/ltm.c
    ${THIRD_PARTY_DIR}/lua/lundump.c
    ${THIRD_PARTY_DIR}/lua/lvm.c
    ${THIRD_PARTY_DIR}/lua/lzio.c
)

if(MSVC)
    set_source_files_properties(${LUA_SYNTAX_SOURCES} PROPERTIES COMPILE_OPTIONS /w)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(${LUA_SYNTAX_SOURCES} PROPERTIES COMPILE_OPTIONS -w)
endif()

set(WIDGETS_SOURCES
    ${UI_SOURCES}
)

set(WIDGETS_HEADERS
    ${UI_HEADERS}
)

if(JXQY_EDITOR_BUILD_BENCHMARK)
    list(APPEND WIDGETS_SOURCES
        diagnostics/EditorPerformanceBenchmark.cpp
    )
    list(APPEND WIDGETS_HEADERS
        diagnostics/EditorPerformanceBenchmark.h
    )
endif()

# Production implementation boundaries. Story graph core owns the reusable
# lexer, graph values, parser, and layout and depends only on Qt Core. The
# aggregate editor core owns format/model/service code and deliberately has no
# Qt Widgets dependency. Widgets owns windows, dialogs, canvases, and the
# GUI-only benchmark driver. Executables and tests link these targets instead
# of compiling private copies of their production sources.
add_library(jxqy-editor-story-graph-core STATIC
    ${STORY_GRAPH_CORE_SOURCES}
    ${STORY_GRAPH_CORE_HEADERS}
)
target_link_libraries(
    jxqy-editor-story-graph-core
    PUBLIC
    Qt6::Core
)
target_include_directories(
    jxqy-editor-story-graph-core
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/core"
    PRIVATE
    "${JXQY_EDITOR_GENERATED_INCLUDE_DIRECTORY}"
)

add_library(jxqy-editor-core STATIC
    ${CORE_SOURCES}
    ${CORE_HEADERS}
    ${MINILZO_SOURCES}
    ${LUA_SYNTAX_SOURCES}
    ${THIRD_PARTY_DIR}/miniz/miniz.c
    ${THIRD_PARTY_DIR}/miniz/miniz_zip.c
    ${THIRD_PARTY_DIR}/miniz/miniz_tinfl.c
    ${THIRD_PARTY_DIR}/miniz/miniz_tdef.c
)
set_source_files_properties(
    ${SRC_DIR}/Resource/ResourceManifest.cpp
    PROPERTIES
    COMPILE_DEFINITIONS JXQY_RESOURCE_MANIFEST_BUFFER_ONLY
)
target_link_libraries(jxqy-editor-core PUBLIC
    Qt6::Core
    Qt6::Gui
)
target_link_libraries(
    jxqy-editor-core
    PRIVATE
    jxqy-editor-story-graph-core
)
target_include_directories(jxqy-editor-core PRIVATE
    "${JXQY_EDITOR_GENERATED_INCLUDE_DIRECTORY}"
    "${JXQY_EDITOR_DESKTOP_INTEGRATION_OUTPUT_DIRECTORY}"
    "${THIRD_PARTY_DIR}/miniz"
)
if(WIN32)
    target_link_libraries(jxqy-editor-core PRIVATE advapi32 shell32)
endif()
if(NOT WIN32)
    target_link_libraries(jxqy-editor-core PRIVATE Iconv::Iconv)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-editor-core PRIVATE m)
endif()

add_library(jxqy-editor-widgets STATIC
    ${WIDGETS_SOURCES}
    ${WIDGETS_HEADERS}
    ${UI_FORMS}
)
target_link_libraries(jxqy-editor-widgets PUBLIC
    jxqy-editor-core
    Qt6::Widgets
    Qt6::Multimedia
)
if(JXQY_EDITOR_BUILD_BENCHMARK)
    target_compile_definitions(jxqy-editor-widgets PUBLIC
        JXQY_EDITOR_BUILD_BENCHMARK=1
    )
endif()

if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 main.cpp ${RESOURCE_FILES})
elseif(APPLE)
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE main.cpp ${RESOURCE_FILES})
else()
    add_executable(${PROJECT_NAME} main.cpp ${RESOURCE_FILES})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    jxqy-editor-widgets
)

deploy_editor_translations(${PROJECT_NAME})

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}-debug")
endif()

if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${JXQY_EDITOR_GENERATED_MACOS_PLIST}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.jxqy.editor"
        MACOSX_BUNDLE_BUNDLE_NAME "UPEdit-JXQY"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE DESTINATION "."
)
if(UNIX AND NOT APPLE)
    install(FILES "${JXQY_EDITOR_GENERATED_LINUX_DESKTOP}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES "${JXQY_EDITOR_GENERATED_LINUX_MIME}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/packages")
    foreach(JXQY_EDITOR_ICON_SIZE 16 32 64 128 256 512)
        set(JXQY_EDITOR_LINUX_ICON
            "${CMAKE_CURRENT_SOURCE_DIR}/../macos_ios/jxqy/res/icon${JXQY_EDITOR_ICON_SIZE}.png")
        if(NOT EXISTS "${JXQY_EDITOR_LINUX_ICON}")
            message(FATAL_ERROR
                "Required Linux editor icon is missing: ${JXQY_EDITOR_LINUX_ICON}")
        endif()
        install(FILES "${JXQY_EDITOR_LINUX_ICON}"
            DESTINATION
                "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${JXQY_EDITOR_ICON_SIZE}x${JXQY_EDITOR_ICON_SIZE}/apps"
            RENAME "com.jxqy.Editor.png")
    endforeach()
endif()

# CLI target: console application for automated migration (no GUI dependency)
set(CLI_PROJECT_NAME "jxqy-editor-cli")

add_executable(${CLI_PROJECT_NAME} main_cli.cpp)

target_link_libraries(${CLI_PROJECT_NAME} PRIVATE
    jxqy-editor-core
)

include(CTest)
if(BUILD_TESTING)
    target_compile_definitions(jxqy-editor-core PUBLIC
        JXQY_EDITOR_ENABLE_TEST_HOOKS)

    function(assert_test_target_has_no_production_sources target_name)
        get_target_property(target_source_files ${target_name} SOURCES)
        foreach(target_source_file IN LISTS target_source_files)
            if(target_source_file MATCHES "^\\$<")
                continue()
            endif()
            get_filename_component(target_source_path
                "${target_source_file}" ABSOLUTE
                BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
            file(TO_CMAKE_PATH "${target_source_path}" target_source_path)
            string(TOLOWER "${target_source_path}" target_source_path)
            foreach(production_directory core ui diagnostics)
                file(TO_CMAKE_PATH
                    "${CMAKE_CURRENT_SOURCE_DIR}/${production_directory}/"
                    production_source_prefix)
                string(TOLOWER "${production_source_prefix}" production_source_prefix)
                string(FIND "${target_source_path}" "${production_source_prefix}"
                    production_source_index)
                if(production_source_index EQUAL 0)
                    message(FATAL_ERROR
                        "Test target ${target_name} compiles production source "
                        "${target_source_file}. Link the matching story graph, "
                        "editor core, or editor widgets production target instead.")
                endif()
            endforeach()
        endforeach()
    endfunction()

    # Shared test-only fixtures used by more than one regression domain. This
    # target must stay free of production source files; test executables link
    # the production libraries separately.
    add_library(jxqy-editor-test-support STATIC
        tests/TestSupport.cpp
        tests/TestSupport.h
    )
    set_target_properties(jxqy-editor-test-support PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    target_link_libraries(jxqy-editor-test-support PUBLIC
        jxqy-editor-core
    )

    add_executable(jxqy-editor-story-graph-tests
        tests/StoryGraphCoreTests.cpp
    )
    target_link_libraries(
        jxqy-editor-story-graph-tests
        PRIVATE
        jxqy-editor-story-graph-core
    )
    add_test(
        NAME jxqy-editor-story-graph-tests
        COMMAND jxqy-editor-story-graph-tests
    )
    set_tests_properties(
        jxqy-editor-story-graph-tests
        PROPERTIES
        TIMEOUT 120
    )

    add_executable(jxqy-editor-story-graph-coordinator-tests
        tests/StoryGraphAnalysisCoordinatorTests.cpp
    )
    target_link_libraries(
        jxqy-editor-story-graph-coordinator-tests
        PRIVATE
        jxqy-editor-story-graph-core
    )
    add_test(
        NAME jxqy-editor-story-graph-coordinator-tests
        COMMAND jxqy-editor-story-graph-coordinator-tests
    )
    set_tests_properties(
        jxqy-editor-story-graph-coordinator-tests
        PROPERTIES
        TIMEOUT 120
    )

    add_executable(jxqy-editor-process-exit-tests
        tests/EditorProcessExitTests.cpp
    )
    set_target_properties(
        jxqy-editor-process-exit-tests
        PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    target_link_libraries(
        jxqy-editor-process-exit-tests
        PRIVATE
        jxqy-editor-widgets
        jxqy-editor-core
        jxqy-editor-story-graph-core
    )
    add_test(
        NAME jxqy-editor-process-exit-tests
        COMMAND jxqy-editor-process-exit-tests
    )
    set_tests_properties(
        jxqy-editor-process-exit-tests
        PROPERTIES
        TIMEOUT 30
    )
    if(NOT WIN32)
        set_tests_properties(
            jxqy-editor-process-exit-tests
            PROPERTIES
            ENVIRONMENT
                "QT_QPA_PLATFORM=offscreen"
        )
    endif()

    add_executable(jxqy-editor-story-graph-project-tests
        tests/StoryGraphProjectResolverTests.cpp
    )
    target_link_libraries(
        jxqy-editor-story-graph-project-tests
        PRIVATE
        jxqy-editor-story-graph-core
    )
    add_test(
        NAME jxqy-editor-story-graph-project-tests
        COMMAND jxqy-editor-story-graph-project-tests
    )
    set_tests_properties(
        jxqy-editor-story-graph-project-tests
        PROPERTIES
        TIMEOUT 120
    )

    add_executable(jxqy-editor-story-graph-runtime-trace-tests
        tests/StoryGraphRuntimeTraceTests.cpp
    )
    target_link_libraries(
        jxqy-editor-story-graph-runtime-trace-tests
        PRIVATE
        jxqy-editor-story-graph-core
    )
    add_test(
        NAME jxqy-editor-story-graph-runtime-trace-tests
        COMMAND jxqy-editor-story-graph-runtime-trace-tests
    )
    set_tests_properties(
        jxqy-editor-story-graph-runtime-trace-tests
        PROPERTIES
        TIMEOUT 120
    )

    add_executable(jxqy-editor-story-graph-trace-matcher-tests
        tests/StoryGraphTraceMatcherTests.cpp
    )
    target_link_libraries(
        jxqy-editor-story-graph-trace-matcher-tests
        PRIVATE
        jxqy-editor-story-graph-core
    )
    add_test(
        NAME jxqy-editor-story-graph-trace-matcher-tests
        COMMAND jxqy-editor-story-graph-trace-matcher-tests
    )
    set_tests_properties(
        jxqy-editor-story-graph-trace-matcher-tests
        PROPERTIES
        TIMEOUT 120
    )

    add_executable(jxqy-editor-story-graph-resource-tests
        tests/StoryGraphResourceContextTests.cpp
    )
    target_link_libraries(
        jxqy-editor-story-graph-resource-tests
        PRIVATE
        jxqy-editor-core
        jxqy-editor-story-graph-core
    )
    add_test(
        NAME jxqy-editor-story-graph-resource-tests
        COMMAND jxqy-editor-story-graph-resource-tests
    )
    set_tests_properties(
        jxqy-editor-story-graph-resource-tests
        PROPERTIES
        TIMEOUT 120
    )

    add_executable(jxqy-editor-story-graph-dialect-tests
        tests/StoryGraphDialectTests.cpp
    )
    target_link_libraries(
        jxqy-editor-story-graph-dialect-tests
        PRIVATE
        jxqy-editor-story-graph-core
        jxqy-editor-core
    )
    add_test(
        NAME jxqy-editor-story-graph-dialect-tests
        COMMAND jxqy-editor-story-graph-dialect-tests
    )
    set_tests_properties(
        jxqy-editor-story-graph-dialect-tests
        PROPERTIES
        TIMEOUT 120
    )

    add_executable(jxqy-editor-story-graph-widget-tests
        tests/StoryGraphWidgetTests.cpp
        resources.qrc
    )
    target_link_libraries(
        jxqy-editor-story-graph-widget-tests
        PRIVATE
        jxqy-editor-widgets
    )
    add_test(
        NAME jxqy-editor-story-graph-widget-tests
        COMMAND jxqy-editor-story-graph-widget-tests
    )
    set_tests_properties(
        jxqy-editor-story-graph-widget-tests
        PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 120
    )
    if(NOT WIN32)
        set_tests_properties(
            jxqy-editor-story-graph-widget-tests
            PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
    endif()
    deploy_editor_translations(
        jxqy-editor-story-graph-widget-tests)

    add_executable(jxqy-editor-core-tests
        tests/CoreRegressionTests.cpp
        resources.qrc
    )
    target_link_libraries(jxqy-editor-core-tests PRIVATE
        jxqy-editor-widgets
        jxqy-editor-test-support
    )
    target_compile_definitions(jxqy-editor-core-tests PRIVATE
        JXQY_SOURCE_ROOT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/.."
        JXQY_EDITOR_DESKTOP_INTEGRATION_PATH="${JXQY_EDITOR_DESKTOP_INTEGRATION_OUTPUT_DIRECTORY}"
    )
    add_test(NAME jxqy-editor-core-tests COMMAND jxqy-editor-core-tests)
    set_tests_properties(jxqy-editor-core-tests PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 300
    )
    deploy_editor_translations(jxqy-editor-core-tests)

    add_executable(jxqy-editor-online-resource-package-tests
        tests/OnlineResourcePackageExporterTests.cpp
    )
    target_link_libraries(
        jxqy-editor-online-resource-package-tests
        PRIVATE
        jxqy-editor-core
    )
    add_test(
        NAME jxqy-editor-online-resource-package-tests
        COMMAND jxqy-editor-online-resource-package-tests
    )

    add_executable(jxqy-editor-desktop-run-document-snapshot-tests
        tests/DesktopRunDocumentSnapshotTests.cpp
        resources.qrc
    )
    target_link_libraries(
        jxqy-editor-desktop-run-document-snapshot-tests
        PRIVATE
        jxqy-editor-widgets
    )
    add_test(
        NAME jxqy-editor-desktop-run-document-snapshot-tests
        COMMAND jxqy-editor-desktop-run-document-snapshot-tests
    )
    set_tests_properties(
        jxqy-editor-desktop-run-document-snapshot-tests
        PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 120
    )
    deploy_editor_translations(
        jxqy-editor-desktop-run-document-snapshot-tests)

    add_executable(jxqy-editor-magic-editor-tests
        tests/MagicEditorTests.cpp
        resources.qrc
    )
    set_target_properties(jxqy-editor-magic-editor-tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
    )
    target_link_libraries(jxqy-editor-magic-editor-tests PRIVATE
        jxqy-editor-widgets
        jxqy-editor-test-support
    )
    add_test(NAME jxqy-editor-magic-editor-tests
        COMMAND jxqy-editor-magic-editor-tests)
    set_tests_properties(jxqy-editor-magic-editor-tests PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 120
    )
    deploy_editor_translations(jxqy-editor-magic-editor-tests)

    add_executable(jxqy-editor-goods-shop-editor-tests
        tests/GoodsShopEditorTests.cpp
    )
    set_target_properties(jxqy-editor-goods-shop-editor-tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
    )
    target_link_libraries(jxqy-editor-goods-shop-editor-tests PRIVATE
        jxqy-editor-widgets
        jxqy-editor-test-support
    )
    add_test(NAME jxqy-editor-goods-shop-editor-tests
        COMMAND jxqy-editor-goods-shop-editor-tests)
    set_tests_properties(jxqy-editor-goods-shop-editor-tests PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 120
    )
    deploy_editor_translations(jxqy-editor-goods-shop-editor-tests)

    add_executable(jxqy-editor-dialogue-editor-tests
        tests/DialogueEditorTests.cpp
    )
    set_target_properties(jxqy-editor-dialogue-editor-tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
    )
    target_link_libraries(jxqy-editor-dialogue-editor-tests PRIVATE
        jxqy-editor-widgets
        jxqy-editor-test-support
    )
    add_test(NAME jxqy-editor-dialogue-editor-tests
        COMMAND jxqy-editor-dialogue-editor-tests)
    set_tests_properties(jxqy-editor-dialogue-editor-tests PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 120
    )
    deploy_editor_translations(jxqy-editor-dialogue-editor-tests)

    add_executable(jxqy-editor-asset-conversion-tests
        tests/AssetConversionTests.cpp
        # Runtime decoder oracle used to verify editor image-format output.
        ${SRC_DIR}/Image/PicDecoder.cpp
    )
    set_target_properties(jxqy-editor-asset-conversion-tests PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    target_link_libraries(jxqy-editor-asset-conversion-tests PRIVATE
        jxqy-editor-core
        jxqy-editor-test-support
    )
    target_compile_definitions(jxqy-editor-asset-conversion-tests PRIVATE
        JXQY_SOURCE_ROOT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/.."
    )
    add_test(NAME jxqy-editor-asset-conversion-tests
        COMMAND jxqy-editor-asset-conversion-tests)
    set_tests_properties(jxqy-editor-asset-conversion-tests PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 300
    )

    add_executable(jxqy-editor-android-external-resource-packager-tests
        tests/AndroidExternalResourcePackagerTests.cpp
    )
    set_target_properties(
        jxqy-editor-android-external-resource-packager-tests
        PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    target_link_libraries(
        jxqy-editor-android-external-resource-packager-tests
        PRIVATE
        jxqy-editor-core
    )
    add_test(
        NAME jxqy-editor-android-external-resource-packager-tests
        COMMAND jxqy-editor-android-external-resource-packager-tests
    )
    set_tests_properties(
        jxqy-editor-android-external-resource-packager-tests
        PROPERTIES
        TIMEOUT 120
    )

    add_executable(jxqy-editor-map-render-tests
        tests/MapRenderTests.cpp
        resources.qrc
    )
    set_target_properties(jxqy-editor-map-render-tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
    )
    target_link_libraries(jxqy-editor-map-render-tests PRIVATE
        jxqy-editor-widgets
        jxqy-editor-test-support
    )
    add_test(NAME jxqy-editor-map-render-tests
        COMMAND jxqy-editor-map-render-tests)
    set_tests_properties(jxqy-editor-map-render-tests PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 180
    )
    deploy_editor_translations(jxqy-editor-map-render-tests)

    add_executable(jxqy-editor-durable-transaction-tests
        tests/DurableFileTransactionTests.cpp
    )
    target_link_libraries(jxqy-editor-durable-transaction-tests PRIVATE
        jxqy-editor-core
    )
    add_test(NAME jxqy-editor-durable-transaction-tests
        COMMAND jxqy-editor-durable-transaction-tests)
    set_tests_properties(jxqy-editor-durable-transaction-tests PROPERTIES
        TIMEOUT 60
    )
    add_executable(jxqy-editor-desktop-run-tests
        tests/DesktopRunTests.cpp
    )
    set_target_properties(jxqy-editor-desktop-run-tests PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    target_link_libraries(jxqy-editor-desktop-run-tests PRIVATE
        jxqy-editor-core
    )
    add_test(NAME jxqy-editor-desktop-run-tests
        COMMAND jxqy-editor-desktop-run-tests)
    set_tests_properties(jxqy-editor-desktop-run-tests PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 180
    )
    add_executable(jxqy-editor-desktop-run-session-workspace-tests
        tests/DesktopRunSessionWorkspaceTests.cpp
    )
    set_target_properties(
        jxqy-editor-desktop-run-session-workspace-tests
        PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    target_link_libraries(
        jxqy-editor-desktop-run-session-workspace-tests
        PRIVATE
        jxqy-editor-core
    )
    add_test(
        NAME jxqy-editor-desktop-run-session-workspace-tests
        COMMAND jxqy-editor-desktop-run-session-workspace-tests
    )
    set_tests_properties(
        jxqy-editor-desktop-run-session-workspace-tests
        PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 180
    )
    add_executable(jxqy-editor-authoring-mutation-gate-tests
        tests/AuthoringMutationGateTests.cpp
    )
    set_target_properties(
        jxqy-editor-authoring-mutation-gate-tests
        PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    target_link_libraries(
        jxqy-editor-authoring-mutation-gate-tests
        PRIVATE
        jxqy-editor-core
    )
    add_test(
        NAME jxqy-editor-authoring-mutation-gate-tests
        COMMAND jxqy-editor-authoring-mutation-gate-tests
    )
    set_tests_properties(
        jxqy-editor-authoring-mutation-gate-tests
        PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 60
    )
    add_executable(jxqy-editor-desktop-run-session-base-tests
        tests/DesktopRunSessionBaseTests.cpp
    )
    set_target_properties(
        jxqy-editor-desktop-run-session-base-tests
        PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    target_link_libraries(
        jxqy-editor-desktop-run-session-base-tests
        PRIVATE
        jxqy-editor-core
    )
    add_test(
        NAME jxqy-editor-desktop-run-session-base-tests
        COMMAND jxqy-editor-desktop-run-session-base-tests
    )
    set_tests_properties(
        jxqy-editor-desktop-run-session-base-tests
        PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 60
    )
    add_executable(jxqy-editor-desktop-run-session-cleanup-tests
        tests/DesktopRunSessionCleanupTests.cpp
    )
    set_target_properties(
        jxqy-editor-desktop-run-session-cleanup-tests
        PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    target_link_libraries(
        jxqy-editor-desktop-run-session-cleanup-tests
        PRIVATE
        jxqy-editor-core
    )
    add_test(
        NAME jxqy-editor-desktop-run-session-cleanup-tests
        COMMAND jxqy-editor-desktop-run-session-cleanup-tests
    )
    set_tests_properties(
        jxqy-editor-desktop-run-session-cleanup-tests
        PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 180
    )
    add_executable(jxqy-editor-project-local-state-tests
        tests/ProjectLocalStateTests.cpp
    )
    set_target_properties(
        jxqy-editor-project-local-state-tests
        PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    target_link_libraries(
        jxqy-editor-project-local-state-tests
        PRIVATE
        jxqy-editor-core
    )
    add_test(
        NAME jxqy-editor-project-local-state-tests
        COMMAND jxqy-editor-project-local-state-tests
    )
    set_tests_properties(
        jxqy-editor-project-local-state-tests
        PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 180
    )
    add_executable(jxqy-editor-settings-migration-tests
        tests/EditorSettingsMigrationTests.cpp
    )
    set_target_properties(
        jxqy-editor-settings-migration-tests
        PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    target_link_libraries(
        jxqy-editor-settings-migration-tests
        PRIVATE
        jxqy-editor-core
    )
    add_test(
        NAME jxqy-editor-settings-migration-tests
        COMMAND jxqy-editor-settings-migration-tests
    )
    set_tests_properties(
        jxqy-editor-settings-migration-tests
        PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 180
    )
    add_executable(jxqy-editor-desktop-run-session-coordinator-tests
        tests/DesktopRunSessionCoordinatorTests.cpp
    )
    set_target_properties(
        jxqy-editor-desktop-run-session-coordinator-tests
        PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    target_link_libraries(
        jxqy-editor-desktop-run-session-coordinator-tests
        PRIVATE
        jxqy-editor-core
    )
    add_test(
        NAME jxqy-editor-desktop-run-session-coordinator-tests
        COMMAND jxqy-editor-desktop-run-session-coordinator-tests
    )
    set_tests_properties(
        jxqy-editor-desktop-run-session-coordinator-tests
        PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 180
    )
    add_executable(jxqy-editor-desktop-run-diagnostics-model-tests
        tests/DesktopRunDiagnosticsModelTests.cpp
    )
    set_target_properties(
        jxqy-editor-desktop-run-diagnostics-model-tests
        PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    target_link_libraries(
        jxqy-editor-desktop-run-diagnostics-model-tests
        PRIVATE
        jxqy-editor-widgets
    )
    add_test(
        NAME jxqy-editor-desktop-run-diagnostics-model-tests
        COMMAND jxqy-editor-desktop-run-diagnostics-model-tests
    )
    set_tests_properties(
        jxqy-editor-desktop-run-diagnostics-model-tests
        PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 60
    )
    add_executable(jxqy-editor-desktop-run-panel-tests
        tests/DesktopRunPanelTests.cpp
    )
    set_target_properties(
        jxqy-editor-desktop-run-panel-tests
        PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    target_link_libraries(
        jxqy-editor-desktop-run-panel-tests
        PRIVATE
        jxqy-editor-widgets
    )
    add_test(
        NAME jxqy-editor-desktop-run-panel-tests
        COMMAND jxqy-editor-desktop-run-panel-tests
    )
    set_tests_properties(
        jxqy-editor-desktop-run-panel-tests
        PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 60
    )
    if(NOT WIN32)
        set_tests_properties(
            jxqy-editor-desktop-run-panel-tests
            PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
    endif()
    add_executable(jxqy-editor-desktop-run-process-fixture
        tests/DesktopRunProcessFixture.cpp
    )
    set_target_properties(
        jxqy-editor-desktop-run-process-fixture
        PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
    )
    if(MINGW)
        target_link_options(
            jxqy-editor-desktop-run-process-fixture
            PRIVATE
            -municode
        )
    endif()
    add_dependencies(
        jxqy-editor-desktop-run-tests
        jxqy-editor-desktop-run-process-fixture)
    add_dependencies(
        jxqy-editor-desktop-run-session-coordinator-tests
        jxqy-editor-desktop-run-process-fixture)
    set_tests_properties(jxqy-editor-desktop-run-tests PROPERTIES
        ENVIRONMENT
            "JXQY_EDITOR_DESKTOP_RUN_FIXTURE=$<TARGET_FILE:jxqy-editor-desktop-run-process-fixture>"
    )
    set_tests_properties(
        jxqy-editor-desktop-run-session-coordinator-tests
        PROPERTIES
        ENVIRONMENT
            "JXQY_EDITOR_DESKTOP_RUN_FIXTURE=$<TARGET_FILE:jxqy-editor-desktop-run-process-fixture>"
    )
    assert_test_target_has_no_production_sources(jxqy-editor-test-support)
    assert_test_target_has_no_production_sources(
        jxqy-editor-story-graph-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-story-graph-coordinator-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-process-exit-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-story-graph-project-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-story-graph-runtime-trace-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-story-graph-trace-matcher-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-story-graph-resource-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-story-graph-dialect-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-story-graph-widget-tests)
    assert_test_target_has_no_production_sources(jxqy-editor-core-tests)
    assert_test_target_has_no_production_sources(jxqy-editor-asset-conversion-tests)
    assert_test_target_has_no_production_sources(jxqy-editor-map-render-tests)
    assert_test_target_has_no_production_sources(jxqy-editor-durable-transaction-tests)
    assert_test_target_has_no_production_sources(jxqy-editor-desktop-run-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-desktop-run-session-workspace-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-authoring-mutation-gate-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-desktop-run-session-base-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-desktop-run-session-cleanup-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-project-local-state-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-settings-migration-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-desktop-run-session-coordinator-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-desktop-run-diagnostics-model-tests)
    assert_test_target_has_no_production_sources(
        jxqy-editor-desktop-run-panel-tests)
    assert_test_target_has_no_production_sources(jxqy-editor-desktop-run-process-fixture)
endif()

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        # Use current build configuration (debug or release), not always --release
        set(WINDEPLOYQT_CONFIG_ARG "")
        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            set(WINDEPLOYQT_CONFIG_ARG "--release")
        endif()
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${WINDEPLOYQT_EXECUTABLE}
                "$<TARGET_FILE:${PROJECT_NAME}>"
                --dir "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                --libdir "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                --plugindir "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                ${WINDEPLOYQT_CONFIG_ARG}
            COMMENT "Deploying Qt dependencies..."
        )
        foreach(deploy_target
                jxqy-editor-core-tests
                jxqy-editor-asset-conversion-tests
                jxqy-editor-map-render-tests
                jxqy-editor-durable-transaction-tests
                jxqy-editor-desktop-run-tests
                jxqy-editor-desktop-run-session-workspace-tests
                jxqy-editor-desktop-run-session-base-tests
                jxqy-editor-desktop-run-session-cleanup-tests
                jxqy-editor-project-local-state-tests
                jxqy-editor-settings-migration-tests
                jxqy-editor-desktop-run-session-coordinator-tests
                jxqy-editor-desktop-run-diagnostics-model-tests
                jxqy-editor-desktop-run-panel-tests)
            if(NOT TARGET ${deploy_target})
                continue()
            endif()
            add_custom_command(TARGET ${deploy_target} POST_BUILD
                COMMAND ${WINDEPLOYQT_EXECUTABLE}
                    "$<TARGET_FILE:${deploy_target}>"
                    --dir "$<TARGET_FILE_DIR:${deploy_target}>"
                    --libdir "$<TARGET_FILE_DIR:${deploy_target}>"
                    --plugindir "$<TARGET_FILE_DIR:${deploy_target}>"
                    ${WINDEPLOYQT_CONFIG_ARG}
                COMMENT "Deploying Qt dependencies for ${deploy_target}..."
            )
        endforeach()
    endif()
endif()
