#pragma once

#include "../core/AssetPreviewLoader.h"

#include <QFileSystemModel>
#include <QSortFilterProxyModel>
#include <QWidget>

class QAudioOutput;
class QGroupBox;
class QLabel;
class QMediaPlayer;
class QMimeData;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QSlider;
class QSplitter;
class QStackedWidget;

class DarkFileSystemProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit DarkFileSystemProxyModel(QObject* parent = nullptr);

    void setAssetsBasePath(const QString& path);
    QVariant data(const QModelIndex& index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    Qt::DropActions supportedDragActions() const override;

protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex& sourceParent) const override;

private:
    QString assetsBasePath;
};

namespace Ui
{
class AssetBrowser;
}

class AssetBrowser : public QWidget
{
    Q_OBJECT

public:
    explicit AssetBrowser(QWidget* parent = nullptr);
    ~AssetBrowser();

    void setAssetsBasePath(const QString& path);
    QString getAssetsBasePath() const;

    void setFilterSuffix(const QString& suffix);
    void setFilterSuffixes(const QStringList& suffixes);

    bool previewRelativePath(const QString& relativePath);

protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

signals:
    void fileSelected(const QString& relativePath);
    void fileDoubleClicked(const QString& relativePath);

private slots:
    void onSearchTextChanged(const QString& text);
    void onFilterChanged(int index);
    void onFileClicked(const QModelIndex& index);
    void onFileDoubleClicked(const QModelIndex& index);
    void onAudioPlayPause();
    void onAudioStop();

private:
    void applyFilter();
    QString getRelativePath(const QString& fullPath) const;
    void updatePathLabel();
    void setupPreviewUi();
    void updatePreviewTranslations();
    void clearPreview();
    void displayPreview(AssetPreviewData preview);
    QString previewFailureText(const AssetPreviewFailure& failure) const;
    void updatePreviewSummary();
    void updateImagePreview();
    void stopAndClearAudio();
    void updateAudioControls();
    void updateAudioTimeLabel();
    bool isCurrentAudioSource() const;

    Ui::AssetBrowser* ui;

    QFileSystemModel* fileSystemModel = nullptr;
    DarkFileSystemProxyModel* proxyModel = nullptr;

    QString assetsBasePath;
    QStringList currentSuffixes;

    QSplitter* browserSplitter = nullptr;
    QGroupBox* previewGroupBox = nullptr;
    QLabel* previewSummaryLabel = nullptr;
    QLabel* previewStatusLabel = nullptr;
    QStackedWidget* previewStack = nullptr;
    QLabel* emptyPreviewLabel = nullptr;
    QScrollArea* imageScrollArea = nullptr;
    QLabel* imagePreviewLabel = nullptr;
    QPlainTextEdit* textPreviewEdit = nullptr;
    QLabel* audioFileLabel = nullptr;
    QLabel* audioTimeLabel = nullptr;
    QPushButton* audioPlayPauseButton = nullptr;
    QPushButton* audioStopButton = nullptr;
    QSlider* audioPositionSlider = nullptr;
    QMediaPlayer* mediaPlayer = nullptr;
    QAudioOutput* audioOutput = nullptr;

    AssetPreviewData currentPreview;
    AssetPreviewFailure lastPreviewFailure;
};
