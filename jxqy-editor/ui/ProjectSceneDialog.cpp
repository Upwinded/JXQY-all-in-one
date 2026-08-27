#include "ProjectSceneDialog.h"

#include "FilePickerHelper.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <limits>

namespace
{
QWidget* createPathRow(
    QLineEdit*& edit,
    QPushButton*& browseButton,
    const QString& editObjectName,
    const QString& buttonObjectName,
    QWidget* parent)
{
    auto* row = new QWidget(parent);
    row->setObjectName(editObjectName + QStringLiteral("Row"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    edit = new QLineEdit(row);
    edit->setObjectName(editObjectName);
    edit->setClearButtonEnabled(true);
    layout->addWidget(edit, 1);

    browseButton = new QPushButton(row);
    browseButton->setObjectName(buttonObjectName);
    layout->addWidget(browseButton);
    return row;
}
}

ProjectSceneDialog::ProjectSceneDialog(
    const ProjectScene& initialScene,
    const QString& activeContentRoot,
    const QSet<QString>& reservedSceneIds,
    QWidget* parent)
    : QDialog(parent)
    , m_scene(initialScene)
    , m_activeContentRoot(activeContentRoot.isEmpty()
          ? QString()
          : QDir::cleanPath(
                QFileInfo(QDir::fromNativeSeparators(activeContentRoot))
                    .absoluteFilePath()))
    , m_reservedSceneIds(reservedSceneIds)
{
    setObjectName(QStringLiteral("projectSceneDialog"));
    setModal(true);
    setMinimumSize(760, 620);

    auto* mainLayout = new QVBoxLayout(this);

    m_sceneGroup = new QGroupBox(this);
    m_sceneGroup->setObjectName(QStringLiteral("projectSceneFieldsGroup"));
    auto* formLayout = new QFormLayout(m_sceneGroup);
    formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_idEdit = new QLineEdit(initialScene.id, m_sceneGroup);
    m_idEdit->setObjectName(QStringLiteral("projectSceneIdEdit"));
    m_idEdit->setClearButtonEnabled(true);
    m_idLabel = new QLabel(m_sceneGroup);
    m_idLabel->setObjectName(QStringLiteral("projectSceneIdLabel"));
    formLayout->addRow(m_idLabel, m_idEdit);

    m_nameEdit = new QLineEdit(initialScene.name, m_sceneGroup);
    m_nameEdit->setObjectName(QStringLiteral("projectSceneNameEdit"));
    m_nameEdit->setClearButtonEnabled(true);
    m_nameLabel = new QLabel(m_sceneGroup);
    m_nameLabel->setObjectName(QStringLiteral("projectSceneNameLabel"));
    formLayout->addRow(m_nameLabel, m_nameEdit);

    QWidget* mapRow = createPathRow(
        m_mapEdit,
        m_mapBrowseButton,
        QStringLiteral("projectSceneMapEdit"),
        QStringLiteral("projectSceneMapBrowseButton"),
        m_sceneGroup);
    m_mapEdit->setText(initialScene.mapPath);
    m_mapLabel = new QLabel(m_sceneGroup);
    m_mapLabel->setObjectName(QStringLiteral("projectSceneMapLabel"));
    formLayout->addRow(m_mapLabel, mapRow);

    QWidget* npcRow = createPathRow(
        m_npcEdit,
        m_npcBrowseButton,
        QStringLiteral("projectSceneNpcEdit"),
        QStringLiteral("projectSceneNpcBrowseButton"),
        m_sceneGroup);
    m_npcEdit->setText(initialScene.npcPath);
    m_npcLabel = new QLabel(m_sceneGroup);
    m_npcLabel->setObjectName(QStringLiteral("projectSceneNpcLabel"));
    formLayout->addRow(m_npcLabel, npcRow);

    QWidget* objectRow = createPathRow(
        m_objectEdit,
        m_objectBrowseButton,
        QStringLiteral("projectSceneObjectEdit"),
        QStringLiteral("projectSceneObjectBrowseButton"),
        m_sceneGroup);
    m_objectEdit->setText(initialScene.objectPath);
    m_objectLabel = new QLabel(m_sceneGroup);
    m_objectLabel->setObjectName(QStringLiteral("projectSceneObjectLabel"));
    formLayout->addRow(m_objectLabel, objectRow);

    QWidget* entryScriptRow = createPathRow(
        m_entryScriptEdit,
        m_entryScriptBrowseButton,
        QStringLiteral("projectSceneEntryScriptEdit"),
        QStringLiteral("projectSceneEntryScriptBrowseButton"),
        m_sceneGroup);
    m_entryScriptEdit->setText(initialScene.entryScriptPath);
    m_entryScriptLabel = new QLabel(m_sceneGroup);
    m_entryScriptLabel->setObjectName(
        QStringLiteral("projectSceneEntryScriptLabel"));
    formLayout->addRow(m_entryScriptLabel, entryScriptRow);

    auto* positionRow = new QWidget(m_sceneGroup);
    positionRow->setObjectName(QStringLiteral("projectScenePlayerPositionRow"));
    auto* positionLayout = new QHBoxLayout(positionRow);
    positionLayout->setContentsMargins(0, 0, 0, 0);
    positionLayout->setSpacing(6);
    m_playerXLabel = new QLabel(positionRow);
    m_playerXLabel->setObjectName(
        QStringLiteral("projectScenePlayerXLabel"));
    positionLayout->addWidget(m_playerXLabel);
    m_playerXSpinBox = new QSpinBox(positionRow);
    m_playerXSpinBox->setObjectName(
        QStringLiteral("projectScenePlayerXSpinBox"));
    m_playerXSpinBox->setRange(
        std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max());
    m_playerXSpinBox->setValue(initialScene.playerPosition.x());
    positionLayout->addWidget(m_playerXSpinBox, 1);
    m_playerYLabel = new QLabel(positionRow);
    m_playerYLabel->setObjectName(
        QStringLiteral("projectScenePlayerYLabel"));
    positionLayout->addWidget(m_playerYLabel);
    m_playerYSpinBox = new QSpinBox(positionRow);
    m_playerYSpinBox->setObjectName(
        QStringLiteral("projectScenePlayerYSpinBox"));
    m_playerYSpinBox->setRange(
        std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max());
    m_playerYSpinBox->setValue(initialScene.playerPosition.y());
    positionLayout->addWidget(m_playerYSpinBox, 1);
    m_playerPositionLabel = new QLabel(m_sceneGroup);
    m_playerPositionLabel->setObjectName(
        QStringLiteral("projectScenePlayerPositionLabel"));
    formLayout->addRow(m_playerPositionLabel, positionRow);

    mainLayout->addWidget(m_sceneGroup);

    m_variablesGroup = new QGroupBox(this);
    m_variablesGroup->setObjectName(
        QStringLiteral("projectSceneIntegerVariablesGroup"));
    auto* variablesLayout = new QVBoxLayout(m_variablesGroup);
    m_variablesTable = new QTableWidget(m_variablesGroup);
    m_variablesTable->setObjectName(
        QStringLiteral("projectSceneIntegerVariablesTable"));
    m_variablesTable->setColumnCount(2);
    m_variablesTable->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    m_variablesTable->setSelectionMode(
        QAbstractItemView::ExtendedSelection);
    m_variablesTable->setAlternatingRowColors(true);
    m_variablesTable->verticalHeader()->setVisible(false);
    m_variablesTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    m_variablesTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    m_variablesTable->setRowCount(initialScene.integerVariables.size());
    int variableRow = 0;
    for (auto variable = initialScene.integerVariables.cbegin();
         variable != initialScene.integerVariables.cend();
         ++variable)
    {
        m_variablesTable->setItem(
            variableRow, 0, new QTableWidgetItem(variable.key()));
        m_variablesTable->setItem(
            variableRow, 1,
            new QTableWidgetItem(QString::number(variable.value())));
        ++variableRow;
    }
    variablesLayout->addWidget(m_variablesTable, 1);

    auto* variableButtonsLayout = new QHBoxLayout;
    m_addVariableButton = new QPushButton(m_variablesGroup);
    m_addVariableButton->setObjectName(
        QStringLiteral("projectSceneAddVariableButton"));
    variableButtonsLayout->addWidget(m_addVariableButton);
    m_removeVariableButton = new QPushButton(m_variablesGroup);
    m_removeVariableButton->setObjectName(
        QStringLiteral("projectSceneRemoveVariableButton"));
    variableButtonsLayout->addWidget(m_removeVariableButton);
    variableButtonsLayout->addStretch();
    variablesLayout->addLayout(variableButtonsLayout);
    mainLayout->addWidget(m_variablesGroup, 1);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(
        QStringLiteral("projectSceneValidationErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_errorLabel->setVisible(false);
    mainLayout->addWidget(m_errorLabel);

    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->setObjectName(QStringLiteral("projectSceneButtonBox"));
    if (QPushButton* okButton =
            m_buttonBox->button(QDialogButtonBox::Ok))
    {
        okButton->setObjectName(
            QStringLiteral("projectSceneAcceptButton"));
    }
    if (QPushButton* cancelButton =
            m_buttonBox->button(QDialogButtonBox::Cancel))
    {
        cancelButton->setObjectName(
            QStringLiteral("projectSceneCancelButton"));
    }
    mainLayout->addWidget(m_buttonBox);

    connect(m_mapBrowseButton, &QPushButton::clicked, this,
        [this]()
        {
            browseResource(
                m_mapEdit,
                {QStringLiteral("map")},
                tr("选择地图文件"),
                tr("地图文件 (*.map);;所有文件 (*)"));
        });
    connect(m_npcBrowseButton, &QPushButton::clicked, this,
        [this]()
        {
            browseResource(
                m_npcEdit,
                {QStringLiteral("ini/save")},
                tr("选择 NPC 列表"),
                tr("NPC 列表 (*.npc);;所有文件 (*)"));
        });
    connect(m_objectBrowseButton, &QPushButton::clicked, this,
        [this]()
        {
            browseResource(
                m_objectEdit,
                {QStringLiteral("ini/save")},
                tr("选择 OBJ 列表"),
                tr("OBJ 列表 (*.obj);;所有文件 (*)"));
        });
    connect(m_entryScriptBrowseButton, &QPushButton::clicked, this,
        [this]()
        {
            browseResource(
                m_entryScriptEdit,
                {QStringLiteral("script")},
                tr("选择入口脚本"),
                tr("脚本文件 (*.lua *.txt);;所有文件 (*)"));
        });
    connect(m_addVariableButton, &QPushButton::clicked,
        this, &ProjectSceneDialog::addVariable);
    connect(m_removeVariableButton, &QPushButton::clicked,
        this, &ProjectSceneDialog::removeSelectedVariables);
    connect(m_buttonBox, &QDialogButtonBox::accepted,
        this, &ProjectSceneDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected,
        this, &QDialog::reject);

    const QList<QLineEdit*> lineEdits = {
        m_idEdit,
        m_nameEdit,
        m_mapEdit,
        m_npcEdit,
        m_objectEdit,
        m_entryScriptEdit
    };
    for (QLineEdit* edit : lineEdits)
    {
        connect(edit, &QLineEdit::textChanged,
            this, &ProjectSceneDialog::clearValidationError);
    }
    connect(m_playerXSpinBox, &QSpinBox::valueChanged,
        this, &ProjectSceneDialog::clearValidationError);
    connect(m_playerYSpinBox, &QSpinBox::valueChanged,
        this, &ProjectSceneDialog::clearValidationError);
    connect(m_variablesTable, &QTableWidget::itemChanged,
        this, &ProjectSceneDialog::clearValidationError);

    retranslateUi();
}

ProjectScene ProjectSceneDialog::scene() const
{
    return m_scene;
}

void ProjectSceneDialog::accept()
{
    clearValidationError();

    ProjectScene candidate = m_scene;
    candidate.id = m_idEdit->text();
    candidate.name = m_nameEdit->text();
    candidate.mapPath = m_mapEdit->text();
    candidate.npcPath = m_npcEdit->text();
    candidate.objectPath = m_objectEdit->text();
    candidate.entryScriptPath = m_entryScriptEdit->text();
    candidate.playerPosition = QPoint(
        m_playerXSpinBox->value(), m_playerYSpinBox->value());

    if (m_reservedSceneIds.contains(candidate.id))
    {
        showValidationError(
            tr("场景 ID“%1”已存在，请输入项目内唯一的 ID。")
                .arg(candidate.id),
            m_idEdit);
        return;
    }

    QString variableError;
    int variableErrorRow = -1;
    if (!collectIntegerVariables(
            candidate.integerVariables,
            variableError,
            variableErrorRow))
    {
        showValidationError(variableError, m_variablesTable);
        if (variableErrorRow >= 0)
        {
            m_variablesTable->setCurrentCell(variableErrorRow, 0);
            m_variablesTable->scrollToItem(
                m_variablesTable->item(variableErrorRow, 0));
        }
        return;
    }

    ProjectRuntimeConfiguration configuration;
    configuration.defaultSceneId = candidate.id;
    configuration.scenes.append(candidate);
    bool repaired = false;
    ProjectRuntimeConfigurationValidationResult validationResult;
    if (!normalizeProjectRuntimeConfiguration(
            configuration, repaired, &validationResult))
    {
        showValidationError(
            validationErrorText(validationResult));
        focusValidationField(validationResult);
        return;
    }

    m_scene = configuration.scenes.front();
    QDialog::accept();
}

void ProjectSceneDialog::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void ProjectSceneDialog::addVariable()
{
    const int row = m_variablesTable->rowCount();
    m_variablesTable->insertRow(row);
    auto* nameItem = new QTableWidgetItem;
    auto* valueItem = new QTableWidgetItem(QStringLiteral("0"));
    m_variablesTable->setItem(row, 0, nameItem);
    m_variablesTable->setItem(row, 1, valueItem);
    m_variablesTable->setCurrentItem(nameItem);
    m_variablesTable->editItem(nameItem);
    clearValidationError();
}

void ProjectSceneDialog::removeSelectedVariables()
{
    QSet<int> selectedRows;
    const QModelIndexList selectedIndexes =
        m_variablesTable->selectionModel()->selectedIndexes();
    for (const QModelIndex& index : selectedIndexes)
        selectedRows.insert(index.row());
    if (selectedRows.isEmpty() && m_variablesTable->currentRow() >= 0)
        selectedRows.insert(m_variablesTable->currentRow());

    QList<int> rows = selectedRows.values();
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows)
        m_variablesTable->removeRow(row);
    clearValidationError();
}

void ProjectSceneDialog::browseResource(
    QLineEdit* targetEdit,
    const QStringList& preferredDirectories,
    const QString& title,
    const QString& filter)
{
    if (m_activeContentRoot.isEmpty() ||
        !QFileInfo(m_activeContentRoot).isDir())
    {
        QMessageBox::warning(
            this,
            tr("活动内容根不可用"),
            tr("当前项目没有可读取的活动内容根，无法选择资源文件。"));
        return;
    }

    QString startDirectory = m_activeContentRoot;
    for (const QString& relativeDirectory : preferredDirectories)
    {
        const QString candidate =
            QDir(m_activeContentRoot).filePath(relativeDirectory);
        if (QFileInfo(candidate).isDir())
        {
            startDirectory = candidate;
            break;
        }
    }

    const QString selectedFilePath = QFileDialog::getOpenFileName(
        this, title, startDirectory, filter, nullptr,
        QFileDialog::DontResolveSymlinks);
    if (selectedFilePath.isEmpty())
        return;

    QString resourceReference;
    if (!QFileInfo(selectedFilePath).isFile() ||
        !FilePickerHelper::makeResourceReference(
            m_activeContentRoot,
            m_activeContentRoot,
            selectedFilePath,
            resourceReference))
    {
        QMessageBox::warning(
            this,
            tr("文件范围限制"),
            tr("所选路径不是可读取的文件、不在当前活动内容根的逻辑路径范围内，或无法生成安全的相对资源路径。\n\n"
               "活动内容根：%1\n文件路径：%2")
                .arg(m_activeContentRoot, selectedFilePath));
        return;
    }

    resourceReference.replace('\\', '/');
    targetEdit->setText(resourceReference);
}

bool ProjectSceneDialog::collectIntegerVariables(
    QMap<QString, int>& variables,
    QString& errorMessage,
    int& errorRow) const
{
    variables.clear();
    errorMessage.clear();
    errorRow = -1;
    QSet<QString> variableNames;
    for (int row = 0; row < m_variablesTable->rowCount(); ++row)
    {
        const QTableWidgetItem* nameItem =
            m_variablesTable->item(row, 0);
        const QTableWidgetItem* valueItem =
            m_variablesTable->item(row, 1);
        const QString name = nameItem ? nameItem->text() : QString();
        const QString valueText = valueItem
            ? valueItem->text().trimmed() : QString();
        if (name.trimmed().isEmpty())
        {
            errorMessage = tr("第 %1 行的变量名不能为空。").arg(row + 1);
            errorRow = row;
            return false;
        }
        if (variableNames.contains(name))
        {
            errorMessage = tr("第 %1 行的变量名“%2”重复。")
                .arg(row + 1)
                .arg(name);
            errorRow = row;
            return false;
        }

        bool valueOk = false;
        const int value = valueText.toInt(&valueOk);
        if (!valueOk)
        {
            errorMessage = tr(
                "第 %1 行变量“%2”的值必须是 32 位整数。")
                .arg(row + 1)
                .arg(name);
            errorRow = row;
            return false;
        }
        variableNames.insert(name);
        variables.insert(name, value);
    }
    return true;
}

void ProjectSceneDialog::clearValidationError()
{
    m_errorLabel->clear();
    m_errorLabel->setVisible(false);
}

void ProjectSceneDialog::showValidationError(
    const QString& message,
    QWidget* focusWidget)
{
    m_errorLabel->setText(message);
    m_errorLabel->setVisible(true);
    if (focusWidget)
    {
        focusWidget->setFocus(Qt::OtherFocusReason);
        if (auto* edit = qobject_cast<QLineEdit*>(focusWidget))
            edit->selectAll();
    }
}

void ProjectSceneDialog::focusValidationField(
    const ProjectRuntimeConfigurationValidationResult& validationResult)
{
    QWidget* target = nullptr;
    if (validationResult.fieldName == QStringLiteral("id"))
        target = m_idEdit;
    else if (validationResult.fieldName == QStringLiteral("name"))
        target = m_nameEdit;
    else if (validationResult.fieldName == QStringLiteral("map"))
        target = m_mapEdit;
    else if (validationResult.fieldName == QStringLiteral("npc"))
        target = m_npcEdit;
    else if (validationResult.fieldName == QStringLiteral("object"))
        target = m_objectEdit;
    else if (validationResult.fieldName == QStringLiteral("entryScript"))
        target = m_entryScriptEdit;
    else if (validationResult.fieldName ==
             QStringLiteral("playerPosition"))
    {
        target = m_playerXSpinBox;
    }
    else if (validationResult.fieldName ==
             QStringLiteral("integerVariables"))
    {
        target = m_variablesTable;
    }

    if (target)
    {
        target->setFocus(Qt::OtherFocusReason);
        if (auto* edit = qobject_cast<QLineEdit*>(target))
            edit->selectAll();
    }
}

QString ProjectSceneDialog::validationErrorText(
    const ProjectRuntimeConfigurationValidationResult& validationResult) const
{
    switch (validationResult.error)
    {
    case ProjectRuntimeConfigurationError::MissingSceneId:
        return tr("场景 ID 不能为空。");
    case ProjectRuntimeConfigurationError::MissingSceneName:
        return tr("场景名称不能为空。");
    case ProjectRuntimeConfigurationError::MissingSceneMap:
        return tr("地图路径不能为空。");
    case ProjectRuntimeConfigurationError::UnsafeResourcePath:
        return tr("%1必须是活动内容根内的安全相对路径，并使用合法的资源文件名。")
            .arg(pathFieldName(validationResult.fieldName));
    case ProjectRuntimeConfigurationError::InvalidPlayerPosition:
        return tr("玩家坐标必须由两个 32 位整数组成。");
    case ProjectRuntimeConfigurationError::InvalidIntegerVariables:
        return tr("整数变量的名称和值无效。");
    case ProjectRuntimeConfigurationError::InvalidVariableName:
        return tr("整数变量名不能为空。");
    case ProjectRuntimeConfigurationError::DuplicateSceneId:
        return tr("场景 ID“%1”重复。").arg(validationResult.value);
    case ProjectRuntimeConfigurationError::InvalidDefaultSceneId:
        return tr("场景 ID 无法作为默认场景引用。");
    case ProjectRuntimeConfigurationError::InvalidFieldType:
        return tr("%1的值类型无效。")
            .arg(pathFieldName(validationResult.fieldName));
    case ProjectRuntimeConfigurationError::None:
    case ProjectRuntimeConfigurationError::InvalidConfigurationObject:
    case ProjectRuntimeConfigurationError::MissingVersion:
    case ProjectRuntimeConfigurationError::InvalidVersion:
    case ProjectRuntimeConfigurationError::UnsupportedVersion:
    case ProjectRuntimeConfigurationError::InvalidScenesArray:
    case ProjectRuntimeConfigurationError::InvalidSceneObject:
        break;
    }
    return tr("场景配置无效，请检查所有字段后重试。");
}

QString ProjectSceneDialog::pathFieldName(const QString& fieldName) const
{
    if (fieldName == QStringLiteral("id"))
        return tr("场景 ID");
    if (fieldName == QStringLiteral("name"))
        return tr("场景名称");
    if (fieldName == QStringLiteral("map"))
        return tr("地图路径");
    if (fieldName == QStringLiteral("npc"))
        return tr("NPC 列表路径");
    if (fieldName == QStringLiteral("object"))
        return tr("OBJ 列表路径");
    if (fieldName == QStringLiteral("entryScript"))
        return tr("入口脚本路径");
    if (fieldName == QStringLiteral("playerPosition"))
        return tr("玩家坐标");
    if (fieldName == QStringLiteral("integerVariables"))
        return tr("整数变量");
    return tr("字段");
}

void ProjectSceneDialog::retranslateUi()
{
    setWindowTitle(tr("场景设置"));
    m_sceneGroup->setTitle(tr("场景"));
    m_idLabel->setText(tr("ID："));
    m_nameLabel->setText(tr("名称："));
    m_mapLabel->setText(tr("地图："));
    m_npcLabel->setText(tr("NPC 列表（可选）："));
    m_objectLabel->setText(tr("OBJ 列表（可选）："));
    m_entryScriptLabel->setText(tr("入口脚本（可选）："));
    m_playerPositionLabel->setText(tr("玩家坐标："));
    m_playerXLabel->setText(tr("X"));
    m_playerYLabel->setText(tr("Y"));

    m_idEdit->setPlaceholderText(tr("项目内唯一，区分大小写"));
    m_nameEdit->setPlaceholderText(tr("面向作者显示的场景名称"));
    m_mapEdit->setPlaceholderText(tr("例如 map/中都.map"));
    m_npcEdit->setPlaceholderText(tr("例如 ini/save/中都.npc"));
    m_objectEdit->setPlaceholderText(tr("例如 ini/save/中都.obj"));
    m_entryScriptEdit->setPlaceholderText(
        tr("例如 script/map/中都/入口.txt"));

    const QList<QPushButton*> browseButtons = {
        m_mapBrowseButton,
        m_npcBrowseButton,
        m_objectBrowseButton,
        m_entryScriptBrowseButton
    };
    for (QPushButton* button : browseButtons)
    {
        button->setText(tr("浏览..."));
        button->setToolTip(tr("从当前活动内容根内选择文件"));
    }

    m_variablesGroup->setTitle(tr("整数变量"));
    m_variablesTable->setHorizontalHeaderLabels(
        {tr("变量名"), tr("32 位整数值")});
    m_addVariableButton->setText(tr("添加变量"));
    m_removeVariableButton->setText(tr("删除所选变量"));
    if (QPushButton* okButton =
            m_buttonBox->button(QDialogButtonBox::Ok))
    {
        okButton->setText(tr("确定"));
    }
    if (QPushButton* cancelButton =
            m_buttonBox->button(QDialogButtonBox::Cancel))
    {
        cancelButton->setText(tr("取消"));
    }
}
