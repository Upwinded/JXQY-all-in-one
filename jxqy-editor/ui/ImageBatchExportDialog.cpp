#include "ImageBatchExportDialog.h"
#include "ui_ImageBatchExportDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QPushButton>

ImageBatchExportDialog::ImageBatchExportDialog(
    int selectedFrameCountValue,
    int totalFrameCountValue,
    const QString& initialDirectory,
    QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::ImageBatchExportDialog)
    , selectedFrameCount(selectedFrameCountValue)
    , totalFrameCount(totalFrameCountValue)
{
    ui->setupUi(this);
    ui->exportScopeComboBox->addItem(
        tr("所选帧（%1 帧）").arg(selectedFrameCount),
        static_cast<int>(ImageFrameBatchScope::Selected));
    ui->exportScopeComboBox->addItem(
        tr("全部帧（%1 帧）").arg(totalFrameCount),
        static_cast<int>(ImageFrameBatchScope::All));
    if (selectedFrameCount <= 0)
        ui->exportScopeComboBox->setCurrentIndex(1);
    ui->outputDirectoryLineEdit->setText(initialDirectory.isEmpty()
        ? QString()
        : QDir::cleanPath(QFileInfo(initialDirectory).absoluteFilePath()));

    connect(ui->browseDirectoryButton, &QPushButton::clicked,
        this, &ImageBatchExportDialog::onBrowseDirectory);
    connect(ui->exportScopeComboBox,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ImageBatchExportDialog::updateUiState);
    connect(ui->outputDirectoryLineEdit, &QLineEdit::textChanged,
        this, &ImageBatchExportDialog::updateUiState);
    connect(ui->buttonBox, &QDialogButtonBox::accepted,
        this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected,
        this, &QDialog::reject);
    updateUiState();
}

ImageBatchExportDialog::~ImageBatchExportDialog()
{
    delete ui;
}

ImageFrameBatchScope ImageBatchExportDialog::scope() const
{
    return static_cast<ImageFrameBatchScope>(
        ui->exportScopeComboBox->currentData().toInt());
}

QString ImageBatchExportDialog::outputDirectory() const
{
    const QString value = ui->outputDirectoryLineEdit->text().trimmed();
    if (value.isEmpty())
        return QString();
    return QDir::cleanPath(
        QFileInfo(value).absoluteFilePath());
}

void ImageBatchExportDialog::onBrowseDirectory()
{
    QFileDialog fileDialog(
        nullptr,
        tr("选择批量导出目录"),
        outputDirectory());
    fileDialog.setAcceptMode(QFileDialog::AcceptOpen);
    fileDialog.setFileMode(QFileDialog::Directory);
    fileDialog.setOption(QFileDialog::ShowDirsOnly);
    fileDialog.setOption(QFileDialog::DontUseNativeDialog);
    if (fileDialog.exec() != QDialog::Accepted ||
        fileDialog.selectedFiles().isEmpty())
    {
        return;
    }
    ui->outputDirectoryLineEdit->setText(
        QDir::cleanPath(fileDialog.selectedFiles().constFirst()));
}

void ImageBatchExportDialog::updateUiState()
{
    const bool selectedScopeValid =
        scope() != ImageFrameBatchScope::Selected || selectedFrameCount > 0;
    const QFileInfo directoryInfo(outputDirectory());
    const bool directoryValid = directoryInfo.exists() &&
                                directoryInfo.isDir() &&
                                directoryInfo.isReadable() &&
                                directoryInfo.isWritable();
    const int exportCount = scope() == ImageFrameBatchScope::All
        ? totalFrameCount : selectedFrameCount;
    ui->summaryLabel->setText(
        directoryValid
            ? tr("将导出 %1 帧，并写入 jxqy-image-frames.json。").arg(exportCount)
            : tr("请选择一个现有且可写的目录。"));
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(
        selectedScopeValid && totalFrameCount > 0 && directoryValid);
}
