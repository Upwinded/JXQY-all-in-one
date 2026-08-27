#include "ThemeManager.h"

#include "../core/EditorSettings.h"

#include <QApplication>
#include <QWidget>

ThemeManager& ThemeManager::instance()
{
    static ThemeManager singleton;
    return singleton;
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
{
}

void ThemeManager::initialize(QApplication& app)
{
    application = &app;

    QSettings settings = EditorSettings::create();
    QString savedTheme = settings.value("theme", "dark").toString();

    if (savedTheme == "light")
    {
        setTheme(Theme::Light);
    }
    else
    {
        setTheme(Theme::Dark);
    }
}

void ThemeManager::toggleTheme()
{
    if (current == Theme::Dark)
    {
        setTheme(Theme::Light);
    }
    else
    {
        setTheme(Theme::Dark);
    }
}

void ThemeManager::setTheme(Theme theme)
{
    current = theme;

    if (application)
    {
        QPalette palette = (theme == Theme::Dark) ? createDarkPalette() : createLightPalette();
        applyPalette(palette);

        // 强制刷新所有顶层窗口的样式，确保主题切换即时生效
        const auto topLevelWidgets = QApplication::topLevelWidgets();
        for (QWidget* widget : topLevelWidgets)
        {
            widget->setPalette(palette);
            widget->update();
            updateWidgetStyles(widget);
        }
    }

    QSettings settings = EditorSettings::create();
    settings.setValue("theme", (theme == Theme::Dark) ? "dark" : "light");
    settings.sync();

    emit themeChanged(theme);
}

ThemeManager::Theme ThemeManager::currentTheme() const
{
    return current;
}

QString ThemeManager::currentThemeName() const
{
    return (current == Theme::Dark) ? "dark" : "light";
}

QPalette ThemeManager::createDarkPalette() const
{
    QPalette palette;

    // 基本背景和前景
    palette.setColor(QPalette::Window, QColor(45, 45, 45));
    palette.setColor(QPalette::WindowText, QColor(212, 212, 212));
    palette.setColor(QPalette::Base, QColor(30, 30, 30));
    palette.setColor(QPalette::AlternateBase, QColor(37, 37, 37));
    palette.setColor(QPalette::Text, QColor(212, 212, 212));

    // 按钮和高亮
    palette.setColor(QPalette::Button, QColor(60, 60, 60));
    palette.setColor(QPalette::ButtonText, QColor(212, 212, 212));
    palette.setColor(QPalette::Highlight, QColor(9, 71, 113));
    palette.setColor(QPalette::HighlightedText, Qt::white);

    // 提示框
    palette.setColor(QPalette::ToolTipBase, QColor(60, 60, 60));
    palette.setColor(QPalette::ToolTipText, QColor(212, 212, 212));

    // 其他颜色
    palette.setColor(QPalette::BrightText, QColor(255, 50, 50));
    palette.setColor(QPalette::Link, QColor(42, 130, 218));

    // 阴影和高光
    palette.setColor(QPalette::Light, QColor(80, 80, 80));
    palette.setColor(QPalette::Midlight, QColor(60, 60, 60));
    palette.setColor(QPalette::Mid, QColor(50, 50, 50));
    palette.setColor(QPalette::Dark, QColor(35, 35, 35));
    palette.setColor(QPalette::Shadow, QColor(20, 20, 20));

    // 去除标题文字浮雕重影
    palette.setColor(QPalette::Active, QPalette::Light, QColor(45, 45, 45));
    palette.setColor(QPalette::Inactive, QPalette::Light, QColor(35, 35, 35));

    // Active 标题栏
    palette.setColor(QPalette::Active, QPalette::Window, QColor(45, 45, 45));
    palette.setColor(QPalette::Active, QPalette::WindowText, QColor(212, 212, 212));

    // Inactive 标题栏
    palette.setColor(QPalette::Inactive, QPalette::Window, QColor(35, 35, 35));
    palette.setColor(QPalette::Inactive, QPalette::WindowText, QColor(150, 150, 150));

    // 禁用状态
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(102, 102, 102));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(102, 102, 102));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(102, 102, 102));
    palette.setColor(QPalette::Disabled, QPalette::Base, QColor(45, 45, 45));
    palette.setColor(QPalette::Disabled, QPalette::Button, QColor(45, 45, 45));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(60, 60, 60));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(102, 102, 102));

    return palette;
}

QPalette ThemeManager::createLightPalette() const
{
    QPalette palette;

    // 基本背景和前景
    palette.setColor(QPalette::Window, QColor(243, 243, 243));
    palette.setColor(QPalette::WindowText, QColor(30, 30, 30));
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::AlternateBase, QColor(245, 245, 245));
    palette.setColor(QPalette::Text, QColor(30, 30, 30));

    // 按钮和高亮
    palette.setColor(QPalette::Button, QColor(243, 243, 243));
    palette.setColor(QPalette::ButtonText, QColor(30, 30, 30));
    palette.setColor(QPalette::Highlight, QColor(0, 120, 212));
    palette.setColor(QPalette::HighlightedText, Qt::white);

    // 提示框
    palette.setColor(QPalette::ToolTipBase, QColor(249, 249, 249));
    palette.setColor(QPalette::ToolTipText, QColor(30, 30, 30));

    // 其他颜色
    palette.setColor(QPalette::BrightText, QColor(255, 0, 0));
    palette.setColor(QPalette::Link, QColor(0, 102, 204));

    // 阴影和高光
    palette.setColor(QPalette::Light, QColor(255, 255, 255));
    palette.setColor(QPalette::Midlight, QColor(235, 235, 235));
    palette.setColor(QPalette::Mid, QColor(190, 190, 190));
    palette.setColor(QPalette::Dark, QColor(160, 160, 160));
    palette.setColor(QPalette::Shadow, QColor(105, 105, 105));

    // Active 标题栏 — 比主背景略深，形成层次
    palette.setColor(QPalette::Active, QPalette::Window, QColor(230, 230, 230));
    palette.setColor(QPalette::Active, QPalette::WindowText, QColor(30, 30, 30));
    palette.setColor(QPalette::Active, QPalette::Light, QColor(230, 230, 230));

    // Inactive 标题栏 — 比 Active 稍浅
    palette.setColor(QPalette::Inactive, QPalette::Window, QColor(238, 238, 238));
    palette.setColor(QPalette::Inactive, QPalette::WindowText, QColor(120, 120, 120));
    palette.setColor(QPalette::Inactive, QPalette::Light, QColor(238, 238, 238));

    // 禁用状态
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(128, 128, 128));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(128, 128, 128));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(128, 128, 128));
    palette.setColor(QPalette::Disabled, QPalette::Base, QColor(240, 240, 240));
    palette.setColor(QPalette::Disabled, QPalette::Button, QColor(240, 240, 240));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(200, 200, 200));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(128, 128, 128));

    return palette;
}

void ThemeManager::applyPalette(const QPalette& palette)
{
    if (application)
    {
        application->setPalette(palette);
    }
}

void ThemeManager::updateWidgetStyles(QWidget* widget)
{
    if (!widget)
    {
        return;
    }

    widget->setPalette(QApplication::palette());

    const auto children = widget->findChildren<QWidget*>();
    for (QWidget* child : children)
    {
        child->setPalette(QApplication::palette());
        child->update();
    }
}
