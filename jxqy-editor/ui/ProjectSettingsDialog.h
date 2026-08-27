#pragma once

#include <QDialog>
#include <QList>

#include "../core/GameProfile.h"
#include "../core/ProjectManager.h"

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;

class ProjectSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode
    {
        CreateProject,
        EditProject
    };

    ProjectSettingsDialog(
        Mode mode,
        const QString& projectFilePath,
        const ProjectResourceConfiguration& initialConfiguration,
        QWidget* parent = nullptr);

    ProjectResourceConfiguration configuration() const;

private:
    QString resolveInputPath(const QString& input) const;
    void browseSourceAssetsRoot();
    void browseEditableAssetsRoot();
    void rebuildResourcePacks();
    void updateResourceContext();
    void updateValidation();
    void acceptSettings();
    QString selectedResourcePackId() const;
    QString selectedResourcePackEntryKey() const;

    QString m_projectFilePath;
    QString m_requestedResourcePackId;
    QString m_requestedResourcePackEntryKey;
    QList<ResourcePackInfo> m_availablePacks;
    QString m_resourceConfigurationError;

    QLineEdit* m_sourceAssetsRootEdit = nullptr;
    QLineEdit* m_editableAssetsRootEdit = nullptr;
    QComboBox* m_activeResourcePackCombo = nullptr;
    QLabel* m_gameContextLabel = nullptr;
    QLabel* m_errorLabel = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
};
