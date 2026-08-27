#pragma once

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>

enum class WindowDisplayMode
{
    Normal,
    Maximized,
    FullScreen
};

namespace WindowPlacementPolicy
{
constexpr int schemaVersion = 1;

QString modeToString(WindowDisplayMode mode);
WindowDisplayMode modeFromString(const QString& value, bool* recognized = nullptr);

// Returns a fully visible normal-window rectangle in Qt logical pixels. The
// first screen is treated as the primary fallback. Existing multi-monitor
// placement is preserved when it still intersects an available screen.
QRect sanitizeNormalGeometry(const QRect& requestedGeometry,
                             const QList<QRect>& availableScreenGeometries,
                             const QSize& minimumSize,
                             const QRect& fallbackGeometry = QRect());
}
