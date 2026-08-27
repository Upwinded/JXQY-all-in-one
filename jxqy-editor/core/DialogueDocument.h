#pragma once

#include "INIFileEditor.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <optional>

enum class DialoguePortraitMode
{
    KeepPrevious,
    NoPortrait,
    Reference
};

struct DialogueLine
{
    int number = 0;
    QString speaker;
    QString text;
    QString portraitReference;
    DialoguePortraitMode portraitMode =
        DialoguePortraitMode::KeepPrevious;
};

struct DialogueResolvedPortrait
{
    QString reference;
    int sourceRow = -1;
    bool explicitlyHidden = false;
};

struct DialogueReference
{
    QString section;
    int line = 0;
    int column = 0;
    int startOffset = -1;
    int endOffset = -1;
};

class DialogueDocument
{
public:
    bool load(const QByteArray& bytes, QString* errorMessage = nullptr);
    bool openFile(const QString& filePath, QString* errorMessage = nullptr);
    bool saveFile(const QString& filePath, QString* errorMessage = nullptr) const;

    QByteArray serializedBytes() const;
    QStringList sectionNames() const;
    bool hasSection(const QString& section) const;
    int lineCount(const QString& section) const;
    DialogueLine line(const QString& section, int row) const;
    DialogueResolvedPortrait resolvedPortrait(
        const QString& section, int row) const;
    bool setLine(const QString& section, int row,
                 const QString& speaker, const QString& text);
    bool setPortrait(const QString& section, int row,
                     DialoguePortraitMode mode,
                     const QString& reference = QString());
    int hiddenFieldCount() const;

    static QVector<DialogueReference> findLiteralTalkReferences(
        const QString& scriptText);
    static std::optional<DialogueReference> literalTalkReferenceAt(
        const QString& scriptText, int cursorPosition);
    static QString talkFileForScript(const QString& scriptPath);

private:
    static QString decodeDialogueText(const QString& value);
    static QString encodeDialogueText(const QString& value);
    static QString unescapeLuaString(const QString& value);

    INIFileEditor ini;
    QStringList orderedSections;
    bool loaded = false;
};
