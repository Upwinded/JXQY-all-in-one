#pragma once

#include "../core/ProjectRuntimeConfiguration.h"

#include <QDialog>
#include <QSet>

class QDialogButtonBox;
class QEvent;
class QLabel;
class QListView;
class QPushButton;

class ProjectSceneListModel;

class ProjectRuntimeConfigurationDialog : public QDialog
{
    Q_OBJECT

public:
    ProjectRuntimeConfigurationDialog(
        const ProjectRuntimeConfiguration& initialConfiguration,
        const QString& activeContentRoot,
        QWidget* parent = nullptr);

    ProjectRuntimeConfiguration configuration() const;

protected:
    void changeEvent(QEvent* event) override;

private:
    int selectedRow() const;
    QSet<QString> reservedSceneIds(int excludedRow = -1) const;
    QString suggestedNewSceneId() const;
    void selectRow(int row);
    void synchronizeConfiguration();
    void createScene();
    void editSelectedScene();
    void copySelectedScene();
    void deleteSelectedScene();
    void setSelectedSceneAsDefault();
    void updateButtonStates();
    void acceptConfiguration();
    void showValidationError(
        const ProjectRuntimeConfigurationValidationResult& result);
    QString validationErrorText(
        const ProjectRuntimeConfigurationValidationResult& result) const;
    void clearError();
    void retranslateUi();

    QString m_activeContentRoot;
    ProjectRuntimeConfiguration m_configuration;
    ProjectRuntimeConfigurationValidationResult m_lastValidationResult;

    QLabel* m_summaryLabel = nullptr;
    QListView* m_sceneListView = nullptr;
    ProjectSceneListModel* m_sceneListModel = nullptr;
    QPushButton* m_createButton = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QPushButton* m_setDefaultButton = nullptr;
    QLabel* m_errorLabel = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
};
