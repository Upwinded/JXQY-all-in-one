#include "AssetBrowser.h"
#include "ui_AssetBrowser.h"
#include "AssetDragDrop.h"
#include "../core/EditorAssetPath.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QAudioOutput>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMediaPlayer>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QSplitter>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace
{
QStringList globPatterns(const QStringList& suffixes)
{
    QStringList patterns;
    patterns.reserve(suffixes.size());
    for (const QString& suffix : suffixes)
        patterns.append(QStringLiteral("*.%1").arg(suffix));
    return patterns;
}

QString byteSizeText(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KiB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

QString timeText(qint64 milliseconds)
{
    const qint64 totalSeconds = (std::max<qint64>)(0, milliseconds) / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0)
    {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}
}

DarkFileSystemProxyModel::DarkFileSystemProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
}

void DarkFileSystemProxyModel::setAssetsBasePath(const QString& path)
{
    assetsBasePath = path.trimmed().isEmpty()
        ? QString() : EditorAssetPath::normalizedAbsolutePath(path);
}

QVariant DarkFileSystemProxyModel::data(const QModelIndex& index, int role) const
{
    if (role == Qt::ForegroundRole)
        return QApplication::palette().color(QPalette::Text);
    return QSortFilterProxyModel::data(index, role);
}

Qt::ItemFlags DarkFileSystemProxyModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags itemFlags = QSortFilterProxyModel::flags(index);
    const auto* source = sourceModel();
    const auto* fileSystem = qobject_cast<const QFileSystemModel*>(source);
    if (!source)
        return itemFlags & ~Qt::ItemIsDragEnabled & ~Qt::ItemIsDropEnabled;
    const QModelIndex sourceIndex = mapToSource(index);
    if (!index.isValid() || index.column() != 0 || !fileSystem ||
        fileSystem->isDir(sourceIndex))
    {
        itemFlags &= ~Qt::ItemIsDragEnabled;
    }
    else
    {
        itemFlags |= Qt::ItemIsDragEnabled;
    }
    itemFlags &= ~Qt::ItemIsDropEnabled;
    return itemFlags;
}

QStringList DarkFileSystemProxyModel::mimeTypes() const
{
    return {AssetDragDrop::mimeType()};
}

QMimeData* DarkFileSystemProxyModel::mimeData(
    const QModelIndexList& indexes) const
{
    const auto* fileSystem = qobject_cast<const QFileSystemModel*>(sourceModel());
    if (!fileSystem || assetsBasePath.isEmpty())
        return nullptr;

    QString selectedPath;
    for (const QModelIndex& index : indexes)
    {
        if (!index.isValid() || index.column() != 0)
            continue;
        const QModelIndex sourceIndex = mapToSource(index);
        if (fileSystem->isDir(sourceIndex))
            continue;
        const QString filePath = fileSystem->filePath(sourceIndex);
        if (!selectedPath.isEmpty() &&
            EditorAssetPath::logicalComparisonKey(selectedPath) !=
                EditorAssetPath::logicalComparisonKey(filePath))
        {
            return nullptr;
        }
        selectedPath = filePath;
    }
    QString relativePath;
    if (selectedPath.isEmpty() ||
        !EditorAssetPath::makeLogicalResourceRelativePath(
            assetsBasePath, selectedPath, relativePath))
    {
        return nullptr;
    }

    return AssetDragDrop::createMimeData(
        assetsBasePath, relativePath);
}

Qt::DropActions DarkFileSystemProxyModel::supportedDragActions() const
{
    return Qt::CopyAction;
}

bool DarkFileSystemProxyModel::filterAcceptsRow(
    int sourceRow, const QModelIndex& sourceParent) const
{
    const auto* source = sourceModel();
    if (!source)
        return false;
    const QModelIndex sourceIndex = source->index(sourceRow, 0, sourceParent);
    const auto* fileSystem = qobject_cast<const QFileSystemModel*>(source);

    // QFileSystemModel loads subdirectories lazily. Keep directory navigation
    // available so recursive filtering can discover matching files after expand.
    if (fileSystem && fileSystem->isDir(sourceIndex))
        return true;
    return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
}

AssetBrowser::AssetBrowser(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::AssetBrowser)
{
    ui->setupUi(this);

    ui->filterCombo->setItemData(0, "");
    ui->filterCombo->setItemData(
        1, globPatterns(AssetPreviewLoader::imageSuffixes()).join(','));
    ui->filterCombo->setItemData(2, "*.ini");
    ui->filterCombo->setItemData(3, "*.txt,*.lua");
    ui->filterCombo->setItemData(4, "*.map");
    ui->filterCombo->setItemData(
        5, globPatterns(AssetPreviewLoader::audioSuffixes()).join(','));
    ui->filterCombo->setItemData(6, "*.avi,*.mp4,*.mkv");

    fileSystemModel = new QFileSystemModel(this);
    fileSystemModel->setFilter(
        QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    fileSystemModel->setReadOnly(true);
    fileSystemModel->setResolveSymlinks(false);

    proxyModel = new DarkFileSystemProxyModel(this);
    proxyModel->setSourceModel(fileSystemModel);
    proxyModel->setRecursiveFilteringEnabled(true);
    proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

    ui->treeView->setModel(proxyModel);
    ui->treeView->setDragEnabled(true);
    ui->treeView->setDragDropMode(QAbstractItemView::DragOnly);
    ui->treeView->setDefaultDropAction(Qt::CopyAction);
    ui->treeView->hideColumn(1);
    ui->treeView->hideColumn(2);
    ui->treeView->hideColumn(3);
    ui->treeView->header()->setStretchLastSection(true);

    setupPreviewUi();

    audioOutput = new QAudioOutput(this);
    audioOutput->setVolume(0.35f);
    mediaPlayer = new QMediaPlayer(this);
    mediaPlayer->setObjectName(QStringLiteral("assetAudioMediaPlayer"));
    mediaPlayer->setAudioOutput(audioOutput);

    connect(ui->searchEdit, &QLineEdit::textChanged,
        this, &AssetBrowser::onSearchTextChanged);
    connect(ui->filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &AssetBrowser::onFilterChanged);
    connect(ui->treeView, &QTreeView::clicked,
        this, &AssetBrowser::onFileClicked);
    connect(ui->treeView, &QTreeView::doubleClicked,
        this, &AssetBrowser::onFileDoubleClicked);
    connect(audioPlayPauseButton, &QPushButton::clicked,
        this, &AssetBrowser::onAudioPlayPause);
    connect(audioStopButton, &QPushButton::clicked,
        this, &AssetBrowser::onAudioStop);
    connect(audioPositionSlider, &QSlider::sliderMoved,
        this, [this](int position)
        {
            if (isCurrentAudioSource())
                mediaPlayer->setPosition(position);
        });
    connect(mediaPlayer, &QMediaPlayer::positionChanged,
        this, [this](qint64 position)
        {
            if (!isCurrentAudioSource())
                return;
            if (!audioPositionSlider->isSliderDown())
            {
                audioPositionSlider->setValue(static_cast<int>((std::min)(
                    position,
                    static_cast<qint64>((std::numeric_limits<int>::max)()))));
            }
            updateAudioTimeLabel();
        });
    connect(mediaPlayer, &QMediaPlayer::durationChanged,
        this, [this](qint64 duration)
        {
            if (!isCurrentAudioSource())
                return;
            audioPositionSlider->setRange(0, static_cast<int>((std::min)(
                duration,
                static_cast<qint64>((std::numeric_limits<int>::max)()))));
            updatePreviewSummary();
            updateAudioTimeLabel();
        });
    connect(mediaPlayer, &QMediaPlayer::mediaStatusChanged,
        this, [this](QMediaPlayer::MediaStatus)
        {
            if (!isCurrentAudioSource())
                return;
            updateAudioControls();
        });
    connect(mediaPlayer, &QMediaPlayer::playbackStateChanged,
        this, [this](QMediaPlayer::PlaybackState)
        {
            if (!isCurrentAudioSource())
                return;
            updateAudioControls();
        });
    connect(mediaPlayer, &QMediaPlayer::errorOccurred,
        this, [this](QMediaPlayer::Error, const QString& errorText)
        {
            if (!isCurrentAudioSource())
                return;
            previewStatusLabel->setText(
                tr("音频加载失败：%1").arg(errorText));
            audioPlayPauseButton->setEnabled(false);
        });

    clearPreview();
}

AssetBrowser::~AssetBrowser()
{
    stopAndClearAudio();
    proxyModel->setSourceModel(nullptr);
    delete ui;
}

void AssetBrowser::setupPreviewUi()
{
    ui->mainLayout->removeWidget(ui->treeView);
    browserSplitter = new QSplitter(Qt::Vertical, this);
    browserSplitter->setObjectName(QStringLiteral("assetBrowserSplitter"));
    browserSplitter->addWidget(ui->treeView);

    previewGroupBox = new QGroupBox(browserSplitter);
    previewGroupBox->setObjectName(QStringLiteral("assetPreviewGroupBox"));
    auto* previewLayout = new QVBoxLayout(previewGroupBox);
    previewLayout->setContentsMargins(6, 6, 6, 6);
    previewLayout->setSpacing(4);

    previewSummaryLabel = new QLabel(previewGroupBox);
    previewSummaryLabel->setObjectName(QStringLiteral("assetPreviewSummaryLabel"));
    previewSummaryLabel->setWordWrap(true);
    previewSummaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    previewLayout->addWidget(previewSummaryLabel);

    previewStatusLabel = new QLabel(previewGroupBox);
    previewStatusLabel->setObjectName(QStringLiteral("assetPreviewStatusLabel"));
    previewStatusLabel->setWordWrap(true);
    previewLayout->addWidget(previewStatusLabel);

    previewStack = new QStackedWidget(previewGroupBox);
    previewStack->setObjectName(QStringLiteral("assetPreviewStack"));

    emptyPreviewLabel = new QLabel(previewStack);
    emptyPreviewLabel->setObjectName(QStringLiteral("assetPreviewEmptyLabel"));
    emptyPreviewLabel->setAlignment(Qt::AlignCenter);
    emptyPreviewLabel->setWordWrap(true);
    previewStack->addWidget(emptyPreviewLabel);

    imageScrollArea = new QScrollArea(previewStack);
    imageScrollArea->setObjectName(QStringLiteral("assetImagePreviewScrollArea"));
    imageScrollArea->setWidgetResizable(true);
    imageScrollArea->setAlignment(Qt::AlignCenter);
    imagePreviewLabel = new QLabel(imageScrollArea);
    imagePreviewLabel->setObjectName(QStringLiteral("assetImagePreviewLabel"));
    imagePreviewLabel->setAlignment(Qt::AlignCenter);
    imagePreviewLabel->setMinimumSize(80, 80);
    imageScrollArea->setWidget(imagePreviewLabel);
    previewStack->addWidget(imageScrollArea);

    textPreviewEdit = new QPlainTextEdit(previewStack);
    textPreviewEdit->setObjectName(QStringLiteral("assetTextPreviewEdit"));
    textPreviewEdit->setReadOnly(true);
    textPreviewEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    previewStack->addWidget(textPreviewEdit);

    auto* audioPage = new QWidget(previewStack);
    audioPage->setObjectName(QStringLiteral("assetAudioPreviewPage"));
    auto* audioLayout = new QVBoxLayout(audioPage);
    audioFileLabel = new QLabel(audioPage);
    audioFileLabel->setObjectName(QStringLiteral("assetAudioFileLabel"));
    audioFileLabel->setWordWrap(true);
    audioLayout->addWidget(audioFileLabel);
    audioTimeLabel = new QLabel(audioPage);
    audioTimeLabel->setObjectName(QStringLiteral("assetAudioTimeLabel"));
    audioLayout->addWidget(audioTimeLabel);
    audioPositionSlider = new QSlider(Qt::Horizontal, audioPage);
    audioPositionSlider->setObjectName(QStringLiteral("assetAudioPositionSlider"));
    audioLayout->addWidget(audioPositionSlider);
    auto* buttonLayout = new QHBoxLayout();
    audioPlayPauseButton = new QPushButton(audioPage);
    audioPlayPauseButton->setObjectName(QStringLiteral("assetAudioPlayPauseButton"));
    buttonLayout->addWidget(audioPlayPauseButton);
    audioStopButton = new QPushButton(audioPage);
    audioStopButton->setObjectName(QStringLiteral("assetAudioStopButton"));
    buttonLayout->addWidget(audioStopButton);
    buttonLayout->addStretch(1);
    audioLayout->addLayout(buttonLayout);
    audioLayout->addStretch(1);
    previewStack->addWidget(audioPage);

    previewLayout->addWidget(previewStack, 1);
    browserSplitter->addWidget(previewGroupBox);
    browserSplitter->setStretchFactor(0, 3);
    browserSplitter->setStretchFactor(1, 2);
    browserSplitter->setSizes({360, 260});
    ui->mainLayout->addWidget(browserSplitter, 1);
    updatePreviewTranslations();
}

void AssetBrowser::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        const int filterIndex = ui->filterCombo->currentIndex();
        ui->retranslateUi(this);
        ui->filterCombo->setCurrentIndex(filterIndex);
        updatePathLabel();
        updatePreviewTranslations();
    }
    QWidget::changeEvent(event);
}

void AssetBrowser::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateImagePreview();
}

void AssetBrowser::updatePreviewTranslations()
{
    previewGroupBox->setTitle(tr("单文件预览"));
    emptyPreviewLabel->setText(tr("请选择图片、音频或文本文件。"));
    audioStopButton->setText(tr("停止"));
    updatePreviewSummary();
    if (lastPreviewFailure.error != AssetPreviewError::None)
        previewStatusLabel->setText(previewFailureText(lastPreviewFailure));
    else if (currentPreview.kind == AssetPreviewKind::None)
        previewStatusLabel->setText(tr("尚未选择可预览文件。"));
    else if (currentPreview.kind == AssetPreviewKind::Audio)
        updateAudioControls();
    else
        previewStatusLabel->setText(tr("预览已就绪。"));
}

void AssetBrowser::updatePathLabel()
{
    ui->pathLabel->setText(assetsBasePath.isEmpty() || !QDir(assetsBasePath).exists()
        ? tr("资源目录: 未设置")
        : tr("资源目录: %1").arg(assetsBasePath));
}

void AssetBrowser::setAssetsBasePath(const QString& path)
{
    clearPreview();
    assetsBasePath = path;
    proxyModel->setAssetsBasePath(path);
    updatePathLabel();

    if (path.isEmpty() || !QDir(path).exists())
    {
        fileSystemModel->setRootPath("");
        ui->treeView->setRootIndex(
            proxyModel->mapFromSource(fileSystemModel->index("")));
        return;
    }

    fileSystemModel->setRootPath(path);
    ui->treeView->setRootIndex(
        proxyModel->mapFromSource(fileSystemModel->index(path)));
    onFilterChanged(ui->filterCombo->currentIndex());
}

QString AssetBrowser::getAssetsBasePath() const
{
    return assetsBasePath;
}

void AssetBrowser::setFilterSuffix(const QString& suffix)
{
    currentSuffixes = suffix.split(',', Qt::SkipEmptyParts);
    applyFilter();
}

void AssetBrowser::setFilterSuffixes(const QStringList& suffixes)
{
    currentSuffixes = suffixes;
    applyFilter();
}

void AssetBrowser::onSearchTextChanged(const QString& text)
{
    proxyModel->setFilterFixedString(text);
}

void AssetBrowser::onFilterChanged(int index)
{
    const QString filterData = ui->filterCombo->itemData(index).toString();
    if (filterData.isEmpty())
    {
        currentSuffixes.clear();
        fileSystemModel->setNameFilters({});
    }
    else
    {
        currentSuffixes = filterData.split(',', Qt::SkipEmptyParts);
        fileSystemModel->setNameFilters(currentSuffixes);
    }
    fileSystemModel->setNameFilterDisables(false);
}

void AssetBrowser::onFileClicked(const QModelIndex& index)
{
    const QModelIndex sourceIndex = proxyModel->mapToSource(index);
    if (fileSystemModel->isDir(sourceIndex))
    {
        clearPreview();
        return;
    }

    const QString relativePath = getRelativePath(
        sourceIndex.data(QFileSystemModel::FilePathRole).toString());
    if (relativePath.isEmpty())
        return;
    previewRelativePath(relativePath);
    emit fileSelected(relativePath);
}

void AssetBrowser::onFileDoubleClicked(const QModelIndex& index)
{
    const QModelIndex sourceIndex = proxyModel->mapToSource(index);
    if (fileSystemModel->isDir(sourceIndex))
        return;
    const QString relativePath = getRelativePath(
        sourceIndex.data(QFileSystemModel::FilePathRole).toString());
    if (!relativePath.isEmpty())
        emit fileDoubleClicked(relativePath);
}

QString AssetBrowser::getRelativePath(const QString& fullPath) const
{
    if (assetsBasePath.isEmpty())
        return fullPath;
    QString relative;
    if (!EditorAssetPath::makeLogicalResourceRelativePath(
            assetsBasePath, fullPath, relative))
    {
        return {};
    }
    relative.replace('/', '\\');
    return relative;
}

void AssetBrowser::applyFilter()
{
    fileSystemModel->setNameFilters(currentSuffixes);
    fileSystemModel->setNameFilterDisables(false);
}

bool AssetBrowser::previewRelativePath(const QString& relativePath)
{
    stopAndClearAudio();
    currentPreview = {};
    lastPreviewFailure = {};
    previewStack->setCurrentIndex(0);
    previewSummaryLabel->clear();
    imagePreviewLabel->clear();
    textPreviewEdit->clear();

    AssetPreviewData preview;
    AssetPreviewFailure failure;
    if (!AssetPreviewLoader::load(
            assetsBasePath, relativePath, &preview, &failure))
    {
        lastPreviewFailure = failure;
        previewStatusLabel->setText(previewFailureText(failure));
        return false;
    }

    displayPreview(std::move(preview));
    return true;
}

void AssetBrowser::clearPreview()
{
    stopAndClearAudio();
    currentPreview = {};
    lastPreviewFailure = {};
    previewSummaryLabel->clear();
    previewStatusLabel->setText(tr("尚未选择可预览文件。"));
    previewStack->setCurrentIndex(0);
    imagePreviewLabel->clear();
    textPreviewEdit->clear();
    audioFileLabel->clear();
}

void AssetBrowser::displayPreview(AssetPreviewData preview)
{
    currentPreview = std::move(preview);
    lastPreviewFailure = {};
    updatePreviewSummary();
    switch (currentPreview.kind)
    {
    case AssetPreviewKind::Image:
        previewStack->setCurrentWidget(imageScrollArea);
        previewStatusLabel->setText(tr("预览已就绪。"));
        updateImagePreview();
        break;
    case AssetPreviewKind::Text:
        previewStack->setCurrentWidget(textPreviewEdit);
        textPreviewEdit->setPlainText(currentPreview.text);
        textPreviewEdit->moveCursor(QTextCursor::Start);
        previewStatusLabel->setText(tr("预览已就绪。"));
        break;
    case AssetPreviewKind::Audio:
        previewStack->setCurrentIndex(3);
        audioFileLabel->setText(currentPreview.relativePath);
        previewStatusLabel->setText(tr("正在加载音频…"));
        mediaPlayer->setSource(QUrl::fromLocalFile(currentPreview.absolutePath));
        updateAudioControls();
        break;
    case AssetPreviewKind::None:
        previewStack->setCurrentIndex(0);
        break;
    }
}

QString AssetBrowser::previewFailureText(
    const AssetPreviewFailure& failure) const
{
    switch (failure.error)
    {
    case AssetPreviewError::InvalidRoot:
        return tr("无法预览：资源目录无效。");
    case AssetPreviewError::InvalidPath:
        return tr("无法预览：文件路径不在当前资源目录内。");
    case AssetPreviewError::MissingFile:
        return tr("无法预览：文件不存在。");
    case AssetPreviewError::NotRegularFile:
        return tr("无法预览：所选对象不是普通文件。");
    case AssetPreviewError::FileNotReadable:
        return tr("无法预览：文件不可读。");
    case AssetPreviewError::UnsupportedType:
        return tr("无法预览：暂不支持该文件类型。");
    case AssetPreviewError::ImageDecodeFailed:
        return tr("无法预览：图片解码失败。");
    case AssetPreviewError::TextReadFailed:
        return tr("无法预览：文本读取失败。");
    case AssetPreviewError::BinaryText:
        return tr("无法预览：文件包含二进制数据。");
    case AssetPreviewError::TextEncodingFailed:
        return tr("无法预览：文本不是有效的 UTF-8 或 GBK。");
    case AssetPreviewError::None:
        break;
    }
    return tr("无法预览文件。");
}

void AssetBrowser::updatePreviewSummary()
{
    if (currentPreview.kind == AssetPreviewKind::None)
    {
        previewSummaryLabel->clear();
        return;
    }

    QString typeText;
    QString detailText;
    switch (currentPreview.kind)
    {
    case AssetPreviewKind::Image:
        typeText = tr("图片");
        detailText = tr("尺寸 %1×%2；帧 %3；方向 %4；间隔 %5 ms；偏移 (%6, %7)")
            .arg(currentPreview.imageWidth)
            .arg(currentPreview.imageHeight)
            .arg(currentPreview.frameCount)
            .arg(currentPreview.directions)
            .arg(currentPreview.intervalMilliseconds)
            .arg(currentPreview.xOffset)
            .arg(currentPreview.yOffset);
        break;
    case AssetPreviewKind::Audio:
        typeText = tr("音频");
        detailText = mediaPlayer && mediaPlayer->duration() > 0
            ? tr("时长 %1").arg(timeText(mediaPlayer->duration()))
            : tr("等待媒体元数据");
        break;
    case AssetPreviewKind::Text:
        typeText = tr("文本");
        detailText = currentPreview.textTruncated
            ? tr("编码 %1；只显示前 256 KiB").arg(currentPreview.textEncoding)
            : tr("编码 %1；完整显示").arg(currentPreview.textEncoding);
        break;
    case AssetPreviewKind::None:
        break;
    }

    previewSummaryLabel->setText(
        tr("路径：%1\n类型：%2 / %3\n大小：%4（%5 字节）\n详情：%6")
            .arg(currentPreview.relativePath)
            .arg(typeText)
            .arg(currentPreview.formatName)
            .arg(byteSizeText(currentPreview.fileSize))
            .arg(currentPreview.fileSize)
            .arg(detailText));
}

void AssetBrowser::updateImagePreview()
{
    if (!imageScrollArea ||
        currentPreview.kind != AssetPreviewKind::Image ||
        currentPreview.image.isNull())
    {
        return;
    }
    const QSize available = imageScrollArea->viewport()->size() - QSize(12, 12);
    if (available.width() <= 0 || available.height() <= 0)
        return;
    imagePreviewLabel->setPixmap(QPixmap::fromImage(
        currentPreview.image.scaled(
            available, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void AssetBrowser::stopAndClearAudio()
{
    if (!mediaPlayer)
        return;
    mediaPlayer->stop();
    mediaPlayer->setSource(QUrl());
    if (audioPositionSlider)
    {
        audioPositionSlider->setRange(0, 0);
        audioPositionSlider->setValue(0);
        audioPositionSlider->setEnabled(false);
    }
    if (audioPlayPauseButton)
        audioPlayPauseButton->setEnabled(false);
    if (audioStopButton)
        audioStopButton->setEnabled(false);
    if (audioTimeLabel)
        audioTimeLabel->setText(QStringLiteral("0:00 / 0:00"));
}

bool AssetBrowser::isCurrentAudioSource() const
{
    return mediaPlayer &&
        currentPreview.kind == AssetPreviewKind::Audio &&
        mediaPlayer->source() == QUrl::fromLocalFile(currentPreview.absolutePath);
}

void AssetBrowser::updateAudioControls()
{
    if (!isCurrentAudioSource())
        return;

    const QMediaPlayer::MediaStatus status = mediaPlayer->mediaStatus();
    const bool ready = status == QMediaPlayer::LoadedMedia ||
        status == QMediaPlayer::BufferedMedia ||
        status == QMediaPlayer::BufferingMedia ||
        status == QMediaPlayer::EndOfMedia;
    audioPlayPauseButton->setEnabled(ready);
    audioStopButton->setEnabled(ready);
    audioPositionSlider->setEnabled(ready && mediaPlayer->duration() > 0);
    audioPlayPauseButton->setText(
        mediaPlayer->playbackState() == QMediaPlayer::PlayingState
            ? tr("暂停") : tr("播放"));

    if (mediaPlayer->error() != QMediaPlayer::NoError)
    {
        previewStatusLabel->setText(
            tr("音频加载失败：%1").arg(mediaPlayer->errorString()));
    }
    else if (mediaPlayer->playbackState() == QMediaPlayer::PlayingState)
    {
        previewStatusLabel->setText(tr("正在播放音频。"));
    }
    else if (mediaPlayer->playbackState() == QMediaPlayer::PausedState)
    {
        previewStatusLabel->setText(tr("音频已暂停。"));
    }
    else if (ready)
    {
        previewStatusLabel->setText(tr("音频已就绪。"));
    }
    else
    {
        previewStatusLabel->setText(tr("正在加载音频…"));
    }
    updateAudioTimeLabel();
}

void AssetBrowser::updateAudioTimeLabel()
{
    if (!audioTimeLabel || !mediaPlayer)
        return;
    audioTimeLabel->setText(QStringLiteral("%1 / %2")
        .arg(timeText(mediaPlayer->position()))
        .arg(timeText(mediaPlayer->duration())));
}

void AssetBrowser::onAudioPlayPause()
{
    if (!isCurrentAudioSource())
        return;
    if (mediaPlayer->playbackState() == QMediaPlayer::PlayingState)
        mediaPlayer->pause();
    else
    {
        if (mediaPlayer->mediaStatus() == QMediaPlayer::EndOfMedia)
            mediaPlayer->setPosition(0);
        mediaPlayer->play();
    }
}

void AssetBrowser::onAudioStop()
{
    if (!isCurrentAudioSource())
        return;
    mediaPlayer->stop();
    mediaPlayer->setPosition(0);
    updateAudioControls();
}
