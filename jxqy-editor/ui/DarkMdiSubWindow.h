#pragma once

#include <QCloseEvent>
#include <QMdiSubWindow>

class DarkMdiSubWindow : public QMdiSubWindow
{
    Q_OBJECT

public:
    explicit DarkMdiSubWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
};
