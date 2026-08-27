#pragma once

#include "../core/ImageFrameBatchExchange.h"

#include <QDialog>

namespace Ui
{
class ImageBatchExportDialog;
}

class ImageBatchExportDialog : public QDialog
{
    Q_OBJECT

public:
    ImageBatchExportDialog(
        int selectedFrameCount,
        int totalFrameCount,
        const QString& initialDirectory,
        QWidget* parent = nullptr);
    ~ImageBatchExportDialog() override;

    ImageFrameBatchScope scope() const;
    QString outputDirectory() const;

private slots:
    void onBrowseDirectory();
    void updateUiState();

private:
    Ui::ImageBatchExportDialog* ui;
    int selectedFrameCount;
    int totalFrameCount;
};
