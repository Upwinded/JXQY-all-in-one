#include "EditorPerformanceBenchmark.h"

#include "../core/MapFileEditor.h"
#include "../ui/MainWindow.h"
#include "../ui/MapRenderCanvas.h"
#include "../ui/MinimapWidget.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointF>
#include <QSaveFile>
#include <QSysInfo>
#include <QThread>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numeric>
#include <vector>

namespace
{
class PaintEventCounter : public QObject
{
public:
    int count() const
    {
        return paintEventCount;
    }

    void reset()
    {
        paintEventCount = 0;
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        Q_UNUSED(watched);
        if (event->type() == QEvent::Paint)
            ++paintEventCount;
        return false;
    }

private:
    int paintEventCount = 0;
};

struct SequenceMeasurement
{
    bool valid = true;
    int failedIteration = -1;
    int paintEvents = 0;
    std::vector<double> samplesMilliseconds;
    std::vector<double> canvasPaintSamplesMilliseconds;
    std::vector<QVariantMap> canvasPaintBreakdowns;
};

bool waitForPaint(PaintEventCounter& counter, int timeoutMilliseconds = 5000)
{
    QElapsedTimer timeoutClock;
    timeoutClock.start();
    while (counter.count() == 0 && timeoutClock.elapsed() < timeoutMilliseconds)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (counter.count() == 0)
            QThread::msleep(1);
    }
    return counter.count() > 0;
}

bool requestAndWaitForPaint(QWidget& widget, PaintEventCounter& counter)
{
    counter.reset();
    widget.update();
    return waitForPaint(counter);
}

SequenceMeasurement measureSequence(
    int iterations,
    PaintEventCounter& counter,
    QWidget* measuredWidget,
    const std::function<void(int)>& action)
{
    SequenceMeasurement measurement;
    measurement.samplesMilliseconds.reserve(static_cast<std::size_t>(iterations));

    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        counter.reset();
        QElapsedTimer actionClock;
        actionClock.start();
        action(iteration);
        if (!waitForPaint(counter))
        {
            measurement.valid = false;
            measurement.failedIteration = iteration;
            break;
        }

        measurement.samplesMilliseconds.push_back(
            static_cast<double>(actionClock.nsecsElapsed()) / 1000000.0);
        measurement.paintEvents += counter.count();
        if (measuredWidget)
        {
            const QVariantMap paintBreakdown = measuredWidget->property(
                "performancePaintBreakdownMs").toMap();
            bool totalOk = false;
            const double totalMilliseconds = paintBreakdown.value(
                "total").toDouble(&totalOk);
            if (totalOk)
            {
                measurement.canvasPaintSamplesMilliseconds.push_back(
                    totalMilliseconds);
                measurement.canvasPaintBreakdowns.push_back(paintBreakdown);
            }
        }
    }

    return measurement;
}

QJsonObject sequenceToJson(const SequenceMeasurement& measurement)
{
    QJsonObject result;
    result["valid"] = measurement.valid;
    result["failed_iteration"] = measurement.failedIteration;
    result["paint_events"] = measurement.paintEvents;
    result["iterations_completed"] =
        static_cast<int>(measurement.samplesMilliseconds.size());

    QJsonArray samples;
    for (double sample : measurement.samplesMilliseconds)
        samples.append(sample);
    result["samples_ms"] = samples;

    QJsonArray canvasPaintSamples;
    for (double sample : measurement.canvasPaintSamplesMilliseconds)
        canvasPaintSamples.append(sample);
    result["canvas_paint_samples_ms"] = canvasPaintSamples;

    QJsonArray canvasPaintBreakdowns;
    for (const QVariantMap& breakdown : measurement.canvasPaintBreakdowns)
        canvasPaintBreakdowns.append(QJsonObject::fromVariantMap(breakdown));
    result["canvas_paint_breakdowns_ms"] = canvasPaintBreakdowns;

    if (measurement.samplesMilliseconds.empty())
        return result;

    const double total = std::accumulate(
        measurement.samplesMilliseconds.begin(),
        measurement.samplesMilliseconds.end(), 0.0);
    std::vector<double> sorted = measurement.samplesMilliseconds;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t percentileIndex = std::min(
        sorted.size() - 1,
        static_cast<std::size_t>(std::ceil(sorted.size() * 0.95)) - 1);

    result["total_ms"] = total;
    result["average_ms"] = total / sorted.size();
    result["minimum_ms"] = sorted.front();
    result["p95_ms"] = sorted[percentileIndex];
    result["maximum_ms"] = sorted.back();
    return result;
}

bool writeJsonReport(const QString& reportPath, const QJsonObject& report)
{
    const QByteArray data = QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (reportPath.isEmpty())
    {
        const std::size_t bytesWritten = std::fwrite(
            data.constData(), 1, static_cast<std::size_t>(data.size()), stdout);
        std::fflush(stdout);
        return bytesWritten == static_cast<std::size_t>(data.size());
    }

    const QFileInfo reportInfo(reportPath);
    QDir reportDirectory = reportInfo.dir();
    if (!reportDirectory.exists() && !reportDirectory.mkpath("."))
        return false;

    QSaveFile reportFile(reportPath);
    if (!reportFile.open(QIODevice::WriteOnly))
        return false;
    if (reportFile.write(data) != data.size())
        return false;
    return reportFile.commit();
}

double calculateMapArtCoverage(
    MapRenderCanvas& canvas, PaintEventCounter& counter, bool& rendered)
{
    const bool obstacleVisible = canvas.isObstacleVisible();
    const bool trapVisible = canvas.isTrapVisible();
    const bool npcVisible = canvas.isNpcVisible();
    const bool objectVisible = canvas.isObjectVisible();
    const bool gridVisible = canvas.isGridVisible();
    const bool coordinateVisible = canvas.isCoordinateVisible();

    canvas.setObstacleVisible(false);
    canvas.setTrapVisible(false);
    canvas.setNpcVisible(false);
    canvas.setObjectVisible(false);
    canvas.setGridVisible(false);
    canvas.setCoordinateVisible(false);
    rendered = requestAndWaitForPaint(canvas, counter);

    const QImage image = canvas.grab().toImage().convertToFormat(QImage::Format_ARGB32);

    canvas.setObstacleVisible(obstacleVisible);
    canvas.setTrapVisible(trapVisible);
    canvas.setNpcVisible(npcVisible);
    canvas.setObjectVisible(objectVisible);
    canvas.setGridVisible(gridVisible);
    canvas.setCoordinateVisible(coordinateVisible);
    rendered = requestAndWaitForPaint(canvas, counter) && rendered;

    if (image.isNull())
        return 0.0;

    const int stepX = std::max(1, image.width() / 160);
    const int stepY = std::max(1, image.height() / 120);
    int sampledPixels = 0;
    int nonBackgroundPixels = 0;
    for (int y = 0; y < image.height(); y += stepY)
    {
        for (int x = 0; x < image.width(); x += stepX)
        {
            const QColor color = QColor::fromRgba(image.pixel(x, y));
            ++sampledPixels;
            if (std::abs(color.red() - 30) > 6 ||
                std::abs(color.green() - 30) > 6 ||
                std::abs(color.blue() - 30) > 6)
            {
                ++nonBackgroundPixels;
            }
        }
    }

    return sampledPixels > 0
        ? static_cast<double>(nonBackgroundPixels) / sampledPixels
        : 0.0;
}

QJsonObject createBaseReport(
    const EditorPerformanceBenchmarkOptions& options,
    const QString& mapPath,
    const QString& assetsPath)
{
    QJsonObject report;
    report["schema_version"] = 1;
    report["valid"] = false;
    report["map_path"] = mapPath;
    report["assets_path"] = assetsPath;
    report["map_file_bytes"] =
        static_cast<double>(QFileInfo(mapPath).size());
    report["qt_version"] = QString::fromLatin1(qVersion());
    report["platform"] = QSysInfo::prettyProductName();
    report["cpu_architecture"] = QSysInfo::currentCpuArchitecture();
#ifdef NDEBUG
    report["build_type"] = "Release";
#else
    report["build_type"] = "Debug";
#endif

    QJsonObject contract;
    contract["hidden_window"] = true;
    contract["window_width"] = options.windowWidth;
    contract["window_height"] = options.windowHeight;
    contract["interaction_iterations"] = options.interactionIterations;
    contract["zoom_levels"] = QJsonArray{1.25, 1.0, 0.8, 1.0};
    contract["interaction_cache_state"] = "warm_after_initial_map_render";
    report["contract"] = contract;
    return report;
}
}

int runEditorPerformanceBenchmark(
    const EditorPerformanceBenchmarkOptions& options,
    const QElapsedTimer& processStartupClock)
{
    const QString mapPath = QFileInfo(options.mapPath).absoluteFilePath();
    const QString assetsPath = QDir(options.assetsPath).absolutePath();
    QJsonObject report = createBaseReport(options, mapPath, assetsPath);

    auto fail = [&](const QString& stage, const QString& message)
    {
        report["valid"] = false;
        report["error_stage"] = stage;
        report["error"] = message;
        if (!writeJsonReport(options.reportPath, report))
        {
            std::fprintf(stderr, "Failed to write editor performance report.\n");
        }
        return 1;
    };

    QElapsedTimer mainWindowClock;
    mainWindowClock.start();
    MainWindow mainWindow;
    mainWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
    mainWindow.resize(options.windowWidth, options.windowHeight);
    PaintEventCounter mainWindowPaintCounter;
    mainWindow.installEventFilter(&mainWindowPaintCounter);
    mainWindow.show();
    if (!waitForPaint(mainWindowPaintCounter))
        return fail("startup", "Main window did not produce a paint event.");

    report["process_to_main_window_first_paint_ms"] =
        static_cast<double>(processStartupClock.nsecsElapsed()) / 1000000.0;
    report["main_window_to_first_paint_ms"] =
        static_cast<double>(mainWindowClock.nsecsElapsed()) / 1000000.0;
    report["main_window_paint_events"] = mainWindowPaintCounter.count();

    if (!QFileInfo(mapPath).isFile())
        return fail("configuration", "Benchmark map does not exist.");
    if (!QFileInfo(assetsPath).isDir())
        return fail("configuration", "Benchmark assets directory does not exist.");
    const QString relativeMapPath = QDir(assetsPath).relativeFilePath(mapPath);
    if (relativeMapPath == ".." || relativeMapPath.startsWith("../"))
        return fail("configuration", "Benchmark map is outside the assets directory.");
    if (options.interactionIterations < 4 || options.interactionIterations > 100)
        return fail("configuration", "Interaction iterations must be between 4 and 100.");
    if (options.windowWidth < 800 || options.windowHeight < 600)
        return fail("configuration", "Benchmark window must be at least 800x600.");

    QElapsedTimer mapOpenClock;
    mapOpenClock.start();
    QElapsedTimer synchronousMapOpenClock;
    synchronousMapOpenClock.start();
    bool opened = false;
    const bool invoked = QMetaObject::invokeMethod(
        &mainWindow,
        "openFileByType",
        Qt::DirectConnection,
        Q_RETURN_ARG(bool, opened),
        Q_ARG(QString, mapPath));
    report["map_open_synchronous_ms"] =
        static_cast<double>(synchronousMapOpenClock.nsecsElapsed()) / 1000000.0;
    MapRenderCanvas* canvas = mainWindow.findChild<MapRenderCanvas*>();
    if (!invoked || !opened || !canvas)
        return fail("map_open", "MainWindow could not open the benchmark map.");

    PaintEventCounter canvasPaintCounter;
    canvas->installEventFilter(&canvasPaintCounter);
    canvas->update();
    if (!waitForPaint(canvasPaintCounter))
        return fail("map_open", "Map canvas did not produce its first paint event.");
    report["map_open_to_first_paint_ms"] =
        static_cast<double>(mapOpenClock.nsecsElapsed()) / 1000000.0;
    report["map_open_paint_events"] = canvasPaintCounter.count();
    report["map_open_canvas_paint_breakdown_ms"] =
        QJsonObject::fromVariantMap(canvas->property(
            "performancePaintBreakdownMs").toMap());
    if (MinimapWidget* minimap = mainWindow.findChild<MinimapWidget*>())
    {
        report["map_open_minimap_rebuild_breakdown_ms"] =
            QJsonObject::fromVariantMap(minimap->property(
                "performanceRebuildBreakdownMs").toMap());
    }

    MapFileEditor mapMetadata;
    if (!mapMetadata.loadFromFile(mapPath.toUtf8().toStdString()))
        return fail("map_metadata", "Map metadata could not be reloaded.");

    const int mapWidth = mapMetadata.getWidth();
    const int mapHeight = mapMetadata.getHeight();
    if (mapWidth <= 0 || mapHeight <= 0)
        return fail("map_metadata", "Map dimensions must be positive.");

    QJsonObject mapSize;
    mapSize["width"] = mapWidth;
    mapSize["height"] = mapHeight;
    report["map_size"] = mapSize;

    QJsonObject canvasSize;
    canvasSize["width"] = canvas->width();
    canvasSize["height"] = canvas->height();
    report["canvas_size"] = canvasSize;

    bool coverageRendered = false;
    const double mapArtCoverage = calculateMapArtCoverage(
        *canvas, canvasPaintCounter, coverageRendered);
    report["map_art_coverage_ratio"] = mapArtCoverage;
    if (!coverageRendered || mapArtCoverage < 0.02)
    {
        return fail(
            "map_render",
            "The map canvas did not render enough non-background map art.");
    }

    const QPoint zoomAnchor(canvas->width() / 2, canvas->height() / 2);
    canvas->zoomAtPoint(zoomAnchor, 1.0f);
    if (!requestAndWaitForPaint(*canvas, canvasPaintCounter))
        return fail("zoom_setup", "Could not render the initial zoom state.");

    const std::array<float, 4> zoomLevels = {1.25f, 1.0f, 0.8f, 1.0f};
    const SequenceMeasurement zoomMeasurement = measureSequence(
        options.interactionIterations,
        canvasPaintCounter,
        canvas,
        [&](int iteration)
        {
            canvas->zoomAtPoint(
                zoomAnchor,
                zoomLevels[static_cast<std::size_t>(iteration) % zoomLevels.size()]);
        });
    report["zoom"] = sequenceToJson(zoomMeasurement);
    if (!zoomMeasurement.valid)
        return fail("zoom", "A zoom iteration did not produce a paint event.");

    canvas->zoomAtPoint(zoomAnchor, 1.0f);
    canvas->centerOnTile(mapWidth / 2, mapHeight / 2);
    if (!requestAndWaitForPaint(*canvas, canvasPaintCounter))
        return fail("pan_setup", "Could not render the initial pan state.");

    const std::array<QPointF, 8> normalizedPanPositions = {
        QPointF(0.10, 0.10),
        QPointF(0.90, 0.10),
        QPointF(0.90, 0.90),
        QPointF(0.10, 0.90),
        QPointF(0.50, 0.50),
        QPointF(0.25, 0.50),
        QPointF(0.75, 0.50),
        QPointF(0.50, 0.25)
    };
    const SequenceMeasurement panMeasurement = measureSequence(
        options.interactionIterations,
        canvasPaintCounter,
        canvas,
        [&](int iteration)
        {
            const QPointF normalized = normalizedPanPositions[
                static_cast<std::size_t>(iteration) % normalizedPanPositions.size()];
            const int tileX = std::clamp(
                static_cast<int>(std::lround(normalized.x() * (mapWidth - 1))),
                0, mapWidth - 1);
            const int tileY = std::clamp(
                static_cast<int>(std::lround(normalized.y() * (mapHeight - 1))),
                0, mapHeight - 1);
            canvas->centerOnTile(tileX, tileY);
        });
    report["pan"] = sequenceToJson(panMeasurement);
    if (!panMeasurement.valid)
        return fail("pan", "A pan iteration did not produce a paint event.");

    report["valid"] = true;
    mainWindow.close();
    QApplication::processEvents();
    if (!writeJsonReport(options.reportPath, report))
    {
        std::fprintf(stderr, "Failed to write editor performance report.\n");
        return 1;
    }
    return 0;
}
