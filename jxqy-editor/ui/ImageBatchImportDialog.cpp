#include "ImageBatchImportDialog.h"

#include "../core/DurableFileTransaction.h"
#include "ui_ImageBatchImportDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidgetItem>

#include <algorithm>
#include <functional>
#include <limits>

namespace
{
constexpr int FileNameColumn = 0;
constexpr int FilePathColumn = 1;
constexpr int DimensionsColumn = 2;
constexpr int XOffsetColumn = 3;
constexpr int YOffsetColumn = 4;
constexpr int StatusColumn = 5;

QTableWidgetItem* readOnlyItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}
}

ImageBatchImportDialog::ImageBatchImportDialog(
    int existingFrameCountValue,
    int currentFrameIndexValue,
    int32_t currentXOffsetValue,
    int32_t currentYOffsetValue,
    ImageBatchImportInsertionMode defaultInsertionMode,
    const QString& initialDirectory,
    QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::ImageBatchImportDialog)
    , existingFrameCount(existingFrameCountValue)
    , currentFrameIndex(currentFrameIndexValue)
    , currentXOffset(currentXOffsetValue)
    , currentYOffset(currentYOffsetValue)
    , fileDialogDirectory(initialDirectory)
{
    ui->setupUi(this);

    ui->insertionPositionComboBox->addItem(
        tr("当前帧之前"),
        static_cast<int>(ImageBatchImportInsertionMode::BeforeCurrent));
    ui->insertionPositionComboBox->addItem(
        tr("当前帧之后"),
        static_cast<int>(ImageBatchImportInsertionMode::AfterCurrent));
    ui->insertionPositionComboBox->addItem(
        tr("文档末尾"),
        static_cast<int>(ImageBatchImportInsertionMode::DocumentEnd));
    const int insertionIndexValue = ui->insertionPositionComboBox->findData(
        static_cast<int>(defaultInsertionMode));
    ui->insertionPositionComboBox->setCurrentIndex(insertionIndexValue);

    ui->offsetTemplateComboBox->addItem(
        tr("零偏移 (0, 0)"), static_cast<int>(OffsetTemplate::Zero));
    ui->offsetTemplateComboBox->addItem(
        tr("当前帧偏移 (%1, %2)")
            .arg(currentXOffset)
            .arg(currentYOffset),
        static_cast<int>(OffsetTemplate::CurrentFrame));
    ui->offsetTemplateComboBox->addItem(
        tr("固定偏移"), static_cast<int>(OffsetTemplate::Fixed));

    ui->fixedXOffsetSpinBox->setRange(
        (std::numeric_limits<int32_t>::min)(),
        (std::numeric_limits<int32_t>::max)());
    ui->fixedYOffsetSpinBox->setRange(
        (std::numeric_limits<int32_t>::min)(),
        (std::numeric_limits<int32_t>::max)());

    QHeaderView* header = ui->importQueueTableWidget->horizontalHeader();
    header->setSectionResizeMode(FileNameColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(FilePathColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(DimensionsColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(XOffsetColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(YOffsetColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(StatusColumn, QHeaderView::ResizeToContents);

    connect(ui->addFilesButton, &QPushButton::clicked,
        this, &ImageBatchImportDialog::onAddFiles);
    connect(ui->loadRecipeButton, &QPushButton::clicked,
        this, &ImageBatchImportDialog::onLoadRecipe);
    connect(ui->saveRecipeButton, &QPushButton::clicked,
        this, &ImageBatchImportDialog::onSaveRecipe);
    connect(ui->removeFilesButton, &QPushButton::clicked,
        this, &ImageBatchImportDialog::onRemoveSelected);
    connect(ui->moveUpButton, &QPushButton::clicked,
        this, &ImageBatchImportDialog::onMoveUp);
    connect(ui->moveDownButton, &QPushButton::clicked,
        this, &ImageBatchImportDialog::onMoveDown);
    connect(ui->clearQueueButton, &QPushButton::clicked,
        this, &ImageBatchImportDialog::onClearQueue);
    connect(ui->revalidateButton, &QPushButton::clicked,
        this, &ImageBatchImportDialog::onRevalidateQueue);
    connect(ui->applyTemplateButton, &QPushButton::clicked,
        this, &ImageBatchImportDialog::onApplyTemplate);
    connect(ui->offsetTemplateComboBox,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ImageBatchImportDialog::onTemplateChanged);
    connect(ui->importQueueTableWidget, &QTableWidget::itemChanged,
        this, &ImageBatchImportDialog::onTableItemChanged);
    connect(ui->importQueueTableWidget->selectionModel(),
        &QItemSelectionModel::selectionChanged,
        this, &ImageBatchImportDialog::updateUiState);
    connect(ui->buttonBox, &QDialogButtonBox::accepted,
        this, &ImageBatchImportDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected,
        this, &ImageBatchImportDialog::reject);

    onTemplateChanged();
    updateUiState();
}

ImageBatchImportDialog::~ImageBatchImportDialog()
{
    delete ui;
}

void ImageBatchImportDialog::enqueueFiles(const QStringList& filePaths)
{
    int32_t xOffset = 0;
    int32_t yOffset = 0;
    selectedTemplateOffsets(&xOffset, &yOffset);
    const int firstNewRow = queueEntries.size();
    for (const QString& filePath : filePaths)
    {
        QueueEntry entry;
        entry.probe = ImageFrameImport::probeFile(filePath);
        entry.filePath = entry.probe.normalizedPath.isEmpty()
            ? filePath : entry.probe.normalizedPath;
        entry.xOffsetText = QString::number(xOffset);
        entry.yOffsetText = QString::number(yOffset);
        queueEntries.append(std::move(entry));
    }
    rebuildTable(firstNewRow);
}

const std::vector<ImageFrameData>&
ImageBatchImportDialog::preparedFrames() const
{
    return acceptedFrames;
}

ImageBatchImportInsertionMode ImageBatchImportDialog::insertionMode() const
{
    return static_cast<ImageBatchImportInsertionMode>(
        ui->insertionPositionComboBox->currentData().toInt());
}

int ImageBatchImportDialog::insertionIndex() const
{
    switch (insertionMode())
    {
    case ImageBatchImportInsertionMode::BeforeCurrent:
        return currentFrameIndex;
    case ImageBatchImportInsertionMode::AfterCurrent:
        return currentFrameIndex + 1;
    case ImageBatchImportInsertionMode::DocumentEnd:
        return existingFrameCount;
    }
    return -1;
}

bool ImageBatchImportDialog::loadRecipeFile(
    const QString& recipeFilePath,
    QString* errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
    std::vector<ImageFrameImportRequest> requests;
    ImageFrameImportRecipeFailure failure;
    std::shared_ptr<DurableFileRecoveredReadLock> coherentRead;
    if (!ImageFrameImportRecipe::load(
            recipeFilePath, &requests, &failure, &coherentRead))
    {
        if (errorMessage)
            *errorMessage = recipeErrorText(failure);
        return false;
    }

    QVector<QueueEntry> loadedEntries;
    loadedEntries.reserve(static_cast<qsizetype>(requests.size()));
    for (const ImageFrameImportRequest& request : requests)
    {
        QueueEntry entry;
        entry.filePath = request.filePath;
        entry.probe = ImageFrameImport::probeFile(request.filePath);
        entry.xOffsetText = QString::number(request.xOffset);
        entry.yOffsetText = QString::number(request.yOffset);
        loadedEntries.append(std::move(entry));
    }
    queueEntries = std::move(loadedEntries);
    acceptedFrames.clear();
    fileDialogDirectory = QFileInfo(recipeFilePath).absolutePath();
    ui->recipeStatusLabel->setText(
        tr("已加载方案：%1；队列修改不会自动写回。")
            .arg(QDir::toNativeSeparators(recipeFilePath)));
    rebuildTable(queueEntries.isEmpty() ? -1 : 0);
    return true;
}

bool ImageBatchImportDialog::saveRecipeFile(
    const QString& recipeFilePath,
    QString* errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
    std::vector<ImageFrameImportRequest> requests;
    int invalidRow = -1;
    if (!collectRecipeRequests(&requests, &invalidRow))
    {
        if (errorMessage)
        {
            *errorMessage = invalidRow >= 0
                ? tr("队列第 %1 项无效，不能保存方案。")
                      .arg(invalidRow + 1)
                : tr("队列为空或超过 10000 项，不能保存方案。");
        }
        return false;
    }

    ImageFrameImportRecipeFailure failure;
    if (!ImageFrameImportRecipe::save(
            recipeFilePath, requests, &failure))
    {
        if (errorMessage)
            *errorMessage = recipeErrorText(failure);
        return false;
    }
    fileDialogDirectory = QFileInfo(recipeFilePath).absolutePath();
    QString status = tr("已耐久保存方案：%1")
        .arg(QDir::toNativeSeparators(recipeFilePath));
    if (!failure.warning.isEmpty())
        status += tr("；清理警告：%1").arg(failure.warning);
    ui->recipeStatusLabel->setText(status);
    return true;
}

void ImageBatchImportDialog::accept()
{
    std::vector<ImageFrameImportRequest> requests;
    int invalidRow = -1;
    if (!collectRequests(&requests, &invalidRow))
    {
        QMessageBox::warning(
            this,
            tr("批量导入失败"),
            invalidRow >= 0
                ? tr("队列第 %1 项无效，请修正或移除后重试。")
                      .arg(invalidRow + 1)
                : tr("队列为空或导入后超过 10000 帧上限。"));
        return;
    }

    int failedIndex = -1;
    ImageFrameImportError error = ImageFrameImportError::None;
    std::vector<ImageFrameData> prepared;
    QStringList recoveryErrors;
    auto coherentRead = DurableFileTransaction::acquireRecoveredReadLock(
        QFileInfo(requests.front().filePath).absolutePath(),
        recoveryErrors);
    if (!coherentRead)
    {
        QMessageBox::warning(
            this,
            tr("批量导入失败"),
            tr("资源正在进行其他写入，请稍后重试。\n%1")
                .arg(recoveryErrors.join('\n')));
        return;
    }
    for (const ImageFrameImportRequest& request : requests)
    {
        if (!coherentRead->addRecoveredReadRoot(
                QFileInfo(request.filePath).absolutePath(),
                recoveryErrors))
        {
            QMessageBox::warning(
                this,
                tr("批量导入失败"),
                tr("资源目录正在更新或保存，请稍后重试。\n%1")
                    .arg(recoveryErrors.isEmpty()
                        ? request.filePath
                        : recoveryErrors.join('\n')));
            return;
        }
    }
    if (!ImageFrameImport::prepareBatch(
            requests,
            existingFrameCount,
            &prepared,
            &failedIndex,
            &error))
    {
        onRevalidateQueue();
        const QString message = failedIndex >= 0
            ? tr("确认时重新读取第 %1 项失败：%2\n%3")
                  .arg(failedIndex + 1)
                  .arg(importErrorText(error))
                  .arg(requests[static_cast<size_t>(failedIndex)].filePath)
            : importErrorText(error);
        QMessageBox::warning(this, tr("批量导入失败"), message);
        return;
    }

    acceptedFrames = std::move(prepared);
    QDialog::accept();
}

void ImageBatchImportDialog::onAddFiles()
{
    QFileDialog fileDialog(
        nullptr,
        tr("选择 PNG 帧"),
        fileDialogDirectory,
        tr("PNG 图片 (*.png)"));
    fileDialog.setAcceptMode(QFileDialog::AcceptOpen);
    fileDialog.setFileMode(QFileDialog::ExistingFiles);
    fileDialog.setOption(QFileDialog::DontUseNativeDialog);
    if (fileDialog.exec() != QDialog::Accepted)
        return;
    const QStringList filePaths = fileDialog.selectedFiles();
    if (filePaths.isEmpty())
        return;
    fileDialogDirectory = QFileInfo(filePaths.constFirst()).absolutePath();
    enqueueFiles(filePaths);
}

void ImageBatchImportDialog::onLoadRecipe()
{
    QFileDialog fileDialog(
        nullptr,
        tr("选择图片帧导入方案"),
        fileDialogDirectory,
        tr("图片帧导入方案 (*.json);;JSON 文件 (*.json)"));
    fileDialog.setAcceptMode(QFileDialog::AcceptOpen);
    fileDialog.setFileMode(QFileDialog::ExistingFile);
    fileDialog.setOption(QFileDialog::DontUseNativeDialog);
    if (fileDialog.exec() != QDialog::Accepted ||
        fileDialog.selectedFiles().isEmpty())
    {
        return;
    }

    if (!queueEntries.isEmpty())
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            tr("替换当前导入队列"),
            tr("加载方案将替换当前 %1 项队列，但不会导入或修改图片文档。继续吗？")
                .arg(queueEntries.size()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    QString errorMessage;
    if (!loadRecipeFile(
            fileDialog.selectedFiles().constFirst(), &errorMessage))
    {
        QMessageBox::warning(
            this, tr("加载导入方案失败"), errorMessage);
    }
}

void ImageBatchImportDialog::onSaveRecipe()
{
    std::vector<ImageFrameImportRequest> requests;
    int invalidRow = -1;
    if (!collectRecipeRequests(&requests, &invalidRow))
    {
        QMessageBox::warning(
            this,
            tr("保存导入方案失败"),
            invalidRow >= 0
                ? tr("队列第 %1 项无效，不能保存方案。")
                      .arg(invalidRow + 1)
                : tr("队列为空或超过 10000 项，不能保存方案。"));
        return;
    }

    QFileDialog fileDialog(
        nullptr,
        tr("保存图片帧导入方案"),
        fileDialogDirectory,
        tr("图片帧导入方案 (*.json)"));
    fileDialog.setAcceptMode(QFileDialog::AcceptSave);
    fileDialog.setFileMode(QFileDialog::AnyFile);
    fileDialog.setDefaultSuffix(QStringLiteral("json"));
    fileDialog.selectFile(QStringLiteral(
        "jxqy-image-frame-import-recipe.json"));
    fileDialog.setOption(QFileDialog::DontUseNativeDialog);
    if (fileDialog.exec() != QDialog::Accepted ||
        fileDialog.selectedFiles().isEmpty())
    {
        return;
    }

    QString errorMessage;
    if (!saveRecipeFile(
            fileDialog.selectedFiles().constFirst(), &errorMessage))
    {
        QMessageBox::warning(
            this, tr("保存导入方案失败"), errorMessage);
    }
}

void ImageBatchImportDialog::onRemoveSelected()
{
    QSet<int> rowSet;
    for (const QModelIndex& index :
         ui->importQueueTableWidget->selectionModel()->selectedRows())
    {
        rowSet.insert(index.row());
    }
    QList<int> rows = rowSet.values();
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows)
        queueEntries.removeAt(row);
    rebuildTable(rows.isEmpty()
        ? -1
        : (std::min)(
              rows.constLast(), static_cast<int>(queueEntries.size()) - 1));
}

void ImageBatchImportDialog::onMoveUp()
{
    moveCurrentRow(-1);
}

void ImageBatchImportDialog::onMoveDown()
{
    moveCurrentRow(1);
}

void ImageBatchImportDialog::onClearQueue()
{
    queueEntries.clear();
    acceptedFrames.clear();
    rebuildTable();
}

void ImageBatchImportDialog::onRevalidateQueue()
{
    const int selectedRow = ui->importQueueTableWidget->currentRow();
    for (QueueEntry& entry : queueEntries)
        entry.probe = ImageFrameImport::probeFile(entry.filePath);
    rebuildTable(selectedRow);
}

void ImageBatchImportDialog::onApplyTemplate()
{
    int32_t xOffset = 0;
    int32_t yOffset = 0;
    selectedTemplateOffsets(&xOffset, &yOffset);
    for (QueueEntry& entry : queueEntries)
    {
        entry.xOffsetText = QString::number(xOffset);
        entry.yOffsetText = QString::number(yOffset);
    }
    rebuildTable(ui->importQueueTableWidget->currentRow());
}

void ImageBatchImportDialog::onTemplateChanged()
{
    const bool fixed = selectedOffsetTemplate() == OffsetTemplate::Fixed;
    ui->fixedXOffsetLabel->setEnabled(fixed);
    ui->fixedXOffsetSpinBox->setEnabled(fixed);
    ui->fixedYOffsetLabel->setEnabled(fixed);
    ui->fixedYOffsetSpinBox->setEnabled(fixed);
}

void ImageBatchImportDialog::onTableItemChanged(QTableWidgetItem* item)
{
    if (item == nullptr)
        return;
    const int row = item->row();
    const int column = item->column();
    if (rebuildingTable ||
        (column != XOffsetColumn && column != YOffsetColumn))
    {
        return;
    }
    if (row < 0 || row >= queueEntries.size())
        return;
    if (column == XOffsetColumn)
        queueEntries[row].xOffsetText =
            ui->importQueueTableWidget->item(row, column)->text();
    else
        queueEntries[row].yOffsetText =
            ui->importQueueTableWidget->item(row, column)->text();
    updateRowStatus(row);
    updateUiState();
}

void ImageBatchImportDialog::updateUiState()
{
    const QModelIndexList selectedRows =
        ui->importQueueTableWidget->selectionModel()->selectedRows();
    const bool hasSelection = !selectedRows.isEmpty();
    const bool singleSelection = selectedRows.size() == 1;
    const int selectedRow = singleSelection ? selectedRows.constFirst().row() : -1;
    ui->removeFilesButton->setEnabled(hasSelection);
    ui->moveUpButton->setEnabled(singleSelection && selectedRow > 0);
    ui->moveDownButton->setEnabled(
        singleSelection && selectedRow + 1 < queueEntries.size());
    ui->clearQueueButton->setEnabled(!queueEntries.isEmpty());
    ui->revalidateButton->setEnabled(!queueEntries.isEmpty());
    ui->applyTemplateButton->setEnabled(!queueEntries.isEmpty());

    std::vector<ImageFrameImportRequest> recipeRequests;
    int recipeInvalidRow = -1;
    const bool recipeValid = collectRecipeRequests(
        &recipeRequests, &recipeInvalidRow);
    ui->saveRecipeButton->setEnabled(recipeValid);

    std::vector<ImageFrameImportRequest> requests;
    int invalidRow = -1;
    const bool valid = collectRequests(&requests, &invalidRow);
    const int finalFrameCount = existingFrameCount + queueEntries.size();
    if (queueEntries.isEmpty())
    {
        ui->queueSummaryLabel->setText(
            tr("队列为空；请添加 PNG。"));
    }
    else if (finalFrameCount > MaximumImageFrameCount)
    {
        ui->queueSummaryLabel->setText(
            tr("队列：%1 项；导入后将达到 %2 帧，超过 10000 帧上限。")
                .arg(queueEntries.size())
                .arg(finalFrameCount));
    }
    else if (!valid)
    {
        ui->queueSummaryLabel->setText(
            tr("队列：%1 项；第 %2 项无效，请修正或移除。")
                .arg(queueEntries.size())
                .arg(invalidRow + 1));
    }
    else
    {
        ui->queueSummaryLabel->setText(
            tr("队列：%1 项；导入后共 %2 / 10000 帧。")
                .arg(queueEntries.size())
                .arg(finalFrameCount));
    }
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(valid);
}

void ImageBatchImportDialog::rebuildTable(int selectedRow)
{
    rebuildingTable = true;
    const QSignalBlocker blocker(ui->importQueueTableWidget);
    ui->importQueueTableWidget->setRowCount(queueEntries.size());
    for (int row = 0; row < queueEntries.size(); row++)
    {
        const QueueEntry& entry = queueEntries[row];
        const QFileInfo fileInfo(entry.filePath);
        ui->importQueueTableWidget->setItem(
            row, FileNameColumn, readOnlyItem(fileInfo.fileName()));
        ui->importQueueTableWidget->setItem(
            row, FilePathColumn, readOnlyItem(entry.filePath));
        const QString dimensions = entry.probe.isValid()
            ? tr("%1 × %2")
                  .arg(entry.probe.imageSize.width())
                  .arg(entry.probe.imageSize.height())
            : QStringLiteral("—");
        ui->importQueueTableWidget->setItem(
            row, DimensionsColumn, readOnlyItem(dimensions));
        ui->importQueueTableWidget->setItem(
            row,
            XOffsetColumn,
            new QTableWidgetItem(entry.xOffsetText));
        ui->importQueueTableWidget->setItem(
            row,
            YOffsetColumn,
            new QTableWidgetItem(entry.yOffsetText));
        ui->importQueueTableWidget->setItem(
            row, StatusColumn, readOnlyItem(QString()));
        updateRowStatus(row);
    }
    rebuildingTable = false;
    if (selectedRow >= 0 && selectedRow < queueEntries.size())
    {
        ui->importQueueTableWidget->setCurrentCell(
            selectedRow,
            FileNameColumn,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    updateUiState();
}

void ImageBatchImportDialog::updateRowStatus(int row)
{
    if (row < 0 || row >= queueEntries.size())
        return;
    QTableWidgetItem* statusItem =
        ui->importQueueTableWidget->item(row, StatusColumn);
    if (statusItem == nullptr)
        return;
    int32_t unused = 0;
    if (!queueEntries[row].probe.isValid())
    {
        statusItem->setText(importErrorText(queueEntries[row].probe.error));
    }
    else if (!parseOffset(row, XOffsetColumn, &unused) ||
             !parseOffset(row, YOffsetColumn, &unused))
    {
        statusItem->setText(tr("偏移必须是 32 位整数"));
    }
    else
    {
        statusItem->setText(tr("有效"));
    }
}

bool ImageBatchImportDialog::collectRequests(
    std::vector<ImageFrameImportRequest>* requests,
    int* invalidRow) const
{
    if (requests == nullptr)
        return false;
    if (!collectRecipeRequests(requests, invalidRow))
        return false;
    if (existingFrameCount <= 0 ||
        existingFrameCount > MaximumImageFrameCount ||
        queueEntries.size() > MaximumImageFrameCount - existingFrameCount)
    {
        return false;
    }

    return true;
}

bool ImageBatchImportDialog::collectRecipeRequests(
    std::vector<ImageFrameImportRequest>* requests,
    int* invalidRow) const
{
    if (requests == nullptr)
        return false;
    requests->clear();
    if (invalidRow)
        *invalidRow = -1;
    if (queueEntries.isEmpty() ||
        queueEntries.size() > MaximumImageFrameCount)
    {
        return false;
    }

    std::vector<ImageFrameImportRequest> collected;
    collected.reserve(static_cast<size_t>(queueEntries.size()));
    for (int row = 0; row < queueEntries.size(); row++)
    {
        int32_t xOffset = 0;
        int32_t yOffset = 0;
        if (!queueEntries[row].probe.isValid() ||
            !parseOffset(row, XOffsetColumn, &xOffset) ||
            !parseOffset(row, YOffsetColumn, &yOffset))
        {
            if (invalidRow)
                *invalidRow = row;
            return false;
        }
        collected.push_back(
            {queueEntries[row].filePath, xOffset, yOffset});
    }
    *requests = std::move(collected);
    return true;
}

bool ImageBatchImportDialog::parseOffset(
    int row, int column, int32_t* value) const
{
    if (value == nullptr)
        return false;
    if (row < 0 || row >= queueEntries.size())
        return false;
    const QString text = column == XOffsetColumn
        ? queueEntries[row].xOffsetText
        : queueEntries[row].yOffsetText;
    bool converted = false;
    const qlonglong parsed = text.trimmed().toLongLong(&converted);
    if (!converted ||
        parsed < (std::numeric_limits<int32_t>::min)() ||
        parsed > (std::numeric_limits<int32_t>::max)())
    {
        return false;
    }
    *value = static_cast<int32_t>(parsed);
    return true;
}

void ImageBatchImportDialog::moveCurrentRow(int delta)
{
    const QModelIndexList selectedRows =
        ui->importQueueTableWidget->selectionModel()->selectedRows();
    if (selectedRows.size() != 1)
        return;
    const int row = selectedRows.constFirst().row();
    const int destination = row + delta;
    if (destination < 0 || destination >= queueEntries.size())
        return;

    queueEntries.swapItemsAt(row, destination);
    rebuildTable(destination);
}

ImageBatchImportDialog::OffsetTemplate
ImageBatchImportDialog::selectedOffsetTemplate() const
{
    return static_cast<OffsetTemplate>(
        ui->offsetTemplateComboBox->currentData().toInt());
}

void ImageBatchImportDialog::selectedTemplateOffsets(
    int32_t* xOffset, int32_t* yOffset) const
{
    if (xOffset == nullptr || yOffset == nullptr)
        return;
    switch (selectedOffsetTemplate())
    {
    case OffsetTemplate::Zero:
        *xOffset = 0;
        *yOffset = 0;
        return;
    case OffsetTemplate::CurrentFrame:
        *xOffset = currentXOffset;
        *yOffset = currentYOffset;
        return;
    case OffsetTemplate::Fixed:
        *xOffset = ui->fixedXOffsetSpinBox->value();
        *yOffset = ui->fixedYOffsetSpinBox->value();
        return;
    }
}

QString ImageBatchImportDialog::importErrorText(
    ImageFrameImportError error) const
{
    switch (error)
    {
    case ImageFrameImportError::None:
        return tr("有效");
    case ImageFrameImportError::EmptyQueue:
        return tr("队列为空");
    case ImageFrameImportError::InvalidDocumentFrameCount:
        return tr("当前文档帧数无效");
    case ImageFrameImportError::FrameLimitExceeded:
        return tr("导入后超过 10000 帧上限");
    case ImageFrameImportError::EmptyPath:
        return tr("路径为空");
    case ImageFrameImportError::FileMissing:
        return tr("文件不存在");
    case ImageFrameImportError::NotRegularFile:
        return tr("路径不是普通文件");
    case ImageFrameImportError::FileNotReadable:
        return tr("文件不可读");
    case ImageFrameImportError::UnsupportedExtension:
        return tr("扩展名必须是 .png");
    case ImageFrameImportError::UnsupportedFormat:
        return tr("文件内容不是 PNG");
    case ImageFrameImportError::DecodeFailed:
        return tr("PNG 解码失败");
    case ImageFrameImportError::EncodeFailed:
        return tr("PNG 规范编码失败");
    }
    return tr("未知导入错误");
}

QString ImageBatchImportDialog::recipeErrorText(
    const ImageFrameImportRecipeFailure& failure) const
{
    QString message;
    switch (failure.error)
    {
    case ImageFrameImportRecipeError::None:
        message = tr("没有错误");
        break;
    case ImageFrameImportRecipeError::InvalidRecipePath:
        message = tr("方案路径无效或文件不可读");
        break;
    case ImageFrameImportRecipeError::TransactionRecoveryFailed:
        message = tr("无法恢复方案目录中的未完成事务");
        break;
    case ImageFrameImportRecipeError::ReadFailed:
        message = tr("无法读取方案文件");
        break;
    case ImageFrameImportRecipeError::InvalidUtf8:
        message = tr("方案不是严格 UTF-8 文本");
        break;
    case ImageFrameImportRecipeError::InvalidJson:
        message = tr("方案 JSON 结构无效");
        break;
    case ImageFrameImportRecipeError::UnsupportedFormat:
        message = tr("文件不是图片帧导入方案");
        break;
    case ImageFrameImportRecipeError::UnsupportedVersion:
        message = tr("不支持该导入方案版本");
        break;
    case ImageFrameImportRecipeError::InvalidFrames:
        message = tr("方案必须包含 1 到 10000 个帧条目");
        break;
    case ImageFrameImportRecipeError::InvalidFramePath:
        message = tr("方案帧路径必须是相对 PNG 路径");
        break;
    case ImageFrameImportRecipeError::InvalidOffset:
        message = tr("方案帧偏移必须是 32 位整数");
        break;
    case ImageFrameImportRecipeError::FrameFileInvalid:
        message = tr("方案引用的 PNG 当前无效：%1")
            .arg(importErrorText(failure.importError));
        break;
    case ImageFrameImportRecipeError::TransactionFailed:
        message = tr("方案耐久保存失败");
        break;
    }
    if (failure.frameIndex >= 0)
        message += tr("（第 %1 项）").arg(failure.frameIndex + 1);
    if (!failure.detail.isEmpty())
        message += tr("\n%1").arg(failure.detail);
    return message;
}
