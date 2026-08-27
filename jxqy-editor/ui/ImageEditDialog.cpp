#include "ImageEditDialog.h"
#include "ui_ImageEditDialog.h"
#include "../core/PicFileEditor.h"

#include <QPainter>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSizePolicy>

// ========== ImageFramePreviewWidget 实现 ==========

ImageFramePreviewWidget::ImageFramePreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(100, 100);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
}

void ImageFramePreviewWidget::setImage(const QImage& image)
{
    frameImage = image;
    update();
}

void ImageFramePreviewWidget::setOffset(int x, int y)
{
    xOffset = x;
    yOffset = y;
    update();
}

void ImageFramePreviewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(48, 48, 48));

    if (frameImage.isNull())
    {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, tr("无图片"));
        return;
    }

    int maxW = width() - 4;
    int maxH = height() - 4;
    if (maxW < 50) maxW = 50;
    if (maxH < 50) maxH = 50;

    // 计算缩放
    scale = 1.0;
    if (frameImage.width() > maxW || frameImage.height() > maxH)
    {
        scale = qMin(static_cast<double>(maxW) / frameImage.width(),
                     static_cast<double>(maxH) / frameImage.height());
    }

    displayWidth = qRound(frameImage.width() * scale);
    displayHeight = qRound(frameImage.height() * scale);
    imageDisplayX = (width() - displayWidth) / 2;
    imageDisplayY = (height() - displayHeight) / 2;

    // 绘制棋盘背景
    static QImage checkerboard;
    if (checkerboard.isNull())
    {
        checkerboard = QImage(16, 16, QImage::Format_ARGB32);
        checkerboard.fill(QColor(64, 64, 64));
        QPainter cp(&checkerboard);
        cp.fillRect(0, 0, 8, 8, QColor(48, 48, 48));
        cp.fillRect(8, 8, 8, 8, QColor(48, 48, 48));
    }

    QRect imageRect(imageDisplayX, imageDisplayY, displayWidth, displayHeight);
    painter.fillRect(imageRect, QBrush(checkerboard));

    // 绘制缩放后的图片
    QImage scaled = frameImage.scaled(displayWidth, displayHeight,
        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    painter.drawImage(imageDisplayX, imageDisplayY, scaled);

    // 绘制图片边界
    painter.setPen(QPen(QColor(100, 100, 100), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(imageRect);

    // 绘制偏移锚点十字线
    int anchorX = imageDisplayX + qRound(xOffset * scale);
    int anchorY = imageDisplayY + qRound(yOffset * scale);

    painter.setPen(QPen(QColor(255, 80, 80), 1, Qt::DashLine));
    painter.drawLine(anchorX, imageDisplayY, anchorX, imageDisplayY + displayHeight);
    painter.drawLine(imageDisplayX, anchorY, imageDisplayX + displayWidth, anchorY);

    // 绘制锚点圆点
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 80, 80));
    painter.drawEllipse(anchorX - 4, anchorY - 4, 8, 8);

    // 绘制坐标文本
    painter.setPen(QColor(255, 200, 200));
    painter.setFont(QFont("Arial", 9));
    QString coordText = QString("(%1, %2)").arg(xOffset).arg(yOffset);
    int textX = anchorX + 8;
    int textY = anchorY - 8;
    if (textX + 80 > width()) textX = anchorX - 80;
    if (textY < 14) textY = anchorY + 16;
    painter.drawText(textX, textY, coordText);
}

void ImageFramePreviewWidget::mousePressEvent(QMouseEvent* event)
{
    if (frameImage.isNull() || event->button() != Qt::LeftButton)
        return;

    // 反算原始图片像素坐标
    QPoint clickPos = event->position().toPoint();
    int clickX = clickPos.x();
    int clickY = clickPos.y();

    // 检查点击是否在图片显示区域内
    QRect imageRect(imageDisplayX, imageDisplayY, displayWidth, displayHeight);
    if (!imageRect.contains(clickPos))
        return;

    int origX = qRound((clickX - imageDisplayX) / scale);
    int origY = qRound((clickY - imageDisplayY) / scale);

    // 限制在图片范围内
    origX = qBound(0, origX, frameImage.width() - 1);
    origY = qBound(0, origY, frameImage.height() - 1);

    xOffset = origX;
    yOffset = origY;
    emit offsetChangedByClick(origX, origY);
    update();
}

// ========== ImageEditDialog 实现 ==========

ImageEditDialog::ImageEditDialog(const QImage& image, int xOffset, int yOffset, QWidget* parent)
    : QDialog(parent)
    , originalImage(image)
    , editedImage(image)
    , originalXOffset(xOffset)
    , originalYOffset(yOffset)
    , ui(new Ui::ImageEditDialog)
{
    ui->setupUi(this);

    // 创建自定义预览控件替换 QLabel
    framePreviewWidget = new ImageFramePreviewWidget(this);
    framePreviewWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    framePreviewWidget->setImage(editedImage);
    framePreviewWidget->setOffset(xOffset, yOffset);

    // 替换 scrollArea 中的 imageLabel
    if (ui->scrollArea->widget() && ui->scrollArea->widget()->layout())
    {
        QLayout* scrollLayout = ui->scrollArea->widget()->layout();
        // 移除旧的 imageLabel
        scrollLayout->removeWidget(ui->imageLabel);
        delete ui->imageLabel;
        ui->imageLabel = nullptr;
        scrollLayout->addWidget(framePreviewWidget);
    }

    ui->xOffsetSpinBox->setValue(xOffset);
    ui->yOffsetSpinBox->setValue(yOffset);

    connect(ui->cropButton, &QPushButton::clicked, this, &ImageEditDialog::onCropTransparent);
    connect(ui->resetButton, &QPushButton::clicked, this, &ImageEditDialog::onResetImage);
    connect(ui->xOffsetSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &ImageEditDialog::onOffsetSpinBoxChanged);
    connect(ui->yOffsetSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &ImageEditDialog::onOffsetSpinBoxChanged);
    connect(framePreviewWidget, &ImageFramePreviewWidget::offsetChangedByClick, this, [this](int x, int y)
    {
        ui->xOffsetSpinBox->setValue(x);
        ui->yOffsetSpinBox->setValue(y);
    });

    updatePreview();
}

ImageEditDialog::~ImageEditDialog()
{
    delete ui;
}

int ImageEditDialog::getXOffset() const
{
    return ui->xOffsetSpinBox->value();
}

int ImageEditDialog::getYOffset() const
{
    return ui->yOffsetSpinBox->value();
}

QImage ImageEditDialog::getEditedImage() const
{
    return editedImage;
}

void ImageEditDialog::updatePreview()
{
    if (editedImage.isNull())
    {
        ui->sizeInfoLabel->setText("");
        return;
    }

    framePreviewWidget->setImage(editedImage);
    framePreviewWidget->setOffset(ui->xOffsetSpinBox->value(), ui->yOffsetSpinBox->value());

    ui->sizeInfoLabel->setText(tr("尺寸: %1 x %2 像素 | 偏移: (%3, %4)")
        .arg(editedImage.width())
        .arg(editedImage.height())
        .arg(ui->xOffsetSpinBox->value())
        .arg(ui->yOffsetSpinBox->value()));
}

void ImageEditDialog::onOffsetSpinBoxChanged()
{
    framePreviewWidget->setOffset(ui->xOffsetSpinBox->value(), ui->yOffsetSpinBox->value());
    ui->sizeInfoLabel->setText(tr("尺寸: %1 x %2 像素 | 偏移: (%3, %4)")
        .arg(editedImage.width())
        .arg(editedImage.height())
        .arg(ui->xOffsetSpinBox->value())
        .arg(ui->yOffsetSpinBox->value()));
}

void ImageEditDialog::onCropTransparent()
{
    if (editedImage.isNull())
        return;

    // Shared core rule: crop alpha == 0 outer edges and strictly adjust the
    // offset as newOffset = oldOffset - cropLeft/Top. Negative offsets are
    // allowed (no transparent padding, no zeroing, no formula change). Fully
    // transparent frames become a 1x1 transparent frame with the offset kept.
    TransparentCropResult cropped = PicFileEditor::cropTransparentEdges(
        editedImage, ui->xOffsetSpinBox->value(), ui->yOffsetSpinBox->value());

    editedImage = cropped.image;
    ui->xOffsetSpinBox->setValue(cropped.xOffset);
    ui->yOffsetSpinBox->setValue(cropped.yOffset);

    updatePreview();
}

void ImageEditDialog::onResetImage()
{
    editedImage = originalImage;
    ui->xOffsetSpinBox->setValue(originalXOffset);
    ui->yOffsetSpinBox->setValue(originalYOffset);
    updatePreview();
}
