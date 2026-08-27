#pragma once

#include "../core/ProjectRuntimeConfiguration.h"

#include <QDialog>
#include <QSet>
#include <QStringList>

class QDialogButtonBox;
class QEvent;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QWidget;

class ProjectSceneDialog final : public QDialog
{
    Q_OBJECT

public:
    ProjectSceneDialog(
        const ProjectScene& initialScene,
        const QString& activeContentRoot,
        const QSet<QString>& reservedSceneIds,
        QWidget* parent = nullptr);

    ProjectScene scene() const;

public slots:
    void accept() override;

protected:
    void changeEvent(QEvent* event) override;

private:
    void addVariable();
    void removeSelectedVariables();
    void browseResource(
        QLineEdit* targetEdit,
        const QStringList& preferredDirectories,
        const QString& title,
        const QString& filter);
    bool collectIntegerVariables(
        QMap<QString, int>& variables,
        QString& errorMessage,
        int& errorRow) const;
    void clearValidationError();
    void showValidationError(
        const QString& message,
        QWidget* focusWidget = nullptr);
    void focusValidationField(
        const ProjectRuntimeConfigurationValidationResult& validationResult);
    QString validationErrorText(
        const ProjectRuntimeConfigurationValidationResult& validationResult) const;
    QString pathFieldName(const QString& fieldName) const;
    void retranslateUi();

    ProjectScene m_scene;
    QString m_activeContentRoot;
    QSet<QString> m_reservedSceneIds;

    QGroupBox* m_sceneGroup = nullptr;
    QLabel* m_idLabel = nullptr;
    QLabel* m_nameLabel = nullptr;
    QLabel* m_mapLabel = nullptr;
    QLabel* m_npcLabel = nullptr;
    QLabel* m_objectLabel = nullptr;
    QLabel* m_entryScriptLabel = nullptr;
    QLabel* m_playerPositionLabel = nullptr;
    QLabel* m_playerXLabel = nullptr;
    QLabel* m_playerYLabel = nullptr;
    QLineEdit* m_idEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_mapEdit = nullptr;
    QLineEdit* m_npcEdit = nullptr;
    QLineEdit* m_objectEdit = nullptr;
    QLineEdit* m_entryScriptEdit = nullptr;
    QPushButton* m_mapBrowseButton = nullptr;
    QPushButton* m_npcBrowseButton = nullptr;
    QPushButton* m_objectBrowseButton = nullptr;
    QPushButton* m_entryScriptBrowseButton = nullptr;
    QSpinBox* m_playerXSpinBox = nullptr;
    QSpinBox* m_playerYSpinBox = nullptr;

    QGroupBox* m_variablesGroup = nullptr;
    QTableWidget* m_variablesTable = nullptr;
    QPushButton* m_addVariableButton = nullptr;
    QPushButton* m_removeVariableButton = nullptr;
    QLabel* m_errorLabel = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
};
