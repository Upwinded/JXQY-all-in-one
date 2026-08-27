#include "ui/MainWindow.h"
#include "ui/ThemeManager.h"
#include "core/DesktopFileOpenEventRouter.h"
#include "core/EditorApplicationBootstrap.h"
#include "core/EditorProcessLifecycle.h"
#include "core/EditorSettings.h"
#include "core/TranslationManager.h"
#ifdef JXQY_EDITOR_BUILD_BENCHMARK
#include "diagnostics/EditorPerformanceBenchmark.h"
#endif

#include <QApplication>
#ifdef JXQY_EDITOR_BUILD_BENCHMARK
#include <QElapsedTimer>
#endif
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QProxyStyle>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleOptionTitleBar>
#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#ifdef JXQY_EDITOR_BUILD_BENCHMARK
#include <QTemporaryDir>
#endif
#include <QTimer>
#include <QMdiSubWindow>

#include <memory>

class FusionNoShadowStyle : public QProxyStyle
{
public:
    explicit FusionNoShadowStyle(QStyle* baseStyle)
        : QProxyStyle(baseStyle)
    {
    }

    void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                            QPainter* painter, const QWidget* widget = nullptr) const override
    {
        if (control != CC_TitleBar)
        {
            QProxyStyle::drawComplexControl(control, option, painter, widget);
            return;
        }

        auto titleBarOption = qstyleoption_cast<const QStyleOptionTitleBar*>(option);
        auto mdiSubWindow = qobject_cast<const QMdiSubWindow*>(widget);
        if (!titleBarOption || !mdiSubWindow)
        {
            QProxyStyle::drawComplexControl(control, option, painter, widget);
            return;
        }

        // 先让 Fusion 按标准方式绘制标题栏和按钮，但临时清空标题，避免样式内部把文字画两次。
        QStyleOptionTitleBar optionWithoutText(*titleBarOption);
        const QString originalText = optionWithoutText.text;
        optionWithoutText.text.clear();
        QProxyStyle::drawComplexControl(control, &optionWithoutText, painter, widget);

        if (originalText.isEmpty())
        {
            return;
        }

        // 再只绘制一次标题文字，彻底去掉 Fusion 在某些环境下出现的浮雕/重影效果。
        QRect labelRect = subControlRect(CC_TitleBar, &optionWithoutText, SC_TitleBarLabel, widget);
        if (!labelRect.isValid())
        {
            return;
        }

        QRect iconRect = subControlRect(CC_TitleBar, &optionWithoutText, SC_TitleBarSysMenu, widget);
        if (iconRect.isValid())
        {
            labelRect.setLeft(qMax(labelRect.left(), iconRect.right() + 6));
        }

        QRect rightLimitRect = optionWithoutText.rect;
        const QRect closeRect = subControlRect(CC_TitleBar, &optionWithoutText, SC_TitleBarCloseButton, widget);
        if (closeRect.isValid())
        {
            rightLimitRect.setRight(closeRect.left() - 6);
        }

        const QRect normalRect = subControlRect(CC_TitleBar, &optionWithoutText, SC_TitleBarNormalButton, widget);
        if (normalRect.isValid())
        {
            rightLimitRect.setRight(qMin(rightLimitRect.right(), normalRect.left() - 6));
        }

        const QRect maxRect = subControlRect(CC_TitleBar, &optionWithoutText, SC_TitleBarMaxButton, widget);
        if (maxRect.isValid())
        {
            rightLimitRect.setRight(qMin(rightLimitRect.right(), maxRect.left() - 6));
        }

        const QRect minRect = subControlRect(CC_TitleBar, &optionWithoutText, SC_TitleBarMinButton, widget);
        if (minRect.isValid())
        {
            rightLimitRect.setRight(qMin(rightLimitRect.right(), minRect.left() - 6));
        }

        labelRect.setRight(qMin(labelRect.right(), rightLimitRect.right()));
        if (labelRect.width() <= 0)
        {
            return;
        }

        painter->save();
        painter->setPen(optionWithoutText.palette.color(QPalette::WindowText));

        const QString elidedText = optionWithoutText.fontMetrics.elidedText(
            originalText, Qt::ElideRight, labelRect.width());
        painter->drawText(labelRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, elidedText);
        painter->restore();
    }
};

int main(int argc, char* argv[])
{
#ifdef JXQY_EDITOR_BUILD_BENCHMARK
    QElapsedTimer processStartupClock;
    processStartupClock.start();
#endif

    QApplication application(argc, argv);
    initializeEditorApplication(
        application,
        EditorApplicationSurface::Graphical);
#ifdef Q_OS_WIN
    application.setWindowIcon(
        QIcon(QStringLiteral(":/icons/application.ico")));
#else
    application.setWindowIcon(
        QIcon(QStringLiteral(":/icons/application.png")));
#endif
    DesktopFileOpenEventRouter desktopFileOpenEventRouter(&application);
    application.installEventFilter(&desktopFileOpenEventRouter);

    QStringList arguments = application.arguments();
    const EditorCommandDispatchResult commandDispatch =
        dispatchEditorAssetCommand(
            arguments,
            EditorCommandDispatchMode::HandleWhenRequested,
            stdout,
            stderr);
    if (commandDispatch.handled)
        return commandDispatch.exitCode;
    const QStringList desktopOpenArguments = arguments.mid(1);

#ifdef JXQY_EDITOR_BUILD_BENCHMARK
    EditorPerformanceBenchmarkOptions performanceOptions;
    performanceOptions.mapPath =
        qEnvironmentVariable("JXQY_EDITOR_PERFORMANCE_MAP");
    performanceOptions.assetsPath =
        qEnvironmentVariable("JXQY_EDITOR_PERFORMANCE_ASSETS");
    performanceOptions.reportPath =
        qEnvironmentVariable("JXQY_EDITOR_PERFORMANCE_REPORT");
    const QString performanceIterations =
        qEnvironmentVariable("JXQY_EDITOR_PERFORMANCE_ITERATIONS");
    if (!performanceIterations.isEmpty())
    {
        bool iterationsOk = false;
        performanceOptions.interactionIterations =
            performanceIterations.toInt(&iterationsOk);
        if (!iterationsOk)
            performanceOptions.interactionIterations = -1;
    }
    const bool performanceBenchmarkRequested =
        !performanceOptions.mapPath.isEmpty();
    application.setProperty(
        "editorPerformanceBenchmark", performanceBenchmarkRequested);
#endif

    QApplication::setStyle(new FusionNoShadowStyle(QStyleFactory::create("Fusion")));

#ifdef JXQY_EDITOR_BUILD_BENCHMARK
    std::unique_ptr<QTemporaryDir> performanceSettingsDirectory;
#endif
    QString configFilePath;
#ifdef JXQY_EDITOR_BUILD_BENCHMARK
    if (performanceBenchmarkRequested)
    {
        performanceSettingsDirectory = std::make_unique<QTemporaryDir>();
        if (!performanceSettingsDirectory->isValid())
            return 1;
        configFilePath = performanceSettingsDirectory->filePath("editor_config.ini");
    }
    else
        configFilePath =
            EditorSettings::userConfigurationFilePath();
#else
    configFilePath =
        EditorSettings::userConfigurationFilePath();
#endif
#ifdef JXQY_EDITOR_BUILD_BENCHMARK
    if (!performanceBenchmarkRequested)
#endif
    {
        const QString legacyConfigFilePath =
            QDir(QFileInfo(
                     application.applicationFilePath())
                     .absolutePath())
                .filePath(
                    QStringLiteral("editor_config.ini"));
        const EditorSettings::ConfigurationMigrationResult
            migration =
                EditorSettings::migrateLegacyConfiguration(
                    legacyConfigFilePath,
                    configFilePath);
        if (!migration.succeeded())
        {
            qCritical().noquote()
                << QStringLiteral(
                       "Cannot migrate the editor configuration: %1")
                       .arg(migration.errorMessage);
            return 1;
        }
        if (migration.status ==
            EditorSettings::ConfigurationMigrationStatus::
                Migrated)
        {
            qInfo().noquote()
                << QStringLiteral(
                       "Migrated editor configuration to %1")
                       .arg(migration.targetPath);
        }
    }
    application.setProperty("configFilePath", configFilePath);

#ifdef JXQY_EDITOR_BUILD_BENCHMARK
    if (performanceBenchmarkRequested)
    {
        QSettings benchmarkSettings(configFilePath, QSettings::IniFormat);
        benchmarkSettings.setValue("assetsPath", performanceOptions.assetsPath);
        benchmarkSettings.setValue("theme", "dark");
        benchmarkSettings.sync();
        if (benchmarkSettings.status() != QSettings::NoError)
            return 1;
    }
#endif

    TranslationManager::instance().initialize(application);

    QFont defaultFont("Microsoft YaHei", 9);
    application.setFont(defaultFont);

    ThemeManager::instance().initialize(application);

    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& context, const QString& msg)
    {
        static QFile logFile;
        static bool logFileOpen = false;
        if (!logFileOpen)
        {
            const QString localDataDirectoryPath =
                QStandardPaths::writableLocation(
                    QStandardPaths::AppLocalDataLocation);
            if (!localDataDirectoryPath.isEmpty())
            {
                const QString logDirectoryPath =
                    QDir(localDataDirectoryPath).filePath("logs");
                if (QDir().mkpath(logDirectoryPath))
                {
                    logFile.setFileName(
                        QDir(logDirectoryPath).filePath(
                            "editor_debug.log"));
                    logFileOpen = logFile.open(
                        QIODevice::WriteOnly |
                        QIODevice::Append |
                        QIODevice::Text);
                }
            }
        }
        if (logFileOpen)
        {
            QTextStream stream(&logFile);
            stream << msg << "\n";
            stream.flush();
        }
        fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
    });

#ifdef JXQY_EDITOR_BUILD_BENCHMARK
    if (performanceBenchmarkRequested)
    {
        return runEditorPerformanceBenchmark(
            performanceOptions, processStartupClock);
    }
#endif

    MainWindow mainWindow;
    desktopFileOpenEventRouter.setOpenHandler(
        [&mainWindow](const QString& filePath)
        {
            return mainWindow.openStartupFileArguments({filePath});
        });
    mainWindow.repairDesktopFileAssociations();
    mainWindow.show();
    QTimer::singleShot(
        0,
        &mainWindow,
        [&mainWindow, &desktopFileOpenEventRouter, desktopOpenArguments]()
        {
            const bool startupOpenAccepted =
                mainWindow.openStartupFileArguments(desktopOpenArguments);
            if (startupOpenAccepted && desktopOpenArguments.size() == 1)
            {
                desktopFileOpenEventRouter.discardPendingFilesMatching(
                    desktopOpenArguments);
            }
            desktopFileOpenEventRouter.setReady(true);
        });

    const int exitCode = application.exec();
    mainWindow.prepareForApplicationExit();
    return finishEditorApplicationExit(exitCode);
}
