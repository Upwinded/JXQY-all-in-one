#pragma once

#include "../core/ImageFrameImport.h"
#include "../core/ImageFrameImportRecipe.h"

#include <QDialog>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <vector>

namespace Ui
{
class ImageBatchImportDialog;
}

class QTableWidgetItem;

enum class ImageBatchImportInsertionMode
{
    BeforeCurrent,
    AfterCurrent,
    DocumentEnd
};

class ImageBatchImportDialog : public QDialog
{
    Q_OBJECT

public:
    ImageBatchImportDialog(
        int existingFrameCount,
        int currentFrameIndex,
        int32_t currentXOffset,
        int32_t currentYOffset,
        ImageBatchImportInsertionMode defaultInsertionMode,
        const QString& initialDirectory,
        QWidget* parent = nullptr);
    ~ImageBatchImportDialog() override;

    void enqueueFiles(const QStringList& filePaths);
    const std::vector<ImageFrameData>& preparedFrames() const;
    ImageBatchImportInsertionMode insertionMode() const;
    int insertionIndex() const;
    bool loadRecipeFile(
        const QString& recipeFilePath,
        QString* errorMessage = nullptr);
    bool saveRecipeFile(
        const QString& recipeFilePath,
        QString* errorMessage = nullptr);

public slots:
    void accept() override;

private slots:
    void onAddFiles();
    void onLoadRecipe();
    void onSaveRecipe();
    void onRemoveSelected();
    void onMoveUp();
    void onMoveDown();
    void onClearQueue();
    void onRevalidateQueue();
    void onApplyTemplate();
    void onTemplateChanged();
    void onTableItemChanged(QTableWidgetItem* item);
    void updateUiState();

private:
    enum class OffsetTemplate
    {
        Zero,
        CurrentFrame,
        Fixed
    };

    struct QueueEntry
    {
        QString filePath;
        ImageFrameImportProbe probe;
        QString xOffsetText = QStringLiteral("0");
        QString yOffsetText = QStringLiteral("0");
    };

    void rebuildTable(int selectedRow = -1);
    void updateRowStatus(int row);
    bool collectRequests(
        std::vector<ImageFrameImportRequest>* requests,
        int* invalidRow = nullptr) const;
    bool collectRecipeRequests(
        std::vector<ImageFrameImportRequest>* requests,
        int* invalidRow = nullptr) const;
    bool parseOffset(int row, int column, int32_t* value) const;
    void moveCurrentRow(int delta);
    OffsetTemplate selectedOffsetTemplate() const;
    void selectedTemplateOffsets(int32_t* xOffset, int32_t* yOffset) const;
    QString importErrorText(ImageFrameImportError error) const;
    QString recipeErrorText(
        const ImageFrameImportRecipeFailure& failure) const;

    Ui::ImageBatchImportDialog* ui;
    QVector<QueueEntry> queueEntries;
    std::vector<ImageFrameData> acceptedFrames;
    int existingFrameCount;
    int currentFrameIndex;
    int32_t currentXOffset;
    int32_t currentYOffset;
    QString fileDialogDirectory;
    bool rebuildingTable = false;
};
