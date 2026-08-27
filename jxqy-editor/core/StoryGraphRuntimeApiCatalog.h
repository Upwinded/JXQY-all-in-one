#pragma once

#include <QList>
#include <QString>

struct StoryGraphRuntimeApiDefinition
{
    QString registeredName;
    QString canonicalName;

    bool isAlias() const;
};

class StoryGraphRuntimeApiCatalog
{
public:
    static const QList<StoryGraphRuntimeApiDefinition>& definitions();

    // Script::registerFunc lowercases every registered global and Lua lookup is
    // case-sensitive. This lookup deliberately does not normalize its input.
    static const StoryGraphRuntimeApiDefinition* findExact(
        const QString& registeredName);
    static bool containsExact(const QString& registeredName);

    // A deterministic identity for the generated runtime registration table.
    static QString catalogFingerprint();
};
