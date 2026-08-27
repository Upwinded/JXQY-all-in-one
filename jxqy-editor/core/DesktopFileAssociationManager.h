#pragma once

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>

#include <memory>

struct DesktopRegistryValue
{
    quint32 type = 0;
    QByteArray data;

    bool operator==(const DesktopRegistryValue& other) const
    {
        return type == other.type && data == other.data;
    }
};

class DesktopRegistryAccess
{
public:
    virtual ~DesktopRegistryAccess() = default;

    virtual bool isAvailable() const = 0;
    virtual bool keyExists(const QString& path, bool& exists) = 0;
    virtual bool readValue(const QString& path,
                           const QString& valueName,
                           DesktopRegistryValue& value,
                           bool& exists) = 0;
    virtual bool writeValue(const QString& path,
                            const QString& valueName,
                            const DesktopRegistryValue& value) = 0;
    virtual bool removeValue(const QString& path,
                             const QString& valueName) = 0;
    virtual bool ensureKey(const QString& path) = 0;
    virtual bool removeKeyIfEmpty(const QString& path) = 0;
    virtual void notifyAssociationsChanged() = 0;
    virtual QString lastError() const = 0;
};

struct DesktopFileAssociationDefinition
{
    QString extension;
    QString programId;
    QString registryDescription;
    QString mimeType;
    QString macUniformTypeIdentifier;
    bool broadTextType = false;
};

struct DesktopFileAssociationState
{
    DesktopFileAssociationDefinition definition;
    bool managed = false;
    bool currentUserFallbackIsEditor = false;
    bool userChoicePresent = false;
    QString userChoiceProgramId;
    bool needsRepair = false;
};

struct DesktopFileAssociationResult
{
    bool success = false;
    bool changed = false;
    QString error;
    QList<DesktopFileAssociationState> states;
};

class DesktopFileAssociationManager
{
public:
    DesktopFileAssociationManager();
    explicit DesktopFileAssociationManager(DesktopRegistryAccess* registry);
    ~DesktopFileAssociationManager();

    DesktopFileAssociationManager(const DesktopFileAssociationManager&) = delete;
    DesktopFileAssociationManager& operator=(
        const DesktopFileAssociationManager&) = delete;

    bool isSupported() const;

    static QList<DesktopFileAssociationDefinition> supportedAssociations();
    static QString programIdForExtension(const QString& extension);
    static QString extensionRegistryPath(const QString& extension);
    static QString openWithRegistryPath(const QString& extension);
    static QString programIdRegistryPath(const QString& extension);
    static QString backupRegistryPath(const QString& extension);
    static QString userChoiceRegistryPath(const QString& extension);
    static QString capabilitiesRegistryPath();
    static QString registeredApplicationsRegistryPath();
    static QString openCommandForExecutable(const QString& executablePath);

    static DesktopRegistryValue stringRegistryValue(const QString& value);
    static bool registryValueToString(const DesktopRegistryValue& value,
                                      QString& text);

    DesktopFileAssociationResult queryStates(
        const QString& executablePath);
    DesktopFileAssociationResult applySelection(
        const QSet<QString>& selectedExtensions,
        const QString& executablePath);
    DesktopFileAssociationResult restoreAll(
        const QString& executablePath);
    DesktopFileAssociationResult repairManagedAssociations(
        const QString& executablePath);

private:
    std::unique_ptr<DesktopRegistryAccess> ownedRegistry;
    DesktopRegistryAccess* registry = nullptr;
};
