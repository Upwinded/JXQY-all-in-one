#include "../core/OnlineResourcePackageExporter.h"
#include "../core/OnlineUpdateCatalogPublisher.h"
#include "../core/AssetCliRunner.h"
#include "../../src/Update/OnlineUpdateCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>
#include <cstdio>

namespace
{
int failureCount = 0;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << std::endl;
        failureCount++;
    }
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) &&
        file.write(bytes) == bytes.size();
}

QByteArray readAll(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool createDirectoryJunction(
    const QString& targetPath,
    const QString& junctionPath)
{
#ifdef Q_OS_WIN
    return QProcess::execute(
        QStringLiteral("cmd.exe"),
        {
            QStringLiteral("/d"),
            QStringLiteral("/c"),
            QStringLiteral("mklink"),
            QStringLiteral("/J"),
            QDir::toNativeSeparators(junctionPath),
            QDir::toNativeSeparators(targetPath)
        }) == 0 && QFileInfo(junctionPath).isJunction();
#else
    Q_UNUSED(targetPath);
    Q_UNUSED(junctionPath);
    return false;
#endif
}

bool removeDirectoryJunction(const QString& junctionPath)
{
#ifdef Q_OS_WIN
    return QProcess::execute(
        QStringLiteral("cmd.exe"),
        {
            QStringLiteral("/d"),
            QStringLiteral("/c"),
            QStringLiteral("rmdir"),
            QDir::toNativeSeparators(junctionPath)
        }) == 0 && !QFileInfo::exists(junctionPath);
#else
    Q_UNUSED(junctionPath);
    return false;
#endif
}

void testDeterministicFullPackage()
{
    QTemporaryDir temporary;
    expect(temporary.isValid(), "temporary directory is available");
    const QString root = QDir(temporary.path()).filePath("pack");
    expect(writeFile(
        QDir(root).filePath("game_profile.ini"),
        "[Game]\nId=TEST\nName=测试资源\nAuthor=测试作者\n"
        "Version=1.0\n\n[Release]\n"
        "MinimumEngineVersion=2.0.0\n"
        "InstalledArtifactCrc32=deadbeef\n"
        "InstalledIncrementalArtifactCrc32=feedface\n"
        "InstalledIncrementalChainCrc32s=deadbeef,feedface\n\n[Resource]\n"
        "ResourceOnly=1\n"),
        "manifest fixture writes");
    expect(writeFile(
        QDir(root).filePath(QString::fromUtf8("script/剧情.txt")),
        QByteArray::fromStdString("print('ok')\n")),
        "UTF-8 resource fixture writes");
    expect(writeFile(
        QDir(root).filePath(".jxqy_editor/session.ini"),
        "must not be packaged"),
        "editor-state fixture writes");
    const QString first = QDir(temporary.path()).filePath("first.zip");
    const QString second = QDir(temporary.path()).filePath("second.zip");
    const auto firstResult =
        OnlineResourcePackageExporter::exportPackage(root, first);
    const auto secondResult =
        OnlineResourcePackageExporter::exportPackage(root, second);
    expect(firstResult.succeeded() && secondResult.succeeded(),
        "valid resource roots export");
    expect(firstResult.fileCount == 2,
        "editor-private state is excluded from exports");
    expect(firstResult.crc32Hex == secondResult.crc32Hex &&
        firstResult.archiveSize == secondResult.archiveSize,
        "repeated exports have the same checksum and size");
    expect(readAll(first) == readAll(second),
        "repeated exports are byte-for-byte deterministic");
    expect(readAll(QDir(root).filePath("game_profile.ini")) ==
            QByteArray("[Game]\nId=TEST\nName=测试资源\nAuthor=测试作者\n"
                "Version=1.0\n\n[Release]\n"
                "MinimumEngineVersion=2.0.0\n"
                "InstalledArtifactCrc32=deadbeef\n"
                "InstalledIncrementalArtifactCrc32=feedface\n"
                "InstalledIncrementalChainCrc32s=deadbeef,feedface\n\n[Resource]\n"
                "ResourceOnly=1\n"),
        "publication does not modify the source manifest");
    expect(QFileInfo::exists(firstResult.catalogPath) &&
            QFileInfo::exists(secondResult.catalogPath),
        "publication writes a catalog beside each archive");
    const QByteArray catalogBytes = readAll(firstResult.catalogPath);
    const OnlineUpdate::CatalogParseResult catalog =
        OnlineUpdate::parseCatalog(
            catalogBytes.constData(),
            static_cast<std::size_t>(catalogBytes.size()));
    expect(catalog.succeeded() &&
            catalog.catalog.resourcePackages.size() == 1 &&
            catalog.catalog.resourcePackages.at("test").displayName ==
                QString::fromUtf8("测试资源").toStdString() &&
            catalog.catalog.resourcePackages.at("test").author ==
                QString::fromUtf8("测试作者").toStdString() &&
            catalog.catalog.resourcePackages.at("test").artifactPath ==
                "first.zip" &&
            catalog.catalog.resourcePackages.at("test").resourceOnly &&
            catalog.catalog.resourcePackages.at("test").crc32Hex ==
                firstResult.crc32Hex.toStdString(),
        "generated one-resource catalog references the published archive");
}

void testResourcePackageCli()
{
    QTemporaryDir temporary;
    expect(temporary.isValid(),
        "resource package CLI temporary directory is available");
    const QString root = QDir(temporary.path()).filePath("pack");
    expect(writeFile(
        QDir(root).filePath("game_profile.ini"),
        "[Game]\nId=CLI_TEST\nName=CLI Test\nVersion=1.0\n\n"
        "[Release]\nMinimumEngineVersion=2.0.0\n"),
        "resource package CLI manifest fixture writes");
    expect(writeFile(QDir(root).filePath("script/start.txt"), "return\n"),
        "resource package CLI content fixture writes");

    const QString output = QDir(temporary.path()).filePath("cli-test.zip");
    const QString stdoutFilePath =
        QDir(temporary.path()).filePath("stdout.txt");
    const QString stderrFilePath =
        QDir(temporary.path()).filePath("stderr.txt");
    const QByteArray stdoutPath = QFile::encodeName(stdoutFilePath);
    const QByteArray stderrPath = QFile::encodeName(stderrFilePath);
    FILE* stdoutFile = std::fopen(stdoutPath.constData(), "w+b");
    FILE* stderrFile = std::fopen(stderrPath.constData(), "w+b");
    expect(stdoutFile != nullptr && stderrFile != nullptr,
        "resource package CLI capture streams open");
    int exitCode = -1;
    const QStringList arguments = {
        "jxqy-editor-cli", "export-resource-package", root, output};
    expect(AssetCliRunner::shouldHandle(arguments),
        "resource package CLI command is recognized");
    if (stdoutFile != nullptr && stderrFile != nullptr)
    {
        exitCode = AssetCliRunner::run(arguments, stdoutFile, stderrFile);
        std::fflush(stdoutFile);
        std::fflush(stderrFile);
    }
    if (stdoutFile != nullptr)
        std::fclose(stdoutFile);
    if (stderrFile != nullptr)
        std::fclose(stderrFile);

    expect(exitCode == 0 && QFileInfo::exists(output),
        "resource package CLI publishes the archive");
    expect(QFileInfo::exists(
            QDir(temporary.path()).filePath("cli-test.catalog.ini")),
        "resource package CLI publishes the catalog fragment");
    expect(readAll(stdoutFilePath).contains("CRC32:") &&
            readAll(stderrFilePath).isEmpty(),
        "resource package CLI reports the checksum without errors");
}

void testInvalidSources()
{
    QTemporaryDir temporary;
    expect(temporary.isValid(), "temporary directory is available");
    const QString root = QDir(temporary.path()).filePath("pack");
    QDir().mkpath(root);
    const QString output = QDir(temporary.path()).filePath("pack.zip");
    expect(
        OnlineResourcePackageExporter::exportPackage(root, output).status ==
            OnlineResourcePackageExporter::Status::MissingManifest,
        "export rejects a resource root without game_profile.ini");

    expect(writeFile(
        QDir(root).filePath("game_profile.ini"),
        "[Game]\nId=TEST\n"),
        "manifest fixture writes");
    expect(
        OnlineResourcePackageExporter::exportPackage(root, output).status ==
            OnlineResourcePackageExporter::Status::InvalidManifest,
        "export rejects a manifest without release display metadata");
    expect(writeFile(
        QDir(root).filePath("game_profile.ini"),
        "[Game]\nId=TEST\nVersion=1.0\n\n[Release]\n"
        "MinimumEngineVersion=2.0.0\n"),
        "complete manifest fixture writes");
    expect(writeFile(QDir(root).filePath("Upper.txt"), "bad"),
        "uppercase fixture writes");
    expect(
        OnlineResourcePackageExporter::exportPackage(root, output).status ==
            OnlineResourcePackageExporter::Status::NonLowercasePath,
        "export rejects uppercase ASCII resource paths");

    expect(QFile::remove(QDir(root).filePath("Upper.txt")),
        "uppercase fixture is removed");
    const QString nestedOutput = QDir(root).filePath("package.zip");
    expect(
        OnlineResourcePackageExporter::exportPackage(root, nestedOutput).status ==
            OnlineResourcePackageExporter::Status::InvalidInput,
        "export rejects an output archive inside its source root");
}

void testWindowsJunctionSourceRoot()
{
#ifdef Q_OS_WIN
    QTemporaryDir temporary;
    expect(temporary.isValid(),
        "junction source-root temporary directory is available");
    const QString targetRoot =
        QDir(temporary.path()).filePath("pack-target");
    const QString junctionRoot =
        QDir(temporary.path()).filePath("pack-junction");
    expect(writeFile(
        QDir(targetRoot).filePath("game_profile.ini"),
        "[Game]\nId=JUNCTION_TEST\nName=Junction Test\nVersion=1.0\n\n"
        "[Release]\nMinimumEngineVersion=2.0.0\n"),
        "junction source-root manifest fixture writes");
    expect(writeFile(
        QDir(targetRoot).filePath("script/start.txt"),
        "return\n"),
        "junction source-root content fixture writes");
    if (!createDirectoryJunction(targetRoot, junctionRoot))
    {
        std::cout << "SKIPPED: Windows junction fixture unavailable"
                  << std::endl;
        return;
    }

    const auto published = OnlineResourcePackageExporter::exportPackage(
        junctionRoot,
        QDir(temporary.path()).filePath("junction-source.zip"));
    expect(published.succeeded() && published.fileCount == 2,
        "export accepts a Windows junction as the source root");
    expect(
        OnlineResourcePackageExporter::exportPackage(
            junctionRoot,
            QDir(junctionRoot).filePath("inside-source.zip")).status ==
            OnlineResourcePackageExporter::Status::InvalidInput,
        "junction source roots still reject an output inside the source tree");

    const QString nestedTarget =
        QDir(temporary.path()).filePath("nested-target");
    const QString nestedJunction =
        QDir(targetRoot).filePath("nested-junction");
    QDir().mkpath(nestedTarget);
    if (createDirectoryJunction(nestedTarget, nestedJunction))
    {
        expect(
            OnlineResourcePackageExporter::exportPackage(
                junctionRoot,
                QDir(temporary.path()).filePath("nested-junction.zip")).status ==
                OnlineResourcePackageExporter::Status::UnsafeSourceEntry,
            "export still rejects a junction inside the source tree");
        expect(removeDirectoryJunction(nestedJunction),
            "nested junction fixture is removed without touching its target");
    }
    expect(removeDirectoryJunction(junctionRoot),
        "source-root junction fixture is removed without touching its target");
#endif
}

void testDependencyCatalogFragment()
{
    QTemporaryDir temporary;
    expect(temporary.isValid(), "dependency fragment temporary directory is available");
    const QString root = QDir(temporary.path()).filePath("pack");
    expect(writeFile(
        QDir(root).filePath("game_profile.ini"),
        "[Game]\nId=MOD\nName=Dependent Mod\nVersion=1.0\n\n"
        "[Release]\nMinimumEngineVersion=2.0.0\n\n"
        "[Resource]\nDependencyId=BASE\n"),
        "dependent manifest fixture writes");
    expect(writeFile(QDir(root).filePath("script/start.txt"), "return\n"),
        "dependent resource fixture writes");
    const QString output = QDir(temporary.path()).filePath("mod.zip");
    const auto published =
        OnlineResourcePackageExporter::exportPackage(root, output);
    expect(published.succeeded(),
        "one-resource catalog fragment permits dependencies published separately");
    const QByteArray catalogBytes = readAll(published.catalogPath);
    expect(catalogBytes.contains("[Resource.MOD]") &&
            catalogBytes.contains("Dependencies=BASE") &&
            !catalogBytes.contains("[Resource.BASE]"),
        "resource fragment records only its declared dependency ID");

    expect(writeFile(
        QDir(root).filePath("game_profile.ini"),
        "[Game]\nId=SELF\nName=Self Cycle\nVersion=1.0\n\n"
        "[Release]\nMinimumEngineVersion=2.0.0\n\n"
        "[Resource]\nDependencyId=SELF\n"),
        "self dependency fixture writes");
    expect(
        OnlineResourcePackageExporter::exportPackage(
            root, QDir(temporary.path()).filePath("self.zip")).status ==
            OnlineResourcePackageExporter::Status::InvalidManifest,
        "resource fragment rejects a self dependency cycle");
}

void testCommonPackagePublishing()
{
    QTemporaryDir temporary;
    expect(temporary.isValid(), "common package temporary directory is available");
    const QString root = QDir(temporary.path()).filePath("common");
    expect(writeFile(QDir(root).filePath("sound/click.wav"), "sound-data"),
        "common sound fixture writes");
    expect(writeFile(
        QDir(root).filePath("version.ini"),
        "[Common]\nVersion=old\nInstalledArtifactCrc32=deadbeef\n"),
        "stale source common version fixture writes");

    const QString first = QDir(temporary.path()).filePath("common-first.zip");
    const QString second = QDir(temporary.path()).filePath("common-second.zip");
    const auto firstResult =
        OnlineResourcePackageExporter::exportCommonPackage(root, "1.1", first);
    const auto secondResult =
        OnlineResourcePackageExporter::exportCommonPackage(root, "1.1", second);
    expect(firstResult.succeeded() && secondResult.succeeded(),
        "valid common roots export");
    expect(firstResult.fileCount == 2,
        "common export replaces the source marker with its published version");
    expect(firstResult.crc32Hex == secondResult.crc32Hex &&
            readAll(first) == readAll(second),
        "common exports are deterministic");

    const QByteArray catalogBytes = readAll(firstResult.catalogPath);
    const OnlineUpdate::CatalogParseResult catalog =
        OnlineUpdate::parseCatalog(
            catalogBytes.constData(),
            static_cast<std::size_t>(catalogBytes.size()));
    expect(catalog.succeeded() && catalog.catalog.commonPackage.has_value() &&
            catalog.catalog.commonPackage->versionText == "1.1" &&
            catalog.catalog.commonPackage->artifactPath ==
                "common-first.zip" &&
            catalog.catalog.commonPackage->crc32Hex ==
                firstResult.crc32Hex.toStdString(),
        "common export writes a matching [Common] catalog fragment");

    const QString cliOutput = QDir(temporary.path()).filePath("common-cli.zip");
    const QString stdoutFilePath =
        QDir(temporary.path()).filePath("stdout.txt");
    const QString stderrFilePath =
        QDir(temporary.path()).filePath("stderr.txt");
    const QByteArray stdoutPath = QFile::encodeName(stdoutFilePath);
    const QByteArray stderrPath = QFile::encodeName(stderrFilePath);
    FILE* stdoutFile = std::fopen(stdoutPath.constData(), "w+b");
    FILE* stderrFile = std::fopen(stderrPath.constData(), "w+b");
    expect(stdoutFile != nullptr && stderrFile != nullptr,
        "common CLI capture streams open");
    int cliExitCode = -1;
    if (stdoutFile != nullptr && stderrFile != nullptr)
    {
        cliExitCode = AssetCliRunner::run(
            { "jxqy-editor-cli", "export-common-package", root,
              "1.1", cliOutput },
            stdoutFile,
            stderrFile);
        std::fflush(stdoutFile);
        std::fflush(stderrFile);
    }
    if (stdoutFile != nullptr)
        std::fclose(stdoutFile);
    if (stderrFile != nullptr)
        std::fclose(stderrFile);
    expect(cliExitCode == 0 && QFileInfo::exists(cliOutput),
        "common package CLI publishes the archive");
    expect(readAll(stdoutFilePath).contains("CRC32:"),
        "common package CLI reports the checksum");
    expect(readAll(QDir(root).filePath("version.ini")) ==
            QByteArray(
                "[Common]\nVersion=old\nInstalledArtifactCrc32=deadbeef\n"),
        "common publication does not modify the source version marker");

    const QString minimalCommonRoot =
        QDir(temporary.path()).filePath("minimal-common");
    expect(writeFile(
        QDir(minimalCommonRoot).filePath("sound/click.wav"), "sound-data"),
        "minimal common fixture writes");
    expect(OnlineResourcePackageExporter::exportCommonPackage(
                minimalCommonRoot,
                "1.1",
                QDir(temporary.path()).filePath("minimal-common.zip")).
            succeeded(),
        "common export does not require engine-owned font files");

    const QString manifestRoot =
        QDir(temporary.path()).filePath("manifest-common");
    expect(writeFile(QDir(manifestRoot).filePath("sound/click.wav"), "sound"),
        "manifest common content fixture writes");
    expect(writeFile(
        QDir(manifestRoot).filePath("game_profile.ini"),
        "[Game]\nId=NOT_COMMON\n"),
        "manifest common profile fixture writes");
    expect(!OnlineResourcePackageExporter::exportCommonPackage(
                manifestRoot,
                "1.1",
                QDir(temporary.path()).filePath("manifest-common.zip")).
            succeeded(),
        "common export rejects playable resource manifests");
}

void testCatalogPublishing()
{
    QTemporaryDir temporary;
    expect(temporary.isValid(), "temporary directory is available");
    const QString artifactRoot = QDir(temporary.path()).filePath("artifacts");
    const QString artifactPath =
        QDir(artifactRoot).filePath("resources/test.zip");
    expect(writeFile(artifactPath, "artifact bytes"),
        "publisher artifact fixture writes");
    const QString incrementalArtifactPath =
        QDir(artifactRoot).filePath("resources/test-incremental.zip");
    expect(writeFile(incrementalArtifactPath, "incremental bytes"),
        "publisher incremental artifact fixture writes");
    const QString firstIncrementalArtifactPath =
        QDir(artifactRoot).filePath("resources/test-incremental-001.zip");
    expect(writeFile(firstIncrementalArtifactPath, "first incremental bytes"),
        "publisher first chain artifact fixture writes");
    const QString commonArtifactPath =
        QDir(artifactRoot).filePath("common.zip");
    expect(writeFile(commonArtifactPath, "common bytes"),
        "publisher common artifact fixture writes");
    const QString templatePath =
        QDir(temporary.path()).filePath("catalog-template.ini");
    expect(writeFile(
        templatePath,
        "[Catalog]\n"
        "SchemaVersion=1\n"
        "\n"
        "[Common]\n"
        "Version=1.0\n"
        "Artifact=common.zip\n"
        "Size=1\n"
        "Crc32=00000000\n"
        "\n"
        "[Resource.TEST]\n"
        "Version=1.0\n"
        "MinimumEngineVersion=1.0.4\n"
        "Artifact=resources/test.zip\n"
        "Size=1\n"
        "Crc32=00000000\n"
        "IncrementalArtifact=resources/test-incremental.zip\n"
        "IncrementalSize=1\n"
        "IncrementalCrc32=00000000\n"
        "IncrementalChainCount=2\n"
        "IncrementalChain1Artifact=resources/test-incremental-001.zip\n"
        "IncrementalChain1Size=1\n"
        "IncrementalChain1Crc32=00000000\n"
        "IncrementalChain2Artifact=resources/test-incremental.zip\n"
        "IncrementalChain2Size=1\n"
        "IncrementalChain2Crc32=00000000\n"),
        "catalog template fixture writes");

    const QString outputPath =
        QDir(temporary.path()).filePath("release/catalog-v1.ini");
    const auto published = OnlineUpdateCatalogPublisher::publish(
        templatePath, artifactRoot, outputPath);
    if (!published.succeeded())
    {
        std::cerr << "Publisher status="
                  << static_cast<int>(published.status)
                  << " detail="
                  << published.detail.toStdString()
                  << std::endl;
    }
    expect(published.succeeded() && published.artifactCount == 4,
        "publisher checksums each unique common, full and chain artifact");

    const QByteArray catalogBytes = readAll(outputPath);
    const OnlineUpdate::CatalogParseResult catalog =
        OnlineUpdate::parseCatalog(
            catalogBytes.constData(),
            static_cast<std::size_t>(catalogBytes.size()));
    expect(catalog.succeeded() &&
        catalog.catalog.commonPackage.has_value() &&
        catalog.catalog.commonPackage->artifactSize ==
            QByteArray("common bytes").size() &&
        catalog.catalog.resourcePackages.at("test").artifactSize ==
            QByteArray("artifact bytes").size() &&
        catalog.catalog.resourcePackages.at("test").incrementalPackage.
            has_value() &&
        catalog.catalog.resourcePackages.at("test").incrementalPackage->
            artifactSize == QByteArray("incremental bytes").size() &&
        catalog.catalog.resourcePackages.at("test").incrementalChain.size() == 2 &&
        catalog.catalog.resourcePackages.at("test").incrementalChain[0].
            artifactSize == QByteArray("first incremental bytes").size() &&
        catalog.catalog.resourcePackages.at("test").incrementalChain[1].
            crc32Hex == catalog.catalog.resourcePackages.at("test").
                incrementalPackage->crc32Hex,
        "published catalog contains computed metadata and preserves the legacy"
        " alias to the chain tail");
    expect(!QFileInfo::exists(outputPath + ".sig"),
        "publisher does not create a detached signature sidecar");

    QByteArray unsupportedTemplate = readAll(templatePath);
    unsupportedTemplate.replace(
        "MinimumEngineVersion=1.0.4",
        "MinimumEngineVersion=1.0.3");
    expect(writeFile(templatePath, unsupportedTemplate),
        "unsupported incremental chain template fixture writes");
    const auto unsupported = OnlineUpdateCatalogPublisher::publish(
        templatePath,
        artifactRoot,
        QDir(temporary.path()).filePath("release/unsupported-catalog.ini"));
    expect(unsupported.status ==
            OnlineUpdateCatalogPublisher::Status::InvalidTemplate &&
        unsupported.detail == QStringLiteral(
            "Resource.TEST.MinimumEngineVersion"),
        "publisher rejects an incremental chain below engine version 1.0.4");
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    testDeterministicFullPackage();
    testResourcePackageCli();
    testInvalidSources();
    testWindowsJunctionSourceRoot();
    testDependencyCatalogFragment();
    testCommonPackagePublishing();
    testCatalogPublishing();
    if (failureCount != 0)
    {
        std::cerr << failureCount << " online package export test(s) failed"
                  << std::endl;
        return 1;
    }
    std::cout << "All online package export tests passed" << std::endl;
    return 0;
}
