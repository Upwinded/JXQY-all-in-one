#pragma once

#include <QObject>
#include <QPalette>

class QApplication;

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    enum class Theme
    {
        Dark,
        Light
    };
    Q_ENUM(Theme)

    static ThemeManager& instance();

    void initialize(QApplication& app);
    void toggleTheme();
    void setTheme(Theme theme);
    Theme currentTheme() const;
    QString currentThemeName() const;

signals:
    void themeChanged(Theme theme);

private:
    explicit ThemeManager(QObject* parent = nullptr);

    QPalette createDarkPalette() const;
    QPalette createLightPalette() const;
    void applyPalette(const QPalette& palette);
    void updateWidgetStyles(QWidget* widget);

    QApplication* application = nullptr;
    Theme current = Theme::Dark;
};
