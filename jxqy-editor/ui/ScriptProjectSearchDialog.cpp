#include "ScriptProjectSearchDialog.h"

#include "../core/EditorAssetPath.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace
{
constexpr int FilePathRole = Qt::UserRole;
constexpr int ResultIndexRole = Qt::UserRole + 1;
}

ScriptProjectSearchDialog::ScriptProjectSearchDialog(
    const QString& activeContentRoot,
    const QSet<QString>& blockedFilePaths,
    QWidget* parent)
    : QDialog(parent)
    , m_activeContentRoot(
          EditorAssetPath::normalizedAbsolutePath(activeContentRoot))
    , m_blockedFilePaths(blockedFilePaths)
{
    for (const QString& blockedPath : blockedFilePaths)
        m_blockedKeys.insert(EditorAssetPath::comparisonKey(blockedPath));

    setObjectName(QStringLiteral("scriptProjectSearchDialog"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    m_rootLabel = new QLabel(this);
    m_rootLabel->setObjectName(QStringLiteral("scriptProjectSearchRootLabel"));
    m_rootLabel->setWordWrap(true);
    layout->addWidget(m_rootLabel);

    auto* form = new QFormLayout();
    m_queryLabel = new QLabel(this);
    m_queryEdit = new QLineEdit(this);
    m_queryEdit->setObjectName(QStringLiteral("scriptProjectSearchQueryEdit"));
    m_queryEdit->setClearButtonEnabled(true);
    form->addRow(m_queryLabel, m_queryEdit);
    m_replacementLabel = new QLabel(this);
    m_replacementEdit = new QLineEdit(this);
    m_replacementEdit->setObjectName(
        QStringLiteral("scriptProjectSearchReplacementEdit"));
    m_replacementEdit->setClearButtonEnabled(true);
    form->addRow(m_replacementLabel, m_replacementEdit);
    layout->addLayout(form);

    auto* optionsLayout = new QGridLayout();
    m_caseSensitiveCheckBox = new QCheckBox(this);
    m_caseSensitiveCheckBox->setObjectName(
        QStringLiteral("scriptProjectSearchCaseSensitiveCheckBox"));
    m_wholeWordsCheckBox = new QCheckBox(this);
    m_wholeWordsCheckBox->setObjectName(
        QStringLiteral("scriptProjectSearchWholeWordsCheckBox"));
    m_regularExpressionCheckBox = new QCheckBox(this);
    m_regularExpressionCheckBox->setObjectName(
        QStringLiteral("scriptProjectSearchRegularExpressionCheckBox"));
    m_searchButton = new QPushButton(this);
    m_searchButton->setObjectName(QStringLiteral("scriptProjectSearchButton"));
    optionsLayout->addWidget(m_caseSensitiveCheckBox, 0, 0);
    optionsLayout->addWidget(m_wholeWordsCheckBox, 0, 1);
    optionsLayout->addWidget(m_regularExpressionCheckBox, 0, 2);
    optionsLayout->setColumnStretch(3, 1);
    optionsLayout->addWidget(m_searchButton, 0, 4);
    layout->addLayout(optionsLayout);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName(
        QStringLiteral("scriptProjectSearchSummaryLabel"));
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);
    m_issueLabel = new QLabel(this);
    m_issueLabel->setObjectName(
        QStringLiteral("scriptProjectSearchIssueLabel"));
    m_issueLabel->setWordWrap(true);
    layout->addWidget(m_issueLabel);

    m_resultTable = new QTableWidget(this);
    m_resultTable->setObjectName(
        QStringLiteral("scriptProjectSearchResultTable"));
    m_resultTable->setColumnCount(4);
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultTable->setAlternatingRowColors(true);
    m_resultTable->verticalHeader()->setVisible(false);
    m_resultTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    m_resultTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    m_resultTable->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    m_resultTable->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::ResizeToContents);
    layout->addWidget(m_resultTable, 1);

    auto* previewSplitter = new QSplitter(Qt::Horizontal, this);
    previewSplitter->setObjectName(
        QStringLiteral("scriptProjectSearchPreviewSplitter"));
    auto* beforeWidget = new QWidget(previewSplitter);
    auto* beforeLayout = new QVBoxLayout(beforeWidget);
    beforeLayout->setContentsMargins(0, 0, 0, 0);
    m_beforeLabel = new QLabel(beforeWidget);
    beforeLayout->addWidget(m_beforeLabel);
    m_beforePreview = new QPlainTextEdit(beforeWidget);
    m_beforePreview->setObjectName(
        QStringLiteral("scriptProjectSearchBeforePreview"));
    m_beforePreview->setReadOnly(true);
    m_beforePreview->setLineWrapMode(QPlainTextEdit::NoWrap);
    beforeLayout->addWidget(m_beforePreview);
    auto* afterWidget = new QWidget(previewSplitter);
    auto* afterLayout = new QVBoxLayout(afterWidget);
    afterLayout->setContentsMargins(0, 0, 0, 0);
    m_afterLabel = new QLabel(afterWidget);
    afterLayout->addWidget(m_afterLabel);
    m_afterPreview = new QPlainTextEdit(afterWidget);
    m_afterPreview->setObjectName(
        QStringLiteral("scriptProjectSearchAfterPreview"));
    m_afterPreview->setReadOnly(true);
    m_afterPreview->setLineWrapMode(QPlainTextEdit::NoWrap);
    afterLayout->addWidget(m_afterPreview);
    previewSplitter->addWidget(beforeWidget);
    previewSplitter->addWidget(afterWidget);
    previewSplitter->setSizes({500, 500});
    layout->addWidget(previewSplitter, 1);

    m_confirmationLabel = new QLabel(this);
    m_confirmationLabel->setObjectName(
        QStringLiteral("scriptProjectSearchConfirmationLabel"));
    m_confirmationLabel->setWordWrap(true);
    m_confirmationLabel->setVisible(false);
    layout->addWidget(m_confirmationLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName(
        QStringLiteral("scriptProjectSearchDialogButtons"));
    m_publishButton = buttons->addButton(
        QString(), QDialogButtonBox::ActionRole);
    m_publishButton->setObjectName(
        QStringLiteral("scriptProjectSearchPublishButton"));
    m_publishButton->setEnabled(false);
    m_cancelConfirmationButton = buttons->addButton(
        QString(), QDialogButtonBox::ActionRole);
    m_cancelConfirmationButton->setObjectName(
        QStringLiteral("scriptProjectSearchCancelConfirmationButton"));
    m_cancelConfirmationButton->setVisible(false);
    connect(buttons, &QDialogButtonBox::rejected,
        this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_searchButton, &QPushButton::clicked,
        this, &ScriptProjectSearchDialog::runSearch);
    connect(m_queryEdit, &QLineEdit::returnPressed,
        this, &ScriptProjectSearchDialog::runSearch);
    connect(m_queryEdit, &QLineEdit::textChanged, this,
        [this](const QString& text)
        {
            resetPublishConfirmation();
            m_searchButton->setEnabled(!text.isEmpty());
        });
    connect(m_replacementEdit, &QLineEdit::textChanged,
        this, &ScriptProjectSearchDialog::resetPublishConfirmation);
    connect(m_caseSensitiveCheckBox, &QCheckBox::toggled,
        this, &ScriptProjectSearchDialog::resetPublishConfirmation);
    connect(m_wholeWordsCheckBox, &QCheckBox::toggled,
        this, &ScriptProjectSearchDialog::resetPublishConfirmation);
    connect(m_regularExpressionCheckBox, &QCheckBox::toggled,
        this, &ScriptProjectSearchDialog::resetPublishConfirmation);
    connect(m_resultTable, &QTableWidget::itemSelectionChanged,
        this, &ScriptProjectSearchDialog::updatePreview);
    connect(m_resultTable, &QTableWidget::itemChanged,
        this, &ScriptProjectSearchDialog::updatePublishButton);
    connect(m_publishButton, &QPushButton::clicked,
        this, &ScriptProjectSearchDialog::publishSelectedFiles);
    connect(m_cancelConfirmationButton, &QPushButton::clicked,
        this, &ScriptProjectSearchDialog::resetPublishConfirmation);

    m_searchButton->setEnabled(false);
    retranslateUi();
    clearResults();
    resize(1200, 820);
}

ScriptProjectReplaceResult ScriptProjectSearchDialog::publishResult() const
{
    return m_publishResult;
}

void ScriptProjectSearchDialog::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void ScriptProjectSearchDialog::runSearch()
{
    clearResults();
    ScriptProjectSearchOptions options;
    options.query = m_queryEdit->text();
    options.replacement = m_replacementEdit->text();
    options.caseSensitive = m_caseSensitiveCheckBox->isChecked();
    options.wholeWords = m_wholeWordsCheckBox->isChecked();
    options.regularExpression = m_regularExpressionCheckBox->isChecked();

    QProgressDialog progress(
        tr("正在扫描项目脚本..."), tr("取消"), 0, 0, this);
    progress.setObjectName(
        QStringLiteral("scriptProjectSearchProgressDialog"));
    progress.setWindowTitle(tr("项目脚本搜索与替换"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();
    QApplication::processEvents();

    QElapsedTimer eventPumpTimer;
    eventPumpTimer.start();
    m_report = ScriptProjectSearch::scan(
        m_activeContentRoot, options,
        [&progress](int current, int total, const QString& currentFile)
        {
            progress.setRange(0, total);
            progress.setValue(current);
            progress.setLabelText(tr("正在扫描 %1\n%2 / %3")
                .arg(currentFile).arg(current).arg(total));
            QApplication::processEvents();
        },
        [&progress, &eventPumpTimer]()
        {
            if (eventPumpTimer.elapsed() >= 16)
            {
                QApplication::processEvents();
                eventPumpTimer.restart();
            }
            return progress.wasCanceled();
        });
    progress.close();

    if (m_report.cancelled)
    {
        clearResults();
        m_summaryLabel->setText(
            tr("搜索已取消；项目、文档和文件均未修改。"));
        return;
    }
    if (!m_report.validationError.isEmpty())
    {
        m_summaryLabel->setText(m_report.validationError);
        QMessageBox::warning(this, tr("项目脚本搜索与替换"),
            m_report.validationError);
        return;
    }

    populateResults();
}

void ScriptProjectSearchDialog::clearResults()
{
    resetPublishConfirmation();
    m_resultTable->blockSignals(true);
    m_resultTable->setRowCount(0);
    m_resultTable->blockSignals(false);
    m_beforePreview->clear();
    m_afterPreview->clear();
    m_summaryLabel->clear();
    m_issueLabel->clear();
    m_issueLabel->setVisible(false);
    m_publishButton->setEnabled(false);
    m_publishResult = ScriptProjectReplaceResult();
}

void ScriptProjectSearchDialog::populateResults()
{
    m_resultTable->blockSignals(true);
    m_resultTable->setRowCount(m_report.files.size());
    for (int row = 0; row < m_report.files.size(); ++row)
    {
        const ScriptProjectSearchFileResult& file = m_report.files[row];
        auto* selectionItem = new QTableWidgetItem();
        selectionItem->setData(FilePathRole, file.filePath);
        selectionItem->setData(ResultIndexRole, row);
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        const bool blocked = isBlocked(file.filePath);
        if (file.hasChanges() && !blocked)
        {
            flags |= Qt::ItemIsUserCheckable;
            selectionItem->setCheckState(Qt::Checked);
        }
        selectionItem->setFlags(flags);
        m_resultTable->setItem(row, 0, selectionItem);
        m_resultTable->setItem(
            row, 1, new QTableWidgetItem(file.relativePath));
        m_resultTable->setItem(row, 2,
            new QTableWidgetItem(QString::number(file.matchCount)));
        const QString state = blocked
            ? tr("有未保存修改，不能发布")
            : (file.hasChanges() ? tr("待替换") : tr("无字节变化"));
        m_resultTable->setItem(row, 3, new QTableWidgetItem(state));
    }
    m_resultTable->blockSignals(false);

    m_summaryLabel->setText(tr(
        "已扫描 %1/%2 个候选（脚本 %3，跳过 %4），命中文件 %5，"
        "匹配 %6，问题 %7；耗时 %8 ms。逐文件查看前后快照并勾选，"
        "事务替换不会进入当前文档 Undo。")
        .arg(m_report.scannedFiles)
        .arg(m_report.totalFiles)
        .arg(m_report.scriptFiles)
        .arg(m_report.skippedFiles)
        .arg(m_report.matchingFiles)
        .arg(m_report.totalMatches)
        .arg(m_report.issues.size())
        .arg(m_report.elapsedMilliseconds));
    if (!m_report.issues.isEmpty())
    {
        QStringList issueLines;
        for (const ScriptProjectSearchIssue& issue : m_report.issues)
        {
            issueLines.append(QStringLiteral("%1: %2")
                .arg(issue.relativePath, issue.message));
        }
        m_issueLabel->setVisible(true);
        m_issueLabel->setText(tr("有 %1 个文件问题；这些文件未进入结果。")
            .arg(m_report.issues.size()));
        m_issueLabel->setToolTip(issueLines.join('\n'));
    }
    if (m_resultTable->rowCount() > 0)
        m_resultTable->selectRow(0);
    updatePublishButton();
}

void ScriptProjectSearchDialog::updatePreview()
{
    const int row = m_resultTable->currentRow();
    if (row < 0 || row >= m_report.files.size())
    {
        m_beforePreview->clear();
        m_afterPreview->clear();
        return;
    }
    const int resultIndex = m_resultTable->item(row, 0)
        ->data(ResultIndexRole).toInt();
    if (resultIndex < 0 || resultIndex >= m_report.files.size())
        return;
    const ScriptProjectSearchFileResult& file = m_report.files[resultIndex];
    m_beforePreview->setPlainText(file.beforeText);
    m_afterPreview->setPlainText(file.afterText);
    m_beforePreview->document()->setModified(false);
    m_afterPreview->document()->setModified(false);
}

void ScriptProjectSearchDialog::updatePublishButton()
{
    const QStringList selectedPaths = selectedFilePaths();
    if (!m_confirmedFilePaths.isEmpty() &&
        selectedPaths != m_confirmedFilePaths)
    {
        resetPublishConfirmation();
    }
    m_publishButton->setEnabled(!selectedPaths.isEmpty());
}

void ScriptProjectSearchDialog::resetPublishConfirmation()
{
    m_confirmedFilePaths.clear();
    m_confirmedReplacementCount = 0;
    if (m_confirmationLabel)
    {
        m_confirmationLabel->clear();
        m_confirmationLabel->setVisible(false);
    }
    if (m_cancelConfirmationButton)
        m_cancelConfirmationButton->setVisible(false);
    if (m_publishButton)
        m_publishButton->setText(tr("事务替换所选文件"));
}

void ScriptProjectSearchDialog::updateConfirmationText()
{
    if (m_confirmedFilePaths.isEmpty())
        return;
    m_confirmationLabel->setText(tr(
        "将把 %1 个文件中的 %2 处匹配作为一个耐久事务写入。"
        "此操作不会进入当前文档 Undo。请再次点击“确认事务替换”，"
        "或取消后调整选择。")
        .arg(m_confirmedFilePaths.size())
        .arg(m_confirmedReplacementCount));
    m_confirmationLabel->setVisible(true);
    m_cancelConfirmationButton->setVisible(true);
    m_publishButton->setText(tr("确认事务替换"));
}

QStringList ScriptProjectSearchDialog::selectedFilePaths() const
{
    QStringList paths;
    for (int row = 0; row < m_resultTable->rowCount(); ++row)
    {
        const QTableWidgetItem* item = m_resultTable->item(row, 0);
        if ((item->flags() & Qt::ItemIsUserCheckable) != 0 &&
            item->checkState() == Qt::Checked)
        {
            paths.append(item->data(FilePathRole).toString());
        }
    }
    return paths;
}

bool ScriptProjectSearchDialog::isBlocked(const QString& filePath) const
{
    return m_blockedKeys.contains(
        EditorAssetPath::comparisonKey(filePath));
}

void ScriptProjectSearchDialog::publishSelectedFiles()
{
    const QStringList selectedPaths = selectedFilePaths();
    if (selectedPaths.isEmpty())
    {
        QMessageBox::information(this, tr("项目脚本搜索与替换"),
            tr("请至少选择一个有变化且无未保存冲突的文件。"));
        return;
    }

    int replacementCount = 0;
    for (const QString& selectedPath : selectedPaths)
    {
        for (const ScriptProjectSearchFileResult& file : m_report.files)
        {
            if (EditorAssetPath::comparisonKey(file.filePath) ==
                EditorAssetPath::comparisonKey(selectedPath))
            {
                replacementCount += file.matchCount;
                break;
            }
        }
    }
    if (selectedPaths != m_confirmedFilePaths)
    {
        m_confirmedFilePaths = selectedPaths;
        m_confirmedReplacementCount = replacementCount;
        updateConfirmationText();
        return;
    }

    m_publishResult = ScriptProjectSearch::publish(
        m_activeContentRoot, m_report, selectedPaths, m_blockedFilePaths);
    if (!m_publishResult.success)
    {
        QMessageBox::critical(this, tr("项目脚本替换失败"),
            m_publishResult.errorMessage);
        return;
    }

    accept();
}

void ScriptProjectSearchDialog::retranslateUi()
{
    setWindowTitle(tr("项目脚本搜索与替换"));
    m_rootLabel->setText(tr(
        "活动内容根：%1\n只扫描 script/** 中的现有 Lua 文本；依赖根不枚举。")
        .arg(m_activeContentRoot));
    m_queryLabel->setText(tr("搜索文本："));
    m_replacementLabel->setText(tr("替换文本："));
    m_queryEdit->setPlaceholderText(tr("输入项目脚本搜索文本"));
    m_replacementEdit->setPlaceholderText(
        tr("替换文本按字面量插入，可为空"));
    m_caseSensitiveCheckBox->setText(tr("区分大小写"));
    m_wholeWordsCheckBox->setText(tr("全字匹配"));
    m_regularExpressionCheckBox->setText(tr("正则"));
    m_searchButton->setText(tr("搜索"));
    if (m_confirmedFilePaths.isEmpty())
        m_publishButton->setText(tr("事务替换所选文件"));
    else
        updateConfirmationText();
    m_cancelConfirmationButton->setText(tr("取消确认"));
    m_beforeLabel->setText(tr("替换前"));
    m_afterLabel->setText(tr("替换后"));
    m_resultTable->setHorizontalHeaderLabels(
        {tr("选择"), tr("文件"), tr("匹配"), tr("状态")});
}
