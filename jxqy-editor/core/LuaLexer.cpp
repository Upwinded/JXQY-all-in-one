#include "LuaLexer.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <utility>

namespace
{
constexpr qsizetype CancellationCheckInterval = 4096;

bool isAsciiLetter(const QChar character)
{
    const ushort value = character.unicode();
    return (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') ||
        value == '_';
}

bool isAsciiDigit(const QChar character)
{
    const ushort value = character.unicode();
    return value >= '0' && value <= '9';
}

bool isAsciiHexDigit(const QChar character)
{
    const ushort value = character.unicode();
    return isAsciiDigit(character) ||
        (value >= 'a' && value <= 'f') ||
        (value >= 'A' && value <= 'F');
}

int hexDigitValue(const QChar character)
{
    const ushort value = character.unicode();
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

bool isAsciiLetterOrDigit(const QChar character)
{
    return isAsciiLetter(character) ||
        isAsciiDigit(character);
}

bool isLineBreak(const QChar character)
{
    return character == '\n' || character == '\r';
}

int longBracketEqualsCount(
    const QString& text,
    qsizetype position)
{
    if (position >= text.size() ||
        text.at(position) != '[')
    {
        return -1;
    }

    qsizetype cursor = position + 1;
    while (cursor < text.size() &&
           text.at(cursor) == '=')
    {
        ++cursor;
    }
    if (cursor >= text.size() ||
        text.at(cursor) != '[')
    {
        return -1;
    }
    return static_cast<int>(
        cursor - position - 1);
}

QString warningMessage(LuaLexWarningCode code)
{
    switch (code)
    {
    case LuaLexWarningCode::UnterminatedString:
        return QCoreApplication::translate(
            "LuaLexer",
            "Lua 普通字符串未闭合");
    case LuaLexWarningCode::RawNewlineInString:
        return QCoreApplication::translate(
            "LuaLexer",
            "Lua 普通字符串包含未转义换行");
    case LuaLexWarningCode::InvalidEscape:
        return QCoreApplication::translate(
            "LuaLexer",
            "Lua 字符串包含无效转义");
    case LuaLexWarningCode::UnterminatedLongString:
        return QCoreApplication::translate(
            "LuaLexer",
            "Lua 长字符串未闭合");
    case LuaLexWarningCode::UnterminatedLongComment:
        return QCoreApplication::translate(
            "LuaLexer",
            "Lua 长注释未闭合");
    case LuaLexWarningCode::InvalidUtf8:
        return QCoreApplication::translate(
            "LuaLexer",
            "Lua 源码不是有效的 UTF-8 文本");
    case LuaLexWarningCode::InvalidCharacter:
        return QCoreApplication::translate(
            "LuaLexer",
            "Lua 源码包含无法识别的字符");
    }
    return QString();
}

class LexerState
{
public:
    LexerState(
        const QString& text,
        const QString& sourcePath,
        qsizetype initialUtf8ByteOffset,
        const LuaLexer::CancelCallback&
            cancelCallback)
        : source(text)
        , path(sourcePath)
        , utf8ByteOffset(initialUtf8ByteOffset)
        , cancellationCallback(cancelCallback)
    {
    }

    bool atEnd() const
    {
        return cancelled ||
            offset >= source.size();
    }

    QChar current() const
    {
        return atEnd()
            ? QChar()
            : source.at(offset);
    }

    QChar peek(qsizetype distance = 1) const
    {
        const qsizetype target = offset + distance;
        return target >= 0 && target < source.size()
            ? source.at(target)
            : QChar();
    }

    LuaSourcePosition position() const
    {
        LuaSourcePosition result;
        result.line = line;
        result.column = column;
        result.utf16Offset = offset;
        result.utf8ByteOffset = utf8ByteOffset;
        return result;
    }

    void advance()
    {
        if (atEnd())
            return;
        if (offset >= nextCancellationCheck)
        {
            nextCancellationCheck =
                offset + CancellationCheckInterval;
            if (cancellationCallback &&
                cancellationCallback())
            {
                cancelled = true;
                return;
            }
        }

        const QChar character = source.at(offset);
        if (character == '\r')
        {
            ++offset;
            ++utf8ByteOffset;
            if (offset < source.size() &&
                source.at(offset) == '\n')
            {
                ++offset;
                ++utf8ByteOffset;
            }
            ++line;
            column = 1;
            return;
        }
        if (character == '\n')
        {
            ++offset;
            ++utf8ByteOffset;
            ++line;
            column = 1;
            return;
        }

        const ushort value = character.unicode();
        if (character.isHighSurrogate() &&
            offset + 1 < source.size() &&
            source.at(offset + 1).isLowSurrogate())
        {
            offset += 2;
            utf8ByteOffset += 4;
        }
        else
        {
            ++offset;
            if (value <= 0x7f)
                ++utf8ByteOffset;
            else if (value <= 0x7ff)
                utf8ByteOffset += 2;
            else
                utf8ByteOffset += 3;
        }
        ++column;
    }

    void advanceCount(qsizetype count)
    {
        const qsizetype limit =
            std::min(source.size(), offset + count);
        while (!atEnd() && offset < limit)
            advance();
    }

    LuaSourceRange range(
        const LuaSourcePosition& start) const
    {
        LuaSourceRange result;
        result.sourcePath = path;
        result.start = start;
        result.end = position();
        return result;
    }

    QString textFrom(
        const LuaSourcePosition& start) const
    {
        return source.mid(
            start.utf16Offset,
            offset - start.utf16Offset);
    }

    const QString& source;
    QString path;
    qsizetype offset = 0;
    qsizetype utf8ByteOffset = 0;
    int line = 1;
    int column = 1;
    LuaLexer::CancelCallback cancellationCallback;
    qsizetype nextCancellationCheck = 0;
    bool cancelled = false;
};

void appendWarning(
    LuaLexResult& result,
    LuaLexWarningCode code,
    const LexerState& state,
    const LuaSourcePosition& start)
{
    if (state.cancelled)
        return;
    LuaLexWarning warning;
    warning.code = code;
    warning.diagnosticCode =
        luaLexWarningDiagnosticCode(code);
    warning.message = warningMessage(code);
    warning.range = state.range(start);
    result.warnings.append(std::move(warning));
}

void appendToken(
    LuaLexResult& result,
    LuaTokenKind kind,
    const LexerState& state,
    const LuaSourcePosition& start,
    QString decodedText = QString(),
    bool complete = true)
{
    if (state.cancelled)
        return;
    LuaToken token;
    token.kind = kind;
    token.text = state.textFrom(start);
    token.decodedText = std::move(decodedText);
    token.range = state.range(start);
    token.complete = complete;
    result.tokens.append(std::move(token));
}

bool beginsWith(
    const LexerState& state,
    const QString& text)
{
    if (state.offset + text.size() >
        state.source.size())
    {
        return false;
    }
    return state.source.mid(
               state.offset,
               text.size()) == text;
}

QString decodeLongStringContent(
    const QString& source,
    qsizetype contentStart,
    qsizetype contentLength)
{
    QString decoded =
        source.mid(contentStart, contentLength);
    if (decoded.startsWith(QStringLiteral("\r\n")))
        decoded.remove(0, 2);
    else if (!decoded.isEmpty() &&
             isLineBreak(decoded.front()))
        decoded.remove(0, 1);
    return decoded;
}

void lexLongBracket(
    LuaLexResult& result,
    LexerState& state,
    LuaTokenKind kind,
    const LuaSourcePosition& tokenStart,
    int equalsCount,
    LuaLexWarningCode unterminatedCode)
{
    const qsizetype openingLength =
        equalsCount + 2;
    state.advanceCount(openingLength);
    const qsizetype contentStart = state.offset;
    const QString closing =
        QStringLiteral("]") +
        QString(equalsCount, '=') +
        QStringLiteral("]");
    while (!state.atEnd() &&
           !beginsWith(state, closing))
    {
        state.advance();
    }
    if (state.cancelled)
        return;
    const bool complete =
        beginsWith(state, closing);
    const qsizetype contentEnd = state.offset;
    const QString decoded =
        kind == LuaTokenKind::LongString
            ? decodeLongStringContent(
                  state.source,
                  contentStart,
                  contentEnd - contentStart)
            : QString();
    if (complete)
        state.advanceCount(closing.size());
    appendToken(
        result,
        kind,
        state,
        tokenStart,
        decoded,
        complete);
    if (!complete)
    {
        appendWarning(
            result,
            unterminatedCode,
            state,
            tokenStart);
    }
}

void appendCodePoint(
    QString& output,
    uint codePoint)
{
    if (codePoint <= 0xffff)
    {
        output.append(
            QChar(static_cast<ushort>(codePoint)));
        return;
    }
    if (codePoint <= 0x10ffff)
    {
        output.append(
            QChar::highSurrogate(codePoint));
        output.append(
            QChar::lowSurrogate(codePoint));
    }
}

void lexQuotedString(
    LuaLexResult& result,
    LexerState& state)
{
    const LuaSourcePosition tokenStart =
        state.position();
    const QChar quote = state.current();
    state.advance();
    QString decoded;
    bool complete = false;

    while (!state.atEnd())
    {
        const QChar character = state.current();
        if (character == quote)
        {
            state.advance();
            complete = true;
            break;
        }
        if (isLineBreak(character))
        {
            appendWarning(
                result,
                LuaLexWarningCode::
                    RawNewlineInString,
                state,
                tokenStart);
            break;
        }
        if (character != '\\')
        {
            const qsizetype before = state.offset;
            state.advance();
            decoded.append(
                state.source.mid(
                    before,
                    state.offset - before));
            continue;
        }

        state.advance();
        if (state.atEnd())
            break;
        const QChar escaped = state.current();
        if (isLineBreak(escaped))
        {
            state.advance();
            decoded.append(QLatin1Char('\n'));
            continue;
        }
        state.advance();
        if (escaped == 'a')
            decoded.append(QChar(0x07));
        else if (escaped == 'b')
            decoded.append(QChar(0x08));
        else if (escaped == 'f')
            decoded.append(QChar(0x0c));
        else if (escaped == 'n')
            decoded.append(QLatin1Char('\n'));
        else if (escaped == 'r')
            decoded.append(QLatin1Char('\r'));
        else if (escaped == 't')
            decoded.append(QLatin1Char('\t'));
        else if (escaped == 'v')
            decoded.append(QChar(0x0b));
        else if (escaped == '\\' ||
                 escaped == '\'' ||
                 escaped == '"')
        {
            decoded.append(escaped);
        }
        else if (escaped == 'z')
        {
            while (!state.atEnd() &&
                   state.current().isSpace())
            {
                state.advance();
            }
        }
        else if (escaped == 'x')
        {
            if (state.offset + 2 <=
                    state.source.size() &&
                isAsciiHexDigit(state.current()) &&
                isAsciiHexDigit(state.peek()))
            {
                const int value =
                    hexDigitValue(state.current()) *
                        16 +
                    hexDigitValue(state.peek());
                state.advanceCount(2);
                decoded.append(
                    QChar(static_cast<ushort>(
                        value)));
            }
            else
            {
                appendWarning(
                    result,
                    LuaLexWarningCode::InvalidEscape,
                    state,
                    tokenStart);
                decoded.append(escaped);
            }
        }
        else if (escaped == 'u' &&
                 state.current() == '{')
        {
            state.advance();
            uint value = 0;
            int digitCount = 0;
            while (!state.atEnd() &&
                   isAsciiHexDigit(state.current()) &&
                   digitCount < 8)
            {
                value = value * 16 +
                    static_cast<uint>(
                        hexDigitValue(
                            state.current()));
                ++digitCount;
                state.advance();
            }
            if (digitCount > 0 &&
                state.current() == '}' &&
                value <= 0x10ffff &&
                !(value >= 0xd800 &&
                  value <= 0xdfff))
            {
                state.advance();
                appendCodePoint(decoded, value);
            }
            else
            {
                appendWarning(
                    result,
                    LuaLexWarningCode::InvalidEscape,
                    state,
                    tokenStart);
            }
        }
        else if (isAsciiDigit(escaped))
        {
            int value = escaped.unicode() - '0';
            int digitCount = 1;
            while (digitCount < 3 &&
                   !state.atEnd() &&
                   isAsciiDigit(state.current()))
            {
                value = value * 10 +
                    state.current().unicode() -
                    '0';
                ++digitCount;
                state.advance();
            }
            if (value <= 255)
            {
                decoded.append(
                    QChar(static_cast<ushort>(
                        value)));
            }
            else
            {
                appendWarning(
                    result,
                    LuaLexWarningCode::InvalidEscape,
                    state,
                    tokenStart);
            }
        }
        else
        {
            appendWarning(
                result,
                LuaLexWarningCode::InvalidEscape,
                state,
                tokenStart);
            decoded.append(escaped);
        }
    }

    if (state.cancelled)
        return;
    appendToken(
        result,
        LuaTokenKind::String,
        state,
        tokenStart,
        decoded,
        complete);
    if (!complete)
    {
        appendWarning(
            result,
            LuaLexWarningCode::UnterminatedString,
            state,
            tokenStart);
    }
}

void lexNumber(
    LuaLexResult& result,
    LexerState& state)
{
    const LuaSourcePosition start =
        state.position();
    const bool hexadecimal =
        state.current() == '0' &&
        (state.peek() == 'x' ||
         state.peek() == 'X');
    if (hexadecimal)
    {
        state.advanceCount(2);
        while (!state.atEnd() &&
               isAsciiHexDigit(state.current()))
        {
            state.advance();
        }
        if (state.current() == '.' &&
            state.peek() != '.')
        {
            state.advance();
            while (!state.atEnd() &&
                   isAsciiHexDigit(
                       state.current()))
            {
                state.advance();
            }
        }
        if (state.current() == 'p' ||
            state.current() == 'P')
        {
            state.advance();
            if (state.current() == '+' ||
                state.current() == '-')
            {
                state.advance();
            }
            while (!state.atEnd() &&
                   isAsciiDigit(state.current()))
            {
                state.advance();
            }
        }
    }
    else
    {
        if (state.current() == '.')
            state.advance();
        while (!state.atEnd() &&
               isAsciiDigit(state.current()))
        {
            state.advance();
        }
        if (state.current() == '.' &&
            state.peek() != '.')
        {
            state.advance();
            while (!state.atEnd() &&
                   isAsciiDigit(
                       state.current()))
            {
                state.advance();
            }
        }
        if (state.current() == 'e' ||
            state.current() == 'E')
        {
            state.advance();
            if (state.current() == '+' ||
                state.current() == '-')
            {
                state.advance();
            }
            while (!state.atEnd() &&
                   isAsciiDigit(state.current()))
            {
                state.advance();
            }
        }
    }
    if (state.cancelled)
        return;
    appendToken(
        result,
        LuaTokenKind::Number,
        state,
        start);
}

QString longestSymbolAt(
    const LexerState& state)
{
    static const QList<QString> symbols = {
        QStringLiteral("..."),
        QStringLiteral("::"),
        QStringLiteral(".."),
        QStringLiteral("=="),
        QStringLiteral("~="),
        QStringLiteral("<="),
        QStringLiteral(">="),
        QStringLiteral("//"),
        QStringLiteral("<<"),
        QStringLiteral(">>"),
        QStringLiteral("+"),
        QStringLiteral("-"),
        QStringLiteral("*"),
        QStringLiteral("/"),
        QStringLiteral("%"),
        QStringLiteral("^"),
        QStringLiteral("#"),
        QStringLiteral("&"),
        QStringLiteral("~"),
        QStringLiteral("|"),
        QStringLiteral("<"),
        QStringLiteral(">"),
        QStringLiteral("="),
        QStringLiteral("("),
        QStringLiteral(")"),
        QStringLiteral("{"),
        QStringLiteral("}"),
        QStringLiteral("["),
        QStringLiteral("]"),
        QStringLiteral(";"),
        QStringLiteral(":"),
        QStringLiteral(","),
        QStringLiteral(".")
    };
    for (const QString& symbol : symbols)
    {
        if (beginsWith(state, symbol))
            return symbol;
    }
    return QString();
}

LuaLexResult lexDecodedSource(
    const QString& sourceText,
    const QString& sourcePath,
    qsizetype initialUtf8ByteOffset,
    const LuaLexer::CancelCallback&
        cancelCallback)
{
    LuaLexResult result;
    LexerState state(
        sourceText,
        sourcePath,
        initialUtf8ByteOffset,
        cancelCallback);

    while (!state.atEnd())
    {
        const QChar character = state.current();
        if (character.isSpace())
        {
            state.advance();
            continue;
        }

        if (character == '-' &&
            state.peek() == '-')
        {
            const LuaSourcePosition start =
                state.position();
            state.advanceCount(2);
            const int equalsCount =
                longBracketEqualsCount(
                    state.source,
                    state.offset);
            if (equalsCount >= 0)
            {
                lexLongBracket(
                    result,
                    state,
                    LuaTokenKind::Comment,
                    start,
                    equalsCount,
                    LuaLexWarningCode::
                        UnterminatedLongComment);
            }
            else
            {
                while (!state.atEnd() &&
                       !isLineBreak(
                           state.current()))
                {
                    state.advance();
                }
                appendToken(
                    result,
                    LuaTokenKind::Comment,
                    state,
                    start);
            }
            continue;
        }

        const int equalsCount =
            longBracketEqualsCount(
                state.source,
                state.offset);
        if (equalsCount >= 0)
        {
            const LuaSourcePosition start =
                state.position();
            lexLongBracket(
                result,
                state,
                LuaTokenKind::LongString,
                start,
                equalsCount,
                LuaLexWarningCode::
                    UnterminatedLongString);
            continue;
        }

        if (character == '\'' ||
            character == '"')
        {
            lexQuotedString(result, state);
            continue;
        }

        if (isAsciiLetter(character))
        {
            const LuaSourcePosition start =
                state.position();
            state.advance();
            while (!state.atEnd() &&
                   isAsciiLetterOrDigit(
                       state.current()))
            {
                state.advance();
            }
            const QString text =
                state.textFrom(start);
            appendToken(
                result,
                LuaLexer::isKeyword(text)
                    ? LuaTokenKind::Keyword
                    : LuaTokenKind::Identifier,
                state,
                start);
            continue;
        }

        if (isAsciiDigit(character) ||
            (character == '.' &&
             isAsciiDigit(state.peek())))
        {
            lexNumber(result, state);
            continue;
        }

        const QString symbol =
            longestSymbolAt(state);
        if (!symbol.isEmpty())
        {
            const LuaSourcePosition start =
                state.position();
            state.advanceCount(symbol.size());
            appendToken(
                result,
                LuaTokenKind::Symbol,
                state,
                start);
            continue;
        }

        const LuaSourcePosition start =
            state.position();
        state.advance();
        appendToken(
            result,
            LuaTokenKind::Symbol,
            state,
            start,
            QString(),
            false);
        appendWarning(
            result,
            LuaLexWarningCode::InvalidCharacter,
            state,
            start);
    }

    LuaToken endToken;
    endToken.kind = LuaTokenKind::EndOfFile;
    endToken.range.sourcePath = sourcePath;
    endToken.range.start = state.position();
    endToken.range.end = state.position();
    result.tokens.append(std::move(endToken));
    result.cancelled = state.cancelled;
    return result;
}
}

LuaLexResult LuaLexer::lex(
    const QString& sourceText,
    const QString& sourcePath,
    const CancelCallback& cancelCallback)
{
    if (!sourceText.startsWith(QChar(0xfeff)))
    {
        return lexDecodedSource(
            sourceText,
            sourcePath,
            0,
            cancelCallback);
    }
    return lexDecodedSource(
        sourceText.mid(1),
        sourcePath,
        3,
        cancelCallback);
}

LuaLexResult LuaLexer::lexUtf8(
    const QByteArray& sourceBytes,
    const QString& sourcePath,
    const CancelCallback& cancelCallback)
{
    const bool hasBom =
        sourceBytes.startsWith("\xEF\xBB\xBF");
    const qsizetype bomLength = hasBom ? 3 : 0;
    const QByteArray payload =
        sourceBytes.mid(bomLength);
    const QString sourceText =
        QString::fromUtf8(payload);
    if (sourceText.toUtf8() == payload)
    {
        return lexDecodedSource(
            sourceText,
            sourcePath,
            bomLength,
            cancelCallback);
    }

    LuaLexResult result;
    LuaLexWarning warning;
    warning.code = LuaLexWarningCode::InvalidUtf8;
    warning.diagnosticCode =
        luaLexWarningDiagnosticCode(warning.code);
    warning.message = warningMessage(warning.code);
    warning.range.sourcePath = sourcePath;
    warning.range.end.utf8ByteOffset =
        sourceBytes.size();
    result.warnings.append(std::move(warning));

    LuaToken endToken;
    endToken.kind = LuaTokenKind::EndOfFile;
    endToken.range.sourcePath = sourcePath;
    result.tokens.append(std::move(endToken));
    return result;
}

bool LuaLexer::isKeyword(const QString& text)
{
    static const QSet<QString> keywords = {
        QStringLiteral("and"),
        QStringLiteral("break"),
        QStringLiteral("do"),
        QStringLiteral("else"),
        QStringLiteral("elseif"),
        QStringLiteral("end"),
        QStringLiteral("false"),
        QStringLiteral("for"),
        QStringLiteral("function"),
        QStringLiteral("goto"),
        QStringLiteral("if"),
        QStringLiteral("in"),
        QStringLiteral("local"),
        QStringLiteral("nil"),
        QStringLiteral("not"),
        QStringLiteral("or"),
        QStringLiteral("repeat"),
        QStringLiteral("return"),
        QStringLiteral("then"),
        QStringLiteral("true"),
        QStringLiteral("until"),
        QStringLiteral("while")
    };
    return keywords.contains(text);
}

bool LuaLexer::isSignificant(
    const LuaToken& token)
{
    return token.kind != LuaTokenKind::Comment &&
        token.kind != LuaTokenKind::EndOfFile;
}

QString luaLexWarningDiagnosticCode(
    LuaLexWarningCode code)
{
    switch (code)
    {
    case LuaLexWarningCode::UnterminatedString:
        return QStringLiteral(
            "story_graph.lexer.unterminated_string");
    case LuaLexWarningCode::RawNewlineInString:
        return QStringLiteral(
            "story_graph.lexer.raw_newline_in_string");
    case LuaLexWarningCode::InvalidEscape:
        return QStringLiteral(
            "story_graph.lexer.invalid_escape");
    case LuaLexWarningCode::UnterminatedLongString:
        return QStringLiteral(
            "story_graph.lexer.unterminated_long_string");
    case LuaLexWarningCode::UnterminatedLongComment:
        return QStringLiteral(
            "story_graph.lexer.unterminated_long_comment");
    case LuaLexWarningCode::InvalidUtf8:
        return QStringLiteral(
            "story_graph.lexer.invalid_utf8");
    case LuaLexWarningCode::InvalidCharacter:
        return QStringLiteral(
            "story_graph.lexer.invalid_character");
    }
    return QStringLiteral(
        "story_graph.lexer.unknown");
}
