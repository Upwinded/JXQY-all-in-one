#pragma once

#include "AssetsPathSwitchParticipant.h"
#include "../core/StoryGraphAnalysisCoordinator.h"
#include "../core/StoryGraphResourceContext.h"
#include "../core/StoryGraphRuntimeTrace.h"
#include "../core/StoryGraphTraceMatcher.h"

#include <QByteArray>
#include <QPointer>
#include <QStringList>
#include <QWidget>

#include <optional>

class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QToolButton;
class QTreeWidget;
class StoryGraphView;

class StoryGraphWindow :
    public QWidget,
    public AssetsPathSwitchParticipant
{
    Q_OBJECT

public:
    explicit StoryGraphWindow(QWidget* parent = nullptr);
    ~StoryGraphWindow() override;

    void setResourceContext(
        const StoryGraphResourceContext& context);
    StoryGraphResourceContext resourceContext() const;

    bool submitAnalysis(
        const StoryGraphProjectRequest& request);
    void showAnalysisError(
        const QString& diagnosticCode,
        const QString& message);
    void markStaleAndScheduleRefresh();
    void cancelAnalysis();

    bool isStale() const;
    bool isAnalyzing() const;
    quint64 presentedGeneration() const;
    StoryGraphKind displayedGraphKind() const;
    const StoryGraphProjectResult*
        currentProjectResult() const;

    void bindRuntimeTraceSession(
        const QString& sessionId,
        const QString& runtimeTracePath,
        bool producerActive,
        bool forcedTermination = false);
    void finalizeRuntimeTraceSession(
        const QString& sessionId,
        bool forcedTermination);
    void clearRuntimeTraceSession();
    void clearRuntimeTraceOverlayMemory();
    QString selectedRuntimeTraceSessionId() const;
    StoryGraphRuntimeTraceStreamState
        runtimeTraceStreamState() const;
    qsizetype runtimeTraceEventCount() const;
    const StoryGraphTraceMatchResult&
        runtimeTraceMatchResult() const;
    // These counters advance only when the bounded issue tree is rebuilt or
    // consumes one cached issue row. Idle refreshes leave them unchanged.
    quint64 runtimeTraceIssuePresentationRevision() const;
    quint64 runtimeTraceIssuePresentationRebuildCount() const;
    quint64 runtimeTraceIssuePresentationWorkItemCount()
        const;

    // Resolves and reads the current logical resource path. A digest mismatch
    // is reported only as staleness metadata and never rejects the read.
    bool readCurrentDiskSource(
        const StoryGraphSourceIdentity& source,
        QString& currentAbsolutePath,
        QByteArray& currentBytes,
        bool& matchesAnalyzedContent) const;

    PathScope assetsPathScope() const override;
    Decision prepareAssetsPathSwitch(
        const QString& path) const override;
    bool resolveAssetsPathSwitch(
        Decision decision) override;
    void commitAssetsPathSwitch(
        const QString& path) override;
    QString currentAssetsPath() const override;

signals:
    void sourceNavigationRequested(
        const StoryGraphNode& node);
    void analysisRefreshRequested();
    void graphWindowClosed();

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUi();
    void populateGraphModeCombo();
    void populateNodeTypeFilter();
    void retranslateUi();
    void presentCurrentGraph();
    void presentWarnings();
    void refreshRuntimeTrace();
    void rematchRuntimeTrace();
    bool appendRuntimeTraceEvents(
        qsizetype firstIndex,
        qsizetype eventCount);
    void failRuntimeTraceMatching();
    void applyRuntimeTraceOverlay();
    void presentRuntimeTraceIssues();
    void updateStatusText();
    void updateRuntimeTraceStatusText();
    void setStale(bool stale);
    QString warningSeverityText(
        StoryGraphWarningSeverity severity) const;
    QString runtimeTraceStreamStateText(
        StoryGraphRuntimeTraceStreamState state) const;
    QString runtimeTraceMatchStatusText(
        StoryGraphTraceMatchStatus status) const;
    StoryGraphKind selectedGraphKind() const;
    const StoryGraphResult* selectedGraph() const;
    const StoryGraphLayoutResult* selectedLayout() const;

    StoryGraphAnalysisCoordinator* coordinator = nullptr;
    StoryGraphView* graphView = nullptr;
    QComboBox* graphModeCombo = nullptr;
    QComboBox* nodeTypeFilterCombo = nullptr;
    QLineEdit* searchEdit = nullptr;
    QToolButton* searchNextButton = nullptr;
    QToolButton* zoomInButton = nullptr;
    QToolButton* zoomOutButton = nullptr;
    QToolButton* resetZoomButton = nullptr;
    QToolButton* fitButton = nullptr;
    QPushButton* refreshButton = nullptr;
    QPushButton* clearRuntimeTraceButton = nullptr;
    QLabel* runtimeTraceStatusLabel = nullptr;
    QLabel* statusLabel = nullptr;
    QLabel* staleLabel = nullptr;
    QLabel* zoomLabel = nullptr;
    QTreeWidget* warningTree = nullptr;
    QTreeWidget* runtimeTraceIssueTree = nullptr;
    QTimer* searchDebounceTimer = nullptr;
    QTimer* analysisRefreshTimer = nullptr;
    QTimer* runtimeTraceRefreshTimer = nullptr;

    StoryGraphResourceContext currentResourceContext;
    std::optional<StoryGraphAnalysisBundle>
        currentBundle;
    StoryGraphRuntimeTraceTailer runtimeTraceTailer;
    StoryGraphTraceMatcher runtimeTraceMatcher;
    QString currentRuntimeTraceSessionId;
    QString currentRuntimeTracePath;
    QString assetsPath;
    QString lastDiagnosticCode;
    QString lastDiagnosticMessage;
    qsizetype appliedRuntimeTraceEventCount = 0;
    quint64 requestedGeneration = 0;
    quint64 currentPresentedGeneration = 0;
    quint64 currentRuntimeTraceIssuePresentationRevision = 0;
    quint64 currentRuntimeTraceIssuePresentationRebuildCount = 0;
    quint64 currentRuntimeTraceIssuePresentationWorkItemCount =
        0;
    bool runtimeTraceProducerTerminal = false;
    bool runtimeTraceForcedTermination = false;
    bool runtimeTraceFinalizationIssued = false;
    bool runtimeTraceMatcherFailed = false;
    bool stale = true;
};
