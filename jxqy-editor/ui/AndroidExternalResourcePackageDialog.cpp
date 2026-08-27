#include "AndroidExternalResourcePackageDialog.h"

#include "../core/EditorProcessLifecycle.h"
#include "../core/EditorSettings.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTextEdit>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
constexpr auto LastOutputParentKey =
    "androidExternalResourceExport/lastOutputParent";
}

AndroidExternalResourcePackageDialog::
AndroidExternalResourcePackageDialog(
    const QString& collectionRootValue,
    const ResourcePackInfo& activePackValue,
    QWidget* parent)
    : QDialog(parent)
    , collectionRoot(collectionRootValue)
    , activePack(activePackValue)
{
    setModal(true);
    resize(760, 560);

    auto* rootLayout = new QVBoxLayout(this);
    titleLabel = new QLabel(this);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleLabel->setFont(titleFont);
    rootLayout->addWidget(titleLabel);

    auto* formLayout = new QFormLayout();
    packLabel = new QLabel(this);
    packLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    sourceLabel = new QLabel(this);
    sourceLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    sourceLabel->setWordWrap(true);
    devicePathCaptionLabel = new QLabel(this);
    devicePathLabel = new QLabel(this);
    devicePathLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    devicePathLabel->setWordWrap(true);
    outputLabel = new QLabel(this);

    packCaptionLabel = new QLabel(this);
    sourceCaptionLabel = new QLabel(this);
    formLayout->addRow(packCaptionLabel, packLabel);
    formLayout->addRow(sourceCaptionLabel, sourceLabel);
    formLayout->addRow(devicePathCaptionLabel, devicePathLabel);

    auto* outputRow = new QHBoxLayout();
    outputPathEdit = new QLineEdit(this);
    browseButton = new QPushButton(this);
    outputRow->addWidget(outputPathEdit, 1);
    outputRow->addWidget(browseButton);
    formLayout->addRow(outputLabel, outputRow);
    rootLayout->addLayout(formLayout);

    QSettings settings = EditorSettings::create();

    noteLabel = new QLabel(this);
    noteLabel->setWordWrap(true);
    rootLayout->addWidget(noteLabel);

    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 1);
    progressBar->setValue(0);
    rootLayout->addWidget(progressBar);

    logEdit = new QTextEdit(this);
    logEdit->setReadOnly(true);
    rootLayout->addWidget(logEdit, 1);

    auto* buttonLayout = new QHBoxLayout();
    openBundleButton = new QPushButton(this);
    openBundleButton->setEnabled(false);
    buttonLayout->addWidget(openBundleButton);
    buttonLayout->addStretch(1);
    exportButton = new QPushButton(this);
    exportButton->setDefault(true);
    cancelButton = new QPushButton(this);
    buttonLayout->addWidget(exportButton);
    buttonLayout->addWidget(cancelButton);
    rootLayout->addLayout(buttonLayout);

    connect(
        browseButton,
        &QPushButton::clicked,
        this,
        &AndroidExternalResourcePackageDialog::
            chooseOutputParent);
    connect(
        exportButton,
        &QPushButton::clicked,
        this,
        &AndroidExternalResourcePackageDialog::startExport);
    connect(
        cancelButton,
        &QPushButton::clicked,
        this,
        &AndroidExternalResourcePackageDialog::cancelOrClose);
    connect(
        openBundleButton,
        &QPushButton::clicked,
        this,
        &AndroidExternalResourcePackageDialog::
            openPublishedBundle);

    QString outputParent = settings.value(
        QString::fromLatin1(LastOutputParentKey)).toString();
    if (!QFileInfo(outputParent).isDir())
    {
        outputParent = QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation);
    }
    if (!QFileInfo(outputParent).isDir())
        outputParent = QDir::homePath();
    outputPathEdit->setText(
        defaultBundleDirectory(outputParent));
    retranslateUi();
}

AndroidExternalResourcePackageDialog::
~AndroidExternalResourcePackageDialog()
{
    if (activeWorkerThread)
    {
        requestCancellation();
        activeWorkerThread->wait();
    }
}

void AndroidExternalResourcePackageDialog::changeEvent(
    QEvent* event)
{
    QDialog::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange)
        retranslateUi();
}

void AndroidExternalResourcePackageDialog::closeEvent(
    QCloseEvent* event)
{
    if (exportInProgress)
    {
        requestCancellation();
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void AndroidExternalResourcePackageDialog::reject()
{
    if (exportInProgress)
    {
        requestCancellation();
        return;
    }
    QDialog::reject();
}

QString AndroidExternalResourcePackageDialog::
defaultBundleDirectory(const QString& parentPath) const
{
    QString directoryName =
        QFileInfo(activePack.rootPath).fileName();
    if (directoryName.isEmpty())
        directoryName = activePack.profile.id.trimmed();
    if (directoryName.isEmpty())
        directoryName = QStringLiteral("resource-pack");
    return QDir(parentPath).filePath(
        directoryName + QStringLiteral("-android-external"));
}

void AndroidExternalResourcePackageDialog::retranslateUi()
{
    setWindowTitle(tr("导出 Android 外部资源"));
    titleLabel->setText(tr("导出当前活动资源包"));

    packCaptionLabel->setText(tr("活动资源包："));
    sourceCaptionLabel->setText(tr("源目录："));
    packLabel->setText(tr("%1（%2）")
        .arg(activePack.profile.name.trimmed().isEmpty()
                 ? activePack.profile.id
                 : activePack.profile.name,
             activePack.profile.id));
    sourceLabel->setText(activePack.rootPath);
    devicePathCaptionLabel->setText(tr("设备目标："));
    const QString deviceRoot = QStringLiteral(
        "/storage/emulated/0/Download/jxqy/assets/");
    const QString devicePackDirectory = deviceRoot +
        QFileInfo(activePack.rootPath).fileName() + '/';
    devicePathLabel->setText(
        tr("外部资源根：%1\n当前包目录：%2\n清单文件：%3")
            .arg(deviceRoot,
                 devicePackDirectory,
                 devicePackDirectory +
                     QStringLiteral("game_profile.ini")));
    outputLabel->setText(tr("导出包目录："));
    browseButton->setText(tr("选择父目录…"));
    noteLabel->setText(tr(
        "只导出磁盘上已保存的文件。不会校验 Lua、地图、NPC、OBJ、封面、说明或 UI 资源；缺失资源不会阻止导出。资源集合的 common 目录会在可用时自动复制。"));
    openBundleButton->setText(tr("打开导出目录"));
    exportButton->setText(tr("开始导出"));
    cancelButton->setText(exportInProgress
        ? tr("取消")
        : tr("关闭"));
}

void AndroidExternalResourcePackageDialog::chooseOutputParent()
{
    const QString currentPath = outputPathEdit->text().trimmed();
    QString initialParent = QFileInfo(currentPath).dir().absolutePath();
    if (!QFileInfo(initialParent).isDir())
        initialParent = QDir::homePath();
    const QString parentPath = QFileDialog::getExistingDirectory(
        this,
        tr("选择 Android 外部资源导出包的父目录"),
        initialParent);
    if (!parentPath.isEmpty())
        outputPathEdit->setText(defaultBundleDirectory(parentPath));
}

void AndroidExternalResourcePackageDialog::startExport()
{
    if (exportInProgress)
        return;

    const QString bundleDirectory =
        QFileInfo(outputPathEdit->text().trimmed())
            .absoluteFilePath();
    if (bundleDirectory.isEmpty() ||
        bundleDirectory == QStringLiteral("."))
    {
        QMessageBox::warning(
            this,
            tr("无法导出"),
            tr("请选择有效的导出包目录。"));
        return;
    }
    if (!QFileInfo(QFileInfo(bundleDirectory).dir().absolutePath()).isDir())
    {
        QMessageBox::warning(
            this,
            tr("无法导出"),
            tr("导出包的父目录不存在。"));
        return;
    }
    if (QFileInfo::exists(bundleDirectory))
    {
        const QMessageBox::StandardButton answer =
            QMessageBox::question(
                this,
                tr("替换已有导出包"),
                tr("导出包目录已经存在。完整导出成功后将替换旧目录；失败或取消时保留旧目录。是否继续？"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    QSettings settings = EditorSettings::create();
    settings.setValue(
        QString::fromLatin1(LastOutputParentKey),
        QFileInfo(bundleDirectory).dir().absolutePath());
    settings.sync();

    logEdit->clear();
    logEdit->append(tr("开始导出：%1").arg(bundleDirectory));
    progressBar->setRange(0, 0);
    publishedBundleDirectory.clear();
    openBundleButton->setEnabled(false);
    cancellationRequested =
        std::make_shared<std::atomic_bool>(false);

    AndroidExternalResourceExportOptions options;
    options.collectionRoot = collectionRoot;
    options.activePack = activePack;
    options.bundleDirectory = bundleDirectory;
    options.cancellationRequested = cancellationRequested;
    options.progressCallback =
        [this](int current, int total, const QString& sourcePath)
        {
            QMetaObject::invokeMethod(
                this,
                [this, current, total, sourcePath]()
                {
                    progressBar->setRange(0, std::max(1, total));
                    progressBar->setValue(current);
                    if (current == total || current % 100 == 0)
                    {
                        logEdit->append(
                            tr("%1/%2  %3")
                                .arg(current)
                                .arg(total)
                                .arg(sourcePath));
                    }
                },
                Qt::QueuedConnection);
        };

    auto result =
        std::make_shared<AndroidExternalResourceExportResult>();
    QThread* workerThread = QThread::create(
        [options, result]()
        {
            *result = AndroidExternalResourcePackager::
                exportBundle(options);
        });
    registerEditorBackgroundWorker(workerThread);
    registerEditorExitProtectedWriteWorker(
        workerThread,
        [cancellation = cancellationRequested]()
        {
            cancellation->store(
                true, std::memory_order_release);
        });
    activeWorkerThread = workerThread;
    setExportInProgress(true);

    connect(
        workerThread,
        &QThread::finished,
        this,
        [this, workerThread, result]()
        {
            if (activeWorkerThread != workerThread)
                return;
            activeWorkerThread = nullptr;
            cancellationRequested.reset();
            setExportInProgress(false);
            finishExport(*result);
        });
    connect(
        workerThread,
        &QThread::finished,
        workerThread,
        &QObject::deleteLater);
    workerThread->start();
}

void AndroidExternalResourcePackageDialog::setExportInProgress(
    bool inProgress)
{
    exportInProgress = inProgress;
    outputPathEdit->setEnabled(!inProgress);
    browseButton->setEnabled(!inProgress);
    exportButton->setEnabled(!inProgress);
    cancelButton->setEnabled(true);
    cancelButton->setText(inProgress ? tr("取消") : tr("关闭"));
}

void AndroidExternalResourcePackageDialog::requestCancellation()
{
    if (!exportInProgress || !cancellationRequested)
        return;
    cancellationRequested->store(
        true, std::memory_order_release);
    cancelButton->setEnabled(false);
    logEdit->append(tr("正在取消；旧导出包将保持不变…"));
}

void AndroidExternalResourcePackageDialog::cancelOrClose()
{
    if (exportInProgress)
    {
        requestCancellation();
        return;
    }
    accept();
}

void AndroidExternalResourcePackageDialog::finishExport(
    const AndroidExternalResourceExportResult& result)
{
    if (result.status ==
        AndroidExternalResourceExportStatus::Success)
    {
        progressBar->setRange(0, 1);
        progressBar->setValue(1);
        publishedBundleDirectory = result.report.bundleDirectory;
        openBundleButton->setEnabled(true);
        logEdit->append(tr(
            "导出完成：复制 %1 项，跳过 %2 项，警告 %3 项。")
            .arg(result.report.copiedCount())
            .arg(result.report.skippedCount())
            .arg(result.report.warnings.size()));
        for (const QString& warning : result.report.warnings)
            logEdit->append(tr("警告：%1").arg(warning));
        QMessageBox::information(
            this,
            tr("导出完成"),
            tr("Android 外部资源目录已完整发布。请把导出包中的 Download 目录复制到 Android 共享存储根目录。"));
        return;
    }

    progressBar->setRange(0, 1);
    progressBar->setValue(0);
    if (result.status ==
        AndroidExternalResourceExportStatus::Cancelled)
    {
        logEdit->append(tr("导出已取消；旧导出包未被替换。"));
        QMessageBox::information(
            this,
            tr("导出已取消"),
            tr("导出已取消，已有导出包保持不变。"));
        return;
    }

    logEdit->append(tr("导出失败：%1")
        .arg(result.errorMessage));
    QMessageBox::critical(
        this,
        tr("导出失败"),
        tr("Android 外部资源未发布，已有导出包保持不变。\n\n%1")
            .arg(result.errorMessage));
}

void AndroidExternalResourcePackageDialog::openPublishedBundle()
{
    if (!publishedBundleDirectory.isEmpty())
    {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(publishedBundleDirectory));
    }
}
