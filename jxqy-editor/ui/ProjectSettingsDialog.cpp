#include "ProjectSettingsDialog.h"

#include "../core/EditorAssetPath.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

ProjectSettingsDialog::ProjectSettingsDialog(
    Mode mode,
    const QString& projectFilePath,
    const ProjectResourceConfiguration& initialConfiguration,
    QWidget* parent)
    : QDialog(parent)
    , m_projectFilePath(projectFilePath)
    , m_requestedResourcePackId(
          initialConfiguration.activeResourcePackId.trimmed())
    , m_requestedResourcePackEntryKey(
          initialConfiguration.activeResourcePackEntryKey.trimmed())
{
    setObjectName(QStringLiteral("projectSettingsDialog"));
    setWindowTitle(mode == Mode::CreateProject
        ? tr("创建项目") : tr("项目设置"));
    setModal(true);
    setMinimumWidth(720);

    auto* projectFileEdit = new QLineEdit(projectFilePath, this);
    projectFileEdit->setObjectName(QStringLiteral("projectFilePathEdit"));
    projectFileEdit->setReadOnly(true);

    m_sourceAssetsRootEdit = new QLineEdit(
        initialConfiguration.sourceAssetsRoot, this);
    m_sourceAssetsRootEdit->setObjectName(
        QStringLiteral("sourceAssetsRootEdit"));
    auto* sourceBrowseButton = new QPushButton(tr("浏览..."), this);
    sourceBrowseButton->setObjectName(
        QStringLiteral("sourceAssetsRootBrowseButton"));
    auto* sourceRow = new QWidget(this);
    auto* sourceLayout = new QHBoxLayout(sourceRow);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    sourceLayout->addWidget(m_sourceAssetsRootEdit, 1);
    sourceLayout->addWidget(sourceBrowseButton);

    m_editableAssetsRootEdit = new QLineEdit(
        initialConfiguration.editableAssetsRoot, this);
    m_editableAssetsRootEdit->setObjectName(
        QStringLiteral("editableAssetsRootEdit"));
    auto* editableBrowseButton = new QPushButton(tr("浏览..."), this);
    editableBrowseButton->setObjectName(
        QStringLiteral("editableAssetsRootBrowseButton"));
    auto* editableRow = new QWidget(this);
    auto* editableLayout = new QHBoxLayout(editableRow);
    editableLayout->setContentsMargins(0, 0, 0, 0);
    editableLayout->addWidget(m_editableAssetsRootEdit, 1);
    editableLayout->addWidget(editableBrowseButton);

    m_activeResourcePackCombo = new QComboBox(this);
    m_activeResourcePackCombo->setObjectName(
        QStringLiteral("activeResourcePackCombo"));

    m_gameContextLabel = new QLabel(this);
    m_gameContextLabel->setObjectName(
        QStringLiteral("projectGameContextLabel"));
    m_gameContextLabel->setWordWrap(true);

    auto* formLayout = new QFormLayout;
    formLayout->addRow(tr("项目文件："), projectFileEdit);
    formLayout->addRow(tr("原始资源根（可选）："), sourceRow);
    formLayout->addRow(tr("可编辑资源根："), editableRow);
    formLayout->addRow(tr("活动资源包："), m_activeResourcePackCombo);
    formLayout->addRow(tr("游戏/资源上下文："), m_gameContextLabel);

    auto* sourceHint = new QLabel(
        tr("原始资源根只记录为导入来源；内容编辑、保存和事务恢复始终使用可编辑资源根。"),
        this);
    sourceHint->setObjectName(QStringLiteral("sourceAssetsRootHint"));
    sourceHint->setWordWrap(true);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("projectSettingsErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));

    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->setObjectName(QStringLiteral("projectSettingsButtonBox"));
    if (QPushButton* okButton = m_buttonBox->button(QDialogButtonBox::Ok))
    {
        okButton->setText(mode == Mode::CreateProject
            ? tr("创建") : tr("应用"));
    }

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(sourceHint);
    mainLayout->addWidget(m_errorLabel);
    mainLayout->addStretch();
    mainLayout->addWidget(m_buttonBox);

    connect(sourceBrowseButton, &QPushButton::clicked,
        this, &ProjectSettingsDialog::browseSourceAssetsRoot);
    connect(editableBrowseButton, &QPushButton::clicked,
        this, &ProjectSettingsDialog::browseEditableAssetsRoot);
    connect(m_sourceAssetsRootEdit, &QLineEdit::editingFinished,
        this, &ProjectSettingsDialog::updateValidation);
    connect(m_editableAssetsRootEdit, &QLineEdit::editingFinished,
        this, &ProjectSettingsDialog::rebuildResourcePacks);
    connect(m_activeResourcePackCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this, [this](int index)
        {
            if (index >= 0)
            {
                m_requestedResourcePackId =
                    selectedResourcePackId();
                m_requestedResourcePackEntryKey =
                    selectedResourcePackEntryKey();
            }
            updateResourceContext();
            updateValidation();
        });
    connect(m_buttonBox, &QDialogButtonBox::accepted,
        this, &ProjectSettingsDialog::acceptSettings);
    connect(m_buttonBox, &QDialogButtonBox::rejected,
        this, &QDialog::reject);

    rebuildResourcePacks();
}

ProjectResourceConfiguration ProjectSettingsDialog::configuration() const
{
    ProjectResourceConfiguration result;
    result.sourceAssetsRoot = resolveInputPath(
        m_sourceAssetsRootEdit->text());
    result.editableAssetsRoot = resolveInputPath(
        m_editableAssetsRootEdit->text());
    result.activeResourcePackId = selectedResourcePackId();
    result.activeResourcePackEntryKey =
        selectedResourcePackEntryKey();
    return result;
}

QString ProjectSettingsDialog::resolveInputPath(const QString& input) const
{
    const QString trimmedPath = input.trimmed();
    if (trimmedPath.isEmpty())
        return QString();

    const QString portablePath = QDir::fromNativeSeparators(trimmedPath);
    if (QDir::isAbsolutePath(portablePath))
        return EditorAssetPath::normalizedAbsolutePath(portablePath);

    const QDir projectDirectory(
        QFileInfo(m_projectFilePath).absolutePath());
    return EditorAssetPath::normalizedAbsolutePath(
        projectDirectory.filePath(portablePath));
}

void ProjectSettingsDialog::browseSourceAssetsRoot()
{
    const QString currentPath = resolveInputPath(
        m_sourceAssetsRootEdit->text());
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("选择原始资源目录"),
        currentPath.isEmpty()
            ? QFileInfo(m_projectFilePath).absolutePath() : currentPath,
        QFileDialog::ShowDirsOnly |
            QFileDialog::DontResolveSymlinks);
    if (directory.isEmpty())
        return;

    m_sourceAssetsRootEdit->setText(QDir::toNativeSeparators(directory));
    updateValidation();
}

void ProjectSettingsDialog::browseEditableAssetsRoot()
{
    const QString currentPath = resolveInputPath(
        m_editableAssetsRootEdit->text());
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("选择可编辑资源目录"),
        currentPath.isEmpty()
            ? QFileInfo(m_projectFilePath).absolutePath() : currentPath,
        QFileDialog::ShowDirsOnly |
            QFileDialog::DontResolveSymlinks);
    if (directory.isEmpty())
        return;

    m_editableAssetsRootEdit->setText(QDir::toNativeSeparators(directory));
    rebuildResourcePacks();
}

void ProjectSettingsDialog::rebuildResourcePacks()
{
    const QString requestedResourcePackId = selectedResourcePackId().isEmpty()
        ? m_requestedResourcePackId : selectedResourcePackId();
    const QString requestedResourcePackEntryKey =
        selectedResourcePackEntryKey().isEmpty()
        ? m_requestedResourcePackEntryKey
        : selectedResourcePackEntryKey();
    const QString editableAssetsRoot = resolveInputPath(
        m_editableAssetsRootEdit->text());

    m_activeResourcePackCombo->blockSignals(true);
    m_activeResourcePackCombo->clear();
    m_availablePacks.clear();
    m_resourceConfigurationError.clear();

    if (!editableAssetsRoot.isEmpty() && QDir(editableAssetsRoot).exists())
    {
        const ResourcePackSelection selection =
            ResourcePackScanner::resolveActivePack(
                editableAssetsRoot,
                requestedResourcePackId,
                requestedResourcePackEntryKey);
        m_availablePacks = selection.availablePacks;

        if (selection.status ==
                 ResourcePackSelectionStatus::RecoveryFailed)
        {
            m_resourceConfigurationError = tr(
                "资源目录中存在未能安全恢复的保存事务。请先处理事务错误后再选择资源包：%1")
                .arg(selection.recoveryErrors.join('\n'));
        }
        else if (selection.status ==
                 ResourcePackSelectionStatus::InvalidAssetsRoot)
        {
            m_resourceConfigurationError = tr(
                "可编辑资源根不存在或不可访问。");
        }
        else if (selection.status ==
                 ResourcePackSelectionStatus::ResourceIdConflict)
        {
            m_resourceConfigurationError = tr(
                "资源集合存在重复 Game.Id。请先修改冲突资源的 ID。");
        }
        else if (m_availablePacks.isEmpty())
        {
            m_activeResourcePackCombo->addItem(
                tr("创作目录（尚无 game_profile.ini）"), QString());
            m_activeResourcePackCombo->setCurrentIndex(0);
        }
        else
        {
            bool invalidResourcePackId = false;
            for (const ResourcePackInfo& pack : m_availablePacks)
            {
                const QString resourcePackId = pack.profile.id.trimmed();
                if (resourcePackId.isEmpty())
                {
                    invalidResourcePackId = true;
                }

                const QString displayName = pack.profile.name.trimmed().isEmpty()
                    ? QFileInfo(pack.rootPath).fileName()
                    : pack.profile.name.trimmed();
                m_activeResourcePackCombo->addItem(
                    tr("%1 — %2 [%3]").arg(
                        displayName,
                        resourcePackId,
                        QDir(editableAssetsRoot).relativeFilePath(
                            pack.rootPath)),
                    resourcePackId);
                m_activeResourcePackCombo->setItemData(
                    m_activeResourcePackCombo->count() - 1,
                    pack.stableEntryKey,
                    Qt::UserRole + 1);
            }

            if (invalidResourcePackId)
            {
                m_resourceConfigurationError = tr(
                    "资源包必须具有非空 Game.Id；重复 Id 将使用稳定清单条目键区分。");
            }

            int selectedIndex = -1;
            for (int index = 0;
                 index < m_activeResourcePackCombo->count(); ++index)
            {
                const bool entryMatches =
                    !requestedResourcePackEntryKey.isEmpty() &&
                    m_activeResourcePackCombo->itemData(
                        index, Qt::UserRole + 1).toString()
                        .compare(
                            requestedResourcePackEntryKey,
                            Qt::CaseInsensitive) == 0;
                const bool idMatches =
                    requestedResourcePackEntryKey.isEmpty() &&
                    !requestedResourcePackId.isEmpty() &&
                    m_activeResourcePackCombo->itemData(index).toString()
                        .compare(
                            requestedResourcePackId,
                            Qt::CaseInsensitive) == 0;
                if (entryMatches || idMatches)
                {
                    selectedIndex = index;
                    break;
                }
            }
            if (selectedIndex < 0 && m_availablePacks.size() == 1)
                selectedIndex = 0;
            m_activeResourcePackCombo->setCurrentIndex(selectedIndex);
        }
    }

    m_activeResourcePackCombo->blockSignals(false);
    if (m_activeResourcePackCombo->currentIndex() >= 0)
    {
        m_requestedResourcePackId = selectedResourcePackId();
        m_requestedResourcePackEntryKey =
            selectedResourcePackEntryKey();
    }
    updateResourceContext();
    updateValidation();
}

void ProjectSettingsDialog::updateResourceContext()
{
    const int currentIndex = m_activeResourcePackCombo->currentIndex();
    if (currentIndex < 0)
    {
        m_gameContextLabel->setText(
            tr("请选择一个活动资源包；项目不会保存资源包目录、名称或 Game.Type 副本。"));
        return;
    }

    const QString resourcePackId = selectedResourcePackId();
    if (resourcePackId.isEmpty())
    {
        m_gameContextLabel->setText(
            tr("该目录可自由编辑并运行当前内容；创建有效 game_profile.ini 后才会作为正式资源包加载。"));
        return;
    }

    if (currentIndex >= m_availablePacks.size())
    {
        m_gameContextLabel->clear();
        return;
    }

    const GameProfile& profile =
        m_availablePacks[currentIndex].profile;
    const QString displayName = profile.name.trimmed().isEmpty()
        ? tr("未命名资源包") : profile.name.trimmed();
    const QString typeText = profile.typeDefined
        ? QString::number(profile.type)
        : tr("继承（由运行时解析）");
    m_gameContextLabel->setText(
        tr("名称：%1；Game.Id：%2；Game.Type：%3")
            .arg(displayName, profile.id.trimmed(), typeText));
}

void ProjectSettingsDialog::updateValidation()
{
    QString error;
    const QString sourceAssetsRoot = resolveInputPath(
        m_sourceAssetsRootEdit->text());
    const QString editableAssetsRoot = resolveInputPath(
        m_editableAssetsRootEdit->text());

    if (!sourceAssetsRoot.isEmpty())
    {
        const QFileInfo sourceInfo(sourceAssetsRoot);
        if (!sourceInfo.isDir() || !sourceInfo.isReadable())
            error = tr("原始资源根不存在或不可读取。");
    }
    if (error.isEmpty())
    {
        const QFileInfo editableInfo(editableAssetsRoot);
        if (editableAssetsRoot.isEmpty())
            error = tr("请选择可编辑资源根。");
        else if (!editableInfo.isDir() || !editableInfo.isReadable())
            error = tr("可编辑资源根不存在或不可读取。");
    }
    if (error.isEmpty() && !sourceAssetsRoot.isEmpty() &&
        EditorAssetPath::comparisonKey(sourceAssetsRoot) ==
            EditorAssetPath::comparisonKey(editableAssetsRoot))
    {
        error = tr("原始资源根与可编辑资源根必须分离，不能选择同一目录。");
    }
    if (error.isEmpty() && !m_resourceConfigurationError.isEmpty())
        error = m_resourceConfigurationError;
    if (error.isEmpty() &&
        m_activeResourcePackCombo->currentIndex() < 0)
    {
        error = tr("当前资源集合包含多个资源包，请明确选择活动资源包。");
    }

    m_errorLabel->setText(error);
    if (QPushButton* okButton =
            m_buttonBox->button(QDialogButtonBox::Ok))
    {
        okButton->setEnabled(error.isEmpty());
    }
}

void ProjectSettingsDialog::acceptSettings()
{
    rebuildResourcePacks();
    if (m_errorLabel->text().isEmpty())
        accept();
}

QString ProjectSettingsDialog::selectedResourcePackId() const
{
    const int currentIndex = m_activeResourcePackCombo->currentIndex();
    return currentIndex < 0
        ? QString()
        : m_activeResourcePackCombo->itemData(currentIndex).toString().trimmed();
}

QString ProjectSettingsDialog::selectedResourcePackEntryKey() const
{
    const int currentIndex =
        m_activeResourcePackCombo->currentIndex();
    return currentIndex < 0
        ? QString()
        : m_activeResourcePackCombo->itemData(
            currentIndex, Qt::UserRole + 1)
            .toString().trimmed();
}
