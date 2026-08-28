#pragma once

#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QWidget>

namespace WidgetTestSupport
{
inline bool sendWidgetMouseEvent(QWidget& widget, QEvent::Type type, const QPoint& pos,
    Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QPointF point(pos);
    QMouseEvent event(type, point, point, point, button, buttons, Qt::NoModifier);
    return QApplication::sendEvent(&widget, &event);
}
}
