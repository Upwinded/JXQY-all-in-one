#include "ProjectRuntimeConfigurationDialog.h"

#include "ProjectSceneDialog.h"
#include "ProjectSceneListModel.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QModelIndex>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
QWidget* topLevelDialogOwner(QWidget* dialog)
{
    QWidget* const parent = dialog->parentWidget();
    return parent ? parent->window() : dialog;
}
}

ProjectRuntimeConfigurationDialog::ProjectRuntimeConfigurationDialog(
    const ProjectRuntimeConfiguration& initialConfiguration,
    const QString& activeContentRoot,
    QWidget* parent)
    : QDialog(parent)
    , m_activeContentRoot(activeContentRoot)
    , m_configuration(initialConfiguration)
{
    setObjectName(QStringLiteral("projectRuntimeConfigurationDialog"));
    setModal(true);
    resize(820, 520);
    setMinimumSize(680, 420);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName(
        QStringLiteral("projectRuntimeConfigurationSummaryLabel"));
    m_summaryLabel->setWordWrap(true);

    m_sceneListModel = new ProjectSceneListModel(this);
    m_sceneListModel->setObjectName(QStringLiteral("projectSceneListModel"));
    m_sceneListModel->setConfiguration(m_configuration);

    m_sceneListView = new QListView(this);
    m_sceneListView->setObjectName(QStringLiteral("projectSceneListView"));
    m_sceneListView->setModel(m_sceneListModel);
    m_sceneListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sceneListView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sceneListView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sceneListView->setAlternatingRowColors(true);

    m_createButton = new QPushButton(this);
    m_createButton->setObjectName(QStringLiteral("projectSceneCreateButton"));
    m_editButton = new QPushButton(this);
    m_editButton->setObjectName(QStringLiteral("projectSceneEditButton"));
    m_copyButton = new QPushButton(this);
    m_copyButton->setObjectName(QStringLiteral("projectSceneCopyButton"));
    m_deleteButton = new QPushButton(this);
    m_deleteButton->setObjectName(QStringLiteral("projectSceneDeleteButton"));
    m_setDefaultButton = new QPushButton(this);
    m_setDefaultButton->setObjectName(
        QStringLiteral("projectSceneSetDefaultButton"));

    auto* actionLayout = new QVBoxLayout;
    actionLayout->addWidget(m_createButton);
    actionLayout->addWidget(m_editButton);
    actionLayout->addWidget(m_copyButton);
    actionLayout->addWidget(m_deleteButton);
    actionLayout->addSpacing(12);
    actionLayout->addWidget(m_setDefaultButton);
    actionLayout->addStretch();

    auto* sceneLayout = new QHBoxLayout;
    sceneLayout->addWidget(m_sceneListView, 1);
    sceneLayout->addLayout(actionLayout);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(
        QStringLiteral("projectRuntimeConfigurationErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));

    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->setObjectName(
        QStringLiteral("projectRuntimeConfigurationButtonBox"));
    if (QPushButton* applyButton =
            m_buttonBox->button(QDialogButtonBox::Ok))
    {
        applyButton->setObjectName(
            QStringLiteral("projectRuntimeConfigurationApplyButton"));
    }
    if (QPushButton* cancelButton =
            m_buttonBox->button(QDialogButtonBox::Cancel))
    {
        cancelButton->setObjectName(
            QStringLiteral("projectRuntimeConfigurationCancelButton"));
    }

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(m_summaryLabel);
    mainLayout->addLayout(sceneLayout, 1);
    mainLayout->addWidget(m_errorLabel);
    mainLayout->addWidget(m_buttonBox);

    connect(m_createButton, &QPushButton::clicked,
        this, &ProjectRuntimeConfigurationDialog::createScene);
    connect(m_editButton, &QPushButton::clicked,
        this, &ProjectRuntimeConfigurationDialog::editSelectedScene);
    connect(m_copyButton, &QPushButton::clicked,
        this, &ProjectRuntimeConfigurationDialog::copySelectedScene);
    connect(m_deleteButton, &QPushButton::clicked,
        this, &ProjectRuntimeConfigurationDialog::deleteSelectedScene);
    connect(m_setDefaultButton, &QPushButton::clicked,
        this,
        &ProjectRuntimeConfigurationDialog::setSelectedSceneAsDefault);
    connect(m_sceneListView, &QListView::doubleClicked, this,
        [this](const QModelIndex& index)
        {
            if (index.isValid())
                editSelectedScene();
        });
    connect(m_sceneListView->selectionModel(),
        &QItemSelectionModel::selectionChanged, this,
        [this]()
        {
            updateButtonStates();
        });
    connect(m_sceneListView->selectionModel(),
        &QItemSelectionModel::currentChanged, this,
        [this]()
        {
            updateButtonStates();
        });
    connect(m_buttonBox, &QDialogButtonBox::accepted,
        this, &ProjectRuntimeConfigurationDialog::acceptConfiguration);
    connect(m_buttonBox, &QDialogButtonBox::rejected,
        this, &QDialog::reject);

    int initialRow = m_sceneListModel->rowForSceneId(
        m_configuration.defaultSceneId);
    if (initialRow < 0 && m_sceneListModel->rowCount() > 0)
        initialRow = 0;
    selectRow(initialRow);
    retranslateUi();
    updateButtonStates();
}

ProjectRuntimeConfiguration
ProjectRuntimeConfigurationDialog::configuration() const
{
    return m_configuration;
}

void ProjectRuntimeConfigurationDialog::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

int ProjectRuntimeConfigurationDialog::selectedRow() const
{
    const QModelIndex index = m_sceneListView->currentIndex();
    if (!index.isValid() || !m_sceneListModel->sceneAt(index.row()))
        return -1;
    return index.row();
}

QSet<QString> ProjectRuntimeConfigurationDialog::reservedSceneIds(
    int excludedRow) const
{
    QSet<QString> ids;
    const ProjectRuntimeConfiguration& configuration =
        m_sceneListModel->configuration();
    for (int row = 0; row < configuration.scenes.size(); ++row)
    {
        if (row != excludedRow)
            ids.insert(configuration.scenes.at(row).id);
    }
    return ids;
}

QString ProjectRuntimeConfigurationDialog::suggestedNewSceneId() const
{
    const QSet<QString> ids = reservedSceneIds();
    int suffix = 1;
    QString candidate;
    do
    {
        candidate = QStringLiteral("scene-%1").arg(suffix);
        ++suffix;
    }
    while (ids.contains(candidate));
    return candidate;
}

void ProjectRuntimeConfigurationDialog::selectRow(int row)
{
    if (row < 0 || row >= m_sceneListModel->rowCount())
    {
        m_sceneListView->clearSelection();
        m_sceneListView->setCurrentIndex(QModelIndex());
        updateButtonStates();
        return;
    }

    const QModelIndex index = m_sceneListModel->index(row, 0);
    m_sceneListView->selectionModel()->setCurrentIndex(
        index, QItemSelectionModel::ClearAndSelect |
            QItemSelectionModel::Rows);
    m_sceneListView->scrollTo(index);
    updateButtonStates();
}

void ProjectRuntimeConfigurationDialog::synchronizeConfiguration()
{
    m_configuration = m_sceneListModel->configuration();
    clearError();
    updateButtonStates();
}

void ProjectRuntimeConfigurationDialog::createScene()
{
    ProjectScene scene;
    scene.id = suggestedNewSceneId();
    scene.name = tr("新场景");

    ProjectSceneDialog dialog(
        scene, m_activeContentRoot, reservedSceneIds(),
        topLevelDialogOwner(this));
    if (dialog.exec() != QDialog::Accepted)
        return;

    int insertedRow = -1;
    if (!m_sceneListModel->addScene(dialog.scene(), &insertedRow))
    {
        m_errorLabel->setText(
            tr("无法添加场景；请检查场景 ID、必填字段和默认场景引用。"));
        return;
    }

    synchronizeConfiguration();
    selectRow(insertedRow);
}

void ProjectRuntimeConfigurationDialog::editSelectedScene()
{
    const int row = selectedRow();
    const ProjectScene* scene = m_sceneListModel->sceneAt(row);
    if (!scene)
        return;

    ProjectSceneDialog dialog(
        *scene, m_activeContentRoot, reservedSceneIds(row),
        topLevelDialogOwner(this));
    if (dialog.exec() != QDialog::Accepted)
        return;

    if (!m_sceneListModel->replaceScene(row, dialog.scene()))
    {
        m_errorLabel->setText(
            tr("无法更新场景；请检查场景 ID、必填字段和默认场景引用。"));
        return;
    }

    synchronizeConfiguration();
    selectRow(row);
}

void ProjectRuntimeConfigurationDialog::copySelectedScene()
{
    const int row = selectedRow();
    const ProjectScene* sourceScene = m_sceneListModel->sceneAt(row);
    if (!sourceScene)
        return;

    ProjectScene copiedScene = *sourceScene;
    copiedScene.id = m_sceneListModel->uniqueSceneId(
        sourceScene->id + QStringLiteral("-copy"));
    copiedScene.name = m_sceneListModel->uniqueSceneName(
        tr("%1 副本").arg(sourceScene->name));

    ProjectSceneDialog dialog(
        copiedScene, m_activeContentRoot, reservedSceneIds(),
        topLevelDialogOwner(this));
    if (dialog.exec() != QDialog::Accepted)
        return;

    int insertedRow = -1;
    if (!m_sceneListModel->insertScene(
            row + 1, dialog.scene(), &insertedRow))
    {
        m_errorLabel->setText(
            tr("无法复制场景；请检查新场景的 ID 和必填字段。"));
        return;
    }

    synchronizeConfiguration();
    selectRow(insertedRow);
}

void ProjectRuntimeConfigurationDialog::deleteSelectedScene()
{
    const int row = selectedRow();
    const ProjectScene* scene = m_sceneListModel->sceneAt(row);
    if (!scene)
        return;

    const QString sceneName = scene->name;
    const QString sceneId = scene->id;
    const QMessageBox::StandardButton answer = QMessageBox::question(
        topLevelDialogOwner(this),
        tr("删除场景"),
        tr("确定删除场景“%1”（%2）吗？此操作只会在应用运行配置后写入项目。")
            .arg(sceneName, sceneId),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    if (!m_sceneListModel->removeScene(row))
    {
        m_errorLabel->setText(tr("无法删除所选场景，运行配置未改变。"));
        return;
    }

    synchronizeConfiguration();
    selectRow(qMin(row, m_sceneListModel->rowCount() - 1));
}

void ProjectRuntimeConfigurationDialog::setSelectedSceneAsDefault()
{
    const int row = selectedRow();
    if (row < 0)
        return;

    if (!m_sceneListModel->setDefaultScene(row))
    {
        m_errorLabel->setText(
            tr("无法把所选场景设为默认场景，运行配置未改变。"));
        return;
    }

    synchronizeConfiguration();
    selectRow(row);
}

void ProjectRuntimeConfigurationDialog::updateButtonStates()
{
    const int row = selectedRow();
    const ProjectScene* scene = m_sceneListModel->sceneAt(row);
    const bool hasSelection = scene != nullptr;
    m_editButton->setEnabled(hasSelection);
    m_copyButton->setEnabled(hasSelection);
    m_deleteButton->setEnabled(hasSelection);
    m_setDefaultButton->setEnabled(
        hasSelection &&
        scene->id != m_sceneListModel->configuration().defaultSceneId);
}

void ProjectRuntimeConfigurationDialog::acceptConfiguration()
{
    ProjectRuntimeConfiguration candidate =
        m_sceneListModel->configuration();
    bool repaired = false;
    ProjectRuntimeConfigurationValidationResult result;
    if (!normalizeProjectRuntimeConfiguration(
            candidate, repaired, &result))
    {
        showValidationError(result);
        return;
    }

    result = validateProjectRuntimeConfiguration(candidate);
    if (!result.isValid())
    {
        showValidationError(result);
        return;
    }

    m_configuration = candidate;
    m_sceneListModel->setConfiguration(candidate);
    clearError();
    QDialog::accept();
}

void ProjectRuntimeConfigurationDialog::showValidationError(
    const ProjectRuntimeConfigurationValidationResult& result)
{
    m_lastValidationResult = result;
    m_errorLabel->setText(validationErrorText(result));
    if (result.sceneIndex >= 0)
        selectRow(result.sceneIndex);
}

QString ProjectRuntimeConfigurationDialog::validationErrorText(
    const ProjectRuntimeConfigurationValidationResult& result) const
{
    const QString scenePrefix = result.sceneIndex >= 0
        ? tr("第 %1 个场景：").arg(result.sceneIndex + 1)
        : QString();
    switch (result.error)
    {
    case ProjectRuntimeConfigurationError::None:
        return QString();
    case ProjectRuntimeConfigurationError::InvalidConfigurationObject:
        return tr("运行配置必须是一个对象。");
    case ProjectRuntimeConfigurationError::MissingVersion:
        return tr("运行配置缺少版本号。");
    case ProjectRuntimeConfigurationError::InvalidVersion:
        return tr("运行配置版本号必须是 32 位整数。");
    case ProjectRuntimeConfigurationError::UnsupportedVersion:
        return tr("不支持运行配置版本 %1。").arg(result.value);
    case ProjectRuntimeConfigurationError::InvalidScenesArray:
        return tr("场景列表必须是数组。");
    case ProjectRuntimeConfigurationError::InvalidSceneObject:
        return scenePrefix + tr("场景必须是对象。");
    case ProjectRuntimeConfigurationError::InvalidFieldType:
        return scenePrefix +
            tr("字段“%1”的类型不正确。").arg(result.fieldName);
    case ProjectRuntimeConfigurationError::MissingSceneId:
        return scenePrefix + tr("场景 ID 不能为空。");
    case ProjectRuntimeConfigurationError::MissingSceneName:
        return scenePrefix + tr("场景名称不能为空。");
    case ProjectRuntimeConfigurationError::MissingSceneMap:
        return scenePrefix + tr("必须选择 MAP 文件。");
    case ProjectRuntimeConfigurationError::UnsafeResourcePath:
        return scenePrefix +
            tr("字段“%1”的资源路径不安全：%2")
                .arg(result.fieldName, result.value);
    case ProjectRuntimeConfigurationError::InvalidPlayerPosition:
        return scenePrefix + tr("玩家坐标必须是两个 32 位整数。");
    case ProjectRuntimeConfigurationError::InvalidIntegerVariables:
        return scenePrefix +
            tr("初始变量“%1”的值必须是 32 位整数。").arg(result.value);
    case ProjectRuntimeConfigurationError::InvalidVariableName:
        return scenePrefix + tr("初始变量名称不能为空。");
    case ProjectRuntimeConfigurationError::DuplicateSceneId:
        return scenePrefix +
            tr("场景 ID“%1”与已有场景重复。").arg(result.value);
    case ProjectRuntimeConfigurationError::InvalidDefaultSceneId:
        return tr("默认场景必须精确引用当前场景列表中的一个 ID。");
    }
    return tr("运行配置无效。");
}

void ProjectRuntimeConfigurationDialog::clearError()
{
    m_lastValidationResult = {};
    m_errorLabel->clear();
}

void ProjectRuntimeConfigurationDialog::retranslateUi()
{
    setWindowTitle(tr("运行配置"));
    m_summaryLabel->setText(tr(
        "场景按列表顺序保存。运行配置只记录桌面测试预设；"
        "应用配置不会启动游戏，也不代表资源已经完整。"));
    m_createButton->setText(tr("新建..."));
    m_editButton->setText(tr("编辑..."));
    m_copyButton->setText(tr("复制..."));
    m_deleteButton->setText(tr("删除"));
    m_setDefaultButton->setText(tr("设为默认"));
    if (QPushButton* applyButton =
            m_buttonBox->button(QDialogButtonBox::Ok))
    {
        applyButton->setText(tr("应用"));
    }
    if (QPushButton* cancelButton =
            m_buttonBox->button(QDialogButtonBox::Cancel))
    {
        cancelButton->setText(tr("取消"));
    }

    const int currentRow = selectedRow();
    const ProjectRuntimeConfiguration modelConfiguration =
        m_sceneListModel->configuration();
    m_sceneListModel->setConfiguration(modelConfiguration);
    selectRow(currentRow);
    if (!m_lastValidationResult.isValid())
        m_errorLabel->setText(validationErrorText(m_lastValidationResult));
}
