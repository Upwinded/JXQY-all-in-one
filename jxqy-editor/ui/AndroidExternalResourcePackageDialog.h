#pragma once

#include "../core/AndroidExternalResourcePackager.h"

#include <QDialog>

#include <atomic>
#include <memory>

class QCheckBox;
class QCloseEvent;
class QEvent;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTextEdit;
class QThread;

class AndroidExternalResourcePackageDialog : public QDialog
{
    Q_OBJECT

public:
    AndroidExternalResourcePackageDialog(
        const QString& collectionRoot,
        const ResourcePackInfo& activePack,
        QWidget* parent = nullptr);
    ~AndroidExternalResourcePackageDialog() override;

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

private slots:
    void chooseOutputParent();
    void startExport();
    void cancelOrClose();
    void openPublishedBundle();

private:
    void retranslateUi();
    void setExportInProgress(bool inProgress);
    void requestCancellation();
    QString defaultBundleDirectory(const QString& parentPath) const;
    void finishExport(
        const AndroidExternalResourceExportResult& result);

    QString collectionRoot;
    ResourcePackInfo activePack;
    QString publishedBundleDirectory;

    QLabel* titleLabel = nullptr;
    QLabel* packCaptionLabel = nullptr;
    QLabel* packLabel = nullptr;
    QLabel* sourceCaptionLabel = nullptr;
    QLabel* sourceLabel = nullptr;
    QLabel* devicePathCaptionLabel = nullptr;
    QLabel* devicePathLabel = nullptr;
    QLabel* outputLabel = nullptr;
    QLabel* noteLabel = nullptr;
    QLineEdit* outputPathEdit = nullptr;
    QPushButton* browseButton = nullptr;
    QProgressBar* progressBar = nullptr;
    QTextEdit* logEdit = nullptr;
    QPushButton* openBundleButton = nullptr;
    QPushButton* exportButton = nullptr;
    QPushButton* cancelButton = nullptr;

    QThread* activeWorkerThread = nullptr;
    std::shared_ptr<std::atomic_bool> cancellationRequested;
    bool exportInProgress = false;
};
