#include "FileAssociationDialog.h"

#include "../core/DesktopFileAssociationManager.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>

FileAssociationDialog::FileAssociationDialog(
    QWidget* parent,
    DesktopFileAssociationManager* associationManager,
    const QString& requestedExecutablePath)
    : QDialog(parent)
    , executablePath(requestedExecutablePath.isEmpty()
          ? QCoreApplication::applicationFilePath()
          : requestedExecutablePath)
{
    setObjectName(QStringLiteral("fileAssociationDialog"));
    setWindowTitle(tr("Windows 文件关联"));
    setModal(true);
    setMinimumSize(680, 560);

    if (associationManager)
    {
        manager = associationManager;
    }
    else
    {
        ownedManager = std::make_unique<DesktopFileAssociationManager>();
        manager = ownedManager.get();
    }

    auto* introductionLabel = new QLabel(
        tr("选择要由 UPEdit-JXQY 打开的工程文件类型。应用只修改当前 Windows 用户；取消或直接关闭不会写入注册表。"),
        this);
    introductionLabel->setObjectName(
        QStringLiteral("associationIntroductionLabel"));
    introductionLabel->setWordWrap(true);

    associationList = new QListWidget(this);
    associationList->setObjectName(QStringLiteral("associationList"));
    associationList->setAlternatingRowColors(true);

    userChoiceLabel = new QLabel(this);
    userChoiceLabel->setObjectName(QStringLiteral("associationUserChoiceLabel"));
    userChoiceLabel->setWordWrap(true);

    statusLabel = new QLabel(this);
    statusLabel->setObjectName(QStringLiteral("associationStatusLabel"));
    statusLabel->setWordWrap(true);

    applyButton = new QPushButton(tr("应用所选关联"), this);
    applyButton->setObjectName(QStringLiteral("applyAssociationButton"));
    restoreButton = new QPushButton(tr("恢复全部关联"), this);
    restoreButton->setObjectName(QStringLiteral("restoreAssociationButton"));
    defaultAppsButton = new QPushButton(tr("打开 Windows 默认应用"), this);
    defaultAppsButton->setObjectName(QStringLiteral("defaultAppsButton"));

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttonBox->setObjectName(QStringLiteral("associationButtonBox"));
    if (QPushButton* closeButton = buttonBox->button(QDialogButtonBox::Close))
        closeButton->setText(tr("关闭"));
    buttonBox->addButton(defaultAppsButton, QDialogButtonBox::ActionRole);
    buttonBox->addButton(restoreButton, QDialogButtonBox::ActionRole);
    buttonBox->addButton(applyButton, QDialogButtonBox::ApplyRole);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(introductionLabel);
    layout->addWidget(associationList, 1);
    layout->addWidget(userChoiceLabel);
    layout->addWidget(statusLabel);
    layout->addWidget(buttonBox);

    connect(applyButton, &QPushButton::clicked,
            this, &FileAssociationDialog::applySelection);
    connect(restoreButton, &QPushButton::clicked,
            this, &FileAssociationDialog::restoreAll);
    connect(defaultAppsButton, &QPushButton::clicked,
            this, &FileAssociationDialog::openWindowsDefaultApps);
    connect(buttonBox, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    refresh();
}

FileAssociationDialog::~FileAssociationDialog() = default;

void FileAssociationDialog::refresh()
{
    resetRestoreConfirmation();
    const DesktopFileAssociationResult result =
        manager->queryStates(executablePath);
    associationList->clear();

    if (!result.success)
    {
        showResult(false, false, result.error);
        applyButton->setEnabled(false);
        restoreButton->setEnabled(false);
        return;
    }

    bool hasManagedAssociation = false;
    QStringList explicitUserChoices;
    for (const DesktopFileAssociationState& state : result.states)
    {
        auto* item = new QListWidgetItem(
            associationLabel(state.definition.extension), associationList);
        item->setData(Qt::UserRole, state.definition.extension);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(state.managed ? Qt::Checked : Qt::Unchecked);

        QStringList details;
        if (state.userChoicePresent)
        {
            details.append(tr("Windows 当前显式默认：%1")
                               .arg(state.userChoiceProgramId));
            explicitUserChoices.append(state.definition.extension);
        }
        else if (state.currentUserFallbackIsEditor)
        {
            details.append(tr("当前用户回退关联由编辑器管理"));
        }
        else
        {
            details.append(tr("当前用户回退关联不是编辑器"));
        }
        if (state.needsRepair)
            details.append(tr("启动路径需要修复"));
        item->setText(QStringLiteral("%1 — %2")
                          .arg(item->text(), details.join(QStringLiteral("；"))));
        item->setToolTip(details.join(QStringLiteral("；")));
        hasManagedAssociation = hasManagedAssociation || state.managed;
    }

    applyButton->setEnabled(true);
    restoreButton->setEnabled(hasManagedAssociation);
    userChoiceLabel->setText(explicitUserChoices.isEmpty()
        ? tr("Windows 未为这些类型记录受保护的显式默认选择；系统有效默认仍以“Windows 默认应用”和资源管理器为准。")
        : tr("Windows 已为以下类型记录显式默认选择：%1。编辑器不会改写或删除 UserChoice；如需更改，请使用“Windows 默认应用”。")
              .arg(explicitUserChoices.join(QStringLiteral(", "))));
    showResult(true, false, QString());
}

void FileAssociationDialog::resetRestoreConfirmation()
{
    restoreConfirmationPending = false;
    if (restoreButton)
        restoreButton->setText(tr("恢复全部关联"));
}

void FileAssociationDialog::applySelection()
{
    resetRestoreConfirmation();
    QSet<QString> selectedExtensions;
    for (int row = 0; row < associationList->count(); ++row)
    {
        QListWidgetItem* item = associationList->item(row);
        if (item->checkState() == Qt::Checked)
            selectedExtensions.insert(item->data(Qt::UserRole).toString());
    }

    const DesktopFileAssociationResult result =
        manager->applySelection(selectedExtensions, executablePath);
    if (result.success)
    {
        refresh();
        showResult(true, result.changed, QString());
    }
    else
    {
        showResult(false, false, result.error);
    }
}

void FileAssociationDialog::restoreAll()
{
    if (!restoreConfirmationPending)
    {
        restoreConfirmationPending = true;
        restoreButton->setText(tr("确认恢复全部关联"));
        statusLabel->setStyleSheet(QStringLiteral("color: #b36b00;"));
        statusLabel->setText(
            tr("再次点击“确认恢复全部关联”将恢复编辑器首次接管前的当前用户关联，并移除 UPEdit-JXQY 的注册信息；关闭或执行其他操作会取消确认。Windows 中后续选择的其他默认应用会保留。"));
        return;
    }

    resetRestoreConfirmation();

    const DesktopFileAssociationResult result =
        manager->restoreAll(executablePath);
    if (result.success)
    {
        refresh();
        showResult(true, result.changed, QString());
    }
    else
    {
        showResult(false, false, result.error);
    }
}

void FileAssociationDialog::openWindowsDefaultApps()
{
    resetRestoreConfirmation();
#ifdef Q_OS_WIN
    if (!QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:defaultapps"))))
    {
        showResult(false, false,
                   tr("无法打开 Windows 默认应用设置。"));
    }
#else
    showResult(false, false,
               tr("文件关联管理仅在 Windows 上可用。"));
#endif
}

QString FileAssociationDialog::associationLabel(
    const QString& extension) const
{
    if (extension == QStringLiteral(".mpc"))
        return tr("MPC 图片 (*.mpc)");
    if (extension == QStringLiteral(".shd"))
        return tr("SHD 图片 (*.shd)");
    if (extension == QStringLiteral(".asf"))
        return tr("ASF 图片 (*.asf)");
    if (extension == QStringLiteral(".pic"))
        return tr("PIC 图片 (*.pic)");
    if (extension == QStringLiteral(".imp"))
        return tr("IMP 图片 (*.imp)");
    if (extension == QStringLiteral(".img"))
        return tr("IMG 图片 (*.img)");
    if (extension == QStringLiteral(".map"))
        return tr("地图 (*.map)");
    if (extension == QStringLiteral(".npc"))
        return tr("NPC 实例表 (*.npc)");
    if (extension == QStringLiteral(".obj"))
        return tr("OBJ 实例表 (*.obj)");
    if (extension == QStringLiteral(".txt"))
        return tr("脚本文本 (*.txt，可能影响其他文本文件的默认打开方式)");
    return extension;
}

void FileAssociationDialog::showResult(
    bool success, bool changed, const QString& error)
{
    if (!success)
    {
        statusLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
        statusLabel->setText(tr("操作失败：%1").arg(error));
        return;
    }

    statusLabel->setStyleSheet(QString());
    statusLabel->setText(changed
        ? tr("文件关联已更新。")
        : tr("当前设置无需更改。"));
}
