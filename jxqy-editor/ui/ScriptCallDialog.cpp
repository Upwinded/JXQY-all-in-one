#include "ScriptCallDialog.h"
#include "FilePickerHelper.h"
#include "../core/ContentAuthoring.h"
#include "../core/EditorAssetPath.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

ScriptCallDialog::ScriptCallDialog(QWidget* parent, const QString& assetsBasePath, const QString& scriptPath)
    : QDialog(parent), m_assetsBasePath(assetsBasePath), m_scriptPath(scriptPath)
{
    setObjectName(QStringLiteral("scriptCallDialog"));
    setWindowTitle(tr("游戏指令列表"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    auto* boundaryLabel = new QLabel(tr(
        "直接选择指令查看用途和示例。有参数表单的指令可填写后插入；其它指令先插入名称与括号，再按说明填写参数。"),
        this);
    boundaryLabel->setObjectName(QStringLiteral("scriptCallBoundaryLabel"));
    boundaryLabel->setWordWrap(true);
    layout->addWidget(boundaryLabel);

    QString configurationError;
    m_apis = buildScriptApiList(&configurationError);
    auto* configurationLabel = new QLabel(configurationError, this);
    configurationLabel->setObjectName(QStringLiteral("scriptCommandConfigurationErrorLabel"));
    configurationLabel->setWordWrap(true);
    configurationLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    configurationLabel->setVisible(!configurationError.isEmpty());
    layout->addWidget(configurationLabel);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName("scriptCommandSearchEdit");
    m_searchEdit->setPlaceholderText(tr("按指令名称或中文用途搜索；留空浏览全部指令"));
    layout->addWidget(m_searchEdit);

    m_apiComboBox = new QComboBox(this);
    m_apiComboBox->setObjectName(QStringLiteral("scriptCallApiComboBox"));
    m_apiComboBox->setMaxVisibleItems(15);
    layout->addWidget(m_apiComboBox);

    std::sort(m_apis.begin(), m_apis.end(), [](const ApiInfo& a, const ApiInfo& b)
    {
        return a.signature.compare(b.signature, Qt::CaseInsensitive) < 0;
    });
    m_helpEdit = new QPlainTextEdit(this);
    m_helpEdit->setObjectName("scriptCommandHelpEdit");
    m_helpEdit->setReadOnly(true);
    m_helpEdit->setMaximumHeight(180);
    layout->addWidget(m_helpEdit);

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
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ScriptCallDialog::filterCommands);
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

    filterCommands({});
    resize(760, 620);
}

void ScriptCallDialog::filterCommands(const QString& query)
{
    const QString previous = m_apiComboBox->currentData().toString();
    const QString search = query.trimmed();
    {
        const QSignalBlocker blocker(m_apiComboBox);
        m_apiComboBox->clear();
        for (const auto& api : m_apis)
        {
            const QString name = api.signature.section('(', 0, 0).trimmed().toLower();
            const QString searchableText = name + " " + api.description + " " + api.tooltip;
            if (!search.isEmpty() && !searchableText.contains(search, Qt::CaseInsensitive))
                continue;
            m_apiComboBox->addItem(tr("%1 — %2").arg(name, api.description), name);
        }
        int index = m_apiComboBox->findData(previous.isEmpty() ? "assign" : previous);
        if (index < 0 && m_apiComboBox->count() > 0)
            index = 0;
        m_apiComboBox->setCurrentIndex(index);
    }
    rebuildArgumentForm();
}

QString ScriptCallDialog::generatedCall() const
{
    return m_generatedCall;
}

void ScriptCallDialog::rebuildArgumentForm()
{
    while (m_argumentForm->count() > 0)
    {
        QLayoutItem* item = m_argumentForm->takeAt(0);
        if (QWidget* widget = item->widget())
            delete widget;
        delete item;
    }
    m_argumentEditors.clear();
    m_helpEdit->clear();
    const QString name = m_apiComboBox->currentData().toString();
    for (const auto& api : m_apis)
    {
        if (api.signature.section('(', 0, 0).trimmed().compare(name, Qt::CaseInsensitive) == 0)
        {
            m_helpEdit->setPlainText(api.tooltip);
            break;
        }
    }

    ScriptCallDefinition definition;
    if (!currentDefinition(definition))
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
        QWidget* field = editor;
        if (!argument.resourceDirectory.isEmpty())
        {
            auto* wrapper = new QWidget(this);
            auto* row = new QHBoxLayout(wrapper);
            row->setContentsMargins(0, 0, 0, 0);
            row->addWidget(editor, 1);
            auto* choose = new QPushButton(tr("选择…"), wrapper);
            choose->setObjectName("scriptCallResource_" + argument.id);
            choose->setEnabled(!m_assetsBasePath.isEmpty());
            row->addWidget(choose);
            connect(choose, &QPushButton::clicked, this, [this, editor, argument]()
            { chooseArgumentResource(qobject_cast<QLineEdit*>(editor), argument); });
            field = wrapper;
        }
        m_argumentForm->addRow(argument.label + QStringLiteral(":"), field);
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
    ScriptCallDefinition definition;
    if (!currentDefinition(definition))
    {
        m_previewCall = m_apiComboBox->currentIndex() < 0
            ? QString() : m_apiComboBox->currentData().toString() + "()";
        m_previewEdit->setPlainText(m_previewCall);
        m_errorLabel->setStyleSheet({});
        m_errorLabel->setText(m_previewCall.isEmpty()
            ? tr("没有匹配的指令，请更换搜索内容。")
            : tr("插入后，按上方说明在括号中填写需要的参数。"));
        m_buttonBox->button(QDialogButtonBox::Ok)->setText(tr("插入名称与括号"));
        m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!m_previewCall.isEmpty());
        return;
    }
    const ScriptCallBuildResult result = ScriptCallTemplate::build(definition, argumentValues());
    m_previewCall = result.success ? result.call : QString();
    m_previewEdit->setPlainText(m_previewCall);
    m_errorLabel->setText(result.error);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #d24646;"));
    m_buttonBox->button(QDialogButtonBox::Ok)->setText(tr("插入调用"));
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(result.success);
}

bool ScriptCallDialog::currentDefinition(ScriptCallDefinition& definition) const
{
    const QString name = m_apiComboBox->currentData().toString();
    for (const auto& api : m_apis)
        if (api.hasParameterForm && api.parameterForm.runtimeName == name)
        {
            definition = api.parameterForm;
            return true;
        }
    return ScriptCallTemplate::findDefinition(name, definition);
}

void ScriptCallDialog::chooseArgumentResource(QLineEdit* editor, const ScriptCallArgumentDefinition& argument)
{
    if (!editor) return;
    QStringList folders{argument.resourceDirectory};
    QString map;
    if (argument.resourceDirectory == "script")
    {
        const QString relative = QDir(m_assetsBasePath).relativeFilePath(ContentAuthoring::projectPath(m_assetsBasePath, m_scriptPath));
        if (relative.startsWith("script/map/")) map = relative.section('/', 2, 2);
        folders = {"script/goods", "script/common"};
        if (!map.isEmpty()) folders.prepend("script/map/" + map);
    }
    QString resourcePath;
    const QString file = FilePickerHelper::chooseResourceFile(this, m_assetsBasePath, folders, argument.resourceFilters, {}, &resourcePath);
    if (file.isEmpty()) return;
    if (argument.resourceDirectory == "script")
    {
        const QString reference = QFileInfo(file).fileName();
        if (EditorAssetPath::logicalComparisonKey(ContentAuthoring::bindingScript(m_assetsBasePath, map, reference)) !=
            EditorAssetPath::logicalComparisonKey(file))
        {
            QMessageBox::warning(this, tr("无法使用该脚本"), tr("这个同名脚本被当前查找范围内的其它文件覆盖，请选择实际生效的脚本。"));
            return;
        }
        editor->setText(reference);
    }
    else
    {
        editor->setText(resourcePath.mid(argument.resourceDirectory.size() + 1).replace('/', '\\'));
    }
}
