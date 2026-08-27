#include "ProjectManager.h"
#include "AuthoringMutationGate.h"
#include "EditorAssetPath.h"
#include "EditorSettings.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QSet>
#include <QSettings>

#include <cmath>
#include <limits>

namespace
{
constexpr int localProjectStateSchemaVersion = 1;

QString normalizedProjectPath(const QString& path)
{
    const QFileInfo fileInfo(path);
    const QString canonicalPath = fileInfo.canonicalFilePath();
    return QDir::cleanPath(
        canonicalPath.isEmpty() ? fileInfo.absoluteFilePath() : canonicalPath);
}

QString localProjectStateGroup(const QString& projectFilePath)
{
    QString path = normalizedProjectPath(projectFilePath);
#if defined(Q_OS_WIN)
    path = path.toLower();
#endif
    const QByteArray digest = QCryptographicHash::hash(
        path.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("projectLocalState/%1")
        .arg(QString::fromLatin1(digest));
}

bool projectPathsMatch(const QString& left, const QString& right)
{
    if (left.isEmpty() || right.isEmpty())
        return false;
#if defined(Q_OS_WIN)
    constexpr Qt::CaseSensitivity pathCaseSensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity pathCaseSensitivity = Qt::CaseSensitive;
#endif
    return normalizedProjectPath(left).compare(
               normalizedProjectPath(right), pathCaseSensitivity) == 0;
}

QString resolveProjectPath(const QString& storedPath,
                           const QString& projectFilePath)
{
    if (storedPath.isEmpty())
        return QString();

    const QString portablePath = QDir::fromNativeSeparators(storedPath);
    if (QDir::isAbsolutePath(portablePath))
        return QDir::cleanPath(portablePath);

    const QDir projectDirectory(QFileInfo(projectFilePath).absolutePath());
    return QDir::cleanPath(projectDirectory.absoluteFilePath(portablePath));
}

QString pathRelativeToProject(const QString& resolvedPath,
                              const QString& projectFilePath)
{
    if (resolvedPath.isEmpty())
        return QString();

    const QString absolutePath = resolveProjectPath(
        resolvedPath, projectFilePath);
    const QDir projectDirectory(QFileInfo(projectFilePath).absolutePath());
    const QString relativePath = QDir::fromNativeSeparators(
        projectDirectory.relativeFilePath(absolutePath));
    return QDir::cleanPath(relativePath);
}

bool readJsonInteger(const QJsonObject& object, const QString& key, int& value)
{
    if (!object.contains(key))
    {
        value = 0;
        return true;
    }

    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isDouble())
        return false;

    const double number = jsonValue.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < std::numeric_limits<int>::min() ||
        number > std::numeric_limits<int>::max())
    {
        return false;
    }

    value = static_cast<int>(number);
    return true;
}

bool readAssetMigrationPolicy(const QJsonObject& root,
                              LegacyImageMigrationPolicy& policy,
                              bool& repaired)
{
    policy = LegacyImageMigrationPolicy();
    const QString assetMigrationKey = QStringLiteral("assetMigration");
    if (!root.contains(assetMigrationKey))
    {
        repaired = true;
        return true;
    }

    const QJsonValue assetMigrationValue = root.value(assetMigrationKey);
    if (!assetMigrationValue.isObject())
    {
        repaired = true;
        return true;
    }

    const QJsonObject assetMigration = assetMigrationValue.toObject();
    int version = 0;
    if (!readJsonInteger(
            assetMigration, QStringLiteral("version"), version))
    {
        repaired = true;
        return true;
    }
    if (version > ProjectManager::assetMigrationSchemaVersion)
        return false;
    if (version != ProjectManager::assetMigrationSchemaVersion)
    {
        repaired = true;
        return true;
    }

    const QJsonValue legacyImagesValue = assetMigration.value(
        QStringLiteral("legacyImages"));
    if (!legacyImagesValue.isObject())
    {
        repaired = true;
        return true;
    }

    const QJsonObject legacyImages = legacyImagesValue.toObject();
    const QJsonValue modesValue = legacyImages.value(QStringLiteral("modes"));
    if (!modesValue.isObject())
    {
        repaired = true;
    }
    else
    {
        const QJsonObject modes = modesValue.toObject();
        for (const LegacyImageCategoryDefinition& item :
             LegacyImageMigrationPolicy::definitions())
        {
            const QJsonValue modeValue = modes.value(item.id);
            if (!modeValue.isString())
            {
                repaired = true;
                continue;
            }

            const std::optional<LegacyImageMode> mode =
                LegacyImageMigrationPolicy::modeFromId(modeValue.toString());
            if (!mode || !policy.setMode(item.category, *mode))
                repaired = true;
        }
    }

    const QJsonValue cropValue = legacyImages.value(
        QStringLiteral("cropTransparent"));
    if (!cropValue.isBool())
    {
        repaired = true;
    }
    else
    {
        policy.setCropTransparent(cropValue.toBool());
    }
    return true;
}

bool readJsonGeometry(const QJsonValue& value, QRect& geometry)
{
    if (!value.isObject())
        return false;

    const QJsonObject object = value.toObject();
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    if (!readJsonInteger(object, QStringLiteral("x"), x) ||
        !readJsonInteger(object, QStringLiteral("y"), y) ||
        !readJsonInteger(object, QStringLiteral("width"), width) ||
        !readJsonInteger(object, QStringLiteral("height"), height))
    {
        return false;
    }
    if (width <= 0 || height <= 0)
    {
        geometry = QRect();
        return true;
    }

    const qint64 right = static_cast<qint64>(x) + width - 1;
    const qint64 bottom = static_cast<qint64>(y) + height - 1;
    if (right < std::numeric_limits<int>::min() ||
        right > std::numeric_limits<int>::max() ||
        bottom < std::numeric_limits<int>::min() ||
        bottom > std::numeric_limits<int>::max())
    {
        return false;
    }

    geometry = QRect(x, y, width, height);
    return true;
}

QString projectSessionPathKey(const QString& path)
{
    QString key = path;
#if defined(Q_OS_WIN)
    key = key.toLower();
#endif
    return key;
}

bool normalizeProjectSessionPath(const QJsonValue& value, QString& path)
{
    if (value.isUndefined())
    {
        path.clear();
        return true;
    }
    if (!value.isString())
        return false;
    const QString storedPath = value.toString();
    if (storedPath.isEmpty())
    {
        path.clear();
        return true;
    }
    return EditorAssetPath::normalizeResourcePath(storedPath, path);
}

bool isValidProjectSessionTextView(
    const ProjectSessionTextViewState& state)
{
    return state.isValid &&
        state.cursorPosition >= 0 &&
        state.verticalScrollValue >= 0 &&
        state.horizontalScrollValue >= 0;
}

bool isValidProjectSessionMapView(
    const ProjectSessionMapViewState& state)
{
    return state.isValid &&
        std::isfinite(state.zoomLevel) &&
        state.zoomLevel >= 0.01 &&
        state.zoomLevel <= 4.0;
}

ProjectSessionTextViewState readProjectSessionTextView(
    const QJsonValue& value)
{
    ProjectSessionTextViewState state;
    if (!value.isObject())
        return state;

    const QJsonObject object = value.toObject();
    int version = 0;
    if (!object.contains(QStringLiteral("version")) ||
        !object.contains(QStringLiteral("cursor")) ||
        !object.contains(QStringLiteral("verticalScroll")) ||
        !object.contains(QStringLiteral("horizontalScroll")) ||
        !readJsonInteger(object, QStringLiteral("version"), version) ||
        version != ProjectSessionTextViewState::schemaVersion ||
        !readJsonInteger(
            object, QStringLiteral("cursor"), state.cursorPosition) ||
        !readJsonInteger(
            object, QStringLiteral("verticalScroll"),
            state.verticalScrollValue) ||
        !readJsonInteger(
            object, QStringLiteral("horizontalScroll"),
            state.horizontalScrollValue))
    {
        return ProjectSessionTextViewState();
    }

    state.isValid = true;
    return isValidProjectSessionTextView(state)
        ? state
        : ProjectSessionTextViewState();
}

ProjectSessionMapViewState readProjectSessionMapView(
    const QJsonValue& value)
{
    ProjectSessionMapViewState state;
    if (!value.isObject())
        return state;

    const QJsonObject object = value.toObject();
    int version = 0;
    const QJsonValue zoomValue =
        object.value(QStringLiteral("zoom"));
    if (!object.contains(QStringLiteral("version")) ||
        !object.contains(QStringLiteral("scrollX")) ||
        !object.contains(QStringLiteral("scrollY")) ||
        !readJsonInteger(object, QStringLiteral("version"), version) ||
        version != ProjectSessionMapViewState::schemaVersion ||
        !zoomValue.isDouble() ||
        !readJsonInteger(
            object, QStringLiteral("scrollX"), state.scrollX) ||
        !readJsonInteger(
            object, QStringLiteral("scrollY"), state.scrollY))
    {
        return state;
    }

    state.isValid = true;
    state.zoomLevel = zoomValue.toDouble();
    return isValidProjectSessionMapView(state)
        ? state
        : ProjectSessionMapViewState();
}

ProjectDocumentSessionState normalizedProjectDocumentSession(
    const ProjectDocumentSessionState& source, bool& repaired)
{
    ProjectDocumentSessionState normalized;
    normalized.preservedFields = source.preservedFields;
    QSet<QString> documentKeys;

    for (ProjectSessionWindowState window : source.windows)
    {
        QString primaryPath;
        QString npcListPath;
        QString objectListPath;
        QString npcResourcePath;
        if (!EditorAssetPath::normalizeResourcePath(
                window.primaryPath, primaryPath))
        {
            repaired = true;
            continue;
        }
        if (!window.npcListPath.isEmpty() &&
            !EditorAssetPath::normalizeResourcePath(
                window.npcListPath, npcListPath))
        {
            repaired = true;
            continue;
        }
        if (!window.objectListPath.isEmpty() &&
            !EditorAssetPath::normalizeResourcePath(
                window.objectListPath, objectListPath))
        {
            repaired = true;
            continue;
        }
        if (!window.npcResourcePath.isEmpty() &&
            !EditorAssetPath::normalizeResourcePath(
                window.npcResourcePath, npcResourcePath))
        {
            repaired = true;
            continue;
        }

        bool invalidCompositeShape = false;
        if (window.type != ProjectSessionWindowType::Script)
            window.textView = ProjectSessionTextViewState();
        if (window.type != ProjectSessionWindowType::Map)
            window.mapView = ProjectSessionMapViewState();
        switch (window.type)
        {
        case ProjectSessionWindowType::Map:
            if (!isValidProjectSessionMapView(window.mapView))
                window.mapView = ProjectSessionMapViewState();
            if (!npcResourcePath.isEmpty())
            {
                repaired = true;
                npcResourcePath.clear();
            }
            break;
        case ProjectSessionWindowType::NpcList:
            invalidCompositeShape = !npcListPath.isEmpty();
            break;
        case ProjectSessionWindowType::ObjectList:
            invalidCompositeShape = !npcListPath.isEmpty() ||
                !objectListPath.isEmpty();
            break;
        case ProjectSessionWindowType::NpcResource:
            invalidCompositeShape = !npcListPath.isEmpty() ||
                !objectListPath.isEmpty() ||
                !npcResourcePath.isEmpty();
            break;
        case ProjectSessionWindowType::Script:
            if (!isValidProjectSessionTextView(window.textView))
                window.textView = ProjectSessionTextViewState();
            [[fallthrough]];
        case ProjectSessionWindowType::Menu:
        case ProjectSessionWindowType::Image:
        case ProjectSessionWindowType::Magic:
        case ProjectSessionWindowType::Goods:
        case ProjectSessionWindowType::Shop:
        case ProjectSessionWindowType::Dialogue:
            if (!npcListPath.isEmpty() || !objectListPath.isEmpty() ||
                !npcResourcePath.isEmpty())
            {
                repaired = true;
            }
            npcListPath.clear();
            objectListPath.clear();
            npcResourcePath.clear();
            break;
        }
        if (invalidCompositeShape)
        {
            repaired = true;
            continue;
        }

        const QStringList paths = {
            primaryPath, npcListPath, objectListPath, npcResourcePath};
        bool duplicatePath = false;
        QSet<QString> windowDocumentKeys;
        for (const QString& path : paths)
        {
            if (path.isEmpty())
                continue;
            const QString key = projectSessionPathKey(path);
            if (windowDocumentKeys.contains(key) ||
                documentKeys.contains(key))
            {
                duplicatePath = true;
                break;
            }
            windowDocumentKeys.insert(key);
        }
        if (duplicatePath)
        {
            repaired = true;
            continue;
        }

        window.primaryPath = primaryPath;
        window.npcListPath = npcListPath;
        window.objectListPath = objectListPath;
        window.npcResourcePath = npcResourcePath;
        normalized.windows.append(window);
        for (const QString& path : paths)
        {
            if (!path.isEmpty())
                documentKeys.insert(projectSessionPathKey(path));
        }
    }

    QString activeWindowPath;
    if (!source.activeWindowPath.isEmpty() &&
        !EditorAssetPath::normalizeResourcePath(
            source.activeWindowPath, activeWindowPath))
    {
        repaired = true;
    }
    if (!activeWindowPath.isEmpty())
    {
        bool matchesWindow = false;
        const QString activeKey = projectSessionPathKey(activeWindowPath);
        for (const ProjectSessionWindowState& window : normalized.windows)
        {
            if (projectSessionPathKey(window.primaryPath) == activeKey)
            {
                matchesWindow = true;
                break;
            }
        }
        if (!matchesWindow)
        {
            repaired = true;
            activeWindowPath.clear();
        }
    }
    normalized.activeWindowPath = activeWindowPath;
    return normalized;
}

ProjectDocumentSessionState readProjectDocumentSession(
    const QJsonObject& root, bool& repaired)
{
    ProjectDocumentSessionState session;
    const QString sessionKey = QStringLiteral("documentSession");
    if (!root.contains(sessionKey))
        return session;

    const QJsonValue sessionValue = root.value(sessionKey);
    if (!sessionValue.isObject())
    {
        repaired = true;
        return session;
    }

    const QJsonObject sessionObject = sessionValue.toObject();
    session.preservedFields = sessionObject;
    session.preservedFields.remove(QStringLiteral("version"));
    session.preservedFields.remove(QStringLiteral("windows"));
    session.preservedFields.remove(QStringLiteral("activeWindow"));
    int version = 0;
    if (!readJsonInteger(
            sessionObject, QStringLiteral("version"), version) ||
        version != ProjectDocumentSessionState::schemaVersion ||
        !sessionObject.value(QStringLiteral("windows")).isArray())
    {
        repaired = true;
        session.preservedFields = QJsonObject();
        return session;
    }

    const QJsonArray windows =
        sessionObject.value(QStringLiteral("windows")).toArray();
    for (const QJsonValue& windowValue : windows)
    {
        if (!windowValue.isObject())
        {
            repaired = true;
            continue;
        }

        const QJsonObject windowObject = windowValue.toObject();
        const QJsonValue typeValue = windowObject.value(QStringLiteral("type"));
        if (!typeValue.isString())
        {
            repaired = true;
            continue;
        }

        ProjectSessionWindowState window;
        const QString type = typeValue.toString();
        if (type == QStringLiteral("script"))
            window.type = ProjectSessionWindowType::Script;
        else if (type == QStringLiteral("map"))
            window.type = ProjectSessionWindowType::Map;
        else if (type == QStringLiteral("npcList"))
            window.type = ProjectSessionWindowType::NpcList;
        else if (type == QStringLiteral("objectList"))
            window.type = ProjectSessionWindowType::ObjectList;
        else if (type == QStringLiteral("menu"))
            window.type = ProjectSessionWindowType::Menu;
        else if (type == QStringLiteral("image"))
            window.type = ProjectSessionWindowType::Image;
        else if (type == QStringLiteral("magic"))
            window.type = ProjectSessionWindowType::Magic;
        else if (type == QStringLiteral("goods"))
            window.type = ProjectSessionWindowType::Goods;
        else if (type == QStringLiteral("shop"))
            window.type = ProjectSessionWindowType::Shop;
        else if (type == QStringLiteral("dialogue"))
            window.type = ProjectSessionWindowType::Dialogue;
        else if (type == QStringLiteral("npcResource"))
            window.type = ProjectSessionWindowType::NpcResource;
        else
        {
            repaired = true;
            continue;
        }

        if (!normalizeProjectSessionPath(
                windowObject.value(QStringLiteral("path")),
                window.primaryPath) ||
            window.primaryPath.isEmpty())
        {
            repaired = true;
            continue;
        }
        if (window.type == ProjectSessionWindowType::Map ||
            window.type == ProjectSessionWindowType::NpcList ||
            window.type == ProjectSessionWindowType::ObjectList ||
            window.type == ProjectSessionWindowType::NpcResource)
        {
            if (!normalizeProjectSessionPath(
                    windowObject.value(QStringLiteral("npcList")),
                    window.npcListPath) ||
                !normalizeProjectSessionPath(
                    windowObject.value(QStringLiteral("objectList")),
                    window.objectListPath) ||
                !normalizeProjectSessionPath(
                    windowObject.value(QStringLiteral("npcResource")),
                    window.npcResourcePath))
            {
                repaired = true;
                continue;
            }
        }
        else if (windowObject.contains(QStringLiteral("npcList")) ||
                 windowObject.contains(QStringLiteral("objectList")) ||
                 windowObject.contains(QStringLiteral("npcResource")))
        {
            repaired = true;
        }
        if (window.type == ProjectSessionWindowType::Script)
        {
            window.textView = readProjectSessionTextView(
                windowObject.value(QStringLiteral("view")));
        }
        else if (window.type == ProjectSessionWindowType::Map)
        {
            window.mapView = readProjectSessionMapView(
                windowObject.value(QStringLiteral("view")));
        }
        window.preservedFields = windowObject;
        window.preservedFields.remove(QStringLiteral("type"));
        window.preservedFields.remove(QStringLiteral("path"));
        window.preservedFields.remove(QStringLiteral("npcList"));
        window.preservedFields.remove(QStringLiteral("objectList"));
        window.preservedFields.remove(QStringLiteral("npcResource"));
        if (window.type == ProjectSessionWindowType::Script ||
            window.type == ProjectSessionWindowType::Map)
        {
            window.preservedFields.remove(QStringLiteral("view"));
        }
        session.windows.append(window);
    }

    const QJsonValue activeWindowValue =
        sessionObject.value(QStringLiteral("activeWindow"));
    if (!activeWindowValue.isUndefined() &&
        !normalizeProjectSessionPath(
            activeWindowValue, session.activeWindowPath))
    {
        repaired = true;
        session.activeWindowPath.clear();
    }

    return normalizedProjectDocumentSession(session, repaired);
}

QJsonObject projectDocumentSessionToJson(
    const ProjectDocumentSessionState& session)
{
    QJsonObject sessionObject = session.preservedFields;
    sessionObject["version"] =
        ProjectDocumentSessionState::schemaVersion;
    QJsonArray sessionWindows;
    for (const ProjectSessionWindowState& window : session.windows)
    {
        QJsonObject windowObject = window.preservedFields;
        switch (window.type)
        {
        case ProjectSessionWindowType::Script:
            windowObject["type"] = QStringLiteral("script");
            break;
        case ProjectSessionWindowType::Map:
            windowObject["type"] = QStringLiteral("map");
            break;
        case ProjectSessionWindowType::NpcList:
            windowObject["type"] = QStringLiteral("npcList");
            break;
        case ProjectSessionWindowType::ObjectList:
            windowObject["type"] = QStringLiteral("objectList");
            break;
        case ProjectSessionWindowType::Menu:
            windowObject["type"] = QStringLiteral("menu");
            break;
        case ProjectSessionWindowType::Image:
            windowObject["type"] = QStringLiteral("image");
            break;
        case ProjectSessionWindowType::Magic:
            windowObject["type"] = QStringLiteral("magic");
            break;
        case ProjectSessionWindowType::Goods:
            windowObject["type"] = QStringLiteral("goods");
            break;
        case ProjectSessionWindowType::Shop:
            windowObject["type"] = QStringLiteral("shop");
            break;
        case ProjectSessionWindowType::Dialogue:
            windowObject["type"] = QStringLiteral("dialogue");
            break;
        case ProjectSessionWindowType::NpcResource:
            windowObject["type"] = QStringLiteral("npcResource");
            break;
        }
        windowObject["path"] = window.primaryPath;
        if (window.type == ProjectSessionWindowType::Map)
        {
            if (window.npcListPath.isEmpty())
                windowObject.remove(QStringLiteral("npcList"));
            else
                windowObject["npcList"] = window.npcListPath;
            if (window.objectListPath.isEmpty())
                windowObject.remove(QStringLiteral("objectList"));
            else
                windowObject["objectList"] = window.objectListPath;
            windowObject.remove(QStringLiteral("npcResource"));
        }
        else if (window.type == ProjectSessionWindowType::NpcList)
        {
            windowObject.remove(QStringLiteral("npcList"));
            if (window.objectListPath.isEmpty())
                windowObject.remove(QStringLiteral("objectList"));
            else
                windowObject["objectList"] = window.objectListPath;
            if (window.npcResourcePath.isEmpty())
                windowObject.remove(QStringLiteral("npcResource"));
            else
                windowObject["npcResource"] = window.npcResourcePath;
        }
        else if (window.type == ProjectSessionWindowType::ObjectList)
        {
            windowObject.remove(QStringLiteral("npcList"));
            windowObject.remove(QStringLiteral("objectList"));
            if (window.npcResourcePath.isEmpty())
                windowObject.remove(QStringLiteral("npcResource"));
            else
                windowObject["npcResource"] = window.npcResourcePath;
        }
        else
        {
            windowObject.remove(QStringLiteral("npcList"));
            windowObject.remove(QStringLiteral("objectList"));
            windowObject.remove(QStringLiteral("npcResource"));
        }
        if (window.type == ProjectSessionWindowType::Script)
        {
            if (isValidProjectSessionTextView(window.textView))
            {
                QJsonObject viewObject;
                viewObject[QStringLiteral("version")] =
                    ProjectSessionTextViewState::schemaVersion;
                viewObject[QStringLiteral("cursor")] =
                    window.textView.cursorPosition;
                viewObject[QStringLiteral("verticalScroll")] =
                    window.textView.verticalScrollValue;
                viewObject[QStringLiteral("horizontalScroll")] =
                    window.textView.horizontalScrollValue;
                windowObject[QStringLiteral("view")] = viewObject;
            }
            else
            {
                windowObject.remove(QStringLiteral("view"));
            }
        }
        else if (window.type == ProjectSessionWindowType::Map)
        {
            if (isValidProjectSessionMapView(window.mapView))
            {
                QJsonObject viewObject;
                viewObject[QStringLiteral("version")] =
                    ProjectSessionMapViewState::schemaVersion;
                viewObject[QStringLiteral("zoom")] =
                    window.mapView.zoomLevel;
                viewObject[QStringLiteral("scrollX")] =
                    window.mapView.scrollX;
                viewObject[QStringLiteral("scrollY")] =
                    window.mapView.scrollY;
                windowObject[QStringLiteral("view")] = viewObject;
            }
            else
            {
                windowObject.remove(QStringLiteral("view"));
            }
        }
        sessionWindows.append(windowObject);
    }
    sessionObject["windows"] = sessionWindows;
    if (session.activeWindowPath.isEmpty())
        sessionObject.remove(QStringLiteral("activeWindow"));
    else
        sessionObject["activeWindow"] = session.activeWindowPath;
    return sessionObject;
}
}

ProjectManager& ProjectManager::instance()
{
    static ProjectManager mgr;
    return mgr;
}

bool ProjectManager::newProject(const QString& projectFilePath)
{
    return newProject(projectFilePath, ProjectResourceConfiguration());
}

bool ProjectManager::newProject(
    const QString& projectFilePath,
    const ProjectResourceConfiguration& resourceConfiguration)
{
    if (m_isOpen && m_hasUnsavedChanges && !saveProject())
        return false;
    const ProjectState previousState = captureState();

    ProjectState newState;
    newState.projectFilePath = projectFilePath;
    newState.sourceAssetsRoot = resolveProjectPath(
        resourceConfiguration.sourceAssetsRoot, projectFilePath);
    newState.editableAssetsRoot = resolveProjectPath(
        resourceConfiguration.editableAssetsRoot, projectFilePath);
    newState.activeResourcePackId =
        resourceConfiguration.activeResourcePackId.trimmed();
    newState.activeResourcePackEntryKey =
        resourceConfiguration.activeResourcePackEntryKey.trimmed();
    newState.theme = QStringLiteral("dark");
    newState.isOpen = true;

    if (!writeProjectFile(projectFilePath, newState))
    {
        applyState(previousState);
        return false;
    }

    applyState(newState);
    persistLocalProjectState(captureState());
    return true;
}

bool ProjectManager::openProject(const QString& projectFilePath)
{
    return openProjectInternal(projectFilePath, nullptr, nullptr, nullptr);
}

bool ProjectManager::openProject(const QString& projectFilePath,
                                 const QString& expectedEditableAssetsRoot)
{
    return openProjectInternal(
        projectFilePath, &expectedEditableAssetsRoot, nullptr, nullptr);
}

bool ProjectManager::openProject(
    const QString& projectFilePath,
    const QString& expectedEditableAssetsRoot,
    const QString& expectedActiveResourcePackId)
{
    return openProjectInternal(projectFilePath,
        &expectedEditableAssetsRoot, &expectedActiveResourcePackId, nullptr);
}

bool ProjectManager::openProject(
    const QString& projectFilePath,
    const QString& expectedEditableAssetsRoot,
    const QString& expectedActiveResourcePackId,
    const QString& expectedActiveResourcePackEntryKey)
{
    return openProjectInternal(
        projectFilePath,
        &expectedEditableAssetsRoot,
        &expectedActiveResourcePackId,
        &expectedActiveResourcePackEntryKey);
}

bool ProjectManager::openProjectInternal(const QString& projectFilePath,
                                          const QString* expectedEditableAssetsRoot,
                                          const QString* expectedActiveResourcePackId,
                                          const QString* expectedActiveResourcePackEntryKey)
{
    const bool openingCurrentProject =
        m_isOpen && projectPathsMatch(m_projectFilePath, projectFilePath);

    ProjectState openedState;
    if (!readProjectFile(projectFilePath, openedState))
        return false;

    const auto matchesExpectedResourceConfiguration =
        [expectedEditableAssetsRoot,
         expectedActiveResourcePackId,
         expectedActiveResourcePackEntryKey](
            const ProjectState& state)
        {
            return
                (!expectedEditableAssetsRoot ||
                 state.editableAssetsRoot ==
                     *expectedEditableAssetsRoot) &&
                (!expectedActiveResourcePackId ||
                 state.activeResourcePackId ==
                     *expectedActiveResourcePackId) &&
                (!expectedActiveResourcePackEntryKey ||
                 state.activeResourcePackEntryKey ==
                     *expectedActiveResourcePackEntryKey);
        };
    const bool savingCurrentProjectBeforeOpen =
        openingCurrentProject && m_hasUnsavedChanges;
    if (!savingCurrentProjectBeforeOpen &&
        !matchesExpectedResourceConfiguration(openedState))
    {
        return false;
    }

    if (m_isOpen && m_hasUnsavedChanges)
    {
        if (openingCurrentProject)
        {
            if (!matchesExpectedResourceConfiguration(
                    captureState()))
            {
                return false;
            }
        }
        if (!saveProject())
            return false;

        if (openingCurrentProject)
        {
            if (!readProjectFile(projectFilePath, openedState))
                return false;
            if (!matchesExpectedResourceConfiguration(
                    openedState))
            {
                return false;
            }
        }
    }

    applyState(openedState);
    persistLocalProjectState(captureState());
    return true;
}

bool ProjectManager::readProjectResourceConfiguration(
    const QString& projectFilePath,
    QString& editableAssetsRoot,
    QString& activeResourcePackId,
    QString* activeResourcePackEntryKey) const
{
    ProjectState state;
    if (!readProjectFile(projectFilePath, state))
        return false;

    editableAssetsRoot = state.editableAssetsRoot;
    activeResourcePackId = state.activeResourcePackId;
    if (activeResourcePackEntryKey)
    {
        *activeResourcePackEntryKey =
            state.activeResourcePackEntryKey;
    }
    return true;
}

bool ProjectManager::readProjectEditableAssetsRoot(
    const QString& projectFilePath, QString& editableAssetsRoot) const
{
    QString activeResourcePackId;
    return readProjectResourceConfiguration(
        projectFilePath, editableAssetsRoot, activeResourcePackId);
}

bool ProjectManager::saveProject()
{
    if (!m_isOpen || m_projectFilePath.isEmpty())
        return false;

    if (!writeProjectFile(m_projectFilePath, captureState()))
        return false;

    persistLocalProjectState(captureState());
    m_documentSessionNeedsRepair = false;
    m_runtimeConfigurationNeedsSave = false;
    m_hasUnsavedChanges = false;
    return true;
}

bool ProjectManager::saveResourceConfiguration(
    const ProjectResourceConfiguration& resourceConfiguration)
{
    if (!m_isOpen || m_projectFilePath.isEmpty())
        return false;

    ProjectState updatedState = captureState();
    updatedState.sourceAssetsRoot = resolveProjectPath(
        resourceConfiguration.sourceAssetsRoot, m_projectFilePath);
    updatedState.editableAssetsRoot = resolveProjectPath(
        resourceConfiguration.editableAssetsRoot, m_projectFilePath);
    updatedState.activeResourcePackId =
        resourceConfiguration.activeResourcePackId.trimmed();
    updatedState.activeResourcePackEntryKey =
        resourceConfiguration.activeResourcePackEntryKey.trimmed();
    updatedState.documentSessionNeedsRepair = false;
    updatedState.runtimeConfigurationNeedsSave = false;
    updatedState.hasUnsavedChanges = false;

    if (!writeProjectFile(m_projectFilePath, updatedState))
        return false;

    applyState(updatedState);
    persistLocalProjectState(captureState());
    return true;
}

ProjectRuntimeConfiguration ProjectManager::runtimeConfiguration() const
{
    return m_runtimeConfiguration;
}

bool ProjectManager::runtimeConfigurationNeedsSave() const
{
    return m_runtimeConfigurationNeedsSave;
}

bool ProjectManager::saveRuntimeConfiguration(
    const ProjectRuntimeConfiguration& runtimeConfiguration,
    ProjectRuntimeConfigurationValidationResult* validationResult)
{
    if (validationResult)
        *validationResult = {};
    if (!m_isOpen || m_projectFilePath.isEmpty())
        return false;

    ProjectRuntimeConfiguration normalizedConfiguration =
        runtimeConfiguration;
    bool repaired = false;
    if (!normalizeProjectRuntimeConfiguration(
            normalizedConfiguration, repaired, validationResult))
    {
        return false;
    }

    ProjectState updatedState = captureState();
    updatedState.runtimeConfiguration = normalizedConfiguration;
    updatedState.documentSessionNeedsRepair = false;
    updatedState.runtimeConfigurationNeedsSave = false;
    updatedState.hasUnsavedChanges = false;

    if (!writeProjectFile(m_projectFilePath, updatedState))
        return false;

    applyState(updatedState);
    persistLocalProjectState(captureState());
    return true;
}

bool ProjectManager::saveProjectAs(const QString& projectFilePath)
{
    if (!m_isOpen || projectFilePath.isEmpty())
        return false;

    if (!writeProjectFile(projectFilePath, captureState()))
        return false;

    m_projectFilePath = projectFilePath;
    persistLocalProjectState(captureState());
    m_documentSessionNeedsRepair = false;
    m_runtimeConfigurationNeedsSave = false;
    m_hasUnsavedChanges = false;
    return true;
}

bool ProjectManager::closeProject()
{
    if (m_isOpen && m_hasUnsavedChanges)
    {
        if (!saveProject())
        {
            return false;
        }
    }
    m_projectFilePath.clear();
    m_sourceAssetsRoot.clear();
    m_editableAssetsRoot.clear();
    m_activeResourcePackId.clear();
    m_activeResourcePackEntryKey.clear();
    m_assetMigrationPolicy = LegacyImageMigrationPolicy();
    m_recentFiles.clear();
    m_theme.clear();
    m_windowGeometry = QRect();
    m_windowMode = WindowDisplayMode::Normal;
    m_hasWindowPlacement = false;
    m_windowState.clear();
    m_documentSession = ProjectDocumentSessionState();
    m_documentSessionNeedsRepair = false;
    m_runtimeConfiguration = ProjectRuntimeConfiguration();
    m_runtimeConfigurationNeedsSave = false;
    m_preservedRoot = QJsonObject();
    m_isOpen = false;
    m_hasUnsavedChanges = false;
    return true;
}

bool ProjectManager::isProjectOpen() const
{
    return m_isOpen;
}

bool ProjectManager::hasUnsavedChanges() const
{
    return m_hasUnsavedChanges;
}

void ProjectManager::markDirty()
{
    m_hasUnsavedChanges = true;
}

QString ProjectManager::projectFilePath() const
{
    return m_projectFilePath;
}

QString ProjectManager::projectRootPath() const
{
    if (m_projectFilePath.isEmpty())
        return QString();
    return QDir::cleanPath(QFileInfo(m_projectFilePath).absolutePath());
}

QString ProjectManager::sourceAssetsRoot() const
{
    return m_sourceAssetsRoot;
}

void ProjectManager::setSourceAssetsRoot(const QString& path)
{
    const QString resolvedPath = resolveProjectPath(path, m_projectFilePath);
    if (m_sourceAssetsRoot != resolvedPath)
    {
        m_sourceAssetsRoot = resolvedPath;
        m_hasUnsavedChanges = true;
    }
}

QString ProjectManager::editableAssetsRoot() const
{
    return m_editableAssetsRoot;
}

void ProjectManager::setEditableAssetsRoot(const QString& path)
{
    const QString resolvedPath = resolveProjectPath(path, m_projectFilePath);
    if (m_editableAssetsRoot != resolvedPath)
    {
        m_editableAssetsRoot = resolvedPath;
        m_hasUnsavedChanges = true;
    }
}

QString ProjectManager::activeResourcePackId() const
{
    return m_activeResourcePackId;
}

void ProjectManager::setActiveResourcePackId(const QString& id)
{
    const QString normalizedId = id.trimmed();
    if (m_activeResourcePackId != normalizedId)
    {
        m_activeResourcePackId = normalizedId;
        m_activeResourcePackEntryKey.clear();
        m_hasUnsavedChanges = true;
    }
}

QString ProjectManager::activeResourcePackEntryKey() const
{
    return m_activeResourcePackEntryKey;
}

void ProjectManager::setActiveResourcePackEntryKey(
    const QString& entryKey)
{
    const QString normalizedEntryKey = entryKey.trimmed();
    if (m_activeResourcePackEntryKey != normalizedEntryKey)
    {
        m_activeResourcePackEntryKey = normalizedEntryKey;
        m_hasUnsavedChanges = true;
    }
}

LegacyImageMigrationPolicy ProjectManager::assetMigrationPolicy() const
{
    return m_assetMigrationPolicy;
}

void ProjectManager::setAssetMigrationPolicy(
    const LegacyImageMigrationPolicy& policy)
{
    if (m_assetMigrationPolicy != policy)
    {
        m_assetMigrationPolicy = policy;
        m_hasUnsavedChanges = true;
    }
}

QStringList ProjectManager::recentFiles() const
{
    return m_recentFiles;
}

void ProjectManager::addRecentFile(const QString& fileName)
{
    if (!m_recentFiles.isEmpty() && m_recentFiles.first() == fileName)
        return;

    m_recentFiles.removeAll(fileName);
    m_recentFiles.prepend(fileName);
    while (m_recentFiles.size() > maxRecentFiles)
    {
        m_recentFiles.removeLast();
    }
    persistLocalProjectState(captureState());
}

void ProjectManager::clearRecentFiles()
{
    if (m_recentFiles.isEmpty())
        return;

    m_recentFiles.clear();
    persistLocalProjectState(captureState());
}

QString ProjectManager::theme() const
{
    return m_theme;
}

void ProjectManager::setTheme(const QString& theme)
{
    if (m_theme != theme)
    {
        m_theme = theme;
        persistLocalProjectState(captureState());
    }
}

QRect ProjectManager::windowGeometry() const
{
    return m_windowGeometry;
}

void ProjectManager::setWindowGeometry(const QRect& geometry)
{
    if (!m_hasWindowPlacement || m_windowGeometry != geometry)
    {
        m_windowGeometry = geometry;
        m_hasWindowPlacement = true;
        persistLocalProjectState(captureState());
    }
}

WindowDisplayMode ProjectManager::windowMode() const
{
    return m_windowMode;
}

void ProjectManager::setWindowMode(WindowDisplayMode mode)
{
    if (!m_hasWindowPlacement || m_windowMode != mode)
    {
        m_windowMode = mode;
        m_hasWindowPlacement = true;
        persistLocalProjectState(captureState());
    }
}

void ProjectManager::setWindowPlacement(
    const QRect& normalGeometry, WindowDisplayMode mode)
{
    if (!m_hasWindowPlacement || m_windowGeometry != normalGeometry ||
        m_windowMode != mode)
    {
        m_windowGeometry = normalGeometry;
        m_windowMode = mode;
        m_hasWindowPlacement = true;
        persistLocalProjectState(captureState());
    }
}

bool ProjectManager::hasWindowPlacement() const
{
    return m_hasWindowPlacement;
}

QByteArray ProjectManager::windowState() const
{
    return m_windowState;
}

void ProjectManager::setWindowState(const QByteArray& state)
{
    if (m_windowState != state)
    {
        m_windowState = state;
        persistLocalProjectState(captureState());
    }
}

ProjectDocumentSessionState ProjectManager::documentSession() const
{
    return m_documentSession;
}

void ProjectManager::setDocumentSession(
    const ProjectDocumentSessionState& session)
{
    ProjectDocumentSessionState candidate = session;
    if (candidate.preservedFields.isEmpty())
        candidate.preservedFields = m_documentSession.preservedFields;

    for (ProjectSessionWindowState& candidateWindow : candidate.windows)
    {
        if (!candidateWindow.preservedFields.isEmpty())
            continue;
        const QString candidateKey =
            projectSessionPathKey(candidateWindow.primaryPath);
        for (const ProjectSessionWindowState& existingWindow :
             m_documentSession.windows)
        {
            if (candidateWindow.type == existingWindow.type &&
                candidateKey ==
                    projectSessionPathKey(existingWindow.primaryPath))
            {
                candidateWindow.preservedFields =
                    existingWindow.preservedFields;
                break;
            }
        }
    }

    bool repaired = false;
    const ProjectDocumentSessionState normalized =
        normalizedProjectDocumentSession(candidate, repaired);
    if (!(m_documentSession == normalized) ||
        m_documentSessionNeedsRepair || repaired)
    {
        m_documentSession = normalized;
        m_documentSessionNeedsRepair = false;
        persistLocalProjectState(captureState());
    }
}

bool ProjectManager::documentSessionNeedsRepair() const
{
    return m_documentSessionNeedsRepair;
}

ProjectManager::ProjectState ProjectManager::captureState() const
{
    ProjectState state;
    state.projectFilePath = m_projectFilePath;
    state.sourceAssetsRoot = m_sourceAssetsRoot;
    state.editableAssetsRoot = m_editableAssetsRoot;
    state.activeResourcePackId = m_activeResourcePackId;
    state.activeResourcePackEntryKey =
        m_activeResourcePackEntryKey;
    state.assetMigrationPolicy = m_assetMigrationPolicy;
    state.recentFiles = m_recentFiles;
    state.theme = m_theme;
    state.windowGeometry = m_windowGeometry;
    state.windowMode = m_windowMode;
    state.hasWindowPlacement = m_hasWindowPlacement;
    state.windowState = m_windowState;
    state.documentSession = m_documentSession;
    state.documentSessionNeedsRepair = m_documentSessionNeedsRepair;
    state.runtimeConfiguration = m_runtimeConfiguration;
    state.runtimeConfigurationNeedsSave =
        m_runtimeConfigurationNeedsSave;
    state.preservedRoot = m_preservedRoot;
    state.isOpen = m_isOpen;
    state.hasUnsavedChanges = m_hasUnsavedChanges;
    return state;
}

void ProjectManager::applyState(const ProjectState& state)
{
    m_projectFilePath = state.projectFilePath;
    m_sourceAssetsRoot = state.sourceAssetsRoot;
    m_editableAssetsRoot = state.editableAssetsRoot;
    m_activeResourcePackId = state.activeResourcePackId;
    m_activeResourcePackEntryKey =
        state.activeResourcePackEntryKey;
    m_assetMigrationPolicy = state.assetMigrationPolicy;
    m_recentFiles = state.recentFiles;
    m_theme = state.theme;
    m_windowGeometry = state.windowGeometry;
    m_windowMode = state.windowMode;
    m_hasWindowPlacement = state.hasWindowPlacement;
    m_windowState = state.windowState;
    m_documentSession = state.documentSession;
    m_documentSessionNeedsRepair = state.documentSessionNeedsRepair;
    m_runtimeConfiguration = state.runtimeConfiguration;
    m_runtimeConfigurationNeedsSave =
        state.runtimeConfigurationNeedsSave;
    m_preservedRoot = state.preservedRoot;
    m_isOpen = state.isOpen;
    m_hasUnsavedChanges = state.hasUnsavedChanges;
}

bool ProjectManager::loadFromJson(const QJsonObject& root,
                                  const QString& projectFilePath,
                                  ProjectState& state) const
{
    const bool isLegacySchema = !root.contains(QStringLiteral("schemaVersion"));
    const bool containsLegacyLocalState =
        root.contains(QStringLiteral("theme")) ||
        root.contains(QStringLiteral("windowPlacement")) ||
        root.contains(QStringLiteral("windowGeometry")) ||
        root.contains(QStringLiteral("windowState")) ||
        root.contains(QStringLiteral("recentFiles")) ||
        root.contains(QStringLiteral("documentSession"));
    bool legacyLocalStateNeedsRepair = false;
    int schemaVersion = 0;
    if (!isLegacySchema &&
        (!readJsonInteger(root, QStringLiteral("schemaVersion"), schemaVersion) ||
         schemaVersion != currentSchemaVersion))
    {
        return false;
    }

    const QString sourceAssetsRootKey = QStringLiteral("sourceAssetsRoot");
    const QString editableAssetsRootKey = QStringLiteral("editableAssetsRoot");
    const QString legacyAssetsPathKey = QStringLiteral("assetsPath");
    const QString resourceContextKey = QStringLiteral("resourceContext");
    const QString activeResourcePackIdKey =
        QStringLiteral("activeResourcePackId");
    const QString activeResourcePackEntryKey =
        QStringLiteral("activeResourcePackEntryKey");
    if (!isLegacySchema && root.contains(sourceAssetsRootKey) &&
        !root.value(sourceAssetsRootKey).isString())
    {
        return false;
    }
    if (!isLegacySchema && root.contains(editableAssetsRootKey) &&
        !root.value(editableAssetsRootKey).isString())
    {
        return false;
    }
    if (isLegacySchema && root.contains(legacyAssetsPathKey) &&
        !root.value(legacyAssetsPathKey).isString())
    {
        return false;
    }
    if (root.contains(resourceContextKey) &&
        !root.value(resourceContextKey).isObject())
    {
        return false;
    }
    const QJsonObject resourceContext =
        root.value(resourceContextKey).toObject();
    if (resourceContext.contains(activeResourcePackIdKey) &&
        !resourceContext.value(activeResourcePackIdKey).isString())
    {
        return false;
    }
    if (resourceContext.contains(activeResourcePackEntryKey) &&
        !resourceContext.value(activeResourcePackEntryKey).isString())
    {
        return false;
    }
    const QString storedSourceAssetsRoot = isLegacySchema
        ? QString() : root.value(sourceAssetsRootKey).toString();
    const QString storedEditableAssetsRoot = isLegacySchema
        ? root.value(legacyAssetsPathKey).toString()
        : root.value(editableAssetsRootKey).toString();
    state.sourceAssetsRoot = resolveProjectPath(
        storedSourceAssetsRoot, projectFilePath);
    state.editableAssetsRoot = resolveProjectPath(
        storedEditableAssetsRoot, projectFilePath);
    state.activeResourcePackId = isLegacySchema
        ? QString()
        : resourceContext.value(activeResourcePackIdKey).toString().trimmed();
    state.activeResourcePackEntryKey = isLegacySchema
        ? QString()
        : resourceContext.value(
            activeResourcePackEntryKey).toString().trimmed();
    state.theme = QStringLiteral("dark");
    if (root.contains(QStringLiteral("theme")))
    {
        if (root.value(QStringLiteral("theme")).isString() &&
            !root.value(QStringLiteral("theme")).toString().isEmpty())
        {
            state.theme =
                root.value(QStringLiteral("theme")).toString();
        }
        else
        {
            legacyLocalStateNeedsRepair = true;
        }
    }
    state.preservedRoot = root;

    bool assetMigrationNeedsRepair = false;
    if (!readAssetMigrationPolicy(root, state.assetMigrationPolicy,
                                  assetMigrationNeedsRepair))
    {
        return false;
    }

    state.hasWindowPlacement = false;
    if (root.contains(QStringLiteral("windowPlacement")))
    {
        const QJsonValue placementValue = root.value(QStringLiteral("windowPlacement"));
        if (placementValue.isObject())
        {
            const QJsonObject placement = placementValue.toObject();
            int version = 0;
            bool recognizedMode = false;
            QRect normalGeometry;
            const WindowDisplayMode mode = WindowPlacementPolicy::modeFromString(
                placement.value(QStringLiteral("mode")).toString(), &recognizedMode);
            if (readJsonInteger(placement, QStringLiteral("version"), version) &&
                version == WindowPlacementPolicy::schemaVersion &&
                readJsonGeometry(
                    placement.value(QStringLiteral("normalGeometry")), normalGeometry) &&
                recognizedMode)
            {
                state.windowGeometry = normalGeometry;
                state.windowMode = mode;
                state.hasWindowPlacement = true;
            }
            else
            {
                legacyLocalStateNeedsRepair = true;
            }
        }
        else
        {
            legacyLocalStateNeedsRepair = true;
        }
    }
    else if (root.contains(QStringLiteral("windowGeometry")))
    {
        // Legacy schema: the raw rectangle was always interpreted as a normal
        // window and had no explicit maximized/fullscreen state.
        QRect legacyGeometry;
        if (readJsonGeometry(root.value(QStringLiteral("windowGeometry")),
                             legacyGeometry))
        {
            state.windowGeometry = legacyGeometry;
            state.windowMode = WindowDisplayMode::Normal;
            state.hasWindowPlacement = true;
        }
        else
        {
            legacyLocalStateNeedsRepair = true;
        }
    }

    state.windowState.clear();
    if (root.contains(QStringLiteral("windowState")))
    {
        if (!root.value(QStringLiteral("windowState")).isString())
        {
            legacyLocalStateNeedsRepair = true;
        }
        else
        {
            const QByteArray encodedWindowState =
                root.value(QStringLiteral("windowState"))
                    .toString().toUtf8();
            const QByteArray::FromBase64Result decodedWindowState =
                QByteArray::fromBase64Encoding(
                    encodedWindowState,
                    QByteArray::AbortOnBase64DecodingErrors);
            if (!decodedWindowState)
            {
                legacyLocalStateNeedsRepair = true;
            }
            else
            {
                state.windowState = decodedWindowState.decoded;
            }
        }
    }

    state.recentFiles.clear();
    if (root.contains(QStringLiteral("recentFiles")) &&
        !root.value(QStringLiteral("recentFiles")).isArray())
    {
        legacyLocalStateNeedsRepair = true;
    }
    else
    {
        const QJsonArray recentArray =
            root.value(QStringLiteral("recentFiles")).toArray();
        for (const QJsonValue& value : recentArray)
        {
            if (!value.isString())
            {
                legacyLocalStateNeedsRepair = true;
                continue;
            }

            const QString recentFile = value.toString();
            if (recentFile.isEmpty() ||
                state.recentFiles.contains(recentFile) ||
                state.recentFiles.size() >= maxRecentFiles)
            {
                legacyLocalStateNeedsRepair = true;
                continue;
            }
            state.recentFiles.append(recentFile);
        }
    }

    state.documentSessionNeedsRepair = false;
    state.documentSession = readProjectDocumentSession(
        root, state.documentSessionNeedsRepair);
    bool runtimeConfigurationNeedsRepair = false;
    if (!readProjectRuntimeConfiguration(
            root,
            state.runtimeConfiguration,
            runtimeConfigurationNeedsRepair))
    {
        return false;
    }
    state.runtimeConfigurationNeedsSave =
        runtimeConfigurationNeedsRepair;
    state.hasUnsavedChanges =
        isLegacySchema || assetMigrationNeedsRepair ||
        containsLegacyLocalState ||
        legacyLocalStateNeedsRepair ||
        runtimeConfigurationNeedsRepair;
    return true;
}

bool ProjectManager::readProjectFile(const QString& projectFilePath,
                                     ProjectState& state) const
{
    QFile file(projectFilePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray data = file.readAll();
    if (file.error() != QFile::NoError)
        return false;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;

    ProjectState loadedState;
    loadedState.projectFilePath = projectFilePath;
    loadedState.isOpen = true;
    if (!loadFromJson(document.object(), projectFilePath, loadedState))
        return false;

    bool localStateFound = false;
    bool localStateRepaired = false;
    loadLocalProjectState(
        projectFilePath,
        loadedState,
        localStateFound,
        localStateRepaired);
    state = loadedState;
    return true;
}

bool ProjectManager::loadLocalProjectState(
    const QString& projectFilePath,
    ProjectState& state,
    bool& found,
    bool& repaired) const
{
    found = false;
    repaired = false;
    QSettings settings = EditorSettings::create();
    settings.beginGroup(localProjectStateGroup(projectFilePath));
    if (!settings.contains(QStringLiteral("schemaVersion")))
    {
        settings.endGroup();
        return settings.status() == QSettings::NoError;
    }

    found = true;
    bool versionOk = false;
    const int version = settings.value(
        QStringLiteral("schemaVersion")).toInt(&versionOk);
    if (!versionOk || version != localProjectStateSchemaVersion)
    {
        repaired = true;
        settings.endGroup();
        return settings.status() == QSettings::NoError;
    }

    state.theme = settings.value(
        QStringLiteral("theme"), QStringLiteral("dark")).toString();
    if (state.theme.isEmpty())
    {
        state.theme = QStringLiteral("dark");
        repaired = true;
    }

    state.hasWindowPlacement = settings.value(
        QStringLiteral("windowPlacement/hasValue"), false).toBool();
    if (state.hasWindowPlacement)
    {
        const QRect geometry = settings.value(
            QStringLiteral("windowPlacement/normalGeometry")).toRect();
        bool recognizedMode = false;
        const WindowDisplayMode mode =
            WindowPlacementPolicy::modeFromString(
                settings.value(
                    QStringLiteral("windowPlacement/mode"))
                    .toString(),
                &recognizedMode);
        if (geometry.width() <= 0 || geometry.height() <= 0 ||
            !recognizedMode)
        {
            state.windowGeometry = QRect();
            state.windowMode = WindowDisplayMode::Normal;
            state.hasWindowPlacement = false;
            repaired = true;
        }
        else
        {
            state.windowGeometry = geometry;
            state.windowMode = mode;
        }
    }
    else
    {
        state.windowGeometry = QRect();
        state.windowMode = WindowDisplayMode::Normal;
    }

    state.windowState = settings.value(
        QStringLiteral("windowState")).toByteArray();
    state.recentFiles.clear();
    const QStringList storedRecentFiles = settings.value(
        QStringLiteral("recentFiles")).toStringList();
    for (const QString& recentFile : storedRecentFiles)
    {
        if (recentFile.isEmpty())
        {
            repaired = true;
            continue;
        }
        if (state.recentFiles.contains(recentFile))
        {
            repaired = true;
            continue;
        }
        if (state.recentFiles.size() >= maxRecentFiles)
        {
            repaired = true;
            break;
        }
        state.recentFiles.append(recentFile);
    }

    state.documentSession = ProjectDocumentSessionState();
    state.documentSessionNeedsRepair = false;
    const QByteArray sessionData = settings.value(
        QStringLiteral("documentSession")).toByteArray();
    if (!sessionData.isEmpty())
    {
        QJsonParseError parseError;
        const QJsonDocument sessionDocument =
            QJsonDocument::fromJson(sessionData, &parseError);
        if (parseError.error != QJsonParseError::NoError ||
            !sessionDocument.isObject())
        {
            state.documentSessionNeedsRepair = true;
            repaired = true;
        }
        else
        {
            state.documentSession = readProjectDocumentSession(
                sessionDocument.object(),
                state.documentSessionNeedsRepair);
            repaired = repaired ||
                state.documentSessionNeedsRepair;
        }
    }

    settings.endGroup();
    if (settings.status() != QSettings::NoError)
        repaired = true;
    return settings.status() == QSettings::NoError;
}

bool ProjectManager::persistLocalProjectState(
    const ProjectState& state) const
{
    if (state.projectFilePath.isEmpty())
    {
        qWarning().noquote()
            << QStringLiteral(
                   "Cannot save project-local user state: project path is empty");
        return false;
    }

    QSettings settings = EditorSettings::create();
    settings.beginGroup(
        localProjectStateGroup(state.projectFilePath));
    settings.setValue(
        QStringLiteral("schemaVersion"),
        localProjectStateSchemaVersion);
    settings.setValue(
        QStringLiteral("projectPath"),
        normalizedProjectPath(state.projectFilePath));
    settings.setValue(QStringLiteral("theme"), state.theme);
    settings.setValue(
        QStringLiteral("windowPlacement/hasValue"),
        state.hasWindowPlacement);
    if (state.hasWindowPlacement)
    {
        settings.setValue(
            QStringLiteral("windowPlacement/normalGeometry"),
            state.windowGeometry);
        settings.setValue(
            QStringLiteral("windowPlacement/mode"),
            WindowPlacementPolicy::modeToString(
                state.windowMode));
    }
    else
    {
        settings.remove(
            QStringLiteral("windowPlacement/normalGeometry"));
        settings.remove(
            QStringLiteral("windowPlacement/mode"));
    }
    settings.setValue(
        QStringLiteral("windowState"), state.windowState);
    settings.setValue(
        QStringLiteral("recentFiles"), state.recentFiles);

    QJsonObject sessionRoot;
    sessionRoot[QStringLiteral("documentSession")] =
        projectDocumentSessionToJson(state.documentSession);
    settings.setValue(
        QStringLiteral("documentSession"),
        QJsonDocument(sessionRoot).toJson(
            QJsonDocument::Compact));
    settings.endGroup();
    settings.sync();
    if (settings.status() != QSettings::NoError)
    {
        qWarning().noquote()
            << QStringLiteral(
                   "Cannot save project-local user state to %1 "
                   "(QSettings status %2); shared project data was not affected")
                   .arg(
                       settings.fileName(),
                       QString::number(
                           static_cast<int>(
                               settings.status())));
        return false;
    }
    return true;
}

QJsonObject ProjectManager::toJson(const ProjectState& state,
                                   const QString& projectFilePath) const
{
    QJsonObject root = state.preservedRoot;
    root["schemaVersion"] = currentSchemaVersion;
    root["sourceAssetsRoot"] = pathRelativeToProject(
        state.sourceAssetsRoot, projectFilePath);
    root["editableAssetsRoot"] = pathRelativeToProject(
        state.editableAssetsRoot, projectFilePath);
    QJsonObject resourceContext = root.value(
        QStringLiteral("resourceContext")).toObject();
    resourceContext["activeResourcePackId"] = state.activeResourcePackId;
    if (state.activeResourcePackEntryKey.isEmpty())
    {
        resourceContext.remove(
            QStringLiteral("activeResourcePackEntryKey"));
    }
    else
    {
        resourceContext["activeResourcePackEntryKey"] =
            state.activeResourcePackEntryKey;
    }
    root["resourceContext"] = resourceContext;
    root.remove(QStringLiteral("assetsPath"));
    root.remove(QStringLiteral("theme"));
    root.remove(QStringLiteral("windowPlacement"));
    root.remove(QStringLiteral("windowGeometry"));
    root.remove(QStringLiteral("windowState"));
    root.remove(QStringLiteral("recentFiles"));
    root.remove(QStringLiteral("documentSession"));

    QJsonObject assetMigration = root.value(
        QStringLiteral("assetMigration")).toObject();
    QJsonObject legacyImages = assetMigration.value(
        QStringLiteral("legacyImages")).toObject();
    QJsonObject modes = legacyImages.value(QStringLiteral("modes")).toObject();
    for (const LegacyImageCategoryDefinition& item :
         LegacyImageMigrationPolicy::definitions())
    {
        modes[item.id] = LegacyImageMigrationPolicy::modeId(
            state.assetMigrationPolicy.mode(item.category));
    }
    legacyImages["modes"] = modes;
    legacyImages["cropTransparent"] =
        state.assetMigrationPolicy.cropTransparent();
    assetMigration["version"] = assetMigrationSchemaVersion;
    assetMigration["legacyImages"] = legacyImages;
    root["assetMigration"] = assetMigration;

    root["runtimeConfiguration"] =
        projectRuntimeConfigurationToJson(state.runtimeConfiguration);

    return root;
}

bool ProjectManager::writeProjectFile(const QString& projectFilePath,
                                      const ProjectState& state) const
{
    if (projectFilePath.isEmpty())
        return false;

    AuthoringMutationGate::Lease mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(projectFilePath);
    if (!mutationLease)
        return false;

    const QJsonDocument document(toJson(state, projectFilePath));
    const QByteArray projectData = document.toJson(QJsonDocument::Indented);

    QSaveFile file(projectFilePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(projectData) != projectData.size())
        return false;
    return file.commit();
}
