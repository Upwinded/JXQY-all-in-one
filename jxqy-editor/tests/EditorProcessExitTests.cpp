#include "../core/EditorProcessLifecycle.h"
#include "../core/StoryGraphAnalysisCoordinator.h"
#include "../ui/BatchConvertWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace
{
constexpr int StoryGraphExitCode = 71;
constexpr int NormalExitCode = 73;
constexpr int BatchConversionExitCode = 74;
constexpr int BatchValidationExitCode = 75;
constexpr int FinishedWorkerObjectExitCode = 76;

bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

class BlockingGate
{
public:
    void enterAndWait()
    {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            entered = true;
        }
        condition.notify_all();

        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(
            lock,
            [this]()
            {
                return released;
            });
    }

    bool waitForEntry(int timeoutMilliseconds = 5000)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(
            lock,
            std::chrono::milliseconds(
                timeoutMilliseconds),
            [this]()
            {
                return entered;
            });
    }

    void release()
    {
        {
            const std::lock_guard<std::mutex> lock(
                mutex);
            released = true;
        }
        condition.notify_all();
    }

private:
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool released = false;
};

class ExitMarker
{
public:
    explicit ExitMarker(QString markerPath)
        : path(std::move(markerPath))
    {
    }

    ~ExitMarker()
    {
        QFile marker(path);
        if (marker.open(
                QIODevice::WriteOnly |
                QIODevice::Truncate))
        {
            marker.write("normal-destruction\n");
            marker.close();
        }
    }

private:
    QString path;
};

bool writeTextFile(
    const QString& path,
    const QByteArray& content)
{
    QDir().mkpath(
        QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(
            QIODevice::WriteOnly |
            QIODevice::Truncate))
    {
        return false;
    }
    return file.write(content) ==
        content.size();
}

StoryGraphProjectRequest storyGraphRequest()
{
    StoryGraphProjectRequest request;
    request.analysisGeneration = 1;
    request.entrySource.identity.portableRootKey =
        QStringLiteral("active:process-exit:0");
    request.entrySource.identity.virtualPath =
        QStringLiteral(
            "script/map/process_exit/entry.lua");
    request.entrySource.utf8Bytes =
        QByteArray("runscript(\"blocked.lua\")\n");
    request.entryMapContext.state =
        StoryGraphMapContextState::Known;
    request.entryMapContext.effectiveMapFolder =
        QStringLiteral("process_exit");

    StoryGraphContentRoot root;
    root.kind = StoryGraphContentRootKind::Active;
    root.portableRootKey =
        request.entrySource.identity.portableRootKey;
    request.orderedContentRoots.append(root);
    return request;
}

int runStoryGraphChild(
    QCoreApplication& application,
    const QString& markerPath)
{
    ExitMarker marker(markerPath);
    const auto gate =
        std::make_shared<BlockingGate>();
    int eventLoopExitCode = 120;
    {
        StoryGraphAnalysisCoordinator coordinator;
        const bool accepted = coordinator.submit(
            storyGraphRequest(),
            [gate](
                const StoryGraphContentRoot&,
                const QString&)
            {
                gate->enterAndWait();
                return StoryGraphReadResult();
            });
        if (!accepted || !gate->waitForEntry())
            return 121;

        QTimer::singleShot(
            0,
            &application,
            [&application]()
            {
                application.exit(
                    StoryGraphExitCode);
            });
        eventLoopExitCode = application.exec();
    }
    return finishEditorApplicationExit(
        eventLoopExitCode);
}

int runBatchWorkerChild(
    QCoreApplication& application,
    const QString& markerPath,
    BatchConvertWorkerKind workerKind,
    int exitCode)
{
    ExitMarker marker(markerPath);
    const QString fixtureRoot =
        QFileInfo(markerPath).dir().filePath(
            workerKind ==
                    BatchConvertWorkerKind::
                        Conversion
                ? QStringLiteral(
                      "batch-conversion")
                : QStringLiteral(
                      "batch-validation"));
    const QString sourceRoot =
        QDir(fixtureRoot).filePath(
            QStringLiteral("source"));
    const QString assetsRoot =
        QDir(fixtureRoot).filePath(
            QStringLiteral("assets"));
    if (!QDir().mkpath(sourceRoot) ||
        !QDir().mkpath(
            QDir(assetsRoot).filePath(
                QStringLiteral("script"))) ||
        !writeTextFile(
            QDir(sourceRoot).filePath(
                QStringLiteral("plain.txt")),
            QByteArray("value\n")) ||
        !writeTextFile(
            QDir(assetsRoot).filePath(
                QStringLiteral(
                    "script/test.lua")),
            QByteArray("return 1\n")))
    {
        return 126;
    }

    const auto gate =
        std::make_shared<BlockingGate>();
    const QString workerFinishedMarkerPath =
        markerPath +
        QStringLiteral(".worker-finished");
    setBatchConvertWorkerTestHookForTests(
        [gate, workerKind,
         workerFinishedMarkerPath](
            BatchConvertWorkerKind actualKind)
        {
            if (actualKind == workerKind)
            {
                gate->enterAndWait();
                if (actualKind ==
                    BatchConvertWorkerKind::
                        Conversion)
                {
                    writeTextFile(
                        workerFinishedMarkerPath,
                        QByteArray(
                            "conversion-worker-finished\n"));
                }
            }
        });

    auto* window = new BatchConvertWindow;
    window->setAttribute(
        Qt::WA_DontShowOnScreen,
        true);
    bool started = false;
    if (workerKind ==
        BatchConvertWorkerKind::Conversion)
    {
        QLineEdit* sourceEdit =
            window->findChild<QLineEdit*>(
                QStringLiteral("sourceDirEdit"));
        QLineEdit* outputEdit =
            window->findChild<QLineEdit*>(
                QStringLiteral("outputDirEdit"));
        QPushButton* startButton =
            window->findChild<QPushButton*>(
                QStringLiteral("startButton"));
        if (sourceEdit && outputEdit && startButton)
        {
            sourceEdit->setText(sourceRoot);
            outputEdit->setText(
                QDir(fixtureRoot).filePath(
                    QStringLiteral("output")));
            startButton->click();
            started = true;
        }
    }
    else
    {
        QLineEdit* outputEdit =
            window->findChild<QLineEdit*>(
                QStringLiteral("outputDirEdit"));
        QPushButton* validateButton =
            window->findChild<QPushButton*>(
                QStringLiteral(
                    "validateScriptsButton"));
        if (outputEdit && validateButton)
        {
            outputEdit->setText(assetsRoot);
            validateButton->click();
            started = true;
        }
    }

    if (!started ||
        !gate->waitForEntry() ||
        activeEditorBackgroundWorkerCount() == 0)
    {
        delete window;
        return finishEditorApplicationExit(127);
    }

    QElapsedTimer destructionTimer;
    destructionTimer.start();
    delete window;
    if (destructionTimer.elapsed() >= 250 ||
        activeEditorBackgroundWorkerCount() == 0)
    {
        return finishEditorApplicationExit(128);
    }

    if (workerKind ==
        BatchConvertWorkerKind::Conversion)
    {
        std::thread(
            [gate]()
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(
                        200));
                gate->release();
            })
            .detach();
    }

    QTimer::singleShot(
        0,
        &application,
        [&application, exitCode]()
        {
            application.exit(exitCode);
        });
    const int eventLoopExitCode =
        application.exec();
    return finishEditorApplicationExit(
        eventLoopExitCode);
}

int runNormalChild(
    QCoreApplication& application,
    const QString& markerPath)
{
    ExitMarker marker(markerPath);
    QThread* worker = QThread::create([]() {});
    registerEditorBackgroundWorker(worker);
    worker->start();
    if (!worker->wait(5000))
    {
        delete worker;
        return 129;
    }
    delete worker;
    if (activeEditorBackgroundWorkerCount() != 0)
        return 130;

    QTimer::singleShot(
        0,
        &application,
        [&application]()
        {
            application.exit(NormalExitCode);
        });
    const int eventLoopExitCode =
        application.exec();
    return finishEditorApplicationExit(
        eventLoopExitCode);
}

int runFinishedWorkerObjectChild(
    const QString& markerPath)
{
    ExitMarker marker(markerPath);
    QThread* worker = QThread::create([]() {});
    registerEditorBackgroundWorker(worker);
    worker->start();
    if (!worker->wait(5000))
    {
        delete worker;
        return 131;
    }
    if (activeEditorBackgroundWorkerCount() == 0)
    {
        delete worker;
        return 132;
    }

    // The platform thread has returned, but retaining its QThread object must
    // retain the process-exit registration until that object is safely
    // destroyed. finishEditorApplicationExit() is expected not to return.
    return finishEditorApplicationExit(
        FinishedWorkerObjectExitCode);
}

struct ChildResult
{
    bool finished = false;
    int exitCode = -1;
    QProcess::ExitStatus exitStatus =
        QProcess::CrashExit;
    qint64 elapsedMilliseconds = -1;
    QByteArray standardError;
};

ChildResult runChild(
    const QString& mode,
    const QString& markerPath)
{
    QProcess child;
    child.setProgram(
        QCoreApplication::applicationFilePath());
    child.setArguments({mode, markerPath});
    child.start();

    ChildResult result;
    if (!child.waitForStarted(5000))
    {
        result.standardError =
            child.errorString().toUtf8();
        return result;
    }

    QElapsedTimer timer;
    timer.start();
    result.finished = child.waitForFinished(3000);
    result.elapsedMilliseconds = timer.elapsed();
    if (!result.finished)
    {
        child.kill();
        child.waitForFinished(5000);
    }
    result.exitCode = child.exitCode();
    result.exitStatus = child.exitStatus();
    result.standardError =
        child.readAllStandardError();
    return result;
}

bool checkHardExitChild(
    const QString& mode,
    int expectedExitCode,
    const QString& markerPath,
    const char* description)
{
    QFile::remove(markerPath);
    const ChildResult result =
        runChild(mode, markerPath);
    const bool passed =
        result.finished &&
        result.exitStatus == QProcess::NormalExit &&
        result.exitCode == expectedExitCode &&
        result.elapsedMilliseconds >= 0 &&
        result.elapsedMilliseconds < 2500 &&
        !QFileInfo::exists(markerPath);
    if (!passed)
    {
        std::cerr
            << description
            << ": finished=" << result.finished
            << " exitCode=" << result.exitCode
            << " exitStatus="
            << static_cast<int>(result.exitStatus)
            << " elapsedMilliseconds="
            << result.elapsedMilliseconds
            << " markerExists="
            << QFileInfo::exists(markerPath)
            << " stderr="
            << result.standardError.constData()
            << '\n';
    }
    return check(passed, description);
}

bool testProcessExitPolicy()
{
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create process-exit test directory"))
    {
        return false;
    }

    bool passed = true;
    passed &= checkHardExitChild(
        QStringLiteral("--child-story-graph"),
        StoryGraphExitCode,
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("story.marker")),
        "blocked story graph worker forces prompt hard exit");
    passed &= checkHardExitChild(
        QStringLiteral(
            "--child-batch-conversion"),
        BatchConversionExitCode,
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral(
                "batch-conversion.marker")),
        "batch conversion closes its window before protected worker exit");
    passed &= check(
        QFileInfo::exists(
            QDir(temporaryDirectory.path()).filePath(
                QStringLiteral(
                    "batch-conversion.marker.worker-finished"))),
        "process exit waits for the cancelled batch conversion worker to finish");
    passed &= checkHardExitChild(
        QStringLiteral(
            "--child-batch-validation"),
        BatchValidationExitCode,
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral(
                "batch-validation.marker")),
        "blocked script validation detaches its window and forces prompt hard exit");
    passed &= checkHardExitChild(
        QStringLiteral(
            "--child-finished-worker-object"),
        FinishedWorkerObjectExitCode,
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral(
                "finished-worker-object.marker")),
        "finished worker remains registered until its QThread object is destroyed");

    const QString normalMarker =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("normal.marker"));
    QFile::remove(normalMarker);
    const ChildResult normal =
        runChild(
            QStringLiteral("--child-normal"),
            normalMarker);
    passed &= check(
        normal.finished &&
            normal.exitStatus ==
                QProcess::NormalExit &&
            normal.exitCode == NormalExitCode &&
            QFileInfo::exists(normalMarker),
        "worker-free exit follows normal automatic destruction");
    return passed;
}
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(
        false);
    const QStringList arguments =
        application.arguments();
    if (arguments.size() == 3)
    {
        const QString& mode = arguments.at(1);
        const QString& markerPath =
            arguments.at(2);
        if (mode == QStringLiteral(
                "--child-story-graph"))
        {
            return runStoryGraphChild(
                application,
                markerPath);
        }
        if (mode == QStringLiteral(
                "--child-normal"))
        {
            return runNormalChild(
                application,
                markerPath);
        }
        if (mode == QStringLiteral(
                "--child-batch-conversion"))
        {
            return runBatchWorkerChild(
                application,
                markerPath,
                BatchConvertWorkerKind::
                    Conversion,
                BatchConversionExitCode);
        }
        if (mode == QStringLiteral(
                "--child-batch-validation"))
        {
            return runBatchWorkerChild(
                application,
                markerPath,
                BatchConvertWorkerKind::
                    ScriptValidation,
                BatchValidationExitCode);
        }
        if (mode == QStringLiteral(
                "--child-finished-worker-object"))
        {
            return runFinishedWorkerObjectChild(
                markerPath);
        }
        return 125;
    }

    const bool passed = testProcessExitPolicy();
    if (passed)
    {
        std::cout
            << "All editor process-exit tests passed\n";
    }
    return passed ? 0 : 1;
}
