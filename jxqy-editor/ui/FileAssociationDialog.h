#pragma once

#include <QDialog>

#include <memory>

class DesktopFileAssociationManager;
class QLabel;
class QListWidget;
class QPushButton;

class FileAssociationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FileAssociationDialog(
        QWidget* parent = nullptr,
        DesktopFileAssociationManager* manager = nullptr,
        const QString& executablePath = QString());
    ~FileAssociationDialog() override;

private:
    void refresh();
    void resetRestoreConfirmation();
    void applySelection();
    void restoreAll();
    void openWindowsDefaultApps();
    QString associationLabel(const QString& extension) const;
    void showResult(bool success, bool changed, const QString& error);

    std::unique_ptr<DesktopFileAssociationManager> ownedManager;
    DesktopFileAssociationManager* manager = nullptr;
    QString executablePath;
    QListWidget* associationList = nullptr;
    QLabel* statusLabel = nullptr;
    QLabel* userChoiceLabel = nullptr;
    QPushButton* applyButton = nullptr;
    QPushButton* restoreButton = nullptr;
    QPushButton* defaultAppsButton = nullptr;
    bool restoreConfirmationPending = false;
};
