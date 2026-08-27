#include "ProjectSceneListModel.h"

#include <QList>

namespace
{
bool normalizeCandidate(ProjectRuntimeConfiguration& configuration)
{
    bool repaired = false;
    return normalizeProjectRuntimeConfiguration(
        configuration, repaired, nullptr);
}
}

ProjectSceneListModel::ProjectSceneListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ProjectSceneListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;

    return m_configuration.scenes.size();
}

QVariant ProjectSceneListModel::data(
    const QModelIndex& index,
    int role) const
{
    if (!index.isValid() || index.column() != 0 ||
        !isValidRow(index.row()))
    {
        return QVariant();
    }

    const ProjectScene& scene = m_configuration.scenes.at(index.row());
    const bool isDefault =
        scene.id == m_configuration.defaultSceneId;
    switch (role)
    {
    case Qt::DisplayRole:
        return QStringLiteral("%1%2 — %3")
            .arg(isDefault ? QStringLiteral("★ ") : QString(),
                 scene.name,
                 scene.id);
    case SceneIdRole:
        return scene.id;
    case SceneNameRole:
        return scene.name;
    case DefaultSceneRole:
        return isDefault;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ProjectSceneListModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractListModel::roleNames();
    roles.insert(SceneIdRole, QByteArrayLiteral("sceneId"));
    roles.insert(SceneNameRole, QByteArrayLiteral("sceneName"));
    roles.insert(DefaultSceneRole, QByteArrayLiteral("isDefaultScene"));
    return roles;
}

void ProjectSceneListModel::setConfiguration(
    const ProjectRuntimeConfiguration& configuration)
{
    ProjectRuntimeConfiguration normalizedConfiguration = configuration;
    if (!normalizeCandidate(normalizedConfiguration) ||
        m_configuration == normalizedConfiguration)
    {
        return;
    }

    beginResetModel();
    m_configuration = normalizedConfiguration;
    endResetModel();
}

const ProjectRuntimeConfiguration&
ProjectSceneListModel::configuration() const
{
    return m_configuration;
}

const ProjectScene* ProjectSceneListModel::sceneAt(int row) const
{
    if (!isValidRow(row))
        return nullptr;

    return &m_configuration.scenes.at(row);
}

int ProjectSceneListModel::rowForSceneId(const QString& sceneId) const
{
    for (int row = 0; row < m_configuration.scenes.size(); ++row)
    {
        if (m_configuration.scenes.at(row).id == sceneId)
            return row;
    }
    return -1;
}

bool ProjectSceneListModel::addScene(
    const ProjectScene& scene,
    int* insertedRow)
{
    return insertScene(
        m_configuration.scenes.size(),
        scene,
        insertedRow);
}

bool ProjectSceneListModel::insertScene(
    int row,
    const ProjectScene& scene,
    int* insertedRow)
{
    if (insertedRow)
        *insertedRow = -1;
    if (row < 0 || row > m_configuration.scenes.size() ||
        scene.id.trimmed().isEmpty() ||
        containsSceneId(scene.id))
    {
        return false;
    }

    ProjectRuntimeConfiguration candidate = m_configuration;
    candidate.scenes.insert(row, scene);
    if (candidate.scenes.size() == 1)
        candidate.defaultSceneId = scene.id;
    if (!normalizeCandidate(candidate))
        return false;

    beginInsertRows(QModelIndex(), row, row);
    m_configuration = candidate;
    endInsertRows();

    if (insertedRow)
        *insertedRow = row;
    return true;
}

bool ProjectSceneListModel::replaceScene(
    int row,
    const ProjectScene& scene)
{
    if (!isValidRow(row) || scene.id.trimmed().isEmpty())
        return false;

    const ProjectScene& currentScene = m_configuration.scenes.at(row);
    const int existingRow = rowForSceneId(scene.id);
    if (existingRow >= 0 && existingRow != row)
        return false;

    ProjectRuntimeConfiguration candidate = m_configuration;
    candidate.scenes[row] = scene;
    if (candidate.defaultSceneId == currentScene.id)
        candidate.defaultSceneId = scene.id;
    if (!normalizeCandidate(candidate))
        return false;

    if (candidate == m_configuration)
        return true;

    m_configuration = candidate;
    emitSceneDataChanged(row);
    return true;
}

bool ProjectSceneListModel::duplicateScene(
    int row,
    const QString& suggestedId,
    const QString& suggestedName,
    int* insertedRow)
{
    if (insertedRow)
        *insertedRow = -1;
    if (!isValidRow(row))
        return false;

    const QString duplicateId = uniqueSceneId(suggestedId);
    const QString duplicateName = uniqueSceneName(suggestedName);
    if (duplicateId.isEmpty() || duplicateName.isEmpty())
        return false;

    ProjectScene duplicate = m_configuration.scenes.at(row);
    duplicate.id = duplicateId;
    duplicate.name = duplicateName;

    ProjectRuntimeConfiguration candidate = m_configuration;
    const int duplicateRow = row + 1;
    candidate.scenes.insert(duplicateRow, duplicate);
    if (!normalizeCandidate(candidate))
        return false;

    beginInsertRows(QModelIndex(), duplicateRow, duplicateRow);
    m_configuration = candidate;
    endInsertRows();

    if (insertedRow)
        *insertedRow = duplicateRow;
    return true;
}

bool ProjectSceneListModel::removeScene(int row)
{
    if (!isValidRow(row))
        return false;

    ProjectRuntimeConfiguration candidate = m_configuration;
    const bool removingDefault =
        candidate.scenes.at(row).id == candidate.defaultSceneId;
    candidate.scenes.removeAt(row);
    if (removingDefault)
    {
        candidate.defaultSceneId = candidate.scenes.isEmpty()
            ? QString()
            : candidate.scenes.front().id;
    }
    if (!normalizeCandidate(candidate))
        return false;

    beginRemoveRows(QModelIndex(), row, row);
    m_configuration = candidate;
    endRemoveRows();

    if (removingDefault && !m_configuration.scenes.isEmpty())
        emitSceneDataChanged(0);
    return true;
}

bool ProjectSceneListModel::setDefaultScene(int row)
{
    if (!isValidRow(row))
        return false;

    const QString newDefaultSceneId =
        m_configuration.scenes.at(row).id;
    if (m_configuration.defaultSceneId == newDefaultSceneId)
        return true;

    ProjectRuntimeConfiguration candidate = m_configuration;
    candidate.defaultSceneId = newDefaultSceneId;
    if (!normalizeCandidate(candidate))
        return false;

    const int previousDefaultRow =
        rowForSceneId(m_configuration.defaultSceneId);
    m_configuration = candidate;
    if (previousDefaultRow >= 0)
        emitSceneDataChanged(previousDefaultRow);
    emitSceneDataChanged(row);
    return true;
}

QString ProjectSceneListModel::uniqueSceneId(
    const QString& suggestedId) const
{
    if (suggestedId.trimmed().isEmpty())
        return QString();
    if (!containsSceneId(suggestedId))
        return suggestedId;

    for (int suffix = 2; ; ++suffix)
    {
        const QString candidate =
            suggestedId + QStringLiteral("-") + QString::number(suffix);
        if (!containsSceneId(candidate))
            return candidate;
    }
}

QString ProjectSceneListModel::uniqueSceneName(
    const QString& suggestedName) const
{
    if (suggestedName.trimmed().isEmpty())
        return QString();
    if (!containsSceneName(suggestedName))
        return suggestedName;

    for (int suffix = 2; ; ++suffix)
    {
        const QString candidate =
            suggestedName + QStringLiteral(" ") + QString::number(suffix);
        if (!containsSceneName(candidate))
            return candidate;
    }
}

bool ProjectSceneListModel::isValidRow(int row) const
{
    return row >= 0 && row < m_configuration.scenes.size();
}

bool ProjectSceneListModel::containsSceneId(
    const QString& sceneId) const
{
    return rowForSceneId(sceneId) >= 0;
}

bool ProjectSceneListModel::containsSceneName(
    const QString& sceneName) const
{
    for (const ProjectScene& scene : m_configuration.scenes)
    {
        if (scene.name == sceneName)
            return true;
    }
    return false;
}

void ProjectSceneListModel::emitSceneDataChanged(int row)
{
    if (!isValidRow(row))
        return;

    const QModelIndex changedIndex = index(row, 0);
    emit dataChanged(
        changedIndex,
        changedIndex,
        QList<int>{
            Qt::DisplayRole,
            SceneIdRole,
            SceneNameRole,
            DefaultSceneRole});
}
