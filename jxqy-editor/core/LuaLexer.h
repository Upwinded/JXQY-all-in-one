#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>

enum class LuaTokenKind
{
    Identifier,
    Keyword,
    Number,
    String,
    LongString,
    Symbol,
    Comment,
    EndOfFile
};

struct LuaSourcePosition
{
    int line = 1;
    int column = 1;
    qsizetype utf16Offset = 0;
    qsizetype utf8ByteOffset = 0;
};

struct LuaSourceRange
{
    QString sourcePath;
    LuaSourcePosition start;
    LuaSourcePosition end;
};

struct LuaToken
{
    LuaTokenKind kind = LuaTokenKind::EndOfFile;
    QString text;
    QString decodedText;
    LuaSourceRange range;
    bool complete = true;
};

enum class LuaLexWarningCode
{
    UnterminatedString,
    RawNewlineInString,
    InvalidEscape,
    UnterminatedLongString,
    UnterminatedLongComment,
    InvalidUtf8,
    InvalidCharacter
};

struct LuaLexWarning
{
    LuaLexWarningCode code =
        LuaLexWarningCode::InvalidCharacter;
    QString diagnosticCode;
    QString message;
    LuaSourceRange range;
};

struct LuaLexResult
{
    QList<LuaToken> tokens;
    QList<LuaLexWarning> warnings;
    bool cancelled = false;
};

class LuaLexer
{
public:
    using CancelCallback = std::function<bool()>;

    static LuaLexResult lex(
        const QString& sourceText,
        const QString& sourcePath = QString(),
        const CancelCallback& cancelCallback =
            CancelCallback());

    static LuaLexResult lexUtf8(
        const QByteArray& sourceBytes,
        const QString& sourcePath = QString(),
        const CancelCallback& cancelCallback =
            CancelCallback());

    static bool isKeyword(const QString& text);
    static bool isSignificant(const LuaToken& token);
};

QString luaLexWarningDiagnosticCode(
    LuaLexWarningCode code);
