#pragma once

#include "../core/AssetReferenceScanner.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;

class AssetReferenceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AssetReferenceDialog(const AssetReferenceScanReport& report,
                                  QWidget* parent = nullptr);

    QString selectedSourceFile() const;
    int selectedLineNumber() const;

protected:
    void changeEvent(QEvent* event) override;

private:
    void populate(const AssetReferenceScanReport& report);
    void applyFilter();
    void retranslateUi();
    QString statusText(AssetReferenceStatus status) const;

    QLabel* m_summaryLabel = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QCheckBox* m_missingOnlyCheckBox = nullptr;
    QTableWidget* m_table = nullptr;
    QString m_selectedSourceFile;
    int m_selectedLineNumber = 0;
    AssetReferenceScanReport m_report;
};
