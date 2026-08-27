#include "AssetReferenceDialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace
{
constexpr int StatusRole = Qt::UserRole;
constexpr int SourcePathRole = Qt::UserRole + 1;
constexpr int IssueStatusValue = -1;
}

AssetReferenceDialog::AssetReferenceDialog(
    const AssetReferenceScanReport& report, QWidget* parent)
    : QDialog(parent)
    , m_report(report)
{
    setObjectName(QStringLiteral("assetReferenceDialog"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName(QStringLiteral("assetReferenceSummaryLabel"));
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("assetReferenceSearchEdit"));
    m_searchEdit->setClearButtonEnabled(true);
    layout->addWidget(m_searchEdit);

    m_missingOnlyCheckBox = new QCheckBox(this);
    m_missingOnlyCheckBox->setObjectName(
        QStringLiteral("assetReferenceMissingOnlyCheckBox"));
    layout->addWidget(m_missingOnlyCheckBox);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("assetReferenceTable"));
    m_table->setColumnCount(6);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    layout->addWidget(m_table, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName(QStringLiteral("assetReferenceDialogButtons"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_searchEdit, &QLineEdit::textChanged,
        this, &AssetReferenceDialog::applyFilter);
    connect(m_missingOnlyCheckBox, &QCheckBox::toggled,
        this, &AssetReferenceDialog::applyFilter);
    connect(m_table, &QTableWidget::cellDoubleClicked, this,
        [this](int row, int)
        {
            const QString sourcePath =
                m_table->item(row, 1)->data(SourcePathRole).toString();
            if (!sourcePath.isEmpty())
            {
                m_selectedSourceFile = sourcePath;
                m_selectedLineNumber = m_table->item(row, 2)->text().toInt();
                accept();
            }
        });

    populate(report);
    retranslateUi();
    resize(1180, 720);
}

QString AssetReferenceDialog::selectedSourceFile() const
{
    return m_selectedSourceFile;
}

int AssetReferenceDialog::selectedLineNumber() const
{
    return m_selectedLineNumber;
}

void AssetReferenceDialog::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void AssetReferenceDialog::populate(const AssetReferenceScanReport& report)
{
    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(report.occurrences.size() + report.issues.size());
    int row = 0;
    for (const AssetReferenceOccurrence& occurrence : report.occurrences)
    {
        auto* statusItem = new QTableWidgetItem();
        statusItem->setData(StatusRole, static_cast<int>(occurrence.status));
        if (occurrence.status == AssetReferenceStatus::Missing ||
            occurrence.status == AssetReferenceStatus::Invalid)
        {
            statusItem->setForeground(QColor(210, 70, 70));
        }
        auto* sourceItem = new QTableWidgetItem(occurrence.sourceRelativePath);
        sourceItem->setData(SourcePathRole, occurrence.sourceFilePath);
        m_table->setItem(row, 0, statusItem);
        m_table->setItem(row, 1, sourceItem);
        m_table->setItem(row, 2,
            new QTableWidgetItem(QString::number(occurrence.lineNumber)));
        const QString field = occurrence.section.isEmpty()
            ? occurrence.field
            : occurrence.section + QStringLiteral("/") + occurrence.field;
        m_table->setItem(row, 3, new QTableWidgetItem(field));
        m_table->setItem(row, 4, new QTableWidgetItem(occurrence.reference));
        m_table->setItem(row, 5, new QTableWidgetItem(occurrence.resolvedFilePath));
        ++row;
    }
    for (const AssetReferenceScanIssue& issue : report.issues)
    {
        auto* statusItem = new QTableWidgetItem();
        statusItem->setData(StatusRole, IssueStatusValue);
        statusItem->setForeground(QColor(210, 70, 70));
        auto* sourceItem = new QTableWidgetItem(issue.sourceRelativePath);
        sourceItem->setData(SourcePathRole, issue.sourceFilePath);
        m_table->setItem(row, 0, statusItem);
        m_table->setItem(row, 1, sourceItem);
        m_table->setItem(row, 2, issue.lineNumber > 0
            ? new QTableWidgetItem(QString::number(issue.lineNumber))
            : new QTableWidgetItem());
        m_table->setItem(row, 3, new QTableWidgetItem());
        m_table->setItem(row, 4, new QTableWidgetItem(issue.message));
        m_table->setItem(row, 5, new QTableWidgetItem());
        ++row;
    }
    m_table->setUpdatesEnabled(true);
}

void AssetReferenceDialog::applyFilter()
{
    const QString query = m_searchEdit->text().trimmed();
    const bool missingOnly = m_missingOnlyCheckBox->isChecked();
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        const int status = m_table->item(row, 0)->data(StatusRole).toInt();
        bool visible = !missingOnly ||
            status == static_cast<int>(AssetReferenceStatus::Missing);
        if (visible && !query.isEmpty())
        {
            visible = false;
            for (int column = 1; column < m_table->columnCount(); ++column)
            {
                if (m_table->item(row, column)->text().contains(
                        query, Qt::CaseInsensitive))
                {
                    visible = true;
                    break;
                }
            }
        }
        m_table->setRowHidden(row, !visible);
    }
}

void AssetReferenceDialog::retranslateUi()
{
    setWindowTitle(tr("资源引用搜索"));
    m_summaryLabel->setText(tr(
        "已扫描 %1/%2 个文件（INI %3，Lua %4），静态引用 %5，"
        "缺失 %6，Lua 字面量候选 %7，动态拼接 %8，问题 %9；耗时 %10 ms。"
        "缺失只表示本工具固定字段在当前内容根顺序中没有命中，不代表项目完整性或可启动性。")
        .arg(m_report.scannedFiles)
        .arg(m_report.totalFiles)
        .arg(m_report.iniFiles)
        .arg(m_report.scriptFiles)
        .arg(m_report.staticReferences)
        .arg(m_report.missingReferences)
        .arg(m_report.luaCandidates)
        .arg(m_report.dynamicExpressions)
        .arg(m_report.issues.size())
        .arg(m_report.elapsedMilliseconds));
    m_searchEdit->setPlaceholderText(
        tr("筛选来源、字段、引用或解析路径"));
    m_missingOnlyCheckBox->setText(tr("只显示缺失的静态引用"));
    m_table->setHorizontalHeaderLabels(
        {tr("状态"), tr("来源文件"), tr("行"), tr("段/字段"),
         tr("引用"), tr("解析路径")});
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        QTableWidgetItem* item = m_table->item(row, 0);
        const int statusValue = item->data(StatusRole).toInt();
        item->setText(statusValue == IssueStatusValue
            ? tr("问题")
            : statusText(static_cast<AssetReferenceStatus>(statusValue)));
    }
}

QString AssetReferenceDialog::statusText(AssetReferenceStatus status) const
{
    switch (status)
    {
    case AssetReferenceStatus::Resolved:
        return tr("已解析");
    case AssetReferenceStatus::Missing:
        return tr("缺失");
    case AssetReferenceStatus::Candidate:
        return tr("字面量候选");
    case AssetReferenceStatus::Dynamic:
        return tr("动态拼接");
    case AssetReferenceStatus::Invalid:
        return tr("非法引用");
    }
    return QString();
}
