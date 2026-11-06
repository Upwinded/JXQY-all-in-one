#pragma once

#include "../core/ScriptCallTemplate.h"
#include "ScriptApiList.h"

#include <QDialog>
#include <QMap>

class QComboBox;
class QDialogButtonBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QWidget;

class ScriptCallDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ScriptCallDialog(QWidget* parent = nullptr,
        const QString& assetsBasePath = {}, const QString& scriptPath = {});

    QString generatedCall() const;

private:
    void rebuildArgumentForm();
    void filterCommands(const QString& query);
    void updatePreview();
    bool currentDefinition(ScriptCallDefinition& definition) const;
    void chooseArgumentResource(QLineEdit* editor, const ScriptCallArgumentDefinition& argument);
    QMap<QString, QString> argumentValues() const;

    QVector<ApiInfo> m_apis;
    QLineEdit* m_searchEdit = nullptr;
    QPlainTextEdit* m_helpEdit = nullptr;
    QComboBox* m_apiComboBox = nullptr;
    QFormLayout* m_argumentForm = nullptr;
    QPlainTextEdit* m_previewEdit = nullptr;
    QLabel* m_errorLabel = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
    QMap<QString, QWidget*> m_argumentEditors;
    QString m_previewCall;
    QString m_generatedCall;
    QString m_assetsBasePath;
    QString m_scriptPath;
};
