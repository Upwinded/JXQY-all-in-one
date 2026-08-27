#include "DarkMdiSubWindow.h"

#include <QApplication>
#include <QIcon>

DarkMdiSubWindow::DarkMdiSubWindow(QWidget* parent)
    : QMdiSubWindow(parent)
{
    QIcon icon = QApplication::windowIcon();
    if (icon.isNull())
        icon = QIcon(QStringLiteral(":/icons/application.png"));
    setWindowIcon(icon);
}

void DarkMdiSubWindow::closeEvent(QCloseEvent* event)
{
    QWidget* inner = widget();
    if (inner)
    {
        QCloseEvent innerEvent;
        QApplication::sendEvent(inner, &innerEvent);
        if (innerEvent.isAccepted())
        {
            event->accept();
        }
        else
        {
            event->ignore();
        }
        return;
    }
    QMdiSubWindow::closeEvent(event);
}
