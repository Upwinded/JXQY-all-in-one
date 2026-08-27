#pragma once

#include <QWidget>
#include "CloseTransactionParticipant.h"
#include "../core/DesktopRunDocumentSnapshot.h"
#include "../core/StoryGraphAnalyzer.h"
#include <QByteArray>
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QCompleter>
#include <QLineEdit>
#include <QCheckBox>
#include <QRegularExpression>
#include <QToolTip>
#include <QTimer>
#include <QMap>
#include <QCloseEvent>

#include <functional>

#include "ScriptApiList.h"
#include "ScriptVariableHints.h"
#include "../core/DialogueDocument.h"

#include <optional>

class LineNumberArea;
class ScriptSyntaxHighlighter;
class QLabel;
class QPushButton;
class QEvent;
class QTreeWidgetItem;

class ScriptEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit ScriptEditor(QWidget* parent = nullptr);

    void lineNumberAreaPaintEvent(QPaintEvent* event);
    int lineNumberAreaWidth();
    void setCompleter(QCompleter* completer);
    QCompleter* getCompleter() const;

    /// 重算当前行高亮底色（主题切换后由窗口调用）。
    void highlightCurrentLine();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

signals:
    void mouseHovered(const QPoint& position);

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect& rect, int dy);
    void insertCompletion(const QString& completion);

private:
    QString textUnderCursor() const;

    QWidget* lineNumberArea;
    QCompleter* completer = nullptr;
};

class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(ScriptEditor* editor)
        : QWidget(editor), editor(editor) {}

    QSize sizeHint() const override { return QSize(editor->lineNumberAreaWidth(), 0); }

protected:
    void paintEvent(QPaintEvent* event) override { editor->lineNumberAreaPaintEvent(event); }

private:
    ScriptEditor* editor;
};

class ScriptSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit ScriptSyntaxHighlighter(QTextDocument* parent = nullptr);
    void setApiNames(const QSet<QString>& names);

    /// 主题切换后重算配色并重新高亮整个文档。
    void refreshForTheme();

protected:
    void highlightBlock(const QString& text) override;

private:
    void buildRules();

    struct HighlightRule
    {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightRule> rules;

    QTextCharFormat commentFormat;
    QTextCharFormat multiLineCommentFormat;
    QTextCharFormat stringFormat;
    QTextCharFormat numberFormat;
    QTextCharFormat keywordFormat;
    QTextCharFormat builtinFormat;
    QTextCharFormat apiFormat;

    QRegularExpression commentStartExpression;
    QRegularExpression commentEndExpression;

    QSet<QString> gameApiSet;

    static const int STATE_NORMAL = 0;
    static const int STATE_IN_COMMENT = 1;
};

class FindReplaceBar : public QWidget
{
    Q_OBJECT

public:
    explicit FindReplaceBar(QWidget* parent = nullptr);

    void setEditor(QPlainTextEdit* editor);
    void focusFindInput();

signals:
    void closeRequested();

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onFindNext();
    void onFindPrevious();
    void onReplace();
    void onReplaceAll();

private:
    QPlainTextEdit* textEdit = nullptr;
    QLineEdit* findInput;
    QLineEdit* replaceInput;
    QCheckBox* caseSensitiveCheck;
    QCheckBox* wholeWordCheck;
    QCheckBox* regexCheck;
    QLabel* findLabel;
    QLabel* replaceLabel;
    QPushButton* findPreviousButton;
    QPushButton* findNextButton;
    QPushButton* closeButton;
    QPushButton* replaceButton;
    QPushButton* replaceAllButton;
};

class ApiCompleter : public QCompleter
{
    Q_OBJECT

public:
    explicit ApiCompleter(const QVector<ApiInfo>& apis, QObject* parent = nullptr);

    QString getTooltipForCompletion(const QString& completion) const;
    const ApiInfo* getApiInfo(const QString& functionName) const;

    /// 追加非 API 的补全项（Lua 关键字、剧情变量），使其与 Script API 共存。
    /// @param words    补全词条
    /// @param tooltip  统一附带的悬停提示（可空）。逐条附加时使用单独的重载。
    void addExtraCompletions(const QStringList& words, const QString& tooltip = QString());

    /// 清除全部非 API 补全项（关键字、剧情变量），仅保留 Script API。
    /// 用于变量提示重新加载前重置，避免残留失效条目。
    void clearExtraCompletions();

    /// 为单个词条设置独立的悬停提示（覆盖 addExtraCompletions 的统一提示）。
    void setExtraTooltip(const QString& word, const QString& tooltip);

    /// 按输入单词返回悬停提示，依次查 API、关键字、剧情变量；找不到返回空串。
    QString getTooltipForWord(const QString& word) const;

private:
    QMap<QString, ApiInfo> apiMap;
    QMap<QString, QString> extraTooltipMap;
};

namespace Ui { class ScriptEditorWindow; }

class ScriptEditorWindow : public QWidget, public CloseTransactionParticipant
{
    Q_OBJECT

public:
    struct EditingViewState
    {
        int cursorPosition = 0;
        int verticalScrollValue = 0;
        int horizontalScrollValue = 0;
    };

    enum class EditingMode
    {
        Script,
        GenericText
    };

    explicit ScriptEditorWindow(
        QWidget* parent = nullptr,
        EditingMode editingMode = EditingMode::Script);
    ~ScriptEditorWindow();

    EditingMode editingMode() const;
    bool isRunnableScript() const;
    ProjectDocumentType projectDocumentType() const;
    bool openFile(const QString& fileName);
    bool openVerifiedContent(
        const QString& fileName,
        const QByteArray& verifiedBytes);
    bool reloadFromDiskIfClean();
    static bool normalizeDesktopRunSourceBytes(
        const QByteArray& sourceBytes,
        QByteArray& normalizedEditorBytes);
    bool saveAsFile(const QString& fileName);
    DesktopRunDocumentSnapshot desktopRunDocumentSnapshot() const;
    StoryGraphSourceSnapshot storyGraphSourceSnapshot(
        const QString& portableRootKey,
        const QString& strictVirtualPath) const;
    QString currentFilePath() const;
    bool hasUnsavedChanges() const;
    void goToLine(int lineNumber);
    bool goToSourceLocation(
        int lineNumber,
        int columnNumber);
    bool insertScriptCall(const QString& call);
    EditingViewState editingViewState() const;
    void restoreEditingViewState(const EditingViewState& state);
    std::optional<DialogueReference> dialogueReferenceAtCursor() const;
    void setDocumentPathValidator(
        std::function<bool(const QString&)> validator);

    ClosePlan prepareCloseTransaction() const override;
    bool resolveCloseTransaction(const ClosePlan& plan) override;
    void commitCloseTransaction(const ClosePlan& plan) override;

    enum class SaveConfirmResult { Saved, Discarded, Cancelled };
    SaveConfirmResult confirmSaveIfModified();

signals:
    void documentStateChanged(const QString& filePath, bool dirty);
    void storyGraphSourceChanged();
    void documentClosed();

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onOpenFile();
    void onSaveFile();
    void onSaveAs();
    void onFindReplace();
    void onInsertApiCall();
    void refreshScriptOutline();
    void onOutlineItemActivated(QTreeWidgetItem* item);
    void onCursorPositionChanged();
    void onDocumentModified();
    void showApiTooltip(const QPoint& position);

private:
    struct SerializedDocumentCache
    {
        int documentRevision = -1;
        QByteArray utf8Bytes;
        QByteArray contentSha256;
        bool valid = false;
    };

    void setupEditor();
    void setupCompleter();
    void setupConnections();
    void applyEditingModeUi();
    void updateWindowTitle();
    void invalidateSerializedDocumentCache();
    const SerializedDocumentCache&
        serializedDocumentForCurrentRevision() const;
    void prewarmSerializedDocumentCache() const;
    bool saveToFile(const QString& targetPath);
    bool loadContentBytes(
        const QString& normalizedFileName,
        const QByteArray& bytes,
        bool allowDirtyReplacement);

    /// 刷新剧情变量提示：重新加载文件并合并进补全列表与悬停提示。
    void reloadVariableHints();

    /// 主题切换时重算高亮配色与当前行底色。
    void applyThemeColors();
    void retranslateDynamicUi();

    Ui::ScriptEditorWindow* ui;
    ScriptEditor* editor;
    ScriptSyntaxHighlighter* highlighter = nullptr;
    ApiCompleter* completer = nullptr;
    FindReplaceBar* findReplaceBar;
    QTimer* outlineRefreshTimer;

    ScriptVariableHints variableHints;

    const EditingMode currentEditingMode;
    QString currentFileName;
    std::function<bool(const QString&)> documentPathValidator;
    QTimer* tooltipTimer;
    QPoint lastHoverPosition;
    mutable SerializedDocumentCache
        serializedDocumentCache;
};
