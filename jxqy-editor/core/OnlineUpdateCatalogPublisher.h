#pragma once

#include <QString>
#include <QStringList>
#include <QMap>

class OnlineUpdateCatalogPublisher
{
public:
    enum class Status
    {
        Success,
        InvalidInput,
        TemplateReadFailed,
        InvalidTemplateEncoding,
        InvalidTemplate,
        UnsafeArtifactPath,
        ArtifactNotFound,
        ArtifactChecksumFailed,
        PublishFailed
    };

    struct Result
    {
        Status status = Status::InvalidInput;
        QString detail;
        QString catalogPath;
        qsizetype artifactCount = 0;
        // Empty Release ID denotes artifacts beside the primary catalog.
        QMap<QString, QStringList> artifactsByRelease;

        bool succeeded() const
        {
            return status == Status::Success;
        }
    };

    static Result publish(
        const QString& templateCatalogPath,
        const QString& artifactRoot,
        const QString& outputCatalogPath);
};
