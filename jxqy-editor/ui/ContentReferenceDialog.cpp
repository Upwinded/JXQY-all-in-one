#include "ContentReferenceDialog.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

void showContentReferences(QWidget* parent, const QString& root,
    const ContentReferenceReport& report,
    const std::function<void(const QString&, int)>& openReference)
{
    auto tr = [](const char* source) { return QCoreApplication::translate("ContentReferenceDialog", source); };
    QDialog dialog(parent);
    dialog.setObjectName("contentReferencesDialog");
    dialog.setWindowTitle(tr("查看引用"));
    dialog.resize(780, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* label = new QLabel(tr("找到 %1 处明确引用；双击可打开。删除前请先修改引用。")
        .arg(report.occurrences.size()), &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);
    auto* tree = new QTreeWidget(&dialog);
    tree->setObjectName("contentReferenceList");
    tree->setHeaderLabels({tr("文件"), tr("行"), tr("用途")});
    tree->setRootIsDecorated(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    auto append = [&](const ContentReferenceOccurrence& occurrence, const QString& context)
    {
        auto* item = new QTreeWidgetItem(tree, {QDir(root).relativeFilePath(occurrence.filePath),
            QString::number(occurrence.line), context});
        item->setData(0, Qt::UserRole, occurrence.sourcePath);
        item->setData(1, Qt::UserRole, occurrence.line);
    };
    for (const auto& occurrence : report.occurrences) append(occurrence, occurrence.context);
    for (const auto& occurrence : report.dynamicReferences)
        append(occurrence, occurrence.context == "script — duplicate filename" ? tr("存在同名脚本，需确认调用地图")
            : occurrence.context == "showgivegoodswin" ? tr("可按显示名或文件名查找，需确认")
            : occurrence.offset >= 0 ? tr("调用地图未确定") : tr("动态调用：%1").arg(occurrence.context));
    for (const auto& path : report.errors)
        new QTreeWidgetItem(tree, {QDir(root).relativeFilePath(path), QString(), tr("无法读取或文本格式不受支持")});
    layout->addWidget(tree);
    if (!report.dynamicReferences.isEmpty() || !report.errors.isEmpty())
    {
        auto* hint = new QLabel(tr("有 %1 处调用需要确认，%2 个文件无法读取。需要确认的调用不会自动改写，请在列表中查看原因。")
            .arg(report.dynamicReferences.size()).arg(report.errors.size()), &dialog);
        hint->setWordWrap(true);
        layout->addWidget(hint);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(tree, &QTreeWidget::itemDoubleClicked, &dialog, [&](QTreeWidgetItem* item, int)
    {
        const QString path = item->data(0, Qt::UserRole).toString();
        if (path.isEmpty()) return;
        openReference(path, item->data(1, Qt::UserRole).toInt());
        dialog.accept();
    });
    dialog.exec();
}
