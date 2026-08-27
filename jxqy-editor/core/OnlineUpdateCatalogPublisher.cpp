#include "OnlineUpdateCatalogPublisher.h"

#include "AuthoringMutationGate.h"
#include "DurableFileTransaction.h"
#include "INIFileEditor.h"
#include "../../src/File/ResourcePathSafety.h"
#include "../../src/Update/OnlineUpdateCatalog.h"
#include "../../src/Update/ArtifactChecksum.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <filesystem>
#include <limits>
#include <set>

namespace
{
QStringList splitList(const std::string& text)
{
    QStringList values;
    for (const QString& raw :
         QString::fromStdString(text).split(',', Qt::KeepEmptyParts))
    {
        const QString value = raw.trimmed();
        if (!value.isEmpty())
        {
            values.append(value);
        }
    }
    return values;
}

bool resolveArtifact(
    const QString& root,
    const std::string& relativePath,
    QString& absolutePath)
{
    if (!OnlineUpdate::isSafeArtifactPath(relativePath))
    {
        return false;
    }
    const QFileInfo rootInfo(root);
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty() || !rootInfo.isDir())
    {
        return false;
    }
    const QFileInfo artifactInfo(
        QDir(canonicalRoot).filePath(QString::fromStdString(relativePath)));
    absolutePath = artifactInfo.canonicalFilePath();
    if (absolutePath.isEmpty() || !artifactInfo.isFile() ||
        artifactInfo.isSymLink())
    {
        return false;
    }
    const QString normalizedRoot = QDir::fromNativeSeparators(
        QDir::cleanPath(canonicalRoot));
    const QString normalizedArtifact = QDir::fromNativeSeparators(
        QDir::cleanPath(absolutePath));
    const QString prefix = normalizedRoot + QLatin1Char('/');
    return normalizedArtifact.startsWith(
        prefix,
#if defined(Q_OS_WIN)
        Qt::CaseInsensitive
#else
        Qt::CaseSensitive
#endif
    );
}

bool updateArtifact(
    INIFileEditor& ini,
    const QString& artifactRoot,
    const std::string& section,
    OnlineUpdateCatalogPublisher::Result& result)
{
    const std::string artifact = ini.get(section, "Artifact", "");
    if (!OnlineUpdate::isSafeArtifactPath(artifact))
    {
        result.status = OnlineUpdateCatalogPublisher::Status::
            UnsafeArtifactPath;
        result.detail = QString::fromStdString(section + ".Artifact=" + artifact);
        return false;
    }
    QString absolutePath;
    if (!resolveArtifact(artifactRoot, artifact, absolutePath))
    {
        result.status = OnlineUpdateCatalogPublisher::Status::ArtifactNotFound;
        result.detail = QString::fromStdString(artifact);
        return false;
    }
    std::uint32_t checksum = 0;
    std::uint64_t size = 0;
    const std::filesystem::path nativeArtifactPath =
#if defined(Q_OS_WIN)
        std::filesystem::path(absolutePath.toStdWString());
#else
        std::filesystem::u8path(absolutePath.toUtf8().constData());
#endif
    if (!OnlineUpdate::calculateFileCrc32(
            nativeArtifactPath,
            checksum,
            size))
    {
        result.status = OnlineUpdateCatalogPublisher::Status::
            ArtifactChecksumFailed;
        result.detail = absolutePath;
        return false;
    }
    ini.set(section, "Size", std::to_string(size));
    ini.set(section, "Crc32", OnlineUpdate::crc32ToLowerHex(checksum));
    result.artifactCount++;
    return true;
}
}

OnlineUpdateCatalogPublisher::Result OnlineUpdateCatalogPublisher::publish(
    const QString& templateCatalogPath,
    const QString& artifactRoot,
    const QString& outputCatalogPath)
{
    Result result;
    if (templateCatalogPath.trimmed().isEmpty() ||
        artifactRoot.trimmed().isEmpty() ||
        outputCatalogPath.trimmed().isEmpty())
    {
        return result;
    }

    QFile templateFile(templateCatalogPath);
    if (!templateFile.open(QIODevice::ReadOnly))
    {
        result.status = Status::TemplateReadFailed;
        result.detail = templateCatalogPath;
        return result;
    }
    const QByteArray templateBytes = templateFile.readAll();
    if (templateBytes.size() <= 0 ||
        templateBytes.size() >
            static_cast<qsizetype>(OnlineUpdate::MaximumCatalogBytes) ||
        templateBytes.size() > std::numeric_limits<int>::max())
    {
        result.status = Status::InvalidTemplate;
        result.detail = QStringLiteral("Catalog template is empty or too large");
        return result;
    }
    const std::string templateText(
        templateBytes.constData(),
        static_cast<std::size_t>(templateBytes.size()));
    if (!ResourcePathSafety::isValidUtf8(templateText) ||
        templateBytes.contains('\0'))
    {
        result.status = Status::InvalidTemplateEncoding;
        return result;
    }

    INIFileEditor ini;
    if (!ini.loadFromBuffer(
            templateBytes.constData(),
            static_cast<int>(templateBytes.size()),
            INIFileEditor::UnrecognizedLinePolicy::Reject))
    {
        result.status = Status::InvalidTemplate;
        return result;
    }

    std::set<std::string> sections;
    for (const std::string& section : ini.getSectionNames())
    {
        const QString sectionName = QString::fromStdString(section);
        if (sectionName.startsWith(
                QStringLiteral("Resource."), Qt::CaseInsensitive) ||
            sectionName.compare(
                QStringLiteral("Common"), Qt::CaseInsensitive) == 0)
        {
            sections.insert(section);
        }
    }
    for (const QString& target :
         splitList(ini.get("Catalog", "ProgramTargets", "")))
    {
        sections.insert("Program." + target.toStdString());
    }
    if (sections.empty())
    {
        result.status = Status::InvalidTemplate;
        result.detail = QStringLiteral("Catalog has no artifacts");
        return result;
    }
    for (const std::string& section : sections)
    {
        if (!updateArtifact(ini, artifactRoot, section, result))
        {
            return result;
        }
    }

    const std::string catalogText = ini.saveToString();
    const OnlineUpdate::CatalogParseResult parsed =
        OnlineUpdate::parseCatalog(catalogText);
    if (!parsed.succeeded())
    {
        result.status = Status::InvalidTemplate;
        if (!parsed.issues.empty())
        {
            result.detail = QString::fromStdString(
                parsed.issues.front().section + "." +
                parsed.issues.front().field);
        }
        return result;
    }

    result.catalogPath = QFileInfo(outputCatalogPath).absoluteFilePath();
    const QString outputRoot = QFileInfo(result.catalogPath).absolutePath();
    auto mutationLease = AuthoringMutationGate::instance().
        acquireMutationLeaseForPath(outputRoot);
    if (!mutationLease)
    {
        result.status = Status::PublishFailed;
        result.detail = outputRoot;
        return result;
    }
    if (!QDir().mkpath(outputRoot))
    {
        result.status = Status::PublishFailed;
        result.detail = outputRoot;
        return result;
    }
    DurableFileTransaction transaction(outputRoot);
    QString error;
    if (!transaction.addBytesWrite(
            result.catalogPath,
            QByteArray(catalogText.data(),
                static_cast<qsizetype>(catalogText.size())),
            error) ||
        !transaction.commit(error))
    {
        result.status = Status::PublishFailed;
        result.detail = error;
        return result;
    }
    result.status = Status::Success;
    result.detail = error;
    return result;
}
