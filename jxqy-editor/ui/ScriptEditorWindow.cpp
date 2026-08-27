#include "ScriptEditorWindow.h"
#include "ScriptCallDialog.h"
#include "ui_ScriptEditorWindow.h"
#include "../core/LuaFunctionOutline.h"
#include "../core/ScriptConverter.h"
#include "../core/Util.h"
#include "../core/EditorAssetPath.h"
#include "ThemeManager.h"

#include <QPainter>
#include <QTextBlock>
#include <QScrollBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QShortcut>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QKeyEvent>
#include <QCompleter>
#include <QAbstractItemView>
#include <QMimeData>
#include <QInputDialog>
#include <QStringListModel>
#include <QFile>
#include <QFileInfo>
#include <QEvent>
#include <QHeaderView>
#include <QTreeWidget>
#include <QTextDocument>
#include <QCryptographicHash>

#include <algorithm>
#include <utility>

static const QStringList LUA_KEYWORDS = {
    "and", "break", "do", "else", "elseif", "end",
    "false", "for", "function", "goto", "if", "in",
    "local", "nil", "not", "or", "repeat", "return",
    "then", "true", "until", "while"
};

static const QStringList LUA_BUILTINS = {
    "print", "type", "tostring", "tonumber", "pairs", "ipairs",
    "next", "select", "unpack", "require", "pcall", "xpcall",
    "error", "assert", "loadstring", "setmetatable", "getmetatable",
    "table", "string", "math", "io", "os", "coroutine"
};

// ========== ScriptSyntaxHighlighter 实现 ==========

ScriptSyntaxHighlighter::ScriptSyntaxHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent)
{
    commentStartExpression = QRegularExpression("--\\[\\[");
    commentEndExpression = QRegularExpression("\\]\\]");

    buildRules();
}

void ScriptSyntaxHighlighter::refreshForTheme()
{
    buildRules();
    rehighlight();
}

void ScriptSyntaxHighlighter::buildRules()
{
    rules.clear();

    // 主题相关配色：深色主题使用更高亮度的前景色，避免在深色编辑器底色上不可读。
    const bool dark = ThemeManager::instance().currentTheme() == ThemeManager::Theme::Dark;

    HighlightRule rule;

    QTextCharFormat commentFormat;
    commentFormat.setForeground(dark ? QColor(106, 153, 85) : QColor(0, 128, 0));
    commentFormat.setFontItalic(true);
    rule.pattern = QRegularExpression("--[^\n]*");
    rule.format = commentFormat;
    rules.append(rule);

    QTextCharFormat numberFormat;
    numberFormat.setForeground(dark ? QColor(181, 206, 168) : QColor(180, 100, 0));
    rule.pattern = QRegularExpression("\\b[0-9]+\\.?[0-9]*\\b");
    rule.format = numberFormat;
    rules.append(rule);

    QTextCharFormat stringFormat;
    stringFormat.setForeground(dark ? QColor(206, 145, 120) : QColor(163, 21, 21));
    rule.pattern = QRegularExpression("\"[^\"\\\\]*(?:\\\\.[^\"\\\\]*)*\"");
    rule.format = stringFormat;
    rules.append(rule);

    rule.pattern = QRegularExpression("'[^'\\\\]*(?:\\\\.[^'\\\\]*)*'");
    rule.format = stringFormat;
    rules.append(rule);

    rule.pattern = QRegularExpression("\\[\\[.*?\\]\\]");
    rule.format = stringFormat;
    rules.append(rule);

    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(dark ? QColor(86, 156, 214) : QColor(0, 0, 255));
    keywordFormat.setFontWeight(QFont::Bold);
    for (const QString& keyword : LUA_KEYWORDS)
    {
        rule.pattern = QRegularExpression("\\b" + keyword + "\\b");
        rule.format = keywordFormat;
        rules.append(rule);
    }

    QTextCharFormat builtinFormat;
    builtinFormat.setForeground(dark ? QColor(197, 134, 192) : QColor(128, 0, 128));
    for (const QString& builtin : LUA_BUILTINS)
    {
        rule.pattern = QRegularExpression("\\b" + builtin + "\\b");
        rule.format = builtinFormat;
        rules.append(rule);
    }

    QTextCharFormat apiFormat;
    apiFormat.setForeground(dark ? QColor(220, 220, 170) : QColor(43, 145, 175));
    apiFormat.setFontWeight(QFont::Bold);
    for (const QString& api : gameApiSet)
    {
        rule.pattern = QRegularExpression(
            "\\b" + QRegularExpression::escape(api) + "\\b",
            QRegularExpression::CaseInsensitiveOption);
        rule.format = apiFormat;
        rules.append(rule);
    }

    multiLineCommentFormat.setForeground(dark ? QColor(106, 153, 85) : QColor(0, 128, 0));
    multiLineCommentFormat.setFontItalic(true);
}

void ScriptSyntaxHighlighter::setApiNames(const QSet<QString>& names)
{
    gameApiSet = names;
    buildRules();
    rehighlight();
}

void ScriptSyntaxHighlighter::highlightBlock(const QString& text)
{
    for (const HighlightRule& rule : rules)
    {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext())
        {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    setCurrentBlockState(0);
    int startIndex = 0;
    if (previousBlockState() != 1)
    {
        QRegularExpressionMatch match = commentStartExpression.match(text);
        startIndex = match.hasMatch() ? match.capturedStart() : -1;
    }

    while (startIndex >= 0)
    {
        QRegularExpressionMatch endMatch = commentEndExpression.match(text, startIndex + 2);
        int endIndex = endMatch.hasMatch() ? endMatch.capturedStart() : -1;
        int commentLength;

        if (endIndex == -1)
        {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        }
        else
        {
            commentLength = endIndex - startIndex + endMatch.capturedLength();
        }

        setFormat(startIndex, commentLength, multiLineCommentFormat);
        QRegularExpressionMatch nextMatch = commentStartExpression.match(text, startIndex + commentLength);
        startIndex = nextMatch.hasMatch() ? nextMatch.capturedStart() : -1;
    }
}

// ========== ScriptEditor 实现 ==========

ScriptEditor::ScriptEditor(QWidget* parent)
    : QPlainTextEdit(parent)
{
    lineNumberArea = new LineNumberArea(this);

    connect(this, &ScriptEditor::blockCountChanged, this, &ScriptEditor::updateLineNumberAreaWidth);
    connect(this, &ScriptEditor::updateRequest, this, &ScriptEditor::updateLineNumberArea);
    connect(this, &ScriptEditor::cursorPositionChanged, this, &ScriptEditor::highlightCurrentLine);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();

    QFont font("Consolas", 11);
    font.setFixedPitch(true);
    setFont(font);

    setTabStopDistance(QFontMetricsF(font).horizontalAdvance(' ') * 4);
    setMouseTracking(true);
}

void ScriptEditor::setCompleter(QCompleter* c)
{
    if (completer)
    {
        disconnect(completer, nullptr, this, nullptr);
    }

    completer = c;
    if (!completer)
        return;

    completer->setWidget(this);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    connect(completer, QOverload<const QString&>::of(&QCompleter::activated),
            this, &ScriptEditor::insertCompletion);
}

QCompleter* ScriptEditor::getCompleter() const
{
    return completer;
}

void ScriptEditor::insertCompletion(const QString& completion)
{
    if (completer->widget() != this)
        return;

    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    tc.insertText(completion);
    setTextCursor(tc);
}

QString ScriptEditor::textUnderCursor() const
{
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    return tc.selectedText();
}

void ScriptEditor::focusInEvent(QFocusEvent* event)
{
    if (completer)
        completer->setWidget(this);
    QPlainTextEdit::focusInEvent(event);
}

void ScriptEditor::mouseMoveEvent(QMouseEvent* event)
{
    QPlainTextEdit::mouseMoveEvent(event);
    emit mouseHovered(event->pos());
}

void ScriptEditor::keyPressEvent(QKeyEvent* event)
{
    if (completer && completer->popup()->isVisible())
    {
        switch (event->key())
        {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            event->ignore();
            return;
        default:
            break;
        }
    }

    bool isShortcut = (event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Space;
    if (!isShortcut)
        QPlainTextEdit::keyPressEvent(event);

    const bool ctrlOrShift = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    if (ctrlOrShift && event->text().isEmpty())
        return;

    static QString eow("~!@#$%^&*()_+{}|:\"<>?,./;'[]\\-=");
    bool hasModifier = (event->modifiers() != Qt::NoModifier) && !ctrlOrShift;
    QString completionPrefix = textUnderCursor();

    if (!completer || (hasModifier || event->text().isEmpty() || completionPrefix.length() < 2
        || eow.contains(event->text().right(1))))
    {
        if (completer)
            completer->popup()->hide();
        return;
    }

    if (completionPrefix != completer->completionPrefix())
    {
        completer->setCompletionPrefix(completionPrefix);
        completer->popup()->setCurrentIndex(completer->completionModel()->index(0, 0));
    }

    QRect cr = cursorRect();
    cr.setWidth(completer->popup()->sizeHintForColumn(0)
                + completer->popup()->verticalScrollBar()->sizeHint().width());
    completer->complete(cr);
}

int ScriptEditor::lineNumberAreaWidth()
{
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10)
    {
        max /= 10;
        ++digits;
    }
    int space = 3 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return space;
}

void ScriptEditor::updateLineNumberAreaWidth(int)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void ScriptEditor::updateLineNumberArea(const QRect& rect, int dy)
{
    if (dy)
        lineNumberArea->scroll(0, dy);
    else
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void ScriptEditor::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);

    QRect cr = contentsRect();
    lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void ScriptEditor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> extraSelections;

    // 深色主题下使用较编辑器底色略亮且偏蓝的低对比带，避免浅色高亮带淹没语法高亮文字。
    const bool dark = ThemeManager::instance().currentTheme() == ThemeManager::Theme::Dark;
    QColor lineColor = dark ? QColor(40, 47, 62) : QColor(232, 242, 255);

    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(lineColor);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    extraSelections.append(selection);

    setExtraSelections(extraSelections);
}

void ScriptEditor::lineNumberAreaPaintEvent(QPaintEvent* event)
{
    QPainter painter(lineNumberArea);
    painter.fillRect(event->rect(), QColor(240, 240, 240));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom())
    {
        if (block.isVisible() && bottom >= event->rect().top())
        {
            QString number = QString::number(blockNumber + 1);
            painter.setPen(QColor(128, 128, 128));
            painter.drawText(0, top, lineNumberArea->width() - 3, fontMetrics().height(),
                             Qt::AlignRight, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

// ========== FindReplaceBar 实现 ==========

FindReplaceBar::FindReplaceBar(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 2, 4, 2);

    auto* findLayout = new QHBoxLayout();
    findLabel = new QLabel(tr("查找:"));
    findLabel->setObjectName(QStringLiteral("findLabel"));
    findLayout->addWidget(findLabel);
    findInput = new QLineEdit();
    findInput->setMinimumWidth(250);
    findLayout->addWidget(findInput);
    caseSensitiveCheck = new QCheckBox(tr("区分大小写"));
    findLayout->addWidget(caseSensitiveCheck);
    wholeWordCheck = new QCheckBox(tr("全字匹配"));
    findLayout->addWidget(wholeWordCheck);
    regexCheck = new QCheckBox(tr("正则"));
    findLayout->addWidget(regexCheck);

    findPreviousButton = new QPushButton(tr("上一个"));
    findPreviousButton->setObjectName(QStringLiteral("findPreviousButton"));
    findNextButton = new QPushButton(tr("下一个"));
    findNextButton->setObjectName(QStringLiteral("findNextButton"));
    findLayout->addWidget(findPreviousButton);
    findLayout->addWidget(findNextButton);

    closeButton = new QPushButton(tr("×"));
    closeButton->setObjectName(QStringLiteral("findCloseButton"));
    closeButton->setMaximumWidth(30);
    findLayout->addWidget(closeButton);

    mainLayout->addLayout(findLayout);

    auto* replaceLayout = new QHBoxLayout();
    replaceLabel = new QLabel(tr("替换:"));
    replaceLabel->setObjectName(QStringLiteral("replaceLabel"));
    replaceLayout->addWidget(replaceLabel);
    replaceInput = new QLineEdit();
    replaceInput->setMinimumWidth(250);
    replaceLayout->addWidget(replaceInput);

    replaceButton = new QPushButton(tr("替换"));
    replaceButton->setObjectName(QStringLiteral("replaceButton"));
    replaceAllButton = new QPushButton(tr("全部替换"));
    replaceAllButton->setObjectName(QStringLiteral("replaceAllButton"));
    replaceLayout->addWidget(replaceButton);
    replaceLayout->addWidget(replaceAllButton);
    replaceLayout->addStretch();

    mainLayout->addLayout(replaceLayout);

    connect(findNextButton, &QPushButton::clicked, this, &FindReplaceBar::onFindNext);
    connect(findPreviousButton, &QPushButton::clicked, this, &FindReplaceBar::onFindPrevious);
    connect(replaceButton, &QPushButton::clicked, this, &FindReplaceBar::onReplace);
    connect(replaceAllButton, &QPushButton::clicked, this, &FindReplaceBar::onReplaceAll);
    connect(closeButton, &QPushButton::clicked, this, &FindReplaceBar::closeRequested);
    connect(findInput, &QLineEdit::returnPressed, this, &FindReplaceBar::onFindNext);

    hide();
}

void FindReplaceBar::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        findLabel->setText(tr("查找:"));
        caseSensitiveCheck->setText(tr("区分大小写"));
        wholeWordCheck->setText(tr("全字匹配"));
        regexCheck->setText(tr("正则"));
        findPreviousButton->setText(tr("上一个"));
        findNextButton->setText(tr("下一个"));
        closeButton->setText(tr("×"));
        replaceLabel->setText(tr("替换:"));
        replaceButton->setText(tr("替换"));
        replaceAllButton->setText(tr("全部替换"));
    }
    QWidget::changeEvent(event);
}

void FindReplaceBar::setEditor(QPlainTextEdit* editor)
{
    textEdit = editor;
}

void FindReplaceBar::focusFindInput()
{
    findInput->setFocus();
    if (textEdit && textEdit->textCursor().hasSelection())
    {
        findInput->setText(textEdit->textCursor().selectedText());
    }
    findInput->selectAll();
}

void FindReplaceBar::onFindNext()
{
    if (!textEdit || findInput->text().isEmpty())
        return;

    QTextDocument::FindFlags flags;
    if (caseSensitiveCheck->isChecked())
        flags |= QTextDocument::FindCaseSensitively;
    if (wholeWordCheck->isChecked())
        flags |= QTextDocument::FindWholeWords;

    bool found = false;
    if (regexCheck->isChecked())
    {
        QRegularExpression regex(findInput->text());
        if (!regex.isValid())
        {
            QMessageBox::warning(this, tr("查找"), regex.errorString());
            return;
        }
        found = textEdit->find(regex, flags);
    }
    else
    {
        found = textEdit->find(findInput->text(), flags);
    }

    if (!found)
    {
        QTextCursor cursor = textEdit->textCursor();
        cursor.movePosition(QTextCursor::Start);
        textEdit->setTextCursor(cursor);
        if (regexCheck->isChecked())
        {
            QRegularExpression regex(findInput->text());
            if (!regex.isValid())
                return;
            if (!textEdit->find(regex, flags))
            {
                QMessageBox::information(this, tr("查找"),
                    tr("未找到匹配内容"));
            }
        }
        else
        {
            if (!textEdit->find(findInput->text(), flags))
            {
                QMessageBox::information(this, tr("查找"),
                    tr("未找到匹配内容"));
            }
        }
    }
}

void FindReplaceBar::onFindPrevious()
{
    if (!textEdit || findInput->text().isEmpty())
        return;

    QTextDocument::FindFlags flags = QTextDocument::FindBackward;
    if (caseSensitiveCheck->isChecked())
        flags |= QTextDocument::FindCaseSensitively;
    if (wholeWordCheck->isChecked())
        flags |= QTextDocument::FindWholeWords;

    bool found = false;
    if (regexCheck->isChecked())
    {
        QRegularExpression regex(findInput->text());
        if (!regex.isValid())
        {
            QMessageBox::warning(this, tr("查找"), regex.errorString());
            return;
        }
        found = textEdit->find(regex, flags);
    }
    else
    {
        found = textEdit->find(findInput->text(), flags);
    }

    if (!found)
    {
        QTextCursor cursor = textEdit->textCursor();
        cursor.movePosition(QTextCursor::End);
        textEdit->setTextCursor(cursor);
        if (regexCheck->isChecked())
        {
            QRegularExpression regex(findInput->text());
            if (!regex.isValid())
                return;
            if (!textEdit->find(regex, flags))
            {
                QMessageBox::information(this, tr("查找"),
                    tr("未找到匹配内容"));
            }
        }
        else
        {
            if (!textEdit->find(findInput->text(), flags))
            {
                QMessageBox::information(this, tr("查找"),
                    tr("未找到匹配内容"));
            }
        }
    }
}

void FindReplaceBar::onReplace()
{
    if (!textEdit || findInput->text().isEmpty())
        return;

    QTextCursor cursor = textEdit->textCursor();
    bool selectedMatch = false;
    if (cursor.hasSelection())
    {
        const QString selectedText = cursor.selectedText();
        if (regexCheck->isChecked())
        {
            QRegularExpression regex(findInput->text());
            if (!regex.isValid())
            {
                QMessageBox::warning(this, tr("替换"), regex.errorString());
                return;
            }
            QRegularExpressionMatch match = regex.match(selectedText);
            selectedMatch = match.hasMatch() && match.capturedStart() == 0 &&
                match.capturedLength() == selectedText.length();
        }
        else
        {
            selectedMatch = selectedText.compare(findInput->text(),
                caseSensitiveCheck->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0;
        }
    }
    if (!selectedMatch)
    {
        onFindNext();
        return;
    }
    cursor.insertText(replaceInput->text());
    onFindNext();
}

void FindReplaceBar::onReplaceAll()
{
    if (!textEdit || findInput->text().isEmpty())
        return;

    QTextCursor cursor = textEdit->textCursor();
    cursor.movePosition(QTextCursor::Start);
    textEdit->setTextCursor(cursor);

    QTextDocument::FindFlags flags;
    if (caseSensitiveCheck->isChecked())
        flags |= QTextDocument::FindCaseSensitively;
    if (wholeWordCheck->isChecked())
        flags |= QTextDocument::FindWholeWords;

    int count = 0;
    QTextCursor editCursor = textEdit->textCursor();
    editCursor.beginEditBlock();

    if (regexCheck->isChecked())
    {
        QRegularExpression regex(findInput->text());
        if (!regex.isValid() ||
            (regex.match(QString()).hasMatch() && regex.match(QString()).capturedLength() == 0))
        {
            QMessageBox::warning(this, tr("替换"),
                regex.isValid() ? tr("正则表达式不能匹配空字符串") : regex.errorString());
            editCursor.endEditBlock();
            return;
        }
        while (textEdit->find(regex, flags))
        {
            QTextCursor foundCursor = textEdit->textCursor();
            foundCursor.insertText(replaceInput->text());
            count++;
        }
    }
    else
    {
        while (textEdit->find(findInput->text(), flags))
        {
            QTextCursor foundCursor = textEdit->textCursor();
            foundCursor.insertText(replaceInput->text());
            count++;
        }
    }

    editCursor.endEditBlock();

    QMessageBox::information(this, tr("替换完成"),
        tr("共替换 %1 处").arg(count));
}

// ========== ApiCompleter 实现 ==========

ApiCompleter::ApiCompleter(const QVector<ApiInfo>& apis, QObject* parent)
    : QCompleter(parent)
{
    QStringList completions;
    for (const ApiInfo& api : apis)
    {
        const QString functionName = api.signature.section('(', 0, 0).trimmed().toLower();
        QString runtimeSignature = api.signature;
        runtimeSignature.replace(0, api.signature.indexOf('('), functionName);
        apiMap[functionName] = api;
        completions.append(runtimeSignature);
    }

    completions.sort(Qt::CaseInsensitive);
    setModel(new QStringListModel(completions, this));
    setCompletionColumn(0);
    setCaseSensitivity(Qt::CaseInsensitive);
    setCompletionMode(QCompleter::PopupCompletion);
    setFilterMode(Qt::MatchStartsWith);
}

QString ApiCompleter::getTooltipForCompletion(const QString& completion) const
{
    QString funcName = completion.section('(', 0, 0).trimmed().toLower();
    auto it = apiMap.find(funcName);
    if (it != apiMap.end())
    {
        return it->tooltip;
    }
    auto extraIt = extraTooltipMap.find(completion);
    if (extraIt != extraTooltipMap.end())
    {
        return extraIt.value();
    }
    return QString();
}

const ApiInfo* ApiCompleter::getApiInfo(const QString& functionName) const
{
    auto it = apiMap.find(functionName.toLower());
    if (it != apiMap.end())
    {
        return &it.value();
    }
    return nullptr;
}

void ApiCompleter::addExtraCompletions(const QStringList& words, const QString& tooltip)
{
    QStringListModel* model = qobject_cast<QStringListModel*>(this->model());
    if (!model)
        return;

    QStringList list = model->stringList();
    for (const QString& word : words)
    {
        if (word.isEmpty())
            continue;
        if (!list.contains(word, Qt::CaseSensitive))
            list.append(word);
        if (!tooltip.isEmpty())
            extraTooltipMap.insert(word, tooltip);
    }
    list.sort(Qt::CaseInsensitive);
    model->setStringList(list);
}

void ApiCompleter::setExtraTooltip(const QString& word, const QString& tooltip)
{
    if (word.isEmpty())
        return;
    extraTooltipMap.insert(word, tooltip);
}

void ApiCompleter::clearExtraCompletions()
{
    extraTooltipMap.clear();

    QStringList completions;
    for (const ApiInfo& api : apiMap)
    {
        const QString functionName = api.signature.section('(', 0, 0).trimmed().toLower();
        QString runtimeSignature = api.signature;
        runtimeSignature.replace(0, api.signature.indexOf('('), functionName);
        completions.append(runtimeSignature);
    }
    completions.sort(Qt::CaseInsensitive);

    QStringListModel* model = qobject_cast<QStringListModel*>(this->model());
    if (model)
        model->setStringList(completions);
}

QString ApiCompleter::getTooltipForWord(const QString& word) const
{
    if (word.isEmpty())
        return QString();

    auto apiIt = apiMap.find(word.toLower());
    if (apiIt != apiMap.end())
    {
        return apiIt->tooltip;
    }
    auto extraIt = extraTooltipMap.find(word);
    if (extraIt != extraTooltipMap.end())
    {
        return extraIt.value();
    }
    return QString();
}

// ========== ScriptEditorWindow 实现 ==========

ScriptEditorWindow::ScriptEditorWindow(
    QWidget* parent,
    EditingMode editingMode)
    : QWidget(parent)
    , ui(new Ui::ScriptEditorWindow)
    , currentEditingMode(editingMode)
{
    ui->setupUi(this);

    editor = new ScriptEditor(this);
    setupEditor();
    if (isRunnableScript())
    {
        highlighter =
            new ScriptSyntaxHighlighter(editor->document());
    }

    findReplaceBar = new FindReplaceBar(this);
    findReplaceBar->setEditor(editor);

    tooltipTimer = new QTimer(this);
    tooltipTimer->setSingleShot(true);
    tooltipTimer->setInterval(500);

    outlineRefreshTimer = new QTimer(this);
    outlineRefreshTimer->setSingleShot(true);
    outlineRefreshTimer->setInterval(150);

    if (ui->editorContainerLayout)
    {
        ui->editorContainerLayout->addWidget(findReplaceBar);
        ui->editorContainerLayout->addWidget(editor);
    }

    if (isRunnableScript())
        setupCompleter();
    setupConnections();
    applyEditingModeUi();
    ui->scriptSplitter->setStretchFactor(0, 1);
    ui->scriptSplitter->setStretchFactor(1, 0);
    ui->scriptSplitter->setSizes({700, 280});
    ui->scriptOutlineTree->header()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    ui->scriptOutlineTree->header()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    if (isRunnableScript())
        refreshScriptOutline();
    updateWindowTitle();

    // 主题切换时重算语法高亮配色与当前行底色，保证深色/浅色主题下均清晰可读。
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](ThemeManager::Theme) {
        applyThemeColors();
    });
}

ScriptEditorWindow::~ScriptEditorWindow()
{
    delete ui;
}

ScriptEditorWindow::EditingMode
ScriptEditorWindow::editingMode() const
{
    return currentEditingMode;
}

bool ScriptEditorWindow::isRunnableScript() const
{
    return currentEditingMode ==
        EditingMode::Script;
}

ProjectDocumentType
ScriptEditorWindow::projectDocumentType() const
{
    return isRunnableScript()
        ? ProjectDocumentType::Script
        : ProjectDocumentType::Text;
}

void ScriptEditorWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
        retranslateDynamicUi();
    }
    QWidget::changeEvent(event);
}

void ScriptEditorWindow::retranslateDynamicUi()
{
    if (isRunnableScript())
    {
        ApiCompleter* previousCompleter = completer;
        completer = nullptr;
        setupCompleter();
        delete previousCompleter;
    }
    QToolTip::hideText();
    applyEditingModeUi();
    if (isRunnableScript())
        refreshScriptOutline();
    updateWindowTitle();
    onCursorPositionChanged();
}

void ScriptEditorWindow::setupEditor()
{
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
}

void ScriptEditorWindow::setupCompleter()
{
    QVector<ApiInfo> apiList = buildScriptApiList();
    completer = new ApiCompleter(apiList, this);
    editor->setCompleter(completer);

    QSet<QString> apiNames;
    for (const ApiInfo& api : apiList)
    {
        apiNames.insert(api.signature.section('(', 0, 0).trimmed().toLower());
    }
    highlighter->setApiNames(apiNames);

    // Lua 关键字补全与剧情变量补全：与 Script API 共存，按前缀同时提示。
    reloadVariableHints();
}

void ScriptEditorWindow::reloadVariableHints()
{
    if (!isRunnableScript())
        return;

    // 变量提示文件缺失或格式错误时 reload 内部只记录日志，不抛出，编辑器仍可正常使用。
    variableHints.reload();

    if (!completer)
        return;

    completer->clearExtraCompletions();

    // Lua 关键字：补全列表与逐条悬停提示。
    for (const QString& keyword : LUA_KEYWORDS)
    {
        completer->setExtraTooltip(keyword, tr("Lua 关键字: %1").arg(keyword));
    }
    completer->addExtraCompletions(LUA_KEYWORDS);

    // 剧情变量：补全列表 + 详情 tooltip。
    QVector<ScriptVariableInfo> variables = variableHints.variables();
    QStringList variableNames;
    variableNames.reserve(variables.size());
    for (const ScriptVariableInfo& variable : variables)
    {
        variableNames.append(variable.name);
        QString tooltip = variable.buildTooltip();
        if (!tooltip.isEmpty())
        {
            completer->setExtraTooltip(variable.name, tooltip);
        }
    }
    completer->addExtraCompletions(variableNames);
}

void ScriptEditorWindow::applyThemeColors()
{
    if (highlighter)
        highlighter->refreshForTheme();
    if (editor)
        editor->highlightCurrentLine();
}

void ScriptEditorWindow::setupConnections()
{
    connect(ui->openButton, &QPushButton::clicked, this, &ScriptEditorWindow::onOpenFile);
    connect(ui->saveButton, &QPushButton::clicked, this, &ScriptEditorWindow::onSaveFile);
    connect(ui->saveAsButton, &QPushButton::clicked, this, &ScriptEditorWindow::onSaveAs);
    connect(ui->findReplaceButton, &QPushButton::clicked, this, &ScriptEditorWindow::onFindReplace);
    connect(ui->insertApiCallButton, &QPushButton::clicked,
        this, &ScriptEditorWindow::onInsertApiCall);

    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, &ScriptEditorWindow::onCursorPositionChanged);
    connect(editor->document(), &QTextDocument::modificationChanged, this, &ScriptEditorWindow::onDocumentModified);
    connect(
        editor->document(),
        &QTextDocument::contentsChanged,
        this,
        &ScriptEditorWindow::
            invalidateSerializedDocumentCache);
    if (isRunnableScript())
    {
        connect(editor->document(), &QTextDocument::contentsChanged,
            outlineRefreshTimer, qOverload<>(&QTimer::start));
        connect(
            editor->document(),
            &QTextDocument::contentsChanged,
            this,
            &ScriptEditorWindow::storyGraphSourceChanged);
        connect(outlineRefreshTimer, &QTimer::timeout,
            this, &ScriptEditorWindow::refreshScriptOutline);
        connect(ui->scriptOutlineTree, &QTreeWidget::itemActivated,
            this, [this](QTreeWidgetItem* item, int)
            {
                onOutlineItemActivated(item);
            });
        connect(ui->scriptOutlineTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int)
            {
                onOutlineItemActivated(item);
            });
    }

    connect(findReplaceBar, &FindReplaceBar::closeRequested, this, [this]() {
        findReplaceBar->hide();
        editor->setFocus();
    });

    if (isRunnableScript())
    {
        connect(editor, &ScriptEditor::mouseHovered,
            this, [this](const QPoint& position)
            {
                lastHoverPosition = position;
                tooltipTimer->start();
            });

        connect(tooltipTimer, &QTimer::timeout,
            this, [this]()
            {
                showApiTooltip(lastHoverPosition);
            });
    }

    auto* ctrlF = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(ctrlF, &QShortcut::activated, this, &ScriptEditorWindow::onFindReplace);

    auto* ctrlH = new QShortcut(QKeySequence("Ctrl+H"), this);
    connect(ctrlH, &QShortcut::activated, this, &ScriptEditorWindow::onFindReplace);

    auto* ctrlS = new QShortcut(QKeySequence("Ctrl+S"), this);
    connect(ctrlS, &QShortcut::activated, this, &ScriptEditorWindow::onSaveFile);

    auto* ctrlG = new QShortcut(QKeySequence("Ctrl+G"), this);
    connect(ctrlG, &QShortcut::activated, this, [this]() {
        bool ok;
        int line = QInputDialog::getInt(this, tr("跳转到行"),
            tr("行号:"), 1, 1, editor->document()->blockCount(), 1, &ok);
        if (ok)
        {
            QTextCursor cursor(editor->document()->findBlockByLineNumber(line - 1));
            editor->setTextCursor(cursor);
            editor->centerCursor();
        }
    });
}

void ScriptEditorWindow::applyEditingModeUi()
{
    const bool scriptMode = isRunnableScript();
    setProperty(
        "jxqyEditingMode",
        scriptMode
            ? QStringLiteral("script")
            : QStringLiteral("genericText"));
    ui->insertApiCallButton->setVisible(scriptMode);
    ui->outlinePanel->setVisible(scriptMode);
    if (!scriptMode)
    {
        editor->setCompleter(nullptr);
        ui->scriptSplitter->setSizes({1, 0});
    }
}

void ScriptEditorWindow::goToLine(int lineNumber)
{
    if (lineNumber <= 0 || !editor)
        return;
    const int targetLine = std::min(lineNumber, editor->document()->blockCount());
    QTextCursor cursor(editor->document()->findBlockByLineNumber(targetLine - 1));
    editor->setTextCursor(cursor);
    editor->centerCursor();
    editor->setFocus();
}

bool ScriptEditorWindow::goToSourceLocation(
    int lineNumber,
    int columnNumber)
{
    if (!editor ||
        lineNumber <= 0 ||
        lineNumber > editor->document()->blockCount() ||
        columnNumber < 0)
    {
        return false;
    }

    const QTextBlock block =
        editor->document()->findBlockByLineNumber(
            lineNumber - 1);
    if (!block.isValid())
        return false;
    const int effectiveColumn =
        columnNumber == 0 ? 1 : columnNumber;
    const QString lineText = block.text();
    int utf16Offset = 0;
    int currentColumn = 1;
    while (currentColumn < effectiveColumn)
    {
        if (utf16Offset >= lineText.size())
            return false;
        const QChar current = lineText.at(utf16Offset);
        if (current.isHighSurrogate() &&
            utf16Offset + 1 < lineText.size() &&
            lineText.at(utf16Offset + 1).
                isLowSurrogate())
        {
            utf16Offset += 2;
        }
        else
        {
            ++utf16Offset;
        }
        ++currentColumn;
    }

    QTextCursor cursor(block);
    cursor.setPosition(
        block.position() + utf16Offset);
    editor->setTextCursor(cursor);
    editor->centerCursor();
    editor->setFocus();
    return true;
}

bool ScriptEditorWindow::insertScriptCall(const QString& call)
{
    if (!isRunnableScript() ||
        !editor ||
        call.isEmpty())
        return false;

    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.insertText(call);
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
    editor->setFocus();
    return true;
}

ClosePlan ScriptEditorWindow::prepareCloseTransaction() const
{
    ClosePlan plan;
    if (!editor->document()->isModified())
    {
        plan.decisions.append(CloseDecision::Ready);
        return plan;
    }

    const int result = QMessageBox::question(
        const_cast<ScriptEditorWindow*>(this),
        tr("保存更改"),
        isRunnableScript()
            ? tr("脚本已修改，是否保存？")
            : tr("文本已修改，是否保存？"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (result == QMessageBox::Cancel)
        plan.decisions.append(CloseDecision::Cancelled);
    else if (result == QMessageBox::Yes)
        plan.decisions.append(CloseDecision::Save);
    else
        plan.decisions.append(CloseDecision::Discard);
    return plan;
}

bool ScriptEditorWindow::resolveCloseTransaction(const ClosePlan& plan)
{
    if (plan.decisions.size() != 1 || plan.isCancelled())
        return false;
    if (plan.decisions.front() == CloseDecision::Save)
    {
        onSaveFile();
        return !editor->document()->isModified();
    }
    return true;
}

void ScriptEditorWindow::commitCloseTransaction(const ClosePlan& plan)
{
    if (plan.decisions.size() != 1 || plan.isCancelled())
        return;
    allowPreparedClose();
}

ScriptEditorWindow::SaveConfirmResult ScriptEditorWindow::confirmSaveIfModified()
{
    if (!editor->document()->isModified())
        return SaveConfirmResult::Discarded;

    int result = QMessageBox::question(this,
        tr("保存更改"),
        isRunnableScript()
            ? tr("脚本已修改，是否保存？")
            : tr("文本已修改，是否保存？"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

    if (result == QMessageBox::Cancel)
        return SaveConfirmResult::Cancelled;

    if (result == QMessageBox::No)
        return SaveConfirmResult::Discarded;

    onSaveFile();
    if (editor->document()->isModified())
        return SaveConfirmResult::Cancelled;

    return SaveConfirmResult::Saved;
}

void ScriptEditorWindow::closeEvent(QCloseEvent* event)
{
    if (consumePreparedClose())
    {
        emit documentClosed();
        event->accept();
        return;
    }

    if (confirmSaveIfModified() == SaveConfirmResult::Cancelled)
    {
        event->ignore();
        return;
    }
    emit documentClosed();
    event->accept();
}

bool ScriptEditorWindow::openFile(const QString& fileName)
{
    if (fileName.isEmpty())
        return false;

    const QString normalizedFileName =
        EditorAssetPath::normalizedAbsolutePath(fileName);
    if (documentPathValidator &&
        !documentPathValidator(normalizedFileName))
    {
        return false;
    }

    if (confirmSaveIfModified() == SaveConfirmResult::Cancelled)
        return false;

    std::string utf8Path = normalizedFileName.toUtf8().toStdString();

    if (!QFileInfo::exists(normalizedFileName))
    {
        QMessageBox::warning(this, tr("错误"),
            tr("文件不存在: %1").arg(normalizedFileName));
        return false;
    }

    std::vector<uint8_t> fileData = Util::readFileToBuffer(utf8Path);
    if (fileData.empty())
    {
        // 区分空文件和读取失败
        QFileInfo info(normalizedFileName);
        if (info.exists() && info.size() > 0)
        {
                QMessageBox::warning(this, tr("错误"),
                tr("无法读取文件: %1").arg(normalizedFileName));
            return false;
        }
    }

    const QByteArray bytes(
        reinterpret_cast<const char*>(fileData.data()),
        static_cast<qsizetype>(fileData.size()));
    return loadContentBytes(
        normalizedFileName, bytes, true);
}

bool ScriptEditorWindow::openVerifiedContent(
    const QString& fileName,
    const QByteArray& verifiedBytes)
{
    if (fileName.isEmpty())
        return false;

    const QString normalizedFileName =
        EditorAssetPath::normalizedAbsolutePath(fileName);
    if (documentPathValidator &&
        !documentPathValidator(normalizedFileName))
    {
        return false;
    }
    return loadContentBytes(
        normalizedFileName,
        verifiedBytes,
        false);
}

bool ScriptEditorWindow::reloadFromDiskIfClean()
{
    if (!editor ||
        editor->document()->isModified() ||
        currentFileName.isEmpty())
    {
        return false;
    }

    QFile file(currentFileName);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    return loadContentBytes(
        currentFileName,
        file.readAll(),
        false);
}

bool ScriptEditorWindow::normalizeDesktopRunSourceBytes(
    const QByteArray& sourceBytes,
    QByteArray& normalizedEditorBytes)
{
    std::string content(
        sourceBytes.constData(),
        static_cast<std::size_t>(
            sourceBytes.size()));
    if (!content.empty() &&
        !ScriptConverter::detectAndConvertEncoding(
            content))
    {
        normalizedEditorBytes.clear();
        return false;
    }

    QTextDocument document;
    document.setPlainText(
        QString::fromUtf8(
            content.data(),
            static_cast<int>(content.size())));
    normalizedEditorBytes =
        document.toPlainText().toUtf8();
    return true;
}

bool ScriptEditorWindow::loadContentBytes(
    const QString& normalizedFileName,
    const QByteArray& bytes,
    bool allowDirtyReplacement)
{
    if (!editor ||
        normalizedFileName.isEmpty() ||
        (!allowDirtyReplacement &&
         editor->document()->isModified()))
    {
        return false;
    }

    std::string content(
        bytes.constData(),
        static_cast<std::size_t>(bytes.size()));
    if (!content.empty() &&
        !ScriptConverter::detectAndConvertEncoding(content))
    {
        QMessageBox::warning(this, tr("错误"),
            isRunnableScript()
                ? tr("无法识别脚本编码: %1")
                      .arg(normalizedFileName)
                : tr("无法识别文本编码: %1")
                      .arg(normalizedFileName));
        return false;
    }

    if (isRunnableScript())
    {
        // 刷新剧情变量提示，使切换项目后能拿到最新的项目级变量定义。
        // 提示文件缺失或损坏不会阻断脚本打开。
        reloadVariableHints();
    }

    editor->setPlainText(QString::fromUtf8(
        content.data(),
        static_cast<int>(content.size())));

    currentFileName = normalizedFileName;
    editor->document()->setModified(false);
    prewarmSerializedDocumentCache();
    updateWindowTitle();
    onCursorPositionChanged();
    emit documentStateChanged(currentFileName, false);
    return true;
}

QString ScriptEditorWindow::currentFilePath() const
{
    return currentFileName;
}

ScriptEditorWindow::EditingViewState
ScriptEditorWindow::editingViewState() const
{
    EditingViewState state;
    if (!editor)
        return state;

    state.cursorPosition = editor->textCursor().position();
    state.verticalScrollValue =
        editor->verticalScrollBar()->value();
    state.horizontalScrollValue =
        editor->horizontalScrollBar()->value();
    return state;
}

std::optional<DialogueReference>
ScriptEditorWindow::dialogueReferenceAtCursor() const
{
    if (!isRunnableScript() || !editor)
        return std::nullopt;
    return DialogueDocument::literalTalkReferenceAt(
        editor->toPlainText(), editor->textCursor().position());
}

void ScriptEditorWindow::restoreEditingViewState(
    const EditingViewState& state)
{
    if (!editor)
        return;

    const int maximumCursorPosition =
        std::max(0, editor->document()->characterCount() - 1);
    QTextCursor cursor = editor->textCursor();
    cursor.clearSelection();
    cursor.setPosition(std::clamp(
        state.cursorPosition, 0, maximumCursorPosition));
    editor->setTextCursor(cursor);
    editor->verticalScrollBar()->setValue(
        std::max(0, state.verticalScrollValue));
    editor->horizontalScrollBar()->setValue(
        std::max(0, state.horizontalScrollValue));
}

DesktopRunDocumentSnapshot
ScriptEditorWindow::desktopRunDocumentSnapshot() const
{
    DesktopRunDocumentSnapshot snapshot;
    snapshot.filePath = currentFileName;
    snapshot.type = projectDocumentType();
    snapshot.dirty = editor && editor->document()->isModified();
    snapshot.serializationSupported = editor != nullptr;
    if (editor)
    {
        snapshot.bytes =
            serializedDocumentForCurrentRevision().
                utf8Bytes;
    }
    else
    {
        snapshot.diagnosticCode =
            QStringLiteral("editor_run.overlay.script_snapshot_unavailable");
    }
    return snapshot;
}

StoryGraphSourceSnapshot
ScriptEditorWindow::storyGraphSourceSnapshot(
    const QString& portableRootKey,
    const QString& strictVirtualPath) const
{
    StoryGraphSourceSnapshot snapshot;
    if (!isRunnableScript())
        return snapshot;

    snapshot.identity.portableRootKey =
        portableRootKey;
    snapshot.identity.virtualPath =
        strictVirtualPath;
    snapshot.identity.canonicalAbsolutePath =
        currentFileName;
    snapshot.identity.fromEditorBuffer = true;
    if (editor)
    {
        const SerializedDocumentCache& serialized =
            serializedDocumentForCurrentRevision();
        snapshot.identity.documentRevision =
            serialized.documentRevision;
        snapshot.utf8Bytes = serialized.utf8Bytes;
        snapshot.identity.contentSha256 =
            serialized.contentSha256;
    }
    else
    {
        snapshot.identity.documentRevision = -1;
    }
    return snapshot;
}

void ScriptEditorWindow::
    invalidateSerializedDocumentCache()
{
    serializedDocumentCache =
        SerializedDocumentCache();
}

const ScriptEditorWindow::SerializedDocumentCache&
ScriptEditorWindow::
    serializedDocumentForCurrentRevision() const
{
    if (!editor)
        return serializedDocumentCache;

    const int revision =
        editor->document()->revision();
    if (serializedDocumentCache.valid &&
        serializedDocumentCache.documentRevision ==
            revision)
    {
        return serializedDocumentCache;
    }

    SerializedDocumentCache refreshed;
    refreshed.documentRevision = revision;
    refreshed.utf8Bytes =
        editor->toPlainText().toUtf8();
    refreshed.contentSha256 =
        QCryptographicHash::hash(
            refreshed.utf8Bytes,
            QCryptographicHash::Sha256);
    refreshed.valid = true;
    serializedDocumentCache =
        std::move(refreshed);
    return serializedDocumentCache;
}

void ScriptEditorWindow::
    prewarmSerializedDocumentCache() const
{
    if (editor)
        serializedDocumentForCurrentRevision();
}

bool ScriptEditorWindow::hasUnsavedChanges() const
{
    return editor->document()->isModified();
}

void ScriptEditorWindow::setDocumentPathValidator(
    std::function<bool(const QString&)> validator)
{
    documentPathValidator = std::move(validator);
}

void ScriptEditorWindow::onOpenFile()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        isRunnableScript()
            ? tr("打开脚本文件")
            : tr("打开文本文件"),
        QString(),
        isRunnableScript()
            ? tr("脚本文件 (*.txt *.lua);;所有文件 (*.*)")
            : tr("文本文件 (*.txt *.ini *.map);;所有文件 (*.*)"),
        nullptr,
        QFileDialog::DontResolveSymlinks);

    if (fileName.isEmpty())
        return;

    openFile(fileName);
}

void ScriptEditorWindow::onSaveFile()
{
    if (currentFileName.isEmpty())
    {
        onSaveAs();
        return;
    }

    if (saveToFile(currentFileName))
    {
        editor->document()->setModified(false);
        updateWindowTitle();
        emit documentStateChanged(currentFileName, false);
    }
}

void ScriptEditorWindow::onSaveAs()
{
    QString fileName = QFileDialog::getSaveFileName(this,
        isRunnableScript()
            ? tr("保存脚本文件")
            : tr("保存文本文件"),
        "",
        isRunnableScript()
            ? tr("脚本文件 (*.txt *.lua);;所有文件 (*.*)")
            : tr("文本文件 (*.txt *.ini *.map);;所有文件 (*.*)"));

    if (fileName.isEmpty())
        return;

    saveAsFile(fileName);
}

bool ScriptEditorWindow::saveAsFile(const QString& fileName)
{
    if (fileName.isEmpty())
        return false;

    const QString normalizedFileName =
        EditorAssetPath::normalizedAbsolutePath(fileName);
    if (documentPathValidator &&
        !documentPathValidator(normalizedFileName))
    {
        return false;
    }

    if (!saveToFile(normalizedFileName))
        return false;

    currentFileName = normalizedFileName;
    editor->document()->setModified(false);
    updateWindowTitle();
    emit documentStateChanged(currentFileName, false);
    return true;
}

bool ScriptEditorWindow::saveToFile(const QString& targetPath)
{
    const QByteArray content = desktopRunDocumentSnapshot().bytes;
    if (!Util::writeFileFromBuffer(
            targetPath.toUtf8().toStdString(),
            content.constData(),
            static_cast<std::size_t>(content.size())))
    {
        QMessageBox::warning(this, tr("保存失败"),
            tr("无法原子写入文件: %1").arg(targetPath));
        return false;
    }
    return true;
}

void ScriptEditorWindow::onFindReplace()
{
    findReplaceBar->show();
    findReplaceBar->focusFindInput();
}

void ScriptEditorWindow::onInsertApiCall()
{
    if (!isRunnableScript())
        return;

    ScriptCallDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    insertScriptCall(dialog.generatedCall());
}

void ScriptEditorWindow::refreshScriptOutline()
{
    if (!isRunnableScript())
        return;

    const QList<LuaFunctionOutlineEntry> entries =
        LuaFunctionOutline::parse(editor ? editor->toPlainText() : QString());

    ui->scriptOutlineTree->setUpdatesEnabled(false);
    ui->scriptOutlineTree->clear();
    for (const LuaFunctionOutlineEntry& entry : entries)
    {
        auto* item = new QTreeWidgetItem(ui->scriptOutlineTree);
        item->setText(0, entry.name);
        item->setText(1, QString::number(entry.lineNumber));
        item->setData(0, Qt::UserRole, entry.lineNumber);
        item->setData(0, Qt::UserRole + 1, entry.isLocal);
        item->setToolTip(0, entry.isLocal
            ? tr("local function %1，第 %2 行").arg(entry.name)
                  .arg(entry.lineNumber)
            : tr("function %1，第 %2 行").arg(entry.name)
                  .arg(entry.lineNumber));
    }
    const bool hasEntries = !entries.isEmpty();
    ui->scriptOutlineTree->setVisible(hasEntries);
    ui->outlineEmptyLabel->setVisible(!hasEntries);
    ui->scriptOutlineTree->setUpdatesEnabled(true);
}

void ScriptEditorWindow::onOutlineItemActivated(QTreeWidgetItem* item)
{
    if (!item)
        return;
    goToLine(item->data(0, Qt::UserRole).toInt());
}

void ScriptEditorWindow::onCursorPositionChanged()
{
    QTextCursor cursor = editor->textCursor();
    int line = cursor.blockNumber() + 1;
    int col = cursor.columnNumber() + 1;
    ui->cursorLabel->setText(tr("行 %1, 列 %2").arg(line).arg(col));
}

void ScriptEditorWindow::onDocumentModified()
{
    updateWindowTitle();
    emit documentStateChanged(
        currentFileName, editor->document()->isModified());
}

void ScriptEditorWindow::updateWindowTitle()
{
    QString fileName = currentFileName.isEmpty()
        ? tr("未命名")
        : QFileInfo(currentFileName).fileName();

    QString modified = editor->document()->isModified() ? " *" : "";
    setWindowTitle(
        isRunnableScript()
            ? tr("脚本编辑器 - %1%2")
                  .arg(fileName)
                  .arg(modified)
            : tr("文本编辑器 - %1%2")
                  .arg(fileName)
                  .arg(modified));
}

void ScriptEditorWindow::showApiTooltip(const QPoint& position)
{
    QTextCursor cursor = editor->cursorForPosition(position);
    cursor.select(QTextCursor::WordUnderCursor);
    QString word = cursor.selectedText();

    if (word.isEmpty() || !completer)
        return;

    // 依次匹配 Script API、Lua 关键字、剧情变量；均无匹配则不显示提示。
    QString tooltip = completer->getTooltipForWord(word);
    if (tooltip.isEmpty())
        return;

    QPoint globalPos = editor->viewport()->mapToGlobal(position);
    QToolTip::showText(globalPos, tooltip, editor);
}
