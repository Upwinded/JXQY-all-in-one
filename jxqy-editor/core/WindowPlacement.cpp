#include "WindowPlacement.h"

#include <QtGlobal>

#include <limits>

namespace
{
qint64 intersectionArea(const QRect& left, const QRect& right)
{
    if (!left.isValid() || !right.isValid())
        return 0;

    const qint64 leftEdge = qMax<qint64>(left.x(), right.x());
    const qint64 topEdge = qMax<qint64>(left.y(), right.y());
    const qint64 rightEdge = qMin<qint64>(
        static_cast<qint64>(left.x()) + left.width(),
        static_cast<qint64>(right.x()) + right.width());
    const qint64 bottomEdge = qMin<qint64>(
        static_cast<qint64>(left.y()) + left.height(),
        static_cast<qint64>(right.y()) + right.height());
    return qMax<qint64>(0, rightEdge - leftEdge) *
        qMax<qint64>(0, bottomEdge - topEdge);
}

qint64 squaredDistanceToRect(qint64 pointX, qint64 pointY, const QRect& rect)
{
    const qint64 left = rect.x();
    const qint64 top = rect.y();
    const qint64 right = left + rect.width() - 1;
    const qint64 bottom = top + rect.height() - 1;
    const qint64 dx = pointX < left ? left - pointX :
        (pointX > right ? pointX - right : 0);
    const qint64 dy = pointY < top ? top - pointY :
        (pointY > bottom ? pointY - bottom : 0);

    // Two squared components must still fit in qint64 when added.
    constexpr qint64 safeRoot = 2147483647LL;
    const qint64 safeDx = qMin(dx, safeRoot);
    const qint64 safeDy = qMin(dy, safeRoot);
    return safeDx * safeDx + safeDy * safeDy;
}

int chooseScreen(const QRect& candidate, const QList<QRect>& screens,
                 bool& intersectsScreen)
{
    int selectedIndex = 0;
    qint64 greatestArea = 0;
    for (int index = 0; index < screens.size(); ++index)
    {
        const qint64 area = intersectionArea(candidate, screens[index]);
        if (area > greatestArea)
        {
            greatestArea = area;
            selectedIndex = index;
        }
    }
    intersectsScreen = greatestArea > 0;
    if (intersectsScreen || !candidate.isValid())
        return selectedIndex;

    qint64 shortestDistance = std::numeric_limits<qint64>::max();
    const qint64 candidateCenterX = static_cast<qint64>(candidate.x()) +
        candidate.width() / 2;
    const qint64 candidateCenterY = static_cast<qint64>(candidate.y()) +
        candidate.height() / 2;
    for (int index = 0; index < screens.size(); ++index)
    {
        const qint64 distance = squaredDistanceToRect(
            candidateCenterX, candidateCenterY, screens[index]);
        if (distance < shortestDistance)
        {
            shortestDistance = distance;
            selectedIndex = index;
        }
    }
    return selectedIndex;
}
}

QString WindowPlacementPolicy::modeToString(WindowDisplayMode mode)
{
    switch (mode)
    {
    case WindowDisplayMode::Maximized:
        return QStringLiteral("maximized");
    case WindowDisplayMode::FullScreen:
        return QStringLiteral("fullscreen");
    case WindowDisplayMode::Normal:
    default:
        return QStringLiteral("normal");
    }
}

WindowDisplayMode WindowPlacementPolicy::modeFromString(
    const QString& value, bool* recognized)
{
    if (value == QStringLiteral("normal"))
    {
        if (recognized)
            *recognized = true;
        return WindowDisplayMode::Normal;
    }
    if (value == QStringLiteral("maximized"))
    {
        if (recognized)
            *recognized = true;
        return WindowDisplayMode::Maximized;
    }
    if (value == QStringLiteral("fullscreen"))
    {
        if (recognized)
            *recognized = true;
        return WindowDisplayMode::FullScreen;
    }
    if (recognized)
        *recognized = false;
    return WindowDisplayMode::Normal;
}

QRect WindowPlacementPolicy::sanitizeNormalGeometry(
    const QRect& requestedGeometry,
    const QList<QRect>& availableScreenGeometries,
    const QSize& minimumSize,
    const QRect& fallbackGeometry)
{
    QList<QRect> screens;
    for (const QRect& screen : availableScreenGeometries)
    {
        if (screen.isValid())
            screens.append(screen);
    }

    QRect candidate = requestedGeometry;
    if (!candidate.isValid() && fallbackGeometry.isValid())
        candidate = fallbackGeometry;

    if (screens.isEmpty())
        return candidate;

    bool intersectsScreen = false;
    const int screenIndex = chooseScreen(candidate, screens, intersectsScreen);
    const QRect target = screens[screenIndex];

    const int minimumWidth = qMin(target.width(), qMax(1, minimumSize.width()));
    const int minimumHeight = qMin(target.height(), qMax(1, minimumSize.height()));
    int width = candidate.width() > 0 ? candidate.width() : minimumWidth;
    int height = candidate.height() > 0 ? candidate.height() : minimumHeight;
    width = qBound(minimumWidth, width, target.width());
    height = qBound(minimumHeight, height, target.height());

    qint64 x = candidate.isValid() ? candidate.x() :
        static_cast<qint64>(target.x()) + (target.width() - width) / 2;
    qint64 y = candidate.isValid() ? candidate.y() :
        static_cast<qint64>(target.y()) + (target.height() - height) / 2;
    if (!intersectsScreen)
    {
        x = static_cast<qint64>(target.x()) + (target.width() - width) / 2;
        y = static_cast<qint64>(target.y()) + (target.height() - height) / 2;
    }

    const qint64 maximumX = static_cast<qint64>(target.x()) + target.width() - width;
    const qint64 maximumY = static_cast<qint64>(target.y()) + target.height() - height;
    x = qBound<qint64>(target.x(), x, maximumX);
    y = qBound<qint64>(target.y(), y, maximumY);
    return QRect(static_cast<int>(x), static_cast<int>(y), width, height);
}
