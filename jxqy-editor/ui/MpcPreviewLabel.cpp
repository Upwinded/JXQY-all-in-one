#include "MpcPreviewLabel.h"
#include "Image/ImageAnimationPlayback.h"

#include <QResizeEvent>

MpcPreviewLabel::MpcPreviewLabel(QWidget* parent)
    : QLabel(parent)
{
    // sizeHint 不受 pixmap 影响：固定一个合理尺寸，由布局（minimumSize/拉伸）决定最终大小。
    // pixmap 仅在 contentsRect 内等比绘制，不参与布局尺寸协商，从而打破
    // setPixmap → sizeHint 变化 → 布局 Resize → 重新缩放 pixmap 的反馈循环。
    setAlignment(Qt::AlignCenter);
    setMinimumSize(120, 120);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    animationTimer = new QTimer(this);
    animationTimer->setInterval(16);
    animationTimer->setTimerType(Qt::PreciseTimer);
    connect(animationTimer, &QTimer::timeout,
            this, &MpcPreviewLabel::updateAnimationFrame);
}

QSize MpcPreviewLabel::sizeHint() const
{
    return QSize(240, 160);
}

QSize MpcPreviewLabel::minimumSizeHint() const
{
    return QSize(120, 120);
}

void MpcPreviewLabel::setSourceImage(const QImage& image)
{
    stopAnimation();
    if (!image.isNull() && !sourceImage.isNull() &&
        image.cacheKey() == sourceImage.cacheKey())
    {
        recomputePixmap();
        return;
    }
    sourceImage = image;
    scaledPixmap = QPixmap();
    lastTargetSize = QSize();
    if (sourceImage.isNull())
    {
        QLabel::setPixmap(QPixmap());
        return;
    }
    recomputePixmap();
}

void MpcPreviewLabel::setSourceAnimation(
    const QVector<QImage>& frames,
    int directions,
    int intervalMilliseconds,
    int direction)
{
    stopAnimation();
    animationFrames = frames;
    animationDirections = qMax(1, directions);
    animationIntervalMilliseconds = intervalMilliseconds;
    animationDirection = qMax(0, direction);
    if (animationFrames.isEmpty())
    {
        clearImage(tr("没有可播放的帧"));
        return;
    }
    animationClock.start();
    updateAnimationFrame();
    if (animationFrames.size() > 1)
        animationTimer->start();
}

void MpcPreviewLabel::clearImage(const QString& text)
{
    stopAnimation();
    sourceImage = QImage();
    scaledPixmap = QPixmap();
    lastTargetSize = QSize();
    QLabel::setPixmap(QPixmap());
    setText(text);
}

void MpcPreviewLabel::stopAnimation()
{
    if (animationTimer)
        animationTimer->stop();
    animationFrames.clear();
    animationClock.invalidate();
    animationDirections = 1;
    animationIntervalMilliseconds = 0;
    animationDirection = 0;
    animationFrameIndex = -1;
}

void MpcPreviewLabel::updateAnimationFrame()
{
    if (animationFrames.isEmpty())
        return;
    const std::optional<std::size_t> frameIndex =
        ImageAnimationPlayback::frameIndex(
            static_cast<std::size_t>(animationFrames.size()),
            animationDirections,
            animationDirection,
            static_cast<std::uint64_t>(qMax<qint64>(0, animationClock.elapsed())),
            animationIntervalMilliseconds);
    if (!frameIndex.has_value())
        return;
    const int index = static_cast<int>(*frameIndex);
    if (index == animationFrameIndex)
        return;
    animationFrameIndex = index;
    sourceImage = animationFrames[index];
    scaledPixmap = QPixmap();
    lastTargetSize = QSize();
    setText(QString());
    recomputePixmap();
}

void MpcPreviewLabel::recomputePixmap()
{
    if (sourceImage.isNull())
        return;

    // 扣除 frame 与留白得到可用内容区。
    QRect contents = contentsRect();
    const int frameMargin = frameWidth() * 2;
    const int padding = 8;
    int availWidth = contents.width() - frameMargin - padding;
    int availHeight = contents.height() - frameMargin - padding;

    if (availWidth <= 0 || availHeight <= 0)
    {
        // 控件尚未完成布局：保留旧 pixmap，不生成空 pixmap。
        return;
    }

    QSize target(availWidth, availHeight);
    // 目标尺寸未变化时复用缓存，避免一次布局过程重复缩放（重入/连续 resize 合并）。
    if (!scaledPixmap.isNull() && target == lastTargetSize)
        return;
    lastTargetSize = target;

    scaledPixmap = QPixmap::fromImage(sourceImage).scaled(
        availWidth, availHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // 直接调用 QLabel::setPixmap 绘制；QLabel 的 pixmap 不再反向改变本控件 sizeHint。
    QLabel::setPixmap(scaledPixmap);
}

void MpcPreviewLabel::resizeEvent(QResizeEvent* event)
{
    QLabel::resizeEvent(event);
    // 重入保护：resize 期间触发的布局变化不再嵌套重算。
    if (recomputeGuard)
        return;
    recomputeGuard = true;
    recomputePixmap();
    recomputeGuard = false;
}
