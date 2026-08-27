#pragma once

#include "../core/ScriptCallTemplate.h"

#include <QDialog>
#include <QMap>

class QComboBox;
class QDialogButtonBox;
class QFormLayout;
class QLabel;
class QPlainTextEdit;
class QWidget;

class ScriptCallDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ScriptCallDialog(QWidget* parent = nullptr);

    QString generatedCall() const;

private:
    void rebuildArgumentForm();
    void updatePreview();
    QMap<QString, QString> argumentValues() const;

    QList<ScriptCallDefinition> m_definitions;
    QComboBox* m_apiComboBox = nullptr;
    QFormLayout* m_argumentForm = nullptr;
    QPlainTextEdit* m_previewEdit = nullptr;
    QLabel* m_errorLabel = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
    QMap<QString, QWidget*> m_argumentEditors;
    QString m_previewCall;
    QString m_generatedCall;
};
