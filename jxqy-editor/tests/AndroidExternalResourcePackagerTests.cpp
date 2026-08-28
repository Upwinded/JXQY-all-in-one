#include "../core/AndroidExternalResourcePackager.h"
#include "../core/GameProfile.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>

#include <atomic>
#include <iostream>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

namespace
{
bool check(bool condition, const QString& message)
{
    if (!condition)
    {
        std::cerr << "FAILED: "
                  << message.toUtf8().constData()
                  << '\n';
    }
    return condition;
}

bool writeFile(
    const QString& filePath,
    const QByteArray& bytes)
{
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath()))
        return false;
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QString fileHash(const QString& filePath)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(
            readFile(filePath),
            QCryptographicHash::Sha256)
            .toHex());
}

bool createDirectoryAlias(
    const QString& targetPath,
    const QString& aliasPath)
{
#ifdef Q_OS_WIN
    QProcess process;
    process.start(
        QStringLiteral("cmd.exe"),
        {
            QStringLiteral("/d"),
            QStringLiteral("/c"),
            QStringLiteral("mklink"),
            QStringLiteral("/J"),
            QDir::toNativeSeparators(aliasPath),
            QDir::toNativeSeparators(targetPath)
        });
    return process.waitForFinished(30000) &&
        process.exitStatus() == QProcess::NormalExit &&
        process.exitCode() == 0 &&
        QFileInfo(aliasPath).isDir();
#else
    const QByteArray targetBytes =
        QFile::encodeName(targetPath);
    const QByteArray aliasBytes =
        QFile::encodeName(aliasPath);
    return ::symlink(
        targetBytes.constData(),
        aliasBytes.constData()) == 0 &&
        QFileInfo(aliasPath).isSymLink();
#endif
}

bool removeDirectoryAlias(const QString& aliasPath)
{
#ifdef Q_OS_WIN
    QProcess process;
    process.start(
        QStringLiteral("cmd.exe"),
        {
            QStringLiteral("/d"),
            QStringLiteral("/c"),
            QStringLiteral("rmdir"),
            QDir::toNativeSeparators(aliasPath)
        });
    return process.waitForFinished(30000) &&
        process.exitStatus() == QProcess::NormalExit &&
        process.exitCode() == 0 &&
        !QFileInfo(aliasPath).exists();
#else
    return QFile::remove(aliasPath);
#endif
}

bool createCollection(
    const QString& collectionRoot,
    ResourcePackSelection& selection)
{
    const QDir collection(collectionRoot);
    const QString profile = QStringLiteral(
        "[Game]\n"
        "Id=MOD\n"
        "Name=Export test\n"
        "Type=3\n"
        "\n"
        "[Resource]\n"
        "DependencyId=BASE\n");
    const bool created =
        writeFile(
            collection.filePath(QStringLiteral("resources.ini")),
            QByteArrayLiteral(
                "[Collection]\n"
                "CommonPath=common\n"
                "[Pack.MOD]\n"
                "Id=MOD\n"
                "Path=mod\n"
                "Enabled=1\n")) &&
        writeFile(
            collection.filePath(
                QStringLiteral("mod/game_profile.ini")),
            profile.toUtf8()) &&
        writeFile(
            collection.filePath(
                QStringLiteral("mod/script/broken.lua")),
            QByteArrayLiteral("function ( this is deliberately invalid")) &&
        writeFile(
            collection.filePath(
                QString::fromUtf8("mod/自定义/未知.dat")),
            QByteArrayLiteral("unknown-custom-data")) &&
        writeFile(
            collection.filePath(
                QStringLiteral("mod/archive.custom")),
            QByteArrayLiteral("custom-extension")) &&
        writeFile(
            collection.filePath(
                QStringLiteral("mod/Custom/Upper.BIN")),
            QByteArrayLiteral("mixed-case-path")) &&
        QDir().mkpath(
            collection.filePath(
                QStringLiteral("mod/empty-directory"))) &&
        writeFile(
            collection.filePath(
                QStringLiteral("mod/save/rpg0/player.dat")),
            QByteArrayLiteral("runtime-save")) &&
        writeFile(
            collection.filePath(
                QStringLiteral("mod/.git/config")),
            QByteArrayLiteral("git")) &&
        writeFile(
            collection.filePath(
                QStringLiteral("mod/.jxqy_editor/state.json")),
            QByteArrayLiteral("editor-state")) &&
        writeFile(
            collection.filePath(
                QStringLiteral("mod/logs/editor.log")),
            QByteArrayLiteral("log")) &&
        writeFile(
            collection.filePath(
                QStringLiteral("mod/project.jxqyproj")),
            QByteArrayLiteral("project")) &&
        writeFile(
            collection.filePath(
                QStringLiteral("mod/migration_report.txt")),
            QByteArrayLiteral("old-report")) &&
        writeFile(
            collection.filePath(
                QStringLiteral("mod/scratch.tmp")),
            QByteArrayLiteral("temp")) &&
		writeFile(
			collection.filePath(
				QStringLiteral("mod/image/tmp/effect.png")),
			QByteArrayLiteral("effect")) &&
		writeFile(
			collection.filePath(
				QStringLiteral("mod/conversion-report-icon.png")),
			QByteArrayLiteral("icon")) &&
		writeFile(
			collection.filePath(
				QStringLiteral("mod/nested/migration_report.txt")),
			QByteArrayLiteral("nested-report-resource")) &&
		writeFile(
			collection.filePath(
				QStringLiteral("mod/art/backup/frame.bak")),
			QByteArrayLiteral("frame")) &&
        writeFile(
            collection.filePath(
                QStringLiteral("common/shared.bin")),
            QByteArrayLiteral("shared-common")) &&
        writeFile(
            collection.filePath(
                QStringLiteral("dependency/dependency.bin")),
            QByteArrayLiteral("dependency"));
    if (!created)
        return false;

    selection = ResourcePackScanner::resolveActivePack(
        collectionRoot,
        QStringLiteral("MOD"));
    return selection.isReady() &&
        selection.activePack.profile.id == QStringLiteral("MOD");
}

AndroidExternalResourceExportResult exportSelection(
    const QString& collectionRoot,
    const ResourcePackSelection& selection,
    const QString& bundleDirectory,
    std::shared_ptr<std::atomic_bool> cancellation = {})
{
    AndroidExternalResourceExportOptions options;
    options.collectionRoot = collectionRoot;
    options.activePack = selection.activePack;
    options.bundleDirectory = bundleDirectory;
    options.cancellationRequested = std::move(cancellation);
    return AndroidExternalResourcePackager::exportBundle(options);
}

bool testExportsActivePackAndExplicitCommonPath()
{
    QTemporaryDir directory;
    if (!check(directory.isValid(),
               QStringLiteral("create export fixture")))
    {
        return false;
    }
    const QString collectionRoot =
        QDir(directory.path()).filePath(QStringLiteral("collection"));
    const QString outputParent =
        QDir(directory.path()).filePath(QStringLiteral("output"));
    QDir().mkpath(collectionRoot);
    QDir().mkpath(outputParent);

    ResourcePackSelection selection;
    bool ok = check(
        createCollection(collectionRoot, selection),
        QStringLiteral("create and resolve shared catalog selection"));
    const QString bundleDirectory =
        QDir(outputParent).filePath(QStringLiteral("mod-android"));
    int lastProgress = 0;
    AndroidExternalResourceExportOptions options;
    options.collectionRoot = collectionRoot;
    options.activePack = selection.activePack;
    options.bundleDirectory = bundleDirectory;
    options.progressCallback =
        [&lastProgress](int current, int total, const QString&)
        {
            if (current <= total)
                lastProgress = current;
        };
    const AndroidExternalResourceExportResult result =
        AndroidExternalResourcePackager::exportBundle(options);
    ok = check(result.succeeded(),
        QStringLiteral("export succeeds: %1")
            .arg(result.errorMessage)) && ok;

    const QDir bundle(bundleDirectory);
    const QString packRoot = bundle.filePath(
        QStringLiteral("Download/jxqy/assets/mod"));
    const QString commonRoot = bundle.filePath(
        QStringLiteral("Download/jxqy/assets/common"));
    ok = check(
        readFile(QDir(packRoot).filePath(
            QStringLiteral("script/broken.lua"))) ==
            QByteArrayLiteral("function ( this is deliberately invalid"),
        QStringLiteral("invalid Lua is copied byte for byte")) && ok;
    ok = check(
        readFile(QDir(packRoot).filePath(
            QString::fromUtf8("自定义/未知.dat"))) ==
            QByteArrayLiteral("unknown-custom-data") &&
        readFile(QDir(packRoot).filePath(
            QStringLiteral("archive.custom"))) ==
            QByteArrayLiteral("custom-extension") &&
        readFile(QDir(packRoot).filePath(
            QStringLiteral("custom/upper.bin"))) ==
            QByteArrayLiteral("mixed-case-path") &&
        QDir(packRoot).entryList(
            QDir::Dirs | QDir::NoDotAndDotDot).
                contains(
                    QStringLiteral("custom"),
                    Qt::CaseSensitive) &&
        QDir(QDir(packRoot).filePath(
            QStringLiteral("custom"))).entryList(
                QDir::Files).
                    contains(
                        QStringLiteral("upper.bin"),
                        Qt::CaseSensitive),
        QStringLiteral("unknown and Unicode resources are copied to lowercase output paths")) && ok;
    ok = check(
        QFileInfo(QDir(packRoot).filePath(
            QStringLiteral("empty-directory"))).isDir(),
        QStringLiteral("empty directory is copied")) && ok;
    ok = check(
        readFile(QDir(commonRoot).filePath(
            QStringLiteral("shared.bin"))) ==
            QByteArrayLiteral("shared-common"),
        QStringLiteral("explicit in-collection CommonPath is copied")) && ok;
	ok = check(
		!QFileInfo(QDir(packRoot).filePath(
			QStringLiteral("save"))).exists() &&
        !QFileInfo(QDir(packRoot).filePath(
            QStringLiteral(".git"))).exists() &&
		!QFileInfo(QDir(packRoot).filePath(
			QStringLiteral(".jxqy_editor"))).exists() &&
		!QFileInfo(QDir(packRoot).filePath(
			QStringLiteral("project.jxqyproj"))).exists() &&
		!QFileInfo(QDir(packRoot).filePath(
			QStringLiteral("migration_report.txt"))).exists(),
		QStringLiteral("identified local state and generated reports are skipped")) && ok;
	ok = check(
		readFile(QDir(packRoot).filePath(
			QStringLiteral("logs/editor.log"))) ==
			QByteArrayLiteral("log") &&
		readFile(QDir(packRoot).filePath(
			QStringLiteral("scratch.tmp"))) ==
			QByteArrayLiteral("temp") &&
		readFile(QDir(packRoot).filePath(
			QStringLiteral("image/tmp/effect.png"))) ==
			QByteArrayLiteral("effect") &&
		readFile(QDir(packRoot).filePath(
			QStringLiteral("conversion-report-icon.png"))) ==
			QByteArrayLiteral("icon") &&
		readFile(QDir(packRoot).filePath(
			QStringLiteral("nested/migration_report.txt"))) ==
			QByteArrayLiteral("nested-report-resource") &&
		readFile(QDir(packRoot).filePath(
			QStringLiteral("art/backup/frame.bak"))) ==
			QByteArrayLiteral("frame"),
		QStringLiteral("generic log and temporary names remain ordinary custom resources")) && ok;
    ok = check(
        !QFileInfo(bundle.filePath(
            QStringLiteral("Download/jxqy/assets/resources.ini"))).exists(),
        QStringLiteral("export does not synthesize resources.ini")) && ok;

    const QString jsonReportPath = bundle.filePath(
        QStringLiteral("android_external_resource_report.json"));
    const QString textReportPath = bundle.filePath(
        QStringLiteral("android_external_resource_report.txt"));
    const QString installPath = bundle.filePath(
        QStringLiteral("INSTALL.txt"));
    ok = check(
        QFileInfo(jsonReportPath).isFile() &&
        QFileInfo(textReportPath).isFile() &&
        QFileInfo(installPath).isFile() &&
        !QFileInfo(QDir(packRoot).filePath(
            QStringLiteral("android_external_resource_report.json"))).exists(),
        QStringLiteral("reports and install instructions stay outside Download")) && ok;
    ok = check(
        readFile(installPath).contains(
            "/storage/emulated/0/Download/jxqy/assets/mod/game_profile.ini"),
        QStringLiteral("INSTALL contains fixed Android manifest path")) && ok;

    QJsonParseError parseError;
    const QJsonDocument reportDocument = QJsonDocument::fromJson(
        readFile(jsonReportPath), &parseError);
    const QJsonObject reportObject = reportDocument.object();
    ok = check(
        parseError.error == QJsonParseError::NoError &&
        reportObject.value(QStringLiteral("schemaVersion")).toInt() == 1 &&
        reportObject.value(QStringLiteral("status")).toString() ==
            QStringLiteral("completed") &&
        reportObject.value(QStringLiteral("resourcePackId")).toString() ==
            QStringLiteral("MOD") &&
        reportObject.value(QStringLiteral("skippedCount")).toInt() >= 7,
        QStringLiteral("JSON report contains stable identity and actions")) && ok;

    const QJsonArray warnings =
        reportObject.value(QStringLiteral("warnings")).toArray();
    const QByteArray warningBytes =
        QJsonDocument(warnings).toJson(QJsonDocument::Compact);
    ok = check(
        warningBytes.contains("DependencyId"),
        QStringLiteral("dependency declarations are reported without expansion")) && ok;

    const QString exportedCustom = QDir(packRoot).filePath(
        QString::fromUtf8("自定义/未知.dat"));
    bool hashFound = false;
    QSet<QString> skipReasons;
    for (const QJsonValue& value :
         reportObject.value(QStringLiteral("entries")).toArray())
    {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("action")).toString() ==
            QStringLiteral("skip"))
        {
            skipReasons.insert(
                entry.value(QStringLiteral("reason")).toString());
        }
        if (entry.value(QStringLiteral("sourcePath")).toString().endsWith(
                QString::fromUtf8("自定义/未知.dat")))
        {
            hashFound = entry.value(QStringLiteral("sha256")).toString() ==
                    fileHash(exportedCustom) &&
                entry.value(QStringLiteral("outputPath")).toString() ==
                    QString::fromUtf8(
                        "Download/jxqy/assets/mod/自定义/未知.dat");
        }
    }
    ok = check(hashFound,
        QStringLiteral("copied-file SHA-256 is recorded")) && ok;
	const QSet<QString> expectedSkipReasons = {
		QStringLiteral("runtime-save-state"),
		QStringLiteral("git-metadata"),
		QStringLiteral("editor-local-state"),
		QStringLiteral("editor-project-file"),
        QStringLiteral("generated-report")
    };
    ok = check(
        (skipReasons & expectedSkipReasons) ==
            expectedSkipReasons,
        QStringLiteral("every requested skip category is recorded in the report")) && ok;
    ok = check(lastProgress > 0,
        QStringLiteral("progress callback is invoked")) && ok;

    return ok;
}

bool testDirectRootTypeThreeNeedsNoOptionalMetadata()
{
    QTemporaryDir directory;
    if (!check(directory.isValid(),
               QStringLiteral("create direct-root fixture")))
    {
        return false;
    }
    const QString collectionRoot =
        QDir(directory.path()).filePath(QStringLiteral("direct-pack"));
    const QString outputParent =
        QDir(directory.path()).filePath(QStringLiteral("output"));
    const bool created =
        writeFile(
            QDir(collectionRoot).filePath(
                QStringLiteral("game_profile.ini")),
            QByteArrayLiteral(
                "[Game]\n"
                "Id=DIRECT_TYPE_3\n"
                "Type=3\n")) &&
        writeFile(
            QDir(collectionRoot).filePath(
                QStringLiteral("missing-references-are-not-checked.lua")),
            QByteArrayLiteral("this is not valid lua")) &&
        writeFile(
            QDir(collectionRoot).filePath(QStringLiteral("save")),
            QByteArrayLiteral("custom-resource-named-save")) &&
        QDir().mkpath(outputParent);
    ResourcePackSelection selection =
        ResourcePackScanner::resolveActivePack(
            collectionRoot, QString());
    bool ok = check(created && selection.isReady(),
        QStringLiteral("resolve direct Type=3 resource root"));
    const QString bundleDirectory =
        QDir(outputParent).filePath(QStringLiteral("direct-export"));
    const AndroidExternalResourceExportResult result =
        exportSelection(
            collectionRoot, selection, bundleDirectory);
    ok = check(result.succeeded(),
        QStringLiteral("direct Type=3 without Release/UI metadata exports: %1")
            .arg(result.errorMessage)) && ok;
    ok = check(
        readFile(QDir(bundleDirectory).filePath(
            QStringLiteral(
                "Download/jxqy/assets/direct-pack/missing-references-are-not-checked.lua"))) ==
            QByteArrayLiteral("this is not valid lua") &&
        readFile(QDir(bundleDirectory).filePath(
            QStringLiteral(
                "Download/jxqy/assets/direct-pack/save"))) ==
            QByteArrayLiteral("custom-resource-named-save"),
        QStringLiteral("direct-root export performs no Lua/resource preflight")) && ok;
    return ok;
}

bool testAtomicFailureAndCancellationPreserveOldBundle()
{
    struct FaultReset
    {
        ~FaultReset()
        {
            AndroidExternalResourcePackager::
                setFaultInjectorForTests({});
        }
    } faultReset;

    QTemporaryDir directory;
    if (!check(directory.isValid(),
               QStringLiteral("create atomic fixture")))
    {
        return false;
    }
    const QString collectionRoot =
        QDir(directory.path()).filePath(QStringLiteral("collection"));
    const QString outputParent =
        QDir(directory.path()).filePath(QStringLiteral("output"));
    QDir().mkpath(collectionRoot);
    QDir().mkpath(outputParent);
    ResourcePackSelection selection;
    bool ok = check(
        createCollection(collectionRoot, selection),
        QStringLiteral("create atomic export collection"));
    const QString bundleDirectory =
        QDir(outputParent).filePath(QStringLiteral("atomic-bundle"));
    const QString sentinel = QDir(bundleDirectory).filePath(
        QStringLiteral("old-generation.txt"));

    const QList<AndroidExternalResourcePackager::FaultPoint> points = {
        AndroidExternalResourcePackager::FaultPoint::AfterBackup,
        AndroidExternalResourcePackager::FaultPoint::AfterPublish
    };
    for (const auto point : points)
    {
        if (QFileInfo::exists(bundleDirectory))
            QDir(bundleDirectory).removeRecursively();
        ok = check(writeFile(sentinel, QByteArrayLiteral("old")),
            QStringLiteral("write old bundle generation")) && ok;
        AndroidExternalResourcePackager::setFaultInjectorForTests(
            [point](AndroidExternalResourcePackager::FaultPoint candidate)
            {
                return candidate == point;
            });
        const AndroidExternalResourceExportResult result =
            exportSelection(
                collectionRoot, selection, bundleDirectory);
        AndroidExternalResourcePackager::setFaultInjectorForTests({});
        ok = check(
            result.status ==
                AndroidExternalResourceExportStatus::Failed &&
            readFile(sentinel) == QByteArrayLiteral("old") &&
            !QFileInfo(QDir(bundleDirectory).filePath(
                QStringLiteral("Download"))).exists(),
            QStringLiteral("fault rollback preserves complete old bundle")) && ok;
    }

    if (QFileInfo::exists(bundleDirectory))
        QDir(bundleDirectory).removeRecursively();
    ok = check(writeFile(sentinel, QByteArrayLiteral("old")),
        QStringLiteral("write old bundle before cancellation")) && ok;
    auto cancellation = std::make_shared<std::atomic_bool>(true);
    const AndroidExternalResourceExportResult cancelled =
        exportSelection(
            collectionRoot,
            selection,
            bundleDirectory,
            cancellation);
    ok = check(
        cancelled.status ==
            AndroidExternalResourceExportStatus::Cancelled &&
        readFile(sentinel) == QByteArrayLiteral("old"),
        QStringLiteral("cancellation preserves old bundle")) && ok;

    const AndroidExternalResourceExportResult succeeded =
        exportSelection(
            collectionRoot, selection, bundleDirectory);
    ok = check(
        succeeded.succeeded() &&
        !QFileInfo(sentinel).exists() &&
        QFileInfo(QDir(bundleDirectory).filePath(
            QStringLiteral(
                "Download/jxqy/assets/mod/game_profile.ini"))).isFile(),
        QStringLiteral("successful replacement removes stale old generation")) && ok;
    const QStringList siblingEntries = QDir(outputParent).entryList(
        QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    bool hasTemporarySibling = false;
    for (const QString& entry : siblingEntries)
    {
        if (entry.contains(QStringLiteral("jxqy-android-export")))
            hasTemporarySibling = true;
    }
    ok = check(!hasTemporarySibling,
        QStringLiteral("successful export leaves no staging or backup sibling")) && ok;
    return ok;
}

bool testRejectsLinksWhenPlatformCanCreateOne()
{
    QTemporaryDir directory;
    if (!check(directory.isValid(),
               QStringLiteral("create link fixture")))
    {
        return false;
    }
    const QString collectionRoot =
        QDir(directory.path()).filePath(QStringLiteral("collection"));
    const QString outputParent =
        QDir(directory.path()).filePath(QStringLiteral("output"));
    QDir().mkpath(collectionRoot);
    QDir().mkpath(outputParent);
    ResourcePackSelection selection;
    bool ok = check(
        createCollection(collectionRoot, selection),
        QStringLiteral("create link export collection"));
    const QString source = QDir(collectionRoot).filePath(
        QStringLiteral("outside.bin"));
    const QString link = QDir(collectionRoot).filePath(
        QStringLiteral("mod/link.bin"));
    ok = check(writeFile(source, QByteArrayLiteral("outside")),
        QStringLiteral("write link target")) && ok;
    if (!QFile::link(source, link) || !QFileInfo(link).isSymLink())
    {
        QFile::remove(link);
        std::cout << "SKIPPED: platform did not permit a symbolic link fixture\n";
        return ok;
    }

    const QString bundleDirectory =
        QDir(outputParent).filePath(QStringLiteral("link-bundle"));
    const AndroidExternalResourceExportResult result =
        exportSelection(
            collectionRoot, selection, bundleDirectory);
    ok = check(
        result.status == AndroidExternalResourceExportStatus::Failed &&
        !QFileInfo(bundleDirectory).exists() &&
        result.report.failedCount() > 0,
        QStringLiteral("symbolic link aborts the whole export")) && ok;
    return ok;
}

bool testRejectsOutputParentDirectoryAlias()
{
    QTemporaryDir directory;
    if (!check(directory.isValid(),
               QStringLiteral("create output-alias fixture")))
    {
        return false;
    }

    const QString collectionContainer =
        QDir(directory.path()).filePath(QStringLiteral("collection-container"));
    const QString collectionRoot =
        QDir(collectionContainer).filePath(QStringLiteral("collection"));
    const QString realOutputParent =
        QDir(directory.path()).filePath(QStringLiteral("real-output"));
    const QString outputAlias =
        QDir(directory.path()).filePath(QStringLiteral("output-alias"));
    QDir().mkpath(collectionRoot);
    QDir().mkpath(realOutputParent);

    ResourcePackSelection selection;
    bool ok = check(
        createCollection(collectionRoot, selection),
        QStringLiteral("create output-alias export collection"));
    if (!createDirectoryAlias(realOutputParent, outputAlias))
    {
        std::cout <<
            "SKIPPED: platform did not permit a directory alias fixture\n";
        return ok;
    }

    const QString bundleThroughAlias =
        QDir(outputAlias).filePath(QStringLiteral("bundle"));
    const AndroidExternalResourceExportResult linkedParentResult =
        exportSelection(
            collectionRoot,
            selection,
            bundleThroughAlias);
    ok = check(
        linkedParentResult.status ==
            AndroidExternalResourceExportStatus::Failed &&
        !QFileInfo(QDir(realOutputParent).filePath(
            QStringLiteral("bundle"))).exists() &&
        QFileInfo(selection.activePack.manifestPath).isFile(),
        QStringLiteral(
            "output parent symlink or junction is rejected before staging")) && ok;

    ok = check(
        removeDirectoryAlias(outputAlias),
        QStringLiteral("remove output directory alias safely")) && ok;

    if (!createDirectoryAlias(collectionRoot, outputAlias))
    {
        std::cout <<
            "SKIPPED: platform did not permit the canonical-overlap alias fixture\n";
        return ok;
    }

    const QByteArray originalManifest =
        readFile(selection.activePack.manifestPath);
    const QString sourcePackThroughAlias =
        QDir(outputAlias).filePath(QStringLiteral("mod"));
    const AndroidExternalResourceExportResult overlapResult =
        exportSelection(
            collectionRoot,
            selection,
            sourcePackThroughAlias);
    ok = check(
        overlapResult.status ==
            AndroidExternalResourceExportStatus::Failed &&
        readFile(selection.activePack.manifestPath) == originalManifest &&
        !QFileInfo(QDir(selection.activePack.rootPath).filePath(
            QStringLiteral("Download"))).exists(),
        QStringLiteral(
            "canonical output overlap cannot replace a source resource pack")) && ok;

    ok = check(
        removeDirectoryAlias(outputAlias),
        QStringLiteral("remove canonical-overlap alias safely")) && ok;

    if (!createDirectoryAlias(collectionContainer, outputAlias))
    {
        std::cout <<
            "SKIPPED: platform did not permit the canonical-container alias fixture\n";
        return ok;
    }

    const AndroidExternalResourceExportResult containerOverlapResult =
        exportSelection(
            collectionRoot,
            selection,
            outputAlias);
    ok = check(
        containerOverlapResult.status ==
            AndroidExternalResourceExportStatus::Failed &&
        readFile(selection.activePack.manifestPath) == originalManifest,
        QStringLiteral(
            "canonical output ancestor cannot contain the resource collection")) && ok;

    ok = check(
        removeDirectoryAlias(outputAlias),
        QStringLiteral("remove canonical-container alias safely")) && ok;
    return ok;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    bool ok = true;
    ok = testExportsActivePackAndExplicitCommonPath() && ok;
    ok = testDirectRootTypeThreeNeedsNoOptionalMetadata() && ok;
    ok = testAtomicFailureAndCancellationPreserveOldBundle() && ok;
    ok = testRejectsLinksWhenPlatformCanCreateOne() && ok;
    ok = testRejectsOutputParentDirectoryAlias() && ok;
    return ok ? 0 : 1;
}
