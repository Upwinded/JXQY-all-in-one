#pragma once

#include <QString>

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
