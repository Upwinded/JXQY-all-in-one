#pragma once

#include <QWidget>
#include "AssetsPathSwitchParticipant.h"
#include "CloseTransactionParticipant.h"
#include <QPaintEvent>
#include <QElapsedTimer>
#include <QTimer>
#include <QScrollArea>
#include <QMenu>
#include <QAction>
#include <QCloseEvent>
#include <QList>
#include <QSet>

#include <functional>

#include "../core/IMPImageFile.h"
#include "../core/ImageFrameTransform.h"
#include "../core/PicFileEditor.h"
#include "../core/ProjectDocumentRegistry.h"

namespace Ui
{
class ImageEditorWindow;
}

class ImageListCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit ImageListCanvas(QWidget* parent = nullptr);

    void setFrames(const QVector<QImage>& images);
    void relayout();
    void setSelectedIndex(int index);
    void setSelectedIndices(const QList<int>& indices, int currentIndex);
    int getSelectedIndex() const;
    QList<int> getSelectedIndices() const;
    void selectFrame(int index, Qt::KeyboardModifiers modifiers);
    void selectFrameForContextMenu(int index);
    void selectAllFrames();
    int frameAtPos(const QPoint& pos) const;

    QSize sizeHint() const override;
    void setScrollArea(QScrollArea* area);

signals:
    void frameClicked(int index);
    void frameRightClicked(int index, const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QVector<QImage> frameImages;
    QSet<int> selectedIndices;
    int currentIndex = -1;
    int selectionAnchor = -1;
    QScrollArea* scrollArea = nullptr;

    static const int CELL_WIDTH = 150;
    static const int CELL_HEIGHT = 150;
    static const int TITLE_HEIGHT = 18;

    int currentCols() const;
    void emitCurrentSelection();
};

class QUndoStack;
class QToolButton;
enum class ImageBatchImportInsertionMode;
struct ImageFrameBatchDocument;
struct ImageFrameBatchFailure;

class ImageEditorWindow : public QWidget,
                          public AssetsPathSwitchParticipant,
                          public CloseTransactionParticipant
{
    Q_OBJECT

public:
    explicit ImageEditorWindow(QWidget* parent = nullptr);
    ~ImageEditorWindow();

    bool openFile(const QString& fileName);
    bool saveFileAs(const QString& fileName);
    bool setAssetsBasePath(const QString& path);
    QString currentFilePath() const;
    QList<ProjectDocumentState> currentProjectDocuments() const;

    using DocumentPathValidator =
        std::function<bool(const QString&, const QString&)>;
    void setDocumentPathValidator(DocumentPathValidator validator);

    Decision prepareAssetsPathSwitch(const QString& path) const override;
    bool resolveAssetsPathSwitch(Decision decision) override;
    void commitAssetsPathSwitch(const QString& path) override;
    QString currentAssetsPath() const override;

    ClosePlan prepareCloseTransaction() const override;
    bool resolveCloseTransaction(const ClosePlan& plan) override;
    void commitCloseTransaction(const ClosePlan& plan) override;

    enum class SaveConfirmResult { Saved, Discarded, Cancelled };
    SaveConfirmResult confirmSaveIfModified();

    enum class BatchReimportResult { Failed, Unchanged, Applied };
    BatchReimportResult reimportFrameBatchFromManifest(
        const QString& manifestPath,
        QString* errorMessage = nullptr);

signals:
    void documentStatesChanged();
    void documentClosed();

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onOpenFile();
    void onSaveFile();
    void onSaveAs();
    void onAddFrame();
    void onInsertFrame();
    void onDeleteFrame();
    void onEditFrame();
    void onExportFrame();
    void onImportFrame();
    void onBatchExportFrames();
    void onReimportFrameBatch();
    void onIntervalChanged(int value);
    void onDirectionChanged(int value);
    void onFrameClicked(int index);
    void onFrameRightClicked(int index, const QPoint& globalPos);
    void onApplyBatchOffsets();
    void onCopyFrames();
    void onPasteFrames();
    void onDuplicateFrames();
    void onMoveFramesEarlier();
    void onMoveFramesLater();
    void onRotateFramesLeft();
    void onRotateFramesRight();
    void onFlipFramesHorizontally();
    void onFlipFramesVertically();
    void onAnimationPlaybackToggled();
    void onAnimationPlaybackReset();
    void onAnimationPreviewDirectionChanged(int direction);
    void onAnimationPlaybackTimeout();

private:
    void updateCanvas();
    void updateWindowTitle();
    void updateFrameInfo();
    void retranslateDynamicUi();
    void loadFromPicEditor();
    QString normalizedSavePath(const QString& fileName,
                               bool replaceExtension) const;
    bool saveToNormalizedPath(const QString& targetPath);
    bool canAdoptDocumentPath(const QString& targetPath) const;
    void notifyDocumentStateChanged();
    int frameCount() const;
    bool frameOffset(int frameIndex, int32_t* xOffset, int32_t* yOffset) const;
    void applyFrameOffsetChanges(
        const std::vector<ImageFrameOffsetChange>& changes,
        bool useNewValues);
    void updateBatchOffsetControls();
    void updateFrameStructureControls();
    void updateCanvasTransformControls();
    void resetAnimationPlayback(bool preservePlaying = false);
    void resetAnimationPlaybackForOpenedDocument();
    void updateAnimationPlaybackControls();
    void updateAnimationPreviewFrame();
    int animationDirectionCount() const;
    int animationInterval() const;
    QImage animationFrameImage(int frameIndex) const;
    std::vector<int> selectedFrameIndices() const;
    bool currentFrameSequence(std::vector<ImageFrameData>* frames) const;
    bool currentBatchDocument(ImageFrameBatchDocument* document) const;
    QString imageFrameBatchErrorText(
        const ImageFrameBatchFailure& failure) const;
    QString imageFrameTransformErrorText(
        const ImageFrameTransformFailure& failure) const;
    bool replaceFrameSequence(
        const std::vector<ImageFrameData>& frames,
        const std::vector<int>& selectedIndices,
        int selectedCurrentIndex);
    void pushFrameSequenceEdit(
        const std::vector<ImageFrameData>& beforeFrames,
        const std::vector<int>& beforeSelection,
        int beforeCurrentIndex,
        ImageFrameSequenceEdit edit,
        const QString& commandText);
    void applySelectedFrameTransform(
        ImageFrameTransformOperation operation,
        const QString& commandText);
    void openBatchImportDialog(
        ImageBatchImportInsertionMode defaultInsertionMode);
    void markModifiedOutsideImageHistory();
    void resetDocumentEditState();
    void synchronizeModifiedStateFromUndoStack();
    void setAdvancedFrameToolsVisible(bool visible);

    Ui::ImageEditorWindow* ui;
    ImageListCanvas* canvas;
    QScrollArea* scrollArea;

    IMPImageFile impFile;
    PicFileEditor picEditor;
    std::string currentFileName;
    PicType currentPicType = PicType::None;
    int currentFrameIndex = -1;
    bool isModified = false;

    QMenu* contextMenu;
    QAction* editAction;
    QAction* deleteAction;
    QAction* insertAction;
    QAction* addAction;
    QAction* exportAction;
    QAction* importAction;
    QAction* batchExportAction;
    QAction* reimportBatchAction;
    QAction* batchOffsetAction;
    QAction* copyFramesAction;
    QAction* pasteFramesAction;
    QAction* duplicateFramesAction;
    QAction* moveFramesEarlierAction;
    QAction* moveFramesLaterAction;
    QAction* undoShortcutAction;
    QAction* redoShortcutAction;
    QToolButton* advancedFrameToolsButton = nullptr;

    QUndoStack* imageUndoStack;
    bool imageHistoryBaseModified = false;
    bool suppressUndoStateSynchronization = false;
    std::vector<ImageFrameData> frameClipboard;

    QTimer animationPlaybackTimer;
    QElapsedTimer animationPlaybackClock;
    qint64 animationAccumulatedMilliseconds = 0;
    bool animationPlaying = false;

    QString assetsBasePath;
    DocumentPathValidator documentPathValidator;
};
