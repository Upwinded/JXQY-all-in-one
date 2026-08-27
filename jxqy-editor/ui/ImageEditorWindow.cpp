#include "ImageEditorWindow.h"
#include "../core/AuthoringMutationGate.h"
#include "ui_ImageEditorWindow.h"
#include "ImageBatchExportDialog.h"
#include "ImageBatchImportDialog.h"
#include "ImageEditDialog.h"
#include "../core/DurableFileTransaction.h"
#include "../core/EditorAssetPath.h"
#include "../core/ImageFrameBatchExchange.h"
#include "Image/ImageAnimationPlayback.h"

#include <QPainter>
#include <QMessageBox>
#include <QBuffer>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QUndoCommand>
#include <QUndoStack>
#include <QToolButton>

#include <limits>
#include <utility>

namespace {

QByteArray imageToPngByteArray(const QImage& image)
{
    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return byteArray;
}

class ImageFrameOffsetBatchCommand final : public QUndoCommand
{
public:
    using ApplyCallback = std::function<void(
        const std::vector<ImageFrameOffsetChange>&, bool)>;

    ImageFrameOffsetBatchCommand(
        std::vector<ImageFrameOffsetChange> changes,
        ApplyCallback applyCallback,
        const QString& text)
        : frameChanges(std::move(changes))
        , apply(std::move(applyCallback))
    {
        setText(text);
    }

    void undo() override
    {
        apply(frameChanges, false);
    }

    void redo() override
    {
        apply(frameChanges, true);
    }

private:
    std::vector<ImageFrameOffsetChange> frameChanges;
    ApplyCallback apply;
};

class ImageFrameSequenceCommand final : public QUndoCommand
{
public:
    using ApplyCallback = std::function<void(
        const std::vector<ImageFrameData>&,
        const std::vector<int>&,
        int)>;

    ImageFrameSequenceCommand(
        std::vector<ImageFrameData> beforeFrames,
        std::vector<int> beforeSelection,
        int beforeCurrentIndex,
        ImageFrameSequenceEdit afterEdit,
        ApplyCallback applyCallback,
        const QString& text)
        : framesBefore(std::move(beforeFrames))
        , selectionBefore(std::move(beforeSelection))
        , currentBefore(beforeCurrentIndex)
        , framesAfter(std::move(afterEdit.frames))
        , selectionAfter(std::move(afterEdit.selectedIndices))
        , currentAfter(afterEdit.currentIndex)
        , apply(std::move(applyCallback))
    {
        setText(text);
    }

    void undo() override
    {
        apply(framesBefore, selectionBefore, currentBefore);
    }

    void redo() override
    {
        apply(framesAfter, selectionAfter, currentAfter);
    }

private:
    std::vector<ImageFrameData> framesBefore;
    std::vector<int> selectionBefore;
    int currentBefore = -1;
    std::vector<ImageFrameData> framesAfter;
    std::vector<int> selectionAfter;
    int currentAfter = -1;
    ApplyCallback apply;
};

} // anonymous namespace

// ========== ImageListCanvas 实现 ==========

ImageListCanvas::ImageListCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void ImageListCanvas::setScrollArea(QScrollArea* area)
{
    scrollArea = area;
}

int ImageListCanvas::currentCols() const
{
    int cols = 1;
    if (scrollArea && scrollArea->viewport())
    {
        int viewWidth = scrollArea->viewport()->width();
        cols = viewWidth / CELL_WIDTH;
    }
    if (cols < 1) cols = 1;
    if (!frameImages.isEmpty() && cols > frameImages.size())
        cols = frameImages.size();
    return cols;
}

void ImageListCanvas::setFrames(const QVector<QImage>& images)
{
    frameImages = images;
    for (auto index = selectedIndices.begin(); index != selectedIndices.end();)
    {
        if (*index < 0 || *index >= frameImages.size())
            index = selectedIndices.erase(index);
        else
            ++index;
    }
    if (currentIndex < 0 || currentIndex >= frameImages.size())
        currentIndex = selectedIndices.isEmpty()
            ? -1 : getSelectedIndices().constFirst();
    if (currentIndex >= 0)
        selectedIndices.insert(currentIndex);
    if (selectionAnchor < 0 || selectionAnchor >= frameImages.size())
        selectionAnchor = currentIndex;

    relayout();
}

void ImageListCanvas::relayout()
{
    int cols = currentCols();
    int rows = (frameImages.size() + cols - 1) / cols;
    if (rows < 1) rows = 1;
    int h = rows * CELL_HEIGHT + TITLE_HEIGHT;
    int w = cols * CELL_WIDTH;
    setMinimumSize(w, h);
    setMaximumHeight(h);
    update();
}

void ImageListCanvas::setSelectedIndex(int index)
{
    selectedIndices.clear();
    if (index >= 0 && index < frameImages.size())
    {
        selectedIndices.insert(index);
        currentIndex = index;
        selectionAnchor = index;
    }
    else
    {
        currentIndex = -1;
        selectionAnchor = -1;
    }
    update();
}

void ImageListCanvas::setSelectedIndices(
    const QList<int>& indices, int selectedCurrentIndex)
{
    QSet<int> replacement;
    for (int index : indices)
    {
        if (index >= 0 && index < frameImages.size())
            replacement.insert(index);
    }
    if (selectedCurrentIndex < 0 ||
        selectedCurrentIndex >= frameImages.size() ||
        !replacement.contains(selectedCurrentIndex))
    {
        setSelectedIndex(-1);
        return;
    }

    selectedIndices = std::move(replacement);
    currentIndex = selectedCurrentIndex;
    selectionAnchor = selectedCurrentIndex;
    update();
}

int ImageListCanvas::getSelectedIndex() const
{
    return currentIndex;
}

QList<int> ImageListCanvas::getSelectedIndices() const
{
    QList<int> indices = selectedIndices.values();
    std::sort(indices.begin(), indices.end());
    return indices;
}

void ImageListCanvas::selectFrame(
    int index, Qt::KeyboardModifiers modifiers)
{
    if (index < 0 || index >= frameImages.size())
        return;

    const bool extendRange = modifiers.testFlag(Qt::ShiftModifier);
    const bool toggleOrUnion = modifiers.testFlag(Qt::ControlModifier);
    if (extendRange)
    {
        const int anchor = selectionAnchor >= 0 &&
                selectionAnchor < frameImages.size()
            ? selectionAnchor : index;
        if (!toggleOrUnion)
            selectedIndices.clear();
        const int first = qMin(anchor, index);
        const int last = qMax(anchor, index);
        for (int frame = first; frame <= last; frame++)
            selectedIndices.insert(frame);
        currentIndex = index;
    }
    else if (toggleOrUnion)
    {
        if (selectedIndices.contains(index))
        {
            selectedIndices.remove(index);
            if (currentIndex == index)
            {
                const QList<int> remaining = getSelectedIndices();
                currentIndex = remaining.isEmpty() ? -1 : remaining.constFirst();
            }
        }
        else
        {
            selectedIndices.insert(index);
            currentIndex = index;
        }
        selectionAnchor = index;
    }
    else
    {
        selectedIndices = {index};
        currentIndex = index;
        selectionAnchor = index;
    }

    update();
    emitCurrentSelection();
}

void ImageListCanvas::selectFrameForContextMenu(int index)
{
    if (index < 0 || index >= frameImages.size())
        return;
    if (!selectedIndices.contains(index))
    {
        selectedIndices = {index};
        selectionAnchor = index;
    }
    currentIndex = index;
    update();
    emitCurrentSelection();
}

void ImageListCanvas::selectAllFrames()
{
    selectedIndices.clear();
    for (int index = 0; index < frameImages.size(); index++)
        selectedIndices.insert(index);
    if (!frameImages.isEmpty())
    {
        if (currentIndex < 0 || currentIndex >= frameImages.size())
            currentIndex = 0;
        selectionAnchor = currentIndex;
    }
    else
    {
        currentIndex = -1;
        selectionAnchor = -1;
    }
    update();
    emitCurrentSelection();
}

void ImageListCanvas::emitCurrentSelection()
{
    emit frameClicked(currentIndex);
}

int ImageListCanvas::frameAtPos(const QPoint& pos) const
{
    int cols = currentCols();

    // 顶部 TITLE_HEIGHT 像素是标题留白区，不属于任何单元格；
    // 不能用 (pos.y() - TITLE_HEIGHT) / CELL_HEIGHT 的截断除法，
    // 否则该区域的负值会被 C++ 截断为 0，误选中第一行。
    if (pos.y() < TITLE_HEIGHT)
        return -1;

    int col = pos.x() / CELL_WIDTH;
    int row = (pos.y() - TITLE_HEIGHT) / CELL_HEIGHT;
    if (col < 0 || col >= cols || row < 0)
        return -1;
    int index = row * cols + col;
    if (index >= frameImages.size())
        return -1;
    return index;
}

QSize ImageListCanvas::sizeHint() const
{
    int cols = currentCols();
    int rows = (frameImages.size() + cols - 1) / cols;
    if (rows < 1) rows = 1;
    return QSize(cols * CELL_WIDTH, rows * CELL_HEIGHT + TITLE_HEIGHT);
}

void ImageListCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    int cols = currentCols();

    QPainter painter(this);
    painter.fillRect(rect(), QColor(240, 240, 240));

    for (int i = 0; i < frameImages.size(); i++)
    {
        int col = i % cols;
        int row = i / cols;
        int x = col * CELL_WIDTH;
        int y = row * CELL_HEIGHT + TITLE_HEIGHT;

        // 绘制单元格边框
        QRect cellRect(x + 1, y + 1, CELL_WIDTH - 2, CELL_HEIGHT - 2);

        if (i == currentIndex)
        {
            painter.setPen(QPen(QColor(0, 120, 215), 2));
            painter.setBrush(QColor(230, 240, 255));
            painter.drawRect(cellRect);
        }
        else if (selectedIndices.contains(i))
        {
            painter.setPen(QPen(QColor(0, 120, 215), 1));
            painter.setBrush(QColor(238, 246, 255));
            painter.drawRect(cellRect);
        }
        else
        {
            painter.setPen(QPen(QColor(200, 200, 200), 1));
            painter.setBrush(Qt::white);
            painter.drawRect(cellRect);
        }

        // 绘制缩略图
        const QImage& img = frameImages[i];
        if (!img.isNull())
        {
            QImage scaled = img.scaled(CELL_WIDTH - 10, CELL_HEIGHT - 10,
                Qt::KeepAspectRatio, Qt::SmoothTransformation);
            int sx = x + (CELL_WIDTH - scaled.width()) / 2;
            int sy = y + (CELL_HEIGHT - scaled.height()) / 2;
            painter.drawImage(sx, sy, scaled);
        }

        // 绘制帧编号徽标：必须绘制在自身单元格左上角内部，
        // 不能落到上一排单元格底部。绘制在缩略图之上以保证可读。
        QFont badgeFont("Arial", 9);
        badgeFont.setBold(true);
        QFontMetrics badgeMetrics(badgeFont);
        QString numberText = QString::number(i);
        int badgePad = 4;
        int badgeW = badgeMetrics.horizontalAdvance(numberText) + badgePad * 2;
        int badgeH = badgeMetrics.height() + 2;
        QRectF badgeRect(x + 4, y + 4, badgeW, badgeH);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 170));
        painter.drawRoundedRect(badgeRect, 3, 3);

        painter.setFont(badgeFont);
        painter.setPen(QColor(255, 255, 255));
        painter.drawText(badgeRect, Qt::AlignCenter, numberText);
    }
}

void ImageListCanvas::mousePressEvent(QMouseEvent* event)
{
    int index = frameAtPos(event->pos());
    if (index >= 0)
    {
        if (event->button() == Qt::LeftButton)
        {
            setFocus(Qt::MouseFocusReason);
            selectFrame(index, event->modifiers());
        }
        else if (event->button() == Qt::RightButton)
        {
            emit frameRightClicked(index, event->globalPosition().toPoint());
        }
    }
}

void ImageListCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::SelectAll))
    {
        selectAllFrames();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

// ========== ImageEditorWindow 实现 ==========

ImageEditorWindow::ImageEditorWindow(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::ImageEditorWindow)
{
    ui->setupUi(this);

    advancedFrameToolsButton = new QToolButton(this);
    advancedFrameToolsButton->setObjectName(
        QStringLiteral("advancedFrameToolsButton"));
    advancedFrameToolsButton->setText(tr("高级帧工具"));
    advancedFrameToolsButton->setCheckable(true);
    advancedFrameToolsButton->setArrowType(Qt::RightArrow);
    advancedFrameToolsButton->setToolButtonStyle(
        Qt::ToolButtonTextBesideIcon);
    ui->toolbarLayout->addWidget(advancedFrameToolsButton);
    connect(
        advancedFrameToolsButton,
        &QToolButton::toggled,
        this,
        &ImageEditorWindow::setAdvancedFrameToolsVisible);
    setAdvancedFrameToolsVisible(false);

    imageUndoStack = new QUndoStack(this);
    imageUndoStack->setObjectName(QStringLiteral("imageEditUndoStack"));
    connect(imageUndoStack, &QUndoStack::indexChanged,
        this, [this]()
        {
            updateFrameInfo();
            synchronizeModifiedStateFromUndoStack();
        });
    connect(imageUndoStack, &QUndoStack::cleanChanged,
        this, [this](bool)
        {
            synchronizeModifiedStateFromUndoStack();
        });
    connect(imageUndoStack, &QUndoStack::canUndoChanged,
        ui->undoButton, &QPushButton::setEnabled);
    connect(imageUndoStack, &QUndoStack::canRedoChanged,
        ui->redoButton, &QPushButton::setEnabled);
    connect(ui->undoButton, &QPushButton::clicked,
        imageUndoStack, &QUndoStack::undo);
    connect(ui->redoButton, &QPushButton::clicked,
        imageUndoStack, &QUndoStack::redo);

    undoShortcutAction = new QAction(tr("撤销图片编辑"), this);
    undoShortcutAction->setObjectName(
        QStringLiteral("imageFrameOffsetUndoAction"));
    undoShortcutAction->setShortcut(QKeySequence::Undo);
    undoShortcutAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(undoShortcutAction, &QAction::triggered,
        imageUndoStack, &QUndoStack::undo);
    QWidget::addAction(undoShortcutAction);

    redoShortcutAction = new QAction(tr("重做图片编辑"), this);
    redoShortcutAction->setObjectName(
        QStringLiteral("imageFrameOffsetRedoAction"));
    redoShortcutAction->setShortcut(QKeySequence::Redo);
    redoShortcutAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(redoShortcutAction, &QAction::triggered,
        imageUndoStack, &QUndoStack::redo);
    QWidget::addAction(redoShortcutAction);

    canvas = new ImageListCanvas(this);
    connect(canvas, &ImageListCanvas::frameClicked, this, &ImageEditorWindow::onFrameClicked);
    connect(canvas, &ImageListCanvas::frameRightClicked, this, &ImageEditorWindow::onFrameRightClicked);

    scrollArea = new QScrollArea(this);
    scrollArea->setWidget(canvas);
    scrollArea->setWidgetResizable(true);
    scrollArea->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    canvas->setScrollArea(scrollArea);

    connect(scrollArea, &QScrollArea::customContextMenuRequested, this, [](const QPoint&){});
    // 当 viewport 尺寸变化时重新布局帧列表
    scrollArea->viewport()->installEventFilter(this);

    // 用滚动区域替换占位布局
    if (ui->canvasContainerLayout)
    {
        ui->canvasContainerLayout->addWidget(scrollArea);
    }

    // 创建右键菜单
    contextMenu = new QMenu(this);
    editAction = contextMenu->addAction(tr("编辑图片"));
    editAction->setObjectName(QStringLiteral("imageEditFrameAction"));
    exportAction = contextMenu->addAction(tr("导出图片"));
    exportAction->setObjectName(QStringLiteral("imageExportFrameAction"));
    importAction = contextMenu->addAction(tr("替换图片"));
    importAction->setObjectName(QStringLiteral("imageImportFrameAction"));
    batchExportAction = contextMenu->addAction(tr("批量导出"));
    batchExportAction->setObjectName(
        QStringLiteral("imageBatchExportAction"));
    reimportBatchAction = contextMenu->addAction(tr("按清单回导"));
    reimportBatchAction->setObjectName(
        QStringLiteral("imageBatchReimportAction"));
    batchOffsetAction = contextMenu->addAction(tr("批量设置所选帧偏移"));
    batchOffsetAction->setObjectName(
        QStringLiteral("imageBatchOffsetAction"));
    copyFramesAction = contextMenu->addAction(tr("复制所选帧"));
    copyFramesAction->setObjectName(QStringLiteral("imageCopyFramesAction"));
    pasteFramesAction = contextMenu->addAction(tr("粘贴到当前帧之后"));
    pasteFramesAction->setObjectName(QStringLiteral("imagePasteFramesAction"));
    duplicateFramesAction = contextMenu->addAction(tr("重复所选帧"));
    duplicateFramesAction->setObjectName(
        QStringLiteral("imageDuplicateFramesAction"));
    moveFramesEarlierAction = contextMenu->addAction(tr("所选帧前移"));
    moveFramesEarlierAction->setObjectName(
        QStringLiteral("imageMoveFramesEarlierAction"));
    moveFramesLaterAction = contextMenu->addAction(tr("所选帧后移"));
    moveFramesLaterAction->setObjectName(
        QStringLiteral("imageMoveFramesLaterAction"));
    contextMenu->addSeparator();
    insertAction = contextMenu->addAction(tr("批量插入图片"));
    insertAction->setObjectName(QStringLiteral("imageInsertFrameAction"));
    addAction = contextMenu->addAction(tr("批量导入到末尾"));
    addAction->setObjectName(QStringLiteral("imageAddFrameAction"));
    contextMenu->addSeparator();
    deleteAction = contextMenu->addAction(tr("删除图片"));
    deleteAction->setObjectName(QStringLiteral("imageDeleteFrameAction"));

    connect(editAction, &QAction::triggered, this, &ImageEditorWindow::onEditFrame);
    connect(exportAction, &QAction::triggered, this, &ImageEditorWindow::onExportFrame);
    connect(importAction, &QAction::triggered, this, &ImageEditorWindow::onImportFrame);
    connect(batchExportAction, &QAction::triggered,
        this, &ImageEditorWindow::onBatchExportFrames);
    connect(reimportBatchAction, &QAction::triggered,
        this, &ImageEditorWindow::onReimportFrameBatch);
    connect(batchOffsetAction, &QAction::triggered,
        this, &ImageEditorWindow::onApplyBatchOffsets);
    connect(copyFramesAction, &QAction::triggered,
        this, &ImageEditorWindow::onCopyFrames);
    connect(pasteFramesAction, &QAction::triggered,
        this, &ImageEditorWindow::onPasteFrames);
    connect(duplicateFramesAction, &QAction::triggered,
        this, &ImageEditorWindow::onDuplicateFrames);
    connect(moveFramesEarlierAction, &QAction::triggered,
        this, &ImageEditorWindow::onMoveFramesEarlier);
    connect(moveFramesLaterAction, &QAction::triggered,
        this, &ImageEditorWindow::onMoveFramesLater);
    connect(insertAction, &QAction::triggered, this, &ImageEditorWindow::onInsertFrame);
    connect(addAction, &QAction::triggered, this, &ImageEditorWindow::onAddFrame);
    connect(deleteAction, &QAction::triggered, this, &ImageEditorWindow::onDeleteFrame);

    connect(ui->openButton, &QPushButton::clicked, this, &ImageEditorWindow::onOpenFile);
    connect(ui->saveButton, &QPushButton::clicked, this, &ImageEditorWindow::onSaveFile);
    connect(ui->saveAsButton, &QPushButton::clicked, this, &ImageEditorWindow::onSaveAs);
    connect(ui->addFrameButton, &QPushButton::clicked, this, &ImageEditorWindow::onAddFrame);
    connect(ui->deleteFrameButton, &QPushButton::clicked, this, &ImageEditorWindow::onDeleteFrame);
    connect(ui->importFrameButton, &QPushButton::clicked, this, &ImageEditorWindow::onImportFrame);
    connect(ui->exportFrameButton, &QPushButton::clicked, this, &ImageEditorWindow::onExportFrame);
    connect(ui->batchExportButton, &QPushButton::clicked,
        this, &ImageEditorWindow::onBatchExportFrames);
    connect(ui->reimportBatchButton, &QPushButton::clicked,
        this, &ImageEditorWindow::onReimportFrameBatch);
    connect(ui->copyFramesButton, &QPushButton::clicked,
        this, &ImageEditorWindow::onCopyFrames);
    connect(ui->pasteFramesButton, &QPushButton::clicked,
        this, &ImageEditorWindow::onPasteFrames);
    connect(ui->duplicateFramesButton, &QPushButton::clicked,
        this, &ImageEditorWindow::onDuplicateFrames);
    connect(ui->moveFramesEarlierButton, &QPushButton::clicked,
        this, &ImageEditorWindow::onMoveFramesEarlier);
    connect(ui->moveFramesLaterButton, &QPushButton::clicked,
        this, &ImageEditorWindow::onMoveFramesLater);
    connect(ui->rotateFramesLeftButton, &QPushButton::clicked,
        this, &ImageEditorWindow::onRotateFramesLeft);
    connect(ui->rotateFramesRightButton, &QPushButton::clicked,
        this, &ImageEditorWindow::onRotateFramesRight);
    connect(ui->flipFramesHorizontallyButton, &QPushButton::clicked,
        this, &ImageEditorWindow::onFlipFramesHorizontally);
    connect(ui->flipFramesVerticallyButton, &QPushButton::clicked,
        this, &ImageEditorWindow::onFlipFramesVertically);
    connect(ui->intervalSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &ImageEditorWindow::onIntervalChanged);
    connect(ui->directionSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &ImageEditorWindow::onDirectionChanged);
    connect(ui->animationPlaybackButton, &QPushButton::clicked,
        this, &ImageEditorWindow::onAnimationPlaybackToggled);
    connect(ui->animationResetButton, &QPushButton::clicked,
        this, &ImageEditorWindow::onAnimationPlaybackReset);
    connect(ui->animationPreviewDirectionSpinBox,
        QOverload<int>::of(&QSpinBox::valueChanged),
        this, &ImageEditorWindow::onAnimationPreviewDirectionChanged);
    animationPlaybackTimer.setInterval(
        ImageAnimationPlayback::LegacyZeroIntervalMilliseconds);
    animationPlaybackTimer.setTimerType(Qt::PreciseTimer);
    connect(&animationPlaybackTimer, &QTimer::timeout,
        this, &ImageEditorWindow::onAnimationPlaybackTimeout);

    ui->batchXOffsetSpinBox->setRange(
        (std::numeric_limits<int32_t>::min)(),
        (std::numeric_limits<int32_t>::max)());
    ui->batchYOffsetSpinBox->setRange(
        (std::numeric_limits<int32_t>::min)(),
        (std::numeric_limits<int32_t>::max)());
    connect(ui->batchXEnabledCheckBox, &QCheckBox::toggled,
        this, [this](bool) { updateBatchOffsetControls(); });
    connect(ui->batchYEnabledCheckBox, &QCheckBox::toggled,
        this, [this](bool) { updateBatchOffsetControls(); });
    connect(ui->applyBatchOffsetButton, &QPushButton::clicked,
        this, &ImageEditorWindow::onApplyBatchOffsets);

    copyFramesAction->setShortcut(QKeySequence::Copy);
    copyFramesAction->setShortcutContext(Qt::WidgetShortcut);
    canvas->addAction(copyFramesAction);
    pasteFramesAction->setShortcut(QKeySequence::Paste);
    pasteFramesAction->setShortcutContext(Qt::WidgetShortcut);
    canvas->addAction(pasteFramesAction);
    duplicateFramesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    duplicateFramesAction->setShortcutContext(Qt::WidgetShortcut);
    canvas->addAction(duplicateFramesAction);
    updateBatchOffsetControls();
    updateFrameStructureControls();
    resetAnimationPlayback();
}

ImageEditorWindow::~ImageEditorWindow()
{
    animationPlaybackTimer.stop();
    suppressUndoStateSynchronization = true;
    imageUndoStack->disconnect(this);
    delete imageUndoStack;
    imageUndoStack = nullptr;
    delete ui;
}

void ImageEditorWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
        retranslateDynamicUi();
    }
    QWidget::changeEvent(event);
}

void ImageEditorWindow::retranslateDynamicUi()
{
    advancedFrameToolsButton->setText(tr("高级帧工具"));
    editAction->setText(tr("编辑图片"));
    exportAction->setText(tr("导出图片"));
    importAction->setText(tr("替换图片"));
    batchExportAction->setText(tr("批量导出"));
    reimportBatchAction->setText(tr("按清单回导"));
    batchOffsetAction->setText(tr("批量设置所选帧偏移"));
    copyFramesAction->setText(tr("复制所选帧"));
    pasteFramesAction->setText(tr("粘贴到当前帧之后"));
    duplicateFramesAction->setText(tr("重复所选帧"));
    moveFramesEarlierAction->setText(tr("所选帧前移"));
    moveFramesLaterAction->setText(tr("所选帧后移"));
    insertAction->setText(tr("批量插入图片"));
    addAction->setText(tr("批量导入到末尾"));
    deleteAction->setText(tr("删除图片"));
    undoShortcutAction->setText(tr("撤销图片编辑"));
    redoShortcutAction->setText(tr("重做图片编辑"));
    updateWindowTitle();
    updateFrameInfo();
    updateBatchOffsetControls();
    updateFrameStructureControls();
    updateAnimationPlaybackControls();
}

void ImageEditorWindow::setAdvancedFrameToolsVisible(bool visible)
{
    advancedFrameToolsButton->setArrowType(
        visible ? Qt::DownArrow : Qt::RightArrow);
    ui->frameStructureGroupBox->setVisible(visible);
    ui->canvasTransformGroupBox->setVisible(visible);
    ui->batchOffsetGroupBox->setVisible(visible);
}

void ImageEditorWindow::updateCanvas()
{
    int count = 0;
    QVector<QImage> images;

    if (currentPicType == PicType::Imp)
    {
        count = impFile.getImageCount();
        images.reserve(count);
        for (int i = 0; i < count; i++)
        {
            images.append(impFile.getFrameImage(i));
        }
    }
    else if (currentPicType != PicType::None)
    {
        count = picEditor.getFrameCount();
        images.reserve(count);
        for (int i = 0; i < count; i++)
        {
            images.append(picEditor.getFrameImage(i));
        }
    }

    canvas->setFrames(images);

    if (currentFrameIndex >= 0 && currentFrameIndex < count)
    {
        canvas->setSelectedIndex(currentFrameIndex);
    }

    updateFrameInfo();
    updateBatchOffsetControls();
    updateFrameStructureControls();
    resetAnimationPlayback();
}

void ImageEditorWindow::updateWindowTitle()
{
    QString fileName = currentFileName.empty() ? tr("未命名") : QString::fromUtf8(currentFileName.c_str());
    QString dirtyMark = isModified ? QStringLiteral(" *") : QString();
    setWindowTitle(tr("图片编辑器 - %1%2").arg(fileName).arg(dirtyMark));
}

QString ImageEditorWindow::currentFilePath() const
{
    if (currentFileName.empty())
        return QString();
    return EditorAssetPath::normalizedAbsolutePath(
        QString::fromUtf8(currentFileName));
}

QList<ProjectDocumentState> ImageEditorWindow::currentProjectDocuments() const
{
    const QString filePath = currentFilePath();
    if (filePath.isEmpty())
        return {};
    return {{filePath, ProjectDocumentType::Image, isModified}};
}

void ImageEditorWindow::setDocumentPathValidator(
    DocumentPathValidator validator)
{
    documentPathValidator = std::move(validator);
}

bool ImageEditorWindow::canAdoptDocumentPath(const QString& targetPath) const
{
    return !documentPathValidator ||
        documentPathValidator(currentFilePath(), targetPath);
}

void ImageEditorWindow::notifyDocumentStateChanged()
{
    emit documentStatesChanged();
}

int ImageEditorWindow::frameCount() const
{
    if (currentPicType == PicType::Imp)
        return impFile.getImageCount();
    if (currentPicType != PicType::None)
        return picEditor.getFrameCount();
    return 0;
}

void ImageEditorWindow::updateFrameInfo()
{
    const int count = frameCount();

    if (currentFrameIndex < 0 || currentFrameIndex >= count)
    {
        ui->frameInfoLabel->setText(tr("共 %1 帧").arg(count));
        return;
    }

    int xOffset = 0, yOffset = 0;
    QImage frameImage;

    if (currentPicType == PicType::Imp)
    {
        impFile.getFrameOffset(currentFrameIndex, &xOffset, &yOffset);
        frameImage = impFile.getFrameImage(currentFrameIndex);
    }
    else
    {
        picEditor.getFrameOffset(currentFrameIndex, &xOffset, &yOffset);
        frameImage = picEditor.getFrameImage(currentFrameIndex);
    }

    const QString frameDetails = tr("帧 %1/%2 | %3x%4 | 位置(%5, %6)")
        .arg(currentFrameIndex + 1)
        .arg(count)
        .arg(frameImage.width())
        .arg(frameImage.height())
        .arg(xOffset)
        .arg(yOffset);
    const int selectedCount = canvas->getSelectedIndices().size();
    if (selectedCount > 1)
    {
        ui->frameInfoLabel->setText(
            tr("已选 %1/%2 帧 | %3")
                .arg(selectedCount)
                .arg(count)
                .arg(frameDetails));
    }
    else
    {
        ui->frameInfoLabel->setText(frameDetails);
    }
}

void ImageEditorWindow::updateBatchOffsetControls()
{
    const int selectedCount = canvas
        ? canvas->getSelectedIndices().size() : 0;
    ui->batchSelectionLabel->setText(
        tr("已选择 %1 帧").arg(selectedCount));
    const bool hasAxis = ui->batchXEnabledCheckBox->isChecked() ||
        ui->batchYEnabledCheckBox->isChecked();
    const bool canApply = currentPicType != PicType::None &&
        selectedCount > 0 && hasAxis;
    ui->applyBatchOffsetButton->setEnabled(canApply);
    batchOffsetAction->setEnabled(canApply);
    ui->batchXOffsetSpinBox->setEnabled(
        ui->batchXEnabledCheckBox->isChecked());
    ui->batchYOffsetSpinBox->setEnabled(
        ui->batchYEnabledCheckBox->isChecked());
}

void ImageEditorWindow::updateFrameStructureControls()
{
    const QList<int> selection = canvas
        ? canvas->getSelectedIndices() : QList<int>();
    const bool hasDocument = currentPicType != PicType::None;
    const bool hasSelection = hasDocument && !selection.isEmpty();
    bool canMoveEarlier = false;
    bool canMoveLater = false;
    if (hasSelection)
    {
        QSet<int> selectedSet;
        for (int index : selection)
            selectedSet.insert(index);
        for (int index : selection)
        {
            canMoveEarlier = canMoveEarlier ||
                (index > 0 && !selectedSet.contains(index - 1));
            canMoveLater = canMoveLater ||
                (index + 1 < frameCount() &&
                 !selectedSet.contains(index + 1));
        }
    }

    const bool canPaste = hasSelection && !frameClipboard.empty();
    copyFramesAction->setEnabled(hasSelection);
    pasteFramesAction->setEnabled(canPaste);
    duplicateFramesAction->setEnabled(hasSelection);
    moveFramesEarlierAction->setEnabled(canMoveEarlier);
    moveFramesLaterAction->setEnabled(canMoveLater);
    batchExportAction->setEnabled(hasDocument);
    reimportBatchAction->setEnabled(hasDocument);
    ui->copyFramesButton->setEnabled(hasSelection);
    ui->pasteFramesButton->setEnabled(canPaste);
    ui->duplicateFramesButton->setEnabled(hasSelection);
    ui->moveFramesEarlierButton->setEnabled(canMoveEarlier);
    ui->moveFramesLaterButton->setEnabled(canMoveLater);
    ui->batchExportButton->setEnabled(hasDocument);
    ui->reimportBatchButton->setEnabled(hasDocument);
    ui->frameClipboardLabel->setText(
        tr("已复制：%1 帧").arg(frameClipboard.size()));
    updateCanvasTransformControls();
}

void ImageEditorWindow::updateCanvasTransformControls()
{
    const int selectedCount = canvas
        ? canvas->getSelectedIndices().size() : 0;
    const bool canTransform =
        currentPicType != PicType::None && selectedCount > 0;
    ui->canvasTransformSelectionLabel->setText(
        tr("已选择 %1 帧").arg(selectedCount));
    ui->rotateFramesLeftButton->setEnabled(canTransform);
    ui->rotateFramesRightButton->setEnabled(canTransform);
    ui->flipFramesHorizontallyButton->setEnabled(canTransform);
    ui->flipFramesVerticallyButton->setEnabled(canTransform);
}

int ImageEditorWindow::animationDirectionCount() const
{
    if (currentPicType == PicType::Imp)
        return impFile.getDirection();
    if (currentPicType != PicType::None)
        return picEditor.getDirection();
    return 1;
}

int ImageEditorWindow::animationInterval() const
{
    if (currentPicType == PicType::Imp)
        return impFile.getInterval();
    if (currentPicType != PicType::None)
        return picEditor.getInterval();
    return 0;
}

QImage ImageEditorWindow::animationFrameImage(int frameIndex) const
{
    if (frameIndex < 0 || frameIndex >= frameCount())
        return {};
    if (currentPicType == PicType::Imp)
        return impFile.getFrameImage(frameIndex);
    if (currentPicType != PicType::None)
        return picEditor.getFrameImage(frameIndex);
    return {};
}

void ImageEditorWindow::resetAnimationPlayback(bool preservePlaying)
{
    const bool restart = preservePlaying && animationPlaying && frameCount() > 0;
    animationPlaybackTimer.stop();
    animationAccumulatedMilliseconds = 0;
    animationPlaying = restart;
    if (animationPlaying)
    {
        animationPlaybackClock.restart();
        animationPlaybackTimer.start();
    }
    updateAnimationPlaybackControls();
}

void ImageEditorWindow::resetAnimationPlaybackForOpenedDocument()
{
    {
        const QSignalBlocker blocker(ui->animationPreviewDirectionSpinBox);
        ui->animationPreviewDirectionSpinBox->setValue(0);
    }
    resetAnimationPlayback();
}

void ImageEditorWindow::updateAnimationPlaybackControls()
{
    const ImageAnimationPlayback::Layout layout =
        ImageAnimationPlayback::calculateLayout(
            static_cast<std::size_t>(qMax(0, frameCount())),
            animationDirectionCount());
    const bool hasFrames = layout.framesPerDirection > 0;

    if (!hasFrames && animationPlaying)
    {
        animationPlaybackTimer.stop();
        animationPlaying = false;
        animationAccumulatedMilliseconds = 0;
    }

    {
        const QSignalBlocker blocker(ui->animationPreviewDirectionSpinBox);
        ui->animationPreviewDirectionSpinBox->setRange(
            0, hasFrames ? layout.directions - 1 : 0);
        ui->animationPreviewDirectionSpinBox->setValue(
            ImageAnimationPlayback::normalizeDirection(
                ui->animationPreviewDirectionSpinBox->value(), layout));
    }
    ui->animationPreviewDirectionSpinBox->setEnabled(hasFrames);
    ui->animationPlaybackButton->setEnabled(hasFrames);
    ui->animationResetButton->setEnabled(hasFrames);
    ui->animationPlaybackButton->setText(
        animationPlaying ? tr("暂停") : tr("播放"));
    updateAnimationPreviewFrame();
}

void ImageEditorWindow::updateAnimationPreviewFrame()
{
    qint64 elapsedMilliseconds = animationAccumulatedMilliseconds;
    if (animationPlaying && animationPlaybackClock.isValid())
        elapsedMilliseconds += animationPlaybackClock.elapsed();
    elapsedMilliseconds = qMax<qint64>(0, elapsedMilliseconds);

    const int storedDirections = animationDirectionCount();
    const int requestedDirection =
        ui->animationPreviewDirectionSpinBox->value();
    const ImageAnimationPlayback::Layout layout =
        ImageAnimationPlayback::calculateLayout(
            static_cast<std::size_t>(qMax(0, frameCount())),
            storedDirections);
    const std::optional<std::size_t> frameIndex =
        ImageAnimationPlayback::frameIndex(
            static_cast<std::size_t>(qMax(0, frameCount())),
            storedDirections,
            requestedDirection,
            static_cast<std::uint64_t>(elapsedMilliseconds),
            animationInterval());
    if (!frameIndex.has_value())
    {
        ui->animationPreviewLabel->setProperty(
            "animationFrameIndex", -1);
        ui->animationPreviewLabel->clearImage(
            tr("没有可播放的帧"));
        ui->animationStatusLabel->setText(
            tr("没有可播放的帧"));
        return;
    }

    const int absoluteFrameIndex = static_cast<int>(*frameIndex);
    const QImage frameImage = animationFrameImage(absoluteFrameIndex);
    ui->animationPreviewLabel->setProperty(
        "animationFrameIndex", absoluteFrameIndex);
    if (frameImage.isNull())
    {
        ui->animationPreviewLabel->clearImage(
            tr("当前动画帧无法显示"));
    }
    else
    {
        ui->animationPreviewLabel->setSourceImage(frameImage);
    }

    const int normalizedDirection =
        ImageAnimationPlayback::normalizeDirection(
            requestedDirection, layout);
    ui->animationStatusLabel->setText(
        tr("方向 %1/%2 | 帧 %3/%4 | 每方向 %5 帧 | 有效间隔 %6 ms")
            .arg(normalizedDirection + 1)
            .arg(layout.directions)
            .arg(absoluteFrameIndex + 1)
            .arg(frameCount())
            .arg(layout.framesPerDirection)
            .arg(ImageAnimationPlayback::effectiveFrameInterval(
                animationInterval())));
}

void ImageEditorWindow::onAnimationPlaybackToggled()
{
    if (animationPlaying)
    {
        if (animationPlaybackClock.isValid())
            animationAccumulatedMilliseconds += animationPlaybackClock.elapsed();
        animationPlaybackTimer.stop();
        animationPlaying = false;
    }
    else
    {
        const ImageAnimationPlayback::Layout layout =
            ImageAnimationPlayback::calculateLayout(
                static_cast<std::size_t>(qMax(0, frameCount())),
                animationDirectionCount());
        if (layout.framesPerDirection == 0)
            return;
        animationPlaybackClock.restart();
        animationPlaybackTimer.start();
        animationPlaying = true;
    }
    updateAnimationPlaybackControls();
}

void ImageEditorWindow::onAnimationPlaybackReset()
{
    resetAnimationPlayback();
}

void ImageEditorWindow::onAnimationPreviewDirectionChanged(int direction)
{
    Q_UNUSED(direction);
    resetAnimationPlayback(true);
}

void ImageEditorWindow::onAnimationPlaybackTimeout()
{
    updateAnimationPreviewFrame();
}

std::vector<int> ImageEditorWindow::selectedFrameIndices() const
{
    const QList<int> selection = canvas->getSelectedIndices();
    return std::vector<int>(selection.begin(), selection.end());
}

bool ImageEditorWindow::currentFrameSequence(
    std::vector<ImageFrameData>* frames) const
{
    if (currentPicType == PicType::Imp)
        return impFile.getFrameSequence(frames);
    if (currentPicType != PicType::None)
        return picEditor.getFrameSequence(frames);
    if (frames)
        frames->clear();
    return false;
}

bool ImageEditorWindow::currentBatchDocument(
    ImageFrameBatchDocument* document) const
{
    if (document == nullptr || currentPicType == PicType::None)
        return false;
    *document = {};
    document->documentName = QFileInfo(
        QString::fromUtf8(currentFileName)).fileName();
    document->directionCount = currentPicType == PicType::Imp
        ? impFile.getDirection() : picEditor.getDirection();
    document->intervalMilliseconds = currentPicType == PicType::Imp
        ? impFile.getInterval() : picEditor.getInterval();
    return !document->documentName.isEmpty() &&
           currentFrameSequence(&document->frames);
}

QString ImageEditorWindow::imageFrameBatchErrorText(
    const ImageFrameBatchFailure& failure) const
{
    switch (failure.error)
    {
    case ImageFrameBatchError::InvalidDocument:
        return tr("当前图片文档无法生成完整帧快照。");
    case ImageFrameBatchError::EmptySelection:
        return tr("没有可导出的所选帧。");
    case ImageFrameBatchError::DuplicateIndex:
        return tr("帧索引 %1 重复。").arg(failure.frameIndex);
    case ImageFrameBatchError::IndexOutOfRange:
        return tr("帧索引 %1 超出当前文档范围。").arg(failure.frameIndex);
    case ImageFrameBatchError::EncodeFailed:
        return tr("第 %1 帧无法规范编码为 PNG。").arg(failure.frameIndex);
    case ImageFrameBatchError::InvalidOutputDirectory:
        return tr("输出目录不存在、不可读或不可写。");
    case ImageFrameBatchError::ManifestReadFailed:
        return tr("无法读取批量图片清单：%1").arg(failure.detail);
    case ImageFrameBatchError::ManifestParseFailed:
        return tr("批量图片清单不是有效 JSON：%1").arg(failure.detail);
    case ImageFrameBatchError::UnsupportedFormat:
        return tr("所选 JSON 不是 jxqy-editor 图片帧批次清单。");
    case ImageFrameBatchError::UnsupportedVersion:
        return tr("批量图片清单版本不受当前编辑器支持。");
    case ImageFrameBatchError::InvalidManifest:
        return tr("批量图片清单缺少字段或字段值非法。");
    case ImageFrameBatchError::UnsafeFrameFileName:
        return tr("第 %1 帧使用了非法或不匹配的文件名：%2")
            .arg(failure.frameIndex)
            .arg(failure.detail);
    case ImageFrameBatchError::ReplacementConfirmationRequired:
        return tr("输出目录中已有一个有效的图片帧批次。");
    case ImageFrameBatchError::TargetCollision:
        return tr("输出目标已存在，但不属于有效旧批次：%1")
            .arg(failure.detail);
    case ImageFrameBatchError::TransactionRecoveryFailed:
        return tr("无法恢复输出目录中的旧事务：%1").arg(failure.detail);
    case ImageFrameBatchError::TransactionFailed:
        return tr("批量导出事务失败：%1").arg(failure.detail);
    case ImageFrameBatchError::DocumentChanged:
        return tr("当前文档自导出后已变化；请重新导出后再回导。");
    case ImageFrameBatchError::FrameFileInvalid:
    {
        QString reason;
        switch (failure.importError)
        {
        case ImageFrameImportError::FileMissing:
            reason = tr("文件不存在");
            break;
        case ImageFrameImportError::NotRegularFile:
            reason = tr("不是普通文件");
            break;
        case ImageFrameImportError::FileNotReadable:
            reason = tr("文件不可读");
            break;
        case ImageFrameImportError::UnsupportedExtension:
            reason = tr("扩展名不是 PNG");
            break;
        case ImageFrameImportError::UnsupportedFormat:
            reason = tr("实际内容不是 PNG");
            break;
        case ImageFrameImportError::DecodeFailed:
            reason = tr("PNG 解码失败");
            break;
        case ImageFrameImportError::EncodeFailed:
            reason = tr("PNG 规范编码失败");
            break;
        default:
            reason = tr("文件无效");
            break;
        }
        return tr("第 %1 帧回导失败：%2\n%3")
            .arg(failure.frameIndex)
            .arg(reason)
            .arg(failure.detail);
    }
    case ImageFrameBatchError::None:
        break;
    }
    return tr("未知批量图片错误。");
}

QString ImageEditorWindow::imageFrameTransformErrorText(
    const ImageFrameTransformFailure& failure) const
{
    switch (failure.error)
    {
    case ImageFrameTransformError::InvalidDocument:
        return tr("当前图片文档无法生成完整帧快照。");
    case ImageFrameTransformError::EmptySelection:
        return tr("没有可变换的所选帧。");
    case ImageFrameTransformError::DuplicateIndex:
        return tr("帧索引 %1 重复。").arg(failure.frameIndex);
    case ImageFrameTransformError::IndexOutOfRange:
        return tr("帧索引 %1 超出当前文档范围。").arg(failure.frameIndex);
    case ImageFrameTransformError::CurrentIndexInvalid:
        return tr("当前帧索引 %1 不属于有效选择。").arg(failure.frameIndex);
    case ImageFrameTransformError::InvalidOperation:
        return tr("所选画布变换不受支持。");
    case ImageFrameTransformError::InvalidFrame:
        return tr("帧索引 %1 的图片数据无效。").arg(failure.frameIndex);
    case ImageFrameTransformError::OffsetOutOfRange:
        return tr("帧索引 %1 的变换偏移超出 32 位有符号整数范围。")
            .arg(failure.frameIndex);
    case ImageFrameTransformError::ImageAllocationFailed:
        return failure.frameIndex >= 0
            ? tr("无法为帧索引 %1 分配变换画布。").arg(failure.frameIndex)
            : tr("无法为所选帧分配变换画布。");
    case ImageFrameTransformError::EncodeFailed:
        return tr("帧索引 %1 无法编码为 PNG。").arg(failure.frameIndex);
    case ImageFrameTransformError::None:
        break;
    }
    return tr("未知画布变换错误。");
}

bool ImageEditorWindow::replaceFrameSequence(
    const std::vector<ImageFrameData>& frames,
    const std::vector<int>& selectedIndices,
    int selectedCurrentIndex)
{
    bool replaced = false;
    if (currentPicType == PicType::Imp)
        replaced = impFile.setFrameSequence(frames);
    else if (currentPicType != PicType::None)
        replaced = picEditor.setFrameSequence(frames);
    if (!replaced)
        return false;

    currentFrameIndex = selectedCurrentIndex;
    updateCanvas();
    QList<int> selection;
    selection.reserve(static_cast<qsizetype>(selectedIndices.size()));
    for (int index : selectedIndices)
        selection.append(index);
    canvas->setSelectedIndices(selection, selectedCurrentIndex);
    updateFrameInfo();
    updateBatchOffsetControls();
    updateFrameStructureControls();
    return true;
}

void ImageEditorWindow::pushFrameSequenceEdit(
    const std::vector<ImageFrameData>& beforeFrames,
    const std::vector<int>& beforeSelection,
    int beforeCurrentIndex,
    ImageFrameSequenceEdit edit,
    const QString& commandText)
{
    if (!edit.changed)
        return;
    imageUndoStack->push(new ImageFrameSequenceCommand(
        beforeFrames,
        beforeSelection,
        beforeCurrentIndex,
        std::move(edit),
        [this](const std::vector<ImageFrameData>& frames,
               const std::vector<int>& selection,
               int selectedCurrentIndex)
        {
            const bool replaced = replaceFrameSequence(
                frames, selection, selectedCurrentIndex);
            Q_ASSERT(replaced);
            Q_UNUSED(replaced);
        },
        commandText));
}

void ImageEditorWindow::applySelectedFrameTransform(
    ImageFrameTransformOperation operation,
    const QString& commandText)
{
    const std::vector<int> selection = selectedFrameIndices();
    std::vector<ImageFrameData> frames;
    if (!currentFrameSequence(&frames))
    {
        QMessageBox::warning(
            this,
            tr("画布变换失败"),
            tr("无法读取当前图片帧，未修改文档。"));
        return;
    }

    ImageFrameSequenceEdit edit;
    ImageFrameTransformFailure failure;
    if (!ImageFrameTransform::transformSelectedFrames(
            frames,
            selection,
            currentFrameIndex,
            operation,
            &edit,
            &failure))
    {
        QMessageBox::warning(
            this,
            tr("画布变换失败"),
            tr("所选帧未修改。\n%1")
                .arg(imageFrameTransformErrorText(failure)));
        return;
    }
    if (!edit.changed)
        return;

    pushFrameSequenceEdit(
        frames,
        selection,
        currentFrameIndex,
        std::move(edit),
        commandText);
}

void ImageEditorWindow::openBatchImportDialog(
    ImageBatchImportInsertionMode defaultInsertionMode)
{
    if (currentPicType == PicType::None || currentFrameIndex < 0)
    {
        QMessageBox::warning(
            this, tr("错误"), tr("请先打开一个图片文件。"));
        return;
    }

    int32_t currentXOffset = 0;
    int32_t currentYOffset = 0;
    if (!frameOffset(
            currentFrameIndex, &currentXOffset, &currentYOffset))
    {
        QMessageBox::warning(
            this,
            tr("批量导入失败"),
            tr("无法读取当前帧偏移，文档未修改。"));
        return;
    }

    QString initialDirectory = assetsBasePath;
    if (!currentFileName.empty())
    {
        initialDirectory = QFileInfo(
            QString::fromUtf8(currentFileName)).absolutePath();
    }
    ImageBatchImportDialog dialog(
        frameCount(),
        currentFrameIndex,
        currentXOffset,
        currentYOffset,
        defaultInsertionMode,
        initialDirectory,
        this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    std::vector<ImageFrameData> frames;
    if (!currentFrameSequence(&frames))
    {
        QMessageBox::warning(
            this,
            tr("批量导入失败"),
            tr("无法读取当前图片帧，文档未修改。"));
        return;
    }

    ImageFrameSequenceEdit edit;
    if (!ImageFrameSequence::insertFramesAt(
            frames,
            dialog.insertionIndex(),
            dialog.preparedFrames(),
            &edit))
    {
        QMessageBox::warning(
            this,
            tr("批量导入失败"),
            tr("无法生成完整导入结果，文档未修改。"));
        return;
    }

    const qsizetype importedCount =
        static_cast<qsizetype>(dialog.preparedFrames().size());
    pushFrameSequenceEdit(
        frames,
        selectedFrameIndices(),
        currentFrameIndex,
        std::move(edit),
        tr("批量导入 %1 帧").arg(importedCount));
}

bool ImageEditorWindow::frameOffset(
    int frameIndex, int32_t* xOffset, int32_t* yOffset) const
{
    if (frameIndex < 0 || frameIndex >= frameCount())
        return false;
    if (currentPicType == PicType::Imp)
    {
        int x = 0;
        int y = 0;
        impFile.getFrameOffset(frameIndex, &x, &y);
        if (xOffset)
            *xOffset = static_cast<int32_t>(x);
        if (yOffset)
            *yOffset = static_cast<int32_t>(y);
        return true;
    }
    if (currentPicType == PicType::None)
        return false;
    picEditor.getFrameOffset(frameIndex, xOffset, yOffset);
    return true;
}

void ImageEditorWindow::applyFrameOffsetChanges(
    const std::vector<ImageFrameOffsetChange>& changes,
    bool useNewValues)
{
    for (const ImageFrameOffsetChange& change : changes)
    {
        const int32_t xOffset = useNewValues
            ? change.newXOffset : change.oldXOffset;
        const int32_t yOffset = useNewValues
            ? change.newYOffset : change.oldYOffset;
        if (currentPicType == PicType::Imp)
        {
            impFile.setFrameOffset(change.frameIndex, xOffset, yOffset);
        }
        else
        {
            const bool applied = picEditor.setFrameOffset(
                change.frameIndex, xOffset, yOffset);
            Q_ASSERT(applied);
            Q_UNUSED(applied);
        }
    }
    updateFrameInfo();
}

void ImageEditorWindow::markModifiedOutsideImageHistory()
{
    suppressUndoStateSynchronization = true;
    imageHistoryBaseModified = true;
    imageUndoStack->clear();
    suppressUndoStateSynchronization = false;
    synchronizeModifiedStateFromUndoStack();
}

void ImageEditorWindow::resetDocumentEditState()
{
    suppressUndoStateSynchronization = true;
    imageHistoryBaseModified = false;
    imageUndoStack->clear();
    imageUndoStack->setClean();
    frameClipboard.clear();
    suppressUndoStateSynchronization = false;
    synchronizeModifiedStateFromUndoStack();
    updateFrameStructureControls();
}

void ImageEditorWindow::synchronizeModifiedStateFromUndoStack()
{
    if (suppressUndoStateSynchronization)
        return;
    const bool modified = imageHistoryBaseModified ||
        !imageUndoStack->isClean();
    if (isModified == modified)
        return;
    isModified = modified;
    updateWindowTitle();
    notifyDocumentStateChanged();
}

void ImageEditorWindow::loadFromPicEditor()
{
    currentFrameIndex = 0;
    updateCanvas();

    ui->intervalSpinBox->blockSignals(true);
    ui->directionSpinBox->blockSignals(true);
    ui->intervalSpinBox->setValue(picEditor.getInterval());
    ui->directionSpinBox->setValue(picEditor.getDirection());
    ui->intervalSpinBox->blockSignals(false);
    ui->directionSpinBox->blockSignals(false);
    resetAnimationPlaybackForOpenedDocument();
}

ClosePlan ImageEditorWindow::prepareCloseTransaction() const
{
    ClosePlan plan;
    if (!isModified)
    {
        plan.decisions.append(CloseDecision::Ready);
        return plan;
    }

    const int result = QMessageBox::question(
        const_cast<ImageEditorWindow*>(this),
        tr("保存更改"),
        tr("图片已修改，是否保存？"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (result == QMessageBox::Cancel)
        plan.decisions.append(CloseDecision::Cancelled);
    else if (result == QMessageBox::Yes)
        plan.decisions.append(CloseDecision::Save);
    else
        plan.decisions.append(CloseDecision::Discard);
    return plan;
}

bool ImageEditorWindow::resolveCloseTransaction(const ClosePlan& plan)
{
    if (plan.decisions.size() != 1 || plan.isCancelled())
        return false;
    if (plan.decisions.front() == CloseDecision::Save)
    {
        onSaveFile();
        return !isModified;
    }
    return true;
}

void ImageEditorWindow::commitCloseTransaction(const ClosePlan& plan)
{
    if (plan.decisions.size() != 1 || plan.isCancelled())
        return;
    allowPreparedClose();
}

ImageEditorWindow::SaveConfirmResult ImageEditorWindow::confirmSaveIfModified()
{
    if (!isModified)
        return SaveConfirmResult::Discarded;

    int result = QMessageBox::question(this,
        tr("保存更改"),
        tr("图片已修改，是否保存？"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

    if (result == QMessageBox::Cancel)
        return SaveConfirmResult::Cancelled;

    if (result == QMessageBox::No)
        return SaveConfirmResult::Discarded;

    onSaveFile();
    if (isModified)
        return SaveConfirmResult::Cancelled;

    return SaveConfirmResult::Saved;
}

void ImageEditorWindow::closeEvent(QCloseEvent* event)
{
    if (consumePreparedClose())
    {
        event->accept();
        emit documentClosed();
        return;
    }

    if (confirmSaveIfModified() == SaveConfirmResult::Cancelled)
    {
        event->ignore();
        return;
    }
    event->accept();
    emit documentClosed();
}

bool ImageEditorWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == scrollArea->viewport() && event->type() == QEvent::Resize)
    {
        canvas->relayout();
    }
    return QWidget::eventFilter(watched, event);
}

bool ImageEditorWindow::openFile(const QString& fileName)
{
    if (fileName.isEmpty())
        return false;

    const QString normalizedFileName =
        EditorAssetPath::normalizedAbsolutePath(fileName);
    if (!canAdoptDocumentPath(normalizedFileName))
        return false;

    if (confirmSaveIfModified() == SaveConfirmResult::Cancelled)
        return false;

    std::string fileNameStr = normalizedFileName.toUtf8().toStdString();
    PicType picType = Util::detectPicType(fileNameStr);

    if (picType == PicType::None)
    {
        QMessageBox::warning(this, tr("错误"),
            tr("无法识别的文件格式！"));
        return false;
    }

    if (picType == PicType::Imp)
    {
        IMPImageFile loadedImpFile;
        if (loadedImpFile.load(fileNameStr))
        {
            impFile = std::move(loadedImpFile);
            currentFileName = fileNameStr;
            currentPicType = PicType::Imp;
            currentFrameIndex = impFile.getImageCount() > 0 ? 0 : -1;
            resetDocumentEditState();
            updateCanvas();

            ui->intervalSpinBox->blockSignals(true);
            ui->directionSpinBox->blockSignals(true);
            ui->intervalSpinBox->setValue(impFile.getInterval());
            ui->directionSpinBox->setValue(impFile.getDirection());
            ui->intervalSpinBox->blockSignals(false);
            ui->directionSpinBox->blockSignals(false);
            resetAnimationPlaybackForOpenedDocument();

            updateWindowTitle();
            notifyDocumentStateChanged();
            return true;
        }
        QMessageBox::warning(this, tr("错误"),
            tr("无法打开文件！"));
        return false;
    }

    PicFileEditor loadedPicEditor;
    if (loadedPicEditor.loadFromFile(fileNameStr))
    {
        picEditor = std::move(loadedPicEditor);
        currentFileName = fileNameStr;
        currentPicType = picType;
        resetDocumentEditState();
        loadFromPicEditor();
        updateWindowTitle();
        notifyDocumentStateChanged();
        return true;
    }
    QMessageBox::warning(this, tr("错误"),
        tr("无法打开文件！"));
    return false;
}

bool ImageEditorWindow::setAssetsBasePath(const QString& path)
{
    const Decision decision = prepareAssetsPathSwitch(path);
    if (decision == Decision::Cancelled || !resolveAssetsPathSwitch(decision))
        return false;
    commitAssetsPathSwitch(path);
    return true;
}

AssetsPathSwitchParticipant::Decision ImageEditorWindow::prepareAssetsPathSwitch(
    const QString& path) const
{
    Q_UNUSED(path);
    return Decision::Ready;
}

bool ImageEditorWindow::resolveAssetsPathSwitch(Decision decision)
{
    return decision != Decision::Cancelled;
}

void ImageEditorWindow::commitAssetsPathSwitch(const QString& path)
{
    assetsBasePath = path;
}

QString ImageEditorWindow::currentAssetsPath() const
{
    return assetsBasePath;
}

void ImageEditorWindow::onOpenFile()
{
    QString startDir = assetsBasePath;
    if (startDir.isEmpty() && !currentFileName.empty())
    {
        QFileInfo fileInfo(QString::fromUtf8(currentFileName));
        startDir = fileInfo.absolutePath();
    }

    QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("打开图片文件"),
        startDir,
        tr("所有支持的格式 (*.img *.mpc *.shd *.asf *.pic *.png);;IMG文件 (*.img);;MPC文件 (*.mpc);;SHD文件 (*.shd);;ASF文件 (*.asf);;PIC文件 (*.pic);;PNG文件 (*.png);;所有文件 (*.*)"),
        nullptr,
        QFileDialog::DontResolveSymlinks);

    if (fileName.isEmpty())
        return;

    openFile(fileName);
}

void ImageEditorWindow::onSaveFile()
{
    if (currentFileName.empty())
    {
        onSaveAs();
        return;
    }

    if (currentPicType == PicType::None)
    {
        QMessageBox::warning(this, tr("错误"),
            tr("没有打开的文件！"));
        return;
    }

    const QString targetPath = normalizedSavePath(
        QString::fromUtf8(currentFileName), true);
    if (saveToNormalizedPath(targetPath))
    {
        QMessageBox::information(this, tr("提示"),
            tr("保存成功！"));
    }
}

void ImageEditorWindow::onSaveAs()
{
    if (currentPicType == PicType::None)
    {
        QMessageBox::warning(this, tr("错误"),
            tr("没有打开的文件！"));
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this,
        tr("保存文件"), "",
        tr("IMG文件 (*.img);;所有文件 (*.*)"));

    if (fileName.isEmpty())
        return;

    if (saveFileAs(fileName))
    {
        QMessageBox::information(this, tr("提示"),
            tr("保存成功！"));
    }
}

QString ImageEditorWindow::normalizedSavePath(
    const QString& fileName, bool replaceExtension) const
{
    QString targetPath = EditorAssetPath::normalizedAbsolutePath(fileName);
    if (QFileInfo(targetPath).suffix().compare(
            QStringLiteral("img"), Qt::CaseInsensitive) == 0)
    {
        return targetPath;
    }

    if (replaceExtension)
    {
        const QFileInfo fileInfo(targetPath);
        return fileInfo.dir().filePath(
            fileInfo.completeBaseName() + QStringLiteral(".img"));
    }
    return targetPath + QStringLiteral(".img");
}

bool ImageEditorWindow::saveFileAs(const QString& fileName)
{
    if (currentPicType == PicType::None || fileName.isEmpty())
        return false;
    return saveToNormalizedPath(normalizedSavePath(fileName, false));
}

bool ImageEditorWindow::saveToNormalizedPath(const QString& targetPath)
{
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(targetPath);
    if (!mutationLease)
        return false;

    if (!canAdoptDocumentPath(targetPath))
        return false;

    const QString tempPath = targetPath + QStringLiteral(".tmp");

    // 步骤1：写入临时文件 — always save as IMP/IMG
    QFile::remove(tempPath);
    bool success = false;
    if (currentPicType == PicType::Imp)
    {
        success = impFile.save(tempPath.toUtf8().toStdString());
    }
    else
    {
        success = picEditor.saveAsIMP(tempPath.toUtf8().toStdString());
    }

    if (!success)
    {
        QFile::remove(tempPath);
        QMessageBox::warning(this, tr("保存失败"),
            tr("无法保存文件: %1").arg(targetPath));
        return false;
    }

    DurableFileTransaction transaction(
        QFileInfo(targetPath).absolutePath());
    QString transactionMessage;
    if (!transaction.addPreparedWrite(
            targetPath, tempPath, transactionMessage) ||
        !transaction.commit(transactionMessage))
    {
        QFile::remove(tempPath);
        QMessageBox::warning(this, tr("保存失败"),
            tr("无法完成保存: %1\n%2").arg(targetPath, transactionMessage));
        return false;
    }

    currentFileName = targetPath.toUtf8().toStdString();
    suppressUndoStateSynchronization = true;
    imageHistoryBaseModified = false;
    imageUndoStack->setClean();
    suppressUndoStateSynchronization = false;
    synchronizeModifiedStateFromUndoStack();
    updateWindowTitle();
    notifyDocumentStateChanged();
    return true;
}

void ImageEditorWindow::onAddFrame()
{
    openBatchImportDialog(ImageBatchImportInsertionMode::DocumentEnd);
}

void ImageEditorWindow::onInsertFrame()
{
    if (currentPicType == PicType::None || currentFrameIndex < 0)
        return;
    openBatchImportDialog(ImageBatchImportInsertionMode::BeforeCurrent);
}

void ImageEditorWindow::onDeleteFrame()
{
    if (currentFrameIndex < 0)
        return;

    if (currentPicType == PicType::Imp)
    {
        impFile.deleteFrame(currentFrameIndex);
    }
    else if (currentPicType != PicType::None)
    {
        picEditor.removeFrame(currentFrameIndex);
    }

    int count = (currentPicType == PicType::Imp) ? impFile.getImageCount() : picEditor.getFrameCount();
    if (currentFrameIndex >= count)
        currentFrameIndex = count - 1;

    updateCanvas();
    markModifiedOutsideImageHistory();
}

void ImageEditorWindow::onEditFrame()
{
    if (currentFrameIndex < 0)
        return;

    QImage frameImage;
    int xOffset = 0, yOffset = 0;

    if (currentPicType == PicType::Imp)
    {
        frameImage = impFile.getFrameImage(currentFrameIndex);
        impFile.getFrameOffset(currentFrameIndex, &xOffset, &yOffset);
    }
    else
    {
        frameImage = picEditor.getFrameImage(currentFrameIndex);
        picEditor.getFrameOffset(currentFrameIndex, &xOffset, &yOffset);
    }

    ImageEditDialog dialog(frameImage, xOffset, yOffset, this);
    if (dialog.exec() == QDialog::Accepted)
    {
        QImage editedImage = dialog.getEditedImage();
        int newXOffset = dialog.getXOffset();
        int newYOffset = dialog.getYOffset();

        if (currentPicType == PicType::Imp)
        {
            QByteArray byteArray = imageToPngByteArray(editedImage);

            impFile.setFrame(currentFrameIndex,
                reinterpret_cast<const uint8_t*>(byteArray.constData()),
                byteArray.size(), newXOffset, newYOffset);
        }
        else
        {
            if (!picEditor.ensureFrameOffsetsEditable())
            {
                QMessageBox::warning(this, tr("编辑失败"),
                    tr("无法准备旧图片的逐帧偏移数据。"));
                return;
            }
            picEditor.setFrameImage(currentFrameIndex, editedImage);
            if (!picEditor.setFrameOffset(
                    currentFrameIndex, newXOffset, newYOffset))
            {
                QMessageBox::warning(this, tr("编辑失败"),
                    tr("无法更新帧偏移。"));
                return;
            }
        }

        updateCanvas();
        markModifiedOutsideImageHistory();
    }
}

void ImageEditorWindow::onExportFrame()
{
    if (currentFrameIndex < 0)
        return;

    QString fileName = QFileDialog::getSaveFileName(this,
        tr("导出帧图片"), "",
        tr("PNG图片 (*.png);;所有文件 (*.*)"));

    if (fileName.isEmpty())
        return;

    if (QFileInfo(fileName).suffix().isEmpty())
    {
        fileName += ".png";
    }
    auto mutationLease = AuthoringMutationGate::instance().
        acquireMutationLeaseForPath(fileName);
    if (!mutationLease)
    {
        QMessageBox::warning(
            this,
            tr("导出失败"),
            tr("目标资源包正在更新或进行其他写入。"));
        return;
    }

    QImage frameImage;
    if (currentPicType == PicType::Imp)
    {
        frameImage = impFile.getFrameImage(currentFrameIndex);
    }
    else
    {
        frameImage = picEditor.getFrameImage(currentFrameIndex);
    }

    if (!frameImage.isNull())
    {
        QSaveFile output(fileName);
        const bool saved = output.open(QIODevice::WriteOnly) &&
            frameImage.save(&output, "PNG") &&
            output.commit();
        if (!saved)
        {
            output.cancelWriting();
            QMessageBox::warning(this, tr("导出失败"), tr("无法写入文件: %1").arg(fileName));
        }
    }
}

void ImageEditorWindow::onBatchExportFrames()
{
    ImageFrameBatchDocument document;
    if (!currentBatchDocument(&document))
    {
        QMessageBox::warning(
            this,
            tr("批量导出失败"),
            tr("当前图片文档无法生成完整帧快照。"));
        return;
    }

    QString initialDirectory = QStandardPaths::writableLocation(
        QStandardPaths::DocumentsLocation);
    if (initialDirectory.isEmpty())
    {
        initialDirectory = QFileInfo(
            QString::fromUtf8(currentFileName)).absolutePath();
    }
    const std::vector<int> selection = selectedFrameIndices();
    ImageBatchExportDialog dialog(
        static_cast<int>(selection.size()),
        static_cast<int>(document.frames.size()),
        initialDirectory,
        this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    ImageFrameBatchPreparedExport preparedExport;
    ImageFrameBatchFailure failure;
    if (!ImageFrameBatchExchange::prepareExport(
            document,
            dialog.scope(),
            selection,
            &preparedExport,
            &failure))
    {
        QMessageBox::warning(
            this, tr("批量导出失败"), imageFrameBatchErrorText(failure));
        return;
    }

    bool published = ImageFrameBatchExchange::publishExport(
        dialog.outputDirectory(), preparedExport, false, &failure);
    if (!published && failure.error ==
            ImageFrameBatchError::ReplacementConfirmationRequired)
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            tr("替换已有批次"),
            tr("输出目录中已有有效的图片帧批次。是否用当前 %1 帧完整替换旧批次？")
                .arg(preparedExport.files.size()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        published = ImageFrameBatchExchange::publishExport(
            dialog.outputDirectory(), preparedExport, true, &failure);
    }
    if (!published)
    {
        QMessageBox::warning(
            this, tr("批量导出失败"), imageFrameBatchErrorText(failure));
        return;
    }

    QString message = tr("已事务导出 %1 帧到：\n%2")
        .arg(preparedExport.files.size())
        .arg(dialog.outputDirectory());
    if (!failure.detail.isEmpty())
        message += tr("\n\n警告：%1").arg(failure.detail);
    QMessageBox::information(this, tr("批量导出完成"), message);
}

void ImageEditorWindow::onReimportFrameBatch()
{
    if (currentPicType == PicType::None)
        return;

    QString initialDirectory = QFileInfo(
        QString::fromUtf8(currentFileName)).absolutePath();
    QFileDialog fileDialog(
        nullptr,
        tr("选择图片帧批次清单"),
        initialDirectory,
        tr("图片帧批次清单 (jxqy-image-frames.json);;JSON 文件 (*.json)"));
    fileDialog.setAcceptMode(QFileDialog::AcceptOpen);
    fileDialog.setFileMode(QFileDialog::ExistingFile);
    fileDialog.setOption(QFileDialog::DontUseNativeDialog);
    if (fileDialog.exec() != QDialog::Accepted ||
        fileDialog.selectedFiles().isEmpty())
    {
        return;
    }

    QString errorMessage;
    const BatchReimportResult result = reimportFrameBatchFromManifest(
        fileDialog.selectedFiles().constFirst(), &errorMessage);
    if (result == BatchReimportResult::Failed)
    {
        QMessageBox::warning(this, tr("回导失败"), errorMessage);
        return;
    }
    if (result == BatchReimportResult::Unchanged)
    {
        QMessageBox::information(
            this,
            tr("回导完成"),
            tr("清单中的 PNG 和偏移与当前文档相同，未产生修改。"));
        return;
    }
}

ImageEditorWindow::BatchReimportResult
ImageEditorWindow::reimportFrameBatchFromManifest(
    const QString& manifestPath,
    QString* errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
    ImageFrameBatchDocument document;
    if (!currentBatchDocument(&document))
    {
        if (errorMessage)
            *errorMessage = tr("当前图片文档无法生成完整帧快照。");
        return BatchReimportResult::Failed;
    }

    ImageFrameSequenceEdit edit;
    ImageFrameBatchFailure failure;
    if (!ImageFrameBatchExchange::prepareReimport(
            manifestPath, document, &edit, &failure))
    {
        if (errorMessage)
            *errorMessage = imageFrameBatchErrorText(failure);
        return BatchReimportResult::Failed;
    }
    if (!edit.changed)
        return BatchReimportResult::Unchanged;

    const qsizetype changedFrameCount =
        static_cast<qsizetype>(edit.selectedIndices.size());
    pushFrameSequenceEdit(
        document.frames,
        selectedFrameIndices(),
        currentFrameIndex,
        std::move(edit),
        tr("按清单回导 %1 帧").arg(changedFrameCount));
    return BatchReimportResult::Applied;
}

void ImageEditorWindow::onImportFrame()
{
    if (currentFrameIndex < 0)
        return;

    QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("选择图片文件"),
        QString(),
        tr("PNG图片 (*.png);;所有文件 (*.*)"),
        nullptr,
        QFileDialog::DontResolveSymlinks);

    if (fileName.isEmpty())
        return;

    QImage image;
    if (!image.load(fileName))
    {
        QMessageBox::warning(this, tr("错误"),
            tr("无法加载图片！"));
        return;
    }

    image = image.convertToFormat(QImage::Format_ARGB32);

    if (currentPicType == PicType::Imp)
    {
        QByteArray byteArray = imageToPngByteArray(image);

        impFile.setFrameData(currentFrameIndex,
            reinterpret_cast<const uint8_t*>(byteArray.constData()),
            byteArray.size());
    }
    else
    {
        picEditor.setFrameImage(currentFrameIndex, image);
    }

    updateCanvas();
    markModifiedOutsideImageHistory();
}

void ImageEditorWindow::onFrameClicked(int index)
{
    currentFrameIndex = index;
    updateFrameInfo();
    updateBatchOffsetControls();
    updateFrameStructureControls();
}

void ImageEditorWindow::onFrameRightClicked(int index, const QPoint& globalPos)
{
    canvas->selectFrameForContextMenu(index);
    contextMenu->popup(globalPos);
}

void ImageEditorWindow::onApplyBatchOffsets()
{
    const QList<int> selectedFrames = canvas->getSelectedIndices();
    const bool updateX = ui->batchXEnabledCheckBox->isChecked();
    const bool updateY = ui->batchYEnabledCheckBox->isChecked();
    if (selectedFrames.isEmpty() || (!updateX && !updateY) ||
        currentPicType == PicType::None)
    {
        return;
    }

    std::vector<ImageFrameOffset> currentOffsets;
    currentOffsets.reserve(static_cast<size_t>(selectedFrames.size()));
    for (int frameIndex : selectedFrames)
    {
        int32_t xOffset = 0;
        int32_t yOffset = 0;
        if (!frameOffset(frameIndex, &xOffset, &yOffset))
        {
            QMessageBox::warning(this, tr("批量偏移失败"),
                tr("所选帧已不再有效，未修改任何帧。"));
            return;
        }
        currentOffsets.push_back({frameIndex, xOffset, yOffset});
    }

    const int modeIndex = ui->batchOffsetModeComboBox->currentIndex();
    if (modeIndex < 0 || modeIndex > 1)
        return;
    const ImageFrameOffsetBatchMode mode = modeIndex == 0
        ? ImageFrameOffsetBatchMode::Set
        : ImageFrameOffsetBatchMode::Add;
    std::vector<ImageFrameOffsetChange> changes;
    if (!PicFileEditor::calculateFrameOffsetChanges(
            currentOffsets,
            mode,
            updateX,
            updateY,
            static_cast<int32_t>(ui->batchXOffsetSpinBox->value()),
            static_cast<int32_t>(ui->batchYOffsetSpinBox->value()),
            &changes))
    {
        QMessageBox::warning(this, tr("批量偏移失败"),
            tr("批量偏移结果超出 32 位有符号整数范围，未修改任何帧。"));
        return;
    }
    if (changes.empty())
        return;

    if (currentPicType != PicType::Imp &&
        !picEditor.ensureFrameOffsetsEditable())
    {
        QMessageBox::warning(this, tr("批量偏移失败"),
            tr("无法准备旧图片的逐帧偏移数据，未修改任何帧。"));
        return;
    }

    imageUndoStack->push(new ImageFrameOffsetBatchCommand(
        std::move(changes),
        [this](const std::vector<ImageFrameOffsetChange>& frameChanges,
               bool useNewValues)
        {
            applyFrameOffsetChanges(frameChanges, useNewValues);
        },
        tr("批量修改 %1 帧偏移").arg(selectedFrames.size())));
}

void ImageEditorWindow::onCopyFrames()
{
    std::vector<ImageFrameData> frames;
    std::vector<ImageFrameData> copiedFrames;
    if (!currentFrameSequence(&frames) ||
        !ImageFrameSequence::copySelectedFrames(
            frames, selectedFrameIndices(), &copiedFrames))
    {
        QMessageBox::warning(this, tr("复制帧失败"),
            tr("无法读取所选帧，文档内缓冲未改变。"));
        return;
    }
    frameClipboard = std::move(copiedFrames);
    updateFrameStructureControls();
}

void ImageEditorWindow::onPasteFrames()
{
    if (frameClipboard.empty())
        return;

    std::vector<ImageFrameData> frames;
    if (!currentFrameSequence(&frames))
    {
        QMessageBox::warning(this, tr("粘贴帧失败"),
            tr("无法读取当前图片帧，未修改文档。"));
        return;
    }

    ImageFrameSequenceEdit edit;
    if (!ImageFrameSequence::insertFramesAfter(
            frames, currentFrameIndex, frameClipboard, &edit))
    {
        const bool exceedsLimit = frameClipboard.size() >
            static_cast<size_t>(MaximumImageFrameCount) - frames.size();
        QMessageBox::warning(this, tr("粘贴帧失败"),
            exceedsLimit
                ? tr("粘贴后将超过 10000 帧上限，未修改文档。")
                : tr("当前帧或文档内缓冲无效，未修改文档。"));
        return;
    }
    pushFrameSequenceEdit(
        frames,
        selectedFrameIndices(),
        currentFrameIndex,
        std::move(edit),
        tr("粘贴 %1 帧").arg(frameClipboard.size()));
}

void ImageEditorWindow::onDuplicateFrames()
{
    const std::vector<int> selection = selectedFrameIndices();
    std::vector<ImageFrameData> frames;
    if (!currentFrameSequence(&frames))
    {
        QMessageBox::warning(this, tr("重复帧失败"),
            tr("无法读取所选帧，未修改文档。"));
        return;
    }

    ImageFrameSequenceEdit edit;
    if (!ImageFrameSequence::duplicateSelectedFrames(
            frames, selection, &edit))
    {
        const bool exceedsLimit = selection.size() >
            static_cast<size_t>(MaximumImageFrameCount) - frames.size();
        QMessageBox::warning(this, tr("重复帧失败"),
            exceedsLimit
                ? tr("重复后将超过 10000 帧上限，未修改文档。")
                : tr("所选帧无效，未修改文档。"));
        return;
    }
    pushFrameSequenceEdit(
        frames,
        selection,
        currentFrameIndex,
        std::move(edit),
        tr("重复 %1 帧").arg(selection.size()));
}

void ImageEditorWindow::onMoveFramesEarlier()
{
    const std::vector<int> selection = selectedFrameIndices();
    std::vector<ImageFrameData> frames;
    if (!currentFrameSequence(&frames))
        return;

    ImageFrameSequenceEdit edit;
    if (!ImageFrameSequence::moveSelectedFrames(
            frames,
            selection,
            currentFrameIndex,
            ImageFrameMoveDirection::Earlier,
            &edit))
    {
        QMessageBox::warning(this, tr("帧排序失败"),
            tr("所选帧无效，未修改文档。"));
        return;
    }
    pushFrameSequenceEdit(
        frames,
        selection,
        currentFrameIndex,
        std::move(edit),
        tr("所选帧前移"));
}

void ImageEditorWindow::onMoveFramesLater()
{
    const std::vector<int> selection = selectedFrameIndices();
    std::vector<ImageFrameData> frames;
    if (!currentFrameSequence(&frames))
        return;

    ImageFrameSequenceEdit edit;
    if (!ImageFrameSequence::moveSelectedFrames(
            frames,
            selection,
            currentFrameIndex,
            ImageFrameMoveDirection::Later,
            &edit))
    {
        QMessageBox::warning(this, tr("帧排序失败"),
            tr("所选帧无效，未修改文档。"));
        return;
    }
    pushFrameSequenceEdit(
        frames,
        selection,
        currentFrameIndex,
        std::move(edit),
        tr("所选帧后移"));
}

void ImageEditorWindow::onRotateFramesLeft()
{
    applySelectedFrameTransform(
        ImageFrameTransformOperation::RotateLeft90,
        tr("所选帧左转 90°"));
}

void ImageEditorWindow::onRotateFramesRight()
{
    applySelectedFrameTransform(
        ImageFrameTransformOperation::RotateRight90,
        tr("所选帧右转 90°"));
}

void ImageEditorWindow::onFlipFramesHorizontally()
{
    applySelectedFrameTransform(
        ImageFrameTransformOperation::FlipHorizontal,
        tr("水平翻转所选帧"));
}

void ImageEditorWindow::onFlipFramesVertically()
{
    applySelectedFrameTransform(
        ImageFrameTransformOperation::FlipVertical,
        tr("垂直翻转所选帧"));
}

void ImageEditorWindow::onIntervalChanged(int value)
{
    if (currentPicType == PicType::None)
        return;

    if (currentPicType == PicType::Imp)
    {
        impFile.setInterval(value);
    }
    else if (currentPicType != PicType::None)
    {
        picEditor.setInterval(value);
    }
    markModifiedOutsideImageHistory();
    resetAnimationPlayback();
}

void ImageEditorWindow::onDirectionChanged(int value)
{
    if (currentPicType == PicType::None)
        return;

    if (currentPicType == PicType::Imp)
    {
        impFile.setDirection(value);
    }
    else if (currentPicType != PicType::None)
    {
        picEditor.setDirection(value);
    }
    markModifiedOutsideImageHistory();
    resetAnimationPlayback();
}
