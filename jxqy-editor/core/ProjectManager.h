#pragma once

#include <QString>
#include <QStringList>
#include <QRect>
#include <QByteArray>
#include <QJsonObject>
#include <QList>

#include "AssetMigrationPolicy.h"
#include "ProjectRuntimeConfiguration.h"
#include "WindowPlacement.h"

enum class ProjectSessionWindowType
{
    Script,
    Map,
    NpcList,
    ObjectList,
    Menu,
    Image,
    Magic,
    Goods,
    Shop,
    Dialogue,
    NpcResource
};

struct ProjectSessionTextViewState
{
    static constexpr int schemaVersion = 1;

    bool isValid = false;
    int cursorPosition = 0;
    int verticalScrollValue = 0;
    int horizontalScrollValue = 0;

    bool operator==(const ProjectSessionTextViewState& other) const
    {
        return isValid == other.isValid &&
            cursorPosition == other.cursorPosition &&
            verticalScrollValue == other.verticalScrollValue &&
            horizontalScrollValue == other.horizontalScrollValue;
    }
};

struct ProjectSessionMapViewState
{
    static constexpr int schemaVersion = 1;

    bool isValid = false;
    double zoomLevel = 1.0;
    int scrollX = 0;
    int scrollY = 0;

    bool operator==(const ProjectSessionMapViewState& other) const
    {
        return isValid == other.isValid &&
            zoomLevel == other.zoomLevel &&
            scrollX == other.scrollX &&
            scrollY == other.scrollY;
    }
};

struct ProjectSessionWindowState
{
    ProjectSessionWindowType type = ProjectSessionWindowType::Script;
    QString primaryPath;
    QString npcListPath;
    QString objectListPath;
    QString npcResourcePath;
    ProjectSessionTextViewState textView;
    ProjectSessionMapViewState mapView;
    QJsonObject preservedFields;

    bool operator==(const ProjectSessionWindowState& other) const
    {
        return type == other.type &&
            primaryPath == other.primaryPath &&
            npcListPath == other.npcListPath &&
            objectListPath == other.objectListPath &&
            npcResourcePath == other.npcResourcePath &&
            textView == other.textView &&
            mapView == other.mapView &&
            preservedFields == other.preservedFields;
    }
};

struct ProjectDocumentSessionState
{
    static constexpr int schemaVersion = 1;

    QList<ProjectSessionWindowState> windows;
    QString activeWindowPath;
    QJsonObject preservedFields;

    bool operator==(const ProjectDocumentSessionState& other) const
    {
        return windows == other.windows &&
            activeWindowPath == other.activeWindowPath &&
            preservedFields == other.preservedFields;
    }
};

struct ProjectResourceConfiguration
{
    QString sourceAssetsRoot;
    QString editableAssetsRoot;
    QString activeResourcePackId;
    QString activeResourcePackEntryKey;
};

class ProjectManager
{
public:
    static constexpr int currentSchemaVersion = 1;
    static constexpr int assetMigrationSchemaVersion = 1;

    static ProjectManager& instance();

    bool newProject(const QString& projectFilePath);
    bool newProject(const QString& projectFilePath,
                    const ProjectResourceConfiguration& resourceConfiguration);
    bool openProject(const QString& projectFilePath);
    bool openProject(const QString& projectFilePath,
                     const QString& expectedEditableAssetsRoot);
    bool openProject(const QString& projectFilePath,
                     const QString& expectedEditableAssetsRoot,
                     const QString& expectedActiveResourcePackId);
    bool openProject(const QString& projectFilePath,
                     const QString& expectedEditableAssetsRoot,
                     const QString& expectedActiveResourcePackId,
                     const QString& expectedActiveResourcePackEntryKey);
    bool readProjectResourceConfiguration(
        const QString& projectFilePath,
        QString& editableAssetsRoot,
        QString& activeResourcePackId,
        QString* activeResourcePackEntryKey = nullptr) const;
    bool readProjectEditableAssetsRoot(const QString& projectFilePath,
                                       QString& editableAssetsRoot) const;
    bool saveProject();
    bool saveResourceConfiguration(
        const ProjectResourceConfiguration& resourceConfiguration);
    ProjectRuntimeConfiguration runtimeConfiguration() const;
    bool saveRuntimeConfiguration(
        const ProjectRuntimeConfiguration& runtimeConfiguration,
        ProjectRuntimeConfigurationValidationResult* validationResult =
            nullptr);
    bool runtimeConfigurationNeedsSave() const;
    bool saveProjectAs(const QString& projectFilePath);
    bool closeProject();
    bool isProjectOpen() const;
    bool hasUnsavedChanges() const;
    void markDirty();

    QString projectFilePath() const;
    QString projectRootPath() const;
    QString sourceAssetsRoot() const;
    void setSourceAssetsRoot(const QString& path);
    QString editableAssetsRoot() const;
    void setEditableAssetsRoot(const QString& path);
    QString activeResourcePackId() const;
    void setActiveResourcePackId(const QString& id);
    QString activeResourcePackEntryKey() const;
    void setActiveResourcePackEntryKey(const QString& entryKey);
    LegacyImageMigrationPolicy assetMigrationPolicy() const;
    void setAssetMigrationPolicy(const LegacyImageMigrationPolicy& policy);

    QStringList recentFiles() const;
    void addRecentFile(const QString& fileName);
    void clearRecentFiles();

    QString theme() const;
    void setTheme(const QString& theme);

    QRect windowGeometry() const;
    void setWindowGeometry(const QRect& geometry);
    WindowDisplayMode windowMode() const;
    void setWindowMode(WindowDisplayMode mode);
    void setWindowPlacement(const QRect& normalGeometry, WindowDisplayMode mode);
    bool hasWindowPlacement() const;

    QByteArray windowState() const;
    void setWindowState(const QByteArray& state);

    ProjectDocumentSessionState documentSession() const;
    void setDocumentSession(const ProjectDocumentSessionState& session);
    bool documentSessionNeedsRepair() const;

private:
    struct ProjectState
    {
        QString projectFilePath;
        QString sourceAssetsRoot;
        QString editableAssetsRoot;
        QString activeResourcePackId;
        QString activeResourcePackEntryKey;
        LegacyImageMigrationPolicy assetMigrationPolicy;
        QStringList recentFiles;
        QString theme;
        QRect windowGeometry;
        WindowDisplayMode windowMode = WindowDisplayMode::Normal;
        bool hasWindowPlacement = false;
        QByteArray windowState;
        ProjectDocumentSessionState documentSession;
        bool documentSessionNeedsRepair = false;
        ProjectRuntimeConfiguration runtimeConfiguration;
        bool runtimeConfigurationNeedsSave = false;
        QJsonObject preservedRoot;
        bool isOpen = false;
        bool hasUnsavedChanges = false;
    };

    ProjectManager() = default;
    ~ProjectManager() = default;
    ProjectManager(const ProjectManager&) = delete;
    ProjectManager& operator=(const ProjectManager&) = delete;

    ProjectState captureState() const;
    void applyState(const ProjectState& state);
    bool loadFromJson(const QJsonObject& root,
                      const QString& projectFilePath,
                      ProjectState& state) const;
    bool readProjectFile(const QString& projectFilePath,
                         ProjectState& state) const;
    bool loadLocalProjectState(
        const QString& projectFilePath,
        ProjectState& state,
        bool& found,
        bool& repaired) const;
    bool persistLocalProjectState(const ProjectState& state) const;
    QJsonObject toJson(const ProjectState& state,
                       const QString& projectFilePath) const;
    bool writeProjectFile(const QString& projectFilePath,
                          const ProjectState& state) const;
    bool openProjectInternal(const QString& projectFilePath,
                             const QString* expectedEditableAssetsRoot,
                             const QString* expectedActiveResourcePackId,
                             const QString* expectedActiveResourcePackEntryKey);

    QString m_projectFilePath;
    QString m_sourceAssetsRoot;
    QString m_editableAssetsRoot;
    QString m_activeResourcePackId;
    QString m_activeResourcePackEntryKey;
    LegacyImageMigrationPolicy m_assetMigrationPolicy;
    QStringList m_recentFiles;
    QString m_theme;
    QRect m_windowGeometry;
    WindowDisplayMode m_windowMode = WindowDisplayMode::Normal;
    bool m_hasWindowPlacement = false;
    QByteArray m_windowState;
    ProjectDocumentSessionState m_documentSession;
    bool m_documentSessionNeedsRepair = false;
    ProjectRuntimeConfiguration m_runtimeConfiguration;
    bool m_runtimeConfigurationNeedsSave = false;
    QJsonObject m_preservedRoot;
    bool m_isOpen = false;
    bool m_hasUnsavedChanges = false;

    static constexpr int maxRecentFiles = 16;
};
