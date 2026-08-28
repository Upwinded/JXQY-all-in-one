#include "../core/StoryGraphResourceContext.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
constexpr qsizetype ResourceReadBudget = 64;
constexpr qsizetype MaximumMapNameIniBytes =
    1024 * 1024;

struct ResourceFixture
{
    QString collectionRoot;
    QString activeRoot;
    QString dependencyRoot;
    QString commonRoot;
};

bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool writeFile(
    const QString& path,
    const QByteArray& bytes)
{
    if (!QDir().mkpath(
            QFileInfo(path).absolutePath()))
    {
        return false;
    }

    QFile file(path);
    if (!file.open(
            QIODevice::WriteOnly |
            QIODevice::Truncate))
    {
        return false;
    }
    return file.write(bytes) == bytes.size() &&
        file.flush();
}

bool createFixture(
    const QString& parentRoot,
    ResourceFixture& fixture)
{
    fixture.collectionRoot =
        QDir(parentRoot).filePath(
            QStringLiteral("资源集合 空格"));
    fixture.activeRoot =
        QDir(fixture.collectionRoot).filePath(
            QStringLiteral("活动 包"));
    fixture.dependencyRoot =
        QDir(fixture.collectionRoot).filePath(
            QStringLiteral("依赖 包"));
    fixture.commonRoot =
        QDir(fixture.collectionRoot).filePath(
            QStringLiteral("common"));

    const QString mapVirtualPath =
        QStringLiteral(
            "map/章节/中 都.地图.map");
    const QString scriptVirtualPath =
        QStringLiteral(
            "script/map/中 都/入口 脚本.txt");

    return
        QDir().mkpath(fixture.activeRoot) &&
        QDir().mkpath(fixture.dependencyRoot) &&
        QDir().mkpath(fixture.commonRoot) &&
        writeFile(
            QDir(fixture.collectionRoot).filePath(
                QStringLiteral("resources.ini")),
            QByteArray(
                "[Pack.MOD]\n"
                "Id=MOD\n"
                "Path=活动 包\n"
                "Enabled=1\n"
                "\n"
                "[Pack.BASE]\n"
                "Id=BASE\n"
                "Path=依赖 包\n"
                "Enabled=1\n")) &&
        writeFile(
            QDir(fixture.activeRoot).filePath(
                QStringLiteral(
                    "game_profile.ini")),
            QByteArray(
                "[Game]\n"
                "Id=MOD\n"
                "Name=活动模组\n"
                "[Resource]\n"
                "DependencyId=BASE\n")) &&
        writeFile(
            QDir(fixture.dependencyRoot).filePath(
                QStringLiteral(
                    "game_profile.ini")),
            QByteArray(
                "[Game]\n"
                "Id=BASE\n"
                "Name=基础资源\n"
                "Type=2\n")) &&
        writeFile(
            QDir(fixture.activeRoot).filePath(
                scriptVirtualPath),
            QStringLiteral(
                "say(\"活动脚本\")\n").toUtf8()) &&
        writeFile(
            QDir(fixture.dependencyRoot).filePath(
                scriptVirtualPath),
            QStringLiteral(
                "say(\"依赖脚本\")\n").toUtf8()) &&
        writeFile(
            QDir(fixture.commonRoot).filePath(
                scriptVirtualPath),
            QStringLiteral(
                "say(\"公共脚本\")\n").toUtf8()) &&
        writeFile(
            QDir(fixture.activeRoot).filePath(
                QStringLiteral(
                    "script/map/中 都/超 大.txt")),
            QByteArray(96, 'x')) &&
        QDir().mkpath(
            QDir(fixture.activeRoot).filePath(
                QStringLiteral("script/目录"))) &&
        QDir().mkpath(
            QDir(fixture.activeRoot).filePath(
                mapVirtualPath)) &&
        writeFile(
            QDir(fixture.dependencyRoot).filePath(
                mapVirtualPath),
            QByteArray("map")) &&
        writeFile(
            QDir(fixture.activeRoot).filePath(
                QStringLiteral(
                    "ini/map/mapname.ini")),
            QStringLiteral(
                "[Init]\n"
                "其它地图=活动目录\n").toUtf8()) &&
        writeFile(
            QDir(fixture.dependencyRoot).filePath(
                QStringLiteral(
                    "ini/map/mapname.ini")),
            QStringLiteral(
                "[Init]\n"
                "中 都.地图=映射 文件夹\n").toUtf8());
}

bool createDependencyIdFixture(
    const QString& parentRoot,
    ResourceFixture& fixture)
{
    fixture.collectionRoot =
        QDir(parentRoot).filePath(
            QStringLiteral("路径依赖集合"));
    fixture.activeRoot =
        QDir(fixture.collectionRoot).filePath(
            QStringLiteral("活动 包"));
    fixture.dependencyRoot =
        QDir(fixture.collectionRoot).filePath(
            QStringLiteral("路径 依赖"));
    fixture.commonRoot =
        QDir(fixture.collectionRoot).filePath(
            QStringLiteral("common"));

    return
        QDir().mkpath(fixture.commonRoot) &&
        writeFile(
            QDir(fixture.collectionRoot).filePath(
                QStringLiteral("resources.ini")),
            QByteArray(
                "[Collection]\n"
                "CommonPath=common\n"
                "\n"
                "[Pack.MOD]\n"
                "Id=MOD\n"
                "Path=活动 包\n"
                "[Pack.PATHBASE]\n"
                "Id=PATHBASE\n"
                "Path=路径 依赖\n")) &&
        writeFile(
            QDir(fixture.activeRoot).filePath(
                QStringLiteral(
                    "game_profile.ini")),
            QByteArray(
                "[Game]\n"
                "Id=MOD\n"
                "Name=路径依赖模组\n"
                "[Resource]\n"
                "DependencyId=PATHBASE\n")) &&
        writeFile(
            QDir(fixture.dependencyRoot).filePath(
                QStringLiteral(
                    "game_profile.ini")),
            QByteArray(
                "[Game]\n"
                "Id=PATHBASE\n"
                "Name=路径基础资源\n"
                "Type=2\n"));
}

StoryGraphResourceContext resolveFixture(
    const ResourceFixture& fixture,
    qsizetype maximumSingleFileBytes,
    QString* diagnosticCode = nullptr,
    QString* message = nullptr)
{
    return StoryGraphResourceContext::resolve(
        fixture.collectionRoot,
        QStringLiteral("mod"),
        maximumSingleFileBytes,
        diagnosticCode,
        message);
}

bool sameRoots(
    const QList<StoryGraphContentRoot>& first,
    const QList<StoryGraphContentRoot>& second)
{
    if (first.size() != second.size())
        return false;
    for (qsizetype index = 0;
         index < first.size();
         ++index)
    {
        if (first.at(index).kind !=
                second.at(index).kind ||
            first.at(index).ordinal !=
                second.at(index).ordinal ||
            first.at(index).portableRootKey !=
                second.at(index).portableRootKey)
        {
            return false;
        }
    }
    return true;
}

const StoryGraphTargetResolution* findResolution(
    const StoryGraphProjectResult& result,
    const QString& literalTarget)
{
    for (const StoryGraphTargetResolution& resolution :
         result.targetResolutions)
    {
        if (resolution.literalTarget == literalTarget)
            return &resolution;
    }
    return nullptr;
}

bool hasWarningCode(
    const StoryGraphProjectResult& result,
    const QString& diagnosticCode)
{
    for (const StoryGraphWarning& warning :
         result.warnings)
    {
        if (warning.diagnosticCode ==
            diagnosticCode)
        {
            return true;
        }
    }
    return false;
}

bool testExactSelectionPortableIdentityAndLifecycle(
    const QString& temporaryRoot)
{
    ResourceFixture firstFixture;
    ResourceFixture relocatedFixture;
    bool passed = true;
    passed &= check(
        createFixture(
            QDir(temporaryRoot).filePath(
                QStringLiteral("第一位置")),
            firstFixture) &&
        createFixture(
            QDir(temporaryRoot).filePath(
                QStringLiteral("第二位置")),
            relocatedFixture),
        "create two relocated real resource collections");
    if (!passed)
        return false;

    QString diagnosticCode;
    QString message;
    StoryGraphResourceContext first =
        resolveFixture(
            firstFixture,
            ResourceReadBudget,
            &diagnosticCode,
            &message);
    StoryGraphResourceContext relocated =
        resolveFixture(
            relocatedFixture,
            ResourceReadBudget);
    const QList<StoryGraphContentRoot> firstRoots =
        first.orderedContentRoots();
    const QList<StoryGraphContentRoot> relocatedRoots =
        relocated.orderedContentRoots();

    if (!check(
        first.isValid() &&
        relocated.isValid() &&
        firstRoots.size() == 3 &&
        relocatedRoots.size() == 3 &&
        diagnosticCode.isEmpty() &&
        message.isEmpty() &&
        first.canonicalActiveResourcePackId() ==
            QStringLiteral("MOD"),
        "resolve exact active selection from real UTF-8 manifests"))
    {
        return false;
    }
    passed &= check(
        first.assetsCollectionRoot() ==
            QFileInfo(
                firstFixture.collectionRoot).
                    canonicalFilePath() &&
        first.activeContentRoot() ==
            QFileInfo(
                firstFixture.activeRoot).
                    canonicalFilePath(),
        "resolved host roots identify the exact selected directories");
    passed &= check(
        firstRoots.size() == 3 &&
        firstRoots.at(0).kind ==
            StoryGraphContentRootKind::Active &&
        firstRoots.at(0).portableRootKey ==
            QStringLiteral(
                "story-root-v1/active/0/MOD") &&
        firstRoots.at(1).kind ==
            StoryGraphContentRootKind::DependencyId &&
        firstRoots.at(1).portableRootKey ==
            QStringLiteral(
                "story-root-v1/dependency-id/1/BASE") &&
        firstRoots.at(2).kind ==
            StoryGraphContentRootKind::Common &&
        firstRoots.at(2).portableRootKey ==
            QStringLiteral(
                "story-root-v1/common/2/"),
        "ordered graph roots preserve exact runtime order and identity");
    passed &= check(
        sameRoots(firstRoots, relocatedRoots) &&
        first.selectionFingerprint() ==
            relocated.selectionFingerprint(),
        "portable root identities and selection fingerprint survive relocation");

    bool hostPathLeaked = false;
    for (const StoryGraphContentRoot& root :
         firstRoots)
    {
        hostPathLeaked |=
            root.portableRootKey.contains(
                QDir::fromNativeSeparators(
                    firstFixture.collectionRoot),
                Qt::CaseInsensitive) ||
            root.portableRootKey.contains(
                QStringLiteral("活动 包")) ||
            root.portableRootKey.contains(
                QStringLiteral("依赖 包"));
    }
    passed &= check(
        !hostPathLeaked,
        "portable root keys contain no host collection or pack path");

    StoryGraphResourceContext baseSelection =
        StoryGraphResourceContext::resolve(
            firstFixture.collectionRoot,
            QStringLiteral("BASE"),
            ResourceReadBudget);
    const QList<StoryGraphContentRoot> baseRoots =
        baseSelection.orderedContentRoots();
    if (!check(
            baseSelection.isValid() &&
            !baseRoots.isEmpty(),
            "resolve independent BASE selection"))
    {
        return false;
    }
    const StoryGraphReadResult baseRead =
        baseSelection.read(
            baseRoots.at(0),
            QStringLiteral(
                "script/map/中 都/入口 脚本.txt"));
    const StoryGraphReadResult originalRead =
        first.read(
            firstRoots.at(0),
            QStringLiteral(
                "script/map/中 都/入口 脚本.txt"));
    passed &= check(
        baseSelection.canonicalActiveResourcePackId() ==
            QStringLiteral("BASE") &&
        baseRead.status ==
            StoryGraphReadStatus::Found &&
        baseRead.utf8Bytes ==
            QStringLiteral(
                "say(\"依赖脚本\")\n").toUtf8() &&
        originalRead.status ==
            StoryGraphReadStatus::Found &&
        originalRead.utf8Bytes ==
            QStringLiteral(
                "say(\"活动脚本\")\n").toUtf8(),
        "exact contexts do not consult or overwrite process-global selection");

    StoryGraphResourceContext retained = first;
    first.clear();
    passed &= check(
        !first.isValid() &&
        retained.isValid(),
        "clearing one context copy retains the shared logical selection");

    const QString previousActiveRoot =
        QDir(firstFixture.collectionRoot).filePath(
            QStringLiteral("活动 包-generation-a"));
    if (!check(
            QDir().rename(
                firstFixture.activeRoot,
                previousActiveRoot) &&
            QFileInfo(
                QDir(previousActiveRoot).filePath(
                    QStringLiteral(
                        "script/map/中 都/入口 脚本.txt"))).
                isFile(),
            "retained context releases native handles so the active root can be renamed"))
    {
        return false;
    }

    passed &= check(
        writeFile(
            QDir(firstFixture.activeRoot).filePath(
                QStringLiteral(
                    "script/map/中 都/入口 脚本.txt")),
            QStringLiteral(
                "say(\"替换活动脚本\")\n").toUtf8()) &&
        writeFile(
            QDir(firstFixture.collectionRoot).filePath(
                QStringLiteral("resources.ini")),
            QByteArray("[broken\n")) &&
        writeFile(
            QDir(firstFixture.activeRoot).filePath(
                QStringLiteral(
                    "game_profile.ini")),
            QByteArray(
                "[Game]\nId=OTHER\n")),
        "replace the active root generation and catalog inputs after logical selection");
    StoryGraphResourceContext fresh =
        resolveFixture(
            firstFixture,
            ResourceReadBudget,
            &diagnosticCode,
            &message);
    const StoryGraphReadResult retainedRead =
        retained.read(
            firstRoots.at(0),
            QStringLiteral(
                "script/map/中 都/入口 脚本.txt"));
    passed &= check(
        fresh.isValid() &&
        diagnosticCode.isEmpty() &&
        message.isEmpty() &&
        retainedRead.status ==
            StoryGraphReadStatus::Found &&
        retainedRead.utf8Bytes ==
            QStringLiteral(
                "say(\"替换活动脚本\")\n").toUtf8(),
        "stable entry selection and retained context read the current same-path root after replacement");

    StoryGraphResourceContext invalidBudget =
        StoryGraphResourceContext::resolve(
            relocatedFixture.collectionRoot,
            QStringLiteral("MOD"),
            -1,
            &diagnosticCode,
            &message);
    passed &= check(
        !invalidBudget.isValid() &&
        diagnosticCode ==
            QStringLiteral(
                "story_graph.resource.invalid_budget") &&
        !message.isEmpty(),
        "invalid read budget is rejected instead of silently clamped");

    StoryGraphResourceContext unrepresentableBudget =
        StoryGraphResourceContext::resolve(
            relocatedFixture.collectionRoot,
            QStringLiteral("MOD"),
            (std::numeric_limits<qsizetype>::max)(),
            &diagnosticCode,
            &message);
    passed &= check(
        !unrepresentableBudget.isValid() &&
        diagnosticCode ==
            QStringLiteral(
                "story_graph.resource.invalid_budget"),
        "budget requiring an unrepresentable exceeded marker is rejected");
    return passed;
}

bool testDependencyIdPortableIdentity(
    const QString& temporaryRoot)
{
    ResourceFixture firstFixture;
    ResourceFixture relocatedFixture;
    if (!check(
            createDependencyIdFixture(
                QDir(temporaryRoot).filePath(
                    QStringLiteral("路径位置一")),
                firstFixture) &&
            createDependencyIdFixture(
                QDir(temporaryRoot).filePath(
                    QStringLiteral("路径位置二")),
                relocatedFixture),
            "create relocated DependencyId fixtures"))
    {
        return false;
    }

    const StoryGraphResourceContext first =
        resolveFixture(
            firstFixture,
            ResourceReadBudget);
    const StoryGraphResourceContext relocated =
        resolveFixture(
            relocatedFixture,
            ResourceReadBudget);
    const QList<StoryGraphContentRoot> firstRoots =
        first.orderedContentRoots();
    const QList<StoryGraphContentRoot> relocatedRoots =
        relocated.orderedContentRoots();
    return check(
        first.isValid() &&
        relocated.isValid() &&
        firstRoots.size() == 3 &&
        firstRoots.at(1).kind ==
            StoryGraphContentRootKind::DependencyId &&
        firstRoots.at(1).portableRootKey ==
            QStringLiteral(
                "story-root-v1/dependency-id/1/PATHBASE") &&
        !firstRoots.at(1).portableRootKey.contains(
            QStringLiteral("路径 依赖")) &&
        sameRoots(firstRoots, relocatedRoots) &&
        first.selectionFingerprint() ==
            relocated.selectionFingerprint(),
        "DependencyId identity is portable and host-path free");
}

bool testStrictReadsAndOversizeBudget(
    const QString& temporaryRoot)
{
    ResourceFixture fixture;
    if (!check(
            createFixture(
                QDir(temporaryRoot).filePath(
                    QStringLiteral("读取状态")),
                fixture),
            "create strict-read resource fixture"))
    {
        return false;
    }

    StoryGraphResourceContext context =
        resolveFixture(
            fixture,
            ResourceReadBudget);
    const QList<StoryGraphContentRoot> roots =
        context.orderedContentRoots();
    if (!check(
            context.isValid() &&
            roots.size() == 3,
            "resolve strict-read resource context"))
    {
        return false;
    }

    const QString scriptPath =
        QStringLiteral(
            "script/map/中 都/入口 脚本.txt");
    const StoryGraphReadResult found =
        context.read(
            roots.at(0),
            scriptPath);
    const StoryGraphReadResult probed =
        context.probeRegularFile(
            roots.at(0),
            scriptPath);
    const StoryGraphReadResult dependency =
        context.read(
            roots.at(1),
            scriptPath);
    const StoryGraphReadResult common =
        context.read(
            roots.at(2),
            scriptPath);
    const StoryGraphReadResult missing =
        context.read(
            roots.at(0),
            QStringLiteral(
                "script/map/中 都/缺失.txt"));
    const StoryGraphReadResult probedMissing =
        context.probeRegularFile(
            roots.at(0),
            QStringLiteral(
                "script/map/中 都/缺失.txt"));
    const StoryGraphReadResult directory =
        context.read(
            roots.at(0),
            QStringLiteral("script/目录"));
    const StoryGraphReadResult probedDirectory =
        context.probeRegularFile(
            roots.at(0),
            QStringLiteral("script/目录"));
    const StoryGraphReadResult unsafe =
        context.read(
            roots.at(0),
            QStringLiteral("../逃逸.txt"));
    const StoryGraphReadResult probedUnsafe =
        context.probeRegularFile(
            roots.at(0),
            QStringLiteral("../逃逸.txt"));
    StoryGraphContentRoot mismatched =
        roots.at(0);
    mismatched.portableRootKey +=
        QStringLiteral("/tampered");
    const StoryGraphReadResult wrongRoot =
        context.read(
            mismatched,
            scriptPath);
    const StoryGraphReadResult probedWrongRoot =
        context.probeRegularFile(
            mismatched,
            scriptPath);
    const StoryGraphReadResult unavailable =
        StoryGraphResourceContext().read(
            roots.at(0),
            scriptPath);
    const StoryGraphReadResult probedUnavailable =
        StoryGraphResourceContext().
            probeRegularFile(
                roots.at(0),
                scriptPath);

    bool passed = true;
    passed &= check(
        found.status ==
            StoryGraphReadStatus::Found &&
        found.utf8Bytes ==
            QStringLiteral(
                "say(\"活动脚本\")\n").toUtf8() &&
        found.message ==
            QStringLiteral(
                "story_graph.resource.success") &&
        found.canonicalAbsolutePath.contains(
            QStringLiteral(
                "入口 脚本.txt")),
        "Found preserves exact UTF-8 bytes and a navigable host path");
    passed &= check(
        probed.status ==
            StoryGraphReadStatus::Found &&
        probed.utf8Bytes.isEmpty() &&
        probed.message ==
            QStringLiteral(
                "story_graph.resource.success") &&
        probed.canonicalAbsolutePath ==
            found.canonicalAbsolutePath,
        "regular-file probe validates the current root path without reading payload");
    passed &= check(
        dependency.status ==
            StoryGraphReadStatus::Found &&
        dependency.utf8Bytes ==
            QStringLiteral(
                "say(\"依赖脚本\")\n").toUtf8() &&
        common.status ==
            StoryGraphReadStatus::Found &&
        common.utf8Bytes ==
            QStringLiteral(
                "say(\"公共脚本\")\n").toUtf8(),
        "reads stay pinned to the supplied exact ordered root");
    passed &= check(
        missing.status ==
            StoryGraphReadStatus::Missing &&
        missing.utf8Bytes.isEmpty() &&
        missing.message ==
            QStringLiteral(
                "story_graph.resource.not_found") &&
        probedMissing.status ==
            StoryGraphReadStatus::Missing &&
        probedMissing.utf8Bytes.isEmpty() &&
        probedMissing.message ==
            QStringLiteral(
                "story_graph.resource.not_found"),
        "read and probe preserve the exact Missing state");
    passed &= check(
        directory.status ==
            StoryGraphReadStatus::Rejected &&
        directory.message ==
            QStringLiteral(
                "story_graph.resource.not_regular_file") &&
        unsafe.status ==
            StoryGraphReadStatus::Rejected &&
        unsafe.message.startsWith(
            QStringLiteral(
                "story_graph.resource.unsafe_path: ")) &&
        probedDirectory.status ==
            StoryGraphReadStatus::Rejected &&
        probedDirectory.message ==
            QStringLiteral(
                "story_graph.resource.not_regular_file") &&
        probedUnsafe.status ==
            StoryGraphReadStatus::Rejected &&
        probedUnsafe.message.startsWith(
            QStringLiteral(
                "story_graph.resource.unsafe_path: ")) &&
        wrongRoot.status ==
            StoryGraphReadStatus::Rejected &&
        wrongRoot.message ==
            QStringLiteral(
                "story_graph.resource.root_identity_mismatch") &&
        probedWrongRoot.status ==
            StoryGraphReadStatus::Rejected &&
        probedWrongRoot.message ==
            QStringLiteral(
                "story_graph.resource.root_identity_mismatch"),
        "read and probe keep unsafe path, non-file target, and root mismatch Rejected");
    passed &= check(
        unavailable.status ==
            StoryGraphReadStatus::Error &&
        unavailable.message ==
            QStringLiteral(
                "story_graph.resource.context_unavailable") &&
        probedUnavailable.status ==
            StoryGraphReadStatus::Error &&
        probedUnavailable.message ==
            QStringLiteral(
                "story_graph.resource.context_unavailable"),
        "read and probe keep a cleared or default context unavailable");

    StoryGraphProjectRequest request;
    request.entrySource.identity.portableRootKey =
        roots.at(0).portableRootKey;
    request.entrySource.identity.virtualPath =
        QStringLiteral(
            "script/map/中 都/预算入口.txt");
    request.entrySource.identity.fromEditorBuffer =
        true;
    request.entrySource.identity.documentRevision = 1;
    request.entrySource.utf8Bytes =
        QStringLiteral(
            "runscript(\"超 大.txt\")\n").toUtf8();
    request.orderedContentRoots = roots;
    request.entryMapContext.state =
        StoryGraphMapContextState::Known;
    request.entryMapContext.effectiveMapFolder =
        QStringLiteral("中 都");
    request.budget.maximumSingleFileBytes =
        ResourceReadBudget;
    request.budget.maximumTotalBytes = 4096;
    request.budget.maximumFileCount = 8;
    request.budget.maximumCallDepth = 8;

    int readCount = 0;
    const StoryGraphProjectResult oversized =
        StoryGraphProjectResolver::analyze(
            request,
            [&context, &readCount](
                const StoryGraphContentRoot& root,
                const QString& path)
            {
                ++readCount;
                return context.read(root, path);
            });
    const StoryGraphTargetResolution* resolution =
        findResolution(
            oversized,
            QStringLiteral("超 大.txt"));
    passed &= check(
        resolution != nullptr &&
        resolution->status ==
            StoryGraphTargetResolutionStatus::
                BudgetExceeded &&
        resolution->targetPortableRootKey.isEmpty() &&
        resolution->targetVirtualPath.isEmpty() &&
        oversized.documents.size() == 1 &&
        oversized.totalBytesRead ==
            request.entrySource.utf8Bytes.size() &&
        readCount == 1,
        "oversized rooted read forms only BudgetExceeded without a target");
    passed &= check(
        hasWarningCode(
            oversized,
            QStringLiteral(
                "story_graph.project.budget.single_file")) &&
        !hasWarningCode(
            oversized,
            QStringLiteral(
                "story_graph.project.read_error")) &&
        !hasWarningCode(
            oversized,
            QStringLiteral(
                "story_graph.project.target_missing")) &&
        !hasWarningCode(
            oversized,
            QStringLiteral(
                "story_graph.project.target_rejected")),
        "oversized rooted read does not degrade into another read status");
    return passed;
}

bool testWholeFileMapNameAndBasenameFallback(
    const QString& temporaryRoot)
{
    ResourceFixture fixture;
    if (!check(
            createFixture(
                QDir(temporaryRoot).filePath(
                    QStringLiteral("地图映射")),
                fixture),
            "create map-folder resource fixture"))
    {
        return false;
    }

    StoryGraphResourceContext context =
        resolveFixture(
            fixture,
            ResourceReadBudget);
    if (!check(
            context.isValid(),
            "resolve map-folder resource context"))
    {
        return false;
    }

    const QString mapTarget =
        QStringLiteral(
            "章节/中 都.地图.map");
    const QString activeMapNamePath =
        QDir(fixture.activeRoot).filePath(
            QStringLiteral(
                "ini/map/mapname.ini"));

    const StoryGraphMapFolderResolution
        wholeFileFallback =
            context.resolveMapFolder(
                mapTarget);
    bool passed = true;
    passed &= check(
        wholeFileFallback.status ==
            StoryGraphMapFolderResolutionStatus::
                Resolved &&
        wholeFileFallback.effectiveMapFolder ==
            QStringLiteral("中 都.地图"),
        "first whole mapname.ini does not merge a key from a later root");

    passed &= check(
        QFile::remove(activeMapNamePath),
        "remove first whole mapname.ini");
    const StoryGraphMapFolderResolution
        dependencyMapping =
            context.resolveMapFolder(
                mapTarget);
    passed &= check(
        dependencyMapping.status ==
            StoryGraphMapFolderResolutionStatus::
                Resolved &&
        dependencyMapping.effectiveMapFolder ==
            QStringLiteral("映射 文件夹"),
        "missing first mapname.ini falls back to the next whole file");

    passed &= check(
        writeFile(
            activeMapNamePath,
            QByteArray(
                MaximumMapNameIniBytes + 1,
                'x')),
        "write oversized first mapname.ini");
    const StoryGraphMapFolderResolution
        oversizedFirstFile =
            context.resolveMapFolder(
                mapTarget);
    passed &= check(
        oversizedFirstFile.status ==
            StoryGraphMapFolderResolutionStatus::
                Resolved &&
        oversizedFirstFile.effectiveMapFolder ==
            QStringLiteral("映射 文件夹"),
        "non-readable oversized mapname.ini continues whole-file fallback");

    passed &= check(
        writeFile(
            activeMapNamePath,
            QByteArray("[Init\nbroken")),
        "write malformed first mapname.ini");
    const StoryGraphMapFolderResolution
        malformedWholeFile =
            context.resolveMapFolder(
                mapTarget);
    passed &= check(
        malformedWholeFile.status ==
            StoryGraphMapFolderResolutionStatus::
                Resolved &&
        malformedWholeFile.effectiveMapFolder ==
            QStringLiteral("中 都.地图"),
        "parsed first whole file failure keeps basename without key merging");

    const StoryGraphMapFolderResolution missing =
        context.resolveMapFolder(
            QStringLiteral(
                "章节/不存在.map"));
    passed &= check(
        missing.status ==
            StoryGraphMapFolderResolutionStatus::
                Missing &&
        missing.message ==
            QStringLiteral(
                "story_graph.resource.map_not_found"),
        "absent map has the exact Missing state");

    passed &= check(
        QDir().mkpath(
            QDir(fixture.activeRoot).filePath(
                QStringLiteral(
                    "map/章节/目录.map"))),
        "create non-file map target");
    const StoryGraphMapFolderResolution nonFile =
        context.resolveMapFolder(
            QStringLiteral(
                "章节/目录.map"));
    const StoryGraphMapFolderResolution unsafe =
        context.resolveMapFolder(
            QStringLiteral(
                "../逃逸.map"));
    const StoryGraphMapFolderResolution unavailable =
        StoryGraphResourceContext().
            resolveMapFolder(mapTarget);
    passed &= check(
        nonFile.status ==
            StoryGraphMapFolderResolutionStatus::
                Rejected &&
        nonFile.message ==
            QStringLiteral(
                "story_graph.resource.not_regular_file") &&
        unsafe.status ==
            StoryGraphMapFolderResolutionStatus::
                Rejected &&
        unavailable.status ==
            StoryGraphMapFolderResolutionStatus::
                Error,
        "map resolution preserves Rejected and Error boundaries");
    return passed;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporaryDirectory(
        QDir(QDir::tempPath()).filePath(
            QStringLiteral(
                "jxqy 剧情图资源 空格-XXXXXX")));
    if (!check(
            temporaryDirectory.isValid(),
            "create UTF-8 temporary root"))
    {
        return EXIT_FAILURE;
    }

    bool passed = true;
    passed &=
        testExactSelectionPortableIdentityAndLifecycle(
            temporaryDirectory.path());
    passed &=
        testDependencyIdPortableIdentity(
            temporaryDirectory.path());
    passed &=
        testStrictReadsAndOversizeBudget(
            temporaryDirectory.path());
    passed &=
        testWholeFileMapNameAndBasenameFallback(
            temporaryDirectory.path());
    if (passed)
    {
        std::cout <<
            "StoryGraphResourceContext tests passed\n";
    }
    return passed
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
