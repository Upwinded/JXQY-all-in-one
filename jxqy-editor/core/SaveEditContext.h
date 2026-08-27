#pragma once

#include <QList>
#include <QString>

struct SaveEditTarget
{
    QString id;
    QString displayName;
    QString relativeDirectory;
    bool highRisk = false;
};

/// Application-wide save-data editing target. The selected target is persisted
/// independently of any one editor so trap, NPC and OBJ editors can share it.
class SaveEditContext
{
public:
    static SaveEditContext& instance();

    const QList<SaveEditTarget>& targets() const;
    int currentIndex() const;
    SaveEditTarget currentTarget() const;
    bool setCurrentIndex(int index);
    bool setCurrentId(const QString& id);

    QString resolveFilePath(const QString& assetsBasePath,
                            const QString& fileName,
                            QString* errorMessage = nullptr) const;

    /// Re-read persisted state. Intended for application configuration reloads
    /// and isolated regression tests.
    void reload();

private:
    SaveEditContext();
    int findTargetIndex(const QString& id) const;
    void refreshDisplayNames() const;

    mutable QList<SaveEditTarget> availableTargets;
    int selectedIndex = 0;
};
