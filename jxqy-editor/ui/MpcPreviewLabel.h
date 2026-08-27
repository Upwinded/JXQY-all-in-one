#pragma once

#include <QLabel>
#include <QPixmap>
#include <QImage>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>

/// MPC 帧主预览控件。
///
/// 设计目的：打破 QLabel 因 pixmap 反向影响布局尺寸形成的反馈循环。
/// 普通 QLabel 设置 pixmap 后 sizeHint 会随 pixmap 改变，布局据此重排控件尺寸，
/// 控件 Resize 又触发按新 contentsRect 重新缩放 pixmap，再改变 sizeHint……
/// 极宽/极高图会令控件尺寸反复跳动（闪烁），并可能缩小到不可用尺寸。
///
/// 本控件固定 sizeHint 只受 minimumSize/布局约束控制，pixmap 永远不参与尺寸计算；
/// 内部按 contentsRect 等比完整容纳原始帧，目标尺寸未变化时不重复生成 pixmap。
class MpcPreviewLabel : public QLabel
{
    Q_OBJECT

public:
    explicit MpcPreviewLabel(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    /// 设置原始帧图像。控件持有原图，按当前 contentsRect 重新等比适配显示。
    /// sizeHint 不受图像尺寸影响。
    void setSourceImage(const QImage& image);

    /// 按资源保存的方向数和帧间隔循环播放完整动画。
    void setSourceAnimation(const QVector<QImage>& frames,
                            int directions,
                            int intervalMilliseconds,
                            int direction = 0);

    /// 清除图像，显示文字提示。
    void clearImage(const QString& text);

    /// 返回当前缩放后的 pixmap 尺寸（用于测试断言稳定性）。
    QSize currentPixmapSize() const { return scaledPixmap.size(); }
    int currentAnimationFrameIndex() const { return animationFrameIndex; }
    bool isAnimationPlaying() const
    {
        return animationTimer && animationTimer->isActive();
    }

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    /// 按当前 contentsRect 重新适配原始帧。目标尺寸与上次相同时直接复用缓存。
    void recomputePixmap();
    void stopAnimation();
    void updateAnimationFrame();

    QImage sourceImage;
    QPixmap scaledPixmap;
    QSize lastTargetSize;
    bool recomputeGuard = false;
    QVector<QImage> animationFrames;
    QTimer* animationTimer = nullptr;
    QElapsedTimer animationClock;
    int animationDirections = 1;
    int animationIntervalMilliseconds = 0;
    int animationDirection = 0;
    int animationFrameIndex = -1;
};
