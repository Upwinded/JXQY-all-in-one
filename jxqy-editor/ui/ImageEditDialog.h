#pragma once

#include <QDialog>
#include <QImage>
#include <QWidget>

namespace Ui
{
class ImageEditDialog;
}

class ImageFramePreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ImageFramePreviewWidget(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setOffset(int x, int y);
    int getXOffset() const { return xOffset; }
    int getYOffset() const { return yOffset; }

signals:
    void offsetChangedByClick(int x, int y);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QImage frameImage;
    int xOffset = 0;
    int yOffset = 0;
    double scale = 1.0;
    int imageDisplayX = 0;
    int imageDisplayY = 0;
    int displayWidth = 0;
    int displayHeight = 0;
};

class ImageEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImageEditDialog(const QImage& image, int xOffset, int yOffset, QWidget* parent = nullptr);
    ~ImageEditDialog();

    int getXOffset() const;
    int getYOffset() const;
    QImage getEditedImage() const;

private slots:
    void onCropTransparent();
    void onResetImage();
    void onOffsetSpinBoxChanged();

private:
    void updatePreview();

    QImage originalImage;
    QImage editedImage;
    int originalXOffset;
    int originalYOffset;
    Ui::ImageEditDialog* ui;
    ImageFramePreviewWidget* framePreviewWidget;
};
