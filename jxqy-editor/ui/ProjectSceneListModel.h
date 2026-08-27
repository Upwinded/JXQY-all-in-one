#pragma once

#include "../core/ProjectRuntimeConfiguration.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QVariant>

class ProjectSceneListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        SceneIdRole = Qt::UserRole + 1,
        SceneNameRole,
        DefaultSceneRole
    };
    Q_ENUM(Role)

    explicit ProjectSceneListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(
        const QModelIndex& index,
        int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setConfiguration(
        const ProjectRuntimeConfiguration& configuration);
    const ProjectRuntimeConfiguration& configuration() const;

    const ProjectScene* sceneAt(int row) const;
    int rowForSceneId(const QString& sceneId) const;

    bool insertScene(
        int row,
        const ProjectScene& scene,
        int* insertedRow = nullptr);
    bool addScene(const ProjectScene& scene, int* insertedRow = nullptr);
    bool replaceScene(int row, const ProjectScene& scene);
    bool duplicateScene(
        int row,
        const QString& suggestedId,
        const QString& suggestedName,
        int* insertedRow = nullptr);
    bool removeScene(int row);
    bool setDefaultScene(int row);

    QString uniqueSceneId(const QString& suggestedId) const;
    QString uniqueSceneName(const QString& suggestedName) const;

private:
    bool isValidRow(int row) const;
    bool containsSceneId(const QString& sceneId) const;
    bool containsSceneName(const QString& sceneName) const;
    void emitSceneDataChanged(int row);

    ProjectRuntimeConfiguration m_configuration;
};
