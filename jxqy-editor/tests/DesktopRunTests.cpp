#include "../core/EditorAssetPath.h"
#include "../core/EditorSettings.h"
#include "../core/DesktopRunController.h"
#include "../core/DesktopRunCurrentTarget.h"
#include "../core/DesktopRunSessionWorkspace.h"
#include "../core/DurableFileTransaction.h"
#include "../core/ProjectManager.h"
#include "../core/SavedSceneLaunchPreparation.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <system_error>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace
{
namespace fs = std::filesystem;

fs::path hostPath(const QString& path)
{
    const QByteArray utf8 = path.toUtf8();
    return fs::u8path(
        utf8.constData(),
        utf8.constData() + utf8.size());
}

bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

const PreparedDesktopRunOverlayFile* findOverlayFile(
    const PreparedSavedSceneLaunch& launch,
    const QString& virtualPath)
{
    for (const PreparedDesktopRunOverlayFile& file :
         launch.overlayFiles)
    {
        if (file.virtualPath == virtualPath)
            return &file;
    }
    return nullptr;
}

void reportPreparationFailure(
    const SavedSceneLaunchPreparationResult& result)
{
    if (result.succeeded())
        return;
    std::cerr << "Preparation issues=" << result.issues.size()
              << " dirty=" << result.dirtyDocuments.size() << '\n';
    for (const SavedSceneLaunchPreparationIssue& issue : result.issues)
    {
        std::cerr << "  error=" << static_cast<int>(issue.error)
                  << " field=" << issue.fieldName.toStdString()
                  << " virtual=" << issue.virtualPath.toStdString()
                  << " absolute=" << issue.absolutePath.toStdString()
                  << '\n';
    }
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) &&
        file.write(bytes) == bytes.size() &&
        file.commit();
}

bool createDirectoryLink(
    const QString& target,
    const QString& link,
    QString& errorText)
{
    errorText.clear();
#ifdef Q_OS_WIN
    constexpr DWORD allowUnprivilegedCreate = 0x2;
    const std::wstring targetPath =
        QDir::toNativeSeparators(target).toStdWString();
    const std::wstring linkPath =
        QDir::toNativeSeparators(link).toStdWString();
    if (CreateSymbolicLinkW(
            linkPath.c_str(),
            targetPath.c_str(),
            SYMBOLIC_LINK_FLAG_DIRECTORY |
                allowUnprivilegedCreate) != FALSE)
    {
        return true;
    }
    DWORD error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER &&
        CreateSymbolicLinkW(
            linkPath.c_str(),
            targetPath.c_str(),
            SYMBOLIC_LINK_FLAG_DIRECTORY) != FALSE)
    {
        return true;
    }
    error = GetLastError();
    errorText = QStringLiteral("Windows error %1")
        .arg(static_cast<qulonglong>(error));
    return false;
#else
    std::error_code error;
    fs::create_directory_symlink(
        hostPath(target),
        hostPath(link),
        error);
    if (error)
    {
        errorText = QString::fromStdString(error.message());
        return false;
    }
    return true;
#endif
}

bool removeDirectoryLink(
    const QString& link,
    QString& errorText)
{
    errorText.clear();
#ifdef Q_OS_WIN
    if (RemoveDirectoryW(
            QDir::toNativeSeparators(link).
                toStdWString().c_str()) != FALSE)
    {
        return true;
    }
    errorText = QStringLiteral("Windows error %1")
        .arg(static_cast<qulonglong>(GetLastError()));
    return false;
#else
    std::error_code error;
    fs::remove(hostPath(link), error);
    if (error)
    {
        errorText = QString::fromStdString(error.message());
        return false;
    }
    return true;
#endif
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QByteArray formalRootDigest(const QString& root)
{
    QStringList entries;
    QDirIterator iterator(
        root,
        QDir::AllEntries |
            QDir::NoDotAndDotDot |
            QDir::Hidden |
            QDir::System,
        QDirIterator::Subdirectories);
    while (iterator.hasNext())
        entries.append(iterator.next());
    std::sort(entries.begin(), entries.end());

    QCryptographicHash hash(QCryptographicHash::Sha256);
    const QDir rootDirectory(root);
    for (const QString& entry : entries)
    {
        const QFileInfo information(entry);
        QByteArray relative =
            rootDirectory.relativeFilePath(entry).toUtf8();
        relative.replace('\\', '/');
        hash.addData(relative);
        hash.addData(QByteArray(1, '\0'));
        if (information.isSymLink())
        {
            hash.addData(QByteArrayLiteral("L"));
            hash.addData(
                information.symLinkTarget().toUtf8());
        }
        else if (information.isDir())
        {
            hash.addData(QByteArrayLiteral("D"));
        }
        else if (information.isFile())
        {
            hash.addData(QByteArrayLiteral("F"));
            QFile file(entry);
            if (!file.open(QIODevice::ReadOnly) ||
                !hash.addData(&file))
            {
                return {};
            }
        }
        else
        {
            hash.addData(QByteArrayLiteral("O"));
        }
        hash.addData(QByteArray(1, '\0'));
    }
    return hash.result();
}

QMap<QString, QByteArray> formalRootDigests(
    const QStringList& roots)
{
    QMap<QString, QByteArray> digests;
    for (const QString& root : roots)
    {
        digests.insert(
            EditorAssetPath::comparisonKey(root),
            formalRootDigest(root));
    }
    return digests;
}

bool openSavedSceneProject(
    ProjectManager& manager,
    const QString& projectPath,
    const QString& assetsRoot,
    const QString& activeResourcePackId,
    const ProjectScene& scene)
{
    if (manager.isProjectOpen() &&
        !manager.closeProject())
    {
        return false;
    }
    ProjectResourceConfiguration resources;
    resources.editableAssetsRoot = assetsRoot;
    resources.activeResourcePackId =
        activeResourcePackId;
    if (!QDir().mkpath(
            QFileInfo(projectPath).absolutePath()) ||
        !manager.newProject(projectPath, resources))
    {
        return false;
    }
    ProjectRuntimeConfiguration configuration;
    configuration.defaultSceneId = scene.id;
    configuration.scenes.append(scene);
    return manager.saveRuntimeConfiguration(configuration);
}

bool hasCatalogDiagnostic(
    const RuntimeResource::ExactSelectionResult& result,
    std::string_view diagnosticCode)
{
    return std::any_of(
        result.diagnostics.cbegin(),
        result.diagnostics.cend(),
        [diagnosticCode](
            const RuntimeResource::CatalogDiagnostic& diagnostic)
        {
            return diagnostic.code == diagnosticCode;
        });
}

bool hasSanitizedCatalogDiagnostic(
    const RuntimeResource::ExactSelectionResult& result)
{
    return std::any_of(
        result.diagnostics.cbegin(),
        result.diagnostics.cend(),
        [](const RuntimeResource::CatalogDiagnostic& diagnostic)
        {
            return diagnostic.code.rfind(
                       "resource.catalog.", 0) == 0 &&
                diagnostic.code.find("_sanitized") !=
                    std::string::npos;
        });
}

bool containsPath(
    const QStringList& paths,
    const QString& expectedPath)
{
    const QString expectedKey =
        EditorAssetPath::comparisonKey(expectedPath);
    return std::any_of(
        paths.cbegin(),
        paths.cend(),
        [&expectedKey](const QString& path)
        {
            return EditorAssetPath::comparisonKey(path) ==
                expectedKey;
        });
}

QString absoluteResourcePath(
    const QString& root,
    const QString& virtualPath)
{
    return EditorAssetPath::normalizedAbsolutePath(
        QDir(root).filePath(virtualPath));
}

bool hasOverlay(
    const SavedSceneLaunchPreparationResult& preparation,
    const QString& virtualPath,
    const QByteArray& bytes)
{
    return preparation.succeeded() &&
        std::any_of(
            preparation.prepared->overlayFiles.cbegin(),
            preparation.prepared->overlayFiles.cend(),
            [&virtualPath, &bytes](
                const PreparedDesktopRunOverlayFile& file)
            {
                return file.virtualPath == virtualPath &&
                    file.bytes == bytes;
            });
}

bool runDesktopRunCurrentTargetTests()
{
    bool ok = true;
    auto makeScene = [](
                         const QString& id,
                         const QString& mapPath,
                         const QString& scriptPath,
                         const QPoint& playerPosition,
                         int variableValue)
    {
        ProjectScene scene;
        scene.id = id;
        scene.name = id + QStringLiteral("-name");
        scene.mapPath = mapPath;
        scene.npcPath =
            QStringLiteral("ini/npc/") + id +
            QStringLiteral(".npc");
        scene.objectPath =
            QStringLiteral("ini/obj/") + id +
            QStringLiteral(".obj");
        scene.entryScriptPath = scriptPath;
        scene.playerPosition = playerPosition;
        scene.integerVariables.insert(
            QStringLiteral("Baseline"),
            variableValue);
        scene.preservedFields.insert(
            QStringLiteral("owner"),
            id);
        return scene;
    };

    const ProjectScene defaultScene = makeScene(
        QStringLiteral("default"),
        QStringLiteral("Map\\Current.MAP"),
        QStringLiteral("Script\\Current.LUA"),
        QPoint(3, 4),
        100);
    const ProjectScene matchingScene = makeScene(
        QStringLiteral("matching"),
        QStringLiteral("map/current.map"),
        QStringLiteral("script/current.lua"),
        QPoint(7, 8),
        200);
    ProjectRuntimeConfiguration matchingConfiguration;
    matchingConfiguration.defaultSceneId = defaultScene.id;
    matchingConfiguration.scenes = {
        defaultScene,
        matchingScene
    };

    DesktopRunCurrentTargetResult mapResult =
        selectCurrentMapTarget(
            matchingConfiguration,
            QStringLiteral("map/current.map"),
            QStringLiteral("ini\\npc\\current.npc"),
            QStringLiteral("ini\\obj\\current.obj"),
            64,
            64,
            Qt::CaseInsensitive);
    ok = check(
        mapResult.succeeded() &&
            mapResult.target->id == defaultScene.id &&
            mapResult.matchingSceneIds ==
                QStringList{
                    defaultScene.id,
                    matchingScene.id
                } &&
            mapResult.target->playerPosition ==
                defaultScene.playerPosition &&
            mapResult.target->integerVariables ==
                defaultScene.integerVariables &&
            mapResult.target->mapPath ==
                QStringLiteral("map/current.map") &&
            mapResult.target->npcPath ==
                QStringLiteral("ini/npc/current.npc") &&
            mapResult.target->objectPath ==
                QStringLiteral("ini/obj/current.obj") &&
            mapResult.target->entryScriptPath.isEmpty(),
        "current-map baseline prefers the matching default and replaces every resource path") &&
        ok;

    mapResult = selectCurrentMapTarget(
        matchingConfiguration,
        QStringLiteral("map/current.map"),
        QStringLiteral("ini/npc/current.npc"),
        QStringLiteral("ini/obj/current.obj"),
        64,
        64,
        Qt::CaseSensitive);
    ok = check(
        mapResult.succeeded() &&
            mapResult.target->id == matchingScene.id &&
            mapResult.matchingSceneIds ==
                QStringList{matchingScene.id} &&
            mapResult.target->playerPosition ==
                matchingScene.playerPosition &&
            mapResult.target->integerVariables ==
                matchingScene.integerVariables,
        "current-map baseline selects one exact match when explicit case-sensitive comparison excludes the default") &&
        ok;

    const ProjectScene ambiguousMapA = makeScene(
        QStringLiteral("ambiguous-map-a"),
        QStringLiteral("map/ambiguous.map"),
        QStringLiteral("script/a.lua"),
        QPoint(1, 1),
        301);
    const ProjectScene ambiguousMapB = makeScene(
        QStringLiteral("ambiguous-map-b"),
        QStringLiteral("map/ambiguous.map"),
        QStringLiteral("script/b.lua"),
        QPoint(2, 2),
        302);
    ProjectRuntimeConfiguration ambiguousMapConfiguration;
    ambiguousMapConfiguration.defaultSceneId =
        defaultScene.id;
    ambiguousMapConfiguration.scenes = {
        defaultScene,
        ambiguousMapA,
        ambiguousMapB
    };
    mapResult = selectCurrentMapTarget(
        ambiguousMapConfiguration,
        QStringLiteral("map/ambiguous.map"),
        QStringLiteral("ini/npc/current.npc"),
        QStringLiteral("ini/obj/current.obj"),
        64,
        64,
        Qt::CaseSensitive);
    ok = check(
        !mapResult.succeeded() &&
            mapResult.error ==
                DesktopRunCurrentTargetError::
                    BaselineAmbiguous &&
            !mapResult.target.has_value() &&
            mapResult.matchingSceneIds ==
                QStringList{
                    ambiguousMapA.id,
                    ambiguousMapB.id
                },
        "current-map baseline rejects multiple non-default matches") &&
        ok;

    mapResult = selectCurrentMapTarget(
        matchingConfiguration,
        QStringLiteral("map/new.map"),
        QStringLiteral("ini/npc/new.npc"),
        QStringLiteral("ini/obj/new.obj"),
        64,
        64,
        Qt::CaseSensitive);
    ok = check(
        mapResult.succeeded() &&
            mapResult.matchingSceneIds.isEmpty() &&
            mapResult.target->id == defaultScene.id &&
            mapResult.target->mapPath ==
                QStringLiteral("map/new.map") &&
            mapResult.target->playerPosition ==
                defaultScene.playerPosition,
        "current-map baseline uses the default environment when no scene references the new map") &&
        ok;

    mapResult = selectCurrentMapTarget(
        matchingConfiguration,
        QStringLiteral("map/new-selected.map"),
        QStringLiteral("ini/npc/new.npc"),
        QStringLiteral("ini/obj/new.obj"),
        64,
        64,
        Qt::CaseSensitive,
        matchingScene.id);
    ok = check(
        mapResult.succeeded() &&
            mapResult.matchingSceneIds.isEmpty() &&
            mapResult.target->id == matchingScene.id &&
            mapResult.target->mapPath ==
                QStringLiteral("map/new-selected.map"),
        "current-map baseline uses the selected saved environment before the default for an unregistered map") &&
        ok;

    ProjectRuntimeConfiguration missingDefaultConfiguration =
        matchingConfiguration;
    missingDefaultConfiguration.defaultSceneId =
        QStringLiteral("missing-default");
    mapResult = selectCurrentMapTarget(
        missingDefaultConfiguration,
        QStringLiteral("map/current.map"),
        QStringLiteral("ini/npc/current.npc"),
        QStringLiteral("ini/obj/current.obj"),
        64,
        64,
        Qt::CaseInsensitive);
    ok = check(
        !mapResult.succeeded() &&
            mapResult.error ==
                DesktopRunCurrentTargetError::
                    BaselineMissing &&
            !mapResult.target.has_value(),
        "current-map baseline rejects a configuration whose default scene is absent") &&
        ok;

    ProjectRuntimeConfiguration emptyConfiguration;
    mapResult = selectCurrentMapTarget(
        emptyConfiguration,
        QStringLiteral("map/current.map"),
        QStringLiteral("ini/npc/current.npc"),
        QStringLiteral("ini/obj/current.obj"),
        64,
        64,
        Qt::CaseSensitive);
    ok = check(
        !mapResult.succeeded() &&
            mapResult.error ==
                DesktopRunCurrentTargetError::
                    BaselineMissing &&
            !mapResult.target.has_value(),
        "current-map baseline rejects a configuration without scenes or a default") &&
        ok;

    ProjectRuntimeConfiguration outOfBoundsConfiguration;
    ProjectScene outOfBoundsScene = defaultScene;
    outOfBoundsScene.playerPosition = QPoint(10, 5);
    outOfBoundsConfiguration.defaultSceneId =
        outOfBoundsScene.id;
    outOfBoundsConfiguration.scenes = {
        outOfBoundsScene
    };
    mapResult = selectCurrentMapTarget(
        outOfBoundsConfiguration,
        outOfBoundsScene.mapPath,
        QStringLiteral("ini/npc/new.npc"),
        QStringLiteral("ini/obj/new.obj"),
        10,
        8,
        Qt::CaseSensitive);
    ok = check(
        mapResult.succeeded() &&
            mapResult.target->playerPosition ==
                QPoint(0, 0) &&
            mapResult.warningCodes.contains(
                QStringLiteral(
                    "editor_run.current_map.player_position_fallback")),
        "current-map target applies a stable origin fallback when the baseline position is outside the current map") &&
        ok;

    DesktopRunCurrentTargetResult scriptResult =
        selectCurrentScriptTarget(
            matchingConfiguration,
            QStringLiteral("script/current.lua"),
            Qt::CaseInsensitive);
    ok = check(
        scriptResult.succeeded() &&
            scriptResult.target->id == defaultScene.id &&
            scriptResult.matchingSceneIds ==
                QStringList{
                    defaultScene.id,
                    matchingScene.id
                } &&
            scriptResult.target->mapPath ==
                defaultScene.mapPath &&
            scriptResult.target->npcPath ==
                defaultScene.npcPath &&
            scriptResult.target->objectPath ==
                defaultScene.objectPath &&
            scriptResult.target->playerPosition ==
                defaultScene.playerPosition &&
            scriptResult.target->integerVariables ==
                defaultScene.integerVariables &&
            scriptResult.target->entryScriptPath ==
                QStringLiteral("script/current.lua"),
        "current-script baseline prefers the matching default and replaces only its entry script") &&
        ok;

    scriptResult = selectCurrentScriptTarget(
        matchingConfiguration,
        QStringLiteral("script/current.lua"),
        Qt::CaseSensitive);
    ok = check(
        scriptResult.succeeded() &&
            scriptResult.target->id == matchingScene.id &&
            scriptResult.matchingSceneIds ==
                QStringList{matchingScene.id} &&
            scriptResult.target->mapPath ==
                matchingScene.mapPath &&
            scriptResult.target->npcPath ==
                matchingScene.npcPath &&
            scriptResult.target->objectPath ==
                matchingScene.objectPath &&
            scriptResult.target->playerPosition ==
                matchingScene.playerPosition &&
            scriptResult.target->integerVariables ==
                matchingScene.integerVariables,
        "current-script baseline selects one exact match under explicit case-sensitive comparison") &&
        ok;

    const ProjectScene ambiguousScriptA = makeScene(
        QStringLiteral("ambiguous-script-a"),
        QStringLiteral("map/a.map"),
        QStringLiteral("script/ambiguous.lua"),
        QPoint(1, 1),
        401);
    const ProjectScene ambiguousScriptB = makeScene(
        QStringLiteral("ambiguous-script-b"),
        QStringLiteral("map/b.map"),
        QStringLiteral("script/ambiguous.lua"),
        QPoint(2, 2),
        402);
    ProjectRuntimeConfiguration ambiguousScriptConfiguration;
    ambiguousScriptConfiguration.defaultSceneId =
        defaultScene.id;
    ambiguousScriptConfiguration.scenes = {
        defaultScene,
        ambiguousScriptA,
        ambiguousScriptB
    };
    scriptResult = selectCurrentScriptTarget(
        ambiguousScriptConfiguration,
        QStringLiteral("script/ambiguous.lua"),
        Qt::CaseSensitive);
    ok = check(
        !scriptResult.succeeded() &&
            scriptResult.error ==
                DesktopRunCurrentTargetError::
                    BaselineAmbiguous &&
            !scriptResult.target.has_value() &&
            scriptResult.matchingSceneIds ==
                QStringList{
                    ambiguousScriptA.id,
                    ambiguousScriptB.id
                },
        "current-script baseline rejects multiple non-default matches") &&
        ok;

    scriptResult = selectCurrentScriptTarget(
        matchingConfiguration,
        QStringLiteral("script/new.lua"),
        Qt::CaseSensitive);
    ok = check(
        scriptResult.succeeded() &&
            scriptResult.matchingSceneIds.isEmpty() &&
            scriptResult.target->id == defaultScene.id &&
            scriptResult.target->entryScriptPath ==
                QStringLiteral("script/new.lua"),
        "current-script baseline uses the default environment when no scene references the new script") &&
        ok;

    scriptResult = selectCurrentScriptTarget(
        matchingConfiguration,
        QStringLiteral("script/new-selected.lua"),
        Qt::CaseSensitive,
        matchingScene.id);
    ok = check(
        scriptResult.succeeded() &&
            scriptResult.matchingSceneIds.isEmpty() &&
            scriptResult.target->id == matchingScene.id &&
            scriptResult.target->entryScriptPath ==
                QStringLiteral("script/new-selected.lua"),
        "current-script baseline uses the selected saved environment before the default for an unregistered script") &&
        ok;

    scriptResult = selectCurrentScriptTarget(
        missingDefaultConfiguration,
        QStringLiteral("script/current.lua"),
        Qt::CaseInsensitive);
    ok = check(
        !scriptResult.succeeded() &&
            scriptResult.error ==
                DesktopRunCurrentTargetError::
                    BaselineMissing &&
            !scriptResult.target.has_value(),
        "current-script baseline rejects a configuration whose default scene is absent") &&
        ok;

    scriptResult = selectCurrentScriptTarget(
        emptyConfiguration,
        QStringLiteral("script/current.lua"),
        Qt::CaseSensitive);
    ok = check(
        !scriptResult.succeeded() &&
            scriptResult.error ==
                DesktopRunCurrentTargetError::
                    BaselineMissing &&
            !scriptResult.target.has_value(),
        "current-script baseline rejects a configuration without scenes or a default") &&
        ok;
    return ok;
}

bool runSavedScenePreparationTests()
{
    bool ok = true;
    ProjectManager& manager = ProjectManager::instance();
    if (manager.isProjectOpen())
    {
        ok = check(
            manager.closeProject(),
            "close prior project before desktop-run tests") && ok;
    }

    ProjectDocumentRegistry emptyRegistry;
    SavedSceneLaunchPreparationResult result =
        prepareSavedSceneLaunch(
            manager,
            emptyRegistry,
            QStringView(u"scene-main"));
    ok = check(
        !result.succeeded() &&
            result.issues.size() == 1 &&
            result.issues.constFirst().error ==
                SavedSceneLaunchPreparationError::ProjectNotOpen,
        "preparation rejects a missing project without side effects") &&
        ok;

    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create desktop-run preparation fixture"))
    {
        return false;
    }
    const QString root = temporaryDirectory.path();
    const QString assetsRoot =
        QDir(root).filePath(QString::fromUtf8("可编辑 资源"));
    const QString commonRoot =
        QDir(assetsRoot).filePath(QStringLiteral("common"));
    const QString mapPath =
        QDir(assetsRoot).filePath(
            QString::fromUtf8("map/中都.map"));
    const QString npcPath =
        QDir(assetsRoot).filePath(
            QString::fromUtf8("ini/npc/中都.npc"));
    const QString objectPath =
        QDir(assetsRoot).filePath(
            QString::fromUtf8("ini/obj/中都.obj"));
    const QString scriptPath =
        QDir(commonRoot).filePath(
            QString::fromUtf8("script/入口.txt"));
    ok = check(
        writeFile(
            QDir(assetsRoot).filePath(
                QStringLiteral("game_profile.ini")),
            QByteArray(
                "[Game]\n"
                "Id=JXQY2\n"
                "Name=JXQY2\n"
                "Type=0\n"
                "[Resource]\n"
                "CommonPath=common\n")) &&
            writeFile(mapPath, QByteArray("map")) &&
            writeFile(npcPath, QByteArray("npc")) &&
            writeFile(objectPath, QByteArray("object")) &&
            writeFile(scriptPath, QByteArray("script")),
        "write profile-backed JXQY2 scene files") && ok;

    const QString projectPath =
        QDir(root).filePath(
            QString::fromUtf8("项目/作者流程.jxqyproj"));
    ProjectResourceConfiguration resources;
    resources.editableAssetsRoot = assetsRoot;
    resources.activeResourcePackId = QStringLiteral("JXQY2");
    ok = check(
        QDir().mkpath(QFileInfo(projectPath).absolutePath()) &&
            manager.newProject(projectPath, resources),
        "create saved-scene project") && ok;

    ProjectScene scene;
    scene.id = QStringLiteral("scene-main");
    scene.name = QString::fromUtf8("中都测试");
    scene.mapPath = QString::fromUtf8("map/中都.map");
    scene.npcPath = QString::fromUtf8("ini/npc/中都.npc");
    scene.objectPath = QString::fromUtf8("ini/obj/中都.obj");
    scene.entryScriptPath = QString::fromUtf8("script/入口.txt");
    scene.playerPosition = QPoint(100, 120);
    scene.integerVariables.insert(QStringLiteral("Event"), 100);
    ProjectRuntimeConfiguration runtimeConfiguration;
    runtimeConfiguration.defaultSceneId = scene.id;
    runtimeConfiguration.scenes.append(scene);
    ok = check(
        manager.saveRuntimeConfiguration(runtimeConfiguration) &&
            !manager.runtimeConfigurationNeedsSave(),
        "persist the runtime scene before preparation") && ok;

    ProjectDocumentRegistry registry;
    const QString normalizedMapPath =
        absoluteResourcePath(assetsRoot, scene.mapPath);
    const QString normalizedNpcPath =
        absoluteResourcePath(assetsRoot, scene.npcPath);
    const QString normalizedObjectPath =
        absoluteResourcePath(assetsRoot, scene.objectPath);
    const QString normalizedScriptPath =
        absoluteResourcePath(commonRoot, scene.entryScriptPath);
    ok = check(
        registry.registerDocument(
            normalizedMapPath,
            ProjectDocumentType::Map,
            false) &&
            registry.registerDocument(
                normalizedNpcPath,
                ProjectDocumentType::NpcList,
                false) &&
            registry.registerDocument(
                normalizedObjectPath,
                ProjectDocumentType::ObjectList,
                false) &&
            registry.registerDocument(
                normalizedScriptPath,
                ProjectDocumentType::Script,
                false),
        "register all directly referenced clean documents") && ok;

    const QString unrelatedPath =
        QDir(assetsRoot).filePath(
            QStringLiteral("script/unrelated.txt"));
    ok = check(
        registry.registerDocument(
            unrelatedPath,
            ProjectDocumentType::Script,
            true),
        "register an unrelated dirty document") && ok;

    const QString transactionStore =
        QDir(assetsRoot).filePath(
            QStringLiteral(".jxqy_editor"));
    ok = check(
        !QFileInfo::exists(transactionStore),
        "fixture starts without an editor transaction directory") && ok;
    const QStringList expectedFormalRoots{
        assetsRoot,
        commonRoot
    };
    const QMap<QString, QByteArray> beforeDigests =
        formalRootDigests(expectedFormalRoots);
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"scene-main"));
    reportPreparationFailure(result);
    ok = check(
        result.succeeded() &&
            result.prepared->targetKind ==
                EditorRun::TargetKind::Scene &&
            result.prepared->canonicalActiveResourcePackId ==
                QStringLiteral("JXQY2") &&
            result.prepared->references.size() == 4 &&
            result.prepared->references.at(3).resolvedRoot ==
                EditorAssetPath::normalizedAbsolutePath(commonRoot) &&
            result.prepared->references.at(3).
                launchVerifiedBytes.isEmpty() &&
            result.prepared->references.at(3).
                launchSha256.isEmpty() &&
            !result.prepared->references.at(3).
                launchContentFromOverlay &&
            !result.prepared->references.at(3).
                launchSourceFromEditorBuffer &&
            result.prepared->overlayFiles.isEmpty() &&
            result.prepared->references.at(0).
                launchSha256.isEmpty() &&
            containsPath(
                result.prepared->formalRoots,
                assetsRoot) &&
            containsPath(
                result.prepared->formalRoots,
                commonRoot) &&
            result.dirtyDocuments.isEmpty(),
        "clean profile-backed scene keeps its entry script on the current formal route while resolving active then Common and allowing unrelated dirty documents") &&
        ok;
    ok = check(
        !QFileInfo::exists(transactionStore) &&
            formalRootDigests(expectedFormalRoots) ==
                beforeDigests,
        "read-only preparation does not create or recover a formal-root transaction") &&
        ok;
    result.prepared.reset();
    ok = check(
        writeFile(
            normalizedScriptPath,
            QByteArray("changed script")),
        "modify the formal entry script after launch preparation") &&
        ok;
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"scene-main"));
    reportPreparationFailure(result);
    ok = check(
        result.succeeded() &&
            result.prepared->references.constLast().
                absolutePath == normalizedScriptPath &&
            result.prepared->references.constLast().
                launchVerifiedBytes.isEmpty() &&
            result.prepared->references.constLast().
                launchSha256.isEmpty() &&
            !result.prepared->references.constLast().
                launchContentFromOverlay &&
            readFile(normalizedScriptPath) ==
                QByteArray("changed script"),
        "formal entry-script changes remain immediately available through the current path without a launch snapshot") &&
        ok;
    result.prepared.reset();
    ok = check(
        writeFile(
            normalizedScriptPath,
            QByteArray("script")),
        "restore the current formal script bytes") &&
        ok;

    ok = check(
        registry.unregisterDocument(normalizedScriptPath),
        "close the clean entry script before formal-route preparation") &&
        ok;
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"scene-main"));
    reportPreparationFailure(result);
    ok = check(
        result.succeeded() &&
            result.prepared->overlayFiles.isEmpty() &&
            !result.prepared->references.constLast().
                launchContentFromOverlay &&
            !result.prepared->references.constLast().
                launchSourceFromEditorBuffer &&
            result.prepared->references.constLast().
                launchVerifiedBytes.isEmpty() &&
            result.prepared->references.constLast().
                launchSha256.isEmpty(),
        "an unopened clean entry script remains on the current formal route instead of being copied into the overlay") &&
        ok;
    result.prepared.reset();
    ok = check(
        registry.registerDocument(
            normalizedScriptPath,
            ProjectDocumentType::Script,
            false),
        "reopen the clean entry script after formal-route preparation") &&
        ok;

    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"SCENE-MAIN"));
    ok = check(
        !result.succeeded() &&
            result.issues.size() == 1 &&
            result.issues.constFirst().error ==
                SavedSceneLaunchPreparationError::SceneNotFound,
        "scene IDs use exact project identity") && ok;

    const QList<QString> directPaths{
        normalizedMapPath,
        normalizedNpcPath,
        normalizedObjectPath,
        normalizedScriptPath
    };
    for (const QString& directPath : directPaths)
    {
        ok = check(
            registry.setDocumentDirty(directPath, true),
            "mark one direct scene document dirty") && ok;
        result = prepareSavedSceneLaunch(
            manager,
            registry,
            QStringView(u"scene-main"));
        ok = check(
            !result.succeeded() &&
                result.issues.isEmpty() &&
                result.dirtyDocuments.size() == 1 &&
                result.dirtyDocuments.constFirst().filePath ==
                    directPath,
            "each direct MAP, NPC, OBJ, or script document blocks launch") &&
            ok;
        ok = check(
            registry.setDocumentDirty(directPath, false),
            "restore one direct scene document to clean") && ok;
    }

    ok = check(
        registry.setDocumentDirty(normalizedNpcPath, true) &&
            registry.setDocumentDirty(
                normalizedObjectPath, true) &&
            registry.setDocumentDirty(
                normalizedScriptPath, true),
        "mark several direct scene documents dirty") && ok;
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"scene-main"));
    ok = check(
        !result.succeeded() &&
            result.issues.isEmpty() &&
            result.dirtyDocuments.size() == 3 &&
            result.dirtyDocuments.at(0).filePath ==
                normalizedNpcPath &&
            result.dirtyDocuments.at(1).filePath ==
                normalizedObjectPath &&
            result.dirtyDocuments.at(2).filePath ==
                normalizedScriptPath,
        "all directly referenced dirty documents are aggregated in scene order") &&
        ok;
    ok = check(
        registry.setDocumentDirty(normalizedNpcPath, false) &&
            registry.setDocumentDirty(
                normalizedObjectPath, false) &&
            registry.setDocumentDirty(
                normalizedScriptPath, false),
        "restore all direct scene documents to clean") && ok;

    ok = check(
        registry.setDocumentDirty(normalizedMapPath, true),
        "mark the direct MAP dirty for overlay export") && ok;
    DesktopRunDocumentSnapshot mapSnapshot;
    mapSnapshot.filePath = normalizedMapPath;
    mapSnapshot.type = ProjectDocumentType::Map;
    mapSnapshot.dirty = true;
    mapSnapshot.serializationSupported = true;
    mapSnapshot.bytes = QByteArray("dirty-map-buffer");
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"scene-main"),
        {mapSnapshot});
    reportPreparationFailure(result);
    ok = check(
        result.succeeded() &&
            result.dirtyDocuments.isEmpty() &&
            result.prepared->overlayFiles.size() == 1 &&
            findOverlayFile(
                *result.prepared,
                scene.mapPath) &&
            findOverlayFile(
                *result.prepared,
                scene.mapPath)->bytes ==
                mapSnapshot.bytes &&
            findOverlayFile(
                *result.prepared,
                scene.mapPath)->
                    contentRootOrdinal == 0 &&
            !findOverlayFile(
                *result.prepared,
                scene.entryScriptPath) &&
            readFile(normalizedMapPath) == QByteArray("map"),
        "a supported dirty MAP enters the overlay while the clean entry script stays on the formal route") &&
        ok;
    result.prepared.reset();
    ok = check(
        registry.setDocumentDirty(normalizedMapPath, false) &&
            registry.setDocumentDirty(normalizedScriptPath, true),
        "switch the direct dirty document from MAP to script") && ok;

    DesktopRunDocumentSnapshot scriptSnapshot;
    scriptSnapshot.filePath = normalizedScriptPath;
    scriptSnapshot.type = ProjectDocumentType::Script;
    scriptSnapshot.dirty = true;
    scriptSnapshot.serializationSupported = true;
    scriptSnapshot.bytes =
        QByteArray("error('overlay script')\n");
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"scene-main"),
        {scriptSnapshot});
    reportPreparationFailure(result);
    ok = check(
        result.succeeded() &&
            result.prepared->overlayFiles.size() == 1 &&
            result.prepared->overlayFiles.constFirst().
                virtualPath == scene.entryScriptPath &&
            result.prepared->references.constLast().
                launchContentFromOverlay &&
            result.prepared->references.constLast().
                launchSourceFromEditorBuffer &&
            result.prepared->references.constLast().
                launchVerifiedBytes == scriptSnapshot.bytes &&
            result.prepared->references.constLast().
                launchSha256 ==
                QCryptographicHash::hash(
                    scriptSnapshot.bytes,
                    QCryptographicHash::Sha256),
        "a dirty entry script binds diagnostics and runtime overlay to the same captured bytes") &&
        ok;
    result.prepared.reset();

    DesktopRunDocumentSnapshot unsupportedScript =
        scriptSnapshot;
    unsupportedScript.serializationSupported = false;
    unsupportedScript.diagnosticCode =
        QStringLiteral(
            "editor_run.overlay.serializer_unavailable");
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"scene-main"),
        {unsupportedScript});
    ok = check(
        !result.succeeded() &&
            result.dirtyDocuments.size() == 1 &&
            result.issues.size() == 1 &&
            result.issues.constFirst().error ==
                SavedSceneLaunchPreparationError::
                    DirtyDocumentSnapshotUnavailable,
        "an explicitly unsupported dirty serializer fails closed with the concrete document retained") &&
        ok;
    ok = check(
        registry.setDocumentDirty(normalizedScriptPath, false),
        "restore the script document to clean after overlay tests") && ok;

    const QString linkedFormalTarget =
        QDir(root).filePath(
            QStringLiteral("linked-formal-target"));
    const QString linkedFormalRoot =
        QDir(assetsRoot).filePath(
            QStringLiteral("linked-formal"));
    const QString linkedScriptVirtualPath =
        QStringLiteral(
            "linked-formal/script/linked.lua");
    const QString linkedMapVirtualPath =
        QStringLiteral(
            "linked-formal/map/linked.map");
    const QString linkedScriptTargetPath =
        QDir(linkedFormalTarget).filePath(
            QStringLiteral("script/linked.lua"));
    const QString linkedMapTargetPath =
        QDir(linkedFormalTarget).filePath(
            QStringLiteral("map/linked.map"));
    QString linkError;
    const bool linkedFormalFixtureCreated =
        writeFile(
            linkedScriptTargetPath,
            QByteArray("Print('formal target')\n")) &&
        writeFile(
            linkedMapTargetPath,
            QByteArray("formal target map")) &&
        createDirectoryLink(
            linkedFormalTarget,
            linkedFormalRoot,
            linkError);
    if (!linkedFormalFixtureCreated)
    {
        std::cout
            << "(dirty descendant-link overlay checks skipped: "
            << linkError.toStdString() << ")\n";
    }
    else
    {
        const QString linkedScriptPath =
            absoluteResourcePath(
                assetsRoot,
                linkedScriptVirtualPath);
        DesktopRunDocumentSnapshot linkedScriptSnapshot;
        linkedScriptSnapshot.filePath =
            linkedScriptPath;
        linkedScriptSnapshot.type =
            ProjectDocumentType::Script;
        linkedScriptSnapshot.dirty = true;
        linkedScriptSnapshot.includeInOverlay = true;
        linkedScriptSnapshot.serializationSupported = true;
        linkedScriptSnapshot.bytes =
            QByteArray("Print('dirty linked buffer')\n");
        ProjectScene linkedScriptScene = scene;
        linkedScriptScene.entryScriptPath =
            linkedScriptVirtualPath;
        ok = check(
            registry.registerDocument(
                linkedScriptPath,
                ProjectDocumentType::Script,
                true),
            "register a dirty script through a formal descendant link") &&
            ok;
        result = prepareTransientSceneLaunch(
            manager,
            registry,
            linkedScriptScene,
            EditorRun::TargetKind::Script,
            {linkedScriptSnapshot});
        reportPreparationFailure(result);
        const PreparedDesktopRunOverlayFile*
            linkedScriptOverlay =
                result.succeeded()
                ? findOverlayFile(
                      *result.prepared,
                      linkedScriptVirtualPath)
                : nullptr;
        ok = check(
            result.succeeded() &&
                result.prepared->overlayFiles.size() == 1 &&
                linkedScriptOverlay &&
                linkedScriptOverlay->contentRootOrdinal == 0 &&
                linkedScriptOverlay->sourcePath ==
                    EditorAssetPath::normalizedAbsolutePath(
                        linkedScriptPath) &&
                linkedScriptOverlay->bytes ==
                    linkedScriptSnapshot.bytes &&
                result.prepared->references.constLast().
                    absolutePath ==
                    EditorAssetPath::normalizedAbsolutePath(
                        linkedScriptPath),
            "a dirty script reached through a descendant link keeps its logical active-root route and enters the private overlay") &&
            ok;
        result.prepared.reset();
        ok = check(
            registry.unregisterDocument(
                linkedScriptPath),
            "unregister the linked dirty script") && ok;

        const QString linkedMapPath =
            absoluteResourcePath(
                assetsRoot,
                linkedMapVirtualPath);
        DesktopRunDocumentSnapshot linkedMapSnapshot;
        linkedMapSnapshot.filePath = linkedMapPath;
        linkedMapSnapshot.type =
            ProjectDocumentType::Map;
        linkedMapSnapshot.dirty = true;
        linkedMapSnapshot.includeInOverlay = true;
        linkedMapSnapshot.serializationSupported = true;
        linkedMapSnapshot.bytes =
            QByteArray("dirty linked map buffer");
        ProjectScene linkedMapScene = scene;
        linkedMapScene.mapPath = linkedMapVirtualPath;
        linkedMapScene.npcPath.clear();
        linkedMapScene.objectPath.clear();
        linkedMapScene.entryScriptPath.clear();
        ok = check(
            registry.registerDocument(
                linkedMapPath,
                ProjectDocumentType::Map,
                true),
            "register a dirty MAP through a formal descendant link") &&
            ok;
        result = prepareTransientSceneLaunch(
            manager,
            registry,
            linkedMapScene,
            EditorRun::TargetKind::Map,
            {linkedMapSnapshot});
        reportPreparationFailure(result);
        const PreparedDesktopRunOverlayFile*
            linkedMapOverlay =
                result.succeeded()
                ? findOverlayFile(
                      *result.prepared,
                      linkedMapVirtualPath)
                : nullptr;
        ok = check(
            result.succeeded() &&
                result.prepared->overlayFiles.size() == 1 &&
                linkedMapOverlay &&
                linkedMapOverlay->contentRootOrdinal == 0 &&
                linkedMapOverlay->sourcePath ==
                    EditorAssetPath::normalizedAbsolutePath(
                        linkedMapPath) &&
                linkedMapOverlay->bytes ==
                    linkedMapSnapshot.bytes,
            "a dirty MAP reached through a descendant link keeps its logical active-root route and enters the private overlay") &&
            ok;
        result.prepared.reset();
        ok = check(
            registry.unregisterDocument(linkedMapPath),
            "unregister the linked dirty MAP") && ok;

        const QString linkedFormalRootB =
            QDir(assetsRoot).filePath(
                QStringLiteral("linked-formal-b"));
        const QString linkedScriptVirtualPathB =
            QStringLiteral(
                "linked-formal-b/script/linked.lua");
        const QString linkedScriptPathB =
            absoluteResourcePath(
                assetsRoot,
                linkedScriptVirtualPathB);
        const bool linkedFormalAliasBCreated =
            createDirectoryLink(
                linkedFormalTarget,
                linkedFormalRootB,
                linkError);
        if (!linkedFormalAliasBCreated)
        {
            std::cout
                << "(logical alias B overlay check skipped: "
                << linkError.toStdString() << ")\n";
        }
        else
        {
            DesktopRunDocumentSnapshot linkedAliasBSnapshot;
            linkedAliasBSnapshot.filePath =
                linkedScriptPathB;
            linkedAliasBSnapshot.type =
                ProjectDocumentType::Script;
            linkedAliasBSnapshot.dirty = true;
            linkedAliasBSnapshot.includeInOverlay = true;
            linkedAliasBSnapshot.serializationSupported = true;
            linkedAliasBSnapshot.bytes =
                QByteArray(
                    "Print('dirty logical alias B')\n");
            ProjectScene linkedAliasBScene = scene;
            linkedAliasBScene.entryScriptPath =
                linkedScriptVirtualPathB;
            ok = check(
                registry.registerDocument(
                    linkedScriptPath,
                    ProjectDocumentType::Script,
                    false) &&
                registry.registerDocument(
                    linkedScriptPathB,
                    ProjectDocumentType::Script,
                    true) &&
                    EditorAssetPath::comparisonKey(
                        linkedScriptPath) ==
                        EditorAssetPath::comparisonKey(
                            linkedScriptPathB) &&
                    registry.findDocument(
                        linkedScriptPath) &&
                    !registry.findDocument(
                        linkedScriptPath)->dirty &&
                    registry.findDocument(
                        linkedScriptPathB) &&
                    registry.findDocument(
                        linkedScriptPathB)->dirty,
                "register clean alias A and dirty alias B for one physical script independently") &&
                ok;
            result = prepareTransientSceneLaunch(
                manager,
                registry,
                linkedAliasBScene,
                EditorRun::TargetKind::Script,
                {linkedAliasBSnapshot});
            reportPreparationFailure(result);
            const PreparedDesktopRunOverlayFile*
                linkedAliasBOverlay =
                    result.succeeded()
                    ? findOverlayFile(
                          *result.prepared,
                          linkedScriptVirtualPathB)
                    : nullptr;
            ok = check(
                result.succeeded() &&
                    result.prepared->
                        overlayFiles.size() == 1 &&
                    linkedAliasBOverlay &&
                    linkedAliasBOverlay->sourcePath ==
                        EditorAssetPath::
                            normalizedAbsolutePath(
                                linkedScriptPathB) &&
                    linkedAliasBOverlay->bytes ==
                        linkedAliasBSnapshot.bytes &&
                    !findOverlayFile(
                        *result.prepared,
                        linkedScriptVirtualPath),
                "saved-scene preparation binds the private overlay to logical alias B rather than physical-equivalent alias A") &&
                ok;
            result.prepared.reset();
            ok = check(
                registry.unregisterDocument(
                    linkedScriptPath) &&
                    registry.unregisterDocument(
                        linkedScriptPathB),
                "unregister both formal logical script aliases") &&
                ok;
            ok = check(
                removeDirectoryLink(
                    linkedFormalRootB,
                    linkError),
                "remove only formal logical alias B") &&
                ok;
        }

        ok = check(
            removeDirectoryLink(
                linkedFormalRoot,
                linkError),
            "remove only the formal descendant link") &&
            ok;
    }

    DesktopRunDocumentSnapshot pendingMpc;
    pendingMpc.filePath =
        QDir(assetsRoot).filePath(
            QStringLiteral("mpc/map/pending.mpc"));
    pendingMpc.ownerMapFilePath = normalizedMapPath;
    pendingMpc.type = ProjectDocumentType::Image;
    pendingMpc.dirty = true;
    pendingMpc.includeInOverlay = true;
    pendingMpc.serializationSupported = true;
    pendingMpc.bytes = QByteArray("pending-mpc");
    DesktopRunDocumentSnapshot capturedMap;
    capturedMap.filePath = normalizedMapPath;
    capturedMap.type = ProjectDocumentType::Map;
    capturedMap.dirty = true;
    capturedMap.includeInOverlay = true;
    capturedMap.serializationSupported = true;
    capturedMap.bytes = QByteArray("captured-current-map");
    ProjectScene transientMapScene = scene;
    transientMapScene.entryScriptPath.clear();

    ok = check(
        EditorAssetPath::isInside(
                root,
                normalizedMapPath) &&
            QFile::remove(normalizedMapPath),
        "remove the active formal MAP after current-map capture without creating a fallback") &&
        ok;
    result = prepareTransientSceneLaunch(
        manager,
        registry,
        transientMapScene,
        EditorRun::TargetKind::Map,
        {capturedMap, pendingMpc});
    reportPreparationFailure(result);
    ok = check(
        result.succeeded() &&
            result.prepared->targetKind ==
                EditorRun::TargetKind::Map &&
            result.prepared->overlayFiles.size() == 2 &&
            hasOverlay(
                result,
                scene.mapPath,
                capturedMap.bytes) &&
            hasOverlay(
                result,
                QStringLiteral("mpc/map/pending.mpc"),
                pendingMpc.bytes) &&
            result.prepared->references.constFirst().
                absolutePath ==
                normalizedMapPath &&
            !QFileInfo::exists(pendingMpc.filePath),
        "current-map preparation uses its explicit captured MAP and keeps owned pending MPC after the formal MAP disappears") &&
        ok;
    result.prepared.reset();
    ok = check(
        writeFile(normalizedMapPath, QByteArray("map")),
        "restore the active formal MAP after current-map authority regression") &&
        ok;

    ProjectScene unsavedMapScene = scene;
    unsavedMapScene.mapPath =
        QStringLiteral(
            "map/__jxqy_editor_current__/current.map");
    unsavedMapScene.npcPath =
        QStringLiteral(
            "ini/npc/__jxqy_editor_current__/current.npc");
    unsavedMapScene.objectPath.clear();
    unsavedMapScene.entryScriptPath.clear();
    DesktopRunDocumentSnapshot unsavedMap;
    unsavedMap.overlayVirtualPath =
        unsavedMapScene.mapPath;
    unsavedMap.type = ProjectDocumentType::Map;
    unsavedMap.dirty = true;
    unsavedMap.includeInOverlay = true;
    unsavedMap.serializationSupported = true;
    unsavedMap.bytes = QByteArray("first-unsaved-map");
    DesktopRunDocumentSnapshot unsavedNpc;
    unsavedNpc.overlayVirtualPath =
        unsavedMapScene.npcPath;
    unsavedNpc.type = ProjectDocumentType::NpcList;
    unsavedNpc.dirty = true;
    unsavedNpc.includeInOverlay = true;
    unsavedNpc.serializationSupported = true;
    unsavedNpc.bytes = QByteArray("[Head]\nCount=0\n");
    DesktopRunDocumentSnapshot unsavedMapPendingMpc =
        pendingMpc;
    unsavedMapPendingMpc.ownerMapFilePath.clear();
    result = prepareTransientSceneLaunch(
        manager,
        registry,
        unsavedMapScene,
        EditorRun::TargetKind::Map,
        {unsavedMap, unsavedNpc,
         unsavedMapPendingMpc});
    reportPreparationFailure(result);
    ok = check(
        result.succeeded() &&
            result.prepared->overlayFiles.size() == 3 &&
            hasOverlay(
                result,
                unsavedMapScene.mapPath,
                unsavedMap.bytes) &&
            hasOverlay(
                result,
                unsavedMapScene.npcPath,
                unsavedNpc.bytes) &&
            hasOverlay(
                result,
                QStringLiteral("mpc/map/pending.mpc"),
                unsavedMapPendingMpc.bytes) &&
            result.prepared->references.constFirst().
                absolutePath.isEmpty(),
        "current-map preparation runs first-unsaved MAP/NPC bytes through safe private-overlay virtual paths") &&
        ok;
    result.prepared.reset();

    result = prepareTransientSceneLaunch(
        manager,
        registry,
        transientMapScene,
        EditorRun::TargetKind::Map,
        {pendingMpc});
    ok = check(
        !result.succeeded() &&
            result.issues.size() == 1 &&
            result.issues.constFirst().diagnosticCode ==
                QStringLiteral(
                    "editor_run.current_map.snapshot_missing"),
        "current-map preparation fails closed without an explicit MAP snapshot") &&
        ok;

    QList<DesktopRunDocumentSnapshot>
        ambiguousMapSnapshots{
            capturedMap,
            capturedMap,
            pendingMpc
        };
    result = prepareTransientSceneLaunch(
        manager,
        registry,
        transientMapScene,
        EditorRun::TargetKind::Map,
        ambiguousMapSnapshots);
    ok = check(
        !result.succeeded() &&
            result.issues.size() == 1 &&
            result.issues.constFirst().diagnosticCode ==
                QStringLiteral(
                    "editor_run.current_map.snapshot_ambiguous"),
        "current-map preparation rejects ambiguous explicit MAP snapshots") &&
        ok;

    DesktopRunDocumentSnapshot outsideMap =
        capturedMap;
    outsideMap.filePath =
        QDir(root).filePath(
            QStringLiteral("outside-current-map.map"));
    result = prepareTransientSceneLaunch(
        manager,
        registry,
        transientMapScene,
        EditorRun::TargetKind::Map,
        {outsideMap, pendingMpc});
    ok = check(
        !result.succeeded() &&
            result.issues.size() == 1 &&
            result.issues.constFirst().diagnosticCode ==
                QStringLiteral(
                    "editor_run.current_map.snapshot_invalid"),
        "current-map preparation rejects an explicit MAP source outside the locked active root") &&
        ok;

    DesktopRunDocumentSnapshot cleanPinnedCompanion;
    cleanPinnedCompanion.filePath =
        QDir(assetsRoot).filePath(
            QStringLiteral("save/game/current.npc"));
    cleanPinnedCompanion.type =
        ProjectDocumentType::NpcList;
    cleanPinnedCompanion.includeInOverlay = true;
    cleanPinnedCompanion.serializationSupported = true;
    cleanPinnedCompanion.bytes =
        QByteArray("[Head]\nCount=0\n");
    result = prepareTransientSceneLaunch(
        manager,
        registry,
        transientMapScene,
        EditorRun::TargetKind::Map,
        {capturedMap, cleanPinnedCompanion});
    reportPreparationFailure(result);
    ok = check(
        result.succeeded() &&
            result.prepared->overlayFiles.size() == 2 &&
            hasOverlay(
                result,
                scene.mapPath,
                capturedMap.bytes) &&
            hasOverlay(
                result,
                QStringLiteral("save/game/current.npc"),
                cleanPinnedCompanion.bytes) &&
            !QFileInfo::exists(
                cleanPinnedCompanion.filePath),
        "a clean runtime-save companion is pinned to captured overlay bytes") &&
        ok;
    result.prepared.reset();

    const QString otherMapPath =
        QDir(assetsRoot).filePath(
            QStringLiteral("map/other-open.map"));
    ok = check(
        writeFile(otherMapPath, QByteArray("other-map")),
        "write a second open MAP owner for generic snapshot filtering") &&
        ok;
    DesktopRunDocumentSnapshot otherPendingMpc =
        pendingMpc;
    otherPendingMpc.filePath =
        QDir(assetsRoot).filePath(
            QStringLiteral("mpc/map/other-pending.mpc"));
    otherPendingMpc.ownerMapFilePath =
        EditorAssetPath::normalizedAbsolutePath(
            otherMapPath);
    otherPendingMpc.bytes = QByteArray("other-pending-mpc");

    DesktopRunDocumentSnapshot genericRuntimeSaveObject =
        cleanPinnedCompanion;
    genericRuntimeSaveObject.filePath =
        QDir(assetsRoot).filePath(
            QStringLiteral("save/game/current.obj"));
    genericRuntimeSaveObject.type =
        ProjectDocumentType::ObjectList;
    genericRuntimeSaveObject.bytes =
        QByteArray("[Head]\nCount=0\n");

    QList<DesktopRunDocumentSnapshot> genericMapSnapshots =
        genericDesktopRunMapDocumentSnapshots({
            pendingMpc,
            cleanPinnedCompanion,
            genericRuntimeSaveObject
        });
    genericMapSnapshots.append(
        genericDesktopRunMapDocumentSnapshots({
            otherPendingMpc
        }));
    ok = check(
        genericMapSnapshots.size() == 4 &&
            !genericMapSnapshots.at(1).includeInOverlay &&
            !genericMapSnapshots.at(2).includeInOverlay,
        "generic collection clears clean NPC and OBJ runtime-save pins while retaining owner-tagged pending MPC candidates") &&
        ok;
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"scene-main"),
        genericMapSnapshots);
    reportPreparationFailure(result);
    ok = check(
        result.succeeded() &&
            result.prepared->targetKind ==
                EditorRun::TargetKind::Scene &&
            result.prepared->overlayFiles.size() == 1 &&
            !hasOverlay(
                result,
                scene.entryScriptPath,
                QByteArray("script")) &&
            hasOverlay(
                result,
                QStringLiteral("mpc/map/pending.mpc"),
                pendingMpc.bytes) &&
            !QFileInfo::exists(otherPendingMpc.filePath) &&
            !QFileInfo::exists(cleanPinnedCompanion.filePath) &&
            !QFileInfo::exists(
                genericRuntimeSaveObject.filePath),
        "saved-scene preparation admits only the owned pending MPC while leaving the clean entry script on the formal route") &&
        ok;
    result.prepared.reset();

    const QString currentScriptVirtualPath =
        QStringLiteral("script/current-buffer.txt");
    const QString currentScriptPath =
        QDir(assetsRoot).filePath(
            currentScriptVirtualPath);
    const QByteArray currentScriptBytes =
        QByteArray("Talk(\"captured current script\")\n");
    ok = check(
        writeFile(currentScriptPath, currentScriptBytes) &&
            registry.registerDocument(
                currentScriptPath,
                ProjectDocumentType::Script,
                false),
        "create and register an active-root current script") &&
        ok;
    ProjectScene transientScriptScene = scene;
    transientScriptScene.entryScriptPath =
        currentScriptVirtualPath;
    DesktopRunDocumentSnapshot capturedScript;
    capturedScript.filePath = currentScriptPath;
    capturedScript.type = ProjectDocumentType::Script;
    capturedScript.includeInOverlay = true;
    capturedScript.serializationSupported = true;
    capturedScript.bytes = currentScriptBytes;
    QList<DesktopRunDocumentSnapshot>
        currentScriptSnapshots = genericMapSnapshots;
    currentScriptSnapshots.append(capturedScript);

    ProjectScene unsavedScriptScene = scene;
    unsavedScriptScene.entryScriptPath =
        QStringLiteral(
            "script/__jxqy_editor_current__/current.lua");
    DesktopRunDocumentSnapshot unsavedScript;
    unsavedScript.overlayVirtualPath =
        unsavedScriptScene.entryScriptPath;
    unsavedScript.type =
        ProjectDocumentType::Script;
    unsavedScript.dirty = true;
    unsavedScript.includeInOverlay = true;
    unsavedScript.serializationSupported = true;
    unsavedScript.bytes =
        QByteArray("return 'first-unsaved-script'\n");
    QList<DesktopRunDocumentSnapshot>
        unsavedScriptSnapshots = genericMapSnapshots;
    unsavedScriptSnapshots.append(unsavedScript);
    result = prepareTransientSceneLaunch(
        manager,
        registry,
        unsavedScriptScene,
        EditorRun::TargetKind::Script,
        unsavedScriptSnapshots);
    reportPreparationFailure(result);
    ok = check(
        result.succeeded() &&
            hasOverlay(
                result,
                unsavedScriptScene.entryScriptPath,
                unsavedScript.bytes) &&
            result.prepared->references.constLast().
                absolutePath.isEmpty() &&
            result.prepared->references.constLast().
                launchSourceFromEditorBuffer,
        "current-script preparation runs a first-unsaved buffer from a safe private-overlay virtual path") &&
        ok;
    result.prepared.reset();

    result = prepareTransientSceneLaunch(
        manager,
        registry,
        transientScriptScene,
        EditorRun::TargetKind::Script,
        genericMapSnapshots);
    ok = check(
        !result.succeeded() &&
            result.issues.size() == 1 &&
            result.issues.constFirst().diagnosticCode ==
                QStringLiteral(
                    "editor_run.current_script.snapshot_missing"),
        "current-script preparation fails closed without an explicit script snapshot") &&
        ok;

    QList<DesktopRunDocumentSnapshot>
        ambiguousScriptSnapshots = currentScriptSnapshots;
    ambiguousScriptSnapshots.append(capturedScript);
    result = prepareTransientSceneLaunch(
        manager,
        registry,
        transientScriptScene,
        EditorRun::TargetKind::Script,
        ambiguousScriptSnapshots);
    ok = check(
        !result.succeeded() &&
            result.issues.size() == 1 &&
            result.issues.constFirst().diagnosticCode ==
                QStringLiteral(
                    "editor_run.current_script.snapshot_ambiguous"),
        "current-script preparation rejects ambiguous explicit script snapshots") &&
        ok;

    DesktopRunDocumentSnapshot outsideScript =
        capturedScript;
    outsideScript.filePath =
        QDir(root).filePath(
            QStringLiteral("outside-current-script.txt"));
    QList<DesktopRunDocumentSnapshot>
        outsideScriptSnapshots = genericMapSnapshots;
    outsideScriptSnapshots.append(outsideScript);
    result = prepareTransientSceneLaunch(
        manager,
        registry,
        transientScriptScene,
        EditorRun::TargetKind::Script,
        outsideScriptSnapshots);
    ok = check(
        !result.succeeded() &&
            result.issues.size() == 1 &&
            result.issues.constFirst().diagnosticCode ==
                QStringLiteral(
                    "editor_run.current_script.snapshot_invalid"),
        "current-script preparation rejects an explicit source outside the locked active root") &&
        ok;

    ok = check(
        QFile::remove(currentScriptPath),
        "remove the current script formal file after capturing its editor buffer") &&
        ok;
    result = prepareTransientSceneLaunch(
        manager,
        registry,
        transientScriptScene,
        EditorRun::TargetKind::Script,
        currentScriptSnapshots);
    reportPreparationFailure(result);
    ok = check(
        result.succeeded() &&
            result.prepared->targetKind ==
                EditorRun::TargetKind::Script &&
            result.prepared->overlayFiles.size() == 2 &&
            hasOverlay(
                result,
                currentScriptVirtualPath,
                currentScriptBytes) &&
            hasOverlay(
                result,
                QStringLiteral("mpc/map/pending.mpc"),
                pendingMpc.bytes) &&
            result.prepared->references.constLast().
                absolutePath ==
                EditorAssetPath::normalizedAbsolutePath(
                    currentScriptPath) &&
            result.prepared->references.constLast().
                launchVerifiedBytes ==
                currentScriptBytes &&
            result.prepared->references.constLast().
                launchSha256 ==
                QCryptographicHash::hash(
                    currentScriptBytes,
                    QCryptographicHash::Sha256) &&
            result.prepared->references.constLast().
                launchContentFromOverlay &&
            result.prepared->references.constLast().
                launchSourceFromEditorBuffer,
        "current-script preparation uses exact captured script authority after its formal file disappears and still filters pending MPC by the locked baseline MAP") &&
        ok;
    result.prepared.reset();
    ok = check(
        registry.unregisterDocument(currentScriptPath),
        "remove the transient current-script registry entry") &&
        ok;

    ok = check(
        registry.updateDocumentState(
            normalizedMapPath,
            ProjectDocumentType::Script,
            false),
        "inject a registry document-type mismatch") && ok;
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"scene-main"));
    ok = check(
        !result.succeeded() &&
            result.issues.size() == 1 &&
            result.issues.constFirst().error ==
                SavedSceneLaunchPreparationError::
                    DocumentTypeMismatch,
        "a direct reference with the wrong registry type fails closed") &&
        ok;

    ok = check(
        registry.updateDocumentState(
            normalizedMapPath,
            ProjectDocumentType::Map,
            false),
        "restore the MAP registry type") && ok;

    ok = check(
        manager.closeProject(),
        "close project before injecting a repairable runtime default") &&
        ok;
    QJsonObject projectObject =
        QJsonDocument::fromJson(readFile(projectPath)).object();
    QJsonObject runtimeObject =
        projectObject.value(
            QStringLiteral("runtimeConfiguration")).toObject();
    QJsonArray scenes =
        runtimeObject.value(QStringLiteral("scenes")).toArray();
    if (!scenes.isEmpty())
    {
        QJsonObject sceneObject = scenes.at(0).toObject();
        sceneObject.remove(QStringLiteral("integerVariables"));
        scenes.replace(0, sceneObject);
        runtimeObject[QStringLiteral("scenes")] = scenes;
        projectObject[QStringLiteral("runtimeConfiguration")] =
            runtimeObject;
    }
    ok = check(
        !scenes.isEmpty() &&
            writeFile(
                projectPath,
                QJsonDocument(projectObject).toJson(
                    QJsonDocument::Indented)) &&
            manager.openProject(projectPath) &&
            manager.runtimeConfigurationNeedsSave(),
        "open a project whose runtime defaults require an explicit save") &&
        ok;
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"scene-main"));
    ok = check(
        !result.succeeded() &&
            result.issues.size() == 1 &&
            result.issues.constFirst().error ==
                SavedSceneLaunchPreparationError::
                    RuntimeConfigurationNeedsSave,
        "a repairable but uncommitted runtime configuration fails closed") &&
        ok;
    ok = check(
        manager.saveProject() &&
            !manager.runtimeConfigurationNeedsSave(),
        "saving the repaired project clears the runtime save gate") &&
        ok;
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"scene-main"));
    reportPreparationFailure(result);
    ok = check(
        result.succeeded(),
        "saved repaired runtime configuration becomes launchable") &&
        ok;
    result.prepared.reset();

    ProjectRuntimeConfiguration missingConfiguration =
        manager.runtimeConfiguration();
    missingConfiguration.scenes.front().mapPath =
        QStringLiteral("map/missing.map");
    missingConfiguration.scenes.front().npcPath =
        QStringLiteral("ini/npc/missing.npc");
    missingConfiguration.scenes.front().objectPath =
        QStringLiteral("ini/obj/missing.obj");
    missingConfiguration.scenes.front().entryScriptPath =
        QStringLiteral("script/missing.lua");
    ok = check(
        manager.saveRuntimeConfiguration(missingConfiguration),
        "persist a missing-map scene candidate") && ok;
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"scene-main"));
    ok = check(
        result.succeeded() &&
            result.prepared->references.size() == 4 &&
            result.prepared->references.at(0).fieldName ==
                QStringLiteral("map") &&
            result.prepared->references.at(0).virtualPath ==
                QStringLiteral("map/missing.map") &&
            result.prepared->references.at(1).virtualPath ==
                QStringLiteral("ini/npc/missing.npc") &&
            result.prepared->references.at(2).virtualPath ==
                QStringLiteral("ini/obj/missing.obj") &&
            result.prepared->references.at(3).virtualPath ==
                QStringLiteral("script/missing.lua"),
        "missing MAP, NPC, OBJ, and script references remain lexical runtime routes instead of blocking session creation") &&
        ok;

    ok = check(
        manager.closeProject(),
        "close desktop-run preparation fixture project") && ok;
    return ok;
}

bool runExactSavedScenePreparationTests()
{
    bool ok = true;
    ProjectManager& manager = ProjectManager::instance();
    if (manager.isProjectOpen())
    {
        ok = check(
            manager.closeProject(),
            "close prior project before exact resource tests") && ok;
    }

    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create exact resource preparation fixture"))
    {
        return false;
    }
    const QString root = temporaryDirectory.path();
    ProjectDocumentRegistry registry;
    ProjectScene scene;
    scene.id = QStringLiteral("exact-scene");
    scene.name = QStringLiteral("Exact");
    scene.mapPath = QStringLiteral("map/path.map");

    const QString configuredCollection =
        QDir(root).filePath(QStringLiteral("configured-paths"));
    const QString configuredActive =
        QDir(configuredCollection).filePath(
            QStringLiteral("mod"));
    const QString configuredCommon =
        QDir(configuredCollection).filePath(
            QStringLiteral("common"));
    const QString configuredExternalUi =
        QDir(configuredCollection).filePath(
            QStringLiteral("ui"));
    ok = check(
        writeFile(
            QDir(configuredCollection).filePath(
                QStringLiteral("resources.ini")),
            QByteArray(
                "[Collection]\n"
                "CommonPath=common\n")) &&
            writeFile(
                QDir(configuredActive).filePath(
                    QStringLiteral("game_profile.ini")),
                QByteArray(
                    "[Game]\n"
                    "Id=MOD\n"
                    "Name=Mod\n"
                    "Type=0\n"
                    "[UI]\n"
                    "BaseId=UI\n")) &&
            writeFile(
                QDir(configuredExternalUi).filePath(
                    QStringLiteral("game_profile.ini")),
                QByteArray(
                    "[Game]\n"
                    "Id=UI\n"
                    "Name=UI\n"
                    "Type=0\n"
                    "[UI]\n"
                    "Profile=JXQY2\n")) &&
            writeFile(
                QDir(configuredCommon).filePath(
                    QStringLiteral("map/path.map")),
                QByteArray("common-map")) &&
            openSavedSceneProject(
                manager,
                QDir(root).filePath(
                    QStringLiteral(
                        "configured-project/configured.jxqyproj")),
                configuredCollection,
                QStringLiteral("mOd"),
                scene),
        "create a configured collection with directly discoverable resource roots") &&
        ok;
    const QStringList configuredFormalRoots{
        configuredCollection,
        configuredActive,
        configuredCommon,
        configuredExternalUi
    };
    const QMap<QString, QByteArray> configuredBefore =
        formalRootDigests(configuredFormalRoots);
    SavedSceneLaunchPreparationResult result =
        prepareSavedSceneLaunch(
            manager,
            registry,
            QStringView(u"exact-scene"));
    reportPreparationFailure(result);
    ok = check(
        result.succeeded() &&
            result.prepared->canonicalActiveResourcePackId ==
                QStringLiteral("MOD") &&
            result.prepared->activeContentRoot ==
                EditorAssetPath::normalizedAbsolutePath(
                    configuredActive) &&
            result.prepared->orderedContentRoots.size() == 2 &&
            result.prepared->orderedContentRoots.at(0).kind ==
                ResourceContentRoot::Kind::Local &&
            result.prepared->orderedContentRoots.at(1).kind ==
                ResourceContentRoot::Kind::Common &&
            result.prepared->references.size() == 1 &&
            result.prepared->references.constFirst().resolvedRoot ==
                EditorAssetPath::normalizedAbsolutePath(
                    configuredCommon) &&
            result.prepared->formalRoots.size() == 4 &&
            containsPath(
                result.prepared->formalRoots,
                configuredExternalUi),
        "editor preparation includes collection, content, Common, and UI fallback as formal roots") &&
        ok;
    ok = check(
        formalRootDigests(configuredFormalRoots) ==
            configuredBefore,
        "multi-root exact preparation leaves collection, content, Common, and UI bytes unchanged") &&
        ok;
    result.prepared.reset();

    DesktopRunDocumentSnapshot activeCollisionSnapshot;
    activeCollisionSnapshot.filePath =
        QDir(configuredActive).filePath(
            QStringLiteral("mpc/shared/collision.mpc"));
    activeCollisionSnapshot.ownerMapFilePath =
        QDir(configuredCommon).filePath(
            scene.mapPath);
    activeCollisionSnapshot.type =
        ProjectDocumentType::Image;
    activeCollisionSnapshot.includeInOverlay = true;
    activeCollisionSnapshot.serializationSupported = true;
    activeCollisionSnapshot.bytes =
        QByteArray("active-collision-bytes");
    DesktopRunDocumentSnapshot commonCollisionSnapshot;
    commonCollisionSnapshot.filePath =
        QDir(configuredCommon).filePath(
            QStringLiteral("mpc/shared/collision.mpc"));
    commonCollisionSnapshot.ownerMapFilePath =
        activeCollisionSnapshot.ownerMapFilePath;
    commonCollisionSnapshot.type =
        ProjectDocumentType::Image;
    commonCollisionSnapshot.includeInOverlay = true;
    commonCollisionSnapshot.serializationSupported = true;
    commonCollisionSnapshot.bytes =
        QByteArray("common-collision-bytes");
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"exact-scene"),
        {
            activeCollisionSnapshot,
            commonCollisionSnapshot
        });
    ok = check(
        EditorAssetPath::comparisonKey(
            activeCollisionSnapshot.filePath) !=
                EditorAssetPath::comparisonKey(
                    commonCollisionSnapshot.filePath) &&
            activeCollisionSnapshot.bytes !=
                commonCollisionSnapshot.bytes &&
            !result.succeeded() &&
            !result.prepared.has_value() &&
            result.issues.size() == 1 &&
            result.issues.constFirst().error ==
                SavedSceneLaunchPreparationError::
                    OverlayPathCollision &&
            result.issues.constFirst().virtualPath ==
                QStringLiteral("mpc/shared/collision.mpc"),
        "preparation rejects different active/Common sources and bytes that map to one overlay virtual path") &&
        ok;

    const QString aliasCollection =
        QDir(root).filePath(
            QStringLiteral("logical-alias-collection"));
    const QString aliasGeneration =
        QDir(root).filePath(
            QStringLiteral("logical-alias-generation"));
    const QString aliasA =
        QDir(aliasCollection).filePath(
            QStringLiteral("alias-a"));
    const QString aliasB =
        QDir(aliasCollection).filePath(
            QStringLiteral("alias-b"));
    QString aliasLinkError;
    bool aliasACreated = false;
    bool aliasBCreated = false;
    const bool aliasFilesCreated =
        writeFile(
            QDir(aliasCollection).filePath(
                QStringLiteral("resources.ini")),
            QByteArray(
                "[Collection]\n"
                "CommonPath=alias-b\n"
                "\n"
                "[Pack.A]\n"
                "Id=A\n"
                "Name=Logical A\n"
                "Path=alias-a\n"
                "Manifest=game_profile.ini\n"
                "\n"
                "[Pack.B]\n"
                "Id=B\n"
                "Name=Logical B\n"
                "Path=alias-b\n"
                "Manifest=game_profile.ini\n")) &&
        writeFile(
            QDir(aliasGeneration).filePath(
                QStringLiteral("game_profile.ini")),
            QByteArray(
                "[Game]\n"
                "Id=SHARED\n"
                "Name=Shared generation\n"
                "Type=0\n")) &&
        writeFile(
            QDir(aliasGeneration).filePath(
                QStringLiteral("map/path.map")),
            QByteArray("shared generation map"));
    if (aliasFilesCreated)
    {
        aliasACreated =
            createDirectoryLink(
                aliasGeneration,
                aliasA,
                aliasLinkError);
        if (aliasACreated)
        {
            aliasBCreated =
                createDirectoryLink(
                    aliasGeneration,
                    aliasB,
                    aliasLinkError);
        }
    }
    if (!aliasFilesCreated ||
        !aliasACreated ||
        !aliasBCreated)
    {
        std::cout
            << "(logical pack alias preparation checks skipped: "
            << aliasLinkError.toStdString() << ")\n";
    }
    else
    {
        ok = check(
            openSavedSceneProject(
                manager,
                QDir(root).filePath(
                    QStringLiteral(
                        "logical-alias-project/project.jxqyproj")),
                aliasCollection,
                QStringLiteral("SHARED"),
                scene),
            "open a project whose active and UI roots are two logical aliases of one physical generation") &&
            ok;
        result = prepareSavedSceneLaunch(
            manager,
            registry,
            QStringView(u"exact-scene"));
        reportPreparationFailure(result);
        const QString logicalAliasA =
            EditorAssetPath::normalizedAbsolutePath(
                aliasA);
        const QString logicalAliasB =
            EditorAssetPath::normalizedAbsolutePath(
                aliasB);
        ok = check(
            result.succeeded() &&
                result.prepared->activeContentRoot ==
                    logicalAliasA &&
                result.prepared->formalRoots.size() == 3 &&
                result.prepared->formalRoots.contains(
                    logicalAliasA) &&
                result.prepared->formalRoots.contains(
                    logicalAliasB) &&
                logicalAliasA != logicalAliasB,
            "desktop preparation keeps both logical formal-root aliases even while they resolve to one physical target") &&
            ok;
        result.prepared.reset();

        const QString aliasBMapPath =
            QDir(aliasB).filePath(scene.mapPath);
        DesktopRunDocumentSnapshot aliasBMapSnapshot;
        aliasBMapSnapshot.filePath = aliasBMapPath;
        aliasBMapSnapshot.type =
            ProjectDocumentType::Map;
        aliasBMapSnapshot.dirty = true;
        aliasBMapSnapshot.serializationSupported = true;
        aliasBMapSnapshot.bytes =
            QByteArray("dirty alias B map");
        ok = check(
            registry.registerDocument(
                aliasBMapPath,
                ProjectDocumentType::Map,
                true),
            "register a dirty document through logical alias B") &&
            ok;
        result = prepareSavedSceneLaunch(
            manager,
            registry,
            QStringView(u"exact-scene"),
            {aliasBMapSnapshot});
        reportPreparationFailure(result);
        ok = check(
            result.succeeded() &&
                result.prepared->references.constFirst().
                    resolvedRoot ==
                    EditorAssetPath::normalizedAbsolutePath(
                        aliasA) &&
                result.prepared->overlayFiles.isEmpty() &&
                result.dirtyDocuments.isEmpty(),
            "an alias A scene does not consume alias B's dirty snapshot even while both aliases share one physical target") &&
            ok;
        result.prepared.reset();
        ok = check(
            registry.unregisterDocument(
                aliasBMapPath),
            "unregister the dirty alias B document") && ok;
    }
    if (aliasBCreated)
    {
        ok = check(
            removeDirectoryLink(aliasB, aliasLinkError),
            "remove logical formal-root alias B") && ok;
    }
    if (aliasACreated)
    {
        ok = check(
            removeDirectoryLink(aliasA, aliasLinkError),
            "remove logical formal-root alias A") && ok;
    }

    const QString longLineRoot =
        QDir(root).filePath(QStringLiteral("long-single-line"));
    const QByteArray longName(4096, 'n');
    ok = check(
        writeFile(
            QDir(longLineRoot).filePath(
                QStringLiteral("game_profile.ini")),
            QByteArray(
                "[Game]\nId=LONGLINE\nName=") +
                longName +
                QByteArray("\nType=0\n")) &&
            writeFile(
                QDir(longLineRoot).filePath(
                    QStringLiteral("map/path.map")),
                QByteArray("map")) &&
            openSavedSceneProject(
                manager,
                QDir(root).filePath(
                    QStringLiteral(
                        "long-line-project/project.jxqyproj")),
                longLineRoot,
                QStringLiteral("LONGLINE"),
                scene),
        "create editor resource profile with one 4 KiB INI value line") &&
        ok;
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"exact-scene"));
    reportPreparationFailure(result);
    ok = check(
        result.succeeded() &&
            result.prepared->
                canonicalActiveResourcePackId ==
                    QStringLiteral("LONGLINE"),
        "editor preparation accepts the same 4 KiB single INI line as runtime exact selection") &&
        ok;
    result.prepared.reset();

    const QString embeddedNulRoot =
        QDir(root).filePath(QStringLiteral("embedded-nul"));
    QByteArray embeddedNulManifest(
        "[Game]\nId=NUL\nName=Before");
    embeddedNulManifest.append('\0');
    embeddedNulManifest.append(
        "After\nType=0\n");
    ok = check(
        writeFile(
            QDir(embeddedNulRoot).filePath(
                QStringLiteral("game_profile.ini")),
            embeddedNulManifest) &&
            writeFile(
                QDir(embeddedNulRoot).filePath(
                    QStringLiteral("map/path.map")),
                QByteArray("map")) &&
            openSavedSceneProject(
                manager,
                QDir(root).filePath(
                    QStringLiteral(
                        "embedded-nul-project/project.jxqyproj")),
                embeddedNulRoot,
                QStringLiteral("NUL"),
                scene),
        "create editor resource profile with an embedded NUL") &&
        ok;
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"exact-scene"));
    const RuntimeResource::ExactSelectionResult
        embeddedNulSelection =
            RuntimeResource::resolveExactResourceSelection(
                hostPath(embeddedNulRoot),
                "NUL");
    ok = check(
        result.succeeded() &&
            embeddedNulSelection.succeeded() &&
            embeddedNulSelection.selection.
                canonicalActiveResourcePackId == "NUL" &&
            hasSanitizedCatalogDiagnostic(
                embeddedNulSelection) &&
            result.prepared->activeContentRoot ==
                EditorAssetPath::normalizedAbsolutePath(
                    embeddedNulRoot) &&
            result.prepared->references.constFirst().
                resolvedRoot ==
                    EditorAssetPath::normalizedAbsolutePath(
                        embeddedNulRoot),
        "an embedded-NUL manifest value is sanitized with a diagnostic while the local resource pack remains runnable") &&
        ok;
    result.prepared.reset();

    const QString invalidLooseRoot =
        QDir(root).filePath(QStringLiteral("invalid-loose"));
    ok = check(
        writeFile(
            QDir(invalidLooseRoot).filePath(
                QStringLiteral("scene.map")),
            QByteArray("not-a-loose-marker")) &&
            openSavedSceneProject(
                manager,
                QDir(root).filePath(
                    QStringLiteral(
                        "invalid-loose-project/project.jxqyproj")),
                invalidLooseRoot,
                QStringLiteral("JXQY2"),
                scene),
        "create an authoring directory without game_profile.ini") &&
        ok;
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"exact-scene"));
    const RuntimeResource::ExactSelectionResult invalidLooseSelection =
        RuntimeResource::resolveExactResourceSelection(
            hostPath(invalidLooseRoot),
            "JXQY2");
    ok = check(
        result.succeeded() &&
            invalidLooseSelection.error ==
                RuntimeResource::ExactSelectionError::
                    ActiveResourcePackIdNotFound &&
            result.prepared->canonicalActiveResourcePackId ==
                QStringLiteral("JXQY2") &&
            result.prepared->orderedContentRoots.size() == 1 &&
            result.prepared->formalRoots.size() == 1,
        "a directory without game_profile.ini stays authorable and overlay-runnable without becoming a formal resource pack") &&
        ok;

    const QString duplicateRoot =
        QDir(root).filePath(QStringLiteral("duplicate-dependency"));
    ok = check(
        writeFile(
            QDir(duplicateRoot).filePath(
                QStringLiteral("resources.ini")),
            QByteArray(
                "[Pack.A]\nPath=a\n"
                "[Pack.B]\nPath=b\n"
                "[Pack.MOD]\nPath=mod\n")) &&
            writeFile(
                QDir(duplicateRoot).filePath(
                    QStringLiteral("a/game_profile.ini")),
                QByteArray("[Game]\nId=Dupe\nName=A\n")) &&
            writeFile(
                QDir(duplicateRoot).filePath(
                    QStringLiteral("b/game_profile.ini")),
                QByteArray("[Game]\nId=dUPE\nName=B\n")) &&
            writeFile(
                QDir(duplicateRoot).filePath(
                    QStringLiteral("mod/game_profile.ini")),
                QByteArray(
                    "[Game]\nId=MOD\nName=Mod\n"
                    "[Resource]\nDependencyId=DUPE\n")) &&
            writeFile(
                QDir(duplicateRoot).filePath(
                    QStringLiteral("mod/map/path.map")),
                QByteArray("local mod map")) &&
            openSavedSceneProject(
                manager,
                QDir(root).filePath(
                    QStringLiteral(
                        "duplicate-project/project.jxqyproj")),
                duplicateRoot,
                QStringLiteral("MOD"),
                scene),
        "create duplicate dependency identity fixture") && ok;
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"exact-scene"));
    const RuntimeResource::ExactSelectionResult
        duplicateDependencySelection =
            RuntimeResource::resolveExactResourceSelection(
                hostPath(duplicateRoot),
                "MOD");
    ok = check(
        result.succeeded() &&
            duplicateDependencySelection.succeeded() &&
            hasCatalogDiagnostic(
                duplicateDependencySelection,
                "resource.catalog.dependency_id_ambiguous") &&
            result.prepared->orderedContentRoots.size() == 1 &&
            result.prepared->orderedContentRoots.at(0).rootPath ==
                EditorAssetPath::normalizedAbsolutePath(
                    QDir(duplicateRoot).filePath(
                        QStringLiteral("mod"))) &&
            result.prepared->references.constFirst().
                resolvedRoot ==
                    EditorAssetPath::normalizedAbsolutePath(
                        QDir(duplicateRoot).filePath(
                            QStringLiteral("mod"))),
        "an ambiguous DependencyId keeps local content runnable, skips the ambiguous dependency, and emits a diagnostic") &&
        ok;
    result.prepared.reset();

    const QString cycleRoot =
        QDir(root).filePath(QStringLiteral("dependency-cycle"));
    ok = check(
        writeFile(
            QDir(cycleRoot).filePath(
                QStringLiteral("resources.ini")),
            QByteArray(
                "[Pack.A]\nPath=a\n"
                "[Pack.B]\nPath=b\n")) &&
            writeFile(
                QDir(cycleRoot).filePath(
                    QStringLiteral("a/game_profile.ini")),
                QByteArray(
                    "[Game]\nId=A\nName=A\n"
                    "[Resource]\nDependencyId=B\n")) &&
            writeFile(
                QDir(cycleRoot).filePath(
                    QStringLiteral("b/game_profile.ini")),
                QByteArray(
                    "[Game]\nId=B\nName=B\n"
                    "[Resource]\nDependencyId=A\n")) &&
            writeFile(
                QDir(cycleRoot).filePath(
                    QStringLiteral("a/map/path.map")),
                QByteArray("local cycle map")) &&
            openSavedSceneProject(
                manager,
                QDir(root).filePath(
                    QStringLiteral(
                        "cycle-project/project.jxqyproj")),
                cycleRoot,
                QStringLiteral("A"),
                scene),
        "create dependency cycle fixture") && ok;
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"exact-scene"));
    const RuntimeResource::ExactSelectionResult
        dependencyCycleSelection =
            RuntimeResource::resolveExactResourceSelection(
                hostPath(cycleRoot),
                "A");
    ok = check(
        result.succeeded() &&
            dependencyCycleSelection.succeeded() &&
            hasCatalogDiagnostic(
                dependencyCycleSelection,
                "resource.catalog.dependency_cycle_ignored") &&
            result.prepared->orderedContentRoots.size() == 2 &&
            result.prepared->orderedContentRoots.at(0).rootPath ==
                EditorAssetPath::normalizedAbsolutePath(
                    QDir(cycleRoot).filePath(
                        QStringLiteral("a"))) &&
            result.prepared->orderedContentRoots.at(1).rootPath ==
                EditorAssetPath::normalizedAbsolutePath(
                    QDir(cycleRoot).filePath(
                        QStringLiteral("b"))),
        "a dependency cycle keeps each reachable root once, preserves local launch, and emits a diagnostic") &&
        ok;
    result.prepared.reset();

    const QString missingCommonRoot =
        QDir(root).filePath(QStringLiteral("missing-common"));
    ok = check(
        writeFile(
            QDir(missingCommonRoot).filePath(
                QStringLiteral("resources.ini")),
            QByteArray(
                "[Collection]\n"
                "CommonPath=missing-common-root\n")) &&
            writeFile(
            QDir(missingCommonRoot).filePath(
                QStringLiteral("game_profile.ini")),
            QByteArray(
                "[Game]\n"
                "Id=MOD\n"
                "Name=Mod\n"
                "Type=0\n")) &&
            writeFile(
                QDir(missingCommonRoot).filePath(
                    QStringLiteral("map/path.map")),
                QByteArray("map")) &&
            openSavedSceneProject(
                manager,
                QDir(root).filePath(
                    QStringLiteral(
                        "missing-common-project/project.jxqyproj")),
                missingCommonRoot,
                QStringLiteral("MOD"),
                scene),
        "create explicitly missing Common root fixture") && ok;
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"exact-scene"));
    const RuntimeResource::ExactSelectionResult
        missingCommonSelection =
            RuntimeResource::resolveExactResourceSelection(
                hostPath(missingCommonRoot),
                "MOD");
    ok = check(
        result.succeeded() &&
            missingCommonSelection.succeeded() &&
            hasCatalogDiagnostic(
                missingCommonSelection,
                "resource.catalog.common_root_unavailable") &&
            result.prepared->orderedContentRoots.size() == 1 &&
            result.prepared->orderedContentRoots.constFirst().
                kind == ResourceContentRoot::Kind::Local &&
            result.prepared->references.constFirst().
                resolvedRoot ==
                    EditorAssetPath::normalizedAbsolutePath(
                        missingCommonRoot),
        "a missing explicit Common root removes only the Common capability while local launch remains available with a diagnostic") &&
        ok;
    result.prepared.reset();

    const QString pendingRoot =
        QDir(root).filePath(QStringLiteral("pending-transaction"));
    ok = check(
        writeFile(
            QDir(pendingRoot).filePath(
                QStringLiteral("map/path.map")),
            QByteArray("map")) &&
            openSavedSceneProject(
                manager,
                QDir(root).filePath(
                    QStringLiteral(
                        "pending-project/project.jxqyproj")),
                pendingRoot,
                QStringLiteral("JXQY2"),
                scene),
        "create a valid loose root for nested transaction checks") &&
        ok;
    const QString nestedTransactionRoot =
        QDir(pendingRoot).filePath(
            QStringLiteral("map"));
    const QString nestedTransactionTarget =
        QDir(nestedTransactionRoot).filePath(
            QStringLiteral("nested-candidate.map"));
    {
        DurableFileTransaction activeNestedTransaction(
            nestedTransactionRoot);
        QString transactionError;
        ok = check(
            activeNestedTransaction.addBytesWrite(
                nestedTransactionTarget,
                QByteArray("candidate"),
                transactionError),
            "prepare an active transaction below a formal root") &&
            ok;
        result = prepareSavedSceneLaunch(
            manager,
            registry,
            QStringView(u"exact-scene"));
        ok = check(
            result.succeeded(),
            "desktop-run preparation does not lock or reject an active resource transaction") &&
            ok;
        result.prepared.reset();
        activeNestedTransaction.cancel();
    }
    ok = check(
        !QFileInfo::exists(nestedTransactionTarget) &&
            !QFileInfo::exists(
                DurableFileTransaction::
                    transactionStorePath(
                        nestedTransactionRoot)),
        "canceling the active nested transaction removes its staged generation") &&
        ok;

    bool simulatedNestedCrash = false;
    {
        DurableFileTransaction crashedNestedTransaction(
            nestedTransactionRoot);
        QString transactionError;
        const bool prepared =
            crashedNestedTransaction.addBytesWrite(
                nestedTransactionTarget,
                QByteArray("candidate"),
                transactionError);
        if (prepared)
        {
            DurableFileTransaction::setFaultInjectorForTests(
                [](DurableFileTransaction::FaultPoint point,
                   int)
                {
                    return point ==
                            DurableFileTransaction::FaultPoint::
                                AfterPrepared
                        ? DurableFileTransaction::FaultAction::
                            SimulateCrash
                        : DurableFileTransaction::FaultAction::
                            Continue;
                });
            simulatedNestedCrash =
                !crashedNestedTransaction.commit(
                    transactionError);
            DurableFileTransaction::setFaultInjectorForTests({});
        }
        ok = check(
            prepared && simulatedNestedCrash,
            "leave a crash-pending transaction below a formal root") &&
            ok;
    }
    const QByteArray crashedNestedDigest =
        formalRootDigest(pendingRoot);
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"exact-scene"));
    ok = check(
        result.succeeded() &&
            formalRootDigest(pendingRoot) ==
                crashedNestedDigest,
        "desktop-run preparation ignores and does not recover a crash-pending resource transaction") &&
        ok;
    result.prepared.reset();
    QStringList nestedRecoveryErrors;
    ok = check(
        DurableFileTransaction::recoverPending(
            nestedTransactionRoot,
            nestedRecoveryErrors) &&
            nestedRecoveryErrors.isEmpty() &&
            !QFileInfo::exists(nestedTransactionTarget),
        "explicit nested-root recovery removes the crash-pending generation") &&
        ok;
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"exact-scene"));
    reportPreparationFailure(result);
    ok = check(
        result.succeeded(),
        "launch preparation succeeds after explicit nested recovery") &&
        ok;
    result.prepared.reset();

    const QString pendingEntry =
        QDir(DurableFileTransaction::transactionStorePath(
                 pendingRoot))
            .filePath(QStringLiteral("pending/manifest.json"));
    ok = check(
        writeFile(
            pendingEntry,
            QByteArray("{\"state\":\"prepared\"}")),
        "create a pending transaction at the formal-root boundary") &&
        ok;
    const QByteArray pendingBefore =
        formalRootDigest(pendingRoot);
    result = prepareSavedSceneLaunch(
        manager,
        registry,
        QStringView(u"exact-scene"));
    ok = check(
        result.succeeded(),
        "pending resource transactions do not block desktop-run preparation") &&
        ok;
    result.prepared.reset();
    ok = check(
        QFileInfo::exists(pendingEntry) &&
            formalRootDigest(pendingRoot) == pendingBefore,
        "desktop-run preparation neither recovers nor mutates the pending resource transaction") &&
        ok;

    ok = check(
        manager.closeProject(),
        "close exact resource preparation fixture project") && ok;
    return ok;
}

bool runDesktopExecutableSettingsTests()
{
    bool ok = true;
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create executable-settings fixture"))
    {
        return false;
    }
    const QString settingsPath =
        QDir(temporaryDirectory.path()).filePath(
            QString::fromUtf8("用户 设置/editor_config.ini"));
    ok = check(
        QDir().mkpath(QFileInfo(settingsPath).absolutePath()),
        "create settings parent") && ok;
    QSettings settings(settingsPath, QSettings::IniFormat);

    EditorSettings::DesktopExecutableValidation validation =
        EditorSettings::readDesktopGameExecutable(
            settings,
            QStringList());
    ok = check(
        validation.error ==
            EditorSettings::DesktopExecutableError::NotSet,
        "an unset desktop executable is distinguished") && ok;

    validation = EditorSettings::validateDesktopGameExecutable(
        temporaryDirectory.path());
    ok = check(
        validation.error ==
            EditorSettings::DesktopExecutableError::IsDirectory,
        "a directory is not accepted as the game executable") && ok;
    validation = EditorSettings::validateDesktopGameExecutable(
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("missing-game.exe")));
    ok = check(
        validation.error ==
            EditorSettings::DesktopExecutableError::DoesNotExist,
        "a missing game executable is distinguished") && ok;

    const QString currentExecutable =
        QCoreApplication::applicationFilePath();
    const QString canonicalCurrentExecutable =
        EditorAssetPath::normalizedAbsolutePath(
            QFileInfo(currentExecutable)
                .canonicalFilePath());
    validation = EditorSettings::validateDesktopGameExecutable(
        currentExecutable);
    ok = check(
        validation.succeeded() &&
            validation.executablePath ==
                canonicalCurrentExecutable,
        "a real executable is returned as its canonical final target") &&
        ok;

#ifdef Q_OS_WIN
    const QString releaseExecutableName =
        QStringLiteral("jxqy-all-in-one.exe");
    const QString debugExecutableName =
        QStringLiteral("jxqy-all-in-one-debug.exe");
#else
    const QString releaseExecutableName =
        QStringLiteral("jxqy-all-in-one");
    const QString debugExecutableName =
        QStringLiteral("jxqy-all-in-one-debug");
#endif
    const QString debugCandidateDirectory =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("discovery/debug-first"));
    const QString releaseCandidateDirectory =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("discovery/release-second"));
    const QString debugCandidate =
        QDir(debugCandidateDirectory).filePath(
            debugExecutableName);
    const QString releaseCandidate =
        QDir(releaseCandidateDirectory).filePath(
            releaseExecutableName);
    const QFileDevice::Permissions currentPermissions =
        QFileInfo(currentExecutable).permissions();
    ok = check(
        QDir().mkpath(debugCandidateDirectory) &&
            QDir().mkpath(releaseCandidateDirectory) &&
            QFile::copy(currentExecutable, debugCandidate) &&
            QFile::copy(currentExecutable, releaseCandidate) &&
            QFile::setPermissions(
                debugCandidate,
                currentPermissions) &&
            QFile::setPermissions(
                releaseCandidate,
                currentPermissions),
        "create Release and Debug executable discovery candidates") &&
        ok;
    const QStringList fallbackDirectories{
        debugCandidateDirectory,
        releaseCandidateDirectory
    };
    const QString canonicalDebugCandidate =
        EditorAssetPath::normalizedAbsolutePath(
            QFileInfo(debugCandidate).canonicalFilePath());
    const QString canonicalReleaseCandidate =
        EditorAssetPath::normalizedAbsolutePath(
            QFileInfo(releaseCandidate).canonicalFilePath());
    QSettings discoverySettings(
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral(
                "discovery/editor_config.ini")),
        QSettings::IniFormat);
    validation =
        EditorSettings::readDesktopGameExecutable(
            discoverySettings,
            fallbackDirectories);
    ok = check(
        validation.succeeded() &&
            validation.executablePath ==
                canonicalReleaseCandidate,
        "automatic executable discovery prefers Release across all search directories") &&
        ok;

    discoverySettings.setValue(
        QString::fromLatin1(
            EditorSettings::DesktopGameExecutableKey),
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("removed-explicit-game")));
    validation =
        EditorSettings::readDesktopGameExecutable(
            discoverySettings,
            fallbackDirectories);
    ok = check(
        validation.succeeded() &&
            validation.executablePath ==
                canonicalReleaseCandidate,
        "an invalid explicit executable falls back to an automatically discovered Release build") &&
        ok;

    discoverySettings.setValue(
        QString::fromLatin1(
            EditorSettings::DesktopGameExecutableKey),
        debugCandidate);
    validation =
        EditorSettings::readDesktopGameExecutable(
            discoverySettings,
            fallbackDirectories);
    ok = check(
        validation.succeeded() &&
            validation.executablePath ==
                canonicalDebugCandidate,
        "a valid explicit Debug executable remains authoritative") &&
        ok;

    discoverySettings.remove(
        QString::fromLatin1(
            EditorSettings::DesktopGameExecutableKey));
    ok = check(
        QFile::remove(releaseCandidate),
        "remove the Release candidate for Debug fallback validation") &&
        ok;
    validation =
        EditorSettings::readDesktopGameExecutable(
            discoverySettings,
            fallbackDirectories);
    ok = check(
        validation.succeeded() &&
            validation.executablePath ==
                canonicalDebugCandidate,
        "automatic executable discovery uses Debug only when Release is unavailable") &&
        ok;

    const QString defaultLayoutRoot =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("default-layout"));
    const QString defaultLayoutApplicationDirectory =
        QDir(defaultLayoutRoot).filePath(
            QStringLiteral(
                "jxqy-editor/build/Debug"));
#ifdef Q_OS_WIN
    const QString defaultLayoutRuntimeDirectory =
        QDir(defaultLayoutRoot).filePath(
            sizeof(void*) == 8
            ? QStringLiteral("bin/win64/Release")
            : QStringLiteral("bin/win32/Release"));
#elif defined(Q_OS_MACOS)
    const QString defaultLayoutRuntimeDirectory =
        QDir(defaultLayoutRoot).filePath(
            QStringLiteral("bin/macos/Release"));
#else
    const QString defaultLayoutRuntimeDirectory =
        QDir(defaultLayoutRoot).filePath(
            QStringLiteral("bin/linux/Release"));
#endif
    const QString defaultLayoutReleaseExecutable =
        QDir(defaultLayoutRuntimeDirectory).filePath(
            releaseExecutableName);
    ok = check(
        writeFile(
            QDir(defaultLayoutRoot).filePath(
                QStringLiteral("CMakeLists.txt")),
            QByteArray("project(jxqy-layout-fixture)\n")) &&
            QDir().mkpath(
                defaultLayoutApplicationDirectory) &&
            QDir().mkpath(
                defaultLayoutRuntimeDirectory) &&
            QFile::copy(
                currentExecutable,
                defaultLayoutReleaseExecutable) &&
            QFile::setPermissions(
                defaultLayoutReleaseExecutable,
                currentPermissions),
        "create the standard repository Release layout") &&
        ok;
    validation =
        EditorSettings::
            discoverDefaultDesktopGameExecutable(
                defaultLayoutApplicationDirectory);
    ok = check(
        validation.succeeded() &&
            validation.executablePath ==
                EditorAssetPath::normalizedAbsolutePath(
                    QFileInfo(
                        defaultLayoutReleaseExecutable)
                        .canonicalFilePath()),
        "default discovery finds the repository Release output from a nested editor build") &&
        ok;

    const QString executableLink =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("linked-game.exe"));
    const QString alternateExecutable =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("alternate-game.exe"));
    ok = check(
        QFile::copy(
            currentExecutable,
            alternateExecutable) &&
            QFile::setPermissions(
                alternateExecutable,
                QFileInfo(currentExecutable)
                    .permissions()),
        "create a second executable target for link-swap validation") &&
        ok;
    std::error_code executableLinkError;
    fs::create_symlink(
        hostPath(currentExecutable),
        hostPath(executableLink),
        executableLinkError);
    if (executableLinkError)
    {
        std::cout
            << "(executable symlink swap check skipped: "
            << executableLinkError.message()
            << ")\n";
    }
    else
    {
        validation =
            EditorSettings::
                validateDesktopGameExecutable(
                    executableLink);
        ok = check(
            validation.succeeded() &&
                validation.executablePath ==
                    canonicalCurrentExecutable,
            "an executable symlink resolves to the canonical final target") &&
            ok;
        QString linkedExecutableError;
        ok = check(
            EditorSettings::writeDesktopGameExecutable(
                settings,
                executableLink,
                &linkedExecutableError) &&
                settings.value(
                    QString::fromLatin1(
                        EditorSettings::
                            DesktopGameExecutableKey))
                        .toString() ==
                    canonicalCurrentExecutable,
            "persisting a selected executable link stores its canonical target") &&
            ok;

        std::error_code removeExecutableLinkError;
        fs::remove(
            hostPath(executableLink),
            removeExecutableLinkError);
        std::error_code swappedExecutableLinkError;
        if (!removeExecutableLinkError)
        {
            fs::create_symlink(
                hostPath(alternateExecutable),
                hostPath(executableLink),
                swappedExecutableLinkError);
        }
        const bool linkSwapped =
            !removeExecutableLinkError &&
            !swappedExecutableLinkError;
        ok = check(
            linkSwapped,
            "swap the selected executable link to another target") &&
            ok;
        if (linkSwapped)
        {
            const EditorSettings::
                DesktopExecutableValidation
                    swappedLinkValidation =
                        EditorSettings::
                            validateDesktopGameExecutable(
                                executableLink);
            const EditorSettings::
                DesktopExecutableValidation
                    persistedLinkValidation =
                        EditorSettings::
                            readDesktopGameExecutable(
                                settings);
            ok = check(
                swappedLinkValidation.succeeded() &&
                    swappedLinkValidation.executablePath ==
                        EditorAssetPath::
                            normalizedAbsolutePath(
                                QFileInfo(
                                    alternateExecutable)
                                    .canonicalFilePath()) &&
                    persistedLinkValidation.succeeded() &&
                    persistedLinkValidation.executablePath ==
                        canonicalCurrentExecutable,
                "a later symlink swap cannot retarget the persisted executable") &&
                ok;
        }
    }

#ifdef Q_OS_MACOS
    const QString bundlePath =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("Jxqy Game.app"));
    const QString bundleMainExecutable =
        QDir(bundlePath).filePath(
            QStringLiteral("Contents/MacOS/JxqyGame"));
    const QString bundleHelperExecutable =
        QDir(bundlePath).filePath(
            QStringLiteral("Contents/MacOS/JxqyHelper"));
    const QFileDevice::Permissions executablePermissions =
        QFileDevice::ReadOwner |
        QFileDevice::WriteOwner |
        QFileDevice::ExeOwner |
        QFileDevice::ReadUser |
        QFileDevice::ExeUser |
        QFileDevice::ReadGroup |
        QFileDevice::ExeGroup |
        QFileDevice::ReadOther |
        QFileDevice::ExeOther;
    ok = check(
        writeFile(
            QDir(bundlePath).filePath(
                QStringLiteral("Contents/Info.plist")),
            QByteArray(
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<!DOCTYPE plist PUBLIC "
                "\"-//Apple//DTD PLIST 1.0//EN\" "
                "\"http://www.apple.com/DTDs/"
                "PropertyList-1.0.dtd\">\n"
                "<plist version=\"1.0\"><dict>"
                "<key>CFBundleExecutable</key>"
                "<string>JxqyGame</string>"
                "</dict></plist>\n")) &&
            writeFile(
                bundleMainExecutable,
                QByteArray("#!/bin/sh\nexit 0\n")) &&
            writeFile(
                bundleHelperExecutable,
                QByteArray("#!/bin/sh\nexit 0\n")) &&
            QFile::setPermissions(
                bundleMainExecutable,
                executablePermissions) &&
            QFile::setPermissions(
                bundleHelperExecutable,
                executablePermissions),
        "create a macOS bundle with a main and helper executable") &&
        ok;
    validation = EditorSettings::validateDesktopGameExecutable(
        bundlePath);
    ok = check(
        validation.succeeded() &&
            validation.executablePath ==
                EditorAssetPath::normalizedAbsolutePath(
                    bundleMainExecutable),
        "macOS bundle selection follows CFBundleExecutable exactly") &&
        ok;
#endif

    QString errorMessage;
    ok = check(
        EditorSettings::writeDesktopGameExecutable(
            settings,
            currentExecutable,
            &errorMessage) &&
            errorMessage.isEmpty(),
        "desktop executable setting is persisted") && ok;
    QSettings reopened(settingsPath, QSettings::IniFormat);
    validation =
        EditorSettings::readDesktopGameExecutable(reopened);
    ok = check(
        validation.succeeded() &&
            validation.executablePath ==
                canonicalCurrentExecutable,
        "desktop executable setting survives a settings reopen") && ok;

    const QString persistedPath = validation.executablePath;
    ok = check(
        !EditorSettings::writeDesktopGameExecutable(
            reopened,
            temporaryDirectory.path(),
            &errorMessage) &&
            !errorMessage.isEmpty() &&
            reopened.value(
                QString::fromLatin1(
                    EditorSettings::DesktopGameExecutableKey))
                .toString() == persistedPath,
        "invalid executable selection preserves the previous setting") &&
        ok;
    return ok;
}

bool waitForController(
    DesktopRunController& controller,
    int timeoutMilliseconds)
{
    if (controller.state() == DesktopRunState::Finished)
        return true;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(
        &controller,
        &DesktopRunController::runFinished,
        &loop,
        &QEventLoop::quit,
        Qt::QueuedConnection);
    QObject::connect(
        &timeout,
        &QTimer::timeout,
        &loop,
        &QEventLoop::quit);
    if (controller.state() != DesktopRunState::Finished)
    {
        timeout.start(timeoutMilliseconds);
        loop.exec();
    }
    return controller.state() == DesktopRunState::Finished;
}

bool waitForControllerState(
    DesktopRunController& controller,
    DesktopRunState expectedState,
    int timeoutMilliseconds)
{
    if (controller.state() == expectedState)
        return true;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(
        &controller,
        &DesktopRunController::stateChanged,
        &loop,
        [&loop, expectedState](DesktopRunState state)
        {
            if (state == expectedState ||
                state == DesktopRunState::Finished)
            {
                loop.quit();
            }
        },
        Qt::QueuedConnection);
    QObject::connect(
        &timeout,
        &QTimer::timeout,
        &loop,
        &QEventLoop::quit);
    if (controller.state() != expectedState &&
        controller.state() != DesktopRunState::Finished)
    {
        timeout.start(timeoutMilliseconds);
        loop.exec();
    }
    return controller.state() == expectedState;
}

bool copyExecutable(
    const QString& sourcePath,
    const QString& targetPath)
{
    if (!QDir().mkpath(QFileInfo(targetPath).absolutePath()) ||
        !QFile::copy(sourcePath, targetPath))
    {
        return false;
    }
#ifdef Q_OS_WIN
    const QDir sourceDirectory(
        QFileInfo(sourcePath).absolutePath());
    const QDir targetDirectory(
        QFileInfo(targetPath).absolutePath());
    const QStringList runtimeLibraries =
        sourceDirectory.entryList(
            {
                QStringLiteral("libgcc_s_*.dll"),
                QStringLiteral("libstdc++-6.dll"),
                QStringLiteral("libwinpthread-1.dll")
            },
            QDir::Files);
    for (const QString& runtimeLibrary : runtimeLibraries)
    {
        const QString targetLibrary =
            targetDirectory.filePath(runtimeLibrary);
        if (!QFileInfo::exists(targetLibrary) &&
            !QFile::copy(
                sourceDirectory.filePath(runtimeLibrary),
                targetLibrary))
        {
            return false;
        }
    }
#endif
    const QFileDevice::Permissions executablePermissions =
        QFileInfo(sourcePath).permissions() |
        QFileDevice::ReadOwner |
        QFileDevice::ExeOwner |
        QFileDevice::ReadUser |
        QFileDevice::ExeUser;
    return QFile::setPermissions(
        targetPath,
        executablePermissions);
}

DesktopRunSessionWorkspace controllerWorkspace(
    const QString& sessionRoot,
    const QByteArray& descriptorBytes,
    const QString& resourceRoot)
{
    DesktopRunSessionWorkspace workspace;
    workspace.sessionId =
        QFileInfo(sessionRoot).fileName();
    workspace.paths.sessionRoot =
        sessionRoot;
    const QDir sessionDirectory(sessionRoot);
    workspace.paths.overlayRoot =
        sessionDirectory.filePath(
            QStringLiteral("overlay"));
    workspace.paths.isolatedSaveRoot =
        sessionDirectory.filePath(
            QStringLiteral("save"));
    workspace.paths.applicationStateRoot =
        sessionDirectory.filePath(
            QStringLiteral("application-state"));
    workspace.paths.diagnosticsRoot =
        sessionDirectory.filePath(
            QStringLiteral("diagnostics"));
    const QDir diagnosticsDirectory(
        workspace.paths.diagnosticsRoot);
    workspace.paths.diagnosticsPath =
        diagnosticsDirectory.filePath(
            QStringLiteral("diagnostics.jsonl"));
    workspace.paths.logPath =
        diagnosticsDirectory.filePath(
            QStringLiteral("game.log"));
    workspace.paths.runtimeTracePath =
        diagnosticsDirectory.filePath(
            QStringLiteral("runtime-trace.jsonl"));
    workspace.paths.markerPath =
        sessionDirectory.filePath(
            QStringLiteral("session-marker.json"));
    workspace.paths.resourceRoutingContractPath =
        sessionDirectory.filePath(
            QStringLiteral(
                "resource-routing-contract.json"));
    workspace.paths.descriptorPath =
        sessionDirectory.filePath(
            QStringLiteral("launch-descriptor.json"));

    workspace.descriptor.sessionId =
        workspace.sessionId.toUtf8().toStdString();
    workspace.descriptor.overlayRoot =
        hostPath(workspace.paths.overlayRoot);
    workspace.descriptor.isolatedSaveRoot =
        hostPath(workspace.paths.isolatedSaveRoot);
    workspace.descriptor.applicationStateRoot =
        hostPath(
            workspace.paths.applicationStateRoot);
    workspace.descriptor.diagnosticsPath =
        hostPath(workspace.paths.diagnosticsPath);
    workspace.descriptor.logPath =
        hostPath(workspace.paths.logPath);
    workspace.descriptorSha256 =
        QCryptographicHash::hash(
            descriptorBytes,
            QCryptographicHash::Sha256)
            .toHex();
    workspace.formalRoots.resourceRoots = {resourceRoot};
    workspace.formalRoots.saveRoot = resourceRoot;
    return workspace;
}

bool createControllerWorkspaceFixture(
    const QString& root,
    const QByteArray& descriptorBytes,
    DesktopRunSessionWorkspace* workspace,
    QString* resourceRootPath = nullptr)
{
    const QString sessionRoot =
        QDir(root).filePath(
            QStringLiteral(
                "11111111-2222-3333-4444-555555555555"));
    const QString formalRoot =
        QDir(root).filePath(
            QString::fromUtf8("正式 资源"));
    if (!QDir().mkpath(sessionRoot) ||
        !QDir().mkpath(formalRoot) ||
        !writeFile(
            QDir(formalRoot).filePath(
                QString::fromUtf8("资源 标记.txt")),
            QByteArray("formal")))
    {
        return false;
    }
    *workspace = controllerWorkspace(
        sessionRoot,
        descriptorBytes,
        formalRoot);
    if (resourceRootPath)
        *resourceRootPath = formalRoot;
    return writeFile(
        workspace->paths.descriptorPath,
        descriptorBytes);
}

#ifndef Q_OS_WIN
bool overwriteFileInPlace(
    const QString& path,
    const QByteArray& bytes,
    QString* errorMessage)
{
    QFile file(path);
    if (!file.open(
            QIODevice::WriteOnly |
            QIODevice::Truncate))
    {
        *errorMessage = file.errorString();
        return false;
    }
    if (file.write(bytes) != bytes.size() ||
        !file.flush())
    {
        *errorMessage = file.errorString();
        return false;
    }
    file.close();
    errorMessage->clear();
    return true;
}

struct PosixTestFileIdentity
{
    dev_t device = 0;
    ino_t inode = 0;
    nlink_t linkCount = 0;
    off_t size = -1;
};

bool queryPosixTestFileIdentity(
    const QString& path,
    PosixTestFileIdentity* identity)
{
    struct stat information = {};
    if (::lstat(
            QFile::encodeName(path).constData(),
            &information) != 0 ||
        !S_ISREG(information.st_mode) ||
        S_ISLNK(information.st_mode))
    {
        return false;
    }
    identity->device = information.st_dev;
    identity->inode = information.st_ino;
    identity->linkCount = information.st_nlink;
    identity->size = information.st_size;
    return true;
}

bool samePosixTestFileIdentity(
    const PosixTestFileIdentity& left,
    const PosixTestFileIdentity& right)
{
    return left.device == right.device &&
        left.inode == right.inode &&
        left.linkCount == right.linkCount &&
        left.size == right.size;
}
#endif

enum class LaunchReplacementTarget
{
    Executable,
    Descriptor,
    WorkingDirectory
};

const char* launchReplacementName(
    LaunchReplacementTarget target)
{
    switch (target)
    {
    case LaunchReplacementTarget::Executable:
        return "executable";
    case LaunchReplacementTarget::Descriptor:
        return "descriptor";
    case LaunchReplacementTarget::WorkingDirectory:
        return "working directory";
    }
    return "unknown";
}

bool runLaunchIdentityReplacementTest(
    const QString& fixturePath,
    LaunchReplacementTarget target)
{
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create launch identity replacement fixture"))
    {
        return false;
    }

#ifdef Q_OS_WIN
    const QString executableFileName =
        QStringLiteral("pinned-game.exe");
    const QString replacementExecutableFileName =
        QStringLiteral("replacement-game.exe");
#else
    const QString executableFileName =
        QStringLiteral("pinned-game");
    const QString replacementExecutableFileName =
        QStringLiteral("replacement-game");
#endif
    const QString executablePath =
        QDir(temporaryDirectory.path()).filePath(
            executableFileName);
    const QString replacementExecutablePath =
        QDir(temporaryDirectory.path()).filePath(
            replacementExecutableFileName);
    const QString workingDirectory =
        QDir(temporaryDirectory.path()).filePath(
            QString::fromUtf8("固定 会话"));
    const QString descriptorPath =
        QDir(workingDirectory).filePath(
            QString::fromUtf8("启动 描述.txt"));
    if (!check(
            copyExecutable(
                fixturePath,
                executablePath) &&
                copyExecutable(
                    fixturePath,
                    replacementExecutablePath) &&
                writeFile(
                    descriptorPath,
                    QByteArray("success")),
            "create launch identity replacement inputs"))
    {
        return false;
    }

    DesktopRunController controller;
    bool replacementAttempted = false;
    bool replacementChangedIdentity = false;
    bool replacementPathCreated = false;
    std::error_code replacementError;
    QObject::connect(
        &controller,
        &DesktopRunController::stateChanged,
        &controller,
        [&](DesktopRunState state)
        {
            if (state != DesktopRunState::Starting)
                return;
            replacementAttempted = true;
            switch (target)
            {
            case LaunchReplacementTarget::Executable:
            {
                const QString movedExecutablePath =
                    executablePath +
                    QStringLiteral(".original");
                fs::rename(
                    hostPath(executablePath),
                    hostPath(movedExecutablePath),
                    replacementError);
                replacementChangedIdentity =
                    !replacementError;
                if (replacementChangedIdentity)
                {
                    replacementPathCreated =
                        copyExecutable(
                            replacementExecutablePath,
                            executablePath);
                }
                break;
            }
            case LaunchReplacementTarget::Descriptor:
            {
                const QString movedDescriptorPath =
                    descriptorPath +
                    QStringLiteral(".original");
                fs::rename(
                    hostPath(descriptorPath),
                    hostPath(movedDescriptorPath),
                    replacementError);
                replacementChangedIdentity =
                    !replacementError;
                if (replacementChangedIdentity)
                {
                    replacementPathCreated =
                        writeFile(
                            descriptorPath,
                            QByteArray("nonzero"));
                }
                break;
            }
            case LaunchReplacementTarget::WorkingDirectory:
            {
                const QString movedWorkingDirectory =
                    workingDirectory +
                    QStringLiteral(".original");
                fs::rename(
                    hostPath(workingDirectory),
                    hostPath(movedWorkingDirectory),
                    replacementError);
                replacementChangedIdentity =
                    !replacementError;
                if (replacementChangedIdentity)
                {
                    replacementPathCreated =
                        writeFile(
                            descriptorPath,
                            QByteArray("nonzero"));
                }
                break;
            }
            }
        },
        Qt::DirectConnection);

    const bool startResult =
        controller.start(
            executablePath,
            descriptorPath,
            workingDirectory);
    bool protectedResult = false;
    if (replacementChangedIdentity)
    {
        protectedResult =
            replacementAttempted &&
            replacementPathCreated &&
            !startResult &&
            controller.state() ==
                DesktopRunState::Finished &&
            controller.outcome() ==
                DesktopRunOutcome::FailedToStart &&
            controller.exitCode() == -1 &&
            !controller.lastError().isEmpty();
    }
    else
    {
        protectedResult =
            replacementAttempted &&
            startResult &&
            waitForController(controller, 5000) &&
            controller.outcome() ==
                DesktopRunOutcome::Succeeded &&
            controller.exitCode() == 0;
    }
    if (!protectedResult)
    {
        std::cerr
            << "replacement target="
            << launchReplacementName(target)
            << " attempted="
            << replacementAttempted
            << " changed="
            << replacementChangedIdentity
            << " created="
            << replacementPathCreated
            << " error="
            << replacementError.message()
            << " start="
            << startResult
            << " state="
            << static_cast<int>(controller.state())
            << " outcome="
            << static_cast<int>(controller.outcome())
            << " exit="
            << controller.exitCode()
            << " controller-error="
            << controller.lastError().toStdString()
            << '\n';
    }
    return check(
        protectedResult,
        target == LaunchReplacementTarget::Executable
            ? "Starting DirectConnection cannot retarget the executable"
            : target == LaunchReplacementTarget::Descriptor
                ? "Starting DirectConnection cannot retarget the descriptor"
                : "Starting DirectConnection cannot retarget the working directory");
}

bool runLaunchContentOverwriteTest(
    const QString& fixturePath,
    LaunchReplacementTarget target)
{
#ifdef Q_OS_WIN
    Q_UNUSED(fixturePath);
    Q_UNUSED(target);
    std::cout
        << "(POSIX in-place launch content overwrite check skipped on Windows)\n";
    return true;
#else
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create launch content overwrite fixture"))
    {
        return false;
    }

    const QString executableFileName =
        QStringLiteral("content-pinned-game");
    const QString executablePath =
        QDir(temporaryDirectory.path()).filePath(
            executableFileName);
    const QString workingDirectory =
        QDir(temporaryDirectory.path()).filePath(
            QString::fromUtf8("内容固定 会话"));
    const QString descriptorPath =
        QDir(workingDirectory).filePath(
            QString::fromUtf8("启动 内容描述.txt"));
    if (!check(
            copyExecutable(
                fixturePath,
                executablePath) &&
                writeFile(
                    descriptorPath,
                    QByteArray("success")),
            "create launch content overwrite inputs"))
    {
        return false;
    }

    const QByteArray originalExecutable =
        readFile(executablePath);
    if (!check(
            !originalExecutable.isEmpty(),
            "read the copied executable before content overwrite"))
    {
        return false;
    }
    QByteArray changedExecutable = originalExecutable;
    changedExecutable[0] = static_cast<char>(
        static_cast<unsigned char>(
            changedExecutable.at(0)) ^
        0x01u);
    const QByteArray changedBytes =
        target == LaunchReplacementTarget::Executable
        ? changedExecutable
        : QByteArray("nonzero");

    PosixTestFileIdentity identityBeforeOverwrite;
    if (!check(
            queryPosixTestFileIdentity(
                target == LaunchReplacementTarget::Executable
                    ? executablePath
                    : descriptorPath,
                &identityBeforeOverwrite),
            "capture POSIX identity before in-place overwrite"))
    {
        return false;
    }

    DesktopRunController controller;
    bool overwriteAttempted = false;
    bool overwriteSucceeded = false;
    bool overwritePreservedIdentity = false;
    QString overwriteError;
    int processStartedCount = 0;
    int runFinishedCount = 0;
    DesktopRunOutcome finishedOutcome =
        DesktopRunOutcome::None;
    int finishedExitCode = 0;
    bool finishedForcedKill = false;
    QObject::connect(
        &controller,
        &DesktopRunController::stateChanged,
        &controller,
        [&](DesktopRunState state)
        {
            if (state == DesktopRunState::Running)
            {
                ++processStartedCount;
                return;
            }
            if (state != DesktopRunState::Starting)
                return;
            overwriteAttempted = true;
            const QString overwritePath =
                target == LaunchReplacementTarget::Executable
                ? executablePath
                : descriptorPath;
            overwriteSucceeded = overwriteFileInPlace(
                overwritePath,
                changedBytes,
                &overwriteError);
            PosixTestFileIdentity identityAfterOverwrite;
            overwritePreservedIdentity =
                overwriteSucceeded &&
                queryPosixTestFileIdentity(
                    overwritePath,
                    &identityAfterOverwrite) &&
                samePosixTestFileIdentity(
                    identityBeforeOverwrite,
                    identityAfterOverwrite);
        },
        Qt::DirectConnection);
    QObject::connect(
        &controller,
        &DesktopRunController::runFinished,
        &controller,
        [&](DesktopRunOutcome outcome,
            int exitCode,
            bool forcedKill)
        {
            ++runFinishedCount;
            finishedOutcome = outcome;
            finishedExitCode = exitCode;
            finishedForcedKill = forcedKill;
        },
        Qt::DirectConnection);

    const bool startResult =
        controller.start(
            executablePath,
            descriptorPath,
            workingDirectory);
    const bool protectedResult =
        overwriteAttempted &&
        overwriteSucceeded &&
        overwritePreservedIdentity &&
        !startResult &&
        processStartedCount == 0 &&
        runFinishedCount == 1 &&
        controller.state() ==
            DesktopRunState::Finished &&
        controller.outcome() ==
            DesktopRunOutcome::FailedToStart &&
        controller.exitCode() == -1 &&
        finishedOutcome ==
            DesktopRunOutcome::FailedToStart &&
        finishedExitCode == -1 &&
        !finishedForcedKill &&
        !controller.lastError().isEmpty();
    if (!protectedResult)
    {
        std::cerr
            << "content overwrite target="
            << launchReplacementName(target)
            << " attempted="
            << overwriteAttempted
            << " succeeded="
            << overwriteSucceeded
            << " same-identity="
            << overwritePreservedIdentity
            << " start-result="
            << startResult
            << " process-started-count="
            << processStartedCount
            << " finished-count="
            << runFinishedCount
            << " outcome="
            << static_cast<int>(controller.outcome())
            << " exit="
            << controller.exitCode()
            << " overwrite-error="
            << overwriteError.toStdString()
            << " controller-error="
            << controller.lastError().toStdString()
            << '\n';
    }
    return check(
        protectedResult,
        target == LaunchReplacementTarget::Executable
            ? "Starting DirectConnection cannot overwrite executable content in place"
            : "Starting DirectConnection cannot overwrite descriptor content in place");
#endif
}

struct FinishedPayload
{
    DesktopRunOutcome outcome = DesktopRunOutcome::None;
    int exitCode = 0;
    bool forcedKill = false;
};

bool runStartingCancellationNoSpawnTest(
    const QString& fixturePath)
{
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create Starting cancellation fixture"))
    {
        return false;
    }
    const QString descriptorPath =
        QDir(temporaryDirectory.path()).filePath(
            QString::fromUtf8(
                "取消前启动 描述.txt"));
    if (!check(
            writeFile(
                descriptorPath,
                QByteArray("success")),
            "create Starting cancellation descriptor"))
    {
        return false;
    }

    DesktopRunController controller;
    QList<DesktopRunState> states;
    QList<FinishedPayload> payloads;
    QString standardOutput;
    QString standardError;
    int runningStateCount = 0;
    bool cancellationRequested = false;
    QObject::connect(
        &controller,
        &DesktopRunController::standardOutputReceived,
        &controller,
        [&](const QString& text)
        {
            standardOutput += text;
        },
        Qt::DirectConnection);
    QObject::connect(
        &controller,
        &DesktopRunController::standardErrorReceived,
        &controller,
        [&](const QString& text)
        {
            standardError += text;
        },
        Qt::DirectConnection);
    QObject::connect(
        &controller,
        &DesktopRunController::stateChanged,
        &controller,
        [&](DesktopRunState state)
        {
            states.append(state);
            if (state == DesktopRunState::Running)
                ++runningStateCount;
            if (state == DesktopRunState::Starting &&
                !cancellationRequested)
            {
                cancellationRequested = true;
                controller.requestStop();
            }
        },
        Qt::DirectConnection);
    QObject::connect(
        &controller,
        &DesktopRunController::runFinished,
        &controller,
        [&](DesktopRunOutcome outcome,
            int exitCode,
            bool forcedKill)
        {
            FinishedPayload payload;
            payload.outcome = outcome;
            payload.exitCode = exitCode;
            payload.forcedKill = forcedKill;
            payloads.append(payload);
        },
        Qt::DirectConnection);

    const bool startResult =
        controller.start(
            fixturePath,
            descriptorPath,
            temporaryDirectory.path());
    QEventLoop observationLoop;
    QTimer observationTimer;
    observationTimer.setSingleShot(true);
    QObject::connect(
        &observationTimer,
        &QTimer::timeout,
        &observationLoop,
        &QEventLoop::quit);
    observationTimer.start(500);
    observationLoop.exec();

    const QList<DesktopRunState> expectedStates{
        DesktopRunState::Starting,
        DesktopRunState::Stopping,
        DesktopRunState::Finished
    };
    const bool payloadValid =
        payloads.size() == 1 &&
        payloads.constFirst().outcome ==
            DesktopRunOutcome::StoppedByUser &&
        payloads.constFirst().exitCode == -1 &&
        !payloads.constFirst().forcedKill;
    const bool cancellationProtected =
        cancellationRequested &&
        !startResult &&
        states == expectedStates &&
        runningStateCount == 0 &&
        standardOutput.isEmpty() &&
        standardError.isEmpty() &&
        payloadValid &&
        !controller.isActive() &&
        controller.state() ==
            DesktopRunState::Finished &&
        controller.outcome() ==
            DesktopRunOutcome::StoppedByUser &&
        controller.exitCode() == -1 &&
        !controller.forcedKill() &&
        controller.lastError().isEmpty();
    if (!cancellationProtected)
    {
        std::cerr
            << "Starting cancellation start="
            << startResult
            << " requested="
            << cancellationRequested
            << " running-count="
            << runningStateCount
            << " payload-count="
            << payloads.size()
            << " state="
            << static_cast<int>(controller.state())
            << " outcome="
            << static_cast<int>(controller.outcome())
            << " exit="
            << controller.exitCode()
            << " forced="
            << controller.forcedKill()
            << " stdout="
            << standardOutput.toUtf8().toHex().constData()
            << " stderr="
            << standardError.toUtf8().toHex().constData()
            << " states=";
        for (DesktopRunState state : states)
            std::cerr << static_cast<int>(state) << ',';
        std::cerr << '\n';
    }
    return check(
        cancellationProtected,
        "Starting DirectConnection cancellation does not spawn the fixture");
}

bool runStartingCancellationReentrantRestartTest(
    const QString& fixturePath)
{
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create Starting cancellation reentry fixture"))
    {
        return false;
    }
    const QString descriptorPath =
        QDir(temporaryDirectory.path()).filePath(
            QString::fromUtf8(
                "取消重入 描述.txt"));
    if (!check(
            writeFile(
                descriptorPath,
                QByteArray("success")),
            "create Starting cancellation reentry descriptor"))
    {
        return false;
    }

    DesktopRunController controller;
    QList<DesktopRunState> states;
    QList<FinishedPayload> payloads;
    QString standardOutput;
    QString standardError;
    int startingStateCount = 0;
    int runningStateCount = 0;
    int finishedStateCount = 0;
    bool secondStartAttempted = false;
    bool secondStartResult = false;
    QObject::connect(
        &controller,
        &DesktopRunController::standardOutputReceived,
        &controller,
        [&](const QString& text)
        {
            standardOutput += text;
        },
        Qt::DirectConnection);
    QObject::connect(
        &controller,
        &DesktopRunController::standardErrorReceived,
        &controller,
        [&](const QString& text)
        {
            standardError += text;
        },
        Qt::DirectConnection);
    QObject::connect(
        &controller,
        &DesktopRunController::stateChanged,
        &controller,
        [&](DesktopRunState state)
        {
            states.append(state);
            if (state == DesktopRunState::Starting)
            {
                ++startingStateCount;
                if (startingStateCount == 1)
                    controller.requestStop();
                return;
            }
            if (state == DesktopRunState::Running)
            {
                ++runningStateCount;
                return;
            }
            if (state != DesktopRunState::Finished)
                return;
            ++finishedStateCount;
            if (finishedStateCount == 1)
            {
                secondStartAttempted = true;
                secondStartResult =
                    controller.start(
                        fixturePath,
                        descriptorPath,
                        temporaryDirectory.path());
            }
        },
        Qt::DirectConnection);
    QObject::connect(
        &controller,
        &DesktopRunController::runFinished,
        &controller,
        [&](DesktopRunOutcome outcome,
            int exitCode,
            bool forcedKill)
        {
            FinishedPayload payload;
            payload.outcome = outcome;
            payload.exitCode = exitCode;
            payload.forcedKill = forcedKill;
            payloads.append(payload);
        },
        Qt::DirectConnection);

    const bool firstStartResult =
        controller.start(
            fixturePath,
            descriptorPath,
            temporaryDirectory.path());
    const bool secondRunFinished =
        waitForController(controller, 5000);
    const QList<DesktopRunState> expectedStates{
        DesktopRunState::Starting,
        DesktopRunState::Stopping,
        DesktopRunState::Finished,
        DesktopRunState::Starting,
        DesktopRunState::Running,
        DesktopRunState::Finished
    };
    const bool payloadsValid =
        payloads.size() == 2 &&
        payloads.at(0).outcome ==
            DesktopRunOutcome::StoppedByUser &&
        payloads.at(0).exitCode == -1 &&
        !payloads.at(0).forcedKill &&
        payloads.at(1).outcome ==
            DesktopRunOutcome::Succeeded &&
        payloads.at(1).exitCode == 0 &&
        !payloads.at(1).forcedKill;
    const bool reentryProtected =
        !firstStartResult &&
        secondStartAttempted &&
        secondStartResult &&
        secondRunFinished &&
        startingStateCount == 2 &&
        runningStateCount == 1 &&
        finishedStateCount == 2 &&
        states == expectedStates &&
        payloadsValid &&
        standardOutput ==
            QString::fromUtf8(
                "标准输出：中文分片\n") &&
        standardError ==
            QString::fromUtf8(
                "错误输出：中文分片\n") &&
        controller.state() ==
            DesktopRunState::Finished &&
        controller.outcome() ==
            DesktopRunOutcome::Succeeded &&
        controller.exitCode() == 0 &&
        !controller.forcedKill();
    if (!reentryProtected)
    {
        std::cerr
            << "Starting cancellation reentry first-start="
            << firstStartResult
            << " second-attempted="
            << secondStartAttempted
            << " second-start="
            << secondStartResult
            << " second-finished="
            << secondRunFinished
            << " starting-count="
            << startingStateCount
            << " running-count="
            << runningStateCount
            << " finished-count="
            << finishedStateCount
            << " payload-count="
            << payloads.size()
            << " state="
            << static_cast<int>(controller.state())
            << " outcome="
            << static_cast<int>(controller.outcome())
            << " exit="
            << controller.exitCode()
            << " stdout="
            << standardOutput.toUtf8().toHex().constData()
            << " stderr="
            << standardError.toUtf8().toHex().constData()
            << " states=";
        for (DesktopRunState state : states)
            std::cerr << static_cast<int>(state) << ',';
        std::cerr << " payloads=";
        for (const FinishedPayload& payload : payloads)
        {
            std::cerr
                << static_cast<int>(payload.outcome)
                << ':' << payload.exitCode
                << ':' << payload.forcedKill
                << ',';
        }
        std::cerr << '\n';
    }
    return check(
        reentryProtected,
        "Starting cancellation preserves Finished reentry and the next run");
}

bool runFinishedReentrancyPayloadTest(
    const QString& fixturePath)
{
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create Finished reentrancy fixture"))
    {
        return false;
    }
    const QString descriptorPath =
        QDir(temporaryDirectory.path()).filePath(
            QString::fromUtf8("重入 描述.txt"));
    if (!check(
            writeFile(
                descriptorPath,
                QByteArray("hang")),
            "create Finished reentrancy descriptor"))
    {
        return false;
    }

    DesktopRunController controller;
    controller.setStopTimeoutMilliseconds(100);
    bool firstStopRequested = false;
    bool secondStartAttempted = false;
    bool secondDescriptorWritten = false;
    bool secondStartSucceeded = false;
    DesktopRunOutcome finishedStateOutcome =
        DesktopRunOutcome::None;
    int finishedStateExitCode = 0;
    bool finishedStateForcedKill = false;
    QList<FinishedPayload> payloads;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(
        &controller,
        &DesktopRunController::stateChanged,
        &controller,
        [&](DesktopRunState state)
        {
            if (state == DesktopRunState::Running &&
                !firstStopRequested &&
                !secondStartAttempted)
            {
                firstStopRequested = true;
                controller.requestStop();
                return;
            }
            if (state != DesktopRunState::Finished ||
                secondStartAttempted)
            {
                return;
            }
            finishedStateOutcome = controller.outcome();
            finishedStateExitCode = controller.exitCode();
            finishedStateForcedKill =
                controller.forcedKill();
            secondStartAttempted = true;
            secondDescriptorWritten =
                writeFile(
                    descriptorPath,
                    QByteArray("success"));
            secondStartSucceeded =
                secondDescriptorWritten &&
                controller.start(
                    fixturePath,
                    descriptorPath,
                    temporaryDirectory.path());
        },
        Qt::DirectConnection);
    QObject::connect(
        &controller,
        &DesktopRunController::runFinished,
        &controller,
        [&](DesktopRunOutcome outcome,
            int exitCode,
            bool forcedKill)
        {
            payloads.append({
                outcome,
                exitCode,
                forcedKill
            });
            if (payloads.size() == 2)
                loop.quit();
        },
        Qt::DirectConnection);
    QObject::connect(
        &timeout,
        &QTimer::timeout,
        &loop,
        &QEventLoop::quit);

    const bool firstStartSucceeded =
        controller.start(
            fixturePath,
            descriptorPath,
            temporaryDirectory.path());
    if (firstStartSucceeded && payloads.size() < 2)
    {
        timeout.start(5000);
        loop.exec();
    }
    const bool payloadsPreserved =
        firstStartSucceeded &&
        firstStopRequested &&
        secondStartAttempted &&
        secondDescriptorWritten &&
        secondStartSucceeded &&
        payloads.size() == 2 &&
        finishedStateOutcome ==
            DesktopRunOutcome::StoppedByUser &&
        finishedStateForcedKill &&
        payloads.at(0).outcome ==
            finishedStateOutcome &&
        payloads.at(0).exitCode ==
            finishedStateExitCode &&
        payloads.at(0).forcedKill ==
            finishedStateForcedKill &&
        payloads.at(1).outcome ==
            DesktopRunOutcome::Succeeded &&
        payloads.at(1).exitCode == 0 &&
        !payloads.at(1).forcedKill &&
        controller.outcome() ==
            DesktopRunOutcome::Succeeded &&
        controller.exitCode() == 0;
    if (!payloadsPreserved)
    {
        std::cerr
            << "Finished reentrancy first-start="
            << firstStartSucceeded
            << " first-stop="
            << firstStopRequested
            << " second-attempt="
            << secondStartAttempted
            << " descriptor-written="
            << secondDescriptorWritten
            << " second-start="
            << secondStartSucceeded
            << " payload-count="
            << payloads.size()
            << " current-outcome="
            << static_cast<int>(controller.outcome())
            << " current-exit="
            << controller.exitCode()
            << '\n';
        for (const FinishedPayload& payload : payloads)
        {
            std::cerr
                << "  payload outcome="
                << static_cast<int>(payload.outcome)
                << " exit="
                << payload.exitCode
                << " forced="
                << payload.forcedKill
                << '\n';
        }
    }
    return check(
        payloadsPreserved,
        "Finished DirectConnection reentry preserves the completed run payload");
}

bool runTypedWorkspaceLifecycleTest(
    const QString& fixturePath)
{
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create typed workspace lifecycle fixture"))
    {
        return false;
    }

    const QByteArray descriptorBytes("success");
    DesktopRunSessionWorkspace workspace;
    QString resourceRootPath;
    if (!check(
            createControllerWorkspaceFixture(
                temporaryDirectory.path(),
                descriptorBytes,
                &workspace,
                &resourceRootPath),
            "create typed workspace lifecycle inputs"))
    {
        return false;
    }

    const QString resourceMutationPath =
        QDir(resourceRootPath).filePath(
            QString::fromUtf8("运行中修改.txt"));
    DesktopRunController controller;
    bool runningObserved = false;
    bool resourceWriteSucceededWhileRunning = false;
    bool resourceWriteVisibleBeforeFinishedSignal =
        false;
    QString standardOutput;
    QString standardError;
    QObject::connect(
        &controller,
        &DesktopRunController::stateChanged,
        &controller,
        [&](DesktopRunState state)
        {
            if (state == DesktopRunState::Running)
            {
                runningObserved = true;
                resourceWriteSucceededWhileRunning =
                    writeFile(
                        resourceMutationPath,
                        QByteArray("changed"));
            }
            else if (state == DesktopRunState::Finished)
            {
                resourceWriteVisibleBeforeFinishedSignal =
                    readFile(resourceMutationPath) ==
                    QByteArray("changed");
            }
        },
        Qt::DirectConnection);
    QObject::connect(
        &controller,
        &DesktopRunController::standardOutputReceived,
        [&standardOutput](const QString& text)
        {
            standardOutput += text;
        });
    QObject::connect(
        &controller,
        &DesktopRunController::standardErrorReceived,
        [&standardError](const QString& text)
        {
            standardError += text;
        });

    const bool startSucceeded =
        controller.start(
            fixturePath,
            workspace,
            workspace.paths.sessionRoot);
    const bool finished =
        startSucceeded &&
        waitForController(controller, 5000);
    const bool lifecycleProtected =
        startSucceeded &&
        finished &&
        runningObserved &&
        resourceWriteSucceededWhileRunning &&
        resourceWriteVisibleBeforeFinishedSignal &&
        controller.outcome() ==
            DesktopRunOutcome::Succeeded &&
        controller.exitCode() == 0 &&
        standardOutput ==
            QString::fromUtf8(
                "标准输出：中文分片\n") &&
        standardError ==
            QString::fromUtf8(
                "错误输出：中文分片\n");
    if (!lifecycleProtected)
    {
        std::cerr
            << "typed lifecycle start="
            << startSucceeded
            << " finished=" << finished
            << " running=" << runningObserved
            << " write-running="
            << resourceWriteSucceededWhileRunning
            << " write-visible-finished="
            << resourceWriteVisibleBeforeFinishedSignal
            << " outcome="
            << static_cast<int>(controller.outcome())
            << " exit=" << controller.exitCode()
            << " error="
            << controller.lastError().toStdString()
            << '\n';
    }
    return check(
        lifecycleProtected,
        "typed workspace keeps formal resources writable throughout the process lifecycle");
}

bool runTypedWorkspaceDescriptorMismatchTest(
    const QString& fixturePath)
{
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create typed workspace descriptor mismatch fixture"))
    {
        return false;
    }

    DesktopRunSessionWorkspace workspace;
    if (!check(
            createControllerWorkspaceFixture(
                temporaryDirectory.path(),
                QByteArray("success"),
                &workspace),
            "create typed workspace descriptor mismatch inputs"))
    {
        return false;
    }
    workspace.descriptorSha256 =
        QCryptographicHash::hash(
            QByteArray("nonzero"),
            QCryptographicHash::Sha256)
            .toHex();

    DesktopRunController controller;
    int stateChangeCount = 0;
    int finishedCount = 0;
    QString processOutput;
    QObject::connect(
        &controller,
        &DesktopRunController::stateChanged,
        [&stateChangeCount](DesktopRunState)
        {
            ++stateChangeCount;
        });
    QObject::connect(
        &controller,
        &DesktopRunController::runFinished,
        [&finishedCount](
            DesktopRunOutcome,
            int,
            bool)
        {
            ++finishedCount;
        });
    QObject::connect(
        &controller,
        &DesktopRunController::standardOutputReceived,
        [&processOutput](const QString& text)
        {
            processOutput += text;
        });
    QObject::connect(
        &controller,
        &DesktopRunController::standardErrorReceived,
        [&processOutput](const QString& text)
        {
            processOutput += text;
        });

    const bool startResult =
        controller.start(
            fixturePath,
            workspace,
            workspace.paths.sessionRoot);
    QEventLoop observationLoop;
    QTimer::singleShot(
        350,
        &observationLoop,
        &QEventLoop::quit);
    observationLoop.exec();
    const bool rejectedWithoutSpawn =
        !startResult &&
        stateChangeCount == 0 &&
        finishedCount == 0 &&
        processOutput.isEmpty() &&
        controller.state() == DesktopRunState::Idle &&
        controller.outcome() == DesktopRunOutcome::None &&
        !controller.isActive() &&
        !controller.lastError().isEmpty();
    if (!rejectedWithoutSpawn)
    {
        std::cerr
            << "typed mismatch start="
            << startResult
            << " states=" << stateChangeCount
            << " finished=" << finishedCount
            << " output-bytes="
            << processOutput.toUtf8().size()
            << " state="
            << static_cast<int>(controller.state())
            << " outcome="
            << static_cast<int>(controller.outcome())
            << " error="
            << controller.lastError().toStdString()
            << '\n';
    }
    return check(
        rejectedWithoutSpawn,
        "typed workspace descriptor hash mismatch performs zero spawn");
}

bool runTypedWorkspaceValidationTest(
    const QString& fixturePath)
{
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create typed workspace validation fixture"))
    {
        return false;
    }

    DesktopRunSessionWorkspace workspace;
    if (!check(
            createControllerWorkspaceFixture(
                temporaryDirectory.path(),
                QByteArray("success"),
                &workspace),
            "create typed workspace validation inputs"))
    {
        return false;
    }

    DesktopRunController controller;
    int stateChangeCount = 0;
    int finishedCount = 0;
    QObject::connect(
        &controller,
        &DesktopRunController::stateChanged,
        [&stateChangeCount](DesktopRunState)
        {
            ++stateChangeCount;
        });
    QObject::connect(
        &controller,
        &DesktopRunController::runFinished,
        [&finishedCount](
            DesktopRunOutcome,
            int,
            bool)
        {
            ++finishedCount;
        });
    const auto rejected =
        [&](const DesktopRunSessionWorkspace& candidate)
        {
            return !controller.start(
                       fixturePath,
                       candidate,
                       candidate.paths.sessionRoot) &&
                !controller.lastError().isEmpty() &&
                controller.state() ==
                    DesktopRunState::Idle;
        };

    DesktopRunSessionWorkspace missingRoutingContract =
        workspace;
    missingRoutingContract.paths.
        resourceRoutingContractPath.clear();
    const bool missingRoutingContractRejected =
        rejected(missingRoutingContract);

    DesktopRunSessionWorkspace shortHash =
        workspace;
    shortHash.descriptorSha256 =
        QByteArray(63, 'a');
    const bool shortHashRejected =
        rejected(shortHash);

    DesktopRunSessionWorkspace uppercaseHash =
        workspace;
    uppercaseHash.descriptorSha256 =
        QByteArray(64, 'A');
    const bool uppercaseHashRejected =
        rejected(uppercaseHash);

    DesktopRunSessionWorkspace sessionMismatch =
        workspace;
    sessionMismatch.descriptor.sessionId =
        "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
    const bool sessionMismatchRejected =
        rejected(sessionMismatch);

    DesktopRunSessionWorkspace pathMismatch =
        workspace;
    pathMismatch.paths.descriptorPath =
        QDir(pathMismatch.paths.sessionRoot)
            .filePath(
                QStringLiteral(
                    "other-descriptor.json"));
    const bool pathMismatchRejected =
        rejected(pathMismatch);

    DesktopRunSessionWorkspace descriptorPathMismatch =
        workspace;
    descriptorPathMismatch.descriptor.overlayRoot =
        hostPath(
            QDir(
                descriptorPathMismatch.paths.sessionRoot)
                .filePath(
                    QStringLiteral(
                        "other-overlay")));
    const bool descriptorPathMismatchRejected =
        rejected(descriptorPathMismatch);

    DesktopRunSessionWorkspace runtimeTracePathMismatch =
        workspace;
    runtimeTracePathMismatch.paths.runtimeTracePath =
        QDir(
            runtimeTracePathMismatch.paths
                .diagnosticsRoot)
            .filePath(
                QStringLiteral(
                    "other-runtime-trace.jsonl"));
    const bool runtimeTracePathMismatchRejected =
        rejected(runtimeTracePathMismatch);

    QEventLoop observationLoop;
    QTimer::singleShot(
        350,
        &observationLoop,
        &QEventLoop::quit);
    observationLoop.exec();
    const bool validationProtected =
        missingRoutingContractRejected &&
        shortHashRejected &&
        uppercaseHashRejected &&
        sessionMismatchRejected &&
        pathMismatchRejected &&
        descriptorPathMismatchRejected &&
        runtimeTracePathMismatchRejected &&
        stateChangeCount == 0 &&
        finishedCount == 0 &&
        controller.state() == DesktopRunState::Idle &&
        controller.outcome() == DesktopRunOutcome::None;
    if (!validationProtected)
    {
        std::cerr
            << "typed validation missing-routing-contract="
            << missingRoutingContractRejected
            << " short-hash=" << shortHashRejected
            << " uppercase-hash="
            << uppercaseHashRejected
            << " session=" << sessionMismatchRejected
            << " workspace-path="
            << pathMismatchRejected
            << " descriptor-path="
            << descriptorPathMismatchRejected
            << " runtime-trace-path="
            << runtimeTracePathMismatchRejected
            << " states=" << stateChangeCount
            << " finished=" << finishedCount
            << " current-state="
            << static_cast<int>(controller.state())
            << " error="
            << controller.lastError().toStdString()
            << '\n';
    }
    return check(
        validationProtected,
        "typed workspace rejects missing private-session paths and inconsistent contracts");
}

bool runTypedWorkspaceFinishedReentryTest(
    const QString& fixturePath)
{
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create typed workspace Finished reentry fixture"))
    {
        return false;
    }

    DesktopRunSessionWorkspace firstWorkspace;
    QString resourceRootPath;
    if (!check(
            createControllerWorkspaceFixture(
                temporaryDirectory.path(),
                QByteArray("success"),
                &firstWorkspace,
                &resourceRootPath),
            "create typed workspace Finished reentry inputs"))
    {
        return false;
    }
    DesktopRunSessionWorkspace secondWorkspace =
        firstWorkspace;
    const QString resourceMutationPath =
        QDir(resourceRootPath).filePath(
            QString::fromUtf8("重入运行中修改.txt"));

    DesktopRunController controller;
    bool secondStartAttempted = false;
    bool secondStartSucceeded = false;
    bool firstRunningResourceWriteSucceeded = false;
    bool firstFinishedResourceWriteVisible = false;
    bool secondRunningResourceWriteSucceeded = false;
    bool secondFinishedResourceWriteVisible = false;
    int runningCount = 0;
    int finishedStateCount = 0;
    QString standardOutput;
    QString standardError;
    QList<FinishedPayload> payloads;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(
        &controller,
        &DesktopRunController::stateChanged,
        &controller,
        [&](DesktopRunState state)
        {
            if (state == DesktopRunState::Running)
            {
                ++runningCount;
                const bool writeSucceeded =
                    writeFile(
                        resourceMutationPath,
                        QByteArray::number(runningCount));
                if (runningCount == 1)
                    firstRunningResourceWriteSucceeded =
                        writeSucceeded;
                else if (runningCount == 2)
                    secondRunningResourceWriteSucceeded =
                        writeSucceeded;
                return;
            }
            if (state != DesktopRunState::Finished)
                return;
            ++finishedStateCount;
            if (finishedStateCount == 1)
            {
                firstFinishedResourceWriteVisible =
                    readFile(resourceMutationPath) ==
                    QByteArray("1");
                secondStartAttempted = true;
                secondStartSucceeded =
                    controller.start(
                        fixturePath,
                        secondWorkspace,
                        secondWorkspace.paths.sessionRoot);
            }
            else if (finishedStateCount == 2)
            {
                secondFinishedResourceWriteVisible =
                    readFile(resourceMutationPath) ==
                    QByteArray("2");
            }
        },
        Qt::DirectConnection);
    QObject::connect(
        &controller,
        &DesktopRunController::standardOutputReceived,
        [&standardOutput](const QString& text)
        {
            standardOutput += text;
        });
    QObject::connect(
        &controller,
        &DesktopRunController::standardErrorReceived,
        [&standardError](const QString& text)
        {
            standardError += text;
        });
    QObject::connect(
        &controller,
        &DesktopRunController::runFinished,
        &controller,
        [&](DesktopRunOutcome outcome,
            int exitCode,
            bool forcedKill)
        {
            payloads.append({
                outcome,
                exitCode,
                forcedKill
            });
            if (payloads.size() == 2)
                loop.quit();
        },
        Qt::DirectConnection);
    QObject::connect(
        &timeout,
        &QTimer::timeout,
        &loop,
        &QEventLoop::quit);

    const bool firstStartSucceeded =
        controller.start(
            fixturePath,
            firstWorkspace,
            firstWorkspace.paths.sessionRoot);
    if (firstStartSucceeded &&
        payloads.size() < 2)
    {
        timeout.start(5000);
        loop.exec();
    }

    const bool payloadsValid =
        payloads.size() == 2 &&
        payloads.at(0).outcome ==
            DesktopRunOutcome::Succeeded &&
        payloads.at(0).exitCode == 0 &&
        !payloads.at(0).forcedKill &&
        payloads.at(1).outcome ==
            DesktopRunOutcome::Succeeded &&
        payloads.at(1).exitCode == 0 &&
        !payloads.at(1).forcedKill;
    const QString expectedOutput =
        QString::fromUtf8(
            "标准输出：中文分片\n"
            "标准输出：中文分片\n");
    const QString expectedError =
        QString::fromUtf8(
            "错误输出：中文分片\n"
            "错误输出：中文分片\n");
    const bool reentryProtected =
        firstStartSucceeded &&
        secondStartAttempted &&
        secondStartSucceeded &&
        firstRunningResourceWriteSucceeded &&
        firstFinishedResourceWriteVisible &&
        secondRunningResourceWriteSucceeded &&
        secondFinishedResourceWriteVisible &&
        runningCount == 2 &&
        finishedStateCount == 2 &&
        payloadsValid &&
        standardOutput == expectedOutput &&
        standardError == expectedError &&
        controller.outcome() ==
            DesktopRunOutcome::Succeeded &&
        controller.exitCode() == 0;
    if (!reentryProtected)
    {
        std::cerr
            << "typed Finished reentry first-start="
            << firstStartSucceeded
            << " second-attempt="
            << secondStartAttempted
            << " second-start="
            << secondStartSucceeded
            << " first-write-running="
            << firstRunningResourceWriteSucceeded
            << " first-write-visible="
            << firstFinishedResourceWriteVisible
            << " second-write-running="
            << secondRunningResourceWriteSucceeded
            << " second-write-visible="
            << secondFinishedResourceWriteVisible
            << " running-count=" << runningCount
            << " finished-count="
            << finishedStateCount
            << " payload-count="
            << payloads.size()
            << " stdout="
            << standardOutput.toUtf8().toHex().constData()
            << " stderr="
            << standardError.toUtf8().toHex().constData()
            << " error="
            << controller.lastError().toStdString()
            << '\n';
    }
    return check(
        reentryProtected,
        "typed Finished reentry keeps formal resources writable in both runs");
}

bool runDesktopRunControllerTests()
{
    bool ok = true;
    const QString fixturePath =
        qEnvironmentVariable(
            "JXQY_EDITOR_DESKTOP_RUN_FIXTURE");
    ok = check(
        QFileInfo(fixturePath).isFile(),
        "desktop-run process fixture is available") && ok;
    if (fixturePath.isEmpty())
        return false;

    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create desktop-run controller fixture"))
    {
        return false;
    }
    const QString workingDirectory =
        temporaryDirectory.path();
    const QString descriptorPath =
        QDir(workingDirectory).filePath(
            QString::fromUtf8("会话 描述.txt"));

    DesktopRunController controller;
    QString standardOutput;
    QString standardError;
    QList<DesktopRunState> states;
    QObject::connect(
        &controller,
        &DesktopRunController::standardOutputReceived,
        [&standardOutput](const QString& text)
        {
            standardOutput += text;
        });
    QObject::connect(
        &controller,
        &DesktopRunController::standardErrorReceived,
        [&standardError](const QString& text)
        {
            standardError += text;
        });
    QObject::connect(
        &controller,
        &DesktopRunController::stateChanged,
        [&states](DesktopRunState state)
        {
            states.append(state);
        });

    ok = check(
        writeFile(
            descriptorPath,
            QByteArray("success")) &&
            controller.start(
                fixturePath,
                descriptorPath,
                workingDirectory) &&
            waitForController(controller, 5000),
        "controller starts the exact helper process and observes completion") &&
        ok;
    const bool successfulOutput =
        controller.outcome() ==
                DesktopRunOutcome::Succeeded &&
            controller.exitCode() == 0 &&
            standardOutput ==
                QString::fromUtf8(
                    "标准输出：中文分片\n") &&
            standardError ==
                QString::fromUtf8(
                    "错误输出：中文分片\n") &&
            states.contains(DesktopRunState::Starting) &&
            states.contains(DesktopRunState::Running) &&
            states.constLast() ==
                DesktopRunState::Finished;
    if (!successfulOutput)
    {
        std::cerr
            << "success outcome="
            << static_cast<int>(controller.outcome())
            << " exit=" << controller.exitCode()
            << " stdout="
            << standardOutput.toUtf8().toHex().constData()
            << " stderr="
            << standardError.toUtf8().toHex().constData()
            << " states=";
        for (DesktopRunState state : states)
            std::cerr << static_cast<int>(state) << ',';
        std::cerr << '\n';
    }
    ok = check(
        successfulOutput,
        "controller preserves fragmented UTF-8 and normal lifecycle states") &&
        ok;
    ok = check(
        controller.executablePath() ==
                EditorAssetPath::normalizedAbsolutePath(
                    QFileInfo(fixturePath)
                        .canonicalFilePath()) &&
            controller.descriptorPath() ==
                EditorAssetPath::normalizedAbsolutePath(
                    QFileInfo(descriptorPath)
                        .canonicalFilePath()),
        "controller retains canonical executable and descriptor targets") &&
        ok;

    const QString hardLinkedDescriptor =
        QDir(workingDirectory).filePath(
            QStringLiteral("hard-linked-descriptor.txt"));
    std::error_code hardLinkError;
    fs::create_hard_link(
        hostPath(descriptorPath),
        hostPath(hardLinkedDescriptor),
        hardLinkError);
    ok = check(
        !hardLinkError &&
            !controller.start(
                fixturePath,
                hardLinkedDescriptor,
                workingDirectory),
        "controller rejects a hard-linked descriptor") &&
        ok;
    ok = check(
        QFile::remove(hardLinkedDescriptor),
        "remove the hard-linked descriptor fixture") &&
        ok;

    const QString foreignDescriptor =
        QDir(workingDirectory).filePath(
            QStringLiteral(
                "foreign/launch-descriptor.json"));
    ok = check(
        writeFile(
            foreignDescriptor,
            QByteArray("success")) &&
            !controller.start(
                fixturePath,
                foreignDescriptor,
                workingDirectory),
        "controller rejects an ordinary descriptor outside the session directory") &&
        ok;

    const QString linkedWorkingDirectory =
        QDir(workingDirectory).filePath(
            QStringLiteral("linked-session"));
    std::error_code directoryLinkError;
    fs::create_directory_symlink(
        hostPath(workingDirectory),
        hostPath(linkedWorkingDirectory),
        directoryLinkError);
    if (directoryLinkError)
    {
        std::cout
            << "(controller linked-session checks skipped: "
            << directoryLinkError.message()
            << ")\n";
    }
    else
    {
        ok = check(
            !controller.start(
                fixturePath,
                QDir(linkedWorkingDirectory).filePath(
                    QFileInfo(descriptorPath).fileName()),
                workingDirectory) &&
                !controller.start(
                    fixturePath,
                    descriptorPath,
                    linkedWorkingDirectory),
            "controller rejects descriptor-parent and working-directory links") &&
            ok;
        std::error_code removeDirectoryLinkError;
        fs::remove(
            hostPath(linkedWorkingDirectory),
            removeDirectoryLinkError);
        ok = check(
            !removeDirectoryLinkError,
            "remove the linked session directory fixture") &&
            ok;
    }

    standardOutput.clear();
    standardError.clear();
    states.clear();
    ok = check(
        writeFile(
            descriptorPath,
            QByteArray("nonzero")) &&
            controller.start(
                fixturePath,
                descriptorPath,
                workingDirectory) &&
            waitForController(controller, 5000) &&
            controller.outcome() ==
                DesktopRunOutcome::NonZeroExit &&
            controller.exitCode() == 70,
        "controller distinguishes a stable nonzero game exit") &&
        ok;

    const bool crashed =
        writeFile(
            descriptorPath,
            QByteArray("crash")) &&
            controller.start(
                fixturePath,
                descriptorPath,
                workingDirectory) &&
            waitForController(controller, 5000) &&
            controller.outcome() ==
                DesktopRunOutcome::Crashed;
    if (!crashed)
    {
        std::cerr
            << "crash outcome="
            << static_cast<int>(controller.outcome())
            << " exit=" << controller.exitCode()
            << " error="
            << controller.lastError().toStdString()
            << '\n';
    }
    ok = check(
        crashed,
        "controller distinguishes a crashed game process") &&
        ok;

    const QString brokenExecutable =
        QDir(workingDirectory).filePath(
            QStringLiteral("broken-game.exe"));
    ok = check(
        writeFile(
            brokenExecutable,
            QByteArray("not an executable")) &&
            QFile::setPermissions(
                brokenExecutable,
                QFileDevice::ReadOwner |
                    QFileDevice::WriteOwner |
                    QFileDevice::ExeOwner |
                    QFileDevice::ReadUser |
                    QFileDevice::ExeUser) &&
            controller.start(
                brokenExecutable,
                descriptorPath,
                workingDirectory) &&
            waitForController(controller, 5000) &&
            controller.outcome() ==
                DesktopRunOutcome::FailedToStart,
        "controller reports an asynchronous process start failure") &&
        ok;

    controller.setStopTimeoutMilliseconds(100);
    ok = check(
        writeFile(
            descriptorPath,
            QByteArray("hang")) &&
            controller.start(
                fixturePath,
                descriptorPath,
                workingDirectory) &&
            waitForControllerState(
                controller,
                DesktopRunState::Running,
                5000),
        "controller starts the stop-timeout fixture and reaches running") &&
        ok;
    ok = check(
        !controller.start(
            fixturePath,
            descriptorPath,
            workingDirectory) &&
            !controller.lastError().isEmpty(),
        "controller rejects a duplicate launch while one process is active") &&
        ok;
    controller.requestStop();
    ok = check(
        waitForController(controller, 5000) &&
            controller.outcome() ==
                DesktopRunOutcome::StoppedByUser &&
            controller.forcedKill(),
        "controller classifies user stop and kills after the bounded timeout") &&
        ok;
    {
        auto closingController =
            std::make_unique<DesktopRunController>();
        const bool closingFixtureStarted =
            writeFile(
                descriptorPath,
                QByteArray("hang")) &&
            closingController->start(
                fixturePath,
                descriptorPath,
                workingDirectory) &&
            waitForControllerState(
                *closingController,
                DesktopRunState::Running,
                5000);
        QElapsedTimer closeTimer;
        closeTimer.start();
        closingController.reset();
        const qint64 closeMilliseconds =
            closeTimer.elapsed();
        ok = check(
            closingFixtureStarted &&
                closeMilliseconds < 250,
            "destroying an active controller requests termination without waiting for the process") &&
            ok;
    }
    ok = runLaunchContentOverwriteTest(
             fixturePath,
             LaunchReplacementTarget::Descriptor) &&
        ok;
    ok = runLaunchContentOverwriteTest(
             fixturePath,
             LaunchReplacementTarget::Executable) &&
        ok;
    ok = runStartingCancellationNoSpawnTest(
             fixturePath) &&
        ok;
    ok = runStartingCancellationReentrantRestartTest(
             fixturePath) &&
        ok;
    ok = runLaunchIdentityReplacementTest(
             fixturePath,
             LaunchReplacementTarget::Executable) &&
        ok;
    ok = runLaunchIdentityReplacementTest(
             fixturePath,
             LaunchReplacementTarget::Descriptor) &&
        ok;
    ok = runLaunchIdentityReplacementTest(
             fixturePath,
             LaunchReplacementTarget::WorkingDirectory) &&
        ok;
    ok = runFinishedReentrancyPayloadTest(
             fixturePath) &&
        ok;
    ok = runTypedWorkspaceLifecycleTest(
             fixturePath) &&
        ok;
    ok = runTypedWorkspaceDescriptorMismatchTest(
             fixturePath) &&
        ok;
    ok = runTypedWorkspaceValidationTest(
             fixturePath) &&
        ok;
    ok = runTypedWorkspaceFinishedReentryTest(
             fixturePath) &&
        ok;
    return ok;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    if (qEnvironmentVariableIsSet(
            "JXQY_DESKTOP_EXECUTABLE_SETTINGS_TEST_ONLY"))
    {
        return runDesktopExecutableSettingsTests()
            ? 0
            : 1;
    }
    bool ok = runDesktopRunCurrentTargetTests();
    ok = runSavedScenePreparationTests() && ok;
    ok = runExactSavedScenePreparationTests() && ok;
    ok = runDesktopExecutableSettingsTests() && ok;
    ok = runDesktopRunControllerTests() && ok;
    return ok ? 0 : 1;
}
