#include "StoryGraphRuntimeApiCatalog.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QHash>
#include <QtGlobal>

namespace
{
struct RawRuntimeApiDefinition
{
    const char* registeredName;
    const char* canonicalName;
};

const RawRuntimeApiDefinition RawDefinitions[] = {
#include "StoryGraphRuntimeApiCatalogEntries.inc"
};

void appendFingerprintField(
    QByteArray& payload,
    const QByteArray& value)
{
    payload.append(QByteArray::number(value.size()));
    payload.append(':');
    payload.append(value);
}

struct RuntimeApiCatalogStorage
{
    QList<StoryGraphRuntimeApiDefinition> definitions;
    QHash<QString, int> indexByRegisteredName;
    QString fingerprint;

    RuntimeApiCatalogStorage()
    {
        QByteArray fingerprintPayload(
            "jxqy-editor.story-graph.runtime-api-catalog.v1");
        for (const RawRuntimeApiDefinition& rawDefinition :
             RawDefinitions)
        {
            StoryGraphRuntimeApiDefinition definition;
            definition.registeredName =
                QString::fromLatin1(
                    rawDefinition.registeredName);
            definition.canonicalName =
                QString::fromLatin1(
                    rawDefinition.canonicalName);

            const auto existing =
                indexByRegisteredName.constFind(
                    definition.registeredName);
            if (existing !=
                indexByRegisteredName.constEnd())
            {
                const StoryGraphRuntimeApiDefinition&
                    existingDefinition =
                        definitions.at(existing.value());
                if (existingDefinition.canonicalName !=
                    definition.canonicalName)
                {
                    qFatal(
                        "Conflicting generated story graph runtime API "
                        "definition");
                }
                continue;
            }

            const int index = definitions.size();
            definitions.append(definition);
            indexByRegisteredName.insert(
                definition.registeredName,
                index);
            appendFingerprintField(
                fingerprintPayload,
                definition.registeredName.toLatin1());
            appendFingerprintField(
                fingerprintPayload,
                definition.canonicalName.toLatin1());
        }

        fingerprint =
            QStringLiteral("story-api-v1:") +
            QString::fromLatin1(
                QCryptographicHash::hash(
                    fingerprintPayload,
                    QCryptographicHash::Sha256)
                    .toHex());
    }
};

const RuntimeApiCatalogStorage& storage()
{
    static const RuntimeApiCatalogStorage result;
    return result;
}
}

bool StoryGraphRuntimeApiDefinition::isAlias() const
{
    return registeredName != canonicalName;
}

const QList<StoryGraphRuntimeApiDefinition>&
StoryGraphRuntimeApiCatalog::definitions()
{
    return storage().definitions;
}

const StoryGraphRuntimeApiDefinition*
StoryGraphRuntimeApiCatalog::findExact(
    const QString& registeredName)
{
    const RuntimeApiCatalogStorage& catalog =
        storage();
    const auto index =
        catalog.indexByRegisteredName.constFind(
            registeredName);
    if (index ==
        catalog.indexByRegisteredName.constEnd())
    {
        return nullptr;
    }
    return &catalog.definitions.at(index.value());
}

bool StoryGraphRuntimeApiCatalog::containsExact(
    const QString& registeredName)
{
    return findExact(registeredName) != nullptr;
}

QString StoryGraphRuntimeApiCatalog::catalogFingerprint()
{
    return storage().fingerprint;
}
