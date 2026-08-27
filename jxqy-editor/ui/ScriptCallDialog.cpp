#include "ScriptCallDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <limits>

ScriptCallDialog::ScriptCallDialog(QWidget* parent)
    : QDialog(parent)
    , m_definitions(ScriptCallTemplate::definitions())
{
    setObjectName(QStringLiteral("scriptCallDialog"));
    setWindowTitle(tr("插入 Script API 调用"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    auto* boundaryLabel = new QLabel(tr(
        "仅生成当前运行时确认的五项固定调用；字符串按 Lua 字面量插入，不会作为表达式执行。"),
        this);
    boundaryLabel->setObjectName(QStringLiteral("scriptCallBoundaryLabel"));
    boundaryLabel->setWordWrap(true);
    layout->addWidget(boundaryLabel);

    m_apiComboBox = new QComboBox(this);
    m_apiComboBox->setObjectName(QStringLiteral("scriptCallApiComboBox"));
    for (const ScriptCallDefinition& definition : m_definitions)
    {
        m_apiComboBox->addItem(
            tr("%1 — %2").arg(definition.displayName, definition.description),
            definition.runtimeName);
    }
    layout->addWidget(m_apiComboBox);

    auto* formContainer = new QWidget(this);
    formContainer->setObjectName(QStringLiteral("scriptCallArgumentContainer"));
    m_argumentForm = new QFormLayout(formContainer);
    m_argumentForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addWidget(formContainer);

    auto* previewLabel = new QLabel(tr("生成预览"), this);
    layout->addWidget(previewLabel);
    m_previewEdit = new QPlainTextEdit(this);
    m_previewEdit->setObjectName(QStringLiteral("scriptCallPreviewEdit"));
    m_previewEdit->setReadOnly(true);
    m_previewEdit->setMaximumBlockCount(1);
    m_previewEdit->setFixedHeight(64);
    layout->addWidget(m_previewEdit);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("scriptCallErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #d24646;"));
    layout->addWidget(m_errorLabel);

    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->setObjectName(QStringLiteral("scriptCallButtonBox"));
    m_buttonBox->button(QDialogButtonBox::Ok)->setText(tr("插入"));
    layout->addWidget(m_buttonBox);

    connect(m_apiComboBox, &QComboBox::currentIndexChanged,
        this, &ScriptCallDialog::rebuildArgumentForm);
    connect(m_buttonBox, &QDialogButtonBox::rejected,
        this, &QDialog::reject);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this,
        [this]()
        {
            updatePreview();
            if (m_previewCall.isEmpty())
                return;
            m_generatedCall = m_previewCall;
            accept();
        });

    rebuildArgumentForm();
    resize(640, 420);
}

QString ScriptCallDialog::generatedCall() const
{
    return m_generatedCall;
}

void ScriptCallDialog::rebuildArgumentForm()
{
    while (QLayoutItem* item = m_argumentForm->takeAt(0))
    {
        if (QWidget* widget = item->widget())
            delete widget;
        delete item;
    }
    m_argumentEditors.clear();

    ScriptCallDefinition definition;
    if (!ScriptCallTemplate::findDefinition(
            m_apiComboBox->currentData().toString(), definition))
    {
        updatePreview();
        return;
    }

    for (const ScriptCallArgumentDefinition& argument : definition.arguments)
    {
        QWidget* editor = nullptr;
        if (argument.type == ScriptCallArgumentType::String)
        {
            auto* lineEdit = new QLineEdit(this);
            lineEdit->setPlaceholderText(argument.description);
            lineEdit->setText(argument.defaultValue);
            connect(lineEdit, &QLineEdit::textChanged,
                this, &ScriptCallDialog::updatePreview);
            editor = lineEdit;
        }
        else
        {
            auto* spinBox = new QSpinBox(this);
            spinBox->setRange(std::numeric_limits<int>::min(),
                              std::numeric_limits<int>::max());
            spinBox->setValue(argument.defaultValue.toInt());
            spinBox->setToolTip(argument.description);
            connect(spinBox, &QSpinBox::valueChanged,
                this, &ScriptCallDialog::updatePreview);
            editor = spinBox;
        }
        editor->setObjectName(
            QStringLiteral("scriptCallArgument_") + argument.id);
        editor->setToolTip(argument.description);
        m_argumentEditors.insert(argument.id, editor);
        m_argumentForm->addRow(argument.label + QStringLiteral(":"), editor);
    }
    updatePreview();
}

QMap<QString, QString> ScriptCallDialog::argumentValues() const
{
    QMap<QString, QString> values;
    for (auto iterator = m_argumentEditors.cbegin();
         iterator != m_argumentEditors.cend(); ++iterator)
    {
        if (auto* lineEdit = qobject_cast<QLineEdit*>(iterator.value()))
            values.insert(iterator.key(), lineEdit->text());
        else if (auto* spinBox = qobject_cast<QSpinBox*>(iterator.value()))
            values.insert(iterator.key(), QString::number(spinBox->value()));
    }
    return values;
}

void ScriptCallDialog::updatePreview()
{
    const ScriptCallBuildResult result = ScriptCallTemplate::build(
        m_apiComboBox->currentData().toString(), argumentValues());
    m_previewCall = result.success ? result.call : QString();
    m_previewEdit->setPlainText(m_previewCall);
    m_errorLabel->setText(result.error);
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(result.success);
}
