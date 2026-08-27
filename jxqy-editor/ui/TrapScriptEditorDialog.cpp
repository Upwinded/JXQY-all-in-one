#include "TrapScriptEditorDialog.h"
#include "../core/AuthoringMutationGate.h"

#include "../core/MapFileEditor.h"
#include "../core/SaveEditContext.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

TrapScriptEditorDialog::TrapScriptEditorDialog(
    const QString& mapName,
    const QString& assetsBasePath,
    const MapFileEditor* mapEditor,
    QWidget* parent)
    : QDialog(parent)
    , mapName(mapName)
    , mapSection(mapName.toLower())
    , assetsBasePath(assetsBasePath)
{
    setWindowTitle(tr("陷阱脚本映射编辑器 - %1").arg(mapName));
    setMinimumSize(680, 500);

    if (mapEditor && mapEditor->isLoaded())
    {
        for (int y = 0; y < mapEditor->getHeight(); y++)
        {
            for (int x = 0; x < mapEditor->getWidth(); x++)
            {
                int trapIndex = mapEditor->getTileTrap(x, y);
                if (trapIndex > 0)
                    trapUsageCount[trapIndex]++;
            }
        }
    }

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QHBoxLayout* contextLayout = new QHBoxLayout();
    contextLayout->addWidget(new QLabel(tr("存档编辑上下文:"), this));
    contextComboBox = new QComboBox(this);
    contextComboBox->setObjectName("saveEditContextCombo");
    const QList<SaveEditTarget>& targets = SaveEditContext::instance().targets();
    for (const SaveEditTarget& target : targets)
        contextComboBox->addItem(target.displayName, target.id);
    activeContextIndex = SaveEditContext::instance().currentIndex();
    contextComboBox->setCurrentIndex(activeContextIndex);
    contextLayout->addWidget(contextComboBox, 1);
    mainLayout->addLayout(contextLayout);

    contextNameLabel = new QLabel(this);
    contextNameLabel->setObjectName("saveEditContextNameLabel");
    mainLayout->addWidget(contextNameLabel);

    pathLabel = new QLabel(this);
    pathLabel->setObjectName("trapContextPathLabel");
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel->setWordWrap(true);
    mainLayout->addWidget(pathLabel);

    messageLabel = new QLabel(this);
    messageLabel->setObjectName("trapContextMessageLabel");
    messageLabel->setWordWrap(true);
    mainLayout->addWidget(messageLabel);

    QLabel* infoLabel = new QLabel(
        tr("地图: %1 | 陷阱脚本索引: 1-19（0 表示无/清除） | "
           "脚本路径: script/map/%2/ 或 script/common/")
            .arg(mapName, mapName),
        this);
    infoLabel->setWordWrap(true);
    mainLayout->addWidget(infoLabel);

    table = new QTableWidget(this);
    table->setObjectName("trapScriptTable");
    table->setColumnCount(3);
    table->setRowCount(19);
    table->setHorizontalHeaderLabels({tr("陷阱索引"), tr("脚本文件"), tr("地图上使用")});
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    table->horizontalHeader()->setStretchLastSection(true);
    mainLayout->addWidget(table, 1);

    QHBoxLayout* actionLayout = new QHBoxLayout();
    QPushButton* browseButton = new QPushButton(tr("浏览脚本..."), this);
    connect(browseButton, &QPushButton::clicked, this, [this]()
    {
        int row = table->currentRow();
        if (row < 0)
            return;

        QString scriptDirectory = QDir(this->assetsBasePath).filePath("script/common");
        QString fileName = QFileDialog::getOpenFileName(
            this,
            tr("选择陷阱脚本"),
            scriptDirectory,
            tr("脚本文件 (*.txt);;所有文件 (*.*)"),
            nullptr,
            QFileDialog::DontResolveSymlinks);
        if (!fileName.isEmpty() && table->item(row, 1))
            table->item(row, 1)->setText(QFileInfo(fileName).fileName());
    });
    actionLayout->addWidget(browseButton);

    QPushButton* clearButton = new QPushButton(tr("清空选中脚本"), this);
    connect(clearButton, &QPushButton::clicked, this, [this]()
    {
        int row = table->currentRow();
        if (row >= 0 && table->item(row, 1))
            table->item(row, 1)->setText(QString());
    });
    actionLayout->addWidget(clearButton);
    actionLayout->addStretch();

    buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &TrapScriptEditorDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &TrapScriptEditorDialog::reject);
    actionLayout->addWidget(buttonBox);
    mainLayout->addLayout(actionLayout);

    connect(table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item)
    {
        if (!loadingTable && item && item->column() == 1)
            dirty = true;
    });
    connect(contextComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int newIndex)
    {
        if (newIndex == activeContextIndex)
            return;
        if (!confirmContextSwitch())
        {
            revertContextCombo();
            return;
        }

        SaveEditContext& context = SaveEditContext::instance();
        if (!context.setCurrentIndex(newIndex))
        {
            QMessageBox::warning(this, tr("错误"), tr("无法持久化存档编辑上下文。"));
            revertContextCombo();
            return;
        }

        activeContextIndex = newIndex;
        temporarySaveConfirmed = false;
        if (context.currentTarget().highRisk)
        {
            QMessageBox::warning(
                this,
                tr("高风险存档上下文"),
                tr("你选择了临时运行存档 save/game。游戏运行时可能随时覆盖这些数据，"
                   "请优先编辑新游戏或明确的存档槽。"));
        }
        loadCurrentContext();
    });

    loadCurrentContext();
}

bool TrapScriptEditorDialog::savedChanges() const
{
    return saved;
}

void TrapScriptEditorDialog::accept()
{
    if (dirty && !saveCurrentContext())
        return;
    QDialog::accept();
}

void TrapScriptEditorDialog::reject()
{
    if (dirty)
    {
        QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            tr("放弃未保存修改"),
            tr("当前陷阱脚本有未保存修改，确定放弃吗？"),
            QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Discard)
            return;
    }
    QDialog::reject();
}

void TrapScriptEditorDialog::loadCurrentContext()
{
    trapsIni = INIFileEditor();
    dirty = false;

    QString pathError;
    currentFilePath = SaveEditContext::instance().resolveFilePath(
        assetsBasePath, "traps.ini", &pathError);

    if (currentFilePath.isEmpty())
    {
        loadingTable = true;
        populateTable();
        loadingTable = false;
        table->setEnabled(false);
        buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
        updateContextDisplay(pathError);
        return;
    }

    table->setEnabled(true);
    buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
    QString message;
    if (QFileInfo::exists(currentFilePath))
    {
        if (!trapsIni.loadFromFile(currentFilePath.toUtf8().toStdString()))
        {
            table->setEnabled(false);
            buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
            message = tr("无法读取陷阱脚本文件，请检查文件格式和权限。");
        }
        else if (trapsIni.hasKey(mapSection.toUtf8().toStdString(), "0"))
        {
            dirty = true;
            message = tr("检测到旧的 key 0；确认保存时将自动清理。");
        }
    }
    else
    {
        message = tr("文件尚不存在；只有在你修改内容并确认保存后才会创建。");
    }

    loadingTable = true;
    populateTable();
    loadingTable = false;
    updateContextDisplay(message);
}

bool TrapScriptEditorDialog::saveCurrentContext()
{
    QString pathError;
    QString resolvedPath = SaveEditContext::instance().resolveFilePath(
        assetsBasePath, "traps.ini", &pathError);
    if (resolvedPath.isEmpty())
    {
        QMessageBox::warning(this, tr("无法保存"), pathError);
        return false;
    }
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(resolvedPath);
    if (!mutationLease)
    {
        QMessageBox::warning(
            this, tr("无法保存"), tr("资源正在更新或进行其他写入。"));
        return false;
    }

    const SaveEditTarget target = SaveEditContext::instance().currentTarget();
    if (target.highRisk && !temporarySaveConfirmed)
    {
        QMessageBox::StandardButton answer = QMessageBox::warning(
            this,
            tr("确认写入临时运行存档"),
            tr("即将写入高风险路径：\n%1\n\n游戏运行时可能覆盖该文件，仍要继续吗？")
                .arg(resolvedPath),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return false;
        temporarySaveConfirmed = true;
    }

    QFileInfo fileInfo(resolvedPath);
    if (!fileInfo.exists())
    {
        QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            tr("创建陷阱脚本文件"),
            tr("文件不存在：\n%1\n\n是否创建目录和文件？").arg(resolvedPath),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return false;
    }

    QDir parentDirectory = fileInfo.absoluteDir();
    if (!parentDirectory.exists() && !parentDirectory.mkpath("."))
    {
        QMessageBox::warning(
            this, tr("无法保存"), tr("无法创建目录：%1").arg(parentDirectory.absolutePath()));
        return false;
    }

    INIFileEditor output = trapsIni;
    std::string section = mapSection.toUtf8().toStdString();
    output.removeKey(section, "0");
    for (int row = 0; row < 19; row++)
    {
        int trapIndex = row + 1;
        QTableWidgetItem* scriptItem = table->item(row, 1);
        std::string key = std::to_string(trapIndex);
        std::string scriptFile = scriptItem ? scriptItem->text().toUtf8().toStdString()
                                            : std::string();
        if (scriptFile.empty())
            output.removeKey(section, key);
        else
            output.set(section, key, scriptFile);
    }

    QByteArray bytes = QByteArray::fromStdString(output.saveToString());
    QSaveFile saveFile(resolvedPath);
    saveFile.setDirectWriteFallback(false);
    if (!saveFile.open(QIODevice::WriteOnly) || saveFile.write(bytes) != bytes.size() ||
        !saveFile.commit())
    {
        QMessageBox::warning(this, tr("无法保存"), tr("事务写入失败：%1").arg(resolvedPath));
        return false;
    }

    trapsIni = output;
    currentFilePath = resolvedPath;
    dirty = false;
    saved = true;
    updateContextDisplay(tr("已安全保存。"));
    return true;
}

void TrapScriptEditorDialog::populateTable()
{
    std::string section = mapSection.toUtf8().toStdString();
    for (int row = 0; row < 19; row++)
    {
        int trapIndex = row + 1;
        QTableWidgetItem* indexItem = new QTableWidgetItem(QString::number(trapIndex));
        indexItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, indexItem);

        std::string scriptFile = trapsIni.get(section, std::to_string(trapIndex), "");
        table->setItem(row, 1, new QTableWidgetItem(QString::fromUtf8(scriptFile)));

        int usageCount = trapUsageCount.value(trapIndex, 0);
        QTableWidgetItem* usageItem = new QTableWidgetItem(
            usageCount > 0 ? tr("%1 个瓦片").arg(usageCount) : tr("未使用"));
        usageItem->setFlags(usageItem->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 2, usageItem);
    }
    table->resizeColumnsToContents();
}

void TrapScriptEditorDialog::updateContextDisplay(const QString& message)
{
    const SaveEditTarget target = SaveEditContext::instance().currentTarget();
    contextNameLabel->setText(tr("当前上下文：%1").arg(target.displayName));
    pathLabel->setText(currentFilePath.isEmpty()
        ? tr("实际路径：无法解析")
        : tr("实际路径：%1").arg(QFileInfo(currentFilePath).absoluteFilePath()));

    QString displayedMessage = message;
    if (target.highRisk)
    {
        QString risk = tr("高风险：save/game 是临时运行存档，可能被游戏覆盖。");
        displayedMessage = displayedMessage.isEmpty() ? risk : risk + "\n" + displayedMessage;
    }
    messageLabel->setText(displayedMessage);
}

void TrapScriptEditorDialog::revertContextCombo()
{
    QSignalBlocker blocker(contextComboBox);
    contextComboBox->setCurrentIndex(activeContextIndex);
}

bool TrapScriptEditorDialog::confirmContextSwitch()
{
    if (!dirty)
        return true;

    QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        tr("切换存档编辑上下文"),
        tr("当前上下文有未保存修改。切换前要如何处理？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
        return saveCurrentContext();
    return true;
}
