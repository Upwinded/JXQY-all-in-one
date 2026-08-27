#pragma once

#include "../core/ScriptProjectSearch.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

class ScriptProjectSearchDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ScriptProjectSearchDialog(
        const QString& activeContentRoot,
        const QSet<QString>& blockedFilePaths,
        QWidget* parent = nullptr);

    ScriptProjectReplaceResult publishResult() const;

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void runSearch();
    void updatePreview();
    void updatePublishButton();
    void publishSelectedFiles();
    void resetPublishConfirmation();

private:
    void clearResults();
    void populateResults();
    void retranslateUi();
    void updateConfirmationText();
    QStringList selectedFilePaths() const;
    bool isBlocked(const QString& filePath) const;

    QString m_activeContentRoot;
    QSet<QString> m_blockedFilePaths;
    QSet<QString> m_blockedKeys;
    ScriptProjectSearchReport m_report;
    ScriptProjectReplaceResult m_publishResult;
    QStringList m_confirmedFilePaths;
    int m_confirmedReplacementCount = 0;

    QLabel* m_rootLabel = nullptr;
    QLabel* m_queryLabel = nullptr;
    QLabel* m_replacementLabel = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_issueLabel = nullptr;
    QLabel* m_confirmationLabel = nullptr;
    QLabel* m_beforeLabel = nullptr;
    QLabel* m_afterLabel = nullptr;
    QLineEdit* m_queryEdit = nullptr;
    QLineEdit* m_replacementEdit = nullptr;
    QCheckBox* m_caseSensitiveCheckBox = nullptr;
    QCheckBox* m_wholeWordsCheckBox = nullptr;
    QCheckBox* m_regularExpressionCheckBox = nullptr;
    QPushButton* m_searchButton = nullptr;
    QPushButton* m_publishButton = nullptr;
    QPushButton* m_cancelConfirmationButton = nullptr;
    QTableWidget* m_resultTable = nullptr;
    QPlainTextEdit* m_beforePreview = nullptr;
    QPlainTextEdit* m_afterPreview = nullptr;
};
